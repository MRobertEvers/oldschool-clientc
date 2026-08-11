#include "engine/torirs_model_from_rscache.h"

#include <3rd/toridraw/toridraw.h>

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static inline size_t
torirs_face_priorities_byte_count(int face_count)
{
    return (size_t)((face_count + 1) / 2);
}

static inline void
torirs_set_face_priority(
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
torirs_calculate_bounds_cylinder(
    struct ToriRS_BoundsCylinder* bounds_cylinder,
    int num_vertices,
    gc_vertexint_t* vertex_x,
    gc_vertexint_t* vertex_y,
    gc_vertexint_t* vertex_z)
{
    memset(bounds_cylinder, 0, sizeof(struct ToriRS_BoundsCylinder));

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
torirs_bones_free(struct ToriRS_Bones* bones)
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

static struct ToriRS_Bones*
torirs_bones_new(
    const uint8_t* bone_map,
    int bone_count)
{
    struct ToriRS_Bones* bones = malloc(sizeof(struct ToriRS_Bones));
    assert(bones);
    memset(bones, 0, sizeof(struct ToriRS_Bones));

    struct RSCache_ModelBones* model_bones = RSCache_ModelBonesNewDecode(bone_map, bone_count);
    if( !model_bones )
    {
        free(bones);
        return NULL;
    }

    bones->bones_count = model_bones->bones_count;
    bones->bones = malloc(sizeof(gc_boneint_t*) * (size_t)bones->bones_count);
    bones->bones_sizes = malloc(sizeof(gc_boneint_t) * (size_t)bones->bones_count);
    if( !bones->bones || !bones->bones_sizes )
    {
        RSCache_ModelBonesFree(model_bones);
        torirs_bones_free(bones);
        return NULL;
    }
    memset(bones->bones, 0, sizeof(gc_boneint_t*) * (size_t)bones->bones_count);
    memset(bones->bones_sizes, 0, sizeof(gc_boneint_t) * (size_t)bones->bones_count);

    for( int i = 0; i < bones->bones_count; i++ )
        bones->bones_sizes[i] = (gc_boneint_t)model_bones->bones_sizes[i];

    for( int i = 0; i < bones->bones_count; i++ )
    {
        bones->bones[i] = malloc(sizeof(gc_boneint_t) * (size_t)model_bones->bones_sizes[i]);
        if( !bones->bones[i] )
        {
            RSCache_ModelBonesFree(model_bones);
            torirs_bones_free(bones);
            return NULL;
        }
        for( int j = 0; j < model_bones->bones_sizes[i]; j++ )
            bones->bones[i][j] = (gc_boneint_t)model_bones->bones[i][j];
    }

    RSCache_ModelBonesFree(model_bones);
    return bones;
}

static gc_vertexint_t*
torirs_steal_vertices_int32(
    int* src,
    int count)
{
    assert(src);
    assert(count > 0);

    gc_vertexint_t* dst = malloc((size_t)count * sizeof(gc_vertexint_t));
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
torirs_steal_face_indices_int32(
    int* src,
    int count)
{
    if( !src || count <= 0 )
        return NULL;

    gc_faceint_t* dst = malloc((size_t)count * sizeof(gc_faceint_t));
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
torirs_sanitize_pnm_texture_coords(struct ToriRS_Model* model)
{
    if( !model || !model->face_texture_coords || model->face_count <= 0 )
        return;

    int const vc = model->vertex_count;
    for( int i = 0; i < model->face_count; i++ )
    {
        int texture_face = (int)model->face_texture_coords[i];
        if( texture_face < 0 )
            continue;

        if( texture_face >= model->textured_face_count || !model->textured_p_coordinate ||
            !model->textured_m_coordinate || !model->textured_n_coordinate )
        {
            model->face_texture_coords[i] = -1;
            continue;
        }

        int const p = (int)model->textured_p_coordinate[texture_face];
        int const m = (int)model->textured_m_coordinate[texture_face];
        int const n = (int)model->textured_n_coordinate[texture_face];
        if( p < 0 || p >= vc || m < 0 || m >= vc || n < 0 || n >= vc )
            model->face_texture_coords[i] = -1;
    }
}

static void
torirs_model_move_from_rscache(
    struct ToriRS_Model* dst,
    struct RSCache_Model* src)
{
    assert(dst);
    assert(src);

    if( src->vertex_count > 0 && src->vertices_x && src->vertices_y && src->vertices_z )
    {
        int count = src->vertex_count;
        dst->vertex_count = count;
        dst->vertices_x = torirs_steal_vertices_int32(src->vertices_x, count);
        dst->vertices_y = torirs_steal_vertices_int32(src->vertices_y, count);
        dst->vertices_z = torirs_steal_vertices_int32(src->vertices_z, count);
        src->vertices_x = NULL;
        src->vertices_y = NULL;
        src->vertices_z = NULL;

        /*
         * Version-13+ ob3 models (the 643 era) store vertices at 4x precision;
         * ModelData.decodeV1 ends with `if (version >= 13) scaleDown(2)`. The rscache
         * decoder keeps the raw values so its byte-exact round-trip holds — the shift
         * drops two bits — which makes this adaptor the reference's scaleDown site.
         * Verified vertex-for-vertex against rs-map-viewer on model 1139: without the
         * shift every such model lands 4x too large (single-tile gravel spanning three
         * tiles). >> matches the reference's JS >>, arithmetic on negatives.
         *
         * The reference also shifts the complex-texture scale blocks; nothing downstream
         * of ToriRS reads those, so there is no field to shift here.
         */
        if( src->format_version >= 13 )
        {
            for( int i = 0; i < count; i++ )
            {
                dst->vertices_x[i] >>= 2;
                dst->vertices_y[i] >>= 2;
                dst->vertices_z[i] >>= 2;
            }
        }
    }

    if( src->face_count > 0 && src->face_indices_a && src->face_indices_b && src->face_indices_c )
    {
        int count = src->face_count;
        dst->face_count = count;
        dst->face_indices_a = torirs_steal_face_indices_int32(src->face_indices_a, count);
        dst->face_indices_b = torirs_steal_face_indices_int32(src->face_indices_b, count);
        dst->face_indices_c = torirs_steal_face_indices_int32(src->face_indices_c, count);
        src->face_indices_a = NULL;
        src->face_indices_b = NULL;
        src->face_indices_c = NULL;
    }

    int fc = src->face_count;
    if( src->face_colors && fc > 0 )
    {
        dst->face_colors = (gc_hsl16_t*)src->face_colors;
        src->face_colors = NULL;
    }

    if( fc > 0 )
    {
        dst->face_colors_a = (gc_hsl16_t*)calloc((size_t)fc, sizeof(gc_hsl16_t));
        dst->face_colors_b = (gc_hsl16_t*)calloc((size_t)fc, sizeof(gc_hsl16_t));
        dst->face_colors_c = (gc_hsl16_t*)calloc((size_t)fc, sizeof(gc_hsl16_t));
    }

    if( src->face_alphas && fc > 0 )
    {
        dst->face_alphas = src->face_alphas;
        src->face_alphas = NULL;
    }

    if( src->face_infos && fc > 0 )
    {
        dst->face_infos = malloc((size_t)fc * sizeof(int));
        if( dst->face_infos )
        {
            for( int i = 0; i < fc; i++ )
                dst->face_infos[i] = (int)src->face_infos[i];
        }
        free(src->face_infos);
        src->face_infos = NULL;
    }

    if( src->face_priorities && fc > 0 )
    {
        size_t nbytes = torirs_face_priorities_byte_count(fc);
        dst->face_priorities = calloc(nbytes, 1);
        if( dst->face_priorities )
        {
            for( int i = 0; i < fc; i++ )
                torirs_set_face_priority(dst->face_priorities, i, src->face_priorities[i]);
        }
        free(src->face_priorities);
        src->face_priorities = NULL;
    }

    dst->model_priority = src->model_priority;

    int tfc = src->textured_face_count;
    const bool had_per_face_tex_coords = (fc > 0 && src->face_texture_coords != NULL);
    if( tfc > 0 && src->textured_p_coordinate && src->textured_m_coordinate &&
        src->textured_n_coordinate )
    {
        dst->textured_p_coordinate = (gc_faceint_t*)src->textured_p_coordinate;
        dst->textured_m_coordinate = (gc_faceint_t*)src->textured_m_coordinate;
        dst->textured_n_coordinate = (gc_faceint_t*)src->textured_n_coordinate;
        src->textured_p_coordinate = NULL;
        src->textured_m_coordinate = NULL;
        src->textured_n_coordinate = NULL;
    }

    if( tfc > 0 && src->texture_render_types )
    {
        dst->texture_render_types = src->texture_render_types;
        src->texture_render_types = NULL;
    }

    if( fc > 0 && src->face_texture_coords )
    {
        dst->face_texture_coords = malloc((size_t)fc * sizeof(gc_faceint_t));
        assert(dst->face_texture_coords);
        for( int i = 0; i < fc; i++ )
            dst->face_texture_coords[i] = (gc_faceint_t)ToriDraw_NormalizeFaceTextureCoord(
                (int)src->face_texture_coords[i], tfc);
        free(src->face_texture_coords);
        src->face_texture_coords = NULL;
    }

    dst->textured_face_count = tfc;

    if( fc > 0 && src->face_textures )
    {
        dst->face_textures = (gc_faceint_t*)src->face_textures;
        src->face_textures = NULL;
    }

    if( tfc > 0 || had_per_face_tex_coords )
        dst->flags |= 0x02u;

    if( src->vertex_bone_map )
        dst->vertex_bones = torirs_bones_new(src->vertex_bone_map, src->vertex_count);
    if( src->face_bone_map )
        dst->face_bones = torirs_bones_new(src->face_bone_map, src->face_count);

    if( src->animaya_vertex_count > 0 && src->animaya_group_counts && src->animaya_groups &&
        src->animaya_scales )
    {
        int vc = src->animaya_vertex_count;
        struct ToriRS_AnimayaSkin* skin = calloc(1, sizeof(struct ToriRS_AnimayaSkin));
        if( skin )
        {
            skin->vertex_count = vc;
            skin->group_counts = malloc((size_t)vc);
            skin->groups = calloc((size_t)vc, sizeof(uint8_t*));
            skin->scales = calloc((size_t)vc, sizeof(uint8_t*));
            if( skin->group_counts && skin->groups && skin->scales )
            {
                memcpy(skin->group_counts, src->animaya_group_counts, (size_t)vc);
                for( int i = 0; i < vc; i++ )
                {
                    int cnt = skin->group_counts[i];
                    if( cnt <= 0 )
                        continue;
                    skin->groups[i] = malloc((size_t)cnt);
                    skin->scales[i] = malloc((size_t)cnt);
                    if( skin->groups[i] && src->animaya_groups[i] )
                        memcpy(skin->groups[i], src->animaya_groups[i], (size_t)cnt);
                    if( skin->scales[i] && src->animaya_scales[i] )
                        memcpy(skin->scales[i], src->animaya_scales[i], (size_t)cnt);
                }
                dst->animaya_skin = skin;
            }
            else
            {
                ToriRS_AnimayaSkinFree(skin);
            }
        }
    }

    if( dst->vertex_count > 0 && dst->vertices_x && dst->vertices_y && dst->vertices_z )
    {
        dst->bounds_cylinder = malloc(sizeof(struct ToriRS_BoundsCylinder));
        if( dst->bounds_cylinder )
            torirs_calculate_bounds_cylinder(
                dst->bounds_cylinder,
                dst->vertex_count,
                dst->vertices_x,
                dst->vertices_y,
                dst->vertices_z);
    }

    dst->flags |= 0x01u;
    torirs_sanitize_pnm_texture_coords(dst);
    ToriRS_ModelAssertPnmTextureInvariant(dst);
}

struct ToriRS_Model*
ToriRS_ModelFromRSCache(struct RSCache_Model* src)
{
    assert(src);

    struct ToriRS_Model* dst = calloc(1, sizeof(struct ToriRS_Model));
    assert(dst);

    torirs_model_move_from_rscache(dst, src);
    return dst;
}
