/*
 * Unit test for the CS2 long stack (rev 241): constants, locals, compares, the
 * long maths 4037..4063 and the two formatters 4125/4126, driven through the
 * real VM dispatch with hand-assembled scripts. Expected values are the rev-241
 * deob's (Statics: the core loop's 61..73, 4037..4063, 4125/4126), including
 * the places where the client throws and the script dies.
 */

#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_host.h"
#include "cs2vm2/cs2vm2_script.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;

#define CHECK_LONG(got, want, label)                                                               \
    do                                                                                             \
    {                                                                                              \
        long long gv = (long long)(got), wv = (long long)(want);                                  \
        if( gv == wv )                                                                             \
            printf("  ok: %s == %lld\n", label, gv);                                               \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s got %lld want %lld\n", label, gv, wv);                              \
            g_fail++;                                                                              \
        }                                                                                          \
    } while( 0 )

#define CHECK_STR(got, want, label)                                                                \
    do                                                                                             \
    {                                                                                              \
        char const* gv = (got);                                                                    \
        if( gv && strcmp(gv, (want)) == 0 )                                                        \
            printf("  ok: %s == \"%s\"\n", label, gv);                                             \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s got \"%s\" want \"%s\"\n", label, gv ? gv : "(null)", (want));     \
            g_fail++;                                                                              \
        }                                                                                          \
    } while( 0 )

/* One assembled instruction. `l` is PUSH_CONSTANT_LONG's operand, `s` a
 * PUSH_CONSTANT_STRING's. */
struct Ins
{
    int op;
    int i;
    int64_t l;
    char const* s;
};

#define LC(v) { CS2_OP_PUSH_CONSTANT_LONG, 0, (int64_t)(v), NULL }
#define IC(v) { CS2_OP_PUSH_CONSTANT_INT, (v), 0, NULL }
#define SC(v) { CS2_OP_PUSH_CONSTANT_STRING, 0, 0, (v) }
#define OP(o) { (o), 0, 0, NULL }
#define OPI(o, v) { (o), (v), 0, NULL }

struct Script
{
    struct CS2VM2_Script script;
};

static void
assemble(
    struct CS2VM2_Script* script,
    int script_id,
    struct Ins const* ins,
    int n)
{
    CS2VM2_ScriptInit(script);
    script->script_id = script_id;
    script->op_count = n + 1;
    script->opcodes = calloc((size_t)n + 1, sizeof(uint16_t));
    script->int_operands = calloc((size_t)n + 1, sizeof(int));
    script->long_operands = calloc((size_t)n + 1, sizeof(int64_t));
    script->string_operands = calloc((size_t)n + 1, sizeof(char*));
    for( int k = 0; k < n; k++ )
    {
        script->opcodes[k] = (uint16_t)ins[k].op;
        script->int_operands[k] = ins[k].i;
        script->long_operands[k] = ins[k].l;
        script->string_operands[k] = (char*)ins[k].s;
    }
    script->opcodes[n] = (uint16_t)CS2_OP_RETURN;
}

static void
disassemble(struct CS2VM2_Script* script)
{
    free(script->opcodes);
    free(script->int_operands);
    free(script->long_operands);
    free(script->string_operands);
}

/* GOSUB_WITH_PARAMS is the one host request these scripts make: it enters
 * g_callee, the way rs_cs2_host.c's exec_push_script does. */
static struct CS2VM2_Script* g_callee;

static int
test_host_exec(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request)
{
    if( request->kind == CS2VM_HOST_REQUEST_GOSUB_WITH_PARAMS && g_callee )
        return CS2VM2_PushCallScript(thread, g_callee);
    printf("  FAIL: unexpected host request %d\n", (int)request->kind);
    g_fail++;
    return CS2VM_EXECNO_ERROR;
}

struct Run
{
    int rc;
    int longs;
    int ints;
    int strs;
    int64_t long_top[4]; /* top first */
    int int_top[4];
    char str_top[64];
};

