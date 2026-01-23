#include "dash_utils.h"

#include <stdlib.h>
#include <string.h>

struct DashFramemap*
dashframemap_new_from_cache_framemap(struct CacheFramemap* framemap)
{
    struct DashFramemap* dashframemap = malloc(sizeof(struct DashFramemap));
    memset(dashframemap, 0, sizeof(struct DashFramemap));
    dashframemap->id = framemap->id;
    dashframemap->length = framemap->length;

    dashframemap->bone_groups = malloc(framemap->length * sizeof(int*));
    memcpy(dashframemap->bone_groups, framemap->bone_groups, framemap->length * sizeof(int*));

    dashframemap->bone_groups_lengths = malloc(framemap->length * sizeof(int));
    memcpy(
        dashframemap->bone_groups_lengths,
        framemap->bone_groups_lengths,
        framemap->length * sizeof(int));

    dashframemap->types = malloc(framemap->length * sizeof(int));
    memcpy(dashframemap->types, framemap->types, framemap->length * sizeof(int));
    return dashframemap;
}

struct DashFrame*
dashframe_new_from_cache_frame(struct CacheFrame* frame)
{
    struct DashFrame* dashframe = malloc(sizeof(struct DashFrame));
    memset(dashframe, 0, sizeof(struct DashFrame));
    dashframe->_id = frame->_id;
    dashframe->framemap_id = frame->framemap_id;
    dashframe->translator_count = frame->translator_count;

    dashframe->index_frame_ids = malloc(frame->translator_count * sizeof(int));
    memcpy(
        dashframe->index_frame_ids, frame->index_frame_ids, frame->translator_count * sizeof(int));

    dashframe->translator_arg_x = malloc(frame->translator_count * sizeof(int));
    memcpy(
        dashframe->translator_arg_x,
        frame->translator_arg_x,
        frame->translator_count * sizeof(int));

    dashframe->translator_arg_y = malloc(frame->translator_count * sizeof(int));
    memcpy(
        dashframe->translator_arg_y,
        frame->translator_arg_y,
        frame->translator_count * sizeof(int));

    dashframe->translator_arg_z = malloc(frame->translator_count * sizeof(int));
    memcpy(
        dashframe->translator_arg_z,
        frame->translator_arg_z,
        frame->translator_count * sizeof(int));

    dashframe->showing = frame->showing;
    return dashframe;
}

struct DashFrame*
dashframe_new_from_animframe(struct CacheAnimframe* animframe)
{
    struct DashFrame* dashframe = malloc(sizeof(struct DashFrame));
    memset(dashframe, 0, sizeof(struct DashFrame));
    dashframe->_id = animframe->id;
    dashframe->framemap_id = -1;
    dashframe->translator_count = animframe->length;

    dashframe->index_frame_ids = malloc(animframe->length * sizeof(int));
    memcpy(dashframe->index_frame_ids, animframe->groups, animframe->length * sizeof(int));

    dashframe->translator_arg_x = malloc(animframe->length * sizeof(int));
    memcpy(dashframe->translator_arg_x, animframe->x, animframe->length * sizeof(int));

    dashframe->translator_arg_y = malloc(animframe->length * sizeof(int));
    memcpy(dashframe->translator_arg_y, animframe->y, animframe->length * sizeof(int));

    dashframe->translator_arg_z = malloc(animframe->length * sizeof(int));
    memcpy(dashframe->translator_arg_z, animframe->z, animframe->length * sizeof(int));

    dashframe->showing = true;
    return dashframe;
}

struct DashFramemap*
dashframemap_new_from_animframe(struct CacheAnimframe* animframe)
{
    struct DashFramemap* dashframemap = malloc(sizeof(struct DashFramemap));
    memset(dashframemap, 0, sizeof(struct DashFramemap));
    dashframemap->id = animframe->id;
    dashframemap->length = animframe->base->length;

    dashframemap->bone_groups = malloc(animframe->base->length * sizeof(int*));
    memcpy(
        dashframemap->bone_groups, animframe->base->labels, animframe->base->length * sizeof(int*));

    dashframemap->bone_groups_lengths = malloc(animframe->base->length * sizeof(int));
    memcpy(
        dashframemap->bone_groups_lengths,
        animframe->base->label_counts,
        animframe->base->length * sizeof(int));

    dashframemap->types = malloc(animframe->base->length * sizeof(int));
    memcpy(dashframemap->types, animframe->base->types, animframe->base->length * sizeof(int));
    return dashframemap;
}

struct DashPix8*
dashpix8_new_from_cache_pix8_palette(struct CacheDatPix8Palette* pix8_palette)
{
    struct DashPix8* dashpix8 = malloc(sizeof(struct DashPix8));
    memset(dashpix8, 0, sizeof(struct DashPix8));
    uint8_t* pixels = malloc(pix8_palette->width * pix8_palette->height * sizeof(uint8_t));
    memcpy(
        pixels, pix8_palette->pixels, pix8_palette->width * pix8_palette->height * sizeof(uint8_t));

    dashpix8->pixels = (uint8_t*)pixels;
    dashpix8->width = pix8_palette->width;
    dashpix8->height = pix8_palette->height;
    return dashpix8;
}

struct DashPixPalette*
dashpixpalette_new_from_cache_pix8_palette(struct CacheDatPix8Palette* pix8_palette)
{
    struct DashPixPalette* dashpixpalette = malloc(sizeof(struct DashPixPalette));
    memset(dashpixpalette, 0, sizeof(struct DashPixPalette));
    void* palette = malloc(pix8_palette->palette_count * sizeof(int));
    memcpy(palette, pix8_palette->palette, pix8_palette->palette_count * sizeof(int));

    dashpixpalette->palette = (int*)palette;
    dashpixpalette->palette_count = pix8_palette->palette_count;
    return dashpixpalette;
}

struct DashSprite*
dashsprite_new_from_cache_pix32(struct CacheDatPix32* pix32)
{
    struct DashPix32 dashpix32 = { 0 };
    dashpix32.pixels = pix32->pixels;
    dashpix32.draw_width = pix32->draw_width;
    dashpix32.draw_height = pix32->draw_height;
    dashpix32.crop_x = pix32->crop_x;
    dashpix32.crop_y = pix32->crop_y;
    dashpix32.stride_x = pix32->stride_x;
    dashpix32.stride_y = pix32->stride_y;
    struct DashSprite* dashsprite = dashsprite_new_from_pix32(&dashpix32);
    return dashsprite;
}

struct DashSprite*
dashsprite_new_from_cache_pix8_palette(struct CacheDatPix8Palette* pix8_palette)
{
    struct DashPix8* dashpix8 = dashpix8_new_from_cache_pix8_palette(pix8_palette);
    struct DashPixPalette* dashpixpalette =
        dashpixpalette_new_from_cache_pix8_palette(pix8_palette);
    struct DashSprite* dashsprite = dashsprite_new_from_pix8(dashpix8, dashpixpalette);
    dashpix8_free(dashpix8);
    dashpixpalette_free(dashpixpalette);
    return dashsprite;
}