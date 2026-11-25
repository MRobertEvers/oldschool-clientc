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
 * The overlay holds enum CS2_Op instructions. What a blob stores for them is
 * the instruction's position in the JIT section (jit_instructions.py), whose
 * order is frozen under CS2VM2_JIT_FORMAT_VERSION, as are the shapes below.
 */

#include "cs2_opcode.h"
#include "cs2vm2_script.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

/*
 * The default of a switch over canonical instructions, which no loaded script
 * can reach: CS2VM2_ScriptResolveDialect gives every script only instructions
 * below CS2_OP_PLAIN_COUNT (an id it cannot translate is UNDEFINED_WIRE_ID),
 * CS2VM2_ScriptPrepare asserts the same of a script built by hand, and a run
 * array holds only what CS2VM2_ScriptPrepare or the jit's builder or
 * validator placed. Loud in a debug build; in a release
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
 * op_count + 1 instructions. It holds indices only, never a pointer into the
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
    /* ops[pc]: what the loop runs at pc (enum CS2_Op): the pc's own run
     * instruction (cs2vm2_run_op_at) or a JIT-section one; END_OF_SCRIPT at
     * op_count. */
    uint16_t ops[];
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
 * the loop's guards (as constants) and the tests.
 */
struct CS2VM2_JitShape
{
    uint16_t op;
    uint8_t k;
    uint8_t need;
    uint8_t peak;
    uint8_t ins[4];
    char const* name;
};

#define CS2VM2_JIT_SHAPE_COUNT (CS2_OPS_JIT_FUSED_END - CS2_OPS_JIT_FUSED_BEGIN)
_Static_assert(CS2VM2_JIT_SHAPE_COUNT == 24, "a shape row for every fused instruction");

