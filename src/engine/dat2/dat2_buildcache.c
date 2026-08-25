#include "engine/dat2/dat2_buildcache.h"

#include "engine/dat2/dat2_tasks.h"
#include "engine/dat2/task_dat2_sequence_load.h"

#include "datatypes/dat2_proctexture.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

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

struct MapEntry_ConfigObject
{
    int id;
    struct RSCache_Dat2ConfigObj* object;
};

struct MapEntry_ConfigNpctype
{
    int id;
    struct RSCache_Dat2ConfigNpc* npc;
};

struct MapEntry_ConfigBas
{
    int id;
    struct RSCache_Dat2ConfigBas* bas;
};

struct MapEntry_ConfigIdentkit
{
    int id;
    struct RSCache_Dat2ConfigIdk* idk;
};

struct MapEntry_ConfigLoc
{
    int id;
    struct RSCache_Dat2ConfigLoc* loc;
};

struct MapEntry_ConfigUnderlay
{
    int id;
    struct RSCache_Dat2ConfigUnderlay* underlay;
};

struct MapEntry_ConfigOverlay
{
    int id;
    struct RSCache_Dat2ConfigOverlay* overlay;
};

struct MapEntry_Texture
{
    int id;
    struct RSCache_Dat2Texture* texture;
};

#define DAT2_MODEL_MAP_CAPACITY 8192
#define DAT2_MAP_REGION_CAPACITY 512
#define DAT2_INTERFACE_MAP_CAPACITY 512
#define DAT2_CLIENTSCRIPT_MAP_CAPACITY 4096
#define DAT2_CONFIG_MAP_CAPACITY 4096
/* Split-group LRU budget, swept against tracked peak on the soft3d boot: 24 MB
 * cost 108.20, 8 MB 108.23, 4 MB 105.63, 2 MB 107.35. 4 MB is the knee.
 *
 * 24 MB never bound -- the census shows the cache topping out around 4 MB with
 * a 98.9% hit rate -- so the budget was only ever an accumulation ceiling. And
 * 2 MB is worse than 4, not better: Dat2GroupCache_Put splits the new group
 * before it evicts, so a put transiently holds the whole cache plus the new
 * blob, and a budget below the largest live group pays that twice over. */
#define DAT2_GROUP_CACHE_BUDGET ((size_t)4 * 1024 * 1024)

/* DAT2_GROUP_CACHE_MB overrides the budget so it can be swept without a
 * rebuild. An unparseable or out-of-range value falls back to the compiled
 * default rather than asserting: this reads a user's environment, not a
 * caller's argument. */
static size_t
dat2_group_cache_budget(void)
{
    char const* env = getenv("DAT2_GROUP_CACHE_MB");
    long mb;

    if( !env || env[0] == '\0' )
        return DAT2_GROUP_CACHE_BUDGET;
    mb = strtol(env, NULL, 10);
    if( mb <= 0 || mb > 4096 )
        return DAT2_GROUP_CACHE_BUDGET;
    return (size_t)mb * 1024 * 1024;
}

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
    assert(map);
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
    assert(new_buffer);

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

/*
 * TextureLoader.isSd for the dat2 provider.
 *
 * Only the procedural (RS2 materials) system distinguishes SD from HD-only: the material's
 * `valid` byte. The sprite-backed system answers true for everything, matching the
 * reference's SpriteTextureLoader. UNPROBED means no texture has loaded yet, so there is
 * nothing to say a texture is invalid — answer true rather than hide it; every caller that
 * matters runs after Task_WorldLoad has loaded terrain textures, which probes the mode.
 */
static bool
dat2_texture_is_sd(
    struct CacheProvider* provider,
    int texture_id)
{
    struct Dat2BuildCache* bc = (struct Dat2BuildCache*)provider;

    if( bc->proctex_mode != DAT2_PROCTEX_PROCEDURAL )
        return true;
    if( !bc->materials || texture_id < 0 || texture_id >= bc->materials->count )
        return true;
    return bc->materials->materials[texture_id].valid;
}

static struct CacheProviderVTable dat2_vtable = {
    .Task_ModelLoad = CreateTask_Dat2ModelLoad,
    .Task_ComponentPackLoad = CreateTask_Dat2ComponentPackLoad,
    .Task_ClientScriptLoad = CreateTask_Dat2ClientScriptLoad,
    .Task_ClientScriptTableLoad = CreateTask_Dat2ClientScriptTableLoad,
    .ClientScriptIdByNameHash = dat2_clientscript_id_by_name_hash,
    .Task_ObjLoad = CreateTask_Dat2ObjLoad,
    .Task_ObjLoadAll = CreateTask_Dat2ObjLoadAll,
    .Task_NpcLoad = CreateTask_Dat2NpcLoad,
    .Task_SpotanimLoad = CreateTask_Dat2SpotanimLoad,
    .Task_SoundLoad = CreateTask_Dat2SoundLoad,
    .Task_MusicLoad = CreateTask_Dat2MusicLoad,
    .Task_IdkLoad = CreateTask_Dat2IdkLoad,
    .Task_MapTerrainLoad = CreateTask_Dat2MapTerrainLoad,
    .Task_MapSceneryLoad = CreateTask_Dat2MapSceneryLoad,
    .Task_LocLoad = CreateTask_Dat2LocLoad,
    .Task_FlotypeLoad = CreateTask_Dat2FlotypeLoad,
    .Task_UnderlayLoad = CreateTask_Dat2UnderlayLoad,
    .Task_TextureLoad = CreateTask_Dat2TextureLoad,
    .Task_SequenceLoad = CreateTask_Dat2SequenceLoad,
    .Task_SpriteLoad = CreateTask_Dat2SpriteLoad,
    .Task_SpriteLoadByName = CreateTask_Dat2SpriteLoadByName,
    .Task_FontLoad = CreateTask_Dat2FontLoad,
    .Task_EnumLoad = CreateTask_Dat2EnumLoad,
    .Task_StructLoad = CreateTask_Dat2StructLoad,
    .Task_ParamLoad = CreateTask_Dat2ParamLoad,
    .Task_InvtypeLoad = CreateTask_Dat2InvtypeLoad,
    .Task_DbRowLoad = CreateTask_Dat2DbRowLoad,
    .Task_DbTableLoad = CreateTask_Dat2DbTableLoad,
    .Task_DbTableIndexLoad = CreateTask_Dat2DbTableIndexLoad,
    .Task_ComponentLoad = CreateTask_Dat2ComponentLoad,
    .Task_WorldMapLoad = CreateTask_Dat2WorldMapLoad,
    .Task_MapElementLoad = CreateTask_Dat2MapElementLoad,
    .Task_WorldMapGeographyLoad = CreateTask_Dat2WorldMapGeographyLoad,
    .TextureIsSd = dat2_texture_is_sd,
};

