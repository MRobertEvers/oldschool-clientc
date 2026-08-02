/*
 * Unit test for the CS2 math / bit opcodes (4000..4030), driven through the real
 * VM dispatch with hand-assembled scripts. Expected values come from the
 * authoritative reference (xrsps-typescript src/rs/cs2/handlers/MathOps.ts).
 */

#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_script.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;

#define CHECK(got, want, label)                                                                    \
    do                                                                                             \
    {                                                                                              \
        int gv = (got), wv = (want);                                                               \
        if( gv == wv )                                                                             \
            printf("  ok: %s == %d\n", label, gv);                                                 \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s got %d want %d\n", label, gv, wv);                                  \
            g_fail++;                                                                              \
        }                                                                                          \
    } while( 0 )

/* Build and run: [PUSH_CONSTANT_INT arg0..argN-1, op, RETURN]; return stack top. */
static int
run_op(
    int op,
    const int* args,
    int argc)
{
    struct CS2VM2 vm;
    CS2VM2_Init(&vm);

    struct CS2VM2_Script script;
    CS2VM2_ScriptInit(&script);
    int op_count = argc + 2;
    script.script_id = 9999;
    script.op_count = op_count;
    script.opcodes = calloc((size_t)op_count, sizeof(uint16_t));
    script.int_operands = calloc((size_t)op_count, sizeof(int));
    script.string_operands = calloc((size_t)op_count, sizeof(char*));

    for( int i = 0; i < argc; i++ )
    {
        script.opcodes[i] = (uint16_t)CS2_OP_PUSH_CONSTANT_INT;
        script.int_operands[i] = args[i];
    }
    script.opcodes[argc] = (uint16_t)op;
    script.opcodes[argc + 1] = (uint16_t)CS2_OP_RETURN;

    struct CS2VM2_Thread* t = CS2VM2_ThreadMain(&vm);
    CS2VM2_PushCallScript(t, &script);
    CS2VM2_RunScript(t);

    int result = 0;
    CS2VM2_PopInt(t, &result);

    /* opcodes/operands are owned by us here, not the copied frame script. */
    free(script.opcodes);
    free(script.int_operands);
    free(script.string_operands);
    return result;
}

static int
op2(
    int op,
    int a,
    int b)
{
    int args[2] = { a, b };
    return run_op(op, args, 2);
}

static int
op1(
    int op,
    int a)
{
    int args[1] = { a };
    return run_op(op, args, 1);
}

static int
op3(
    int op,
    int a,
    int b,
    int c)
{
    int args[3] = { a, b, c };
    return run_op(op, args, 3);
}

static int
op4(
    int op,
    int a,
    int b,
    int c,
    int d)
{
    int args[4] = { a, b, c, d };
    return run_op(op, args, 4);
}

/*
 * Run a one-argument op with a sentinel underneath it and report BOTH stack
 * cells: `*out_result` is what the op pushed, `*out_below` is whatever is left
 * under it.
 *
 * `run_op` above cannot see the difference between an op that pops its argument
 * and one that does not — both leave the answer on top. That difference is
 * exactly what was wrong with RANDOM, which pushed rand() and popped nothing,
 * so its argument stayed on the stack and its result was unbounded. Neither
 * half is loud: a leftover int is tolerated at script end, and an out-of-range
 * value used as an array index reads back as 0.
 */
static void
op1_with_sentinel(
    int op,
    int arg,
    int sentinel,
    int* out_result,
    int* out_below)
{
    struct CS2VM2 vm;
    struct CS2VM2_Script script;
    struct CS2VM2_Thread* t;
    const int op_count = 4;

    CS2VM2_Init(&vm);
    CS2VM2_ScriptInit(&script);
    script.script_id = 9998;
    script.op_count = op_count;
    script.opcodes = calloc((size_t)op_count, sizeof(uint16_t));
    script.int_operands = calloc((size_t)op_count, sizeof(int));
    script.string_operands = calloc((size_t)op_count, sizeof(char*));

    script.opcodes[0] = (uint16_t)CS2_OP_PUSH_CONSTANT_INT;
    script.int_operands[0] = sentinel;
    script.opcodes[1] = (uint16_t)CS2_OP_PUSH_CONSTANT_INT;
    script.int_operands[1] = arg;
    script.opcodes[2] = (uint16_t)op;
    script.opcodes[3] = (uint16_t)CS2_OP_RETURN;

    t = CS2VM2_ThreadMain(&vm);
    CS2VM2_PushCallScript(t, &script);
    CS2VM2_RunScript(t);

    *out_result = 0;
    *out_below = 0;
    CS2VM2_PopInt(t, out_result);
    CS2VM2_PopInt(t, out_below);

    free(script.opcodes);
    free(script.int_operands);
    free(script.string_operands);
}

int
main(void)
{
    /* AND / MIN / MAX */
    CHECK(op2(CS2_OP_AND, 6, 3), 2, "AND(6,3)");
    CHECK(op2(CS2_OP_MIN, 5, 9), 5, "MIN(5,9)");
    CHECK(op2(CS2_OP_MIN, -3, 2), -3, "MIN(-3,2)");
    CHECK(op2(CS2_OP_MAX, 5, 9), 9, "MAX(5,9)");
    CHECK(op2(CS2_OP_MAX, -3, -8), -3, "MAX(-3,-8)");

    /* ADDPERCENT: value + value*percent/100 */
    CHECK(op2(CS2_OP_ADDPERCENT, 200, 10), 220, "ADDPERCENT(200,10)");
    CHECK(op2(CS2_OP_ADDPERCENT, 50, -10), 45, "ADDPERCENT(50,-10)");

    /* BITCOUNT / TOGGLEBIT */
    CHECK(op1(CS2_OP_BITCOUNT, 0xFF), 8, "BITCOUNT(0xFF)");
    CHECK(op1(CS2_OP_BITCOUNT, 0), 0, "BITCOUNT(0)");
    CHECK(op1(CS2_OP_BITCOUNT, -1), 32, "BITCOUNT(-1)");
    CHECK(op2(CS2_OP_TOGGLEBIT, 0xA /*1010*/, 0), 0xB, "TOGGLEBIT(1010,0)");
    CHECK(op2(CS2_OP_TOGGLEBIT, 0xA, 1), 0x8, "TOGGLEBIT(1010,1)");

    /* Bit-range ops: pushed value, low, high (high on top). */
    CHECK(op3(CS2_OP_SETBIT_RANGE, 0, 2, 4), 28, "SETBIT_RANGE(0,2,4)");
    CHECK(op3(CS2_OP_CLEARBIT_RANGE, 0xFF, 2, 4), 227, "CLEARBIT_RANGE(0xFF,2,4)");
    CHECK(op3(CS2_OP_GETBIT_RANGE, 0xD8 /*11011000*/, 3, 5), 3, "GETBIT_RANGE(11011000,3,5)");

    /* SETBIT_RANGE_VALUE: pushed value, newBits, low, high. */
    CHECK(op4(CS2_OP_SETBIT_RANGE_VALUE, 0xFF, 1, 2, 4), 231, "SETBIT_RANGE_VALUE(0xFF,1,2,4)");
    CHECK(op4(CS2_OP_SETBIT_RANGE_VALUE, 0, 100, 2, 4), 28, "SETBIT_RANGE_VALUE clamp(0,100,2,4)");

    /* Already-present ops, for regression coverage vs the reference. */
    CHECK(op2(CS2_OP_ADD, 7, 5), 12, "ADD(7,5)");
    CHECK(op2(CS2_OP_SUB, 7, 5), 2, "SUB(7,5)");
    CHECK(op2(CS2_OP_MULTIPLY, 6, 7), 42, "MULTIPLY(6,7)");
    CHECK(op2(CS2_OP_DIV, 7, 2), 3, "DIV(7,2)");
    CHECK(op2(CS2_OP_MOD, 7, 3), 1, "MOD(7,3)");
    CHECK(op2(CS2_OP_POW, 2, 10), 1024, "POW(2,10)");
    CHECK(op2(CS2_OP_OR, 6, 3), 7, "OR(6,3)");
    CHECK(op2(CS2_OP_SETBIT, 0, 3), 8, "SETBIT(0,3)");
    CHECK(op2(CS2_OP_CLEARBIT, 0xF, 1), 0xD, "CLEARBIT(0xF,1)");
    CHECK(op2(CS2_OP_TESTBIT, 0x8, 3), 1, "TESTBIT(0x8,3)");
    CHECK(op3(CS2_OP_SCALE, 10, 3, 6), 20, "SCALE(10,3,6)");

    /* INVPOW = integer root (was previously mis-implemented as base^exp). */
    CHECK(op2(CS2_OP_INVPOW, 16, 2), 4, "INVPOW(16,2)=sqrt");
    CHECK(op2(CS2_OP_INVPOW, 27, 3), 3, "INVPOW(27,3)=cbrt");
    CHECK(op2(CS2_OP_INVPOW, 81, 4), 3, "INVPOW(81,4)");
    CHECK(op2(CS2_OP_INVPOW, 100, 1), 100, "INVPOW(100,1)");
    CHECK(op2(CS2_OP_INVPOW, 5, 0), 2147483647, "INVPOW(5,0)=MAXINT");
    CHECK(op2(CS2_OP_INVPOW, 0, 5), 0, "INVPOW(0,5)=0");

    /* INTERPOLATE: a + (b-a)*(e-c)/(d-c) */
    {
        int args[5] = { 0, 100, 0, 10, 5 };
        CHECK(run_op(CS2_OP_INTERPOLATE, args, 5), 50, "INTERPOLATE(0,100,0,10,5)");
    }

    /*
     * RANDOM / RANDOMINC.
     *
     * `[command,random](int $num)` is "0 to $num - 1" and `randominc` is "0 to
     * $num" (LostCity content/scripts/engine.rs2:958-961). Two properties, and
     * RANDOM was failing both: it popped nothing and pushed a raw rand().
     *
     * The failure was silent everywhere until the bank PIN keypad, which
     * shuffles its ten digit buttons with `random(9)` twenty times. Every swap
     * indexed past the end of the array, PUSH_ARRAY_INT answered 0 for each,
     * and the keypad drew its digits in plain 0-to-9 order with the last button
     * blank — the anti-shoulder-surfing property of the whole screen, gone,
     * with nothing on screen to say so.
     */
    {
        const int k_sentinel = 0x5A5A5A;
        int result = 0;
        int below = 0;
        int in_range = 1;
        int saw_nonzero = 0;

        /* Popped, not left behind — see op1_with_sentinel. */
        op1_with_sentinel(CS2_OP_RANDOM, 9, k_sentinel, &result, &below);
        CHECK(below, k_sentinel, "RANDOM pops its argument");
        op1_with_sentinel(CS2_OP_RANDOMINC, 9, k_sentinel, &result, &below);
        CHECK(below, k_sentinel, "RANDOMINC pops its argument");

        /* Bounded. 200 samples of random(10) must all land in 0..9, and must
         * not all be the same number — a handler that answers a constant is
         * still "in range" and is what an unpopped argument degrades into. */
        for( int i = 0; i < 200; i++ )
        {
            int v = op1(CS2_OP_RANDOM, 10);
            if( v < 0 || v > 9 )
                in_range = 0;
            if( v != 0 )
                saw_nonzero = 1;
        }
        CHECK(in_range, 1, "random(10) always lands in 0..9");
        CHECK(saw_nonzero, 1, "random(10) is not stuck on one value");

        /* randominc(0) is the inclusive edge: 0..0. random(1) is 0..0 too. */
        CHECK(op1(CS2_OP_RANDOM, 1), 0, "random(1) is always 0");
        CHECK(op1(CS2_OP_RANDOMINC, 0), 0, "randominc(0) is always 0");

        /* A non-positive bound must not divide by zero. */
        CHECK(op1(CS2_OP_RANDOM, 0), 0, "random(0) is 0, not a division by zero");
        CHECK(op1(CS2_OP_RANDOM, -5), 0, "random(-5) is 0");

        /* random(n) excludes n; randominc(n) includes it. 400 samples of
         * random(2) must never be 2, and randominc(1) must eventually be 1. */
        {
            int saw_bound = 0;
            int saw_inc_bound = 0;
            for( int i = 0; i < 400; i++ )
            {
                if( op1(CS2_OP_RANDOM, 2) == 2 )
                    saw_bound = 1;
                if( op1(CS2_OP_RANDOMINC, 1) == 1 )
                    saw_inc_bound = 1;
            }
            CHECK(saw_bound, 0, "random(2) never returns 2 (exclusive)");
            CHECK(saw_inc_bound, 1, "randominc(1) does return 1 (inclusive)");
        }
    }

    if( g_fail )
    {
        printf("math ops test: %d FAILED\n", g_fail);
        return 1;
    }
    printf("math ops test: all passed\n");
    return 0;
}
