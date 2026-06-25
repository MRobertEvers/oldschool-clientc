#ifndef TORIAUXLIBC_DAT2_WORLD_H
#define TORIAUXLIBC_DAT2_WORLD_H

#include "../../ioqueue/libtorirs_ioqueue.h"
#include "../../libtorirs.h"
#include "3rd/minipt.h"
#include "buildcache/dat2_buildcache.h"
#include "osrs/rscache/dat2a/dat2a_configs.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "toriauxlib/c/dat2io.h"
#include "toriauxlib/c/toriauxlibc.h"
#include "toriauxlib/c/toriauxlibc_submit.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define TASK_DAT2_WORLD_SCENE_WIDTH 104
#define TASK_DAT2_WORLD_MAX_CHUNKS 64
#define TASK_DAT2_WORLD_MAX_MODELS 8192
#define TASK_DAT2_WORLD_IO_BATCH 64

struct Task_Dat2WorldRebuildNormal
{
    struct pt thread;
    struct ToriAuxLibC* c;
    int zonex;
    int zonez;

    int chunk_count;
    int chunks_x[TASK_DAT2_WORLD_MAX_CHUNKS];
    int chunks_z[TASK_DAT2_WORLD_MAX_CHUNKS];
    int chunk_index;

    int pending_fetches;
    int pending_decodes;

    int* model_ids;
    int model_count;
    int model_index;
};

struct Task_Dat2WorldRebuildNormal*
Task_Dat2WorldRebuildNormal_New(
    struct ToriAuxLibC* c,
    int zonex,
    int zonez)
{
    struct Task_Dat2WorldRebuildNormal* task =
        calloc(1, sizeof(struct Task_Dat2WorldRebuildNormal));
    assert(task);
    PT_INIT(&task->thread);
    task->c = c;
    task->zonex = zonex;
    task->zonez = zonez;
    return task;
}

void
Task_Dat2WorldRebuildNormal_Free(struct Task_Dat2WorldRebuildNormal* task)
{
    if( !task )
        return;
    free(task->model_ids);
    free(task);
}

static void
task_dat2_world_compute_chunks(struct Task_Dat2WorldRebuildNormal* task)
{
    int zone_padding = TASK_DAT2_WORLD_SCENE_WIDTH / (2 * 8);
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
            assert(task->chunk_count < TASK_DAT2_WORLD_MAX_CHUNKS);
            task->chunks_x[task->chunk_count] = x;
            task->chunks_z[task->chunk_count] = z;
            task->chunk_count++;
        }
    }
}

int
Task_Dat2WorldRebuildNormal_Run(
    struct Task_Dat2WorldRebuildNormal* task,
    struct LibToriRS_IOContext* ctx)
{
    struct Dat2BuildCache* dat2_bc = dat2(task->c);
    struct RSCacheDat2Disk* cache_disk = ToriAuxLibC_Dat2Disk(task->c);

    PT_BEGIN(&task->thread);

    LibToriRS_IOQueueClear(ctx->io);
    task_dat2_world_compute_chunks(task);

    task->pending_fetches = 0;
    for( task->chunk_index = 0; task->chunk_index < task->chunk_count; task->chunk_index++ )
    {
        int mapx = task->chunks_x[task->chunk_index];
        int mapz = task->chunks_z[task->chunk_index];
        int map_id = (mapx << 16) | (mapz & 0xFFFF);
        if( !dat2_buildcache_map_terrain_has(dat2_bc, map_id) )
        {
            dat2io_map_chunk_terrain_fetch(ctx, mapx, mapz);
            task->pending_fetches++;
        }
    }
    PT_YIELD(&task->thread);

    for( task->pending_decodes = 0; task->pending_decodes < task->pending_fetches;
         task->pending_decodes++ )
    {
        struct RSCacheDat2A_MapTerrain* terrain = NULL;
        int map_id = dat2io_map_chunk_terrain_decode(ctx, &terrain);
        if( map_id >= 0 && terrain )
        {
            dat2_buildcache_map_terrain_add(dat2_bc, map_id, terrain);
            ToriAuxLibC_SubmitMapTerrainFromDat2(task->c, map_id);
        }
    }

    task->pending_fetches = 0;
    for( task->chunk_index = 0; task->chunk_index < task->chunk_count; task->chunk_index++ )
    {
        int mapx = task->chunks_x[task->chunk_index];
        int mapz = task->chunks_z[task->chunk_index];
        int map_id = (mapx << 16) | (mapz & 0xFFFF);
        if( !dat2_buildcache_map_scenery_has(dat2_bc, map_id) )
        {
            dat2io_map_chunk_scenery_fetch(ctx, mapx, mapz);
            task->pending_fetches++;
        }
    }
    PT_YIELD(&task->thread);

