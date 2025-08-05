#include "texture_pixels.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
set_gamma(int rgb, double gamma)
{
    double r = (rgb >> 16) / 256.0;
    double g = ((rgb >> 8) & 255) / 256.0;
    double b = (rgb & 255) / 256.0;
    r = pow(r, gamma);
    g = pow(g, gamma);
    b = pow(b, gamma);
    int new_r = (int)(r * 256.0);
    int new_g = (int)(g * 256.0);
    int new_b = (int)(b * 256.0);
    return (new_r << 16) | (new_g << 8) | new_b;
}

int*
texture_pixels_new_from_definition(
    struct CacheTexture* def,
    int size,
    struct CacheSpritePack* sprite_packs,
    int* pack_ids,
    int pack_count,
    double brightness)
{
    int* pixels = (int*)malloc(size * size * sizeof(int));
    if( !pixels )
        return NULL;
    memset(pixels, 0, size * size * sizeof(int));

    for( int sprite_index = 0; sprite_index < def->sprite_ids_count; sprite_index++ )
    {
        int sprite_id = def->sprite_ids[sprite_index];

        int* palette = NULL;
        int palette_length = 0;
        struct CacheSprite* sprite = NULL;
        uint8_t* palette_pixels = NULL;
        for( int i = 0; i < pack_count; i++ )
        {
            if( pack_ids[i] == sprite_id )
            {
                sprite = &sprite_packs[i].sprites[0];
                palette = sprite_packs[i].palette;
                palette_length = sprite_packs[i].palette_length;
                break;
            }
        }

        if( !sprite )
        {
            printf("Sprite not found\n");
            return NULL;
        }

        palette_pixels = sprite->palette_pixels;

        int* adjusted_palette = (int*)malloc(palette_length * sizeof(int));
        if( !adjusted_palette )
            return NULL;
        memset(adjusted_palette, 0, palette_length * sizeof(int));

        for( int pi = 0; pi < palette_length; pi++ )
        {
            int alpha = 0xff;
            if( palette[pi] == 0 )
                alpha = 0;
            adjusted_palette[pi] = (alpha << 24) | set_gamma(palette[pi], brightness);
        }

        int index = 0;
        if( sprite_index > 0 && def->sprite_types )
            index = def->sprite_types[sprite_index - 1];

        if( index == 0 )
        {
            if( size == sprite->width )
            {
                for( int pixel_index = 0; pixel_index < sprite->width * sprite->height;
                     pixel_index++ )
                {
                    int palette_index = palette_pixels[pixel_index];
                    pixels[pixel_index] = adjusted_palette[palette_index];
                }
            }
            else if( sprite->width == 64 && size == 128 )
            {
                int pixel_index = 0;
                for( int y = 0; y < size; y++ )
                {
                    for( int x = 0; x < size; x++ )
                    {
                        int palette_index = palette_pixels[((x >> 1) << 6) + (y >> 1)];
                        pixels[pixel_index] = adjusted_palette[palette_index];
                        pixel_index++;
                    }
                }
            }
            else
            {
                if( size != 64 && sprite->width != 128 )
                {
                    printf("Invalid size for sprite\n");
                    return NULL;
                }

                int pixel_index = 0;
                for( int x = 0; x < size; x++ )
                {
                    for( int y = 0; y < size; y++ )
                    {
                        int palette_index = palette_pixels[(y << 1) + ((x << 1) << 7)];
                        pixels[pixel_index] = adjusted_palette[palette_index];
                        pixel_index++;
                    }
                }
            }
        }

        free(adjusted_palette);
    }

    return pixels;
}