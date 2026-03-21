/*
 * WaspCompiler - QuakeC Compiler
 * codegen.c - Bytecode code generator
 *
 * Walks the AST and emits QuakeC bytecode in the progs.dat format.
 */
#include "codegen.h"
#include "lexer.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * String table
 * -------------------------------------------------------------------------*/
int strtab_add(StringTable *st, const char *s) {
    if (!s) s = "";
    int ofs = st->size;
    int len = (int)strlen(s) + 1;
    while (st->size + len > st->cap) {
        st->cap = st->cap ? st->cap * 2 : 4096;
        st->data = (char *)wasp_realloc(st->data, st->cap);
    }
    memcpy(st->data + st->size, s, len);
    st->size += len;
    return ofs;
}

const char *strtab_get(StringTable *st, int ofs) {
    if (ofs < 0 || ofs >= st->size) return "";
    return st->data + ofs;
}

void strtab_free(StringTable *st) {
    free(st->data);
    st->data = NULL; st->size = st->cap = 0;
}

/* -------------------------------------------------------------------------
 * CodeGen helpers
 * -------------------------------------------------------------------------*/
static int emit(CodeGen *cg, opcode_t op, int a, int b, int c) {
    dstatement_t s;
    s.op = (uint16_t)op;
    s.a  = (int16_t)a;
    s.b  = (int16_t)b;
    s.c  = (int16_t)c;
    int idx = cg->num_statements;
    DA_PUSH(cg->statements, cg->num_statements, cg->cap_statements, s);
    return idx;
}

/* Emit a placeholder jump, return its index for patching */
static int emit_jump(CodeGen *cg, opcode_t op, int a) {
    return emit(cg, op, a, 0, 0);
}

/* Patch a jump instruction at idx to target */
static void patch_jump(CodeGen *cg, int idx, int target) {
    /* GOTO uses c for offset; IF/IFNOT use b */
    opcode_t op = (opcode_t)cg->statements[idx].op;
    if (op == OP_GOTO) {
        cg->statements[idx].a = (int16_t)(target - idx);
    } else {
        cg->statements[idx].b = (int16_t)(target - idx);
    }
}

/* Allocate a global slot of given size, return offset */
static int alloc_global(CodeGen *cg, int size) {
    int ofs = cg->num_globals;
    while (cg->num_globals + size > cg->cap_globals) {
        cg->cap_globals = cg->cap_globals ? cg->cap_globals * 2 : 1024;
        cg->globals = (float *)wasp_realloc(cg->globals,
                                             cg->cap_globals * sizeof(float));
    }
    memset(cg->globals + cg->num_globals, 0, size * sizeof(float));
    cg->num_globals += size;
    return ofs;
}

/* Allocate a local variable slot (above parm_start) */
static int alloc_local(CodeGen *cg, int size) {
    int ofs = cg->local_base + cg->temp_ofs;
    cg->temp_ofs += size;
    if (cg->temp_ofs > cg->temp_high)
        cg->temp_high = cg->temp_ofs;
    return ofs;
}

/* Get a temporary slot */
static int temp_alloc(CodeGen *cg, int size) {
    return alloc_local(cg, size);
}

/* Store a float constant in the globals, return its offset */
static int const_float(CodeGen *cg, float v) {
    /* Look for existing constant */
    for (int i = RESERVED_OFS; i < cg->num_globals; i++) {
        if (cg->globals[i] == v) {
            /* Make sure it's not a variable slot (tricky - just alloc new) */
        }
    }
    int ofs = alloc_global(cg, 1);
    cg->globals[ofs] = v;
    return ofs;
}

/* Store a string constant - returns global offset containing the string index */
static int const_string(CodeGen *cg, const char *s) {
    int str_ofs = strtab_add(&cg->strtab, s);
    int ofs = alloc_global(cg, 1);
    /* Store the string table offset as a float (raw bits) */
    memcpy(&cg->globals[ofs], &str_ofs, sizeof(int));
    return ofs;
}

/* -------------------------------------------------------------------------
 * Define a global variable symbol and allocate global storage
 * -------------------------------------------------------------------------*/
static Symbol *define_global_var(CodeGen *cg, const char *name,
                                  TypeInfo *type, etype_t base_type) {
    Symbol *sym = symtab_define(cg->symtab, name, SYM_GLOBAL, type);
    int size = type_size(base_type);
    int ofs  = alloc_global(cg, size);
    sym->global_ofs = ofs;

    /* Add to globaldef list */
    ddef_t def;
    def.type   = (uint16_t)(base_type | DEF_SAVEGLOBAL);
    def.ofs    = (uint16_t)ofs;
    def.s_name = strtab_add(&cg->strtab, name);
    DA_PUSH(cg->globaldefs, cg->num_globaldefs, cg->cap_globaldefs, def);

    return sym;
}

/* -------------------------------------------------------------------------
 * Expression code generation
 * Returns the global offset where the result is stored
 * -------------------------------------------------------------------------*/
typedef struct {
    int      ofs;     /* offset in globals */
    etype_t  type;    /* result type */
    bool     is_lval; /* is this a left-value (can be assigned to) */
} ExprResult;

