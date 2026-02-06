#ifndef DASH_UTILS_H
#define DASH_UTILS_H

#include "graphics/dash.h"
#include "osrs/rscache/tables/frame.h"
#include "osrs/rscache/tables/framemap.h"
#include "osrs/rscache/tables/model.h"
#include "osrs/rscache/tables_dat/animframe.h"
#include "osrs/rscache/tables_dat/pix32.h"
#include "osrs/rscache/tables_dat/pix8.h"
#include "osrs/rscache/tables_dat/pixfont.h"

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

struct DashPixFont*
dashpixfont_new_from_cache_dat_pixfont_move(struct CacheDatPixfont* pixfont);

struct DashModel*
dashmodel_new_from_cache_model(struct CacheModel* model);

struct DashModelBones*
dashmodel_bones_new(
    int* bone_map,
    int bone_count);

static struct DashModelLighting*
dashmodel_lighting_new_default(
    struct CacheModel* model,
    struct DashModelNormals* normals,
    int model_contrast,
    int model_attenuation);

#endif