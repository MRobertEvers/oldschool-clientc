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
 * sound, a song, the skybox, an obj model) is the provider's CreateTask_*
 * wrapped in Task_AssetLoad, which reports the end to the table. A COMPOSITE
 * (an npc body, a held item, a transmog, the inventory icon pass, a
 * spot-animation, a reference table) is the task that already followed its
 * chain, registered by the unit that owns it through App_AssetsSetLoader.
 */

#include "app/app_internal.h"
#include "mem/torirs_mem.h"

#include "engine/asset_requests.h"
#include "engine/task_obj_model_load.h"
#include "game/task_entity_assets.h"

struct Task_AssetLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    struct AssetKey key;
    /* Made by the start, handed to the await on the first run. */
    struct ToriRS_Task* child;
    /* ASSET_KIND_OBJ_MODEL: the one-element arrays CreateTask_ObjModelLoad
     * reads by pointer for as long as it runs. */
    int obj_id;
    int count;
    /* The end has been reported. A task freed without it was torn down. */
    int ended;
};

static struct ToriRS_Task*
asset_load_take_child(struct Task_AssetLoad* self)
{
    struct ToriRS_Task* child = self->child;
    self->child = NULL;
    return child;
}

static int
Task_AssetLoad_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_AssetLoad* self = (struct Task_AssetLoad*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);
    PT_TASK_AWAITSELF(asset_load_take_child(self));
    self->ended = 1;
    Assets_Finished(app->assets, self->key, App_AssetResident(app, self->key));
    PT_END(&self->pt);
}

static void
Task_AssetLoad_Free(struct ToriRS_Task* base)
{
    struct Task_AssetLoad* self = (struct Task_AssetLoad*)base;

    /* Freed mid-read (a queue torn down): the entry must not stay "loading"
     * for the rest of the session, which is what a flag left set did. */
    if( !self->ended )
        Assets_Abandoned(self->app->assets, self->key);
    if( self->pt.user )
        task_free((struct ToriRS_Task*)self->pt.user);
    if( self->child )
        task_free(self->child);
    ToriMem_Free(ToriMem_Process(), self);
}

static struct ToriRS_TaskVTable Task_AssetLoad_VTable = {
    .run = Task_AssetLoad_Run,
    .free = Task_AssetLoad_Free,
};

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
    /* A song is "here" only while the player holds it; the loader installs
     * it, so the end of the load is the whole answer. */
    if( key.kind == ASSET_KIND_MUSIC )
        return true;
    assert(0 && "a composite kind answers through its own row");
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

static enum AssetStart
asset_start_leaf(
    void* data,
    struct Assets* assets,
    struct AssetKey key)
{
    struct App* app = data;
    struct Task_AssetLoad* task;
    struct ToriRS_Task* child = NULL;

    (void)assets;
    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->obj_id = key.id;
    task->count = key.variant;
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
    else if( key.kind == ASSET_KIND_MUSIC )
        child = CreateTask_MusicLoad(app->provider, &app->audio.music, key.id, key.variant);
    else if( key.kind == ASSET_KIND_OBJ_MODEL )
        child = CreateTask_ObjModelLoad(app->provider, &task->obj_id, &task->count, 1);
    else
        assert(0 && "not a leaf kind");

    if( !child )
    {
        /* The provider makes no task for a record it already holds. */
        ToriMem_Free(ToriMem_Process(), task);
        return key.kind != ASSET_KIND_MUSIC && App_AssetResident(app, key) ? ASSET_START_HERE
                                                                           : ASSET_START_FAILED;
    }
    task->task.vtable = &Task_AssetLoad_VTable;
    snprintf(task->task.name, sizeof(task->task.name), "AssetLoad:%s",
        Assets_Policy((enum AssetKind)key.kind)->name);
    task->app = app;
    task->key = key;
    task->child = child;
    PT_INIT(&task->pt);
    ToriRS_TaskQueue_Add(app->frame_runner.queue, &task->task);
    return ASSET_START_LOADING;
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
        ASSET_KIND_OBJ_MODEL,
    };

    assert(app);
    assert(!app->assets);
    app->assets = Assets_New();
    for( size_t i = 0; i < sizeof(leaves) / sizeof(leaves[0]); i++ )
        App_AssetsSetLoader(app, leaves[i], asset_start_leaf, asset_resident_leaf);
    /* The composites: each unit registers the task that follows its chain. */
    EntityAssets_RegisterLoaders(app);
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
}
