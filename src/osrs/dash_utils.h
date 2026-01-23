#ifndef DASH_UTILS_H
#define DASH_UTILS_H

#include "graphics/dash.h"
#include "osrs/rscache/tables/frame.h"
#include "osrs/rscache/tables/framemap.h"
#include "osrs/rscache/tables_dat/animframe.h"
#include "osrs/rscache/tables_dat/pix32.h"
#include "osrs/rscache/tables_dat/pix8.h"

struct CacheFrame;
struct CacheFramemap;

struct DashFramemap*
dashframemap_new_from_cache_framemap(struct CacheFramemap* framemap);

struct DashFrame*
dashframe_new_from_cache_frame(struct CacheFrame* frame);

struct DashFrame*
dashframe_new_from_animframe(struct CacheAnimframe* animframe);

struct DashFramemap*
dashframemap_new_from_animframe(struct CacheAnimframe* animframe);

struct DashPix8*
dashpix8_new_from_cache_pix8_palette(struct CacheDatPix8Palette* pix8_palette);

struct DashPixPalette*
dashpixpalette_new_from_cache_pix8_palette(struct CacheDatPix8Palette* pix8_palette);

struct DashSprite*
dashsprite_new_from_cache_pix32(struct CacheDatPix32* pix32);

struct DashSprite*
dashsprite_new_from_cache_pix8_palette(struct CacheDatPix8Palette* pix8_palette);

#endif