static ExprResult gen_expr(CodeGen *cg, AstNode *node);
static void gen_stmt(CodeGen *cg, AstNode *node);

static ExprResult make_result(int ofs, etype_t type, bool is_lval) {
    ExprResult r; r.ofs = ofs; r.type = type; r.is_lval = is_lval;
    return r;
}

/* Get the appropriate STORE opcode for a type */
static opcode_t store_op(etype_t t) {
    switch (t) {
        case ev_float:    return OP_STORE_F;
        case ev_vector:   return OP_STORE_V;
        case ev_string:   return OP_STORE_S;
        case ev_entity:   return OP_STORE_ENT;
        case ev_field:    return OP_STORE_FLD;
        case ev_function: return OP_STORE_FNC;
        default:          return OP_STORE_F;
    }
}

/* Get the appropriate STOREP opcode */
static opcode_t storep_op(etype_t t) {
    switch (t) {
        case ev_float:    return OP_STOREP_F;
        case ev_vector:   return OP_STOREP_V;
        case ev_string:   return OP_STOREP_S;
        case ev_entity:   return OP_STOREP_ENT;
        case ev_field:    return OP_STOREP_FLD;
        case ev_function: return OP_STOREP_FNC;
        default:          return OP_STOREP_F;
    }
}

/* Get the appropriate LOAD opcode */
static opcode_t load_op(etype_t t) {
    switch (t) {
        case ev_float:    return OP_LOAD_F;
        case ev_vector:   return OP_LOAD_V;
        case ev_string:   return OP_LOAD_S;
        case ev_entity:   return OP_LOAD_ENT;
        case ev_field:    return OP_LOAD_FLD;
        case ev_function: return OP_LOAD_FNC;
        default:          return OP_LOAD_F;
    }
}

/* Determine result type of a binary expression */
static etype_t binary_result_type(int op, etype_t lt, etype_t rt) {
    switch (op) {
        case '+':
            if (lt == ev_vector && rt == ev_vector) return ev_vector;
            return ev_float;
        case '-':
            if (lt == ev_vector && rt == ev_vector) return ev_vector;
            return ev_float;
        case '*':
            if (lt == ev_vector && rt == ev_vector) return ev_float; /* dot product */
            if (lt == ev_float  && rt == ev_vector) return ev_vector;
            if (lt == ev_vector && rt == ev_float)  return ev_vector;
            return ev_float;
        case '/':    return ev_float;
        case TK_EQ: case TK_NE:
        case '<': case '>': case TK_LE: case TK_GE:
        case TK_AND: case TK_OR:
        case '&': case '|':
            return ev_float;
        default: return ev_float;
    }
}

/* Get opcode for binary operation */
static opcode_t binary_opcode(int op, etype_t lt, etype_t rt) {
    switch (op) {
        case '+':
            if (lt == ev_vector) return OP_ADD_V;
            return OP_ADD_F;
        case '-':
            if (lt == ev_vector) return OP_SUB_V;
            return OP_SUB_F;
        case '*':
            if (lt == ev_vector && rt == ev_vector) return OP_MUL_V;
            if (lt == ev_float  && rt == ev_vector) return OP_MUL_FV;
            if (lt == ev_vector && rt == ev_float)  return OP_MUL_VF;
            return OP_MUL_F;
        case '/':    return OP_DIV_F;
        case TK_EQ:
            switch (lt) {
                case ev_vector:   return OP_EQ_V;
                case ev_string:   return OP_EQ_S;
                case ev_entity:   return OP_EQ_E;
                case ev_function: return OP_EQ_FNC;
                default:          return OP_EQ_F;
            }
        case TK_NE:
            switch (lt) {
                case ev_vector:   return OP_NE_V;
                case ev_string:   return OP_NE_S;
                case ev_entity:   return OP_NE_E;
                case ev_function: return OP_NE_FNC;
                default:          return OP_NE_F;
            }
        case '<':    return OP_LT;
        case '>':    return OP_GT;
        case TK_LE:  return OP_LE;
        case TK_GE:  return OP_GE;
        case TK_AND: return OP_AND;
        case TK_OR:  return OP_OR;
        case '&':    return OP_BITAND;
        case '|':    return OP_BITOR;
        default:     return OP_ADD_F;
    }
}

