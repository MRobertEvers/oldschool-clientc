#include "toridraw_rscache.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/*
 * Two libraries, four disagreements. Everything in this file is one of them:
 *
 *  1. Width. A cache vertex is an int32 and a cache face index is an int32;
 *     ToriDraw stores both in 16 bits, because the projection reads six arrays
 *     of them per model and the halving is what keeps a model's working set in
 *     cache. Every crossing narrows.
 *  2. Packing. A cache face priority is a byte holding 0..12; ToriDraw packs
 *     two per byte, low nibble first.
 *  3. Scale. Format version 13 and up stores vertices at 4x precision and the
 *     REFERENCE decode shifts them down. RSCache deliberately does not -- the
 *     shift drops two bits and its bar is byte-exact round-trip -- so this is
 *     the scaleDown site. Skipping it renders every 643-era model four times
 *     too large.
 *  4. Meaning. `textured_p/m/n` are vertex indices for render type 0 and a raw
 *     axis triple for types 1-3. A range check that treats them all as indices
 *     silently strips the mapping off every cylinder, cube and sphere face.
 */

static size_t
face_priority_byte_count(int face_count)
{
    return (size_t)((face_count + 1) / 2);
}

static void
set_face_priority(
    uint8_t* packed,
    int index,
    int value)
{
    int byte_idx = index >> 1;

    assert(value >= 0 && value <= 15);
    if( index & 1 )
        packed[byte_idx] = (uint8_t)((packed[byte_idx] & 0x0Fu) | (uint8_t)(value << 4));
    else
        packed[byte_idx] = (uint8_t)((packed[byte_idx] & 0xF0u) | (uint8_t)(value & 0x0F));
}

static vertexint_t*
narrow_vertices(
    const int* src,
    int count)
{
    vertexint_t* dst;
    int i;

    assert(src);
    assert(count > 0);

    dst = malloc((size_t)count * sizeof(*dst));
    assert(dst);
    for( i = 0; i < count; i++ )
        dst[i] = (vertexint_t)src[i];
    return dst;
}

static faceint_t*
narrow_face_indices(
    const int* src,
    int count)
{
    faceint_t* dst;
    int i;

    assert(src);
    assert(count > 0);

    dst = malloc((size_t)count * sizeof(*dst));
    assert(dst);
    for( i = 0; i < count; i++ )
        dst[i] = (faceint_t)src[i];
    return dst;
}

static void*
buf_copy(
    const void* src,
    size_t count,
    size_t elem_size)
{
    void* dst;

    assert(src);
    assert(count > 0);

    dst = malloc(count * elem_size);
    assert(dst);
    memcpy(dst, src, count * elem_size);
    return dst;
}

/* Whether an optional array is present is decided at the CALL, not inside the
 * copy: a model carries a face_count for arrays it does not have. */
#define RSC_COPY(model, field, src, count)                                                         \
    ((model)->field = ((src) && (count) > 0)                                                       \
                          ? (__typeof__((model)->field))buf_copy(                                  \
                                (src), (size_t)(count), sizeof(*(model)->field))                   \
                          : NULL)

static struct ToriDraw_Bones*
bones_from_bone_map(
    const uint8_t* bone_map,
    int count)
{
    struct RSCache_ModelBones* decoded;
    struct ToriDraw_Bones* bones;
    int i;
    int j;

    assert(bone_map);
    assert(count > 0);

    decoded = RSCache_ModelBonesNewDecode(bone_map, count);
    /* A bone map that does not group into bones is ordinary cache data, not a
     * caller error: the model is simply unrigged. */
    if( !decoded )
        return NULL;

    bones = calloc(1, sizeof(*bones));
    assert(bones);
    bones->bones_count = decoded->bones_count;
    if( decoded->bones_count <= 0 )
    {
        RSCache_ModelBonesFree(decoded);
        return bones;
    }

    bones->bones = calloc((size_t)decoded->bones_count, sizeof(boneint_t*));
    bones->bones_sizes = calloc((size_t)decoded->bones_count, sizeof(boneint_t));
    assert(bones->bones);
    assert(bones->bones_sizes);

    for( i = 0; i < decoded->bones_count; i++ )
    {
        bones->bones_sizes[i] = (boneint_t)decoded->bones_sizes[i];
        if( decoded->bones_sizes[i] <= 0 )
            continue;
        bones->bones[i] = malloc((size_t)decoded->bones_sizes[i] * sizeof(boneint_t));
        assert(bones->bones[i]);
        for( j = 0; j < decoded->bones_sizes[i]; j++ )
            bones->bones[i][j] = (boneint_t)decoded->bones[i][j];
    }

    RSCache_ModelBonesFree(decoded);
    return bones;
}

