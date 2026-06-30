#ifndef DAT2_BUILDCACHE_UI_H
#define DAT2_BUILDCACHE_UI_H

#include "buildcache/dat2_buildcache.h"
#include "osrs/rscache/dat2a/dat2a_component.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "revconfig/revconfig.h"
#include "toridraw/toridraw_font.h"
#include "toridraw/toridraw_sprite.h"

struct ToriDraw_Sprite**
dat2_buildcache_sprite_decode_from_archive(
    struct RSCacheDat2Disk_Archive* archive,
    struct RevConfigCacheItem const* item,
    int* out_count);

struct ToriDraw_Sprite**
dat2_buildcache_sprite_decode_id_from_archive(
    struct RSCacheDat2Disk_Archive* archive,
    int sprite_id,
    int* out_count);

struct ToriDraw_Font*
dat2_buildcache_font_decode_from_archive(
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive,
    int font_id);

Component*
dat2_buildcache_component_decode_iface_file_from_archive(
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive,
    int iface_id,
    int file_index,
    int* out_size);

struct Dat2BuildCache_InterfaceArchive*
dat2_buildcache_component_decode_iface_archive_from_archive(
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive,
    int iface_id);

#endif
