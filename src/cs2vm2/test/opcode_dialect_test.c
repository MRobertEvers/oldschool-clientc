/*
 * The dialects: every one CS2_DialectForCache can return is a complete table
 * from its wire ids to canonical instructions, applied once at load
 * (CS2VM2_ScriptResolveDialect). Checked here: each table translates a wire
 * script to the expected instructions, the versioned ones included; a wire id
 * a dialect does not define loads as UNDEFINED_WIRE_ID; and what the translated
 * scripts then do (RS2 51 is SWITCH, CC_CREATE_V1 pops three, V1 arrays are
 * shared), plus the two non-colliding gaps (47/48 varc-string-old,
 * 86 BRANCH_IF_ONE).
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
    int calls;
    enum CS2VM_HostRequestKind kinds[2];
};

static int
stub_varc_read(
    struct CS2VM2_Thread* vm,
    struct StubVarcHost* host,
    struct CS2VM_HostRequest* request)
{
    int id;
    char const* value = "";

    if( request->kind == CS2VM_HOST_REQUEST_PUSH_VARC_STRING_OLD )
        id = request->u.PUSH_VARC_STRING_OLD.varc_id;
    else
    {
        assert(request->kind == CS2VM_HOST_REQUEST_PUSH_VARC_STRING);
        id = request->u.PUSH_VARC_STRING.varc_id;
    }
    if( id >= 0 && id < 8 && host->slots[id] )
        value = host->slots[id];
    return CS2VM2_PushStr(vm, CS2VM2_StrDup(vm, value));
}

static int
stub_varc_write(
    struct StubVarcHost* host,
    struct CS2VM_HostRequest* request)
{
    int id;
    struct CS2VM2_Str* value;

    if( request->kind == CS2VM_HOST_REQUEST_POP_VARC_STRING_OLD )
    {
        id = request->u.POP_VARC_STRING_OLD.varc_id;
        value = request->u.POP_VARC_STRING_OLD.value;
    }
    else
    {
        assert(request->kind == CS2VM_HOST_REQUEST_POP_VARC_STRING);
        id = request->u.POP_VARC_STRING.varc_id;
        value = request->u.POP_VARC_STRING.value;
    }
    /* The request's string is borrowed from the VM's pool, so a host that
     * keeps it must copy (as the real VarCManager does). */
    if( id >= 0 && id < 8 )
    {
        free(host->slots[id]);
        host->slots[id] = strdup(CS2VM2_StrText(value));
    }
    return CS2VM_EXECNO_OK;
}

