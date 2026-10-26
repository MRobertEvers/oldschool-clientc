#ifndef CS2_SCRIPT_H
#define CS2_SCRIPT_H

#include "cs2vm2_dialect.h"

#include <stdbool.h>
#include <stdint.h>

#define CS2VM2_SCRIPT_STACK_MAX 2048
#define CS2VM2_SCRIPT_STRING_POOL 262144

struct CS2VM2_ScriptSwitchCase
{
    int key;
    int target_pc;
};

/*
 * Switch tables are heap arrays, matching RSCache_CS2_Script — they were
 * `[32]` tables of `[256]` cases and real scripts exceed both
 * (`[proc,skill_guide_build]` carries 73 tables; four scripts in an OSRS dump
 * carry a table of more than 256 cases). The fixed form also put 64 KB of
 * mostly-unused cases inside every decoded script.
 */
struct CS2VM2_ScriptSwitch
{
    int case_count;
    struct CS2VM2_ScriptSwitchCase* cases;
};

/** Decoded clientscript bytecode (RuneStar Script.kt). */
struct CS2VM2_Script
{
    int script_id;
    /* The era this script's bytes are in (cs2vm2_dialect.h): where its
     * commands differ from what the VM implements natively. NULL is the
     * native one, CS2VM2_DIALECT_OSRS -- a script built in memory (a test,
     * the server's compiler) is in the VM's own dialect. */
    struct CS2VM2_Dialect const* dialect;
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
    /* PUSH_CONSTANT_LONG's operand, a full int64 (rscache long_operands); NULL
     * for a script that has none. */
    int64_t* long_operands;
    char** string_operands;
    int switch_table_count;
    struct CS2VM2_ScriptSwitch* switch_tables;
};

void
CS2VM2_ScriptInit(struct CS2VM2_Script* script);

/** Allocate `count` switch tables (all empty). False on allocation failure. */
bool
CS2VM2_ScriptAllocSwitches(
    struct CS2VM2_Script* script,
    int count);

/** Allocate one table's cases. False on allocation failure. */
bool
CS2VM2_ScriptAllocSwitchCases(
    struct CS2VM2_Script* script,
    int table_index,
    int case_count);

void
CS2VM2_ScriptFree(struct CS2VM2_Script* script); /* frees the arrays from ToriMem_Script(); the struct is the caller's */

bool
CS2VM2_ScriptCopy(
    const struct CS2VM2_Script* src,
    struct CS2VM2_Script* dst);

#endif
