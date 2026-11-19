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

#include "engine/asset_load_task.h"
#include "engine/asset_requests.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/dat2/dat2_tasks.h"
#include "engine/task_obj_model_load.h"
#include "game/task_entity_assets.h"

bool
App_AssetResident(
    struct App* app,
    struct AssetKey key)
{
    assert(app);
    if( key.kind == ASSET_KIND_MODEL || key.kind == ASSET_KIND_SKYBOX )
        return CacheProvider_ModelHas(app->provider, key.id);
    if( key.kind == ASSET_KIND_TEXTURE )
        return CacheProvider_TextureHas(app->provider, key.id);
    if( key.kind == ASSET_KIND_SPRITE )
        return CacheProvider_SpriteHas(app->provider, key.id);
    if( key.kind == ASSET_KIND_SEQUENCE )
        return ToriDraw_SceneAnimationHas(app->scene, key.id);
    if( key.kind == ASSET_KIND_IF_GROUP )
        return CacheProvider_ComponentPackHas(app->provider, key.id);
    if( key.kind == ASSET_KIND_SOUND )
        return CacheProvider_SoundHas(app->provider, key.id);
    if( key.kind == ASSET_KIND_OBJ_MODEL )
        return !ObjModelLoad_NeedsWork(app->provider, key.id, key.variant);
    if( key.kind == ASSET_KIND_SPOTANIM )
        return app_entity_spotanim_resident(app, key.id) != 0;
    if( key.kind == ASSET_KIND_CONFIG && key.variant == ASSET_CONFIG_IDK )
        return CacheProvider_IdkHas(app->provider, key.id);
    if( key.kind == ASSET_KIND_CONFIG )
    {
        assert(key.variant == ASSET_CONFIG_OBJ);
        return CacheProvider_ObjtypeHas(app->provider, key.id);
    }
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

static enum AssetStart
asset_start_leaf(
    void* data,
    struct Assets* assets,
    struct AssetKey key)
{
    struct App* app = data;
    struct AssetLoadTask* task = AssetLoadTask_New(assets, key, asset_resident_at_end, app);
    struct ToriRS_Task* child = NULL;

    if( key.kind == ASSET_KIND_MODEL || key.kind == ASSET_KIND_SKYBOX )
        child = CreateTask_ModelLoad(app->provider, key.id);
    else if( key.kind == ASSET_KIND_TEXTURE )
        child = CreateTask_TextureLoad(app->provider, key.id);
    else if( key.kind == ASSET_KIND_SPRITE )
        child = CreateTask_SpriteLoad(app->provider, key.id);
    else if( key.kind == ASSET_KIND_SEQUENCE )
        child = CreateTask_SequenceLoad(app->provider, app->scene, key.id);
    else if( key.kind == ASSET_KIND_IF_GROUP )
        child = CreateTask_ComponentPackLoad(app->provider, key.id);
    else if( key.kind == ASSET_KIND_SOUND )
        child = CreateTask_SoundLoad(app->provider, key.id);
    else if( key.kind == ASSET_KIND_CONFIG && key.variant == ASSET_CONFIG_IDK )
        child = CreateTask_IdkLoad(app->provider, key.id);
    else if( key.kind == ASSET_KIND_CONFIG )
        child = CreateTask_ObjLoad(app->provider, key.id);
    else if( key.kind == ASSET_KIND_MUSIC )
        child = CreateTask_MusicLoad(app->provider, &app->audio.music, key.id, key.variant);
    else if( key.kind == ASSET_KIND_OBJ_MODEL )
    {
        child = CreateTask_ObjModelLoad(app->provider, &task->args[0], &task->args[1], 1);
        /* A ground stack's model lands at the load's end, where its
         * placeholder task landed it before the table. */
        task->land_mask = ASSET_KIND_BIT(ASSET_KIND_OBJ_MODEL);
    }
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
    static enum AssetKind const leaves[] = {
        ASSET_KIND_MODEL,    ASSET_KIND_TEXTURE, ASSET_KIND_SPRITE, ASSET_KIND_SEQUENCE,
        ASSET_KIND_IF_GROUP, ASSET_KIND_SOUND,   ASSET_KIND_MUSIC,  ASSET_KIND_SKYBOX,
        ASSET_KIND_OBJ_MODEL, ASSET_KIND_SPOTANIM, ASSET_KIND_CONFIG,
    };

    assert(app);
    assert(!app->assets);
    app->assets = Assets_New();
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
    Assets_SetClock(app->assets, (uint64_t)app->host.client.now_ms);
    Assets_Pump(app->assets);
    /* Kinds that land at their load's end: anything that arrived without a
     * load ending (a loader with nothing to load) lands here. */
    Assets_Land(app->assets, ASSET_KIND_BIT(ASSET_KIND_OBJ_MODEL) | ASSET_KIND_BIT(ASSET_KIND_NPC_BODY));
}