static int
stub_host_exec(
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostRequest* request)
{
    struct StubVarcHost* host = (struct StubVarcHost*)vm->vm->user;
    assert(host);
    assert(host->calls < 2);
    host->kinds[host->calls++] = request->kind;

    switch( request->kind )
    {
    case CS2VM_HOST_REQUEST_PUSH_VARC_STRING_OLD:
        return stub_varc_read(vm, host, request);
    case CS2VM_HOST_REQUEST_POP_VARC_STRING_OLD:
        return stub_varc_write(host, request);
    case CS2VM_HOST_REQUEST_PUSH_VARC_STRING:
        return stub_varc_read(vm, host, request);
    case CS2VM_HOST_REQUEST_POP_VARC_STRING:
        return stub_varc_write(host, request);
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

    CHECK(RSCache_CS2_ScriptAllocSwitches(src, 1), "allocate switch table");
    CHECK(RSCache_CS2_ScriptAllocSwitchCases(src, 0, 1), "allocate switch cases");
    src->switch_tables[0].cases[0].key = match_key;
    src->switch_tables[0].cases[0].target_pc = 2; /* from pc=1, +2 lands on push 7 */
}

static void
free_wire_script(struct RSCache_CS2_Script* src)
{
    if( src->switch_tables )
    {
        for( int i = 0; i < src->switch_table_count; i++ )
            free(src->switch_tables[i].cases);
        free(src->switch_tables);
    }
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
    CS2VM2_ScriptPrepare(script); /* what a loader does */
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
    CHECK(CS2VM2_ScriptCopyFromRSCache(&src, &dst, &CS2VM2_DIALECT_RS2_634), "copy ok");
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
    CHECK(CS2VM2_ScriptCopyFromRSCache(&src, &dst, &CS2VM2_DIALECT_RS2_634), "copy ok");
    CHECK_EQ(run_converted_script(&dst), 0, "unmatched key falls through to 0");

    CS2VM2_ScriptFree(&dst);
    free_wire_script(&src);
}

static void
test_canonical_leaves_51(void)
{
    printf("TEST: the native dialect reads wire 51 as PUSH_VARC_LONG\n");

    struct RSCache_CS2_Script src;
    build_wire_switch_script(&src, 42);

    struct CS2VM2_Script dst;
    CHECK(CS2VM2_ScriptCopyFromRSCache(&src, &dst, &CS2VM2_DIALECT_OSRS), "copy ok");
    CHECK_EQ(dst.opcodes[1], CS2_OP_PUSH_VARC_LONG, "OldSchool's wire 51 is PUSH_VARC_LONG");

    CS2VM2_ScriptFree(&dst);
    free_wire_script(&src);
}

static void
test_dialect_for_cache(void)
{
    printf("TEST: CS2_DialectForCache picks by game and revision\n");

    struct RSCache rs2 = RSCache_ProfileZero();
    rs2.game = RSCACHE_GAME_RS2;
    rs2.epoch = RSCACHE_EPOCH_DAT2;
    rs2.revision = 643;
    CHECK(CS2_DialectForCache(&rs2) == &CS2VM2_DIALECT_RS2_634, "rs2 dat2 -> RS2_634");

    struct RSCache osrs = RSCache_ProfileZero();
    osrs.game = RSCACHE_GAME_OLDSCHOOL;
    osrs.epoch = RSCACHE_EPOCH_DAT2;
    osrs.revision = 239;
    CHECK(CS2_DialectForCache(&osrs) == &CS2VM2_DIALECT_OSRS, "osrs 239 -> OSRS");

    osrs.revision = 230;
    CHECK(CS2_DialectForCache(&osrs) == &CS2VM2_DIALECT_OSRS_230, "osrs 230 -> OSRS_230");
    osrs.revision = 231;
    CHECK(CS2_DialectForCache(&osrs) == &CS2VM2_DIALECT_OSRS, "osrs 231 -> OSRS");
    osrs.revision = 229;
    CHECK(CS2_DialectForCache(&osrs) == &CS2VM2_DIALECT_OSRS_229, "osrs 229 -> OSRS_229");
    osrs.revision = RSCACHE_REVISION_UNKNOWN;
    CHECK(CS2_DialectForCache(&osrs) == &CS2VM2_DIALECT_OSRS, "osrs of unknown revision -> OSRS");
    CHECK_EQ(CS2VM2_DialectTranslate(&CS2VM2_DIALECT_OSRS_229, 51), CS2_OP_PUSH_VARC_LONG, "osrs 229: 51");
    CHECK_EQ(CS2VM2_DialectTranslate(&CS2VM2_DIALECT_RS2_634, 51), CS2_OP_SWITCH, "rs2: 51 is SWITCH");
    CHECK_EQ(CS2VM2_DialectTranslate(&CS2VM2_DIALECT_OSRS, 51), CS2_OP_PUSH_VARC_LONG, "osrs: 51");
    CHECK_EQ(CS2VM2_DialectTranslate(&CS2VM2_DIALECT_RS2_634, 60), CS2_OP_SWITCH, "rs2: 60 is SWITCH too");
    CHECK_EQ(CS2VM2_DialectWireOf(&CS2VM2_DIALECT_RS2_634, CS2_OP_SWITCH), 51, "rs2 prints SWITCH as 51");
    CHECK_EQ(CS2VM2_DialectWireOf(&CS2VM2_DIALECT_OSRS, CS2_OP_SWITCH), 60, "osrs prints SWITCH as 60");
    CHECK_EQ(CS2VM2_DialectWireOf(NULL, CS2_OP_CC_CREATE_V2), 100, "NULL is the native dialect");
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
    CS2VM2_ScriptPrepare(&script); /* what a loader does */
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
test_varc_string_pair(
    int pop_opcode,
    int push_opcode,
    enum CS2VM_HostRequestKind expected_pop_kind,
    enum CS2VM_HostRequestKind expected_push_kind,
    char const* label)
{
    printf("TEST: %s\n", label);

    struct StubVarcHost host;
    memset(&host, 0, sizeof(host));

    struct CS2VM2 vm;
    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, &host, stub_host_exec);

    struct CS2VM2_Script script;
    CS2VM2_ScriptInit(&script);
    script.script_id = pop_opcode * 100 + push_opcode;
    script.op_count = 4;
    script.opcodes = calloc(4, sizeof(uint16_t));
    script.int_operands = calloc(4, sizeof(int));
    script.string_operands = calloc(4, sizeof(char*));

    /* PUSH_CONSTANT_STRING "hello"; POP pair 3; PUSH pair 3; RETURN. */
    script.opcodes[0] = (uint16_t)CS2_OP_PUSH_CONSTANT_STRING;
    script.string_operands[0] = strdup("hello");
    script.opcodes[1] = (uint16_t)pop_opcode;
    script.int_operands[1] = 3;
    script.opcodes[2] = (uint16_t)push_opcode;
    script.int_operands[2] = 3;
    script.opcodes[3] = (uint16_t)CS2_OP_RETURN;

    struct CS2VM2_Thread* t = CS2VM2_ThreadMain(&vm);
    CS2VM2_ScriptPrepare(&script); /* what a loader does */
    CS2VM2_PushCallScript(t, &script);
    CS2VM2_RunScript(t);

    /* Pool-owned: read it, don't free it — CS2VM2_Free releases the pool. */
    struct CS2VM2_Str* result = NULL;
    CS2VM2_PopStr(t, &result);
    CHECK(result && strcmp(result->text, "hello") == 0, "varc string roundtrip");
    CHECK_EQ(host.calls, 2, "one host request per varc opcode");
    CHECK_EQ((int)host.kinds[0], (int)expected_pop_kind, "pop request keeps its opcode kind");
    CHECK_EQ((int)host.kinds[1], (int)expected_push_kind, "push request keeps its opcode kind");

    CS2VM2_Free(&vm);
    for( int i = 0; i < 8; i++ )
        free(host.slots[i]);
    free(script.opcodes);
    free(script.int_operands);
    free(script.string_operands[0]);
    free(script.string_operands);
}

struct UnexpectedHost
{
    int calls;
};

static int
unexpected_host_exec(
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostRequest* request)
{
    struct UnexpectedHost* host = (struct UnexpectedHost*)CS2VM_USER(vm);
    (void)request;
    host->calls++;
    return CS2VM_EXECNO_ERROR;
}

static void
test_rs2_overlay_entry(
    int opcode,
    int expect,
    int int_in,
    int str_in,
    int int_out,
    int str_out,
    char const* label)
{
    struct CS2VM2 vm;
    struct CS2VM2_Script script;
    struct CS2VM2_ThreadError error;
    struct UnexpectedHost host;
    uint16_t wire_opcode = (uint16_t)opcode;
    int operand = 0;
    char* string_operand = NULL;
    char value[] = "overlay-input";

    memset(&host, 0, sizeof(host));
    memset(&error, 0, sizeof(error));
    CS2VM2_ScriptInit(&script);
    script.script_id = 634000 + opcode;
    script.dialect = &CS2VM2_DIALECT_RS2_634;
    script.op_count = 1;
    script.opcodes = &wire_opcode;
    script.int_operands = &operand;
    script.string_operands = &string_operand;
    /* What every loader does: the RS2 table names the 634 command. */
    CS2VM2_ScriptResolveDialect(&script);
    CHECK_EQ(wire_opcode, expect, "RS2 collision is the 634 command's own instruction");

    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, &host, unexpected_host_exec);
    struct CS2VM2_Thread* thread = CS2VM2_ThreadMain(&vm);
    CS2VM2_ScriptPrepare(&script); /* what a loader does */
    CHECK_EQ(CS2VM2_ThreadStart(thread, &script), CS2VM_EXECNO_OK, label);
    for( int i = 0; i < int_in; i++ )
        thread->ints_stack[thread->ints_stack_top++] = 100 + i;
    for( int i = 0; i < str_in; i++ )
        thread->strs_stack[thread->strs_stack_top++] = CS2VM2_StrDup(thread, value);

    CHECK_EQ(
        (int)CS2VM2_ThreadRun(thread, &error),
        (int)CS2VM2_THREAD_DONE,
        "RS2 overlay stub completes");
    CHECK_EQ(host.calls, 0, "RS2 overlay collision never reaches canonical host handler");
    CHECK_EQ(thread->ints_stack_top, int_out, "RS2 overlay int stack signature");
    CHECK_EQ(thread->strs_stack_top, str_out, "RS2 overlay string stack signature");
    CS2VM2_Free(&vm);
}

