#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_SOUNDSCAPE_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_SOUNDSCAPE_H

#include "../rsbuffer.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Ambient soundscape (dat2 config kind 15) — the region's background bed.
 *
 * This is what an `AMBIENTSOUND_START` id names. The packet carries a flag and a
 * two-byte id, and the reference resolves that id through *this* type rather
 * than treating it as a sound-effect id:
 *
 *     boolean fade = g1() == 1;
 *     int id       = g2();
 *     class410 s   = load(configArchive, group 15, file id);   // this record
 *     player.set(s, fade, <ambient volume setting>);
 *
 * A record is not one sound. It is a set of continuous loops that all play at
 * once, plus up to eight independent *random sets*, each of which fires one shot
 * from its own list every few seconds on its own timer. That is what makes a
 * cave sound like a cave: a low drone under occasional drips at unrelated
 * intervals.
 *
 * Weighting is done by repetition. A set that lists a sound three times and
 * silence six times plays that sound a third of the time; there is no weight
 * field. `cache.osrs239` record 1 does exactly this with sound 2411 as the
 * filler.
 *
 * ## Era
 *
 * The group is an OldSchool 231..239 addition. `cache.osrs230` has no group 15
 * at all and `cache.osrs239` has eight records, so a decoder for it must not be
 * reached on an older cache — the group id is reused for varclient strings in
 * the config-kind numbering other games use.
 */

/** Reference caps: `field5221` is `new ArrayList(8)`, and a set with more than
 *  48 ids is skipped rather than truncated. */
#define RSCACHE_SOUNDSCAPE_MAX_SETS 8
#define RSCACHE_SOUNDSCAPE_MAX_SET_IDS 48

/** Opcode-order slots: at most one each of 1/3/4 plus eight sets, rounded up. */
#define RSCACHE_SOUNDSCAPE_MAX_OPCODES 16

/** One timed set: pick a random id from `ids`, play it once, wait, repeat. */
struct RSCache_SoundscapeSet
{
    int* ids;
    int id_count;
    /** Gap bounds in **milliseconds**. The wire carries client ticks and the
     *  reference multiplies by 20 as it reads, so the conversion is part of the
     *  format rather than something the caller should redo. */
    int min_ms;
    int max_ms;
};

struct RSCache_Dat2ConfigSoundscape
{
    int id;

    /** Continuous loops; every one of them plays for as long as the soundscape
     *  is active. Opcode 1. */
    int* loop_ids;
    int loop_count;

    struct RSCache_SoundscapeSet sets[RSCACHE_SOUNDSCAPE_MAX_SETS];
    int set_count;

    /** Fade curve ids and durations in ms (opcodes 3 and 4). The curve is an
     *  easing enum shared with the loc ambient fades; 0 is linear. */
    int fade_in_curve;
    int fade_in_ms;
    int fade_out_curve;
    int fade_out_ms;

    /**
     * The opcodes in the order the record wrote them, so a re-encode can put
     * them back the same way.
     *
     * This is not tidiness. Four of `cache.osrs239`'s eight records emit their
     * random sets *before* the continuous-loop opcode, so an encoder that
     * assumes ascending order differs from the original at byte 0 -- and two of
     * them carry a fade opcode whose curve and duration are both zero, which is
     * indistinguishable from "absent" unless the order records that it was
     * there. `RSCache_SoundBank.order[]` exists for the same reason and was the
     * precedent for this.
     *
     * Empty for a record built in memory rather than decoded; the encoder then
     * writes a canonical 1, 2.., 3, 4 order.
     */
    uint8_t order[RSCACHE_SOUNDSCAPE_MAX_OPCODES];
    int order_count;

    /**
     * Payload the decode read and then threw away, which makes the record
     * un-re-encodable. The encoder reports that by refusing, rather than
     * writing a shorter record that looks fine.
     *
     * `dropped_sets` counts sets past one of the reference's caps -- no cache
     * record does this. `superseded_loops` counts opcode-1 lists overwritten by
     * a later one: the reference *assigns* the loop list rather than appending,
     * so a record carrying opcode 1 twice keeps only the last, and the first
     * list is gone before any caller sees the record. `cache.osrs239` record 6
     * is exactly that (`01 01 2d 67  01 01 2d 5b ...`, ids 11623 then 11611),
     * and it is the only record in either cache that does not round trip.
     */
    int dropped_sets;
    int superseded_loops;

    /** Bytes consumed, set on reaching the terminating opcode 0. Zero when the
     *  decode stopped early, the same diagnostic the other config types carry. */
    int _consumed;
};

void
RSCache_Dat2ConfigSoundscapeDecodeInplace(
    struct RSCache_Dat2ConfigSoundscape* entry,
    const void* data,
    int data_size);

/** Decode into a fresh allocation. NULL on allocation failure. */
struct RSCache_Dat2ConfigSoundscape*
RSCache_Dat2ConfigSoundscapeNewDecode(const void* data, int data_size);

void
RSCache_Dat2ConfigSoundscapeFreeInplace(struct RSCache_Dat2ConfigSoundscape* entry);

void
RSCache_Dat2ConfigSoundscapeFree(struct RSCache_Dat2ConfigSoundscape* entry);

/**
 * Encode `entry` back to the wire.
 *
 * Byte-exact for 7 of `cache.osrs239`'s 8 records; the eighth carries opcode 1
 * twice and the decode discards the superseded list, so it cannot be rebuilt
 * (see `superseded_loops`). Returns bytes written, or 0 -- which also means
 * "this record is not re-encodable", not only "the buffer was too small".
 */
uint32_t
RSCache_Dat2ConfigSoundscapeEncode(
    const struct RSCache_Dat2ConfigSoundscape* entry,
    uint8_t* out,
    uint32_t out_capacity);

/** An upper bound on what the encoder will write. */
uint32_t
RSCache_Dat2ConfigSoundscapeEncodeBound(const struct RSCache_Dat2ConfigSoundscape* entry);

#endif
