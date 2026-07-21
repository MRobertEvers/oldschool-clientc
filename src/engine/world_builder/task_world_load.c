#include "task_world_load.h"

#include "engine/cache_provider.h"
#include "engine/dat2/task_dat2_sequence_load.h"
#include "engine/torirs_types.h"
#include "engine/world_builder/world_builder.h"
#include "world/world.h"

#include "asyncio.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    int chunks_xz[WORLD_LOAD_MAX_CHUNKS * 2];
    int chunk_count;

    struct WorldLoadIdSet underlays;
    struct WorldLoadIdSet overlays;
    struct WorldLoadIdSet textures;
    struct WorldLoadIdSet locs;
    struct WorldLoadIdSet models;
    struct WorldLoadIdSet seqs;

    /* Protothread loop cursors (locals do not survive yields). */
    int c;
    int i;
};

static void
world_load_collect_loc_models(
    struct ToriRS_Location* loc,
    struct WorldLoadIdSet* models)
{
    if( !loc )
        return;

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

    PT_BEGIN(&self->pt);

    /* 1. Map terrain + scenery per chunk. */
    for( self->c = 0; self->c < self->chunk_count; self->c++ )
    {
        TASK_AWAITSELF_IF(CreateTask_MapTerrainLoad(
            p, self->chunks_xz[self->c * 2], self->chunks_xz[self->c * 2 + 1]));
        TASK_AWAITSELF_IF(CreateTask_MapSceneryLoad(
            p, self->chunks_xz[self->c * 2], self->chunks_xz[self->c * 2 + 1]));
        if( !CacheProvider_MapTerrainHas(
                p,
                CacheProvider_MapId(
                    self->chunks_xz[self->c * 2], self->chunks_xz[self->c * 2 + 1])) )
            fprintf(
                stderr,
                "world_load: map %d,%d unavailable (xtea/missing)\n",
                self->chunks_xz[self->c * 2],
                self->chunks_xz[self->c * 2 + 1]);
    }

    /* 2. Scan loaded chunks for referenced config ids (CPU only). */
    world_load_scan_chunk_refs(self);

    for( self->i = 0; self->i < self->underlays.count; self->i++ )
        TASK_AWAITSELF_IF(
            CacheProvider_UnderlayHas(p, self->underlays.items[self->i])
                ? NULL
                : CreateTask_UnderlayLoad(p, self->underlays.items[self->i]));

    for( self->i = 0; self->i < self->overlays.count; self->i++ )
        TASK_AWAITSELF_IF(
            CacheProvider_FlotypeHas(p, self->overlays.items[self->i])
                ? NULL
                : CreateTask_FlotypeLoad(p, self->overlays.items[self->i]));

    /* 3. Textures referenced by loaded overlay flotypes. */
    for( self->i = 0; self->i < self->overlays.count; self->i++ )
    {
        struct ToriRS_Flotype* flo = CacheProvider_FlotypeGet(p, self->overlays.items[self->i]);
        if( flo && flo->texture >= 0 )
            idset_add(&self->textures, flo->texture);
    }
    for( self->i = 0; self->i < self->textures.count; self->i++ )
        TASK_AWAITSELF_IF(
            CacheProvider_TextureHas(p, self->textures.items[self->i])
                ? NULL
                : CreateTask_TextureLoad(p, self->textures.items[self->i]));

    /* 4. Scenery locs, then their models. */
    for( self->i = 0; self->i < self->locs.count; self->i++ )
        TASK_AWAITSELF_IF(
            CacheProvider_LocationHas(p, self->locs.items[self->i])
                ? NULL
                : CreateTask_LocLoad(p, self->locs.items[self->i]));

    for( self->i = 0; self->i < self->locs.count; self->i++ )
        world_load_collect_loc_models(
            CacheProvider_LocationGet(p, self->locs.items[self->i]), &self->models);

    for( self->i = 0; self->i < self->models.count; self->i++ )
        TASK_AWAITSELF_IF(
            CacheProvider_ModelHas(p, self->models.items[self->i])
                ? NULL
                : CreateTask_ModelLoad(p, self->models.items[self->i]));

    /* 4c. Textures referenced by the loaded loc models (face texture ids) and
     * by loc retextures — preload so scenery is textured at rebuild (v1
     * preloads every texture; app_sync_textures remains the safety net). */
    for( self->i = 0; self->i < self->models.count; self->i++ )
    {
        struct ToriRS_Model* model = CacheProvider_ModelGet(p, self->models.items[self->i]);
        if( !model || !model->face_textures )
            continue;
        for( int f = 0; f < model->face_count; f++ )
            if( model->face_textures[f] != (gc_faceint_t)-1 )
                idset_add(&self->textures, (int)model->face_textures[f]);
    }
    for( self->i = 0; self->i < self->locs.count; self->i++ )
    {
        struct ToriRS_Location* loc = CacheProvider_LocationGet(p, self->locs.items[self->i]);
        if( !loc )
            continue;
        for( int r = 0; r < loc->retexture_count; r++ )
            if( loc->retextures_to[r] >= 0 )
                idset_add(&self->textures, loc->retextures_to[r]);
    }
    for( self->i = 0; self->i < self->textures.count; self->i++ )
        TASK_AWAITSELF_IF(
            CacheProvider_TextureHas(p, self->textures.items[self->i])
                ? NULL
                : CreateTask_TextureLoad(p, self->textures.items[self->i]));

    /* 4d. Sequences for animated locs — registered in the scene animation
     * registry so scenery_load_animation can bind them during the rebuild.
     * CreateTask_Dat2SequenceLoad no-ops (NULL) when already registered. */
    for( self->i = 0; self->i < self->locs.count; self->i++ )
    {
        struct ToriRS_Location* loc = CacheProvider_LocationGet(p, self->locs.items[self->i]);
        if( loc && loc->seq_id >= 0 )
            idset_add(&self->seqs, loc->seq_id);
    }
    for( self->i = 0; self->i < self->seqs.count; self->i++ )
        TASK_AWAITSELF_IF(
            CreateTask_Dat2SequenceLoad(p, self->builder->scene, self->seqs.items[self->i]));

    fprintf(
        stderr,
        "world_load: %d chunks, %d underlays, %d overlays, %d textures, %d locs, %d models, "
        "%d seqs\n",
        self->chunk_count,
        self->underlays.count,
        self->overlays.count,
        self->textures.count,
        self->locs.count,
        self->models.count,
        self->seqs.count);

    /* 5. Synchronous rebuild: world + scene elements from the loaded assets. */
    WorldBuilder_RebuildChunklist(self->builder, self->chunks_xz, self->chunk_count);
    World_SetLoadComplete(self->builder->world, true);

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
    const int* chunks_xz,
    int chunk_count)
{
    struct Task_WorldLoad* task;

    assert(provider);
    assert(builder);
    assert(chunks_xz);
    assert(chunk_count > 0 && chunk_count <= WORLD_LOAD_MAX_CHUNKS);

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_WorldLoad_VTable;
    strcpy(task->task.name, "WorldLoad");
    task->provider = provider;
    task->builder = builder;
    memcpy(task->chunks_xz, chunks_xz, (size_t)chunk_count * 2 * sizeof(int));
    task->chunk_count = chunk_count;
    PT_INIT(&task->pt);
    return &task->task;
}
