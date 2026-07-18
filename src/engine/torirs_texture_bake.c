#include "engine/torirs_texture_bake.h"

#include "osrs/palette.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int
gamma_blend(
    int rgb,
    double gamma)
{
    double r = (rgb >> 16) / 256.0;
    double g = ((rgb >> 8) & 255) / 256.0;
    double b = (rgb & 255) / 256.0;
    r = pow(r, gamma);
    g = pow(g, gamma);
    b = pow(b, gamma);
    return ((int)(r * 256.0) << 16) | ((int)(g * 256.0) << 8) | (int)(b * 256.0);
}

static int
average_hsl_from_texels(
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
ToriRS_TextureBake(
    const struct ToriRS_TextureLayer* layers,
    int layer_count,
    int dest_size,
    int animation_direction,
    int animation_speed,
    int average_hsl)
{
    struct ToriRS_Texture* texture;
    int* pixels;
    bool opaque;
    int i;

    assert(layers);
    assert(layer_count > 0);
    assert(dest_size == 64 || dest_size == 128);

    pixels = calloc((size_t)dest_size * (size_t)dest_size, sizeof(*pixels));
    if( !pixels )
        return NULL;

    opaque = true;

    for( i = 0; i < layer_count; i++ )
    {
        const struct ToriRS_TextureLayer* layer = &layers[i];
        int* adjusted_palette;
        int pi;
        int pixel_index;

        assert(layer->palette_pixels);
        assert(layer->palette);
        assert(layer->palette_length > 0);
        assert(layer->width > 0 && layer->height > 0);

        adjusted_palette = malloc((size_t)layer->palette_length * sizeof(*adjusted_palette));
        if( !adjusted_palette )
        {
            free(pixels);
            return NULL;
        }

        for( pi = 0; pi < layer->palette_length; pi++ )
        {
            int alpha = 0xff;
            if( (layer->palette[pi] & 0xf8f8ff) == 0 )
                alpha = 0;
            adjusted_palette[pi] = (alpha << 24) | gamma_blend(layer->palette[pi], 0.8);
        }

        for( pixel_index = 0; pixel_index < layer->width * layer->height; pixel_index++ )
        {
            int palette_index = layer->palette_pixels[pixel_index];
            assert(palette_index >= 0 && palette_index < layer->palette_length);
            if( (adjusted_palette[palette_index] & 0xf8f8ff) == 0 )
                opaque = false;
        }

        /* Only blend_type 0 (replace) is implemented, matching legacy decode. */
        if( layer->blend_type == 0 )
        {
            if( dest_size == layer->width )
            {
                for( pixel_index = 0; pixel_index < layer->width * layer->height; pixel_index++ )
                {
                    int palette_index = layer->palette_pixels[pixel_index];
                    pixels[pixel_index] = adjusted_palette[palette_index];
                }
            }
            else if( layer->width == 64 && dest_size == 128 )
            {
                int x;
                int y;
                pixel_index = 0;
                for( x = 0; x < dest_size; x++ )
                {
                    for( y = 0; y < dest_size; y++ )
                    {
                        int palette_index = layer->palette_pixels[((x >> 1) << 6) + (y >> 1)];
                        pixels[pixel_index++] = adjusted_palette[palette_index];
                    }
                }
            }
            else if( layer->width == 128 && dest_size == 64 )
            {
                int x;
                int y;
                pixel_index = 0;
                for( x = 0; x < dest_size; x++ )
                {
                    for( y = 0; y < dest_size; y++ )
                    {
                        int palette_index =
                            layer->palette_pixels[(y << 1) + ((x << 1) << 7)];
                        pixels[pixel_index++] = adjusted_palette[palette_index];
                    }
                }
            }
        }

        free(adjusted_palette);
    }

    texture = calloc(1, sizeof(*texture));
    if( !texture )
    {
        free(pixels);
        return NULL;
    }

    texture->texels = pixels;
    texture->width = dest_size;
    texture->height = dest_size;
    texture->opaque = opaque;
    texture->animation_direction = animation_direction;
    texture->animation_speed = animation_speed;
    texture->average_hsl = average_hsl;
    if( texture->average_hsl == 0 )
        texture->average_hsl = average_hsl_from_texels(pixels, dest_size, dest_size);

    return texture;
}
