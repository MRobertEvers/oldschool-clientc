#include "toridraw_model_transform.h"

#include "toridraw_model.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static size_t
ToriDraw_FacePrioritiesByteCount(int face_count)
{
    return (size_t)((face_count + 1) / 2);
}

static void
ToriDraw_SetFacePriority(
    uint8_t* packed,
    int index,
    int value)
{
    int byte_idx = index >> 1;
    if( index & 1 )
        packed[byte_idx] = (uint8_t)((packed[byte_idx] & 0x0Fu) | (uint8_t)(value << 4));
    else
        packed[byte_idx] = (uint8_t)((packed[byte_idx] & 0xF0u) | (uint8_t)(value & 0x0F));
}

static int
ToriDraw_GetFacePriority(
    const uint8_t* packed,
    int index)
{
    uint8_t byte = packed[index >> 1];
    return (index & 1) ? (int)(byte >> 4) : (int)(byte & 0x0Fu);
}

static struct ToriDraw_Bones*
ToriDraw_BonesMerge(
    struct ToriDraw_Model** models,
    const int* index_offsets,
    int model_count,
    bool vertex)
{
    int max_bones = 0;
    for( int i = 0; i < model_count; i++ )
    {
        struct ToriDraw_Model* m = models[i];
        struct ToriDraw_Bones* bones = vertex ? m->vertex_bones : m->face_bones;
        if( bones && bones->bones_count > max_bones )
            max_bones = bones->bones_count;
    }

    if( max_bones <= 0 )
        return NULL;

    boneint_t* group_sizes = calloc((size_t)max_bones, sizeof(boneint_t));
    if( !group_sizes )
        return NULL;

    for( int i = 0; i < model_count; i++ )
    {
        struct ToriDraw_Model* m = models[i];
        struct ToriDraw_Bones* bones = vertex ? m->vertex_bones : m->face_bones;
        if( !bones )
            continue;

        for( int g = 0; g < bones->bones_count; g++ )
            group_sizes[g] += bones->bones_sizes[g];
    }

    struct ToriDraw_Bones* out = calloc(1, sizeof(struct ToriDraw_Bones));
    if( !out )
    {
        free(group_sizes);
        return NULL;
    }

    out->bones_count = max_bones;
    out->bones = calloc((size_t)max_bones, sizeof(boneint_t*));
    out->bones_sizes = malloc((size_t)max_bones * sizeof(boneint_t));
    if( !out->bones || !out->bones_sizes )
    {
        free(group_sizes);
        ToriDraw_BonesFree(out);
        return NULL;
    }

    memcpy(out->bones_sizes, group_sizes, (size_t)max_bones * sizeof(boneint_t));

    for( int g = 0; g < max_bones; g++ )
    {
        int const group_size = (int)group_sizes[g];
        if( group_size <= 0 )
            continue;

        out->bones[g] = malloc((size_t)group_size * sizeof(boneint_t));
        if( !out->bones[g] )
        {
            free(group_sizes);
            ToriDraw_BonesFree(out);
            return NULL;
        }

        int write_pos = 0;
        for( int i = 0; i < model_count; i++ )
        {
            struct ToriDraw_Model* m = models[i];
            struct ToriDraw_Bones* bones = vertex ? m->vertex_bones : m->face_bones;
            int const index_offset = index_offsets[i];
            if( !bones || g >= bones->bones_count || !bones->bones[g] )
                continue;

            int const bone_length = (int)bones->bones_sizes[g];
            for( int j = 0; j < bone_length; j++ )
                out->bones[g][write_pos++] = (boneint_t)((int)bones->bones[g][j] + index_offset);
        }
    }

    free(group_sizes);
    return out;
}

