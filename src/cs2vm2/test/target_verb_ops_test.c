#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_script.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct RecordingHost
{
    int calls;
    enum CS2VM_HostRequestKind kind;
    int component_id;
    int op_index;
    int script_id;
    int argc;
    int argv[2];
    char text[32];
};

static int
recording_host_exec(struct CS2VM2_Thread* thread, struct CS2VM_HostRequest* request)
{
    struct RecordingHost* host = (struct RecordingHost*)thread->vm->user;
    host->calls++;
    host->kind = request->kind;
    if( request->kind == CS2VM_HOST_REQUEST_IF_SETTARGETVERB )
    {
        host->component_id = request->u.if_set_target_verb.component_id;
        snprintf(host->text, sizeof(host->text), "%s", request->u.if_set_target_verb.text);
    }
    else if( request->kind == CS2VM_HOST_REQUEST_IF_SETONTARGETENTER ||
             request->kind == CS2VM_HOST_REQUEST_IF_SETONTARGETLEAVE ||
             request->kind == CS2VM_HOST_REQUEST_IF_SETONCLICKREPEAT ||
             request->kind == CS2VM_HOST_REQUEST_IF_SETONRELEASE ||
             request->kind == CS2VM_HOST_REQUEST_IF_SETONDIALOGABORT )
    {
        host->component_id = request->u.if_set_on_op.component_id;
        host->script_id = request->u.if_set_on_op.script_id;
        host->argc = request->u.if_set_on_op.args->int_arg_count;
        memcpy(host->argv, request->u.if_set_on_op.args->int_args, sizeof(host->argv));
    }
    else if( request->kind == CS2VM_HOST_REQUEST_CC_SETONTARGETENTER ||
             request->kind == CS2VM_HOST_REQUEST_CC_SETONTARGETLEAVE ||
             request->kind == CS2VM_HOST_REQUEST_CC_SETONCLICKREPEAT ||
             request->kind == CS2VM_HOST_REQUEST_CC_SETONRELEASE ||
             request->kind == CS2VM_HOST_REQUEST_CC_SETONDIALOGABORT ||
             request->kind == CS2VM_HOST_REQUEST_CC_SETONFRIENDTRANSMIT ||
             request->kind == CS2VM_HOST_REQUEST_CC_SETONSTATTRANSMIT )
    {
        host->component_id = request->u.cc_set_on_op.component_id;
        host->script_id = request->u.cc_set_on_op.script_id;
        host->argc = request->u.cc_set_on_op.args->int_arg_count;
        memcpy(host->argv, request->u.cc_set_on_op.args->int_args, sizeof(host->argv));
    }
    else if( request->kind == CS2VM_HOST_REQUEST_CC_GETOP ||
             request->kind == CS2VM_HOST_REQUEST_IF_GETOP )
    {
        host->component_id = request->u.widget_get_op.component_id;
        host->op_index = request->u.widget_get_op.op_index;
        return CS2VM2_PushStr(thread, CS2VM2_StrDup(thread, "fixture operation"));
    }
    return CS2VM_EXECNO_OK;
}

static int
is_named_set_on_opcode(int opcode)
{
    switch( opcode )
    {
    case CS2_OP_IF_SETONTARGETENTER:
    case CS2_OP_IF_SETONTARGETLEAVE:
    case CS2_OP_IF_SETONCLICKREPEAT:
    case CS2_OP_IF_SETONRELEASE:
    case CS2_OP_IF_SETONDIALOGABORT:
        return 1;
    default:
        return 0;
    }
}

static int
run_target_hook(
    int opcode,
    int operand,
    int named_component,
    int active,
    int dot,
    struct RecordingHost* host)
{
    struct CS2VM2 vm;
    struct CS2VM2_Script script;
    struct CS2VM2_Thread* thread;
    int const named = is_named_set_on_opcode(opcode);
    int const op_count = named ? 7 : 6;

    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, host, recording_host_exec);
    CS2VM2_ScriptInit(&script);
    script.script_id = 9447;
    script.op_count = op_count;
    script.opcodes = calloc((size_t)op_count, sizeof(uint16_t));
    script.int_operands = calloc((size_t)op_count, sizeof(int));
    script.string_operands = calloc((size_t)op_count, sizeof(char*));
    script.opcodes[0] = CS2_OP_PUSH_CONSTANT_INT;
    script.int_operands[0] = 2617;
    script.opcodes[1] = CS2_OP_PUSH_CONSTANT_INT;
    script.int_operands[1] = 1;
    script.opcodes[2] = CS2_OP_PUSH_CONSTANT_INT;
    script.int_operands[2] = 556;
    script.opcodes[3] = CS2_OP_PUSH_CONSTANT_STRING;
    script.string_operands[3] = strdup("ii");
    if( named )
    {
        script.opcodes[4] = CS2_OP_PUSH_CONSTANT_INT;
        script.int_operands[4] = named_component;
    }
    script.opcodes[op_count - 2] = (uint16_t)opcode;
    script.int_operands[op_count - 2] = operand;
    script.opcodes[op_count - 1] = CS2_OP_RETURN;

    thread = CS2VM2_ThreadMain(&vm);
    CS2VM2_SetTargetComponentId(thread, 0, active);
    CS2VM2_SetTargetComponentId(thread, 1, dot);
    CS2VM2_PushCallScript(thread, &script);
    {
        int const result = CS2VM2_RunScript(thread);
        CS2VM2_Free(&vm);
        free(script.string_operands[3]);
        free(script.opcodes);
        free(script.int_operands);
        free(script.string_operands);
        return result;
    }
}