static struct Run
run(
    struct Ins const* ins,
    int n)
{
    struct CS2VM2 vm;
    struct CS2VM2_Script script;
    struct Run r;

    memset(&r, 0, sizeof(r));
    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, NULL, test_host_exec);
    assemble(&script, 9999, ins, n);

    struct CS2VM2_Thread* t = CS2VM2_ThreadMain(&vm);
    CS2VM2_ThreadStart(t, &script);
    r.rc = CS2VM2_RunScript(t);
    r.longs = t->longs_stack_top;
    r.ints = t->ints_stack_top;
    r.strs = t->strs_stack_top;
    for( int k = 0; k < 4 && k < t->longs_stack_top; k++ )
        r.long_top[k] = t->longs_stack[t->longs_stack_top - 1 - k];
    for( int k = 0; k < 4 && k < t->ints_stack_top; k++ )
        r.int_top[k] = t->ints_stack[t->ints_stack_top - 1 - k];
    if( t->strs_stack_top > 0 && t->strs_stack[t->strs_stack_top - 1] )
        snprintf(r.str_top, sizeof(r.str_top), "%s", t->strs_stack[t->strs_stack_top - 1]);

    CS2VM2_Free(&vm);
    disassemble(&script);
    return r;
}

/* (a, b) -> long, through `op`. */
static int64_t
long2(
    int op,
    int64_t a,
    int64_t b,
    int* rc)
{
    struct Ins ins[] = { LC(a), LC(b), OP(op) };
    struct Run r = run(ins, 3);
    *rc = r.rc;
    return r.long_top[0];
}

static void
test_constants_locals_compares(void)
{
    printf("constants, locals, discard, compares\n");
    {
        struct Ins ins[] = {
            LC(INT64_C(0x123456789A)),
            OPI(CS2_OP_POP_LONG_LOCAL, 3),
            OPI(CS2_OP_PUSH_LONG_LOCAL, 3),
            OPI(CS2_OP_PUSH_LONG_LOCAL, 0), /* never written: 0 */
            LC(-5),
            OP(CS2_OP_POP_LONG_DISCARD),
        };
        struct Run r = run(ins, 6);
        CHECK_LONG(r.rc, CS2VM_EXECNO_DONE, "script ran");
        CHECK_LONG(r.longs, 2, "two longs left");
        CHECK_LONG(r.long_top[0], 0, "unwritten long local");
        CHECK_LONG(r.long_top[1], INT64_C(0x123456789A), "long local round trip");
        CHECK_LONG(r.ints, 0, "int stack untouched");
    }

    /* Each compare jumps over one PUSH_CONSTANT_INT 0 to a PUSH_CONSTANT_INT 1. */
    struct
    {
        int op;
        int64_t a, b;
        int taken;
        char const* label;
    } const cases[] = {
        { CS2_OP_LONG_BRANCH_NOT, 1, 2, 1, "1L != 2L" },
        { CS2_OP_LONG_BRANCH_NOT, 7, 7, 0, "7L != 7L" },
        { CS2_OP_LONG_BRANCH_EQUALS, INT64_C(1) << 40, INT64_C(1) << 40, 1, "2^40 == 2^40" },
        { CS2_OP_LONG_BRANCH_EQUALS, INT64_C(1) << 40, 0, 0, "2^40 == 0" },
        { CS2_OP_LONG_BRANCH_LESS_THAN, -1, 0, 1, "-1L < 0L" },
        { CS2_OP_LONG_BRANCH_GREATER_THAN, INT64_C(5000000000), INT64_C(4000000000), 1, "5e9 > 4e9" },
        { CS2_OP_LONG_BRANCH_LESS_THAN_OR_EQUALS, 3, 3, 1, "3L <= 3L" },
        { CS2_OP_LONG_BRANCH_GREATER_THAN_OR_EQUALS, 2, 3, 0, "2L >= 3L" },
    };
    for( size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); k++ )
    {
        struct Ins ins[] = {
            LC(cases[k].a), LC(cases[k].b), OPI(cases[k].op, 2),
            IC(0), OPI(CS2_OP_BRANCH, 1),
            IC(1),
        };
        struct Run r = run(ins, 6);
        CHECK_LONG(r.ints == 1 ? r.int_top[0] : -1, cases[k].taken, cases[k].label);
    }
}