struct ToriDraw_Model*
ToriDraw_ModelCopy(struct ToriDraw_Model* src)
{
    if( !src )
        return NULL;

    struct ToriDraw_Model* dst = calloc(1, sizeof(struct ToriDraw_Model));
    assert(dst && "ToriDraw_ModelCopy: failed to allocate destination model");

    dst->flags = src->flags;
    dst->vertex_count = src->vertex_count;
    dst->face_count = src->face_count;
    dst->textured_face_count = src->textured_face_count;

    if( src->vertex_count > 0 )
    {
        dst->vertices_x = (vertexint_t*)malloc((size_t)src->vertex_count * sizeof(vertexint_t));
        dst->vertices_y = (vertexint_t*)malloc((size_t)src->vertex_count * sizeof(vertexint_t));
        dst->vertices_z = (vertexint_t*)malloc((size_t)src->vertex_count * sizeof(vertexint_t));
        memcpy(dst->vertices_x, src->vertices_x, (size_t)src->vertex_count * sizeof(vertexint_t));
        memcpy(dst->vertices_y, src->vertices_y, (size_t)src->vertex_count * sizeof(vertexint_t));
        memcpy(dst->vertices_z, src->vertices_z, (size_t)src->vertex_count * sizeof(vertexint_t));
    }

#define COPY_ARRAY(DST, SRC, COUNT, TYPE)                                                          \
    if( (SRC) && (COUNT) > 0 )                                                                     \
    {                                                                                              \
        (DST) = (TYPE*)malloc((size_t)(COUNT) * sizeof(TYPE));                                     \
        memcpy((DST), (SRC), (size_t)(COUNT) * sizeof(TYPE));                                      \
    }

    COPY_ARRAY(dst->face_indices_a, src->face_indices_a, src->face_count, faceint_t);
    COPY_ARRAY(dst->face_indices_b, src->face_indices_b, src->face_count, faceint_t);
    COPY_ARRAY(dst->face_indices_c, src->face_indices_c, src->face_count, faceint_t);
    COPY_ARRAY(dst->face_colors_a, src->face_colors_a, src->face_count, hsl16_t);
    COPY_ARRAY(dst->face_colors_b, src->face_colors_b, src->face_count, hsl16_t);
    COPY_ARRAY(dst->face_colors_c, src->face_colors_c, src->face_count, hsl16_t);
    COPY_ARRAY(dst->face_colors, src->face_colors, src->face_count, hsl16_t);
    COPY_ARRAY(dst->face_textures, src->face_textures, src->face_count, faceint_t);
    COPY_ARRAY(dst->face_alphas, src->face_alphas, src->face_count, alphaint_t);
    COPY_ARRAY(dst->face_texture_coords, src->face_texture_coords, src->face_count, faceint_t);
    COPY_ARRAY(
        dst->textured_p_coordinate,
        src->textured_p_coordinate,
        src->textured_face_count,
        faceint_t);
    COPY_ARRAY(
        dst->textured_m_coordinate,
        src->textured_m_coordinate,
        src->textured_face_count,
        faceint_t);
    COPY_ARRAY(
        dst->textured_n_coordinate,
        src->textured_n_coordinate,
        src->textured_face_count,
        faceint_t);

    if( src->face_infos && src->face_count > 0 )
    {
        dst->face_infos = (int*)malloc((size_t)src->face_count * sizeof(int));
        memcpy(dst->face_infos, src->face_infos, (size_t)src->face_count * sizeof(int));
    }

    if( src->face_priorities && src->face_count > 0 )
    {
        size_t nbytes = ToriDraw_FacePrioritiesByteCount(src->face_count);
        dst->face_priorities = (uint8_t*)malloc(nbytes);
        memcpy(dst->face_priorities, src->face_priorities, nbytes);
    }

    dst->vertex_bones = ToriDraw_BonesCopy(src->vertex_bones);
    dst->face_bones = ToriDraw_BonesCopy(src->face_bones);

    ToriDraw_ModelSetBoundsCylinder(dst);
    return dst;
}

