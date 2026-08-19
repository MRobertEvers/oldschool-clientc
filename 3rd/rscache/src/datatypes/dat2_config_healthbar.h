#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_HEALTHBAR_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_HEALTHBAR_H

#include "../rsbuffer.h"

/**
 * A healthbar type (config group 33): how an entity's overhead health bar is drawn.
 *
 * ## Where the field meanings come from
 *
 * The widths were derived from the caches before any reference existed (see below);
 * the *meanings* are the rev-239 client's own, read out of `class381`'s constructor
 * and the health-bar block of `drawEntities`. Four opcodes the client reads are absent
 * from every cache measured, so they have no field here — a record carrying one stops
 * the decode short rather than being guessed at:
 *
 * | Opcode | Client does | Here |
 * |---|---|---|
 * | 1 | reads a u16 and discards it | not decoded |
 * | 2 | list insert key | `draw_order` |
 * | 3 | eviction key | `evict_priority` |
 * | 4 | sets the fade threshold to 0 | not decoded |
 * | 5 | cycles the bar outlives its update | `persist_cycles` |
 * | 6 | reads a u8 and discards it | not decoded |
 * | 7 | front (filled) sprite | `front_sprite_id` |
 * | 8 | back (empty) sprite | `back_sprite_id` |
 * | 11 | fade threshold | `fade_threshold` |
 * | 14 | fill denominator | `width` |
 * | 15 | horizontal padding | not decoded |
 *
 * ### `width` is a denominator, not the pixel width
 *
 * Both, usually, and that is the trap. The client sizes the drawn bar from the *front
 * sprite's* pixel width and uses `width` only to scale the server's fill value into it
 * — `drawn = fill * sprite_width / width`. The two agree for almost every record,
 * which is why the sprite names carry the same number (`standard_shield_60` has
 * `width` 60), but `healthbar_9` pairs a 100px `headbar_olmtimer_100` with `width`
 * 120. A renderer that takes either one for the other is right 84 times out of 85.
 *
 * ## How the format was established, since no reference for it existed in-tree
 *
 * Client-TS is 2004-era dat1 and predates the type, so the widths were derived from
 * the caches in two steps.
 *
 * **Exact consumption alone is not enough here**, which is worth stating because the
 * rest of this library leans on it. Brute-forcing the seven observed opcodes over
 * operand widths 0-4 leaves **41 assignments that all consume 100%** of the 85
 * distinct records across three caches — the records are short with few opcodes, so
 * there is slack to re-segment them into a different but equally-consuming parse. Only
 * `7`, `11` and `14` are pinned by consumption.
 *
 * The second constraint settles it: **an operand value larger than the biggest sprite
 * id in its own cache cannot be an id, a width, a duration or a colour index** — it is
 * a wrong width being read across a field boundary. Applying that leaves **exactly
 * one** assignment of the 41, and it is the one a hand-parse of the shortest records
 * produces — and the one the client turned out to read.
 *
 * | Opcode | Width | Observed values |
 * |---|---|---|
 * | 2 | u8 | always 250 |
 * | 3 | u8 | always 250 |
 * | 5 | u16 | 0, 1, 15, 30, 31, 60, 300, 600 |
 * | 7 | u16 | ~78 distinct, all valid sprite ids |
 * | 8 | u16 | ~78 distinct, all valid sprite ids, adjacent to opcode 7's |
 * | 11 | u16 | 0, 40, 280 |
 * | 14 | u8 | 40 to 160, a multiple of ten |
 */
struct RSCache_Dat2ConfigHealthbar
{
    int id;

    /** Opcode 7. The filled half of the bar, drawn clipped to the current fill.
     *  Its pixel width is the bar's. -1 when absent. */
    int front_sprite_id;
    /** Opcode 8. The empty half, drawn underneath in full; normally
     *  `front_sprite_id` ± 1. -1 when absent. A bar needs both to draw as
     *  sprites at all -- with either missing the client falls back to a plain
     *  green/red rectangle `width` pixels across. */
    int back_sprite_id;

    /** Opcode 2, u8. Where the bar sorts among an entity's others. Always 250. */
    int draw_order;
    /** Opcode 3, u8. Which bar is dropped when an entity already holds four:
     *  the highest, so lower is more important. Always 250. */
    int evict_priority;
    /** Opcode 5, u16. Cycles (20ms) the bar stays up past the end of its
     *  update. Defaults to 70 in the client; the standard bar sets 300. */
    int persist_cycles;
    /** Opcode 11, u16. Where in `persist_cycles` the fade-out begins; the bar
     *  is drawn at `(remaining << 8) / (persist_cycles - fade_threshold)`
     *  alpha. -1 (the client's default, unrepresentable here) never fades. */
    int fade_threshold;
    /** Opcode 14, u8. The fill denominator -- the value a full bar arrives as.
     *  NOT the pixel width; see the header. Defaults to 30 in the client. */
    int width;

    /* Presence is tracked separately from value for every optional field, because 0 is
     * a value the wire carries explicitly — opcode 5 does — and an encoder keyed on
     * the value would drop the opcode and change the byte count. */
    bool has_draw_order;
    bool has_evict_priority;
    bool has_persist_cycles;
    bool has_fade_threshold;
    bool has_width;

    /** Bytes consumed. Equal to the record size for a fully understood record. */
    int _consumed;
};

/** Decode from a cursor, so back-to-back records in one buffer can be walked. */
void
RSCache_Dat2ConfigHealthbarDecode(
    struct RSCache_Dat2ConfigHealthbar* entry,
    struct RSCache_Buffer* buffer);

/**
 * Decode one dat2 record and record `_consumed`.
 *
 * Stops on an opcode it does not know rather than guessing a width, leaving
 * `_consumed` short of the record — the signal the round-trip harness asserts on. That
 * matters more here than for the settled types: the opcode set is complete for this
 * corpus, but a newer cache could carry an eighth opcode, and this is how it surfaces.
 */
void
RSCache_Dat2ConfigHealthbarDecodeInplace(
    struct RSCache_Dat2ConfigHealthbar* entry,
    const void* data,
    int data_size);

/**
 * Set the type's non-zero defaults on an already-zeroed record.
 *
 * Must run before any `DecodeOp` call: both sprite ids default to -1 because
 * sprite 0 is real and cannot double as "absent".
 */
void
RSCache_Dat2ConfigHealthbarInit(struct RSCache_Dat2ConfigHealthbar* entry);

/**
 * Handle one opcode, advancing `buffer`. True when consumed, false when unknown.
 *
 * The extension point: a server-side record that embeds this one calls this first
 * and handles only what comes back false. See `opcode_codec.h`.
 */
bool
RSCache_Dat2ConfigHealthbarDecodeOp(
    struct RSCache_Dat2ConfigHealthbar* entry,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags);

/** Byte-exact on every record in the corpus. Returns bytes written, or 0. */
uint32_t
RSCache_Dat2ConfigHealthbarEncode(
    const struct RSCache_Dat2ConfigHealthbar* entry,
    uint8_t* out,
    uint32_t out_capacity);

uint32_t
RSCache_Dat2ConfigHealthbarEncodeBound(const struct RSCache_Dat2ConfigHealthbar* entry);

#endif // RSCACHE_DATATYPES_DAT2_CONFIG_HEALTHBAR_H
