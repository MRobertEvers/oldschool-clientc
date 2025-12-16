#include "scene_cache.h"

#include "cache.h"
#include "datastruct/hmap.h"
#include "datastruct/ht.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct TexItem
{
    int id;
    int ref_count;
    struct Texture* texture;

    struct TexItem* next;
    struct TexItem* prev;
};

struct TexturesCache
{
    struct HMap* map;

    struct TexItem root;
};

void
texture_animate(struct Texture* texture, int time_delta)
{
    if( texture->animation_direction == TEXTURE_DIRECTION_NONE )
        return;

    int animation_speed = texture->animation_speed;
    int animation_direction = texture->animation_direction;

    int width = texture->width;
    int height = texture->height;
    int length = width * height;

    int* pixels = texture->texels;

    int v_offset = width * time_delta * animation_speed;
    if( animation_direction == TEXTURE_DIRECTION_V_DOWN )
    {
        v_offset = -v_offset;
    }

    if( v_offset > 0 )
    {
        for( int offset = 0; offset < length - 1; offset++ )
        {
            int index = (v_offset + (offset)) & (length - 1);
            pixels[offset] = pixels[index];
        }
    }
    else
    {
        for( int offset = length - 2; offset >= 0; offset-- )
        {
            int index = (v_offset + (offset)) & (length - 1);
            pixels[offset] = pixels[index];
        }
    }
    // else if( animation_direction == TEXTURE_DIRECTION_V_UP )
    // {
    //     for( int y = height - 1; y >= 0; y-- )
    //     {
    //         for( int x = 0; x < width; x++ )
    //         {
    //             int index = y * width + x;
    //             int pixel = pixels[index];
    //             pixels[index] = pixel;
    //         }
    //     }
    // }
    // else if( animation_direction == TEXTURE_DIRECTION_U_DOWN )
    // {
    //     for( int y = 0; y < height; y++ )
    //     {
    //         for( int x = width - 1; x >= 0; x-- )
    //         {
    //             int index = y * width + x;
    //             int pixel = pixels[index];
    //             pixels[index] = pixel;
    //         }
    //     }
    // }
    // else if( animation_direction == TEXTURE_DIRECTION_U_UP )
    // {
    //     for( int y = height - 1; y >= 0; y-- )
    //     {
    //         for( int x = width - 1; x >= 0; x-- )
    //         {
    //             int index = y * width + x;
    //             int pixel = pixels[index];
    //             pixels[index] = pixel;
    //         }
    //     }
    // }
}

struct TexturesCache*
textures_cache_new(struct Cache* cache)
{
    struct TexturesCache* textures_cache =
        (struct TexturesCache*)malloc(sizeof(struct TexturesCache));
    memset(textures_cache, 0, sizeof(struct TexturesCache));

    uint8_t* buffer = (uint8_t*)malloc(4096); // buffer for hash map
    struct HashConfig config = {
        .buffer = buffer,
        .buffer_size = 4096,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct TexItem),
    };

    textures_cache->map = hmap_new(&config, 0);
    return textures_cache;
}

static void
free_texture(struct Texture* texture)
{
    free(texture->texels);
    free(texture);
}

void
textures_cache_free(struct TexturesCache* textures_cache)
{
    struct HMapIter* iter = hmap_iter_new(textures_cache->map);
    struct TexItem* item;
    while( (item = (struct TexItem*)hmap_iter_next(iter)) != NULL )
    {
        free_texture(item->texture);
    }
    hmap_iter_free(iter);

    void* buffer = hmap_buffer_ptr(textures_cache->map);
    hmap_free(textures_cache->map);
    free(buffer);
    free(textures_cache);
}

void
textures_cache_add(struct TexturesCache* textures_cache, int texture_id, struct Texture* texture)
{
    // Add new texture
    struct TexItem* item =
        (struct TexItem*)hmap_search(textures_cache->map, &texture_id, HMAP_INSERT);

    memset(item, 0, sizeof(struct TexItem));
    item->texture = texture;
    item->ref_count = 1;
    item->id = texture_id;

    // Add to linked list for walking
    struct TexItem* walk = &textures_cache->root;
    while( walk->next )
        walk = walk->next;

    walk->next = item;
    item->prev = walk;
}

bool
textures_cache_contains(struct TexturesCache* textures_cache, int texture_id)
{
    struct TexItem* item =
        (struct TexItem*)hmap_search(textures_cache->map, &texture_id, HMAP_FIND);
    return item != NULL;
}

struct Texture*
textures_cache_get(struct TexturesCache* textures_cache, int texture_id)
{
    return (struct Texture*)ht_lookup(&textures_cache->map, &texture_id, HMAP_FIND);
}

struct Texture*
textures_cache_remove(struct TexturesCache* textures_cache, int texture_id)
{
    struct TexItem* item =
        (struct TexItem*)hmap_search(textures_cache->map, &texture_id, HMAP_REMOVE);
    if( !item )
        return NULL;

    // Remove from linked list
    if( item->prev )
        item->prev->next = item->next;
    if( item->next )
        item->next->prev = item->prev;

    return item->texture;
}

struct TexWalk*
textures_cache_walk_new(struct TexturesCache* textures_cache)
{
    struct TexWalk* walk = (struct TexWalk*)malloc(sizeof(struct TexWalk));
    walk->item = &textures_cache->root;

    walk->texture = NULL;
    return walk;
}

