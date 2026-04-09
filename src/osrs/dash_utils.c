#include "dash_utils.h"

#include "graphics/dash_model_internal.h"
#include "osrs/rscache/tables/model.h"

#include <assert.h>
#include <math.h>
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

    dashframe->index_frame_ids = malloc(frame->translator_count * sizeof(int16_t));
    memcpy(
        dashframe->index_frame_ids,
        frame->index_frame_ids,
        frame->translator_count * sizeof(int16_t));

    dashframe->translator_arg_x = malloc(frame->translator_count * sizeof(int16_t));
    memcpy(
        dashframe->translator_arg_x,
        frame->translator_arg_x,
        frame->translator_count * sizeof(int16_t));

    dashframe->translator_arg_y = malloc(frame->translator_count * sizeof(int16_t));
    memcpy(
        dashframe->translator_arg_y,
        frame->translator_arg_y,
        frame->translator_count * sizeof(int16_t));

    dashframe->translator_arg_z = malloc(frame->translator_count * sizeof(int16_t));
    memcpy(
        dashframe->translator_arg_z,
        frame->translator_arg_z,
        frame->translator_count * sizeof(int16_t));

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

    dashframe->index_frame_ids = malloc(animframe->length * sizeof(int16_t));
    memcpy(dashframe->index_frame_ids, animframe->groups, animframe->length * sizeof(int16_t));

    dashframe->translator_arg_x = malloc(animframe->length * sizeof(int16_t));
    memcpy(dashframe->translator_arg_x, animframe->x, animframe->length * sizeof(int16_t));

    dashframe->translator_arg_y = malloc(animframe->length * sizeof(int16_t));
    memcpy(dashframe->translator_arg_y, animframe->y, animframe->length * sizeof(int16_t));

    dashframe->translator_arg_z = malloc(animframe->length * sizeof(int16_t));
    memcpy(dashframe->translator_arg_z, animframe->z, animframe->length * sizeof(int16_t));

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
    dashframemap->bone_groups_lengths = malloc(animframe->base->length * sizeof(int));
    dashframemap->types = malloc(animframe->base->length * sizeof(int));
    memcpy(
        dashframemap->bone_groups_lengths,
        animframe->base->label_counts,
        animframe->base->length * sizeof(int));
    memcpy(dashframemap->types, animframe->base->types, animframe->base->length * sizeof(int));

    for( int i = 0; i < animframe->base->length; i++ )
    {
        int count = animframe->base->label_counts[i];
        dashframemap->bone_groups[i] = malloc((size_t)count * sizeof(int));
        memcpy(
            dashframemap->bone_groups[i], animframe->base->labels[i], (size_t)count * sizeof(int));
    }
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
    if( dashsprite && pix8_palette )
    {
        dashsprite->crop_x = pix8_palette->crop_x;
        dashsprite->crop_y = pix8_palette->crop_y;
    }
    return dashsprite;
}

struct DashPixFont*
dashpixfont_new_from_cache_dat_pixfont_move(struct CacheDatPixfont* pixfont)
{
    struct DashPixFont* dashpixfont = malloc(sizeof(struct DashPixFont));
    memset(dashpixfont, 0, sizeof(struct DashPixFont));
    dashpixfont->charcode_set = pixfont->charcode_set;

    memcpy(dashpixfont->char_mask, pixfont->char_mask, sizeof(int*) * DASH_FONT_CHAR_COUNT);

    memcpy(
        dashpixfont->char_mask_width, pixfont->char_mask_width, sizeof(int) * DASH_FONT_CHAR_COUNT);
    memcpy(
        dashpixfont->char_mask_height,
        pixfont->char_mask_height,
        sizeof(int) * DASH_FONT_CHAR_COUNT);
    memcpy(dashpixfont->char_offset_x, pixfont->char_offset_x, sizeof(int) * DASH_FONT_CHAR_COUNT);
    memcpy(dashpixfont->char_offset_y, pixfont->char_offset_y, sizeof(int) * DASH_FONT_CHAR_COUNT);
    memcpy(
        dashpixfont->char_advance, pixfont->char_advance, sizeof(int) * (DASH_FONT_CHAR_COUNT + 1));
    memcpy(dashpixfont->draw_width, pixfont->draw_width, sizeof(int) * 256);

    // Calculate height2d as max char height
    dashpixfont->height2d = 0;
    for( int i = 0; i < DASH_FONT_CHAR_COUNT; i++ )
    {
        if( dashpixfont->char_mask_height[i] > dashpixfont->height2d )
        {
            dashpixfont->height2d = dashpixfont->char_mask_height[i];
        }
    }

    dashpixfont->atlas = dashfont_build_atlas(dashpixfont);

    // Moved out of.
    memset(pixfont, 0x00, sizeof(struct CacheDatPixfont));
    return dashpixfont;
}

