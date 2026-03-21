/*
 * WaspCompiler - QuakeC Compiler
 * parser.c - Recursive descent parser for QuakeC
 *
 * Grammar (simplified):
 *   program       = toplevel*
 *   toplevel      = field_decl | func_decl | var_decl
 *   field_decl    = '.' type name (',' name)* ';'
 *   func_decl     = func_type name ('=' ('#' num | '{' body }) | ';')
 *   var_decl      = type name ('=' expr)? ';'
 *   func_type     = ret_type '(' parms ')'
 */
#include "parser.h"
#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Forward declarations
 * -------------------------------------------------------------------------*/
static AstNode   *parse_statement(Parser *p);
static AstNode   *parse_expr(Parser *p);
static AstNode   *parse_assign(Parser *p);
static AstNode   *parse_or(Parser *p);
static AstNode   *parse_and(Parser *p);
static AstNode   *parse_bitor(Parser *p);
static AstNode   *parse_bitand(Parser *p);
static AstNode   *parse_equality(Parser *p);
static AstNode   *parse_relational(Parser *p);
static AstNode   *parse_additive(Parser *p);
static AstNode   *parse_multiplicative(Parser *p);
static AstNode   *parse_unary(Parser *p);
static AstNode   *parse_postfix(Parser *p);
static AstNode   *parse_primary(Parser *p);
static TypeInfo  *parse_base_type(Parser *p);
static TypeInfo  *parse_type(Parser *p, bool allow_field);

/* -------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------*/
static Token      *cur(Parser *p) { return lexer_current(p->lexer); }
static Token       adv(Parser *p) { return lexer_next(p->lexer); }
static bool        check(Parser *p, TokenType t) { return cur(p)->type == t; }

static bool match(Parser *p, TokenType t) {
    if (check(p, t)) { adv(p); return true; }
    return false;
}

static Token expect_tok(Parser *p, TokenType t) {
    Token tok = *cur(p);
    if (tok.type != t)
        wasp_error(tok.file, tok.line, "expected '%s', got '%s'",
                   token_type_str(t), token_type_str(tok.type));
    else
        adv(p);
    return tok;
}

/* Is the current token the start of a type? */
static bool is_type_kw(TokenType t) {
    return t==TK_VOID||t==TK_FLOAT||t==TK_VECTOR_KW||t==TK_STRING_KW||t==TK_ENTITY;
}

/* -------------------------------------------------------------------------
 * Type parsing
 *
 * base_type ::= 'void' | 'float' | 'vector' | 'string' | 'entity'
 * type      ::= base_type ['(' parms ')']    (function type)
 *             | '.' base_type                (field type)
 *             | '.' base_type '(' parms ')'  (field of function type)
 * -------------------------------------------------------------------------*/
static TypeInfo *parse_base_type(Parser *p) {
    Token tok = *cur(p);
    switch (tok.type) {
        case TK_VOID:      adv(p); return type_new(ev_void);
        case TK_FLOAT:     adv(p); return type_new(ev_float);
        case TK_VECTOR_KW: adv(p); return type_new(ev_vector);
        case TK_STRING_KW: adv(p); return type_new(ev_string);
        case TK_ENTITY:    adv(p); return type_new(ev_entity);
        default:
            wasp_error(tok.file, tok.line, "expected type, got '%s'",
                       token_type_str(tok.type));
            return type_new(ev_void);
    }
}

/* Parse a parameter list '(' parms ')' — cursor before '(' on entry */
static TypeInfo *parse_func_parms(Parser *p, TypeInfo *ret) {
    TypeInfo   *pt[MAX_PARMS];
    const char *pn[MAX_PARMS];
    int n = 0;
    expect_tok(p, '(');
    while (!check(p,')')&&!check(p,TK_EOF)) {
        if (n>0) { if (!match(p,',')) break; }
        if (check(p,TK_ELLIPSIS)) { adv(p); break; }
        if (n>=MAX_PARMS) { wasp_error(cur(p)->file,cur(p)->line,"too many parms"); break; }
        TypeInfo *t = parse_base_type(p);
        /* optional parameter name */
        const char *name = NULL;
        if (check(p,TK_IDENT)) { name=cur(p)->str_val; adv(p); }
        pt[n]=t; pn[n]=name; n++;
    }
    expect_tok(p, ')');
    return type_func(ret, pt, pn, n);
}