struct Dat2BuildCache*
dat2_buildcache_new(void)
{
    struct Dat2BuildCache* dat2_buildcache = calloc(1, sizeof(*dat2_buildcache));
    assert(dat2_buildcache);

    dat2_buildcache->base.vtable = &dat2_vtable;
    CacheProvider_InitEngineCaches(&dat2_buildcache->base);
    /* Enough for the handful of config groups a boot reads back to back (the
     * loc group alone is several MB) without letting every table stay resident
     * for the whole session. */
    dat2_buildcache->group_cache =
        Dat2GroupCache_New(dat2_group_cache_budget());
    dat2_buildcache->models_hmap = dat2_buildcache_map_new(
        dat2_buildcache, sizeof(struct MapEntry_CacheModel), DAT2_MODEL_MAP_CAPACITY);
    dat2_buildcache->componentpacks_hmap = dat2_buildcache_map_new(
        dat2_buildcache, sizeof(struct MapEntry_ComponentPack), DAT2_INTERFACE_MAP_CAPACITY);
    dat2_buildcache->object_hmap = dat2_buildcache_map_new(
        dat2_buildcache, sizeof(struct MapEntry_ConfigObject), DAT2_CONFIG_MAP_CAPACITY);
    dat2_buildcache->npctype_hmap = dat2_buildcache_map_new(
        dat2_buildcache, sizeof(struct MapEntry_ConfigNpctype), DAT2_CONFIG_MAP_CAPACITY);
    dat2_buildcache->bas_hmap = dat2_buildcache_map_new(
        dat2_buildcache, sizeof(struct MapEntry_ConfigBas), DAT2_CONFIG_MAP_CAPACITY);
    dat2_buildcache->identkit_hmap = dat2_buildcache_map_new(
        dat2_buildcache, sizeof(struct MapEntry_ConfigIdentkit), DAT2_CONFIG_MAP_CAPACITY);
    dat2_buildcache->map_terrain_hmap = dat2_buildcache_map_new(
        dat2_buildcache, sizeof(struct MapEntry_Terrain), DAT2_MAP_REGION_CAPACITY);
    dat2_buildcache->map_scenery_hmap = dat2_buildcache_map_new(
        dat2_buildcache, sizeof(struct MapEntry_Scenery), DAT2_MAP_REGION_CAPACITY);
    dat2_buildcache->loc_hmap = dat2_buildcache_map_new(
        dat2_buildcache, sizeof(struct MapEntry_ConfigLoc), DAT2_CONFIG_MAP_CAPACITY);
    dat2_buildcache->underlay_hmap = dat2_buildcache_map_new(
        dat2_buildcache, sizeof(struct MapEntry_ConfigUnderlay), DAT2_CONFIG_MAP_CAPACITY);
    dat2_buildcache->overlay_hmap = dat2_buildcache_map_new(
        dat2_buildcache, sizeof(struct MapEntry_ConfigOverlay), DAT2_CONFIG_MAP_CAPACITY);
    dat2_buildcache->texture_hmap = dat2_buildcache_map_new(
        dat2_buildcache, sizeof(struct MapEntry_Texture), DAT2_CONFIG_MAP_CAPACITY);
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
    dat2_buildcache_locs_cleanup(dat2_buildcache);
    dat2_buildcache_underlays_cleanup(dat2_buildcache);
    dat2_buildcache_overlays_cleanup(dat2_buildcache);
    dat2_buildcache_textures_cleanup(dat2_buildcache);
    dat2_buildcache_componentpacks_cleanup(dat2_buildcache);
    dat2_buildcache_objects_cleanup(dat2_buildcache);
    dat2_buildcache_npctypes_cleanup(dat2_buildcache);
    dat2_buildcache_bas_cleanup(dat2_buildcache);
    dat2_buildcache_identkits_cleanup(dat2_buildcache);
    dat2_buildcache_clientscripts_cleanup(dat2_buildcache);
    dat2_buildcache_reference_tables_cleanup(dat2_buildcache);

    dat2_buildcache_map_free(dat2_buildcache, dat2_buildcache->models_hmap);
    dat2_buildcache_map_free(dat2_buildcache, dat2_buildcache->componentpacks_hmap);
    dat2_buildcache_map_free(dat2_buildcache, dat2_buildcache->object_hmap);
    dat2_buildcache_map_free(dat2_buildcache, dat2_buildcache->npctype_hmap);
    dat2_buildcache_map_free(dat2_buildcache, dat2_buildcache->bas_hmap);
    dat2_buildcache_map_free(dat2_buildcache, dat2_buildcache->identkit_hmap);
    dat2_buildcache_map_free(dat2_buildcache, dat2_buildcache->map_terrain_hmap);
    dat2_buildcache_map_free(dat2_buildcache, dat2_buildcache->map_scenery_hmap);
    dat2_buildcache_map_free(dat2_buildcache, dat2_buildcache->loc_hmap);
    dat2_buildcache_map_free(dat2_buildcache, dat2_buildcache->underlay_hmap);
    dat2_buildcache_map_free(dat2_buildcache, dat2_buildcache->overlay_hmap);
    dat2_buildcache_map_free(dat2_buildcache, dat2_buildcache->texture_hmap);
    dat2_buildcache_map_free(dat2_buildcache, dat2_buildcache->clientscripts_hmap);
    Dat2GroupCache_Free(dat2_buildcache->group_cache);
    dat2_buildcache->group_cache = NULL;
    CacheProvider_FreeEngineCaches(&dat2_buildcache->base);
    RSCache_VorbisSetupFree(dat2_buildcache->rs2012_vorbis_setup);
    dat2_buildcache->rs2012_vorbis_setup = NULL;

    for( table_id = 0; table_id < RSCACHE_DAT2_TABLE_COUNT; table_id++ )
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

    dat2_buildcache_prepare_hmap_insert(dat2_buildcache, &dat2_buildcache->models_hmap);
    entry = (struct MapEntry_CacheModel*)hmap_search(
        dat2_buildcache->models_hmap, &model_id, HMAP_INSERT);
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
        dat2_buildcache->models_hmap, &model_id, HMAP_FIND);
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
        dat2_buildcache->models_hmap, &model_id, HMAP_REMOVE);
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
    if( !dat2_buildcache->models_hmap )
        return;

    iter = hmap_iter_new(dat2_buildcache->models_hmap);
    while( (entry = (struct MapEntry_CacheModel*)hmap_iter_next(iter)) )
    {
        if( entry->model )
            RSCache_ModelFree(entry->model);
    }
    hmap_iter_free(iter);

    dat2_buildcache_map_reset(
        dat2_buildcache,
        &dat2_buildcache->models_hmap,
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

    dat2_buildcache_prepare_hmap_insert(dat2_buildcache, &dat2_buildcache->componentpacks_hmap);
    entry = (struct MapEntry_ComponentPack*)hmap_search(
        dat2_buildcache->componentpacks_hmap, &iface_id, HMAP_INSERT);
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
        dat2_buildcache->componentpacks_hmap, &iface_id, HMAP_FIND);
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
    if( !dat2_buildcache->componentpacks_hmap )
        return;

    iter = hmap_iter_new(dat2_buildcache->componentpacks_hmap);
    while( (entry = (struct MapEntry_ComponentPack*)hmap_iter_next(iter)) )
    {
        if( entry->pack )
            RSCache_Dat2ComponentPackFree(entry->pack);
    }
    hmap_iter_free(iter);

    dat2_buildcache_map_reset(
        dat2_buildcache,
        &dat2_buildcache->componentpacks_hmap,
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
    int table,
    struct RSCache_ReferenceTable* reference_table)
{
    assert(dat2_buildcache);
    assert(reference_table);
    assert(table >= 0 && table < RSCACHE_DAT2_TABLE_COUNT);

