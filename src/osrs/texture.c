#include "texture.h"

#include "datastruct/hmap.h"
#include "tables/sprites.h"
#include "tables/textures.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

// Gamma blending function (simplified)
static int
gamma_blend(int rgb, float gamma)
{
    // Simple gamma correction - for now just return the rgb as-is
    // This should be implemented properly for full texture support
    return rgb;
}

struct Texture*
texture_new_from_definition(struct CacheTexture* texture_definition, struct Cache* cache)
{
    if( !texture_definition || !cache )
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
        struct CacheSpritePack* sprite_pack = sprite_pack_new_from_cache(cache, sprite_id);
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
        struct CacheSprite* sprite = &sprite_pack->sprites[0];
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
            adjusted_palette[pi] = (alpha << 24) | gamma_blend(palette[pi], 1.0f);
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
    struct Texture* texture = (struct Texture*)malloc(sizeof(struct Texture));
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

struct SpritePacksCache*
spritepacks_cache_new()
{
    struct SpritePacksCache* spritepacks_cache =
        (struct SpritePacksCache*)malloc(sizeof(struct SpritePacksCache));
    memset(spritepacks_cache, 0, sizeof(struct SpritePacksCache));

    uint8_t* buffer = (uint8_t*)malloc(4096); // buffer for hash map
    struct HashConfig config = {
        .buffer = buffer,
        .buffer_size = 4096,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct SpritePackItem),
    };

    spritepacks_cache->map = hmap_new(&config, 0);
    return spritepacks_cache;
}

static void
free_sprite_pack(struct CacheSpritePack* sprite_pack)
{
    sprite_pack_free(sprite_pack);
}

void
spritepacks_cache_free(struct SpritePacksCache* spritepacks_cache)
{
    struct HMapIter* iter = hmap_iter_new(spritepacks_cache->map);
    struct SpritePackItem* item;
    while( (item = (struct SpritePackItem*)hmap_iter_next(iter)) != NULL )
    {
        free_sprite_pack(item->sprite_pack);
    }
    hmap_iter_free(iter);

    void* buffer = hmap_buffer_ptr(spritepacks_cache->map);
    hmap_free(spritepacks_cache->map);
    free(buffer);
    free(spritepacks_cache);
}

void
spritepacks_cache_add(
    struct SpritePacksCache* spritepacks_cache,
    int sprite_pack_id,
    struct CacheSpritePack* sprite_pack)
{
    // Add new sprite pack
    struct SpritePackItem* item =
        (struct SpritePackItem*)hmap_search(spritepacks_cache->map, &sprite_pack_id, HMAP_INSERT);

    memset(item, 0, sizeof(struct SpritePackItem));
    item->sprite_pack = sprite_pack;
    item->ref_count = 1;
    item->id = sprite_pack_id;

    // Add to linked list for walking
    struct SpritePackItem* walk = &spritepacks_cache->root;
    while( walk->next )
        walk = walk->next;

    walk->next = item;
    item->prev = walk;
}

bool
spritepacks_cache_contains(struct SpritePacksCache* spritepacks_cache, int sprite_pack_id)
{
    struct SpritePackItem* item =
        (struct SpritePackItem*)hmap_search(spritepacks_cache->map, &sprite_pack_id, HMAP_FIND);
    return item != NULL;
}

struct CacheSpritePack*
spritepacks_cache_get(struct SpritePacksCache* spritepacks_cache, int sprite_pack_id)
{
    struct SpritePackItem* item =
        (struct SpritePackItem*)hmap_search(spritepacks_cache->map, &sprite_pack_id, HMAP_FIND);
    return item ? item->sprite_pack : NULL;
}

struct CacheSpritePack*
spritepacks_cache_remove(struct SpritePacksCache* spritepacks_cache, int sprite_pack_id)
{
    struct SpritePackItem* item =
        (struct SpritePackItem*)hmap_search(spritepacks_cache->map, &sprite_pack_id, HMAP_REMOVE);
    if( !item )
        return NULL;

    // Remove from linked list
    if( item->prev )
        item->prev->next = item->next;
    if( item->next )
        item->next->prev = item->prev;

    return item->sprite_pack;
}

