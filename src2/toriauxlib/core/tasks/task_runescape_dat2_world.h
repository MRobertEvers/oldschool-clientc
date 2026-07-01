#ifndef TORIAUXLIB_TASK_RUNESCAPE_DAT2_WORLD_H
#define TORIAUXLIB_TASK_RUNESCAPE_DAT2_WORLD_H

#include "../../../ioqueue/libtorirs_ioqueue.h"
#include "../../../libtorirs.h"
#include "3rd/minipt.h"
#include "buildcache/dat2_buildcache.h"
#include "osrs/rscache/dat2a/dat2a_config_locs.h"
#include "osrs/rscache/dat2a/dat2a_config_sequence.h"
#include "osrs/rscache/dat2a/dat2a_configs.h"
#include "core/tapi/tapi_dat2.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/c/toriauxlibcache_submit.h"
#include "toriauxlib/core/tasks/core_task_await.h"
#include "toriauxlib/core/tasks/task_dat2_anim_io.h"
#include "toriauxlib/core/tasks/task_dat2_io.h"
#include "toriauxlib/core/tasks/task_runescape_dat2_anim_load.h"
#include "toridraw/toridraw_map.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define TASK_DAT2_WORLD_SCENE_WIDTH 104
#define TASK_DAT2_WORLD_MAX_CHUNKS 64
#define TASK_DAT2_WORLD_MAX_MODELS 8192
#define TASK_DAT2_WORLD_IO_BATCH 64

struct Task_Dat2WorldRebuildCore
{
    struct pt thread;
    struct ToriAuxLibCache* c;

    int chunk_count;
    int chunks_x[TASK_DAT2_WORLD_MAX_CHUNKS];
    int chunks_z[TASK_DAT2_WORLD_MAX_CHUNKS];
    int chunk_index;

    struct LibToriRS_IOBatch io_batch;

    int* model_ids;
    int model_count;
    int model_index;
    struct Task_Dat2AnimResolve anim_resolve;
    bool anim_resolve_ready;
    struct RSCacheDat2Disk_Archive* sequence_archive;
};

static void
Task_Dat2WorldRebuildCore_Init(
    struct Task_Dat2WorldRebuildCore* core,
    struct ToriAuxLibCache* c)
{
    PT_INIT(&core->thread);
    core->c = c;
}

static void
Task_Dat2WorldRebuildCore_SetChunks(
    struct Task_Dat2WorldRebuildCore* core,
    const int* xs,
    const int* zs,
    int count)
{
    assert(count <= TASK_DAT2_WORLD_MAX_CHUNKS);
    core->chunk_count = count;
    for( int i = 0; i < count; i++ )
    {
        core->chunks_x[i] = xs[i];
        core->chunks_z[i] = zs[i];
    }
}

static void
Task_Dat2WorldRebuildCore_Cleanup(struct Task_Dat2WorldRebuildCore* core)
{
    Task_Dat2AnimResolve_Destroy(&core->anim_resolve);
    if( core->sequence_archive )
    {
        RSCacheDat2Disk_ArchiveFree(core->sequence_archive);
        core->sequence_archive = NULL;
    }
    free(core->model_ids);
    core->model_ids = NULL;
}

static void
task_dat2_world_compute_centerzone_chunks(
    struct Task_Dat2WorldRebuildCore* core,
    int zonex,
    int zonez)
{
    int zone_padding = TASK_DAT2_WORLD_SCENE_WIDTH / (2 * 8);
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
            assert(count < TASK_DAT2_WORLD_MAX_CHUNKS);
            core->chunks_x[count] = x;
            core->chunks_z[count] = z;
            count++;
        }
    }
    core->chunk_count = count;
}

int
Task_Dat2WorldRebuildCore_Run(
    struct Task_Dat2WorldRebuildCore* core,
    struct LibToriRS_IOContext* ctx)
{
    struct RSCacheDat2Disk_Archive* underlay_archive = NULL;
    struct RSCacheDat2Disk_Archive* overlay_archive = NULL;
    struct RSCacheDat2Disk_Archive* locs_archive = NULL;
    struct Dat2BuildCache* dat2_bc = dat2(core->c);

    PT_BEGIN(&core->thread);

    LibToriRS_IOQueueClear(ctx->io);