    if( dat2_buildcache->reference_tables[table] )
        RSCache_ReferenceTableFree(dat2_buildcache->reference_tables[table]);
    dat2_buildcache->reference_tables[table] = reference_table;
}

struct RSCache_ReferenceTable*
dat2_buildcache_reference_table_get(
    struct Dat2BuildCache* dat2_buildcache,
    int table)
{
    assert(dat2_buildcache);
    if( table < 0 || table >= RSCACHE_DAT2_TABLE_COUNT )
        return NULL;
    return dat2_buildcache->reference_tables[table];
}

bool
dat2_buildcache_reference_table_has(
    struct Dat2BuildCache* dat2_buildcache,
    int table)
{
    return dat2_buildcache_reference_table_get(dat2_buildcache, table) != NULL;
}

void
dat2_buildcache_reference_tables_cleanup(struct Dat2BuildCache* dat2_buildcache)
{
    int table_id;

    free(dat2_buildcache->clientscript_names);
    dat2_buildcache->clientscript_names = NULL;
    dat2_buildcache->clientscript_name_count = 0;

    assert(dat2_buildcache);

    for( table_id = 0; table_id < RSCACHE_DAT2_TABLE_COUNT; table_id++ )
    {
        if( dat2_buildcache->reference_tables[table_id] )
        {
            RSCache_ReferenceTableFree(dat2_buildcache->reference_tables[table_id]);
            dat2_buildcache->reference_tables[table_id] = NULL;
        }
    }
}

static bool
dat2_id_wanted(
    int id,
    const int* wanted_ids,
    int wanted_count)
{
    if( !wanted_ids )
        return true;

    for( int i = 0; i < wanted_count; i++ )
    {
        if( wanted_ids[i] == id )
            return true;
    }
    return false;
}

void
dat2_buildcache_object_add(
    struct Dat2BuildCache* dat2_buildcache,
    int obj_id,
    struct RSCache_Dat2ConfigObj* object)
{
    struct MapEntry_ConfigObject* entry;

    assert(dat2_buildcache);
    assert(object);

    dat2_buildcache_prepare_hmap_insert(dat2_buildcache, &dat2_buildcache->object_hmap);
    entry = (struct MapEntry_ConfigObject*)hmap_search(
        dat2_buildcache->object_hmap, &obj_id, HMAP_INSERT);
    assert(entry && "Object must be inserted into hmap");

    if( entry->object )
        RSCache_Dat2ConfigObjFree(entry->object);

    entry->id = obj_id;
    entry->object = object;
}

