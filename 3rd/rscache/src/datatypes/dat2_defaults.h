#ifndef RSCACHE_DATATYPES_DAT2_DEFAULTS_H
#define RSCACHE_DATATYPES_DAT2_DEFAULTS_H

#include <stdint.h>

/*
 * The defaults table — OldSchool idx17, RS2 idx28. See docs/CACHE_INDEX_16_17.md
 * for how it was traced and what is still open about it.
 *
 * Two shapes live in this table and this header covers both:
 *
 *   group 3   one record, the ids the engine needs before it can draw. Decoded
 *             by `class11.method235` in the deob, which is the shape
 *             RSCache_Dat2Defaults mirrors.
 *   group 1   3164 files, each a list of colour stops. Never read by rev239 --
 *             the client declares the group (`class9.field57`) and nothing
 *             reads it -- but it ships, and it packs.
 *
 * ## Why the decode carries encoding detail it never uses
 *
 * These structs keep the opcode the ids arrived on, the order the opcodes came
 * in, and (for the sprite ids) nothing else -- because the point of decoding
 * this table is to write it back out again byte-for-byte. A record that repacks
 * to different bytes than it unpacked is a corrupt cache, not a tidier one, and
 * the fields below are exactly what re-encoding needs in order to not guess.
 *
 * Opcodes 2 and 6 write the same eleven ids; only `sprite_opcode` says which
 * one this record used. Callers that just want the ids can ignore it.
 *
 * The remaining ambiguity is bigsmart width -- a value under 32767 can legally
 * be written 2 bytes or 4 -- and that is deliberately *not* modelled here.
 * RSCache_Dat2DefaultsEncode emits the short form, and a caller that needs
 * certainty re-encodes and compares (see RSCache_Dat2DefaultsRoundTrips). Every
 * record in cache.osrs239 passes; one that did not would be declined rather
 * than silently rewritten.
 */

struct RSCache_Buffer;

/** The graphic-defaults sprite ids, in the order opcode 2 writes them. */
#define RSCACHE_DAT2_DEFAULTS_SPRITE_COUNT 11
#define RSCACHE_DAT2_DEFAULTS_RAMP_ROWS 3
#define RSCACHE_DAT2_DEFAULTS_RAMP_STOPS 5
#define RSCACHE_DAT2_DEFAULTS_MODEL_COUNT 2
/** Opcodes 0..6, so the order list cannot need more than six entries. */
#define RSCACHE_DAT2_DEFAULTS_MAX_OPCODES 6

/**
 * Slot names for the eleven ids, indexed by position.
 *
 * The record stores ids and no names -- index 17's reference table has the name
 * bit clear -- so this array is the mapping from position to meaning, recovered
 * by resolving osrs239's ids against the sprite table's own name hashes. It is
 * the same list as src/engine/static_sprites.c, minus the three dat1-era slots
 * that tree carries and this record does not have.
 */
extern const char* const RSCache_Dat2DefaultsSpriteSlotNames[RSCACHE_DAT2_DEFAULTS_SPRITE_COUNT];

struct RSCache_Dat2Defaults
{
    /** Sprite ids, or -1 for a slot the record leaves unset. */
    int sprite_ids[RSCACHE_DAT2_DEFAULTS_SPRITE_COUNT];
    /** 2 or 6 -- which opcode carried the ids -- or 0 when neither appeared. */
    int sprite_opcode;
    /** Opcode 6's trailing bigsmart. The client reads it and drops it. */
    int sprite_trailer;

    /** Opcode 1's 24-bit value. The client reads it and drops it. */
    int legacy_value;
    int has_legacy_value;

    /** Opcode 3: three 5-stop 24-bit RGB ramps. */
    int ramps[RSCACHE_DAT2_DEFAULTS_RAMP_ROWS][RSCACHE_DAT2_DEFAULTS_RAMP_STOPS];
    int has_ramps;

    /** Opcode 5: two model ids, int32. */
    int model_ids[RSCACHE_DAT2_DEFAULTS_MODEL_COUNT];
    int has_models;

    /** The opcodes in the order they appeared, so an encode can replay them. */
    uint8_t opcode_order[RSCACHE_DAT2_DEFAULTS_MAX_OPCODES];
    int opcode_count;

    /** Bytes the decode consumed, which for a well-formed record is all of them. */
    int consumed;
};

/**
 * A group-1 record: colour stops with an interval between consecutive stops.
 *
 *     colour(3) [ interval(1) colour(3) ]*
 *
 * so the encoded size is always `4 * stop_count - 1`. osrs239 holds 3152
 * one-stop records, 11 two-stop and one sixteen-stop.
 */
#define RSCACHE_DAT2_DEFAULTS_COLOUR_MAX_STOPS 64

struct RSCache_Dat2DefaultsColours
{
    int stop_count;
    /** 24-bit RGB, `stop_count` entries. */
    int colours[RSCACHE_DAT2_DEFAULTS_COLOUR_MAX_STOPS];
    /** `stop_count - 1` entries; intervals[i] sits between colour i and i+1. */
    int intervals[RSCACHE_DAT2_DEFAULTS_COLOUR_MAX_STOPS - 1];
};

/**
 * Decode the group-3 record.
 *
 * Returns 1 on success, 0 when the bytes are not this record -- an unknown
 * opcode, a truncated payload, or trailing bytes after the terminator. A caller
 * that gets 0 should write the payload raw rather than describe it wrongly.
 */
int
RSCache_Dat2DefaultsDecode(
    const uint8_t* data,
    int data_size,
    struct RSCache_Dat2Defaults* out);

/** Bytes RSCache_Dat2DefaultsEncode needs at most. */
uint32_t
RSCache_Dat2DefaultsEncodeBound(const struct RSCache_Dat2Defaults* defaults);

/** Returns bytes written, or 0 if `out_capacity` was not enough. */
uint32_t
RSCache_Dat2DefaultsEncode(
    const struct RSCache_Dat2Defaults* defaults,
    uint8_t* out,
    uint32_t out_capacity);

/** 1 when `defaults` re-encodes to exactly `data[0..data_size)`. */
int
RSCache_Dat2DefaultsRoundTrips(
    const struct RSCache_Dat2Defaults* defaults,
    const uint8_t* data,
    int data_size);

/** Decode a group-1 colour record. Returns 1, or 0 when the size is not 4n-1. */
int
RSCache_Dat2DefaultsColoursDecode(
    const uint8_t* data,
    int data_size,
    struct RSCache_Dat2DefaultsColours* out);

uint32_t
RSCache_Dat2DefaultsColoursEncodeBound(const struct RSCache_Dat2DefaultsColours* colours);

uint32_t
RSCache_Dat2DefaultsColoursEncode(
    const struct RSCache_Dat2DefaultsColours* colours,
    uint8_t* out,
    uint32_t out_capacity);

int
RSCache_Dat2DefaultsColoursRoundTrips(
    const struct RSCache_Dat2DefaultsColours* colours,
    const uint8_t* data,
    int data_size);

#endif
