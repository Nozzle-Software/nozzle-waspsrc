/*
 * WaspCompiler - QuakeC Compiler
 * symtab.c - Symbol table implementation
 */
#include "symtab.h"

SymTable *symtab_new(void) {
    SymTable *st = (SymTable *)wasp_malloc(sizeof(SymTable));
    Scope *gs = (Scope *)wasp_malloc(sizeof(Scope));
    gs->symbols = NULL;
    gs->parent  = NULL;
    st->current      = gs;
    st->global_scope = gs;
    return st;
}

void symtab_push_scope(SymTable *st) {
    Scope *s = (Scope *)wasp_malloc(sizeof(Scope));
    s->symbols = NULL;
    s->parent  = st->current;
    st->current = s;
}

void symtab_pop_scope(SymTable *st) {
    if (st->current == st->global_scope) return;
    Scope *s = st->current;
    st->current = s->parent;
    /* Free symbols in this scope */
    Symbol *sym = s->symbols;
    while (sym) {
        Symbol *next = sym->next;
        free(sym);
        sym = next;
    }
    free(s);
}

Symbol *symtab_define(SymTable *st, const char *name, SymKind kind, TypeInfo *type) {
    name = str_intern(name);
    Symbol *sym = (Symbol *)wasp_malloc(sizeof(Symbol));
    sym->name       = name;
    sym->kind       = kind;
    sym->type       = type;
    sym->global_ofs = 0;
    sym->field_ofs  = 0;
    sym->func_index = -1;
    sym->builtin_id = 0;
    sym->const_val  = 0;
    sym->const_str  = NULL;
    sym->next       = st->current->symbols;
    st->current->symbols = sym;
    return sym;
}

Symbol *symtab_lookup(SymTable *st, const char *name) {
    name = str_intern(name);
    for (Scope *s = st->current; s; s = s->parent) {
        for (Symbol *sym = s->symbols; sym; sym = sym->next)
            if (sym->name == name) return sym;
    }
    return NULL;
}

Symbol *symtab_lookup_local(SymTable *st, const char *name) {
    name = str_intern(name);
    for (Symbol *sym = st->current->symbols; sym; sym = sym->next)
        if (sym->name == name) return sym;
    return NULL;
}

void symtab_free(SymTable *st) {
    if (!st) return;
    Scope *s = st->current;
    while (s) {
        Symbol *sym = s->symbols;
        while (sym) {
            Symbol *next = sym->next;
            free(sym);
            sym = next;
        }
        Scope *parent = s->parent;
        free(s);
        s = parent;
    }
    free(st);
}