struct RSCache_Dat2ConfigObj*
dat2_buildcache_object_get(
    struct Dat2BuildCache* dat2_buildcache,
    int obj_id)
{
    struct MapEntry_ConfigObject* entry;

    assert(dat2_buildcache);

    entry = (struct MapEntry_ConfigObject*)hmap_search(
        dat2_buildcache->object_hmap, &obj_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->object;
}

bool
dat2_buildcache_object_has(
    struct Dat2BuildCache* dat2_buildcache,
    int obj_id)
{
    return dat2_buildcache_object_get(dat2_buildcache, obj_id) != NULL;
}

void
dat2_buildcache_objects_cleanup(struct Dat2BuildCache* dat2_buildcache)
{
    struct HMapIter* iter;
    struct MapEntry_ConfigObject* entry;

    assert(dat2_buildcache);
    if( !dat2_buildcache->object_hmap )
        return;

    iter = hmap_iter_new(dat2_buildcache->object_hmap);
    while( (entry = (struct MapEntry_ConfigObject*)hmap_iter_next(iter)) )
    {
        if( entry->object )
            RSCache_Dat2ConfigObjFree(entry->object);
    }
    hmap_iter_free(iter);

    dat2_buildcache_map_reset(
        dat2_buildcache,
        &dat2_buildcache->object_hmap,
        sizeof(struct MapEntry_ConfigObject),
        DAT2_CONFIG_MAP_CAPACITY);
}

static void
dat2_buildcache_objects_init_from_filelist(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCache_FileList* filelist,
    struct RSCache_Dat2DiskArchive* archive,
    const int* wanted_ids,
    int wanted_count)
{
    for( int i = 0; i < filelist->file_count; i++ )
    {
        int id = archive->file_ids[i];
        struct RSCache_Dat2ConfigObj* object;

        if( !dat2_id_wanted(id, wanted_ids, wanted_count) )
            continue;
        if( dat2_buildcache_object_get(dat2_buildcache, id) )
            continue;

        /* Profile, not flags-0 — same reason as the loc decode below. A rev-239
         * obj record can carry opcodes 160 and 200-202, and the flags-0 decoder
         * treats an unknown opcode as "stop, do not misalign", so every field
         * after the first one of those is silently dropped. `params` (249) is
         * written last, so it was the field that always went. */
        object = RSCache_Dat2ConfigObjNewDecodeProfile(
            CacheProvider_Profile(&dat2_buildcache->base),
            filelist->files[i],
            filelist->file_sizes[i]);
        if( !object )
            continue;

        object->_id = id;
        dat2_buildcache_object_add(dat2_buildcache, id, object);
    }
}

void
dat2_buildcache_objects_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCache_Dat2DiskArchive* archive,
    const int* wanted_ids,
    int wanted_count)
{
    struct RSCache_FileList* filelist;

    assert(dat2_buildcache);
    assert(archive);
    assert(archive->file_ids);

    filelist = RSCache_FileListNewFromDecode(
        archive->data, archive->data_size, archive->file_count);
    if( !filelist )
        return;

    dat2_buildcache_objects_init_from_filelist(
        dat2_buildcache, filelist, archive, wanted_ids, wanted_count);
    RSCache_FileListFree(filelist);
}

void
dat2_buildcache_npctype_add(
    struct Dat2BuildCache* dat2_buildcache,
    int npc_id,
    struct RSCache_Dat2ConfigNpc* npc)
{
    struct MapEntry_ConfigNpctype* entry;

    assert(dat2_buildcache);
    assert(npc);

    dat2_buildcache_prepare_hmap_insert(dat2_buildcache, &dat2_buildcache->npctype_hmap);
    entry = (struct MapEntry_ConfigNpctype*)hmap_search(
        dat2_buildcache->npctype_hmap, &npc_id, HMAP_INSERT);
    assert(entry && "Npc must be inserted into hmap");

    if( entry->npc )
        RSCache_Dat2ConfigNpcFree(entry->npc);

    entry->id = npc_id;
    entry->npc = npc;
}

struct RSCache_Dat2ConfigNpc*
dat2_buildcache_npctype_get(
    struct Dat2BuildCache* dat2_buildcache,
    int npc_id)
{
    struct MapEntry_ConfigNpctype* entry;

    assert(dat2_buildcache);

    entry = (struct MapEntry_ConfigNpctype*)hmap_search(
        dat2_buildcache->npctype_hmap, &npc_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->npc;
}

bool
dat2_buildcache_npctype_has(
    struct Dat2BuildCache* dat2_buildcache,
    int npc_id)
{
    return dat2_buildcache_npctype_get(dat2_buildcache, npc_id) != NULL;
}

void
dat2_buildcache_npctypes_cleanup(struct Dat2BuildCache* dat2_buildcache)
{
    struct HMapIter* iter;
    struct MapEntry_ConfigNpctype* entry;

    assert(dat2_buildcache);
    if( !dat2_buildcache->npctype_hmap )
        return;

    iter = hmap_iter_new(dat2_buildcache->npctype_hmap);
    while( (entry = (struct MapEntry_ConfigNpctype*)hmap_iter_next(iter)) )
    {
        if( entry->npc )
            RSCache_Dat2ConfigNpcFree(entry->npc);
    }
    hmap_iter_free(iter);

    dat2_buildcache_map_reset(
        dat2_buildcache,
        &dat2_buildcache->npctype_hmap,
        sizeof(struct MapEntry_ConfigNpctype),
        DAT2_CONFIG_MAP_CAPACITY);
}

void
dat2_buildcache_npctypes_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCache_Dat2DiskArchive* archive,
    const int* wanted_ids,
    int wanted_count)
{
    dat2_buildcache_npctypes_init_from_archive_based(
        dat2_buildcache, archive, wanted_ids, wanted_count, 0);
}

void
dat2_buildcache_npctypes_init_from_archive_based(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCache_Dat2DiskArchive* archive,
    const int* wanted_ids,
    int wanted_count,
    int base_id)
{
    struct RSCache_FileList* filelist;

    assert(dat2_buildcache);
    assert(archive);
    assert(archive->file_ids);

    filelist = RSCache_FileListNewFromDecode(
        archive->data, archive->data_size, archive->file_count);
    if( !filelist )
        return;

    for( int i = 0; i < filelist->file_count; i++ )
    {
        int id = archive->file_ids[i];
        struct RSCache_Dat2ConfigNpc* npc;

        if( !dat2_id_wanted(id, wanted_ids, wanted_count) )
            continue;
        /* Global id — same D19 trap as locs: sharded groups number files
         * 0..mask locally, so testing `id` alone skips every later group once
         * group 0 is resident. */
        if( dat2_buildcache_npctype_get(dat2_buildcache, base_id + id) )
            continue;

        npc = RSCache_Dat2ConfigNpcNewDecodeProfile(
            CacheProvider_Profile(&dat2_buildcache->base),
            filelist->files[i],
            filelist->file_sizes[i]);
        if( !npc )
            continue;

        dat2_buildcache_npctype_add(dat2_buildcache, base_id + id, npc);
    }

    RSCache_FileListFree(filelist);
}

static void
dat2_buildcache_bas_add(
    struct Dat2BuildCache* dat2_buildcache,
    int bas_id,
    struct RSCache_Dat2ConfigBas* bas)
{
    struct MapEntry_ConfigBas* entry;

    assert(dat2_buildcache);
    assert(bas);

