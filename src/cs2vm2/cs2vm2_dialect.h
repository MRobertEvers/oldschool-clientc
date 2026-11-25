#ifndef CS2VM2_DIALECT_H
#define CS2VM2_DIALECT_H

#include <stdbool.h>
#include <stdint.h>

/*
 * What one era of CS2 means by the same bytes.
 *
 * A cache spells each command with a WIRE id; the VM runs CANONICAL
 * instructions (cs2_opcode.h), its own dense numbering, which no cache
 * spells. A dialect is a complete table from the wire ids its caches use to
 * canonical instructions, generated (tools/cs2_gen_opcodes/canonical.py),
 * with nothing implicit: OldSchool's own wire ids are a table like any
 * other's. Where an era changed a command's stack shape the table picks the
 * version (CC_CREATE_V1 / _V2, DEFINE_ARRAY_V1 / _V2, ...); where the 634
 * client's id names another command it picks that command (RS2_*). Nothing
 * about the dialect is decided while a script runs.
 *
 * The translation happens once per script, when it is loaded:
 * CS2VM2_ScriptResolveDialect. It is the load-time validation every switch
 * over canonical instructions relies on: after it, every opcode of the
 * script is below CS2_OP_PLAIN_COUNT (a wire id the dialect does not define
 * becomes UNDEFINED_WIRE_ID), so the interpreter's dispatch has no default case to
 * reach. CS2VM2_ScriptPrepare asserts the same of a script built by hand.
 *
 * The engine picks one per cache profile (CS2_DialectForCache); a script
 * carries a pointer to it, so scripts from two caches can share a VM, and a
 * print can name the wire id a human will look up (CS2VM2_DialectWireOf).
 */

struct CS2VM2_DialectWire
{
    uint16_t wire;
    uint16_t op; /* enum CS2_Op */
};

/* op_to_wire for an instruction the dialect never produces. */
#define CS2VM2_DIALECT_NO_WIRE 0xFFFFu

struct CS2VM2_Dialect
{
    /* Stable short name ("osrs", "osrs230", "osrs229", "rs2_634"): logs, and
     * the jit's dialect tag. */
    char const* name;
    /* Every wire id this era defines, sorted by wire id. */
    struct CS2VM2_DialectWire const* wire_to_op;
    int wire_count;
    /* [CS2_OP_PLAIN_COUNT]: the wire id a print names for an instruction
     * (where two wire ids reach one instruction, the era's own spelling), or
     * CS2VM2_DIALECT_NO_WIRE. */
    uint16_t const* op_to_wire;
};

/* Numbered arrays a run shares under the V1 array instructions (the classic
 * client's five). */
#define CS2VM2_SHARED_ARRAYS 5

struct CS2VM2_Script;

/* The canonical instruction `wire` is in `dialect`, or -1 when the dialect
 * does not define it. Asserts `dialect`. */
int
CS2VM2_DialectTranslate(
    struct CS2VM2_Dialect const* dialect,
    int wire);

/* The wire id `dialect` spells canonical instruction `op` with, for a print a
 * human looks up; -1 when it has none (a jit instruction, or one the dialect
 * never produces). NULL is the native dialect, as on CS2VM2_Script::dialect. */
int
CS2VM2_DialectWireOf(
    struct CS2VM2_Dialect const* dialect,
    int op);

/*
 * Translate `script`'s opcodes, as decoded from the cache, from wire ids of
 * `script->dialect` to canonical instructions, in place. Every loader calls
 * it exactly once, before CS2VM2_ScriptPrepare; a script built in memory (a
 * test) is written in canonical instructions and skips it.
 *
 * A wire id the dialect does not define becomes UNDEFINED_WIRE_ID, its wire
 * id kept in script->undefined_wires: the script loads, and that instruction
 * does what an instruction with no stack signature does if it is ever run.
 */
void
CS2VM2_ScriptResolveDialect(struct CS2VM2_Script* script);

/* OldSchool from rev 231: CC_CREATE_V2, handle arrays (the V2s). */
extern struct CS2VM2_Dialect const CS2VM2_DIALECT_OSRS;
/* OldSchool rev 230: CC_CREATE_V2, numbered arrays (the V1s). */
extern struct CS2VM2_Dialect const CS2VM2_DIALECT_OSRS_230;
/* OldSchool through rev 229: CC_CREATE_V1, numbered arrays. */
extern struct CS2VM2_Dialect const CS2VM2_DIALECT_OSRS_229;
/* RS2 on dat2, verified against the 634 client: CC_CREATE_V1, handle arrays,
 * SWITCH at 51, STRUCT_PARAM at 4500, and the RS2_ commands. */
extern struct CS2VM2_Dialect const CS2VM2_DIALECT_RS2_634;

#endif
