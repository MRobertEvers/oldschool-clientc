#ifndef ENGINE_TEXTURE_PALETTE_BAKE_H
#define ENGINE_TEXTURE_PALETTE_BAKE_H

#include <stdint.h>

struct ToriRS_Texture;

/*
 * Palette-indexed texture -> render-ready ARGB texels.
 *
 * Both cache formats store textures the same way underneath (an index buffer
 * plus a small palette); they differ only in where that pair comes from — dat2
 * layers sprite packs from the texture defs group, dat1 has one Pix8 image per
 * texture in the TEXTURES jagfile. The colour rules below (gamma, the
 * transparency test, the average used for distant shading) are what the
 * renderer expects and must stay identical across the two.
 */

/** Reference Pix3D gamma curve applied to every palette entry (0.8). */
int
ToriRS_TextureGammaBlend(
    int rgb,
    double gamma);

/** Mean colour as HSL16, used when the cache does not carry one. */
int
ToriRS_TextureAverageHsl(
    const int* texels,
    int width,
    int height);

/**
 * Bake one indexed image into a dest_size x dest_size texture (64 or 128).
 * Handles the 64 -> 128 and 128 -> 64 rescales the reference does. Returns an
 * owned ToriRS_Texture, or NULL on bad input / allocation failure.
 * average_hsl <= 0 means "compute it from the baked texels".
 */
struct ToriRS_Texture*
ToriRS_TextureBakeIndexed(
    const uint8_t* indices,
    int src_width,
    int src_height,
    const int* palette,
    int palette_length,
    int dest_size,
    int animation_direction,
    int animation_speed,
    int average_hsl);

#endif