/*
 * Strip a per-face texture coordinate that does not name a usable triangle.
 *
 * Only render type 0 stores VERTEX INDICES in p/m/n. Types 1-3 store a raw
 * axis triple for their projection -- m/32767 is the axis's y component, so a
 * clean -Y axis is the value -32767 -- and reading that as an index is
 * meaningless and reliably out of range. Range-checking them strips the
 * mapping from every cylinder, cube and sphere face, which does not crash: it
 * silently demotes them to untextured.
 */
static void
sanitize_pnm_texture_coords(struct ToriDraw_Model* model)
{
    int const vc = model->vertex_count;
    int i;

    assert(model);
    if( !model->face_texture_coords || model->face_count <= 0 )
        return;

    for( i = 0; i < model->face_count; i++ )
    {
        int texture_face = (int)model->face_texture_coords[i];
        int render_type;
        int p;
        int m;
        int n;

        if( texture_face < 0 )
            continue;

        if( texture_face >= model->textured_face_count || !model->textured_p_coordinate ||
            !model->textured_m_coordinate || !model->textured_n_coordinate )
        {
            model->face_texture_coords[i] = -1;
            continue;
        }

        render_type =
            model->texture_render_types ? (model->texture_render_types[texture_face] & 0xFF) : 0;
        if( render_type != 0 )
            continue;

        p = (int)model->textured_p_coordinate[texture_face];
        m = (int)model->textured_m_coordinate[texture_face];
        n = (int)model->textured_n_coordinate[texture_face];
        if( p < 0 || p >= vc || m < 0 || m >= vc || n < 0 || n >= vc )
            model->face_texture_coords[i] = -1;
    }
}

/*
 * The fields that do not depend on whether arrays were moved or copied: the
 * counts, the priority repack, the per-face texture coordinate normalisation,
 * the bones, the animaya skin, the bounds.
 */
