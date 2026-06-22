#include "toridraw_cachemodel.h"

#include "osrs/rscache/dat2a/dat2a_model.h"
#include "toridraw/toridraw_model.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static inline size_t
ToriDraw_FacePrioritiesByteCount(int face_count)
{
    return (size_t)((face_count + 1) / 2);
}

static inline void
ToriDraw_SetFacePriority(
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
ToriDraw_CalculateBoundsCylinder(
    struct ToriDraw_BoundsCylinder* bounds_cylinder,
    int num_vertices,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z)
{
    memset(bounds_cylinder, 0, sizeof(struct ToriDraw_BoundsCylinder));

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

static struct ToriDraw_Bones*
ToriDraw_BonesNew(
    const uint8_t* bone_map,
    int bone_count)
{
    struct ToriDraw_Bones* bones = (struct ToriDraw_Bones*)malloc(sizeof(struct ToriDraw_Bones));
    if( !bones )
        return NULL;
    memset(bones, 0, sizeof(struct ToriDraw_Bones));

    struct RSCacheDat2A_ModelBones* model_bones = RSCacheDat2A_ModelBonesNewDecode(bone_map, bone_count);
    if( !model_bones )
    {
        free(bones);
        return NULL;
    }

    bones->bones_count = model_bones->bones_count;
    bones->bones = (boneint_t**)malloc(sizeof(boneint_t*) * (size_t)bones->bones_count);
    bones->bones_sizes = (boneint_t*)malloc(sizeof(boneint_t) * (size_t)bones->bones_count);
    if( !bones->bones || !bones->bones_sizes )
    {
        RSCacheDat2A_ModelBonesFree(model_bones);
        ToriDraw_BonesFree(bones);
        return NULL;
    }
    memset(bones->bones, 0, sizeof(boneint_t*) * (size_t)bones->bones_count);
    memset(bones->bones_sizes, 0, sizeof(boneint_t) * (size_t)bones->bones_count);

    for( int i = 0; i < bones->bones_count; i++ )
        bones->bones_sizes[i] = (boneint_t)model_bones->bones_sizes[i];

    for( int i = 0; i < bones->bones_count; i++ )
    {
        bones->bones[i] =
            (boneint_t*)malloc(sizeof(boneint_t) * (size_t)model_bones->bones_sizes[i]);
        if( !bones->bones[i] )
        {
            RSCacheDat2A_ModelBonesFree(model_bones);
            ToriDraw_BonesFree(bones);
            return NULL;
        }
        for( int j = 0; j < model_bones->bones_sizes[i]; j++ )
            bones->bones[i][j] = (boneint_t)model_bones->bones[i][j];
    }

    RSCacheDat2A_ModelBonesFree(model_bones);
    return bones;
}

static void
ToriDraw_ModelMoveFromCacheModel(
    struct ToriDraw_Model* td,
    struct RSCacheDat2A_Model* model)
{
    assert(td && model);

    if( model->vertex_count > 0 && model->vertices_x && model->vertices_y && model->vertices_z )
    {
        int count = model->vertex_count;
        td->vertex_count = count;
        td->vertices_x = (vertexint_t*)malloc((size_t)count * sizeof(vertexint_t));
        td->vertices_y = (vertexint_t*)malloc((size_t)count * sizeof(vertexint_t));
        td->vertices_z = (vertexint_t*)malloc((size_t)count * sizeof(vertexint_t));
        for( int i = 0; i < count; i++ )
        {
            td->vertices_x[i] = (vertexint_t)model->vertices_x[i];
            td->vertices_y[i] = (vertexint_t)model->vertices_y[i];
            td->vertices_z[i] = (vertexint_t)model->vertices_z[i];
        }
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
        int count = model->face_count;
        td->face_count = count;
        td->face_indices_a = (faceint_t*)malloc((size_t)count * sizeof(faceint_t));
        td->face_indices_b = (faceint_t*)malloc((size_t)count * sizeof(faceint_t));
        td->face_indices_c = (faceint_t*)malloc((size_t)count * sizeof(faceint_t));
        for( int i = 0; i < count; i++ )
        {
            td->face_indices_a[i] = (faceint_t)model->face_indices_a[i];
            td->face_indices_b[i] = (faceint_t)model->face_indices_b[i];
            td->face_indices_c[i] = (faceint_t)model->face_indices_c[i];
        }
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
        td->face_colors = (hsl16_t*)malloc((size_t)fc * sizeof(hsl16_t));
        memcpy(td->face_colors, model->face_colors, (size_t)fc * sizeof(hsl16_t));
        free(model->face_colors);
        model->face_colors = NULL;
    }

    if( fc > 0 )
    {
        td->face_colors_a = (hsl16_t*)calloc((size_t)fc, sizeof(hsl16_t));
        td->face_colors_b = (hsl16_t*)calloc((size_t)fc, sizeof(hsl16_t));
        td->face_colors_c = (hsl16_t*)calloc((size_t)fc, sizeof(hsl16_t));
    }

    if( model->face_alphas && fc > 0 )
    {
        td->face_alphas = (alphaint_t*)malloc((size_t)fc * sizeof(alphaint_t));
        memcpy(td->face_alphas, model->face_alphas, (size_t)fc * sizeof(alphaint_t));
        free(model->face_alphas);
        model->face_alphas = NULL;
    }

    if( model->face_infos && fc > 0 )
    {
        td->face_infos = (int*)malloc((size_t)fc * sizeof(int));
        for( int i = 0; i < fc; i++ )
            td->face_infos[i] = model->face_infos[i];
        free(model->face_infos);
        model->face_infos = NULL;
    }

    if( model->face_priorities && fc > 0 )
    {
        size_t nbytes = ToriDraw_FacePrioritiesByteCount(fc);
        td->face_priorities = (uint8_t*)calloc(nbytes, 1);
        for( int i = 0; i < fc; i++ )
            ToriDraw_SetFacePriority(td->face_priorities, i, model->face_priorities[i]);
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

    td->textured_face_count = tfc;
    if( tfc > 0 && tp && tm && tn )
    {
        td->textured_p_coordinate = tp;
        td->textured_m_coordinate = tm;
        td->textured_n_coordinate = tn;
        tp = tm = tn = NULL;
    }
    if( fc > 0 && ftc_arr )
    {
        td->face_texture_coords = ftc_arr;
        ftc_arr = NULL;
    }

    free(tp);
    free(tm);
    free(tn);
    free(ftc_arr);

    if( fc > 0 && model->face_textures )
    {
        td->face_textures = (faceint_t*)malloc((size_t)fc * sizeof(faceint_t));
        memcpy(td->face_textures, model->face_textures, (size_t)fc * sizeof(faceint_t));
        free(model->face_textures);
        model->face_textures = NULL;
    }

    if( tfc > 0 || had_per_face_tex_coords )
        td->flags |= 0x02u;

    td->normals = NULL;
    td->merged_normals = NULL;

    if( model->vertex_bone_map )
        td->vertex_bones = ToriDraw_BonesNew(model->vertex_bone_map, model->vertex_count);
    if( model->face_bone_map )
        td->face_bones = ToriDraw_BonesNew(model->face_bone_map, model->face_count);

    if( td->vertex_count > 0 && td->vertices_x && td->vertices_y && td->vertices_z )
    {
        td->bounds_cylinder =
            (struct ToriDraw_BoundsCylinder*)malloc(sizeof(struct ToriDraw_BoundsCylinder));
        if( td->bounds_cylinder )
            ToriDraw_CalculateBoundsCylinder(
                td->bounds_cylinder,
                td->vertex_count,
                td->vertices_x,
                td->vertices_y,
                td->vertices_z);
    }

    td->flags |= 0x01u;
}

struct ToriDraw_Model*
ToriDraw_ModelNewFromCacheModel(struct RSCacheDat2A_Model* model)
{
    if( !model )
        return NULL;

    struct ToriDraw_Model* td = (struct ToriDraw_Model*)calloc(1, sizeof(struct ToriDraw_Model));
    if( !td )
        return NULL;

    ToriDraw_ModelMoveFromCacheModel(td, model);
    return td;
}
