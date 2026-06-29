#ifndef TORIAUXLIBC_T_RUNESCAPE_DAT2_WORLD_H
#define TORIAUXLIBC_T_RUNESCAPE_DAT2_WORLD_H

#include "../../ioqueue/libtorirs_ioqueue.h"
#include "../../libtorirs.h"
#include "3rd/minipt.h"
#include "buildcache/dat2_buildcache.h"
#include "osrs/rscache/dat2a/dat2a_animaya.h"
#include "osrs/rscache/dat2a/dat2a_config_locs.h"
#include "osrs/rscache/dat2a/dat2a_config_sequence.h"
#include "osrs/rscache/dat2a/dat2a_configs.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "toriauxlib/c/dat2io.h"
#include "toriauxlib/c/toriauxlibc.h"
#include "toriauxlib/c/toriauxlibc_submit.h"
#include "toridraw/toridraw_map.h"

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

    struct LibToriRS_IOBatch io_batch;

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
    struct RSCacheDat2Disk_Archive* underlay_archive;
    struct RSCacheDat2Disk_Archive* overlay_archive;
    struct RSCacheDat2Disk_Archive* sequence_archive;
    struct RSCacheDat2Disk_Archive* locs_archive;
    struct Dat2BuildCache* dat2_bc = dat2(task->c);
    struct RSCacheDat2Disk* cache_disk = ToriAuxLibC_Dat2Disk(task->c);

    PT_BEGIN(&task->thread);

    LibToriRS_IOQueueClear(ctx->io);
    task_dat2_world_compute_chunks(task);

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
    PT_YIELD(&task->thread);

    for( int i = 0; i < LibToriRS_IOBatchCount(&task->io_batch); i++ )
    {
        struct RSCacheDat2A_MapTerrain* terrain = NULL;
        int map_id = TAPIDat2_DecodeMapChunkTerrain(ctx, i, &terrain);
        if( map_id >= 0 && terrain )
        {
            dat2_buildcache_map_terrain_add(dat2_bc, map_id, terrain);
            ToriAuxLibC_SubmitMapTerrainFromDat2(task->c, map_id);
        }
    }

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
    PT_YIELD(&task->thread);

    for( int i = 0; i < LibToriRS_IOBatchCount(&task->io_batch); i++ )
    {
        struct RSCacheDat2A_MapLocs* locs = NULL;
        int map_id = TAPIDat2_DecodeMapChunkScenery(ctx, i, &locs);
        if( map_id >= 0 && locs )
        {
            dat2_buildcache_map_scenery_add(dat2_bc, map_id, locs);
            ToriAuxLibC_SubmitMapSceneryFromDat2(task->c, map_id);
        }
    }

    IO_REQUEST(ctx, 0, TAPIDat2_FetchConfigGroup(ctx, RSCacheDat2A_ConfigKind_Underlay));
    IO_REQUEST(ctx, 1, TAPIDat2_FetchConfigGroup(ctx, RSCacheDat2A_ConfigKind_Overlay));
    IO_REQUEST(ctx, 2, TAPIDat2_FetchConfigGroup(ctx, RSCacheDat2A_ConfigKind_Sequence));
    IO_REQUEST(ctx, 3, TAPIDat2_FetchConfigGroup(ctx, RSCacheDat2A_ConfigKind_Locs));
    PT_YIELD(&task->thread);

    underlay_archive = TAPIDat2_DecodeConfigGroup(ctx, 0, RSCacheDat2A_ConfigKind_Underlay);
    assert(underlay_archive);
    overlay_archive = TAPIDat2_DecodeConfigGroup(ctx, 1, RSCacheDat2A_ConfigKind_Overlay);
    assert(overlay_archive);
    sequence_archive = TAPIDat2_DecodeConfigGroup(ctx, 2, RSCacheDat2A_ConfigKind_Sequence);
    assert(sequence_archive);
    locs_archive = TAPIDat2_DecodeConfigGroup(ctx, 3, RSCacheDat2A_ConfigKind_Locs);
    assert(locs_archive);

    dat2_buildcache_underlays_init_from_archive(dat2_bc, cache_disk, underlay_archive);
    dat2_buildcache_overlays_init_from_archive(dat2_bc, cache_disk, overlay_archive);
    dat2_buildcache_scenery_configs_init_from_archive(
        dat2_bc, cache_disk, locs_archive, ToriAuxLibC_VarPVarBit(task->c));

    RSCacheDat2Disk_ArchiveFree(underlay_archive);
    RSCacheDat2Disk_ArchiveFree(overlay_archive);
    RSCacheDat2Disk_ArchiveFree(locs_archive);

    ToriAuxLibC_SubmitAllUnderlaysFromDat2(task->c);
    ToriAuxLibC_SubmitAllFlotypesFromDat2(task->c);
    ToriAuxLibC_SubmitAllLocationsFromDat2(task->c);

    /* Load classic frame/framemap archives and skeletal anims referenced by
     * visible scene locs only — do NOT walk every sequence in the cache. */
    {
        int* seq_ids = NULL;
        int seq_capacity = 256;
        int seq_count = 0;
        seq_ids = malloc((size_t)seq_capacity * sizeof(int));

        int* archive_ids = NULL;
        int archive_capacity = 128;
        int archive_count = 0;
        archive_ids = malloc((size_t)archive_capacity * sizeof(int));

        if( seq_ids && archive_ids )
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

            /* --- Phase 2: for each wanted seq, collect archive ids + skeletal --- */
            struct _SeqEntry
            {
                int id;
                struct RSCacheDat2A_ConfigSequence* seq;
            };

            for( int si = 0; si < seq_count; si++ )
            {
                int seq_id = seq_ids[si];
                if( sequence_archive && cache_disk )
                    dat2_buildcache_sequence_load_from_archive(
                        dat2_bc, cache_disk, sequence_archive, seq_id);

                struct _SeqEntry* se = (struct _SeqEntry*)ToriDraw_MapSearch(
                    dat2_bc->sequences_hmap, &seq_id, TORIDRAW_MAP_FIND);
                if( !se || !se->seq )
                    continue;

                /* Collect unique idx0 archive ids */
                for( int fi = 0; fi < se->seq->frame_count; fi++ )
                {
                    int aid = (se->seq->frame_ids[fi] >> 16) & 0xFFFF;
                    if( aid < 0 )
                        continue;
                    bool found = false;
                    for( int k = 0; k < archive_count; k++ )
                        if( archive_ids[k] == aid )
                        {
                            found = true;
                            break;
                        }
                    if( !found )
                    {
                        if( archive_count >= archive_capacity )
                        {
                            archive_capacity *= 2;
                            int* grow =
                                realloc(archive_ids, (size_t)archive_capacity * sizeof(int));
                            if( grow )
                                archive_ids = grow;
                            else
                                break;
                        }
                        archive_ids[archive_count++] = aid;
                    }
                }

                /* Load skeletal anim if present */
                int maya_id = se->seq->anim_maya_id;
                if( maya_id >= 0 && !dat2_buildcache_skeletal_has(dat2_bc, maya_id) )
                {
                    struct RSCacheDat2A_AnimMaya* maya =
                        RSCacheDat2A_AnimMayaNewFromCache(cache_disk, maya_id);
                    if( maya )
                        dat2_buildcache_skeletal_add(dat2_bc, maya_id, maya);
                }
            }
            free(seq_ids);
            seq_ids = NULL;

            /* --- Phase 3: load and submit each unique idx0 archive --- */
            for( int ai = 0; ai < archive_count; ai++ )
            {
                int aid = archive_ids[ai];
                if( !dat2_buildcache_frames_has(dat2_bc, aid) )
                    dat2_buildcache_frames_init_from_archive(dat2_bc, cache_disk, aid);
                ToriAuxLibC_SubmitAnimationFromDat2(task->c, aid);
            }
            free(archive_ids);
            archive_ids = NULL;

            /* --- Phase 4: submit all loaded skeletal animations --- */
            {
                struct ToriDraw_MapIter* sk_iter = ToriDraw_MapIterNew(dat2_bc->skeletal_hmap);
                void* sk_entry_v = NULL;
                while( (sk_entry_v = ToriDraw_MapIterNext(sk_iter)) )
                {
                    struct _SkEntry
                    {
                        int id;
                        struct RSCacheDat2A_AnimMaya* maya;
                    };
                    struct _SkEntry* ske = sk_entry_v;
                    ToriAuxLibC_SubmitSkeletalFromDat2(task->c, ske->id);
                }
                ToriDraw_MapIterFree(sk_iter);
            }
        }

        ToriAuxLibC_SubmitAllSequencesFromDat2(task->c);

        if( sequence_archive )
        {
            RSCacheDat2Disk_ArchiveFree(sequence_archive);
            sequence_archive = NULL;
        }
    }

    LibToriRS_IOQueueClear(ctx->io);

    task->model_count = dat2_buildcache_get_all_unique_scenery_model_ids(dat2_bc, &task->model_ids);
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

        PT_YIELD(&task->thread);

        for( int i = 0; i < LibToriRS_IOBatchCount(&task->io_batch); i++ )
        {
            struct RSCacheDat2A_Model* model = TAPIDat2_DecodeModel(ctx, i);
            if( model )
            {
                int model_id = LibToriRS_IOBatchUser(&task->io_batch, i);
                dat2_buildcache_model_add(dat2_bc, model_id, model);
                ToriAuxLibC_SubmitModelFromDat2(task->c, model_id);
            }
        }
        LibToriRS_IOQueueClear(ctx->io);
    }

    PT_END(&task->thread);
}

#endif
