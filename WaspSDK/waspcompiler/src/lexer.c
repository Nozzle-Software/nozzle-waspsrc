/*
 * WaspCompiler - QuakeC Compiler
 * lexer.c - Tokenizer implementation
 *
 * All string-valued tokens (TK_IDENT, TK_STRING, TK_FRAME_MACRO) store
 * interned pointers — never individually heap-allocated or freed.
 */
#include "lexer.h"

/* -------------------------------------------------------------------------
 * Keyword table
 * -------------------------------------------------------------------------*/
typedef struct { const char *word; TokenType type; } Keyword;
static const Keyword keywords[] = {
    {"void",    TK_VOID},    {"float",   TK_FLOAT},  {"vector", TK_VECTOR_KW},
    {"string",  TK_STRING_KW},{"entity", TK_ENTITY}, {"if",     TK_IF},
    {"else",    TK_ELSE},    {"while",   TK_WHILE},  {"do",     TK_DO},
    {"return",  TK_RETURN},  {"local",   TK_LOCAL},  {"for",    TK_FOR},
    {"break",   TK_BREAK},   {"continue",TK_CONTINUE},{NULL,    0}
};
static TokenType kw_lookup(const char *id) {
    for (int i=0; keywords[i].word; i++)
        if (strcmp(keywords[i].word, id)==0) return keywords[i].type;
    return TK_IDENT;
}

/* -------------------------------------------------------------------------
 * Whitespace / comment skipping
 * '#' is ONLY treated as a directive when it appears at the start of a line
 * (first non-whitespace after a newline). Otherwise it's passed to scan()
 * as a normal '#' character (used by = #N builtin syntax).
 * -------------------------------------------------------------------------*/
static void skip_ws(Lexer *l) {
    for (;;) {
        /* whitespace */
        while (*l->p && isspace((unsigned char)*l->p)) {
            if (*l->p == '\n') { l->line++; l->at_line_start = true; }
            l->p++;
        }
        /* line comment */
        if (l->p[0]=='/' && l->p[1]=='/') {
            while (*l->p && *l->p!='\n') l->p++;
            continue;
        }
        /* block comment */
        if (l->p[0]=='/' && l->p[1]=='*') {
            l->p += 2;
            while (*l->p) {
                if (l->p[0]=='*' && l->p[1]=='/') { l->p+=2; break; }
                if (*l->p=='\n') { l->line++; l->at_line_start = true; }
                l->p++;
            }
            continue;
        }
        /* preprocessor directive — ONLY at start of line */
        if (*l->p=='#' && l->at_line_start) {
            l->p++;
            while (*l->p==' '||*l->p=='\t') l->p++;
            char dir[32]; int di=0;
            while (isalpha((unsigned char)*l->p) && di<31) dir[di++]=*l->p++;
            dir[di]='\0';
            if (strcmp(dir,"define")==0) {
                while (*l->p==' '||*l->p=='\t') l->p++;
                char key[128]; int ki=0;
                while ((isalnum((unsigned char)*l->p)||*l->p=='_') && ki<127) key[ki++]=*l->p++;
                key[ki]='\0';
                while (*l->p==' '||*l->p=='\t') l->p++;
                char val[256]; int vi=0;
                while (*l->p && *l->p!='\n' && *l->p!='\r' && vi<255) val[vi++]=*l->p++;
                while (vi>0 && isspace((unsigned char)val[vi-1])) vi--;
                val[vi]='\0';
                if (ki>0) {
                    int idx = l->defines.count;
                    const char *ik = str_intern(key);
                    DA_PUSH(l->defines.keys, l->defines.count, l->defines.cap, ik);
                    if (idx >= l->defines.vcap) {
                        l->defines.vcap = l->defines.vcap ? l->defines.vcap*2 : DA_INIT_CAP;
                        l->defines.values=(char**)wasp_realloc(l->defines.values,
                                           l->defines.vcap*sizeof(char*));
                    }
                    l->defines.values[idx] = wasp_strdup(val);
                }
            }
            /* skip rest of directive line */
            while (*l->p && *l->p!='\n') l->p++;
            continue;
        }
        /* not whitespace / comment / directive */
        l->at_line_start = false;
        break;
    }
}

