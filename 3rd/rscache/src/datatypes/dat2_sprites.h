#ifndef RSCACHE_DATATYPES_DAT2_SPRITES_H
#define RSCACHE_DATATYPES_DAT2_SPRITES_H

#include <stdint.h>

#define FLAG_VERTICAL 1
#define FLAG_ALPHA 2

enum RSCache_SpriteLoadFlags
{
    RSCACHE_SPRITELOAD_FLAG_NONE = 0,
    RSCACHE_SPRITELOAD_FLAG_NORMALIZE = 1 << 0,
    /**
     * OldSchool 232+: after any FLAG_ALPHA payload, force every pixel whose
     * palette index is non-zero to fully opaque.
     *
     * RuneLite dropped the `else` that used to make the two mutually exclusive, so
     * from 232 the stored alpha channel only ever *reduces* to a mask. Older
     * clients keep the channel verbatim — the rev-634 decoder
     * (Class207.method1517) reads the plane, discards it only when every byte is
     * 0xFF, and has no such override. Applying the newer rule to a 634 cache turns
     * every soft-edged asset into a hard block: interface 548's tab-flash overlays
     * (sprites 1840/1841) and the summoning orb glow (1796) all ship an alpha
     * ramp, and drawing them opaque paints solid yellow over the tab strip.
     */
    RSCACHE_SPRITELOAD_FLAG_OPAQUE_INDEX = 1 << 1,
};

struct RSCache;

/** Decode flags for a cache, minus RSCACHE_SPRITELOAD_FLAG_NORMALIZE (a caller
 *  choice, not an era one). */
enum RSCache_SpriteLoadFlags
RSCache_Dat2SpriteFlags(const struct RSCache* cache);

struct RSCache_Dat2Sprite
{
    int id;
    int file_id;
    int frame;
    /* Size of the sprite in memory (runelite maxWidth/maxHeight). */
    int width;
    int height;
    /* Size of the sprite on disk (runelite width/height). */
    int crop_width;
    int crop_height;
    int offset_x;
    int offset_y;

    uint8_t* palette_pixels;
    uint8_t* pixel_alphas;
};

struct RSCache_Dat2SpritePack
{
    int count;
    struct RSCache_Dat2Sprite* sprites;

    int palette_length;
    int* palette;
};

struct RSCache_Dat2SpritePack*
RSCache_Dat2SpritePackNewDecode(
    const unsigned char* data,
    int length,
    enum RSCache_SpriteLoadFlags flags);

/**
 * Encode a sprite pack.
 *
 * **Decode with RSCACHE_SPRITELOAD_FLAG_NONE if you want the original geometry
 * back.** Normalising rewrites the pack in place — `crop_width`/`crop_height`
 * become the memory dimensions and the offsets are zeroed — so encoding a
 * normalised pack yields a valid pack of full-size, unoffset sprites rather than a
 * copy of the source.
 *
 * Two things cannot be reproduced, because the decoder does not retain them:
 *   - the per-sprite flags byte. Row-major is always written (never
 *     FLAG_VERTICAL), and FLAG_ALPHA only when the alpha channel cannot be
 *     re-derived from `index != 0`. A vertically-stored sprite therefore
 *     round-trips semantically but not byte-exactly.
 *   - a palette entry of 0, which the decoder rewrites to 1 on the way in.
 *
 * Returns bytes written, or 0 on failure.
 */
uint32_t
RSCache_Dat2SpritePackEncode(
    const struct RSCache_Dat2SpritePack* pack,
    uint8_t* out,
    uint32_t out_capacity);

/** Worst-case output size for RSCache_Dat2SpritePackEncode. */
uint32_t
RSCache_Dat2SpritePackEncodeBound(const struct RSCache_Dat2SpritePack* pack);

void
RSCache_Dat2SpritePackFree(struct RSCache_Dat2SpritePack* pack);

int*
RSCache_Dat2SpriteGetPixels(
    struct RSCache_Dat2Sprite* sprite,
    int* palette,
    int brightness);

#endif
