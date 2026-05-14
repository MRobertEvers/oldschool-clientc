#include "osrs/gamecache/gamecache.h"

#include "osrs/gamecache/gamecache_store.h"
#include "osrs/rscache/rsbuf.h"
#include "osrs/rscache/tables/config_floortype.h"
#include "osrs/rscache/tables/config_locs.h"
#include "osrs/rscache/tables/config_sequence.h"
#include "osrs/rscache/tables/maps.h"
#include "osrs/rscache/tables/model.h"
#include "osrs/rscache/tables_dat/animframe.h"
#include "osrs/rscache/tables_dat/config_idk.h"
#include "osrs/rscache/tables_dat/config_npc.h"
#include "osrs/rscache/tables_dat/config_obj.h"
#include "osrs/rscache/tables_dat/config_spotanim.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define GAMECACHE_HMAP_INITIAL_CAPACITY 64

_Static_assert(
    sizeof(struct GameCacheFloor) == sizeof(struct CacheMapFloor),
    "GameCacheFloor");
_Static_assert(
    sizeof(struct GameCacheLoc) == sizeof(struct CacheMapLoc),
    "GameCacheLoc");
_Static_assert(
    sizeof(struct GameCacheTerrain) == sizeof(struct CacheMapTerrain),
    "GameCacheTerrain");
_Static_assert(
    sizeof(struct GameCacheLocList) == sizeof(struct CacheMapLocs),
    "GameCacheLocList");
_Static_assert(
    sizeof(struct GameCacheFloorType) == sizeof(struct CacheConfigOverlay),
    "GameCacheFloorType");
_Static_assert(
    sizeof(struct GameCacheModel) == sizeof(struct CacheModel),
    "GameCacheModel");
_Static_assert(
    sizeof(struct GameCacheSequence) == sizeof(struct CacheDatSequence),
    "GameCacheSequence");
_Static_assert(
    sizeof(struct GameCacheAnimBase) == sizeof(struct CacheAnimBase),
    "GameCacheAnimBase");
_Static_assert(
    sizeof(struct GameCacheAnimframe) == sizeof(struct CacheAnimframe),
    "GameCacheAnimframe");
_Static_assert(
    sizeof(struct GameCacheObj) == sizeof(struct CacheDatConfigObj),
    "GameCacheObj");
_Static_assert(
    sizeof(struct GameCacheIdk) == sizeof(struct CacheDatConfigIdk),
    "GameCacheIdk");
_Static_assert(
    sizeof(struct GameCacheNpc) == sizeof(struct CacheDatConfigNpc),
    "GameCacheNpc");
_Static_assert(
    sizeof(struct GameCacheSpotAnim) == sizeof(struct CacheDatConfigSpotAnim),
    "GameCacheSpotAnim");
_Static_assert(
    sizeof(struct GameCacheParams) == sizeof(struct Params),
    "GameCacheParams");
_Static_assert(
    sizeof(struct GameCacheLocConfig) == sizeof(struct CacheConfigLocation),
    "GameCacheLocConfig");

struct MapTerrainEntry
{
    int id;
    int mapx;
    int mapz;
    struct GameCacheTerrain* terrain;
};

struct FlotypeEntry
{
    int id;
    struct GameCacheFloorType* flotype;
};

struct ConfigLocEntry
{
    int id;
    struct GameCacheLocConfig* config_loc;
};

struct SceneryEntry
{
    int id;
    int mapx;
    int mapz;
    struct GameCacheLocList* locs;
};

struct ModelEntry
{
    int id;
    struct GameCacheModel* model;
};

struct SequenceEntry
{
    int id;
    struct GameCacheSequence* sequence;
};

struct AnimframeEntry
{
    int id;
    struct GameCacheAnimframe* animframe;
};

struct IdkEntry
{
    int id;
    struct GameCacheIdk* idk;
};

struct ObjEntry
{
    int id;
    struct GameCacheObj* obj;
};

