#include "scene_cache.h"

#include "cache.h"
#include "datastruct/ht.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Item
{
    int id;
    int ref_count;
    struct CacheModel* model;
};

struct ModelCache
{
    struct HashTable table;
};

struct ModelCache*
model_cache_new()
{
    struct ModelCache* model_cache = (struct ModelCache*)malloc(sizeof(struct ModelCache));

    ht_init(
        &model_cache->table,
        (struct HashTableInit){
            .element_size = sizeof(struct Item),
            .capacity_hint = 2000,
            .key_size = sizeof(int),
        });

    return model_cache;
}

void
model_cache_free(struct ModelCache* model_cache)
{
    struct HashTableIter iter;
    for( int i = 0; i < model_cache->table.capacity; i++ )
    {
        iter = ht_atsloth(&model_cache->table, i);
        if( iter.at_end )
            break;
        if( !iter.empty )
        {
            struct Item* item = (struct Item*)iter.value;
            model_free(item->model);
        }
    }
    ht_cleanup(&model_cache->table);
    free(model_cache);
}

static int
ht_cache_keyslot(struct ModelCache* model_cache, int key)
{
    struct Hash hash = { 0 };
    ht_hash_init(&hash);
    ht_hash_update(&hash, &key, sizeof(key));
    ht_hash_end(&hash);

    struct HashTableIter iter = ht_lookuph(&model_cache->table, &hash);
    do
    {
        if( iter.tombstone )
            continue;

        if( iter.empty )
            return -1;

        if( *(int*)iter.key == key )
            return iter._slot;

    } while( ht_nexth(&model_cache->table, &iter) );

    return -1;
}

static struct Item*
ht_cache_lookup(struct ModelCache* model_cache, int model_id)
{
    struct Hash hash = { 0 };
    ht_hash_init(&hash);
    ht_hash_update(&hash, &model_id, sizeof(model_id));
    ht_hash_end(&hash);

    struct HashTableIter iter = ht_lookuph(&model_cache->table, &hash);
    do
    {
        if( iter.empty )
            return NULL;

        if( *(int*)iter.key == model_id )
            return (struct Item*)iter.value;

    } while( ht_nexth(&model_cache->table, &iter) );

    return NULL;
}

static struct Item*
ht_cache_emplace(struct ModelCache* model_cache, int model_id)
{
    struct Hash hash = { 0 };
    ht_hash_init(&hash);
    ht_hash_update(&hash, &model_id, sizeof(model_id));
    ht_hash_end(&hash);

    struct HashTableIter iter = ht_lookuph(&model_cache->table, &hash);
    do
    {
        // 1402 and 25133
        if( iter.empty )
            return ht_emplaceh(&model_cache->table, &iter, &model_id);

        struct Item* item = (struct Item*)iter.value;
        if( item->id == model_id )
            return item;
    } while( ht_nexth(&model_cache->table, &iter) );

    return NULL;
}

static struct Item*
ht_cache_remove(struct ModelCache* model_cache, int model_id)
{
    struct Hash hash = { 0 };
    ht_hash_init(&hash);
    ht_hash_update(&hash, &model_id, sizeof(model_id));
    ht_hash_end(&hash);

    struct HashTableIter iter = ht_lookuph(&model_cache->table, &hash);
    do
    {
        if( iter.empty )
            return NULL;

        struct Item* item = (struct Item*)iter.value;
        if( item->id == model_id )
            return ht_removeh(&model_cache->table, &iter, &model_id);
    } while( ht_nexth(&model_cache->table, &iter) );

    return NULL;
}

