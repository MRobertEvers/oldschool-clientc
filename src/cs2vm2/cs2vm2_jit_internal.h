#ifndef CS2VM2_JIT_INTERNAL_H
#define CS2VM2_JIT_INTERNAL_H

/*
 * VM-PRIVATE half of the load-time jit (cs2vm2_jit.h is the public half,
 * docs/CS2_JIT.md the design). Only cs2vm2.c, cs2vm2_jit.c and the VM's own
 * tests include this file; a host holds a script, never a struct CS2VM2_Jit.
 *
 * Everything here is static or a type, so cs2vm2.c can read an overlay
 * through struct fields alone and the standalone test links that compile
 * cs2vm2.c without cs2vm2_script.c / cs2vm2_jit.c keep linking.
 *
 * The overlay code numbering below is PART OF THE BLOB FORMAT. Changing a
 * number, a shape, or what a code means bumps CS2VM2_JIT_FORMAT_VERSION.
 */

#include "cs2_opcode.h"
#include "cs2vm2_script.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

/*
 * The default of a switch over canonical instructions, which no loaded script
 * can reach: CS2VM2_ScriptResolveDialect gives every script only instructions
 * below CS2_OP_PLAIN_COUNT (an id it cannot translate is UNDEFINED_WIRE_ID), CS2VM2_ScriptPrepare
 * asserts the same of a script built by hand, and an overlay holds only codes
 * the jit's builder or validator placed. Loud in a debug build; in a release
 * build the compiler drops the range check in front of the jump table.
 */
#ifndef NDEBUG
#define CS2VM2_UNREACHABLE(why)                                                                    \
    do                                                                                             \
    {                                                                                              \
        assert(0 && (why));                                                                        \
        abort();                                                                                   \
    } while( 0 )
#else
#define CS2VM2_UNREACHABLE(why) __builtin_unreachable()
#endif

/*
 * One script's overlay, in ONE ToriMem_Script block: this header followed by
 * op_count code bytes. It holds indices only, never a pointer into the
 * script, so a byte-for-byte copy is valid for a copy of the script.
 */
struct CS2VM2_Jit
{
    /* == script->op_count when it was built or attached. */
    int32_t op_count;
    /* == script->local_int_count when it was built or attached. */
    int32_t local_int_count;
    /* A hash of the script's constant strings, taken when the overlay was
     * made (debug builds only; 0 under NDEBUG). Constants are pushed by
     * pointer, so CS2VM2_JitFree asserts nobody wrote through one. */
    uint64_t strings_hash;
    /* code[pc]: what the hot loop runs at pc (enum CS2VM2_JitCode). */
    uint8_t code[];
};

/*
 * The hot loop's instruction numbers. 1..31 are the plain hot instructions,
 * the same numbers cs2vm2.c's hot loop has always switched on (formerly a
 * private enum there): the overlay code for a plain instruction at pc is
 * exactly g_cs2vm2_hot_op[opcodes[pc]].
 */
