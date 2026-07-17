#ifndef CS2_SCRIPT_H
#define CS2_SCRIPT_H

#include <stdbool.h>
#include <stdint.h>

#define CS2VM2_SCRIPT_MAX_SWITCHES 32
#define CS2VM2_SCRIPT_MAX_SWITCH_CASES 256
#define CS2VM2_SCRIPT_STACK_MAX 2048
#define CS2VM2_SCRIPT_STRING_POOL 262144

struct CS2VM2_ScriptSwitchCase
{
    int key;
    int target_pc;
};

struct CS2VM2_ScriptSwitch
{
    int case_count;
    struct CS2VM2_ScriptSwitchCase cases[CS2VM2_SCRIPT_MAX_SWITCH_CASES];
};

/** Decoded clientscript bytecode (RuneStar Script.kt). */
struct CS2VM2_Script
{
    int script_id;
    char* signature;
    int local_int_count;
    int local_string_count;
    int local_long_count;
    int int_argument_count;
    int string_argument_count;
    int long_argument_count;
    int int_stack_depth;
    int str_stack_depth;
    int op_count;
    uint16_t* opcodes;
    int* int_operands;
    char** string_operands;
    int switch_table_count;
    struct CS2VM2_ScriptSwitch switch_tables[CS2VM2_SCRIPT_MAX_SWITCHES];
};

void
CS2VM2_ScriptInit(struct CS2VM2_Script* script);

void
CS2VM2_ScriptFree(struct CS2VM2_Script* script);

bool
CS2VM2_ScriptCopy(
    const struct CS2VM2_Script* src,
    struct CS2VM2_Script* dst);

#endif