static void
test_maths(void)
{
    int rc;

    printf("long maths 4037..4043, 4055..4058\n");
    CHECK_LONG(long2(CS2_OP_LONG_ADD, INT64_C(3000000000), INT64_C(4000000000), &rc), INT64_C(7000000000), "long_add");
    long2(CS2_OP_LONG_ADD, INT64_MAX, 1, &rc);
    CHECK_LONG(rc, CS2VM_EXECNO_ERROR, "long_add overflow kills the script");
    long2(CS2_OP_LONG_SUB, INT64_MIN, 1, &rc);
    CHECK_LONG(rc, CS2VM_EXECNO_ERROR, "long_sub overflow kills the script");
    CHECK_LONG(long2(CS2_OP_LONG_SUB, 10, 25, &rc), -15, "long_sub");
    CHECK_LONG(long2(CS2_OP_LONG_MULTIPLY, INT64_C(2147483647), 100, &rc), INT64_C(214748364700), "long_multiply");
    long2(CS2_OP_LONG_MULTIPLY, INT64_C(1) << 62, 4, &rc);
    CHECK_LONG(rc, CS2VM_EXECNO_ERROR, "long_multiply overflow kills the script");
    /* Java: MIN * -1 wraps to MIN and MIN / -1 == MIN, so the check passes. */
    CHECK_LONG(long2(CS2_OP_LONG_MULTIPLY, INT64_MIN, -1, &rc), INT64_MIN, "long_multiply MIN * -1 (Java)");
    CHECK_LONG(long2(CS2_OP_LONG_DIVIDE, -7, 2, &rc), -3, "long_divide truncates");
    CHECK_LONG(long2(CS2_OP_LONG_DIVIDE, INT64_MIN, -1, &rc), INT64_MIN, "long_divide MIN / -1 (Java)");
    long2(CS2_OP_LONG_DIVIDE, 1, 0, &rc);
    CHECK_LONG(rc, CS2VM_EXECNO_ERROR, "long_divide by zero kills the script");
    CHECK_LONG(long2(CS2_OP_LONG_MODULO, -7, 3, &rc), -1, "long_modulo sign of dividend");
    CHECK_LONG(long2(CS2_OP_LONG_MODULO, INT64_MIN, -1, &rc), 0, "long_modulo MIN % -1 (Java)");
    CHECK_LONG(long2(CS2_OP_LONG_MIN, -2, 9, &rc), -2, "long_min");
    CHECK_LONG(long2(CS2_OP_LONG_MAX, -2, 9, &rc), 9, "long_max");
    CHECK_LONG(long2(CS2_OP_LONG_AND, 0xF0F0, 0xFF00, &rc), 0xF000, "long_and");
    CHECK_LONG(long2(CS2_OP_LONG_OR, 0xF0F0, 0x0F0F, &rc), 0xFFFF, "long_or");
    {
        struct Ins ins[] = { LC(0), OP(CS2_OP_LONG_NOT) };
        CHECK_LONG(run(ins, 2).long_top[0], -1, "long_not");
    }
    {
        /* scale(a, b, c) = a * c / b, wide: 2^62 * 6 / 4 fits only via 128 bits. */
        struct Ins ins[] = { LC(INT64_C(1) << 62), LC(4), LC(1), OP(CS2_OP_LONG_SCALE) };
        CHECK_LONG(run(ins, 4).long_top[0], INT64_C(1) << 60, "long_scale 2^62 * 1 / 4");
        struct Ins ins2[] = { LC(INT64_C(1) << 61), LC(4), LC(6), OP(CS2_OP_LONG_SCALE) };
        CHECK_LONG(run(ins2, 4).long_top[0], INT64_C(3) << 60, "long_scale 2^61 * 6 / 4 (wide)");
        struct Ins ins3[] = { LC(1), LC(0), LC(1), OP(CS2_OP_LONG_SCALE) };
        CHECK_LONG(run(ins3, 4).rc, CS2VM_EXECNO_ERROR, "long_scale by zero kills the script");
    }
}

