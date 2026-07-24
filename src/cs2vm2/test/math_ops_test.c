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

    if( g_fail )
    {
        printf("math ops test: %d FAILED\n", g_fail);
        return 1;
    }
    printf("math ops test: all passed\n");
    return 0;
}
