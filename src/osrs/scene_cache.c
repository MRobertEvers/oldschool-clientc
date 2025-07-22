#include "scene_cache.h"

#include "cache.h"
#include "datastruct/ht.h"

#include <assert.h>
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
    model = model_new_from_cache(cache, model_id);

    return model;
    struct Item* item = ht_cache_lookup(model_cache, model_id);
    if( item )
    {
        item->ref_count++;
        return item->model;
    }

    if( !model )
        return NULL;

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