    LibToriRS_IOBatchReset(&core->io_batch);
    for( core->chunk_index = 0; core->chunk_index < core->chunk_count; core->chunk_index++ )
    {
        int mapx = core->chunks_x[core->chunk_index];
        int mapz = core->chunks_z[core->chunk_index];
        int map_id = (mapx << 16) | (mapz & 0xFFFF);
        if( !dat2_buildcache_map_terrain_has(dat2_bc, map_id) )
        {
            int slot = LibToriRS_IOBatchAdd(&core->io_batch, 0);
            IO_REQUEST(ctx, slot, TAPIDat2_FetchMapChunkTerrain(ctx, mapx, mapz));
        }
    }
    PT_YIELD(&core->thread);

    for( int i = 0; i < LibToriRS_IOBatchCount(&core->io_batch); i++ )
    {
        struct RSCacheDat2A_MapTerrain* terrain = NULL;
        int map_id = TAPIDat2_DecodeMapChunkTerrain(ctx, i, &terrain);
        if( map_id >= 0 && terrain )
        {
            dat2_buildcache_map_terrain_add(dat2_bc, map_id, terrain);
            ToriAuxLibCache_SubmitMapTerrainFromDat2(core->c, map_id);
        }
    }
    dat2_buildcache_map_terrain_cleanup(dat2_bc);

    LibToriRS_IOBatchReset(&core->io_batch);
    for( core->chunk_index = 0; core->chunk_index < core->chunk_count; core->chunk_index++ )
    {
        int mapx = core->chunks_x[core->chunk_index];
        int mapz = core->chunks_z[core->chunk_index];
        int map_id = (mapx << 16) | (mapz & 0xFFFF);
        if( !dat2_buildcache_map_scenery_has(dat2_bc, map_id) )
        {
            int slot = LibToriRS_IOBatchAdd(&core->io_batch, 0);
            IO_REQUEST(ctx, slot, TAPIDat2_FetchMapChunkScenery(ctx, mapx, mapz));
        }
    }
    PT_YIELD(&core->thread);

    for( int i = 0; i < LibToriRS_IOBatchCount(&core->io_batch); i++ )
    {
        struct RSCacheDat2A_MapLocs* locs = NULL;
        int map_id = TAPIDat2_DecodeMapChunkScenery(ctx, i, &locs);
        if( map_id >= 0 && locs )
        {
            dat2_buildcache_map_scenery_add(dat2_bc, map_id, locs);
            ToriAuxLibCache_SubmitMapSceneryFromDat2(core->c, map_id);
        }
    }

    IO_REQUEST(ctx, 0, TAPIDat2_FetchConfigGroup(ctx, RSCacheDat2A_ConfigKind_Underlay));
    IO_REQUEST(ctx, 1, TAPIDat2_FetchConfigGroup(ctx, RSCacheDat2A_ConfigKind_Overlay));
    IO_REQUEST(ctx, 2, TAPIDat2_FetchConfigGroup(ctx, RSCacheDat2A_ConfigKind_Sequence));
    IO_REQUEST(ctx, 3, TAPIDat2_FetchConfigGroup(ctx, RSCacheDat2A_ConfigKind_Locs));
    PT_YIELD(&core->thread);

    underlay_archive = TAPIDat2_DecodeConfigGroup(ctx, 0, RSCacheDat2A_ConfigKind_Underlay);
    assert(underlay_archive);
    overlay_archive = TAPIDat2_DecodeConfigGroup(ctx, 1, RSCacheDat2A_ConfigKind_Overlay);
    assert(overlay_archive);
    core->sequence_archive = TAPIDat2_DecodeConfigGroup(ctx, 2, RSCacheDat2A_ConfigKind_Sequence);
    assert(core->sequence_archive);
    locs_archive = TAPIDat2_DecodeConfigGroup(ctx, 3, RSCacheDat2A_ConfigKind_Locs);
    assert(locs_archive);

    DAT2_ENSURE_CONFIGS_REFERENCE_TABLE(ctx, &core->thread, core->c);

    dat2_buildcache_underlays_init_from_archive(dat2_bc, underlay_archive);
    dat2_buildcache_overlays_init_from_archive(dat2_bc, overlay_archive);
    dat2_buildcache_scenery_configs_init_from_archive(
        dat2_bc, locs_archive, ToriAuxLibCache_VarPVarBit(core->c));

    RSCacheDat2Disk_ArchiveFree(underlay_archive);
    RSCacheDat2Disk_ArchiveFree(overlay_archive);
    RSCacheDat2Disk_ArchiveFree(locs_archive);

    dat2_buildcache_map_scenery_cleanup(dat2_bc);

