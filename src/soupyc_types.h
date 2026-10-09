#pragma once
#include <stdint.h>

/* soupyc's data types, lifted out of soupyc.c so the per-invocation context
 * struct can be declared before any function that touches it. Nothing here
 * holds state: the state lives in soupyc_ctx_t, one per running script. */

/* Identifier and token length. NOT a limit on string values: those are
 * heap-backed (see val_t). */
#define SVAL_LEN   48
#define POOL_SIZE  512
#define MAX_ARRAYS 24
#define ARR_CAP    48          /* elements per array */
#define MAX_VARS   64
#define MAX_FUNCS  16
#define MAX_PARAMS 8
#define MAX_CALL_DEPTH    16
#define MAX_INCLUDE_DEPTH 4
#define MAX_SC_FILES      8

typedef struct node {
    int   type, op, ival;
    int   line;          /* source line where this node began (for errors) */
    char  sval[SVAL_LEN];       /* identifier / variable / function name */
    const char *lit;            /* N_STR: interned literal, any length */
    uint32_t    litlen;
    struct node *next;   /* next stmt in a block / next arg in a call */
    struct node *left;   /* binop LHS / if-cond / while-cond / beep freq */
    struct node *right;  /* binop RHS / beep ms */
    struct node *body;   /* if then-block / while body / pour/return/unary expr */
    struct node *else_;  /* if else-block */
} node_t;

typedef struct { int type; int ival; const char *sval; uint32_t slen; } val_t;

typedef struct str_blk { struct str_blk *next; char data[]; } str_blk_t;

typedef struct {
    int   used;
    int   len;
    val_t elems[ARR_CAP];
} arr_t;

typedef struct {
    char    name[32];
    char    params[MAX_PARAMS][32];
    int     nparam;
    node_t *body;
} func_t;

typedef struct {
    int type, ival, line;
    char sval[SVAL_LEN];        /* identifiers and keywords */
    const char *str;            /* TK_STR: interned literal, any length */
    uint32_t    slen;
} tok_t;