struct NpcEntry
{
    int id;
    struct GameCacheNpc* npc;
};

struct IdkModelEntry
{
    int id;
    struct GameCacheModel* model;
};

struct ObjModelEntry
{
    int id;
    struct GameCacheModel* model;
};

struct NpcModelEntry
{
    int id;
    struct GameCacheModel* model;
};

struct GameCache
{
    struct DashMap* map_terrains_hmap;
    struct DashMap* flotype_hmap;
    struct DashMap* scenery_hmap;
    struct DashMap* models_hmap;
    struct DashMap* config_loc_hmap;
    struct DashMap* sequences_hmap;
    struct DashMap* animframes_hmap;
    struct DashMap* idk_models_hmap;
    struct DashMap* obj_models_hmap;
    struct DashMap* npc_models_hmap;
    struct DashMap* idk_hmap;
    struct DashMap* obj_hmap;
    struct DashMap* npc_hmap;
};

static struct DashMap*
gamecache_create_hmap(
    size_t key_size,
    size_t entry_size,
    size_t initial_capacity)
{
    size_t buffer_size = dashmap_buffer_size_for(entry_size, initial_capacity);
    struct DashMapConfig config = {
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = key_size,
        .entry_size = entry_size,
    };
    return dashmap_new(&config, 0);
}

static void
gamecache_maybe_grow_hmap(struct DashMap* map)
{
    uint32_t count = dashmap_count(map);
    uint32_t capacity = dashmap_capacity(map);
    if( count * 4 <= capacity * 3 )
        return;

    size_t new_capacity = (size_t)capacity * 2;
    size_t esize = dashmap_entry_size(map);
    size_t new_buffer_size = dashmap_buffer_size_for(esize, new_capacity);
    void* new_buffer = malloc(new_buffer_size);
    void* old_buffer = NULL;
    int rc = dashmap_resize(map, new_buffer, new_buffer_size, new_capacity, &old_buffer);
    assert(rc == DASHMAP_OK);
    (void)rc;
    free(old_buffer);
}

static void
gamecache_hmap_destroy(
    struct DashMap** pmap,
    void (*entry_free)(void*))
{
    struct DashMap* map = *pmap;
    if( !map )
        return;
    if( entry_free )
    {
        struct DashMapIter* iter = dashmap_iter_new(map);
        void* entry;
        while( (entry = dashmap_iter_next(iter)) )
            entry_free(entry);
        dashmap_iter_free(iter);
    }
    free(dashmap_buffer_ptr(map));
    dashmap_free(map);
    *pmap = NULL;
}

static void
gamecache_hmaps_init(struct GameCache* gc)
{
    gc->map_terrains_hmap = gamecache_create_hmap(
        sizeof(int), sizeof(struct MapTerrainEntry), GAMECACHE_HMAP_INITIAL_CAPACITY);
    gc->flotype_hmap = gamecache_create_hmap(
        sizeof(int), sizeof(struct FlotypeEntry), GAMECACHE_HMAP_INITIAL_CAPACITY);
    gc->scenery_hmap = gamecache_create_hmap(
        sizeof(int), sizeof(struct SceneryEntry), GAMECACHE_HMAP_INITIAL_CAPACITY);
    gc->models_hmap = gamecache_create_hmap(
        sizeof(int), sizeof(struct ModelEntry), GAMECACHE_HMAP_INITIAL_CAPACITY);
    gc->config_loc_hmap = gamecache_create_hmap(
        sizeof(int), sizeof(struct ConfigLocEntry), GAMECACHE_HMAP_INITIAL_CAPACITY);
    gc->sequences_hmap = gamecache_create_hmap(
        sizeof(int), sizeof(struct SequenceEntry), GAMECACHE_HMAP_INITIAL_CAPACITY);
    gc->animframes_hmap = gamecache_create_hmap(
        sizeof(int), sizeof(struct AnimframeEntry), GAMECACHE_HMAP_INITIAL_CAPACITY);
    gc->idk_models_hmap = gamecache_create_hmap(
        sizeof(int), sizeof(struct IdkModelEntry), GAMECACHE_HMAP_INITIAL_CAPACITY);
    gc->obj_models_hmap = gamecache_create_hmap(
        sizeof(int), sizeof(struct ObjModelEntry), GAMECACHE_HMAP_INITIAL_CAPACITY);
    gc->npc_models_hmap = gamecache_create_hmap(
        sizeof(int), sizeof(struct NpcModelEntry), GAMECACHE_HMAP_INITIAL_CAPACITY);
    gc->idk_hmap = gamecache_create_hmap(
        sizeof(int), sizeof(struct IdkEntry), GAMECACHE_HMAP_INITIAL_CAPACITY);
    gc->obj_hmap = gamecache_create_hmap(
        sizeof(int), sizeof(struct ObjEntry), GAMECACHE_HMAP_INITIAL_CAPACITY);
    gc->npc_hmap = gamecache_create_hmap(
        sizeof(int), sizeof(struct NpcEntry), GAMECACHE_HMAP_INITIAL_CAPACITY);
}

