/*
 * What a yield puts back (cs2vm2.h, struct CS2VM2_Took).
 *
 * Nothing is saved before an instruction runs. An implementation that pops and
 * is then told "not ready" puts back what it took, so the instruction runs
 * again on resume from the stacks it started with:
 *
 *   1. INV_GETOBJ pops (inv, slot) in the VM and asks. The host says "not
 *      ready" once. Both ints must be back on the stack, the pc on the
 *      instruction, and the second run must answer as if the first never
 *      happened.
 *   2. DB_GETFIELD pops nothing in the VM: the host pops its three arguments
 *      for itself before it can know the row is resident. It counts them and
 *      puts them back (CS2VM2_YieldRestore) before it yields.
 *   1b. The same through the host's second door, a reader bound with
 *      CS2VM2_BindHostReads (CS2VM2_AskRead): the same put-back, and the
 *      request never reaches host_exec.
 *   3. A host that pops and yields WITHOUT putting back stops a debug build
 *      (the run aborts). A release build does not check, so that arm is only
 *      compiled with asserts on.
 */

#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_host.h"
#include "cs2vm2/cs2vm2_script.h"

#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif

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

enum HostMode
{
    HOST_YIELDS_ONCE,           /* not ready the first time, touching nothing */
    HOST_POPS_AND_RESTORES,     /* pops its own arguments, puts them back, yields once */
    HOST_POPS_AND_DOES_NOT,     /* pops one argument and yields: the defect */
};

struct Host
{
    enum HostMode mode;
    int calls;
    int reads;
    int seen[3];
};

static int
host_exec(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request)
{
    struct Host* host = (struct Host*)CS2VM_USER(thread);

    host->calls++;
    if( request->kind == CS2VM_HOST_REQUEST_INV_GETOBJ )
    {
        host->seen[0] = request->u.INV_GETOBJ.inv_id;
        host->seen[1] = request->u.INV_GETOBJ.slot;
        if( host->calls == 1 )
            return CS2VM_EXECNO_YIELD;
        return CS2VM2_PushInt(thread, 4151);
    }
    if( request->kind == CS2VM_HOST_REQUEST_DB_GETFIELD )
    {
        if( host->mode == HOST_POPS_AND_DOES_NOT )
        {
            int discard;
            if( CS2VM2_PopInt(thread, &discard) != CS2VM_EXECNO_OK )
                return CS2VM_EXECNO_ERROR;
            return CS2VM_EXECNO_YIELD;
        }
        struct CS2VM2_Took took = CS2VM2_TOOK_NOTHING;
        if( CS2VM2_TakeInt(thread, &took, &host->seen[2]) != CS2VM_EXECNO_OK ||
            CS2VM2_TakeInt(thread, &took, &host->seen[1]) != CS2VM_EXECNO_OK ||
            CS2VM2_TakeInt(thread, &took, &host->seen[0]) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( host->calls == 1 )
        {
            CS2VM2_YieldRestore(thread, took);
            return CS2VM_EXECNO_YIELD;
        }
        return CS2VM2_PushInt(thread, 55);
    }
    return CS2VM_EXECNO_ERROR;
}

/* The reader door (CS2VM2_BindHostReads): counted apart, answered alike. */
static int
host_read(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request)
{
    struct Host* host = (struct Host*)CS2VM_USER(thread);
    host->reads++;
    return host_exec(thread, request);
}

/* [push sentinel, push a.., OP, RETURN] */
static void
build(
    struct CS2VM2_Script* script,
    uint16_t* opcodes,
    int* operands,
    int opcode,
    int const* args,
    int arg_count)
{
    int n = 0;

    CS2VM2_ScriptInit(script);
    script->script_id = 777000 + opcode;
    operands[n] = -99; /* the sentinel under the arguments */
    opcodes[n++] = CS2_OP_PUSH_CONSTANT_INT;
    for( int i = 0; i < arg_count; i++ )
    {
        operands[n] = args[i];
        opcodes[n++] = CS2_OP_PUSH_CONSTANT_INT;
    }
    operands[n] = 0;
    opcodes[n++] = (uint16_t)opcode;
    operands[n] = 0;
    opcodes[n++] = CS2_OP_RETURN;
    script->op_count = n;
    script->opcodes = opcodes;
    script->int_operands = operands;
    CS2VM2_ScriptPrepare(script); /* what a loader does */
}

static void
run_yield_then_answer(
    enum HostMode mode,
    int bind_reader,
    int opcode,
    int const* args,
    int arg_count,
    int answer,
    char const* label)
{
    struct CS2VM2 vm;
    struct CS2VM2_Script script;
    struct CS2VM2_ThreadError error;
    struct Host host;
    uint16_t opcodes[8];
    int operands[8];

    printf("TEST: %s\n", label);
    memset(&host, 0, sizeof(host));
    memset(&error, 0, sizeof(error));
    host.mode = mode;
    build(&script, opcodes, operands, opcode, args, arg_count);
    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, &host, host_exec);
    if( bind_reader )
        CS2VM2_BindHostReads(&vm, host_read);
    struct CS2VM2_Thread* thread = CS2VM2_ThreadMain(&vm);
    CHECK_INT(CS2VM2_ThreadStart(thread, &script), CS2VM_EXECNO_OK, "start");

    CHECK_INT((int)CS2VM2_ThreadRun(thread, &error), (int)CS2VM2_THREAD_YIELDED, "first run yields");
    CHECK_INT(host.calls, 1, "the host was asked once");
    CHECK_INT(thread->ints_stack_top, 1 + arg_count, "every argument is back on the stack");
    CHECK_INT(thread->ints_stack[0], -99, "the sentinel under them is untouched");
    for( int i = 0; i < arg_count; i++ )
        CHECK_INT(thread->ints_stack[1 + i], args[i], "an argument is where it was");
    CHECK_INT(CS2VM_FRAME(thread)->pc, 1 + arg_count, "the pc is back on the instruction");

    CHECK_INT((int)CS2VM2_ThreadResume(thread, &error), (int)CS2VM2_THREAD_DONE, "resume finishes");
    CHECK_INT(host.calls, 2, "the instruction ran again");
    for( int i = 0; i < arg_count; i++ )
        CHECK_INT(host.seen[i], args[i], "the second run saw the same argument");
    CHECK_INT(thread->ints_stack_top, 2, "the arguments are gone and the answer is pushed");
    CHECK_INT(thread->ints_stack[1], answer, "the answer");
    CHECK_INT(thread->ints_stack[0], -99, "the sentinel is still under it");
    CHECK_INT(host.reads, bind_reader ? 2 : 0, "the reader door was asked when bound, never otherwise");

    CS2VM2_Free(&vm);
    /* The opcode and operand arrays are this function's; the script owns
     * only what CS2VM2_ScriptPrepare made. */
    script.opcodes = NULL;
    script.int_operands = NULL;
    CS2VM2_ScriptFree(&script);
}