enum CS2VM2_JitCode
{
    CS2VM2_HOT_NONE = 0, /* general path (CS2VM2_RunOp); legal at any pc */
    CS2VM2_HOT_PUSH_CONSTANT_INT,
    CS2VM2_HOT_PUSH_INT_LOCAL,
    CS2VM2_HOT_POP_INT_LOCAL,
    CS2VM2_HOT_BRANCH,
    CS2VM2_HOT_BRANCH_EQUALS,
    CS2VM2_HOT_BRANCH_NOT,
    CS2VM2_HOT_BRANCH_LESS_THAN,
    CS2VM2_HOT_BRANCH_GREATER_THAN,
    CS2VM2_HOT_BRANCH_LESS_THAN_OR_EQUALS,
    CS2VM2_HOT_BRANCH_GREATER_THAN_OR_EQUALS,
    CS2VM2_HOT_ADD,
    CS2VM2_HOT_SUB,
    CS2VM2_HOT_MULTIPLY,
    CS2VM2_HOT_DIVIDE,
    CS2VM2_HOT_MODULO,
    CS2VM2_HOT_AND,
    CS2VM2_HOT_OR,
    CS2VM2_HOT_MIN,
    CS2VM2_HOT_MAX,
    CS2VM2_HOT_ADDPERCENT,
    CS2VM2_HOT_SCALE,
    CS2VM2_HOT_ABS,
    CS2VM2_HOT_BITCOUNT,
    CS2VM2_HOT_SETBIT,
    CS2VM2_HOT_CLEARBIT,
    CS2VM2_HOT_TESTBIT,
    CS2VM2_HOT_TOGGLEBIT,
    CS2VM2_HOT_SETBIT_RANGE,
    CS2VM2_HOT_CLEARBIT_RANGE,
    CS2VM2_HOT_GETBIT_RANGE,
    CS2VM2_HOT_SETBIT_RANGE_VALUE, /* 31 */
    CS2VM2_HOT_PLAIN_LAST = CS2VM2_HOT_SETBIT_RANGE_VALUE,

    /* PUSH_INT_LOCAL a, 0 <= a < local_int_count <= CS2VM_MAX_LOCALS. */
    CS2VM2_JIT_LD = 32,
    /* POP_INT_LOCAL b, same bound. */
    CS2VM2_JIT_PD = 33,
    /* PUSH_CONSTANT_STRING whose string_operands[pc] is not NULL. */
    CS2VM2_JIT_CSTR = 34,
    /* Not an instruction and never in a blob: the code at index op_count of
     * every code array the loop runs (a script's plain codes, an overlay's
     * codes in memory). Running off the script's last instruction dispatches
     * it and the run is over, so the loop has no "is the pc past the end"
     * test of its own. */
    CS2VM2_JIT_END = 35,

    /* The 24 fused groups (docs/CS2_JIT.md 2.1). L = declared-local push,
     * C = constant int push, P = declared-local pop, A = ADD, BR = BRANCH,
     * BRcc = BRANCH_EQUALS / _NOT / _LESS_THAN / _GREATER_THAN. */
    CS2VM2_JIT_FUSED_FIRST = 64,
    CS2VM2_JIT_LL = 64,
    CS2VM2_JIT_LC = 65,
    CS2VM2_JIT_CL = 66,
    CS2VM2_JIT_CC = 67,
    CS2VM2_JIT_LLL = 68,
    CS2VM2_JIT_LCAP = 69,
    CS2VM2_JIT_LCA = 70,
    CS2VM2_JIT_CA = 71,
    CS2VM2_JIT_AP = 72,
    CS2VM2_JIT_CP = 73,
    CS2VM2_JIT_LP = 74,
    CS2VM2_JIT_PBR = 75,
    CS2VM2_JIT_CBREQ = 76,
    CS2VM2_JIT_CBRNE = 77,
    CS2VM2_JIT_CBRLT = 78,
    CS2VM2_JIT_CBRGT = 79,
    CS2VM2_JIT_LCBREQ = 80,
    CS2VM2_JIT_LCBRNE = 81,
    CS2VM2_JIT_LCBRLT = 82,
    CS2VM2_JIT_LCBRGT = 83,
    CS2VM2_JIT_LLBREQ = 84,
    CS2VM2_JIT_LLBRNE = 85,
    CS2VM2_JIT_LLBRLT = 86,
    CS2VM2_JIT_LLBRGT = 87,
    CS2VM2_JIT_FUSED_LAST = 87,
};

/* opcode -> plain hot code, 0 for "not a hot instruction". Static so each
 * includer has its own copy and no link needs another object. */