static char unescape(Lexer *l) {
    l->p++;
    switch (*l->p++) {
        case 'n': return '\n'; case 't': return '\t'; case 'r': return '\r';
        case '\\':return '\\'; case '"': return '"';  case '\'':return '\'';
        case '0': return '\0'; default:  return *(l->p-1);
    }
}

static Token tok_make(Lexer *l, TokenType t) {
    Token tok; tok.type=t; tok.file=l->filename; tok.line=l->line; tok.str_val=NULL;
    return tok;
}

/* -------------------------------------------------------------------------
 * Individual readers
 * -------------------------------------------------------------------------*/
static Token read_string_tok(Lexer *l) {
    Token tok = tok_make(l, TK_STRING);
    l->p++;
    char buf[4096]; int bi=0;
    while (*l->p && *l->p!='"') {
        if (*l->p=='\\') buf[bi++]=unescape(l);
        else { if (*l->p=='\n') l->line++; buf[bi++]=*l->p++; }
        if (bi>=4094) break;
    }
    buf[bi]='\0';
    if (*l->p=='"') l->p++;
    tok.str_val = str_intern(buf);
    return tok;
}

static Token read_vec_tok(Lexer *l) {
    Token tok = tok_make(l, TK_VECTOR);
    l->p++;
    tok.vec_val[0]=(float)strtod(l->p,(char**)&l->p);
    while (*l->p==' '||*l->p=='\t') l->p++;
    tok.vec_val[1]=(float)strtod(l->p,(char**)&l->p);
    while (*l->p==' '||*l->p=='\t') l->p++;
    tok.vec_val[2]=(float)strtod(l->p,(char**)&l->p);
    while (*l->p && *l->p!='\'') l->p++;
    if (*l->p=='\'') l->p++;
    return tok;
}

static Token read_num_tok(Lexer *l) {
    Token tok = tok_make(l, TK_NUMBER);
    char *end; tok.num_val=strtod(l->p,&end); l->p=end;
    return tok;
}

static Token read_ident_tok(Lexer *l) {
    char buf[256]; int bi=0;
    while ((isalnum((unsigned char)*l->p)||*l->p=='_') && bi<255) buf[bi++]=*l->p++;
    buf[bi]='\0';
    const char *ik = str_intern(buf);
    for (int i=0; i<l->defines.count; i++) {
        if (l->defines.keys[i]==ik) {
            const char *val=l->defines.values[i];
            char *end; double d=strtod(val,&end);
            if (end!=val && *end=='\0') { Token t=tok_make(l,TK_NUMBER); t.num_val=d; return t; }
            TokenType kt=kw_lookup(val);
            Token t=tok_make(l,kt);
            if (kt==TK_IDENT) t.str_val=str_intern(val);
            return t;
        }
    }
    TokenType kt=kw_lookup(buf);
    Token t=tok_make(l,kt);
    if (kt==TK_IDENT) t.str_val=ik;
    return t;
}

