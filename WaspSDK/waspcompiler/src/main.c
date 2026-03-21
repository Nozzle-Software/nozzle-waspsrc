/*
 * WaspCompiler - QuakeC Compiler
 * main.c - Entry point, CLI argument handling, compilation pipeline
 *
 *  __        __                  ____                       _ _
 *  \ \      / /_ _ ___ _ __    / ___|___  _ __ ___  _ __ (_) | ___ _ __
 *   \ \ /\ / / _` / __| '_ \  | |   / _ \| '_ ` _ \| '_ \| | |/ _ \ '__|
 *    \ V  V / (_| \__ \ |_) | | |__| (_) | | | | | | |_) | | |  __/ |
 *     \_/\_/ \__,_|___/ .__/   \____\___/|_| |_| |_| .__/|_|_|\___|_|
 *                      |_|                           |_|
 *
 *  WaspCompiler v1.0.0 - QuakeC Compiler
 *  Produces standard progs.dat compatible with Quake 1 engine.
 */

#include "common.h"
#include "lexer.h"
#include "parser.h"
#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* -------------------------------------------------------------------------
 * Options
 * -------------------------------------------------------------------------*/
typedef struct {
    const char *src_file;       /* progs.src or single .qc file */
    const char *out_file;       /* output progs.dat */
    bool        verbose;
    bool        dump_ast;
    bool        werror;
    bool        stats;
} Options;

static void print_banner(void) {
    fprintf(stderr,
        "WaspCompiler v%s - QuakeC Compiler\n"
        "Produces standard Quake 1 progs.dat output.\n\n",
        WASP_VERSION_STR);
}

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options] <progs.src|file.qc>\n"
        "\n"
        "Options:\n"
        "  -o <file>   Output file (default: progs.dat)\n"
        "  -v          Verbose output\n"
        "  -Werror     Treat warnings as errors\n"
        "  -stats      Print compilation statistics\n"
        "  -help       Show this help\n"
        "  -version    Show version information\n"
        "\n"
        "Input formats:\n"
        "  progs.src   List of .qc files to compile (standard Quake format)\n"
        "  file.qc     Single QuakeC source file\n"
        "\n"
        "The progs.src format:\n"
        "  First line: output filename (e.g. progs.dat)\n"
        "  Remaining lines: source files to compile in order\n"
        "\n",
        prog);
}

/* -------------------------------------------------------------------------
 * File reading
 * -------------------------------------------------------------------------*/
static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = (char *)wasp_malloc(size + 1);
    size_t n  = fread(buf, 1, size, f);
    buf[n]    = '\0';
    fclose(f);
    return buf;
}

/* Get directory part of a path */
static char *path_dir(const char *path) {
    const char *last_sep = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\') last_sep = p + 1;
    if (last_sep == path) return wasp_strdup(".");
    size_t len = (size_t)(last_sep - path);
    char *dir = (char *)wasp_malloc(len + 1);
    memcpy(dir, path, len - 1);
    dir[len - 1] = '\0';
    return dir;
}

static char *path_join(const char *dir, const char *file) {
    size_t dlen = strlen(dir);
    size_t flen = strlen(file);
    char *result = (char *)wasp_malloc(dlen + flen + 2);
    memcpy(result, dir, dlen);
    result[dlen] = '/';
    memcpy(result + dlen + 1, file, flen + 1);
    return result;
}

/* -------------------------------------------------------------------------
 * Compile a single .qc file into the shared codegen context
 * -------------------------------------------------------------------------*/
static bool compile_file(CodeGen *cg, const char *filename, bool verbose) {
    if (verbose)
        fprintf(stderr, "  Compiling %s...\n", filename);

    char *src = read_file(filename);
    if (!src) {
        wasp_error(NULL, 0, "cannot open file '%s'", filename);
        return false;
    }

    /* Update source file name in codegen */
    cg->s_file = strtab_add(&cg->strtab, filename);

    Lexer  *lexer  = lexer_new(filename, src);
    Parser *parser = parser_new(lexer);
    AstNode *ast   = parser_parse(parser);

    bool ok = (g_error_count == 0);
    if (ok) {
        codegen_compile(cg, ast);
        ok = (g_error_count == 0);
    }

    parser_free(parser);
    lexer_free(lexer);
    free(src);

    return ok;
}

/* -------------------------------------------------------------------------
 * Parse progs.src and compile all listed files
 * -------------------------------------------------------------------------*/
