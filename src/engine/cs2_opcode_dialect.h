#ifndef CS2_OPCODE_DIALECT_H
#define CS2_OPCODE_DIALECT_H

#include "cs2vm2/cs2vm2_dialect.h"

#include <rscache.h>
#include <stdint.h>

/*
 * Which CS2 dialect a cache speaks (cs2vm2/cs2vm2_dialect.h).
 *
 * The VM runs canonical instructions (cs2vm2/cs2_opcode.h). Each dialect is a
 * generated table from the wire ids its caches spell to those instructions
 * (tools/cs2_gen_opcodes/canonical.py), applied once when an RSCache_CS2_Script
 * becomes a CS2VM2_Script (CS2VM2_ScriptResolveDialect). The cache layer keeps
 * the raw wire opcodes so ClientScriptEncode stays byte-exact.
 *
 * Audit of Class66.method711 (rev 634 client) vs the OldSchool catalogue,
 * opcodes 0..99:
 *
 * Identical in both eras:
 *   0, 1, 2, 3, 6, 7, 8, 9, 10, 21, 25, 27, 31, 32, 33, 34, 35, 36, 37, 38, 39,
 *   40, 42, 43, 44, 45, 46
 *
 * True collision -- the RS2 table maps it elsewhere:
 *   51  634 = SWITCH (OldSchool 60); OSRS = PUSH_VARC_LONG
 *
 * Same id, same meaning, missing VM dispatch (not a collision):
 *   47 / 48  PUSH/POP_VARC_STRING_OLD  (identical to 49/50; 634 has no 49/50)
 *
 * 634-only, free in the OldSchool numbering (claimed as BRANCH_IF_ONE):
 *   86  pop int; branch by operand if value == 1
 *
 * OldSchool-only, never emitted by 634 (no action):
 *   49, 50, 52, 60, 61, 62, 63, 66..73, 74, 76
 *
 * Command range (>= 100): audited against the 634 client's dispatcher. A
 * renumbered command (STRUCT_PARAM at 4500) and every id that names a
 * different command at 634 (the RS2_ section of cs2_opcode.h, mostly
 * stubbed signatures, RS2_CC_GETPARAM with real behaviour) are rows of the
 * RS2 table; CC_CREATE is CC_CREATE_V1 there.
 * The full list is docs/RS2_634_CLIENT_REFERENCES.md section 3.
 */

/** Pick the dialect for a cache profile: RS2 on dat2 -> RS2_634; OldSchool
 *  through 229 -> OSRS_229, 230 -> OSRS_230; any other (and an OldSchool cache
 *  of unknown revision) -> OSRS, the native one. */
struct CS2VM2_Dialect const*
CS2_DialectForCache(const struct RSCache* cache);

#endif
