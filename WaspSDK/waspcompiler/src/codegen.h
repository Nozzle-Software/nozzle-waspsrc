/*
 * WaspCompiler - QuakeC Compiler
 * codegen.h - Bytecode code generator
 */
#pragma once
#include "ast.h"
#include "symtab.h"
#include "progs.h"

/* -------------------------------------------------------------------------
 * String table
 * -------------------------------------------------------------------------*/
typedef struct {
    char *data;
    int   size;
    int   cap;
} StringTable;

int  strtab_add(StringTable *st, const char *s);
const char *strtab_get(StringTable *st, int ofs);
void strtab_free(StringTable *st);

/* -------------------------------------------------------------------------
 * Code generator context
 * -------------------------------------------------------------------------*/
typedef struct {
    /* Symbol table */
    SymTable    *symtab;

    /* String table */
    StringTable  strtab;

    /* Statements */
    dstatement_t *statements;
    int           num_statements;
    int           cap_statements;

    /* Global definitions */
    ddef_t       *globaldefs;
    int           num_globaldefs;
    int           cap_globaldefs;

    /* Field definitions */
    ddef_t       *fielddefs;
    int           num_fielddefs;
    int           cap_fielddefs;

    /* Functions */
    dfunction_t  *functions;
    int           num_functions;
    int           cap_functions;

    /* Globals data (float-sized slots) */
    float        *globals;
    int           num_globals;     /* current allocation pointer */
    int           cap_globals;

    /* Field counter */
    int           num_fields;

    /* Current function being compiled */
    const char   *cur_func_name;
    int           cur_func_parm_start;
    int           local_base;      /* where locals start for this function */
    int           max_locals;      /* high-water mark for this function */

    /* Temporaries stack */
    int           temp_ofs;        /* next temp slot (above locals) */
    int           temp_high;       /* high-water mark for temps */

    /* Break/continue patch lists */
    int          *break_patches;
    int           num_break_patches;
    int           cap_break_patches;
    int          *continue_patches;
    int           num_continue_patches;
    int           cap_continue_patches;

    /* Source file name string offset */
    int           s_file;
} CodeGen;

/* -------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------*/
CodeGen *codegen_new(void);
void     codegen_free(CodeGen *cg);
bool     codegen_compile(CodeGen *cg, AstNode *program);
bool     codegen_write(CodeGen *cg, const char *filename);