struct SpritePackWalk*
spritepacks_cache_walk_new(struct SpritePacksCache* spritepacks_cache)
{
    struct SpritePackWalk* walk = (struct SpritePackWalk*)malloc(sizeof(struct SpritePackWalk));
    walk->item = &spritepacks_cache->root;

    walk->sprite_pack = NULL;
    return walk;
}

void
spritepacks_cache_walk_free(struct SpritePackWalk* walk)
{
    free(walk);
}

struct CacheSpritePack*
spritepacks_cache_walk_next(struct SpritePackWalk* walk)
{
    struct SpritePackItem* item = (struct SpritePackItem*)walk->item;
    if( !item->next )
        return NULL;
    item = item->next;

    walk->item = item;
    walk->sprite_pack = item->sprite_pack;

    return item->sprite_pack;
}

struct Texture*
texture_new_from_definition_with_spritepacks(
    struct CacheTexture* texture_definition, struct CacheSpritePack** spritepacks)
{
    if( !texture_definition || !spritepacks )
        return NULL;

    bool opaque = texture_definition->opaque;
    int size = 128; // Default texture size
    int* pixels = (int*)malloc(size * size * sizeof(int));
    if( !pixels )
        return NULL;
    memset(pixels, 0, size * size * sizeof(int));

    // Use the provided spritepacks instead of loading them
    struct CacheSpritePack** sprite_packs = spritepacks;

    // Process each sprite pack (same logic as original function)
    for( int i = 0; i < texture_definition->sprite_ids_count; i++ )
    {
        struct CacheSpritePack* sprite_pack = sprite_packs[i];
        if( !sprite_pack || sprite_pack->count == 0 )
            continue;

        int* palette = sprite_pack->palette;
        int palette_length = sprite_pack->palette_length;
        struct CacheSprite* sprite = &sprite_pack->sprites[0];
        uint8_t* palette_pixels = sprite->palette_pixels;

        int* adjusted_palette = (int*)malloc(palette_length * sizeof(int));
        if( !adjusted_palette )
        {
            // Clean up and return NULL
            free(pixels);
            return NULL;
        }

        // Adjust palette colors (same logic as original)
        for( int k = 0; k < palette_length; k++ )
        {
            int color = palette[k];
            if( color == 0 )
            {
                adjusted_palette[k] = 0;
            }
            else
            {
                int red = (color >> 16) & 0xFF;
                int green = (color >> 8) & 0xFF;
                int blue = color & 0xFF;

                if( !opaque )
                {
                    red = (int)(red * 1.4);
                    if( red > 255 )
                        red = 255;
                    green = (int)(green * 1.4);
                    if( green > 255 )
                        green = 255;
                    blue = (int)(blue * 1.4);
                    if( blue > 255 )
                        blue = 255;
                }

                adjusted_palette[k] = (red << 16) | (green << 8) | blue;
            }
        }

        // Process palette pixels
        for( int y = 0; y < sprite->height; y++ )
        {
            for( int x = 0; x < sprite->width; x++ )
            {
                int pixel_index = y * sprite->width + x;
                uint8_t palette_index = palette_pixels[pixel_index];

                if( palette_index != 0 )
                {
                    int pixel_x = x; // Could apply transformations here
                    int pixel_y = y;

                    if( pixel_x >= 0 && pixel_x < size && pixel_y >= 0 && pixel_y < size )
                    {
                        pixels[pixel_y * size + pixel_x] = adjusted_palette[palette_index];
                    }
                }
            }
        }

        free(adjusted_palette);
    }

    // Create the texture structure
    struct Texture* texture = (struct Texture*)malloc(sizeof(struct Texture));
    if( !texture )
    {
        free(pixels);
        return NULL;
    }

    texture->texels = pixels;
    texture->width = size;
    texture->height = size;
    texture->opaque = opaque;

    return texture;
}