/* What the CC_CREATE host saw. */
struct CreateHost
{
    int calls;
    int parent_id;
    int component_type;
    int child_index;
    int is_nested;
};

static int
create_host_exec(
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostRequest* request)
{
    struct CreateHost* host = (struct CreateHost*)CS2VM_USER(vm);
    if( request->kind != CS2VM_HOST_REQUEST_CC_CREATE )
        return CS2VM_EXECNO_ERROR;
    host->calls++;
    host->parent_id = request->u.CC_CREATE.parent_id;
    host->component_type = request->u.CC_CREATE.component_type;
    host->child_index = request->u.CC_CREATE.child_index;
    host->is_nested = request->u.CC_CREATE.is_nested;
    return CS2VM_EXECNO_OK;
}

/* push parent, type, index[, nested]; CC_CREATE; RETURN -- under `dialect`. */
static void
test_cc_create_arity(
    struct CS2VM2_Dialect const* dialect,
    int pushes_nested,
    char const* label)
{
    struct RSCache_CS2_Script src;
    struct CS2VM2_Script dst;
    struct CreateHost host;
    struct CS2VM2 vm;
    int n = 0;

    printf("TEST: CC_CREATE arity, %s\n", label);
    RSCache_CS2_ScriptInit(&src);
    src.script_id = 3953;
    src.op_count = pushes_nested ? 6 : 5;
    src.opcodes = calloc((size_t)src.op_count, sizeof(uint16_t));
    src.int_operands = calloc((size_t)src.op_count, sizeof(int));
    src.string_operands = calloc((size_t)src.op_count, sizeof(char*));
    assert(src.opcodes && src.int_operands && src.string_operands);
    /* Wire ids, as the cache spells them: 0 PUSH_CONSTANT_INT, 100 the create,
     * 21 RETURN. */
    src.int_operands[n] = 10551300; /* parent 161:4 */
    src.opcodes[n++] = 0;
    src.int_operands[n] = 5; /* type: graphic */
    src.opcodes[n++] = 0;
    src.int_operands[n] = 3; /* index */
    src.opcodes[n++] = 0;
    if( pushes_nested )
    {
        src.int_operands[n] = 1;
        src.opcodes[n++] = 0;
    }
    src.opcodes[n++] = 100;
    src.opcodes[n++] = 21;

    CHECK(CS2VM2_ScriptCopyFromRSCache(&src, &dst, dialect), "copy ok");
    CHECK_EQ(
        dst.opcodes[n - 2],
        pushes_nested ? CS2_OP_CC_CREATE_V2 : CS2_OP_CC_CREATE_V1,
        "wire 100 is the era's version of the create");
    memset(&host, 0, sizeof(host));
    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, &host, create_host_exec);
    struct CS2VM2_Thread* thread = CS2VM2_ThreadMain(&vm);
    CS2VM2_ScriptPrepare(&dst); /* what a loader does */
    CS2VM2_PushCallScript(thread, &dst);
    CS2VM2_RunScript(thread);
    CHECK_EQ(host.calls, 1, label);
    CHECK_EQ(host.parent_id, 10551300, label);
    CHECK_EQ(host.component_type, 5, label);
    CHECK_EQ(host.child_index, 3, label);
    CHECK_EQ(host.is_nested, pushes_nested ? 1 : 0, label);
    CHECK_EQ(thread->ints_stack_top, 0, label);

    CS2VM2_Free(&vm);
    CS2VM2_ScriptFree(&dst);
    free_wire_script(&src);
}

