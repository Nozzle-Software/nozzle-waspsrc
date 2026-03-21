/*
 * WaspCompiler - QuakeC Compiler
 * ast.h - Abstract Syntax Tree node definitions
 */
#pragma once
#include "common.h"
#include "progs.h"

/* -------------------------------------------------------------------------
 * Type representation
 * -------------------------------------------------------------------------*/
struct TypeInfo;

typedef struct TypeInfo {
    etype_t         kind;
    /* For function types */
    struct TypeInfo *ret_type;
    struct TypeInfo **parm_types;
    const char      **parm_names;
    int              num_parms;
    /* For field types: the type of the field */
    struct TypeInfo *field_type;
} TypeInfo;

/* -------------------------------------------------------------------------
 * AST node types
 * -------------------------------------------------------------------------*/
typedef enum {
    /* Declarations */
    AST_PROGRAM,            /* root: list of top-level decls */
    AST_FUNC_DECL,          /* function declaration or definition */
    AST_VAR_DECL,           /* variable / constant declaration */
    AST_FIELD_DECL,         /* .type name; */
    AST_BUILTIN_DECL,       /* rettype(args) name = #N; */

    /* Statements */
    AST_BLOCK,              /* { stmts... } */
    AST_IF,                 /* if (cond) then [else] */
    AST_WHILE,              /* while (cond) body */
    AST_DO_WHILE,           /* do body while (cond) */
    AST_FOR,                /* for (init; cond; post) body */
    AST_RETURN,             /* return [expr] */
    AST_BREAK,
    AST_CONTINUE,
    AST_EXPR_STMT,          /* expression as statement */
    AST_LOCAL_DECL,         /* local declaration inside function */

    /* Expressions */
    AST_NUMBER,             /* float literal */
    AST_VECTOR_LIT,         /* 'x y z' literal */
    AST_STRING_LIT,         /* "..." */
    AST_IDENT,              /* identifier reference */
    AST_FIELD_ACCESS,       /* entity.field */
    AST_UNARY,              /* -x, !x */
    AST_BINARY,             /* x + y, x == y, etc. */
    AST_ASSIGN,             /* x = y (also +=, -=, etc.) */
    AST_CALL,               /* func(args...) */
    AST_ADDRESS,            /* &expr */
    AST_SUBSCRIPT,          /* arr[idx] - not in QuakeC but harmless */
    AST_FRAME_REF,          /* $framename */
    AST_STATE,              /* state(frame, think) */
} AstKind;

/* -------------------------------------------------------------------------
 * AST node
 * -------------------------------------------------------------------------*/
typedef struct AstNode AstNode;

struct AstNode {
    AstKind     kind;
    const char *file;
    int         line;

    /* Resolved type (filled in by type checker / codegen) */
    TypeInfo   *type;

    union {
        /* AST_PROGRAM */
        struct {
            AstNode **decls;
            int       num_decls;
            int       cap_decls;
        } program;

        /* AST_FUNC_DECL */
        struct {
            const char *name;
            TypeInfo   *func_type;
            AstNode    *body;       /* NULL = forward decl */
            bool        is_builtin; /* #N declaration */
            int         builtin_id;
        } func_decl;

        /* AST_VAR_DECL */
        struct {
            const char *name;
            TypeInfo   *var_type;
            AstNode    *init;       /* optional initializer */
            bool        is_const;
        } var_decl;

        /* AST_FIELD_DECL */
        struct {
            const char *name;
            TypeInfo   *field_type; /* type of the field (e.g. float) */
        } field_decl;

        /* AST_BLOCK */
        struct {
            AstNode **stmts;
            int       num_stmts;
            int       cap_stmts;
        } block;

        /* AST_IF */
        struct {
            AstNode *cond;
            AstNode *then_branch;
            AstNode *else_branch; /* may be NULL */
        } if_stmt;

        /* AST_WHILE, AST_DO_WHILE */
        struct {
            AstNode *cond;
            AstNode *body;
        } loop;

        /* AST_FOR */
        struct {
            AstNode *init;  /* expr stmt or local decl */
            AstNode *cond;
            AstNode *post;
            AstNode *body;
        } for_stmt;

        /* AST_RETURN */
        struct {
            AstNode *value; /* may be NULL */
        } ret;

        /* AST_EXPR_STMT / AST_LOCAL_DECL */
        struct {
            AstNode *expr;
            const char *name;
            TypeInfo   *decl_type;
            AstNode    *init;
        } local_decl;

        /* AST_NUMBER */
        double num_val;

        /* AST_VECTOR_LIT */
        float vec_val[3];

        /* AST_STRING_LIT */
        const char *str_val;

        /* AST_IDENT */
        const char *ident;

        /* AST_FIELD_ACCESS */
        struct {
            AstNode    *object;
            const char *field;
        } field_access;

        /* AST_UNARY */
        struct {
            int      op;       /* token type of operator */
            AstNode *operand;
        } unary;

        /* AST_BINARY */
        struct {
            int      op;
            AstNode *left;
            AstNode *right;
        } binary;

        /* AST_ASSIGN */
        struct {
            int      op;    /* '=' or TK_PLUSEQ etc. */
            AstNode *target;
            AstNode *value;
        } assign;

        /* AST_CALL */
        struct {
            AstNode  *func;
            AstNode **args;
            int       num_args;
            int       cap_args;
        } call;

        /* AST_ADDRESS */
        struct {
            AstNode *operand;
        } address;

        /* AST_FRAME_REF */
        const char *frame_name;

        /* AST_STATE */
        struct {
            AstNode *frame;
            AstNode *think;
        } state;
    };
};

/* -------------------------------------------------------------------------
 * AST allocation helpers
 * -------------------------------------------------------------------------*/
AstNode *ast_new(AstKind kind, const char *file, int line);
TypeInfo *type_new(etype_t kind);
TypeInfo *type_func(TypeInfo *ret, TypeInfo **parms, const char **parm_names, int n);
TypeInfo *type_field(TypeInfo *inner);
bool      type_equal(const TypeInfo *a, const TypeInfo *b);
const char *type_to_str(const TypeInfo *t);
int       type_info_size(const TypeInfo *t);
void      ast_print(const AstNode *n, int depth); /* debug */
