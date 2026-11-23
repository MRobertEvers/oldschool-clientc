/*
 * The client's loaders for the assets table (engine/asset_requests.h).
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 *
 * The table says WHEN a load runs (at most one per asset, under its kind's
 * policy); this unit says HOW, one row per kind: a `start` that puts the
 * kind's ordinary cache task on the parallel queue (app->frame_runner), where
 * every one of these loads ran before the table existed, and a `resident` that
 * asks the cache whether the asset is here.
 *
 * A LEAF load (a model, a texture, a sprite, a sequence, a component pack, a
 * sound, a song, the skybox, an obj model, an idk or obj config) is the provider's CreateTask_*
 * wrapped in AssetLoadTask (engine/asset_load_task.h), which reports the end
 * to the table; a
 * spot-animation is the spawn task that follows its chain, wrapped the same. A COMPOSITE
 * (an npc body, a held item, a transmog, a
 * spot-animation, a reference table) is the task that already followed its
 * chain, registered by the unit that owns it through App_AssetsSetLoader.
 */

#include "app/app_internal.h"
#include "mem/torirs_mem.h"

#include "engine/asset_cache_loaders.h"
#include "engine/asset_load_task.h"
#include "engine/asset_requests.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/dat2/dat2_tasks.h"
#include "engine/task_obj_model_load.h"
#include "game/rs_worldmap_render.h"
#include "game/task_entity_assets.h"

bool
App_AssetResident(
    struct App* app,
    struct AssetKey key)
{
    assert(app);
    if( AssetCache_HandlesKind((enum AssetKind)key.kind) )
        return AssetCache_Resident(app->provider, key);
    if( key.kind == ASSET_KIND_SEQUENCE )
        return ToriDraw_SceneAnimationHas(app->scene, key.id);
    if( key.kind == ASSET_KIND_SPOTANIM )
        return app_entity_spotanim_resident(app, key.id) != 0;
    if( key.kind == ASSET_KIND_WORLDMAP_GEOGRAPHY )
        return CacheProvider_WorldMapGeographyHas(app->provider, key.id);
    /* A song is "here" only while the player holds it; the loader installs
     * it, so the end of the load is the whole answer. */
    if( key.kind == ASSET_KIND_MUSIC )
        return true;
    assert(0 && "a composite kind answers through its own row");
    return false;
}

static bool
asset_resident_never(
    void* data,
    struct AssetKey key)
{
    (void)data;
    (void)key;
    return false;
}

static bool
asset_resident_leaf(
    void* data,
    struct AssetKey key)
{
    /* No residency for a song: every want must reach its loader. */
    if( key.kind == ASSET_KIND_MUSIC )
        return false;
    return App_AssetResident((struct App*)data, key);
}

/* The end's answer: a song's load is its whole answer (the player installs
 * it), everything else is asked of the cache. */
static bool
asset_resident_at_end(
    void* data,
    struct AssetKey key)
{
    return App_AssetResident((struct App*)data, key);
}

/* A region's tiles: the compositemap records of its area that land on it,
 * found again from the key (which carries the area id and the region). NULL
 * when the world map is not loaded, the area is gone or nothing lands there
 * -- a failed attempt. */
static struct ToriRS_Task*
app_worldmap_geography_load(
    struct App* app,
    int key)
{
    struct ToriRS_WorldMapAreas* areas = CacheProvider_WorldMapGet(app->provider);
    struct ToriRS_WorldMapRegionSource sources[64];
    int const area_id = (int)(((uint32_t)key >> 24) & 0xFFu);
    int const region_x = (key >> 12) & 0xFFF;
    int const region_y = key & 0xFFF;

    for( int i = 0; areas && i < areas->count; i++ )
    {
        struct ToriRS_WorldMapArea const* area = &areas->areas[i];
        int count;

        if( (area->id & 0xFF) != area_id )
            continue;
        if( CacheProvider_WorldMapGeographyKey(area->id, region_x, region_y) != key )
            continue;
        count = RS_WorldMapRender_RegionSources(
            area, region_x, region_y, sources, (int)(sizeof(sources) / sizeof(sources[0])));
        if( count == 0 )
            return NULL;
        return CreateTask_WorldMapGeographyLoad(app->provider, key, sources, count);
    }
    return NULL;
}

