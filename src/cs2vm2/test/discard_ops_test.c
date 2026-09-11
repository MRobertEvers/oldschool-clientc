/*
 * Unit test for POP_INT_DISCARD (38) and POP_STRING_DISCARD (39), driven
 * through the real VM dispatch.
 *
 * These are how a call site throws away the values a proc returned:
 *
 *     gosub_with_params 9193      ; [proc,script9193](int,int,int)(int, int)
 *     pop_int_discard   0
 *     pop_int_discard   0
 *
 * The one property worth pinning is the operand. 38/39 are two of the three
 * opcodes (with RETURN) whose operand the loader reads as a single BYTE rather
 * than an int, and the reference interpreter's entire body for them is one
 * decrement. Reading that byte as a repeat count instead made the opcode a
 * NO-OP everywhere, because every discard in every cache here carries operand
 * 0 — 1,588 of them in cache.osrs239, 172 in cache.osrs184, 9 in cache.void634,
 * not one with a non-zero operand.
 *
 * A no-op discard does not fail where it happens. It leaks the discarded values
 * onto the operand stack, and the script dies later, on a full stack, in an
 * innocent callee: the skill guide's Overview creates one text widget per word
 * (script 9187) and discards two ints per word, so ~500 words of the Crafting
 * page filled all 1,024 int slots and the next PUSH_CONSTANT_INT — inside
 * script 8303, four frames away — was the reported failure.
 *
 * So both cases below run the opcode with a *non-zero* operand too: that is the
 * shape a "repeat count" reading gets wrong in the opposite direction, and the
 * sentinel underneath is what tells "popped exactly one" from "popped none" and
 * from "popped more".
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

static int
no_host_exec(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request)
{
    (void)thread;
    (void)request;
    printf("  FAIL: a discard reached the host\n");
    g_fail++;
    return CS2VM_EXECNO_ERROR;
}

/*
 * [push sentinel, push victim, POP_INT_DISCARD operand, RETURN] — then report
 * what the int stack has left.
 */
static void
run_int_discard(
    int operand,
    int* out_depth,
    int* out_top)
{
    struct CS2VM2 vm;
    struct CS2VM2_Script script;
    struct CS2VM2_Thread* thread;
    int const op_count = 4;
    int value = 0;

    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, NULL, no_host_exec);

    CS2VM2_ScriptInit(&script);
    script.script_id = 9993;
    script.op_count = op_count;
    script.opcodes = calloc((size_t)op_count, sizeof(uint16_t));
    script.int_operands = calloc((size_t)op_count, sizeof(int));
    script.string_operands = calloc((size_t)op_count, sizeof(char*));

    script.opcodes[0] = (uint16_t)CS2_OP_PUSH_CONSTANT_INT;
    script.int_operands[0] = 111;
    script.opcodes[1] = (uint16_t)CS2_OP_PUSH_CONSTANT_INT;
    script.int_operands[1] = 222;
    script.opcodes[2] = (uint16_t)CS2_OP_POP_INT_DISCARD;
    script.int_operands[2] = operand;
    script.opcodes[3] = (uint16_t)CS2_OP_RETURN;

    thread = CS2VM2_ThreadMain(&vm);
    CS2VM2_PushCallScript(thread, &script);
    CS2VM2_RunScript(thread);

    *out_depth = 0;
    *out_top = 0;
    while( CS2VM2_PopInt(thread, &value) == CS2VM_EXECNO_OK )
    {
        if( *out_depth == 0 )
            *out_top = value;
        (*out_depth)++;
    }

    CS2VM2_Free(&vm);
    free(script.opcodes);
    free(script.int_operands);
    free(script.string_operands);
}

/* The string twin, with the same sentinel-underneath shape. */
static void
run_str_discard(
    int operand,
    int* out_depth,
    char** out_top)
{
    struct CS2VM2 vm;
    struct CS2VM2_Script script;
    struct CS2VM2_Thread* thread;
    int const op_count = 4;
    char* value = NULL;

    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, NULL, no_host_exec);

    CS2VM2_ScriptInit(&script);
    script.script_id = 9994;
    script.op_count = op_count;
    script.opcodes = calloc((size_t)op_count, sizeof(uint16_t));
    script.int_operands = calloc((size_t)op_count, sizeof(int));
    script.string_operands = calloc((size_t)op_count, sizeof(char*));

    script.opcodes[0] = (uint16_t)CS2_OP_PUSH_CONSTANT_STRING;
    script.string_operands[0] = strdup("SENTINEL");
    script.opcodes[1] = (uint16_t)CS2_OP_PUSH_CONSTANT_STRING;
    script.string_operands[1] = strdup("VICTIM");
    script.opcodes[2] = (uint16_t)CS2_OP_POP_STRING_DISCARD;
    script.int_operands[2] = operand;
    script.opcodes[3] = (uint16_t)CS2_OP_RETURN;

    thread = CS2VM2_ThreadMain(&vm);
    CS2VM2_PushCallScript(thread, &script);
    CS2VM2_RunScript(thread);

    *out_depth = 0;
    *out_top = NULL;
    while( CS2VM2_PopStr(thread, &value) == CS2VM_EXECNO_OK )
    {
        if( *out_depth == 0 )
            *out_top = value ? strdup(value) : NULL;
        (*out_depth)++;
    }

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
    int depth = 0;
    int top = 0;
    char* str_top = NULL;

    printf("TEST: POP_INT_DISCARD / POP_STRING_DISCARD pop exactly one\n");

    run_int_discard(0, &depth, &top);
    CHECK_INT(depth, 1, "operand 0 leaves one int");
    CHECK_INT(top, 111, "and it is the sentinel, not the discarded value");

    run_int_discard(3, &depth, &top);
    CHECK_INT(depth, 1, "operand 3 still leaves one int (not a repeat count)");
    CHECK_INT(top, 111, "and still the sentinel");

    run_str_discard(0, &depth, &str_top);
    CHECK_INT(depth, 1, "operand 0 leaves one string");
    CHECK_STR(str_top, "SENTINEL", "and it is the sentinel");
    free(str_top);

    run_str_discard(3, &depth, &str_top);
    CHECK_INT(depth, 1, "operand 3 still leaves one string");
    CHECK_STR(str_top, "SENTINEL", "and still the sentinel");
    free(str_top);

    if( g_fail )
    {
        printf("discard ops test: %d FAILED\n", g_fail);
        return 1;
    }
    printf("discard ops test: all passed\n");
    return 0;
}