static ExprResult gen_expr(CodeGen *cg, AstNode *node) {
    if (!node) return make_result(OFS_NULL, ev_void, false);

    switch (node->kind) {

    case AST_NUMBER: {
        float v = (float)node->num_val;
        if (v == 0.0f) return make_result(OFS_NULL, ev_float, false);
        int ofs = const_float(cg, v);
        return make_result(ofs, ev_float, false);
    }

    case AST_VECTOR_LIT: {
        int ofs = alloc_global(cg, 3);
        cg->globals[ofs]   = node->vec_val[0];
        cg->globals[ofs+1] = node->vec_val[1];
        cg->globals[ofs+2] = node->vec_val[2];
        return make_result(ofs, ev_vector, false);
    }

    case AST_STRING_LIT: {
        int ofs = const_string(cg, node->str_val);
        return make_result(ofs, ev_string, false);
    }

    case AST_IDENT: {
        Symbol *sym = symtab_lookup(cg->symtab, node->ident);
        if (!sym) {
            wasp_error(node->file, node->line,
                       "undefined identifier '%s'", node->ident);
            return make_result(OFS_NULL, ev_float, false);
        }
        switch (sym->kind) {
            case SYM_GLOBAL:
            case SYM_LOCAL:
            case SYM_PARAM:
                return make_result(sym->global_ofs,
                                   sym->type ? sym->type->kind : ev_float, true);
            case SYM_CONST: {
                float v = (float)sym->const_val;
                if (v == 0.0f) return make_result(OFS_NULL, ev_float, false);
                int ofs = const_float(cg, v);
                return make_result(ofs, ev_float, false);
            }
            case SYM_FUNCTION:
            case SYM_BUILTIN:
                return make_result(sym->global_ofs, ev_function, false);
            case SYM_FIELD: {
                /* Field reference - returns a float containing the field offset */
                int ofs = const_float(cg, (float)sym->field_ofs);
                return make_result(ofs, ev_field, false);
            }
        }
        return make_result(OFS_NULL, ev_float, false);
    }

    case AST_FIELD_ACCESS: {
        /* entity.field */
        ExprResult obj = gen_expr(cg, node->field_access.object);
        const char *fname = node->field_access.field;

        /* Special: vector component access */
        if (strcmp(fname, "x") == 0) {
            return make_result(obj.ofs, ev_float, obj.is_lval);
        }
        if (strcmp(fname, "y") == 0) {
            return make_result(obj.ofs + 1, ev_float, obj.is_lval);
        }
        if (strcmp(fname, "z") == 0) {
            return make_result(obj.ofs + 2, ev_float, obj.is_lval);
        }

        /* Look up field */
        Symbol *fsym = symtab_lookup(cg->symtab, fname);
        if (!fsym || fsym->kind != SYM_FIELD) {
            wasp_error(node->file, node->line,
                       "unknown field '%s'", fname);
            return make_result(OFS_NULL, ev_float, false);
        }

        etype_t field_type = fsym->type && fsym->type->field_type ?
                             fsym->type->field_type->kind : ev_float;

        /* Get field offset as a constant */
        int fofs_slot = const_float(cg, (float)fsym->field_ofs);

        /* Emit LOAD_F/LOAD_V/etc. : ent.field -> result */
        int result = temp_alloc(cg, type_size(field_type));
        emit(cg, load_op(field_type), obj.ofs, fofs_slot, result);
        return make_result(result, field_type, false);
    }

    case AST_UNARY: {
        switch (node->unary.op) {
            case '-': {
                ExprResult operand = gen_expr(cg, node->unary.operand);
                int result_size = type_size(operand.type);
                int result = temp_alloc(cg, result_size);
                if (operand.type == ev_vector) {
                    emit(cg, OP_NOT_V, OFS_NULL, 0, result); /* workaround: sub from 0 */
                    /* Better: negate manually */
                    int neg1 = const_float(cg, -1.0f);
                    emit(cg, OP_MUL_VF, operand.ofs, neg1, result);
                } else {
                    int zero = OFS_NULL;
                    emit(cg, OP_SUB_F, zero, operand.ofs, result);
                }
                return make_result(result, operand.type, false);
            }
            case '!': {
                ExprResult operand = gen_expr(cg, node->unary.operand);
                int result = temp_alloc(cg, 1);
                opcode_t not_op;
                switch (operand.type) {
                    case ev_vector:   not_op = OP_NOT_V; break;
                    case ev_string:   not_op = OP_NOT_S; break;
                    case ev_entity:   not_op = OP_NOT_ENT; break;
                    case ev_function: not_op = OP_NOT_FNC; break;
                    default:          not_op = OP_NOT_F; break;
                }
                emit(cg, not_op, operand.ofs, 0, result);
                return make_result(result, ev_float, false);
            }
            case 'P': /* post-increment */ {
                ExprResult lval = gen_expr(cg, node->unary.operand);
                int result = temp_alloc(cg, 1);
                emit(cg, OP_STORE_F, lval.ofs, result, 0); /* save old value */
                int one = const_float(cg, 1.0f);
                int tmp  = temp_alloc(cg, 1);
                emit(cg, OP_ADD_F, lval.ofs, one, tmp);
                emit(cg, OP_STORE_F, tmp, lval.ofs, 0);
                return make_result(result, ev_float, false);
            }
            case 'Q': /* post-decrement */ {
                ExprResult lval = gen_expr(cg, node->unary.operand);
                int result = temp_alloc(cg, 1);
                emit(cg, OP_STORE_F, lval.ofs, result, 0);
                int one = const_float(cg, 1.0f);
                int tmp  = temp_alloc(cg, 1);
                emit(cg, OP_SUB_F, lval.ofs, one, tmp);
                emit(cg, OP_STORE_F, tmp, lval.ofs, 0);
                return make_result(result, ev_float, false);
            }
        }
        return make_result(OFS_NULL, ev_float, false);
    }

    case AST_BINARY: {
        ExprResult left  = gen_expr(cg, node->binary.left);
        ExprResult right = gen_expr(cg, node->binary.right);
        etype_t res_type = binary_result_type(node->binary.op, left.type, right.type);
        int result = temp_alloc(cg, type_size(res_type));
        opcode_t opc = binary_opcode(node->binary.op, left.type, right.type);
        emit(cg, opc, left.ofs, right.ofs, result);
        return make_result(result, res_type, false);
    }

    case AST_ASSIGN: {
        ExprResult rval = gen_expr(cg, node->assign.value);

        /* For compound assignment, compute lhs + rhs first */
        ExprResult lval = gen_expr(cg, node->assign.target);

        if (node->assign.op != '=') {
            int tmp = temp_alloc(cg, type_size(lval.type));
            int op2;
            switch (node->assign.op) {
                case TK_PLUSEQ:  op2 = '+'; break;
                case TK_MINUSEQ: op2 = '-'; break;
                case TK_MULEQ:   op2 = '*'; break;
                case TK_DIVEQ:   op2 = '/'; break;
                default:         op2 = '+'; break;
            }
            opcode_t opc = binary_opcode(op2, lval.type, rval.type);
            emit(cg, opc, lval.ofs, rval.ofs, tmp);
            rval.ofs  = tmp;
            rval.type = lval.type;
        }

        /* Field access assignment (STOREP) */
        if (node->assign.target->kind == AST_FIELD_ACCESS) {
            /* We need to ADDRESS the field then STOREP */
            AstNode *fa = node->assign.target;
            const char *fname = fa->field_access.field;

            /* Skip x/y/z - handled as direct global */
            if (strcmp(fname, "x") != 0 && strcmp(fname, "y") != 0 &&
                strcmp(fname, "z") != 0) {
                Symbol *fsym = symtab_lookup(cg->symtab, fname);
                if (fsym && fsym->kind == SYM_FIELD) {
                    ExprResult obj = gen_expr(cg, fa->field_access.object);
                    int fofs_slot = const_float(cg, (float)fsym->field_ofs);
                    int addr = temp_alloc(cg, 1);
                    emit(cg, OP_ADDRESS, obj.ofs, fofs_slot, addr);
                    etype_t ft = fsym->type && fsym->type->field_type ?
                                 fsym->type->field_type->kind : ev_float;
                    emit(cg, storep_op(ft), rval.ofs, addr, 0);
                    return rval;
                }
            }
        }

        /* Direct store */
        emit(cg, store_op(lval.type), rval.ofs, lval.ofs, 0);
        return rval;
    }

    case AST_CALL: {
        /* Evaluate function */
        ExprResult func = gen_expr(cg, node->call.func);

        /* Determine number of arguments */
        int nargs = node->call.num_args;
        if (nargs > MAX_PARMS) {
            wasp_error(node->file, node->line,
                       "too many arguments to function call (max %d)", MAX_PARMS);
            nargs = MAX_PARMS;
        }

        /* Evaluate arguments and copy to OFS_PARM slots */
        static const int parm_ofs[] = {
            OFS_PARM0, OFS_PARM1, OFS_PARM2, OFS_PARM3,
            OFS_PARM4, OFS_PARM5, OFS_PARM6, OFS_PARM7
        };

        for (int i = 0; i < nargs; i++) {
            ExprResult arg = gen_expr(cg, node->call.args[i]);
            int dest = parm_ofs[i];
            emit(cg, store_op(arg.type), arg.ofs, dest, 0);
        }

        /* Emit CALLn */
        opcode_t call_op = (opcode_t)(OP_CALL0 + nargs);
        emit(cg, call_op, func.ofs, 0, 0);

        /* Return value is in OFS_RETURN */
        /* Determine return type from function type */
        etype_t ret_type = ev_void;
        if (node->call.func->kind == AST_IDENT) {
            Symbol *sym = symtab_lookup(cg->symtab, node->call.func->ident);
            if (sym && sym->type && sym->type->ret_type)
                ret_type = sym->type->ret_type->kind;
        }

        if (ret_type == ev_void) {
            return make_result(OFS_RETURN, ev_void, false);
        }

        /* Copy return value to a temp */
        int result = temp_alloc(cg, type_size(ret_type));
        emit(cg, store_op(ret_type), OFS_RETURN, result, 0);
        return make_result(result, ret_type, false);
    }

    case AST_ADDRESS: {
        ExprResult operand = gen_expr(cg, node->address.operand);
        int result = temp_alloc(cg, 1);
        /* For entity fields, ADDRESS ent, field -> ptr */
        /* For simple globals, just return the offset as a pointer */
        int ofs_slot = const_float(cg, (float)operand.ofs);
        emit(cg, OP_ADDRESS, operand.ofs, ofs_slot, result);
        return make_result(result, ev_pointer, false);
    }

    default:
        wasp_error(node->file, node->line,
                   "unhandled expression type %d", node->kind);
        return make_result(OFS_NULL, ev_void, false);
    }
}

