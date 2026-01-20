
#include "texture.h"

#include "rscache/tables/sprites.h"
#include "rscache/tables/textures.h"
#include "rscache/tables_dat/config_textures.h"

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
    int new_r = (int)(r * 256.0);
    int new_g = (int)(g * 256.0);
    int new_b = (int)(b * 256.0);
    return (new_r << 16) | (new_g << 8) | new_b;
}

static inline int*
normalize_pixel_buffer(
    int* pixels,
    int current_width,
    int current_height,
    int new_width,
    int new_height)
{
    if( current_width != new_width || current_height != new_height )
    {
        int* normalized_pixels = (int*)malloc(new_width * new_height * sizeof(int));
        if( !normalized_pixels )
            return NULL;
        memset(normalized_pixels, 0, new_width * new_height * sizeof(int));

        int index = 0;

        for( int y = 0; y < current_height; y++ )
        {
            for( int x = 0; x < current_width; x++ )
            {
                normalized_pixels[x + y * new_width] = pixels[x + y * current_width];
            }
        }
        return normalized_pixels;
    }
    else
        return pixels;
}

struct DashTexture*
texture_new_from_definition(
    struct CacheTexture* texture_definition,
    struct DashMap* sprites_hmap)
{
    struct SpritePackEntry* spritepack_entry = NULL;
    struct CacheSpritePack* sprite_pack = NULL;
    struct CacheSprite* sprite = NULL;
    if( !texture_definition || !sprites_hmap )
        return NULL;

    bool opaque = texture_definition->opaque;
    int size = 128; // Default texture size
    int* pixels = (int*)malloc(size * size * sizeof(int));
    if( !pixels )
        return NULL;
    memset(pixels, 0, size * size * sizeof(int));

    struct CacheSpritePack** sprite_packs = (struct CacheSpritePack**)malloc(
        texture_definition->sprite_ids_count * sizeof(struct CacheSpritePack*));
    memset(sprite_packs, 0, texture_definition->sprite_ids_count * sizeof(struct CacheSpritePack*));

    // Load all sprite packs
    for( int i = 0; i < texture_definition->sprite_ids_count; i++ )
    {
        int sprite_id = texture_definition->sprite_ids[i];
        spritepack_entry =
            (struct SpritePackEntry*)dashmap_search(sprites_hmap, &sprite_id, DASHMAP_FIND);
        assert(spritepack_entry && "Spritepack must be inserted into hmap");
        sprite_pack = spritepack_entry->spritepack;
        assert(sprite_pack && "Texture SpritePacks must be loaded prior to texture creation");
        if( !sprite_pack )
            continue;

        sprite_packs[i] = sprite_pack;
    }

    // Process each sprite pack
    for( int i = 0; i < texture_definition->sprite_ids_count; i++ )
    {
        struct CacheSpritePack* sprite_pack = sprite_packs[i];
        if( !sprite_pack || sprite_pack->count == 0 )
            continue;

        int* palette = sprite_pack->palette;
        int palette_length = sprite_pack->palette_length;
        sprite = &sprite_pack->sprites[0];
        uint8_t* palette_pixels = sprite->palette_pixels;

        int* adjusted_palette = (int*)malloc(palette_length * sizeof(int));
        if( !adjusted_palette )
        {
            // Clean up and return NULL
            for( int j = 0; j < texture_definition->sprite_ids_count; j++ )
            {
                if( sprite_packs[j] )
                    sprite_pack_free(sprite_packs[j]);
            }
            free(sprite_packs);
            free(pixels);
            return NULL;
        }

        // Adjust palette with gamma
        for( int pi = 0; pi < palette_length; pi++ )
        {
            int alpha = 0xff;
            if( (palette[pi] & 0xf8f8ff) == 0 )
            {
                alpha = 0;
                opaque = false;
            }
            adjusted_palette[pi] = (alpha << 24) | gamma_blend(palette[pi], 0.8f);
        }

        // Determine sprite type index
        int index = 0;
        if( i > 0 && texture_definition->sprite_types )
            index = texture_definition->sprite_types[i - 1];

        // Combine sprite into texture
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
                for( int x = 0; x < size; x++ )
                {
                    for( int y = 0; y < size; y++ )
                    {
                        int palette_index = palette_pixels[((x >> 1) << 6) + (y >> 1)];
                        pixels[pixel_index] = adjusted_palette[palette_index];
                        pixel_index++;
                    }
                }
            }
        }

        free(adjusted_palette);
    }

    // Clean up sprite packs
    for( int i = 0; i < texture_definition->sprite_ids_count; i++ )
    {
        if( sprite_packs[i] )
            sprite_pack_free(sprite_packs[i]);
    }
    free(sprite_packs);

    // Create texture structure
    struct DashTexture* texture = (struct DashTexture*)malloc(sizeof(struct DashTexture));
    memset(texture, 0, sizeof(struct DashTexture));
    if( !texture )
    {
        free(pixels);
        return NULL;
    }

    texture->texels = pixels;
    texture->width = size;
    texture->height = size;
    texture->opaque = opaque;
    texture->animation_direction = texture_definition->animation_direction;
    texture->animation_speed = texture_definition->animation_speed;

    return texture;
}
// @ObfuscatedName("kb.a([II[B[IIIIIII)V")
// public void plot(int[] pal, int h, byte[] src, int[] dst, int srcStep, int dstOff, int srcOff,
// int w, int dstStep) {
//     int qw = -(w >> 2);
//     w = -(w & 0x3);