#define CS2VM2_HOT_TABLE_SIZE ((unsigned)CS2_OP_PLAIN_COUNT)
static const uint8_t g_cs2vm2_hot_op[CS2VM2_HOT_TABLE_SIZE] = {
    [CS2_OP_PUSH_CONSTANT_INT] = CS2VM2_HOT_PUSH_CONSTANT_INT,
    [CS2_OP_PUSH_INT_LOCAL] = CS2VM2_HOT_PUSH_INT_LOCAL,
    [CS2_OP_POP_INT_LOCAL] = CS2VM2_HOT_POP_INT_LOCAL,
    [CS2_OP_BRANCH] = CS2VM2_HOT_BRANCH,
    [CS2_OP_BRANCH_EQUALS] = CS2VM2_HOT_BRANCH_EQUALS,
    [CS2_OP_BRANCH_NOT] = CS2VM2_HOT_BRANCH_NOT,
    [CS2_OP_BRANCH_LESS_THAN] = CS2VM2_HOT_BRANCH_LESS_THAN,
    [CS2_OP_BRANCH_GREATER_THAN] = CS2VM2_HOT_BRANCH_GREATER_THAN,
    [CS2_OP_BRANCH_LESS_THAN_OR_EQUALS] = CS2VM2_HOT_BRANCH_LESS_THAN_OR_EQUALS,
    [CS2_OP_BRANCH_GREATER_THAN_OR_EQUALS] = CS2VM2_HOT_BRANCH_GREATER_THAN_OR_EQUALS,
    [CS2_OP_ADD] = CS2VM2_HOT_ADD,
    [CS2_OP_SUB] = CS2VM2_HOT_SUB,
    [CS2_OP_MULTIPLY] = CS2VM2_HOT_MULTIPLY,
    [CS2_OP_DIVIDE] = CS2VM2_HOT_DIVIDE,
    [CS2_OP_MODULO] = CS2VM2_HOT_MODULO,
    [CS2_OP_AND] = CS2VM2_HOT_AND,
    [CS2_OP_OR] = CS2VM2_HOT_OR,
    [CS2_OP_MIN] = CS2VM2_HOT_MIN,
    [CS2_OP_MAX] = CS2VM2_HOT_MAX,
    [CS2_OP_ADDPERCENT] = CS2VM2_HOT_ADDPERCENT,
    [CS2_OP_SCALE] = CS2VM2_HOT_SCALE,
    [CS2_OP_ABS] = CS2VM2_HOT_ABS,
    [CS2_OP_BITCOUNT] = CS2VM2_HOT_BITCOUNT,
    [CS2_OP_SETBIT] = CS2VM2_HOT_SETBIT,
    [CS2_OP_CLEARBIT] = CS2VM2_HOT_CLEARBIT,
    [CS2_OP_TESTBIT] = CS2VM2_HOT_TESTBIT,
    [CS2_OP_TOGGLEBIT] = CS2VM2_HOT_TOGGLEBIT,
    [CS2_OP_SETBIT_RANGE] = CS2VM2_HOT_SETBIT_RANGE,
    [CS2_OP_CLEARBIT_RANGE] = CS2VM2_HOT_CLEARBIT_RANGE,
    [CS2_OP_GETBIT_RANGE] = CS2VM2_HOT_GETBIT_RANGE,
    [CS2_OP_SETBIT_RANGE_VALUE] = CS2VM2_HOT_SETBIT_RANGE_VALUE,
};

/* What one instruction of a fused shape must be. */
enum CS2VM2_JitIns
{
    CS2VM2_JIT_INS_L = 1, /* PUSH_INT_LOCAL, operand a declared local */
    CS2VM2_JIT_INS_C,     /* PUSH_CONSTANT_INT */
    CS2VM2_JIT_INS_P,     /* POP_INT_LOCAL, operand a declared local */
    CS2VM2_JIT_INS_ADD,   /* ADD */
    CS2VM2_JIT_INS_BR,    /* BRANCH */
    CS2VM2_JIT_INS_BREQ,  /* BRANCH_EQUALS */
    CS2VM2_JIT_INS_BRNE,  /* BRANCH_NOT */
    CS2VM2_JIT_INS_BRLT,  /* BRANCH_LESS_THAN */
    CS2VM2_JIT_INS_BRGT,  /* BRANCH_GREATER_THAN */
};

