#include "toridraw_model_from_torirs.h"

#include "cache_provider.h"
#include "torirs_types.h"

#include "toridraw_model.h"
#include "toridraw_model_transform.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* toridraw_model.h's TORIDRAW_MODEL_COPY uses the GNU `typeof` keyword, which clang rejects
 * under strict -std=c11. Redefine it with the always-available `__typeof__` spelling. */
#undef TORIDRAW_MODEL_COPY
#define TORIDRAW_MODEL_COPY(model, field, src, count)                                              \
    ((model)->field = (__typeof__((model)->field))ToriDraw_BufCopy(                                \
         (src), (size_t)(count), sizeof(*(model)->field)))

/* Texture ids wanted by models built since the host last drained them. One flag
 * per id, so repeats collapse and the drain is a fixed-size walk regardless of
 * how much geometry was built. Sized to TORIDRAW_TEXTURE_ID_CAPACITY: 643's SD
 * materials number past 255, so the old 256 silently dropped their wants and the
 * raster then skipped those faces forever. */
#define TORIDRAW_MODEL_TEXTURE_ID_MAX 2048
static unsigned char g_texture_wants[TORIDRAW_MODEL_TEXTURE_ID_MAX];

static void
note_texture_wants(const struct ToriRS_Model* src)
{
    if( !src->face_textures )
        return;
    for( int f = 0; f < src->face_count; f++ )
    {
        int const texture_id = (int)src->face_textures[f];
        if( texture_id < 0 || texture_id >= TORIDRAW_MODEL_TEXTURE_ID_MAX )
            continue;
        g_texture_wants[texture_id] = 1;
    }
}

void
ToriDraw_ModelNoteTextureWants(const struct ToriDraw_Model* model)
{
    if( !model || !model->face_textures )
        return;
    for( int f = 0; f < model->face_count; f++ )
    {
        int const texture_id = (int)model->face_textures[f];
        if( texture_id < 0 || texture_id >= TORIDRAW_MODEL_TEXTURE_ID_MAX )
            continue;
        g_texture_wants[texture_id] = 1;
    }
}

void
ToriDraw_ModelDropNonSdTextures(
    struct CacheProvider* provider,
    struct ToriDraw_Model* model)
{
    if( !provider || !model || !model->face_textures )
        return;
    for( int face = 0; face < model->face_count; face++ )
    {
        int const texture_id = (int)model->face_textures[face];
        if( texture_id < 0 )
            continue;
        if( !CacheProvider_TextureIsSd(provider, texture_id) )
            model->face_textures[face] = (faceint_t)-1;
        /* face_texture_coords stays: with the texture gone the raster never consults it,
         * which is also the reference's arrangement (textureCoords outlives the null). */
    }
}

int
ToriDraw_ModelTextureWantsTake(
    int* out_ids,
    int max_ids)
{
    int count = 0;
    if( !out_ids || max_ids <= 0 )
        return 0;
    for( int id = 0; id < TORIDRAW_MODEL_TEXTURE_ID_MAX && count < max_ids; id++ )
    {
        if( !g_texture_wants[id] )
            continue;
        g_texture_wants[id] = 0;
        out_ids[count++] = id;
    }
    return count;
}

static struct ToriDraw_Bones*
bones_from_torirs(const struct ToriRS_Bones* src)
{
    if( !src )
        return NULL;

    struct ToriDraw_Bones* bones = calloc(1, sizeof(struct ToriDraw_Bones));
    if( !bones )
        return NULL;

    bones->bones_count = src->bones_count;
    if( src->bones_count <= 0 )
        return bones;

    bones->bones = calloc((size_t)src->bones_count, sizeof(boneint_t*));
    bones->bones_sizes = calloc((size_t)src->bones_count, sizeof(boneint_t));
    if( !bones->bones || !bones->bones_sizes )
    {
        ToriDraw_BonesFree(bones);
        return NULL;
    }

    for( int i = 0; i < src->bones_count; i++ )
    {
        bones->bones_sizes[i] = (boneint_t)src->bones_sizes[i];
        if( src->bones_sizes[i] <= 0 )
            continue;
        bones->bones[i] = malloc((size_t)src->bones_sizes[i] * sizeof(boneint_t));
        if( !bones->bones[i] )
        {
            ToriDraw_BonesFree(bones);
            return NULL;
        }
        for( int j = 0; j < src->bones_sizes[i]; j++ )
            bones->bones[i][j] = (boneint_t)src->bones[i][j];
    }
    return bones;
}

