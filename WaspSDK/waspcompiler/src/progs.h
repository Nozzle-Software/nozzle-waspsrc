/*
 * WaspCompiler - QuakeC Compiler
 * progs.h - QuakeC bytecode (progs.dat) binary format
 *
 * Based on id Software's QuakeC specification.
 * Compatible with standard Quake 1 engine.
 */
#pragma once
#include <stdint.h>

#define PROG_VERSION 6

/* Standard global offsets */
#define OFS_NULL        0
#define OFS_RETURN      1
#define OFS_PARM0       4
#define OFS_PARM1       7
#define OFS_PARM2       10
#define OFS_PARM3       13
#define OFS_PARM4       16
#define OFS_PARM5       19
#define OFS_PARM6       22
#define OFS_PARM7       25
#define RESERVED_OFS    28

#define MAX_PARMS       8
#define MAX_LOCAL_VARS  256

/* Entity type index - save bit */
#define DEF_SAVEGLOBAL  (1u << 15)

/* QuakeC type identifiers */
typedef enum {
    ev_void     = 0,
    ev_string   = 1,
    ev_float    = 2,
    ev_vector   = 3,
    ev_entity   = 4,
    ev_field    = 5,
    ev_function = 6,
    ev_pointer  = 7,
    ev_bad      = 8
} etype_t;

/* QuakeC opcodes */
typedef enum {
    OP_DONE     = 0,
    OP_MUL_F    = 1,
    OP_MUL_V    = 2,
    OP_MUL_FV   = 3,
    OP_MUL_VF   = 4,
    OP_DIV_F    = 5,
    OP_ADD_F    = 6,
    OP_ADD_V    = 7,
    OP_SUB_F    = 8,
    OP_SUB_V    = 9,
    OP_EQ_F     = 10,
    OP_EQ_V     = 11,
    OP_EQ_S     = 12,
    OP_EQ_E     = 13,
    OP_EQ_FNC   = 14,
    OP_NE_F     = 15,
    OP_NE_V     = 16,
    OP_NE_S     = 17,
    OP_NE_E     = 18,
    OP_NE_FNC   = 19,
    OP_LE       = 20,
    OP_GE       = 21,
    OP_LT       = 22,
    OP_GT       = 23,
    OP_LOAD_F   = 24,
    OP_LOAD_V   = 25,
    OP_LOAD_S   = 26,
    OP_LOAD_ENT = 27,
    OP_LOAD_FLD = 28,
    OP_LOAD_FNC = 29,
    OP_ADDRESS  = 30,
    OP_STORE_F  = 31,
    OP_STORE_V  = 32,
    OP_STORE_S  = 33,
    OP_STORE_ENT= 34,
    OP_STORE_FLD= 35,
    OP_STORE_FNC= 36,
    OP_STOREP_F = 37,
    OP_STOREP_V = 38,
    OP_STOREP_S = 39,
    OP_STOREP_ENT=40,
    OP_STOREP_FLD=41,
    OP_STOREP_FNC=42,
    OP_RETURN   = 43,
    OP_NOT_F    = 44,
    OP_NOT_V    = 45,
    OP_NOT_S    = 46,
    OP_NOT_ENT  = 47,
    OP_NOT_FNC  = 48,
    OP_IF       = 49,
    OP_IFNOT    = 50,
    OP_CALL0    = 51,
    OP_CALL1    = 52,
    OP_CALL2    = 53,
    OP_CALL3    = 54,
    OP_CALL4    = 55,
    OP_CALL5    = 56,
    OP_CALL6    = 57,
    OP_CALL7    = 58,
    OP_CALL8    = 59,
    OP_STATE    = 60,
    OP_GOTO     = 61,
    OP_AND      = 62,
    OP_OR       = 63,
    OP_BITAND   = 64,
    OP_BITOR    = 65
} opcode_t;

#pragma pack(push, 1)

/* progs.dat file header */
typedef struct {
    int32_t version;
    int32_t crc;
    int32_t ofs_statements;
    int32_t numstatements;
    int32_t ofs_globaldefs;
    int32_t numglobaldefs;
    int32_t ofs_fielddefs;
    int32_t numfielddefs;
    int32_t ofs_functions;
    int32_t numfunctions;
    int32_t ofs_strings;
    int32_t numstrings;
    int32_t ofs_globals;
    int32_t numglobals;
    int32_t entityfields;
} dprograms_t;

/* A single bytecode instruction */
typedef struct {
    uint16_t op;
    int16_t  a, b, c;
} dstatement_t;

/* A global or field definition entry */
typedef struct {
    uint16_t type;    /* etype_t | DEF_SAVEGLOBAL */
    uint16_t ofs;     /* global or field offset */
    int32_t  s_name;  /* offset into string table */
} ddef_t;

/* A function entry */
typedef struct {
    int32_t  first_statement; /* negative = built-in index */
    int32_t  parm_start;      /* offset in globals for params */
    int32_t  locals;          /* number of local float slots */
    int32_t  profile;         /* profiling counter (0) */
    int32_t  s_name;          /* function name in string table */
    int32_t  s_file;          /* source file in string table */
    int32_t  numparms;
    uint8_t  parm_size[8];    /* size in floats of each parm */
} dfunction_t;

#pragma pack(pop)

/* Returns the float-slot size of a type */
static inline int type_size(etype_t t) {
    return (t == ev_vector) ? 3 : 1;
}

/* Name of type for diagnostics */
static inline const char *type_name(etype_t t) {
    switch (t) {
        case ev_void:     return "void";
        case ev_string:   return "string";
        case ev_float:    return "float";
        case ev_vector:   return "vector";
        case ev_entity:   return "entity";
        case ev_field:    return "field";
        case ev_function: return "function";
        case ev_pointer:  return "pointer";
        default:          return "bad";
    }
}
