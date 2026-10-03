/*
 * functions.c
 * Implementations of all functions exposed on the web by cflask.
 * All functions are thread-safe: they only use their arguments and stack.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "fn.h"

/* ---------- helpers: query-string parsing ---------- */

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Decode %XX escapes and '+' (space) from src[0..srclen) into dst. */
static void url_decode(char *dst, size_t dstlen, const char *src, size_t srclen) {
    size_t j = 0;
    for (size_t i = 0; i < srclen && j + 1 < dstlen; i++) {
        if (src[i] == '+') {
            dst[j++] = ' ';
        } else if (src[i] == '%' && i + 2 < srclen &&
                   hexval(src[i + 1]) >= 0 && hexval(src[i + 2]) >= 0) {
            dst[j++] = (char)(hexval(src[i + 1]) * 16 + hexval(src[i + 2]));
            i += 2;
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
}

/*
 * Find `key` in a query string like "a=1&b=hello" and copy its decoded value
 * into out. Returns 1 if found (with a non-empty value), 0 otherwise.
 */
static int get_param(const char *query, const char *key, char *out, size_t outlen) {
    if (!query || !*query) return 0;
    size_t keylen = strlen(key);
    const char *p = query;

    while (*p) {
        const char *end = strchr(p, '&');
        if (!end) end = p + strlen(p);
        const char *eq = memchr(p, '=', (size_t)(end - p));

        if (eq && (size_t)(eq - p) == keylen && strncmp(p, key, keylen) == 0) {
            url_decode(out, outlen, eq + 1, (size_t)(end - (eq + 1)));
            return out[0] != '\0';
        }
        p = (*end == '&') ? end + 1 : end;
    }
    return 0;
}

/* Parse integer param. Returns 1 = parsed, 0 = absent, -1 = invalid. */
static int get_int_param(const char *query, const char *key, long long *val) {
    char buf[64];
    if (!get_param(query, key, buf, sizeof(buf))) return 0;
    char *endp;
    errno = 0;
    long long v = strtoll(buf, &endp, 10);
    if (errno != 0 || *endp != '\0') return -1;
    *val = v;
    return 1;
}

/* ---------- exposed functions ---------- */

/* /  -> hello world, no arguments */
void fn_root(const char *query, char *out, size_t outlen) {
    (void)query;
    snprintf(out, outlen, "Hello World");
}

/* /square?num=3 -> 9 ; no argument -> 1 */
void fn_square(const char *query, char *out, size_t outlen) {
    long long n;
    int r = get_int_param(query, "num", &n);
    if (r == 0)      snprintf(out, outlen, "1");
    else if (r < 0)  snprintf(out, outlen, "Error: num must be an integer");
    else             snprintf(out, outlen, "%lld", n * n);
}

/* /cube?num=7 -> 343 ; no argument -> 1 */
void fn_cube(const char *query, char *out, size_t outlen) {
    long long n;
    int r = get_int_param(query, "num", &n);
    if (r == 0)      snprintf(out, outlen, "1");
    else if (r < 0)  snprintf(out, outlen, "Error: num must be an integer");
    else             snprintf(out, outlen, "%lld", n * n * n);
}

/* /helloworld?str=Ashwin -> "Hello, Ashwin" ; no argument -> "Hello" */
void fn_helloworld(const char *query, char *out, size_t outlen) {
    char str[512];
    if (get_param(query, "str", str, sizeof(str)))
        snprintf(out, outlen, "Hello, %s", str);
    else
        snprintf(out, outlen, "Hello");
}

/* /pingpong?str=cs744 -> "cs744" ; no argument -> "PingPong" */
void fn_pingpong(const char *query, char *out, size_t outlen) {
    char str[512];
    if (get_param(query, "str", str, sizeof(str)))
        snprintf(out, outlen, "%s", str);
    else
        snprintf(out, outlen, "PingPong");
}

/* /arithmetic/prime?num=13 -> "True" ; no argument -> "False"
 * Trial division: O(sqrt(n)) work, useful as a CPU-heavy function for load tests. */
void fn_prime(const char *query, char *out, size_t outlen) {
    long long n;
    int r = get_int_param(query, "num", &n);
    if (r <= 0 || n < 2) { snprintf(out, outlen, "False"); return; }
    if (n < 4)           { snprintf(out, outlen, "True");  return; }
    if (n % 2 == 0)      { snprintf(out, outlen, "False"); return; }

    for (long long i = 3; i <= n / i; i += 2) {
        if (n % i == 0) { snprintf(out, outlen, "False"); return; }
    }
    snprintf(out, outlen, "True");
}

/* /arithmetic/fibonacci?num=10 -> 55 (F1 = F2 = 1) ; no argument -> 1
 * F(93) is the largest that fits in 64 bits. */
void fn_fibonacci(const char *query, char *out, size_t outlen) {
    long long k;
    int r = get_int_param(query, "num", &k);
    if (r == 0)       { snprintf(out, outlen, "1"); return; }
    if (r < 0 || k < 0) { snprintf(out, outlen, "Error: num must be a non-negative integer"); return; }
    if (k > 93)       { snprintf(out, outlen, "Error: num must be <= 93 (64-bit overflow)"); return; }

    unsigned long long a = 0, b = 1;   /* F0, F1 */
    for (long long i = 0; i < k; i++) {
        unsigned long long t = a + b;
        a = b;
        b = t;
    }
    snprintf(out, outlen, "%llu", a);
}