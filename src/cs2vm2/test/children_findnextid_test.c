/*
 * Unit test for IF_QUERY_NEXTID (214), driven through the real VM dispatch.
 *
 * 214 is the id-returning step of the children iterator that IF_QUERY
 * (211) and CC_QUERY (212) fill. rev-239 Statics.method7953 is the
 * whole body:
 *
 *     cursor >= count ? -1 : ids[cursor++]
 *
 * Until it was implemented the VM had no case for it at all, so every script
 * that walks a parent's dynamic children died on the opcode — proc 8490
 * (torirs_if_next_free_child) is a `while ($id ! -1) { ... $id = _214; }` loop
 * and it aborted on its second pass, which is what took the free-child-slot
 * search down with it.
 *
 * Three properties are worth pinning, and each of them is a different wrong
 * implementation:
 *
 *   - it ADVANCES. A read that forgets the `cursor++` is an infinite loop in
 *     every one of those scripts, not an error.
 *   - it terminates with -1, not 0. Zero is a legitimate sub-id; a script
 *     looping on `! -1` never stops, and one looping on `>= 0` stops one
 *     element early.
 *   - it shares its cursor with IF_QUERY_NEXT (213), which walks the
 *     same list and resolves the id to a component instead of pushing it.
 *     Separate cursors would let a mixed walk visit a child twice.
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

/* 214 answers out of the thread's own iterator state; nothing reaches the host. */
static int
no_host_exec(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request)
{
    (void)thread;
    (void)request;
    printf("  FAIL: IF_QUERY_NEXTID reached the host\n");
    g_fail++;
    return CS2VM_EXECNO_ERROR;
}

/* The host side of 213: report which sub-id it was handed, and target nothing. */
static int g_findnext_sub_id = -1;

static int
findnext_host_exec(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request)
{
    (void)thread;
    if( request->kind != CS2VM_HOST_REQUEST_IF_QUERY_NEXT )
    {
        printf("  FAIL: unexpected host request kind %d\n", (int)request->kind);
        g_fail++;
        return CS2VM_EXECNO_ERROR;
    }
    g_findnext_sub_id = request->u.IF_QUERY_NEXT.sub_id;
    return CS2VM_EXECNO_OK;
}

/*
 * Seed the iterator with `count` sub-ids, run `steps` copies of 214, and hand
 * back what each one pushed. `pushed` is filled bottom-first.
 */
static void
run_findnextid(
    int const* ids,
    int count,
    int steps,
    int* pushed)
{
    struct CS2VM2 vm;
    struct CS2VM2_Script script;
    struct CS2VM2_Thread* thread;
    int const op_count = steps + 1;

    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, NULL, no_host_exec);

    CS2VM2_ScriptInit(&script);
    script.script_id = 8490;
    script.op_count = op_count;
    script.opcodes = calloc((size_t)op_count, sizeof(uint16_t));
    script.int_operands = calloc((size_t)op_count, sizeof(int));
    script.string_operands = calloc((size_t)op_count, sizeof(char*));

    for( int i = 0; i < steps; i++ )
        script.opcodes[i] = (uint16_t)CS2_OP_IF_QUERY_NEXTID;
    script.opcodes[steps] = (uint16_t)CS2_OP_RETURN;

    thread = CS2VM2_ThreadMain(&vm);
    CS2VM2_PushCallScript(thread, &script);

    thread->children_iter_parent = 1;
    thread->children_iter_count = count;
    thread->children_iter_index = 0;
    for( int i = 0; i < count; i++ )
        thread->children_iter_indices[i] = ids[i];

    CS2VM2_RunScript(thread);

    /* The stack pops top-first; fill the caller's array bottom-first. */
    for( int i = steps - 1; i >= 0; i-- )
    {
        int value = 0;
        if( CS2VM2_PopInt(thread, &value) != CS2VM_EXECNO_OK )
        {
            printf("  FAIL: only %d of %d values were pushed\n", steps - 1 - i, steps);
            g_fail++;
            break;
        }
        pushed[i] = value;
    }

    CS2VM2_Free(&vm);
    free(script.opcodes);
    free(script.int_operands);
    free(script.string_operands);
}

/*
 * A walk of three children: each 214 answers the next sub-id, and the step past
 * the end answers -1 and keeps answering it.
 */
static void
test_walks_then_terminates(void)
{
    int const ids[3] = { 0, 4, 5 };
    int pushed[5] = { 0 };

    printf("214 walks the collected sub-ids, then answers -1:\n");
    run_findnextid(ids, 3, 5, pushed);

    CHECK_INT(pushed[0], 0, "first id (0 is a real sub-id, not an end marker)");
    CHECK_INT(pushed[1], 4, "second id");
    CHECK_INT(pushed[2], 5, "third id");
    CHECK_INT(pushed[3], -1, "exhausted");
    CHECK_INT(pushed[4], -1, "still exhausted");
}

/* An empty collect is the first-ever child case: -1 on the very first read. */
static void
test_empty_iterator(void)
{
    int pushed[1] = { 0 };

    printf("214 on an empty iterator:\n");
    run_findnextid(NULL, 0, 1, pushed);

    CHECK_INT(pushed[0], -1, "empty answers -1 immediately");
}

/*
 * 213 and 214 walk one cursor. Running 214 first must leave 213 pointing at the
 * SECOND child, not the first.
 */
static void
test_shares_the_cursor_with_213(void)
{
    struct CS2VM2 vm;
    struct CS2VM2_Script script;
    struct CS2VM2_Thread* thread;
    int const op_count = 3;
    int first = 0;

    printf("214 and 213 share one cursor:\n");

    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, NULL, findnext_host_exec);

    CS2VM2_ScriptInit(&script);
    script.script_id = 8491;
    script.op_count = op_count;
    script.opcodes = calloc((size_t)op_count, sizeof(uint16_t));
    script.int_operands = calloc((size_t)op_count, sizeof(int));
    script.string_operands = calloc((size_t)op_count, sizeof(char*));

    script.opcodes[0] = (uint16_t)CS2_OP_IF_QUERY_NEXTID;
    script.opcodes[1] = (uint16_t)CS2_OP_IF_QUERY_NEXT;
    script.opcodes[2] = (uint16_t)CS2_OP_RETURN;

    thread = CS2VM2_ThreadMain(&vm);
    CS2VM2_PushCallScript(thread, &script);

    thread->children_iter_parent = 1;
    thread->children_iter_count = 2;
    thread->children_iter_index = 0;
    thread->children_iter_indices[0] = 7;
    thread->children_iter_indices[1] = 9;

    g_findnext_sub_id = -1;
    CS2VM2_RunScript(thread);
    (void)CS2VM2_PopInt(thread, &first);

    CHECK_INT(first, 7, "214 took the first child");
    CHECK_INT(g_findnext_sub_id, 9, "213 resumed at the second child");

    CS2VM2_Free(&vm);
    free(script.opcodes);
    free(script.int_operands);
    free(script.string_operands);
}

int
main(void)
{
    test_walks_then_terminates();
    test_empty_iterator();
    test_shares_the_cursor_with_213();

    if( g_fail )
    {
        printf("children_findnextid_test: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("children_findnextid_test: all checks passed\n");
    return 0;
}