/* -------------------------------------------------------------------------
 * Statement code generation
 * -------------------------------------------------------------------------*/
static void gen_stmt(CodeGen *cg, AstNode *node) {
    if (!node) return;

    switch (node->kind) {
    case AST_BLOCK:
        for (int i = 0; i < node->block.num_stmts; i++)
            gen_stmt(cg, node->block.stmts[i]);
        break;

    case AST_EXPR_STMT:
        gen_expr(cg, node->local_decl.expr);
        /* Reset temp_ofs to reclaim temporaries after each statement */
        /* (simplified: keep all locals active) */
        break;

    case AST_LOCAL_DECL: {
        /* Allocate a local slot */
        TypeInfo *dtype = node->local_decl.decl_type;
        etype_t ekind   = dtype ? dtype->kind : ev_float;
        int size        = type_size(ekind);
        int ofs         = alloc_local(cg, size);

        Symbol *sym = symtab_define(cg->symtab,
                                     node->local_decl.name,
                                     SYM_LOCAL, dtype);
        sym->global_ofs = ofs;

        if (node->local_decl.init) {
            ExprResult init = gen_expr(cg, node->local_decl.init);
            emit(cg, store_op(ekind), init.ofs, ofs, 0);
        }
        break;
    }

    case AST_IF: {
        ExprResult cond = gen_expr(cg, node->if_stmt.cond);
        /* IFNOT cond, jump_to_else */
        int ifnot_idx = emit_jump(cg, OP_IFNOT, cond.ofs);

        gen_stmt(cg, node->if_stmt.then_branch);

        if (node->if_stmt.else_branch) {
            int goto_idx = emit_jump(cg, OP_GOTO, 0);
            patch_jump(cg, ifnot_idx, cg->num_statements);
            gen_stmt(cg, node->if_stmt.else_branch);
            patch_jump(cg, goto_idx, cg->num_statements);
        } else {
            patch_jump(cg, ifnot_idx, cg->num_statements);
        }
        break;
    }

    case AST_WHILE: {
        int loop_start = cg->num_statements;
        ExprResult cond = gen_expr(cg, node->loop.cond);
        int exit_jump = emit_jump(cg, OP_IFNOT, cond.ofs);

        /* Save break/continue state */
        int saved_bp = cg->num_break_patches;
        int saved_cp = cg->num_continue_patches;

        gen_stmt(cg, node->loop.body);

        /* continue jumps here (re-test condition) */
        for (int i = saved_cp; i < cg->num_continue_patches; i++)
            patch_jump(cg, cg->continue_patches[i], loop_start);
        cg->num_continue_patches = saved_cp;

        /* Jump back to condition */
        emit(cg, OP_GOTO, 0, 0, (int16_t)(loop_start - cg->num_statements));
        int after = cg->num_statements;
        patch_jump(cg, exit_jump, after);

        /* Patch break statements */
        for (int i = saved_bp; i < cg->num_break_patches; i++)
            patch_jump(cg, cg->break_patches[i], after);
        cg->num_break_patches = saved_bp;
        break;
    }

    case AST_DO_WHILE: {
        int loop_start = cg->num_statements;
        int saved_bp = cg->num_break_patches;
        int saved_cp = cg->num_continue_patches;

        gen_stmt(cg, node->loop.body);

        /* continue target */
        int cond_start = cg->num_statements;
        for (int i = saved_cp; i < cg->num_continue_patches; i++)
            patch_jump(cg, cg->continue_patches[i], cond_start);
        cg->num_continue_patches = saved_cp;

        ExprResult cond = gen_expr(cg, node->loop.cond);
        emit(cg, OP_IF, cond.ofs, 0, (int16_t)(loop_start - cg->num_statements));

        int after = cg->num_statements;
        for (int i = saved_bp; i < cg->num_break_patches; i++)
            patch_jump(cg, cg->break_patches[i], after);
        cg->num_break_patches = saved_bp;
        break;
    }

    case AST_FOR: {
        if (node->for_stmt.init) gen_stmt(cg, node->for_stmt.init);
        int loop_start = cg->num_statements;
        int exit_jump = -1;

        if (node->for_stmt.cond) {
            ExprResult cond = gen_expr(cg, node->for_stmt.cond);
            exit_jump = emit_jump(cg, OP_IFNOT, cond.ofs);
        }

        int saved_bp = cg->num_break_patches;
        int saved_cp = cg->num_continue_patches;

        gen_stmt(cg, node->for_stmt.body);

        int cont_target = cg->num_statements;
        for (int i = saved_cp; i < cg->num_continue_patches; i++)
            patch_jump(cg, cg->continue_patches[i], cont_target);
        cg->num_continue_patches = saved_cp;

        if (node->for_stmt.post) gen_expr(cg, node->for_stmt.post);

        emit(cg, OP_GOTO, 0, 0, (int16_t)(loop_start - cg->num_statements));
        int after = cg->num_statements;
        if (exit_jump >= 0) patch_jump(cg, exit_jump, after);

        for (int i = saved_bp; i < cg->num_break_patches; i++)
            patch_jump(cg, cg->break_patches[i], after);
        cg->num_break_patches = saved_bp;
        break;
    }

    case AST_RETURN: {
        if (node->ret.value) {
            ExprResult val = gen_expr(cg, node->ret.value);
            emit(cg, store_op(val.type), val.ofs, OFS_RETURN, 0);
        }
        emit(cg, OP_RETURN, 0, 0, 0);
        break;
    }

    case AST_BREAK: {
        int idx = emit_jump(cg, OP_GOTO, 0);
        DA_PUSH(cg->break_patches, cg->num_break_patches,
                cg->cap_break_patches, idx);
        break;
    }

    case AST_CONTINUE: {
        int idx = emit_jump(cg, OP_GOTO, 0);
        DA_PUSH(cg->continue_patches, cg->num_continue_patches,
                cg->cap_continue_patches, idx);
        break;
    }

    default:
        wasp_error(node->file, node->line,
                   "unhandled statement type %d", node->kind);
        break;
    }
}

