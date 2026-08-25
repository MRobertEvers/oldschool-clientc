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
        assert(m && "ToriDraw_BonesMerge: model is NULL");
        struct ToriDraw_Bones* bones;
        bones = vertex ? m->vertex_bones : m->face_bones;
        if( bones && bones->bones_count > max_bones )
            max_bones = bones->bones_count;
    }

    if( max_bones <= 0 )
        return NULL;

    int* group_sizes = calloc((size_t)max_bones, sizeof(int));
    assert(group_sizes);

    for( int i = 0; i < model_count; i++ )
    {
        struct ToriDraw_Model* m = models[i];
        struct ToriDraw_Bones* bones;
        if( !m )
            continue;
        bones = vertex ? m->vertex_bones : m->face_bones;
        if( !bones )
            continue;

        for( int g = 0; g < bones->bones_count; g++ )
            group_sizes[g] += (int)bones->bones_sizes[g];
    }

    struct ToriDraw_Bones* out = calloc(1, sizeof(struct ToriDraw_Bones));
    assert(out);

    out->bones_count = max_bones;
    out->bones = calloc((size_t)max_bones, sizeof(boneint_t*));
    out->bones_sizes = malloc((size_t)max_bones * sizeof(boneint_t));
    assert(out->bones);
    assert(out->bones_sizes);

    for( int g = 0; g < max_bones; g++ )
    {
        int group_size = group_sizes[g];
        if( group_size > (int)UINT16_MAX )
            group_size = (int)UINT16_MAX;
        out->bones_sizes[g] = group_size <= 0 ? 0 : (boneint_t)group_size;
        if( group_size <= 0 )
            continue;

        out->bones[g] = malloc((size_t)group_size * sizeof(boneint_t));
        assert(out->bones[g]);

        int write_pos = 0;
        for( int i = 0; i < model_count; i++ )
        {
            struct ToriDraw_Model* m = models[i];
            struct ToriDraw_Bones* bones;
            int index_offset;
            if( !m )
                continue;
            bones = vertex ? m->vertex_bones : m->face_bones;
            index_offset = index_offsets[i];
            if( !bones || g >= bones->bones_count || !bones->bones[g] )
                continue;

            int const bone_length = (int)bones->bones_sizes[g];
            for( int j = 0; j < bone_length; j++ )
            {
                if( write_pos >= group_size )
                    break;
                out->bones[g][write_pos++] = (boneint_t)((int)bones->bones[g][j] + index_offset);
            }
        }
    }

    free(group_sizes);
    return out;
}

static void
ToriDraw_ModelMoveArrays(
    struct ToriDraw_Model* dst,
    struct ToriDraw_Model* src)
{
    dst->flags = src->flags;
    dst->vertex_count = src->vertex_count;
    dst->face_count = src->face_count;
    dst->textured_face_count = src->textured_face_count;
    dst->model_priority = src->model_priority;
    dst->post_transform = src->post_transform;
    dst->post_resize = src->post_resize;
    dst->post_resize_x = src->post_resize_x;
    dst->post_resize_z = src->post_resize_z;
    dst->post_resize_height = src->post_resize_height;
    dst->post_orient = src->post_orient;
    dst->post_offset_x = src->post_offset_x;
    dst->post_offset_y = src->post_offset_y;
    dst->post_offset_z = src->post_offset_z;

#define MODEL_MOVE(field) TORIDRAW_MODEL_MOVE(dst, field, src->field)

    MODEL_MOVE(vertices_x);
    MODEL_MOVE(vertices_y);
    MODEL_MOVE(vertices_z);
    MODEL_MOVE(face_indices_a);
    MODEL_MOVE(face_indices_b);
    MODEL_MOVE(face_indices_c);
    MODEL_MOVE(face_colors_a);
    MODEL_MOVE(face_colors_b);
    MODEL_MOVE(face_colors_c);
    MODEL_MOVE(face_colors);
    MODEL_MOVE(face_textures);
    MODEL_MOVE(face_alphas);
    MODEL_MOVE(face_infos);
    MODEL_MOVE(face_priorities);
    MODEL_MOVE(face_texture_coords);
    MODEL_MOVE(textured_p_coordinate);
    MODEL_MOVE(textured_m_coordinate);
    MODEL_MOVE(textured_n_coordinate);
    MODEL_MOVE(texture_render_types);
    MODEL_MOVE(original_vertices_x);
    MODEL_MOVE(original_vertices_y);
    MODEL_MOVE(original_vertices_z);
    MODEL_MOVE(normals);
    MODEL_MOVE(merged_normals);
    MODEL_MOVE(vertex_bones);
    MODEL_MOVE(face_bones);
    MODEL_MOVE(bounds_cylinder);
    MODEL_MOVE(animaya_group_counts);
    MODEL_MOVE(animaya_groups);
    MODEL_MOVE(animaya_scales);

#undef MODEL_MOVE

