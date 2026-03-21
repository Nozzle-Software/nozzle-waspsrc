/*
 * WaspCompiler - QuakeC Compiler
 * common.h - Shared utilities, memory, error handling
 */
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <ctype.h>

#define WASP_VERSION_MAJOR 1
#define WASP_VERSION_MINOR 0
#define WASP_VERSION_PATCH 0
#define WASP_VERSION_STR   "1.0.0"

/* -------------------------------------------------------------------------
 * Dynamic array (stretchy buffer)
 * Usage: DA_PUSH(arr, count, cap, item)
 * -------------------------------------------------------------------------*/
#define DA_INIT_CAP 16

#define DA_PUSH(arr, count, cap, item)                          \
    do {                                                         \
        if ((count) >= (cap)) {                                  \
            (cap) = ((cap) == 0) ? DA_INIT_CAP : (cap) * 2;    \
            (arr) = wasp_realloc((arr), (cap) * sizeof(*(arr)));\
        }                                                        \
        (arr)[(count)++] = (item);                               \
    } while (0)

/* -------------------------------------------------------------------------
 * Memory helpers
 * -------------------------------------------------------------------------*/
static inline void *wasp_malloc(size_t n) {
    void *p = malloc(n);
    if (!p) { fprintf(stderr, "WaspCompiler: out of memory\n"); exit(1); }
    memset(p, 0, n);
    return p;
}

static inline void *wasp_realloc(void *p, size_t n) {
    void *r = realloc(p, n);
    if (!r && n) { fprintf(stderr, "WaspCompiler: out of memory\n"); exit(1); }
    return r;
}

static inline char *wasp_strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *d = (char *)wasp_malloc(len);
    memcpy(d, s, len);
    return d;
}

/* -------------------------------------------------------------------------
 * Error / warning reporting
 * -------------------------------------------------------------------------*/
extern int g_error_count;
extern int g_warning_count;
extern bool g_opt_werror;

void wasp_error(const char *file, int line, const char *fmt, ...);
void wasp_warning(const char *file, int line, const char *fmt, ...);
void wasp_fatal(const char *fmt, ...);
void wasp_ice(const char *file_c, int line_c, const char *msg); /* internal compiler error */

#define ICE(msg) wasp_ice(__FILE__, __LINE__, msg)
#define UNREACHABLE() ICE("unreachable code reached")

/* -------------------------------------------------------------------------
 * String interning
 * -------------------------------------------------------------------------*/
const char *str_intern(const char *s);
void str_intern_free(void);