#if !defined(NDEBUG) && !defined(_WIN32)
/* The defect, in a child process: a debug build must not survive it. */
static void
test_a_yield_that_did_not_restore_stops_a_debug_build(void)
{
    int const args[3] = { 11, 22, 33 };
    pid_t child;
    int status = 0;

    printf("TEST: a yield that did not put back what it took stops a debug build\n");
    fflush(stdout);
    child = fork();
    if( child == 0 )
    {
        struct CS2VM2 vm;
        struct CS2VM2_Script script;
        struct CS2VM2_ThreadError error;
        struct Host host;
        uint16_t opcodes[8];
        int operands[8];

        /* The abort's own message is expected; keep it out of the test log. */
        (void)freopen("/dev/null", "w", stderr);
        memset(&host, 0, sizeof(host));
        memset(&error, 0, sizeof(error));
        host.mode = HOST_POPS_AND_DOES_NOT;
        build(&script, opcodes, operands, CS2_OP_DB_GETFIELD, args, 3);
        CS2VM2_Init(&vm);
        CS2VM2_BindHost(&vm, &host, host_exec);
        struct CS2VM2_Thread* thread = CS2VM2_ThreadMain(&vm);
        CS2VM2_ThreadStart(thread, &script);
        (void)CS2VM2_ThreadRun(thread, &error);
        _exit(0); /* reached only if nothing stopped it */
    }
    if( child < 0 || waitpid(child, &status, 0) != child )
    {
        printf("  FAIL: could not run the child\n");
        g_fail++;
        return;
    }
    CHECK_INT(WIFSIGNALED(status) ? WTERMSIG(status) : 0, SIGABRT, "the run aborted");
}
#endif

int
main(void)
{
    int const inv_args[2] = { 95, 3 };
    int const db_args[3] = { 4001, 7, 0 };

    run_yield_then_answer(
        HOST_YIELDS_ONCE,
        0,
        CS2_OP_INV_GETOBJ,
        inv_args,
        2,
        4151,
        "an instruction that popped two ints and was told not-ready puts them back");
    run_yield_then_answer(
        HOST_YIELDS_ONCE,
        1,
        CS2_OP_INV_GETOBJ,
        inv_args,
        2,
        4151,
        "the same through a bound reader (CS2VM2_AskRead) puts them back too");
    run_yield_then_answer(
        HOST_POPS_AND_RESTORES,
        0,
        CS2_OP_DB_GETFIELD,
        db_args,
        3,
        55,
        "a host that pops its own arguments puts them back before it yields");
#if !defined(NDEBUG) && !defined(_WIN32)
    test_a_yield_that_did_not_restore_stops_a_debug_build();
#else
    printf("SKIP: the debug-build stop (this build has no asserts, or no fork)\n");
#endif

    if( g_fail )
    {
        printf("yield restore test: %d FAILED\n", g_fail);
        return 1;
    }
    printf("yield restore test: all passed\n");
    return 0;
}
