#include "ioqueue/libtorirs_io.h"
#include "buildcache/dat2_buildcache.h"
#include "core/tapi/tapi_dat2.h"
#include "osrs/rscache/dat2a/dat2a_config_locs.h"
#include "osrs/rscache/dat2a/dat2a_config_sequence.h"
#include "osrs/rscache/dat2a/dat2a_configs.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/cache/toriauxlibcache_submit.h"
#include "toriauxlib/core/tasks/dat2/dat2_anim_load.h"
#include "toriauxlib/core/tasks/dat2/task_dat2_io.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define TASK_DAT2_WORLD_MAX_CHUNKS 64
#define TASK_DAT2_WORLD_IO_BATCH 64
#define TASK_DAT2_ANIM_RESOLVE_MAX_SEQS 16

struct LibToriRS_Task*
Task_Dat2AnimResolve_New(
    struct ToriAuxLibCache* c,
    struct Dat2BuildCache* bc,
    const int* seq_ids,
    int seq_count);

struct Task_Dat2WorldRebuildCore
{
    struct LibToriRS_Task base;
    struct pt pt;
    struct ToriAuxLibCache* c;

    int chunk_count;
    int chunks_x[TASK_DAT2_WORLD_MAX_CHUNKS];
    int chunks_z[TASK_DAT2_WORLD_MAX_CHUNKS];
    int chunk_index;

    struct LibToriRS_IOBatch io_batch;

    int* model_ids;
    int model_count;
    int model_index;

    int* seq_ids;
    int seq_count;
    int seq_batch_start;

    struct RSCacheDat2Disk_Archive* sequence_archive;
};

struct Task_Dat2WorldCollectSeqCtx
{
    int* ids;
    int count;
    int capacity;
};

static bool
task_dat2_world_collect_seq(
    struct Task_Dat2WorldCollectSeqCtx* ctx,
    int sid)
{
    if( sid < 0 )
        return true;

    for( int k = 0; k < ctx->count; k++ )
        if( ctx->ids[k] == sid )
            return true;

    if( ctx->count >= ctx->capacity )
    {
        ctx->capacity *= 2;
        int* grown = realloc(ctx->ids, (size_t)ctx->capacity * sizeof(int));
        if( !grown )
            return false;
        ctx->ids = grown;
    }

    ctx->ids[ctx->count++] = sid;
    return true;
}

static void
task_dat2_world_collect_config_loc_seqs_cb(
    int loc_id,
    struct RSCacheDat2A_ConfigLocation* config_loc,
    void* user_data)
{
    (void)loc_id;
    struct Task_Dat2WorldCollectSeqCtx* ctx = user_data;

    if( !task_dat2_world_collect_seq(ctx, config_loc->seq_id) )
        return;

    for( int ri = 0; ri < config_loc->random_seq_id_count; ri++ )
    {
        if( !task_dat2_world_collect_seq(ctx, config_loc->random_seq_ids[ri]) )
            return;
    }
}

