/*
 * Unit test for RESUME_COUNTDIALOG (3104), driven through the real VM dispatch
 * against a recording host.
 *
 * The opcode is how a rev-230 interface answers a server script parked on
 * P_COUNTDIALOG. The one caller in cache.osrs239 is the bank PIN keypad:
 * `bankpin_button_op` (script_685) accumulates four clicked digits into a CS2
 * local and, on the fourth, calls `resume_countdialog(tostring($int2))`.
 *
 * Before it had a handler its generated stack row was { 0, 0, 0, 0, 0 } —
 * known = 0 — so it did not silently no-op, it reached StackMetaStub's assert
 * and took the client down at the moment the PIN was submitted. That is the
 * behaviour the "unimplemented opcode" assert exists to produce, and it is what
 * this test would go back to if the handler were removed.
 *
 * Three properties, and the first is the one a reading of the bytecode cannot
 * make obvious:
 *
 *   1. it pops ONE STRING, not an int. The wire packet carries an int, so the
 *      obvious mistake is to pop one here and convert on the way in — which
 *      underflows the int stack and desyncs everything after it.
 *   2. the text reaches the host verbatim; the conversion is the host's.
 *   3. nothing is pushed.
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

#define CHECK_STR(got, want, label)                                                                \
    do                                                                                             \
    {                                                                                              \
        char const* gv = (got) ? (got) : "(null)";                                                 \
        char const* wv = (want);                                                                   \
        if( strcmp(gv, wv) == 0 )                                                                  \
            printf("  ok: %s == \"%s\"\n", label, gv);                                             \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s got \"%s\" want \"%s\"\n", label, gv, wv);                          \
            g_fail++;                                                                              \
        }                                                                                          \
    } while( 0 )

struct RecordingHost
{
    int calls;
    enum CS2VM_HostRequestKind kind;
    char text[64];
};

static int
recording_host_exec(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request)
{
    struct RecordingHost* host = (struct RecordingHost*)thread->vm->user;

    host->calls++;
    host->kind = request->kind;
    host->text[0] = '\0';
    if( request->kind == CS2VM_HOST_REQUEST_RESUME_COUNTDIALOG &&
        request->u.RESUME_COUNTDIALOG.text )
    {
        snprintf(
            host->text,
            sizeof(host->text),
            "%s",
            request->u.RESUME_COUNTDIALOG.text);
    }
    return CS2VM_EXECNO_OK;
}

/*
 * Run [push_string sentinel, push_string answer, RESUME_COUNTDIALOG, RETURN]
 * and report what is left on each stack.
 *
 * The sentinel underneath is what distinguishes "popped exactly one string"
 * from "popped none" and from "popped two": only the first leaves the sentinel
 * on top at the end.
 */
static void
run_resume(
    struct RecordingHost* host,
    char const* answer,
    char const* sentinel,
    char** out_str_top,
    int* out_int_depth_changed)
{
    struct CS2VM2 vm;
    struct CS2VM2_Script script;
    struct CS2VM2_Thread* thread;
    int const op_count = 4;
    int scratch = 0;

    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, host, recording_host_exec);

    CS2VM2_ScriptInit(&script);
    script.script_id = 9995;
    script.op_count = op_count;
    script.opcodes = calloc((size_t)op_count, sizeof(uint16_t));
    script.int_operands = calloc((size_t)op_count, sizeof(int));
    script.string_operands = calloc((size_t)op_count, sizeof(char*));

    script.opcodes[0] = (uint16_t)CS2_OP_PUSH_CONSTANT_STRING;
    script.string_operands[0] = strdup(sentinel);
    script.opcodes[1] = (uint16_t)CS2_OP_PUSH_CONSTANT_STRING;
    script.string_operands[1] = strdup(answer);
    script.opcodes[2] = (uint16_t)CS2_OP_RESUME_COUNTDIALOG;
    script.opcodes[3] = (uint16_t)CS2_OP_RETURN;

    thread = CS2VM2_ThreadMain(&vm);
    CS2VM2_PushCallScript(thread, &script);
    CS2VM2_RunScript(thread);

    /* Whatever is on top of the string stack, copied out before the pool dies. */
    *out_str_top = NULL;
    {
        char* top = NULL;
        if( CS2VM2_PopStr(thread, &top) == CS2VM_EXECNO_OK && top )
            *out_str_top = strdup(top);
    }

    /* The int stack must be untouched: a successful pop means something was
     * pushed onto it, which nothing here should have done. */
    *out_int_depth_changed = (CS2VM2_PopInt(thread, &scratch) == CS2VM_EXECNO_OK) ? 1 : 0;

    CS2VM2_Free(&vm);
    for( int i = 0; i < op_count; i++ )
        free(script.string_operands[i]);
    free(script.opcodes);
    free(script.int_operands);
    free(script.string_operands);
}

int
main(void)
{
    struct RecordingHost host;
    char* left = NULL;
    int int_pushed = 0;

    memset(&host, 0, sizeof(host));
    run_resume(&host, "4622", "SENTINEL", &left, &int_pushed);

    CHECK_INT(host.calls, 1, "the host is called exactly once");
    CHECK_INT(host.kind == CS2VM_HOST_REQUEST_RESUME_COUNTDIALOG, 1,
              "and with the RESUME_COUNTDIALOG kind");
    CHECK_STR(host.text, "4622", "the answer reaches the host verbatim");
    CHECK_STR(left, "SENTINEL", "exactly one string was popped");
    CHECK_INT(int_pushed, 0, "nothing lands on the int stack");
    free(left);

    /*
     * The keypad's two non-digit rows come through the same channel: script 653
     * arms "Exit" with 12345 and "I don't know it." with 54321, both outside
     * 0000..9999 so a server can tell them from a PIN. Nothing here is special
     * about them — that is the point, and the test says so rather than leaving
     * a reader to wonder whether the opcode filters.
     */
    memset(&host, 0, sizeof(host));
    left = NULL;
    run_resume(&host, "12345", "SENTINEL", &left, &int_pushed);
    CHECK_STR(host.text, "12345", "the Exit sentinel is passed through unfiltered");
    CHECK_STR(left, "SENTINEL", "and still pops exactly one string");
    free(left);

    if( g_fail )
    {
        printf("resume_countdialog test: %d FAILED\n", g_fail);
        return 1;
    }
    printf("resume_countdialog test: all passed\n");
    return 0;
}
