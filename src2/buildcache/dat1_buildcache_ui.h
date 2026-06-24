#ifndef DAT1_BUILDCACHE_UI_H
#define DAT1_BUILDCACHE_UI_H

#include "buildcache/dat1_buildcache.h"
#include "osrs/revconfig/revconfig.h"
#include "toridraw/toridraw_font.h"
#include "toridraw/toridraw_sprite.h"

struct ToriDraw_Sprite**
dat1_buildcache_sprite_decode(
    struct Dat1BuildCache* buildcache,
    struct RevConfigCacheItem const* item,
    int* out_count);

struct ToriDraw_Sprite*
dat1_buildcache_sprite_decode_ref(
    struct Dat1BuildCache* buildcache,
    char const* sprite_ref);

struct ToriDraw_Font*
dat1_buildcache_font_decode(
    struct Dat1BuildCache* buildcache,
    char const* font_name);

#endif
