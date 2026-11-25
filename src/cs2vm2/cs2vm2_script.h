#ifndef CS2_SCRIPT_H
#define CS2_SCRIPT_H

#include "cs2vm2_dialect.h"

struct CS2VM2_Jit;

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

/* A wire id the script's dialect does not define, and the pc that held it:
 * the opcode there is UNDEFINED_WIRE_ID (CS2VM2_ScriptResolveDialect). */
struct CS2VM2_UndefinedWire
{
    int pc;
    uint16_t wire;
};

/** Decoded clientscript bytecode (RuneStar Script.kt). */
struct CS2VM2_Script
{
    int script_id;
    /* The era this script's bytes came from (cs2vm2_dialect.h). Its opcodes
     * were translated from that era's wire ids to canonical instructions when
     * it was loaded; the dialect stays so a print can name the wire id. NULL
     * is the native one, CS2VM2_DIALECT_OSRS -- a script built in memory (a
     * test, the server's compiler) is written in canonical instructions. */
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
    /* Canonical instructions (enum CS2_Op), each below CS2_OP_PLAIN_COUNT. */
    uint16_t* opcodes;
    /* The wire id behind each pc whose opcode is UNDEFINED_WIRE_ID, in pc
     * order (CS2VM2_ScriptResolveDialect); NULL and 0 for a script with none. */
    struct CS2VM2_UndefinedWire* undefined_wires;
    int undefined_wire_count;
    int* int_operands;
    /* PUSH_CONSTANT_LONG's operand, a full int64 (rscache long_operands); NULL
     * for a script that has none. */
    int64_t* long_operands;
    /* PUSH_CONSTANT_STRING's operand as decoded text, NULL where an op has
     * none: the script's own copy, what the jit fingerprints and serializes,
     * what a debug dump prints, and what a script built in memory (a test) is
     * given. */
    char** string_operands;
    /* The same constants as the strings the VM pushes (cs2vm2_str.h), made
     * from string_operands by CS2VM2_ScriptEnsureConstants and owned by the
     * script. NULL until then. */
    struct CS2VM2_Str** string_constants;
    int switch_table_count;
    struct CS2VM2_ScriptSwitch* switch_tables;
    /* The load-time jit's overlay (cs2vm2_jit.h): NULL runs the plain codes
     * below. Built or attached only while no frame references the script;
     * freed by CS2VM2_ScriptFree. */
    struct CS2VM2_Jit* jit;
    /* What the interpreter's one loop switches on: a code per pc
     * (enum CS2VM2_JitCode). run_code is the jit overlay's codes while one is
     * attached and plain_code otherwise; plain_code is the script's own
     * (g_cs2vm2_hot_op of each opcode, 0 = call the opcode's handler), built
     * by CS2VM2_ScriptPrepare. The loop never looks an opcode up to decide
     * how to run it. */
    uint8_t* plain_code;
    uint8_t const* run_code;
    /* Remembered gosub callees (cs2vm2_callee.h): per pc, the script the
     * gosub there pushed last, good while callees_epoch is the process's
     * script epoch. NULL for a script with no GOSUB_WITH_PARAMS. Made by
     * CS2VM2_ScriptPrepare, freed by CS2VM2_ScriptFree. */
    struct CS2VM2_Script** callees;
    uint32_t callees_epoch;
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

/*
 * Make `script`'s string_constants from its string_operands, once; a no-op
 * when they exist or the script has no string operands. CS2VM2_ScriptPrepare
 * and the jit's build and attach call it.
 */
void
CS2VM2_ScriptEnsureConstants(struct CS2VM2_Script* script);

/*
 * Everything a run assumes of a script, done once when it is loaded: its
 * string constants (a constant row for every pc, NULL where an op has none)
 * and its dispatch codes (plain_code, and run_code while no jit overlay is
 * attached). Every loader and CS2VM2_ScriptCopy call it after the opcodes are
 * final (the dialect's translation, CS2VM2_ScriptResolveDialect); whoever builds a script by hand
 * calls it before the script first runs, and again after changing an opcode.
 * CS2VM2_PushCallScript asserts it was done and checks nothing itself.
 */
void
CS2VM2_ScriptPrepare(struct CS2VM2_Script* script);

#endif