static TypeInfo *parse_type(Parser *p, bool allow_field) {
    bool is_field = (allow_field && check(p,'.'));
    if (is_field) adv(p);
    TypeInfo *base = parse_base_type(p);
    if (check(p,'(')) {
        TypeInfo *ft = parse_func_parms(p, base);
        return is_field ? type_field(ft) : ft;
    }
    return is_field ? type_field(base) : base;
}

/* -------------------------------------------------------------------------
 * Top-level declaration parsing
 * -------------------------------------------------------------------------*/
static AstNode *parse_toplevel(Parser *p) {
    const char *file = cur(p)->file;
    int line = cur(p)->line;

    /* Field declaration: '.' base_type ['(' parms ')'] name [',' name]* ';' */
    if (check(p, '.')) {
        adv(p);
        TypeInfo *fbase = parse_base_type(p);
        /* Optional function parms for .void() fields */
        TypeInfo *ftype;
        if (check(p,'(')) {
            TypeInfo *ft = parse_func_parms(p, fbase);
            ftype = type_field(ft);
        } else {
            ftype = type_field(fbase);
        }

        /* one or more names */
        AstNode *first = NULL;
        AstNode *block = NULL;
        do {
            Token name_tok = expect_tok(p, TK_IDENT);
            const char *name = str_intern(name_tok.str_val);
            AstNode *fd = ast_new(AST_FIELD_DECL, file, line);
            fd->field_decl.name       = name;
            fd->field_decl.field_type = ftype->field_type; /* unwrap the field wrapper */
            if (!first) { first = fd; }
            else {
                if (!block) {
                    block = ast_new(AST_BLOCK, file, line);
                    DA_PUSH(block->block.stmts, block->block.num_stmts,
                            block->block.cap_stmts, first);
                }
                DA_PUSH(block->block.stmts, block->block.num_stmts,
                        block->block.cap_stmts, fd);
            }
        } while (match(p, ','));
        expect_tok(p, ';');
        return block ? block : first;
    }

    /* Parse return/base type */
    /* Skip stray semicolons at top level (e.g. after function bodies "};") */
    if (check(p, ';')) { adv(p); return NULL; }

    if (!is_type_kw(cur(p)->type)) {
        wasp_error(file, line, "expected declaration, got '%s'",
                   token_type_str(cur(p)->type));
        /* error recovery: skip to next ';' or '}' */
        while (!check(p,';')&&!check(p,'}')&&!check(p,TK_EOF)) adv(p);
        match(p, ';');
        return NULL;
    }

    TypeInfo *base = parse_base_type(p);

    /* Function type: base '(' parms ')' name ... */
    if (check(p, '(')) {
        TypeInfo *func_type = parse_func_parms(p, base);
        Token name_tok = expect_tok(p, TK_IDENT);
        const char *name = str_intern(name_tok.str_val);

        /* Forward declaration: functype name ';' */
        if (match(p, ';')) {
            AstNode *nd = ast_new(AST_FUNC_DECL, file, line);
            nd->func_decl.name       = name;
            nd->func_decl.func_type  = func_type;
            nd->func_decl.body       = NULL;
            nd->func_decl.is_builtin = false;
            return nd;
        }

        expect_tok(p, '=');

        /* Built-in: functype name = #N; */
        if (match(p, '#')) {
            Token num = expect_tok(p, TK_NUMBER);
            expect_tok(p, ';');
            AstNode *nd = ast_new(AST_FUNC_DECL, file, line);
            nd->func_decl.name       = name;
            nd->func_decl.func_type  = func_type;
            nd->func_decl.body       = NULL;
            nd->func_decl.is_builtin = true;
            nd->func_decl.builtin_id = (int)num.num_val;
            return nd;
        }

        /* Function definition: functype name = '{' body '}' [';'] */
        AstNode *body = parse_statement(p);
        match(p, ';');  /* optional trailing semicolon after function body */
        AstNode *nd = ast_new(AST_FUNC_DECL, file, line);
        nd->func_decl.name      = name;
        nd->func_decl.func_type = func_type;
        nd->func_decl.body      = body;
        nd->func_decl.is_builtin= false;
        return nd;
    }

    /* Variable / constant: base name ['=' expr] ';'
       or multiple:         base name, name, ... ; */
    Token name_tok = expect_tok(p, TK_IDENT);
    const char *name = str_intern(name_tok.str_val);

    /* Multiple names: float a, b, c; */
    if (check(p, ',')) {
        AstNode *blk = ast_new(AST_BLOCK, file, line);
        AstNode *first_nd = ast_new(AST_VAR_DECL, file, line);
        first_nd->var_decl.name     = name;
        first_nd->var_decl.var_type = base;
        first_nd->var_decl.init     = NULL;
        DA_PUSH(blk->block.stmts, blk->block.num_stmts, blk->block.cap_stmts, first_nd);
        while (match(p, ',')) {
            Token nm = expect_tok(p, TK_IDENT);
            AstNode *nd2 = ast_new(AST_VAR_DECL, file, line);
            nd2->var_decl.name     = str_intern(nm.str_val);
            nd2->var_decl.var_type = base;
            nd2->var_decl.init     = NULL;
            DA_PUSH(blk->block.stmts, blk->block.num_stmts, blk->block.cap_stmts, nd2);
        }
        expect_tok(p, ';');
        return blk;
    }

    /* Single var: base name [= expr] ; */
    AstNode *init_expr = NULL;
    if (match(p, '=')) init_expr = parse_expr(p);
    expect_tok(p, ';');

    AstNode *nd = ast_new(AST_VAR_DECL, file, line);
    nd->var_decl.name     = name;
    nd->var_decl.var_type = base;
    nd->var_decl.init     = init_expr;
    nd->var_decl.is_const = false;
    return nd;
}

