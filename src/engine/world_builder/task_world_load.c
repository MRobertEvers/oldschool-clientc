#include "task_world_load.h"

#include "engine/cache_provider.h"
#include "engine/torirs_types.h"
#include "engine/world_builder/world_builder.h"
#include "net/rev/revpacket.h"
#include "world/world.h"

#include "asyncio.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

#define WORLD_LOAD_MAX_CHUNKS 64

/* Sorted dynamic int set (uniqueness for referenced asset ids; the id scans
 * are pure CPU between awaits, so plain heap storage on the task is fine). */
struct WorldLoadIdSet
{
    int* items;
    int count;
    int cap;
};

static void
idset_free(struct WorldLoadIdSet* s)
{
    free(s->items);
    s->items = NULL;
    s->count = 0;
    s->cap = 0;
}

static void
idset_add(
    struct WorldLoadIdSet* s,
    int v)
{
    int lo = 0;
    int hi = s->count;
    while( lo < hi )
    {
        int mid = (lo + hi) / 2;
        if( s->items[mid] < v )
            lo = mid + 1;
        else
            hi = mid;
    }
    if( lo < s->count && s->items[lo] == v )
        return;

    if( s->count == s->cap )
    {
        s->cap = s->cap ? s->cap * 2 : 64;
        s->items = realloc(s->items, (size_t)s->cap * sizeof(int));
        assert(s->items);
    }
    memmove(&s->items[lo + 1], &s->items[lo], (size_t)(s->count - lo) * sizeof(int));
    s->items[lo] = v;
    s->count++;
}

struct Task_WorldLoad
{
    struct ToriRS_Task task;
    struct pt pt;

    struct CacheProvider* provider;
    struct WorldBuilder* builder;
    /*
     * Where this task puts work it wants done ALONGSIDE it rather than inside
     * it -- every load below. It is the queue this task is itself on; a
     * sibling is a task, not a subtask, which is the whole point: the runner
     * can have several of them in flight at once, and their reads go out
     * together instead of nose to tail.
     */
    struct ToriRS_TaskQueue* queue;
    int chunks_xz[WORLD_LOAD_MAX_CHUNKS * 2];
    int chunk_count;
    /* >= 0: RebuildCenterzone(zone, scene_size). < 0: RebuildChunklist. */
    int zone_center_x;
    int zone_center_z;
    /* Scene side in tiles for the centerzone/instance rebuilds (104 = the
     * classic root scene; a boat view's own 8..104). The chunklist path
     * derives its own size and ignores this. */
    int scene_size;
    /* Non-zero `have_zones`: RebuildInstance from `zones` instead. Copied rather
     * than borrowed because the packet that carried them frees on task teardown,
     * and this task outlives the parse. */
    int have_zones;
    int32_t zones[PKT_MAP_REBUILD_ZONES];

    /* Invoked once at the synchronous tail (see CreateTask_WorldLoad). */
    void (*on_done)(void*);
    void* on_done_ud;

    struct WorldLoadIdSet underlays;
    struct WorldLoadIdSet overlays;
    struct WorldLoadIdSet textures;
    struct WorldLoadIdSet locs;
    struct WorldLoadIdSet models;
    struct WorldLoadIdSet seqs;

    /* Protothread loop cursors (locals do not survive yields). */
    int c;
    int i;
    int pass;
    int added;
    /*
     * Siblings queued and not yet ended. Every stage fans its loads out
     * against this and then joins on it (PT_TASK_JOIN), so a stage ends when
     * its loaders END -- a loader that exits on a record the cache cannot
     * serve counts the same as one that landed. The wait this replaced was on
     * RESIDENCY with a 600-pass budget, and a single unservable id spent the
     * whole budget, two passes a frame: six seconds per rebuild on a local
     * disk, with nothing on the wire.
     */
    int pending;

    /* TORIRS_REBUILD_TIMING=1: when the task first ran, when its assets were
     * all resident, and how many times the runner resumed it in between --
     * the three numbers that say whether a slow load was the wire, the decode
     * or the rebuild. */
    double t_start_ms;
    double t_assets_ms;
    int resumes;
};

/*
 * A residency test and a loader factory, both shaped (provider, id).
 *
 * Every stage of the load below is the same three steps over a different kind
 * of record -- ask what is missing, queue it, join it -- so the stages say
 * which kind and share the rest.
 */
typedef bool (*WorldLoadHasFn)(struct CacheProvider*, int);
typedef struct ToriRS_Task* (*WorldLoadMakeFn)(struct CacheProvider*, int);