static Token read_frame_tok(Lexer *l) {
    l->p++;
    if (isdigit((unsigned char)*l->p)||*l->p=='-'||*l->p=='+') {
        Token t=tok_make(l,TK_NUMBER); t.num_val=strtod(l->p,(char**)&l->p); return t;
    }
    char buf[256]; int bi=0;
    while ((isalnum((unsigned char)*l->p)||*l->p=='_') && bi<255) buf[bi++]=*l->p++;
    buf[bi]='\0';
    if (strcmp(buf,"modelname")==0||strcmp(buf,"model")==0) {
        while (*l->p==' '||*l->p=='\t') l->p++;
        char mb[256]; int mi=0;
        while (*l->p&&*l->p!='\n'&&!isspace((unsigned char)*l->p)&&mi<255) mb[mi++]=*l->p++;
        mb[mi]='\0'; free(l->model_name); l->model_name=wasp_strdup(mb); l->frame_counter=0;
        while (*l->p&&*l->p!='\n') l->p++;
        Token t=tok_make(l,TK_FRAME_MACRO); t.str_val=str_intern("$modelname"); return t;
    }
    if (strcmp(buf,"frame")==0||strcmp(buf,"frames")==0) {
        while (*l->p&&*l->p!='\n') {
            while (*l->p==' '||*l->p=='\t') l->p++;
            if (!*l->p||*l->p=='\n') break;
            char fn[128]; int fi=0;
            while ((isalnum((unsigned char)*l->p)||*l->p=='_')&&fi<127) fn[fi++]=*l->p++;
            fn[fi]='\0';
            if (fi>0) {
                const char *in=str_intern(fn); int fv=l->frame_counter++;
                DA_PUSH(l->frame_names, l->num_frames, l->cap_frames, in);
                if (l->num_frames>l->cap_fvals) {
                    l->cap_fvals=l->num_frames*2;
                    l->frame_vals=(int*)wasp_realloc(l->frame_vals,l->cap_fvals*sizeof(int));
                }
                l->frame_vals[l->num_frames-1]=fv;
            }
        }
        Token t=tok_make(l,TK_FRAME_MACRO); t.str_val=str_intern("$frame"); return t;
    }
    Token t=tok_make(l,TK_FRAME_MACRO); t.str_val=str_intern(buf); return t;
}

/* -------------------------------------------------------------------------
 * Core scanner
 * -------------------------------------------------------------------------*/
static Token scan(Lexer *l) {
    skip_ws(l);
    if (!*l->p) return tok_make(l,TK_EOF);
    char c=*l->p, c2=l->p[1];
    if (isdigit((unsigned char)c)||(c=='.'&&isdigit((unsigned char)c2))) return read_num_tok(l);
    if (c=='"') return read_string_tok(l);
    if (c=='\''&&(c2=='-'||isdigit((unsigned char)c2)||c2=='.'||c2==' '||c2=='\t')) return read_vec_tok(l);
    if (c=='$') return read_frame_tok(l);
    if (isalpha((unsigned char)c)||c=='_') return read_ident_tok(l);
    l->p++;
    switch (c) {
        case '=': if(c2=='='){l->p++;return tok_make(l,TK_EQ);}  return tok_make(l,'=');
        case '!': if(c2=='='){l->p++;return tok_make(l,TK_NE);}  return tok_make(l,'!');
        case '<': if(c2=='='){l->p++;return tok_make(l,TK_LE);}  return tok_make(l,'<');
        case '>': if(c2=='='){l->p++;return tok_make(l,TK_GE);}  return tok_make(l,'>');
        case '&': if(c2=='&'){l->p++;return tok_make(l,TK_AND);} return tok_make(l,'&');
        case '|': if(c2=='|'){l->p++;return tok_make(l,TK_OR);}  return tok_make(l,'|');
        case '+': if(c2=='='){l->p++;return tok_make(l,TK_PLUSEQ);}
                  if(c2=='+'){l->p++;return tok_make(l,TK_INC);} return tok_make(l,'+');
        case '-': if(c2=='='){l->p++;return tok_make(l,TK_MINUSEQ);}
                  if(c2=='-'){l->p++;return tok_make(l,TK_DEC);} return tok_make(l,'-');
        case '*': if(c2=='='){l->p++;return tok_make(l,TK_MULEQ);} return tok_make(l,'*');
        case '/': if(c2=='='){l->p++;return tok_make(l,TK_DIVEQ);} return tok_make(l,'/');
        case '.': if(c2=='.'&&l->p[1]=='.'){l->p+=2;return tok_make(l,TK_ELLIPSIS);}
                  return tok_make(l,'.');
        default:  return tok_make(l,(TokenType)(unsigned char)c);
    }
}

