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

/*
 * One opcode, two ways in.
 *
 * An arithmetic opcode runs in the interpreter's hot loop, and on the general
 * path (through CS2VM2_RunOp) when a run is being traced or profiled. Both
 * must give one answer, and must agree on whether the script ends: the
 * reference throws on a divide by zero and on a bit range whose high is below
 * its low. `traced` arms a trace capture, which is what sends every
 * instruction down the general path.
 *
 * Returns the run's result code; `*out_top` is the value left on the stack
 * and `*out_depth` how many ints are on it.
 */
static int
run_op_path(
    int op,
    const int* args,
    int argc,
    int traced,
    int* out_top,
    int* out_depth)
{
    struct CS2VM2 vm;
    struct CS2VM2_Script script;
    struct CS2VM2_TraceRecord records[16];
    int op_count = argc + 2;
    int result;

    CS2VM2_Init(&vm);
    CS2VM2_ScriptInit(&script);
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
    if( traced )
        CS2VM2_TraceCaptureBegin(records, 16);
    result = CS2VM2_RunScript(t);
    if( traced )
        (void)CS2VM2_TraceCaptureEnd();

    *out_depth = t->ints_stack_top;
    *out_top = t->ints_stack_top > 0 ? t->ints_stack[t->ints_stack_top - 1] : 0;

    free(script.opcodes);
    free(script.int_operands);
    free(script.string_operands);
    return result;
}

