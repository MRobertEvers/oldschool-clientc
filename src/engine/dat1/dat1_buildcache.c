#include "engine/dat1/dat1_buildcache.h"

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

#define DAT1_MODEL_MAP_CAPACITY 8192
#define DAT1_MAP_REGION_CAPACITY 512

static size_t
dat1_hmap_buffer_bytes(
    size_t entry_size,
    size_t capacity)
{
    const size_t align = 16;
    size_t entry_offset = align;
    size_t raw_stride = entry_offset + entry_size;
    size_t stride = (raw_stride + align - 1) & ~(align - 1);
    return align + stride * capacity;
}

static struct HMap*
dat1_hmap_new(
    size_t entry_size,
    size_t capacity)
{
    size_t buffer_size = dat1_hmap_buffer_bytes(entry_size, capacity);
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
    return map;
}

static void
dat1_hmap_free(struct HMap* map)
{
    if( !map )
        return;
    free(hmap_free(map));
}

static void
dat1_hmap_maybe_grow(struct HMap** map_out)
{
    struct HMap* map;
    size_t new_capacity;
    size_t buffer_size;
    void* new_buffer;
    void* old_buffer;

    assert(map_out);
    map = *map_out;
    assert(map);

    if( map->size * 4 <= map->capacity * 3 )
        return;

    new_capacity = map->capacity * 2;
    buffer_size = dat1_hmap_buffer_bytes(map->entry_size, new_capacity);
    new_buffer = malloc(buffer_size);
    assert(new_buffer);

    if( hmap_resize(*map_out, new_buffer, buffer_size, new_capacity, &old_buffer) != HMAP_OK )
    {
        free(new_buffer);
        return;
    }

    free(old_buffer);
}

static void
dat1_hmap_prepare_insert(struct HMap** map_out)
{
    assert(map_out);
    assert(*map_out);
    if( (*map_out)->capacity > 0 && (*map_out)->size * 4 >= (*map_out)->capacity * 3 )
        dat1_hmap_maybe_grow(map_out);
}

static void
dat1_jagfile_set(
    struct RSCache_FileListDat** slot,
    struct RSCache_FileListDat* jagfile)
{
    assert(slot);
    if( *slot )
        RSCache_FileListDatFree(*slot);
    *slot = jagfile;
}

static void
dat1_jagfile_clear(struct RSCache_FileListDat** slot)
{
    assert(slot);
    if( *slot )
        RSCache_FileListDatFree(*slot);
    *slot = NULL;
}

static struct ToriRS_Task*
Dat1BuildCache_Task_ModelLoad(
    struct CacheProvider* provider,
    int model_id,
    struct ToriRS_Model** out_model)
{
    struct Dat1BuildCache* dat1_buildcache;

    assert(provider);
    assert(out_model);

    dat1_buildcache = (struct Dat1BuildCache*)provider;
    (void)dat1_buildcache_model_get(dat1_buildcache, model_id);
    *out_model = NULL;
    return NULL;
}

static struct CacheProviderVTable dat1_vtable = {
    .Task_ModelLoad = Dat1BuildCache_Task_ModelLoad,
};

struct Dat1BuildCache*
dat1_buildcache_new(void)
{
    struct Dat1BuildCache* dat1_buildcache = calloc(1, sizeof(*dat1_buildcache));
    assert(dat1_buildcache);

    dat1_buildcache->base.vtable = &dat1_vtable;
    dat1_buildcache->base.model_cache =
        dat1_hmap_new(sizeof(struct MapEntry_CacheModel), DAT1_MODEL_MAP_CAPACITY);
    dat1_buildcache->base.sprite_cache = NULL;
    dat1_buildcache->base.componentpack_cache = NULL;
    dat1_buildcache->map_terrain_hmap =
        dat1_hmap_new(sizeof(struct MapEntry_Terrain), DAT1_MAP_REGION_CAPACITY);
    dat1_buildcache->map_scenery_hmap =
        dat1_hmap_new(sizeof(struct MapEntry_Scenery), DAT1_MAP_REGION_CAPACITY);

    return dat1_buildcache;
}

static void
dat1_hmap_reset(
    struct HMap** map_out,
    size_t entry_size,
    size_t capacity)
{
    assert(map_out);
    dat1_hmap_free(*map_out);
    *map_out = dat1_hmap_new(entry_size, capacity);
}

void
dat1_buildcache_free(struct Dat1BuildCache* dat1_buildcache)
{
    assert(dat1_buildcache);

    dat1_jagfile_clear(&dat1_buildcache->config_jagfile);
    dat1_jagfile_clear(&dat1_buildcache->versionlist_jagfile);
    dat1_jagfile_clear(&dat1_buildcache->media_2d_graphics_jagfile);
    dat1_jagfile_clear(&dat1_buildcache->title_fonts_jagfile);

    dat1_buildcache_models_cleanup(dat1_buildcache);
    dat1_buildcache_map_terrain_cleanup(dat1_buildcache);
    dat1_buildcache_map_scenery_cleanup(dat1_buildcache);

    dat1_hmap_free(dat1_buildcache->base.model_cache);
    dat1_hmap_free(dat1_buildcache->map_terrain_hmap);
    dat1_hmap_free(dat1_buildcache->map_scenery_hmap);

    free(dat1_buildcache);
}