/* -------------------------------------------------------------------------
 * Statement parsing
 * -------------------------------------------------------------------------*/
static AstNode *parse_block(Parser *p) {
    const char *file = cur(p)->file;
    int line = cur(p)->line;
    expect_tok(p, '{');
    AstNode *block = ast_new(AST_BLOCK, file, line);
    while (!check(p,'}')&&!check(p,TK_EOF)) {
        AstNode *s = parse_statement(p);
        if (s) DA_PUSH(block->block.stmts, block->block.num_stmts,
                       block->block.cap_stmts, s);
    }
    expect_tok(p, '}');
    return block;
}

static AstNode *parse_statement(Parser *p) {
    const char *file = cur(p)->file;
    int line = cur(p)->line;

    if (check(p,'{'))      return parse_block(p);

    if (check(p,TK_IF)) {
        adv(p); expect_tok(p,'(');
        AstNode *cond = parse_expr(p); expect_tok(p,')');
        AstNode *then_b = parse_statement(p);
        AstNode *else_b = NULL;
        if (match(p,TK_ELSE)) else_b = parse_statement(p);
        AstNode *nd = ast_new(AST_IF,file,line);
        nd->if_stmt.cond=cond; nd->if_stmt.then_branch=then_b; nd->if_stmt.else_branch=else_b;
        return nd;
    }

    if (check(p,TK_WHILE)) {
        adv(p); expect_tok(p,'(');
        AstNode *cond = parse_expr(p); expect_tok(p,')');
        AstNode *body = parse_statement(p);
        AstNode *nd = ast_new(AST_WHILE,file,line);
        nd->loop.cond=cond; nd->loop.body=body;
        return nd;
    }

    if (check(p,TK_DO)) {
        adv(p);
        AstNode *body = parse_statement(p);
        expect_tok(p,TK_WHILE); expect_tok(p,'(');
        AstNode *cond = parse_expr(p); expect_tok(p,')'); expect_tok(p,';');
        AstNode *nd = ast_new(AST_DO_WHILE,file,line);
        nd->loop.cond=cond; nd->loop.body=body;
        return nd;
    }

    if (check(p,TK_FOR)) {
        adv(p); expect_tok(p,'(');
        AstNode *init=NULL, *cond=NULL, *post=NULL;
        if (!check(p,';')) {
            if (is_type_kw(cur(p)->type)) init=parse_statement(p);
            else { AstNode *e=ast_new(AST_EXPR_STMT,file,line); e->local_decl.expr=parse_expr(p); init=e; expect_tok(p,';'); }
        } else adv(p);
        if (!check(p,';')) cond=parse_expr(p); expect_tok(p,';');
        if (!check(p,')')) post=parse_expr(p); expect_tok(p,')');
        AstNode *body=parse_statement(p);
        AstNode *nd=ast_new(AST_FOR,file,line);
        nd->for_stmt.init=init; nd->for_stmt.cond=cond;
        nd->for_stmt.post=post; nd->for_stmt.body=body;
        return nd;
    }

    if (check(p,TK_RETURN)) {
        adv(p);
        AstNode *val=NULL;
        if (!check(p,';')) val=parse_expr(p);
        expect_tok(p,';');
        AstNode *nd=ast_new(AST_RETURN,file,line);
        nd->ret.value=val; return nd;
    }

    if (check(p,TK_BREAK))    { adv(p); expect_tok(p,';'); return ast_new(AST_BREAK,file,line); }
    if (check(p,TK_CONTINUE)) { adv(p); expect_tok(p,';'); return ast_new(AST_CONTINUE,file,line); }

    /* local declaration: 'local' type name [= expr] ';'  OR  type name [= expr] ';' */
    bool have_local = match(p, TK_LOCAL);
    if (have_local || is_type_kw(cur(p)->type)) {
        TypeInfo *lt = parse_type(p, false);
        /* Support: local float a, b, c; */
        AstNode *multi_block = NULL;
        for (;;) {
            Token nm = expect_tok(p, TK_IDENT);
            const char *lname = str_intern(nm.str_val);
            AstNode *linit = NULL;
            if (match(p,'=')) linit=parse_expr(p);
            AstNode *nd=ast_new(AST_LOCAL_DECL,file,line);
            nd->local_decl.name=lname; nd->local_decl.decl_type=lt; nd->local_decl.init=linit;
            if (!multi_block && !check(p,',')) { expect_tok(p,';'); return nd; }
            if (!multi_block) { multi_block=ast_new(AST_BLOCK,file,line); }
            DA_PUSH(multi_block->block.stmts, multi_block->block.num_stmts,
                    multi_block->block.cap_stmts, nd);
            if (!match(p,',')) break;
        }
        expect_tok(p,';');
        return multi_block;
    }

    /* expression statement */
    AstNode *e = parse_expr(p);
    if (e) { expect_tok(p,';'); AstNode *nd=ast_new(AST_EXPR_STMT,file,line); nd->local_decl.expr=e; return nd; }

    /* unexpected token — skip */
    wasp_error(file,line,"unexpected token '%s' in statement",token_type_str(cur(p)->type));
    if (!check(p,TK_EOF)) adv(p);
    return NULL;
}

