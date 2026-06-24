#ifndef CORE_DAT1_DAT1IO_H
#define CORE_DAT1_DAT1IO_H

#include "../../libtorirs.h"
#include "osrs/rscache/dat1disk/dat1disk.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "osrs/rscache/dat2a/dat2a_maps.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "osrs/rscache/dat1a/dat1a_anim_frame.h"
#include "platforms/platform_x/cachelib_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline void
dat1io_model_fetch(
    struct LibToriRS_IOContext* ctx,
    int model_id)
{
    struct RSCacheDat2DiskLib_IORequest request;
    cachelib_dat1_model_fetch(model_id, &request);
    LibToriRS_IOQueuePushCache(ctx->io, request.table_id, request.archive_id, request.flags);
}

static inline struct RSCacheDat2A_Model*
dat1io_model_decode(
    struct LibToriRS_IOContext* ctx,
    int* model_id_out)
{
    struct RSCacheDat1Disk_Archive* archive = NULL;
    struct RSCacheDat2A_Model* model = NULL;
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

    model = RSCacheDat2A_ModelNewFromDatArchive(archive, model_id);
    if( !model )
    {
        fprintf(stderr, "Failed to decode model\n");
        goto cleanup;
    }

cleanup:
    RSCacheDat1Disk_ArchiveFree(archive);
    item.data = NULL;
    return model;
}

static inline void
dat1io_map_chunk_terrain_fetch(
    struct LibToriRS_IOContext* ctx,
    int mapx,
    int mapz)
{
    struct RSCacheDat2DiskLib_IORequest request;
    cachelib_dat1_map_chunk_terrain_fetch(mapx, mapz, &request);
    LibToriRS_IOQueuePushCache(ctx->io, request.table_id, request.archive_id, request.flags);
}

static inline int
dat1io_map_chunk_terrain_decode(
    struct LibToriRS_IOContext* ctx,
    struct RSCacheDat2A_MapTerrain** terrain_out)
{
    struct LibToriRS_IOQueueItem item;
    *terrain_out = NULL;
    if( !LibToriRS_IOQueuePopRead(ctx->io, &item) )
        return -1;
    if( item.kind != TORIRSIO_KIND_CACHE || item.status != TORIRSIO_STAT_DONE ||
        item.error_code != 0 )
        return -1;
    if( item.u.cache.table_id != RSCacheDat1Disk_Table_Maps || item.u.cache.flags != CACHELIB_MAPCHUNK_TERRAIN )
        return -1;

    struct RSCacheDat1Disk_Archive* archive = item.data;
    if( !archive )
        return -1;

    int map_id = item.u.cache.archive_id;
    int map_x = map_id >> 16;
    int map_z = map_id & 0xFFFF;
    *terrain_out = RSCacheDat2A_MapTerrainNewFromDecodeFlags(
        archive->data, archive->data_size, map_x, map_z, MAP_TERRAIN_DECODE_U8);
    RSCacheDat1Disk_ArchiveFree(archive);
    return map_id;
}

static inline void
dat1io_map_chunk_scenery_fetch(
    struct LibToriRS_IOContext* ctx,
    int mapx,
    int mapz)
{
    struct RSCacheDat2DiskLib_IORequest request;
    cachelib_dat1_map_chunk_scenery_fetch(mapx, mapz, &request);
    LibToriRS_IOQueuePushCache(ctx->io, request.table_id, request.archive_id, request.flags);
}

static inline int
dat1io_map_chunk_scenery_decode(
    struct LibToriRS_IOContext* ctx,
    struct RSCacheDat2A_MapLocs** locs_out)
{
    struct LibToriRS_IOQueueItem item;
    *locs_out = NULL;
    if( !LibToriRS_IOQueuePopRead(ctx->io, &item) )
        return -1;
    if( item.kind != TORIRSIO_KIND_CACHE || item.status != TORIRSIO_STAT_DONE ||
        item.error_code != 0 )
        return -1;
    if( item.u.cache.table_id != RSCacheDat1Disk_Table_Maps || item.u.cache.flags != CACHELIB_MAPCHUNK_SCENERY )
        return -1;

    struct RSCacheDat1Disk_Archive* archive = item.data;
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
    RSCacheDat1Disk_ArchiveFree(archive);
    return map_id;
}

static inline void
dat1io_config_jagfile_fetch(struct LibToriRS_IOContext* ctx)
{
    struct RSCacheDat2DiskLib_IORequest request;
    cachelib_dat1_config_file_fetch(&request);
    LibToriRS_IOQueuePushCache(ctx->io, request.table_id, request.archive_id, request.flags);
}

static inline void
dat1io_versionlist_jagfile_fetch(struct LibToriRS_IOContext* ctx)
{
    struct RSCacheDat2DiskLib_IORequest request;
    cachelib_dat1_versionlist_fetch(&request);
    LibToriRS_IOQueuePushCache(ctx->io, request.table_id, request.archive_id, request.flags);
}

static inline struct RSCacheShared_FileListDat*
dat1io_config_jagfile_decode(struct LibToriRS_IOContext* ctx)
{
    struct LibToriRS_IOQueueItem item;
    if( !LibToriRS_IOQueuePopRead(ctx->io, &item) )
        return NULL;
    if( item.kind != TORIRSIO_KIND_CACHE || item.status != TORIRSIO_STAT_DONE ||
        item.error_code != 0 )
        return NULL;
    if( item.u.cache.table_id != RSCacheDat1Disk_Table_Configs ||
        item.u.cache.archive_id != RSCacheDat1A_ConfigKind_Configs )
        return NULL;

    struct RSCacheDat1Disk_Archive* archive = item.data;
    if( !archive )
        return NULL;

    struct RSCacheShared_FileListDat* filelist = RSCacheShared_FileListDatNewFromCacheDatArchive(archive);
    RSCacheDat1Disk_ArchiveFree(archive);
    return filelist;
}

