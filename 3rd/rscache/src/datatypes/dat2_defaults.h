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

/**
 * Group ids inside the table. The client's own register (`class9` in the deob)
 * declares exactly these two and nothing else.
 */
#define RSCACHE_DAT2_DEFAULTS_GROUP_COLOURS 1
#define RSCACHE_DAT2_DEFAULTS_GROUP_RECORD 3
/** Group 3 holds a single file, at file id 0. */
#define RSCACHE_DAT2_DEFAULTS_RECORD_FILE 0

/** The graphic-defaults sprite ids, in the order opcode 2 writes them. */
#define RSCACHE_DAT2_DEFAULTS_SPRITE_COUNT 11
#define RSCACHE_DAT2_DEFAULTS_RAMP_ROWS 3
#define RSCACHE_DAT2_DEFAULTS_RAMP_STOPS 5
#define RSCACHE_DAT2_DEFAULTS_MODEL_COUNT 2
/** Opcodes 0..6, so the order list cannot need more than six entries. */
#define RSCACHE_DAT2_DEFAULTS_MAX_OPCODES 6

/**
 * What each slot's id was called *in index 8*, at rev239. Commentary only.
 *
 * Read the qualifiers, because they are the whole content of this array:
 *
 * - The defaults record stores no names. Opcode 2 writes eleven integers and
 *   index 17's reference table has the name bit clear, so there is nothing in
 *   that table to recover a name from.
 * - These names are the *sprite table's*, and they are real: index 8 carries a
 *   name hash per group (flags 0x5) and djb2 of every one of these eleven
 *   matches its stored hash exactly. They are recovered, not authored.
 * - But index 8 names the sprite a slot points at, not the slot. For rev239 the
 *   two coincide. A cache that pointed slot 0 somewhere else would still have a
 *   compass slot at 0 and this array would be naming the wrong thing.
 *
 * Only slot 0 is corroborated independently: field65 feeds Statics.field2579 in
 * the deob, which is what RuneLite's injected setCompass writes. The other ten
 * rest on the id lookup alone.
 *
 * So nothing may key on this array. cachepack prints it as a trailing comment
 * and parses the slot index; that is the only use it is good for. (It happens to
 * be src/engine/static_sprites.c's list minus that tree's three dat1-era slots,
 * in the same order -- corroboration, but from our own artifact, not a source.)
 */
extern const char* const RSCache_Dat2DefaultsSpriteSlotNames[RSCACHE_DAT2_DEFAULTS_SPRITE_COUNT];

/**
 * What each of opcode 5's two model slots draws. Commentary only, like the
 * sprite names above — and on weaker footing, so read where it comes from.
 *
 * The cache does not name these models. `pack/7_models.pack` has them as
 * `model_57378` and `model_57379`, plain filler, while both their neighbours
 * are named (`loc/fai_falador_roof_edge_short1z`, `obj/huntguide_moonlight_moth`)
 * — because a model's name there is recovered from the loc/obj/npc that
 * references it, and nothing references these two except this record. So unlike
 * the sprite slots, there is no name in any table to check against; the names
 * here are read off what the client does with them.
 *
 * What it does, all of which is in the deob and none of which is in doubt:
 *
 * - Both load through `class142.method4501(models, id, 0)` and are drawn into
 *   the world by `Statics.method1711(scene, angle, model)`, which places a model
 *   at a compass *bearing* `angle`, at radius `max(512, 1400 - f(zoom))` from the
 *   player's tile.
 * - Both are gated on `client.field1144 > 0`, a 30-tick countdown.
 * - That countdown and `client.field1088` are set together in `class377`, on a
 *   click on a widget of kind `class528.field6175`: it takes `atan2` of the click
 *   about the widget's centre, subtracts camera yaw, and quantises to 16
 *   directions. So `field1088` is a 16-point bearing, and the click also sends it
 *   to the server as a one-byte packet.
 * - Slot 1 draws at `field1088` — the bearing just chosen.
 * - Slot 0 draws at `field831.field6801[last] * 128` when that entry's kind is
 *   60 — a bearing already queued — and is suppressed while the countdown runs
 *   if the new choice equals it, so the same bearing is never drawn twice.
 *
 * Hence "queued" and "selected". Both names describe placement, which is what
 * the draw math proves.
 *
 * What is NOT established: that this is the Sailing helm. `3rd/rsprot` has a
 * `SET_HEADING` (one byte, `heading`) at rev239 and the shape matches exactly,
 * but the deob gives this packet opcode 109 while our rev239 client table puts
 * SET_HEADING at 44 and has no client 109 at all. Either the table and this jar
 * are different sub-revisions or the packet match is wrong, and until that is
 * settled the word "heading" here means the bearing the model is placed at and
 * claims nothing about which game system asked for it.
 */
#define RSCACHE_DAT2_DEFAULTS_MODEL_SLOT_QUEUED 0
#define RSCACHE_DAT2_DEFAULTS_MODEL_SLOT_SELECTED 1

extern const char* const RSCache_Dat2DefaultsModelSlotNames[RSCACHE_DAT2_DEFAULTS_MODEL_COUNT];

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