void
dashmodel_move_from_cache_model(
    struct DashModel* dash_model,
    struct CacheModel* model)
{
    assert(dash_model && model);
    struct DashModelFull* dm = (struct DashModelFull*)(void*)dash_model;
    assert((dm->flags & DASHMODEL_FLAG_VALID) != 0);
    assert(dashmodel__is_full_layout(dash_model));

    if( model->vertex_count > 0 && model->vertices_x && model->vertices_y && model->vertices_z )
    {
        dashmodel_set_vertices_i32(
            dash_model,
            model->vertex_count,
            model->vertices_x,
            model->vertices_y,
            model->vertices_z);
        free(model->vertices_x);
        free(model->vertices_y);
        free(model->vertices_z);
        model->vertices_x = NULL;
        model->vertices_y = NULL;
        model->vertices_z = NULL;
    }

    if( model->face_count > 0 && model->face_indices_a && model->face_indices_b &&
        model->face_indices_c )
    {
        dashmodel_set_face_indices_i32(
            dash_model,
            model->face_count,
            model->face_indices_a,
            model->face_indices_b,
            model->face_indices_c);
        free(model->face_indices_a);
        free(model->face_indices_b);
        free(model->face_indices_c);
        model->face_indices_a = NULL;
        model->face_indices_b = NULL;
        model->face_indices_c = NULL;
    }

    int fc = model->face_count;
    if( model->face_colors && fc > 0 )
    {
        hsl16_t* flat = (hsl16_t*)malloc((size_t)fc * sizeof(hsl16_t));
        for( int i = 0; i < fc; i++ )
            flat[i] = (hsl16_t)(unsigned)(model->face_colors[i] & 0xffff);
        dashmodel_set_face_colors_flat(dash_model, flat, fc);
        free(flat);
        free(model->face_colors);
        model->face_colors = NULL;
    }

    if( fc > 0 )
        dashmodel_alloc_lit_face_colors_zero(dash_model, fc);

    if( model->face_alphas && fc > 0 )
    {
        alphaint_t* al = (alphaint_t*)malloc((size_t)fc * sizeof(alphaint_t));
        for( int i = 0; i < fc; i++ )
            al[i] = (alphaint_t)(model->face_alphas[i] & 0xFF);
        dashmodel_set_face_alphas(dash_model, al, fc);
        free(al);
        free(model->face_alphas);
        model->face_alphas = NULL;
    }

    if( model->face_infos && fc > 0 )
    {
        dashmodel_set_face_infos(dash_model, model->face_infos, fc);
        free(model->face_infos);
        model->face_infos = NULL;
    }

    if( model->face_priorities && fc > 0 )
    {
        dashmodel_set_face_priorities(dash_model, model->face_priorities, fc);
        free(model->face_priorities);
        model->face_priorities = NULL;
    }

