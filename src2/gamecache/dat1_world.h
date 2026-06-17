#ifndef GAMECACHE_DAT1_WORLD_H
#define GAMECACHE_DAT1_WORLD_H

#include "../ioqueue/libtorirs_ioqueue.h"
#include "../libtorirs.h"
#include "3rd/minipt.h"
#include "buildcache/dat1_buildcache.h"
#include "dat1io.h"
#include "gamecache_l.h"
#include "gamecache_submit.h"

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
    struct GameCacheL* gamecache_l;
    int zonex;
    int zonez;

    int chunk_count;
    int chunks_x[TASK_WORLD_MAX_CHUNKS];
    int chunks_z[TASK_WORLD_MAX_CHUNKS];
    int chunk_index;

    int pending_fetches;
    int pending_decodes;

    int anim_count;
    int anim_index;

    int* model_ids;
    int model_count;
    int model_index;
};

struct Task_Dat1WorldRebuildNormal*
Task_Dat1WorldRebuildNormal_New(
    struct GameCacheL* gamecache_l,
    int zonex,
    int zonez)
{
    struct Task_Dat1WorldRebuildNormal* task =
        calloc(1, sizeof(struct Task_Dat1WorldRebuildNormal));
    assert(task);
    PT_INIT(&task->thread);
    task->gamecache_l = gamecache_l;
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
    struct Dat1BuildCache* dat1_bc = dat1(task->gamecache_l);
    struct GameCache* gc = gamecache(task->gamecache_l);

    PT_BEGIN(&task->thread);

    dat1_buildcache_clear_config_jagfile(dat1_bc);
    dat1_buildcache_clear_versionlist_jagfile(dat1_bc);
    LibToriRS_IOQueueClear(ctx->io);
    task_world_compute_chunks(task);

    task->pending_fetches = 0;
    for( task->chunk_index = 0; task->chunk_index < task->chunk_count; task->chunk_index++ )
    {
        int mapx = task->chunks_x[task->chunk_index];
        int mapz = task->chunks_z[task->chunk_index];
        int map_id = (mapx << 16) | (mapz & 0xFFFF);
        if( !dat1_buildcache_map_terrain_has(dat1_bc, map_id) )
        {
            dat1io_map_chunk_terrain_fetch(ctx, mapx, mapz);
            task->pending_fetches++;
        }
    }
    PT_YIELD(&task->thread);

    for( task->pending_decodes = 0; task->pending_decodes < task->pending_fetches;
         task->pending_decodes++ )
    {
        struct CacheMapTerrain* terrain = NULL;
        int map_id = dat1io_map_chunk_terrain_decode(ctx, &terrain);
        if( map_id >= 0 && terrain )
        {
            dat1_buildcache_map_terrain_add(dat1_bc, map_id, terrain);
            gamecache_submit_map_terrain_from_dat1(gc, dat1_bc, map_id);
        }
    }

    task->pending_fetches = 0;
    for( task->chunk_index = 0; task->chunk_index < task->chunk_count; task->chunk_index++ )
    {
        int mapx = task->chunks_x[task->chunk_index];
        int mapz = task->chunks_z[task->chunk_index];
        int map_id = (mapx << 16) | (mapz & 0xFFFF);
        if( !dat1_buildcache_map_scenery_has(dat1_bc, map_id) )
        {
            dat1io_map_chunk_scenery_fetch(ctx, mapx, mapz);
            task->pending_fetches++;
        }
    }
    PT_YIELD(&task->thread);

    for( task->pending_decodes = 0; task->pending_decodes < task->pending_fetches;
         task->pending_decodes++ )
    {
        struct CacheMapLocs* locs = NULL;
        int map_id = dat1io_map_chunk_scenery_decode(ctx, &locs);
        if( map_id >= 0 && locs )
        {
            dat1_buildcache_map_scenery_add(dat1_bc, map_id, locs);
            gamecache_submit_map_scenery_from_dat1(gc, dat1_bc, map_id);
        }
    }

    dat1io_config_jagfile_fetch(ctx);
    dat1io_versionlist_jagfile_fetch(ctx);
    PT_YIELD(&task->thread);

    {
        struct FileListDat* config_jag = dat1io_config_jagfile_decode(ctx);
        struct FileListDat* versionlist_jag = dat1io_versionlist_jagfile_decode(ctx);
        if( config_jag )
            dat1_buildcache_set_fromconfigtable_config_jagfile(dat1_bc, config_jag);
        if( versionlist_jag )
            dat1_buildcache_set_versionlist_jagfile(dat1_bc, versionlist_jag);
    }
    LibToriRS_IOQueueClear(ctx->io);

    task->anim_count = dat1_buildcache_get_animbaseframes_count_from_versionlist_jagfile(dat1_bc);
    for( task->anim_index = 0; task->anim_index < task->anim_count; )
    {
        task->pending_fetches = 0;
        int batch_end = task->anim_index + TASK_WORLD_IO_BATCH;
        if( batch_end > task->anim_count )
            batch_end = task->anim_count;

        for( ; task->anim_index < batch_end; task->anim_index++ )
        {
            if( !dat1_buildcache_animbaseframes_has(dat1_bc, task->anim_index) )
            {
                dat1io_animations_fetch(ctx, task->anim_index);
                task->pending_fetches++;
            }
        }

        if( task->pending_fetches == 0 )
            continue;

        PT_YIELD(&task->thread);

        for( task->pending_decodes = 0; task->pending_decodes < task->pending_fetches;
             task->pending_decodes++ )
        {
            struct CacheDatAnimBaseFrames* abf = NULL;
            int anim_id = dat1io_animations_decode(ctx, &abf);
            if( anim_id >= 0 && abf )
            {
                dat1_buildcache_animbaseframes_add(dat1_bc, anim_id, abf);
                gamecache_submit_animation_from_dat1(gc, dat1_bc, anim_id);
            }
        }
        LibToriRS_IOQueueClear(ctx->io);
    }

    dat1_buildcache_sequences_init_from_config_jagfile(dat1_bc);
    dat1_buildcache_floortypes_init_from_config_jagfile(dat1_bc);
    dat1_buildcache_init_scenery_configs_from_config_jagfile(dat1_bc);
    gamecache_submit_all_sequences_from_dat1(gc, dat1_bc);
    gamecache_submit_all_flotypes_from_dat1(gc, dat1_bc);
    gamecache_submit_all_locations_from_dat1(gc, dat1_bc);

    task->model_count = dat1_buildcache_get_all_unique_scenery_model_ids(dat1_bc, &task->model_ids);
    for( task->model_index = 0; task->model_index < task->model_count; )
    {
        task->pending_fetches = 0;
        int batch_end = task->model_index + TASK_WORLD_IO_BATCH;
        if( batch_end > task->model_count )
            batch_end = task->model_count;

        for( ; task->model_index < batch_end; task->model_index++ )
        {
            int model_id = task->model_ids[task->model_index];
            if( !dat1_buildcache_model_get(dat1_bc, model_id) )
            {
                dat1io_model_fetch(ctx, model_id);
                task->pending_fetches++;
            }
        }

        if( task->pending_fetches == 0 )
            continue;

        PT_YIELD(&task->thread);

        for( task->pending_decodes = 0; task->pending_decodes < task->pending_fetches;
             task->pending_decodes++ )
        {
            struct CacheModel* model = NULL;
            int decoded_model_id = -1;
            model = dat1io_model_decode(ctx, &decoded_model_id);
            if( model && decoded_model_id >= 0 )
            {
                dat1_buildcache_model_add(dat1_bc, decoded_model_id, model);
                gamecache_submit_model_from_dat1(gc, dat1_bc, decoded_model_id);
            }
        }
        LibToriRS_IOQueueClear(ctx->io);
    }

    PT_END(&task->thread);
}

#endif
