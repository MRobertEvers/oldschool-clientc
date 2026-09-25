/*
 * Unit test for CC_TRIGGEROP (1928), driven through the real VM dispatch
 * against a recording host.
 *
 * Before this opcode existed it fell through to CS2VM2_Op_StackMetaStub,
 * which asserts on an unknown signature — script6014 (the shift-click
 * inventory option handler, called on every inventory left-click) aborted
 * the client outright:
 *
 *     if( cc_find($component0, $comsubid2) = ^true ) {
 *         ...
 *         cc_triggerop(enum(int, int, enum_4303, oc_shiftclickiop($int1)));
 *     }
 *
 * Stack shape (one int in, nothing out) is a witnessed fact from
 * 3rd/rscache/tools/cs2/local_commands.py (`"cc_triggerop": (["INT"], [],
 * False)`), not guessed — StackMetaStub exists precisely to stop a guessed
 * arity from landing quietly.
 */

#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_script.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

struct RecordingHost
{
    int calls;
    enum CS2VM_HostRequestKind kind;
    struct CS2VM_HostRequest_CC_TRIGGEROP trigger_op;
};

static int
recording_host_exec(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request)
{
    struct RecordingHost* host = (struct RecordingHost*)thread->vm->user;
    host->calls++;
    host->kind = request->kind;
    host->trigger_op = request->u.CC_TRIGGEROP;
    return CS2VM_EXECNO_OK;
}

/* One PUSH_CONSTANT_INT(op_index), then CC_TRIGGEROP(operand), then RETURN —
 * the exact shape script6014 compiles to at pc=27 (op index arrives already
 * computed by an earlier ENUM lookup, so only the result needs pushing
 * here). */
static void
run_op(
    struct RecordingHost* host,
    int operand,
    int op_index,
    int active_component,
    int dot_component)
{
    struct CS2VM2 vm;
    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, host, recording_host_exec);

    struct CS2VM2_Script script;
    CS2VM2_ScriptInit(&script);
    int const op_count = 3;
    script.script_id = 6014;
    script.op_count = op_count;
    script.opcodes = calloc((size_t)op_count, sizeof(uint16_t));
    script.int_operands = calloc((size_t)op_count, sizeof(int));
    script.string_operands = calloc((size_t)op_count, sizeof(char*));

    script.opcodes[0] = (uint16_t)CS2_OP_PUSH_CONSTANT_INT;
    script.int_operands[0] = op_index;
    script.opcodes[1] = (uint16_t)CS2_OP_CC_TRIGGEROP;
    script.int_operands[1] = operand;
    script.opcodes[2] = (uint16_t)CS2_OP_RETURN;

    struct CS2VM2_Thread* thread = CS2VM2_ThreadMain(&vm);
    CS2VM2_SetTargetComponentId(thread, 0, active_component);
    CS2VM2_SetTargetComponentId(thread, 1, dot_component);
    CS2VM2_PushCallScript(thread, &script);
    CS2VM2_RunScript(thread);

    CS2VM2_Free(&vm);
    free(script.opcodes);
    free(script.int_operands);
    free(script.string_operands);
}

int
main(void)
{
    printf("TEST: CC_TRIGGEROP\n");

    int const active = (161 << 16) | 10;
    int const dot = (161 << 16) | 11;

    /* script6014's own case: operand 0, targets the dot component cc_find
     * just located. */
    {
        struct RecordingHost host;
        memset(&host, 0, sizeof(host));
        run_op(&host, 0, 3, active, dot);
        CHECK_INT(host.calls, 1, "reaches the host exactly once (no StackMetaStub abort)");
        CHECK_INT((int)host.kind, (int)CS2VM_HOST_REQUEST_CC_TRIGGEROP, "request kind");
        CHECK_INT(
            host.trigger_op.component_id,
            active,
            "operand 0 targets the active component");
        CHECK_INT(
            host.trigger_op.op_index,
            3,
            "op index is the popped int, not the operand");
    }

    /* operand 1 targets the dot component, same convention as every other CC
     * setter (CC_SETPOSITION, CC_SETTRANS, ...). */
    {
        struct RecordingHost host;
        memset(&host, 0, sizeof(host));
        run_op(&host, 1, 1, active, dot);
        CHECK_INT(
            host.trigger_op.component_id, dot, "operand 1 targets the dot component");
        CHECK_INT(host.trigger_op.op_index, 1, "op index 1 (primary/left-click)");
    }

    if( g_fail )
    {
        printf("%d failure(s)\n", g_fail);
        return 1;
    }
    printf("All CC_TRIGGEROP opcode tests passed.\n");
    return 0;
}
