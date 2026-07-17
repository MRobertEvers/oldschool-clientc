#include "engine/dat2/dat2_buildcache.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct MapEntry_CacheModel
{
    int id;
    struct RSCache_Model* model;
};

struct MapEntry_Terrain
{
    int id;
    struct RSCache_MapTerrain* terrain;
};

struct MapEntry_Scenery
{
    int id;
    struct RSCache_MapLocs* locs;
};

struct MapEntry_ComponentPack
{
    int id;
    struct RSCache_Dat2ComponentPack* pack;
};

struct MapEntry_ClientScript
{
    int id;
    struct RSCache_ClientScript* script;
};

#define DAT2_MODEL_MAP_CAPACITY 8192
#define DAT2_MAP_REGION_CAPACITY 512
#define DAT2_INTERFACE_MAP_CAPACITY 512
#define DAT2_CLIENTSCRIPT_MAP_CAPACITY 4096

static size_t
dat2_hmap_buffer_bytes(
    size_t entry_size,
    size_t capacity)
{
    const size_t align = 16;
    size_t entry_offset = align;
    size_t raw_stride = entry_offset + entry_size;
    size_t stride = (raw_stride + align - 1) & ~(align - 1);
    return align + stride * capacity;
}

static size_t
dat2_hmap_buffer_size(struct HMap* map)
{
    if( !map )
        return 0;
    return dat2_hmap_buffer_bytes(map->entry_size, map->capacity);
}

static struct HMap*
dat2_buildcache_map_new(
    struct Dat2BuildCache* cache,
    size_t entry_size,
    size_t capacity)
{
    size_t buffer_size = dat2_hmap_buffer_bytes(entry_size, capacity);
    void* buffer = malloc(buffer_size);
    assert(buffer);

    struct HashConfig config = {
        .key_size = sizeof(int),
        .entry_size = entry_size,
        .buffer = buffer,
        .buffer_size = buffer_size,
        .capacity = capacity,
    };
    struct HMap* map = hmap_new(&config, 0);
    assert(map);

    if( cache )
        cache->map_buffer_bytes += buffer_size;
    return map;
}

static void
dat2_buildcache_map_free(
    struct Dat2BuildCache* cache,
    struct HMap* map)
{
    if( !map )
        return;

    if( cache )
        cache->map_buffer_bytes -= dat2_hmap_buffer_size(map);
    free(hmap_free(map));
}

static void
dat2_buildcache_map_reset(
    struct Dat2BuildCache* cache,
    struct HMap** map_out,
    size_t entry_size,
    size_t capacity)
{
    assert(map_out);
    dat2_buildcache_map_free(cache, *map_out);
    *map_out = dat2_buildcache_map_new(cache, entry_size, capacity);
}

static void
dat2_buildcache_maybe_grow_hmap(
    struct Dat2BuildCache* cache,
    struct HMap** map_out)
{
    struct HMap* map;
    size_t new_capacity;
    size_t buffer_size;
    size_t old_buffer_size;
    void* new_buffer;
    void* old_buffer;

    assert(map_out);
    map = *map_out;
    if( !map )
        return;

    if( map->size * 4 <= map->capacity * 3 )
        return;

    new_capacity = map->capacity * 2;
    buffer_size = dat2_hmap_buffer_bytes(map->entry_size, new_capacity);
    new_buffer = malloc(buffer_size);
    if( !new_buffer )
        return;

    old_buffer_size = dat2_hmap_buffer_size(map);
    old_buffer = NULL;
    if( hmap_resize(map, new_buffer, buffer_size, new_capacity, &old_buffer) != HMAP_OK )
    {
        free(new_buffer);
        return;
    }

    if( cache )
    {
        cache->map_buffer_bytes -= old_buffer_size;
        cache->map_buffer_bytes += buffer_size;
    }
    free(old_buffer);
}

static void
dat2_buildcache_prepare_hmap_insert(
    struct Dat2BuildCache* cache,
    struct HMap** map_out)
{
    assert(map_out);
    assert(*map_out);

    if( (*map_out)->capacity > 0 && (*map_out)->size * 4 >= (*map_out)->capacity * 3 )
        dat2_buildcache_maybe_grow_hmap(cache, map_out);
}

