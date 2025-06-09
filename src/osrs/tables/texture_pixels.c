#include "texture_pixels.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
brighten_rgb(int rgb, double brightness)
{
    double r = (rgb >> 16) / 256.0;
    double g = ((rgb >> 8) & 255) / 256.0;
    double b = (rgb & 255) / 256.0;
    r = pow(r, brightness);
    g = pow(g, brightness);
    b = pow(b, brightness);
    int new_r = (int)(r * 256.0);
    int new_g = (int)(g * 256.0);
    int new_b = (int)(b * 256.0);
    return (new_r << 16) | (new_g << 8) | new_b;
}

int*
texture_pixels_new_from_definition(
    struct TextureDefinition* def,
    int size,
    struct SpritePack* sprite_packs,
    int* pack_ids,
    int pack_count,
    int brightness)
{
    int* pixels = (int*)malloc(size * size * sizeof(int));
    if( !pixels )
        return NULL;
    memset(pixels, 0, size * size * sizeof(int));

    for( int sprite_index = 0; sprite_index < def->sprite_ids_count; sprite_index++ )
    {
        int sprite_id = def->sprite_ids[sprite_index];

        int* palette = NULL;
        struct Sprite* sprite = NULL;
        int* sprite_pixels = NULL;
        for( int i = 0; i < pack_count; i++ )
        {
            if( pack_ids[i] == sprite_id )
            {
                sprite = &sprite_packs[i].sprites[0];
                palette = sprite_packs[i].palette;
                break;
            }
        }

        if( !sprite )
        {
            printf("Sprite not found\n");
            return NULL;
        }

        sprite_pixels = sprite->pixels;

        for( int pi = 0; pi < sprite->width * sprite->height; pi++ )
        {
            int alpha = 0xff;
            if( palette[pi] == 0 )
                alpha = 0;
            palette[pi] = (alpha << 24) | palette[pi];
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
                    pixels[pixel_index] = sprite_pixels[pixel_index];
                }
            }
            else if( sprite->width == 64 && size == 128 )
            {
                int pixel_index = 0;
                for( int y = 0; y < size; y++ )
                {
                    for( int x = 0; x < size; x++ )
                    {
                        int palette_index = sprite_pixels[((x >> 1) << 6) + (y >> 1)];
                        pixels[pixel_index] = palette_index;
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
                        int palette_index = sprite_pixels[(y << 1) + ((x << 1) << 7)];
                        pixels[pixel_index] = palette_index;
                        pixel_index++;
                    }
                }
            }
        }
    }

    return pixels;
}