    for( task->pending_decodes = 0; task->pending_decodes < task->pending_fetches;
         task->pending_decodes++ )
    {
        struct RSCacheDat2A_MapLocs* locs = NULL;
        int map_id = dat2io_map_chunk_scenery_decode(ctx, &locs);
        if( map_id >= 0 && locs )
        {
            dat2_buildcache_map_scenery_add(dat2_bc, map_id, locs);
            ToriAuxLibC_SubmitMapSceneryFromDat2(task->c, map_id);
        }
    }

    dat2io_config_group_fetch(ctx, RSCacheDat2A_ConfigKind_Underlay);
    dat2io_config_group_fetch(ctx, RSCacheDat2A_ConfigKind_Overlay);
    dat2io_config_group_fetch(ctx, RSCacheDat2A_ConfigKind_Sequence);
    dat2io_config_group_fetch(ctx, RSCacheDat2A_ConfigKind_Locs);
    PT_YIELD(&task->thread);

    {
        struct RSCacheDat2Disk_Archive* underlay_archive =
            dat2io_config_group_decode(ctx, RSCacheDat2A_ConfigKind_Underlay);
        struct RSCacheDat2Disk_Archive* overlay_archive =
            dat2io_config_group_decode(ctx, RSCacheDat2A_ConfigKind_Overlay);
        struct RSCacheDat2Disk_Archive* sequence_archive =
            dat2io_config_group_decode(ctx, RSCacheDat2A_ConfigKind_Sequence);
        struct RSCacheDat2Disk_Archive* locs_archive =
            dat2io_config_group_decode(ctx, RSCacheDat2A_ConfigKind_Locs);

        if( underlay_archive && cache_disk )
            dat2_buildcache_underlays_init_from_archive(dat2_bc, cache_disk, underlay_archive);
        if( overlay_archive && cache_disk )
            dat2_buildcache_overlays_init_from_archive(dat2_bc, cache_disk, overlay_archive);
        if( sequence_archive && cache_disk )
            dat2_buildcache_sequences_init_from_archive(dat2_bc, cache_disk, sequence_archive);
        if( locs_archive && cache_disk )
            dat2_buildcache_scenery_configs_init_from_archive(dat2_bc, cache_disk, locs_archive);

        if( underlay_archive )
            RSCacheDat2Disk_ArchiveFree(underlay_archive);
        if( overlay_archive )
            RSCacheDat2Disk_ArchiveFree(overlay_archive);
        if( sequence_archive )
            RSCacheDat2Disk_ArchiveFree(sequence_archive);
        if( locs_archive )
            RSCacheDat2Disk_ArchiveFree(locs_archive);
    }

    ToriAuxLibC_SubmitAllUnderlaysFromDat2(task->c);
    ToriAuxLibC_SubmitAllFlotypesFromDat2(task->c);
    ToriAuxLibC_SubmitAllSequencesFromDat2(task->c);
    ToriAuxLibC_SubmitAllLocationsFromDat2(task->c);
    LibToriRS_IOQueueClear(ctx->io);

    task->model_count = dat2_buildcache_get_all_unique_scenery_model_ids(dat2_bc, &task->model_ids);
    for( task->model_index = 0; task->model_index < task->model_count; )
    {
        task->pending_fetches = 0;
        int batch_end = task->model_index + TASK_DAT2_WORLD_IO_BATCH;
        if( batch_end > task->model_count )
            batch_end = task->model_count;

        for( ; task->model_index < batch_end; task->model_index++ )
        {
            int model_id = task->model_ids[task->model_index];
            if( !dat2_buildcache_model_get(dat2_bc, model_id) )
            {
                dat2io_model_fetch(ctx, model_id);
                task->pending_fetches++;
            }
        }

        if( task->pending_fetches == 0 )
            continue;

        PT_YIELD(&task->thread);

        for( task->pending_decodes = 0; task->pending_decodes < task->pending_fetches;
             task->pending_decodes++ )
        {
            struct RSCacheDat2A_Model* model = NULL;
            int decoded_model_id = -1;
            model = dat2io_model_decode(ctx, &decoded_model_id);
            if( model && decoded_model_id >= 0 )
            {
                dat2_buildcache_model_add(dat2_bc, decoded_model_id, model);
                ToriAuxLibC_SubmitModelFromDat2(task->c, decoded_model_id);
            }
        }
        LibToriRS_IOQueueClear(ctx->io);
    }

    PT_END(&task->thread);
}

#endif