    int tfc = model->textured_face_count;
    const bool had_per_face_tex_coords = (fc > 0 && model->face_texture_coords != NULL);
    faceint_t* tp = NULL;
    faceint_t* tm = NULL;
    faceint_t* tn = NULL;
    if( tfc > 0 && model->textured_p_coordinate && model->textured_m_coordinate &&
        model->textured_n_coordinate )
    {
        tp = (faceint_t*)malloc((size_t)tfc * sizeof(faceint_t));
        tm = (faceint_t*)malloc((size_t)tfc * sizeof(faceint_t));
        tn = (faceint_t*)malloc((size_t)tfc * sizeof(faceint_t));
        for( int i = 0; i < tfc; i++ )
        {
            tp[i] = (faceint_t)model->textured_p_coordinate[i];
            tm[i] = (faceint_t)model->textured_m_coordinate[i];
            tn[i] = (faceint_t)model->textured_n_coordinate[i];
        }
        free(model->textured_p_coordinate);
        free(model->textured_m_coordinate);
        free(model->textured_n_coordinate);
        model->textured_p_coordinate = NULL;
        model->textured_m_coordinate = NULL;
        model->textured_n_coordinate = NULL;
    }

    faceint_t* ftc_arr = NULL;
    if( fc > 0 && model->face_texture_coords )
    {
        ftc_arr = (faceint_t*)malloc((size_t)fc * sizeof(faceint_t));
        for( int i = 0; i < fc; i++ )
            ftc_arr[i] = (faceint_t)model->face_texture_coords[i];
        free(model->face_texture_coords);
        model->face_texture_coords = NULL;
    }

    dashmodel_set_texture_coords(
        dash_model,
        tfc,
        tp,
        tm,
        tn,
        ftc_arr,
        fc);
    free(tp);
    free(tm);
    free(tn);
    free(ftc_arr);

    if( fc > 0 && model->face_textures )
    {
        dashmodel_set_face_textures_i32(dash_model, model->face_textures, fc);
        free(model->face_textures);
        model->face_textures = NULL;
    }

    dashmodel_set_has_textures(
        dash_model,
        (tfc > 0 || had_per_face_tex_coords));

    dm->normals = NULL;
    dm->merged_normals = NULL;

    if( model->vertex_bone_map )
        dm->vertex_bones = dashmodel_bones_new(model->vertex_bone_map, model->vertex_count);
    if( model->face_bone_map )
        dm->face_bones = dashmodel_bones_new(model->face_bone_map, model->face_count);

    dashmodel_set_bounds_cylinder(dash_model);
    dashmodel_set_loaded(dash_model, true);
}

struct DashModel*
dashmodel_new_from_cache_model(struct CacheModel* model)
{
    struct DashModel* dash_model = dashmodelfull_new();
    if( !dash_model )
        return NULL;
    dashmodel_move_from_cache_model(dash_model, model);
    return dash_model;
}

struct DashModelBones*
dashmodel_bones_new(
    int* bone_map,
    int bone_count)
{
    struct DashModelBones* bones = (struct DashModelBones*)malloc(sizeof(struct DashModelBones));
    memset(bones, 0, sizeof(struct DashModelBones));

    struct ModelBones* model_bones = modelbones_new_decode(bone_map, bone_count);
    assert(model_bones != NULL);

    bones->bones_count = model_bones->bones_count;
    bones->bones = (boneint_t**)malloc(sizeof(boneint_t*) * bones->bones_count);
    bones->bones_sizes = (boneint_t*)malloc(sizeof(boneint_t) * bones->bones_count);
    memset(bones->bones, 0, sizeof(boneint_t*) * bones->bones_count);
    memset(bones->bones_sizes, 0, sizeof(boneint_t) * bones->bones_count);

    for( int i = 0; i < bones->bones_count; i++ )
        bones->bones_sizes[i] = model_bones->bones_sizes[i];

    for( int i = 0; i < bones->bones_count; i++ )
    {
        bones->bones[i] = (boneint_t*)malloc(sizeof(boneint_t) * model_bones->bones_sizes[i]);
        for( int j = 0; j < model_bones->bones_sizes[i]; j++ )
        {
            bones->bones[i][j] = (boneint_t)model_bones->bones[i][j];
        }
    }

    modelbones_free(model_bones);

    return bones;
}