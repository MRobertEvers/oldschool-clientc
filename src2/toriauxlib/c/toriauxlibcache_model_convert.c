#include "osrs/rscache/dat2a/dat2a_model.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/core/toriauxlibcore_types.h"
#include "toridraw/toridraw_types.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static inline size_t
gc_face_priorities_byte_count(int face_count)
{
    return (size_t)((face_count + 1) / 2);
}

static inline void
gc_set_face_priority(
    uint8_t* packed,
    int index,
    int value)
{
    assert(value >= 0 && value <= 15);
    int byte_idx = index >> 1;
    if( index & 1 )
        packed[byte_idx] = (uint8_t)((packed[byte_idx] & 0x0Fu) | (uint8_t)(value << 4));
    else
        packed[byte_idx] = (uint8_t)((packed[byte_idx] & 0xF0u) | (uint8_t)(value & 0x0F));
}

static void
gc_calculate_bounds_cylinder(
    struct ToriAuxLibCore_BoundsCylinder* bounds_cylinder,
    int num_vertices,
    gc_vertexint_t* vertex_x,
    gc_vertexint_t* vertex_y,
    gc_vertexint_t* vertex_z)
{
    memset(bounds_cylinder, 0, sizeof(struct ToriAuxLibCore_BoundsCylinder));

    int min_y = INT_MAX;
    int max_y = INT_MIN;
    int radius_squared = 0;

    for( int i = 0; i < num_vertices; i++ )
    {
        int x = (int)vertex_x[i];
        int y = (int)vertex_y[i];
        int z = (int)vertex_z[i];
        if( y < min_y )
            min_y = y;
        if( y > max_y )
            max_y = y;
        int radius_squared_vertex = x * x + z * z;
        if( radius_squared_vertex > radius_squared )
            radius_squared = radius_squared_vertex;
    }

    int center_to_bottom_edge = (int)sqrt(radius_squared + min_y * min_y) + 1;
    int center_to_top_edge = (int)sqrt(radius_squared + max_y * max_y) + 1;
    bounds_cylinder->center_to_bottom_edge = center_to_bottom_edge;
    bounds_cylinder->center_to_top_edge = center_to_top_edge;
    bounds_cylinder->min_y = min_y;
    bounds_cylinder->max_y = max_y;
    bounds_cylinder->radius = (int)sqrt(radius_squared);
    bounds_cylinder->min_z_depth_any_rotation =
        center_to_top_edge > center_to_bottom_edge ? center_to_top_edge : center_to_bottom_edge;
}

static void
gc_bones_free(struct ToriAuxLibCore_Bones* bones)
{
    if( !bones )
        return;
    if( bones->bones )
    {
        for( int i = 0; i < bones->bones_count; i++ )
            free(bones->bones[i]);
        free(bones->bones);
    }
    free(bones->bones_sizes);
    free(bones);
}

static struct ToriAuxLibCore_Bones*
gc_bones_new(
    const uint8_t* bone_map,
    int bone_count)
{
    struct ToriAuxLibCore_Bones* bones =
        (struct ToriAuxLibCore_Bones*)malloc(sizeof(struct ToriAuxLibCore_Bones));
    assert(bones);
    memset(bones, 0, sizeof(struct ToriAuxLibCore_Bones));

    struct RSCacheDat2A_ModelBones* model_bones =
        RSCacheDat2A_ModelBonesNewDecode(bone_map, bone_count);
    if( !model_bones )
    {
        free(bones);
        return NULL;
    }

    bones->bones_count = model_bones->bones_count;
    bones->bones = (gc_boneint_t**)malloc(sizeof(gc_boneint_t*) * (size_t)bones->bones_count);
    bones->bones_sizes = (gc_boneint_t*)malloc(sizeof(gc_boneint_t) * (size_t)bones->bones_count);
    if( !bones->bones || !bones->bones_sizes )
    {
        RSCacheDat2A_ModelBonesFree(model_bones);
        gc_bones_free(bones);
        return NULL;
    }
    memset(bones->bones, 0, sizeof(gc_boneint_t*) * (size_t)bones->bones_count);
    memset(bones->bones_sizes, 0, sizeof(gc_boneint_t) * (size_t)bones->bones_count);

    for( int i = 0; i < bones->bones_count; i++ )
        bones->bones_sizes[i] = (gc_boneint_t)model_bones->bones_sizes[i];

    for( int i = 0; i < bones->bones_count; i++ )
    {
        bones->bones[i] =
            (gc_boneint_t*)malloc(sizeof(gc_boneint_t) * (size_t)model_bones->bones_sizes[i]);
        if( !bones->bones[i] )
        {
            RSCacheDat2A_ModelBonesFree(model_bones);
            gc_bones_free(bones);
            return NULL;
        }
        for( int j = 0; j < model_bones->bones_sizes[i]; j++ )
            bones->bones[i][j] = (gc_boneint_t)model_bones->bones[i][j];
    }

    RSCacheDat2A_ModelBonesFree(model_bones);
    return bones;
}

