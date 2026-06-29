#ifndef TORIAUXLIBC_T_RUNESCAPE_DAT1_WORLD_H
#define TORIAUXLIBC_T_RUNESCAPE_DAT1_WORLD_H

#include "../../ioqueue/libtorirs_ioqueue.h"
#include "../../libtorirs.h"
#include "3rd/minipt.h"
#include "buildcache/dat1_buildcache.h"
#include "toriauxlib/c/dat1io.h"
#include "toriauxlib/c/toriauxlibc.h"
#include "toriauxlib/c/toriauxlibc_submit.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define TASK_WORLD_SCENE_WIDTH 104
#define TASK_WORLD_MAX_CHUNKS 64
#define TASK_WORLD_MAX_MODELS 8192
#define TASK_WORLD_IO_BATCH 64

struct Task_Dat1WorldRebuildNormal
{
    struct pt thread;
    struct ToriAuxLibC* c;
    int zonex;
    int zonez;

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

struct Task_Dat1WorldRebuildNormal*
Task_Dat1WorldRebuildNormal_New(
    struct ToriAuxLibC* c,
    int zonex,
    int zonez)
{
    struct Task_Dat1WorldRebuildNormal* task =
        calloc(1, sizeof(struct Task_Dat1WorldRebuildNormal));
    assert(task);
    PT_INIT(&task->thread);
    task->c = c;
    task->zonex = zonex;
    task->zonez = zonez;
    return task;
}

void
Task_Dat1WorldRebuildNormal_Free(struct Task_Dat1WorldRebuildNormal* task)
{
    if( !task )
        return;
    free(task->model_ids);
    free(task);
}

static void
task_world_compute_chunks(struct Task_Dat1WorldRebuildNormal* task)
{
    int zone_padding = TASK_WORLD_SCENE_WIDTH / (2 * 8);
    int zone_sw_x = task->zonex - zone_padding;
    int zone_sw_z = task->zonez - zone_padding;
    int zone_ne_x = task->zonex + zone_padding;
    int zone_ne_z = task->zonez + zone_padding;

    int map_sw_x = zone_sw_x / 8;
    int map_sw_z = zone_sw_z / 8;
    int map_ne_x = zone_ne_x / 8;
    int map_ne_z = zone_ne_z / 8;

    task->chunk_count = 0;
    for( int x = map_sw_x; x <= map_ne_x; x++ )
    {
        for( int z = map_sw_z; z <= map_ne_z; z++ )
        {
            assert(task->chunk_count < TASK_WORLD_MAX_CHUNKS);
            task->chunks_x[task->chunk_count] = x;
            task->chunks_z[task->chunk_count] = z;
            task->chunk_count++;
        }
    }
}

int
Task_Dat1WorldRebuildNormal_Run(
    struct Task_Dat1WorldRebuildNormal* task,
    struct LibToriRS_IOContext* ctx)
{
    struct Dat1BuildCache* dat1_bc = dat1(task->c);

    PT_BEGIN(&task->thread);

    dat1_buildcache_clear_config_jagfile(dat1_bc);
    dat1_buildcache_clear_versionlist_jagfile(dat1_bc);

    task_world_compute_chunks(task);

    LibToriRS_IOBatchReset(&task->io_batch);
    for( task->chunk_index = 0; task->chunk_index < task->chunk_count; task->chunk_index++ )
    {
        int mapx = task->chunks_x[task->chunk_index];
        int mapz = task->chunks_z[task->chunk_index];
        int map_id = (mapx << 16) | (mapz & 0xFFFF);
        if( !dat1_buildcache_map_terrain_has(dat1_bc, map_id) )
        {
            int slot = LibToriRS_IOBatchAdd(&task->io_batch, 0);
            IO_REQUEST(ctx, slot, TAPIDat1_FetchMapChunkTerrain(ctx, mapx, mapz));
        }
    }
    PT_YIELD(&task->thread);

    for( int i = 0; i < LibToriRS_IOBatchCount(&task->io_batch); i++ )
    {
        struct RSCacheDat2A_MapTerrain* terrain = NULL;
        int map_id = TAPIDat1_DecodeMapChunkTerrain(ctx, i, &terrain);
        if( map_id >= 0 && terrain )
        {
            dat1_buildcache_map_terrain_add(dat1_bc, map_id, terrain);
            ToriAuxLibC_SubmitMapTerrainFromDat1(task->c, map_id);
        }
    }