    dat2_buildcache_prepare_hmap_insert(dat2_buildcache, &dat2_buildcache->bas_hmap);
    entry = (struct MapEntry_ConfigBas*)hmap_search(
        dat2_buildcache->bas_hmap, &bas_id, HMAP_INSERT);
    assert(entry && "Bas must be inserted into hmap");

    if( entry->bas )
        RSCache_Dat2ConfigBasFree(entry->bas);

    entry->id = bas_id;
    entry->bas = bas;
}

struct RSCache_Dat2ConfigBas*
dat2_buildcache_bas_get(
    struct Dat2BuildCache* dat2_buildcache,
    int bas_id)
{
    struct MapEntry_ConfigBas* entry;

    assert(dat2_buildcache);

    entry = (struct MapEntry_ConfigBas*)hmap_search(
        dat2_buildcache->bas_hmap, &bas_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->bas;
}

void
dat2_buildcache_bas_cleanup(struct Dat2BuildCache* dat2_buildcache)
{
    struct HMapIter* iter;
    struct MapEntry_ConfigBas* entry;

    assert(dat2_buildcache);
    if( !dat2_buildcache->bas_hmap )
        return;

    iter = hmap_iter_new(dat2_buildcache->bas_hmap);
    while( (entry = (struct MapEntry_ConfigBas*)hmap_iter_next(iter)) )
    {
        if( entry->bas )
            RSCache_Dat2ConfigBasFree(entry->bas);
    }
    hmap_iter_free(iter);

    dat2_buildcache_map_reset(
        dat2_buildcache,
        &dat2_buildcache->bas_hmap,
        sizeof(struct MapEntry_ConfigBas),
        DAT2_CONFIG_MAP_CAPACITY);
}

void
dat2_buildcache_bas_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCache_Dat2DiskArchive* archive)
{
    struct RSCache_FileList* filelist;

    assert(dat2_buildcache);
    assert(archive);

    filelist = RSCache_FileListNewFromDecode(
        archive->data, archive->data_size, archive->file_count);
    if( !filelist )
        return;

    for( int i = 0; i < filelist->file_count; i++ )
    {
        int id = archive->file_ids ? archive->file_ids[i] : i;
        struct RSCache_Dat2ConfigBas* bas;

        if( dat2_buildcache_bas_get(dat2_buildcache, id) )
            continue;

        bas = RSCache_Dat2ConfigBasNewDecode(filelist->files[i], filelist->file_sizes[i]);
        if( !bas )
            continue;

        dat2_buildcache_bas_add(dat2_buildcache, id, bas);
    }

    RSCache_FileListFree(filelist);
}

void
dat2_buildcache_identkit_add(
    struct Dat2BuildCache* dat2_buildcache,
    int idk_id,
    struct RSCache_Dat2ConfigIdk* idk)
{
    struct MapEntry_ConfigIdentkit* entry;

    assert(dat2_buildcache);
    assert(idk);

    dat2_buildcache_prepare_hmap_insert(dat2_buildcache, &dat2_buildcache->identkit_hmap);
    entry = (struct MapEntry_ConfigIdentkit*)hmap_search(
        dat2_buildcache->identkit_hmap, &idk_id, HMAP_INSERT);
    assert(entry && "Identkit must be inserted into hmap");

    if( entry->idk )
        RSCache_Dat2ConfigIdkFree(entry->idk);

    entry->id = idk_id;
    entry->idk = idk;
}

struct RSCache_Dat2ConfigIdk*
dat2_buildcache_identkit_get(
    struct Dat2BuildCache* dat2_buildcache,
    int idk_id)
{
    struct MapEntry_ConfigIdentkit* entry;

    assert(dat2_buildcache);

    entry = (struct MapEntry_ConfigIdentkit*)hmap_search(
        dat2_buildcache->identkit_hmap, &idk_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->idk;
}

bool
dat2_buildcache_identkit_has(
    struct Dat2BuildCache* dat2_buildcache,
    int idk_id)
{
    return dat2_buildcache_identkit_get(dat2_buildcache, idk_id) != NULL;
}

void
dat2_buildcache_identkits_cleanup(struct Dat2BuildCache* dat2_buildcache)
{
    struct HMapIter* iter;
    struct MapEntry_ConfigIdentkit* entry;

    assert(dat2_buildcache);
    if( !dat2_buildcache->identkit_hmap )
        return;

    iter = hmap_iter_new(dat2_buildcache->identkit_hmap);
    while( (entry = (struct MapEntry_ConfigIdentkit*)hmap_iter_next(iter)) )
    {
        if( entry->idk )
            RSCache_Dat2ConfigIdkFree(entry->idk);
    }
    hmap_iter_free(iter);

    dat2_buildcache_map_reset(
        dat2_buildcache,
        &dat2_buildcache->identkit_hmap,
        sizeof(struct MapEntry_ConfigIdentkit),
        DAT2_CONFIG_MAP_CAPACITY);
}

static void
dat2_buildcache_identkits_init_from_filelist(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCache_FileList* filelist,
    struct RSCache_Dat2DiskArchive* archive,
    const int* wanted_ids,
    int wanted_count)
{
    for( int i = 0; i < filelist->file_count; i++ )
    {
        int id = archive->file_ids[i];
        struct RSCache_Dat2ConfigIdk* idk;

        if( !dat2_id_wanted(id, wanted_ids, wanted_count) )
            continue;
        if( dat2_buildcache_identkit_get(dat2_buildcache, id) )
            continue;

        idk = RSCache_Dat2ConfigIdkNewDecode(filelist->files[i], filelist->file_sizes[i]);
        if( !idk )
            continue;

        idk->_id = id;
        dat2_buildcache_identkit_add(dat2_buildcache, id, idk);
    }
}

void
dat2_buildcache_identkits_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCache_Dat2DiskArchive* archive,
    const int* wanted_ids,
    int wanted_count)
{
    struct RSCache_FileList* filelist;

    assert(dat2_buildcache);
    assert(archive);
    assert(archive->file_ids);

    filelist = RSCache_FileListNewFromDecode(
        archive->data, archive->data_size, archive->file_count);
    if( !filelist )
        return;

    dat2_buildcache_identkits_init_from_filelist(
        dat2_buildcache, filelist, archive, wanted_ids, wanted_count);
    RSCache_FileListFree(filelist);
}