static gc_vertexint_t*
gc_steal_vertices_int32(
    int* src,
    int count)
{
    if( !src || count <= 0 )
        return NULL;

    gc_vertexint_t* dst = (gc_vertexint_t*)malloc((size_t)count * sizeof(gc_vertexint_t));
    if( !dst )
    {
        free(src);
        return NULL;
    }

    for( int i = 0; i < count; i++ )
        dst[i] = (gc_vertexint_t)src[i];
    free(src);
    return dst;
}

static gc_faceint_t*
gc_steal_face_indices_int32(
    int* src,
    int count)
{
    if( !src || count <= 0 )
        return NULL;

    gc_faceint_t* dst = (gc_faceint_t*)malloc((size_t)count * sizeof(gc_faceint_t));
    if( !dst )
    {
        free(src);
        return NULL;
    }

    for( int i = 0; i < count; i++ )
        dst[i] = (gc_faceint_t)src[i];
    free(src);
    return dst;
}

static void
gc_model_move_from_cache_model(
    struct ToriAuxLibCore_Model* gc,
    struct RSCacheDat2A_Model* model)
{
    assert(gc && model);

    if( model->vertex_count > 0 && model->vertices_x && model->vertices_y && model->vertices_z )
    {
        int count = model->vertex_count;
        gc->vertex_count = count;
        gc->vertices_x = gc_steal_vertices_int32(model->vertices_x, count);
        gc->vertices_y = gc_steal_vertices_int32(model->vertices_y, count);
        gc->vertices_z = gc_steal_vertices_int32(model->vertices_z, count);
        model->vertices_x = NULL;
        model->vertices_y = NULL;
        model->vertices_z = NULL;
    }

    if( model->face_count > 0 && model->face_indices_a && model->face_indices_b &&
        model->face_indices_c )
    {
        int count = model->face_count;
        gc->face_count = count;
        gc->face_indices_a = gc_steal_face_indices_int32(model->face_indices_a, count);
        gc->face_indices_b = gc_steal_face_indices_int32(model->face_indices_b, count);
        gc->face_indices_c = gc_steal_face_indices_int32(model->face_indices_c, count);
        model->face_indices_a = NULL;
        model->face_indices_b = NULL;
        model->face_indices_c = NULL;
    }

    int fc = model->face_count;
    if( model->face_colors && fc > 0 )
    {
        gc->face_colors = (gc_hsl16_t*)model->face_colors;
        model->face_colors = NULL;
    }

    if( fc > 0 )
    {
        gc->face_colors_a = (gc_hsl16_t*)calloc((size_t)fc, sizeof(gc_hsl16_t));
        gc->face_colors_b = (gc_hsl16_t*)calloc((size_t)fc, sizeof(gc_hsl16_t));
        gc->face_colors_c = (gc_hsl16_t*)calloc((size_t)fc, sizeof(gc_hsl16_t));
    }

    if( model->face_alphas && fc > 0 )
    {
        gc->face_alphas = model->face_alphas;
        model->face_alphas = NULL;
    }

    if( model->face_infos && fc > 0 )
    {
        gc->face_infos = (int*)malloc((size_t)fc * sizeof(int));
        if( gc->face_infos )
        {
            for( int i = 0; i < fc; i++ )
                gc->face_infos[i] = (int)model->face_infos[i];
        }
        free(model->face_infos);
        model->face_infos = NULL;
    }

    if( model->face_priorities && fc > 0 )
    {
        size_t nbytes = gc_face_priorities_byte_count(fc);
        gc->face_priorities = (uint8_t*)calloc(nbytes, 1);
        if( gc->face_priorities )
        {
            for( int i = 0; i < fc; i++ )
                gc_set_face_priority(gc->face_priorities, i, model->face_priorities[i]);
        }
        free(model->face_priorities);
        model->face_priorities = NULL;
    }