    LibToriRS_IOBatchReset(&task->io_batch);
    for( task->chunk_index = 0; task->chunk_index < task->chunk_count; task->chunk_index++ )
    {
        int mapx = task->chunks_x[task->chunk_index];
        int mapz = task->chunks_z[task->chunk_index];
        int map_id = (mapx << 16) | (mapz & 0xFFFF);
        if( !dat1_buildcache_map_scenery_has(dat1_bc, map_id) )
        {
            int slot = LibToriRS_IOBatchAdd(&task->io_batch, 0);
            IO_REQUEST(ctx, slot, TAPIDat1_FetchMapChunkScenery(ctx, mapx, mapz));
        }
    }
    PT_YIELD(&task->thread);

    for( int i = 0; i < LibToriRS_IOBatchCount(&task->io_batch); i++ )
    {
        struct RSCacheDat2A_MapLocs* locs = NULL;
        int map_id = TAPIDat1_DecodeMapChunkScenery(ctx, i, &locs);
        if( map_id >= 0 && locs )
        {
            dat1_buildcache_map_scenery_add(dat1_bc, map_id, locs);
            ToriAuxLibC_SubmitMapSceneryFromDat1(task->c, map_id);
        }
    }

    IO_REQUEST(ctx, 0, TAPIDat1_FetchConfigJagfile(ctx));
    IO_REQUEST(ctx, 1, TAPIDat1_FetchVersionlistJagfile(ctx));
    PT_YIELD(&task->thread);

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

    task->anim_count = dat1_buildcache_get_animbaseframes_count_from_versionlist_jagfile(dat1_bc);
    for( task->anim_index = 0; task->anim_index < task->anim_count; )
    {
        LibToriRS_IOBatchReset(&task->io_batch);
        int batch_end = task->anim_index + TASK_WORLD_IO_BATCH;
        if( batch_end > task->anim_count )
            batch_end = task->anim_count;

        for( ; task->anim_index < batch_end; task->anim_index++ )
        {
            if( !dat1_buildcache_animbaseframes_has(dat1_bc, task->anim_index) )
            {
                int slot = LibToriRS_IOBatchAdd(&task->io_batch, 0);
                IO_REQUEST(ctx, slot, TAPIDat1_FetchAnimations(ctx, task->anim_index));
            }
        }

        if( LibToriRS_IOBatchEmpty(&task->io_batch) )
            continue;

        PT_YIELD(&task->thread);

        for( int i = 0; i < LibToriRS_IOBatchCount(&task->io_batch); i++ )
        {
            struct RSCacheDat1A_AnimBaseFrames* abf = NULL;
            int anim_id = TAPIDat1_DecodeAnimations(ctx, i, &abf);
            if( anim_id >= 0 && abf )
            {
                dat1_buildcache_animbaseframes_add(dat1_bc, anim_id, abf);
                ToriAuxLibC_SubmitAnimationFromDat1(task->c, anim_id);
            }
        }
        LibToriRS_IOQueueClear(ctx->io);
    }

    dat1_buildcache_sequences_init_from_config_jagfile(dat1_bc);
    dat1_buildcache_floortypes_init_from_config_jagfile(dat1_bc);
    dat1_buildcache_init_scenery_configs_from_config_jagfile(dat1_bc);
    ToriAuxLibC_SubmitAllSequencesFromDat1(task->c);
    ToriAuxLibC_SubmitAllFlotypesFromDat1(task->c);
    ToriAuxLibC_SubmitAllLocationsFromDat1(task->c);

    task->model_count = dat1_buildcache_get_all_unique_scenery_model_ids(dat1_bc, &task->model_ids);
    for( task->model_index = 0; task->model_index < task->model_count; )
    {
        LibToriRS_IOBatchReset(&task->io_batch);
        int batch_end = task->model_index + TASK_WORLD_IO_BATCH;
        if( batch_end > task->model_count )
            batch_end = task->model_count;

        for( ; task->model_index < batch_end; task->model_index++ )
        {
            int model_id = task->model_ids[task->model_index];
            if( !dat1_buildcache_model_get(dat1_bc, model_id) )
            {
                int slot = LibToriRS_IOBatchAdd(&task->io_batch, model_id);
                IO_REQUEST(ctx, slot, TAPIDat1_FetchModel(ctx, model_id));
            }
        }

        if( LibToriRS_IOBatchEmpty(&task->io_batch) )
            continue;

        PT_YIELD(&task->thread);

        for( int i = 0; i < LibToriRS_IOBatchCount(&task->io_batch); i++ )
        {
            struct RSCacheDat2A_Model* model = TAPIDat1_DecodeModel(ctx, i);
            if( model )
            {
                int model_id = LibToriRS_IOBatchUser(&task->io_batch, i);
                dat1_buildcache_model_add(dat1_bc, model_id, model);
                ToriAuxLibC_SubmitModelFromDat1(task->c, model_id);
            }
        }
        LibToriRS_IOQueueClear(ctx->io);
    }

    PT_END(&task->thread);
}

#endif
