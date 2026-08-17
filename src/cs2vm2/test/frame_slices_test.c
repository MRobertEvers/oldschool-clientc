/*
 * Unit test for sized call frames (docs/CS2_OPTIMIZER_PLAN.md §11.1).
 *
 * A frame no longer carries int_locals[1024] + str_locals[1024] of its own; it
 * carries a base and a count into two per-thread stacks, and CS2VM2_PushCallScript
 * bumps those stacks by what the script's trailer declares. Every property a
 * caller depends on is now arithmetic that can be got wrong silently:
 *
 *   1. a callee's locals must not alias its caller's. With a fixed frame this
 *      was free (separate arrays); with slices it is `base = top` at push and
 *      `top = base` at pop, and getting either wrong makes a proc scribble on
 *      the local of whoever called it. That is the failure this file is mostly
 *      about, and it is invisible in any test whose callee writes the same
 *      values its caller holds — so the checks below deliberately have the
 *      callee store a value its caller would never have.
 *   2. recursion has to give each level its own slice, which is the same
 *      property but at a depth where a stack that fails to grow, or a grow
 *      that invalidates the frames' view of it, shows up. Depth 100 is past
 *      the 256-cell initial stack, so this also exercises the realloc — and a
 *      realloc is exactly why the frame holds an index and not a pointer.
 *   3. argument passing is by bank ordinal, not by signature position: the
 *      k-th int argument is int local k and the k-th string argument is string
 *      local k, in two independent banks.
 *   4. an array handle is a raw pointer parked in a string local, so it rides
 *      the string slice through a gosub. If the callee resolved the handle
 *      through the wrong slice base it would find no array and the op would
 *      silently read 0 — the exact way the spellbook sort failed once before.
 *   5. a yield inside a nested call replays the op from a checkpoint. The
 *      checkpoint now has to carry the locals tops as well, or a yielding call
 *      leaks slice on every retry.
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

/* --- script building ------------------------------------------------------ */

