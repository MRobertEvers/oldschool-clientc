#ifndef TORIAUXLIB_TASK_RUNESCAPE_DAT1_WORLD_H
#define TORIAUXLIB_TASK_RUNESCAPE_DAT1_WORLD_H

#include "../../../ioqueue/libtorirs_ioqueue.h"
#include "../../../libtorirs.h"
#include "3rd/minipt.h"
#include "buildcache/dat1_buildcache.h"
#include "core/tapi/tapi_dat1.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/c/toriauxlibcache_submit.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define TASK_WORLD_SCENE_WIDTH 104
#define TASK_WORLD_MAX_CHUNKS 64
#define TASK_WORLD_MAX_MODELS 8192
#define TASK_WORLD_IO_BATCH 64

struct Task_Dat1WorldRebuildCore
{
    struct pt thread;
    struct ToriAuxLibCache* c;

    int chunk_count;
    int chunks_x[TASK_WORLD_MAX_CHUNKS];
    int chunks_z[TASK_WORLD_MAX_CHUNKS];
    int chunk_index;

    struct LibToriRS_IOBatch io_batch;

    int anim_count;
    int anim_index;

    int* model_ids;
    int model_count;
    int model_index;
};

static void
Task_Dat1WorldRebuildCore_Init(
    struct Task_Dat1WorldRebuildCore* core,
    struct ToriAuxLibCache* c)
{
    PT_INIT(&core->thread);
    core->c = c;
}

static void
Task_Dat1WorldRebuildCore_SetChunks(
    struct Task_Dat1WorldRebuildCore* core,
    const int* xs,
    const int* zs,
    int count)
{
    assert(count <= TASK_WORLD_MAX_CHUNKS);
    core->chunk_count = count;
    for( int i = 0; i < count; i++ )
    {
        core->chunks_x[i] = xs[i];
        core->chunks_z[i] = zs[i];
    }
}

static void
Task_Dat1WorldRebuildCore_Cleanup(struct Task_Dat1WorldRebuildCore* core)
{
    free(core->model_ids);
    core->model_ids = NULL;
}

static void
task_dat1_world_compute_centerzone_chunks(
    struct Task_Dat1WorldRebuildCore* core,
    int zonex,
    int zonez)
{
    int zone_padding = TASK_WORLD_SCENE_WIDTH / (2 * 8);
    int zone_sw_x = zonex - zone_padding;
    int zone_sw_z = zonez - zone_padding;
    int zone_ne_x = zonex + zone_padding;
    int zone_ne_z = zonez + zone_padding;

    int map_sw_x = zone_sw_x / 8;
    int map_sw_z = zone_sw_z / 8;
    int map_ne_x = zone_ne_x / 8;
    int map_ne_z = zone_ne_z / 8;

    int count = 0;
    for( int x = map_sw_x; x <= map_ne_x; x++ )
    {
        for( int z = map_sw_z; z <= map_ne_z; z++ )
        {
            assert(count < TASK_WORLD_MAX_CHUNKS);
            core->chunks_x[count] = x;
            core->chunks_z[count] = z;
            count++;
        }
    }
    core->chunk_count = count;
}

int
Task_Dat1WorldRebuildCore_Run(
    struct Task_Dat1WorldRebuildCore* core,
    struct LibToriRS_IOContext* ctx)
{
    struct Dat1BuildCache* dat1_bc = dat1(core->c);

    PT_BEGIN(&core->thread);

    dat1_buildcache_clear_config_jagfile(dat1_bc);
    dat1_buildcache_clear_versionlist_jagfile(dat1_bc);

    LibToriRS_IOBatchReset(&core->io_batch);
    for( core->chunk_index = 0; core->chunk_index < core->chunk_count; core->chunk_index++ )
    {
        int mapx = core->chunks_x[core->chunk_index];
        int mapz = core->chunks_z[core->chunk_index];
        int map_id = (mapx << 16) | (mapz & 0xFFFF);
        if( !dat1_buildcache_map_terrain_has(dat1_bc, map_id) )
        {
            int slot = LibToriRS_IOBatchAdd(&core->io_batch, 0);
            IO_REQUEST(ctx, slot, TAPIDat1_FetchMapChunkTerrain(ctx, mapx, mapz));
        }
    }
    PT_YIELD(&core->thread);