static int
Task_Dat2WorldRebuildCore_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_Dat2WorldRebuildCore* task =
        LibToriRS_container_of(base, struct Task_Dat2WorldRebuildCore, base);
    struct RSCacheDat2Disk_Archive* underlay_archive = NULL;
    struct RSCacheDat2Disk_Archive* overlay_archive = NULL;
    struct RSCacheDat2Disk_Archive* locs_archive = NULL;
    struct Dat2BuildCache* dat2_bc = dat2(task->c);

    PT_BEGIN(&task->pt);

    LibToriRS_IOQueueClear(ctx->io);

    LibToriRS_IOBatchReset(&task->io_batch);
    for( task->chunk_index = 0; task->chunk_index < task->chunk_count; task->chunk_index++ )
    {
        int mapx = task->chunks_x[task->chunk_index];
        int mapz = task->chunks_z[task->chunk_index];
        int map_id = (mapx << 16) | (mapz & 0xFFFF);
        if( !dat2_buildcache_map_terrain_has(dat2_bc, map_id) )
        {
            int slot = LibToriRS_IOBatchAdd(&task->io_batch, 0);
            IO_REQUEST(ctx, slot, TAPIDat2_FetchMapChunkTerrain(ctx, mapx, mapz));
        }
    }
    PT_YIELD(&task->pt);

    for( int i = 0; i < LibToriRS_IOBatchCount(&task->io_batch); i++ )
    {
        struct RSCacheDat2A_MapTerrain* terrain = NULL;
        int map_id = TAPIDat2_DecodeMapChunkTerrain(ctx, i, &terrain);
        if( map_id >= 0 && terrain )
        {
            dat2_buildcache_map_terrain_add(dat2_bc, map_id, terrain);
            ToriAuxLibCache_SubmitMapTerrainFromDat2(task->c, map_id);
        }
    }
    dat2_buildcache_map_terrain_cleanup(dat2_bc);

    LibToriRS_IOBatchReset(&task->io_batch);
    for( task->chunk_index = 0; task->chunk_index < task->chunk_count; task->chunk_index++ )
    {
        int mapx = task->chunks_x[task->chunk_index];
        int mapz = task->chunks_z[task->chunk_index];
        int map_id = (mapx << 16) | (mapz & 0xFFFF);
        if( !dat2_buildcache_map_scenery_has(dat2_bc, map_id) )
        {
            int slot = LibToriRS_IOBatchAdd(&task->io_batch, 0);
            IO_REQUEST(ctx, slot, TAPIDat2_FetchMapChunkScenery(ctx, mapx, mapz));
        }
    }
    PT_YIELD(&task->pt);

    for( int i = 0; i < LibToriRS_IOBatchCount(&task->io_batch); i++ )
    {
        struct RSCacheDat2A_MapLocs* locs = NULL;
        int map_id = TAPIDat2_DecodeMapChunkScenery(ctx, i, &locs);
        if( map_id >= 0 && locs )
        {
            dat2_buildcache_map_scenery_add(dat2_bc, map_id, locs);
            ToriAuxLibCache_SubmitMapSceneryFromDat2(task->c, map_id);
        }
    }

    DAT2_ENSURE_CONFIGS_REFERENCE_TABLE(ctx, &task->pt, task->c);

    IO_REQUEST(ctx, 0, TAPIDat2_FetchConfigGroup(ctx, RSCacheDat2A_ConfigKind_Underlay));
    IO_REQUEST(ctx, 1, TAPIDat2_FetchConfigGroup(ctx, RSCacheDat2A_ConfigKind_Overlay));
    IO_REQUEST(ctx, 2, TAPIDat2_FetchConfigGroup(ctx, RSCacheDat2A_ConfigKind_Sequence));
    IO_REQUEST(ctx, 3, TAPIDat2_FetchConfigGroup(ctx, RSCacheDat2A_ConfigKind_Locs));
    PT_YIELD(&task->pt);

    underlay_archive = TAPIDat2_DecodeConfigGroup(ctx, 0, RSCacheDat2A_ConfigKind_Underlay);
    assert(underlay_archive);
    overlay_archive = TAPIDat2_DecodeConfigGroup(ctx, 1, RSCacheDat2A_ConfigKind_Overlay);
    assert(overlay_archive);
    task->sequence_archive = TAPIDat2_DecodeConfigGroup(ctx, 2, RSCacheDat2A_ConfigKind_Sequence);
    assert(task->sequence_archive);
    locs_archive = TAPIDat2_DecodeConfigGroup(ctx, 3, RSCacheDat2A_ConfigKind_Locs);
    assert(locs_archive);

    dat2_buildcache_underlays_init_from_archive(dat2_bc, underlay_archive);
    dat2_buildcache_overlays_init_from_archive(dat2_bc, overlay_archive);
    dat2_buildcache_scenery_configs_init_from_archive(
        dat2_bc, locs_archive, ToriAuxLibCache_VarPVarBit(task->c));

    RSCacheDat2Disk_ArchiveFree(underlay_archive);
    RSCacheDat2Disk_ArchiveFree(overlay_archive);
    RSCacheDat2Disk_ArchiveFree(locs_archive);

    dat2_buildcache_map_scenery_cleanup(dat2_bc);

    ToriAuxLibCache_SubmitAllUnderlaysFromDat2(task->c);
    ToriAuxLibCache_SubmitAllFlotypesFromDat2(task->c);
    ToriAuxLibCache_SubmitAllLocationsFromDat2(task->c);
    dat2_buildcache_underlays_cleanup(dat2_bc);
    dat2_buildcache_flotypes_cleanup(dat2_bc);

    task->seq_ids = NULL;
    task->seq_count = 0;
    task->seq_batch_start = 0;

    {
        int seq_capacity = 256;
        task->seq_ids = malloc((size_t)seq_capacity * sizeof(int));

        if( task->seq_ids )
        {
            struct Task_Dat2WorldCollectSeqCtx collect_ctx = {
                .ids = task->seq_ids,
                .count = 0,
                .capacity = seq_capacity,
            };
            dat2_buildcache_foreach_config_loc(
                dat2_bc, task_dat2_world_collect_config_loc_seqs_cb, &collect_ctx);
            task->seq_ids = collect_ctx.ids;
            task->seq_count = collect_ctx.count;

            for( int si = 0; si < task->seq_count; si++ )
            {
                int seq_id = task->seq_ids[si];
                if( task->sequence_archive )
                    dat2_buildcache_sequence_load_from_archive(
                        dat2_bc, task->sequence_archive, seq_id);
            }
        }
    }

    if( task->seq_ids && task->seq_count > 0 )
    {
        for( task->seq_batch_start = 0; task->seq_batch_start < task->seq_count; )
        {
            int batch_count = task->seq_count - task->seq_batch_start;
            if( batch_count > TASK_DAT2_ANIM_RESOLVE_MAX_SEQS )
                batch_count = TASK_DAT2_ANIM_RESOLVE_MAX_SEQS;

            TASK_AWAITEX(
                &task->pt,
                ctx,
                Task_Dat2AnimResolve_New(
                    task->c,
                    dat2_bc,
                    &task->seq_ids[task->seq_batch_start],
                    batch_count));
            task->seq_batch_start += batch_count;
        }
        dat2_anim_submit_all_skeletal(task->c, dat2_bc);
    }

    if( task->seq_ids )
    {
        free(task->seq_ids);
        task->seq_ids = NULL;
        task->seq_count = 0;
    }

    ToriAuxLibCache_SubmitAllSequencesFromDat2(task->c);
    dat2_buildcache_sequences_cleanup(dat2_bc);

    if( task->sequence_archive )
    {
        RSCacheDat2Disk_ArchiveFree(task->sequence_archive);
        task->sequence_archive = NULL;
    }

    LibToriRS_IOQueueClear(ctx->io);

    task->model_count = dat2_buildcache_get_all_unique_scenery_model_ids(dat2_bc, &task->model_ids);
    dat2_buildcache_scenery_configs_cleanup(dat2_bc);
    for( task->model_index = 0; task->model_index < task->model_count; )
    {
        LibToriRS_IOBatchReset(&task->io_batch);
        int batch_end = task->model_index + TASK_DAT2_WORLD_IO_BATCH;
        if( batch_end > task->model_count )
            batch_end = task->model_count;

        for( ; task->model_index < batch_end; task->model_index++ )
        {
            int model_id = task->model_ids[task->model_index];
            if( !dat2_buildcache_model_get(dat2_bc, model_id) )
            {
                int slot = LibToriRS_IOBatchAdd(&task->io_batch, model_id);
                IO_REQUEST(ctx, slot, TAPIDat2_FetchModel(ctx, model_id));
            }
        }

        if( LibToriRS_IOBatchEmpty(&task->io_batch) )
            continue;

        PT_YIELD(&task->pt);

        for( int i = 0; i < LibToriRS_IOBatchCount(&task->io_batch); i++ )
        {
            struct RSCacheDat2A_Model* model = TAPIDat2_DecodeModel(ctx, i);
            if( model )
            {
                int model_id = LibToriRS_IOBatchUser(&task->io_batch, i);
                dat2_buildcache_model_add(dat2_bc, model_id, model);
                ToriAuxLibCache_SubmitModelFromDat2(task->c, model_id);
                dat2_buildcache_model_remove(dat2_bc, model_id);
            }
        }
        LibToriRS_IOQueueClear(ctx->io);
    }

    ToriAuxLibCache_PruneBuildCaches(task->c);

    PT_END(&task->pt);
}