struct ToriDraw_Model*
ToriDraw_ModelFromToriRS(const struct ToriRS_Model* src)
{
    assert(src);

    struct ToriDraw_Model* dst = ToriDraw_ModelNew(src->vertex_count, src->face_count, src->flags);
    if( !dst )
        return NULL;

    note_texture_wants(src);

    dst->textured_face_count = src->textured_face_count;

    TORIDRAW_MODEL_COPY(dst, vertices_x, src->vertices_x, src->vertex_count);
    TORIDRAW_MODEL_COPY(dst, vertices_y, src->vertices_y, src->vertex_count);
    TORIDRAW_MODEL_COPY(dst, vertices_z, src->vertices_z, src->vertex_count);
    TORIDRAW_MODEL_COPY(dst, face_indices_a, src->face_indices_a, src->face_count);
    TORIDRAW_MODEL_COPY(dst, face_indices_b, src->face_indices_b, src->face_count);
    TORIDRAW_MODEL_COPY(dst, face_indices_c, src->face_indices_c, src->face_count);
    TORIDRAW_MODEL_COPY(dst, face_colors_a, src->face_colors_a, src->face_count);
    TORIDRAW_MODEL_COPY(dst, face_colors_b, src->face_colors_b, src->face_count);
    TORIDRAW_MODEL_COPY(dst, face_colors_c, src->face_colors_c, src->face_count);
    TORIDRAW_MODEL_COPY(dst, face_colors, src->face_colors, src->face_count);
    TORIDRAW_MODEL_COPY(dst, face_alphas, src->face_alphas, src->face_count);
    TORIDRAW_MODEL_COPY(dst, face_infos, src->face_infos, src->face_count);
    TORIDRAW_MODEL_COPY(dst, face_textures, src->face_textures, src->face_count);
    TORIDRAW_MODEL_COPY(
        dst, textured_p_coordinate, src->textured_p_coordinate, src->textured_face_count);
    TORIDRAW_MODEL_COPY(
        dst, textured_m_coordinate, src->textured_m_coordinate, src->textured_face_count);
    TORIDRAW_MODEL_COPY(
        dst, textured_n_coordinate, src->textured_n_coordinate, src->textured_face_count);
    TORIDRAW_MODEL_COPY(dst, face_texture_coords, src->face_texture_coords, src->face_count);

    if( src->face_priorities && src->face_count > 0 )
    {
        size_t nbytes = (size_t)((src->face_count + 1) / 2);
        dst->face_priorities = ToriDraw_BufCopy(src->face_priorities, nbytes, 1);
        if( !dst->face_priorities )
            goto fail;
    }

    dst->model_priority = src->model_priority;

    dst->vertex_bones = bones_from_torirs(src->vertex_bones);
    dst->face_bones = bones_from_torirs(src->face_bones);

    if( src->animaya_skin && src->animaya_skin->vertex_count > 0 )
    {
        const struct ToriRS_AnimayaSkin* skin = src->animaya_skin;
        int vc = skin->vertex_count;

        dst->animaya_vertex_count = vc;
        dst->animaya_group_counts = malloc((size_t)vc);
        dst->animaya_groups = calloc((size_t)vc, sizeof(uint8_t*));
        dst->animaya_scales = calloc((size_t)vc, sizeof(uint8_t*));
        if( !dst->animaya_group_counts || !dst->animaya_groups || !dst->animaya_scales )
            goto fail;

        memcpy(dst->animaya_group_counts, skin->group_counts, (size_t)vc);
        for( int i = 0; i < vc; i++ )
        {
            int cnt = (int)dst->animaya_group_counts[i];
            if( cnt <= 0 )
                continue;

            dst->animaya_groups[i] = malloc((size_t)cnt);
            dst->animaya_scales[i] = malloc((size_t)cnt);
            if( !dst->animaya_groups[i] || !dst->animaya_scales[i] )
                goto fail;

            if( skin->groups && skin->groups[i] )
                memcpy(dst->animaya_groups[i], skin->groups[i], (size_t)cnt);
            if( skin->scales && skin->scales[i] )
                memcpy(dst->animaya_scales[i], skin->scales[i], (size_t)cnt);
        }
    }

    if( src->bounds_cylinder )
    {
        dst->bounds_cylinder =
            ToriDraw_BufCopy(src->bounds_cylinder, 1, sizeof(struct ToriDraw_BoundsCylinder));
        if( !dst->bounds_cylinder )
            goto fail;
    }
    else if( dst->vertex_count > 0 && dst->vertices_x && dst->vertices_y && dst->vertices_z )
        ToriDraw_ModelSetBoundsCylinder(dst);

    ToriDraw_ModelAssertPnmTextureInvariant(dst);
    return dst;

fail:
    ToriDraw_ModelFree(dst);
    return NULL;
}