struct CacheModel*
model_cache_checkout(struct ModelCache* model_cache, struct Cache* cache, int model_id)
{
    struct CacheModel* model = NULL;
    // model = model_new_from_cache(cache, model_id);

    // return model;
    struct Item* item = ht_cache_lookup(model_cache, model_id);
    if( item )
    {
        item->ref_count++;
        return item->model;
    }

    if( model_id == 1263 )
    {
        int iiii = 0;
    }

    model = model_new_from_cache(cache, model_id);
    if( !model )
        return NULL;

    model->_flags |= CMODEL_FLAG_SHARED;

    item = ht_cache_emplace(model_cache, model_id);
    assert(item != NULL);
    item->model = model;
    item->ref_count = 1;
    item->id = model_id;

    return model;
}

void
model_cache_checkin(struct ModelCache* model_cache, struct CacheModel* model)
{
    struct Item* item = ht_cache_lookup(model_cache, model->_id);
    assert(item != NULL);
    assert(item->model == model);
    item->ref_count--;
    if( item->ref_count == 0 )
    {
        ht_cache_remove(model_cache, model->_id);
        model_free(model);
    }
}

struct TexturesCache
{
    struct Cache* cache;

    struct HashTable table;
};

struct TexItem
{
    int id;
    int ref_count;
    struct Texture* texture;
};

static int
httex_cache_keyslot(struct TexturesCache* textures_cache, int key)
{
    struct Hash hash = { 0 };
    ht_hash_init(&hash);
    ht_hash_update(&hash, &key, sizeof(key));
    ht_hash_end(&hash);

    struct HashTableIter iter = ht_lookuph(&textures_cache->table, &hash);
    do
    {
        if( iter.tombstone )
            continue;

        if( iter.empty )
            return -1;

        if( *(int*)iter.key == key )
            return iter._slot;

    } while( ht_nexth(&textures_cache->table, &iter) );

    return -1;
}

static struct TexItem*
httex_cache_lookup(struct TexturesCache* textures_cache, int model_id)
{
    struct Hash hash = { 0 };
    ht_hash_init(&hash);
    ht_hash_update(&hash, &model_id, sizeof(model_id));
    ht_hash_end(&hash);

    struct HashTableIter iter = ht_lookuph(&textures_cache->table, &hash);
    do
    {
        if( iter.empty )
            return NULL;

        if( *(int*)iter.key == model_id )
            return (struct TexItem*)iter.value;

    } while( ht_nexth(&textures_cache->table, &iter) );

    return NULL;
}

static struct TexItem*
httex_cache_emplace(struct TexturesCache* textures_cache, int model_id)
{
    struct Hash hash = { 0 };
    ht_hash_init(&hash);
    ht_hash_update(&hash, &model_id, sizeof(model_id));
    ht_hash_end(&hash);

    struct HashTableIter iter = ht_lookuph(&textures_cache->table, &hash);
    do
    {
        // 1402 and 25133
        if( iter.empty )
            return ht_emplaceh(&textures_cache->table, &iter, &model_id);

        struct TexItem* item = (struct TexItem*)iter.value;
        if( item->id == model_id )
            return item;
    } while( ht_nexth(&textures_cache->table, &iter) );

    return NULL;
}

static struct TexItem*
httex_cache_remove(struct TexturesCache* textures_cache, int model_id)
{
    struct Hash hash = { 0 };
    ht_hash_init(&hash);
    ht_hash_update(&hash, &model_id, sizeof(model_id));
    ht_hash_end(&hash);

    struct HashTableIter iter = ht_lookuph(&textures_cache->table, &hash);
    do
    {
        if( iter.empty )
            return NULL;

        struct TexItem* item = (struct TexItem*)iter.value;
        if( item->id == model_id )
            return ht_removeh(&textures_cache->table, &iter, &model_id);
    } while( ht_nexth(&textures_cache->table, &iter) );

    return NULL;
}