static int
run_target_verb(int opcode, int operand, int named_component, int active, int dot,
                struct RecordingHost* host)
{
    struct CS2VM2 vm;
    struct CS2VM2_Script script;
    struct CS2VM2_Thread* thread;
    int const named = opcode == CS2_OP_IF_SETTARGETVERB;
    int const op_count = named ? 4 : 3;

    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, host, recording_host_exec);
    CS2VM2_ScriptInit(&script);
    script.script_id = 7779;
    script.op_count = op_count;
    script.opcodes = calloc((size_t)op_count, sizeof(uint16_t));
    script.int_operands = calloc((size_t)op_count, sizeof(int));
    script.string_operands = calloc((size_t)op_count, sizeof(char*));
    script.opcodes[0] = CS2_OP_PUSH_CONSTANT_STRING;
    script.string_operands[0] = strdup("Use");
    if( named )
    {
        script.opcodes[1] = CS2_OP_PUSH_CONSTANT_INT;
        script.int_operands[1] = named_component;
    }
    script.opcodes[op_count - 2] = (uint16_t)opcode;
    script.int_operands[op_count - 2] = operand;
    script.opcodes[op_count - 1] = CS2_OP_RETURN;

    thread = CS2VM2_ThreadMain(&vm);
    CS2VM2_SetTargetComponentId(thread, 0, active);
    CS2VM2_SetTargetComponentId(thread, 1, dot);
    CS2VM2_PushCallScript(thread, &script);
    int result = CS2VM2_RunScript(thread);
    CS2VM2_Free(&vm);
    free(script.string_operands[0]);
    free(script.opcodes);
    free(script.int_operands);
    free(script.string_operands);
    return result;
}

static int
run_get_op(
    int opcode,
    int operand,
    int named_component,
    int active,
    int dot,
    int op_index,
    struct RecordingHost* host)
{
    struct CS2VM2 vm;
    struct CS2VM2_Script script;
    struct CS2VM2_Thread* thread;
    int const named = opcode == CS2_OP_IF_GETOP;
    int const op_count = named ? 4 : 3;

    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, host, recording_host_exec);
    CS2VM2_ScriptInit(&script);
    script.script_id = 54;
    script.op_count = op_count;
    script.opcodes = calloc((size_t)op_count, sizeof(uint16_t));
    script.int_operands = calloc((size_t)op_count, sizeof(int));
    script.string_operands = calloc((size_t)op_count, sizeof(char*));
    script.opcodes[0] = CS2_OP_PUSH_CONSTANT_INT;
    script.int_operands[0] = op_index;
    if( named )
    {
        /* Rev-239 IF_GETOP stack order: op index first, component on top. */
        script.opcodes[1] = CS2_OP_PUSH_CONSTANT_INT;
        script.int_operands[1] = named_component;
    }
    script.opcodes[op_count - 2] = (uint16_t)opcode;
    script.int_operands[op_count - 2] = operand;
    script.opcodes[op_count - 1] = CS2_OP_RETURN;

    thread = CS2VM2_ThreadMain(&vm);
    CS2VM2_SetTargetComponentId(thread, 0, active);
    CS2VM2_SetTargetComponentId(thread, 1, dot);
    CS2VM2_PushCallScript(thread, &script);
    int result = CS2VM2_RunScript(thread);
    CS2VM2_Free(&vm);
    free(script.opcodes);
    free(script.int_operands);
    free(script.string_operands);
    return result;
}