#define MAX_SCRIPTS 8
#define MAX_OPS 64

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
    int str_args,
    int int_locals,
    int str_locals)
{
    memset(ts, 0, sizeof(*ts));
    CS2VM2_ScriptInit(&ts->script);
    ts->script.script_id = id;
    ts->script.int_argument_count = int_args;
    ts->script.string_argument_count = str_args;
    ts->script.local_int_count = int_locals;
    ts->script.local_string_count = str_locals;
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

static void
emit_str(
    struct TestScript* ts,
    int op,
    char* text)
{
    ts->opcodes[ts->n] = (uint16_t)op;
    ts->string_operands[ts->n] = text;
    ts->n++;
    ts->script.op_count = ts->n;
}

/* --- host ----------------------------------------------------------------- */

/*
 * Resolves GOSUB_WITH_PARAMS against a registry, and can be told to yield once
 * on the next PUSH_VAR so the checkpoint path gets exercised from inside a
 * nested frame.
 */
struct TestHost
{
    struct TestScript* scripts[MAX_SCRIPTS];
    int script_count;
    int varp_yields_left;
    int varp_value;
    int varp_reads;
};

static int
test_host_exec(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request)
{
    struct TestHost* host = (struct TestHost*)thread->vm->user;

    if( request->kind == CS2VM_HOST_REQUEST_PUSHSCRIPT )
    {
        for( int i = 0; i < host->script_count; i++ )
        {
            if( host->scripts[i]->script.script_id != request->u.push_script.script_id )
                continue;
            return CS2VM2_PushCallScript(thread, &host->scripts[i]->script);
        }
        return CS2VM_EXECNO_ERROR;
    }

    if( request->kind == CS2VM_HOST_REQUEST_VARS_READ_VARP_AKA_PUSH_VAR )
    {
        if( host->varp_yields_left > 0 )
        {
            host->varp_yields_left--;
            return CS2VM_EXECNO_YIELD;
        }
        host->varp_reads++;
        return CS2VM2_PushInt(thread, host->varp_value);
    }

    return CS2VM_EXECNO_OK;
}

/* --- 1/3: nested gosub, arg banks, and slice isolation -------------------- */

/*
 * leaf(int a0, int a1, string s0):
 *     push s0                       -> string result
 *     push a0; push a1; SUB         -> a0 - a1
 *     $a1 = 777                     (writes leaf's OWN int local 1)
 *
 * The last line is the trap. leaf's int local 1 and mid's int local 1 are the
 * same index in two different slices; if the base is dropped they are the same
 * cell, and mid's read below comes back 777.
 */
static void
build_leaf(struct TestScript* ts)
{
    script_begin(ts, 200, /*int_args*/ 2, /*str_args*/ 1, /*int_locals*/ 4, /*str_locals*/ 2);
    emit(ts, CS2_OP_PUSH_STRING_LOCAL, 0);
    emit(ts, CS2_OP_PUSH_INT_LOCAL, 0);
    emit(ts, CS2_OP_PUSH_INT_LOCAL, 1);
    emit(ts, CS2_OP_SUB, 0);
    emit(ts, CS2_OP_PUSH_CONSTANT_INT, 777);
    emit(ts, CS2_OP_POP_INT_LOCAL, 1);
    emit(ts, CS2_OP_RETURN, 0);
}

/*
 * mid(int n):
 *     $i1 = 10; $s0 = "abc";
 *     leaf($n, $i1, $s0);
 *     push $i1                      -> must still be 10, not leaf's 777
 */
static void
build_mid(struct TestScript* ts)
{
    script_begin(ts, 201, /*int_args*/ 1, /*str_args*/ 0, /*int_locals*/ 3, /*str_locals*/ 1);
    emit(ts, CS2_OP_PUSH_CONSTANT_INT, 10);
    emit(ts, CS2_OP_POP_INT_LOCAL, 1);
    emit_str(ts, CS2_OP_PUSH_CONSTANT_STRING, "abc");
    emit(ts, CS2_OP_POP_STRING_LOCAL, 0);
    emit(ts, CS2_OP_PUSH_INT_LOCAL, 0);
    emit(ts, CS2_OP_PUSH_INT_LOCAL, 1);
    emit(ts, CS2_OP_PUSH_STRING_LOCAL, 0);
    emit(ts, CS2_OP_GOSUB_WITH_PARAMS, 200);
    emit(ts, CS2_OP_PUSH_INT_LOCAL, 1);
    emit(ts, CS2_OP_RETURN, 0);
}

static void
test_nested_gosub(void)
{
    struct TestScript leaf, mid, top;
    struct TestHost host;
    struct CS2VM2 vm;

    printf("nested gosub / arg banks / slice isolation:\n");

    build_leaf(&leaf);
    build_mid(&mid);
    script_begin(&top, 100, 0, 0, 2, 1);
    emit(&top, CS2_OP_PUSH_CONSTANT_INT, 42);
    emit(&top, CS2_OP_GOSUB_WITH_PARAMS, 201);
    emit(&top, CS2_OP_RETURN, 0);

    memset(&host, 0, sizeof(host));
    host.scripts[host.script_count++] = &leaf;
    host.scripts[host.script_count++] = &mid;

    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, &host, test_host_exec);
    struct CS2VM2_Thread* t = CS2VM2_ThreadMain(&vm);
    CS2VM2_ThreadStart(t, &top.script);
    CHECK_INT(CS2VM2_RunScript(t), CS2VM_EXECNO_DONE, "run completes");

    int caller_local = -1, difference = -1;
    CS2VM2_PopInt(t, &caller_local);
    CS2VM2_PopInt(t, &difference);
    char* text = NULL;
    CS2VM2_PopStr(t, &text);

    CHECK_INT(difference, 32, "leaf saw its args in bank order (42 - 10)");
    CHECK_INT(caller_local, 10, "callee's local 1 did not alias caller's local 1");
    CHECK_STR(text, "abc", "string arg reached the callee's string bank");

    /* Every frame popped, so both locals stacks are back at zero. A pop that
     * forgot to rewind would leave them high and eventually exhaust the stack
     * in a loop of calls. */
    CHECK_INT(t->int_locals_top, 0, "int locals stack fully unwound");
    CHECK_INT(t->str_locals_top, 0, "str locals stack fully unwound");
    CHECK_INT(t->frame_sp, 0, "frame stack fully unwound");

    CS2VM2_Free(&vm);
}

