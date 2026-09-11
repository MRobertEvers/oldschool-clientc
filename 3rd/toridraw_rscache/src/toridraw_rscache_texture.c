#include "toridraw_rscache.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/*
 * A texel is ARGB8888 on every target -- see graphics/pixel_format.h on why the
 * shading space is not the framebuffer format. So this bakes to ARGB8888 and
 * the raster converts at the store, and nothing here knows what
 * TORIDRAW_PIXEL_FORMAT selected.
 */

/*
 * The colour key, spelled as the reference spells it.
 *
 * 0xf8f8ff masks the top five bits of red and green and all eight of blue. A
 * palette entry that is zero under that mask is "transparent" to the reference
 * -- which is a wider set than pure black, and deliberately so: it is the same
 * test the raster's own colour key applies, so a texel that survives the bake
 * is a texel that will be drawn.
 */
#define RSC_TEXTURE_KEY_MASK 0xf8f8ff

/** Reference gamma for every palette Pix3D builds, textures included. */
#define RSC_TEXTURE_GAMMA 0.8

static int*
build_adjusted_palette(
    const int* palette,
    int palette_length)
{
    int* adjusted;
    int i;

    assert(palette);
    assert(palette_length > 0);

    adjusted = malloc((size_t)palette_length * sizeof(*adjusted));
    assert(adjusted);
    for( i = 0; i < palette_length; i++ )
    {
        int alpha = (palette[i] & RSC_TEXTURE_KEY_MASK) == 0 ? 0 : 0xFF;

        adjusted[i] = (alpha << 24) | pix3d_set_gamma(palette[i], RSC_TEXTURE_GAMMA);
    }
    return adjusted;
}

struct ToriDraw_Texture*
ToriDraw_RSCacheTextureFromSprite(
    const struct RSCache_Dat2SpritePack* pack,
    int sprite_index,
    const struct RSCache_Dat2Texture* def,
    int dest_size)
{
    const struct RSCache_Dat2Sprite* sprite;
    struct ToriDraw_Texture* texture;
    int* adjusted;
    int* pixels;
    bool opaque = true;
    int i;

    assert(pack);
    assert(pack->sprites);
    assert(pack->palette);
    assert(pack->palette_length > 0);
    assert(sprite_index >= 0 && sprite_index < pack->count);
    /* The two sizes the reference atlas uses, and the two the resampling below
     * implements. Anything else would silently produce a texture whose uv space
     * does not match what the model was authored against. */
    assert(dest_size == 64 || dest_size == 128);

    sprite = &pack->sprites[sprite_index];
    assert(sprite->palette_pixels);
    assert(sprite->width > 0 && sprite->height > 0);

    adjusted = build_adjusted_palette(pack->palette, pack->palette_length);

    for( i = 0; i < sprite->width * sprite->height; i++ )
    {
        int palette_index = sprite->palette_pixels[i];

        assert(palette_index >= 0 && palette_index < pack->palette_length);
        if( (adjusted[palette_index] & RSC_TEXTURE_KEY_MASK) == 0 )
            opaque = false;
    }

    pixels = calloc((size_t)dest_size * (size_t)dest_size, sizeof(*pixels));
    assert(pixels);

    /*
     * Three cases, and the two resampling ones walk COLUMN-major with a
     * row-major source index -- which is a transpose, and is what the reference
     * does. It is not a bug to tidy: the uv the model carries was authored
     * against exactly this walk, and straightening it here rotates every
     * scaled texture ninety degrees.
     */
    if( sprite->width == dest_size )
    {
        for( i = 0; i < sprite->width * sprite->height; i++ )
            pixels[i] = adjusted[sprite->palette_pixels[i]];
    }
    else if( sprite->width == 64 && dest_size == 128 )
    {
        int out = 0;
        int x;
        int y;

        for( x = 0; x < dest_size; x++ )
            for( y = 0; y < dest_size; y++ )
                pixels[out++] = adjusted[sprite->palette_pixels[((x >> 1) << 6) + (y >> 1)]];
    }
    else if( sprite->width == 128 && dest_size == 64 )
    {
        int out = 0;
        int x;
        int y;

        for( x = 0; x < dest_size; x++ )
            for( y = 0; y < dest_size; y++ )
                pixels[out++] = adjusted[sprite->palette_pixels[(y << 1) + ((x << 1) << 7)]];
    }
    else
    {
        /* A sprite that is neither the destination size nor one of the two
         * documented halvings is data this function cannot resample without
         * inventing a filter the reference does not have. */
        assert(0 && "sprite size is not 64 or 128 and does not match dest_size");
    }

    free(adjusted);

    texture = calloc(1, sizeof(*texture));
    assert(texture);
    texture->texels = pixels;
    texture->width = dest_size;
    texture->height = dest_size;
    /* The def's own opaque flag is advisory; what was actually baked is not.
     * A def claiming opaque over a palette carrying the colour key would let
     * the raster take the no-key span and draw the key colour as a colour. */
    texture->opaque = def ? (def->opaque && opaque) : opaque;
    texture->animation_direction = def ? def->animation_direction : 0;
    texture->animation_speed = def ? def->animation_speed : 0;
    return texture;
}
