/*
 * Unit test for CC_SETCOMPONENTPARAM / CC_GETCOMPONENTPARAM (OldSchool wire
 * 1704/1703), driven through the real VM dispatch against a recording host.
 *
 * What it pins is the part a reading of the bytecode cannot make obvious: the
 * argument order. The scripts push (param_id, value, kind) and the top of the
 * stack is therefore `kind`, so the handler has to pop backwards; getting it
 * wrong writes the value under the id 0 and the id under nothing, which no
 * script would notice until a much later read came back with the wrong tag.
 * Also checked: the operand picks the dot component over the active one, the
 * same way every other CC opcode reads it.
 *
 * Before these opcodes existed the whole family fell through to
 * CS2VM2_Op_StackMetaStub, which asserts on an unknown signature — proc 228
 * (the gameframe's "build a text row" helper, called from ~50 scripts) aborted
 * the client outright.
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
    struct CS2VM_HostRequest_CC_ComponentParam param;
    /*
     * A COPY of param.str_value, taken during the call.
     *
     * A request's strings are borrowed for the duration of host_exec and no
     * longer (see cs2vm2_strpool.h); param.str_value is only valid while the
     * host is on the stack. Keeping the pointer and reading it after the run
     * is what a real host must not do, so this test does not do it either.
     * `had_str` records whether there was a string at all, which is the
     * property the int-write case checks.
     */
    int had_str;
    char str_value[128];
    /** What the fake host answers a getter with. */
    int answer;
};

static int
recording_host_exec(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request)
{
    struct RecordingHost* host = (struct RecordingHost*)thread->vm->user;
    host->calls++;
    host->kind = request->kind;
    host->param = request->u.cc_component_param;
    host->had_str = request->u.cc_component_param.str_value != NULL;
    snprintf(
        host->str_value, sizeof(host->str_value), "%s",
        request->u.cc_component_param.str_value ? request->u.cc_component_param.str_value : "");
    if( request->kind == CS2VM_HOST_REQUEST_CC_GETCOMPONENTPARAM )
        return CS2VM2_PushInt(thread, host->answer);
    return CS2VM_EXECNO_OK;
}

/*
 * Run one opcode with `pushes` constants in front of it, under `operand`.
 *
 * A push whose value is STR_PUSH is a push_constant_string of `str` instead of
 * an int, which is how the string-param write is assembled: the value goes on
 * the string stack and the int stack carries only the param id and the kind.
 */
#define STR_PUSH 0x7f000001

static void
run_op(
    struct RecordingHost* host,
    int op,
    int operand,
    int const* pushes,
    int push_count,
    char const* str,
    int active_component,
    int dot_component,
    int* out_top)
{
    struct CS2VM2 vm;
    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, host, recording_host_exec);

    struct CS2VM2_Script script;
    CS2VM2_ScriptInit(&script);
    int const op_count = push_count + 2;
    script.script_id = 9996;
    script.op_count = op_count;
    script.opcodes = calloc((size_t)op_count, sizeof(uint16_t));
    script.int_operands = calloc((size_t)op_count, sizeof(int));
    script.string_operands = calloc((size_t)op_count, sizeof(char*));

    for( int i = 0; i < push_count; i++ )
    {
        if( pushes[i] == STR_PUSH )
        {
            script.opcodes[i] = (uint16_t)CS2_OP_PUSH_CONSTANT_STRING;
            script.string_operands[i] = strdup(str ? str : "");
            continue;
        }
        script.opcodes[i] = (uint16_t)CS2_OP_PUSH_CONSTANT_INT;
        script.int_operands[i] = pushes[i];
    }
    script.opcodes[push_count] = (uint16_t)op;
    script.int_operands[push_count] = operand;
    script.opcodes[push_count + 1] = (uint16_t)CS2_OP_RETURN;

    struct CS2VM2_Thread* thread = CS2VM2_ThreadMain(&vm);
    CS2VM2_SetTargetComponentId(thread, 0, active_component);
    CS2VM2_SetTargetComponentId(thread, 1, dot_component);
    CS2VM2_PushCallScript(thread, &script);
    CS2VM2_RunScript(thread);

    if( out_top )
    {
        *out_top = 0;
        CS2VM2_PopInt(thread, out_top);
    }

    CS2VM2_Free(&vm);
    for( int i = 0; i < push_count; i++ )
        free(script.string_operands[i]);
    free(script.opcodes);
    free(script.int_operands);
    free(script.string_operands);
}