/*
 * Queue a loader for every id in `ids` that is not already resident.
 *
 * Queued as SIBLINGS of this task rather than awaited inside it, which is the
 * whole point: they are independent reads, so the runner can have them all on
 * the wire at once instead of one per round trip. Nothing downstream of a
 * cache read cares which of them lands first. Each is counted on
 * `self->pending`, which is what the stage then joins on.
 */
static void
world_load_fanout(
    struct Task_WorldLoad* self,
    struct WorldLoadIdSet const* ids,
    WorldLoadHasFn has,
    WorldLoadMakeFn make)
{
    assert(self);
    assert(self->queue);
    assert(ids);
    for( int i = 0; i < ids->count; i++ )
    {
        if( has(self->provider, ids->items[i]) )
            continue;
        ToriRS_TaskQueue_AddJoined(self->queue, make(self->provider, ids->items[i]), &self->pending);
    }
}

/*
 * Pull every varbit/varp morph target into `locs`.
 *
 * world_builder_resolve_loc swaps a loc config for transforms[VarPManager_GetVarbit(...)] at
 * rebuild time, but the map only names the *base* ids. Against the lazy CacheProvider the
 * resolved id is then absent, resolve returns NULL, and the whole loc is dropped from the scene
 * (its models would be missing too). v1 avoided this by converting the entire cache up front;
 * here we preload the closure instead.
 *
 * The scan runs off a snapshot: idset_add inserts in sorted position, so appending while walking
 * the live array by index would shift entries under the cursor. Returns how many ids are new, so
 * the caller can iterate until the closure is complete (transform targets may themselves morph).
 */
static int
world_load_collect_loc_transforms(
    struct CacheProvider* provider,
    struct WorldLoadIdSet* locs)
{
    int* snapshot;
    int snapshot_count = locs->count;
    int before = locs->count;

    if( snapshot_count <= 0 )
        return 0;

    snapshot = malloc((size_t)snapshot_count * sizeof(int));
    assert(snapshot);
    memcpy(snapshot, locs->items, (size_t)snapshot_count * sizeof(int));

    for( int i = 0; i < snapshot_count; i++ )
    {
        struct ToriRS_Location* loc = CacheProvider_LocationGet(provider, snapshot[i]);
        if( !loc || loc->transform_count <= 0 || !loc->transforms )
            continue;

        /* -1 is a legal entry meaning "nothing here in this state" — not an id. */
        for( int ti = 0; ti < loc->transform_count; ti++ )
            if( loc->transforms[ti] >= 0 )
                idset_add(locs, loc->transforms[ti]);
    }

    free(snapshot);
    return locs->count - before;
}

static void
world_load_collect_loc_models(
    struct ToriRS_Location* loc,
    struct WorldLoadIdSet* models)
{
    assert(loc);

    if( !loc->shapes )
    {
        int count = loc->lengths ? loc->lengths[0] : 0;
        for( int j = 0; j < count && loc->models && loc->models[0]; j++ )
            idset_add(models, loc->models[0][j]);
        return;
    }

    for( int i = 0; i < loc->shapes_and_model_count; i++ )
    {
        int count = loc->lengths ? loc->lengths[i] : 0;
        for( int j = 0; j < count && loc->models && loc->models[i]; j++ )
            idset_add(models, loc->models[i][j]);
    }
}

static void
world_load_scan_chunk_refs(struct Task_WorldLoad* self)
{
    for( int c = 0; c < self->chunk_count; c++ )
    {
        int map_id = CacheProvider_MapId(self->chunks_xz[c * 2], self->chunks_xz[c * 2 + 1]);
        struct ToriRS_MapTerrain* terrain = CacheProvider_MapTerrainGet(self->provider, map_id);
        struct ToriRS_MapLocs* map_locs = CacheProvider_MapSceneryGet(self->provider, map_id);

        if( terrain )
        {
            int n = TORIRS_MAP_TERRAIN_X * TORIRS_MAP_TERRAIN_Z * TORIRS_MAP_TERRAIN_LEVELS;
            for( int i = 0; i < n; i++ )
            {
                struct ToriRS_MapFloor* t = &terrain->tiles_xyz[i];
                if( t->underlay_id > 0 )
                    idset_add(&self->underlays, t->underlay_id - 1);
                if( t->overlay_id > 0 )
                    idset_add(&self->overlays, t->overlay_id - 1);
            }
        }

        if( map_locs )
        {
            for( int i = 0; i < map_locs->locs_count; i++ )
                idset_add(&self->locs, map_locs->locs[i].loc_id);
        }
    }
}