/* --- 2: recursion to depth 100 -------------------------------------------- */

/*
 * rec(int n): return n = 0 ? 0 : n + rec(n - 1)
 *
 * Each level reads its own $n after the recursive call returns, so a slice that
 * did not survive the callee (or a stack realloc) gives the wrong sum rather
 * than crashing.
 */
static void
build_rec(struct TestScript* ts)
{
    script_begin(ts, 300, /*int_args*/ 1, /*str_args*/ 0, /*int_locals*/ 2, /*str_locals*/ 0);
    emit(ts, CS2_OP_PUSH_INT_LOCAL, 0);         /* 0 */
    emit(ts, CS2_OP_PUSH_CONSTANT_INT, 0);      /* 1 */
    emit(ts, CS2_OP_BRANCH_EQUALS, 7);          /* 2 -> base at 10 */
    emit(ts, CS2_OP_PUSH_INT_LOCAL, 0);         /* 3 */
    emit(ts, CS2_OP_PUSH_CONSTANT_INT, 1);      /* 4 */
    emit(ts, CS2_OP_SUB, 0);                    /* 5 */
    emit(ts, CS2_OP_GOSUB_WITH_PARAMS, 300);    /* 6 */
    emit(ts, CS2_OP_PUSH_INT_LOCAL, 0);         /* 7 */
    emit(ts, CS2_OP_ADD, 0);                    /* 8 */
    emit(ts, CS2_OP_RETURN, 0);                 /* 9 */
    emit(ts, CS2_OP_PUSH_CONSTANT_INT, 0);      /* 10 base */
    emit(ts, CS2_OP_RETURN, 0);                 /* 11 */
}

static void
test_recursion(void)
{
    struct TestScript rec, top;
    struct TestHost host;
    struct CS2VM2 vm;

    printf("recursion to depth 100:\n");

    build_rec(&rec);
    script_begin(&top, 101, 0, 0, 0, 0);
    emit(&top, CS2_OP_PUSH_CONSTANT_INT, 100);
    emit(&top, CS2_OP_GOSUB_WITH_PARAMS, 300);
    emit(&top, CS2_OP_RETURN, 0);

    memset(&host, 0, sizeof(host));
    host.scripts[host.script_count++] = &rec;

    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, &host, test_host_exec);
    struct CS2VM2_Thread* t = CS2VM2_ThreadMain(&vm);
    CS2VM2_ThreadStart(t, &top.script);
    CHECK_INT(CS2VM2_RunScript(t), CS2VM_EXECNO_DONE, "run completes");

    int sum = -1;
    CS2VM2_PopInt(t, &sum);
    CHECK_INT(sum, 5050, "sum 0..100, one live $n per level");
    /* 101 frames x 2 int locals is past the 256-cell initial stack, so the
     * stack was reallocated under live frames at least once. */
    CHECK_INT(t->int_locals_cap >= 202, 1, "locals stack grew past its initial size");
    CHECK_INT(t->int_locals_top, 0, "int locals stack fully unwound");

    CS2VM2_Free(&vm);
}

/* --- 4: an array handle passed to a proc ---------------------------------- */

/*
 * fill(string handle): $handle(1) = 55
 * top: def_int $arr(4) in string local 0; $arr(1) = 7; fill($arr);
 *      push $arr(1) -> 55
 *
 * The callee resolves the handle out of ITS string local 0, which is a
 * different cell of the same stack from the caller's string local 0.
 */