static void
finish_model(
    struct ToriDraw_Model* dst,
    const struct RSCache_Model* src)
{
    int const fc = src->face_count;
    int const tfc = src->textured_face_count;
    int i;

    dst->model_priority = src->model_priority;
    dst->textured_face_count = tfc;

    if( src->face_priorities && fc > 0 )
    {
        size_t nbytes = face_priority_byte_count(fc);

        dst->face_priorities = calloc(nbytes, 1);
        assert(dst->face_priorities);
        for( i = 0; i < fc; i++ )
            set_face_priority(dst->face_priorities, i, src->face_priorities[i]);
    }

    if( src->face_texture_coords && fc > 0 )
    {
        dst->face_texture_coords = malloc((size_t)fc * sizeof(faceint_t));
        assert(dst->face_texture_coords);
        for( i = 0; i < fc; i++ )
            dst->face_texture_coords[i] = (faceint_t)ToriDraw_NormalizeFaceTextureCoord(
                (int)src->face_texture_coords[i], tfc);
    }

    /* face_infos is int in ToriDraw and uint8_t in the cache. */
    if( src->face_infos && fc > 0 )
    {
        dst->face_infos = malloc((size_t)fc * sizeof(int));
        assert(dst->face_infos);
        for( i = 0; i < fc; i++ )
            dst->face_infos[i] = (int)src->face_infos[i];
    }

    /* Whether a model has bones is answered HERE, once, by the side that can
     * see the bone map -- not inside bones_from_bone_map. */
    if( src->vertex_bone_map && src->vertex_count > 0 )
        dst->vertex_bones = bones_from_bone_map(src->vertex_bone_map, src->vertex_count);
    if( src->face_bone_map && fc > 0 )
        dst->face_bones = bones_from_bone_map(src->face_bone_map, fc);

    if( src->animaya_vertex_count > 0 && src->animaya_group_counts && src->animaya_groups &&
        src->animaya_scales )
    {
        int vc = src->animaya_vertex_count;

        dst->animaya_vertex_count = vc;
        dst->animaya_group_counts = malloc((size_t)vc);
        dst->animaya_groups = calloc((size_t)vc, sizeof(uint8_t*));
        dst->animaya_scales = calloc((size_t)vc, sizeof(uint8_t*));
        assert(dst->animaya_group_counts);
        assert(dst->animaya_groups);
        assert(dst->animaya_scales);

        memcpy(dst->animaya_group_counts, src->animaya_group_counts, (size_t)vc);
        for( i = 0; i < vc; i++ )
        {
            int cnt = (int)dst->animaya_group_counts[i];

            if( cnt <= 0 )
                continue;
            dst->animaya_groups[i] = malloc((size_t)cnt);
            dst->animaya_scales[i] = malloc((size_t)cnt);
            assert(dst->animaya_groups[i]);
            assert(dst->animaya_scales[i]);
            /* A decoded model may carry a group count for a vertex it has no
             * group data for; the source arrays stay guarded. */
            if( src->animaya_groups[i] )
                memcpy(dst->animaya_groups[i], src->animaya_groups[i], (size_t)cnt);
            if( src->animaya_scales[i] )
                memcpy(dst->animaya_scales[i], src->animaya_scales[i], (size_t)cnt);
        }
    }

    sanitize_pnm_texture_coords(dst);

    if( dst->vertex_count > 0 && dst->vertices_x && dst->vertices_y && dst->vertices_z )
        ToriDraw_ModelSetBoundsCylinder(dst);

    /* Per-corner colours exist but are black until ToriDraw_RSCacheModelLight
     * fills them. Allocated here rather than there so the model is structurally
     * complete on return and a caller that forgets to light gets a black model
     * rather than a NULL dereference in the raster. */
    if( fc > 0 )
    {
        dst->face_colors_a = calloc((size_t)fc, sizeof(hsl16_t));
        dst->face_colors_b = calloc((size_t)fc, sizeof(hsl16_t));
        dst->face_colors_c = calloc((size_t)fc, sizeof(hsl16_t));
        assert(dst->face_colors_a);
        assert(dst->face_colors_b);
        assert(dst->face_colors_c);
    }

    ToriDraw_ModelAssertPnmTextureInvariant(dst);
}

/*
 * The reference's ModelData.decodeV1 scale-down. See disagreement 3 at the top.
 * Arithmetic >>, matching the reference's JS >> on negatives.
 */
static void
apply_format_scale_down(
    struct ToriDraw_Model* dst,
    int format_version)
{
    int i;

    if( format_version < 13 )
        return;
    for( i = 0; i < dst->vertex_count; i++ )
    {
        dst->vertices_x[i] >>= 2;
        dst->vertices_y[i] >>= 2;
        dst->vertices_z[i] >>= 2;
    }
}

struct ToriDraw_Model*
ToriDraw_RSCacheModelNew(const struct RSCache_Model* src)
{
    struct ToriDraw_Model* dst;
    int const fc = src->face_count;
    int const tfc = src->textured_face_count;

    assert(src);

    /* Render flags start CLEAR and are not inherited: the cache model's flags
     * are decode bookkeeping and ToriDraw's bit 0 is
     * TORIDRAW_MODEL_FLAG_ZBUFFER. Forwarding the byte opts every cache model
     * into the depth kernels, which also drops its face priorities. */
    dst = ToriDraw_ModelNew(src->vertex_count, fc, 0);
    assert(dst);

    if( src->vertex_count > 0 && src->vertices_x && src->vertices_y && src->vertices_z )
    {
        dst->vertices_x = narrow_vertices(src->vertices_x, src->vertex_count);
        dst->vertices_y = narrow_vertices(src->vertices_y, src->vertex_count);
        dst->vertices_z = narrow_vertices(src->vertices_z, src->vertex_count);
        apply_format_scale_down(dst, src->format_version);
    }

    if( fc > 0 && src->face_indices_a && src->face_indices_b && src->face_indices_c )
    {
        dst->face_indices_a = narrow_face_indices(src->face_indices_a, fc);
        dst->face_indices_b = narrow_face_indices(src->face_indices_b, fc);
        dst->face_indices_c = narrow_face_indices(src->face_indices_c, fc);
    }

    RSC_COPY(dst, face_colors, src->face_colors, fc);
    RSC_COPY(dst, face_alphas, src->face_alphas, fc);
    RSC_COPY(dst, face_textures, src->face_textures, fc);
    RSC_COPY(dst, textured_p_coordinate, src->textured_p_coordinate, tfc);
    RSC_COPY(dst, textured_m_coordinate, src->textured_m_coordinate, tfc);
    RSC_COPY(dst, textured_n_coordinate, src->textured_n_coordinate, tfc);
    RSC_COPY(dst, texture_render_types, src->texture_render_types, tfc);

    finish_model(dst, src);
    return dst;
}

