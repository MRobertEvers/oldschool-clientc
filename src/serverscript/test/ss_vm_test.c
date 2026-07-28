/*
 * The VM: core language, arithmetic, strings, frames, limits, suspend/resume.
 *
 * Scripts are built in memory, encoded, and loaded back through the real
 * provider rather than being handed to the VM directly. That costs a few lines
 * and buys a lot: every case here also exercises the encoder and the container,
 * so a test that passes is testing the path the mock server actually uses.
 *
 * No corpus needed — this runs anywhere.
 */

#include "ss_opcode.h"
#include "ssvm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;

#define CHECK(cond, msg)                                                       \
    do                                                                         \
    {                                                                          \
        if( cond )                                                             \
        {                                                                      \
            printf("  ok   %s\n", (msg));                                      \
        }                                                                      \
        else                                                                   \
        {                                                                      \
            printf("  FAIL %s (%s:%d)\n", (msg), __FILE__, __LINE__);          \
            g_fail++;                                                          \
        }                                                                      \
    } while( 0 )

#define CHECK_EQ(got, want, msg)                                               \
    do                                                                         \
    {                                                                          \
        long long gv = (long long)(got), wv = (long long)(want);               \
        if( gv == wv )                                                         \
        {                                                                      \
            printf("  ok   %s == %lld\n", (msg), gv);                          \
        }                                                                      \
        else                                                                   \
        {                                                                      \
            printf("  FAIL %s: got %lld want %lld (%s:%d)\n", (msg), gv, wv,   \
                   __FILE__, __LINE__);                                        \
            g_fail++;                                                          \
        }                                                                      \
    } while( 0 )

/* ------------------------------------------------------------------ */
/* Script builder                                                      */
/* ------------------------------------------------------------------ */

enum
{
    BUILD_MAX_OPS = 4096,
    BUILD_MAX_TABLES = 4,
    BUILD_MAX_CASES = 16,
};

struct Builder
{
    struct SSVM_Script script;
    uint16_t opcodes[BUILD_MAX_OPS];
    int32_t int_operands[BUILD_MAX_OPS];
    char* string_operands[BUILD_MAX_OPS];
    struct SSVM_SwitchTable tables[BUILD_MAX_TABLES];
    struct SSVM_SwitchCase cases[BUILD_MAX_TABLES][BUILD_MAX_CASES];
    char name[64];
};

static void
build_init(struct Builder* builder, const char* name, int32_t lookup_key)
{
    memset(builder, 0, sizeof(*builder));
    snprintf(builder->name, sizeof(builder->name), "%s", name);
    builder->script.name = builder->name;
    builder->script.source_path = (char*)"test.rs2";
    builder->script.lookup_key = lookup_key;
    builder->script.opcodes = builder->opcodes;
    builder->script.int_operands = builder->int_operands;
    builder->script.string_operands = builder->string_operands;
    builder->script.switch_tables = builder->tables;
}

/** Returns the instruction index, so branch deltas can be computed. */
static int
emit(struct Builder* builder, int opcode, int32_t operand)
{
    int index = builder->script.op_count;

    builder->opcodes[index] = (uint16_t)opcode;
    builder->int_operands[index] = operand;
    builder->script.op_count++;
    return index;
}

static int
emit_str(struct Builder* builder, const char* text)
{
    int index = emit(builder, SS_OP_PUSH_CONSTANT_STRING, 0);

    builder->string_operands[index] = (char*)text;
    return index;
}

/** Patch a branch emitted earlier so it lands on the next instruction. */
static void
patch_branch_to_here(struct Builder* builder, int branch_index)
{
    /* The VM does pc += delta and then ++pc, so landing on instruction N from
     * a branch at index B needs delta = N - B - 1. */
    builder->int_operands[branch_index] = builder->script.op_count - branch_index - 1;
}

static int
add_switch_table(struct Builder* builder, int case_count)
{
    int index = builder->script.switch_table_count++;

    builder->tables[index].cases = builder->cases[index];
    builder->tables[index].case_count = (uint16_t)case_count;
    return index;
}

/* ------------------------------------------------------------------ */
/* Container assembly                                                  */
/* ------------------------------------------------------------------ */

/**
 * Encode a set of scripts into a script.dat/.idx pair and load them.
 *
 * Going through the real container rather than pointing the VM at the builder
 * structs directly means these tests also cover the encoder, the decoder and
 * the index — and it is the only way gosub can resolve a script id, since the
 * VM resolves those through the provider.
 */
