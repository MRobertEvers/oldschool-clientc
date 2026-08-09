#define HMAP_IMPLEMENTATION

#include "cachedat2.h"

#include <datastruct/hmap.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct CacheDat2_ReferenceTable_Entry
{
    int table_id;
    void* data;
};

struct CacheDat2_Model_Entry
{
    int id;
    void* data;
};

struct CacheDat2_ObjectModel_Entry
{
    int object_id;
    void* data;
};

void
CacheDat2_Init(struct CacheDat2* cache)
{
    int capacity = 1024;
    struct HashConfig config;

    memset(cache, 0, sizeof(struct CacheDat2));

    void* models_buffer = malloc(capacity);
    config = (struct HashConfig){
        .key_size = sizeof(int),
        .entry_size = sizeof(struct CacheDat2_Model_Entry),
        .buffer = models_buffer,
        .buffer_size = capacity,
    };
    cache->models = hmap_new(&config, 0);

    void* object_models_buffer = malloc(capacity);
    config = (struct HashConfig){
        .key_size = sizeof(int),
        .entry_size = sizeof(struct CacheDat2_ObjectModel_Entry),
        .buffer = object_models_buffer,
        .buffer_size = capacity,
    };
    cache->object_models = hmap_new(&config, 0);

    void* reference_tables_buffer = malloc(capacity);
    config = (struct HashConfig){
        .key_size = sizeof(int),
        .entry_size = sizeof(struct CacheDat2_ReferenceTable_Entry),
        .buffer = reference_tables_buffer,
        .buffer_size = capacity,
    };
    cache->reference_tables = hmap_new(&config, 0);
}

void
CacheDat2_Model_Add(
    struct CacheDat2* cache,
    int id,
    struct RSCacheDat2A_Model* model)
{
    struct CacheDat2_Model_Entry* entry;

    entry = hmap_search(cache->models, &id, HMAP_INSERT);
    assert(entry && "Must have an entry");
    entry->id = id;
    entry->data = model;
}

void
CacheDat2_ObjectModel_Add(
    struct CacheDat2* cache,
    int object_id,
    struct RSCacheDat2A_Model* model)
{
    struct CacheDat2_ObjectModel_Entry* entry;
    entry = hmap_search(cache->object_models, &object_id, HMAP_INSERT);
    assert(entry && "Must have an entry");
    entry->object_id = object_id;
    entry->data = model;
}

void
CacheDat2_ReferenceTable_Add(
    struct CacheDat2* cache,
    int table_id,
    struct RSCacheDat2Disk_ReferenceTable* reference_table)
{
    struct CacheDat2_ReferenceTable_Entry* entry;
    entry = hmap_search(cache->reference_tables, &table_id, HMAP_INSERT);
    assert(entry && "Must have an entry");
    entry->table_id = table_id;
    entry->data = reference_table;
}

struct RSCacheDat2Disk_ReferenceTable*
CacheDat2_ReferenceTable_Get(
    struct CacheDat2* cache,
    int table_id)
{
    struct CacheDat2_ReferenceTable_Entry* entry;
    entry = hmap_search(cache->reference_tables, &table_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return (struct RSCacheDat2Disk_ReferenceTable*)entry->data;
}

struct RSCacheDat2A_Model*
CacheDat2_ObjectModel_Get(
    struct CacheDat2* cache,
    int object_id)
{
    struct CacheDat2_ObjectModel_Entry* entry;
    entry = hmap_search(cache->object_models, &object_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return (struct RSCacheDat2A_Model*)entry->data;
}