struct CacheProvider*
dat1_buildcache_as_provider(struct Dat1BuildCache* dat1_buildcache)
{
    assert(dat1_buildcache);
    return &dat1_buildcache->base;
}

void
dat1_buildcache_set_config_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    struct RSCache_FileListDat* config_jagfile)
{
    assert(dat1_buildcache);
    dat1_jagfile_set(&dat1_buildcache->config_jagfile, config_jagfile);
}

void
dat1_buildcache_clear_config_jagfile(struct Dat1BuildCache* dat1_buildcache)
{
    assert(dat1_buildcache);
    dat1_jagfile_clear(&dat1_buildcache->config_jagfile);
}

struct RSCache_FileListDat*
dat1_buildcache_get_config_jagfile(struct Dat1BuildCache* dat1_buildcache)
{
    assert(dat1_buildcache);
    return dat1_buildcache->config_jagfile;
}

void
dat1_buildcache_set_versionlist_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    struct RSCache_FileListDat* versionlist_jagfile)
{
    assert(dat1_buildcache);
    dat1_jagfile_set(&dat1_buildcache->versionlist_jagfile, versionlist_jagfile);
}

void
dat1_buildcache_clear_versionlist_jagfile(struct Dat1BuildCache* dat1_buildcache)
{
    assert(dat1_buildcache);
    dat1_jagfile_clear(&dat1_buildcache->versionlist_jagfile);
}

struct RSCache_FileListDat*
dat1_buildcache_get_versionlist_jagfile(struct Dat1BuildCache* dat1_buildcache)
{
    assert(dat1_buildcache);
    return dat1_buildcache->versionlist_jagfile;
}

void
dat1_buildcache_set_media_2d_graphics_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    struct RSCache_FileListDat* media_2d_graphics_jagfile)
{
    assert(dat1_buildcache);
    dat1_jagfile_set(&dat1_buildcache->media_2d_graphics_jagfile, media_2d_graphics_jagfile);
}

void
dat1_buildcache_clear_media_2d_graphics_jagfile(struct Dat1BuildCache* dat1_buildcache)
{
    assert(dat1_buildcache);
    dat1_jagfile_clear(&dat1_buildcache->media_2d_graphics_jagfile);
}

struct RSCache_FileListDat*
dat1_buildcache_get_media_2d_graphics_jagfile(struct Dat1BuildCache* dat1_buildcache)
{
    assert(dat1_buildcache);
    return dat1_buildcache->media_2d_graphics_jagfile;
}

void
dat1_buildcache_set_title_fonts_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    struct RSCache_FileListDat* title_fonts_jagfile)
{
    assert(dat1_buildcache);
    dat1_jagfile_set(&dat1_buildcache->title_fonts_jagfile, title_fonts_jagfile);
}

void
dat1_buildcache_clear_title_fonts_jagfile(struct Dat1BuildCache* dat1_buildcache)
{
    assert(dat1_buildcache);
    dat1_jagfile_clear(&dat1_buildcache->title_fonts_jagfile);
}

struct RSCache_FileListDat*
dat1_buildcache_get_title_fonts_jagfile(struct Dat1BuildCache* dat1_buildcache)
{
    assert(dat1_buildcache);
    return dat1_buildcache->title_fonts_jagfile;
}

void
dat1_buildcache_model_add(
    struct Dat1BuildCache* dat1_buildcache,
    int model_id,
    struct RSCache_Model* model)
{
    struct MapEntry_CacheModel* entry;

    assert(dat1_buildcache);
    assert(model);

    dat1_hmap_prepare_insert(&dat1_buildcache->base.model_cache);
    entry = (struct MapEntry_CacheModel*)hmap_search(
        dat1_buildcache->base.model_cache, &model_id, HMAP_INSERT);
    assert(entry && "Model must be inserted into hmap");

    entry->id = model_id;
    entry->model = model;
}

