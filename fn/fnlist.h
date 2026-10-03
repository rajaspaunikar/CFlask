#ifndef FNLIST_H
#define FNLIST_H

#include "fn.h"

/*
 * functionslist.h
 * Function pointer array: each function from functions.c is placed at the
 * index given by its ID in functions.h (designated initializers make the
 * index explicit, so the order here does not matter).
 */
static const cflask_fn function_list[NUM_FUNCTIONS] = {
    [FN_ROOT]       = fn_root,
    [FN_SQUARE]     = fn_square,
    [FN_CUBE]       = fn_cube,
    [FN_HELLOWORLD] = fn_helloworld,
    [FN_PINGPONG]   = fn_pingpong,
    [FN_PRIME]      = fn_prime,
    [FN_FIBONACCI]  = fn_fibonacci,
};

#endif /* FNLIST_H */