void
textures_cache_walk_free(struct TexWalk* walk)
{
    free(walk);
}

struct Texture*
textures_cache_walk_next(struct TexWalk* walk)
{
    struct TexItem* item = (struct TexItem*)walk->item;
    if( !item->next )
        return NULL;
    item = item->next;

    walk->item = item;
    walk->texture = item->texture;

    return item->texture;
}

// struct Texture*
// textures_cache_checkout(
//     struct TexturesCache* textures_cache,
//     struct Cache* cache,
//     int texture_id,
//     int size,
//     double gamma)
// {
//     struct TexItem* item = NULL;
//     item = httex_cache_lookup(textures_cache, texture_id);
//     if( item )
//         return item->texture;

//     cache = textures_cache->cache;
//     struct CacheSpritePack* sprite_pack = NULL;
//     struct Texture* texture = NULL;
//     uint8_t* palette_pixels = NULL;
//     int* palette = NULL;
//     int palette_length = 0;
//     struct CacheTexture* texture_definition = NULL;
//     int* pixels = (int*)malloc(size * size * sizeof(int));
//     if( !pixels )
//         return NULL;

//     texture_definition = texture_definition_new_from_cache(cache, texture_id);
//     if( !texture_definition )
//         return NULL;

//     bool opaque = texture_definition->opaque;
//     struct CacheSpritePack** sprite_packs = (struct CacheSpritePack**)malloc(
//         texture_definition->sprite_ids_count * sizeof(struct CacheSpritePack*));
//     memset(sprite_packs, 0, texture_definition->sprite_ids_count * sizeof(struct
//     CacheSpritePack*));

//     for( int i = 0; i < texture_definition->sprite_ids_count; i++ )
//     {
//         int sprite_id = texture_definition->sprite_ids[i];
//         sprite_pack = sprite_pack_new_from_cache(cache, sprite_id);
//         assert(sprite_pack);
//         if( !sprite_pack )
//             continue;

//         sprite_packs[i] = sprite_pack;
//     }

//     for( int i = 0; i < texture_definition->sprite_ids_count; i++ )
//     {
//         sprite_pack = sprite_packs[i];
//         assert(sprite_pack->count > 0);

//         palette = sprite_pack->palette;
//         palette_length = sprite_pack->palette_length;
//         struct CacheSprite* sprite = &sprite_pack->sprites[0];

//         palette_pixels = sprite->palette_pixels;

//         int* adjusted_palette = (int*)malloc(palette_length * sizeof(int));
//         if( !adjusted_palette )
//             return NULL;

//         for( int pi = 0; pi < palette_length; pi++ )
//         {
//             int alpha = 0xff;
//             // 0xf8f8ff masks off the lower 3 bits of the rgb values.
//             // The trick is they can shift bits downward to change the darkness.
//             // So the top 5 bits of each color contains the full-bright color.
//             if( (palette[pi] & 0xf8f8ff) == 0 )
//             {
//                 // opaque = false;
//                 alpha = 0;
//             }
//             adjusted_palette[pi] = (alpha << 24) + gamma_blend(palette[pi], gamma);
//         }

//         int index = 0;
//         if( i > 0 && texture_definition->sprite_types )
//             index = texture_definition->sprite_types[i - 1];

//         if( index == 0 )
//         {
//             if( size == sprite->width )
//             {
//                 for( int pixel_index = 0; pixel_index < sprite->width * sprite->height;
//                      pixel_index++ )
//                 {
//                     int palette_index = palette_pixels[pixel_index];
//                     pixels[pixel_index] = adjusted_palette[palette_index];
//                 }
//             }
//             else if( sprite->width == 64 && size == 128 )
//             {
//                 int pixel_index = 0;
//                 for( int x = 0; x < size; x++ )
//                 {
//                     for( int y = 0; y < size; y++ )
//                     {
//                         int palette_index = palette_pixels[((x >> 1) << 6) + (y >> 1)];

//                         pixels[pixel_index] = adjusted_palette[palette_index];
//                         pixel_index++;
//                     }
//                 }
//             }
//             else
//             {
//                 if( size != 64 && sprite->width != 128 )
//                 {
//                     printf("Invalid size for sprite\n");
//                     return NULL;
//                 }
//             }
//         }

//         free(adjusted_palette);
//     }

//     for( int i = 0; i < texture_definition->sprite_ids_count; i++ )
//     {
//         sprite_pack = sprite_packs[i];
//         assert(sprite_pack);
//         sprite_pack_free(sprite_pack);
//     }
//     free(sprite_packs);

//     texture = (struct Texture*)malloc(sizeof(struct Texture));
//     assert(texture);
//     texture->texels = pixels;
//     texture->width = size;
//     texture->height = size;
//     texture->opaque = opaque;
//     texture->animation_direction = texture_definition->animation_direction;
//     texture->animation_speed = texture_definition->animation_speed;

//     texture_definition_free(texture_definition);

//     item = httex_cache_emplace(textures_cache, texture_id);
//     memset(item, 0, sizeof(struct TexItem));
//     assert(item != NULL);
//     item->texture = texture;
//     item->ref_count = 1;
//     item->id = texture_id;

//     struct TexItem* walk = &textures_cache->root;
//     while( walk->next )
//         walk = walk->next;

//     if( walk )
//     {
//         walk->next = item;
//         item->prev = walk;
//     }

//     return texture;
// }