    int tfc = model->textured_face_count;
    const bool had_per_face_tex_coords = (fc > 0 && model->face_texture_coords != NULL);
    if( tfc > 0 && model->textured_p_coordinate && model->textured_m_coordinate &&
        model->textured_n_coordinate )
    {
        gc->textured_p_coordinate = (gc_faceint_t*)model->textured_p_coordinate;
        gc->textured_m_coordinate = (gc_faceint_t*)model->textured_m_coordinate;
        gc->textured_n_coordinate = (gc_faceint_t*)model->textured_n_coordinate;
        model->textured_p_coordinate = NULL;
        model->textured_m_coordinate = NULL;
        model->textured_n_coordinate = NULL;
    }

    if( fc > 0 && model->face_texture_coords )
    {
        gc->face_texture_coords = (gc_faceint_t*)malloc((size_t)fc * sizeof(gc_faceint_t));
        assert(gc->face_texture_coords);
        for( int i = 0; i < fc; i++ )
            gc->face_texture_coords[i] = (gc_faceint_t)ToriDraw_NormalizeFaceTextureCoord(
                model->face_texture_coords[i], tfc);
        free(model->face_texture_coords);
        model->face_texture_coords = NULL;
    }

    gc->textured_face_count = tfc;

    if( fc > 0 && model->face_textures )
    {
        gc->face_textures = (gc_faceint_t*)model->face_textures;
        model->face_textures = NULL;
    }

    if( tfc > 0 || had_per_face_tex_coords )
        gc->flags |= 0x02u;

    if( model->vertex_bone_map )
        gc->vertex_bones = gc_bones_new(model->vertex_bone_map, model->vertex_count);
    if( model->face_bone_map )
        gc->face_bones = gc_bones_new(model->face_bone_map, model->face_count);

    /* Copy animaya skin if present */
    if( model->animaya_vertex_count > 0 && model->animaya_group_counts && model->animaya_groups &&
        model->animaya_scales )
    {
        int vc = model->animaya_vertex_count;
        struct ToriAuxLibCore_AnimayaSkin* skin =
            calloc(1, sizeof(struct ToriAuxLibCore_AnimayaSkin));
        if( skin )
        {
            skin->vertex_count = vc;
            skin->group_counts = malloc((size_t)vc);
            skin->groups = calloc((size_t)vc, sizeof(uint8_t*));
            skin->scales = calloc((size_t)vc, sizeof(uint8_t*));
            if( skin->group_counts && skin->groups && skin->scales )
            {
                memcpy(skin->group_counts, model->animaya_group_counts, (size_t)vc);
                for( int i = 0; i < vc; i++ )
                {
                    int cnt = skin->group_counts[i];
                    if( cnt <= 0 )
                        continue;
                    skin->groups[i] = malloc((size_t)cnt);
                    skin->scales[i] = malloc((size_t)cnt);
                    if( skin->groups[i] && model->animaya_groups[i] )
                        memcpy(skin->groups[i], model->animaya_groups[i], (size_t)cnt);
                    if( skin->scales[i] && model->animaya_scales[i] )
                        memcpy(skin->scales[i], model->animaya_scales[i], (size_t)cnt);
                }
                gc->animaya_skin = skin;
            }
            else
            {
                ToriAuxLibCore_AnimayaSkinFree(skin);
            }
        }
    }

    if( gc->vertex_count > 0 && gc->vertices_x && gc->vertices_y && gc->vertices_z )
    {
        gc->bounds_cylinder = (struct ToriAuxLibCore_BoundsCylinder*)malloc(
            sizeof(struct ToriAuxLibCore_BoundsCylinder));
        if( gc->bounds_cylinder )
            gc_calculate_bounds_cylinder(
                gc->bounds_cylinder,
                gc->vertex_count,
                gc->vertices_x,
                gc->vertices_y,
                gc->vertices_z);
    }

    gc->flags |= 0x01u;
    ToriAuxLibCore_ModelAssertPnmTextureInvariant(gc);
}

struct ToriAuxLibCore_Model*
ToriAuxLibCache_ModelNewFromCacheModel(const void* cache_model_ptr)
{
    struct RSCacheDat2A_Model* model = (struct RSCacheDat2A_Model*)cache_model_ptr;
    assert(model);

    struct ToriAuxLibCore_Model* gc =
        (struct ToriAuxLibCore_Model*)calloc(1, sizeof(struct ToriAuxLibCore_Model));
    assert(gc);

    gc_model_move_from_cache_model(gc, model);
    return gc;
}