/* The callee a GOSUB in test_array_model reaches. */
static struct CS2VM2_Script* g_gosub_callee;

static int
gosub_host_exec(
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostRequest* request)
{
    if( request->kind != CS2VM_HOST_REQUEST_GOSUB_WITH_PARAMS )
        return CS2VM_EXECNO_ERROR;
    CS2VM2_ScriptPrepare(g_gosub_callee); /* what a loader does */
    CS2VM2_PushCallScript(vm, g_gosub_callee);
    return CS2VM_EXECNO_OK;
}

/* A script of `count` ops from parallel opcode/operand lists. */
static void
build_ops(
    struct CS2VM2_Script* script,
    int id,
    struct CS2VM2_Dialect const* dialect,
    uint16_t const* opcodes,
    int const* operands,
    int count)
{
    CS2VM2_ScriptInit(script);
    script->script_id = id;
    script->dialect = dialect;
    script->op_count = count;
    script->local_string_count = 1;
    script->opcodes = calloc((size_t)count, sizeof(uint16_t));
    script->int_operands = calloc((size_t)count, sizeof(int));
    script->string_operands = calloc((size_t)count, sizeof(char*));
    assert(script->opcodes && script->int_operands && script->string_operands);
    memcpy(script->opcodes, opcodes, (size_t)count * sizeof(uint16_t));
    memcpy(script->int_operands, operands, (size_t)count * sizeof(int));
    /* As a loader would: the opcodes are the dialect's wire ids, and its table
     * picks the array model here, not at run time. */
    CS2VM2_ScriptResolveDialect(script);
}

