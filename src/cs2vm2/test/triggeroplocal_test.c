/*
 * Unit test for IF_TRIGGEROPLOCAL (2929), driven through the real VM dispatch
 * against a recording host.
 *
 * Before this opcode existed it fell through to CS2VM2_Op_StackMetaStub and
 * aborted the client on every Quest XP "View-journal" click (script 9189):
 *
 *     _2929(1707091816, 56360977, -1, $questId, "i");
 *
 * Stack shape for the common "i" signature is a witnessed fact from
 * local_commands.py (`"_2929": (["INT","INT","INT","INT","STRING"], [], False)`).
 * Real rev-239 wire is IF_SCRIPT_TRIGGER; this client adapts to IF_BUTTON1
 * with sub = typed int when childIndex is -1.
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
    struct CS2VM_HostRequest_IF_TRIGGEROPLOCAL trig;
};

static int
recording_host_exec(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request)
{
    struct RecordingHost* host = (struct RecordingHost*)thread->vm->user;
    host->calls++;
    host->kind = request->kind;
    host->trig = request->u.IF_TRIGGEROPLOCAL;
    return CS2VM_EXECNO_OK;
}

/* Push crc, component, childIndex, typedInt, signature "i", then 2929. */
static void
run_op(
    struct RecordingHost* host,
    int crc,
    int component_id,
    int child_index,
    int typed_int)
{
    struct CS2VM2 vm;
    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, host, recording_host_exec);

    struct CS2VM2_Script script;
    CS2VM2_ScriptInit(&script);
    int const op_count = 6;
    script.script_id = 9189;
    script.op_count = op_count;
    script.opcodes = calloc((size_t)op_count, sizeof(uint16_t));
    script.int_operands = calloc((size_t)op_count, sizeof(int));
    script.string_operands = calloc((size_t)op_count, sizeof(char*));

    script.opcodes[0] = (uint16_t)CS2_OP_PUSH_CONSTANT_INT;
    script.int_operands[0] = crc;
    script.opcodes[1] = (uint16_t)CS2_OP_PUSH_CONSTANT_INT;
    script.int_operands[1] = component_id;
    script.opcodes[2] = (uint16_t)CS2_OP_PUSH_CONSTANT_INT;
    script.int_operands[2] = child_index;
    script.opcodes[3] = (uint16_t)CS2_OP_PUSH_CONSTANT_INT;
    script.int_operands[3] = typed_int;
    script.opcodes[4] = (uint16_t)CS2_OP_PUSH_CONSTANT_STRING;
    script.string_operands[4] = strdup("i");
    script.opcodes[5] = (uint16_t)CS2_OP_IF_TRIGGEROPLOCAL;

    struct CS2VM2_Thread* thread = CS2VM2_ThreadMain(&vm);
    CS2VM2_PushCallScript(thread, &script);
    CS2VM2_RunScript(thread);

    CS2VM2_Free(&vm);
    free(script.string_operands[4]);
    free(script.opcodes);
    free(script.int_operands);
    free(script.string_operands);
}

int
main(void)
{
    printf("TEST: IF_TRIGGEROPLOCAL\n");

    int const trigger = (860 << 16) | 17; /* skill_guide_v2:quest_journal_button_trigger */
    int const crc = 1707091816;
    int const quest_id = 42;

    /* script9189: childIndex -1 → sub is the typed quest id. */
    {
        struct RecordingHost host;
        memset(&host, 0, sizeof(host));
        run_op(&host, crc, trigger, -1, quest_id);
        CHECK_INT(host.calls, 1, "reaches the host exactly once (no StackMetaStub abort)");
        CHECK_INT(
            (int)host.kind, (int)CS2VM_HOST_REQUEST_IF_TRIGGEROPLOCAL, "request kind");
        CHECK_INT(host.trig.component_id, trigger, "component is the trigger");
        CHECK_INT(
            host.trig.sub,
            quest_id,
            "sub is the typed quest id when childIndex is -1");
    }

    /* childIndex set → sub is childIndex, typed int ignored for the packet. */
    {
        struct RecordingHost host;
        memset(&host, 0, sizeof(host));
        run_op(&host, crc, trigger, 7, quest_id);
        CHECK_INT(host.trig.component_id, trigger, "component still the trigger");
        CHECK_INT(host.trig.sub, 7, "sub is childIndex when it is not -1");
    }

    if( g_fail )
    {
        printf("%d failure(s)\n", g_fail);
        return 1;
    }
    printf("All IF_TRIGGEROPLOCAL opcode tests passed.\n");
    return 0;
}