/* Every combination of `values` as the operands of `op`, down both paths. */
static void
sweep_both_paths(
    int op,
    int argc,
    char const* name)
{
    static const int values[] = { INT32_MIN, INT32_MIN + 1, -100, -33, -32, -1, 0,  1,
                                  2,         7,             30,   31,  32,  33, 100, 46341,
                                  65536,     INT32_MAX - 1, INT32_MAX };
    int const n = (int)(sizeof(values) / sizeof(values[0]));
    int total = 1;
    int mismatches = 0;
    for( int i = 0; i < argc; i++ )
        total *= n;

    for( int combo = 0; combo < total; combo++ )
    {
        int args[4];
        int c = combo;
        for( int i = 0; i < argc; i++ )
        {
            args[i] = values[c % n];
            c /= n;
        }
        int hot_top, hot_depth, gen_top, gen_depth;
        int const hot = run_op_path(op, args, argc, 0, &hot_top, &hot_depth);
        int const gen = run_op_path(op, args, argc, 1, &gen_top, &gen_depth);
        if( hot != gen || (hot != CS2VM_EXECNO_ERROR && (hot_top != gen_top || hot_depth != gen_depth)) )
        {
            if( mismatches++ < 3 )
                printf(
                    "  FAIL: %s(%d,%d,%d,%d) hot loop %d/%d depth %d, general path %d/%d depth %d\n",
                    name,
                    args[0],
                    argc > 1 ? args[1] : 0,
                    argc > 2 ? args[2] : 0,
                    argc > 3 ? args[3] : 0,
                    hot,
                    hot_top,
                    hot_depth,
                    gen,
                    gen_top,
                    gen_depth);
        }
    }
    CHECK(mismatches, 0, name);

    /* One operand short: both paths end the script, neither reads under the stack. */
    if( argc > 0 )
    {
        int args[4] = { 5, 6, 7, 8 };
        int top, depth;
        CHECK(
            run_op_path(op, args, argc - 1, 0, &top, &depth) ==
                run_op_path(op, args, argc - 1, 1, &top, &depth),
            1,
            "  one operand short: same result code on both paths");
    }
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

    /* ABS. Had no handler at all until 2026-08-02: it fell through to
     * StackMetaStub, so before the cs2_command.gen.h bridge it aborted the
     * script and after the bridge it would have answered 0 for every input --
     * the wrong-but-plausible answer this test exists to rule out. INT_MIN is
     * the wrap case: it has no positive counterpart, and the reference client
     * wraps rather than saturating. */
    CHECK(op1(CS2_OP_ABS, 7), 7, "ABS(7)");
    CHECK(op1(CS2_OP_ABS, -7), 7, "ABS(-7)");
    CHECK(op1(CS2_OP_ABS, 0), 0, "ABS(0)");
    CHECK(op1(CS2_OP_ABS, -2147483647 - 1), -2147483647 - 1, "ABS(INT_MIN) wraps");

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
    CHECK(op2(CS2_OP_DIVIDE, 7, 2), 3, "DIV(7,2)");
    CHECK(op2(CS2_OP_MODULO, 7, 3), 1, "MOD(7,3)");
    CHECK(op2(CS2_OP_POW, 2, 10), 1024, "POW(2,10)");
    CHECK(op2(CS2_OP_OR, 6, 3), 7, "OR(6,3)");
    CHECK(op2(CS2_OP_SETBIT, 0, 3), 8, "SETBIT(0,3)");
    CHECK(op2(CS2_OP_CLEARBIT, 0xF, 1), 0xD, "CLEARBIT(0xF,1)");
    CHECK(op2(CS2_OP_TESTBIT, 0x8, 3), 1, "TESTBIT(0x8,3)");

    /* Bit 31 and bit indices outside 0..31 (rev-239 Statics 4008-4010,
     * 4026-4030): SETBIT, CLEARBIT, TESTBIT and the range ops test the index
     * and leave the value (TESTBIT answers 0); TOGGLEBIT has no test and Java
     * takes its shift count mod 32. Bit 31 is the sign bit, set by a wrap. */
    CHECK(op2(CS2_OP_SETBIT, 0, 31), -2147483647 - 1, "SETBIT(0,31) sets the sign bit");
    CHECK(op2(CS2_OP_TESTBIT, -2147483647 - 1, 31), 1, "TESTBIT(INT_MIN,31)");
    CHECK(op2(CS2_OP_CLEARBIT, -1, 31), 2147483647, "CLEARBIT(-1,31)");
    CHECK(op2(CS2_OP_SETBIT, 4, 32), 4, "SETBIT(4,32) leaves the value");
    CHECK(op2(CS2_OP_SETBIT, 0, -1), 0, "SETBIT(0,-1) leaves the value");
    CHECK(op2(CS2_OP_CLEARBIT, 1, 32), 1, "CLEARBIT(1,32) leaves the value");
    CHECK(op2(CS2_OP_TESTBIT, 1, 32), 0, "TESTBIT(1,32) is 0");
    CHECK(op2(CS2_OP_TOGGLEBIT, 0, 33), 2, "TOGGLEBIT(0,33) toggles bit 1");
    CHECK(op3(CS2_OP_SETBIT_RANGE, 0, 0, 31), -1, "SETBIT_RANGE(0,0,31)");
    CHECK(op3(CS2_OP_CLEARBIT_RANGE, -1, 0, 31), 0, "CLEARBIT_RANGE(-1,0,31)");
    CHECK(op3(CS2_OP_GETBIT_RANGE, -1, 0, 31), -1, "GETBIT_RANGE(-1,0,31)");
    CHECK(op3(CS2_OP_GETBIT_RANGE, -2147483647 - 1, 31, 31), 1, "GETBIT_RANGE(INT_MIN,31,31)");
    CHECK(op3(CS2_OP_SETBIT_RANGE, 7, 0, 32), 7, "SETBIT_RANGE(7,0,32) leaves the value");
    CHECK(op3(CS2_OP_GETBIT_RANGE, 0xD8, -1, 5), 0xD8, "GETBIT_RANGE(0xD8,-1,5) leaves the value");
    CHECK(op4(CS2_OP_SETBIT_RANGE_VALUE, 0, 5, 31, 31), -2147483647 - 1,
          "SETBIT_RANGE_VALUE(0,5,31,31) clamps to 1 in the sign bit");
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

    /* The hot loop and the general path agree on every arithmetic opcode. */
    {
        int top, depth;
        int a2[2];

        sweep_both_paths(CS2_OP_ADD, 2, "ADD both paths");
        sweep_both_paths(CS2_OP_SUB, 2, "SUB both paths");
        sweep_both_paths(CS2_OP_MULTIPLY, 2, "MULTIPLY both paths");
        sweep_both_paths(CS2_OP_DIVIDE, 2, "DIVIDE both paths");
        sweep_both_paths(CS2_OP_MODULO, 2, "MODULO both paths");
        sweep_both_paths(CS2_OP_AND, 2, "AND both paths");
        sweep_both_paths(CS2_OP_OR, 2, "OR both paths");
        sweep_both_paths(CS2_OP_MIN, 2, "MIN both paths");
        sweep_both_paths(CS2_OP_MAX, 2, "MAX both paths");
        sweep_both_paths(CS2_OP_ADDPERCENT, 2, "ADDPERCENT both paths");
        sweep_both_paths(CS2_OP_SCALE, 3, "SCALE both paths");
        sweep_both_paths(CS2_OP_ABS, 1, "ABS both paths");
        sweep_both_paths(CS2_OP_BITCOUNT, 1, "BITCOUNT both paths");
        sweep_both_paths(CS2_OP_SETBIT, 2, "SETBIT both paths");
        sweep_both_paths(CS2_OP_CLEARBIT, 2, "CLEARBIT both paths");
        sweep_both_paths(CS2_OP_TESTBIT, 2, "TESTBIT both paths");
        sweep_both_paths(CS2_OP_TOGGLEBIT, 2, "TOGGLEBIT both paths");
        sweep_both_paths(CS2_OP_SETBIT_RANGE, 3, "SETBIT_RANGE both paths");
        sweep_both_paths(CS2_OP_CLEARBIT_RANGE, 3, "CLEARBIT_RANGE both paths");
        sweep_both_paths(CS2_OP_GETBIT_RANGE, 3, "GETBIT_RANGE both paths");
        sweep_both_paths(CS2_OP_SETBIT_RANGE_VALUE, 4, "SETBIT_RANGE_VALUE both paths");

        /* Java int arithmetic at its edges: it wraps, and INT_MIN / -1 is
         * INT_MIN with remainder 0 where C traps or is undefined. */
        CHECK(op2(CS2_OP_ADD, INT32_MAX, 1), INT32_MIN, "ADD wraps");
        CHECK(op2(CS2_OP_SUB, INT32_MIN, 1), INT32_MAX, "SUB wraps");
        CHECK(op2(CS2_OP_MULTIPLY, 65536, 65536), 0, "MULTIPLY wraps");
        CHECK(op2(CS2_OP_MULTIPLY, 46341, 46341), -2147479015, "MULTIPLY wraps into the sign bit");
        CHECK(op2(CS2_OP_DIVIDE, INT32_MIN, -1), INT32_MIN, "INT_MIN / -1 is INT_MIN");
        CHECK(op2(CS2_OP_MODULO, INT32_MIN, -1), 0, "INT_MIN % -1 is 0");
        CHECK(op2(CS2_OP_DIVIDE, -7, 2), -3, "DIVIDE truncates toward zero");
        CHECK(op2(CS2_OP_MODULO, -7, 2), -1, "MODULO takes the dividend's sign");

        /* A divide by zero ends the script on both paths. */
        a2[0] = 9;
        a2[1] = 0;
        CHECK(run_op_path(CS2_OP_DIVIDE, a2, 2, 0, &top, &depth), CS2VM_EXECNO_ERROR, "DIVIDE by zero ends the script (hot loop)");
        CHECK(run_op_path(CS2_OP_DIVIDE, a2, 2, 1, &top, &depth), CS2VM_EXECNO_ERROR, "DIVIDE by zero ends the script (general path)");
        CHECK(run_op_path(CS2_OP_MODULO, a2, 2, 0, &top, &depth), CS2VM_EXECNO_ERROR, "MODULO by zero ends the script (hot loop)");
        CHECK(run_op_path(CS2_OP_MODULO, a2, 2, 1, &top, &depth), CS2VM_EXECNO_ERROR, "MODULO by zero ends the script (general path)");
    }

    if( g_fail )
    {
        printf("math ops test: %d FAILED\n", g_fail);
        return 1;
    }
    printf("math ops test: all passed\n");
    return 0;
}