static int
Task_WorldLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_WorldLoad* self = (struct Task_WorldLoad*)task_base;
    struct CacheProvider* p = self->provider;

    /* Every read this task wants is a sibling's, so its own slot goes unused. */
    (void)io;

    /* Before PT_BEGIN, so it counts every resume rather than the first. */
    self->resumes++;
    if( WorldBuilder_TimingOn() && self->t_start_ms == 0.0 )
        self->t_start_ms = WorldBuilder_TimingNowMs();

    PT_BEGIN(&self->pt);

    /*
     * 0. Reclaim before preloading, not after.
     *
     * Everything below adds to the derived caches, and by the time the build
     * runs they also hold the *previous* scene's models and sprites — which is
     * how a session that walks across a few map squares ends up holding every
     * model it has ever seen. This is the one point where dropping them is
     * safe: the last build has finished consuming its preload, and this one has
     * not started. Client-TS reclaims at the same point (Client.mapBuild calls
     * clearCaches before building, and clears LocType.mc1 again after).
     */
    CacheProvider_TrimDerivedCaches(p);

    /*
     * 1. Map terrain + scenery per chunk, ALL AT ONCE.
     *
     * A square's terrain and its scenery are the two biggest archives a
     * rebuild reads, and a rebuild reads a handful of squares. They are
     * independent of each other, so they go out together rather than four
     * round trips deep -- the same fan-out every stage below uses, spelled out
     * here because a map is addressed by (x, z) rather than by an id.
     *
     * Resident squares are skipped, the same way the underlay, flotype and
     * texture loads below skip what the provider already holds. Without that a
     * square is re-read on every rebuild, which is wasted IO for the game --
     * and wrong for anything that puts a square into the provider from
     * somewhere other than the cache. The map editor does exactly that: it
     * parses the `.jm2`/`.jl2` text in the content tree and seeds the provider
     * with it, so what the world builder meshes is the file being edited
     * rather than the last bake. An unconditional load would overwrite the
     * edit between one frame and the next.
     */
    for( self->c = 0; self->c < self->chunk_count; self->c++ )
    {
        int const map_x = self->chunks_xz[self->c * 2];
        int const map_z = self->chunks_xz[self->c * 2 + 1];
        int const map_id = CacheProvider_MapId(map_x, map_z);

        if( !CacheProvider_MapTerrainHas(p, map_id) )
            ToriRS_TaskQueue_AddJoined(
                self->queue, CreateTask_MapTerrainLoad(p, map_x, map_z), &self->pending);
        if( !CacheProvider_MapSceneryHas(p, map_id) )
            ToriRS_TaskQueue_AddJoined(
                self->queue, CreateTask_MapSceneryLoad(p, map_x, map_z), &self->pending);
    }
    PT_TASK_JOIN(pending);
    /* Its loader has ended, so a square still absent is one the cache does
     * not hold -- the loader said why on its own line. */
    for( self->c = 0; self->c < self->chunk_count; self->c++ )
    {
        if( !CacheProvider_MapTerrainHas(
                p,
                CacheProvider_MapId(
                    self->chunks_xz[self->c * 2], self->chunks_xz[self->c * 2 + 1])) )
            TORIRS_ERR("world_load: map %d,%d unavailable (missing archive)\n",
                self->chunks_xz[self->c * 2],
                self->chunks_xz[self->c * 2 + 1]);
    }

    /*
     * 2. Everything the squares name directly, ALL AT ONCE.
     *
     * Underlays, overlay flotypes and locs are three independent config
     * lookups off the same scan, so they are one fan-out and one join. They
     * used to be two stages -- floors, then locs -- which put a whole round
     * trip of the loc group behind the floor records for no dependency at all.
     */
    world_load_scan_chunk_refs(self);
    world_load_fanout(self, &self->underlays, CacheProvider_UnderlayHas, CreateTask_UnderlayLoad);
    world_load_fanout(self, &self->overlays, CacheProvider_FlotypeHas, CreateTask_FlotypeLoad);
    world_load_fanout(self, &self->locs, CacheProvider_LocationHas, CreateTask_LocLoad);
    PT_TASK_JOIN(pending);

    /*
     * 3. Overlay textures, alongside the loc morph closure.
     *
     * The textures depend only on the flotypes, which just landed, so they go
     * out now and ride under the closure's joins rather than getting a round
     * trip of their own.
     */
    for( self->i = 0; self->i < self->overlays.count; self->i++ )
    {
        struct ToriRS_Flotype* flo = CacheProvider_FlotypeGet(p, self->overlays.items[self->i]);
        if( flo && flo->texture >= 0 )
            idset_add(&self->textures, flo->texture);
    }
    world_load_fanout(self, &self->textures, CacheProvider_TextureHas, CreateTask_TextureLoad);

    /* 3b. Morph closure: a loc the map names may resolve to a transform target at rebuild time,
     * and that target can morph again. Collect (CPU only) then load, until nothing new appears.
     * The bound is a guard against a self-referential transform table in a bad cache. */
    for( self->pass = 0; self->pass < 4; self->pass++ )
    {
        self->added = world_load_collect_loc_transforms(p, &self->locs);
        if( self->added <= 0 )
            break;

        world_load_fanout(self, &self->locs, CacheProvider_LocationHas, CreateTask_LocLoad);
        PT_TASK_JOIN(pending);
    }
    PT_TASK_JOIN(pending);

    for( self->i = 0; self->i < self->locs.count; self->i++ )
    {
        struct ToriRS_Location* dbg = CacheProvider_LocationGet(p, self->locs.items[self->i]);
        if( getenv("TORIRS_LOC_MODEL_DEBUG") )
            TORIRS_ERR("collect loc %d: %s groups=%d shapes=%s models=%s\n",
                self->locs.items[self->i],
                dbg ? "present" : "MISSING",
                dbg ? dbg->shapes_and_model_count : -1,
                (dbg && dbg->shapes) ? "yes" : "no",
                (dbg && dbg->models) ? "yes" : "no");
        /* A loc the cache could not serve has no models to collect; its
         * loader already said so. */
        if( dbg )
            world_load_collect_loc_models(dbg, &self->models);
        if( dbg && dbg->seq_id >= 0 )
            idset_add(&self->seqs, dbg->seq_id);
    }

    /*
     * 4. Every loc model the region needs, and every animated loc's sequence,
     * ALL AT ONCE.
     *
     * This is the big one: a region names hundreds of models, and awaiting
     * them one at a time cost a network round trip each, in a line, on a cache
     * being streamed. The sequences are registered in the scene animation
     * registry so scenery_load_animation can bind them during the rebuild;
     * CreateTask_SequenceLoad answers NULL for one already registered. They
     * used to be awaited one after another AFTER the models, each several
     * reads deep -- a chain of round trips behind the biggest stage.
     */
    world_load_fanout(self, &self->models, CacheProvider_ModelHas, CreateTask_ModelLoad);
    for( self->i = 0; self->i < self->seqs.count; self->i++ )
        ToriRS_TaskQueue_AddJoined(
            self->queue,
            CreateTask_SequenceLoad(p, self->builder->scene, self->seqs.items[self->i]),
            &self->pending);
    PT_TASK_JOIN(pending);

    /* 4c. Textures referenced by the loaded loc models (face texture ids) and
     * by loc retextures — preload so scenery is textured at rebuild (v1
     * preloads every texture; app_sync_textures remains the safety net).
     * HD-only materials are skipped: the SD gate strips them from the faces
     * before lighting, so a bake would go unused — 880 of 643's 1164. This
     * filter is reliable here because step 3's texture loads already probed
     * the materials table. */
    for( self->i = 0; self->i < self->models.count; self->i++ )
    {
        struct ToriRS_Model* model = CacheProvider_ModelGet(p, self->models.items[self->i]);
        if( !model || !model->face_textures )
            continue;
        for( int f = 0; f < model->face_count; f++ )
            if( model->face_textures[f] != (gc_faceint_t)-1 &&
                CacheProvider_TextureIsSd(p, (int)model->face_textures[f]) )
                idset_add(&self->textures, (int)model->face_textures[f]);
    }
    for( self->i = 0; self->i < self->locs.count; self->i++ )
    {
        struct ToriRS_Location* loc = CacheProvider_LocationGet(p, self->locs.items[self->i]);
        if( !loc )
            continue;
        for( int r = 0; r < loc->retexture_count; r++ )
            if( loc->retextures_to[r] >= 0 &&
                CacheProvider_TextureIsSd(p, loc->retextures_to[r]) )
                idset_add(&self->textures, loc->retextures_to[r]);
    }
    world_load_fanout(self, &self->textures, CacheProvider_TextureHas, CreateTask_TextureLoad);
    PT_TASK_JOIN(pending);

    TORIRS_LOG("world_load: %d chunks, %d underlays, %d overlays, %d textures, %d locs, %d models, "
        "%d seqs\n",
        self->chunk_count,
        self->underlays.count,
        self->overlays.count,
        self->textures.count,
        self->locs.count,
        self->models.count,
        self->seqs.count);

    if( WorldBuilder_TimingOn() )
        self->t_assets_ms = WorldBuilder_TimingNowMs();

    /* 5. Synchronous rebuild: world + scene elements from the loaded assets.
     * REBUILD_NORMAL passes the zone centre so the scene base is (zone-6)*8
     * (Client-TS / deob method3310); offline loads keep the chunk-list path. */
    if( self->have_zones )
        WorldBuilder_RebuildInstance(
            self->builder, self->zone_center_x, self->zone_center_z, self->scene_size,
            self->zones);
    else if( self->zone_center_x >= 0 )
        WorldBuilder_RebuildCenterzone(
            self->builder, self->zone_center_x, self->zone_center_z, self->scene_size);
    else
        WorldBuilder_RebuildChunklist(self->builder, self->chunks_xz, self->chunk_count);
    World_SetLoadComplete(self->builder->world, true);

    if( WorldBuilder_TimingOn() )
    {
        double const t_end = WorldBuilder_TimingNowMs();
        TORIRS_REPORT("world_load_timing: total=%.1fms assets=%.1fms rebuild=%.1fms resumes=%d "
            "chunks=%d locs=%d models=%d textures=%d seqs=%d\n",
            t_end - self->t_start_ms,
            self->t_assets_ms - self->t_start_ms,
            t_end - self->t_assets_ms,
            self->resumes,
            self->chunk_count,
            self->locs.count,
            self->models.count,
            self->textures.count,
            self->seqs.count);
    }

    /* 6. "The load landed" hook — runs here, in the same synchronous span as the
     * rebuild, so callers get completion without polling a flag. NULL for callers
     * that await this task and run their own tail instead. */
    if( self->on_done )
        self->on_done(self->on_done_ud);

    PT_END(&self->pt);
}