struct ToriDraw_Model*
ToriDraw_ModelMerge(
    struct ToriDraw_Model** models,
    int model_count)
{
    if( !models || model_count <= 0 )
        return NULL;
    if( model_count == 1 )
        return ToriDraw_ModelCopy(models[0]);

    int total_vertices = 0;
    int total_faces = 0;
    int total_textured_faces = 0;
    bool has_face_colors = false;
    bool has_face_textures = false;
    bool has_face_alphas = false;
    bool has_face_infos = false;
    bool has_face_priorities = false;
    bool has_tex_coords = false;
    bool has_textured_coords = false;

    for( int i = 0; i < model_count; i++ )
    {
        struct ToriDraw_Model* m = models[i];
        if( !m )
            continue;
        total_vertices += m->vertex_count;
        total_faces += m->face_count;
        total_textured_faces += m->textured_face_count;
        if( m->face_colors || m->face_colors_a )
            has_face_colors = true;
        if( m->face_textures )
            has_face_textures = true;
        if( m->face_alphas )
            has_face_alphas = true;
        if( m->face_infos )
            has_face_infos = true;
        if( m->face_priorities )
            has_face_priorities = true;
        if( m->face_texture_coords )
            has_tex_coords = true;
        if( m->textured_p_coordinate )
            has_textured_coords = true;
    }

    struct ToriDraw_Model* out = calloc(1, sizeof(struct ToriDraw_Model));
    if( !out )
        return NULL;

    out->vertex_count = total_vertices;
    out->face_count = total_faces;
    out->textured_face_count = total_textured_faces;

    if( total_vertices > 0 )
    {
        out->vertices_x = (vertexint_t*)malloc((size_t)total_vertices * sizeof(vertexint_t));
        out->vertices_y = (vertexint_t*)malloc((size_t)total_vertices * sizeof(vertexint_t));
        out->vertices_z = (vertexint_t*)malloc((size_t)total_vertices * sizeof(vertexint_t));
    }

    if( total_faces > 0 )
    {
        out->face_indices_a = (faceint_t*)malloc((size_t)total_faces * sizeof(faceint_t));
        out->face_indices_b = (faceint_t*)malloc((size_t)total_faces * sizeof(faceint_t));
        out->face_indices_c = (faceint_t*)malloc((size_t)total_faces * sizeof(faceint_t));
        if( has_face_colors )
        {
            out->face_colors_a = (hsl16_t*)calloc((size_t)total_faces, sizeof(hsl16_t));
            out->face_colors_b = (hsl16_t*)calloc((size_t)total_faces, sizeof(hsl16_t));
            out->face_colors_c = (hsl16_t*)calloc((size_t)total_faces, sizeof(hsl16_t));
            out->face_colors = (hsl16_t*)calloc((size_t)total_faces, sizeof(hsl16_t));
        }
        if( has_face_textures )
        {
            out->face_textures = (faceint_t*)malloc((size_t)total_faces * sizeof(faceint_t));
            if( out->face_textures )
            {
                for( int fi = 0; fi < total_faces; fi++ )
                    out->face_textures[fi] = (faceint_t)-1;
            }
        }
        if( has_face_alphas )
            out->face_alphas = (alphaint_t*)malloc((size_t)total_faces * sizeof(alphaint_t));
        if( has_face_infos )
            out->face_infos = (int*)calloc((size_t)total_faces, sizeof(int));
        if( has_face_priorities )
            out->face_priorities =
                (uint8_t*)calloc(ToriDraw_FacePrioritiesByteCount(total_faces), 1);
        if( has_tex_coords )
            out->face_texture_coords = (faceint_t*)malloc((size_t)total_faces * sizeof(faceint_t));
    }

    if( total_textured_faces > 0 && has_textured_coords )
    {
        out->textured_p_coordinate =
            (faceint_t*)malloc((size_t)total_textured_faces * sizeof(faceint_t));
        out->textured_m_coordinate =
            (faceint_t*)malloc((size_t)total_textured_faces * sizeof(faceint_t));
        out->textured_n_coordinate =
            (faceint_t*)malloc((size_t)total_textured_faces * sizeof(faceint_t));
    }

    int vtx_off = 0;
    int face_off = 0;
    int tex_face_off = 0;

    int* vertex_offsets = calloc((size_t)model_count, sizeof(int));
    int* face_offsets = calloc((size_t)model_count, sizeof(int));
    if( !vertex_offsets || !face_offsets )
    {
        free(vertex_offsets);
        free(face_offsets);
        ToriDraw_ModelFree(out);
        return NULL;
    }

    for( int i = 0; i < model_count; i++ )
    {
        struct ToriDraw_Model* m = models[i];
        if( !m )
            continue;

        vertex_offsets[i] = vtx_off;
        face_offsets[i] = face_off;

        for( int v = 0; v < m->vertex_count; v++ )
        {
            out->vertices_x[vtx_off + v] = m->vertices_x[v];
            out->vertices_y[vtx_off + v] = m->vertices_y[v];
            out->vertices_z[vtx_off + v] = m->vertices_z[v];
        }

        for( int f = 0; f < m->face_count; f++ )
        {
            int dst = face_off + f;
            out->face_indices_a[dst] = (faceint_t)(m->face_indices_a[f] + vtx_off);
            out->face_indices_b[dst] = (faceint_t)(m->face_indices_b[f] + vtx_off);
            out->face_indices_c[dst] = (faceint_t)(m->face_indices_c[f] + vtx_off);

            if( out->face_colors && m->face_colors )
                out->face_colors[dst] = m->face_colors[f];
            if( out->face_colors_a && m->face_colors_a )
                out->face_colors_a[dst] = m->face_colors_a[f];
            if( out->face_colors_b && m->face_colors_b )
                out->face_colors_b[dst] = m->face_colors_b[f];
            if( out->face_colors_c && m->face_colors_c )
                out->face_colors_c[dst] = m->face_colors_c[f];
            if( out->face_textures && m->face_textures )
                out->face_textures[dst] = m->face_textures[f];
            if( out->face_alphas && m->face_alphas )
                out->face_alphas[dst] = m->face_alphas[f];
            if( out->face_infos && m->face_infos )
                out->face_infos[dst] = m->face_infos[f];
            if( out->face_priorities && m->face_priorities )
                ToriDraw_SetFacePriority(
                    out->face_priorities, dst, ToriDraw_GetFacePriority(m->face_priorities, f));
            if( out->face_texture_coords && m->face_texture_coords )
                out->face_texture_coords[dst] = m->face_texture_coords[f];
        }

        for( int t = 0; t < m->textured_face_count; t++ )
        {
            int dst = tex_face_off + t;
            if( out->textured_p_coordinate )
                out->textured_p_coordinate[dst] =
                    (faceint_t)(m->textured_p_coordinate[t] + vtx_off);
            if( out->textured_m_coordinate )
                out->textured_m_coordinate[dst] =
                    (faceint_t)(m->textured_m_coordinate[t] + vtx_off);
            if( out->textured_n_coordinate )
                out->textured_n_coordinate[dst] =
                    (faceint_t)(m->textured_n_coordinate[t] + vtx_off);
        }

        vtx_off += m->vertex_count;
        face_off += m->face_count;
        tex_face_off += m->textured_face_count;
    }

    out->vertex_bones = ToriDraw_BonesMerge(models, vertex_offsets, model_count, true);
    out->face_bones = ToriDraw_BonesMerge(models, face_offsets, model_count, false);

    free(vertex_offsets);
    free(face_offsets);

    ToriDraw_ModelSetBoundsCylinder(out);
    return out;
}

