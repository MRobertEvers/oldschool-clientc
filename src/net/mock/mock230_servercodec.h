#ifndef MOCK230_SERVERCODEC_H
#define MOCK230_SERVERCODEC_H

/*
 * The server band of an npc record, as bytes.
 *
 * ## What this is for
 *
 * A cache npc record says what the *client* needs — models, name, size,
 * animations. It says nothing about hitpoints, respawn rate or hunt mode,
 * because no client opcode exists for them. Today those reach the server by
 * being re-parsed out of text at every boot, which is the problem
 * `docs/CONTENT_ARCHITECTURE.md` §3.5 describes: two tools write a derived cache
 * and they do not compose.
 *
 * This is the other half of the pair. `cachepack` writes a client cache and a
 * *server pack*; the server reads both. The client record seeds every field it
 * knows, then this stream overrides per opcode present — which is what makes
 * "absent" and "present and zero" different states, the distinction a text
 * overlay cannot express.
 *
 * ## Why the opcodes are not in this file
 *
 * They are declared in `fields/npc.ini` (`server = opcode:<n>:<wire>`) and read
 * through the field register. A number written here as well would be a second
 * copy that can drift from the one the packer uses, and the two disagreeing
 * means a record that encodes under one opcode and decodes under another — with
 * no error, because both are valid streams. The register is the single source;
 * this file holds only the mapping from opcode to *field*, which is the part C
 * has to know.
 *
 * ## The band
 *
 * 64..255. Client npc opcodes run 1..147, so a server record can never be
 * mistaken for a client one and a client decoder fed this stream stops at the
 * first byte it does not know rather than misreading it — the property
 * `test_opcode_codec`'s subclass check proves for the interface as a whole.
 *
 * This is stricter than LostCity, whose server band includes opcode 18 and
 * overlaps the client's at opcode 1. That overlap is fine there because its
 * server record is the *only* npc record; ours is an overlay on a decoded cache
 * record, so keeping the bands disjoint is what lets one stream be read safely
 * by either side.
 */

#include "mock230_content.h"

#include <stddef.h>
#include <stdint.h>

/**
 * Encode the server-only fields of `def` into `out`.
 *
 * **Sparse by design.** A field at its engine default is omitted, so a record
 * that states nothing encodes to a single terminator byte. That is what keeps
 * the pack proportional to what content authors actually wrote — 38 npcs in this
 * tree, not the cache's 16,292 — and it is why "absent" has to stay
 * distinguishable from "zero".
 *
 * `defaults` is what a field is compared against to decide whether to emit it —
 * normally `mock230_content_npc_default()`. Passed rather than looked up so the
 * comparison is visible at the call site and so this file links without the
 * content loader behind it.
 *
 * Returns bytes written, or 0 if `out_capacity` is too small.
 */
uint32_t
Mock230_ServerNpcEncode(
    const struct Mock230NpcDef* def,
    const struct Mock230NpcDef* defaults,
    uint8_t* out,
    uint32_t out_capacity);

/** An upper bound on what `Mock230_ServerNpcEncode` will write. */
uint32_t
Mock230_ServerNpcEncodeBound(const struct Mock230NpcDef* def);

/**
 * Decode a server-band stream over `def`.
 *
 * `def` is expected to arrive already seeded — from the cache record and the
 * engine defaults — and this overrides only the fields the stream states. That
 * ordering is the precedence rule: **the server pack wins for any opcode
 * present; the seed supplies every opcode absent.**
 *
 * Returns bytes consumed, or -1 on failure. A return short of `size` means an
 * opcode this build does not know — the same signal every rscache decoder uses,
 * and never a silent skip, because an unknown opcode's payload width is unknown.
 */
int
Mock230_ServerNpcDecode(
    struct Mock230NpcDef* def,
    const uint8_t* src,
    int size);

/** Declared wire widths, matching `server = opcode:<n>:<wire>` in the register. */
enum ServerWire
{
    WIRE_U1 = 1,
    WIRE_U2 = 2,
    WIRE_U4 = 4,
};

/** One server field: which opcode carries it, how wide, and where it lands. */
struct ServerField
{
    /** Opcode, 64..255. Must match `server = opcode:<n>:<wire>` in fields/npc.ini. */
    int opcode;
    enum ServerWire wire;
    /** Byte offset of the `int` field inside struct Mock230NpcDef. */
    size_t offset;
    /** For the report and for the register cross-check. */
    const char* name;
};

/**
 * The opcode table, for the register cross-check.
 *
 * Exposed only so `mock230_servercodec_test` can hold it against
 * `fields/npc.ini`. Nothing in the server should read it: the whole point is
 * that the register is the single source and this is generated from it.
 */
const struct ServerField*
Mock230_ServerNpcFields(int* out_count);

#endif