/*
 * Caller: def_int $a(3); $a(1) = 42; gosub callee; return.
 * Callee: push $a(1) -- array 0, never defined or received here; return.
 * Numbered arrays (OldSchool through 230) are shared, so the callee reads 42
 * (script 2255's recursive sort depends on it); a handle lives in the frame
 * that defined it, so the native dialect reads nothing (0). An int array
 * starts at 0 either way, so $a(0) reads 0, not -1.
 */
static void
test_array_model(
    struct CS2VM2_Dialect const* dialect,
    int want,
    char const* label)
{
    /* Wire ids: 0 PUSH_CONSTANT_INT, 44 DEFINE_ARRAY, 46 POP_ARRAY_INT,
     * 40 GOSUB_WITH_PARAMS, 45 PUSH_ARRAY_INT, 21 RETURN. */
    uint16_t const caller_ops[] = { 0, 44, 0, 0, 46, 40, 0, 45, 21 };
    int const caller_operands[] = { 3, (0 << 16) | 'i', 1, 42, 0, 2256, 0, 0, 0 };
    uint16_t const callee_ops[] = { 0, 45, 21 };
    int const callee_operands[] = { 1, 0, 0 };
    struct CS2VM2_Script caller;
    struct CS2VM2_Script callee;
    struct CS2VM2 vm;
    int unset = -7;
    int got = -7;

    printf("TEST: array model, %s\n", label);
    build_ops(&caller, 2255, dialect, caller_ops, caller_operands, 9);
    build_ops(&callee, 2256, dialect, callee_ops, callee_operands, 3);
    g_gosub_callee = &callee;
    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, NULL, gosub_host_exec);
    struct CS2VM2_Thread* thread = CS2VM2_ThreadMain(&vm);
    CS2VM2_ScriptPrepare(&caller); /* what a loader does */
    CS2VM2_PushCallScript(thread, &caller);
    CS2VM2_RunScript(thread);
    CHECK_EQ(thread->ints_stack_top, 2, label);
    CS2VM2_PopInt(thread, &unset);
    CS2VM2_PopInt(thread, &got);
    CHECK_EQ(got, want, label);
    CHECK_EQ(unset, 0, "an int array starts at 0");

    CS2VM2_Free(&vm);
    for( struct CS2VM2_Script* s = &caller; s; s = s == &caller ? &callee : NULL )
    {
        free(s->opcodes);
        free(s->int_operands);
        free(s->string_operands);
    }
}

static void
test_rs2_overlay_binary_lookup(void)
{
    printf("TEST: RS2 collisions translate to the 634 commands and keep their signatures\n");

    /* First, middle and last rows of the renumbering table; 1122/1311/2122/2314
     * are canonical widget handlers and therefore also prove a renumbered id
     * never dispatches the wrong command. */
    test_rs2_overlay_entry(202, CS2_OP_RS2_IF_REMOVEFROMGROUP, 1, 0, 0, 0, "overlay first entry 202");
    test_rs2_overlay_entry(1122, CS2_OP_RS2_CC_SETGRAPHICFLAG, 1, 0, 0, 0, "overlay collision 1122");
    test_rs2_overlay_entry(1311, CS2_OP_RS2_CC_SETANINT713, 1, 0, 0, 0, "overlay collision 1311");
    test_rs2_overlay_entry(2122, CS2_OP_RS2_IF_SETGRAPHICFLAG, 2, 0, 0, 0, "overlay collision 2122");
    test_rs2_overlay_entry(2314, CS2_OP_RS2_IF_SETANINT719, 2, 0, 0, 0, "overlay collision 2314");
    test_rs2_overlay_entry(6506, CS2_OP_RS2_WORLDLIST_SPECIFIC, 1, 0, 4, 3, "overlay mixed output 6506");
    test_rs2_overlay_entry(6900, CS2_OP_RS2_GETINT_D, 0, 0, 1, 0, "overlay last entry 6900");
}


