/*
 * The HIGHLIGHT_* family (7000..7044), driven through the real VM dispatch
 * against a recording host.
 *
 * This client draws no highlight overlay, so every one of these opcodes is a
 * host-side no-op — which is exactly why the family needs pinning. A no-op that
 * is *dispatched* pops the right arguments and leaves the stack where the
 * script expects it; a no-op that falls through to CS2VM2_Op_StackMetaStub does
 * the same arithmetic from the generated table but never reaches the host, and
 * announces itself as a faked result. Half of this family used to do the
 * latter: opening the settings panel ran HIGHLIGHT_TILE_SETUP (clientscript
 * 4763) and HIGHLIGHT_TILE_CLEAR (5198) straight into the stub.
 *
 * So the test is "the host was reached, with these arguments, and the stack is
 * this deep afterwards", for all 45 opcodes. The arities below are written out
 * rather than read from cs2vm2_opcode_stack.gen.h on purpose: reading them from
 * the table the implementation pops from would make any wrong row agree with
 * itself.
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
        if( gv != wv )                                                                             \
        {                                                                                          \
            printf("  FAIL: %s got %d want %d\n", label, gv, wv);                                  \
            g_fail++;                                                                              \
        }                                                                                          \
    } while( 0 )

struct RecordingHost
{
    int calls;
    enum CS2VM_HostRequestKind kind;
    struct CS2VM_HostRequest_Highlight highlight;
};

/* Answers a query the way rs_cs2_host.c does — "not highlighted" — because the
 * push is part of the contract being tested, not an implementation detail. */
static int
recording_host_exec(struct CS2VM2_Thread* thread, struct CS2VM_HostRequest* request)
{
    struct RecordingHost* host = (struct RecordingHost*)thread->vm->user;

    host->calls++;
    host->kind = request->kind;
    if( request->kind != CS2VM_HOST_REQUEST_HIGHLIGHT )
        return CS2VM_EXECNO_OK;

    host->highlight = request->u.highlight;
    if( request->u.highlight.query )
        return CS2VM2_PushInt(thread, 0);
    return CS2VM_EXECNO_OK;
}

struct HighlightCase
{
    int opcode;
    char const* name;
    int int_in;
    int str_in;
    int int_out;
};

/* SETUP, ON, OFF, GET, CLEAR per subject, in opcode order. */
static struct HighlightCase const CASES[] = {
    { CS2_OP_HIGHLIGHT_NPC_SETUP, "HIGHLIGHT_NPC_SETUP", 5, 0, 0 },
    { CS2_OP_HIGHLIGHT_NPC_ON, "HIGHLIGHT_NPC_ON", 3, 0, 0 },
    { CS2_OP_HIGHLIGHT_NPC_OFF, "HIGHLIGHT_NPC_OFF", 3, 0, 0 },
    { CS2_OP_HIGHLIGHT_NPC_GET, "HIGHLIGHT_NPC_GET", 3, 0, 1 },
    { CS2_OP_HIGHLIGHT_NPC_CLEAR, "HIGHLIGHT_NPC_CLEAR", 1, 0, 0 },

    { CS2_OP_HIGHLIGHT_NPCTYPE_SETUP, "HIGHLIGHT_NPCTYPE_SETUP", 5, 0, 0 },
    { CS2_OP_HIGHLIGHT_NPCTYPE_ON, "HIGHLIGHT_NPCTYPE_ON", 2, 0, 0 },
    { CS2_OP_HIGHLIGHT_NPCTYPE_OFF, "HIGHLIGHT_NPCTYPE_OFF", 2, 0, 0 },
    { CS2_OP_HIGHLIGHT_NPCTYPE_GET, "HIGHLIGHT_NPCTYPE_GET", 2, 0, 1 },
    { CS2_OP_HIGHLIGHT_NPCTYPE_CLEAR, "HIGHLIGHT_NPCTYPE_CLEAR", 1, 0, 0 },

    { CS2_OP_HIGHLIGHT_LOC_SETUP, "HIGHLIGHT_LOC_SETUP", 5, 0, 0 },
    { CS2_OP_HIGHLIGHT_LOC_ON, "HIGHLIGHT_LOC_ON", 4, 0, 0 },
    { CS2_OP_HIGHLIGHT_LOC_OFF, "HIGHLIGHT_LOC_OFF", 4, 0, 0 },
    { CS2_OP_HIGHLIGHT_LOC_GET, "HIGHLIGHT_LOC_GET", 4, 0, 1 },
    { CS2_OP_HIGHLIGHT_LOC_CLEAR, "HIGHLIGHT_LOC_CLEAR", 1, 0, 0 },