static struct ToriRS_Task*
Dat2BuildCache_Task_ModelLoad(
    struct CacheProvider* provider,
    int model_id,
    struct ToriRS_Model** out_model)
{
    struct Dat2BuildCache* dat2_buildcache;

    assert(provider);
    assert(out_model);

    dat2_buildcache = (struct Dat2BuildCache*)provider;
    (void)dat2_buildcache_model_get(dat2_buildcache, model_id);
    *out_model = NULL;
    return NULL;
}

static struct CacheProviderVTable dat2_vtable = {
    .Task_ModelLoad = Dat2BuildCache_Task_ModelLoad,
};

struct Dat2BuildCache*
dat2_buildcache_new(void)
{
    struct Dat2BuildCache* dat2_buildcache = calloc(1, sizeof(*dat2_buildcache));
    assert(dat2_buildcache);

    dat2_buildcache->base.vtable = &dat2_vtable;
    dat2_buildcache->base.model_cache =
        dat2_buildcache_map_new(dat2_buildcache, sizeof(struct MapEntry_CacheModel), DAT2_MODEL_MAP_CAPACITY);
    dat2_buildcache->base.sprite_cache = NULL;
    dat2_buildcache->base.componentpack_cache = dat2_buildcache_map_new(
        dat2_buildcache, sizeof(struct MapEntry_ComponentPack), DAT2_INTERFACE_MAP_CAPACITY);
    dat2_buildcache->map_terrain_hmap =
        dat2_buildcache_map_new(dat2_buildcache, sizeof(struct MapEntry_Terrain), DAT2_MAP_REGION_CAPACITY);
    dat2_buildcache->map_scenery_hmap =
        dat2_buildcache_map_new(dat2_buildcache, sizeof(struct MapEntry_Scenery), DAT2_MAP_REGION_CAPACITY);
    dat2_buildcache->clientscripts_hmap = dat2_buildcache_map_new(
        dat2_buildcache, sizeof(struct MapEntry_ClientScript), DAT2_CLIENTSCRIPT_MAP_CAPACITY);

    return dat2_buildcache;
}

void
dat2_buildcache_free(struct Dat2BuildCache* dat2_buildcache)
{
    int table_id;

    assert(dat2_buildcache);

    dat2_buildcache_models_cleanup(dat2_buildcache);
    dat2_buildcache_map_terrain_cleanup(dat2_buildcache);
    dat2_buildcache_map_scenery_cleanup(dat2_buildcache);
    dat2_buildcache_componentpacks_cleanup(dat2_buildcache);
    dat2_buildcache_clientscripts_cleanup(dat2_buildcache);
    dat2_buildcache_reference_tables_cleanup(dat2_buildcache);

    dat2_buildcache_map_free(dat2_buildcache, dat2_buildcache->base.model_cache);
    dat2_buildcache_map_free(dat2_buildcache, dat2_buildcache->base.componentpack_cache);
    dat2_buildcache_map_free(dat2_buildcache, dat2_buildcache->map_terrain_hmap);
    dat2_buildcache_map_free(dat2_buildcache, dat2_buildcache->map_scenery_hmap);
    dat2_buildcache_map_free(dat2_buildcache, dat2_buildcache->clientscripts_hmap);

    for( table_id = 0; table_id < RSCACHE_DAT2_DISK_TABLE_COUNT; table_id++ )
        dat2_buildcache->reference_tables[table_id] = NULL;

    free(dat2_buildcache);
}

struct CacheProvider*
dat2_buildcache_as_provider(struct Dat2BuildCache* dat2_buildcache)
{
    assert(dat2_buildcache);
    return &dat2_buildcache->base;
}

size_t
dat2_buildcache_bytes_total(struct Dat2BuildCache* dat2_buildcache)
{
    assert(dat2_buildcache);
    return dat2_buildcache->map_buffer_bytes;
}

void
dat2_buildcache_model_add(
    struct Dat2BuildCache* dat2_buildcache,
    int model_id,
    struct RSCache_Model* model)
{
    struct MapEntry_CacheModel* entry;

    assert(dat2_buildcache);
    assert(model);

    dat2_buildcache_prepare_hmap_insert(dat2_buildcache, &dat2_buildcache->base.model_cache);
    entry = (struct MapEntry_CacheModel*)hmap_search(
        dat2_buildcache->base.model_cache, &model_id, HMAP_INSERT);
    assert(entry && "Model must be inserted into hmap");

