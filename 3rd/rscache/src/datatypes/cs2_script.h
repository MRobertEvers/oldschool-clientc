#ifndef RSCACHE_DATATYPES_CS2_SCRIPT_H
#define RSCACHE_DATATYPES_CS2_SCRIPT_H

#include <stdint.h>

struct RSCache_CS2_ScriptSwitchCase
{
    int key;
    int target_pc;
};

struct RSCache_CS2_ScriptSwitch
{
    int case_count;
    struct RSCache_CS2_ScriptSwitchCase* cases;
};

/**
 * Decoded clientscript bytecode (RuneStar Script.kt).
 *
 * Switch tables are heap arrays rather than fixed ones. They were `[32]` tables
 * of `[256]` cases, and real scripts exceed both: `[proc,skill_guide_build]`
 * carries 73 tables, and four scripts in an OSRS dump carry a single table of
 * more than 256 cases. Those five records simply failed to decode.
 */
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
    struct RSCache_CS2_ScriptSwitch* switch_tables;
};

void
RSCache_CS2_ScriptInit(struct RSCache_CS2_Script* script);

/**
 * Allocate `count` switch tables. Returns false on failure, leaving the script
 * with none.
 */
int
RSCache_CS2_ScriptAllocSwitches(struct RSCache_CS2_Script* script, int count);

/** Allocate one table's cases. Returns false on failure. */
int
RSCache_CS2_ScriptAllocSwitchCases(
    struct RSCache_CS2_Script* script,
    int table_index,
    int case_count);

void
RSCache_CS2_ScriptFree(struct RSCache_CS2_Script* script);

#endif