static void
free_terrain_entry(void* e)
{
    struct GameCacheTerrain* t = ((struct MapTerrainEntry*)e)->terrain;
    if( t )
        map_terrain_free((struct CacheMapTerrain*)(void*)t);
}

static void
free_flotype_entry(void* e)
{
    struct GameCacheFloorType* ft = ((struct FlotypeEntry*)e)->flotype;
    if( ft )
        config_floortype_overlay_free((struct CacheConfigOverlay*)(void*)ft);
}

static void
free_scenery_entry(void* e)
{
    struct GameCacheLocList* locs = ((struct SceneryEntry*)e)->locs;
    if( locs )
    {
        free(locs->locs);
        free(locs);
    }
}

static void
free_model_entry(void* e)
{
    struct GameCacheModel* m = ((struct ModelEntry*)e)->model;
    if( m )
        model_free((struct CacheModel*)(void*)m);
}

static void
free_config_loc_entry(void* e)
{
    struct GameCacheLocConfig* loc = ((struct ConfigLocEntry*)e)->config_loc;
    if( loc )
        config_locs_free((struct CacheConfigLocation*)(void*)loc);
}

static void
free_sequence_entry(void* e)
{
    struct GameCacheSequence* s = ((struct SequenceEntry*)e)->sequence;
    if( s )
        config_dat_sequence_free((struct CacheDatSequence*)(void*)s);
}

static void
free_animframe_entry(void* e)
{
    struct GameCacheAnimframe* f = ((struct AnimframeEntry*)e)->animframe;
    if( f )
        cache_dat_animframe_free((struct CacheAnimframe*)(void*)f);
}

static void
free_idk_entry(void* e)
{
    struct GameCacheIdk* idk = ((struct IdkEntry*)e)->idk;
    if( idk )
        cache_dat_config_idk_free((struct CacheDatConfigIdk*)(void*)idk);
}

static void
free_obj_entry(void* e)
{
    struct GameCacheObj* obj = ((struct ObjEntry*)e)->obj;
    if( obj )
        cache_dat_config_obj_free((struct CacheDatConfigObj*)(void*)obj);
}

static void
free_npc_entry(void* e)
{
    struct GameCacheNpc* npc = ((struct NpcEntry*)e)->npc;
    if( npc )
        cache_dat_config_npc_free((struct CacheDatConfigNpc*)(void*)npc);
}

static void
free_idk_model_entry(void* e)
{
    struct GameCacheModel* m = ((struct IdkModelEntry*)e)->model;
    if( m )
        model_free((struct CacheModel*)(void*)m);
}