/* -------------------------------------------------------------------------
 * Top-level declaration processing
 * -------------------------------------------------------------------------*/
static void process_field_decl(CodeGen *cg, AstNode *node) {
    const char *name   = node->field_decl.name;
    TypeInfo   *ftype  = node->field_decl.field_type;
    etype_t     ekind  = ftype ? ftype->kind : ev_float;
    int         fsize  = type_size(ekind);

    int fofs = cg->num_fields;
    cg->num_fields += fsize;

    Symbol *sym = symtab_define(cg->symtab, name, SYM_FIELD,
                                 type_field(ftype));
    sym->field_ofs = fofs;

    ddef_t def;
    def.type   = (uint16_t)ev_field;
    def.ofs    = (uint16_t)fofs;
    def.s_name = strtab_add(&cg->strtab, name);
    DA_PUSH(cg->fielddefs, cg->num_fielddefs, cg->cap_fielddefs, def);
}

static void process_var_decl(CodeGen *cg, AstNode *node) {
    const char *name  = node->var_decl.name;
    TypeInfo   *vtype = node->var_decl.var_type;
    etype_t     ekind = vtype ? vtype->kind : ev_float;

    /* Check for forward declaration of function type */
    if (ekind == ev_function) {
        /* Ensure a function entry exists */
        Symbol *existing = symtab_lookup(cg->symtab, name);
        if (existing) return; /* already declared */
        Symbol *sym = define_global_var(cg, name, vtype, ev_function);
        sym->func_index = -1;
        (void)sym;
        return;
    }

    /* Constant: if initializer is a number, store as constant */
    if (node->var_decl.init && node->var_decl.init->kind == AST_NUMBER &&
        ekind == ev_float) {
        Symbol *sym = symtab_define(cg->symtab, name, SYM_CONST, vtype);
        sym->const_val = node->var_decl.init->num_val;
        /* Also allocate a global slot for it (needed for address-of etc.) */
        int ofs = alloc_global(cg, 1);
        cg->globals[ofs] = (float)sym->const_val;
        sym->global_ofs = ofs;
        ddef_t def;
        def.type   = (uint16_t)ekind;
        def.ofs    = (uint16_t)ofs;
        def.s_name = strtab_add(&cg->strtab, name);
        DA_PUSH(cg->globaldefs, cg->num_globaldefs, cg->cap_globaldefs, def);
        return;
    }

    Symbol *sym = define_global_var(cg, name, vtype, ekind);

    /* Initialize */
    if (node->var_decl.init) {
        switch (node->var_decl.init->kind) {
            case AST_NUMBER:
                cg->globals[sym->global_ofs] = (float)node->var_decl.init->num_val;
                break;
            case AST_VECTOR_LIT:
                cg->globals[sym->global_ofs]   = node->var_decl.init->vec_val[0];
                cg->globals[sym->global_ofs+1] = node->var_decl.init->vec_val[1];
                cg->globals[sym->global_ofs+2] = node->var_decl.init->vec_val[2];
                break;
            case AST_STRING_LIT: {
                int sofs = strtab_add(&cg->strtab, node->var_decl.init->str_val);
                memcpy(&cg->globals[sym->global_ofs], &sofs, sizeof(int));
                break;
            }
            default:
                break;
        }
    }
}

