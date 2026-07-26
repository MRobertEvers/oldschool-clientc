#ifndef RSCACHE_DATATYPES_CS2_SCRIPT_H
#define RSCACHE_DATATYPES_CS2_SCRIPT_H

#include <stdint.h>

#define RSCACHE_CS2_SCRIPT_MAX_SWITCHES 32
#define RSCACHE_CS2_SCRIPT_MAX_SWITCH_CASES 256

struct RSCache_CS2_ScriptSwitchCase
{
    int key;
    int target_pc;
};

struct RSCache_CS2_ScriptSwitch
{
    int case_count;
    struct RSCache_CS2_ScriptSwitchCase cases[RSCACHE_CS2_SCRIPT_MAX_SWITCH_CASES];
};

/** Decoded clientscript bytecode (RuneStar Script.kt). */
struct RSCache_CS2_Script
{
    int script_id;
    char* signature;
    int local_int_count;
    int local_string_count;
    int local_long_count;
    int int_argument_count;
    int string_argument_count;
    int long_argument_count;
    int op_count;
    uint16_t* opcodes;
    int* int_operands;
    /** Parallel to opcodes; used by LCONST (61). Other ops leave entries zero. */
    int64_t* long_operands;
    char** string_operands;
    int switch_table_count;
    struct RSCache_CS2_ScriptSwitch switch_tables[RSCACHE_CS2_SCRIPT_MAX_SWITCHES];
};

void
RSCache_CS2_ScriptInit(struct RSCache_CS2_Script* script);

void
RSCache_CS2_ScriptFree(struct RSCache_CS2_Script* script);

#endif