static inline struct RSCacheShared_FileListDat*
dat1io_versionlist_jagfile_decode(struct LibToriRS_IOContext* ctx)
{
    struct LibToriRS_IOQueueItem item;
    if( !LibToriRS_IOQueuePopRead(ctx->io, &item) )
        return NULL;
    if( item.kind != TORIRSIO_KIND_CACHE || item.status != TORIRSIO_STAT_DONE ||
        item.error_code != 0 )
        return NULL;
    if( item.u.cache.table_id != RSCacheDat1Disk_Table_Configs ||
        item.u.cache.archive_id != RSCacheDat1A_ConfigKind_VersionList )
        return NULL;

    struct RSCacheDat1Disk_Archive* archive = item.data;
    if( !archive )
        return NULL;

    struct RSCacheShared_FileListDat* filelist = RSCacheShared_FileListDatNewFromCacheDatArchive(archive);
    RSCacheDat1Disk_ArchiveFree(archive);
    return filelist;
}

static inline void
dat1io_animations_fetch(
    struct LibToriRS_IOContext* ctx,
    int anim_id)
{
    struct RSCacheDat2DiskLib_IORequest request;
    cachelib_dat1_animations_fetch(anim_id, &request);
    LibToriRS_IOQueuePushCache(ctx->io, request.table_id, request.archive_id, request.flags);
}

static inline int
dat1io_animations_decode(
    struct LibToriRS_IOContext* ctx,
    struct RSCacheDat1A_AnimBaseFrames** abf_out)
{
    struct LibToriRS_IOQueueItem item;
    *abf_out = NULL;
    if( !LibToriRS_IOQueuePopRead(ctx->io, &item) )
        return -1;
    if( item.kind != TORIRSIO_KIND_CACHE || item.status != TORIRSIO_STAT_DONE ||
        item.error_code != 0 )
        return -1;
    if( item.u.cache.table_id != RSCacheDat1Disk_Table_Animations )
        return -1;

    struct RSCacheDat1Disk_Archive* archive = item.data;
    if( !archive )
        return -1;

    int anim_id = item.u.cache.archive_id;
    *abf_out = RSCacheDat1A_AnimBaseFramesNewDecode(archive->data, archive->data_size);
    RSCacheDat1Disk_ArchiveFree(archive);
    return anim_id;
}

static inline struct RSCacheShared_FileListDat*
dat1io_jagfile_decode_kind(
    struct LibToriRS_IOContext* ctx,
    int expected_archive_id)
{
    struct LibToriRS_IOQueueItem item;
    if( !LibToriRS_IOQueuePopRead(ctx->io, &item) )
        return NULL;
    if( item.kind != TORIRSIO_KIND_CACHE || item.status != TORIRSIO_STAT_DONE ||
        item.error_code != 0 )
        return NULL;
    if( item.u.cache.table_id != RSCacheDat1Disk_Table_Configs ||
        item.u.cache.archive_id != expected_archive_id )
        return NULL;

    struct RSCacheDat1Disk_Archive* archive = item.data;
    if( !archive )
        return NULL;

    struct RSCacheShared_FileListDat* filelist =
        RSCacheShared_FileListDatNewFromCacheDatArchive(archive);
    RSCacheDat1Disk_ArchiveFree(archive);
    return filelist;
}

static inline void
dat1io_config_archive_fetch(
    struct LibToriRS_IOContext* ctx,
    int archive_id)
{
    struct RSCacheDat2DiskLib_IORequest request;
    request.table_id = RSCacheDat1Disk_Table_Configs;
    request.archive_id = archive_id;
    request.flags = 0;
    LibToriRS_IOQueuePushCache(ctx->io, request.table_id, request.archive_id, request.flags);
}

static inline void
dat1io_media_jagfile_fetch(struct LibToriRS_IOContext* ctx)
{
    dat1io_config_archive_fetch(ctx, RSCacheDat1A_ConfigKind_Media2dGraphics);
}

static inline struct RSCacheShared_FileListDat*
dat1io_media_jagfile_decode(struct LibToriRS_IOContext* ctx)
{
    return dat1io_jagfile_decode_kind(ctx, RSCacheDat1A_ConfigKind_Media2dGraphics);
}

static inline void
dat1io_title_fonts_jagfile_fetch(struct LibToriRS_IOContext* ctx)
{
    dat1io_config_archive_fetch(ctx, RSCacheDat1A_ConfigKind_TitleAndFonts);
}

static inline struct RSCacheShared_FileListDat*
dat1io_title_fonts_jagfile_decode(struct LibToriRS_IOContext* ctx)
{
    return dat1io_jagfile_decode_kind(ctx, RSCacheDat1A_ConfigKind_TitleAndFonts);
}

static inline void
dat1io_interfaces_jagfile_fetch(struct LibToriRS_IOContext* ctx)
{
    dat1io_config_archive_fetch(ctx, RSCacheDat1A_ConfigKind_Interfaces);
}

static inline struct RSCacheShared_FileListDat*
dat1io_interfaces_jagfile_decode(struct LibToriRS_IOContext* ctx)
{
    return dat1io_jagfile_decode_kind(ctx, RSCacheDat1A_ConfigKind_Interfaces);
}

#endif