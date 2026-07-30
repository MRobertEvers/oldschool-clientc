#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_HITSPLAT_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_HITSPLAT_H

#include "../rsbuffer.h"

/** Most opcodes one hitsplat record carries. Observed maximum is 5. */
#define RSCACHE_HITSPLAT_MAX_OPCODES 12
/** Bytes in opcode 18's composite payload. */
#define RSCACHE_HITSPLAT_OPCODE_18_BYTES 11

/**
 * A hitsplat type (config group 32): how a damage splat is drawn.
 *
 * ## Establishing the format, and a correction
 *
 * This was first recorded as "not a fixed-width opcode stream, needs a reference".
 * Half right. Brute-forcing every assignment of operand widths 0-4 over 243 distinct
 * records really does yield **zero** that consume the file — but the conclusion drawn
 * from that was wrong. The obstruction is a single opcode:
 *
 *   **Opcode 18 carries an 11-byte composite payload**, which a search over widths 0-4
 *   cannot express, so it makes every assignment fail rather than pointing at itself.
 *
 * Hand-parsing the extremes found it. The shortest record is unambiguous —
 * `05 0d c1 | 08 00 00 | 09 00 96 | 00` is three u16 opcodes and a terminator — and the
 * longest, `09 00 32 | 12 <11 bytes> | 00`, only balances if opcode 18 (0x12) consumes
 * eleven. Re-running the search with opcode 18 allowed up to 16 bytes wide leaves four
 * assignments; requiring every *scalar* operand to be no larger than its own cache's
 * biggest sprite id — the same constraint that settled healthbar — leaves exactly one:
 *
 * | Opcode | Width | Observed values |
 * |---|---|---|
 * | 5 | u16 | 49 distinct, 1105-4770, **all valid sprite ids** |
 * | 8 | u16 | 0 or 37 |
 * | 9 | u16 | 7, 16, 30, 50, 150 — present on every record |
 * | 11 | flag, no operand | 2 records |
 * | 13 | u16 | 1 or 2 |
 * | 18 | 11-byte composite | 32 distinct |
 * | 49 | u8 | always 0 |
 *
 * Verified at 243/243 records consuming exactly.
 *
 * ## Opcode order is per record, so it is recorded
 *
 * Unlike healthbar, hitsplat records do **not** share one packing order:
 *
 * ```
 *   5, 8, 9          5, 8, 11, 9        8, 49, 5, 9        8, 49, 5, 9, 13        9, 18
 * ```
 *
 * These look like distinct kinds of splat with distinct field sets, each with its own
 * order. Rather than guess a rule, the order is kept in `opcodes` and replayed — the
 * same approach the model encoder takes for its per-face index types. An encoder given
 * no order falls back to ascending, which round-trips semantically but not byte-wise.
 *
 * ## Opcode 18's payload is carried, not split
 *
 * Its 11 bytes are kept raw. The apparent structure is
 * `u16, i16, u16, u8, u16, u16` — bytes 2-3 are `ff ff` on every record, which is what
 * an i16 `-1` sentinel looks like, and the last three fields track each other closely
 * (`…0007 01 0007 0007`, `…001a 01 001a 001a`). That is suggestive but not established,
 * and splitting on an unverified boundary would bake a guess into the API for no gain:
 * round-tripping needs the bytes, not their names.
 */
struct RSCache_Dat2ConfigHitsplat
{
    int id;

    /** Opcode 5. A sprite id — the splat graphic. -1 when absent. */
    int sprite_id;

    /** Opcode 8, u16. */
    int opcode_8;
    /** Opcode 9, u16. Present on every record measured. */
    int opcode_9;
    /** Opcode 11. A bare flag with no operand. */
    int opcode_11;
    /** Opcode 13, u16. */
    int opcode_13;
    /** Opcode 49, u8. Always 0 in every cache measured. */
    int opcode_49;

    bool has_opcode_8;
    bool has_opcode_9;
    bool has_opcode_13;
    bool has_opcode_49;

    /** Opcode 18's payload, verbatim. Valid only when `has_opcode_18`. */
    uint8_t opcode_18[RSCACHE_HITSPLAT_OPCODE_18_BYTES];
    bool has_opcode_18;

    /**
     * The opcodes in the order the record carried them, which differs per record.
     * Replayed by the encoder to reproduce the bytes; see the note above.
     */
    uint8_t opcodes[RSCACHE_HITSPLAT_MAX_OPCODES];
    int opcode_count;

    /** Bytes consumed. Equal to the record size for a fully understood record. */
    int _consumed;
};

/** Decode from a cursor, so back-to-back records in one buffer can be walked. */
void
RSCache_Dat2ConfigHitsplatDecode(
    struct RSCache_Dat2ConfigHitsplat* entry,
    struct RSCache_Buffer* buffer);

/**
 * Set the type's non-zero defaults on an already-zeroed record.
 *
 * Must run before any `DecodeOp` call. `sprite_id` defaults to -1 because sprite
 * 0 is a real sprite and cannot double as "absent"; a record decoded without this
 * draws splat sprite 0 instead of none, with nothing else to show for it.
 */
void
RSCache_Dat2ConfigHitsplatInit(struct RSCache_Dat2ConfigHitsplat* entry);

/**
 * Handle one opcode, advancing `buffer`. True when consumed, false when unknown.
 *
 * The extension point: a server-side record that embeds this one calls this first
 * and handles only what comes back false. See `opcode_codec.h`.
 */
bool
RSCache_Dat2ConfigHitsplatDecodeOp(
    struct RSCache_Dat2ConfigHitsplat* entry,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags);

/** Decode one dat2 record and record `_consumed`. Stops on an unknown opcode rather
 *  than guessing its width, leaving `_consumed` short — the harness asserts on that. */
void
RSCache_Dat2ConfigHitsplatDecodeInplace(
    struct RSCache_Dat2ConfigHitsplat* entry,
    const void* data,
    int data_size);

/** Byte-exact when `opcodes` holds the source order. Returns bytes written, or 0. */
uint32_t
RSCache_Dat2ConfigHitsplatEncode(
    const struct RSCache_Dat2ConfigHitsplat* entry,
    uint8_t* out,
    uint32_t out_capacity);

uint32_t
RSCache_Dat2ConfigHitsplatEncodeBound(const struct RSCache_Dat2ConfigHitsplat* entry);

#endif // RSCACHE_DATATYPES_DAT2_CONFIG_HITSPLAT_H