static void process_func_decl(CodeGen *cg, AstNode *node) {
    const char  *name      = node->func_decl.name;
    TypeInfo    *func_type = node->func_decl.func_type;

    /* Check if already defined (forward decl) */
    Symbol *sym = symtab_lookup_local(cg->symtab, name);
    if (!sym) {
        sym = define_global_var(cg, name, func_type, ev_function);
    }

    if (node->func_decl.is_builtin) {
        /* Built-in: create function entry with negative first_statement */
        dfunction_t fn;
        memset(&fn, 0, sizeof(fn));
        fn.first_statement = -(node->func_decl.builtin_id);
        fn.parm_start      = RESERVED_OFS;
        fn.locals          = 0;
        fn.profile         = 0;
        fn.s_name          = strtab_add(&cg->strtab, name);
        fn.s_file          = cg->s_file;
        fn.numparms        = func_type ? func_type->num_parms : 0;
        for (int i = 0; i < fn.numparms; i++) {
            TypeInfo *pt = func_type->parm_types[i];
            fn.parm_size[i] = (uint8_t)(pt ? type_size(pt->kind) : 1);
        }

        int fi = cg->num_functions;
        DA_PUSH(cg->functions, cg->num_functions, cg->cap_functions, fn);
        sym->func_index = fi;

        /* Store function index in global slot */
        cg->globals[sym->global_ofs] = (float)(fi + 1); /* 1-based in progs */
        return;
    }

    if (!node->func_decl.body) {
        /* Forward declaration only - create placeholder function */
        dfunction_t fn;
        memset(&fn, 0, sizeof(fn));
        fn.first_statement = 0;
        fn.parm_start      = RESERVED_OFS;
        fn.locals          = 0;
        fn.s_name          = strtab_add(&cg->strtab, name);
        fn.s_file          = cg->s_file;
        fn.numparms        = func_type ? func_type->num_parms : 0;
        for (int i = 0; i < fn.numparms; i++) {
            TypeInfo *pt = func_type->parm_types[i];
            fn.parm_size[i] = (uint8_t)(pt ? type_size(pt->kind) : 1);
        }
        int fi = cg->num_functions;
        DA_PUSH(cg->functions, cg->num_functions, cg->cap_functions, fn);
        sym->func_index = fi;
        cg->globals[sym->global_ofs] = (float)(fi + 1);
        return;
    }

    /* Full function definition */
    /* Save state */
    int saved_temp_ofs  = cg->temp_ofs;
    int saved_temp_high = cg->temp_high;
    const char *saved_func = cg->cur_func_name;

    cg->cur_func_name = name;
    cg->temp_high     = 0;

    /* Set up parameter slots */
    int parm_start = RESERVED_OFS;
    int nparms = func_type ? func_type->num_parms : 0;

    /* Parameters occupy the OFS_PARM slots */
    symtab_push_scope(cg->symtab);

    static const int parm_ofs[] = {
        OFS_PARM0, OFS_PARM1, OFS_PARM2, OFS_PARM3,
        OFS_PARM4, OFS_PARM5, OFS_PARM6, OFS_PARM7
    };

    for (int i = 0; i < nparms; i++) {
        TypeInfo *pt = func_type->parm_types[i];
        const char *pname = (func_type->parm_names && func_type->parm_names[i])
                            ? func_type->parm_names[i] : NULL;
        if (pname) {
            Symbol *psym = symtab_define(cg->symtab, pname, SYM_PARAM, pt);
            psym->global_ofs = parm_ofs[i];
        }
    }

    /* Locals start above the parameters area, after RESERVED_OFS + parm space */
    /* Actually, locals live in the locals area which is above the parm_start */
    /* We allocate locals in the globals area dynamically */
    int local_start = cg->num_globals;
    cg->local_base  = local_start;
    cg->temp_ofs    = 0;
    cg->temp_high   = 0;

    /* Emit function body */
    int first_stmt = cg->num_statements;

    gen_stmt(cg, node->func_decl.body);

    /* Ensure function ends with a RETURN */
    if (cg->num_statements == first_stmt ||
        cg->statements[cg->num_statements - 1].op != OP_RETURN) {
        emit(cg, OP_RETURN, 0, 0, 0);
    }

    int num_locals = cg->temp_high;

    /* Ensure local space is allocated in globals */
    if (local_start + num_locals > cg->num_globals) {
        alloc_global(cg, local_start + num_locals - cg->num_globals);
    }

    symtab_pop_scope(cg->symtab);

    /* Build function entry */
    dfunction_t fn;
    memset(&fn, 0, sizeof(fn));
    fn.first_statement = first_stmt;
    fn.parm_start      = parm_start;
    fn.locals          = num_locals;
    fn.profile         = 0;
    fn.s_name          = strtab_add(&cg->strtab, name);
    fn.s_file          = cg->s_file;
    fn.numparms        = nparms;
    for (int i = 0; i < nparms; i++) {
        TypeInfo *pt = func_type ? func_type->parm_types[i] : NULL;
        fn.parm_size[i] = (uint8_t)(pt ? type_size(pt->kind) : 1);
    }

    /* Update existing function entry if forward-declared */
    int fi;
    if (sym->func_index >= 0 && sym->func_index < cg->num_functions) {
        fi = sym->func_index;
        cg->functions[fi] = fn;
    } else {
        fi = cg->num_functions;
        DA_PUSH(cg->functions, cg->num_functions, cg->cap_functions, fn);
        sym->func_index = fi;
    }

    cg->globals[sym->global_ofs] = (float)(fi + 1); /* 1-based */

    /* Restore state */
    cg->temp_ofs      = saved_temp_ofs;
    cg->temp_high     = saved_temp_high;
    cg->cur_func_name = saved_func;
}