    ToriAuxLibCache_SubmitAllUnderlaysFromDat2(core->c);
    ToriAuxLibCache_SubmitAllFlotypesFromDat2(core->c);
    ToriAuxLibCache_SubmitAllLocationsFromDat2(core->c);
    dat2_buildcache_underlays_cleanup(dat2_bc);
    dat2_buildcache_flotypes_cleanup(dat2_bc);

    /* Load classic frame/framemap archives and skeletal anims referenced by
     * visible scene locs only — do NOT walk every sequence in the cache. */
    if( !core->anim_resolve_ready )
    {
        int* seq_ids = NULL;
        int seq_capacity = 256;
        int seq_count = 0;
        seq_ids = malloc((size_t)seq_capacity * sizeof(int));

        if( seq_ids )
        {
            /* --- Phase 1: collect deduped seq ids from visible loc configs --- */
            struct ToriDraw_MapIter* loc_iter = ToriDraw_MapIterNew(dat2_bc->config_loc_hmap);
            void* loc_entry_v = NULL;
            while( (loc_entry_v = ToriDraw_MapIterNext(loc_iter)) )
            {
                struct _LocEntry
                {
                    int id;
                    struct RSCacheDat2A_ConfigLocation* config_loc;
                };
                struct _LocEntry* le = loc_entry_v;
                if( !le->config_loc )
                    continue;

                /* Inline helper: add sid to seq_ids if not already present */
#define COLLECT_SEQ(sid)                                                                           \
    do                                                                                             \
    {                                                                                              \
        int _s = (sid);                                                                            \
        if( _s < 0 )                                                                               \
            break;                                                                                 \
        bool _found = false;                                                                       \
        for( int _k = 0; _k < seq_count; _k++ )                                                    \
            if( seq_ids[_k] == _s )                                                                \
            {                                                                                      \
                _found = true;                                                                     \
                break;                                                                             \
            }                                                                                      \
        if( !_found )                                                                              \
        {                                                                                          \
            if( seq_count >= seq_capacity )                                                        \
            {                                                                                      \
                seq_capacity *= 2;                                                                 \
                int* _g = realloc(seq_ids, (size_t)seq_capacity * sizeof(int));                    \
                if( _g )                                                                           \
                    seq_ids = _g;                                                                  \
                else                                                                               \
                    break;                                                                         \
            }                                                                                      \
            seq_ids[seq_count++] = _s;                                                             \
        }                                                                                          \
    } while( 0 )

                COLLECT_SEQ(le->config_loc->seq_id);
                for( int ri = 0; ri < le->config_loc->random_seq_id_count; ri++ )
                    COLLECT_SEQ(le->config_loc->random_seq_ids[ri]);

#undef COLLECT_SEQ
            }
            ToriDraw_MapIterFree(loc_iter);

            struct Dat2AnimArchiveSet aset;

            Task_Dat2AnimResolve_Init(&core->anim_resolve, core->c, dat2_bc);
            dat2_anim_archive_set_init(&aset, 128);
            if( aset.ids )
            {
                for( int si = 0; si < seq_count; si++ )
                {
                    int seq_id = seq_ids[si];
                    if( core->sequence_archive )
                        dat2_buildcache_sequence_load_from_archive(
                            dat2_bc, core->sequence_archive, seq_id);

                    struct RSCacheDat2A_ConfigSequence* seq =
                        dat2_buildcache_sequence_get(dat2_bc, seq_id);
                    if( !seq )
                        continue;

                    dat2_anim_set_add_sequence_archives(&aset, seq);
                    Task_Dat2AnimResolve_AddSequenceId(&core->anim_resolve, seq_id);
                }

                Task_Dat2AnimResolve_SetArchiveSet(&core->anim_resolve, &aset);
                dat2_anim_archive_set_free(&aset);
                core->anim_resolve_ready = true;
            }
            free(seq_ids);
        }
    }

    if( core->anim_resolve_ready )
    {
        TASK_AWAIT(&core->thread, Task_Dat2AnimResolve_Run(&core->anim_resolve, ctx));
        Task_Dat2AnimResolve_Destroy(&core->anim_resolve);
        core->anim_resolve_ready = false;
        dat2_anim_submit_all_skeletal(core->c, dat2_bc);
    }

    ToriAuxLibCache_SubmitAllSequencesFromDat2(core->c);
    dat2_buildcache_sequences_cleanup(dat2_bc);

    if( core->sequence_archive )
    {
        RSCacheDat2Disk_ArchiveFree(core->sequence_archive);
        core->sequence_archive = NULL;
    }

