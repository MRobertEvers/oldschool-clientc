/*
 * The option opcodes, driven through the real VM dispatch against a recording
 * host.
 *
 * Two things here have failed silently before and cannot be seen from a
 * screenshot:
 *
 *   - GETREMOVEROOFS / SETREMOVEROOFS (3111/3112) had a name and a stack
 *     signature but no case in the dispatch, so the Display panel's Roofs
 *     toggle popped its argument through the stack stub and changed nothing.
 *     There is no error for that: the client keeps running with the setting
 *     it already had.
 *   - CLIENTOPTION_SET/GET (3209/3210) carry an id and must reach the host as
 *     that id, because the host is where the reference's "device table first,
 *     game table second" resolution lives (RS_CS2Host_ClientOptionKind).
 *
 * The stack shapes are the generated meta's, which is what StackMetaStub
 * asserts against — a wrong arity here would abort rather than pass.
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
    struct CS2VM_HostRequest_ClientOption option;
    /** What a getter should push back, so the script can be seen to receive it. */
    int get_result;
};

static int
recording_host_exec(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request)
{
    struct RecordingHost* host = (struct RecordingHost*)thread->vm->user;

    host->calls++;
    host->kind = request->kind;
    host->option = request->u.client_option;
    switch( request->kind )
    {
    case CS2VM_HOST_REQUEST_GETREMOVEROOFS:
    case CS2VM_HOST_REQUEST_CLIENTOPTION_GET:
        return CS2VM2_PushInt(thread, host->get_result);
    default:
        break;
    }
    return CS2VM_EXECNO_OK;
}

/* `push arg0 [, push arg1] ; <opcode> ; return` — the shape a settings script
 * compiles to. `argc` is how many ints to push before the op. */
static void
run_op(
    struct RecordingHost* host,
    int opcode,
    int argc,
    int arg0,
    int arg1)
{
    struct CS2VM2 vm;
    struct CS2VM2_Script script;
    struct CS2VM2_Thread* thread;
    int op_count = argc + 2;
    int at = 0;

    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, host, recording_host_exec);

    CS2VM2_ScriptInit(&script);
    script.script_id = 3998;
    script.op_count = op_count;
    script.opcodes = calloc((size_t)op_count, sizeof(uint16_t));
    script.int_operands = calloc((size_t)op_count, sizeof(int));
    script.string_operands = calloc((size_t)op_count, sizeof(char*));

    if( argc > 0 )
    {
        script.opcodes[at] = (uint16_t)CS2_OP_PUSH_CONSTANT_INT;
        script.int_operands[at++] = arg0;
    }
    if( argc > 1 )
    {
        script.opcodes[at] = (uint16_t)CS2_OP_PUSH_CONSTANT_INT;
        script.int_operands[at++] = arg1;
    }
    script.opcodes[at++] = (uint16_t)opcode;
    script.opcodes[at] = (uint16_t)CS2_OP_RETURN;

    thread = CS2VM2_ThreadMain(&vm);
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
    printf("TEST: client/game/device option opcodes\n");

    /* setremoveroofs(true): one int in, nothing out. */
    {
        struct RecordingHost host;

        memset(&host, 0, sizeof(host));
        run_op(&host, CS2_OP_SETREMOVEROOFS, 1, 1, 0);
        CHECK_INT(host.calls, 1, "setremoveroofs reaches the host");
        CHECK_INT((int)host.kind, (int)CS2VM_HOST_REQUEST_SETREMOVEROOFS, "request kind");
        CHECK_INT(host.option.opcode, CS2_OP_SETREMOVEROOFS, "opcode carried through");
        CHECK_INT(host.option.value, 1, "the pushed flag is the value");
    }

    /* getremoveroofs(): nothing in, the host's answer out. */
    {
        struct RecordingHost host;

        memset(&host, 0, sizeof(host));
        host.get_result = 1;
        run_op(&host, CS2_OP_GETREMOVEROOFS, 0, 0, 0);
        CHECK_INT(host.calls, 1, "getremoveroofs reaches the host");
        CHECK_INT((int)host.kind, (int)CS2VM_HOST_REQUEST_GETREMOVEROOFS, "getter request kind");
        CHECK_INT(host.option.opcode, CS2_OP_GETREMOVEROOFS, "opcode carried through");
    }

    /* clientoption_set(id, value): the id must survive to the host, which is
     * the only place that knows which table it names. Pop order is value then
     * id, so the script pushes the id first. */
    {
        struct RecordingHost host;

        memset(&host, 0, sizeof(host));
        run_op(&host, CS2_OP_CLIENTOPTION_SET, 2, 7, 42);
        CHECK_INT(host.calls, 1, "clientoption_set reaches the host");
        CHECK_INT((int)host.kind, (int)CS2VM_HOST_REQUEST_CLIENTOPTION_SET, "set request kind");
        CHECK_INT(host.option.option_id, 7, "option id (music volume)");
        CHECK_INT(host.option.value, 42, "option value");
    }

    /* The getter has the same payload shape as the setter, but must remain a
     * distinct host request and return the host-provided value. */
    {
        struct RecordingHost host;

        memset(&host, 0, sizeof(host));
        host.get_result = 73;
        run_op(&host, CS2_OP_CLIENTOPTION_GET, 1, 7, 0);
        CHECK_INT(host.calls, 1, "clientoption_get reaches the host");
        CHECK_INT((int)host.kind, (int)CS2VM_HOST_REQUEST_CLIENTOPTION_GET, "get request kind");
        CHECK_INT(host.option.option_id, 7, "getter option id");
    }

    if( g_fail )
    {
        printf("%d failure(s)\n", g_fail);
        return 1;
    }
    printf("All option opcode tests passed.\n");
    return 0;
}
