/*
 * WaspCompiler - QuakeC Compiler
 * ast.c - AST node construction and utilities
 */
#include "ast.h"
#include <stdio.h>

AstNode *ast_new(AstKind kind, const char *file, int line) {
    AstNode *n = (AstNode *)wasp_malloc(sizeof(AstNode));
    n->kind = kind;
    n->file = file;
    n->line = line;
    n->type = NULL;
    return n;
}

TypeInfo *type_new(etype_t kind) {
    TypeInfo *t = (TypeInfo *)wasp_malloc(sizeof(TypeInfo));
    t->kind       = kind;
    t->ret_type   = NULL;
    t->parm_types = NULL;
    t->parm_names = NULL;
    t->num_parms  = 0;
    t->field_type = NULL;
    return t;
}

TypeInfo *type_func(TypeInfo *ret, TypeInfo **parms, const char **parm_names, int n) {
    TypeInfo *t = type_new(ev_function);
    t->ret_type   = ret;
    t->num_parms  = n;
    if (n > 0) {
        t->parm_types = (TypeInfo **)wasp_malloc(n * sizeof(TypeInfo *));
        t->parm_names = (const char **)wasp_malloc(n * sizeof(const char *));
        for (int i = 0; i < n; i++) {
            t->parm_types[i] = parms[i];
            t->parm_names[i] = parm_names ? parm_names[i] : NULL;
        }
    }
    return t;
}

TypeInfo *type_field(TypeInfo *inner) {
    TypeInfo *t = type_new(ev_field);
    t->field_type = inner;
    return t;
}

bool type_equal(const TypeInfo *a, const TypeInfo *b) {
    if (!a || !b) return a == b;
    if (a->kind != b->kind) return false;
    if (a->kind == ev_function) {
        if (!type_equal(a->ret_type, b->ret_type)) return false;
        if (a->num_parms != b->num_parms) return false;
        for (int i = 0; i < a->num_parms; i++)
            if (!type_equal(a->parm_types[i], b->parm_types[i])) return false;
    }
    if (a->kind == ev_field)
        return type_equal(a->field_type, b->field_type);
    return true;
}

const char *type_to_str(const TypeInfo *t) {
    if (!t) return "<null>";
    static char buf[256];
    switch (t->kind) {
        case ev_void:     return "void";
        case ev_string:   return "string";
        case ev_float:    return "float";
        case ev_vector:   return "vector";
        case ev_entity:   return "entity";
        case ev_pointer:  return "pointer";
        case ev_field: {
            snprintf(buf, sizeof(buf), ".%s", type_to_str(t->field_type));
            return buf;
        }
        case ev_function: {
            int pos = 0;
            pos += snprintf(buf + pos, sizeof(buf) - pos, "%s(",
                            type_to_str(t->ret_type));
            for (int i = 0; i < t->num_parms; i++) {
                if (i > 0) pos += snprintf(buf + pos, sizeof(buf) - pos, ", ");
                pos += snprintf(buf + pos, sizeof(buf) - pos, "%s",
                                type_to_str(t->parm_types[i]));
            }
            snprintf(buf + pos, sizeof(buf) - pos, ")");
            return buf;
        }
        default: return "bad";
    }
}

int type_info_size(const TypeInfo *t) {
    if (!t) return 1;
    return type_size(t->kind);
}

void ast_print(const AstNode *n, int depth) {
    if (!n) return;
    for (int i = 0; i < depth; i++) printf("  ");
    switch (n->kind) {
        case AST_NUMBER:   printf("NUM(%g)\n", n->num_val); break;
        case AST_STRING_LIT: printf("STR(%s)\n", n->str_val); break;
        case AST_IDENT:    printf("ID(%s)\n", n->ident); break;
        case AST_BINARY:   printf("BIN(%c)\n", n->binary.op);
            ast_print(n->binary.left, depth+1);
            ast_print(n->binary.right, depth+1);
            break;
        case AST_CALL:     printf("CALL\n");
            ast_print(n->call.func, depth+1);
            for (int i = 0; i < n->call.num_args; i++)
                ast_print(n->call.args[i], depth+1);
            break;
        default: printf("NODE(%d)\n", n->kind); break;
    }
}