    LibToriRS_IOQueueClear(ctx->io);

    core->model_count = dat2_buildcache_get_all_unique_scenery_model_ids(dat2_bc, &core->model_ids);
    dat2_buildcache_scenery_configs_cleanup(dat2_bc);
    for( core->model_index = 0; core->model_index < core->model_count; )
    {
        LibToriRS_IOBatchReset(&core->io_batch);
        int batch_end = core->model_index + TASK_DAT2_WORLD_IO_BATCH;
        if( batch_end > core->model_count )
            batch_end = core->model_count;

        for( ; core->model_index < batch_end; core->model_index++ )
        {
            int model_id = core->model_ids[core->model_index];
            if( !dat2_buildcache_model_get(dat2_bc, model_id) )
            {
                int slot = LibToriRS_IOBatchAdd(&core->io_batch, model_id);
                IO_REQUEST(ctx, slot, TAPIDat2_FetchModel(ctx, model_id));
            }
        }

        if( LibToriRS_IOBatchEmpty(&core->io_batch) )
            continue;

        PT_YIELD(&core->thread);

        for( int i = 0; i < LibToriRS_IOBatchCount(&core->io_batch); i++ )
        {
            struct RSCacheDat2A_Model* model = TAPIDat2_DecodeModel(ctx, i);
            if( model )
            {
                int model_id = LibToriRS_IOBatchUser(&core->io_batch, i);
                dat2_buildcache_model_add(dat2_bc, model_id, model);
                ToriAuxLibCache_SubmitModelFromDat2(core->c, model_id);
                dat2_buildcache_model_remove(dat2_bc, model_id);
            }
        }
        LibToriRS_IOQueueClear(ctx->io);
    }

    ToriAuxLibCache_PruneBuildCaches(core->c);

    PT_END(&core->thread);
}

struct Task_Dat2WorldRebuildNormalCenterzone
{
    struct Task_Dat2WorldRebuildCore core;
    int zonex;
    int zonez;
};

struct Task_Dat2WorldRebuildNormalCenterzone*
Task_Dat2WorldRebuildNormalCenterzone_New(
    struct ToriAuxLibCache* c,
    int zonex,
    int zonez)
{
    struct Task_Dat2WorldRebuildNormalCenterzone* task =
        calloc(1, sizeof(struct Task_Dat2WorldRebuildNormalCenterzone));
    assert(task);
    Task_Dat2WorldRebuildCore_Init(&task->core, c);
    task->zonex = zonex;
    task->zonez = zonez;
    task_dat2_world_compute_centerzone_chunks(&task->core, zonex, zonez);
    return task;
}

void
Task_Dat2WorldRebuildNormalCenterzone_Free(struct Task_Dat2WorldRebuildNormalCenterzone* task)
{
    if( !task )
        return;
    Task_Dat2WorldRebuildCore_Cleanup(&task->core);
    free(task);
}

int
Task_Dat2WorldRebuildNormalCenterzone_Run(
    struct Task_Dat2WorldRebuildNormalCenterzone* task,
    struct LibToriRS_IOContext* ctx)
{
    return Task_Dat2WorldRebuildCore_Run(&task->core, ctx);
}

struct Task_Dat2WorldRebuildChunkList
{
    struct Task_Dat2WorldRebuildCore core;
};

struct Task_Dat2WorldRebuildChunkList*
Task_Dat2WorldRebuildChunkList_New(
    struct ToriAuxLibCache* c,
    const int* chunks_x,
    const int* chunks_z,
    int count)
{
    struct Task_Dat2WorldRebuildChunkList* task =
        calloc(1, sizeof(struct Task_Dat2WorldRebuildChunkList));
    assert(task);
    Task_Dat2WorldRebuildCore_Init(&task->core, c);
    Task_Dat2WorldRebuildCore_SetChunks(&task->core, chunks_x, chunks_z, count);
    return task;
}

void
Task_Dat2WorldRebuildChunkList_Free(struct Task_Dat2WorldRebuildChunkList* task)
{
    if( !task )
        return;
    Task_Dat2WorldRebuildCore_Cleanup(&task->core);
    free(task);
}

int
Task_Dat2WorldRebuildChunkList_Run(
    struct Task_Dat2WorldRebuildChunkList* task,
    struct LibToriRS_IOContext* ctx)
{
    return Task_Dat2WorldRebuildCore_Run(&task->core, ctx);
}

#endif
