/*
 * WaspCompiler - QuakeC Compiler
 * lexer.h - Tokenizer interface
 */
#pragma once
#include "common.h"

/* -------------------------------------------------------------------------
 * Token types
 * -------------------------------------------------------------------------*/
typedef enum {
    /* Literals */
    TK_IDENT    = 256,  /* identifier */
    TK_NUMBER,          /* float literal */
    TK_STRING,          /* "..." */
    TK_VECTOR,          /* 'x y z' */

    /* Keywords */
    TK_VOID,
    TK_FLOAT,
    TK_VECTOR_KW,
    TK_STRING_KW,
    TK_ENTITY,
    TK_IF,
    TK_ELSE,
    TK_WHILE,
    TK_DO,
    TK_RETURN,
    TK_LOCAL,
    TK_FOR,             /* fteqcc extension */
    TK_BREAK,
    TK_CONTINUE,

    /* Operators (multi-char) */
    TK_EQ,              /* == */
    TK_NE,              /* != */
    TK_LE,              /* <= */
    TK_GE,              /* >= */
    TK_AND,             /* && */
    TK_OR,              /* || */
    TK_PLUSEQ,          /* += */
    TK_MINUSEQ,         /* -= */
    TK_MULEQ,           /* *= */
    TK_DIVEQ,           /* /= */
    TK_INC,             /* ++ */
    TK_DEC,             /* -- */
    TK_ELLIPSIS,        /* ... */

    /* Frame macro token */
    TK_FRAME_MACRO,     /* $framename */

    /* End of file */
    TK_EOF,

    /* Single-char tokens use their ASCII value directly (< 256) */
} TokenType;

/* -------------------------------------------------------------------------
 * Token structure
 * -------------------------------------------------------------------------*/
typedef struct {
    TokenType   type;
    const char *file;
    int         line;

    /* Value (for literals/idents) */
    union {
        double  num_val;
        const char *str_val;  /* interned — never freed */
        float   vec_val[3];
    };
} Token;

/* -------------------------------------------------------------------------
 * Lexer state
 * -------------------------------------------------------------------------*/
typedef struct {
    const char *filename;
    char       *src;        /* full source text (heap) */
    const char *p;          /* current position */
    int         line;
    bool        at_line_start; /* for # directive detection */

    /* Current token */
    Token       cur;
    /* Frame macro state */
    int         frame_counter;
    char       *model_name;

    /* Frame name -> value table */
    const char **frame_names;
    int         *frame_vals;
    int          num_frames;
    int          cap_frames;
    int          cap_fvals;

    /* For #define substitution */
    struct {
        const char **keys;
        char       **values;
        int          count;
        int          cap;
        int          vcap;
    } defines;

    /* peek not used in new implementation */
} Lexer;

/* -------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------*/
Lexer  *lexer_new(const char *filename, const char *src);
void    lexer_free(Lexer *l);

/* Advance to next token, returning it */
Token   lexer_next(Lexer *l);

/* Consume current token, return its type */
TokenType lexer_advance(Lexer *l);

/* Require a specific token type, consume it, or emit error */
Token   lexer_expect(Lexer *l, TokenType type);

/* Check if current token matches, if so consume and return true */
bool    lexer_match(Lexer *l, TokenType type);

/* Get current token without advancing */
Token  *lexer_current(Lexer *l);

const char *token_type_str(TokenType t);