static void
test_array_through_proc(void)
{
    struct TestScript fill, top;
    struct TestHost host;
    struct CS2VM2 vm;

    printf("array handle through a proc:\n");

    script_begin(&fill, 400, /*int_args*/ 0, /*str_args*/ 1, /*int_locals*/ 1, /*str_locals*/ 2);
    emit(&fill, CS2_OP_PUSH_CONSTANT_INT, 1);  /* index */
    emit(&fill, CS2_OP_PUSH_CONSTANT_INT, 55); /* value */
    emit(&fill, CS2_OP_POP_ARRAY_INT, 0);
    emit(&fill, CS2_OP_RETURN, 0);

    /* The caller parks the handle in string local 1, the callee receives it in
     * string local 0 — different slots, so a callee that resolved through the
     * caller's base would find no array and the write would vanish. */
    script_begin(&top, 102, 0, 0, 1, 2);
    emit(&top, CS2_OP_PUSH_CONSTANT_INT, 4);
    emit(&top, CS2_OP_DEFINE_ARRAY, (1 << 16) | 'i');
    emit(&top, CS2_OP_PUSH_CONSTANT_INT, 1);
    emit(&top, CS2_OP_PUSH_CONSTANT_INT, 7);
    emit(&top, CS2_OP_POP_ARRAY_INT, 1);
    emit(&top, CS2_OP_PUSH_STRING_LOCAL, 1);
    emit(&top, CS2_OP_GOSUB_WITH_PARAMS, 400);
    emit(&top, CS2_OP_PUSH_CONSTANT_INT, 1);
    emit(&top, CS2_OP_PUSH_ARRAY_INT, 1);
    emit(&top, CS2_OP_RETURN, 0);

    memset(&host, 0, sizeof(host));
    host.scripts[host.script_count++] = &fill;

    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, &host, test_host_exec);
    struct CS2VM2_Thread* t = CS2VM2_ThreadMain(&vm);
    CS2VM2_ThreadStart(t, &top.script);
    CHECK_INT(CS2VM2_RunScript(t), CS2VM_EXECNO_DONE, "run completes");

    int cell = -1;
    CS2VM2_PopInt(t, &cell);
    CHECK_INT(cell, 55, "the proc wrote through the handle it was passed");

    CS2VM2_Free(&vm);
}

/* --- 5: yield and replay inside a nested call ------------------------------ */

/*
 * inner(): $i0 = 5; push_var(7); push $i0; ADD    -> 5 + varp
 * outer(): $i0 = 1000; inner(); push $i0; ADD     -> 1000 + that
 *
 * The host yields the first PUSH_VAR. The op is replayed from the checkpoint,
 * which must roll the operand stacks back without disturbing either frame's
 * locals — and must leave the locals tops where they were, since the frames
 * below are still live.
 */
