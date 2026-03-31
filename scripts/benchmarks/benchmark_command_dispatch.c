/*
 * Microbenchmark: packed command buffer (tag + union + switch) vs
 * function-pointer dispatch. Timings depend on CPU, compiler, and -O level;
 * this compares relative decode/branch cost vs indirect calls for your build.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define N 4096
#define ITERATIONS 50000

static inline long long
now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

enum CmdKind
{
    CMD_1,
    CMD_2
};

struct Cmd
{
    enum CmdKind kind;
    union
    {
        int _one;
        long _two;
    } u;
};

struct IfaceOp;

#if defined(_MSC_VER)
#define BENCH_NOINLINE __declspec(noinline)
#else
#define BENCH_NOINLINE __attribute__((noinline))
#endif

typedef void
iface_fn(
    struct IfaceOp* op,
    volatile long long* sum);

struct IfaceOp
{
    iface_fn* fn;
    union
    {
        int one;
        long two;
    } u;
};

static void
cmd1_impl(
    struct IfaceOp* op,
    volatile long long* sum)
{
    *sum += op->u.one;
}

static void
cmd2_impl(
    struct IfaceOp* op,
    volatile long long* sum)
{
    *sum += (int)(op->u.two & 0x7fffffffL);
}

static void
run_cmd_buffer(
    struct Cmd* cmds,
    int n,
    volatile long long* sum)
{
    for( int i = 0; i < n; i++ )
    {
        switch( cmds[i].kind )
        {
        case CMD_1:
            *sum += cmds[i].u._one;
            break;
        case CMD_2:
            *sum += (int)(cmds[i].u._two & 0x7fffffffL);
            break;
        }
    }
}

static void
run_cmd_iface(
    struct IfaceOp* ops,
    int n,
    volatile long long* sum)
{
    for( int i = 0; i < n; i++ )
        ops[i].fn(&ops[i], sum);
}

static void
fill_arrays(
    struct Cmd* cmds,
    struct IfaceOp* ops,
    int n)
{
    for( int i = 0; i < n; i++ )
    {
        if( (i & 1) == 0 )
        {
            int v = (int)(i * 1103515245 + 12345);
            cmds[i].kind = CMD_1;
            cmds[i].u._one = v;
            ops[i].fn = cmd1_impl;
            ops[i].u.one = v;
        }
        else
        {
            long v = (long)i * 0x9e3779b97f4a7c15L + 7L;
            cmds[i].kind = CMD_2;
            cmds[i].u._two = v;
            ops[i].fn = cmd2_impl;
            ops[i].u.two = v;
        }
    }
}

/*
cc -std=c11 -O3 -o benchmark_command_dispatch benchmark_command_dispatch.c
./benchmark_command_dispatch
*/
int
main(void)
{
    struct Cmd* cmds = malloc((size_t)N * sizeof(struct Cmd));
    struct IfaceOp* ops = malloc((size_t)N * sizeof(struct IfaceOp));
    if( !cmds || !ops )
    {
        fprintf(stderr, "allocation failed\n");
        free(cmds);
        free(ops);
        return 1;
    }

    fill_arrays(cmds, ops, N);

    volatile long long checksum = 0;
    long long buf_total_ns = 0;
    long long iface_total_ns = 0;

    for( int iter = 0; iter < ITERATIONS; iter++ )
    {
        long long t0 = now_ns();
        run_cmd_buffer(cmds, N, &checksum);
        long long t1 = now_ns();
        buf_total_ns += (t1 - t0);

        long long t2 = now_ns();
        run_cmd_iface(ops, N, &checksum);
        long long t3 = now_ns();
        iface_total_ns += (t3 - t2);
    }

    printf(
        "buffer:  avg %g ns per pass (%d cmds x %d iters), total wall %lld ns\n",
        (double)buf_total_ns / (double)ITERATIONS,
        N,
        ITERATIONS,
        buf_total_ns);
    printf(
        "iface:   avg %g ns per pass (%d cmds x %d iters), total wall %lld ns\n",
        (double)iface_total_ns / (double)ITERATIONS,
        N,
        ITERATIONS,
        iface_total_ns);
    printf("combined checksum (both paths each iter): %lld\n", checksum);

    volatile long long c_buf = 0;
    volatile long long c_iface = 0;
    run_cmd_buffer(cmds, N, &c_buf);
    run_cmd_iface(ops, N, &c_iface);
    printf("verify buffer-only checksum:  %lld\n", c_buf);
    printf("verify iface-only checksum:   %lld\n", c_iface);
    if( c_buf != c_iface )
    {
        fprintf(stderr, "mismatch: buffer and iface should agree\n");
        free(cmds);
        free(ops);
        return 1;
    }

    free(cmds);
    free(ops);
    return 0;
}
