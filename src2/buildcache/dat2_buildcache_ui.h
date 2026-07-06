#ifndef DAT2_BUILDCACHE_UI_H
#define DAT2_BUILDCACHE_UI_H

#include "buildcache/dat2_buildcache.h"
#include "osrs/rscache/dat2a/dat2a_component.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "revconfig/revconfig.h"
#include "toriauxlib/core/toriauxlibcore_types.h"

struct ToriAuxLibCore_Sprite*
dat2_buildcache_sprite_decode_from_archive(
    struct RSCacheDat2Disk_Archive* archive,
    struct RevConfigCacheItem const* item);

struct ToriAuxLibCore_Sprite*
dat2_buildcache_sprite_decode_id_from_archive(
    struct RSCacheDat2Disk_Archive* archive,
    int sprite_id);

struct ToriAuxLibCore_Font*
dat2_buildcache_font_decode_from_archive(
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive,
    int font_id);

RSCacheDat2A_Component*
dat2_buildcache_component_decode_iface_file_from_archive(
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive,
    int iface_id,
    int file_index,
    int* out_size);

struct Dat2BuildCache_InterfaceArchive*
dat2_buildcache_component_decode_iface_archive_from_archive(
    struct RSCacheDat2Disk_ReferenceTable* reference_table,
    struct RSCacheDat2Disk_Archive* archive,
    int iface_id);

struct ToriDraw_Scene;

struct ToriAuxLibCore_Sprite*
dat2_buildcache_obj_icon_sprite(
    struct Dat2BuildCache* buildcache,
    struct ToriDraw_Scene* scene,
    int obj_id,
    int count);

struct ToriAuxLibCore_Sprite*
dat2_buildcache_widget_model_sprite(
    struct Dat2BuildCache* buildcache,
    struct ToriDraw_Scene* scene,
    int model_id,
    int zoom,
    int xan,
    int yan,
    int width,
    int height);

#endif
