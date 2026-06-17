#ifndef CORE_DAT1_DAT1IO_H
#define CORE_DAT1_DAT1IO_H

#include "../libtorirs.h"
#include "osrs/rscache/cache_dat.h"
#include "osrs/rscache/filelist.h"
#include "osrs/rscache/tables/maps.h"
#include "osrs/rscache/tables/model.h"
#include "osrs/rscache/tables_dat/animframe.h"
#include "platforms/platform_x/cachelib_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline void
dat1io_model_fetch(
    struct LibToriRS_IOContext* ctx,
    int model_id)
{
    struct CacheLib_IORequest request;
    cachelib_dat1_model_fetch(model_id, &request);
    LibToriRS_IOQueuePushCache(ctx->io, request.table_id, request.archive_id, request.flags);
}

static inline struct CacheModel*
dat1io_model_decode(
    struct LibToriRS_IOContext* ctx,
    int* model_id_out)
{
    struct CacheDatArchive* archive = NULL;
    struct CacheModel* model = NULL;
    struct LibToriRS_IOQueueItem item;
    if( model_id_out )
        *model_id_out = -1;
    if( !LibToriRS_IOQueuePopRead(ctx->io, &item) )
        return NULL;
    if( item.kind != TORIRSIO_KIND_CACHE )
        return NULL;
    if( item.status != TORIRSIO_STAT_DONE || item.error_code != 0 )
        return NULL;

    archive = item.data;
    if( !archive )
    {
        fprintf(stderr, "Failed to get cache dat archive\n");
        goto cleanup;
    }

    int model_id = item.u.cache.archive_id;
    if( model_id_out )
        *model_id_out = model_id;

    model = model_new_from_dat_archive(archive, model_id);
    if( !model )
    {
        fprintf(stderr, "Failed to decode model\n");
        goto cleanup;
    }

cleanup:
    cache_dat_archive_free(archive);
    item.data = NULL;
    return model;
}

static inline void
dat1io_map_chunk_terrain_fetch(
    struct LibToriRS_IOContext* ctx,
    int mapx,
    int mapz)
{
    struct CacheLib_IORequest request;
    cachelib_dat1_map_chunk_terrain_fetch(mapx, mapz, &request);
    LibToriRS_IOQueuePushCache(ctx->io, request.table_id, request.archive_id, request.flags);
}

static inline int
dat1io_map_chunk_terrain_decode(
    struct LibToriRS_IOContext* ctx,
    struct CacheMapTerrain** terrain_out)
{
    struct LibToriRS_IOQueueItem item;
    *terrain_out = NULL;
    if( !LibToriRS_IOQueuePopRead(ctx->io, &item) )
        return -1;
    if( item.kind != TORIRSIO_KIND_CACHE || item.status != TORIRSIO_STAT_DONE ||
        item.error_code != 0 )
        return -1;
    if( item.u.cache.table_id != CACHE_DAT_MAPS || item.u.cache.flags != CACHELIB_MAPCHUNK_TERRAIN )
        return -1;

    struct CacheDatArchive* archive = item.data;
    if( !archive )
        return -1;

    int map_id = item.u.cache.archive_id;
    int map_x = map_id >> 16;
    int map_z = map_id & 0xFFFF;
    *terrain_out = map_terrain_new_from_decode_flags(
        archive->data, archive->data_size, map_x, map_z, MAP_TERRAIN_DECODE_U8);
    cache_dat_archive_free(archive);
    return map_id;
}

static inline void
dat1io_map_chunk_scenery_fetch(
    struct LibToriRS_IOContext* ctx,
    int mapx,
    int mapz)
{
    struct CacheLib_IORequest request;
    cachelib_dat1_map_chunk_scenery_fetch(mapx, mapz, &request);
    LibToriRS_IOQueuePushCache(ctx->io, request.table_id, request.archive_id, request.flags);
}

static inline int
dat1io_map_chunk_scenery_decode(
    struct LibToriRS_IOContext* ctx,
    struct CacheMapLocs** locs_out)
{
    struct LibToriRS_IOQueueItem item;
    *locs_out = NULL;
    if( !LibToriRS_IOQueuePopRead(ctx->io, &item) )
        return -1;
    if( item.kind != TORIRSIO_KIND_CACHE || item.status != TORIRSIO_STAT_DONE ||
        item.error_code != 0 )
        return -1;
    if( item.u.cache.table_id != CACHE_DAT_MAPS || item.u.cache.flags != CACHELIB_MAPCHUNK_SCENERY )
        return -1;

