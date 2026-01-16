
#include "texture.h"

#include "tables/sprites.h"
#include "tables/textures.h"

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