static void
Task_WorldLoad_Free(struct ToriRS_Task* task_base)
{
    struct Task_WorldLoad* self = (struct Task_WorldLoad*)task_base;
    idset_free(&self->underlays);
    idset_free(&self->overlays);
    idset_free(&self->textures);
    idset_free(&self->locs);
    idset_free(&self->models);
    idset_free(&self->seqs);
    free(self);
}

static struct ToriRS_TaskVTable Task_WorldLoad_VTable = {
    .run = Task_WorldLoad_Run,
    .free = Task_WorldLoad_Free,
};

struct ToriRS_Task*
CreateTask_WorldLoad(
    struct CacheProvider* provider,
    struct WorldBuilder* builder,
    struct ToriRS_TaskQueue* queue,
    const int* chunks_xz,
    int chunk_count,
    int zone_center_x,
    int zone_center_z,
    int scene_size,
    const int32_t* zones,
    void (*on_done)(void*),
    void* on_done_ud)
{
    struct Task_WorldLoad* task;

    assert(provider);
    assert(builder);
    /* Every stage fans its loads out as siblings, so there is no such thing as
     * a world load with nowhere to put them. */
    assert(queue);
    assert(chunks_xz);
    /* Zero squares is legal, and only for an instance: a house with no rooms
     * built is an all-void scene with nothing to prefetch. Every other caller
     * names at least one square. */
    assert(chunk_count >= 0 && chunk_count <= WORLD_LOAD_MAX_CHUNKS);
    assert((chunk_count > 0 || zones) && "a non-instanced load with no squares");
    /* Whole zones only: World_ResetScene's base math and the instance grid
     * both count in zones of 8 tiles. */
    assert(scene_size > 0);
    assert(scene_size % 8 == 0);

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_WorldLoad_VTable;
    strcpy(task->task.name, "WorldLoad");
    task->provider = provider;
    task->builder = builder;
    task->queue = queue;
    task->zone_center_x = zone_center_x;
    task->zone_center_z = zone_center_z;
    task->scene_size = scene_size;
    if( zones )
    {
        task->have_zones = 1;
        memcpy(task->zones, zones, sizeof(task->zones));
    }
    memcpy(task->chunks_xz, chunks_xz, (size_t)chunk_count * 2 * sizeof(int));
    task->chunk_count = chunk_count;
    task->on_done = on_done;
    task->on_done_ud = on_done_ud;
    PT_INIT(&task->pt);
    return &task->task;
}
