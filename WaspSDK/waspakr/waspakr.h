/*
 * WasPakr - Quake PAK Archiver
 * waspakr.h - PAK format definitions and internal API
 *
 * Quake PAK format:
 *   [header]          12 bytes
 *   [file data]       variable
 *   [directory]       num_entries * 64 bytes
 */
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>

/* -------------------------------------------------------------------------
 * Version
 * -------------------------------------------------------------------------*/
#define WASPAKR_VERSION "1.0.0"

/* -------------------------------------------------------------------------
 * PAK format constants
 * -------------------------------------------------------------------------*/
#define PAK_MAGIC       "PACK"
#define PAK_MAGIC_LEN   4
#define PAK_ENTRY_SIZE  64
#define PAK_NAME_LEN    56
#define PAK_HEADER_SIZE 12
#define PAK_MAX_ENTRIES 4096   /* practical limit; Quake uses ~2048 */

/* -------------------------------------------------------------------------
 * On-disk structures (little-endian)
 * -------------------------------------------------------------------------*/
#pragma pack(push, 1)

typedef struct {
    char     magic[4];      /* "PACK" */
    uint32_t dir_offset;    /* byte offset of directory */
    uint32_t dir_size;      /* byte size of directory (num_entries * 64) */
} pak_header_t;

typedef struct {
    char     name[PAK_NAME_LEN]; /* null-terminated relative path */
    uint32_t offset;             /* byte offset of file data */
    uint32_t size;               /* byte size of file data */
} pak_entry_t;

#pragma pack(pop)

/* -------------------------------------------------------------------------
 * In-memory entry (during build/read)
 * -------------------------------------------------------------------------*/
typedef struct {
    char     name[PAK_NAME_LEN]; /* PAK-relative path (forward slashes) */
    char    *src_path;           /* absolute/relative path on disk (heap) */
    uint32_t offset;             /* position in PAK file */
    uint32_t size;               /* file size */
    uint8_t *data;               /* file data (heap, optional) */
} PakEntry;

/* -------------------------------------------------------------------------
 * PAK archive context
 * -------------------------------------------------------------------------*/
typedef struct {
    PakEntry *entries;
    int       count;
    int       cap;
} PakArchive;

/* -------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------*/
static inline void die(const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "waspakr: error: ");
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}

static inline void warn(const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "waspakr: warning: ");
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fprintf(stderr, "\n");
}

/* Portable mkdir -p */
static inline void mkdirs(const char *path) {
    char buf[1024];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf)-1] = '\0';
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char saved = *p; *p = '\0';
#ifdef _WIN32
            _mkdir(buf);
#else
            mkdir(buf, 0755);
#endif
            *p = saved;
        }
    }
}

/* Convert backslashes to forward slashes */
static inline void normalize_slashes(char *s) {
    for (; *s; s++) if (*s == '\\') *s = '/';
}

/* Lowercase a string in-place (PAK names are case-insensitive in Quake) */
static inline void to_lower(char *s) {
    for (; *s; s++) if (*s >= 'A' && *s <= 'Z') *s += 32;
}

/* File size helper */
static inline long file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (long)st.st_size;
}

static inline bool is_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

/* Read entire file into heap buffer */
static inline uint8_t *read_file(const char *path, long *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc(sz);
    if (!buf) { fclose(f); return NULL; }
    if ((long)fread(buf, 1, sz, f) != sz) { free(buf); fclose(f); return NULL; }
    fclose(f);
    if (out_size) *out_size = sz;
    return buf;
}

/* Human-readable file size */
static inline void fmt_size(long bytes, char *out, size_t outlen) {
    if (bytes >= 1024*1024)
        snprintf(out, outlen, "%.1f MB", bytes / (1024.0*1024.0));
    else if (bytes >= 1024)
        snprintf(out, outlen, "%.1f KB", bytes / 1024.0);
    else
        snprintf(out, outlen, "%ld B", bytes);
}
