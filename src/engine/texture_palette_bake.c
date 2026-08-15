#include "engine/texture_palette_bake.h"

#include "engine/torirs_types.h"

#include "osrs/palette.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

int
ToriRS_TextureGammaBlend(
    int rgb,
    double gamma)
{
    double red = (rgb >> 16) / 256.0;
    double green = ((rgb >> 8) & 255) / 256.0;
    double blue = (rgb & 255) / 256.0;
    red = pow(red, gamma);
    green = pow(green, gamma);
    blue = pow(blue, gamma);
    return ((int)(red * 256.0) << 16) | ((int)(green * 256.0) << 8) | (int)(blue * 256.0);
}

int
ToriRS_TextureAverageHsl(
    const int* texels,
    int width,
    int height)
{
    int red = 0;
    int green = 0;
    int blue = 0;
    int colour_count = width * height;
    int i;

    assert(texels && width > 0 && height > 0);

    for( i = 0; i < colour_count; i++ )
    {
        red += (texels[i] >> 16) & 0xff;
        green += (texels[i] >> 8) & 0xff;
        blue += texels[i] & 0xff;
    }
    return palette_rgb_to_hsl16(
        ((red / colour_count) << 16) + ((green / colour_count) << 8) + (blue / colour_count));
}

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
    int average_hsl)
{
    struct ToriRS_Texture* texture;
    int* adjusted_palette;
    int* pixels;
    bool opaque = true;
    int index;

    if( !indices || !palette || palette_length <= 0 || src_width <= 0 || src_height <= 0 )
        return NULL;
    if( dest_size != 64 && dest_size != 128 )
        return NULL;

    adjusted_palette = malloc((size_t)palette_length * sizeof(*adjusted_palette));
    assert(adjusted_palette);

    /* Reference rule: a colour that survives the 0xf8f8ff mask as 0 is the
     * transparent slot, and any transparent texel makes the whole texture
     * non-opaque (the rasterizer skips the fast path for those). */
    for( index = 0; index < palette_length; index++ )
    {
        int alpha = (palette[index] & 0xf8f8ff) == 0 ? 0 : 0xff;
        adjusted_palette[index] = (alpha << 24) | ToriRS_TextureGammaBlend(palette[index], 0.8);
    }

    pixels = calloc((size_t)dest_size * (size_t)dest_size, sizeof(*pixels));
    assert(pixels);

    for( index = 0; index < src_width * src_height; index++ )
    {
        int palette_index = indices[index];
        if( palette_index >= palette_length )
            continue;
        if( (adjusted_palette[palette_index] & 0xf8f8ff) == 0 )
            opaque = false;
    }

    if( src_width == dest_size && src_height == dest_size )
    {
        for( index = 0; index < dest_size * dest_size; index++ )
        {
            int palette_index = indices[index];
            if( palette_index < palette_length )
                pixels[index] = adjusted_palette[palette_index];
        }
    }
    else if( src_width == 64 && dest_size == 128 )
    {
        int dst = 0;
        for( int x = 0; x < dest_size; x++ )
            for( int y = 0; y < dest_size; y++ )
            {
                int palette_index = indices[((x >> 1) << 6) + (y >> 1)];
                pixels[dst++] = palette_index < palette_length ? adjusted_palette[palette_index]
                                                               : 0;
            }
    }
    else if( src_width == 128 && dest_size == 64 )
    {
        int dst = 0;
        for( int x = 0; x < dest_size; x++ )
            for( int y = 0; y < dest_size; y++ )
            {
                int palette_index = indices[(y << 1) + ((x << 1) << 7)];
                pixels[dst++] = palette_index < palette_length ? adjusted_palette[palette_index]
                                                               : 0;
            }
    }
    else
    {
        free(adjusted_palette);
        free(pixels);
        return NULL;
    }

    free(adjusted_palette);

    texture = calloc(1, sizeof(*texture));
    assert(texture);

    texture->texels = pixels;
    texture->width = dest_size;
    texture->height = dest_size;
    texture->opaque = opaque;
    texture->animation_direction = animation_direction;
    texture->animation_speed = animation_speed;
    texture->average_hsl = average_hsl > 0
                               ? average_hsl
                               : ToriRS_TextureAverageHsl(pixels, dest_size, dest_size);
    return texture;
}