struct RSCache_Model*
dat1_buildcache_model_get(
    struct Dat1BuildCache* dat1_buildcache,
    int model_id)
{
    struct MapEntry_CacheModel* entry;

    assert(dat1_buildcache);

    entry = (struct MapEntry_CacheModel*)hmap_search(
        dat1_buildcache->base.model_cache, &model_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->model;
}

void
dat1_buildcache_model_remove(
    struct Dat1BuildCache* dat1_buildcache,
    int model_id)
{
    struct MapEntry_CacheModel* entry;

    assert(dat1_buildcache);

    entry = (struct MapEntry_CacheModel*)hmap_search(
        dat1_buildcache->base.model_cache, &model_id, HMAP_REMOVE);
    if( !entry || !entry->model )
        return;

    RSCache_ModelFree(entry->model);
    entry->model = NULL;
}

void
dat1_buildcache_models_cleanup(struct Dat1BuildCache* dat1_buildcache)
{
    struct HMapIter* iter;
    struct MapEntry_CacheModel* entry;

    assert(dat1_buildcache);
    if( !dat1_buildcache->base.model_cache )
        return;

    iter = hmap_iter_new(dat1_buildcache->base.model_cache);
    while( (entry = (struct MapEntry_CacheModel*)hmap_iter_next(iter)) )
    {
        if( entry->model )
            RSCache_ModelFree(entry->model);
    }
    hmap_iter_free(iter);

    dat1_hmap_reset(
        &dat1_buildcache->base.model_cache,
        sizeof(struct MapEntry_CacheModel),
        DAT1_MODEL_MAP_CAPACITY);
}

void
dat1_buildcache_map_terrain_add(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id,
    struct RSCache_MapTerrain* terrain)
{
    struct MapEntry_Terrain* entry;

    assert(dat1_buildcache);
    assert(terrain);

    dat1_hmap_prepare_insert(&dat1_buildcache->map_terrain_hmap);
    entry = (struct MapEntry_Terrain*)hmap_search(
        dat1_buildcache->map_terrain_hmap, &map_id, HMAP_INSERT);
    assert(entry && "Terrain must be inserted into hmap");

    entry->id = map_id;
    entry->terrain = terrain;
}

struct RSCache_MapTerrain*
dat1_buildcache_map_terrain_get(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id)
{
    struct MapEntry_Terrain* entry;

    assert(dat1_buildcache);

    entry = (struct MapEntry_Terrain*)hmap_search(
        dat1_buildcache->map_terrain_hmap, &map_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->terrain;
}

bool
dat1_buildcache_map_terrain_has(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id)
{
    return dat1_buildcache_map_terrain_get(dat1_buildcache, map_id) != NULL;
}

void
dat1_buildcache_map_terrain_cleanup(struct Dat1BuildCache* dat1_buildcache)
{
    struct HMapIter* iter;
    struct MapEntry_Terrain* entry;

    assert(dat1_buildcache);
    if( !dat1_buildcache->map_terrain_hmap )
        return;

    iter = hmap_iter_new(dat1_buildcache->map_terrain_hmap);
    while( (entry = (struct MapEntry_Terrain*)hmap_iter_next(iter)) )
    {
        if( entry->terrain )
            RSCache_MapTerrainFree(entry->terrain);
    }
    hmap_iter_free(iter);

    dat1_hmap_reset(
        &dat1_buildcache->map_terrain_hmap,
        sizeof(struct MapEntry_Terrain),
        DAT1_MAP_REGION_CAPACITY);
}

void
dat1_buildcache_map_scenery_add(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id,
    struct RSCache_MapLocs* locs)
{
    struct MapEntry_Scenery* entry;

    assert(dat1_buildcache);
    assert(locs);

    dat1_hmap_prepare_insert(&dat1_buildcache->map_scenery_hmap);
    entry = (struct MapEntry_Scenery*)hmap_search(
        dat1_buildcache->map_scenery_hmap, &map_id, HMAP_INSERT);
    assert(entry && "Scenery must be inserted into hmap");

    entry->id = map_id;
    entry->locs = locs;
}

struct RSCache_MapLocs*
dat1_buildcache_map_scenery_get(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id)
{
    struct MapEntry_Scenery* entry;

    assert(dat1_buildcache);

    entry = (struct MapEntry_Scenery*)hmap_search(
        dat1_buildcache->map_scenery_hmap, &map_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->locs;
}

bool
dat1_buildcache_map_scenery_has(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id)
{
    return dat1_buildcache_map_scenery_get(dat1_buildcache, map_id) != NULL;
}

void
dat1_buildcache_map_scenery_cleanup(struct Dat1BuildCache* dat1_buildcache)
{
    struct HMapIter* iter;
    struct MapEntry_Scenery* entry;

    assert(dat1_buildcache);
    if( !dat1_buildcache->map_scenery_hmap )
        return;

    iter = hmap_iter_new(dat1_buildcache->map_scenery_hmap);
    while( (entry = (struct MapEntry_Scenery*)hmap_iter_next(iter)) )
    {
        if( entry->locs )
            RSCache_MapLocsFree(entry->locs);
    }
    hmap_iter_free(iter);

    dat1_hmap_reset(
        &dat1_buildcache->map_scenery_hmap,
        sizeof(struct MapEntry_Scenery),
        DAT1_MAP_REGION_CAPACITY);
}

void
dat1_buildcache_prune(struct Dat1BuildCache* dat1_buildcache)
{
    assert(dat1_buildcache);

    dat1_buildcache_models_cleanup(dat1_buildcache);
    dat1_buildcache_map_terrain_cleanup(dat1_buildcache);
    dat1_buildcache_map_scenery_cleanup(dat1_buildcache);
}