static void
test_bits_and_conversions(void)
{
    printf("bits 4047..4054, 4059, conversions 4044, 4060..4063\n");
    {
        struct Ins ins[] = { LC(0), IC(40), OP(CS2_OP_LONG_SETBIT) };
        CHECK_LONG(run(ins, 3).long_top[0], INT64_C(1) << 40, "long_setbit 40");
        struct Ins ins2[] = { LC(0), IC(64 + 3), OP(CS2_OP_LONG_SETBIT) };
        CHECK_LONG(run(ins2, 3).long_top[0], 8, "long_setbit shift count mod 64");
        struct Ins ins3[] = { LC(-1), IC(63), OP(CS2_OP_LONG_CLEARBIT) };
        CHECK_LONG(run(ins3, 3).long_top[0], INT64_MAX, "long_clearbit 63");
        struct Ins ins4[] = { LC(INT64_C(1) << 33), IC(33), OP(CS2_OP_LONG_TESTBIT) };
        struct Run r = run(ins4, 3);
        CHECK_LONG(r.int_top[0], 1, "long_testbit 33");
        CHECK_LONG(r.longs, 0, "long_testbit consumed its long");
        struct Ins ins5[] = { LC(5), IC(0), OP(CS2_OP_LONG_TOGGLEBIT) };
        CHECK_LONG(run(ins5, 3).long_top[0], 4, "long_togglebit 0");
        struct Ins ins6[] = { LC(-1), OP(CS2_OP_LONG_BITCOUNT) };
        CHECK_LONG(run(ins6, 2).int_top[0], 64, "long_bitcount -1");
    }
    {
        struct Ins ins[] = { LC(0), IC(4), IC(35), OP(CS2_OP_LONG_SETBIT_RANGE) };
        CHECK_LONG(run(ins, 4).long_top[0], (int64_t)((INT64_C(1) << 36) - 16), "long_setbit_range 4..35");
        struct Ins ins2[] = { LC(-1), IC(-5), IC(99), OP(CS2_OP_LONG_CLEARBIT_RANGE) };
        CHECK_LONG(run(ins2, 4).long_top[0], 0, "long_clearbit_range clamps to 0..63");
        struct Ins ins3[] = { LC(INT64_C(0xABCD) << 32), IC(36), IC(43), OP(CS2_OP_LONG_GETBIT_RANGE) };
        CHECK_LONG(run(ins3, 4).long_top[0], 0xBC, "long_getbit_range 36..43");
        struct Ins ins4[] = { LC(0), IC(10), IC(2), OP(CS2_OP_LONG_SETBIT_RANGE) };
        CHECK_LONG(run(ins4, 4).rc, CS2VM_EXECNO_ERROR, "long_setbit_range high < low kills the script");
        struct Ins ins5[] = { LC(-1), LC(5), IC(8), IC(11), OP(CS2_OP_LONG_SETBIT_RANGE_TOLONG) };
        CHECK_LONG(run(ins5, 5).long_top[0], (int64_t)~(INT64_C(0xA) << 8), "long_setbit_range_tolong 8..11 = 5");
        struct Ins ins6[] = { LC(0), LC(99), IC(0), IC(3), OP(CS2_OP_LONG_SETBIT_RANGE_TOLONG) };
        CHECK_LONG(run(ins6, 5).long_top[0], 15, "long_setbit_range_tolong caps the value");
    }
    {
        struct Ins ins[] = { IC(-2), OP(CS2_OP_INT_TO_LONG) };
        CHECK_LONG(run(ins, 2).long_top[0], -2, "int_to_long sign-extends");
        struct Ins ins2[] = { IC(1), IC(-1), OP(CS2_OP_LONG_PACK) };
        CHECK_LONG(run(ins2, 3).long_top[0], INT64_C(0x1FFFFFFFF), "long_pack(1, -1)");
        struct Ins ins3[] = { LC(INT64_C(0x00000007FFFFFFFE)), OP(CS2_OP_LONG_UNPACK) };
        struct Run r = run(ins3, 2);
        CHECK_LONG(r.int_top[1], 7, "long_unpack high pushed first");
        CHECK_LONG(r.int_top[0], -2, "long_unpack low on top");
        struct Ins ins4[] = { LC(INT64_C(0x1234567880000001)), OP(CS2_OP_LONG_TO_INT) };
        CHECK_LONG(run(ins4, 2).int_top[0], (int)0x80000001u, "long_to_int keeps the low half");
        struct Ins ins5[] = { LC(INT64_C(0x1234567880000001)), OP(CS2_OP_LONG_HIGH_TO_INT) };
        CHECK_LONG(run(ins5, 2).int_top[0], 0x12345678, "long_high_to_int");
    }
}