    entry->id = model_id;
    entry->model = model;
}

struct RSCache_Model*
dat2_buildcache_model_get(
    struct Dat2BuildCache* dat2_buildcache,
    int model_id)
{
    struct MapEntry_CacheModel* entry;

    assert(dat2_buildcache);

    entry = (struct MapEntry_CacheModel*)hmap_search(
        dat2_buildcache->base.model_cache, &model_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->model;
}

void
dat2_buildcache_model_remove(
    struct Dat2BuildCache* dat2_buildcache,
    int model_id)
{
    struct MapEntry_CacheModel* entry;

    assert(dat2_buildcache);

    entry = (struct MapEntry_CacheModel*)hmap_search(
        dat2_buildcache->base.model_cache, &model_id, HMAP_REMOVE);
    if( !entry || !entry->model )
        return;

    RSCache_ModelFree(entry->model);
    entry->model = NULL;
}

void
dat2_buildcache_models_cleanup(struct Dat2BuildCache* dat2_buildcache)
{
    struct HMapIter* iter;
    struct MapEntry_CacheModel* entry;

    assert(dat2_buildcache);
    if( !dat2_buildcache->base.model_cache )
        return;

    iter = hmap_iter_new(dat2_buildcache->base.model_cache);
    while( (entry = (struct MapEntry_CacheModel*)hmap_iter_next(iter)) )
    {
        if( entry->model )
            RSCache_ModelFree(entry->model);
    }
    hmap_iter_free(iter);

    dat2_buildcache_map_reset(
        dat2_buildcache,
        &dat2_buildcache->base.model_cache,
        sizeof(struct MapEntry_CacheModel),
        DAT2_MODEL_MAP_CAPACITY);
}

void
dat2_buildcache_map_terrain_add(
    struct Dat2BuildCache* dat2_buildcache,
    int map_id,
    struct RSCache_MapTerrain* terrain)
{
    struct MapEntry_Terrain* entry;

    assert(dat2_buildcache);
    assert(terrain);

    dat2_buildcache_prepare_hmap_insert(dat2_buildcache, &dat2_buildcache->map_terrain_hmap);
    entry = (struct MapEntry_Terrain*)hmap_search(
        dat2_buildcache->map_terrain_hmap, &map_id, HMAP_INSERT);
    assert(entry && "Terrain must be inserted into hmap");

    entry->id = map_id;
    entry->terrain = terrain;
}

struct RSCache_MapTerrain*
dat2_buildcache_map_terrain_get(
    struct Dat2BuildCache* dat2_buildcache,
    int map_id)
{
    struct MapEntry_Terrain* entry;

    assert(dat2_buildcache);

    entry = (struct MapEntry_Terrain*)hmap_search(
        dat2_buildcache->map_terrain_hmap, &map_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->terrain;
}

bool
dat2_buildcache_map_terrain_has(
    struct Dat2BuildCache* dat2_buildcache,
    int map_id)
{
    return dat2_buildcache_map_terrain_get(dat2_buildcache, map_id) != NULL;
}

void
dat2_buildcache_map_terrain_cleanup(struct Dat2BuildCache* dat2_buildcache)
{
    struct HMapIter* iter;
    struct MapEntry_Terrain* entry;

    assert(dat2_buildcache);
    if( !dat2_buildcache->map_terrain_hmap )
        return;

    iter = hmap_iter_new(dat2_buildcache->map_terrain_hmap);
    while( (entry = (struct MapEntry_Terrain*)hmap_iter_next(iter)) )
    {
        if( entry->terrain )
            RSCache_MapTerrainFree(entry->terrain);
    }
    hmap_iter_free(iter);

    dat2_buildcache_map_reset(
        dat2_buildcache,
        &dat2_buildcache->map_terrain_hmap,
        sizeof(struct MapEntry_Terrain),
        DAT2_MAP_REGION_CAPACITY);
}

void
dat2_buildcache_map_scenery_add(
    struct Dat2BuildCache* dat2_buildcache,
    int map_id,
    struct RSCache_MapLocs* locs)
{
    struct MapEntry_Scenery* entry;

    assert(dat2_buildcache);
    assert(locs);

    dat2_buildcache_prepare_hmap_insert(dat2_buildcache, &dat2_buildcache->map_scenery_hmap);
    entry = (struct MapEntry_Scenery*)hmap_search(
        dat2_buildcache->map_scenery_hmap, &map_id, HMAP_INSERT);
    assert(entry && "Scenery must be inserted into hmap");

    entry->id = map_id;
    entry->locs = locs;
}

struct RSCache_MapLocs*
dat2_buildcache_map_scenery_get(
    struct Dat2BuildCache* dat2_buildcache,
    int map_id)
{
    struct MapEntry_Scenery* entry;

    assert(dat2_buildcache);

    entry = (struct MapEntry_Scenery*)hmap_search(
        dat2_buildcache->map_scenery_hmap, &map_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->locs;
}

bool
dat2_buildcache_map_scenery_has(
    struct Dat2BuildCache* dat2_buildcache,
    int map_id)
{
    return dat2_buildcache_map_scenery_get(dat2_buildcache, map_id) != NULL;
}

void
dat2_buildcache_map_scenery_cleanup(struct Dat2BuildCache* dat2_buildcache)
{
    struct HMapIter* iter;
    struct MapEntry_Scenery* entry;

    assert(dat2_buildcache);
    if( !dat2_buildcache->map_scenery_hmap )
        return;

    iter = hmap_iter_new(dat2_buildcache->map_scenery_hmap);
    while( (entry = (struct MapEntry_Scenery*)hmap_iter_next(iter)) )
    {
        if( entry->locs )
            RSCache_MapLocsFree(entry->locs);
    }
    hmap_iter_free(iter);

    dat2_buildcache_map_reset(
        dat2_buildcache,
        &dat2_buildcache->map_scenery_hmap,
        sizeof(struct MapEntry_Scenery),
        DAT2_MAP_REGION_CAPACITY);
}

void
dat2_buildcache_componentpack_add(
    struct Dat2BuildCache* dat2_buildcache,
    int iface_id,
    struct RSCache_Dat2ComponentPack* pack)
{
    struct MapEntry_ComponentPack* entry;

    assert(dat2_buildcache);
    assert(pack);

    dat2_buildcache_prepare_hmap_insert(dat2_buildcache, &dat2_buildcache->base.componentpack_cache);
    entry = (struct MapEntry_ComponentPack*)hmap_search(
        dat2_buildcache->base.componentpack_cache, &iface_id, HMAP_INSERT);
    assert(entry && "Component pack must be inserted into hmap");

    entry->id = iface_id;
    entry->pack = pack;
}

struct RSCache_Dat2ComponentPack*
dat2_buildcache_componentpack_get(
    struct Dat2BuildCache* dat2_buildcache,
    int iface_id)
{
    struct MapEntry_ComponentPack* entry;

    assert(dat2_buildcache);

    entry = (struct MapEntry_ComponentPack*)hmap_search(
        dat2_buildcache->base.componentpack_cache, &iface_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->pack;
}

bool
dat2_buildcache_componentpack_has(
    struct Dat2BuildCache* dat2_buildcache,
    int iface_id)
{
    return dat2_buildcache_componentpack_get(dat2_buildcache, iface_id) != NULL;
}

void
dat2_buildcache_componentpacks_cleanup(struct Dat2BuildCache* dat2_buildcache)
{
    struct HMapIter* iter;
    struct MapEntry_ComponentPack* entry;

    assert(dat2_buildcache);
    if( !dat2_buildcache->base.componentpack_cache )
        return;

    iter = hmap_iter_new(dat2_buildcache->base.componentpack_cache);
    while( (entry = (struct MapEntry_ComponentPack*)hmap_iter_next(iter)) )
    {
        if( entry->pack )
            RSCache_Dat2ComponentPackFree(entry->pack);
    }
    hmap_iter_free(iter);

    dat2_buildcache_map_reset(
        dat2_buildcache,
        &dat2_buildcache->base.componentpack_cache,
        sizeof(struct MapEntry_ComponentPack),
        DAT2_INTERFACE_MAP_CAPACITY);
}

void
dat2_buildcache_clientscript_add(
    struct Dat2BuildCache* dat2_buildcache,
    int script_id,
    struct RSCache_ClientScript* script)
{
    struct MapEntry_ClientScript* entry;

    assert(dat2_buildcache);
    assert(script);

    dat2_buildcache_prepare_hmap_insert(dat2_buildcache, &dat2_buildcache->clientscripts_hmap);
    entry = (struct MapEntry_ClientScript*)hmap_search(
        dat2_buildcache->clientscripts_hmap, &script_id, HMAP_INSERT);
    assert(entry && "Client script must be inserted into hmap");

    entry->id = script_id;
    entry->script = script;
}

struct RSCache_ClientScript*
dat2_buildcache_clientscript_get(
    struct Dat2BuildCache* dat2_buildcache,
    int script_id)
{
    struct MapEntry_ClientScript* entry;

    assert(dat2_buildcache);

    entry = (struct MapEntry_ClientScript*)hmap_search(
        dat2_buildcache->clientscripts_hmap, &script_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->script;
}

bool
dat2_buildcache_clientscript_has(
    struct Dat2BuildCache* dat2_buildcache,
    int script_id)
{
    return dat2_buildcache_clientscript_get(dat2_buildcache, script_id) != NULL;
}

void
dat2_buildcache_clientscripts_cleanup(struct Dat2BuildCache* dat2_buildcache)
{
    struct HMapIter* iter;
    struct MapEntry_ClientScript* entry;

    assert(dat2_buildcache);
    if( !dat2_buildcache->clientscripts_hmap )
        return;

    iter = hmap_iter_new(dat2_buildcache->clientscripts_hmap);
    while( (entry = (struct MapEntry_ClientScript*)hmap_iter_next(iter)) )
    {
        if( entry->script )
            RSCache_ClientScriptFree(entry->script);
    }
    hmap_iter_free(iter);

    dat2_buildcache_map_reset(
        dat2_buildcache,
        &dat2_buildcache->clientscripts_hmap,
        sizeof(struct MapEntry_ClientScript),
        DAT2_CLIENTSCRIPT_MAP_CAPACITY);
}

void
dat2_buildcache_reference_table_add(
    struct Dat2BuildCache* dat2_buildcache,
    int table_id,
    struct RSCache_ReferenceTable* table)
{
    assert(dat2_buildcache);
    assert(table);
    assert(RSCache_Dat2DiskIsValidTableId(table_id));

    if( dat2_buildcache->reference_tables[table_id] )
        RSCache_ReferenceTableFree(dat2_buildcache->reference_tables[table_id]);
    dat2_buildcache->reference_tables[table_id] = table;
}

struct RSCache_ReferenceTable*
dat2_buildcache_reference_table_get(
    struct Dat2BuildCache* dat2_buildcache,
    int table_id)
{
    assert(dat2_buildcache);
    if( !RSCache_Dat2DiskIsValidTableId(table_id) )
        return NULL;
    return dat2_buildcache->reference_tables[table_id];
}

bool
dat2_buildcache_reference_table_has(
    struct Dat2BuildCache* dat2_buildcache,
    int table_id)
{
    return dat2_buildcache_reference_table_get(dat2_buildcache, table_id) != NULL;
}

void
dat2_buildcache_reference_tables_cleanup(struct Dat2BuildCache* dat2_buildcache)
{
    int table_id;

    assert(dat2_buildcache);

    for( table_id = 0; table_id < RSCACHE_DAT2_DISK_TABLE_COUNT; table_id++ )
    {
        if( dat2_buildcache->reference_tables[table_id] )
        {
            RSCache_ReferenceTableFree(dat2_buildcache->reference_tables[table_id]);
            dat2_buildcache->reference_tables[table_id] = NULL;
        }
    }
}

void
dat2_buildcache_prune(struct Dat2BuildCache* dat2_buildcache)
{
    assert(dat2_buildcache);

    dat2_buildcache_models_cleanup(dat2_buildcache);
    dat2_buildcache_map_terrain_cleanup(dat2_buildcache);
    dat2_buildcache_map_scenery_cleanup(dat2_buildcache);
    dat2_buildcache_componentpacks_cleanup(dat2_buildcache);
    dat2_buildcache_clientscripts_cleanup(dat2_buildcache);
    dat2_buildcache_reference_tables_cleanup(dat2_buildcache);
}