static enum AssetStart
asset_start_leaf(
    void* data,
    struct Assets* assets,
    struct AssetKey key)
{
    struct App* app = data;
    struct AssetLoadTask* task = AssetLoadTask_New(assets, key, asset_resident_at_end, app);
    struct ToriRS_Task* child = NULL;

    if( key.kind == ASSET_KIND_SEQUENCE )
        child = CreateTask_SequenceLoad(app->provider, app->scene, key.id);
    else if( key.kind == ASSET_KIND_WORLDMAP_GEOGRAPHY )
        child = app_worldmap_geography_load(app, key.id);
    else if( key.kind == ASSET_KIND_MUSIC )
        child = CreateTask_MusicLoad(app->provider, &app->audio.music, key.id, key.variant);
    else if( key.kind == ASSET_KIND_SPOTANIM )
    {
        /* The type, then its model and sequence together (each of those
         * through the table in turn). */
        struct Task_AppSpawn* spawn = app_spawn_task_new(app, APP_SPAWN_ENTITY_SPOTANIM, 0, 0, 0);
        spawn->spotanim_id = key.id;
        spawn->entity_element_id = -1;
        child = &spawn->task;
    }
    else
        assert(0 && "not a leaf kind");
    /* No task from the provider: it already holds the record (or, for a
     * song, cannot load it). */
    if( !child && key.kind == ASSET_KIND_MUSIC )
        task->resident = asset_resident_never;
    return AssetLoadTask_Start(task, child, app->frame_runner.queue);
}

void
App_AssetsSetLoader(
    struct App* app,
    enum AssetKind kind,
    enum AssetStart (*start)(
        void* data,
        struct Assets* assets,
        struct AssetKey key),
    bool (*resident)(
        void* data,
        struct AssetKey key))
{
    struct AssetLoader loader;

    assert(app);
    assert(app->assets);
    assert(start);
    loader.start = start;
    loader.resident = resident;
    loader.data = app;
    Assets_SetLoader(app->assets, kind, &loader);
}

void
App_AssetsInit(struct App* app)
{
    /* What the App's own state loads: a sequence into the scene, a song into
     * the player, a spot-animation's chain, a world map region's tiles. Every
     * kind the cache loads on its own is the engine's (asset_cache_loaders). */
    static enum AssetKind const leaves[] = {
        ASSET_KIND_SEQUENCE, ASSET_KIND_MUSIC, ASSET_KIND_SPOTANIM, ASSET_KIND_WORLDMAP_GEOGRAPHY,
    };

    assert(app);
    assert(app->provider);
    assert(!app->assets);
    app->assets = Assets_New();
    AssetCache_RegisterLoaders(app->assets, app->provider);
    for( size_t i = 0; i < sizeof(leaves) / sizeof(leaves[0]); i++ )
        App_AssetsSetLoader(app, leaves[i], asset_start_leaf, asset_resident_leaf);
    /* The composites: each unit registers the task that follows its chain. */
    EntityAssets_RegisterLoaders(app);
    /* The dat2 cache's shared prerequisite, its reference tables -- when the
     * provider is the dat2 cache the mounts opened (not a test's stand-in). */
    if( app->cache_mounts.dat2_bc &&
        dat2_buildcache_as_provider(app->cache_mounts.dat2_bc) == app->provider )
        Dat2BuildCache_AttachAssets(app->cache_mounts.dat2_bc, app->assets);
}

void
App_AssetsFree(struct App* app)
{
    assert(app);
    Assets_Free(app->assets);
    app->assets = NULL;
}

void
app_assets_frame(struct App* app)
{
    assert(app);
    assert(app->assets);
    /* The clock a want's back-off is measured on. Nothing is landed or
     * restarted here: what follows an arrival is the body of the task that
     * awaited it, and a want waiting out a back-off starts when it is next
     * made after the wait. */
    Assets_SetClock(app->assets, (uint64_t)app->host.client.now_ms);
}