int
main(void)
{
    struct RecordingHost host = { 0 };
    int const inventory = 149 << 16;
    int const child = inventory | 1;

    if( run_target_verb(CS2_OP_CC_SETTARGETVERB, 1, -1, inventory, child, &host) !=
            CS2VM_EXECNO_DONE ||
        host.calls != 1 || host.kind != CS2VM_HOST_REQUEST_IF_SETTARGETVERB ||
        host.component_id != child || strcmp(host.text, "Use") != 0 )
    {
        fputs("CC_SETTARGETVERB did not preserve its dot target and text\n", stderr);
        return 1;
    }

    memset(&host, 0, sizeof(host));
    if( run_target_verb(CS2_OP_IF_SETTARGETVERB, 0, inventory, -1, -1, &host) !=
            CS2VM_EXECNO_DONE ||
        host.calls != 1 || host.kind != CS2VM_HOST_REQUEST_IF_SETTARGETVERB ||
        host.component_id != inventory || strcmp(host.text, "Use") != 0 )
    {
        fputs("IF_SETTARGETVERB did not preserve its named target and text\n", stderr);
        return 1;
    }

    memset(&host, 0, sizeof(host));
    if( run_target_hook(
            CS2_OP_IF_SETONTARGETENTER, 0, child, -1, -1, &host) != CS2VM_EXECNO_DONE ||
        host.calls != 1 || host.kind != CS2VM_HOST_REQUEST_IF_SETONTARGETENTER ||
        host.component_id != child || host.script_id != 2617 || host.argc != 2 ||
        host.argv[0] != 1 || host.argv[1] != 556 )
    {
        fputs("IF_SETONTARGETENTER did not preserve the spellbook hook\n", stderr);
        return 1;
    }

    memset(&host, 0, sizeof(host));
    if( run_target_hook(
            CS2_OP_CC_SETONTARGETLEAVE, 1, -1, inventory, child, &host) !=
            CS2VM_EXECNO_DONE ||
        host.calls != 1 || host.kind != CS2VM_HOST_REQUEST_CC_SETONTARGETLEAVE ||
        host.component_id != child || host.script_id != 2617 || host.argc != 2 ||
        host.argv[0] != 1 || host.argv[1] != 556 )
    {
        fputs("CC_SETONTARGETLEAVE did not preserve its dot-target hook\n", stderr);
        return 1;
    }

    {
        struct HookCase
        {
            int opcode;
            enum CS2VM_HostRequestKind kind;
            int named;
            char const* name;
        } const hook_cases[] = {
            { CS2_OP_CC_SETONCLICKREPEAT,
              CS2VM_HOST_REQUEST_CC_SETONCLICKREPEAT, 0, "CC_SETONCLICKREPEAT" },
            { CS2_OP_CC_SETONRELEASE,
              CS2VM_HOST_REQUEST_CC_SETONRELEASE, 0, "CC_SETONRELEASE" },
            { CS2_OP_CC_SETONDIALOGABORT,
              CS2VM_HOST_REQUEST_CC_SETONDIALOGABORT, 0, "CC_SETONDIALOGABORT" },
            { CS2_OP_CC_SETONFRIENDTRANSMIT,
              CS2VM_HOST_REQUEST_CC_SETONFRIENDTRANSMIT, 0, "CC_SETONFRIENDTRANSMIT" },
            { CS2_OP_CC_SETONSTATTRANSMIT,
              CS2VM_HOST_REQUEST_CC_SETONSTATTRANSMIT, 0, "CC_SETONSTATTRANSMIT" },
            { CS2_OP_IF_SETONCLICKREPEAT,
              CS2VM_HOST_REQUEST_IF_SETONCLICKREPEAT, 1, "IF_SETONCLICKREPEAT" },
            { CS2_OP_IF_SETONRELEASE,
              CS2VM_HOST_REQUEST_IF_SETONRELEASE, 1, "IF_SETONRELEASE" },
            { CS2_OP_IF_SETONDIALOGABORT,
              CS2VM_HOST_REQUEST_IF_SETONDIALOGABORT, 1, "IF_SETONDIALOGABORT" },
        };
        size_t i;
        for( i = 0; i < sizeof(hook_cases) / sizeof(hook_cases[0]); i++ )
        {
            int expect_component = hook_cases[i].named ? inventory : child;
            memset(&host, 0, sizeof(host));
            if( run_target_hook(
                    hook_cases[i].opcode,
                    hook_cases[i].named ? 0 : 1,
                    inventory,
                    inventory,
                    child,
                    &host) != CS2VM_EXECNO_DONE ||
                host.calls != 1 || host.kind != hook_cases[i].kind ||
                host.component_id != expect_component || host.script_id != 2617 ||
                host.argc != 2 || host.argv[0] != 1 || host.argv[1] != 556 )
            {
                fprintf(stderr, "%s did not preserve its runtime hook\n", hook_cases[i].name);
                return 1;
            }
        }
    }

    memset(&host, 0, sizeof(host));
    if( run_get_op(CS2_OP_CC_GETOP, 1, -1, inventory, child, 1, &host) !=
            CS2VM_EXECNO_DONE ||
        host.calls != 1 || host.kind != CS2VM_HOST_REQUEST_CC_GETOP ||
        host.component_id != child || host.op_index != 1 )
    {
        fputs("CC_GETOP did not preserve its dot target and operation index\n", stderr);
        return 1;
    }

    memset(&host, 0, sizeof(host));
    if( run_get_op(CS2_OP_IF_GETOP, 0, inventory, -1, -1, 1, &host) !=
            CS2VM_EXECNO_DONE ||
        host.calls != 1 || host.kind != CS2VM_HOST_REQUEST_IF_GETOP ||
        host.component_id != inventory || host.op_index != 1 )
    {
        fputs("IF_GETOP did not pop component then operation index like rev239\n", stderr);
        return 1;
    }

    puts("target verb/getop/runtime-hook opcodes: CC and IF forms passed");
    return 0;
}
