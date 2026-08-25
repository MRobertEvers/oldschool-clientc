#include "cs2vm2/cs2vm2.h"

#include <stdio.h>
#include <string.h>

struct HostRequestProducerEntry
{
    char const* name;
    int opcode;
};

static struct HostRequestProducerEntry const HOST_REQUEST_PRODUCERS[] = {
#define CS2VM_HOST_REQUEST_KIND(name, opcode, fields) \
    { #name, CS2_OP_##name },
#include "cs2vm2/cs2vm2_host_request_kinds.def"
#undef CS2VM_HOST_REQUEST_KIND
};

enum
{
    HOST_REQUEST_PRODUCER_COUNT =
        (int)(sizeof(HOST_REQUEST_PRODUCERS) / sizeof(HOST_REQUEST_PRODUCERS[0]))
};

_Static_assert(
    HOST_REQUEST_PRODUCER_COUNT == 633,
    "the producer replay test must exercise every hosted opcode");

struct CaptureHost
{
    int calls;
    int kinds[2];
};

struct InputSetCaptureHost
{
    enum CS2VM_HostRequestKind expected_kind;
    int calls;
    int bad_kind;
    int component_ids[2];
    int values[2];
};

static int
capture_host_exec(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request)
{
    struct CaptureHost* host = (struct CaptureHost*)CS2VM_USER(thread);

    if( host->calls < (int)(sizeof(host->kinds) / sizeof(host->kinds[0])) )
        host->kinds[host->calls] = (int)request->kind;
    host->calls++;

    /* Force the interpreter to roll the opcode back once, then let its exact
     * retry complete. This catches both first-emission aliases and handlers
     * that accidentally rebuild a retry as a family-level request. */
    return host->calls == 1 ? CS2VM_EXECNO_YIELD : CS2VM_EXECNO_OK;
}

static int
capture_input_set_exec(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request)
{
    struct InputSetCaptureHost* host = (struct InputSetCaptureHost*)CS2VM_USER(thread);
    int call = host->calls++;

    if( request->kind != host->expected_kind || call >= 2 )
    {
        host->bad_kind = 1;
        return CS2VM_EXECNO_ERROR;
    }

    switch( request->kind )
    {
    case CS2VM_HOST_REQUEST_CC_INPUT_SETCURSORWIDTH:
        host->component_ids[call] =
            request->u.CC_INPUT_SETCURSORWIDTH.component_id;
        host->values[call] = request->u.CC_INPUT_SETCURSORWIDTH.value;
        break;
    case CS2VM_HOST_REQUEST_IF_INPUT_SETCURSORWIDTH:
        host->component_ids[call] =
            request->u.IF_INPUT_SETCURSORWIDTH.component_id;
        host->values[call] = request->u.IF_INPUT_SETCURSORWIDTH.value;
        break;
    default:
        host->bad_kind = 1;
        return CS2VM_EXECNO_ERROR;
    }

    return host->calls == 1 ? CS2VM_EXECNO_YIELD : CS2VM_EXECNO_OK;
}

static int
exercise_producer(struct HostRequestProducerEntry const* entry)
{
    struct CS2VM2 vm;
    struct CaptureHost host;
    struct CS2VM2_Script script;
    struct CS2VM2_Thread* thread;
    struct CS2VM2_ThreadError error;
    enum CS2VM2_ThreadStatus first_status;
    enum CS2VM2_ThreadStatus retry_status;
    uint16_t opcode = (uint16_t)entry->opcode;
    int int_operand = 0;
    char stack_string[] = "i";
    char* string_operand = stack_string;
    int failed = 0;

    memset(&vm, 0, sizeof(vm));
    memset(&host, 0, sizeof(host));
    memset(&script, 0, sizeof(script));
    memset(&error, 0, sizeof(error));

    script.script_id = 1;
    script.op_count = 1;
    script.opcodes = &opcode;
    script.int_operands = &int_operand;
    script.string_operands = &string_operand;

    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, &host, capture_host_exec);
    thread = CS2VM2_ThreadMain(&vm);
    if( CS2VM2_ThreadStart(thread, &script) != CS2VM_EXECNO_OK )
    {
        fprintf(stderr, "FAIL: %s (%d) could not start\n", entry->name, entry->opcode);
        CS2VM2_Free(&vm);
        return 1;
    }

    /* Every producer gets valid, deterministic operands without coupling this
     * contract test to 632 separate stack signatures. Hosted ops pop at most a
     * small fraction of these values; `i` is also a valid SETON descriptor. */
    for( int i = 0; i < CS2VM_STACK_MAX / 2; i++ )
    {
        thread->ints_stack[i] = 1;
        thread->strs_stack[i] = stack_string;
    }
    thread->ints_stack_top = CS2VM_STACK_MAX / 2;
    thread->strs_stack_top = CS2VM_STACK_MAX / 2;
    thread->active_component_id = 1;
    thread->dot_component_id = 1;

    /* Opcode 213 consumes the iterator state before asking the host to target
     * the child. Seed one entry so this producer is exercised too. */
    thread->children_iter_count = 1;
    thread->children_iter_parent = 1;
    thread->children_iter_indices[0] = 1;

