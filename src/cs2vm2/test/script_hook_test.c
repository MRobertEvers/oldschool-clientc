/*
 * The script hooks (CS2VM2_SetScriptHook / CS2VM2_ScriptWatch): RuneLite's
 * ScriptPreFired / ScriptPostFired for the ids someone watches, driven
 * through the real VM with hand-assembled scripts. A top-level script fires
 * PRE before its first opcode (arguments in its locals); a gosub callee fires
 * PRE with its arguments still on the caller's stack, where a hook may rewrite
 * them; RETURN fires POST with what the callee left above its frame's base;
 * an unwatched id fires nothing.
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

static void
assemble(struct CS2VM2_Script* script, int script_id, int int_args, struct Ins const* ins, int n)
{
    CS2VM2_ScriptInit(script);
    script->script_id = script_id;
    script->int_argument_count = int_args;
    script->local_int_count = int_args;
    script->op_count = n + 1;
    script->opcodes = calloc((size_t)n + 1, sizeof(uint16_t));
    script->int_operands = calloc((size_t)n + 1, sizeof(int));
    script->long_operands = calloc((size_t)n + 1, sizeof(int64_t));
    script->string_operands = calloc((size_t)n + 1, sizeof(char*));
    for( int k = 0; k < n; k++ )
    {
        script->opcodes[k] = (uint16_t)ins[k].op;
        script->int_operands[k] = ins[k].i;
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

static struct CS2VM2_Script* g_callee;

static int
test_host_exec(struct CS2VM2_Thread* thread, struct CS2VM_HostRequest* request)
{
    CS2VM2_ScriptPrepare(g_callee); /* what a loader does */
    if( request->kind == CS2VM_HOST_REQUEST_GOSUB_WITH_PARAMS && g_callee )
        return CS2VM2_PushCallScript(thread, g_callee);
    printf("  FAIL: unexpected host request %d\n", (int)request->kind);
    g_fail++;
    return CS2VM_EXECNO_ERROR;
}

static char g_trace[256];

static void
hook(void* user, struct CS2VM2_Thread* thread, int script_id, enum CS2VM2_ScriptHookPhase phase, int invoked,
     int int_count, int string_count)
{
    char step[64];
    int top = -1;
    (void)user;
    (void)string_count;
    if( phase == CS2VM2_SCRIPT_HOOK_PRE && !invoked && int_count > 0 )
        top = CS2VM2_GetIntCurrentFrameLocal(thread, int_count - 1);
    else if( int_count > 0 )
        top = thread->ints_stack[thread->ints_stack_top - 1];
    snprintf(step, sizeof(step), "%s%d%s:%d:%d ", phase == CS2VM2_SCRIPT_HOOK_PRE ? "pre" : "post", script_id,
             invoked ? "i" : "", int_count, top);
    strncat(g_trace, step, sizeof(g_trace) - strlen(g_trace) - 1);
    /* ScriptPreFired rewrites the last argument; ScriptPostFired the result. */
    if( script_id == 200 && phase == CS2VM2_SCRIPT_HOOK_PRE )
        thread->ints_stack[thread->ints_stack_top - 1] = 80;
    if( script_id == 200 && phase == CS2VM2_SCRIPT_HOOK_POST )
        thread->ints_stack[thread->ints_stack_top - 1] = 99;
}

/* 100(): gosub 200(7, 8) and return what it returned.
 * 200(a, b): return a + b. */
static int
run_caller(int* out_top)
{
    struct Ins caller_ins[] = {
        { CS2_OP_PUSH_CONSTANT_INT, 7 },
        { CS2_OP_PUSH_CONSTANT_INT, 8 },
        { CS2_OP_GOSUB_WITH_PARAMS, 200 },
    };
    struct Ins callee_ins[] = {
        { CS2_OP_PUSH_INT_LOCAL, 0 },
        { CS2_OP_PUSH_INT_LOCAL, 1 },
        { CS2_OP_ADD, 0 },
    };
    struct CS2VM2 vm;
    struct CS2VM2_Script caller, callee;
    int rc;

    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, NULL, test_host_exec);
    assemble(&caller, 100, 0, caller_ins, 3);
    assemble(&callee, 200, 2, callee_ins, 3);
    g_callee = &callee;
    {
        struct CS2VM2_Thread* t = CS2VM2_ThreadMain(&vm);
        CS2VM2_ScriptPrepare(&caller); /* what a loader does */
        CS2VM2_ThreadStart(t, &caller);
        rc = CS2VM2_RunScript(t);
        *out_top = t->ints_stack_top > 0 ? t->ints_stack[t->ints_stack_top - 1] : -1;
    }
    CS2VM2_Free(&vm);
    disassemble(&caller);
    disassemble(&callee);
    g_callee = NULL;
    return rc;
}

int
main(void)
{
    int top = 0;
    printf("script hooks\n");
    CS2VM2_SetScriptHook(hook, NULL);

    g_trace[0] = 0;
    CHECK(run_caller(&top) == CS2VM_EXECNO_DONE && top == 15, "nothing watched: the script runs as it is");
    CHECK(g_trace[0] == 0, "nothing watched: no hook fires");

    CHECK(CS2VM2_ScriptWatch(200, true), "watch the callee");
    CHECK(!CS2VM2_ScriptWatch(70000, true), "an id past the table is refused");
    CHECK(CS2VM2_ScriptWatchCount() == 1, "one id watched");
    g_trace[0] = 0;
    CHECK(run_caller(&top) == CS2VM_EXECNO_DONE, "the watched run completes");
    CHECK(strcmp(g_trace, "pre200i:2:8 post200i:1:87 ") == 0,
          "PRE sees the args on the stack (last on top), POST the returned value");
    CHECK(top == 99, "PRE's rewrite reached the callee (7+80) and POST's rewrite reached the caller");

    CHECK(CS2VM2_ScriptWatch(100, true), "watch the top-level script too");
    g_trace[0] = 0;
    (void)run_caller(&top);
    CHECK(strcmp(g_trace, "pre100:0:-1 pre200i:2:8 post200i:1:87 post100:1:99 ") == 0,
          "a top-level script fires PRE before its first opcode and POST at its RETURN");

    CHECK(CS2VM2_ScriptWatch(200, false) && CS2VM2_ScriptWatch(100, false), "unwatch both");
    CHECK(CS2VM2_ScriptWatchCount() == 0, "nothing watched again");
    g_trace[0] = 0;
    (void)run_caller(&top);
    CHECK(g_trace[0] == 0 && top == 15, "unwatched: silent and unchanged");

    CHECK(CS2VM2_ScriptWatch(200, true) && CS2VM2_ScriptWatch(200, true), "two watches of one id");
    CHECK(CS2VM2_ScriptWatch(200, false) && CS2VM2_ScriptWatchCount() == 1, "one unwatch leaves it watched");
    CHECK(CS2VM2_ScriptWatch(200, false) && CS2VM2_ScriptWatchCount() == 0, "the second releases it");
    CS2VM2_SetScriptHook(NULL, NULL);

    printf("%s (%d failures)\n", g_fail ? "FAILED" : "passed", g_fail);
    return g_fail ? 1 : 0;
}
