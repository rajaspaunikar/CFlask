/*
 * cflask.c  (Step 4: thread-pool version)
 *
 * 1. web server initialization
 * 2. accept http requests
 * 3. parse http requests
 * 4. dispatch http requests
 * 5. process, generate and send http responses
 *
 * Design: the main thread only accept()s connections and pushes the client
 * fd into a bounded queue. N worker threads pop fds and run handle_client().
 *
 * Usage: ./cflask <port> <num_threads>
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <pthread.h>

#include "http-parser.h"
#include "../fn/fn.h"
#include "../fn/fnlist.h"

#define REQ_BUF_SIZE    16384
#define RESP_BUF_SIZE   4096
#define LISTEN_BACKLOG  4096
#define RECV_TIMEOUT_S  5
#define QUEUE_SIZE      4096
#define MAX_THREADS     1024

/* ---------- 1. server initialization ---------- */

static int create_server_socket(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); exit(1); }
    if (listen(fd, LISTEN_BACKLOG) < 0) { perror("listen"); exit(1); }
    return fd;
}

/* ---------- socket I/O helpers ---------- */

/*
 * Read one full HTTP request into buf: keep reading until the header
 * terminator (\r\n\r\n) arrives and, if Content-Length is present, until the
 * whole body has arrived. Returns bytes read, or -1 on error/closed.
 */
static ssize_t recv_request(int fd, char *buf, size_t cap) {
    size_t len = 0;
    while (len < cap - 1) {
        ssize_t n = recv(fd, buf + len, cap - 1 - len, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return len > 0 ? (ssize_t)len : -1;
        len += (size_t)n;
        buf[len] = '\0';

        char *hdr_end = strstr(buf, "\r\n\r\n");
        if (!hdr_end) continue;                       /* headers incomplete */

        size_t content_len = 0;
        char *cl = strcasestr(buf, "Content-Length:");
        if (cl && cl < hdr_end) content_len = strtoul(cl + 15, NULL, 10);

        size_t body_have = len - (size_t)(hdr_end + 4 - buf);
        if (body_have >= content_len) break;          /* request complete */
    }
    buf[len] = '\0';
    return (ssize_t)len;
}

static int send_all(int fd, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

static void send_response(int fd, const char *status, const char *body) {
    size_t resp_len = 0;
    char *resp = create_http_response(status, "text/plain", body, strlen(body), &resp_len);
    if (resp) {
        send_all(fd, resp, resp_len);
        free(resp);
    }
}

/* ---------- 4. dispatch: URL -> function ID ---------- */

static int lookup_route(const char *path) {
    for (size_t i = 0; i < NUM_ROUTES; i++)
        if (strcmp(routes[i].url, path) == 0)
            return routes[i].id;
    return -1;
}

/* ---------- 2-5. handle one connection ---------- */

static void handle_client(int client_fd) {
    char buf[REQ_BUF_SIZE];

    struct timeval tv = { .tv_sec = RECV_TIMEOUT_S, .tv_usec = 0 };
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (recv_request(client_fd, buf, sizeof(buf)) <= 0) {
        close(client_fd);
        return;
    }

    /* 3. parse */
    ParsedRequest req = {0};
    parse_http_request(buf, &req);

    const char *method = req.request_line.method;
    if (method[0] == '\0' || req.request_line.uri[0] == '\0') {
        send_response(client_fd, "400 Bad Request", "Bad Request");
        close(client_fd);
        return;
    }
    if (strcmp(method, "GET") != 0 && strcmp(method, "POST") != 0) {
        send_response(client_fd, "405 Method Not Allowed", "Method Not Allowed");
        close(client_fd);
        return;
    }

    /* split "/square?num=3" into path "/square" and query "num=3" */
    char *path = req.request_line.uri;
    char *query = NULL;
    char *qmark = strchr(path, '?');
    if (qmark) {
        *qmark = '\0';
        query = qmark + 1;
    }

    /* POST: arguments come in the body (application/x-www-form-urlencoded) */
    if (strcmp(method, "POST") == 0 && req.body && req.body[0] != '\0')
        query = req.body;

    /* 4. dispatch */
    int id = lookup_route(path);
    if (id < 0) {
        send_response(client_fd, "404 Not Found", "Not Found");
        close(client_fd);
        return;
    }

    /* 5. process and respond */
    char out[RESP_BUF_SIZE];
    out[0] = '\0';
    function_list[id](query, out, sizeof(out));
    send_response(client_fd, "200 OK", out);
    close(client_fd);
}


/* ---------- thread pool: bounded queue of client fds ---------- */

typedef struct {
    int fds[QUEUE_SIZE];
    int head;               /* next fd to pop  */
    int tail;               /* next slot to push */
    int count;
    pthread_mutex_t lock;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
} ConnQueue;

static ConnQueue queue = {
    .head = 0, .tail = 0, .count = 0,
    .lock      = PTHREAD_MUTEX_INITIALIZER,
    .not_empty = PTHREAD_COND_INITIALIZER,
    .not_full  = PTHREAD_COND_INITIALIZER,
};

/* Producer: called by the accept thread. Blocks if the queue is full. */
static void queue_push(int fd) {
    pthread_mutex_lock(&queue.lock);
    while (queue.count == QUEUE_SIZE)
        pthread_cond_wait(&queue.not_full, &queue.lock);
    queue.fds[queue.tail] = fd;
    queue.tail = (queue.tail + 1) % QUEUE_SIZE;
    queue.count++;
    pthread_cond_signal(&queue.not_empty);
    pthread_mutex_unlock(&queue.lock);
}

/* Consumer: called by workers. Blocks while the queue is empty. */
static int queue_pop(void) {
    pthread_mutex_lock(&queue.lock);
    while (queue.count == 0)
        pthread_cond_wait(&queue.not_empty, &queue.lock);
    int fd = queue.fds[queue.head];
    queue.head = (queue.head + 1) % QUEUE_SIZE;
    queue.count--;
    pthread_cond_signal(&queue.not_full);
    pthread_mutex_unlock(&queue.lock);
    return fd;
}

static void *worker_main(void *arg) {
    (void)arg;
    for (;;) {
        int client_fd = queue_pop();
        handle_client(client_fd);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <port> <num_threads>\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);
    int num_threads = atoi(argv[2]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port: %s\n", argv[1]);
        return 1;
    }
    if (num_threads <= 0 || num_threads > MAX_THREADS) {
        fprintf(stderr, "num_threads must be between 1 and %d\n", MAX_THREADS);
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);   /* don't die if a client disconnects mid-send */

    int server_fd = create_server_socket(port);

    /* start the worker pool */
    pthread_t tids[MAX_THREADS];
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&tids[i], NULL, worker_main, NULL) != 0) {
            perror("pthread_create");
            return 1;
        }
        pthread_detach(tids[i]);
    }

    printf("cflask listening on http://0.0.0.0:%d with %d worker threads\n",
           port, num_threads);
    fflush(stdout);

    /* accept loop: hand every connection to the pool */
    for (;;) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }
        queue_push(client_fd);
    }

    close(server_fd);
    return 0;
}