    dst->animaya_vertex_count = src->animaya_vertex_count;
    src->animaya_vertex_count = 0;
}

struct ToriDraw_Model*
ToriDraw_ModelSteal(struct ToriDraw_Model* src)
{
    assert(src);

    struct ToriDraw_Model* dst = calloc(1, sizeof(struct ToriDraw_Model));
    assert(dst);

    ToriDraw_ModelMoveArrays(dst, src);
    return dst;
}

static size_t
model_bones_bytes(const struct ToriDraw_Bones* bones)
{
    size_t total;
    int i;

    if( !bones )
        return 0;

    total = sizeof(*bones);
    total += (size_t)bones->bones_count * sizeof(boneint_t*);
    total += (size_t)bones->bones_count * sizeof(boneint_t);
    for( i = 0; i < bones->bones_count; i++ )
        total += (size_t)bones->bones_sizes[i] * sizeof(boneint_t);

    return total;
}

static size_t
model_normals_bytes(const struct ToriDraw_Normals* normals)
{
    size_t total;

    if( !normals )
        return 0;

    /* Capacity, not count: a recycled block keeps the larger allocation. */
    total = sizeof(*normals);
    total += (size_t)normals->vertex_normals_cap * sizeof(struct ToriDraw_Normal);
    total += (size_t)normals->face_normals_cap * sizeof(struct ToriDraw_Normal);

    return total;
}

size_t
ToriDraw_ModelHeapBytes(const struct ToriDraw_Model* model)
{
    size_t total;
    int i;

    assert(model);

    total = sizeof(*model);

#define BYTES_IF(FIELD, COUNT, TYPE)                                                               \
    if( (model->FIELD) )                                                                           \
        total += (size_t)(COUNT) * sizeof(TYPE);

    BYTES_IF(vertices_x, model->vertex_count, vertexint_t)
    BYTES_IF(vertices_y, model->vertex_count, vertexint_t)
    BYTES_IF(vertices_z, model->vertex_count, vertexint_t)
    BYTES_IF(original_vertices_x, model->vertex_count, vertexint_t)
    BYTES_IF(original_vertices_y, model->vertex_count, vertexint_t)
    BYTES_IF(original_vertices_z, model->vertex_count, vertexint_t)

    BYTES_IF(face_indices_a, model->face_count, faceint_t)
    BYTES_IF(face_indices_b, model->face_count, faceint_t)
    BYTES_IF(face_indices_c, model->face_count, faceint_t)
    BYTES_IF(face_colors_a, model->face_count, hsl16_t)
    BYTES_IF(face_colors_b, model->face_count, hsl16_t)
    BYTES_IF(face_colors_c, model->face_count, hsl16_t)
    BYTES_IF(face_colors, model->face_count, hsl16_t)
    BYTES_IF(face_textures, model->face_count, faceint_t)
    BYTES_IF(face_alphas, model->face_count, alphaint_t)
    BYTES_IF(original_face_alphas, model->face_count, alphaint_t)
    BYTES_IF(face_infos, model->face_count, int)
    BYTES_IF(face_texture_coords, model->face_count, faceint_t)

    BYTES_IF(textured_p_coordinate, model->textured_face_count, faceint_t)
    BYTES_IF(textured_m_coordinate, model->textured_face_count, faceint_t)
    BYTES_IF(textured_n_coordinate, model->textured_face_count, faceint_t)
    BYTES_IF(texture_render_types, model->textured_face_count, uint8_t)

#undef BYTES_IF

    if( model->face_priorities )
        total += ToriDraw_FacePrioritiesByteCount(model->face_count);

    if( model->bounds_cylinder )
        total += sizeof(*model->bounds_cylinder);

    total += model_normals_bytes(model->normals);
    total += model_normals_bytes(model->merged_normals);
    total += model_bones_bytes(model->vertex_bones);
    total += model_bones_bytes(model->face_bones);

    if( model->animaya_vertex_count > 0 && model->animaya_group_counts )
    {
        total += (size_t)model->animaya_vertex_count;
        total += (size_t)model->animaya_vertex_count * sizeof(uint8_t*) * 2u;
        for( i = 0; i < model->animaya_vertex_count; i++ )
            total += (size_t)model->animaya_group_counts[i] * 2u;
    }

    return total;
}

