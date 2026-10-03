/*
 * cflask.c - a Flask-like web server in C
 *
 * Usage:
 *   ./cflask s <port>                  single-threaded         (Part 1a)
 *   ./cflask m <port>                  thread per request      (Part 1b)
 *   ./cflask t <port> <num_threads>    thread pool             (Part 1c)
 *
 * All three modes share the same request path (handle_client):
 *   receive -> parse -> dispatch -> respond -> close
 * Only the way connections are assigned to threads differs.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <netinet/in.h>

#include "http-parser.h"
#include "fn/fn.h"
#include "fn/fnlist.h"

#define REQ_BUF_SIZE     16384
#define RESP_BUF_SIZE    4096
#define LISTEN_BACKLOG   4096
#define RECV_TIMEOUT_S   5
#define QUEUE_SIZE       4096
#define MAX_THREADS      1024
#define WORKER_STACK     (256 * 1024)   /* handle_client needs ~32 KB of stack */

/* ================================================================
 *  Server socket
 * ================================================================ */

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

static int accept_client(int server_fd) {
    for (;;) {
        int fd = accept(server_fd, NULL, NULL);
        if (fd >= 0) return fd;
        if (errno != EINTR) perror("accept");
    }
}

/* ================================================================
 *  Socket I/O helpers
 * ================================================================ */

/*
 * Read one full HTTP request: keep reading until the end of the headers
 * (\r\n\r\n) and, if Content-Length is present, until the whole body has
 * arrived. Returns bytes read, or -1 on error / connection closed.
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
        if (!hdr_end) continue;                        /* headers incomplete */

        size_t content_len = 0;
        char *cl = strcasestr(buf, "Content-Length:");
        if (cl && cl < hdr_end) content_len = strtoul(cl + 15, NULL, 10);

        size_t body_have = len - (size_t)(hdr_end + 4 - buf);
        if (body_have >= content_len) break;           /* request complete */
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

/*
 * Request log: build with `make LOG=1` to print one line per request with the
 * ID of the thread that served it. Off by default so load tests are unaffected.
 */
#ifdef LOG_REQUESTS
#define LOG_REQ(method, path, status) \
    fprintf(stderr, "[thread %ld] %s %s -> %s\n", \
            (long)syscall(SYS_gettid), (method), (path), (status))
#else
#define LOG_REQ(method, path, status) ((void)0)
#endif

static void send_response(int fd, const char *status, const char *body) {
    size_t resp_len = 0;
    char *resp = create_http_response(status, "text/plain", body, strlen(body), &resp_len);
    if (resp) {
        send_all(fd, resp, resp_len);
        free(resp);
    }
}

/* ================================================================
 *  Dispatch: URL -> function ID
 * ================================================================ */

static int lookup_route(const char *path) {
    for (size_t i = 0; i < NUM_ROUTES; i++)
        if (strcmp(routes[i].url, path) == 0)
            return routes[i].id;
    return -1;
}

/* ================================================================
 *  Serve one connection (shared by all modes)
 * ================================================================ */

static void handle_client(int client_fd) {
    char buf[REQ_BUF_SIZE];

    struct timeval tv = { .tv_sec = RECV_TIMEOUT_S, .tv_usec = 0 };
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (recv_request(client_fd, buf, sizeof(buf)) <= 0) {
        close(client_fd);
        return;
    }

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

    /* POST: arguments in the body (application/x-www-form-urlencoded) */
    if (strcmp(method, "POST") == 0 && req.body && req.body[0] != '\0')
        query = req.body;

    int id = lookup_route(path);
    if (id < 0) {
        LOG_REQ(method, path, "404 Not Found");
        send_response(client_fd, "404 Not Found", "Not Found");
        close(client_fd);
        return;
    }

    char out[RESP_BUF_SIZE];
    out[0] = '\0';
    function_list[id](query, out, sizeof(out));
    LOG_REQ(method, path, "200 OK");
    send_response(client_fd, "200 OK", out);
    close(client_fd);
}

/* ================================================================
 *  Mode s: single-threaded (Part 1a)
 * ================================================================ */

static void run_single(int server_fd) {
    for (;;) {
        int client_fd = accept_client(server_fd);
        handle_client(client_fd);
    }
}

/* ================================================================
 *  Mode m: a new thread for every request, no limit (Part 1b)
 * ================================================================ */

static void *per_request_worker(void *arg) {
    int client_fd = (int)(intptr_t)arg;   /* fd passed by value, not by address */
    handle_client(client_fd);
    return NULL;                          /* thread exits after one request */
}

static void run_thread_per_request(int server_fd) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);  /* no join needed */
    pthread_attr_setstacksize(&attr, WORKER_STACK);

    for (;;) {
        int client_fd = accept_client(server_fd);

        pthread_t tid;
        if (pthread_create(&tid, &attr, per_request_worker,
                           (void *)(intptr_t)client_fd) != 0) {
            /* out of threads or memory: drop this connection, keep serving */
            perror("pthread_create");
            close(client_fd);
        }
    }
}

/* ================================================================
 *  Mode t: fixed thread pool with a bounded queue (Part 1c)
 * ================================================================ */

typedef struct {
    int fds[QUEUE_SIZE];
    int head;                       /* next fd to pop   */
    int tail;                       /* next slot to push */
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

/* Producer (accept thread): blocks while the queue is full. */
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

/* Consumer (workers): blocks while the queue is empty. */
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

static void *pool_worker(void *arg) {
    (void)arg;
    for (;;)
        handle_client(queue_pop());
    return NULL;
}

static void run_thread_pool(int server_fd, int num_threads) {
    for (int i = 0; i < num_threads; i++) {
        pthread_t tid;
        if (pthread_create(&tid, NULL, pool_worker, NULL) != 0) {
            perror("pthread_create");
            exit(1);
        }
        pthread_detach(tid);
    }
    for (;;)
        queue_push(accept_client(server_fd));
}

/* ================================================================
 *  main
 * ================================================================ */

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage:\n"
        "  %s s <port>                  single-threaded\n"
        "  %s m <port>                  thread per request\n"
        "  %s t <port> <num_threads>    thread pool\n",
        prog, prog, prog);
    exit(1);
}

int main(int argc, char *argv[]) {
    if (argc < 3 || strlen(argv[1]) != 1) usage(argv[0]);

    char mode = argv[1][0];
    int port = atoi(argv[2]);
    int threads = 0;

    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port: %s\n", argv[2]);
        usage(argv[0]);
    }

    if (mode == 's' || mode == 'm') {
        if (argc != 3) usage(argv[0]);
    } else if (mode == 't') {
        if (argc != 4) usage(argv[0]);
        threads = atoi(argv[3]);
        if (threads <= 0 || threads > MAX_THREADS) {
            fprintf(stderr, "Thread count must be between 1 and %d\n", MAX_THREADS);
            usage(argv[0]);
        }
    } else {
        usage(argv[0]);
    }

    signal(SIGPIPE, SIG_IGN);   /* don't die if a client disconnects mid-send */
    int server_fd = create_server_socket(port);

    switch (mode) {
    case 's':
        printf("cflask [single-threaded] on http://0.0.0.0:%d\n", port);
        fflush(stdout);
        run_single(server_fd);
        break;
    case 'm':
        printf("cflask [thread per request] on http://0.0.0.0:%d\n", port);
        fflush(stdout);
        run_thread_per_request(server_fd);
        break;
    case 't':
        printf("cflask [thread pool, %d threads] on http://0.0.0.0:%d\n", threads, port);
        fflush(stdout);
        run_thread_pool(server_fd, threads);
        break;
    }

    close(server_fd);
    return 0;
}