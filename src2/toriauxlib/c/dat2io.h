#ifndef CORE_DAT2_DAT2IO_H
#define CORE_DAT2_DAT2IO_H

#include "../../ioqueue/libtorirs_ioqueue.h"
#include "../../libtorirs.h"
#include "osrs/rscache/dat2a/dat2a_maps.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "platforms/platform_x/cachelib_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline void
TAPIDat2_FetchModel(
    struct LibToriRS_IOContext* ctx,
    int model_id)
{
    struct RSCacheDat2DiskLib_IORequest request;
    cachelib_dat2_model_fetch(model_id, &request);
    LibToriRS_IOQueuePushCache(ctx->io, request.table_id, request.archive_id, request.flags);
}

static inline struct RSCacheDat2A_Model*
TAPIDat2_DecodeModel(
    struct LibToriRS_IOContext* ctx,
    int slot_id)
{
    struct RSCacheDat2Disk_Archive* archive = NULL;
    struct RSCacheDat2A_Model* model = NULL;
    struct LibToriRS_IOQueueItem* item = LibToriRS_IOQueueFindBySlot(ctx->io, slot_id);

    if( !item )
        return NULL;
    if( item->kind != TORIRSIO_KIND_CACHE )
        return NULL;
    if( item->status != TORIRSIO_STAT_DONE || item->error_code != 0 )
        return NULL;
    if( item->u.cache.table_id != RSCacheDat2Disk_Table_Models )
        return NULL;

    archive = item->data;
    if( !archive )
        return NULL;

    model = RSCacheDat2A_ModelNewDecode((const unsigned char*)archive->data, archive->data_size);
    RSCacheDat2Disk_ArchiveFree(archive);
    item->data = NULL;
    return model;
}

static inline void
TAPIDat2_FetchMapChunkTerrain(
    struct LibToriRS_IOContext* ctx,
    int mapx,
    int mapz)
{
    struct RSCacheDat2DiskLib_IORequest request;
    cachelib_dat2_map_chunk_terrain_fetch(mapx, mapz, &request);
    LibToriRS_IOQueuePushCache(ctx->io, request.table_id, request.archive_id, request.flags);
}

static inline int
TAPIDat2_DecodeMapChunkTerrain(
    struct LibToriRS_IOContext* ctx,
    int slot_id,
    struct RSCacheDat2A_MapTerrain** terrain_out)
{
    struct LibToriRS_IOQueueItem* item = LibToriRS_IOQueueFindBySlot(ctx->io, slot_id);
    *terrain_out = NULL;
    if( !item )
        return -1;
    if( item->kind != TORIRSIO_KIND_CACHE || item->status != TORIRSIO_STAT_DONE ||
        item->error_code != 0 )
        return -1;
    if( item->u.cache.table_id != RSCacheDat2Disk_Table_Maps ||
        item->u.cache.flags != CACHELIB_MAPCHUNK_TERRAIN )
        return -1;

    struct RSCacheDat2Disk_Archive* archive = item->data;
    if( !archive )
        return -1;

    int map_id = item->u.cache.archive_id;
    int map_x = map_id >> 16;
    int map_z = map_id & 0xFFFF;
    *terrain_out = map_terrain_new_from_decode(archive->data, archive->data_size, map_x, map_z);
    RSCacheDat2Disk_ArchiveFree(archive);
    item->data = NULL;
    return map_id;
}

static inline void
TAPIDat2_FetchMapChunkScenery(
    struct LibToriRS_IOContext* ctx,
    int mapx,
    int mapz)
{
    struct RSCacheDat2DiskLib_IORequest request;
    cachelib_dat2_map_chunk_scenery_fetch(mapx, mapz, &request);
    LibToriRS_IOQueuePushCache(ctx->io, request.table_id, request.archive_id, request.flags);
}

static inline int
TAPIDat2_DecodeMapChunkScenery(
    struct LibToriRS_IOContext* ctx,
    int slot_id,
    struct RSCacheDat2A_MapLocs** locs_out)
{
    struct LibToriRS_IOQueueItem* item = LibToriRS_IOQueueFindBySlot(ctx->io, slot_id);
    *locs_out = NULL;
    if( !item )
        return -1;
    if( item->kind != TORIRSIO_KIND_CACHE || item->status != TORIRSIO_STAT_DONE ||
        item->error_code != 0 )
        return -1;
    if( item->u.cache.table_id != RSCacheDat2Disk_Table_Maps ||
        item->u.cache.flags != CACHELIB_MAPCHUNK_SCENERY )
        return -1;

    struct RSCacheDat2Disk_Archive* archive = item->data;
    if( !archive )
        return -1;

    int map_id = item->u.cache.archive_id;
    int map_x = map_id >> 16;
    int map_z = map_id & 0xFFFF;
    *locs_out = map_locs_new_from_decode(archive->data, archive->data_size);
    if( *locs_out )
    {
        (*locs_out)->_chunk_mapx = map_x;
        (*locs_out)->_chunk_mapz = map_z;
    }
    RSCacheDat2Disk_ArchiveFree(archive);
    item->data = NULL;
    return map_id;
}

static inline void
TAPIDat2_FetchConfigGroup(
    struct LibToriRS_IOContext* ctx,
    int config_kind)
{
    struct RSCacheDat2DiskLib_IORequest request;
    cachelib_dat2_config_group_fetch(config_kind, &request);
    LibToriRS_IOQueuePushCache(ctx->io, request.table_id, request.archive_id, request.flags);
}

static inline struct RSCacheDat2Disk_Archive*
TAPIDat2_DecodeConfigGroup(
    struct LibToriRS_IOContext* ctx,
    int slot_id,
    int expected_config_kind)
{
    struct LibToriRS_IOQueueItem* item = LibToriRS_IOQueueFindBySlot(ctx->io, slot_id);
    if( !item )
        return NULL;
    if( item->kind != TORIRSIO_KIND_CACHE || item->status != TORIRSIO_STAT_DONE ||
        item->error_code != 0 )
        return NULL;
    if( item->u.cache.table_id != RSCacheDat2Disk_Table_Configs ||
        item->u.cache.archive_id != expected_config_kind )
        return NULL;

    return item->data;
}

#endif
