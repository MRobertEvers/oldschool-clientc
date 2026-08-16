#ifndef CS2_OPCODE_DIALECT_H
#define CS2_OPCODE_DIALECT_H

#include <rscache.h>
#include <stdint.h>

/*
 * CS2 wire-opcode dialects.
 *
 * The VM (cs2_opcode.h) always speaks the canonical (OldSchool / RuneStar) numbering.
 * RS2-on-dat2 caches emit a different numbering for a handful of core opcodes; those
 * are translated to canonical ids when an RSCache_CS2_Script becomes a CS2VM2_Script.
 * The cache layer keeps the raw wire opcodes so ClientScriptEncode stays byte-exact.
 *
 * Audit of Class66.method711 (rev 634 client) vs cs2_opcode_meta.c, opcodes 0..99:
 *
 * Identical in both eras (no translation):
 *   0, 1, 2, 3, 6, 7, 8, 9, 10, 21, 25, 27, 31, 32, 33, 34, 35, 36, 37, 38, 39,
 *   40, 42, 43, 44, 45, 46
 *
 * True collision — needs translation under RS2_DAT2:
 *   51  634 = SWITCH (canonical 60); OSRS = GET_VARC_LONG
 *
 * Same id, same meaning, missing VM dispatch (not a collision):
 *   47 / 48  PUSH/POP_VARC_STRING_OLD  (identical to 49/50; 634 has no 49/50)
 *
 * 634-only, free in the canonical space (claim as BRANCH_IF_ONE):
 *   86  pop int; branch by operand if value == 1
 *
 * Canonical-only, never emitted by 634 (no action):
 *   49, 50, 52, 60, 61, 62, 63, 66..73, 74, 76
 *
 * Command range (>= 100): audited against the 634 client's dispatcher and split
 * three ways — renumbered ids translate here, ids that name a *different* command
 * under RS2 divert to a stubbed signature (g_cs2vm2_opcode_stack_rs2 in
 * cs2vm2.c), and two need real behaviour (CC_CREATE's arity, CC_GETPARAM).
 * The full list is docs/RS2_634_CLIENT_REFERENCES.md section 3.
 */

enum CS2_OpcodeDialect
{
    CS2_OPCODE_DIALECT_CANONICAL = 0, /* cs2_opcode.h numbering (OldSchool) */
    CS2_OPCODE_DIALECT_RS2_DAT2,      /* RS2 on dat2; verified against the 634 client */
};

/** Pick the dialect for a cache profile. RS2-on-dat2 -> RS2_DAT2, else CANONICAL. */
enum CS2_OpcodeDialect
CS2_OpcodeDialectForCache(const struct RSCache* cache);

/**
 * Map a wire opcode from `dialect` into the canonical numbering the VM speaks.
 * Identity under CANONICAL; under RS2_DAT2 remaps 51 -> CS2_OP_SWITCH (60).
 */
uint16_t
CS2_OpcodeTranslate(
    enum CS2_OpcodeDialect dialect,
    uint16_t wire_opcode);

#endif