/* Indexed by instruction - CS2_OPS_JIT_FUSED_BEGIN. */
static const struct CS2VM2_JitShape g_cs2vm2_jit_shapes[CS2VM2_JIT_SHAPE_COUNT] = {
#define CS2VM2_JIT_I_(x) CS2VM2_JIT_INS_##x
    [CS2_OP_PUSH_INT_LOCAL__PUSH_INT_LOCAL - CS2_OPS_JIT_FUSED_BEGIN] = {
        CS2_OP_PUSH_INT_LOCAL__PUSH_INT_LOCAL, 2, 0, 2, { CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(L) }, "LL" },
    [CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT - CS2_OPS_JIT_FUSED_BEGIN] = {
        CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT, 2, 0, 2, { CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(C) }, "LC" },
    [CS2_OP_PUSH_CONSTANT_INT__PUSH_INT_LOCAL - CS2_OPS_JIT_FUSED_BEGIN] = {
        CS2_OP_PUSH_CONSTANT_INT__PUSH_INT_LOCAL, 2, 0, 2, { CS2VM2_JIT_I_(C), CS2VM2_JIT_I_(L) }, "CL" },
    [CS2_OP_PUSH_CONSTANT_INT__PUSH_CONSTANT_INT - CS2_OPS_JIT_FUSED_BEGIN] = {
        CS2_OP_PUSH_CONSTANT_INT__PUSH_CONSTANT_INT, 2, 0, 2, { CS2VM2_JIT_I_(C), CS2VM2_JIT_I_(C) }, "CC" },
    [CS2_OP_PUSH_INT_LOCAL__PUSH_INT_LOCAL__PUSH_INT_LOCAL - CS2_OPS_JIT_FUSED_BEGIN] = {
        CS2_OP_PUSH_INT_LOCAL__PUSH_INT_LOCAL__PUSH_INT_LOCAL, 3, 0, 3,
        { CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(L) }, "LLL" },
    [CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT__ADD__POP_INT_LOCAL - CS2_OPS_JIT_FUSED_BEGIN] = {
        CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT__ADD__POP_INT_LOCAL, 4, 0, 2,
        { CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(C), CS2VM2_JIT_I_(ADD), CS2VM2_JIT_I_(P) }, "LCAP" },
    [CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT__ADD - CS2_OPS_JIT_FUSED_BEGIN] = {
        CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT__ADD, 3, 0, 2,
        { CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(C), CS2VM2_JIT_I_(ADD) }, "LCA" },
    [CS2_OP_PUSH_CONSTANT_INT__ADD - CS2_OPS_JIT_FUSED_BEGIN] = {
        CS2_OP_PUSH_CONSTANT_INT__ADD, 2, 1, 1, { CS2VM2_JIT_I_(C), CS2VM2_JIT_I_(ADD) }, "CA" },
    [CS2_OP_ADD__POP_INT_LOCAL - CS2_OPS_JIT_FUSED_BEGIN] = {
        CS2_OP_ADD__POP_INT_LOCAL, 2, 2, 0, { CS2VM2_JIT_I_(ADD), CS2VM2_JIT_I_(P) }, "AP" },
    [CS2_OP_PUSH_CONSTANT_INT__POP_INT_LOCAL - CS2_OPS_JIT_FUSED_BEGIN] = {
        CS2_OP_PUSH_CONSTANT_INT__POP_INT_LOCAL, 2, 0, 1, { CS2VM2_JIT_I_(C), CS2VM2_JIT_I_(P) }, "CP" },
    [CS2_OP_PUSH_INT_LOCAL__POP_INT_LOCAL - CS2_OPS_JIT_FUSED_BEGIN] = {
        CS2_OP_PUSH_INT_LOCAL__POP_INT_LOCAL, 2, 0, 1, { CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(P) }, "LP" },
    [CS2_OP_POP_INT_LOCAL__BRANCH - CS2_OPS_JIT_FUSED_BEGIN] = {
        CS2_OP_POP_INT_LOCAL__BRANCH, 2, 1, 0, { CS2VM2_JIT_I_(P), CS2VM2_JIT_I_(BR) }, "PBR" },
    [CS2_OP_PUSH_CONSTANT_INT__BRANCH_EQUALS - CS2_OPS_JIT_FUSED_BEGIN] = {
        CS2_OP_PUSH_CONSTANT_INT__BRANCH_EQUALS, 2, 1, 1,
        { CS2VM2_JIT_I_(C), CS2VM2_JIT_I_(BREQ) }, "CBREQ" },
    [CS2_OP_PUSH_CONSTANT_INT__BRANCH_NOT - CS2_OPS_JIT_FUSED_BEGIN] = {
        CS2_OP_PUSH_CONSTANT_INT__BRANCH_NOT, 2, 1, 1, { CS2VM2_JIT_I_(C), CS2VM2_JIT_I_(BRNE) }, "CBRNE" },
    [CS2_OP_PUSH_CONSTANT_INT__BRANCH_LESS_THAN - CS2_OPS_JIT_FUSED_BEGIN] = {
        CS2_OP_PUSH_CONSTANT_INT__BRANCH_LESS_THAN, 2, 1, 1,
        { CS2VM2_JIT_I_(C), CS2VM2_JIT_I_(BRLT) }, "CBRLT" },
    [CS2_OP_PUSH_CONSTANT_INT__BRANCH_GREATER_THAN - CS2_OPS_JIT_FUSED_BEGIN] = {
        CS2_OP_PUSH_CONSTANT_INT__BRANCH_GREATER_THAN, 2, 1, 1,
        { CS2VM2_JIT_I_(C), CS2VM2_JIT_I_(BRGT) }, "CBRGT" },
    [CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT__BRANCH_EQUALS - CS2_OPS_JIT_FUSED_BEGIN] = {
        CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT__BRANCH_EQUALS, 3, 0, 2,
        { CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(C), CS2VM2_JIT_I_(BREQ) }, "LCBREQ" },
    [CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT__BRANCH_NOT - CS2_OPS_JIT_FUSED_BEGIN] = {
        CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT__BRANCH_NOT, 3, 0, 2,
        { CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(C), CS2VM2_JIT_I_(BRNE) }, "LCBRNE" },
    [CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT__BRANCH_LESS_THAN - CS2_OPS_JIT_FUSED_BEGIN] = {
        CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT__BRANCH_LESS_THAN, 3, 0, 2,
        { CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(C), CS2VM2_JIT_I_(BRLT) }, "LCBRLT" },
    [CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT__BRANCH_GREATER_THAN - CS2_OPS_JIT_FUSED_BEGIN] = {
        CS2_OP_PUSH_INT_LOCAL__PUSH_CONSTANT_INT__BRANCH_GREATER_THAN, 3, 0, 2,
        { CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(C), CS2VM2_JIT_I_(BRGT) }, "LCBRGT" },
    [CS2_OP_PUSH_INT_LOCAL__PUSH_INT_LOCAL__BRANCH_EQUALS - CS2_OPS_JIT_FUSED_BEGIN] = {
        CS2_OP_PUSH_INT_LOCAL__PUSH_INT_LOCAL__BRANCH_EQUALS, 3, 0, 2,
        { CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(BREQ) }, "LLBREQ" },
    [CS2_OP_PUSH_INT_LOCAL__PUSH_INT_LOCAL__BRANCH_NOT - CS2_OPS_JIT_FUSED_BEGIN] = {
        CS2_OP_PUSH_INT_LOCAL__PUSH_INT_LOCAL__BRANCH_NOT, 3, 0, 2,
        { CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(BRNE) }, "LLBRNE" },
    [CS2_OP_PUSH_INT_LOCAL__PUSH_INT_LOCAL__BRANCH_LESS_THAN - CS2_OPS_JIT_FUSED_BEGIN] = {
        CS2_OP_PUSH_INT_LOCAL__PUSH_INT_LOCAL__BRANCH_LESS_THAN, 3, 0, 2,
        { CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(BRLT) }, "LLBRLT" },
    [CS2_OP_PUSH_INT_LOCAL__PUSH_INT_LOCAL__BRANCH_GREATER_THAN - CS2_OPS_JIT_FUSED_BEGIN] = {
        CS2_OP_PUSH_INT_LOCAL__PUSH_INT_LOCAL__BRANCH_GREATER_THAN, 3, 0, 2,
        { CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(L), CS2VM2_JIT_I_(BRGT) }, "LLBRGT" },
#undef CS2VM2_JIT_I_
};

