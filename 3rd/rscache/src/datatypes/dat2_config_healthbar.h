#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_HEALTHBAR_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_HEALTHBAR_H

#include "../rsbuffer.h"

/**
 * A healthbar type (config group 33): how an entity's overhead health bar is drawn.
 *
 * ## How this format was established, since no reference for it exists in-tree
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
 * produces:
 *
 * | Opcode | Width | Observed values |
 * |---|---|---|
 * | 2 | u8 | always 250 |
 * | 3 | u8 | always 250 |
 * | 5 | u16 | 0, 1, 15, 30, 31 |
 * | 7 | u16 | ~78 distinct, all valid sprite ids |
 * | 8 | u16 | ~78 distinct, all valid sprite ids, adjacent to opcode 7's |
 * | 11 | u16 | 0, 40, 280 |
 * | 14 | u8 | 40, 50, 60, 70, 80 |
 *
 * ## Why most fields are named after their opcode
 *
 * The widths are settled; the *meanings* are not, and inventing names for them would
 * be a guess dressed up as knowledge. Only opcodes 7 and 8 have an established role —
 * their values are sprite ids, verified against each cache's own sprite table, and
 * they come in adjacent pairs (0x0587/0x0588, 0x0880/0x0881), which is what a
 * front-and-back bar pair looks like. Which of the two is the front is *not*
 * established, so they are `sprite_id_a` and `sprite_id_b` rather than a coin flip.
 *
 * This costs nothing for round-tripping and for reading the sprites out, which is what
 * a renderer needs. If a reference turns up, renaming the fields is mechanical.
 */
struct RSCache_Dat2ConfigHealthbar
{
    int id;

    /** Opcode 7. A sprite id; pairs with `sprite_id_b`. -1 when absent. */
    int sprite_id_a;
    /** Opcode 8. A sprite id, normally `sprite_id_a` ± 1. -1 when absent. */
    int sprite_id_b;

    /** Opcode 2, u8. Always 250 in every cache measured. */
    int opcode_2;
    /** Opcode 3, u8. Always 250 in every cache measured. */
    int opcode_3;
    /** Opcode 5, u16. Small: 0, 1, 15, 30 or 31. */
    int opcode_5;
    /** Opcode 11, u16. 0, 40 or 280. */
    int opcode_11;
    /** Opcode 14, u8. A multiple of ten, 40 to 80. */
    int opcode_14;

    /* Presence is tracked separately from value for every optional field, because 0 is
     * a value the wire carries explicitly — opcode 5 does — and an encoder keyed on
     * the value would drop the opcode and change the byte count. */
    bool has_opcode_2;
    bool has_opcode_3;
    bool has_opcode_5;
    bool has_opcode_11;
    bool has_opcode_14;

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

/** Byte-exact on every record in the corpus. Returns bytes written, or 0. */
uint32_t
RSCache_Dat2ConfigHealthbarEncode(
    const struct RSCache_Dat2ConfigHealthbar* entry,
    uint8_t* out,
    uint32_t out_capacity);

uint32_t
RSCache_Dat2ConfigHealthbarEncodeBound(const struct RSCache_Dat2ConfigHealthbar* entry);

#endif // RSCACHE_DATATYPES_DAT2_CONFIG_HEALTHBAR_H