/* One wire id in each dialect: the canonical instruction its table names, or
 * -1 where the dialect does not define it. */
struct WireCase
{
    int wire;
    int osrs;
    int osrs230;
    int osrs229;
    int rs2;
};

static int
translate_one(
    struct CS2VM2_Dialect const* dialect,
    int wire)
{
    uint16_t op = (uint16_t)wire;
    int operand = 0;
    char* string_operand = NULL;
    struct CS2VM2_Script script;
    CS2VM2_ScriptInit(&script);
    script.script_id = 900000 + wire;
    script.dialect = dialect;
    script.op_count = 1;
    script.opcodes = &op;
    script.int_operands = &operand;
    script.string_operands = &string_operand;
    CS2VM2_ScriptResolveDialect(&script);
    int const result = op == CS2_OP_UNDEFINED_WIRE_ID ? -1 : (int)op;
    CHECK_EQ(CS2VM2_DialectTranslate(dialect, wire), result, "translate one == table");
    if( op == CS2_OP_UNDEFINED_WIRE_ID )
    {
        CHECK(script.undefined_wire_count == 1 && script.undefined_wires[0].pc == 0 &&
                  script.undefined_wires[0].wire == wire,
            "the undefined wire id is kept");
        script.opcodes = NULL;
        script.int_operands = NULL;
        script.string_operands = NULL;
        CS2VM2_ScriptFree(&script);
    }
    return result;
}

