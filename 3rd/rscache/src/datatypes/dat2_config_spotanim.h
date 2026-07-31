#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_SPOTANIM_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_SPOTANIM_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Spotanim (graphical effect) config from the dat2 config group (kind 13).
 * Mirrors Runelite's SpotAnimLoader / the OSRS SpotAnimType format, whose
 * core opcodes match the dat1 SpotType.decode:
 *   1: model g2        2: animation/seq g2   3: model g4 (rev237+)
 *   4: resizeh g2      5: resizev g2      6: rotation g2
 *   7: ambient g1      8: contrast g1
 *   40..49: recolor_to_find[code-40] g2   50..59: recolor_to_replace[code-50] g2
 *   60..69: retexture_to_find[code-60] g2 70..79: retexture_to_replace[code-70] g2
 *   0: terminator
 */
/** Recolour / retexture slots the struct carries. The opcode ranges above are 10
 *  wide, but only 6 slots exist and only 6 are ever used; indices past this are
 *  consumed and dropped rather than written past the end of the array. */
#define RSCACHE_SPOTANIM_COLOUR_SLOTS 6

struct RSCache_Dat2ConfigSpotanim
{
    int model;
    int anim; /* seq id, or -1 */
    int resizeh;
    int resizev;
    int angle;
    int ambient;
    int contrast;
    /** Opcode 10 — payload-free flag (present on some osrs239 records). */
    bool unknown10;
    int recol_s[RSCACHE_SPOTANIM_COLOUR_SLOTS];
    int recol_d[RSCACHE_SPOTANIM_COLOUR_SLOTS];
    int retex_s[RSCACHE_SPOTANIM_COLOUR_SLOTS];
    int retex_d[RSCACHE_SPOTANIM_COLOUR_SLOTS];

    /** How many recolour / retexture pairs the record actually carried. The
     *  lists are count-prefixed on the wire, so the count is data, not derived. */
    int recol_count;
    int retex_count;

    /** Opcode 9 debug/content name, or NULL. Only some caches carry it. */
    char* name;

    /** Bytes consumed by the last decode; compare against the record size to
     *  detect misalignment. Zero if the decode bailed early. */
    int _consumed;
};

struct RSCache_Dat2ConfigSpotanim*
RSCache_Dat2ConfigSpotanimNewDecode(int revision, char* data, int data_size);

/**
 * Encode a spotanim record.
 *
 * `revision` selects whether model ids prefer the int opcode 3 (OSRS >= 237)
 * or the u16 opcode 1. Pass the game revision, or 0 to pick by value width.
 *
 * Emits opcodes in ascending order and omits any field still at its decode
 * default, which is what the packer does — so a record that was itself packed
 * that way round-trips byte-exactly, and one that was not still round-trips
 * semantically.
 *
 * Returns bytes written, or 0 if `out_capacity` is too small.
 */
uint32_t
RSCache_Dat2ConfigSpotanimEncode(
    const struct RSCache_Dat2ConfigSpotanim* spotanim,
    uint8_t* out,
    uint32_t out_capacity);

/** Like Encode, but prefer opcode 3 for models when `revision >= 237`. */
uint32_t
RSCache_Dat2ConfigSpotanimEncodeRevision(
    int revision,
    const struct RSCache_Dat2ConfigSpotanim* spotanim,
    uint8_t* out,
    uint32_t out_capacity);

/** Set the type's non-zero defaults on a record. */
void
RSCache_Dat2ConfigSpotanimInit(struct RSCache_Dat2ConfigSpotanim* spotanim);

/** Decode into a caller-owned record. `NewDecode` is this plus an allocation. */
void
RSCache_Dat2ConfigSpotanimDecodeInplace(
    struct RSCache_Dat2ConfigSpotanim* spotanim,
    const void* data,
    int data_size);

/** An upper bound on what `RSCache_Dat2ConfigSpotanimEncode` will write. */
uint32_t
RSCache_Dat2ConfigSpotanimEncodeBound(const struct RSCache_Dat2ConfigSpotanim* spotanim);

/** Release what the record owns, leaving the struct to the caller. */
void
RSCache_Dat2ConfigSpotanimFreeInplace(struct RSCache_Dat2ConfigSpotanim* spotanim);

void
RSCache_Dat2ConfigSpotanimFree(struct RSCache_Dat2ConfigSpotanim* spotanim);

#endif
