/*
 * The await record (cs2vm2.h, CS2VM2_AwaitSpent): what a handler that finds a
 * resource missing reads to tell "first time: yield and load" from "this is
 * the retry: the load was tried".
 *
 *   1. A load that FAILED is not awaited again forever: the retry finds the
 *      resource still missing, sees the record, and completes with a default.
 *      One yield, no halt.
 *   2. A resource EVICTED LATER is awaited again: the same instruction, run
 *      again after its retry completed (a loop), finds it missing and yields
 *      again instead of degrading. The record was retired with the retry.
 *   3. A different instruction asking for the same missing resource after the
 *      retry completed is not a retry either.
 *
 * The record is written only on the yield path and retired by the loop when
 * the retried instruction completes; nothing is written for a request that
 * neither yields nor is the retry. The host here is a fake resource keyed by
 * PUSH_VARBIT's id: the real host's loads (rs_cs2_yield_load) ask the same
 * two questions, CS2VM2_AwaitSpent and CS2VM2_AwaitNote.
 */

#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_script.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_fail = 0;

#define CHECK_INT(got, want, label)                                                                \
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

/* One fake resource, keyed by the varbit id: resident or not. */
struct Store
{
    int resident;
    int evict_after_answer;
    int yields;
    int degrades;
    int answers;
};

enum
{
    RESOURCE = 77
};

static int
host_exec(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request)
{
    struct Store* store = (struct Store*)CS2VM_USER(thread);
    int id;

    if( request->kind != CS2VM_HOST_REQUEST_PUSH_VARBIT )
        return CS2VM_EXECNO_ERROR;
    id = request->u.PUSH_VARBIT.varbit_id;
    if( store->resident )
    {
        store->answers++;
        if( store->evict_after_answer )
            store->resident = 0;
        return CS2VM2_PushInt(thread, 1);
    }
    if( CS2VM2_AwaitSpent(thread, CS2VM_HOST_REQUEST_PUSH_VARBIT, id, -1) )
    {
        store->degrades++;
        return CS2VM2_PushInt(thread, -1);
    }
    store->yields++;
    CS2VM2_AwaitNote(thread, CS2VM_HOST_REQUEST_PUSH_VARBIT, id, -1);
    return CS2VM_EXECNO_YIELD;
}

static void
finish(
    struct CS2VM2_Script* script,
    uint16_t* opcodes,
    int* operands,
    int n)
{
    script->op_count = n;
    script->opcodes = opcodes;
    script->int_operands = operands;
    CS2VM2_ScriptPrepare(script);
}

/* PUSH_VARBIT RESOURCE twice in a loop over ONE instruction:
 *   0 C 0; 1 PI 0          i = 0
 *   2 PUSH_VARBIT          the site
 *   3 PI 1                 drop the answer
 *   4 LI 0; 5 C 1; 6 ADD; 7 PI 0     i++
 *   8 LI 0; 9 C 2; 10 BRLT -9        while i < 2 -> 2
 *   11 RETURN */
static void
build_loop(
    struct CS2VM2_Script* script,
    uint16_t* o,
    int* a)
{
    int n = 0;
    CS2VM2_ScriptInit(script);
    script->script_id = 880101;
    script->local_int_count = 2;
#define EMIT(op, arg)                                                                              \
    do                                                                                             \
    {                                                                                              \
        o[n] = (uint16_t)(op);                                                                     \
        a[n] = (arg);                                                                              \
        n++;                                                                                       \
    } while( 0 )
    EMIT(CS2_OP_PUSH_CONSTANT_INT, 0);
    EMIT(CS2_OP_POP_INT_LOCAL, 0);
    EMIT(CS2_OP_PUSH_VARBIT, RESOURCE);
    EMIT(CS2_OP_POP_INT_LOCAL, 1);
    EMIT(CS2_OP_PUSH_INT_LOCAL, 0);
    EMIT(CS2_OP_PUSH_CONSTANT_INT, 1);
    EMIT(CS2_OP_ADD, 0);
    EMIT(CS2_OP_POP_INT_LOCAL, 0);
    EMIT(CS2_OP_PUSH_INT_LOCAL, 0);
    EMIT(CS2_OP_PUSH_CONSTANT_INT, 2);
    EMIT(CS2_OP_BRANCH_LESS_THAN, -9);
    EMIT(CS2_OP_RETURN, 0);
    finish(script, o, a, n);
}

/* Two different instructions asking for the same resource:
 *   0 PUSH_VARBIT; 1 PI 0; 2 PUSH_VARBIT; 3 PI 1; 4 RETURN */