    { CS2_OP_HIGHLIGHT_LOCTYPE_SETUP, "HIGHLIGHT_LOCTYPE_SETUP", 5, 0, 0 },
    { CS2_OP_HIGHLIGHT_LOCTYPE_ON, "HIGHLIGHT_LOCTYPE_ON", 2, 0, 0 },
    { CS2_OP_HIGHLIGHT_LOCTYPE_OFF, "HIGHLIGHT_LOCTYPE_OFF", 2, 0, 0 },
    { CS2_OP_HIGHLIGHT_LOCTYPE_GET, "HIGHLIGHT_LOCTYPE_GET", 2, 0, 1 },
    { CS2_OP_HIGHLIGHT_LOCTYPE_CLEAR, "HIGHLIGHT_LOCTYPE_CLEAR", 1, 0, 0 },

    { CS2_OP_HIGHLIGHT_OBJ_SETUP, "HIGHLIGHT_OBJ_SETUP", 5, 0, 0 },
    { CS2_OP_HIGHLIGHT_OBJ_ON, "HIGHLIGHT_OBJ_ON", 4, 0, 0 },
    { CS2_OP_HIGHLIGHT_OBJ_OFF, "HIGHLIGHT_OBJ_OFF", 4, 0, 0 },
    { CS2_OP_HIGHLIGHT_OBJ_GET, "HIGHLIGHT_OBJ_GET", 4, 0, 1 },
    { CS2_OP_HIGHLIGHT_OBJ_CLEAR, "HIGHLIGHT_OBJ_CLEAR", 1, 0, 0 },

    { CS2_OP_HIGHLIGHT_OBJTYPE_SETUP, "HIGHLIGHT_OBJTYPE_SETUP", 5, 0, 0 },
    { CS2_OP_HIGHLIGHT_OBJTYPE_ON, "HIGHLIGHT_OBJTYPE_ON", 2, 0, 0 },
    { CS2_OP_HIGHLIGHT_OBJTYPE_OFF, "HIGHLIGHT_OBJTYPE_OFF", 2, 0, 0 },
    { CS2_OP_HIGHLIGHT_OBJTYPE_GET, "HIGHLIGHT_OBJTYPE_GET", 2, 0, 1 },
    { CS2_OP_HIGHLIGHT_OBJTYPE_CLEAR, "HIGHLIGHT_OBJTYPE_CLEAR", 1, 0, 0 },

    /* The two name-keyed groups: one int (the group) plus one string. */
    { CS2_OP_HIGHLIGHT_PLAYER_SETUP, "HIGHLIGHT_PLAYER_SETUP", 5, 0, 0 },
    { CS2_OP_HIGHLIGHT_PLAYER_ON, "HIGHLIGHT_PLAYER_ON", 1, 1, 0 },
    { CS2_OP_HIGHLIGHT_PLAYER_OFF, "HIGHLIGHT_PLAYER_OFF", 1, 1, 0 },
    { CS2_OP_HIGHLIGHT_PLAYER_GET, "HIGHLIGHT_PLAYER_GET", 1, 1, 1 },
    { CS2_OP_HIGHLIGHT_PLAYER_CLEAR, "HIGHLIGHT_PLAYER_CLEAR", 1, 0, 0 },

    { CS2_OP_HIGHLIGHT_TILE_SETUP, "HIGHLIGHT_TILE_SETUP", 5, 0, 0 },
    { CS2_OP_HIGHLIGHT_TILE_ON, "HIGHLIGHT_TILE_ON", 3, 0, 0 },
    { CS2_OP_HIGHLIGHT_TILE_OFF, "HIGHLIGHT_TILE_OFF", 3, 0, 0 },
    { CS2_OP_HIGHLIGHT_TILE_GET, "HIGHLIGHT_TILE_GET", 3, 0, 1 },
    { CS2_OP_HIGHLIGHT_TILE_CLEAR, "HIGHLIGHT_TILE_CLEAR", 1, 0, 0 },