//     for (int y = -h; y < 0; y++) {
//         for (int x = qw; x < 0; x++) {
//             byte palIndex = src[srcOff++];
//             if (palIndex == 0) {
//                 dstOff++;
//             } else {
//                 dst[dstOff++] = pal[palIndex & 0xFF];
//             }

//             palIndex = src[srcOff++];
//             if (palIndex == 0) {
//                 dstOff++;
//             } else {
//                 dst[dstOff++] = pal[palIndex & 0xFF];
//             }

//             palIndex = src[srcOff++];
//             if (palIndex == 0) {
//                 dstOff++;
//             } else {
//                 dst[dstOff++] = pal[palIndex & 0xFF];
//             }

//             palIndex = src[srcOff++];
//             if (palIndex == 0) {
//                 dstOff++;
//             } else {
//                 dst[dstOff++] = pal[palIndex & 0xFF];
//             }
//         }

//         for (int x = w; x < 0; x++) {
//             byte palIndex = src[srcOff++];
//             if (palIndex == 0) {
//                 dstOff++;
//             } else {
//                 dst[dstOff++] = pal[palIndex & 0xFF];
//             }
//         }

//         dstOff += dstStep;
//         srcOff += srcStep;
//     }
// }

// const sprite = this.loadTextureSprite(id);

// const palettePixels = sprite.pixels;
// const palette = sprite.palette;

// for (let pi = 0; pi < palette.length; pi++) {
//     let alpha = 0xff;
//     if (palette[pi] === 0) {
//         alpha = 0;
//     }
//     palette[pi] = (alpha << 24) | brightenRgb(palette[pi], brightness);
// }

// const pixelCount = size * size;
// const pixels = new Int32Array(pixelCount);

// if (size === sprite.subWidth) {
//     for (let pixelIndex = 0; pixelIndex < pixelCount; pixelIndex++) {
//         const paletteIndex = palettePixels[pixelIndex];
//         pixels[pixelIndex] = palette[paletteIndex];
//     }
// } else if (sprite.subWidth === 64 && size === 128) {
//     let pixelIndex = 0;

//     for (let x = 0; x < size; x++) {
//         for (let y = 0; y < size; y++) {
//             const paletteIndex = palettePixels[((x >> 1) << 6) + (y >> 1)];
//             pixels[pixelIndex++] = palette[paletteIndex];
//         }
//     }
// } else {
//     if (sprite.subWidth !== 128 || size !== 64) {
//         throw new Error("Texture sprite has unexpected size");
//     }

//     let pixelIndex = 0;

//     for (let x = 0; x < size; x++) {
//         for (let y = 0; y < size; y++) {
//             const paletteIndex = palettePixels[(y << 1) + ((x << 1) << 7)];
//             pixels[pixelIndex++] = palette[paletteIndex];
//         }
//     }
// }

// return pixels;
struct DashTexture*
texture_new_from_texture_sprite(struct CacheDatTexture* texture)
{
    int size = 128;
    int* normalized_pixels =
        normalize_pixel_buffer(texture->pixels, texture->wi, texture->hi, size, size);
    if( !normalized_pixels )
        return NULL;

    struct DashTexture* dash_texture = (struct DashTexture*)malloc(sizeof(struct DashTexture));
    memset(dash_texture, 0, sizeof(struct DashTexture));
    if( !dash_texture )
        return NULL;

    for( int pi = 0; pi < texture->palette_count; pi++ )
    {
        int alpha = 0xff;
        if( texture->palette[pi] == 0 )
        {
            alpha = 0;
        }
        texture->palette[pi] = (alpha << 24) | gamma_blend(texture->palette[pi], 0.8f);
    }

    int pixel_count = size * size;
    int* pixels = (int*)malloc(pixel_count * sizeof(int));
    if( !pixels )
        return NULL;
    memset(pixels, 0, pixel_count * sizeof(int));

    if( size == texture->wi )
    {
        for( int pixel_index = 0; pixel_index < pixel_count; pixel_index++ )
        {
            int palette_index = normalized_pixels[pixel_index];
            pixels[pixel_index] = texture->palette[palette_index];
        }
    }
    else if( texture->wi == 64 && size == 128 )
    {
        int pixel_index = 0;

        for( int x = 0; x < size; x++ )
        {
            for( int y = 0; y < size; y++ )
            {
                int palette_index = normalized_pixels[((x >> 1) << 6) + (y >> 1)];
                pixels[pixel_index++] = texture->palette[palette_index];
            }
        }
    }
    else
    {
        if( texture->wi != 128 || size != 64 )
        {
            return NULL;
        }

        int pixel_index = 0;

        for( int x = 0; x < size; x++ )
        {
            for( int y = 0; y < size; y++ )
            {
                int palette_index = normalized_pixels[(y << 1) + ((x << 1) << 7)];
                pixels[pixel_index++] = texture->palette[palette_index];
            }
        }
    }

    dash_texture->texels = pixels;
    dash_texture->width = size;
    dash_texture->height = size;
    dash_texture->opaque = true;
    dash_texture->animation_direction = 0;
    dash_texture->animation_speed = 0;

    if( normalized_pixels != pixels )
        free(normalized_pixels);

    return dash_texture;
}