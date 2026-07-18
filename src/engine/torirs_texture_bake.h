#ifndef TORIRS_TEXTURE_BAKE_H
#define TORIRS_TEXTURE_BAKE_H

#include "engine/torirs_types.h"

#include <stdint.h>

struct ToriRS_TextureLayer
{
    const uint8_t* palette_pixels;
    int width;
    int height;
    const int* palette; /* RGB entries */
    int palette_length;
    int blend_type; /* from dat2 sprite_types; 0 = replace */
};

/**
 * Bake indexed palette layers into a ToriRS_Texture with texels.
 * Owns the returned texture and its texel buffer.
 * average_hsl == 0 recomputes from the baked texels.
 * Returns NULL on allocation failure.
 */
struct ToriRS_Texture*
ToriRS_TextureBake(
    const struct ToriRS_TextureLayer* layers,
    int layer_count,
    int dest_size,
    int animation_direction,
    int animation_speed,
    int average_hsl);

#endif