void
dat2_buildcache_loc_add(
    struct Dat2BuildCache* dat2_buildcache,
    int loc_id,
    struct RSCache_Dat2ConfigLoc* loc)
{
    struct MapEntry_ConfigLoc* entry;

    assert(dat2_buildcache);
    assert(loc);

    dat2_buildcache_prepare_hmap_insert(dat2_buildcache, &dat2_buildcache->loc_hmap);
    entry = (struct MapEntry_ConfigLoc*)hmap_search(
        dat2_buildcache->loc_hmap, &loc_id, HMAP_INSERT);
    assert(entry && "Loc must be inserted into hmap");

    if( entry->loc )
        RSCache_Dat2ConfigLocFree(entry->loc);

    entry->id = loc_id;
    entry->loc = loc;
}

struct RSCache_Dat2ConfigLoc*
dat2_buildcache_loc_get(
    struct Dat2BuildCache* dat2_buildcache,
    int loc_id)
{
    struct MapEntry_ConfigLoc* entry;

    assert(dat2_buildcache);

    entry = (struct MapEntry_ConfigLoc*)hmap_search(
        dat2_buildcache->loc_hmap, &loc_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->loc;
}

bool
dat2_buildcache_loc_has(
    struct Dat2BuildCache* dat2_buildcache,
    int loc_id)
{
    return dat2_buildcache_loc_get(dat2_buildcache, loc_id) != NULL;
}

void
dat2_buildcache_locs_cleanup(struct Dat2BuildCache* dat2_buildcache)
{
    struct HMapIter* iter;
    struct MapEntry_ConfigLoc* entry;

    assert(dat2_buildcache);
    if( !dat2_buildcache->loc_hmap )
        return;

    iter = hmap_iter_new(dat2_buildcache->loc_hmap);
    while( (entry = (struct MapEntry_ConfigLoc*)hmap_iter_next(iter)) )
    {
        if( entry->loc )
            RSCache_Dat2ConfigLocFree(entry->loc);
    }
    hmap_iter_free(iter);

    dat2_buildcache_map_reset(
        dat2_buildcache,
        &dat2_buildcache->loc_hmap,
        sizeof(struct MapEntry_ConfigLoc),
        DAT2_CONFIG_MAP_CAPACITY);
}

void
dat2_buildcache_locs_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCache_Dat2DiskArchive* archive,
    const int* wanted_ids,
    int wanted_count)
{
    dat2_buildcache_locs_init_from_archive_based(
        dat2_buildcache, archive, wanted_ids, wanted_count, 0);
}

void
dat2_buildcache_locs_init_from_archive_based(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCache_Dat2DiskArchive* archive,
    const int* wanted_ids,
    int wanted_count,
    int base_id)
{
    struct RSCache_FileList* filelist;

    assert(dat2_buildcache);
    assert(archive);
    assert(archive->file_ids);

    filelist = RSCache_FileListNewFromDecode(
        archive->data, archive->data_size, archive->file_count);
    if( !filelist )
        return;

    /* TORIRS_LOC_SCAN=1: exact-consumption scan — decode every file under
     * each era-flag combination and report how many consume the file exactly.
     * Pins the correct payload flags for this cache empirically (misaligned
     * decodes stop early or overrun). */
    if( getenv("TORIRS_LOC_SCAN") )
    {
        static const struct
        {
            char const* name;
            int flags;
        } combos[] = {
            { "plain", RSCACHE_CONFIG_LOC_DECODE_DAT2 },
            { "large_ids", RSCACHE_CONFIG_LOC_DECODE_LARGE_MODEL_IDS },
            { "osrs220", RSCACHE_CONFIG_LOC_DECODE_OSRS_220 },
            { "large_ids+osrs220",
              RSCACHE_CONFIG_LOC_DECODE_LARGE_MODEL_IDS | RSCACHE_CONFIG_LOC_DECODE_OSRS_220 },
        };
        fprintf(
            stderr,
            "loc_scan: archive revision=%d files=%d (flags for revision: 0x%x)\n",
            archive->revision,
            filelist->file_count,
            RSCache_Dat2ConfigLocFlags(CacheProvider_Profile(&dat2_buildcache->base)));
        for( size_t c = 0; c < sizeof(combos) / sizeof(combos[0]); c++ )
        {
            int exact = 0;
            int first_bad = -1;
            for( int i = 0; i < filelist->file_count; i++ )
            {
                struct RSCache_Dat2ConfigLoc scan_loc;
                RSCache_Dat2ConfigLocDecodeInplace(
                    &scan_loc, filelist->files[i], filelist->file_sizes[i], combos[c].flags);
                if( scan_loc._consumed == filelist->file_sizes[i] )
                    exact++;
                else if( first_bad < 0 )
                    first_bad = archive->file_ids[i];
                RSCache_Dat2ConfigLocFreeInplace(&scan_loc);
            }
            fprintf(
                stderr,
                "loc_scan: %-18s exact=%d/%d first_mismatch_id=%d\n",
                combos[c].name,
                exact,
                filelist->file_count,
                first_bad);
        }
    }

    for( int i = 0; i < filelist->file_count; i++ )
    {
        int id = archive->file_ids[i];
        struct RSCache_Dat2ConfigLoc* loc;

        if( !dat2_id_wanted(id, wanted_ids, wanted_count) )
            continue;
        /* The *global* id, not the group-local one. A sharded group numbers its files
         * 0..255 locally, so testing `id` here asked "is loc 0..255 already loaded" for
         * every group — and once group 0 was in, that was true, so every later group was
         * skipped wholesale. Group 0 was the only one that worked, because there
         * `base_id + id == id` and the bug is invisible. */
        if( dat2_buildcache_loc_get(dat2_buildcache, base_id + id) )
            continue;

        /* Profile, not the archive revision. Equivalent on the caches the client
         * boots — a modern archive revision is a timestamp, which clears the same
         * OSRS-220 gate the declared revision does — but the profile additionally
         * carries the container and the Kronos quirk, neither of which a revision
         * number can imply. */
        loc = RSCache_Dat2ConfigLocNewDecodeProfile(
            CacheProvider_Profile(&dat2_buildcache->base),
            filelist->files[i],
            filelist->file_sizes[i]);
        if( !loc )
            continue;

        /* `base_id` is 0 for the OSRS config-group layout, where the file id already *is*
         * the loc id. A sharded RS2 group numbers its files 0..255 locally, so the group's
         * base has to be added back to reach the global id. */
        loc->_id = base_id + id;
        dat2_buildcache_loc_add(dat2_buildcache, base_id + id, loc);
    }

    RSCache_FileListFree(filelist);
}

