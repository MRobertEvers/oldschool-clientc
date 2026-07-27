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
    /* True when this script came from an RS2 (634/643) cache.
     *
     * Opcode *numbers* are normalised at copy time (cs2_opcode_dialect.h), but a
     * handful of ids above 4000 mean different commands in the two eras and so
     * carry different stack signatures — 6506 pushes 4 ints + 3 strings at 634
     * and 4 + 2 at OldSchool 239, 4124 pops two ints at 634 and none at
     * OldSchool. Renumbering those would need a private canonical id per
     * divergence; carrying the era on the script and overlaying a small table is
     * cheaper and keeps the canonical numbering as-is.
     * See g_cs2vm2_opcode_stack_rs2 in cs2vm2.c. */
    bool rs2_dialect;
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