static bool compile_progs_src(const char *src_path, const char *out_path,
                               bool verbose) {
    char *src_content = read_file(src_path);
    if (!src_content) {
        wasp_fatal("cannot open progs.src file '%s'", src_path);
        return false;
    }

    char *base_dir = path_dir(src_path);

    /* Parse the file list */
    char **files  = NULL;
    int    nfiles = 0, cap_files = 0;
    char   output[256] = "progs.dat";
    bool   first_line  = true;

    char *line = src_content;
    while (*line) {
        /* Skip whitespace */
        while (*line == ' ' || *line == '\t') line++;
        if (*line == '\n' || *line == '\r' || *line == '\0') {
            while (*line == '\n' || *line == '\r') line++;
            continue;
        }
        /* Skip comments */
        if (line[0] == '/' && line[1] == '/') {
            while (*line && *line != '\n') line++;
            continue;
        }

        /* Read to end of line */
        char linebuf[512];
        int li = 0;
        while (*line && *line != '\n' && *line != '\r' && li < 511)
            linebuf[li++] = *line++;
        while (*line == '\n' || *line == '\r') line++;
        linebuf[li] = '\0';

        /* Trim trailing whitespace */
        while (li > 0 && (linebuf[li-1] == ' ' || linebuf[li-1] == '\t'))
            linebuf[--li] = '\0';

        if (li == 0) continue;

        if (first_line) {
            /* First line is the output filename */
            strncpy(output, linebuf, sizeof(output) - 1);
            first_line = false;
        } else {
            /* Source file */
            char *full_path = path_join(base_dir, linebuf);
            DA_PUSH(files, nfiles, cap_files, full_path);
        }
    }

    free(src_content);

    /* Use override output path if specified */
    const char *final_out = out_path ? out_path : output;

    if (verbose) {
        fprintf(stderr, "Output: %s\n", final_out);
        fprintf(stderr, "Source files: %d\n\n", nfiles);
    }

    /* Compile all files into one codegen context */
    CodeGen *cg = codegen_new();
    bool ok = true;

    for (int i = 0; i < nfiles && ok; i++) {
        ok = compile_file(cg, files[i], verbose);
    }

    if (ok) {
        ok = codegen_write(cg, final_out);
        if (ok && verbose)
            fprintf(stderr, "\nWritten: %s\n", final_out);
    }

    /* Free file list */
    for (int i = 0; i < nfiles; i++) free(files[i]);
    free(files);
    free(base_dir);

    if (ok && g_error_count == 0) {
        return true;
    }

    codegen_free(cg);
    return ok && g_error_count == 0;
}

/* -------------------------------------------------------------------------
 * Print compilation statistics
 * -------------------------------------------------------------------------*/
static void print_stats(CodeGen *cg, double elapsed) {
    fprintf(stderr, "\n--- WaspCompiler Statistics ---\n");
    fprintf(stderr, "  Statements  : %d\n", cg->num_statements);
    fprintf(stderr, "  Functions   : %d\n", cg->num_functions);
    fprintf(stderr, "  Global defs : %d\n", cg->num_globaldefs);
    fprintf(stderr, "  Field defs  : %d\n", cg->num_fielddefs);
    fprintf(stderr, "  Globals     : %d floats (%zu bytes)\n",
            cg->num_globals, (size_t)cg->num_globals * sizeof(float));
    fprintf(stderr, "  String data : %d bytes\n", cg->strtab.size);
    fprintf(stderr, "  Entity flds : %d\n", cg->num_fields);
    fprintf(stderr, "  Errors      : %d\n", g_error_count);
    fprintf(stderr, "  Warnings    : %d\n", g_warning_count);
    fprintf(stderr, "  Time        : %.3f seconds\n", elapsed);
    fprintf(stderr, "-------------------------------\n");
}

/* -------------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------------*/
int main(int argc, char *argv[]) {
    Options opts;
    memset(&opts, 0, sizeof(opts));
    opts.out_file = NULL;

    if (argc < 2) {
        print_banner();
        print_usage(argv[0]);
        return 1;
    }

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-help") == 0 || strcmp(argv[i], "--help") == 0 ||
            strcmp(argv[i], "-h") == 0) {
            print_banner();
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-version") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("WaspCompiler v%s\n", WASP_VERSION_STR);
            printf("QuakeC Compiler - progs.dat output\n");
            printf("Compatible with Quake 1 engine.\n");
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "-verbose") == 0) {
            opts.verbose = true;
        } else if (strcmp(argv[i], "-Werror") == 0 || strcmp(argv[i], "-werror") == 0) {
            opts.werror = true;
        } else if (strcmp(argv[i], "-stats") == 0) {
            opts.stats = true;
        } else if (strcmp(argv[i], "-dump-ast") == 0) {
            opts.dump_ast = true;
        } else if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: -o requires an argument\n");
                return 1;
            }
            opts.out_file = argv[++i];
        } else if (argv[i][0] != '-') {
            opts.src_file = argv[i];
        } else {
            fprintf(stderr, "warning: unknown option '%s'\n", argv[i]);
        }
    }

    if (!opts.src_file) {
        fprintf(stderr, "error: no input file specified\n");
        print_usage(argv[0]);
        return 1;
    }

    g_opt_werror = opts.werror;

    if (opts.verbose) print_banner();

    clock_t t_start = clock();

    bool success = false;

    /* Determine if input is progs.src or a direct .qc file */
    size_t srclen = strlen(opts.src_file);
    bool is_progs_src = (srclen >= 9 &&
                         strcmp(opts.src_file + srclen - 9, "progs.src") == 0) ||
                        (srclen >= 4 &&
                         strcmp(opts.src_file + srclen - 4, ".src") == 0);

    if (is_progs_src) {
        success = compile_progs_src(opts.src_file, opts.out_file, opts.verbose);
    } else {
        /* Single file compilation */
        const char *out = opts.out_file ? opts.out_file : "progs.dat";

        if (opts.verbose) {
            fprintf(stderr, "Output: %s\n\n", out);
        }

        CodeGen *cg = codegen_new();
        success = compile_file(cg, opts.src_file, opts.verbose);

        if (success) {
            success = codegen_write(cg, out);
            if (success && opts.verbose)
                fprintf(stderr, "\nWritten: %s\n", out);
        }

        if (opts.stats) {
            clock_t t_end = clock();
            double elapsed = (double)(t_end - t_start) / CLOCKS_PER_SEC;
            print_stats(cg, elapsed);
        }

        codegen_free(cg);
    }

    if (!success || g_error_count > 0) {
        fprintf(stderr, "\nCompilation failed with %d error(s).\n", g_error_count);
        return 1;
    }

    if (g_warning_count > 0 && opts.verbose) {
        fprintf(stderr, "\nCompilation succeeded with %d warning(s).\n",
                g_warning_count);
    } else if (opts.verbose) {
        fprintf(stderr, "\nCompilation successful.\n");
    }

    str_intern_free();
    return 0;
}
