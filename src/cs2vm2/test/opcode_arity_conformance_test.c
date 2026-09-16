/*
 * Every implemented CS2 opcode pops what the rev-239 client declares it pops.
 *
 * Three tables describe an opcode's arguments -- the VM's stack table
 * (cs2vm2_opcode_stack.gen.h), the decoder/compiler's command table
 * (3rd/rscache/src/cs2/cs2_command.gen.h) and the client's own command
 * catalogue they are both generated from -- and the generators refuse to let
 * those disagree. None of that says anything about the HANDLER. A hand-written
 * handler pops whatever its author thought the opcode took, and the table
 * cannot see it: every cc_input setter popped one int, including
 * cc_input_setplaceholdertext (a string) and cc_input_setselection (two ints),
 * while the table row for each said the right thing.
 *
 * So this runs each one. For every opcode the VM dispatches (known == 1) with a
 * fixed signature, it runs a one-op script over stacks pre-filled with sentinel
 * values and measures, at the moment the handler hands off to the host, how
 * many ints and strings it took. That is the handler's arity, independent of
 * whether it or the host then pushes the results. An opcode that never reaches
 * the host is measured by its net stack change instead.
 *
 * The ARRAY_* commands take their array as a handle on the string stack, and
 * reject anything that is not a live one exactly as the client throws on a
 * null array -- aborting before they push. So for those the string sentinels
 * are a handle to a live int array of two elements instead: every array op
 * then runs to completion, and a typed result (ARRAY_MAX, ARRAY_DELETE) comes
 * out in its int form, which is the one the table records.
 *
 * Skipped, with the count reported: commands whose arity is not fixed (a typed
 * pop selected by a base-type id, a hook's descriptor-driven argument list, a
 * param lookup that answers on either stack) -- the decoder's command kind says
 * which -- and the VM core below 100, whose control-flow ops have their own
 * tests.
 */

#include "cs2/cs2_command.h"
#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2_opcode_meta.h"
#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_opcode_stack.gen.h"
#include "cs2vm2/cs2vm2_script.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    SENTINELS = 32
};

struct Probe
{
    int calls;
    int ints_at_call;
    int strs_at_call;
};

static int
probe_exec(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request)
{
    struct Probe* probe = (struct Probe*)CS2VM_USER(thread);
    (void)request;
    if( probe->calls++ == 0 )
    {
        probe->ints_at_call = thread->ints_stack_top;
        probe->strs_at_call = thread->strs_stack_top;
    }
    return CS2VM_EXECNO_OK;
}

/* The DB family's handler (rs_cs2_host.c exec_db) pops its own arguments, by
 * design: the find forms cannot know which stack the search value is on until
 * they read its type tag. The VM hands off with the stacks untouched, so this
 * probe would see 0 pops. db_cache_test drives those handlers against the real
 * host instead. */
static bool
host_owns_stack(int opcode)
{
    return opcode >= CS2_OP_DB_FIND && opcode <= CS2_OP_DB_LISTALL_PRE228;
}

static bool
fixed_arity(int opcode)
{
    const struct RSCache_CS2_CommandInfo* info = RSCache_CS2_CommandGet(opcode);
    if( !info || info->kind != RSCACHE_CS2_CMD_BASIC )
        return false;
    for( int i = 0; i < info->arg_count; i++ )
        if( RSCache_CS2_CommandArg(info, i) == RSCACHE_CS2_PROTO_NONE )
            return false;
    return true;
}