static int
load_builders(struct SSVM_Provider* provider, struct Builder* builders, int count)
{
    uint8_t* dat;
    uint8_t* idx;
    size_t dat_capacity = 0;
    size_t dat_used = 8;
    struct SSVM_Error err;
    int i;
    int ok;

    SSVM_ErrorClear(&err);

    for( i = 0; i < count; i++ )
    {
        size_t needed = 0;

        builders[i].script.id = i;
        if( !SSVM_ScriptEncode(&builders[i].script, NULL, 0, &needed, &err) )
        {
            printf("  FAIL sizing script %d: %s\n", i, err.message);
            g_fail++;
            return 0;
        }
        dat_capacity += needed;
    }
    dat_capacity += 8;

    dat = (uint8_t*)calloc(dat_capacity, 1);
    idx = (uint8_t*)calloc((size_t)(4 + count * 4), 1);

    /* Both headers are big-endian i32s: entry count, then compiler version. */
    dat[0] = (uint8_t)(count >> 24);
    dat[1] = (uint8_t)(count >> 16);
    dat[2] = (uint8_t)(count >> 8);
    dat[3] = (uint8_t)count;
    dat[4] = 0;
    dat[5] = 0;
    dat[6] = 0;
    dat[7] = SSVM_COMPILER_VERSION;
    memcpy(idx, dat, 4);

    for( i = 0; i < count; i++ )
    {
        size_t written = 0;

        if( !SSVM_ScriptEncode(
                &builders[i].script, dat + dat_used, dat_capacity - dat_used, &written, &err) )
        {
            printf("  FAIL encoding script %d: %s\n", i, err.message);
            g_fail++;
            free(dat);
            free(idx);
            return 0;
        }
        idx[4 + i * 4 + 0] = (uint8_t)(written >> 24);
        idx[4 + i * 4 + 1] = (uint8_t)(written >> 16);
        idx[4 + i * 4 + 2] = (uint8_t)(written >> 8);
        idx[4 + i * 4 + 3] = (uint8_t)written;
        dat_used += written;
    }

    ok = SSVM_ProviderLoadMem(provider, dat, dat_used, idx, (size_t)(4 + count * 4), &err);
    if( !ok )
    {
        printf("  FAIL loading container: %s\n", err.message);
        g_fail++;
    }
    free(dat);
    free(idx);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Running                                                             */
/* ------------------------------------------------------------------ */

struct Fixture
{
    struct SSVM_Provider provider;
    struct SSVM_Env env;
};

static int
fixture_open(struct Fixture* fixture, struct Builder* builders, int count)
{
    if( !load_builders(&fixture->provider, builders, count) )
        return 0;
    SSVM_EnvInit(&fixture->env, &fixture->provider);
    return 1;
}

static void
fixture_close(struct Fixture* fixture)
{
    SSVM_EnvFree(&fixture->env);
    SSVM_ProviderFree(&fixture->provider);
}

/** Run script 0 and report the top of the int stack. */
static int
run_expect_int(struct Builder* builders, int count, int32_t* out, const char* label)
{
    struct Fixture fixture;
    struct SSVM_State* state;
    enum SSVM_Exec status;
    int ok = 0;

    if( !fixture_open(&fixture, builders, count) )
        return 0;

    state = SSVM_StateAlloc(&fixture.env, SSVM_ProviderGet(&fixture.provider, 0), NULL, 0, NULL, 0);
    if( !state )
    {
        printf("  FAIL %s: could not allocate a state\n", label);
        g_fail++;
        fixture_close(&fixture);
        return 0;
    }

    status = SSVM_Execute(state);
    if( status != SSVM_FINISHED )
    {
        printf("  FAIL %s: %s\n", label, SSVM_Backtrace(state));
        g_fail++;
    }
    else if( state->isp < 1 )
    {
        printf("  FAIL %s: nothing on the int stack\n", label);
        g_fail++;
    }
    else
    {
        *out = state->int_stack[state->isp - 1];
        ok = 1;
    }

    SSVM_StateRelease(state);
    fixture_close(&fixture);
    return ok;
}

/** Emit `PUSH a; PUSH b; <op>; RETURN` and report the result. */
static int32_t
binary_op(int opcode, int32_t left, int32_t right, const char* label)
{
    struct Builder builders[1];
    int32_t result = 0;

    build_init(&builders[0], "[proc,binary]", -1);
    emit(&builders[0], SS_OP_PUSH_CONSTANT_INT, left);
    emit(&builders[0], SS_OP_PUSH_CONSTANT_INT, right);
    emit(&builders[0], opcode, 0);
    emit(&builders[0], SS_OP_RETURN, 0);

    run_expect_int(builders, 1, &result, label);
    return result;
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

static void
test_constants_and_locals(void)
{
    struct Builder builders[1];
    int32_t result = 0;

    printf("constants and locals\n");

    build_init(&builders[0], "[proc,locals]", -1);
    builders[0].script.int_local_count = 3;
    emit(&builders[0], SS_OP_PUSH_CONSTANT_INT, 41);
    emit(&builders[0], SS_OP_POP_INT_LOCAL, 2);
    emit(&builders[0], SS_OP_PUSH_INT_LOCAL, 2);
    emit(&builders[0], SS_OP_PUSH_CONSTANT_INT, 1);
    emit(&builders[0], SS_OP_ADD, 0);
    emit(&builders[0], SS_OP_RETURN, 0);

    if( run_expect_int(builders, 1, &result, "locals") )
        CHECK_EQ(result, 42, "an int local round-trips");
}

static void
test_branches(void)
{
    static const struct
    {
        int opcode;
        int32_t left;
        int32_t right;
        int taken;
        const char* label;
    } k_cases[] = {
        { SS_OP_BRANCH_EQUALS, 5, 5, 1, "branch_equals taken" },
        { SS_OP_BRANCH_EQUALS, 5, 6, 0, "branch_equals not taken" },
        { SS_OP_BRANCH_NOT, 5, 6, 1, "branch_not taken" },
        { SS_OP_BRANCH_NOT, 5, 5, 0, "branch_not not taken" },
        { SS_OP_BRANCH_LESS_THAN, 4, 5, 1, "branch_less_than taken" },
        { SS_OP_BRANCH_LESS_THAN, 5, 5, 0, "branch_less_than not taken" },
        { SS_OP_BRANCH_GREATER_THAN, 6, 5, 1, "branch_greater_than taken" },
        { SS_OP_BRANCH_GREATER_THAN, 5, 5, 0, "branch_greater_than not taken" },
        { SS_OP_BRANCH_LESS_THAN_OR_EQUALS, 5, 5, 1, "branch_lte taken" },
        { SS_OP_BRANCH_LESS_THAN_OR_EQUALS, 6, 5, 0, "branch_lte not taken" },
        { SS_OP_BRANCH_GREATER_THAN_OR_EQUALS, 5, 5, 1, "branch_gte taken" },
        { SS_OP_BRANCH_GREATER_THAN_OR_EQUALS, 4, 5, 0, "branch_gte not taken" },
    };
    size_t i;

    printf("branches\n");

    for( i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++ )
    {
        struct Builder builders[1];
        int32_t result = 0;
        int branch;

        build_init(&builders[0], "[proc,branch]", -1);
        emit(&builders[0], SS_OP_PUSH_CONSTANT_INT, k_cases[i].left);
        emit(&builders[0], SS_OP_PUSH_CONSTANT_INT, k_cases[i].right);
        branch = emit(&builders[0], k_cases[i].opcode, 0);
        emit(&builders[0], SS_OP_PUSH_CONSTANT_INT, 100); /* not-taken path */
        emit(&builders[0], SS_OP_RETURN, 0);
        patch_branch_to_here(&builders[0], branch);
        emit(&builders[0], SS_OP_PUSH_CONSTANT_INT, 200); /* taken path */
        emit(&builders[0], SS_OP_RETURN, 0);

        if( run_expect_int(builders, 1, &result, k_cases[i].label) )
            CHECK_EQ(result, k_cases[i].taken ? 200 : 100, k_cases[i].label);
    }
}

static void
test_backward_branch(void)
{
    struct Builder builders[1];
    int32_t result = 0;
    int loop_top;
    int branch;

    printf("backward branch\n");

    /* Count 0..9 with a negative delta. The corpus contains deltas as low as
     * -268, so a decoder that read the operand unsigned would loop forever
     * here rather than terminate. */
    build_init(&builders[0], "[proc,loop]", -1);
    builders[0].script.int_local_count = 1;

    loop_top = emit(&builders[0], SS_OP_PUSH_INT_LOCAL, 0);
    emit(&builders[0], SS_OP_PUSH_CONSTANT_INT, 1);
    emit(&builders[0], SS_OP_ADD, 0);
    emit(&builders[0], SS_OP_POP_INT_LOCAL, 0);
    emit(&builders[0], SS_OP_PUSH_INT_LOCAL, 0);
    emit(&builders[0], SS_OP_PUSH_CONSTANT_INT, 10);
    branch = emit(&builders[0], SS_OP_BRANCH_LESS_THAN, 0);
    builders[0].int_operands[branch] = loop_top - branch - 1;
    emit(&builders[0], SS_OP_PUSH_INT_LOCAL, 0);
    emit(&builders[0], SS_OP_RETURN, 0);

    if( run_expect_int(builders, 1, &result, "backward branch") )
        CHECK_EQ(result, 10, "a negative branch delta loops and terminates");
}

static void
test_switch(void)
{
    static const struct
    {
        int32_t key;
        int32_t expect;
        const char* label;
    } k_cases[] = {
        { 1, 11, "switch hits case 1" },
        { 2, 22, "switch hits case 2" },
        { 99, 99, "switch falls through on a miss" },
    };
    size_t i;

    printf("switch\n");

    for( i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++ )
    {
        struct Builder builders[1];
        int32_t result = 0;
        int table;
        int switch_index;
        int arm1;
        int arm2;

        build_init(&builders[0], "[proc,switch]", -1);
        table = add_switch_table(&builders[0], 2);

        emit(&builders[0], SS_OP_PUSH_CONSTANT_INT, k_cases[i].key);
        switch_index = emit(&builders[0], SS_OP_SWITCH, table);
        emit(&builders[0], SS_OP_PUSH_CONSTANT_INT, 99); /* default arm */
        emit(&builders[0], SS_OP_RETURN, 0);

        arm1 = builders[0].script.op_count;
        emit(&builders[0], SS_OP_PUSH_CONSTANT_INT, 11);
        emit(&builders[0], SS_OP_RETURN, 0);

        arm2 = builders[0].script.op_count;
        emit(&builders[0], SS_OP_PUSH_CONSTANT_INT, 22);
        emit(&builders[0], SS_OP_RETURN, 0);

        builders[0].cases[table][0].key = 1;
        builders[0].cases[table][0].delta = arm1 - switch_index - 1;
        builders[0].cases[table][1].key = 2;
        builders[0].cases[table][1].delta = arm2 - switch_index - 1;

        if( run_expect_int(builders, 1, &result, k_cases[i].label) )
            CHECK_EQ(result, k_cases[i].expect, k_cases[i].label);
    }
}

static void
test_gosub(void)
{
    struct Builder builders[2];
    int32_t result = 0;

    printf("gosub with params\n");

    /* Callee: two int args, returns arg0 - arg1. Argument ORDER is the point:
     * they are popped in reverse so `~sub(10, 3)` gives $arg0 == 10. Getting
     * this backwards is invisible for symmetric operations. */
    build_init(&builders[1], "[proc,callee]", -1);
    builders[1].script.int_arg_count = 2;
    builders[1].script.int_local_count = 2;
    emit(&builders[1], SS_OP_PUSH_INT_LOCAL, 0);
    emit(&builders[1], SS_OP_PUSH_INT_LOCAL, 1);
    emit(&builders[1], SS_OP_SUB, 0);
    emit(&builders[1], SS_OP_RETURN, 0);

    build_init(&builders[0], "[proc,caller]", -1);
    emit(&builders[0], SS_OP_PUSH_CONSTANT_INT, 10);
    emit(&builders[0], SS_OP_PUSH_CONSTANT_INT, 3);
    emit(&builders[0], SS_OP_GOSUB_WITH_PARAMS, 1);
    emit(&builders[0], SS_OP_RETURN, 0);

    if( run_expect_int(builders, 2, &result, "gosub") )
        CHECK_EQ(result, 7, "arguments bind in source order");
}

static void
test_gosub_depth(void)
{
    struct Builder builders[1];
    struct Fixture fixture;
    struct SSVM_State* state;
    enum SSVM_Exec status;

    printf("gosub depth limit\n");

    /* Unbounded recursion. The reference caps the frame stack at 50; without a
     * cap this would run to the instruction budget or smash the frame array. */
    build_init(&builders[0], "[proc,recurse]", -1);
    emit(&builders[0], SS_OP_GOSUB_WITH_PARAMS, 0);
    emit(&builders[0], SS_OP_RETURN, 0);

    if( !fixture_open(&fixture, builders, 1) )
        return;

    state = SSVM_StateAlloc(&fixture.env, SSVM_ProviderGet(&fixture.provider, 0), NULL, 0, NULL, 0);
    status = SSVM_Execute(state);

    CHECK(status == SSVM_ABORTED, "runaway recursion aborts");
    CHECK(strstr(state->err.message, "overflow") != NULL, "the message names a stack overflow");
    CHECK_EQ(state->fp, SSVM_MAX_FRAMES, "it stopped at the frame cap");

    SSVM_StateRelease(state);
    fixture_close(&fixture);
}

static void
test_op_budget(void)
{
    struct Builder builders[1];
    struct Fixture fixture;
    struct SSVM_State* state;
    enum SSVM_Exec status;
    int branch;

    printf("instruction budget\n");

    /* An infinite loop with no recursion: only the op budget can stop it. */
    build_init(&builders[0], "[proc,spin]", -1);
    branch = emit(&builders[0], SS_OP_BRANCH, 0);
    builders[0].int_operands[branch] = -1; /* branch to itself */
    emit(&builders[0], SS_OP_RETURN, 0);

    if( !fixture_open(&fixture, builders, 1) )
        return;

    state = SSVM_StateAlloc(&fixture.env, SSVM_ProviderGet(&fixture.provider, 0), NULL, 0, NULL, 0);
    status = SSVM_Execute(state);

    CHECK(status == SSVM_ABORTED, "an infinite loop aborts");
    CHECK(strstr(state->err.message, "instructions") != NULL, "the message names the budget");

    SSVM_StateRelease(state);
    fixture_close(&fixture);
}

static void
test_stack_underflow(void)
{
    struct Builder builders[1];
    struct Fixture fixture;
    struct SSVM_State* state;
    enum SSVM_Exec status;

    printf("stack underflow\n");

    build_init(&builders[0], "[proc,underflow]", -1);
    emit(&builders[0], SS_OP_PUSH_CONSTANT_INT, 1);
    emit(&builders[0], SS_OP_ADD, 0); /* needs two, has one */
    emit(&builders[0], SS_OP_RETURN, 0);

    if( !fixture_open(&fixture, builders, 1) )
        return;

    state = SSVM_StateAlloc(&fixture.env, SSVM_ProviderGet(&fixture.provider, 0), NULL, 0, NULL, 0);
    status = SSVM_Execute(state);

    /* The reference returns 0 for a pop past the bottom, which turns a stack
     * discipline bug into a wrong answer far downstream. */
    CHECK(status == SSVM_ABORTED, "popping an empty stack aborts rather than yielding 0");
    CHECK(strstr(state->err.message, "underflow") != NULL, "the message names the underflow");

    SSVM_StateRelease(state);
    fixture_close(&fixture);
}

static void
test_number_ops(void)
{
    printf("arithmetic\n");

    CHECK_EQ(binary_op(SS_OP_ADD, 20, 22, "add"), 42, "add");
    CHECK_EQ(binary_op(SS_OP_SUB, 50, 8, "sub"), 42, "sub");
    CHECK_EQ(binary_op(SS_OP_MULTIPLY, 6, 7, "multiply"), 42, "multiply");
    CHECK_EQ(binary_op(SS_OP_DIVIDE, 85, 2, "divide"), 42, "divide truncates toward zero");
    CHECK_EQ(binary_op(SS_OP_DIVIDE, -85, 2, "divide negative"), -42,
             "negative divide truncates toward zero");
    CHECK_EQ(binary_op(SS_OP_MODULO, 100, 29, "modulo"), 13, "modulo");
    CHECK_EQ(binary_op(SS_OP_MODULO, -100, 29, "modulo negative"), -13,
             "modulo keeps the dividend's sign");
    CHECK_EQ(binary_op(SS_OP_AND, 0xf0, 0x3c, "and"), 0x30, "and");
    CHECK_EQ(binary_op(SS_OP_OR, 0xf0, 0x0c, "or"), 0xfc, "or");
    CHECK_EQ(binary_op(SS_OP_MIN, 3, 9, "min"), 3, "min");
    CHECK_EQ(binary_op(SS_OP_MAX, 3, 9, "max"), 9, "max");
    CHECK_EQ(binary_op(SS_OP_POW, 2, 10, "pow"), 1024, "pow");
    CHECK_EQ(binary_op(SS_OP_POW, 2, -1, "pow negative"), 0,
             "a negative exponent truncates to zero");
    CHECK_EQ(binary_op(SS_OP_INVPOW, 144, 2, "invpow"), 12, "invpow square root");
    CHECK_EQ(binary_op(SS_OP_INVPOW, 27, 3, "invpow cbrt"), 3, "invpow cube root");
    CHECK_EQ(binary_op(SS_OP_SETBIT, 0, 4, "setbit"), 16, "setbit");
    CHECK_EQ(binary_op(SS_OP_CLEARBIT, 0xff, 4, "clearbit"), 0xef, "clearbit");
    CHECK_EQ(binary_op(SS_OP_TESTBIT, 0x10, 4, "testbit"), 1, "testbit");
    CHECK_EQ(binary_op(SS_OP_TOGGLEBIT, 0x10, 4, "togglebit"), 0, "togglebit");
    CHECK_EQ(binary_op(SS_OP_ADDPERCENT, 200, 50, "addpercent"), 300, "addpercent");

    /* MULTIPLY wraps at 32 bits in the reference (every push goes through
     * ToInt32), so a product that overflows must wrap here too rather than
     * saturating or promoting. */
    CHECK_EQ(binary_op(SS_OP_MULTIPLY, 65536, 65536, "multiply overflow"), 0,
             "multiply wraps at 32 bits");
}

static void
test_divide_by_zero(void)
{
    struct Builder builders[1];
    struct Fixture fixture;
    struct SSVM_State* state;
    enum SSVM_Exec status;

    printf("divide by zero\n");

    build_init(&builders[0], "[proc,divzero]", -1);
    emit(&builders[0], SS_OP_PUSH_CONSTANT_INT, 1);
    emit(&builders[0], SS_OP_PUSH_CONSTANT_INT, 0);
    emit(&builders[0], SS_OP_DIVIDE, 0);
    emit(&builders[0], SS_OP_RETURN, 0);

    if( !fixture_open(&fixture, builders, 1) )
        return;

    state = SSVM_StateAlloc(&fixture.env, SSVM_ProviderGet(&fixture.provider, 0), NULL, 0, NULL, 0);
    status = SSVM_Execute(state);

    /* The reference yields 0 (JS Infinity through ToInt32). Aborting is a
     * deliberate divergence: dividing by zero in content is a bug, and a
     * plausible number hides it. */
    CHECK(status == SSVM_ABORTED, "divide by zero aborts instead of yielding 0");

    SSVM_StateRelease(state);
    fixture_close(&fixture);
}

/* ------------------------------------------------------------------ */
/* Strings                                                             */
/* ------------------------------------------------------------------ */

/** Run script 0 and report the top of the string stack. */
static int
run_expect_str(struct Builder* builders, int count, char* out, size_t capacity,
               const char* label)
{
    struct Fixture fixture;
    struct SSVM_State* state;
    enum SSVM_Exec status;
    int ok = 0;

    if( !fixture_open(&fixture, builders, count) )
        return 0;

    state = SSVM_StateAlloc(&fixture.env, SSVM_ProviderGet(&fixture.provider, 0), NULL, 0, NULL, 0);
    status = SSVM_Execute(state);

    if( status != SSVM_FINISHED )
    {
        printf("  FAIL %s: %s\n", label, SSVM_Backtrace(state));
        g_fail++;
    }
    else if( state->ssp < 1 )
    {
        printf("  FAIL %s: nothing on the string stack\n", label);
        g_fail++;
    }
    else
    {
        snprintf(out, capacity, "%s", state->str_stack[state->ssp - 1]);
        ok = 1;
    }

    SSVM_StateRelease(state);
    fixture_close(&fixture);
    return ok;
}

static void
test_join_string(void)
{
    struct Builder builders[1];
    char result[128];

    printf("join_string\n");

    /* JOIN_STRING pops `operand` strings and concatenates them back in push
     * order — the reference pops then reverses. Reversed output is invisible
     * for a single part and for symmetric inputs. */
    build_init(&builders[0], "[proc,join]", -1);
    emit_str(&builders[0], "a");
    emit_str(&builders[0], "b");
    emit_str(&builders[0], "c");
    emit(&builders[0], SS_OP_JOIN_STRING, 3);
    emit(&builders[0], SS_OP_RETURN, 0);

    if( run_expect_str(builders, 1, result, sizeof(result), "join 3") )
        CHECK_EQ(strcmp(result, "abc"), 0, "join_string keeps push order");

    build_init(&builders[0], "[proc,join0]", -1);
    emit(&builders[0], SS_OP_JOIN_STRING, 0);
    emit(&builders[0], SS_OP_RETURN, 0);

    if( run_expect_str(builders, 1, result, sizeof(result), "join 0") )
        CHECK_EQ(strcmp(result, ""), 0, "join_string of nothing is empty");
}

static void
test_string_ops(void)
{
    struct Builder builders[1];
    char result[128];
    int32_t number = 0;

    printf("string ops\n");

    build_init(&builders[0], "[proc,append]", -1);
    emit_str(&builders[0], "hello ");
    emit_str(&builders[0], "world");
    emit(&builders[0], SS_OP_APPEND, 0);
    emit(&builders[0], SS_OP_RETURN, 0);
    if( run_expect_str(builders, 1, result, sizeof(result), "append") )
        CHECK_EQ(strcmp(result, "hello world"), 0, "append");

    build_init(&builders[0], "[proc,appendnum]", -1);
    emit(&builders[0], SS_OP_PUSH_CONSTANT_INT, 42);
    emit_str(&builders[0], "level ");
    emit(&builders[0], SS_OP_APPEND_NUM, 0);
    emit(&builders[0], SS_OP_RETURN, 0);
    if( run_expect_str(builders, 1, result, sizeof(result), "append_num") )
        CHECK_EQ(strcmp(result, "level 42"), 0, "append_num");

    build_init(&builders[0], "[proc,signnum]", -1);
    emit(&builders[0], SS_OP_PUSH_CONSTANT_INT, 7);
    emit_str(&builders[0], "bonus ");
    emit(&builders[0], SS_OP_APPEND_SIGNNUM, 0);
    emit(&builders[0], SS_OP_RETURN, 0);
    if( run_expect_str(builders, 1, result, sizeof(result), "append_signnum") )
        CHECK_EQ(strcmp(result, "bonus +7"), 0, "append_signnum signs a positive");

    build_init(&builders[0], "[proc,lower]", -1);
    emit_str(&builders[0], "MiXeD Case");
    emit(&builders[0], SS_OP_LOWERCASE, 0);
    emit(&builders[0], SS_OP_RETURN, 0);
    if( run_expect_str(builders, 1, result, sizeof(result), "lowercase") )
        CHECK_EQ(strcmp(result, "mixed case"), 0, "lowercase");

    build_init(&builders[0], "[proc,tostring]", -1);
    emit(&builders[0], SS_OP_PUSH_CONSTANT_INT, -17);
    emit(&builders[0], SS_OP_TOSTRING, 0);
    emit(&builders[0], SS_OP_RETURN, 0);
    if( run_expect_str(builders, 1, result, sizeof(result), "tostring") )
        CHECK_EQ(strcmp(result, "-17"), 0, "tostring");

    build_init(&builders[0], "[proc,substring]", -1);
    emit(&builders[0], SS_OP_PUSH_CONSTANT_INT, 6);
    emit(&builders[0], SS_OP_PUSH_CONSTANT_INT, 11);
    emit_str(&builders[0], "hello world");
    emit(&builders[0], SS_OP_SUBSTRING, 0);
    emit(&builders[0], SS_OP_RETURN, 0);
    if( run_expect_str(builders, 1, result, sizeof(result), "substring") )
        CHECK_EQ(strcmp(result, "world"), 0, "substring");

    /* JS substring clamps out-of-range ends rather than throwing. */
    build_init(&builders[0], "[proc,substring_clamp]", -1);
    emit(&builders[0], SS_OP_PUSH_CONSTANT_INT, 3);
    emit(&builders[0], SS_OP_PUSH_CONSTANT_INT, 999);
    emit_str(&builders[0], "abcdef");
    emit(&builders[0], SS_OP_SUBSTRING, 0);
    emit(&builders[0], SS_OP_RETURN, 0);
    if( run_expect_str(builders, 1, result, sizeof(result), "substring clamp") )
        CHECK_EQ(strcmp(result, "def"), 0, "substring clamps a past-the-end index");

    build_init(&builders[0], "[proc,strlen]", -1);
    emit_str(&builders[0], "12345");
    emit(&builders[0], SS_OP_STRING_LENGTH, 0);
    emit(&builders[0], SS_OP_RETURN, 0);
    if( run_expect_int(builders, 1, &number, "string_length") )
        CHECK_EQ(number, 5, "string_length");

    /* java.lang.String.compareTo returns the character difference, not just a
     * sign, and content compares against exact values. */
    build_init(&builders[0], "[proc,compare]", -1);
    emit_str(&builders[0], "a");
    emit_str(&builders[0], "c");
    emit(&builders[0], SS_OP_COMPARE, 0);
    emit(&builders[0], SS_OP_RETURN, 0);
    if( run_expect_int(builders, 1, &number, "compare") )
        CHECK_EQ(number, -2, "compare returns the character difference");

    build_init(&builders[0], "[proc,indexof]", -1);
    emit_str(&builders[0], "wor");
    emit_str(&builders[0], "hello world");
    emit(&builders[0], SS_OP_STRING_INDEXOF_STRING, 0);
    emit(&builders[0], SS_OP_RETURN, 0);
    if( run_expect_int(builders, 1, &number, "indexof") )
        CHECK_EQ(number, 6, "string_indexof_string");
}

/* ------------------------------------------------------------------ */
/* Suspend and resume                                                  */
/* ------------------------------------------------------------------ */

struct StubHost
{
    int delay_calls;
    int mes_calls;
    char last_message[128];
};

static int
stub_command(struct SSVM_State* state, int opcode, int dot)
{
    struct StubHost* host = (struct StubHost*)state->env->host.user;

    (void)dot;

    switch( opcode )
    {
    case SS_OP_P_DELAY:
    {
        int32_t ticks;

        if( !SSVM_PopInt(state, &ticks) )
            return 1;
        host->delay_calls++;
        SSVM_Suspend(state, SSVM_SUSPENDED);
        return 1;
    }

    case SS_OP_MES:
    {
        const char* text;

        if( !SSVM_PopStr(state, &text) )
            return 1;
        host->mes_calls++;
        snprintf(host->last_message, sizeof(host->last_message), "%s", text);
        return 1;
    }

    default:
        return 0;
    }
}

static void
test_suspend_resume(void)
{
    struct Builder builders[1];
    struct Fixture fixture;
    struct StubHost host;
    struct SSVM_State* state;
    enum SSVM_Exec status;
    int32_t opcount_at_suspend;

    printf("suspend and resume\n");

    memset(&host, 0, sizeof(host));

    /* mes; p_delay(3); mes — the shape of every timed interaction. The string
     * built before the suspend must still be readable after it, which is why
     * the string pool belongs to the state rather than to the call. */
    build_init(&builders[0], "[proc,delayed]", -1);
    builders[0].script.string_local_count = 1;
    emit_str(&builders[0], "before");
    emit(&builders[0], SS_OP_POP_STRING_LOCAL, 0);
    emit(&builders[0], SS_OP_PUSH_STRING_LOCAL, 0);
    emit(&builders[0], SS_OP_MES, 0);
    emit(&builders[0], SS_OP_PUSH_CONSTANT_INT, 3);
    emit(&builders[0], SS_OP_P_DELAY, 0);
    emit(&builders[0], SS_OP_PUSH_STRING_LOCAL, 0);
    emit(&builders[0], SS_OP_MES, 0);
    emit(&builders[0], SS_OP_RETURN, 0);

    if( !fixture_open(&fixture, builders, 1) )
        return;
    SSVM_EnvBindHost(&fixture.env, &host, stub_command);

    state = SSVM_StateAlloc(&fixture.env, SSVM_ProviderGet(&fixture.provider, 0), NULL, 0, NULL, 0);
    /* mes requires active_player; p_delay requires p_active_player, the
     * *protected* form. They are separate bits because protected access is
     * something the engine grants per trigger, not something an active entity
     * implies — and the VM enforces both from the generated table before
     * dispatching. A test that skips either gets an abort instead of a host
     * call, which is the check doing its job. */
    SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, &host);
    SSVM_PointerAdd(state, SSVM_PTR_PROTECTED_PLAYER);
    status = SSVM_Execute(state);

    CHECK(status == SSVM_SUSPENDED, "p_delay suspends the script");
    CHECK_EQ(host.mes_calls, 1, "only the first mes ran");
    CHECK_EQ(strcmp(host.last_message, "before"), 0, "the first message arrived");
    opcount_at_suspend = state->opcount;

    /* Resuming picks up at the same pc with locals, stacks and pool intact. */
    status = SSVM_Execute(state);

    CHECK(status == SSVM_FINISHED, "resuming runs the script to completion");
    CHECK_EQ(host.mes_calls, 2, "the second mes ran after the resume");
    CHECK_EQ(strcmp(host.last_message, "before"), 0,
             "a string built before the suspend survives it");

    /* The budget is shared across re-entries in the reference, which is what
     * makes it an anti-runaway guarantee. The intuitive implementation resets
     * it, and then a script that suspends in a loop can never be stopped. */
    CHECK(state->opcount > opcount_at_suspend, "the instruction budget accumulates");

    /* Re-entering a finished state must be inert: the engine's tick phases do
     * not always know a parked script already completed. */
    status = SSVM_Execute(state);
    CHECK(status == SSVM_FINISHED, "re-running a finished state is a no-op");
    CHECK_EQ(host.mes_calls, 2, "and does not run anything again");

    SSVM_StateRelease(state);
    fixture_close(&fixture);
}

static void
test_unknown_command(void)
{
    struct Builder builders[1];
    struct Fixture fixture;
    struct StubHost host;
    struct SSVM_State* state;
    enum SSVM_Exec status;

    printf("unimplemented commands\n");

    memset(&host, 0, sizeof(host));

    /* inv_total(inv, obj) -> int. The stub host does not handle it, so the loud
     * stub must pop both args and push one int, leaving the stack consistent
     * enough for the script to finish. */
    build_init(&builders[0], "[proc,stubbed]", -1);
    emit(&builders[0], SS_OP_PUSH_CONSTANT_INT, 93);
    emit(&builders[0], SS_OP_PUSH_CONSTANT_INT, 995);
    emit(&builders[0], SS_OP_INV_TOTAL, 0);
    emit(&builders[0], SS_OP_RETURN, 0);

    if( !fixture_open(&fixture, builders, 1) )
        return;
    SSVM_EnvBindHost(&fixture.env, &host, stub_command);

    state = SSVM_StateAlloc(&fixture.env, SSVM_ProviderGet(&fixture.provider, 0), NULL, 0, NULL, 0);
    SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, &host);
    status = SSVM_Execute(state);

    CHECK(status == SSVM_FINISHED, "a known-but-unhandled opcode keeps the script running");
    CHECK_EQ(state->isp, 1, "the stub left exactly the declared return value");
    CHECK_EQ(state->int_stack[0], 0, "and it is the default");

    SSVM_StateRelease(state);
    fixture_close(&fixture);
}

