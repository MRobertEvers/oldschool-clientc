#include "cs2vm2/cs2vm2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_find(int string_array, int start, int end, int expected)
{
    /* Two equal entries separated by another value exercise the exact
     * previous_index+1 loop used by Sailing's native facility rows. */
    uint16_t ops[] = {
        CS2_OP_PUSH_CONSTANT_INT, CS2_OP_DEFINE_ARRAY,
        CS2_OP_PUSH_CONSTANT_INT, CS2_OP_PUSH_CONSTANT_INT, CS2_OP_POP_ARRAY_INT,
        CS2_OP_PUSH_CONSTANT_INT, CS2_OP_PUSH_CONSTANT_INT, CS2_OP_POP_ARRAY_INT,
        CS2_OP_PUSH_CONSTANT_INT, CS2_OP_PUSH_CONSTANT_INT, CS2_OP_POP_ARRAY_INT,
        CS2_OP_PUSH_STRING_LOCAL, CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_PUSH_CONSTANT_INT, CS2_OP_PUSH_CONSTANT_INT, CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_ARRAY_FIND, CS2_OP_RETURN,
    };
    int values[] = {5, 'i', 1, 7, 0, 2, 9, 0, 3, 7, 0, 0, 7, start, end, 0, 0, 0};
    char* strings[18] = {0};
    if( string_array )
    {
        values[1] = 's'; values[15] = 2;
        ops[3] = ops[6] = ops[9] = ops[12] = CS2_OP_PUSH_CONSTANT_STRING;
        strings[3] = strings[9] = strings[12] = "hook";
        strings[6] = "cargo";
    }
    struct CS2VM2 vm;
    struct CS2VM2_Script script = {0};
    struct CS2VM2_ThreadError error = {0};
    script.script_id = 990805;
    script.local_string_count = 1;
    script.op_count = 18;
    script.opcodes = ops; script.int_operands = values; script.string_operands = strings;
    CS2VM2_Init(&vm);
    struct CS2VM2_Thread* thread = CS2VM2_ThreadMain(&vm);
    if( CS2VM2_ThreadStart(thread, &script) != CS2VM_EXECNO_OK ||
        CS2VM2_ThreadRun(thread, &error) != CS2VM2_THREAD_DONE ||
        thread->ints_stack_top != 1 || thread->strs_stack_top != 0 ||
        thread->ints_stack[0] != expected )
    {
        fprintf(stderr, "ARRAY_FIND type=%d range[%d,%d) expected%d, stack%d/%d result%d\n",
                string_array, start, end, expected, thread->ints_stack_top,
                thread->strs_stack_top, thread->ints_stack_top ? thread->ints_stack[0] : -999);
        exit(1);
    }
    CS2VM2_Free(&vm);
}

int main(void)
{
    for( int type = 0; type < 2; ++type )
    {
        check_find(type, 0, -1, 1);
        check_find(type, 2, -1, 3);
        check_find(type, 4, -1, -1);
        check_find(type, 2, 3, -1);
        check_find(type, -20, 100, 1);
        check_find(type, 4, 2, -1);
        for( int i = 0; i < 40; ++i ) check_find(type, 2, -1, 3);
    }
    puts("PASS ARRAY_FIND integer/string values, bounded ranges and balanced stacks");
    return 0;
}