/* Run [opcode, RETURN] once. Returns 0 when the measured arity matches. */
static int
check_opcode(int opcode)
{
    struct CS2VM2OpcodeStack const want = g_cs2vm2_opcode_stack[opcode];
    struct CS2VM2 vm;
    struct CS2VM2_Script script;
    struct CS2VM2_Thread* thread;
    struct Probe probe;
    uint16_t opcodes[2] = { (uint16_t)opcode, (uint16_t)CS2_OP_RETURN };
    int int_operands[2] = { 0, 0 };
    char* string_operands[2] = { NULL, NULL };
    static char sentinel[] = "1";
    int got_ints;
    int got_strs;
    int failed = 0;

    memset(&probe, 0, sizeof(probe));
    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, &probe, probe_exec);
    CS2VM2_ScriptInit(&script);
    script.script_id = 1;
    script.op_count = 2;
    script.opcodes = opcodes;
    script.int_operands = int_operands;
    script.string_operands = string_operands;

    thread = CS2VM2_ThreadMain(&vm);
    CS2VM2_PushCallScript(thread, &script);
    for( int i = 0; i < SENTINELS; i++ )
    {
        thread->ints_stack[i] = 1;
        thread->strs_stack[i] = sentinel;
    }
    thread->ints_stack_top = SENTINELS;
    thread->strs_stack_top = SENTINELS;
    if( strncmp(CS2_OpCode_String(opcode), "ARRAY_", 6) == 0 )
    {
        struct CS2VM2_Array* const array = &thread->arrays[thread->array_alloc++];
        array->cells.strings = calloc(4, sizeof(char*));
        assert(array->cells.strings);
        array->capacity = 4;
        array->size = 2;
        array->defined = 1;
        array->is_string = 0;
        array->cells.ints[0] = 5;
        array->cells.ints[1] = 7;
        for( int i = 0; i < SENTINELS; i++ )
            thread->strs_stack[i] = (char*)array;
    }
    thread->active_component_id = 1;
    thread->dot_component_id = 1;
    thread->children_iter_count = 1;
    thread->children_iter_parent = 1;
    thread->children_iter_indices[0] = 1;

    (void)CS2VM2_RunScript(thread);

    if( probe.calls > 0 )
    {
        got_ints = SENTINELS - probe.ints_at_call;
        got_strs = SENTINELS - probe.strs_at_call;
        if( got_ints != want.int_in || got_strs != want.str_in )
        {
            printf(
                "FAIL %5d %-40s pops %di/%ds before the host, client declares %di/%ds\n",
                opcode,
                CS2_OpCode_String(opcode),
                got_ints,
                got_strs,
                want.int_in,
                want.str_in);
            failed = 1;
        }
    }
    else
    {
        int const net_ints = thread->ints_stack_top - SENTINELS;
        int const net_strs = thread->strs_stack_top - SENTINELS;
        int const want_ints = (int)want.int_out - (int)want.int_in;
        int const want_strs = (int)want.str_out - (int)want.str_in;
        if( net_ints != want_ints || net_strs != want_strs )
        {
            printf(
                "FAIL %5d %-40s nets %+di/%+ds without the host, client declares %+di/%+ds\n",
                opcode,
                CS2_OpCode_String(opcode),
                net_ints,
                net_strs,
                want_ints,
                want_strs);
            failed = 1;
        }
    }

    CS2VM2_Free(&vm);
    return failed;
}

int
main(void)
{
    int checked = 0;
    int skipped = 0;
    int failures = 0;
    bool const trace = getenv("CS2_ARITY_TRACE") != NULL;

    setvbuf(stdout, NULL, _IONBF, 0);

    for( int opcode = 100; opcode < CS2VM2_OPCODE_STACK_MAX; opcode++ )
    {
        if( g_cs2vm2_opcode_stack[opcode].known != 1 )
            continue;
        if( !fixed_arity(opcode) || host_owns_stack(opcode) )
        {
            skipped++;
            continue;
        }
        checked++;
        if( trace )
            printf("... %d %s\n", opcode, CS2_OpCode_String(opcode));
        failures += check_opcode(opcode);
    }

    printf(
        "opcode arity conformance: %d implemented opcodes checked, %d with a non-fixed arity skipped, %d failures\n",
        checked,
        skipped,
        failures);
    return failures ? 1 : 0;
}