static void
test_yield_in_nested_call(void)
{
    struct TestScript inner, outer;
    struct TestHost host;
    struct CS2VM2 vm;

    printf("yield and replay inside a nested call:\n");

    script_begin(&inner, 500, /*int_args*/ 0, /*str_args*/ 0, /*int_locals*/ 3, /*str_locals*/ 0);
    emit(&inner, CS2_OP_PUSH_CONSTANT_INT, 5);
    emit(&inner, CS2_OP_POP_INT_LOCAL, 0);
    emit(&inner, CS2_OP_PUSH_VAR, 7);
    emit(&inner, CS2_OP_PUSH_INT_LOCAL, 0);
    emit(&inner, CS2_OP_ADD, 0);
    emit(&inner, CS2_OP_RETURN, 0);

    script_begin(&outer, 103, 0, 0, 2, 0);
    emit(&outer, CS2_OP_PUSH_CONSTANT_INT, 1000);
    emit(&outer, CS2_OP_POP_INT_LOCAL, 0);
    emit(&outer, CS2_OP_GOSUB_WITH_PARAMS, 500);
    emit(&outer, CS2_OP_PUSH_INT_LOCAL, 0);
    emit(&outer, CS2_OP_ADD, 0);
    emit(&outer, CS2_OP_RETURN, 0);

    memset(&host, 0, sizeof(host));
    host.scripts[host.script_count++] = &inner;
    host.varp_yields_left = 1;
    host.varp_value = 99;

    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, &host, test_host_exec);
    struct CS2VM2_Thread* t = CS2VM2_ThreadMain(&vm);
    CS2VM2_ThreadStart(t, &outer.script);

    CHECK_INT(CS2VM2_RunScript(t), CS2VM_EXECNO_YIELD, "first pass yields in the callee");
    CHECK_INT(t->frame_sp, 2, "the callee's frame survives the yield");
    int int_top_at_yield = t->int_locals_top;
    CHECK_INT(int_top_at_yield, 5, "both frames' slices are still held (2 + 3)");

    CHECK_INT(CS2VM2_RunScript(t), CS2VM_EXECNO_DONE, "resume completes");
    CHECK_INT(host.varp_reads, 1, "the host answered the replayed read once");

    int total = -1;
    CS2VM2_PopInt(t, &total);
    CHECK_INT(total, 1104, "1000 + (5 + 99): no local was lost across the yield");
    CHECK_INT(t->int_locals_top, 0, "int locals stack fully unwound");

    CS2VM2_Free(&vm);
}

/*
 * The same yield, but taken over and over. Each retry re-runs the op from the
 * checkpoint; if the checkpoint did not restore the locals tops, a push that
 * happened before the yield would be re-applied on every pass and the stack
 * would climb without bound.
 */
static void
test_repeated_yield_does_not_leak_slice(void)
{
    struct TestScript inner, outer;
    struct TestHost host;
    struct CS2VM2 vm;

    printf("repeated yield does not leak locals stack:\n");

    script_begin(&inner, 501, 0, 0, 3, 0);
    emit(&inner, CS2_OP_PUSH_VAR, 7);
    emit(&inner, CS2_OP_RETURN, 0);

    script_begin(&outer, 104, 0, 0, 2, 0);
    emit(&outer, CS2_OP_GOSUB_WITH_PARAMS, 501);
    emit(&outer, CS2_OP_RETURN, 0);

    memset(&host, 0, sizeof(host));
    host.scripts[host.script_count++] = &inner;
    host.varp_yields_left = 20;
    host.varp_value = 3;

    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, &host, test_host_exec);
    struct CS2VM2_Thread* t = CS2VM2_ThreadMain(&vm);
    CS2VM2_ThreadStart(t, &outer.script);

    int tops_stable = 1;
    for( int i = 0; i < 20; i++ )
    {
        /* The VM refuses a second yield at the same site unless the halt is
         * cleared, which is what a host does when its awaited work advances. */
        CS2VM2_ClearYieldHalt(t);
        if( CS2VM2_RunScript(t) != CS2VM_EXECNO_YIELD )
        {
            tops_stable = 0;
            break;
        }
        if( t->int_locals_top != 5 )
            tops_stable = 0;
    }
    CHECK_INT(tops_stable, 1, "locals top is 5 on every one of 20 retries");

    CS2VM2_ClearYieldHalt(t);
    CHECK_INT(CS2VM2_RunScript(t), CS2VM_EXECNO_DONE, "the retried call finally completes");
    CHECK_INT(t->int_locals_top, 0, "int locals stack fully unwound");

    CS2VM2_Free(&vm);
}

int
main(void)
{
    test_nested_gosub();
    test_recursion();
    test_array_through_proc();
    test_yield_in_nested_call();
    test_repeated_yield_does_not_leak_slice();

    if( g_fail )
    {
        printf("frame slices test: %d FAILED\n", g_fail);
        return 1;
    }
    printf("frame slices test: all passed\n");
    return 0;
}
