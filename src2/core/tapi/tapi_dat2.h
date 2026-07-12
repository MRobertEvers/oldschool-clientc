#ifndef CORE_TAPI_DAT2_H
#define CORE_TAPI_DAT2_H

#include "../../ioqueue/libtorirs_ioqueue.h"
#include "../../platforms/platform_x/cachelib_client.h"
#include "osrs/rscache/dat2a/dat2a_maps.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "osrs/rscache/dat2disk/dat2disk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline void
TAPIDat2_FetchModel(
    struct LibToriRS_IOContext* ctx,
    int model_id)
{
    struct CacheLib_IORequest request;
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
    if( !archive || archive->data_size < 2 )
    {
        if( archive )
            RSCacheDat2Disk_ArchiveFree(archive);
        item->data = NULL;
        return NULL;
    }

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
    struct CacheLib_IORequest request;
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
    struct CacheLib_IORequest request;
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
    struct CacheLib_IORequest request;
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

static inline void
TAPIDat2_FetchSprite(
    struct LibToriRS_IOContext* ctx,
    int sprite_id)
{
    struct CacheLib_IORequest request;
    cachelib_dat2_sprite_fetch(sprite_id, &request);
    LibToriRS_IOQueuePushCache(ctx->io, request.table_id, request.archive_id, request.flags);
}

static inline struct RSCacheDat2Disk_Archive*
TAPIDat2_DecodeSpriteArchive(
    struct LibToriRS_IOContext* ctx,
    int slot_id,
    int expected_sprite_id)
{
    struct LibToriRS_IOQueueItem* item = LibToriRS_IOQueueFindBySlot(ctx->io, slot_id);
    if( !item )
        return NULL;
    if( item->kind != TORIRSIO_KIND_CACHE || item->status != TORIRSIO_STAT_DONE ||
        item->error_code != 0 )
        return NULL;
    if( item->u.cache.table_id != RSCacheDat2Disk_Table_Sprites ||
        item->u.cache.archive_id != expected_sprite_id )
        return NULL;

    return item->data;
}

static inline void
TAPIDat2_FetchInterface(
    struct LibToriRS_IOContext* ctx,
    int iface_id)
{
    struct CacheLib_IORequest request;
    cachelib_dat2_interface_fetch(iface_id, &request);
    LibToriRS_IOQueuePushCache(ctx->io, request.table_id, request.archive_id, request.flags);
}

static inline struct RSCacheDat2Disk_Archive*
TAPIDat2_DecodeInterfaceArchive(
    struct LibToriRS_IOContext* ctx,
    int slot_id,
    int expected_iface_id)
{
    struct LibToriRS_IOQueueItem* item = LibToriRS_IOQueueFindBySlot(ctx->io, slot_id);
    if( !item )
        return NULL;
    if( item->kind != TORIRSIO_KIND_CACHE || item->status != TORIRSIO_STAT_DONE ||
        item->error_code != 0 )
        return NULL;
    if( item->u.cache.table_id != RSCacheDat2Disk_Table_Interfaces ||
        item->u.cache.archive_id != expected_iface_id )
        return NULL;

    return item->data;
}

static inline void
TAPIDat2_FetchFont(
    struct LibToriRS_IOContext* ctx,
    int font_id)
{
    struct CacheLib_IORequest request;
    cachelib_dat2_font_fetch(font_id, &request);
    LibToriRS_IOQueuePushCache(ctx->io, request.table_id, request.archive_id, request.flags);
}

static inline struct RSCacheDat2Disk_Archive*
TAPIDat2_DecodeFontArchive(
    struct LibToriRS_IOContext* ctx,
    int slot_id,
    int expected_font_id)
{
    struct LibToriRS_IOQueueItem* item = LibToriRS_IOQueueFindBySlot(ctx->io, slot_id);
    if( !item )
        return NULL;
    if( item->kind != TORIRSIO_KIND_CACHE || item->status != TORIRSIO_STAT_DONE ||
        item->error_code != 0 )
        return NULL;
    if( item->u.cache.table_id != RSCacheDat2Disk_Table_Fonts ||
        item->u.cache.archive_id != expected_font_id )
        return NULL;

    return item->data;
}

static inline void
TAPIDat2_FetchClientScript(
    struct LibToriRS_IOContext* ctx,
    int script_id)
{
    struct CacheLib_IORequest request;
    cachelib_dat2_clientscript_fetch(script_id, &request);
    LibToriRS_IOQueuePushCache(ctx->io, request.table_id, request.archive_id, request.flags);
}

static inline struct RSCacheDat2Disk_Archive*
TAPIDat2_DecodeClientScriptArchive(
    struct LibToriRS_IOContext* ctx,
    int slot_id,
    int expected_script_id)
{
    struct LibToriRS_IOQueueItem* item = LibToriRS_IOQueueFindBySlot(ctx->io, slot_id);
    if( !item )
        return NULL;
    if( item->kind != TORIRSIO_KIND_CACHE || item->status != TORIRSIO_STAT_DONE ||
        item->error_code != 0 )
        return NULL;
    if( item->u.cache.table_id != RSCacheDat2Disk_Table_Clientscript ||
        item->u.cache.archive_id != expected_script_id )
        return NULL;

    return item->data;
}

static inline void
TAPIDat2_FetchReferenceTable(
    struct LibToriRS_IOContext* ctx,
    int table_id)
{
    LibToriRS_IOQueuePushReferenceTable(ctx->io, table_id);
}

static inline struct RSCacheDat2Disk_ReferenceTable*
TAPIDat2_DecodeReferenceTable(
    struct LibToriRS_IOContext* ctx,
    int slot_id,
    int expected_table_id)
{
    struct LibToriRS_IOQueueItem* item =
        LibToriRS_IOQueueFindReferenceTable(ctx->io, slot_id, expected_table_id);
    if( !item )
        return NULL;
    if( item->error_code != 0 )
        return NULL;

    struct RSCacheDat2Disk_Archive* archive = item->data;
    if( !archive )
        return NULL;

    struct RSCacheDat2Disk_ReferenceTable* table =
        RSCacheDat2Disk_ReferenceTableNewDecode(archive->data, archive->data_size);
    RSCacheDat2Disk_ArchiveFree(archive);
    item->data = NULL;
    return table;
}

static inline void
TAPIDat2_FetchArchive(
    struct LibToriRS_IOContext* ctx,
    int table_id,
    int archive_id)
{
    struct CacheLib_IORequest request;
    cachelib_dat2_archive_fetch(table_id, archive_id, &request);
    LibToriRS_IOQueuePushCache(ctx->io, request.table_id, request.archive_id, request.flags);
}

static inline struct RSCacheDat2Disk_Archive*
TAPIDat2_DecodeArchive(
    struct LibToriRS_IOContext* ctx,
    int slot_id,
    int expected_table_id,
    int expected_archive_id)
{
    struct LibToriRS_IOQueueItem* item = LibToriRS_IOQueueFindBySlot(ctx->io, slot_id);
    if( !item )
        return NULL;
    if( item->kind != TORIRSIO_KIND_CACHE || item->status != TORIRSIO_STAT_DONE ||
        item->error_code != 0 )
        return NULL;
    if( item->u.cache.table_id != expected_table_id ||
        item->u.cache.archive_id != expected_archive_id )
        return NULL;

    return item->data;
}
#endif