static void
test_dialect_tables(void)
{
    printf("TEST: every dialect's table, the versioned instructions included\n");
    static struct WireCase const cases[] = {
        { 0, CS2_OP_PUSH_CONSTANT_INT, CS2_OP_PUSH_CONSTANT_INT, CS2_OP_PUSH_CONSTANT_INT, CS2_OP_PUSH_CONSTANT_INT },
        { 21, CS2_OP_RETURN, CS2_OP_RETURN, CS2_OP_RETURN, CS2_OP_RETURN },
        /* Arrays: numbered and shared through rev 230, handles from 231 (and
         * at 634). */
        { 44, CS2_OP_DEFINE_ARRAY_V2, CS2_OP_DEFINE_ARRAY_V1, CS2_OP_DEFINE_ARRAY_V1, CS2_OP_DEFINE_ARRAY_V2 },
        { 45, CS2_OP_PUSH_ARRAY_INT_V2, CS2_OP_PUSH_ARRAY_INT_V1, CS2_OP_PUSH_ARRAY_INT_V1, CS2_OP_PUSH_ARRAY_INT_V2 },
        { 46, CS2_OP_POP_ARRAY_INT_V2, CS2_OP_POP_ARRAY_INT_V1, CS2_OP_POP_ARRAY_INT_V1, CS2_OP_POP_ARRAY_INT_V2 },
        /* The create pops a nested flag from rev 230; 229 and 634 do not. */
        { 100, CS2_OP_CC_CREATE_V2, CS2_OP_CC_CREATE_V2, CS2_OP_CC_CREATE_V1, CS2_OP_CC_CREATE_V1 },
        /* 634 spells SWITCH 51 and STRUCT_PARAM 4500. */
        { 51, CS2_OP_PUSH_VARC_LONG, CS2_OP_PUSH_VARC_LONG, CS2_OP_PUSH_VARC_LONG, CS2_OP_SWITCH },
        { 60, CS2_OP_SWITCH, CS2_OP_SWITCH, CS2_OP_SWITCH, CS2_OP_SWITCH },
        { 4500, -1, -1, -1, CS2_OP_STRUCT_PARAM },
        { 6516, CS2_OP_STRUCT_PARAM, CS2_OP_STRUCT_PARAM, CS2_OP_STRUCT_PARAM, CS2_OP_STRUCT_PARAM },
        /* A 634 command at an id where OldSchool has another. */
        { 1613, CS2_OP_CC_GETBLENDMODE, CS2_OP_CC_GETBLENDMODE, CS2_OP_CC_GETBLENDMODE, CS2_OP_RS2_CC_GETPARAM },
        { 4124, CS2_OP_PRONOUN, CS2_OP_PRONOUN, CS2_OP_PRONOUN, CS2_OP_RS2_TOSTRING_GROUPED },
        { 3609, CS2_OP_FRIEND_TEST, CS2_OP_FRIEND_TEST, CS2_OP_FRIEND_TEST, CS2_OP_RS2_FRIEND_TEST },
        /* What no cache spells: the old vm-dialect range, an id past them all. */
        { 9000, -1, -1, -1, -1 },
        { 9046, -1, -1, -1, -1 },
        { 65535, -1, -1, -1, -1 },
    };
    for( size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++ )
    {
        struct WireCase const* c = &cases[i];
        char label[96];
        snprintf(label, sizeof(label), "osrs wire %d", c->wire);
        CHECK_EQ(translate_one(&CS2VM2_DIALECT_OSRS, c->wire), c->osrs, label);
        snprintf(label, sizeof(label), "osrs230 wire %d", c->wire);
        CHECK_EQ(translate_one(&CS2VM2_DIALECT_OSRS_230, c->wire), c->osrs230, label);
        snprintf(label, sizeof(label), "osrs229 wire %d", c->wire);
        CHECK_EQ(translate_one(&CS2VM2_DIALECT_OSRS_229, c->wire), c->osrs229, label);
        snprintf(label, sizeof(label), "rs2_634 wire %d", c->wire);
        CHECK_EQ(translate_one(&CS2VM2_DIALECT_RS2_634, c->wire), c->rs2, label);
    }

    /* Each table, end to end: every wire id it defines translates, the result
     * is a plain instruction, and prints name the wire id back (the era's own
     * spelling where two reach one instruction). */
    struct CS2VM2_Dialect const* const dialects[] = {
        &CS2VM2_DIALECT_OSRS, &CS2VM2_DIALECT_OSRS_230, &CS2VM2_DIALECT_OSRS_229, &CS2VM2_DIALECT_RS2_634
    };
    for( size_t d = 0; d < sizeof(dialects) / sizeof(dialects[0]); d++ )
    {
        struct CS2VM2_Dialect const* dialect = dialects[d];
        int bad = 0;
        for( int i = 0; i < dialect->wire_count; i++ )
        {
            int const wire = dialect->wire_to_op[i].wire;
            int const op = CS2VM2_DialectTranslate(dialect, wire);
            if( i > 0 && dialect->wire_to_op[i - 1].wire >= wire )
                bad++;
            if( op < 0 || op >= CS2_OP_PLAIN_COUNT )
                bad++;
            else if( CS2VM2_DialectTranslate(dialect, CS2VM2_DialectWireOf(dialect, op)) != op )
                bad++;
        }
        char label[96];
        snprintf(label, sizeof(label), "%s: %d wire ids sorted, each round-trips", dialect->name, dialect->wire_count);
        CHECK_EQ(bad, 0, label);
    }
}

/* A wire script holding an id its dialect does not define loads; the id is an
 * instruction of its own that does nothing unless it is run. Run under
 * TORIRS_CS2_SURVEY it reports the wire id and goes on (without it, an assert:
 * the path of an instruction with no stack signature). */