/* -------------------------------------------------------------------------
 * Main compile entry point
 * -------------------------------------------------------------------------*/
bool codegen_compile(CodeGen *cg, AstNode *program) {
    if (!program || program->kind != AST_PROGRAM) return false;

    /* Initialize null/return/parm slots */
    /* These are pre-allocated in RESERVED_OFS space */
    while (cg->num_globals < RESERVED_OFS)
        alloc_global(cg, 1);

    /* Register built-in constants */
    {
        /* TRUE / FALSE not needed as symbols - just 1.0 / 0.0 */
    }

    /* First pass: process all declarations (fields, vars, func protos) */
    /* This ensures forward references work */
    for (int i = 0; i < program->program.num_decls; i++) {
        AstNode *decl = program->program.decls[i];
        if (!decl) continue;
        switch (decl->kind) {
            case AST_FIELD_DECL:
                process_field_decl(cg, decl);
                break;
            case AST_VAR_DECL:
                process_var_decl(cg, decl);
                break;
            case AST_FUNC_DECL:
                if (!decl->func_decl.body || decl->func_decl.is_builtin) {
                    /* Forward decl or builtin */
                    process_func_decl(cg, decl);
                }
                break;
            default:
                break;
        }
    }

    /* Second pass: compile function bodies */
    for (int i = 0; i < program->program.num_decls; i++) {
        AstNode *decl = program->program.decls[i];
        if (!decl) continue;
        if (decl->kind == AST_FUNC_DECL &&
            decl->func_decl.body &&
            !decl->func_decl.is_builtin) {
            process_func_decl(cg, decl);
        }
    }

    return g_error_count == 0;
}

