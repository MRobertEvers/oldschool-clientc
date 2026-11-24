/*
 * Remembered gosub callees (cs2vm2/cs2vm2_callee.h), through the real VM with
 * hand-assembled scripts and a host that counts what it is asked.
 *
 *   - the first gosub at a pc asks the host; the next push the same callee
 *     without asking;
 *   - a callee that is not resident yields, remembers nothing, and is asked
 *     for again on resume, exactly as before;
 *   - freeing ANY script makes every remembered callee stale, so a freed
 *     callee is never pushed: the host's new answer is;
 *   - preparing the caller again (a test that changed an opcode) forgets.
 *
 *   make -C src test-cs2-callee
 */

#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_callee.h"
#include "cs2vm2/cs2vm2_host.h"
#include "cs2vm2/cs2vm2_script.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
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

struct Ins
{
    int op;
    int i;
};

/* A script whose arrays are the script group's, as a loader's are, so
 * CS2VM2_ScriptFree may free it. */
static struct CS2VM2_Script*
assemble(int script_id, int int_args, struct Ins const* ins, int n)
{
    struct CS2VM2_Script* script = ToriMem_Calloc(ToriMem_Script(), 1, sizeof(*script));
    assert(script);
    CS2VM2_ScriptInit(script);
    script->script_id = script_id;
    script->int_argument_count = int_args;
    script->local_int_count = int_args;
    script->op_count = n + 1;
    script->opcodes = ToriMem_Calloc(ToriMem_Script(), (size_t)n + 1, sizeof(uint16_t));
    script->int_operands = ToriMem_Calloc(ToriMem_Script(), (size_t)n + 1, sizeof(int));
    assert(script->opcodes && script->int_operands);
    for( int k = 0; k < n; k++ )
    {
        script->opcodes[k] = (uint16_t)ins[k].op;
        script->int_operands[k] = ins[k].i;
    }
    script->opcodes[n] = (uint16_t)CS2_OP_RETURN;
    CS2VM2_ScriptPrepare(script); /* what a loader does */
    return script;
}

static void
destroy(struct CS2VM2_Script* script)
{
    CS2VM2_ScriptFree(script);
    ToriMem_Free(ToriMem_Script(), script);
}

/* The host: answers GOSUB_WITH_PARAMS with g_callee, or "not ready" while
 * g_yields is positive. Counts every ask. */
static struct CS2VM2_Script* g_callee;
static int g_asks;
static int g_yields;

static int
test_host_exec(struct CS2VM2_Thread* thread, struct CS2VM_HostRequest* request)
{
    if( request->kind != CS2VM_HOST_REQUEST_GOSUB_WITH_PARAMS )
    {
        printf("  FAIL: unexpected host request %d\n", (int)request->kind);
        g_fail++;
        return CS2VM_EXECNO_ERROR;
    }
    g_asks++;
    if( g_yields > 0 )
    {
        g_yields--;
        return CS2VM_EXECNO_YIELD;
    }
    assert(g_callee);
    return CS2VM2_PushCallScript(thread, g_callee);
}

/* Run `caller` to completion on `vm` (resuming after any yield) and answer
 * what it left on top of the int stack. */
static int
run(struct CS2VM2* vm, struct CS2VM2_Script* caller)
{
    struct CS2VM2_Thread* t = CS2VM2_ThreadMain(vm);
    struct CS2VM2_ThreadError err = { 0 };
    enum CS2VM2_ThreadStatus status;

    CS2VM2_ThreadStart(t, caller);
    status = CS2VM2_ThreadRun(t, &err);
    while( status == CS2VM2_THREAD_YIELDED )
        status = CS2VM2_ThreadResume(t, &err);
    if( status != CS2VM2_THREAD_DONE )
        return -1000;
    return t->ints_stack_top > 0 ? t->ints_stack[t->ints_stack_top - 1] : -1;
}

int
main(void)
{
    /* 100(): return 300(7, 8). */
    struct Ins const caller_ins[] = {
        { CS2_OP_PUSH_CONSTANT_INT, 7 },
        { CS2_OP_PUSH_CONSTANT_INT, 8 },
        { CS2_OP_GOSUB_WITH_PARAMS, 300 },
    };
    /* 300(a, b), first decode: a + b. */
    struct Ins const add_ins[] = {
        { CS2_OP_PUSH_INT_LOCAL, 0 },
        { CS2_OP_PUSH_INT_LOCAL, 1 },
        { CS2_OP_ADD, 0 },
    };
    /* 300(a, b), as reloaded: a * b. */
    struct Ins const mul_ins[] = {
        { CS2_OP_PUSH_INT_LOCAL, 0 },
        { CS2_OP_PUSH_INT_LOCAL, 1 },
        { CS2_OP_MULTIPLY, 0 },
    };
    struct CS2VM2 vm;

    printf("remembered gosub callees\n");
    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, NULL, test_host_exec);

    struct CS2VM2_Script* caller = assemble(100, 0, caller_ins, 3);
    struct CS2VM2_Script* added = assemble(300, 2, add_ins, 3);
    CHECK(caller->callees != NULL, "a script with a gosub gets a callee table at load");
    CHECK(added->callees == NULL, "a script without one gets none");

    /* Not resident: the gosub yields, remembers nothing, asks again. */
    g_callee = added;
    g_asks = 0;
    g_yields = 1;
    CHECK(run(&vm, caller) == 15, "a callee that yields first: the run completes");
    CHECK(g_asks == 2, "the yield and the retry both asked the host");
    CHECK(CS2VM2_CalleeRemembered(caller, 2) == added, "the callee the retry pushed is remembered");

    /* Remembered: no ask. */
    g_asks = 0;
    CHECK(run(&vm, caller) == 15, "second run: same answer");
    CHECK(run(&vm, caller) == 15, "third run: same answer");
    CHECK(g_asks == 0, "a remembered callee is pushed without asking the host");

    /* Any script freed: every table is stale. */
    {
        struct CS2VM2_Script* other = assemble(400, 0, add_ins, 1);
        destroy(other);
    }
    CHECK(CS2VM2_CalleeRemembered(caller, 2) == NULL, "freeing any script: remembered callees read stale");
    g_asks = 0;
    CHECK(run(&vm, caller) == 15, "after a free: same answer");
    CHECK(g_asks == 1, "after a free: the gosub asks once again");

    /* The callee itself freed and its id reloaded as another script: the
     * freed one is never pushed. */
    struct CS2VM2_Script* multiplied = assemble(300, 2, mul_ins, 3);
    destroy(added);
    g_callee = multiplied;
    g_asks = 0;
    CHECK(run(&vm, caller) == 56, "callee freed and reloaded: the new script runs");
    CHECK(g_asks == 1, "callee freed and reloaded: asked once");
    g_asks = 0;
    CHECK(run(&vm, caller) == 56 && g_asks == 0, "and is remembered in its turn");

    /* Preparing the caller again forgets. */
    CS2VM2_ScriptPrepare(caller);
    CHECK(CS2VM2_CalleeRemembered(caller, 2) == NULL, "prepared again: forgotten");
    g_asks = 0;
    CHECK(run(&vm, caller) == 56 && g_asks == 1, "prepared again: asks once");

    CS2VM2_Free(&vm);
    destroy(caller);
    destroy(multiplied);

    if( g_fail )
        printf("callee memory: %d FAILED\n", g_fail);
    else
        printf("callee memory: all passed\n");
    return g_fail ? 1 : 0;
}
