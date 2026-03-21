/*
 * WaspCompiler - QuakeC Compiler
 * symtab.h - Symbol table
 */
#pragma once
#include "common.h"
#include "ast.h"

/* -------------------------------------------------------------------------
 * Symbol kinds
 * -------------------------------------------------------------------------*/
typedef enum {
    SYM_GLOBAL,     /* global variable */
    SYM_LOCAL,      /* local variable */
    SYM_PARAM,      /* function parameter */
    SYM_FUNCTION,   /* function */
    SYM_FIELD,      /* entity field */
    SYM_CONST,      /* compile-time constant */
    SYM_BUILTIN,    /* built-in function */
} SymKind;

/* -------------------------------------------------------------------------
 * Symbol entry
 * -------------------------------------------------------------------------*/
typedef struct Symbol {
    const char     *name;
    SymKind         kind;
    TypeInfo       *type;

    /* Runtime location */
    int             global_ofs;   /* float-slot offset in globals array */
    int             field_ofs;    /* field slot offset (for SYM_FIELD) */
    int             func_index;   /* index in functions array */
    int             builtin_id;   /* negative function index for builtins */

    /* For constants */
    double          const_val;
    float           const_vec[3];
    const char     *const_str;

    /* Linked list for scope chain */
    struct Symbol  *next;
} Symbol;

/* -------------------------------------------------------------------------
 * Scope
 * -------------------------------------------------------------------------*/
typedef struct Scope {
    Symbol       *symbols;  /* linked list */
    struct Scope *parent;
} Scope;

/* -------------------------------------------------------------------------
 * Symbol table
 * -------------------------------------------------------------------------*/
typedef struct {
    Scope *current;
    Scope *global_scope;
} SymTable;

/* -------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------*/
SymTable *symtab_new(void);
void      symtab_free(SymTable *st);

void      symtab_push_scope(SymTable *st);
void      symtab_pop_scope(SymTable *st);

Symbol   *symtab_define(SymTable *st, const char *name, SymKind kind, TypeInfo *type);
Symbol   *symtab_lookup(SymTable *st, const char *name);
Symbol   *symtab_lookup_local(SymTable *st, const char *name); /* current scope only */