static void
free_obj_model_entry(void* e)
{
    struct GameCacheModel* m = ((struct ObjModelEntry*)e)->model;
    if( m )
        model_free((struct CacheModel*)(void*)m);
}

static void
free_npc_model_entry(void* e)
{
    struct GameCacheModel* m = ((struct NpcModelEntry*)e)->model;
    if( m )
        model_free((struct CacheModel*)(void*)m);
}

struct GameCache*
gamecache_new(void)
{
    struct GameCache* gc = malloc(sizeof(struct GameCache));
    if( !gc )
        return NULL;
    memset(gc, 0, sizeof(struct GameCache));

    gamecache_hmaps_init(gc);

    return gc;
}

void
gamecache_free(struct GameCache* gc)
{
    if( !gc )
        return;

    gamecache_hmap_destroy(&gc->map_terrains_hmap, free_terrain_entry);
    gamecache_hmap_destroy(&gc->flotype_hmap, free_flotype_entry);
    gamecache_hmap_destroy(&gc->scenery_hmap, free_scenery_entry);
    gamecache_hmap_destroy(&gc->models_hmap, free_model_entry);
    gamecache_hmap_destroy(&gc->config_loc_hmap, free_config_loc_entry);
    gamecache_hmap_destroy(&gc->sequences_hmap, free_sequence_entry);
    gamecache_hmap_destroy(&gc->animframes_hmap, free_animframe_entry);
    gamecache_hmap_destroy(&gc->idk_models_hmap, free_idk_model_entry);
    gamecache_hmap_destroy(&gc->obj_models_hmap, free_obj_model_entry);
    gamecache_hmap_destroy(&gc->npc_models_hmap, free_npc_model_entry);
    gamecache_hmap_destroy(&gc->idk_hmap, free_idk_entry);
    gamecache_hmap_destroy(&gc->obj_hmap, free_obj_entry);
    gamecache_hmap_destroy(&gc->npc_hmap, free_npc_entry);

    free(gc);
}

void
gamecache_clear(struct GameCache* gc)
{
    if( !gc )
        return;

    gamecache_hmap_destroy(&gc->map_terrains_hmap, free_terrain_entry);
    gamecache_hmap_destroy(&gc->flotype_hmap, free_flotype_entry);
    gamecache_hmap_destroy(&gc->scenery_hmap, free_scenery_entry);
    gamecache_hmap_destroy(&gc->models_hmap, free_model_entry);
    gamecache_hmap_destroy(&gc->config_loc_hmap, free_config_loc_entry);
    gamecache_hmap_destroy(&gc->sequences_hmap, free_sequence_entry);
    gamecache_hmap_destroy(&gc->animframes_hmap, free_animframe_entry);
    gamecache_hmap_destroy(&gc->idk_models_hmap, free_idk_model_entry);
    gamecache_hmap_destroy(&gc->obj_models_hmap, free_obj_model_entry);
    gamecache_hmap_destroy(&gc->npc_models_hmap, free_npc_model_entry);
    gamecache_hmap_destroy(&gc->idk_hmap, free_idk_entry);
    gamecache_hmap_destroy(&gc->obj_hmap, free_obj_entry);
    gamecache_hmap_destroy(&gc->npc_hmap, free_npc_entry);

    gamecache_hmaps_init(gc);
}

struct GameCacheTerrain*
gamecache_get_map_terrain(
    struct GameCache* gc,
    int mapx,
    int mapz)
{
    if( !gc || !gc->map_terrains_hmap )
        return NULL;
    int mapxz = MAPREGIONXZ(mapx, mapz);
    struct MapTerrainEntry* e =
        (struct MapTerrainEntry*)dashmap_search(gc->map_terrains_hmap, &mapxz, DASHMAP_FIND);
    return e ? e->terrain : NULL;
}