static void
ToriDraw_ModelRecolorHslArray(
    hsl16_t* colors,
    int count,
    int color_src,
    int color_dst)
{
    if( !colors )
        return;
    for( int i = 0; i < count; i++ )
    {
        if( colors[i] == (hsl16_t)color_src )
            colors[i] = (hsl16_t)color_dst;
    }
}

void
ToriDraw_ModelRecolor(
    struct ToriDraw_Model* model,
    int color_src,
    int color_dst)
{
    if( !model )
        return;
    ToriDraw_ModelRecolorHslArray(model->face_colors, model->face_count, color_src, color_dst);
    ToriDraw_ModelRecolorHslArray(model->face_colors_a, model->face_count, color_src, color_dst);
    ToriDraw_ModelRecolorHslArray(model->face_colors_b, model->face_count, color_src, color_dst);
    ToriDraw_ModelRecolorHslArray(model->face_colors_c, model->face_count, color_src, color_dst);
}

void
ToriDraw_ModelRetexture(
    struct ToriDraw_Model* model,
    int texture_src,
    int texture_dst)
{
    if( !model || !model->face_textures )
        return;
    for( int i = 0; i < model->face_count; i++ )
    {
        if( model->face_textures[i] == texture_src )
            model->face_textures[i] = (faceint_t)texture_dst;
    }
}