    { CS2_OP__7040, "_7040", 5, 0, 0 },
    { CS2_OP__7041, "_7041", 1, 1, 0 },
    { CS2_OP__7042, "_7042", 1, 1, 0 },
    { CS2_OP__7043, "_7043", 1, 1, 1 },
    { CS2_OP__7044, "_7044", 1, 0, 0 },
};

/* `push str? ; push int x N ; <opcode> ; return` — the shape every call site in
 * the cache compiles to. The pushed ints are 101, 102, ... so a mixed-up pop
 * order shows as a wrong value rather than a wrong count. */
static void
run_case(struct RecordingHost* host, struct HighlightCase const* c, int* out_int_top, int* out_str_top)
{
    struct CS2VM2 vm;
    struct CS2VM2_Script script;
    struct CS2VM2_Thread* thread;
    int const op_count = c->int_in + c->str_in + 2;
    int at = 0;

    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, host, recording_host_exec);

    CS2VM2_ScriptInit(&script);
    script.script_id = 4763;
    script.op_count = op_count;
    script.opcodes = calloc((size_t)op_count, sizeof(uint16_t));
    script.int_operands = calloc((size_t)op_count, sizeof(int));
    script.string_operands = calloc((size_t)op_count, sizeof(char*));

    for( int i = 0; i < c->str_in; i++ )
    {
        script.opcodes[at] = (uint16_t)CS2_OP_PUSH_CONSTANT_STRING;
        script.string_operands[at++] = "Zezima";
    }
    for( int i = 0; i < c->int_in; i++ )
    {
        script.opcodes[at] = (uint16_t)CS2_OP_PUSH_CONSTANT_INT;
        script.int_operands[at++] = 101 + i;
    }
    script.opcodes[at++] = (uint16_t)c->opcode;
    script.opcodes[at] = (uint16_t)CS2_OP_RETURN;

    thread = CS2VM2_ThreadMain(&vm);
    CS2VM2_PushCallScript(thread, &script);
    CS2VM2_RunScript(thread);

    *out_int_top = thread->ints_stack_top;
    *out_str_top = thread->strs_stack_top;

    CS2VM2_Free(&vm);
    free(script.opcodes);
    free(script.int_operands);
    free(script.string_operands);
}

int
main(void)
{
    int const case_count = (int)(sizeof(CASES) / sizeof(CASES[0]));

    printf("TEST: HIGHLIGHT_* opcodes (7000..7044)\n");

    for( int i = 0; i < case_count; i++ )
    {
        struct HighlightCase const* c = &CASES[i];
        struct RecordingHost host;
        char label[128];
        int int_top = -1;
        int str_top = -1;

        memset(&host, 0, sizeof(host));
        run_case(&host, c, &int_top, &str_top);

        /* Reached the host at all — the stub would have popped the same args
         * and returned without ever calling it. */
        snprintf(label, sizeof(label), "%s reaches the host", c->name);
        CHECK_INT(host.calls, 1, label);
        snprintf(label, sizeof(label), "%s request kind", c->name);
        CHECK_INT((int)host.kind, (int)CS2VM_HOST_REQUEST_HIGHLIGHT, label);
        snprintf(label, sizeof(label), "%s opcode carried through", c->name);
        CHECK_INT(host.highlight.opcode, c->opcode, label);

        snprintf(label, sizeof(label), "%s arg count", c->name);
        CHECK_INT(host.highlight.arg_count, c->int_in, label);
        snprintf(label, sizeof(label), "%s query flag", c->name);
        CHECK_INT(host.highlight.query ? 1 : 0, c->int_out ? 1 : 0, label);

        /* args[0] is the first int the script pushed, not the first popped. */
        for( int a = 0; a < c->int_in; a++ )
        {
            snprintf(label, sizeof(label), "%s args[%d]", c->name, a);
            CHECK_INT(host.highlight.args[a], 101 + a, label);
        }

        /* What the script sees afterwards: the GET result and nothing else. */
        snprintf(label, sizeof(label), "%s int stack depth after", c->name);
        CHECK_INT(int_top, c->int_out, label);
        snprintf(label, sizeof(label), "%s str stack depth after", c->name);
        CHECK_INT(str_top, 0, label);
    }

    if( g_fail )
    {
        printf("%d failure(s) across %d opcodes\n", g_fail, case_count);
        return 1;
    }
    printf("All %d HIGHLIGHT opcodes dispatch to the host. passed\n", case_count);
    return 0;
}