static void
test_random_is_java_compatible(void)
{
    struct SSVM_Env env;

    printf("random\n");

    memset(&env, 0, sizeof(env));
    SSVM_EnvSeed(&env, 42);

    /* java.util.Random(42).nextDouble() yields 0.7275636800328681,
     * 0.6832234717598454, 0.30871945533265976 — so scaled by 100 and truncated
     * the draws are 72, 68, 30. Anchoring to Java's published sequence rather
     * than to whatever this implementation happens to emit is the whole point:
     * it is what makes "ported, not substituted" a checkable claim, and three
     * consecutive matches cannot be coincidence. */
    CHECK_EQ(SSVM_Random(&env, 100), 72, "first draw from seed 42");
    CHECK_EQ(SSVM_Random(&env, 100), 68, "second draw");
    CHECK_EQ(SSVM_Random(&env, 100), 30, "third draw");

    SSVM_EnvSeed(&env, 42);
    CHECK_EQ(SSVM_Random(&env, 100), 72, "reseeding replays the sequence");
    CHECK_EQ(SSVM_Random(&env, 0), 0, "a zero bound is zero, not a division fault");
}

int
main(void)
{
    test_constants_and_locals();
    test_branches();
    test_backward_branch();
    test_switch();
    test_gosub();
    test_gosub_depth();
    test_op_budget();
    test_stack_underflow();
    test_number_ops();
    test_divide_by_zero();
    test_join_string();
    test_string_ops();
    test_suspend_resume();
    test_unknown_command();
    test_random_is_java_compatible();

    if( g_fail )
    {
        printf("ss vm test: %d FAILED\n", g_fail);
        return 1;
    }
    printf("ss vm test: all passed\n");
    return 0;
}