static void
test_undefined_wire_loads(void)
{
    printf("TEST: a wire id the dialect does not define loads as UNDEFINED_WIRE_ID\n");
    struct RSCache_CS2_Script src;
    struct CS2VM2_Script dst;
    build_wire_switch_script(&src, 42);
    src.opcodes[2] = 9000; /* the old vm-dialect CC_CREATE_FLAT: no cache's */

    /* The SWITCH takes its case past pc 2: the script never reaches it. */
    CHECK(CS2VM2_ScriptCopyFromRSCache(&src, &dst, &CS2VM2_DIALECT_RS2_634), "rs2 loads a script holding wire 9000");
    CHECK_EQ(dst.opcodes[2], CS2_OP_UNDEFINED_WIRE_ID, "wire 9000 is UNDEFINED_WIRE_ID");
    CHECK_EQ(dst.undefined_wire_count, 1, "one undefined wire id");
    CHECK_EQ(dst.undefined_wires[0].pc, 2, "at pc 2");
    CHECK_EQ((int)dst.undefined_wires[0].wire, 9000, "wire 9000 kept");
    CHECK_EQ(run_converted_script(&dst), 7, "the path that skips it runs as before");
    struct CS2VM2_Script copy;
    CS2VM2_ScriptInit(&copy);
    CHECK(CS2VM2_ScriptCopy(&dst, &copy), "copied");
    CHECK(copy.undefined_wire_count == 1, "a copy keeps the record");
    CHECK(copy.undefined_wires != dst.undefined_wires, "in its own allocation");
    CHECK_EQ((int)copy.undefined_wires[0].wire, 9000, "of the wire id");
    CS2VM2_ScriptFree(&copy);
    CS2VM2_ScriptFree(&dst);

    /* The SWITCH misses: the script runs into it. The survey reports it and
     * the run goes on to the RETURN after it. */
    setenv("TORIRS_CS2_SURVEY", "1", 1);
    src.int_operands[0] = 41;
    CHECK(CS2VM2_ScriptCopyFromRSCache(&src, &dst, &CS2VM2_DIALECT_RS2_634), "loads again");
    struct CS2VM2 vm;
    CS2VM2_Init(&vm);
    struct CS2VM2_Thread* t = CS2VM2_ThreadMain(&vm);
    CS2VM2_PushCallScript(t, &dst);
    CHECK_EQ(CS2VM2_RunScript(t), CS2VM_EXECNO_DONE, "under the survey it runs on");
    CHECK_EQ(t->ints_stack_top, 0, "and touches no stack");
    CS2VM2_Free(&vm);
    CS2VM2_ScriptFree(&dst);
    unsetenv("TORIRS_CS2_SURVEY");

    /* OldSchool has no 4500 (634's STRUCT_PARAM): undefined there, defined at 634. */
    src.int_operands[0] = 42;
    src.opcodes[2] = 4500;
    CHECK(CS2VM2_ScriptCopyFromRSCache(&src, &dst, &CS2VM2_DIALECT_OSRS), "osrs loads 634's 4500");
    CHECK_EQ(dst.opcodes[2], CS2_OP_UNDEFINED_WIRE_ID, "osrs 4500 is UNDEFINED_WIRE_ID");
    CS2VM2_ScriptFree(&dst);
    CHECK(CS2VM2_ScriptCopyFromRSCache(&src, &dst, &CS2VM2_DIALECT_RS2_634), "rs2 takes 4500");
    CHECK_EQ(dst.opcodes[2], CS2_OP_STRUCT_PARAM, "rs2 4500 is STRUCT_PARAM");
    CHECK(dst.undefined_wires == NULL, "nothing undefined");
    CS2VM2_ScriptFree(&dst);
    free_wire_script(&src);
}

int
main(void)
{
    test_dialect_for_cache();
    test_dialect_tables();
    test_undefined_wire_loads();
    test_rs2_switch_hit();
    test_rs2_switch_miss();
    test_canonical_leaves_51();
    test_branch_if_one();
    test_varc_string_pair(
        CS2_OP_POP_VARC_STRING_OLD,
        CS2_OP_PUSH_VARC_STRING_OLD,
        CS2VM_HOST_REQUEST_POP_VARC_STRING_OLD,
        CS2VM_HOST_REQUEST_PUSH_VARC_STRING_OLD,
        "PUSH/POP_VARC_STRING_OLD (47/48)");
    test_varc_string_pair(
        CS2_OP_POP_VARC_STRING,
        CS2_OP_PUSH_VARC_STRING,
        CS2VM_HOST_REQUEST_POP_VARC_STRING,
        CS2VM_HOST_REQUEST_PUSH_VARC_STRING,
        "PUSH/POP_VARC_STRING (49/50)");
    test_rs2_overlay_binary_lookup();
    test_cc_create_arity(&CS2VM2_DIALECT_OSRS, 1, "rev 230+ pops the nested flag");
    test_cc_create_arity(&CS2VM2_DIALECT_OSRS_229, 0, "rev 229 and below pop three");
    test_cc_create_arity(&CS2VM2_DIALECT_RS2_634, 0, "RS2 pops three");
    test_array_model(&CS2VM2_DIALECT_OSRS_230, 42, "rev 230 and below share numbered arrays");
    test_array_model(&CS2VM2_DIALECT_OSRS, 0, "rev 231+ keeps a handle in its frame");

    if( g_fail )
    {
        printf("opcode dialect test: %d FAILED\n", g_fail);
        return 1;
    }
    printf("opcode dialect test: all passed\n");
    return 0;
}