struct ToriDraw_Model*
ToriDraw_RSCacheModelSteal(struct RSCache_Model* src)
{
    struct ToriDraw_Model* dst;
    int const fc = src->face_count;
    int const tfc = src->textured_face_count;

    assert(src);

    dst = ToriDraw_ModelNew(src->vertex_count, fc, 0);
    assert(dst);

    if( src->vertex_count > 0 && src->vertices_x && src->vertices_y && src->vertices_z )
    {
        dst->vertices_x = narrow_vertices(src->vertices_x, src->vertex_count);
        dst->vertices_y = narrow_vertices(src->vertices_y, src->vertex_count);
        dst->vertices_z = narrow_vertices(src->vertices_z, src->vertex_count);
        apply_format_scale_down(dst, src->format_version);
        free(src->vertices_x);
        free(src->vertices_y);
        free(src->vertices_z);
        src->vertices_x = NULL;
        src->vertices_y = NULL;
        src->vertices_z = NULL;
    }

    if( fc > 0 && src->face_indices_a && src->face_indices_b && src->face_indices_c )
    {
        dst->face_indices_a = narrow_face_indices(src->face_indices_a, fc);
        dst->face_indices_b = narrow_face_indices(src->face_indices_b, fc);
        dst->face_indices_c = narrow_face_indices(src->face_indices_c, fc);
        free(src->face_indices_a);
        free(src->face_indices_b);
        free(src->face_indices_c);
        src->face_indices_a = NULL;
        src->face_indices_b = NULL;
        src->face_indices_c = NULL;
    }

    /*
     * These five cross by POINTER: the element types are identical on both
     * sides (uint16_t/hsl16_t, uint8_t/alphaint_t, int16_t/faceint_t), so a
     * copy would be a memcpy to an identical layout. That is the whole saving
     * this entry point exists for.
     */
#define RSC_MOVE(field, src_field, count)                                                          \
    do                                                                                             \
    {                                                                                              \
        if( (src_field) && (count) > 0 )                                                           \
        {                                                                                          \
            _Static_assert(                                                                        \
                sizeof(*dst->field) == sizeof(*(src_field)), "moved array element widths differ"); \
            dst->field = (__typeof__(dst->field))(src_field);                                      \
            (src_field) = NULL;                                                                    \
        }                                                                                          \
    } while( 0 )

    RSC_MOVE(face_colors, src->face_colors, fc);
    RSC_MOVE(face_alphas, src->face_alphas, fc);
    RSC_MOVE(face_textures, src->face_textures, fc);
    RSC_MOVE(textured_p_coordinate, src->textured_p_coordinate, tfc);
    RSC_MOVE(textured_m_coordinate, src->textured_m_coordinate, tfc);
    RSC_MOVE(textured_n_coordinate, src->textured_n_coordinate, tfc);
    RSC_MOVE(texture_render_types, src->texture_render_types, tfc);

#undef RSC_MOVE

    finish_model(dst, src);
    return dst;
}

struct ToriDraw_Model*
ToriDraw_RSCacheModelFromBlob(
    const uint8_t* data,
    int data_size)
{
    struct RSCache_Model* raw;
    struct ToriDraw_Model* model;

    assert(data);
    assert(data_size > 0);

    /* RSCache_ModelNewDecode takes a non-const pointer but does not retain it;
     * the cast is to that signature, not a licence to write. */
    raw = RSCache_ModelNewDecode((uint8_t*)data, data_size);
    /* A blob that does not decode is ordinary: a cache group can hold a record
     * this revision's codec does not understand. */
    if( !raw )
        return NULL;

    model = ToriDraw_RSCacheModelSteal(raw);
    RSCache_ModelFree(raw);
    return model;
}
