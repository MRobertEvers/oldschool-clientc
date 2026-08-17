/*
 * Micro-benchmark for the VM's per-call and per-opcode cost.
 *
 * Not a pass/fail test — it prints numbers. It exists because the client's own
 * profile cannot answer these: a login screen runs a few hundred opcodes across
 * a frame, so the wall-clock difference between two VM builds is buried in the
 * noise of everything else the frame does. Here the VM is the only thing
 * running, the work is fixed, and the two costs the optimizer plan targets are
 * separated:
 *
 *   gosub   — a call per iteration and almost nothing else, which is what
 *             §11.1 (sized frames) and §12.3 (callee cache) move.
 *   arith   — a straight line of arithmetic with no call and no host op, which
 *             is what §12.5 (skip the yield checkpoint for opcodes that cannot
 *             yield) moves.
 *   deep    — recursion to the frame limit, where the old fixed frame paid
 *             12,352 bytes of memset per level.
 */

#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_script.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_OPS 256

struct TestScript
{
    struct CS2VM2_Script script;
    uint16_t opcodes[MAX_OPS];
    int int_operands[MAX_OPS];
    char* string_operands[MAX_OPS];
    int n;
};

static void
script_begin(
    struct TestScript* ts,
    int id,
    int int_args,
    int int_locals)
{
    memset(ts, 0, sizeof(*ts));
    CS2VM2_ScriptInit(&ts->script);
    ts->script.script_id = id;
    ts->script.int_argument_count = int_args;
    ts->script.local_int_count = int_locals;
    ts->script.opcodes = ts->opcodes;
    ts->script.int_operands = ts->int_operands;
    ts->script.string_operands = ts->string_operands;
}

static void
emit(
    struct TestScript* ts,
    int op,
    int operand)
{
    ts->opcodes[ts->n] = (uint16_t)op;
    ts->int_operands[ts->n] = operand;
    ts->n++;
    ts->script.op_count = ts->n;
}

struct BenchHost
{
    struct TestScript* scripts[4];
    int script_count;
};

static int
bench_host_exec(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request)
{
    struct BenchHost* host = (struct BenchHost*)thread->vm->user;

    if( request->kind == CS2VM_HOST_REQUEST_PUSHSCRIPT )
    {
        for( int i = 0; i < host->script_count; i++ )
        {
            if( host->scripts[i]->script.script_id == request->u.push_script.script_id )
                return CS2VM2_PushCallScript(thread, &host->scripts[i]->script);
        }
        return CS2VM_EXECNO_ERROR;
    }
    return CS2VM_EXECNO_OK;
}

static double
now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

static void
report(
    char const* label,
    double ms,
    long units,
    char const* unit_name)
{
    printf(
        "  %-28s %8.2f ms   %10.1f ns/%s\n", label, ms, ms * 1e6 / (double)units, unit_name);
}

