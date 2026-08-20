#ifndef TORIRSSERVER_SERVERCODEC_H
#define TORIRSSERVER_SERVERCODEC_H

/*
 * The server band of a config record, as bytes.
 *
 * ## What this is for
 *
 * A cache npc record says what the *client* needs — models, name, size,
 * animations. It says nothing about hitpoints, respawn rate or hunt mode,
 * because no client opcode exists for them. A cache loc record says what a door
 * looks like and nothing about which loc it opens into. Those reached the server
 * by being re-parsed out of text at every boot, which is the problem
 * `docs/CONTENT_ARCHITECTURE.md` §3.5 describes: two tools write a derived cache
 * and they do not compose.
 *
 * This is the other half of the pair. `cachepack` writes a client cache and a
 * *server pack*; the server reads both. The client record seeds every field it
 * knows, then this stream overrides per opcode present — which is what makes
 * "absent" and "present and zero" different states, the distinction a text
 * overlay cannot express.
 *
 * ## One codec, not one per type
 *
 * Everything below is generic over a `(field table, record base)` pair. There is
 * no `ToriRSServer_ServerNpcEncode`, and that is the point: adding `obj` or `prayer`
 * must be a `fields/<type>.ini` plus a table of offsets, never a second copy of
 * this file. A per-type codec is how the two directions come to disagree — and
 * here they would disagree twice over, once between encode and decode and once
 * between the copy and the original.
 *
 * The generic shape is affordable because the band is deliberately narrow: fixed
 * widths, no strings, no nested lists. A *client* record cannot be handled this
 * way — `RSCache_OpcodeCodec` is whole-record precisely because the encoders must
 * choose an opcode order that is load-bearing (`opcode_codec.h`, "The asymmetry
 * is real"). Ours is not: the reader dispatches per opcode, so the writer sorts
 * by opcode purely so two packs of the same content are byte-identical.
 *
 * ## Why the opcodes are not in this file
 *
 * They are declared in `fields/<type>.ini` (`server = opcode:<n>:<wire>`) and read
 * through the field register. A number written here as well would be a second
 * copy that can drift from the one the packer uses, and the two disagreeing
 * means a record that encodes under one opcode and decodes under another — with
 * no error, because both are valid streams. The register is the single source;
 * this file holds only the mapping from opcode to *field*, which is the part C
 * has to know. `ToriRSServer_ServerCodecTest` walks every registered type against the
 * register, so a type added without a register row fails the build.
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

#include "torirs_server_content.h"

#include <stddef.h>
#include <stdint.h>

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
    /** Opcode, 64..255. Must match `server = opcode:<n>:<wire>` in the register. */
    int opcode;
    enum ServerWire wire;
    /** Byte offset of the `int` field inside the record struct. */
    size_t offset;
    /** For the report and for the register cross-check. */
    const char* name;
};

/**
 * One record type's band.
 *
 * `name` is the register's own spelling — `npc` resolves `fields/npc.ini` — so the
 * cross-check needs no second table mapping one to the other.
 */
struct ServerType
{
    const char* name;
    const struct ServerField* fields;
    int count;
    /** `sizeof` the record struct, so a caller can allocate one without naming
     *  it — the same reason `RSCache_OpcodeCodec` carries it. */
    size_t record_size;
};

/**
 * Every type with a server band.
 *
 * Exposed as a list rather than as a lookup per type so the test can *iterate*.
 * A registry a test walks is a registry a new row cannot silently join without
 * its register file: the check is "every registered type agrees with its
 * `fields/<name>.ini`", which is only a real check if nothing has to remember to
 * add the type to the test as well.
 */
const struct ServerType*
ToriRSServer_ServerTypes(int* out_count);

/** By register name (`npc`, `loc`), or NULL. */
const struct ServerType*
ToriRSServer_ServerTypeFor(const char* name);

/**
 * Encode the server-only fields of `record` into `out`.
 *
 * **Sparse by design.** A field at its engine default is omitted, so a record
 * that states nothing encodes to a single terminator byte. That is what keeps
 * the pack proportional to what content authors actually wrote — 38 npcs in this
 * tree, not the cache's 16,292 — and it is why "absent" has to stay
 * distinguishable from "zero".
 *
 * `defaults` is what a field is compared against to decide whether to emit it —
 * normally `ToriRSServer_ContentNpcDefault()`. Passed rather than looked up so the
 * comparison is visible at the call site and so this file links without the
 * content loader behind it. **Never compared against zero:** `death_drop`
 * defaults to -1 and obj 0 is a real obj, so a zero-compare would emit the field
 * for every npc that drops nothing and omit it for the one that drops obj 0.
 *
 * Returns bytes written, or 0 if `out_capacity` is too small.
 */
uint32_t
ToriRSServer_ServerEncode(
    const struct ServerType* type,
    const void* record,
    const void* defaults,
    uint8_t* out,
    uint32_t out_capacity);

/** An upper bound on what `ToriRSServer_ServerEncode` will write for `type`. */
uint32_t
ToriRSServer_ServerEncodeBound(const struct ServerType* type);

/**
 * Decode a server-band stream over `record`.
 *
 * `record` is expected to arrive already seeded — from the cache record and the
 * engine defaults — and this overrides only the fields the stream states. That
 * ordering is the precedence rule: **the server pack wins for any opcode
 * present; the seed supplies every opcode absent.**
 *
 * Returns bytes consumed, or -1 on failure. A return short of `size` means an
 * opcode this build does not know — the same signal every rscache decoder uses,
 * and never a silent skip, because an unknown opcode's payload width is unknown.
 */
int
ToriRSServer_ServerDecode(
    const struct ServerType* type,
    void* record,
    const uint8_t* src,
    int size);

#endif