/* -------------------------------------------------------------------------
 * Expression parsing (standard precedence climbing)
 * -------------------------------------------------------------------------*/
static AstNode *parse_expr(Parser *p) { return parse_assign(p); }

static AstNode *parse_assign(Parser *p) {
    const char *f=cur(p)->file; int l=cur(p)->line;
    AstNode *left = parse_or(p);
    int op = cur(p)->type;
    if (op=='='||op==TK_PLUSEQ||op==TK_MINUSEQ||op==TK_MULEQ||op==TK_DIVEQ) {
        adv(p);
        AstNode *right = parse_assign(p);
        AstNode *nd=ast_new(AST_ASSIGN,f,l);
        nd->assign.op=op; nd->assign.target=left; nd->assign.value=right;
        return nd;
    }
    return left;
}

#define BINOP(name, next_fn, ...)                                         \
static AstNode *name(Parser *p) {                                         \
    AstNode *left = next_fn(p);                                           \
    for (;;) {                                                            \
        const char *f=cur(p)->file; int l=cur(p)->line;                  \
        int op=cur(p)->type;                                              \
        if (!( __VA_ARGS__ )) break;                                      \
        adv(p);                                                           \
        AstNode *right = next_fn(p);                                     \
        AstNode *nd=ast_new(AST_BINARY,f,l);                             \
        nd->binary.op=op; nd->binary.left=left; nd->binary.right=right;  \
        left=nd;                                                          \
    }                                                                     \
    return left;                                                          \
}

BINOP(parse_or,           parse_and,           op==TK_OR)
BINOP(parse_and,          parse_bitor,         op==TK_AND)
BINOP(parse_bitor,        parse_bitand,        op=='|')
BINOP(parse_bitand,       parse_equality,      op=='&')
BINOP(parse_equality,     parse_relational,    op==TK_EQ||op==TK_NE)
BINOP(parse_relational,   parse_additive,      op=='<'||op=='>'||op==TK_LE||op==TK_GE)
BINOP(parse_additive,     parse_multiplicative,op=='+'||op=='-')
BINOP(parse_multiplicative, parse_unary,       op=='*'||op=='/')

static AstNode *parse_unary(Parser *p) {
    const char *f=cur(p)->file; int l=cur(p)->line;
    if (check(p,'-')||check(p,'!')||check(p,'~')) {
        int op=cur(p)->type; adv(p);
        AstNode *nd=ast_new(AST_UNARY,f,l);
        nd->unary.op=op; nd->unary.operand=parse_unary(p);
        return nd;
    }
    return parse_postfix(p);
}