int
main(void)
{
    printf("cs2 vm micro-benchmark\n");

    /* --- gosub: one call per iteration ------------------------------------ */
    {
        struct TestScript callee, top;
        struct BenchHost host;
        struct CS2VM2 vm;

        /* 8 int locals: about the median-to-p90 script in the cache. */
        script_begin(&callee, 900, 1, 8);
        emit(&callee, CS2_OP_PUSH_INT_LOCAL, 0);
        emit(&callee, CS2_OP_RETURN, 0);

        script_begin(&top, 800, 0, 2);
        emit(&top, CS2_OP_PUSH_CONSTANT_INT, 1);
        emit(&top, CS2_OP_GOSUB_WITH_PARAMS, 900);
        emit(&top, CS2_OP_POP_INT_DISCARD, 0);
        emit(&top, CS2_OP_RETURN, 0);

        memset(&host, 0, sizeof(host));
        host.scripts[host.script_count++] = &callee;

        CS2VM2_Init(&vm);
        CS2VM2_BindHost(&vm, &host, bench_host_exec);
        struct CS2VM2_Thread* t = CS2VM2_ThreadMain(&vm);

        long const iters = 400000;
        double t0 = now_ms();
        for( long i = 0; i < iters; i++ )
        {
            CS2VM2_ThreadStart(t, &top.script);
            CS2VM2_RunScript(t);
        }
        double ms = now_ms() - t0;
        report("gosub (1 call/iter)", ms, iters, "call");
        CS2VM2_Free(&vm);
    }

    /* --- arithmetic: no call, no host op ---------------------------------- */
    {
        struct BenchHost host;
        struct CS2VM2 vm;
        struct TestScript top;

        script_begin(&top, 801, 0, 2);
        emit(&top, CS2_OP_PUSH_CONSTANT_INT, 7);
        emit(&top, CS2_OP_POP_INT_LOCAL, 0);
        /* 40 ops of read-add-store, the shape a layout script's arithmetic has */
        for( int i = 0; i < 10; i++ )
        {
            emit(&top, CS2_OP_PUSH_INT_LOCAL, 0);
            emit(&top, CS2_OP_PUSH_CONSTANT_INT, 3);
            emit(&top, CS2_OP_ADD, 0);
            emit(&top, CS2_OP_POP_INT_LOCAL, 0);
        }
        emit(&top, CS2_OP_RETURN, 0);
        int const ops_per_run = top.n;

        memset(&host, 0, sizeof(host));
        CS2VM2_Init(&vm);
        CS2VM2_BindHost(&vm, &host, bench_host_exec);
        struct CS2VM2_Thread* t = CS2VM2_ThreadMain(&vm);

        long const iters = 400000;
        double t0 = now_ms();
        for( long i = 0; i < iters; i++ )
        {
            CS2VM2_ThreadStart(t, &top.script);
            CS2VM2_RunScript(t);
        }
        double ms = now_ms() - t0;
        report("arithmetic", ms, iters * ops_per_run, "op");
        CS2VM2_Free(&vm);
    }

    /* --- recursion to the frame limit ------------------------------------- */
    {
        struct TestScript rec, top;
        struct BenchHost host;
        struct CS2VM2 vm;

        script_begin(&rec, 902, 1, 8);
        emit(&rec, CS2_OP_PUSH_INT_LOCAL, 0);
        emit(&rec, CS2_OP_PUSH_CONSTANT_INT, 0);
        emit(&rec, CS2_OP_BRANCH_EQUALS, 6);
        emit(&rec, CS2_OP_PUSH_INT_LOCAL, 0);
        emit(&rec, CS2_OP_PUSH_CONSTANT_INT, 1);
        emit(&rec, CS2_OP_SUB, 0);
        emit(&rec, CS2_OP_GOSUB_WITH_PARAMS, 902);
        emit(&rec, CS2_OP_RETURN, 0);
        emit(&rec, CS2_OP_RETURN, 0);

        script_begin(&top, 802, 0, 1);
        emit(&top, CS2_OP_PUSH_CONSTANT_INT, 100);
        emit(&top, CS2_OP_GOSUB_WITH_PARAMS, 902);
        emit(&top, CS2_OP_RETURN, 0);

        memset(&host, 0, sizeof(host));
        host.scripts[host.script_count++] = &rec;

        CS2VM2_Init(&vm);
        CS2VM2_BindHost(&vm, &host, bench_host_exec);
        struct CS2VM2_Thread* t = CS2VM2_ThreadMain(&vm);

        long const iters = 20000;
        double t0 = now_ms();
        for( long i = 0; i < iters; i++ )
        {
            CS2VM2_ThreadStart(t, &top.script);
            CS2VM2_RunScript(t);
        }
        double ms = now_ms() - t0;
        report("recursion to depth 100", ms, iters * 101, "call");
        CS2VM2_Free(&vm);
    }

    /* --- VM acquire/release ------------------------------------------------ */
    {
        long const iters = 200000;
        double t0 = now_ms();
        for( long i = 0; i < iters; i++ )
        {
            struct CS2VM2* vm = CS2VM2_Acquire();
            CS2VM2_Release(vm);
        }
        double ms = now_ms() - t0;
        report("VM acquire+release", ms, iters, "vm");
        CS2VM2_PoolDrain();
    }

    return 0;
}
