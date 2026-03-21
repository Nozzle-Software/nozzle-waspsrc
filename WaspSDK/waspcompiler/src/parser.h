/*
 * WaspCompiler - QuakeC Compiler
 * parser.h - Parser interface
 */
#pragma once
#include "lexer.h"
#include "ast.h"

typedef struct {
    Lexer    *lexer;
    AstNode  *root;
    /* Frame macro table */
    struct {
        const char **names;
        int         *values;
        int          count;
        int          cap;
    } frames;
    int frame_base;
} Parser;

Parser  *parser_new(Lexer *lexer);
AstNode *parser_parse(Parser *p);    /* parse entire file, returns AST_PROGRAM */
void     parser_free(Parser *p);