/* -------------------------------------------------------------------------
 * Write progs.dat
 * -------------------------------------------------------------------------*/
bool codegen_write(CodeGen *cg, const char *filename) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        wasp_fatal("cannot open output file '%s'", filename);
        return false;
    }

    /* Calculate file layout */
    int header_size = sizeof(dprograms_t);
    int ofs_statements = header_size;
    int ofs_globaldefs = ofs_statements + cg->num_statements * sizeof(dstatement_t);
    int ofs_fielddefs  = ofs_globaldefs + cg->num_globaldefs * sizeof(ddef_t);
    int ofs_functions  = ofs_fielddefs  + cg->num_fielddefs  * sizeof(ddef_t);
    int ofs_strings    = ofs_functions  + cg->num_functions  * sizeof(dfunction_t);
    int ofs_globals    = ofs_strings    + cg->strtab.size;
    /* Align to 4 bytes */
    ofs_globals = (ofs_globals + 3) & ~3;

    /* Build header */
    dprograms_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.version         = PROG_VERSION;
    hdr.crc             = 0; /* We don't compute CRC for now */
    hdr.ofs_statements  = ofs_statements;
    hdr.numstatements   = cg->num_statements;
    hdr.ofs_globaldefs  = ofs_globaldefs;
    hdr.numglobaldefs   = cg->num_globaldefs;
    hdr.ofs_fielddefs   = ofs_fielddefs;
    hdr.numfielddefs    = cg->num_fielddefs;
    hdr.ofs_functions   = ofs_functions;
    hdr.numfunctions    = cg->num_functions;
    hdr.ofs_strings     = ofs_strings;
    hdr.numstrings      = cg->strtab.size;
    hdr.ofs_globals     = ofs_globals;
    hdr.numglobals      = cg->num_globals;
    hdr.entityfields    = cg->num_fields;

    /* Write header */
    fwrite(&hdr, sizeof(hdr), 1, f);

    /* Write statements */
    fwrite(cg->statements, sizeof(dstatement_t), cg->num_statements, f);

    /* Write global defs */
    fwrite(cg->globaldefs, sizeof(ddef_t), cg->num_globaldefs, f);

    /* Write field defs */
    fwrite(cg->fielddefs, sizeof(ddef_t), cg->num_fielddefs, f);

    /* Write functions */
    fwrite(cg->functions, sizeof(dfunction_t), cg->num_functions, f);

    /* Write string table */
    fwrite(cg->strtab.data, 1, cg->strtab.size, f);

    /* Pad to alignment */
    {
        int padding = ofs_globals - (ofs_strings + cg->strtab.size);
        char zero = 0;
        for (int i = 0; i < padding; i++) fwrite(&zero, 1, 1, f);
    }

    /* Write globals */
    fwrite(cg->globals, sizeof(float), cg->num_globals, f);

    fclose(f);
    return true;
}

/* -------------------------------------------------------------------------
 * Constructor / Destructor
 * -------------------------------------------------------------------------*/
CodeGen *codegen_new(void) {
    CodeGen *cg = (CodeGen *)wasp_malloc(sizeof(CodeGen));
    cg->symtab = symtab_new();

    /* Initialize string table with empty string at offset 0 */
    strtab_add(&cg->strtab, "");

    /* Reserve NULL/RETURN/PARM space in globals (RESERVED_OFS = 28 slots) */
    /* These are initialized to 0 */
    cg->num_globals  = 0;
    cg->cap_globals  = 256;
    cg->globals      = (float *)wasp_malloc(cg->cap_globals * sizeof(float));
    /* Explicitly zero */
    memset(cg->globals, 0, cg->cap_globals * sizeof(float));

    cg->s_file       = strtab_add(&cg->strtab, "<unknown>");
    cg->cur_func_name = NULL;
    cg->temp_ofs     = 0;
    cg->temp_high    = 0;
    cg->local_base   = RESERVED_OFS;
    cg->num_fields   = 0;

    return cg;
}

void codegen_free(CodeGen *cg) {
    if (!cg) return;
    symtab_free(cg->symtab);
    strtab_free(&cg->strtab);
    free(cg->statements);
    free(cg->globaldefs);
    free(cg->fielddefs);
    free(cg->functions);
    free(cg->globals);
    free(cg->break_patches);
    free(cg->continue_patches);
    free(cg);
}