struct GameCacheFloorType*
gamecache_get_flotype(
    struct GameCache* gc,
    int flotype_id)
{
    if( !gc || !gc->flotype_hmap )
        return NULL;
    struct FlotypeEntry* e =
        (struct FlotypeEntry*)dashmap_search(gc->flotype_hmap, &flotype_id, DASHMAP_FIND);
    return e ? e->flotype : NULL;
}

struct GameCacheLocList*
gamecache_get_scenery(
    struct GameCache* gc,
    int mapx,
    int mapz)
{
    if( !gc || !gc->scenery_hmap )
        return NULL;
    int mapxz = MAPREGIONXZ(mapx, mapz);
    struct SceneryEntry* e =
        (struct SceneryEntry*)dashmap_search(gc->scenery_hmap, &mapxz, DASHMAP_FIND);
    return e ? e->locs : NULL;
}

struct GameCacheModel*
gamecache_get_model(
    struct GameCache* gc,
    int model_id)
{
    if( !gc || !gc->models_hmap )
        return NULL;
    struct ModelEntry* e =
        (struct ModelEntry*)dashmap_search(gc->models_hmap, &model_id, DASHMAP_FIND);
    return e ? e->model : NULL;
}

struct GameCacheLocConfig*
gamecache_get_config_loc(
    struct GameCache* gc,
    int config_loc_id)
{
    if( !gc || !gc->config_loc_hmap )
        return NULL;
    struct ConfigLocEntry* e =
        (struct ConfigLocEntry*)dashmap_search(gc->config_loc_hmap, &config_loc_id, DASHMAP_FIND);
    return e ? e->config_loc : NULL;
}

struct GameCacheSequence*
gamecache_get_sequence(
    struct GameCache* gc,
    int sequence_id)
{
    if( !gc || !gc->sequences_hmap )
        return NULL;
    struct SequenceEntry* e =
        (struct SequenceEntry*)dashmap_search(gc->sequences_hmap, &sequence_id, DASHMAP_FIND);
    return e ? e->sequence : NULL;
}

struct GameCacheAnimframe*
gamecache_get_animframe(
    struct GameCache* gc,
    int animframe_id)
{
    if( !gc || !gc->animframes_hmap )
        return NULL;
    struct AnimframeEntry* e =
        (struct AnimframeEntry*)dashmap_search(gc->animframes_hmap, &animframe_id, DASHMAP_FIND);
    return e ? e->animframe : NULL;
}

struct GameCacheObj*
gamecache_get_obj(
    struct GameCache* gc,
    int obj_id)
{
    if( !gc || !gc->obj_hmap )
        return NULL;
    struct ObjEntry* e = (struct ObjEntry*)dashmap_search(gc->obj_hmap, &obj_id, DASHMAP_FIND);
    return e ? e->obj : NULL;
}

struct GameCacheIdk*
gamecache_get_idk(
    struct GameCache* gc,
    int idk_id)
{
    if( !gc || !gc->idk_hmap )
        return NULL;
    struct IdkEntry* e = (struct IdkEntry*)dashmap_search(gc->idk_hmap, &idk_id, DASHMAP_FIND);
    return e ? e->idk : NULL;
}

struct GameCacheNpc*
gamecache_get_npc(
    struct GameCache* gc,
    int npc_id)
{
    if( !gc || !gc->npc_hmap )
        return NULL;
    struct NpcEntry* e = (struct NpcEntry*)dashmap_search(gc->npc_hmap, &npc_id, DASHMAP_FIND);
    return e ? e->npc : NULL;
}

struct GameCacheModel*
gamecache_get_obj_model(
    struct GameCache* gc,
    int obj_id)
{
    if( !gc || !gc->obj_models_hmap )
        return NULL;
    struct ObjModelEntry* e =
        (struct ObjModelEntry*)dashmap_search(gc->obj_models_hmap, &obj_id, DASHMAP_FIND);
    return e ? e->model : NULL;
}