void
ToriDraw_ModelMirror(struct ToriDraw_Model* model)
{
    if( !model )
        return;
    for( int v = 0; v < model->vertex_count; v++ )
        model->vertices_z[v] = (vertexint_t)(-model->vertices_z[v]);
    for( int f = 0; f < model->face_count; f++ )
    {
        faceint_t tmp = model->face_indices_a[f];
        model->face_indices_a[f] = model->face_indices_c[f];
        model->face_indices_c[f] = tmp;
    }
}

void
ToriDraw_ModelOrient(
    struct ToriDraw_Model* model,
    int orientation)
{
    if( !model )
        return;
    orientation &= 3;
    while( orientation-- > 0 )
    {
        for( int v = 0; v < model->vertex_count; v++ )
        {
            vertexint_t tmp = model->vertices_x[v];
            model->vertices_x[v] = model->vertices_z[v];
            model->vertices_z[v] = (vertexint_t)(-tmp);
        }
    }
}

void
ToriDraw_ModelScale(
    struct ToriDraw_Model* model,
    int x,
    int z,
    int height)
{
    if( !model )
        return;
    for( int i = 0; i < model->vertex_count; i++ )
    {
        model->vertices_x[i] = (vertexint_t)((int)model->vertices_x[i] * x / 128);
        model->vertices_y[i] = (vertexint_t)((int)model->vertices_y[i] * height / 128);
        model->vertices_z[i] = (vertexint_t)((int)model->vertices_z[i] * z / 128);
    }
}

void
ToriDraw_ModelTranslate(
    struct ToriDraw_Model* model,
    int x,
    int y,
    int z)
{
    if( !model )
        return;
    for( int i = 0; i < model->vertex_count; i++ )
    {
        model->vertices_x[i] = (vertexint_t)((int)model->vertices_x[i] + x);
        model->vertices_y[i] = (vertexint_t)((int)model->vertices_y[i] + y);
        model->vertices_z[i] = (vertexint_t)((int)model->vertices_z[i] + z);
    }
}

void
ToriDraw_ModelSetBoundsCylinder(struct ToriDraw_Model* model)
{
    assert(model && "ToriDraw_ModelSetBoundsCylinder: model is NULL");
    if( !model->bounds_cylinder )
        model->bounds_cylinder = calloc(1, sizeof(struct ToriDraw_BoundsCylinder));
    assert(
        model->bounds_cylinder &&
        "ToriDraw_ModelSetBoundsCylinder: failed to allocate bounds cylinder");

    if( model->vertex_count <= 0 )
    {
        memset(model->bounds_cylinder, 0, sizeof(struct ToriDraw_BoundsCylinder));
        return;
    }

    int min_y = INT_MAX;
    int max_y = INT_MIN;
    int radius_squared = 0;

    for( int i = 0; i < model->vertex_count; i++ )
    {
        int x = (int)model->vertices_x[i];
        int y = (int)model->vertices_y[i];
        int z = (int)model->vertices_z[i];
        if( y < min_y )
            min_y = y;
        if( y > max_y )
            max_y = y;
        int rs = x * x + z * z;
        if( rs > radius_squared )
            radius_squared = rs;
    }

    int center_to_bottom_edge = (int)sqrt((double)radius_squared + (double)min_y * min_y) + 1;
    int center_to_top_edge = (int)sqrt((double)radius_squared + (double)max_y * max_y) + 1;
    model->bounds_cylinder->center_to_bottom_edge = center_to_bottom_edge;
    model->bounds_cylinder->center_to_top_edge = center_to_top_edge;
    model->bounds_cylinder->min_y = min_y;
    model->bounds_cylinder->max_y = max_y;
    model->bounds_cylinder->radius = (int)sqrt((double)radius_squared);
    model->bounds_cylinder->min_z_depth_any_rotation =
        center_to_top_edge > center_to_bottom_edge ? center_to_top_edge : center_to_bottom_edge;
}