    first_status = CS2VM2_ThreadRun(thread, &error);
    retry_status = first_status == CS2VM2_THREAD_YIELDED
                       ? CS2VM2_ThreadResume(thread, &error)
                       : CS2VM2_THREAD_ERROR;

    if( first_status != CS2VM2_THREAD_YIELDED || retry_status != CS2VM2_THREAD_DONE ||
        host.calls != 2 || host.kinds[0] != entry->opcode || host.kinds[1] != entry->opcode )
    {
        fprintf(
            stderr,
            "FAIL: %s (%d): statuses=%d/%d calls=%d kinds=%d/%d\n",
            entry->name,
            entry->opcode,
            (int)first_status,
            (int)retry_status,
            host.calls,
            host.kinds[0],
            host.kinds[1]);
        failed = 1;
    }

    CS2VM2_Free(&vm);
    return failed;
}

static int
exercise_input_set_producer(
    enum CS2VM_HostRequestKind kind,
    int component_id,
    int value)
{
    struct CS2VM2 vm;
    struct InputSetCaptureHost host;
    struct CS2VM2_Script script;
    struct CS2VM2_Thread* thread;
    struct CS2VM2_ThreadError error;
    enum CS2VM2_ThreadStatus first_status;
    enum CS2VM2_ThreadStatus retry_status;
    uint16_t opcode = (uint16_t)kind;
    int int_operand = 0;
    char* string_operand = NULL;
    int expected_initial_stack_top;
    int failed = 0;

    memset(&vm, 0, sizeof(vm));
    memset(&host, 0, sizeof(host));
    memset(&script, 0, sizeof(script));
    memset(&error, 0, sizeof(error));

    host.expected_kind = kind;
    script.script_id = 2;
    script.op_count = 1;
    script.opcodes = &opcode;
    script.int_operands = &int_operand;
    script.string_operands = &string_operand;

    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, &host, capture_input_set_exec);
    thread = CS2VM2_ThreadMain(&vm);
    if( CS2VM2_ThreadStart(thread, &script) != CS2VM_EXECNO_OK )
    {
        fprintf(stderr, "FAIL: input-set %d could not start\n", (int)kind);
        CS2VM2_Free(&vm);
        return 1;
    }

    if( kind == CS2VM_HOST_REQUEST_CC_INPUT_SETCURSORWIDTH )
    {
        thread->active_component_id = component_id;
        thread->ints_stack[0] = value;
        expected_initial_stack_top = 1;
    }
    else
    {
        /* IF_INPUT_SET* pops the component first, then the value. Distinct
         * sentinels make a reversed pop order visible in the captured arm. */
        thread->ints_stack[0] = value;
        thread->ints_stack[1] = component_id;
        expected_initial_stack_top = 2;
    }
    thread->ints_stack_top = expected_initial_stack_top;

    first_status = CS2VM2_ThreadRun(thread, &error);
    if( first_status != CS2VM2_THREAD_YIELDED ||
        thread->ints_stack_top != expected_initial_stack_top )
    {
        fprintf(
            stderr,
            "FAIL: input-set %d initial status=%d restored_top=%d\n",
            (int)kind,
            (int)first_status,
            thread->ints_stack_top);
        failed = 1;
    }

    retry_status = first_status == CS2VM2_THREAD_YIELDED
                       ? CS2VM2_ThreadResume(thread, &error)
                       : CS2VM2_THREAD_ERROR;

    if( retry_status != CS2VM2_THREAD_DONE || host.calls != 2 || host.bad_kind ||
        thread->ints_stack_top != 0 )
    {
        fprintf(
            stderr,
            "FAIL: input-set %d retry status=%d calls=%d bad_kind=%d final_top=%d\n",
            (int)kind,
            (int)retry_status,
            host.calls,
            host.bad_kind,
            thread->ints_stack_top);
        failed = 1;
    }

    for( int call = 0; call < host.calls && call < 2; call++ )
    {
        if( host.component_ids[call] != component_id || host.values[call] != value )
        {
            fprintf(
                stderr,
                "FAIL: input-set %d call %d: component=%d value=%d\n",
                (int)kind,
                call + 1,
                host.component_ids[call],
                host.values[call]);
            failed = 1;
        }
    }

    CS2VM2_Free(&vm);
    return failed;
}

int
main(void)
{
    int failures = 0;

    for( int i = 0; i < HOST_REQUEST_PRODUCER_COUNT; i++ )
        failures += exercise_producer(&HOST_REQUEST_PRODUCERS[i]);

    failures += exercise_input_set_producer(
        CS2VM_HOST_REQUEST_CC_INPUT_SETCURSORWIDTH,
        0x13579,
        -0x2468);
    failures += exercise_input_set_producer(
        CS2VM_HOST_REQUEST_IF_INPUT_SETCURSORWIDTH,
        0x24680,
        -0x1357);

    if( failures )
    {
        fprintf(stderr, "host request producer replay: %d failure(s)\n", failures);
        return 1;
    }

    printf(
        "host request producer replay: %d exact kinds and CC/IF input payloads "
        "preserved across yield/retry\n",
        HOST_REQUEST_PRODUCER_COUNT);
    return 0;
}