struct ToriDraw_Model*
ToriDraw_ModelCopy(struct ToriDraw_Model* src)
{
    assert(src);

    struct ToriDraw_Model* dst = calloc(1, sizeof(struct ToriDraw_Model));
    assert(dst && "ToriDraw_ModelCopy: failed to allocate destination model");

    dst->flags = src->flags;
    dst->vertex_count = src->vertex_count;
    dst->face_count = src->face_count;
    dst->textured_face_count = src->textured_face_count;
    /* Travels with the bind pose below, and for the same reason: a copy whose
     * originals are the authored geometry but whose placement record is empty
     * poses itself back to unplaced on the next frame. */
    dst->post_transform = src->post_transform;
    dst->post_resize = src->post_resize;
    dst->post_resize_x = src->post_resize_x;
    dst->post_resize_z = src->post_resize_z;
    dst->post_resize_height = src->post_resize_height;
    dst->post_orient = src->post_orient;
    dst->post_offset_x = src->post_offset_x;
    dst->post_offset_y = src->post_offset_y;
    dst->post_offset_z = src->post_offset_z;

    if( src->vertex_count > 0 )
    {
        dst->vertices_x = (vertexint_t*)malloc((size_t)src->vertex_count * sizeof(vertexint_t));
        dst->vertices_y = (vertexint_t*)malloc((size_t)src->vertex_count * sizeof(vertexint_t));
        dst->vertices_z = (vertexint_t*)malloc((size_t)src->vertex_count * sizeof(vertexint_t));
        memcpy(dst->vertices_x, src->vertices_x, (size_t)src->vertex_count * sizeof(vertexint_t));
        memcpy(dst->vertices_y, src->vertices_y, (size_t)src->vertex_count * sizeof(vertexint_t));
        memcpy(dst->vertices_z, src->vertices_z, (size_t)src->vertex_count * sizeof(vertexint_t));

        /*
         * The captured bind pose travels with the copy.
         *
         * ToriDraw_ModelAnimateReset is gated on original_vertices_x: with no
         * originals it RETURNS instead of restoring, so every keyframe composes
         * with the previous frame's output and the model inflates without bound
         * -- it does not animate, it accumulates. Leaving these out therefore
         * does not produce a model that animates from its current pose, it
         * produces one that cannot animate at all, and nothing reports it.
         *
         * That is not hypothetical: npc models come from
         * TorirsModelInstCache_CopyGet, so every cache hit was handing the scene
         * a model with no bind pose. It stayed hidden because
         * ToriDraw_SceneElementSetAnimationSeq captures on the way past, which
         * covers the ordinary spawn-then-animate order. The Queen Black Dragon's
         * artefact restore does `npc_changetype` and `npc_anim` on ONE tick
         * (rs2012_qbd_session.rs2) and re-binds the sequence she is already
         * playing, so the model was swapped underneath a live animation with no
         * SetAnimationSeq behind it -- and her pose compounded 3076 -> 11952 ->
         * 32839 -> saturation, every cycle.
         */
        if( src->original_vertices_x && src->original_vertices_y && src->original_vertices_z )
        {
            size_t const nbytes = (size_t)src->vertex_count * sizeof(vertexint_t);
            dst->original_vertices_x = (vertexint_t*)malloc(nbytes);
            dst->original_vertices_y = (vertexint_t*)malloc(nbytes);
            dst->original_vertices_z = (vertexint_t*)malloc(nbytes);
            /* All three or none: the reset gates on x alone and would memcpy
             * from a NULL y/z. */
            assert(dst->original_vertices_x);
            assert(dst->original_vertices_y);
            assert(dst->original_vertices_z);
            memcpy(dst->original_vertices_x, src->original_vertices_x, nbytes);
            memcpy(dst->original_vertices_y, src->original_vertices_y, nbytes);
            memcpy(dst->original_vertices_z, src->original_vertices_z, nbytes);
        }
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
    /* The alpha half of the bind pose, for the same reason as the vertices:
     * AnimateReset restores it alongside them, and an animation that fades a
     * model would otherwise keep fading from wherever it left off. */
    COPY_ARRAY(
        dst->original_face_alphas, src->original_face_alphas, src->face_count, alphaint_t);
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
    COPY_ARRAY(
        dst->texture_render_types,
        src->texture_render_types,
        src->textured_face_count,
        uint8_t);

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

    dst->model_priority = src->model_priority;

    /* Most models are unrigged; having bones is the condition, and it is
     * decided here rather than inside the copy. */
    if( src->vertex_bones )
        dst->vertex_bones = ToriDraw_BonesCopy(src->vertex_bones);
    if( src->face_bones )
        dst->face_bones = ToriDraw_BonesCopy(src->face_bones);

    if( src->animaya_vertex_count > 0 && src->animaya_group_counts &&
        src->animaya_groups && src->animaya_scales )
    {
        int vc = src->animaya_vertex_count;
        dst->animaya_vertex_count = vc;
        dst->animaya_group_counts = (uint8_t*)malloc((size_t)vc);
        dst->animaya_groups = (uint8_t**)calloc((size_t)vc, sizeof(uint8_t*));
        dst->animaya_scales = (uint8_t**)calloc((size_t)vc, sizeof(uint8_t*));
        assert(dst->animaya_group_counts);
        assert(dst->animaya_groups);
        assert(dst->animaya_scales);

        memcpy(dst->animaya_group_counts, src->animaya_group_counts, (size_t)vc);
        for( int i = 0; i < vc; i++ )
        {
            int cnt = (int)dst->animaya_group_counts[i];
            if( cnt <= 0 )
                continue;

            dst->animaya_groups[i] = (uint8_t*)malloc((size_t)cnt);
            dst->animaya_scales[i] = (uint8_t*)malloc((size_t)cnt);
            assert(dst->animaya_groups[i]);
            assert(dst->animaya_scales[i]);

            if( src->animaya_groups[i] )
                memcpy(dst->animaya_groups[i], src->animaya_groups[i], (size_t)cnt);
            if( src->animaya_scales[i] )
                memcpy(dst->animaya_scales[i], src->animaya_scales[i], (size_t)cnt);
        }
    }

    ToriDraw_ModelSetBoundsCylinder(dst);
    ToriDraw_ModelAssertPnmTextureInvariant(dst);
    return dst;
}

struct ToriDraw_Model*
ToriDraw_ModelMerge(
    struct ToriDraw_Model** models,
    int model_count)
{
    struct ToriDraw_Model* out = ToriDraw_ModelNewMerge(models, model_count);
    if( out )
        ToriDraw_ModelSetBoundsCylinder(out);
    return out;
}

struct ToriDraw_Model*
ToriDraw_ModelNewMerge(
    struct ToriDraw_Model** models,
    int model_count)
{
    if( model_count <= 0 )
        return NULL;
    assert(models);
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
    bool has_texture_render_types = false;
    bool has_animaya = false;

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
        /* A source with a uniform model_priority needs the merged array too: without it its faces
         * fall to priority 0 and the depth sort interleaves parts that must stay layered (the
         * classic case is a loc whose base and body are separate models). */
        if( m->face_priorities || m->model_priority )
            has_face_priorities = true;
        if( m->face_texture_coords )
            has_tex_coords = true;
        if( m->textured_p_coordinate )
            has_textured_coords = true;
        if( m->texture_render_types )
            has_texture_render_types = true;
        if( m->animaya_group_counts && m->animaya_groups && m->animaya_scales )
            has_animaya = true;
    }

    struct ToriDraw_Model* out = calloc(1, sizeof(struct ToriDraw_Model));
    assert(out);

    out->vertex_count = total_vertices;
    out->face_count = total_faces;
    out->textured_face_count = total_textured_faces;
    /* Render flags are per model, and the merged model IS the parts. A part that
     * asked for the depth test (TORIDRAW_MODEL_FLAG_ZBUFFER) still needs it once
     * merged — more so, since merging is what puts it in the same face order as
     * whatever it interpenetrates. */
    for( int i = 0; i < model_count; i++ )
        if( models[i] )
            out->flags |= models[i]->flags;

    if( total_vertices > 0 )
    {
        out->vertices_x = (vertexint_t*)malloc((size_t)total_vertices * sizeof(vertexint_t));
        out->vertices_y = (vertexint_t*)malloc((size_t)total_vertices * sizeof(vertexint_t));
        out->vertices_z = (vertexint_t*)malloc((size_t)total_vertices * sizeof(vertexint_t));
    }

    if( total_vertices > 0 && has_animaya )
    {
        out->animaya_vertex_count = total_vertices;
        out->animaya_group_counts = (uint8_t*)calloc((size_t)total_vertices, sizeof(uint8_t));
        out->animaya_groups = (uint8_t**)calloc((size_t)total_vertices, sizeof(uint8_t*));
        out->animaya_scales = (uint8_t**)calloc((size_t)total_vertices, sizeof(uint8_t*));
        assert(out->animaya_group_counts);
        assert(out->animaya_groups);
        assert(out->animaya_scales);
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
            assert(out->face_textures);
            for( int fi = 0; fi < total_faces; fi++ )
                out->face_textures[fi] = (faceint_t)-1;
        }
        if( has_face_alphas )
            /* Zeroed: faces from alpha-less inputs must stay opaque (raster
             * inverts stored alpha, 0 -> 0xFF). */
            out->face_alphas = (alphaint_t*)calloc((size_t)total_faces, sizeof(alphaint_t));
        if( has_face_infos )
            out->face_infos = (int*)calloc((size_t)total_faces, sizeof(int));
        if( has_face_priorities )
            out->face_priorities =
                (uint8_t*)calloc(ToriDraw_FacePrioritiesByteCount(total_faces), 1);
        if( has_tex_coords )
        {
            out->face_texture_coords = (faceint_t*)malloc((size_t)total_faces * sizeof(faceint_t));
            assert(out->face_texture_coords);
            /* Faces from coord-less inputs must read untextured. */
            for( int fi = 0; fi < total_faces; fi++ )
                out->face_texture_coords[fi] = (faceint_t)-1;
        }
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
    if( total_textured_faces > 0 && has_texture_render_types )
        out->texture_render_types =
            (uint8_t*)calloc((size_t)total_textured_faces, sizeof(uint8_t));

    int vtx_off = 0;
    int face_off = 0;
    int tex_face_off = 0;

    int* vertex_offsets = calloc((size_t)model_count, sizeof(int));
    int* face_offsets = calloc((size_t)model_count, sizeof(int));
    assert(vertex_offsets);
    assert(face_offsets);

    for( int i = 0; i < model_count; i++ )
    {
        struct ToriDraw_Model* m = models[i];
        if( !m )
            continue;

        vertex_offsets[i] = vtx_off;
        face_offsets[i] = face_off;

        for( int v = 0; v < m->vertex_count; v++ )
        {
            int dst_v = vtx_off + v;
            out->vertices_x[dst_v] = m->vertices_x[v];
            out->vertices_y[dst_v] = m->vertices_y[v];
            out->vertices_z[dst_v] = m->vertices_z[v];

            if( out->animaya_group_counts && m->animaya_group_counts )
            {
                int cnt = (int)m->animaya_group_counts[v];
                out->animaya_group_counts[dst_v] = (uint8_t)cnt;
                if( cnt > 0 && m->animaya_groups && m->animaya_scales )
                {
                    out->animaya_groups[dst_v] = (uint8_t*)malloc((size_t)cnt);
                    out->animaya_scales[dst_v] = (uint8_t*)malloc((size_t)cnt);
                    assert(out->animaya_groups[dst_v]);
                    assert(out->animaya_scales[dst_v]);
                    if( m->animaya_groups[v] )
                        memcpy(
                            out->animaya_groups[dst_v],
                            m->animaya_groups[v],
                            (size_t)cnt);
                    if( m->animaya_scales[v] )
                        memcpy(
                            out->animaya_scales[dst_v],
                            m->animaya_scales[v],
                            (size_t)cnt);
                }
            }
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
            if( out->face_priorities )
                ToriDraw_SetFacePriority(
                    out->face_priorities,
                    dst,
                    m->face_priorities ? ToriDraw_GetFacePriority(m->face_priorities, f)
                                       : m->model_priority);
            if( out->face_texture_coords && m->face_texture_coords )
            {
                /* Coord indices point into the source model's textured p/m/n
                 * tables; those concatenate at tex_face_off in the merge. */
                faceint_t coord = m->face_texture_coords[f];
                out->face_texture_coords[dst] =
                    coord >= 0 ? (faceint_t)(coord + tex_face_off) : (faceint_t)-1;
            }
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
            if( out->texture_render_types && m->texture_render_types )
                out->texture_render_types[dst] = m->texture_render_types[t];
        }

        vtx_off += m->vertex_count;
        face_off += m->face_count;
        tex_face_off += m->textured_face_count;
    }

    out->vertex_bones = ToriDraw_BonesMerge(models, vertex_offsets, model_count, true);
    out->face_bones = ToriDraw_BonesMerge(models, face_offsets, model_count, false);

    free(vertex_offsets);
    free(face_offsets);

    ToriDraw_ModelAssertPnmTextureInvariant(out);
    return out;
}