struct GameCacheModel*
gamecache_get_idk_model(
    struct GameCache* gc,
    int idk_id)
{
    if( !gc || !gc->idk_models_hmap )
        return NULL;
    struct IdkModelEntry* e =
        (struct IdkModelEntry*)dashmap_search(gc->idk_models_hmap, &idk_id, DASHMAP_FIND);
    return e ? e->model : NULL;
}

struct GameCacheModel*
gamecache_get_npc_model(
    struct GameCache* gc,
    int npc_id)
{
    if( !gc || !gc->npc_models_hmap )
        return NULL;
    struct NpcModelEntry* e =
        (struct NpcModelEntry*)dashmap_search(gc->npc_models_hmap, &npc_id, DASHMAP_FIND);
    return e ? e->model : NULL;
}

void
gamecache_add_obj_model(
    struct GameCache* gc,
    int obj_id,
    struct GameCacheModel* model)
{
    struct ObjModelEntry* e =
        (struct ObjModelEntry*)dashmap_search(gc->obj_models_hmap, &obj_id, DASHMAP_INSERT);
    assert(e);
    e->id = obj_id;
    e->model = model;
    gamecache_maybe_grow_hmap(gc->obj_models_hmap);
}

void
gamecache_add_idk_model(
    struct GameCache* gc,
    int idk_id,
    struct GameCacheModel* model)
{
    struct IdkModelEntry* e =
        (struct IdkModelEntry*)dashmap_search(gc->idk_models_hmap, &idk_id, DASHMAP_INSERT);
    assert(e);
    e->id = idk_id;
    e->model = model;
    gamecache_maybe_grow_hmap(gc->idk_models_hmap);
}

void
gamecache_add_npc_model(
    struct GameCache* gc,
    int npc_id,
    struct GameCacheModel* model)
{
    struct NpcModelEntry* e =
        (struct NpcModelEntry*)dashmap_search(gc->npc_models_hmap, &npc_id, DASHMAP_INSERT);
    assert(e);
    e->id = npc_id;
    e->model = model;
    gamecache_maybe_grow_hmap(gc->npc_models_hmap);
}

void
gamecache_store_put_terrain(
    struct GameCache* gc,
    int mapx,
    int mapz,
    struct GameCacheTerrain* t)
{
    int mapxz = MAPREGIONXZ(mapx, mapz);
    struct MapTerrainEntry* e =
        (struct MapTerrainEntry*)dashmap_search(gc->map_terrains_hmap, &mapxz, DASHMAP_INSERT);
    assert(e);
    if( e->terrain )
        free(e->terrain);
    e->id = mapxz;
    e->mapx = mapx;
    e->mapz = mapz;
    e->terrain = t;
    gamecache_maybe_grow_hmap(gc->map_terrains_hmap);
}

void
gamecache_store_put_scenery(
    struct GameCache* gc,
    int mapx,
    int mapz,
    struct GameCacheLocList* locs)
{
    int mapxz = MAPREGIONXZ(mapx, mapz);
    struct SceneryEntry* e =
        (struct SceneryEntry*)dashmap_search(gc->scenery_hmap, &mapxz, DASHMAP_INSERT);
    assert(e);
    if( e->locs )
    {
        free(e->locs->locs);
        free(e->locs);
    }
    e->id = mapxz;
    e->mapx = mapx;
    e->mapz = mapz;
    e->locs = locs;
    gamecache_maybe_grow_hmap(gc->scenery_hmap);
}

void
gamecache_store_put_flotype(
    struct GameCache* gc,
    int flotype_id,
    struct GameCacheFloorType* ft)
{
    struct FlotypeEntry* e =
        (struct FlotypeEntry*)dashmap_search(gc->flotype_hmap, &flotype_id, DASHMAP_INSERT);
    assert(e);
    if( e->flotype )
        config_floortype_overlay_free((struct CacheConfigOverlay*)(void*)e->flotype);
    e->id = flotype_id;
    e->flotype = ft;
    gamecache_maybe_grow_hmap(gc->flotype_hmap);
}