int
main(void)
{
    printf("TEST: CC component param opcodes\n");

    int const active = (161 << 16) | 10;
    int const dot = (161 << 16) | 11;

    /* Int setter, exactly as script 9581 pc 101..104 assembles it:
     * push param, push value, push kind 0. */
    {
        struct RecordingHost host;
        memset(&host, 0, sizeof(host));
        int const pushes[3] = { 2365, 600, CS2_CC_COMPONENTPARAM_KIND_INT };
        run_op(
            &host, CS2_OP_CC_SETCOMPONENTPARAM, 0, pushes, 3, NULL, active, dot, NULL);
        CHECK_INT(host.calls, 1, "setter reaches the host once");
        CHECK_INT((int)host.kind, (int)CS2VM_HOST_REQUEST_CC_SETCOMPONENTPARAM, "setter kind");
        CHECK_INT(host.param.param_id, 2365, "setter param id (bottom of the three)");
        CHECK_INT(host.param.value, 600, "setter value (middle)");
        CHECK_INT(host.param.kind, 0, "setter kind arg (top)");
        CHECK_INT(host.had_str == 0, 1, "an int write carries no string");
        CHECK_INT(host.param.component_id, active, "setter targets the active component");
    }

    /* String setter, as script 9581 pc 109..112 assembles it: push param,
     * push_string_local, push kind 2. Only two ints are on the stack, and an
     * extra int below them must survive — popping three would eat it and leave
     * the string behind, which is what a flat three-int arity does. */
    {
        struct RecordingHost host;
        memset(&host, 0, sizeof(host));
        int const pushes[4] = { -2, 1017, STR_PUSH, CS2_CC_COMPONENTPARAM_KIND_STRING };
        int top = 0;
        run_op(
            &host,
            CS2_OP_CC_SETCOMPONENTPARAM,
            1,
            pushes,
            4,
            "Bank of Gielinor",
            active,
            dot,
            &top);
        CHECK_INT(host.param.param_id, 1017, "string write takes the param id");
        CHECK_INT(host.param.kind, 2, "string write carries kind 2");
        CHECK_INT(
            host.had_str && strcmp(host.str_value, "Bank of Gielinor") == 0,
            1,
            "string write carries the string");
        CHECK_INT(host.param.component_id, dot, "operand 1 targets the dot component");
        CHECK_INT(top, -2, "the int under a string write is left alone");
    }

    /* Getter: one arg in, one int out. */
    {
        struct RecordingHost host;
        memset(&host, 0, sizeof(host));
        host.answer = -1;
        int const pushes[1] = { 2557 };
        int top = 0;
        run_op(&host, CS2_OP_CC_GETCOMPONENTPARAM, 0, pushes, 1, NULL, active, dot, &top);
        CHECK_INT(host.calls, 1, "getter reaches the host once");
        CHECK_INT((int)host.kind, (int)CS2VM_HOST_REQUEST_CC_GETCOMPONENTPARAM, "getter kind");
        CHECK_INT(host.param.param_id, 2557, "getter param id");
        CHECK_INT(host.param.component_id, active, "getter targets the active component");
        CHECK_INT(top, -1, "getter pushes the host's answer");
    }

    if( g_fail )
    {
        printf("%d failure(s)\n", g_fail);
        return 1;
    }
    printf("All CC component param opcode tests passed.\n");
    return 0;
}
