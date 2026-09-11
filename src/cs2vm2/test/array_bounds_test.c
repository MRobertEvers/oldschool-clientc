/*
 * POP_ARRAY_INT writes directly because the opcode cannot yield.  These cases
 * pin the bounds behavior that the old tracked-store helper supplied: negative
 * and index == size writes are consumed and dropped, for both stack domains.
 */

#include "cs2vm2/cs2vm2.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition, label)                         \
    do                                                  \
    {                                                   \
        if( condition )                                 \
            printf("  ok: %s\n", label);               \
        else                                            \
        {                                               \
            fprintf(stderr, "  FAIL: %s\n", label);    \
            failures++;                                 \
        }                                               \
    } while( 0 )

static void
test_int_bounds(void)
{
    uint16_t opcodes[] = {
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_DEFINE_ARRAY,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_POP_ARRAY_INT,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_POP_ARRAY_INT,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_POP_ARRAY_INT,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_PUSH_ARRAY_INT,
        CS2_OP_RETURN,
    };
    int operands[] = {
        2,
        'i',
        0,
        17,
        0,
        -1,
        99,
        0,
        2,
        88,
        0,
        0,
        0,
        0,
    };
    char* string_operands[sizeof(opcodes) / sizeof(opcodes[0])] = { 0 };
    struct CS2VM2_Script script;
    struct CS2VM2 vm;
    struct CS2VM2_ThreadError error;

    memset(&error, 0, sizeof(error));
    memset(&script, 0, sizeof(script));
    script.script_id = 990001;
    script.local_string_count = 1;
    script.op_count = (int)(sizeof(opcodes) / sizeof(opcodes[0]));
    script.opcodes = opcodes;
    script.int_operands = operands;
    script.string_operands = string_operands;

    CS2VM2_Init(&vm);
    struct CS2VM2_Thread* thread = CS2VM2_ThreadMain(&vm);
    CHECK(CS2VM2_ThreadStart(thread, &script) == CS2VM_EXECNO_OK, "int array starts");
    CHECK(CS2VM2_ThreadRun(thread, &error) == CS2VM2_THREAD_DONE, "int array completes");
    CHECK(thread->array_alloc == 1, "int array allocated once");
    CHECK(thread->arrays[0].cells.ints[0] == 17, "int in-range write is retained");
    CHECK(thread->arrays[0].cells.ints[1] == -1, "int neighbor stays initialized");
    CHECK(thread->ints_stack_top == 1 && thread->ints_stack[0] == 17,
          "negative and index == size int writes are dropped");
    CHECK(thread->undo_log_len == 0, "VM-only int write creates no undo entry");
    CS2VM2_Free(&vm);
}

static void
test_string_bounds(void)
{
    uint16_t opcodes[] = {
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_DEFINE_ARRAY,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_PUSH_CONSTANT_STRING,
        CS2_OP_POP_ARRAY_INT,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_PUSH_CONSTANT_STRING,
        CS2_OP_POP_ARRAY_INT,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_PUSH_CONSTANT_STRING,
        CS2_OP_POP_ARRAY_INT,
        CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_PUSH_ARRAY_INT,
        CS2_OP_RETURN,
    };
    int operands[] = {
        2,
        's',
        0,
        0,
        0,
        -1,
        0,
        0,
        2,
        0,
        0,
        0,
        0,
        0,
    };
    char* string_operands[] = {
        NULL,
        NULL,
        NULL,
        "keep",
        NULL,
        NULL,
        "bad-negative",
        NULL,
        NULL,
        "bad-high",
        NULL,
        NULL,
        NULL,
        NULL,
    };
    struct CS2VM2_Script script;
    struct CS2VM2 vm;
    struct CS2VM2_ThreadError error;

    memset(&error, 0, sizeof(error));
    memset(&script, 0, sizeof(script));
    script.script_id = 990002;
    script.local_string_count = 1;
    script.op_count = (int)(sizeof(opcodes) / sizeof(opcodes[0]));
    script.opcodes = opcodes;
    script.int_operands = operands;
    script.string_operands = string_operands;

    CS2VM2_Init(&vm);
    struct CS2VM2_Thread* thread = CS2VM2_ThreadMain(&vm);
    CHECK(CS2VM2_ThreadStart(thread, &script) == CS2VM_EXECNO_OK, "string array starts");
    CHECK(CS2VM2_ThreadRun(thread, &error) == CS2VM2_THREAD_DONE, "string array completes");
    CHECK(thread->array_alloc == 1, "string array allocated once");
    CHECK(thread->arrays[0].cells.strings[0] &&
              strcmp(thread->arrays[0].cells.strings[0], "keep") == 0,
          "string in-range write is retained");
    CHECK(thread->arrays[0].cells.strings[1] == NULL, "string neighbor stays initialized");
    CHECK(thread->strs_stack_top == 1 && thread->strs_stack[0] &&
              strcmp(thread->strs_stack[0], "keep") == 0,
          "negative and index == size string writes are dropped");
    CHECK(thread->undo_log_len == 0, "VM-only string write creates no undo entry");
    CS2VM2_Free(&vm);
}

int
main(void)
{
    printf("TEST: POP_ARRAY_INT bounds and direct-store behavior\n");
    test_int_bounds();
    test_string_bounds();

    if( failures )
    {
        fprintf(stderr, "array bounds: %d failure(s)\n", failures);
        return 1;
    }
    printf("array bounds: int/string OOB writes dropped; direct writes untracked\n");
    return 0;
}