void
dat2_buildcache_underlay_add(
    struct Dat2BuildCache* dat2_buildcache,
    int underlay_id,
    struct RSCache_Dat2ConfigUnderlay* underlay)
{
    struct MapEntry_ConfigUnderlay* entry;

    assert(dat2_buildcache);
    assert(underlay);

    dat2_buildcache_prepare_hmap_insert(dat2_buildcache, &dat2_buildcache->underlay_hmap);
    entry = (struct MapEntry_ConfigUnderlay*)hmap_search(
        dat2_buildcache->underlay_hmap, &underlay_id, HMAP_INSERT);
    assert(entry && "Underlay must be inserted into hmap");

    if( entry->underlay )
        RSCache_Dat2ConfigUnderlayFree(entry->underlay);

    entry->id = underlay_id;
    entry->underlay = underlay;
}

struct RSCache_Dat2ConfigUnderlay*
dat2_buildcache_underlay_get(
    struct Dat2BuildCache* dat2_buildcache,
    int underlay_id)
{
    struct MapEntry_ConfigUnderlay* entry;

    assert(dat2_buildcache);

    entry = (struct MapEntry_ConfigUnderlay*)hmap_search(
        dat2_buildcache->underlay_hmap, &underlay_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->underlay;
}

bool
dat2_buildcache_underlay_has(
    struct Dat2BuildCache* dat2_buildcache,
    int underlay_id)
{
    return dat2_buildcache_underlay_get(dat2_buildcache, underlay_id) != NULL;
}

void
dat2_buildcache_underlays_cleanup(struct Dat2BuildCache* dat2_buildcache)
{
    struct HMapIter* iter;
    struct MapEntry_ConfigUnderlay* entry;

    assert(dat2_buildcache);
    if( !dat2_buildcache->underlay_hmap )
        return;

    iter = hmap_iter_new(dat2_buildcache->underlay_hmap);
    while( (entry = (struct MapEntry_ConfigUnderlay*)hmap_iter_next(iter)) )
    {
        if( entry->underlay )
            RSCache_Dat2ConfigUnderlayFree(entry->underlay);
    }
    hmap_iter_free(iter);

    dat2_buildcache_map_reset(
        dat2_buildcache,
        &dat2_buildcache->underlay_hmap,
        sizeof(struct MapEntry_ConfigUnderlay),
        DAT2_CONFIG_MAP_CAPACITY);
}

void
dat2_buildcache_underlays_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCache_Dat2DiskArchive* archive,
    const int* wanted_ids,
    int wanted_count)
{
    int flo_flags =
        RSCache_Dat2ConfigFloFlags(CacheProvider_Profile(&dat2_buildcache->base));
    struct RSCache_FileList* filelist;

    assert(dat2_buildcache);
    assert(archive);
    assert(archive->file_ids);

    filelist = RSCache_FileListNewFromDecode(
        archive->data, archive->data_size, archive->file_count);
    if( !filelist )
        return;

    for( int i = 0; i < filelist->file_count; i++ )
    {
        int id = archive->file_ids[i];
        struct RSCache_Dat2ConfigUnderlay* underlay;

        if( !dat2_id_wanted(id, wanted_ids, wanted_count) )
            continue;
        if( dat2_buildcache_underlay_get(dat2_buildcache, id) )
            continue;

        underlay = malloc(sizeof(struct RSCache_Dat2ConfigUnderlay));
        RSCache_Dat2ConfigUnderlayDecodeInplaceFlags(
            underlay, filelist->files[i], filelist->file_sizes[i], flo_flags);
        if( !underlay )
            continue;

        underlay->_id = id;
        dat2_buildcache_underlay_add(dat2_buildcache, id, underlay);
    }

    RSCache_FileListFree(filelist);
}

void
dat2_buildcache_overlay_add(
    struct Dat2BuildCache* dat2_buildcache,
    int overlay_id,
    struct RSCache_Dat2ConfigOverlay* overlay)
{
    struct MapEntry_ConfigOverlay* entry;

    assert(dat2_buildcache);
    assert(overlay);

    dat2_buildcache_prepare_hmap_insert(dat2_buildcache, &dat2_buildcache->overlay_hmap);
    entry = (struct MapEntry_ConfigOverlay*)hmap_search(
        dat2_buildcache->overlay_hmap, &overlay_id, HMAP_INSERT);
    assert(entry && "Overlay must be inserted into hmap");

    if( entry->overlay )
        RSCache_Dat2ConfigOverlayFree(entry->overlay);

    entry->id = overlay_id;
    entry->overlay = overlay;
}

