/*
 * Unit test for the RS2-era CS2 opcode dialect translation and the two
 * non-colliding gaps it accompanies (47/48 varc-string-old, 86 BRANCH_IF_ONE).
 *
 * Wire opcode 51 is SWITCH under RS2-on-dat2 and GET_VARC_LONG under OSRS.
 * CS2VM2_ScriptCopyFromRSCache translates 51 -> CS2_OP_SWITCH (60) under the
 * RS2 dialect and leaves it alone under the canonical dialect.
 */

#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_host.h"
#include "cs2vm2/cs2vm2_script.h"
#include "engine/cs2_opcode_dialect.h"
#include "engine/cs2vm2_script_from_rscache.h"

#include <assert.h>
#include <rscache.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;

#define CHECK(cond, label)                                                                         \
    do                                                                                             \
    {                                                                                              \
        if( cond )                                                                                 \
            printf("  ok: %s\n", label);                                                           \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s\n", label);                                                         \
            g_fail++;                                                                              \
        }                                                                                          \
    } while( 0 )

#define CHECK_EQ(got, want, label)                                                                 \
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

/* Minimal host for 47/48: one string slot keyed by varc id. */
struct StubVarcHost
{
    char* slots[8];
};

static int
stub_host_exec(
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostRequest* request)
{
    struct StubVarcHost* host = (struct StubVarcHost*)vm->vm->user;
    assert(host);

    switch( request->kind )
    {
    case CS2VM_HOST_REQUEST_VARS_READ_VARC_STRING:
    {
        int id = request->u.vars_read_varc_string.varc_id;
        char const* value = "";
        if( id >= 0 && id < 8 && host->slots[id] )
            value = host->slots[id];
        return CS2VM2_PushStr(vm, strdup(value));
    }
    case CS2VM_HOST_REQUEST_VARS_WRITE_VARC_STRING:
    {
        int id = request->u.vars_write_varc_string.varc_id;
        char* value = request->u.vars_write_varc_string.value;
        if( id >= 0 && id < 8 )
        {
            free(host->slots[id]);
            host->slots[id] = value ? strdup(value) : strdup("");
        }
        free(value);
        return CS2VM_EXECNO_OK;
    }
    default:
        return CS2VM_EXECNO_ERROR;
    }
}

/*
 * Build a wire-opcode RSCache script:
 *   PUSH_CONSTANT_INT key; 51 (SWITCH table 0); PUSH_CONSTANT_INT 0; RETURN;
 *   PUSH_CONSTANT_INT 7; RETURN
 * with switch table 0 mapping match_key -> +2 (skip the fallthrough return).
 */
static void
build_wire_switch_script(
    struct RSCache_CS2_Script* src,
    int match_key)
{
    RSCache_CS2_ScriptInit(src);
    src->script_id = 1314;
    src->op_count = 6;
    src->opcodes = calloc(6, sizeof(uint16_t));
    src->int_operands = calloc(6, sizeof(int));
    src->string_operands = calloc(6, sizeof(char*));
    assert(src->opcodes && src->int_operands && src->string_operands);

    src->opcodes[0] = 0; /* PUSH_CONSTANT_INT */
    src->int_operands[0] = match_key;
    src->opcodes[1] = 51; /* wire SWITCH under RS2 */
    src->int_operands[1] = 0;
    src->opcodes[2] = 0;
    src->int_operands[2] = 0; /* fallthrough: push 0 */
    src->opcodes[3] = 21;     /* RETURN */
    src->opcodes[4] = 0;
    src->int_operands[4] = 7; /* taken case: push 7 */
    src->opcodes[5] = 21;     /* RETURN */

    src->switch_table_count = 1;
    src->switch_tables[0].case_count = 1;
    src->switch_tables[0].cases[0].key = match_key;
    src->switch_tables[0].cases[0].target_pc = 2; /* from pc=1, +2 lands on push 7 */
}

static void
free_wire_script(struct RSCache_CS2_Script* src)
{
    free(src->opcodes);
    free(src->int_operands);
    free(src->long_operands);
    if( src->string_operands )
    {
        for( int i = 0; i < src->op_count; i++ )
            free(src->string_operands[i]);
        free(src->string_operands);
    }
    free(src->signature);
    RSCache_CS2_ScriptInit(src);
}

static int
run_converted_script(struct CS2VM2_Script* script)
{
    struct CS2VM2 vm;
    CS2VM2_Init(&vm);
    struct CS2VM2_Thread* t = CS2VM2_ThreadMain(&vm);
    CS2VM2_PushCallScript(t, script);
    CS2VM2_RunScript(t);

    int result = -1;
    CS2VM2_PopInt(t, &result);
    return result;
}

static void
test_rs2_switch_hit(void)
{
    printf("TEST: RS2 dialect translates wire 51 -> SWITCH and takes the case\n");

    struct RSCache_CS2_Script src;
    build_wire_switch_script(&src, 42);

    struct CS2VM2_Script dst;
    CHECK(CS2VM2_ScriptCopyFromRSCache(&src, &dst, CS2_OPCODE_DIALECT_RS2_DAT2), "copy ok");
    CHECK_EQ(dst.opcodes[1], CS2_OP_SWITCH, "opcodes[1] remapped to SWITCH");
    CHECK_EQ(run_converted_script(&dst), 7, "matched key returns 7");

    CS2VM2_ScriptFree(&dst);
    free_wire_script(&src);
}

static void
test_rs2_switch_miss(void)
{
    printf("TEST: RS2 dialect SWITCH falls through on unmatched key\n");

    struct RSCache_CS2_Script src;
    build_wire_switch_script(&src, 42);
    /* Force a miss by pushing a different key. */
    src.int_operands[0] = 99;

    struct CS2VM2_Script dst;
    CHECK(CS2VM2_ScriptCopyFromRSCache(&src, &dst, CS2_OPCODE_DIALECT_RS2_DAT2), "copy ok");
    CHECK_EQ(run_converted_script(&dst), 0, "unmatched key falls through to 0");

    CS2VM2_ScriptFree(&dst);
    free_wire_script(&src);
}

static void
test_canonical_leaves_51(void)
{
    printf("TEST: canonical dialect leaves wire 51 untranslated\n");

    struct RSCache_CS2_Script src;
    build_wire_switch_script(&src, 42);

    struct CS2VM2_Script dst;
    CHECK(CS2VM2_ScriptCopyFromRSCache(&src, &dst, CS2_OPCODE_DIALECT_CANONICAL), "copy ok");
    CHECK_EQ(dst.opcodes[1], 51, "opcodes[1] still wire 51");

    CS2VM2_ScriptFree(&dst);
    free_wire_script(&src);
}

static void
test_dialect_for_cache(void)
{
    printf("TEST: CS2_OpcodeDialectForCache gates on RSCache_IsRs2Dat2\n");

    struct RSCache rs2 = RSCache_ProfileZero();
    rs2.game = RSCACHE_GAME_RS2;
    rs2.epoch = RSCACHE_EPOCH_DAT2;
    rs2.revision = 643;
    CHECK_EQ(
        (int)CS2_OpcodeDialectForCache(&rs2),
        (int)CS2_OPCODE_DIALECT_RS2_DAT2,
        "rs2 dat2 -> RS2_DAT2");

    struct RSCache osrs = RSCache_ProfileZero();
    osrs.game = RSCACHE_GAME_OLDSCHOOL;
    osrs.epoch = RSCACHE_EPOCH_DAT2;
    osrs.revision = 239;
    CHECK_EQ(
        (int)CS2_OpcodeDialectForCache(&osrs),
        (int)CS2_OPCODE_DIALECT_CANONICAL,
        "osrs -> CANONICAL");

    CHECK_EQ(CS2_OpcodeTranslate(CS2_OPCODE_DIALECT_RS2_DAT2, 51), CS2_OP_SWITCH, "translate 51");
    CHECK_EQ(CS2_OpcodeTranslate(CS2_OPCODE_DIALECT_CANONICAL, 51), 51, "canonical identity");
    CHECK_EQ(CS2_OpcodeTranslate(CS2_OPCODE_DIALECT_RS2_DAT2, 60), 60, "60 unchanged");
}

/* BRANCH_IF_ONE: push value; BRANCH_IF_ONE +2; push 0; RETURN; push 7; RETURN */
static int
run_branch_if_one(int value)
{
    struct CS2VM2 vm;
    CS2VM2_Init(&vm);

    struct CS2VM2_Script script;
    CS2VM2_ScriptInit(&script);
    script.script_id = 86;
    script.op_count = 6;
    script.opcodes = calloc(6, sizeof(uint16_t));
    script.int_operands = calloc(6, sizeof(int));
    script.string_operands = calloc(6, sizeof(char*));
    assert(script.opcodes && script.int_operands && script.string_operands);

    script.opcodes[0] = (uint16_t)CS2_OP_PUSH_CONSTANT_INT;
    script.int_operands[0] = value;
    script.opcodes[1] = (uint16_t)CS2_OP_BRANCH_IF_ONE;
    script.int_operands[1] = 2;
    script.opcodes[2] = (uint16_t)CS2_OP_PUSH_CONSTANT_INT;
    script.int_operands[2] = 0;
    script.opcodes[3] = (uint16_t)CS2_OP_RETURN;
    script.opcodes[4] = (uint16_t)CS2_OP_PUSH_CONSTANT_INT;
    script.int_operands[4] = 7;
    script.opcodes[5] = (uint16_t)CS2_OP_RETURN;

    struct CS2VM2_Thread* t = CS2VM2_ThreadMain(&vm);
    CS2VM2_PushCallScript(t, &script);
    CS2VM2_RunScript(t);

    int result = -1;
    CS2VM2_PopInt(t, &result);

    free(script.opcodes);
    free(script.int_operands);
    free(script.string_operands);
    return result;
}

static void
test_branch_if_one(void)
{
    printf("TEST: BRANCH_IF_ONE (opcode 86)\n");
    CHECK_EQ(run_branch_if_one(1), 7, "value==1 takes branch");
    CHECK_EQ(run_branch_if_one(0), 0, "value==0 falls through");
    CHECK_EQ(run_branch_if_one(2), 0, "value==2 falls through");
}

static void
test_varc_string_old(void)
{
    printf("TEST: PUSH/POP_VARC_STRING_OLD (47/48)\n");

    struct StubVarcHost host;
    memset(&host, 0, sizeof(host));

    struct CS2VM2 vm;
    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, &host, stub_host_exec);

    struct CS2VM2_Script script;
    CS2VM2_ScriptInit(&script);
    script.script_id = 4748;
    script.op_count = 4;
    script.opcodes = calloc(4, sizeof(uint16_t));
    script.int_operands = calloc(4, sizeof(int));
    script.string_operands = calloc(4, sizeof(char*));

    /* PUSH_CONSTANT_STRING "hello"; POP_VARC_STRING_OLD 3; PUSH_VARC_STRING_OLD 3; RETURN */
    script.opcodes[0] = (uint16_t)CS2_OP_PUSH_CONSTANT_STRING;
    script.string_operands[0] = strdup("hello");
    script.opcodes[1] = (uint16_t)CS2_OP_POP_VARC_STRING_OLD;
    script.int_operands[1] = 3;
    script.opcodes[2] = (uint16_t)CS2_OP_PUSH_VARC_STRING_OLD;
    script.int_operands[2] = 3;
    script.opcodes[3] = (uint16_t)CS2_OP_RETURN;

    struct CS2VM2_Thread* t = CS2VM2_ThreadMain(&vm);
    CS2VM2_PushCallScript(t, &script);
    CS2VM2_RunScript(t);

    char* result = NULL;
    CS2VM2_PopStr(t, &result);
    CHECK(result && strcmp(result, "hello") == 0, "varc string-old roundtrip");
    free(result);

    for( int i = 0; i < 8; i++ )
        free(host.slots[i]);
    free(script.opcodes);
    free(script.int_operands);
    free(script.string_operands[0]);
    free(script.string_operands);
}

int
main(void)
{
    test_dialect_for_cache();
    test_rs2_switch_hit();
    test_rs2_switch_miss();
    test_canonical_leaves_51();
    test_branch_if_one();
    test_varc_string_old();

    if( g_fail )
    {
        printf("opcode dialect test: %d FAILED\n", g_fail);
        return 1;
    }
    printf("opcode dialect test: all passed\n");
    return 0;
}
