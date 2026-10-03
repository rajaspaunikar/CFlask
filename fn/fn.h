#ifndef FN_H
#define FN_H

#include <stddef.h>

/*
 * fn.h
 * Lists every URL exposed by cflask and associates it with an integer ID.
 * The ID is the index into function_list[] (see functionslist.h).
 *
 *   URL                     ID
 *   /                       0
 *   /square                 1
 *   /cube                   2
 *   /helloworld             3
 *   /pingpong               4
 *   /arithmetic/prime       5
 *   /arithmetic/fibonacci   6
 */

#define FN_ROOT        0
#define FN_SQUARE      1
#define FN_CUBE        2
#define FN_HELLOWORLD  3
#define FN_PINGPONG    4
#define FN_PRIME       5
#define FN_FIBONACCI   6

#define NUM_FUNCTIONS  7

/*
 * Every exposed function has the same signature:
 *   query  : the raw argument string, e.g. "num=3&x=y" (NULL if none)
 *   out    : buffer the function writes its response text into
 *   outlen : size of out
 */
typedef void (*cflask_fn)(const char *query, char *out, size_t outlen);

/* URL -> function ID table */
typedef struct {
    const char *url;
    int id;
} Route;

static const Route routes[] = {
    { "/",                     FN_ROOT       },
    { "/square",               FN_SQUARE     },
    { "/cube",                 FN_CUBE       },
    { "/helloworld",           FN_HELLOWORLD },
    { "/pingpong",             FN_PINGPONG   },
    { "/arithmetic/prime",     FN_PRIME      },
    { "/arithmetic/fibonacci", FN_FIBONACCI  },
};

#define NUM_ROUTES (sizeof(routes) / sizeof(routes[0]))

/* Implementations live in functions.c */
void fn_root(const char *query, char *out, size_t outlen);
void fn_square(const char *query, char *out, size_t outlen);
void fn_cube(const char *query, char *out, size_t outlen);
void fn_helloworld(const char *query, char *out, size_t outlen);
void fn_pingpong(const char *query, char *out, size_t outlen);
void fn_prime(const char *query, char *out, size_t outlen);
void fn_fibonacci(const char *query, char *out, size_t outlen);

#endif /* FN_H */