static void
ToriDraw_ModelRecolorHslArray(
    hsl16_t* colors,
    int count,
    int color_src,
    int color_dst)
{
    assert(colors);
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
    assert(model);
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
    assert(model);
    if( !model->face_textures )
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
    assert(model);
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
    assert(model);
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
    assert(model);
    for( int i = 0; i < model->vertex_count; i++ )
    {
        model->vertices_x[i] = (vertexint_t)((int)model->vertices_x[i] * x / 128);
        model->vertices_y[i] = (vertexint_t)((int)model->vertices_y[i] * height / 128);
        model->vertices_z[i] = (vertexint_t)((int)model->vertices_z[i] * z / 128);
    }
}

/* One flag for the whole record, so a posed model pays a single branch. */
static void
ToriDraw_ModelPostTransformRefresh(struct ToriDraw_Model* model)
{
    model->post_transform = model->post_resize || model->post_orient != 0 ||
                            model->post_offset_x != 0 || model->post_offset_y != 0 ||
                            model->post_offset_z != 0;
}

void
ToriDraw_ModelSetPostResize(
    struct ToriDraw_Model* model,
    int x,
    int z,
    int height)
{
    assert(model);
    model->post_resize = (x != 128 || z != 128 || height != 128);
    model->post_resize_x = x;
    model->post_resize_z = z;
    model->post_resize_height = height;
    ToriDraw_ModelPostTransformRefresh(model);
}

