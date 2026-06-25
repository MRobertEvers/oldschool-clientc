#ifndef PLATFORM_X_CACHELIB_CLIENT_H
#define PLATFORM_X_CACHELIB_CLIENT_H

#include "cachelib.h"
#include "osrs/rscache/dat1disk/dat1disk.h"
#include "osrs/rscache/dat1a/dat1a_configs_dat.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "osrs/rscache/dat2a/dat2a_configs.h"

static inline void
cachelib_dat1_config_file_fetch(struct RSCacheDat2DiskLib_IORequest* out)
{
    out->table_id = RSCacheDat1Disk_Table_Configs;
    out->archive_id = RSCacheDat1A_ConfigKind_Configs;
    out->flags = 0;
}

static inline void
cachelib_dat1_model_fetch(
    int model_id,
    struct RSCacheDat2DiskLib_IORequest* out)
{
    out->table_id = RSCacheDat1Disk_Table_Models;
    out->archive_id = model_id;
    out->flags = 0;
}

#define CACHELIB_MAPCHUNK_TERRAIN 0
#define CACHELIB_MAPCHUNK_SCENERY 1

#define CACHELIB_MAPCHUNK_ID(mapx, mapz) (mapx << 16 | mapz)

#define CACHELIB_MAPCHUNK_MAPX(map_id) (map_id >> 16)
#define CACHELIB_MAPCHUNK_MAPZ(map_id) (map_id & 0xFFFF)

static inline void
cachelib_dat1_map_chunk_terrain_fetch(
    int mapx,
    int mapz,
    struct RSCacheDat2DiskLib_IORequest* out)
{
    out->table_id = RSCacheDat1Disk_Table_Maps;
    out->archive_id = CACHELIB_MAPCHUNK_ID(mapx, mapz);
    out->flags = CACHELIB_MAPCHUNK_TERRAIN;
}

static inline void
cachelib_dat1_map_chunk_scenery_fetch(
    int mapx,
    int mapz,
    struct RSCacheDat2DiskLib_IORequest* out)
{
    out->table_id = RSCacheDat1Disk_Table_Maps;
    out->archive_id = CACHELIB_MAPCHUNK_ID(mapx, mapz);
    out->flags = CACHELIB_MAPCHUNK_SCENERY;
}

static inline void
cachelib_dat1_textures_archive_fetch(struct RSCacheDat2DiskLib_IORequest* out)
{
    out->table_id = RSCacheDat1Disk_Table_Configs;
    out->archive_id = RSCacheDat1A_ConfigKind_Textures;
    out->flags = 0;
}

static inline void
cachelib_dat1_versionlist_fetch(struct RSCacheDat2DiskLib_IORequest* out)
{
    out->table_id = RSCacheDat1Disk_Table_Configs;
    out->archive_id = RSCacheDat1A_ConfigKind_VersionList;
    out->flags = 0;
}

static inline void
cachelib_dat1_interfaces_fetch(struct RSCacheDat2DiskLib_IORequest* out)
{
    out->table_id = RSCacheDat1Disk_Table_Configs;
    out->archive_id = RSCacheDat1A_ConfigKind_Interfaces;
    out->flags = 0;
}

static inline void
cachelib_dat1_media_2d_graphics_fetch(struct RSCacheDat2DiskLib_IORequest* out)
{
    out->table_id = RSCacheDat1Disk_Table_Configs;
    out->archive_id = RSCacheDat1A_ConfigKind_Media2dGraphics;
    out->flags = 0;
}

static inline void
cachelib_dat1_animations_fetch(
    int archive_id,
    struct RSCacheDat2DiskLib_IORequest* out)
{
    out->table_id = RSCacheDat1Disk_Table_Animations;
    out->archive_id = archive_id;
    out->flags = 0;
}

static inline void
cachelib_dat2_model_fetch(
    int model_id,
    struct RSCacheDat2DiskLib_IORequest* out)
{
    out->table_id = RSCacheDat2Disk_Table_Models;
    out->archive_id = model_id;
    out->flags = 0;
}

static inline void
cachelib_dat2_map_chunk_terrain_fetch(
    int mapx,
    int mapz,
    struct RSCacheDat2DiskLib_IORequest* out)
{
    out->table_id = RSCacheDat2Disk_Table_Maps;
    out->archive_id = CACHELIB_MAPCHUNK_ID(mapx, mapz);
    out->flags = CACHELIB_MAPCHUNK_TERRAIN;
}

static inline void
cachelib_dat2_map_chunk_scenery_fetch(
    int mapx,
    int mapz,
    struct RSCacheDat2DiskLib_IORequest* out)
{
    out->table_id = RSCacheDat2Disk_Table_Maps;
    out->archive_id = CACHELIB_MAPCHUNK_ID(mapx, mapz);
    out->flags = CACHELIB_MAPCHUNK_SCENERY;
}

static inline void
cachelib_dat2_config_group_fetch(
    int config_kind,
    struct RSCacheDat2DiskLib_IORequest* out)
{
    out->table_id = RSCacheDat2Disk_Table_Configs;
    out->archive_id = config_kind;
    out->flags = 0;
}

static inline void
cachelib_dat2_textures_archive_fetch(struct RSCacheDat2DiskLib_IORequest* out)
{
    out->table_id = RSCacheDat2Disk_Table_Textures;
    out->archive_id = 0;
    out->flags = 0;
}

#endif