struct RSCache_Dat2ConfigOverlay*
dat2_buildcache_overlay_get(
    struct Dat2BuildCache* dat2_buildcache,
    int overlay_id)
{
    struct MapEntry_ConfigOverlay* entry;

    assert(dat2_buildcache);

    entry = (struct MapEntry_ConfigOverlay*)hmap_search(
        dat2_buildcache->overlay_hmap, &overlay_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->overlay;
}

bool
dat2_buildcache_overlay_has(
    struct Dat2BuildCache* dat2_buildcache,
    int overlay_id)
{
    return dat2_buildcache_overlay_get(dat2_buildcache, overlay_id) != NULL;
}

void
dat2_buildcache_overlays_cleanup(struct Dat2BuildCache* dat2_buildcache)
{
    struct HMapIter* iter;
    struct MapEntry_ConfigOverlay* entry;

    assert(dat2_buildcache);
    if( !dat2_buildcache->overlay_hmap )
        return;

    iter = hmap_iter_new(dat2_buildcache->overlay_hmap);
    while( (entry = (struct MapEntry_ConfigOverlay*)hmap_iter_next(iter)) )
    {
        if( entry->overlay )
            RSCache_Dat2ConfigOverlayFree(entry->overlay);
    }
    hmap_iter_free(iter);

    dat2_buildcache_map_reset(
        dat2_buildcache,
        &dat2_buildcache->overlay_hmap,
        sizeof(struct MapEntry_ConfigOverlay),
        DAT2_CONFIG_MAP_CAPACITY);
}

void
dat2_buildcache_overlays_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCache_Dat2DiskArchive* archive,
    const int* wanted_ids,
    int wanted_count)
{
    int flo_flags =
        RSCache_Dat2ConfigFloFlags(CacheProvider_Profile(&dat2_buildcache->base));
    struct RSCache_FileList* filelist;

    assert(dat2_buildcache);
    assert(archive);
    assert(archive->file_ids);

    filelist = RSCache_FileListNewFromDecode(
        archive->data, archive->data_size, archive->file_count);
    if( !filelist )
        return;

    for( int i = 0; i < filelist->file_count; i++ )
    {
        int id = archive->file_ids[i];
        struct RSCache_Dat2ConfigOverlay* overlay;

        if( !dat2_id_wanted(id, wanted_ids, wanted_count) )
            continue;
        if( dat2_buildcache_overlay_get(dat2_buildcache, id) )
            continue;

        overlay = malloc(sizeof(struct RSCache_Dat2ConfigOverlay));
        RSCache_Dat2ConfigOverlayDecodeInplaceFlags(
            overlay, filelist->files[i], filelist->file_sizes[i], flo_flags);
        if( !overlay )
            continue;

        overlay->_id = id;
        dat2_buildcache_overlay_add(dat2_buildcache, id, overlay);
    }

    RSCache_FileListFree(filelist);
}

void
dat2_buildcache_texture_add(
    struct Dat2BuildCache* dat2_buildcache,
    int texture_id,
    struct RSCache_Dat2Texture* texture)
{
    struct MapEntry_Texture* entry;

    assert(dat2_buildcache);
    assert(texture);

    dat2_buildcache_prepare_hmap_insert(dat2_buildcache, &dat2_buildcache->texture_hmap);
    entry = (struct MapEntry_Texture*)hmap_search(
        dat2_buildcache->texture_hmap, &texture_id, HMAP_INSERT);
    assert(entry && "Texture must be inserted into hmap");

    if( entry->texture )
        RSCache_Dat2TextureFree(entry->texture);

    entry->id = texture_id;
    entry->texture = texture;
}

struct RSCache_Dat2Texture*
dat2_buildcache_texture_get(
    struct Dat2BuildCache* dat2_buildcache,
    int texture_id)
{
    struct MapEntry_Texture* entry;

    assert(dat2_buildcache);

    entry = (struct MapEntry_Texture*)hmap_search(
        dat2_buildcache->texture_hmap, &texture_id, HMAP_FIND);
    if( !entry )
        return NULL;
    return entry->texture;
}

bool
dat2_buildcache_texture_has(
    struct Dat2BuildCache* dat2_buildcache,
    int texture_id)
{
    return dat2_buildcache_texture_get(dat2_buildcache, texture_id) != NULL;
}

void
dat2_buildcache_textures_cleanup(struct Dat2BuildCache* dat2_buildcache)
{
    struct HMapIter* iter;
    struct MapEntry_Texture* entry;

    assert(dat2_buildcache);
    if( !dat2_buildcache->texture_hmap )
        return;

    iter = hmap_iter_new(dat2_buildcache->texture_hmap);
    while( (entry = (struct MapEntry_Texture*)hmap_iter_next(iter)) )
    {
        if( entry->texture )
            RSCache_Dat2TextureFree(entry->texture);
    }
    hmap_iter_free(iter);

    dat2_buildcache_map_reset(
        dat2_buildcache,
        &dat2_buildcache->texture_hmap,
        sizeof(struct MapEntry_Texture),
        DAT2_CONFIG_MAP_CAPACITY);
}

void
dat2_buildcache_textures_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCache_Dat2DiskArchive* archive,
    const int* wanted_ids,
    int wanted_count)
{
    struct RSCache_FileList* filelist;

    assert(dat2_buildcache);
    assert(archive);
    assert(archive->file_ids);

    filelist = RSCache_FileListNewFromDecode(
        archive->data, archive->data_size, archive->file_count);
    if( !filelist )
        return;

    for( int i = 0; i < filelist->file_count; i++ )
    {
        int id = archive->file_ids[i];
        struct RSCache_Dat2Texture* texture;

        if( !dat2_id_wanted(id, wanted_ids, wanted_count) )
            continue;
        if( dat2_buildcache_texture_get(dat2_buildcache, id) )
            continue;

        texture = RSCache_Dat2TextureNewDecodeProfile(
            CacheProvider_Profile(&dat2_buildcache->base),
            filelist->files[i],
            filelist->file_sizes[i]);
        if( !texture )
            continue;

        texture->_id = id;
        dat2_buildcache_texture_add(dat2_buildcache, id, texture);
    }

    RSCache_FileListFree(filelist);
}

void
dat2_buildcache_prune(struct Dat2BuildCache* dat2_buildcache)
{
    assert(dat2_buildcache);

    dat2_buildcache_models_cleanup(dat2_buildcache);
    dat2_buildcache_map_terrain_cleanup(dat2_buildcache);
    dat2_buildcache_map_scenery_cleanup(dat2_buildcache);
    dat2_buildcache_locs_cleanup(dat2_buildcache);
    dat2_buildcache_underlays_cleanup(dat2_buildcache);
    dat2_buildcache_overlays_cleanup(dat2_buildcache);
    dat2_buildcache_textures_cleanup(dat2_buildcache);
    dat2_buildcache_componentpacks_cleanup(dat2_buildcache);
    dat2_buildcache_objects_cleanup(dat2_buildcache);
    dat2_buildcache_npctypes_cleanup(dat2_buildcache);
    dat2_buildcache_bas_cleanup(dat2_buildcache);
    dat2_buildcache_identkits_cleanup(dat2_buildcache);
    dat2_buildcache_clientscripts_cleanup(dat2_buildcache);
    dat2_buildcache_reference_tables_cleanup(dat2_buildcache);
}
