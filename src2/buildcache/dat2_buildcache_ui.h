#ifndef DAT2_BUILDCACHE_UI_H
#define DAT2_BUILDCACHE_UI_H

#include "buildcache/dat2_buildcache.h"
#include "osrs/rscache/dat2a/dat2a_component.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "revconfig/revconfig.h"
#include "toridraw/toridraw_font.h"
#include "toridraw/toridraw_sprite.h"

struct Dat2BuildCache;

void
dat2_buildcache_ui_set_disk(
    struct Dat2BuildCache* buildcache,
    struct RSCacheDat2Disk* disk);

struct RSCacheDat2Disk*
dat2_buildcache_ui_disk(struct Dat2BuildCache* buildcache);

struct ToriDraw_Sprite**
dat2_buildcache_sprite_decode(
    struct Dat2BuildCache* buildcache,
    struct RevConfigCacheItem const* item,
    int* out_count);

struct ToriDraw_Sprite**
dat2_buildcache_sprite_decode_id(
    struct Dat2BuildCache* buildcache,
    int sprite_id,
    int* out_count);

struct ToriDraw_Font*
dat2_buildcache_font_decode_id(
    struct Dat2BuildCache* buildcache,
    int font_id);

Component*
dat2_buildcache_component_decode_iface_file(
    struct Dat2BuildCache* buildcache,
    int iface_id,
    int file_index,
    int* out_size);

#endif