/*
 * One fused group. `k` instructions; on entry the int stack must hold at
 * least `need` values and have room for `peak` more (how far above the entry
 * top the unfused sequence reaches). Shared by the builder, the validator,
 * the hot loop's guards (as constants) and the tests.
 */
struct CS2VM2_JitShape
{
    uint8_t code;
    uint8_t k;
    uint8_t need;
    uint8_t peak;
    uint8_t ins[4];
    char const* name;
};

#define CS2VM2_JIT_SHAPE_COUNT 24

/* Indexed by code - CS2VM2_JIT_FUSED_FIRST. */
static const struct CS2VM2_JitShape g_cs2vm2_jit_shapes[CS2VM2_JIT_SHAPE_COUNT] = {
#define CS2VM2_JIT_I_(x) CS2VM2_JIT_INS_##x
    { CS2VM2_JIT_LL, 2, 0, 2, { CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(L) }, "LL" },
    { CS2VM2_JIT_LC, 2, 0, 2, { CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(C) }, "LC" },
    { CS2VM2_JIT_CL, 2, 0, 2, { CS2VM2_JIT_I_(C), CS2VM2_JIT_I_(L) }, "CL" },
    { CS2VM2_JIT_CC, 2, 0, 2, { CS2VM2_JIT_I_(C), CS2VM2_JIT_I_(C) }, "CC" },
    { CS2VM2_JIT_LLL, 3, 0, 3, { CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(L) }, "LLL" },
    { CS2VM2_JIT_LCAP, 4, 0, 2,
      { CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(C), CS2VM2_JIT_I_(ADD), CS2VM2_JIT_I_(P) }, "LCAP" },
    { CS2VM2_JIT_LCA, 3, 0, 2, { CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(C), CS2VM2_JIT_I_(ADD) }, "LCA" },
    { CS2VM2_JIT_CA, 2, 1, 1, { CS2VM2_JIT_I_(C), CS2VM2_JIT_I_(ADD) }, "CA" },
    { CS2VM2_JIT_AP, 2, 2, 0, { CS2VM2_JIT_I_(ADD), CS2VM2_JIT_I_(P) }, "AP" },
    { CS2VM2_JIT_CP, 2, 0, 1, { CS2VM2_JIT_I_(C), CS2VM2_JIT_I_(P) }, "CP" },
    { CS2VM2_JIT_LP, 2, 0, 1, { CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(P) }, "LP" },
    { CS2VM2_JIT_PBR, 2, 1, 0, { CS2VM2_JIT_I_(P), CS2VM2_JIT_I_(BR) }, "PBR" },
    { CS2VM2_JIT_CBREQ, 2, 1, 1, { CS2VM2_JIT_I_(C), CS2VM2_JIT_I_(BREQ) }, "CBREQ" },
    { CS2VM2_JIT_CBRNE, 2, 1, 1, { CS2VM2_JIT_I_(C), CS2VM2_JIT_I_(BRNE) }, "CBRNE" },
    { CS2VM2_JIT_CBRLT, 2, 1, 1, { CS2VM2_JIT_I_(C), CS2VM2_JIT_I_(BRLT) }, "CBRLT" },
    { CS2VM2_JIT_CBRGT, 2, 1, 1, { CS2VM2_JIT_I_(C), CS2VM2_JIT_I_(BRGT) }, "CBRGT" },
    { CS2VM2_JIT_LCBREQ, 3, 0, 2, { CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(C), CS2VM2_JIT_I_(BREQ) }, "LCBREQ" },
    { CS2VM2_JIT_LCBRNE, 3, 0, 2, { CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(C), CS2VM2_JIT_I_(BRNE) }, "LCBRNE" },
    { CS2VM2_JIT_LCBRLT, 3, 0, 2, { CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(C), CS2VM2_JIT_I_(BRLT) }, "LCBRLT" },
    { CS2VM2_JIT_LCBRGT, 3, 0, 2, { CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(C), CS2VM2_JIT_I_(BRGT) }, "LCBRGT" },
    { CS2VM2_JIT_LLBREQ, 3, 0, 2, { CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(BREQ) }, "LLBREQ" },
    { CS2VM2_JIT_LLBRNE, 3, 0, 2, { CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(BRNE) }, "LLBRNE" },
    { CS2VM2_JIT_LLBRLT, 3, 0, 2, { CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(BRLT) }, "LLBRLT" },
    { CS2VM2_JIT_LLBRGT, 3, 0, 2, { CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(BRGT) }, "LLBRGT" },
#undef CS2VM2_JIT_I_
};