    struct CacheDatArchive* archive = item.data;
    if( !archive )
        return -1;

    int map_id = item.u.cache.archive_id;
    int map_x = map_id >> 16;
    int map_z = map_id & 0xFFFF;
    *locs_out = map_locs_new_from_decode(archive->data, archive->data_size);
    if( *locs_out )
    {
        (*locs_out)->_chunk_mapx = map_x;
        (*locs_out)->_chunk_mapz = map_z;
    }
    cache_dat_archive_free(archive);
    return map_id;
}

static inline void
dat1io_config_jagfile_fetch(struct LibToriRS_IOContext* ctx)
{
    struct CacheLib_IORequest request;
    cachelib_dat1_config_file_fetch(&request);
    LibToriRS_IOQueuePushCache(ctx->io, request.table_id, request.archive_id, request.flags);
}

static inline void
dat1io_versionlist_jagfile_fetch(struct LibToriRS_IOContext* ctx)
{
    struct CacheLib_IORequest request;
    cachelib_dat1_versionlist_fetch(&request);
    LibToriRS_IOQueuePushCache(ctx->io, request.table_id, request.archive_id, request.flags);
}

static inline struct FileListDat*
dat1io_config_jagfile_decode(struct LibToriRS_IOContext* ctx)
{
    struct LibToriRS_IOQueueItem item;
    if( !LibToriRS_IOQueuePopRead(ctx->io, &item) )
        return NULL;
    if( item.kind != TORIRSIO_KIND_CACHE || item.status != TORIRSIO_STAT_DONE ||
        item.error_code != 0 )
        return NULL;
    if( item.u.cache.table_id != CACHE_DAT_CONFIGS ||
        item.u.cache.archive_id != CONFIG_DAT_CONFIGS )
        return NULL;

    struct CacheDatArchive* archive = item.data;
    if( !archive )
        return NULL;

    struct FileListDat* filelist = filelist_dat_new_from_cache_dat_archive(archive);
    cache_dat_archive_free(archive);
    return filelist;
}

static inline struct FileListDat*
dat1io_versionlist_jagfile_decode(struct LibToriRS_IOContext* ctx)
{
    struct LibToriRS_IOQueueItem item;
    if( !LibToriRS_IOQueuePopRead(ctx->io, &item) )
        return NULL;
    if( item.kind != TORIRSIO_KIND_CACHE || item.status != TORIRSIO_STAT_DONE ||
        item.error_code != 0 )
        return NULL;
    if( item.u.cache.table_id != CACHE_DAT_CONFIGS ||
        item.u.cache.archive_id != CONFIG_DAT_VERSION_LIST )
        return NULL;

    struct CacheDatArchive* archive = item.data;
    if( !archive )
        return NULL;

    struct FileListDat* filelist = filelist_dat_new_from_cache_dat_archive(archive);
    cache_dat_archive_free(archive);
    return filelist;
}

static inline void
dat1io_animations_fetch(
    struct LibToriRS_IOContext* ctx,
    int anim_id)
{
    struct CacheLib_IORequest request;
    cachelib_dat1_animations_fetch(anim_id, &request);
    LibToriRS_IOQueuePushCache(ctx->io, request.table_id, request.archive_id, request.flags);
}

static inline int
dat1io_animations_decode(
    struct LibToriRS_IOContext* ctx,
    struct CacheDatAnimBaseFrames** abf_out)
{
    struct LibToriRS_IOQueueItem item;
    *abf_out = NULL;
    if( !LibToriRS_IOQueuePopRead(ctx->io, &item) )
        return -1;
    if( item.kind != TORIRSIO_KIND_CACHE || item.status != TORIRSIO_STAT_DONE ||
        item.error_code != 0 )
        return -1;
    if( item.u.cache.table_id != CACHE_DAT_ANIMATIONS )
        return -1;

    struct CacheDatArchive* archive = item.data;
    if( !archive )
        return -1;

    int anim_id = item.u.cache.archive_id;
    *abf_out = cache_dat_animbaseframes_new_decode(archive->data, archive->data_size);
    cache_dat_archive_free(archive);
    return anim_id;
}

#endif