static AstNode *parse_postfix(Parser *p) {
    AstNode *node = parse_primary(p);
    for (;;) {
        const char *f=cur(p)->file; int l=cur(p)->line;
        if (check(p,'.')) {
            adv(p);
            Token ft=expect_tok(p,TK_IDENT);
            AstNode *nd=ast_new(AST_FIELD_ACCESS,f,l);
            nd->field_access.object=node; nd->field_access.field=str_intern(ft.str_val);
            node=nd; continue;
        }
        if (check(p,'(')) {
            adv(p);
            AstNode *call=ast_new(AST_CALL,f,l);
            call->call.func=node;
            while (!check(p,')')&&!check(p,TK_EOF)) {
                if (call->call.num_args>0) expect_tok(p,',');
                AstNode *arg=parse_assign(p);
                DA_PUSH(call->call.args,call->call.num_args,call->call.cap_args,arg);
            }
            expect_tok(p,')');
            node=call; continue;
        }
        if (check(p,TK_INC)||check(p,TK_DEC)) {
            int op=cur(p)->type; adv(p);
            AstNode *nd=ast_new(AST_UNARY,f,l);
            nd->unary.op=(op==TK_INC)?'P':'Q'; nd->unary.operand=node;
            node=nd; continue;
        }
        break;
    }
    return node;
}

static AstNode *parse_primary(Parser *p) {
    const char *f=cur(p)->file; int l=cur(p)->line;

    if (check(p,TK_NUMBER)) {
        double v=cur(p)->num_val; adv(p);
        AstNode *nd=ast_new(AST_NUMBER,f,l); nd->num_val=v; return nd;
    }
    if (check(p,TK_VECTOR)) {
        float vx=cur(p)->vec_val[0],vy=cur(p)->vec_val[1],vz=cur(p)->vec_val[2]; adv(p);
        AstNode *nd=ast_new(AST_VECTOR_LIT,f,l);
        nd->vec_val[0]=vx; nd->vec_val[1]=vy; nd->vec_val[2]=vz; return nd;
    }
    if (check(p,TK_STRING)) {
        const char *sv=str_intern(cur(p)->str_val); adv(p);
        AstNode *nd=ast_new(AST_STRING_LIT,f,l); nd->str_val=sv; return nd;
    }
    if (check(p,TK_IDENT)) {
        const char *name=cur(p)->str_val; adv(p);
        AstNode *nd=ast_new(AST_IDENT,f,l); nd->ident=name; return nd;
    }
    if (check(p,'(')) {
        adv(p); AstNode *nd=parse_expr(p); expect_tok(p,')'); return nd;
    }
    if (check(p,TK_FRAME_MACRO)) {
        const char *fname=cur(p)->str_val; adv(p);
        for (int i=0; i<p->frames.count; i++) {
            if (p->frames.names[i]==fname) {
                AstNode *nd=ast_new(AST_NUMBER,f,l); nd->num_val=p->frames.values[i]; return nd;
            }
        }
        wasp_warning(f,l,"unknown frame '$%s', using 0",fname);
        AstNode *nd=ast_new(AST_NUMBER,f,l); nd->num_val=0; return nd;
    }

    wasp_error(f,l,"unexpected token '%s' in expression",token_type_str(cur(p)->type));
    if (!check(p,TK_EOF)) adv(p);
    AstNode *nd=ast_new(AST_NUMBER,f,l); nd->num_val=0; return nd;
}

/* -------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/
Parser *parser_new(Lexer *lexer) {
    Parser *p=(Parser*)wasp_malloc(sizeof(Parser));
    p->lexer=lexer; p->root=NULL; p->frame_base=0;
    return p;
}

AstNode *parser_parse(Parser *p) {
    AstNode *prog=ast_new(AST_PROGRAM, p->lexer->filename, 0);
    while (!check(p,TK_EOF)) {
        AstNode *decl=parse_toplevel(p);
        if (!decl) continue;
        if (decl->kind==AST_BLOCK) {
            for (int i=0;i<decl->block.num_stmts;i++)
                DA_PUSH(prog->program.decls,prog->program.num_decls,
                        prog->program.cap_decls,decl->block.stmts[i]);
        } else {
            DA_PUSH(prog->program.decls,prog->program.num_decls,
                    prog->program.cap_decls,decl);
        }
    }
    return prog;
}

void parser_free(Parser *p) {
    if (!p) return;
    free(p->frames.names);
    free(p->frames.values);
    free(p);
}