/* Skip directive-only frame tokens, resolve $framename → float */
static Token scan_next(Lexer *l) {
    for (;;) {
        Token t = scan(l);
        if (t.type != TK_FRAME_MACRO) return t;
        const char *n = t.str_val;
        if (n==str_intern("$frame")||n==str_intern("$modelname")) continue;
        for (int i=0; i<l->num_frames; i++) {
            if (l->frame_names[i]==n) {
                Token nt=tok_make(l,TK_NUMBER); nt.num_val=l->frame_vals[i]; return nt;
            }
        }
        wasp_warning(l->filename,l->line,"unknown frame macro '$%s', using 0",n);
        Token nt=tok_make(l,TK_NUMBER); nt.num_val=0; return nt;
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/
Lexer *lexer_new(const char *filename, const char *src) {
    Lexer *l=(Lexer*)wasp_malloc(sizeof(Lexer));
    l->filename=str_intern(filename); l->src=wasp_strdup(src); l->p=l->src;
    l->line=1; l->at_line_start=true;
    l->frame_counter=0; l->model_name=NULL;
    l->num_frames=0; l->cap_frames=0; l->cap_fvals=0;
    l->frame_names=NULL; l->frame_vals=NULL;
    l->cur=scan_next(l);
    return l;
}
void lexer_free(Lexer *l) {
    if (!l) return;
    free(l->src); free(l->model_name); free(l->frame_names); free(l->frame_vals);
    for (int i=0;i<l->defines.count;i++) free(l->defines.values[i]);
    free(l->defines.keys); free(l->defines.values); free(l);
}
Token *lexer_current(Lexer *l) { return &l->cur; }
Token  lexer_next(Lexer *l)    { l->cur=scan_next(l); return l->cur; }
TokenType lexer_advance(Lexer *l) { TokenType t=l->cur.type; lexer_next(l); return t; }
Token lexer_expect(Lexer *l, TokenType type) {
    Token tok=l->cur;
    if (tok.type!=type)
        wasp_error(tok.file,tok.line,"expected '%s' but got '%s'",
                   token_type_str(type),token_type_str(tok.type));
    lexer_next(l);
    return tok;
}
bool lexer_match(Lexer *l, TokenType type) {
    if (l->cur.type==type){lexer_next(l);return true;} return false;
}
const char *token_type_str(TokenType t) {
    switch(t){
        case TK_IDENT: return "identifier"; case TK_NUMBER: return "number";
        case TK_STRING: return "string"; case TK_VECTOR: return "vector";
        case TK_VOID: return "void"; case TK_FLOAT: return "float";
        case TK_VECTOR_KW: return "vector"; case TK_STRING_KW: return "string";
        case TK_ENTITY: return "entity"; case TK_IF: return "if";
        case TK_ELSE: return "else"; case TK_WHILE: return "while";
        case TK_DO: return "do"; case TK_RETURN: return "return";
        case TK_LOCAL: return "local"; case TK_FOR: return "for";
        case TK_BREAK: return "break"; case TK_CONTINUE: return "continue";
        case TK_EQ: return "=="; case TK_NE: return "!=";
        case TK_LE: return "<="; case TK_GE: return ">=";
        case TK_AND: return "&&"; case TK_OR: return "||";
        case TK_PLUSEQ: return "+="; case TK_MINUSEQ: return "-=";
        case TK_MULEQ: return "*="; case TK_DIVEQ: return "/=";
        case TK_INC: return "++"; case TK_DEC: return "--";
        case TK_ELLIPSIS: return "..."; case TK_FRAME_MACRO: return "$frame";
        case TK_EOF: return "<EOF>";
        default: { static char buf[4]={'\'',0,'\'',0}; if(t>0&&t<256){buf[1]=(char)t;return buf;} return "<?>"; }
    }
}
