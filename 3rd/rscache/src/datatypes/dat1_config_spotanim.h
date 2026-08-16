#ifndef RSCACHE_DATATYPES_DAT1_CONFIG_SPOTANIM_H
#define RSCACHE_DATATYPES_DAT1_CONFIG_SPOTANIM_H

/*
 * Spotanim (graphical effect) config from the dat1 config jagfile's
 * "spotanim.dat". Mirrors SpotType.decode in the LostCity JavaClient
 * (Client-TS config/SpotType.ts):
 *   code 1: model g2       code 2: anim/seq g2
 *   code 4: resizeh g2     code 5: resizev g2
 *   code 6: angle g2       code 7: ambient g1   code 8: contrast g1
 *   code 40..49: recol_s[code-40] g2   code 50..59: recol_d[code-50] g2
 *   code 0: terminator
 */
#include "../rsbuffer.h"
#include "dat2_config_spotanim.h"

struct RSCache_Dat1ConfigSpotanim
{
    int model;
    int anim; /* seq id, or -1 */
    int resizeh;
    int resizev;
    int angle;
    int ambient;
    int contrast;
    /**
     * Opcode 3 — a bare flag with no payload.
     *
     * The decoder did not know this opcode and stopped on it, which desynced the
     * whole archive: dat1 spotanims are one concatenated stream, so every record
     * after the first flagged one was lost. It reads as a flag in the reference
     * (RS2 SpotAnimType `disposeAlpha`), and that reading is what makes all 270
     * records in cache254 decode and re-encode byte-identically.
     */
    bool dispose_alpha;
    /* Six slots, though the opcode ranges above are ten wide — see
     * RSCACHE_SPOTANIM_COLOUR_SLOTS. */
    int recol_s[RSCACHE_SPOTANIM_COLOUR_SLOTS];
    int recol_d[RSCACHE_SPOTANIM_COLOUR_SLOTS];
    /** The opcodes in the order the record carried them; replayed by the encoder.
     *  dat1 config records are not sorted — see `dat1_config_idk.h`. */
    int opcodes[64];
    int opcode_count;
};

/** Whole "spotanim.dat" table: entries are variable-length and only
 * sequentially addressable, so they decode as one list. */
struct RSCache_Dat1ConfigSpotanimList
{
    struct RSCache_Dat1ConfigSpotanim* spotanims;
    int spotanims_count;
};

struct RSCache_Dat1ConfigSpotanimList*
RSCache_Dat1ConfigSpotanimListNewDecode(
    char* data,
    int data_size);

void
RSCache_Dat1ConfigSpotanimListFree(struct RSCache_Dat1ConfigSpotanimList* list);

/**
 * Bring a zeroed allocation to this type's defaults.
 *
 * `anim` is -1 (sequence 0 is a real sequence) and `resizeh`/`resizev` are 128,
 * the unity scale — zero there renders the effect at no size at all.
 */
void
RSCache_Dat1ConfigSpotanimInit(struct RSCache_Dat1ConfigSpotanim* spotanim);

/**
 * Handle one opcode, advancing `buffer` past its payload.
 *
 * False means "not mine", which ends the stream — the width of an unknown
 * opcode cannot be guessed. See `opcode_codec.h`.
 */
bool
RSCache_Dat1ConfigSpotanimDecodeOp(
    struct RSCache_Dat1ConfigSpotanim* spotanim,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags);

/** Returns the number of bytes this entry consumed. */
int
RSCache_Dat1ConfigSpotanimDecodeInplace(
    struct RSCache_Dat1ConfigSpotanim* spotanim,
    char* data,
    int data_size);

/**
 * Encode one dat1 spotanim record, replaying its decoded opcode order.
 *
 * Colour slots 6..9 are *not* reproducible: the decoder consumes their value to
 * keep the stream aligned but has only six array slots to store it in, so this
 * writes zero for them. `test_dat1_encode` counts how many records that costs.
 */
uint32_t
RSCache_Dat1ConfigSpotanimEncode(
    const struct RSCache_Dat1ConfigSpotanim* spotanim,
    uint8_t* out,
    uint32_t out_capacity);

/** An upper bound on what `RSCache_Dat1ConfigSpotanimEncode` will write. */
uint32_t
RSCache_Dat1ConfigSpotanimEncodeBound(const struct RSCache_Dat1ConfigSpotanim* spotanim);

#endif