/* The shape of a fused instruction, or NULL for any other. */
static inline struct CS2VM2_JitShape const*
cs2vm2_jit_shape(unsigned op)
{
    if( op < CS2_OPS_JIT_FUSED_BEGIN || op >= CS2_OPS_JIT_FUSED_END )
        return NULL;
    return &g_cs2vm2_jit_shapes[op - CS2_OPS_JIT_FUSED_BEGIN];
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

/* Whether `op` is one of the FAST int branches, which the loop runs with no
 * range test of its own. */
static inline int
cs2vm2_op_is_fast_branch(unsigned op)
{
    return op == CS2_OP_BRANCH || op == CS2_OP_BRANCH_NOT || op == CS2_OP_BRANCH_EQUALS ||
           op == CS2_OP_BRANCH_LESS_THAN || op == CS2_OP_BRANCH_GREATER_THAN ||
           op == CS2_OP_BRANCH_LESS_THAN_OR_EQUALS || op == CS2_OP_BRANCH_GREATER_THAN_OR_EQUALS;
}

/*
 * The instruction the loop runs at `pc` when nothing proved more about it:
 * the script's own opcodes[pc], except that a FAST int branch whose target
 * is outside [0, op_count] is RUN_HANDLER, which gives it to its handler.
 * The loop runs a FAST branch with no range test, which is sound only because
 * every one it can reach was checked here, when the script was loaded; the
 * handler path checks the pc after each handler instead. (An overlay is only
 * built for a script all of whose jumps are in range, so there this is
 * always opcodes[pc].)
 */
static inline uint16_t
cs2vm2_run_op_at(
    struct CS2VM2_Script const* script,
    int pc)
{
    unsigned const op = script->opcodes[pc];
    if( cs2vm2_op_is_fast_branch(op) )
    {
        int64_t const target = (int64_t)pc + 1 + (int64_t)script->int_operands[pc];
        if( target < 0 || target > (int64_t)script->op_count )
            return CS2_OP_RUN_HANDLER;
    }
    return (uint16_t)op;
}

#endif /* CS2VM2_JIT_INTERNAL_H */