void
ToriDraw_ModelSetPostOrient(
    struct ToriDraw_Model* model,
    int quarter_turns)
{
    assert(model);
    model->post_orient = quarter_turns & 3;
    ToriDraw_ModelPostTransformRefresh(model);
}

void
ToriDraw_ModelSetPostOffset(
    struct ToriDraw_Model* model,
    int x,
    int y,
    int z)
{
    assert(model);
    model->post_offset_x = x;
    model->post_offset_y = y;
    model->post_offset_z = z;
    ToriDraw_ModelPostTransformRefresh(model);
}

void
ToriDraw_ModelApplyPostTransforms(struct ToriDraw_Model* model)
{
    assert(model);
    if( !model->post_transform )
        return;
    /* Orient, resize, translate -- see post_transform in ToriDraw_Model. */
    if( model->post_orient != 0 )
        ToriDraw_ModelOrient(model, model->post_orient);
    if( model->post_resize )
        ToriDraw_ModelScale(
            model, model->post_resize_x, model->post_resize_z, model->post_resize_height);
    if( model->post_offset_x != 0 || model->post_offset_y != 0 || model->post_offset_z != 0 )
        ToriDraw_ModelTranslate(
            model, model->post_offset_x, model->post_offset_y, model->post_offset_z);
}

void
ToriDraw_ModelTranslate(
    struct ToriDraw_Model* model,
    int x,
    int y,
    int z)
{
    assert(model);
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
