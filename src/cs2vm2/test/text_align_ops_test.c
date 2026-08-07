/*
 * Revision-239 text-alignment opcode coverage.
 *
 * Golden clientscript 600 is the server-callable wrapper used by NPC chat:
 *
 *   if_settextalign(1, 1, 16, chat_left:text)
 *
 * Its four integer arguments are pushed in that order, so IF_SETTEXTALIGN
 * must pop the component first and preserve the three alignment values. A
 * reversed pop order leaves chat_left:text at its cache default valign=top,
 * which puts Hans's one-line greeting roughly 24 pixels too high.
 */

#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_script.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail;

#define CHECK_INT(got, want, label)                                                                \
    do                                                                                             \
    {                                                                                              \
        int const gv = (got);                                                                      \
        int const wv = (want);                                                                     \
        if( gv == wv )                                                                             \
            printf("  ok: %s == %d\n", label, gv);                                                \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s got %d want %d\n", label, gv, wv);                                 \
            g_fail++;                                                                              \
        }                                                                                          \
    } while( 0 )

struct RecordingHost
{
    int calls;
    enum CS2VM_HostRequestKind kind;
    struct CS2VM_HostRequest_CC_SetTextAlign align;
};

static int
recording_host_exec(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request)
{
    struct RecordingHost* host = (struct RecordingHost*)thread->vm->user;
    host->calls++;
    host->kind = request->kind;
    host->align = request->u.cc_set_text_align;
    return CS2VM_EXECNO_OK;
}

static void
run_align(
    struct RecordingHost* host,
    int opcode,
    int operand,
    int const* pushes,
    int push_count,
    int active_component,
    int dot_component)
{
    struct CS2VM2 vm;
    struct CS2VM2_Script script;
    struct CS2VM2_Thread* thread;
    int const op_count = push_count + 2;

    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, host, recording_host_exec);
    CS2VM2_ScriptInit(&script);
    script.script_id = 600;
    script.op_count = op_count;
    script.opcodes = calloc((size_t)op_count, sizeof(uint16_t));
    script.int_operands = calloc((size_t)op_count, sizeof(int));
    script.string_operands = calloc((size_t)op_count, sizeof(char*));

    for( int i = 0; i < push_count; i++ )
    {
        script.opcodes[i] = (uint16_t)CS2_OP_PUSH_CONSTANT_INT;
        script.int_operands[i] = pushes[i];
    }
    script.opcodes[push_count] = (uint16_t)opcode;
    script.int_operands[push_count] = operand;
    script.opcodes[push_count + 1] = (uint16_t)CS2_OP_RETURN;

    thread = CS2VM2_ThreadMain(&vm);
    CS2VM2_SetTargetComponentId(thread, 0, active_component);
    CS2VM2_SetTargetComponentId(thread, 1, dot_component);
    CS2VM2_PushCallScript(thread, &script);
    CHECK_INT(CS2VM2_RunScript(thread), CS2VM_EXECNO_DONE, "script completes");

    CS2VM2_Free(&vm);
    free(script.opcodes);
    free(script.int_operands);
    free(script.string_operands);
}

int
main(void)
{
    int const chat_left_text = (231 << 16) | 6;

    printf("TEST: revision-239 text alignment opcodes\n");

    /* Literal body of the server-callable golden clientscript 600. */
    {
        struct RecordingHost host;
        int const pushes[4] = { 1, 1, 16, chat_left_text };
        memset(&host, 0, sizeof(host));
        run_align(&host, CS2_OP_IF_SETTEXTALIGN, 0, pushes, 4, -1, -1);
        CHECK_INT(host.calls, 1, "IF_SETTEXTALIGN reaches the host once");
        CHECK_INT((int)host.kind, (int)CS2VM_HOST_REQUEST_CC_SETTEXTALIGN, "host request kind");
        CHECK_INT(host.align.component_id, chat_left_text, "chat_left:text component");
        CHECK_INT(host.align.x_align, 1, "horizontal centre");
        CHECK_INT(host.align.y_align, 1, "vertical centre");
        CHECK_INT(host.align.line_height, 16, "revision-239 line height");
    }

    /* The CC form uses the same host request but gets its target from the
     * active/dot selector instead of the stack. */
    {
        struct RecordingHost host;
        int const active = (217 << 16) | 6;
        int const dot = (231 << 16) | 6;
        int const pushes[3] = { 2, 0, 13 };
        memset(&host, 0, sizeof(host));
        run_align(&host, CS2_OP_CC_SETTEXTALIGN, 1, pushes, 3, active, dot);
        CHECK_INT(host.calls, 1, "CC_SETTEXTALIGN reaches the host once");
        CHECK_INT(host.align.component_id, dot, "operand 1 selects dot component");
        CHECK_INT(host.align.x_align, 2, "CC horizontal alignment");
        CHECK_INT(host.align.y_align, 0, "CC vertical alignment");
        CHECK_INT(host.align.line_height, 13, "CC line height");
    }

    if( g_fail )
    {
        printf("%d failure(s)\n", g_fail);
        return 1;
    }
    printf("All text alignment opcode tests passed.\n");
    return 0;
}