static void
build_two_sites(
    struct CS2VM2_Script* script,
    uint16_t* o,
    int* a)
{
    int n = 0;
    CS2VM2_ScriptInit(script);
    script->script_id = 880102;
    script->local_int_count = 2;
    EMIT(CS2_OP_PUSH_VARBIT, RESOURCE);
    EMIT(CS2_OP_POP_INT_LOCAL, 0);
    EMIT(CS2_OP_PUSH_VARBIT, RESOURCE);
    EMIT(CS2_OP_POP_INT_LOCAL, 1);
    EMIT(CS2_OP_RETURN, 0);
#undef EMIT
    finish(script, o, a, n);
}

static void
free_script(struct CS2VM2_Script* script)
{
    script->opcodes = NULL;
    script->int_operands = NULL;
    CS2VM2_ScriptFree(script);
}

static void
test_failed_load_completes(void)
{
    struct CS2VM2 vm;
    struct CS2VM2_Script script;
    struct CS2VM2_ThreadError err;
    struct Store store;
    uint16_t o[16];
    int a[16];

    printf("TEST: a load that failed is not awaited again forever\n");
    memset(&store, 0, sizeof(store));
    memset(&err, 0, sizeof(err));
    build_two_sites(&script, o, a);
    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, &store, host_exec);
    struct CS2VM2_Thread* thread = CS2VM2_ThreadMain(&vm);
    CS2VM2_ThreadStart(thread, &script);

    CHECK_INT((int)CS2VM2_ThreadRun(thread, &err), (int)CS2VM2_THREAD_YIELDED, "the first ask yields");
    CHECK_INT(store.yields, 1, "one yield");
    /* The load fails: the resource stays missing. */
    CHECK_INT(
        (int)CS2VM2_ThreadResume(thread, &err),
        (int)CS2VM2_THREAD_YIELDED,
        "the retry completes; the NEXT instruction is a new wait");
    CHECK_INT(store.degrades, 1, "the retry degraded instead of yielding again");
    CHECK_INT(CS2VM2_GetIntCurrentFrameLocal(thread, 0), -1, "with the default");
    CHECK_INT(store.yields, 2, "the second instruction is not the retry: it yields");
    CHECK_INT((int)CS2VM2_ThreadResume(thread, &err), (int)CS2VM2_THREAD_DONE, "its retry completes");
    CHECK_INT(store.degrades, 2, "and degrades in turn");
    CHECK_INT(store.yields, 2, "no instruction yielded twice");

    CS2VM2_Free(&vm);
    free_script(&script);
}

static void
test_evicted_later_is_awaited_again(void)
{
    struct CS2VM2 vm;
    struct CS2VM2_Script script;
    struct CS2VM2_ThreadError err;
    struct Store store;
    uint16_t o[16];
    int a[16];

    printf("TEST: a resource evicted later is awaited again at the same instruction\n");
    memset(&store, 0, sizeof(store));
    memset(&err, 0, sizeof(err));
    store.evict_after_answer = 1;
    build_loop(&script, o, a);
    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, &store, host_exec);
    struct CS2VM2_Thread* thread = CS2VM2_ThreadMain(&vm);
    CS2VM2_ThreadStart(thread, &script);

    CHECK_INT((int)CS2VM2_ThreadRun(thread, &err), (int)CS2VM2_THREAD_YIELDED, "first visit yields");
    store.resident = 1; /* the load lands */
    CHECK_INT(
        (int)CS2VM2_ThreadResume(thread, &err),
        (int)CS2VM2_THREAD_YIELDED,
        "the retry answers; the second visit (evicted meanwhile) yields again");
    CHECK_INT(store.answers, 1, "the retry was answered from the loaded resource");
    CHECK_INT(store.degrades, 0, "nothing degraded: the second visit is a new wait");
    CHECK_INT(store.yields, 2, "two yields at one instruction, one per visit");
    store.resident = 1; /* and lands again */
    CHECK_INT((int)CS2VM2_ThreadResume(thread, &err), (int)CS2VM2_THREAD_DONE, "the loop finishes");
    CHECK_INT(store.answers, 2, "both visits answered");
    CHECK_INT(store.degrades, 0, "none degraded");

    CS2VM2_Free(&vm);
    free_script(&script);
}

int
main(void)
{
    test_failed_load_completes();
    test_evicted_later_is_awaited_again();
    if( g_fail )
    {
        printf("await record test: %d FAILED\n", g_fail);
        return 1;
    }
    printf("await record test: all passed\n");
    return 0;
}