/* The shape for a fused code, or NULL for any other byte. */
static inline struct CS2VM2_JitShape const*
cs2vm2_jit_shape(unsigned code)
{
    if( code < CS2VM2_JIT_FUSED_FIRST || code > CS2VM2_JIT_FUSED_LAST )
        return NULL;
    return &g_cs2vm2_jit_shapes[code - CS2VM2_JIT_FUSED_FIRST];
}

/*
 * Every opcode that moves the pc by its own int operand: the seven hot int
 * branches, the six long compares (68..73) and BRANCH_IF_ONE (86). SWITCH is
 * the only other jump, by a case's target_pc. This is the one list of them:
 * the builder's range check (cs2vm2_jit_jumps_in_range) reads it, and
 * CS2VM2_JumpRelative asserts in a debug build that the opcode it moves the
 * pc for is on it, so a new jump that is missed here aborts the first time it
 * is taken instead of leaving a script with an out-of-range jump jittable.
 * Changing the list changes which scripts are unjittable, which bumps
 * CS2VM2_JIT_FORMAT_VERSION.
 */
static inline int
cs2vm2_op_jumps_by_operand(int op)
{
    return op == CS2_OP_BRANCH || op == CS2_OP_BRANCH_NOT || op == CS2_OP_BRANCH_EQUALS ||
           op == CS2_OP_BRANCH_LESS_THAN || op == CS2_OP_BRANCH_GREATER_THAN ||
           op == CS2_OP_BRANCH_LESS_THAN_OR_EQUALS || op == CS2_OP_BRANCH_GREATER_THAN_OR_EQUALS ||
           op == CS2_OP_LONG_BRANCH_NOT || op == CS2_OP_LONG_BRANCH_EQUALS ||
           op == CS2_OP_LONG_BRANCH_LESS_THAN || op == CS2_OP_LONG_BRANCH_GREATER_THAN ||
           op == CS2_OP_LONG_BRANCH_LESS_THAN_OR_EQUALS ||
           op == CS2_OP_LONG_BRANCH_GREATER_THAN_OR_EQUALS || op == CS2_OP_BRANCH_IF_ONE;
}

/*
 * The plain code of the instruction at `pc`: g_cs2vm2_hot_op of its opcode,
 * except that a hot branch whose target is outside [0, op_count] is 0, the
 * handler's. The loop runs a hot branch with no range test, which is sound
 * only because every one it can reach was checked here, when the script was
 * loaded; the handler path checks the pc after each handler instead. (An
 * overlay is only built for a script all of whose jumps are in range, so this
 * is the overlay's plain code too.)
 */
static inline unsigned
cs2vm2_plain_code_at(
    struct CS2VM2_Script const* script,
    int pc)
{
    unsigned const op = script->opcodes[pc];
    unsigned const code = op < CS2VM2_HOT_TABLE_SIZE ? g_cs2vm2_hot_op[op] : CS2VM2_HOT_NONE;
    if( code >= CS2VM2_HOT_BRANCH && code <= CS2VM2_HOT_BRANCH_GREATER_THAN_OR_EQUALS )
    {
        int64_t const target = (int64_t)pc + 1 + (int64_t)script->int_operands[pc];
        if( target < 0 || target > (int64_t)script->op_count )
            return CS2VM2_HOT_NONE;
    }
    return code;
}

#endif /* CS2VM2_JIT_INTERNAL_H */