void
gamecache_store_put_config_loc(
    struct GameCache* gc,
    int id,
    struct GameCacheLocConfig* loc)
{
    struct ConfigLocEntry* e =
        (struct ConfigLocEntry*)dashmap_search(gc->config_loc_hmap, &id, DASHMAP_INSERT);
    assert(e);
    if( e->config_loc )
        config_locs_free((struct CacheConfigLocation*)(void*)e->config_loc);
    e->id = id;
    e->config_loc = loc;
    gamecache_maybe_grow_hmap(gc->config_loc_hmap);
}

void
gamecache_store_put_model(
    struct GameCache* gc,
    int model_id,
    struct GameCacheModel* model)
{
    struct ModelEntry* e =
        (struct ModelEntry*)dashmap_search(gc->models_hmap, &model_id, DASHMAP_INSERT);
    assert(e);
    if( e->model )
        model_free((struct CacheModel*)(void*)e->model);
    e->id = model_id;
    e->model = model;
    gamecache_maybe_grow_hmap(gc->models_hmap);
}

void
gamecache_store_put_sequence(
    struct GameCache* gc,
    int sequence_id,
    struct GameCacheSequence* seq)
{
    struct SequenceEntry* e =
        (struct SequenceEntry*)dashmap_search(gc->sequences_hmap, &sequence_id, DASHMAP_INSERT);
    assert(e);
    if( e->sequence )
        config_dat_sequence_free((struct CacheDatSequence*)(void*)e->sequence);
    e->id = sequence_id;
    e->sequence = seq;
    gamecache_maybe_grow_hmap(gc->sequences_hmap);
}

void
gamecache_store_put_animframe(
    struct GameCache* gc,
    int animframe_id,
    struct GameCacheAnimframe* af)
{
    struct AnimframeEntry* e =
        (struct AnimframeEntry*)dashmap_search(gc->animframes_hmap, &animframe_id, DASHMAP_INSERT);
    assert(e);
    if( e->animframe )
        cache_dat_animframe_free((struct CacheAnimframe*)(void*)e->animframe);
    e->id = animframe_id;
    e->animframe = af;
    gamecache_maybe_grow_hmap(gc->animframes_hmap);
}

void
gamecache_store_put_obj(
    struct GameCache* gc,
    int obj_id,
    struct GameCacheObj* obj)
{
    struct ObjEntry* e = (struct ObjEntry*)dashmap_search(gc->obj_hmap, &obj_id, DASHMAP_INSERT);
    assert(e);
    if( e->obj )
        cache_dat_config_obj_free((struct CacheDatConfigObj*)(void*)e->obj);
    e->id = obj_id;
    e->obj = obj;
    gamecache_maybe_grow_hmap(gc->obj_hmap);
}

void
gamecache_store_put_idk(
    struct GameCache* gc,
    int idk_id,
    struct GameCacheIdk* idk)
{
    struct IdkEntry* e = (struct IdkEntry*)dashmap_search(gc->idk_hmap, &idk_id, DASHMAP_INSERT);
    assert(e);
    if( e->idk )
        cache_dat_config_idk_free((struct CacheDatConfigIdk*)(void*)e->idk);
    e->id = idk_id;
    e->idk = idk;
    gamecache_maybe_grow_hmap(gc->idk_hmap);
}

void
gamecache_store_put_npc(
    struct GameCache* gc,
    int npc_id,
    struct GameCacheNpc* npc)
{
    struct NpcEntry* e = (struct NpcEntry*)dashmap_search(gc->npc_hmap, &npc_id, DASHMAP_INSERT);
    assert(e);
    if( e->npc )
        cache_dat_config_npc_free((struct CacheDatConfigNpc*)(void*)e->npc);
    e->id = npc_id;
    e->npc = npc;
    gamecache_maybe_grow_hmap(gc->npc_hmap);
}