static void
Task_Dat2WorldRebuildCore_Free(struct LibToriRS_Task* base)
{
    struct Task_Dat2WorldRebuildCore* task =
        LibToriRS_container_of(base, struct Task_Dat2WorldRebuildCore, base);

    if( task->sequence_archive )
    {
        RSCacheDat2Disk_ArchiveFree(task->sequence_archive);
        task->sequence_archive = NULL;
    }
    free(task->model_ids);
    task->model_ids = NULL;
    free(task->seq_ids);
    task->seq_ids = NULL;
    free(task);
}

static struct LibToriRS_TaskVTable g_task_dat2_world_rebuild_core_vtable = {
    .run_fn = Task_Dat2WorldRebuildCore_Run,
    .free_fn = Task_Dat2WorldRebuildCore_Free,
};

struct LibToriRS_Task*
Task_Dat2WorldRebuildCore_New(
    struct ToriAuxLibCache* c,
    const int* chunk_xs,
    const int* chunk_zs,
    int chunk_count)
{
    struct Task_Dat2WorldRebuildCore* task;

    assert(c);
    assert(chunk_xs);
    assert(chunk_zs);
    assert(chunk_count > 0);
    assert(chunk_count <= TASK_DAT2_WORLD_MAX_CHUNKS);

    task = calloc(1, sizeof(struct Task_Dat2WorldRebuildCore));
    if( !task )
        return NULL;

    task->base.vtable = &g_task_dat2_world_rebuild_core_vtable;
    task->c = c;
    task->chunk_count = chunk_count;
    for( int i = 0; i < chunk_count; i++ )
    {
        task->chunks_x[i] = chunk_xs[i];
        task->chunks_z[i] = chunk_zs[i];
    }
    PT_INIT(&task->pt);
    return &task->base;
}