    for( int i = 0; i < LibToriRS_IOBatchCount(&core->io_batch); i++ )
    {
        struct RSCacheDat2A_MapTerrain* terrain = NULL;
        int map_id = TAPIDat1_DecodeMapChunkTerrain(ctx, i, &terrain);
        if( map_id >= 0 && terrain )
        {
            dat1_buildcache_map_terrain_add(dat1_bc, map_id, terrain);
            ToriAuxLibCache_SubmitMapTerrainFromDat1(core->c, map_id);
        }
    }
    dat1_buildcache_map_terrain_cleanup(dat1_bc);

    LibToriRS_IOBatchReset(&core->io_batch);
    for( core->chunk_index = 0; core->chunk_index < core->chunk_count; core->chunk_index++ )
    {
        int mapx = core->chunks_x[core->chunk_index];
        int mapz = core->chunks_z[core->chunk_index];
        int map_id = (mapx << 16) | (mapz & 0xFFFF);
        if( !dat1_buildcache_map_scenery_has(dat1_bc, map_id) )
        {
            int slot = LibToriRS_IOBatchAdd(&core->io_batch, 0);
            IO_REQUEST(ctx, slot, TAPIDat1_FetchMapChunkScenery(ctx, mapx, mapz));
        }
    }
    PT_YIELD(&core->thread);

    for( int i = 0; i < LibToriRS_IOBatchCount(&core->io_batch); i++ )
    {
        struct RSCacheDat2A_MapLocs* locs = NULL;
        int map_id = TAPIDat1_DecodeMapChunkScenery(ctx, i, &locs);
        if( map_id >= 0 && locs )
        {
            dat1_buildcache_map_scenery_add(dat1_bc, map_id, locs);
            ToriAuxLibCache_SubmitMapSceneryFromDat1(core->c, map_id);
        }
    }

    IO_REQUEST(ctx, 0, TAPIDat1_FetchConfigJagfile(ctx));
    IO_REQUEST(ctx, 1, TAPIDat1_FetchVersionlistJagfile(ctx));
    PT_YIELD(&core->thread);

    {
        struct RSCacheShared_FileListDat* config_jag = TAPIDat1_DecodeConfigJagfile(ctx, 0);
        struct RSCacheShared_FileListDat* versionlist_jag =
            TAPIDat1_DecodeVersionlistJagfile(ctx, 1);
        if( config_jag )
            dat1_buildcache_set_fromconfigtable_config_jagfile(dat1_bc, config_jag);
        if( versionlist_jag )
            dat1_buildcache_set_versionlist_jagfile(dat1_bc, versionlist_jag);
    }
    LibToriRS_IOQueueClear(ctx->io);

    core->anim_count = dat1_buildcache_get_animbaseframes_count_from_versionlist_jagfile(dat1_bc);
    for( core->anim_index = 0; core->anim_index < core->anim_count; )
    {
        LibToriRS_IOBatchReset(&core->io_batch);
        int batch_end = core->anim_index + TASK_WORLD_IO_BATCH;
        if( batch_end > core->anim_count )
            batch_end = core->anim_count;

        for( ; core->anim_index < batch_end; core->anim_index++ )
        {
            if( !dat1_buildcache_animbaseframes_has(dat1_bc, core->anim_index) )
            {
                int slot = LibToriRS_IOBatchAdd(&core->io_batch, 0);
                IO_REQUEST(ctx, slot, TAPIDat1_FetchAnimations(ctx, core->anim_index));
            }
        }

        if( LibToriRS_IOBatchEmpty(&core->io_batch) )
            continue;

        PT_YIELD(&core->thread);

        for( int i = 0; i < LibToriRS_IOBatchCount(&core->io_batch); i++ )
        {
            struct RSCacheDat1A_AnimBaseFrames* abf = NULL;
            int anim_id = TAPIDat1_DecodeAnimations(ctx, i, &abf);
            if( anim_id >= 0 && abf )
            {
                dat1_buildcache_animbaseframes_add(dat1_bc, anim_id, abf);
                ToriAuxLibCache_SubmitAnimationFromDat1(core->c, anim_id);
            }
        }
        LibToriRS_IOQueueClear(ctx->io);
    }

    dat1_buildcache_sequences_init_from_config_jagfile(dat1_bc);
    dat1_buildcache_floortypes_init_from_config_jagfile(dat1_bc);
    dat1_buildcache_init_scenery_configs_from_config_jagfile(dat1_bc);
    ToriAuxLibCache_SubmitAllSequencesFromDat1(core->c);
    ToriAuxLibCache_SubmitAllFlotypesFromDat1(core->c);
    ToriAuxLibCache_SubmitAllLocationsFromDat1(core->c);
    dat1_buildcache_sequences_cleanup(dat1_bc);
    dat1_buildcache_floortypes_cleanup(dat1_bc);