static void
test_tostring(void)
{
    printf("tostring_long / tostring_spacer_long\n");
    struct
    {
        int64_t v;
        char const* sep;
        char const* want;
    } const cases[] = {
        { INT64_C(2147483648000), ",", "2,147,483,648,000" },
        { -1234, ",", "-1,234" },
        { 999, ",", "999" },
        { 0, ",", "0" },
        { 1234567, "", "1234567" },
        { 1234567, " ", "1 234 567" },
        { INT64_MIN, ",", "-" }, /* the reference's own loop: -MIN is MIN */
    };
    for( size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); k++ )
    {
        struct Ins ins[] = { LC(cases[k].v), SC(cases[k].sep), OP(CS2_OP_TOSTRING_SPACER_LONG) };
        struct Run r = run(ins, 3);
        CHECK_STR(r.str_top, cases[k].want, cases[k].want);
    }
    struct Ins ins[] = { LC(INT64_MIN), OP(CS2_OP_TOSTRING_LONG) };
    CHECK_STR(run(ins, 2).str_top, "-9223372036854775808", "tostring_long MIN");
}

static void
test_gosub_long_args(void)
{
    printf("gosub: long args land in the callee's long locals, a long return stays\n");
    /* callee(int, long a, long b) returns a - b + int. */
    struct CS2VM2_Script callee;
    struct Ins body[] = {
        OPI(CS2_OP_PUSH_LONG_LOCAL, 0),
        OPI(CS2_OP_PUSH_LONG_LOCAL, 1),
        OP(CS2_OP_LONG_SUB),
        OPI(CS2_OP_PUSH_INT_LOCAL, 0),
        OP(CS2_OP_INT_TO_LONG),
        OP(CS2_OP_LONG_ADD),
    };
    assemble(&callee, 9998, body, 6);
    callee.int_argument_count = 1;
    callee.long_argument_count = 2;
    callee.local_int_count = 1;
    callee.local_long_count = 2;
    g_callee = &callee;

    struct Ins ins[] = {
        IC(5), LC(INT64_C(10000000000)), LC(3), OPI(CS2_OP_GOSUB_WITH_PARAMS, 9998),
    };
    struct Run r = run(ins, 4);
    CHECK_LONG(r.rc, CS2VM_EXECNO_DONE, "gosub ran");
    CHECK_LONG(r.longs, 1, "one long returned");
    CHECK_LONG(r.long_top[0], INT64_C(10000000002), "10e9 - 3 + 5");
    CHECK_LONG(r.ints, 0, "the int arg was consumed");
    g_callee = NULL;
    disassemble(&callee);
}

int
main(void)
{
    test_constants_locals_compares();
    test_maths();
    test_bits_and_conversions();
    test_tostring();
    test_gosub_long_args();
    if( g_fail )
    {
        printf("long ops: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("long ops: all passed\n");
    return 0;
}