struct TexturesCache*
textures_cache_new(struct Cache* cache)
{
    struct TexturesCache* textures_cache =
        (struct TexturesCache*)malloc(sizeof(struct TexturesCache));
    memset(textures_cache, 0, sizeof(struct TexturesCache));

    textures_cache->cache = cache;

    ht_init(
        &textures_cache->table,
        (struct HashTableInit){
            .element_size = sizeof(struct TexItem),
            .capacity_hint = 2000,
            .key_size = sizeof(int),
        });

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
    struct HashTableIter iter;
    for( int i = 0; i < textures_cache->table.capacity; i++ )
    {
        iter = ht_atsloth(&textures_cache->table, i);
        if( iter.at_end )
            break;
        if( !iter.empty )
        {
            struct TexItem* item = (struct TexItem*)iter.value;
            free_texture(item->texture);
        }
    }

    ht_cleanup(&textures_cache->table);
    free(textures_cache);
}

static int
gamma_blend(int rgb, double gamma)
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

struct Texture*
textures_cache_checkout(
    struct TexturesCache* textures_cache,
    struct Cache* cache,
    int texture_id,
    int size,
    double gamma)
{
    struct TexItem* item = NULL;
    item = httex_cache_lookup(textures_cache, texture_id);
    if( item )
        return item->texture;

    cache = textures_cache->cache;
    struct CacheSpritePack* sprite_pack = NULL;
    struct Texture* texture = NULL;
    uint8_t* palette_pixels = NULL;
    int* palette = NULL;
    int palette_length = 0;
    bool opaque = true;
    struct CacheTexture* texture_definition = NULL;
    int* pixels = (int*)malloc(size * size * sizeof(int));
    if( !pixels )
        return NULL;

    texture_definition = texture_definition_new_from_cache(cache, texture_id);
    if( !texture_definition )
        return NULL;

    struct CacheSpritePack** sprite_packs = (struct CacheSpritePack**)malloc(
        texture_definition->sprite_ids_count * sizeof(struct CacheSpritePack*));
    memset(sprite_packs, 0, texture_definition->sprite_ids_count * sizeof(struct CacheSpritePack*));

    for( int i = 0; i < texture_definition->sprite_ids_count; i++ )
    {
        int sprite_id = texture_definition->sprite_ids[i];
        sprite_pack = sprite_pack_new_from_cache(cache, sprite_id);
        assert(sprite_pack);
        if( !sprite_pack )
            continue;

        sprite_packs[i] = sprite_pack;
    }

    for( int i = 0; i < texture_definition->sprite_ids_count; i++ )
    {
        sprite_pack = sprite_packs[i];
        assert(sprite_pack->count > 0);

        palette = sprite_pack->palette;
        palette_length = sprite_pack->palette_length;
        struct CacheSprite* sprite = &sprite_pack->sprites[0];

        palette_pixels = sprite->palette_pixels;

        int* adjusted_palette = (int*)malloc(palette_length * sizeof(int));
        if( !adjusted_palette )
            return NULL;

        for( int pi = 0; pi < palette_length; pi++ )
        {
            int alpha = 0xff;
            if( palette[pi] == 0 )
                alpha = 0;
            adjusted_palette[pi] = (alpha << 24) + gamma_blend(palette[pi], gamma);
        }

        int index = 0;
        if( i > 0 && texture_definition->sprite_types )
            index = texture_definition->sprite_types[i - 1];

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
                        int palette_index = palette_pixels[pixel_index];
                        pixels[pixel_index] = adjusted_palette[palette_index];
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
            }
        }

        free(adjusted_palette);
    }

    for( int i = 0; i < texture_definition->sprite_ids_count; i++ )
    {
        sprite_pack = sprite_packs[i];
        assert(sprite_pack);
        sprite_pack_free(sprite_pack);
    }
    free(sprite_packs);

    texture_definition_free(texture_definition);

    texture = (struct Texture*)malloc(sizeof(struct Texture));
    assert(texture);
    texture->texels = pixels;
    texture->width = size;
    texture->height = size;
    texture->opaque = false;

    item = httex_cache_emplace(textures_cache, texture_id);
    assert(item != NULL);
    item->texture = texture;
    item->ref_count = 1;
    item->id = texture_id;

    return texture;
}

void
textures_cache_checkin(struct TexturesCache* textures_cache, struct Texture* texture)
{}