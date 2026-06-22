#ifndef DASH_UTILS_H
#define DASH_UTILS_H

#include "graphics/dash.h"
#include "osrs/rscache/dat2a/dat2a_frame.h"
#include "osrs/rscache/dat2a/dat2a_framemap.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "osrs/rscache/dat1a/dat1a_anim_frame.h"
#include "osrs/rscache/dat1a/dat1a_pix32.h"
#include "osrs/rscache/dat1a/dat1a_pix8.h"
#include "osrs/rscache/dat1a/dat1a_pix_font.h"

struct RSCacheDat2A_Frame;
struct RSCacheDat2A_Framemap;

struct DashFramemap*
dashRSCacheDat2A_FramemapNewFromCache_framemap(struct RSCacheDat2A_Framemap* framemap);

struct DashFrame*
dashRSCacheDat2A_FrameNewFromCache_frame(struct RSCacheDat2A_Frame* frame);

struct DashFrame*
dashframe_new_from_animframe(struct RSCacheDat1A_AnimFrame* animframe);

struct DashFramemap*
dashframemap_new_from_animframe(struct RSCacheDat1A_AnimFrame* animframe);

struct DashPix8*
dashpix8_new_from_cache_pix8_palette(struct RSCacheDat1A_Pix8Palette* pix8_palette);

struct DashPixPalette*
dashpixpalette_new_from_cache_pix8_palette(struct RSCacheDat1A_Pix8Palette* pix8_palette);

struct DashSprite*
dashsprite_new_from_cache_pix32(struct RSCacheDat1A_Pix32* pix32);

struct DashSprite*
dashsprite_new_from_cache_pix8_palette(struct RSCacheDat1A_Pix8Palette* pix8_palette);

struct DashPixFont*
dashpixfont_new_from_cache_dat_pixfont_move(struct RSCacheDat1A_PixFont* pixfont);

struct DashModel*
dashRSCacheDat2A_ModelNewFromCache_model(struct RSCacheDat2A_Model* model);

void
dashmodel_move_from_cache_model(
    struct DashModel* dash_model,
    struct RSCacheDat2A_Model* model);

struct DashModelBones*
dashmodel_bones_new(
    const uint8_t* bone_map,
    int bone_count);

#endif