    core->model_count = dat1_buildcache_get_all_unique_scenery_model_ids(dat1_bc, &core->model_ids);
    dat1_buildcache_map_scenery_cleanup(dat1_bc);
    dat1_buildcache_scenery_configs_cleanup(dat1_bc);
    for( core->model_index = 0; core->model_index < core->model_count; )
    {
        LibToriRS_IOBatchReset(&core->io_batch);
        int batch_end = core->model_index + TASK_WORLD_IO_BATCH;
        if( batch_end > core->model_count )
            batch_end = core->model_count;

        for( ; core->model_index < batch_end; core->model_index++ )
        {
            int model_id = core->model_ids[core->model_index];
            if( !dat1_buildcache_model_get(dat1_bc, model_id) )
            {
                int slot = LibToriRS_IOBatchAdd(&core->io_batch, model_id);
                IO_REQUEST(ctx, slot, TAPIDat1_FetchModel(ctx, model_id));
            }
        }

        if( LibToriRS_IOBatchEmpty(&core->io_batch) )
            continue;

        PT_YIELD(&core->thread);

        for( int i = 0; i < LibToriRS_IOBatchCount(&core->io_batch); i++ )
        {
            struct RSCacheDat2A_Model* model = TAPIDat1_DecodeModel(ctx, i);
            if( model )
            {
                int model_id = LibToriRS_IOBatchUser(&core->io_batch, i);
                dat1_buildcache_model_add(dat1_bc, model_id, model);
                ToriAuxLibCache_SubmitModelFromDat1(core->c, model_id);
                dat1_buildcache_model_remove(dat1_bc, model_id);
            }
        }
        LibToriRS_IOQueueClear(ctx->io);
    }

    ToriAuxLibCache_PruneBuildCaches(core->c);

    PT_END(&core->thread);
}

struct Task_Dat1WorldRebuildNormalCenterzone
{
    struct Task_Dat1WorldRebuildCore core;
    int zonex;
    int zonez;
};

struct Task_Dat1WorldRebuildNormalCenterzone*
Task_Dat1WorldRebuildNormalCenterzone_New(
    struct ToriAuxLibCache* c,
    int zonex,
    int zonez)
{
    struct Task_Dat1WorldRebuildNormalCenterzone* task =
        calloc(1, sizeof(struct Task_Dat1WorldRebuildNormalCenterzone));
    assert(task);
    Task_Dat1WorldRebuildCore_Init(&task->core, c);
    task->zonex = zonex;
    task->zonez = zonez;
    task_dat1_world_compute_centerzone_chunks(&task->core, zonex, zonez);
    return task;
}

void
Task_Dat1WorldRebuildNormalCenterzone_Free(struct Task_Dat1WorldRebuildNormalCenterzone* task)
{
    if( !task )
        return;
    Task_Dat1WorldRebuildCore_Cleanup(&task->core);
    free(task);
}

int
Task_Dat1WorldRebuildNormalCenterzone_Run(
    struct Task_Dat1WorldRebuildNormalCenterzone* task,
    struct LibToriRS_IOContext* ctx)
{
    return Task_Dat1WorldRebuildCore_Run(&task->core, ctx);
}

struct Task_Dat1WorldRebuildChunkList
{
    struct Task_Dat1WorldRebuildCore core;
};

struct Task_Dat1WorldRebuildChunkList*
Task_Dat1WorldRebuildChunkList_New(
    struct ToriAuxLibCache* c,
    const int* chunks_x,
    const int* chunks_z,
    int count)
{
    struct Task_Dat1WorldRebuildChunkList* task =
        calloc(1, sizeof(struct Task_Dat1WorldRebuildChunkList));
    assert(task);
    Task_Dat1WorldRebuildCore_Init(&task->core, c);
    Task_Dat1WorldRebuildCore_SetChunks(&task->core, chunks_x, chunks_z, count);
    return task;
}

void
Task_Dat1WorldRebuildChunkList_Free(struct Task_Dat1WorldRebuildChunkList* task)
{
    if( !task )
        return;
    Task_Dat1WorldRebuildCore_Cleanup(&task->core);
    free(task);
}

int
Task_Dat1WorldRebuildChunkList_Run(
    struct Task_Dat1WorldRebuildChunkList* task,
    struct LibToriRS_IOContext* ctx)
{
    return Task_Dat1WorldRebuildCore_Run(&task->core, ctx);
}

#endif
