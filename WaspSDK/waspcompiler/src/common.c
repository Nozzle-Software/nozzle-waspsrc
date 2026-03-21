/*
 * WaspCompiler - QuakeC Compiler
 * common.c - Error handling and string interning
 */
#include "common.h"

int  g_error_count   = 0;
int  g_warning_count = 0;
bool g_opt_werror    = false;

void wasp_error(const char *file, int line, const char *fmt, ...) {
    va_list ap;
    if (file && line > 0)
        fprintf(stderr, "%s:%d: error: ", file, line);
    else
        fprintf(stderr, "error: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    g_error_count++;
    if (g_error_count >= 100) {
        fprintf(stderr, "WaspCompiler: too many errors, aborting.\n");
        exit(1);
    }
}

void wasp_warning(const char *file, int line, const char *fmt, ...) {
    va_list ap;
    if (g_opt_werror) {
        if (file && line > 0)
            fprintf(stderr, "%s:%d: error (warning treated as error): ", file, line);
        else
            fprintf(stderr, "error (warning treated as error): ");
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
        fprintf(stderr, "\n");
        g_error_count++;
        return;
    }
    if (file && line > 0)
        fprintf(stderr, "%s:%d: warning: ", file, line);
    else
        fprintf(stderr, "warning: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    g_warning_count++;
}

void wasp_fatal(const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "WaspCompiler: fatal: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}

void wasp_ice(const char *file_c, int line_c, const char *msg) {
    fprintf(stderr, "WaspCompiler: INTERNAL COMPILER ERROR at %s:%d: %s\n",
            file_c, line_c, msg);
    fprintf(stderr, "Please report this bug.\n");
    abort();
}

/* -------------------------------------------------------------------------
 * String interning - simple hash table
 * -------------------------------------------------------------------------*/
#define INTERN_BUCKETS 4096

typedef struct InternEntry {
    char              *str;
    struct InternEntry *next;
} InternEntry;

static InternEntry *intern_table[INTERN_BUCKETS];

static unsigned int hash_str(const char *s) {
    unsigned int h = 5381;
    while (*s) h = ((h << 5) + h) + (unsigned char)*s++;
    return h;
}

const char *str_intern(const char *s) {
    if (!s) return NULL;
    unsigned int h = hash_str(s) % INTERN_BUCKETS;
    for (InternEntry *e = intern_table[h]; e; e = e->next)
        if (strcmp(e->str, s) == 0) return e->str;
    InternEntry *e = (InternEntry *)wasp_malloc(sizeof(InternEntry));
    e->str  = wasp_strdup(s);
    e->next = intern_table[h];
    intern_table[h] = e;
    return e->str;
}

void str_intern_free(void) {
    for (int i = 0; i < INTERN_BUCKETS; i++) {
        InternEntry *e = intern_table[i];
        while (e) {
            InternEntry *next = e->next;
            free(e->str);
            free(e);
            e = next;
        }
        intern_table[i] = NULL;
    }
}
