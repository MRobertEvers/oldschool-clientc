#include "toriauxlib/td/toriauxlibtd.h"

#include "buildcache/dat1_buildcache.h"

#include "toriauxlib/c/toriauxlibc_submit.h"
#include "osrs/rscache/dat1a/dat1a_anim_frame.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_animation.h"
#include "toridraw/toridraw_map.h"
#include "toridraw/toridraw_model.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct MapEntry_SequenceAnim
{
    int id;
    struct ToriDraw_Animation* animation;
};

struct ToriAuxLibTD
{
    struct ToriAuxLibCore* core;
    struct ToriAuxLibC* c;
    struct ToriDraw_Scene* scene;
    struct ToriDraw_Map* sequence_anim_hmap;
};

static struct ToriDraw_Map*
tdx_map_new(
    int entry_size,
    int capacity)
{
    int buffer_size = ToriDraw_MapBufferSizeFor(entry_size, capacity);
    struct ToriDraw_MapConfig config = {
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = entry_size,
    };
    return ToriDraw_MapNew(&config, 0);
}

static void
tdx_map_free(struct ToriDraw_Map* map)
{
    if( !map )
        return;
    free(ToriDraw_MapBufferPtr(map));
    ToriDraw_MapFree(map);
}

static void
tdx_free_sequence_anims(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(map);
    struct MapEntry_SequenceAnim* entry = NULL;
    while( (entry = (struct MapEntry_SequenceAnim*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->animation )
            ToriDraw_AnimationFree(entry->animation);
    }
    ToriDraw_MapIterFree(iter);
}

struct ToriAuxLibTD*
ToriAuxLibTD_New(
    struct ToriAuxLibCore* core,
    struct ToriAuxLibC* c,
    struct ToriDraw_Scene* scene)
{
    struct ToriAuxLibTD* td = calloc(1, sizeof(struct ToriAuxLibTD));
    if( !td )
        return NULL;

    td->core = core;
    td->c = c;
    td->scene = scene;
    td->sequence_anim_hmap = tdx_map_new(sizeof(struct MapEntry_SequenceAnim), 1024);
    if( !td->sequence_anim_hmap )
    {
        free(td);
        return NULL;
    }
    return td;
}

void
ToriAuxLibTD_Free(struct ToriAuxLibTD* td)
{
    if( !td )
        return;
    tdx_free_sequence_anims(td->sequence_anim_hmap);
    tdx_map_free(td->sequence_anim_hmap);
    free(td);
}

struct ToriAuxLibC*
ToriAuxLibTD_C(struct ToriAuxLibTD* td)
{
    return td ? td->c : NULL;
}

struct ToriAuxLibCore*
ToriAuxLibTD_Core(struct ToriAuxLibTD* td)
{
    return td ? td->core : NULL;
}

struct ToriDraw_Scene*
ToriAuxLibTD_Scene(struct ToriAuxLibTD* td)
{
    return td ? td->scene : NULL;
}

static struct ToriDraw_Bones*
tdx_bones_new_from_core(const struct ToriAuxLibCore_Bones* src)
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
ToriAuxLibTD_ModelNewFromCore(const struct ToriAuxLibCore_Model* src)
{
    if( !src )
        return NULL;

    struct ToriDraw_Model* dst = ToriDraw_ModelNew(src->vertex_count, src->face_count, src->flags);
    if( !dst )
        return NULL;

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

    dst->vertex_bones = tdx_bones_new_from_core(src->vertex_bones);
    dst->face_bones = tdx_bones_new_from_core(src->face_bones);

    if( src->bounds_cylinder )
    {
        dst->bounds_cylinder =
            ToriDraw_BufCopy(src->bounds_cylinder, 1, sizeof(struct ToriDraw_BoundsCylinder));
        if( !dst->bounds_cylinder )
            goto fail;
    }

    return dst;

fail:
    ToriDraw_ModelFree(dst);
    return NULL;
}

static struct ToriDraw_AnimBase*
tdx_animbase_new_from_core(const struct ToriAuxLibCore_AnimBase* src)
{
    if( !src )
        return NULL;

    struct ToriDraw_AnimBase* dst = calloc(1, sizeof(struct ToriDraw_AnimBase));
    if( !dst )
        return NULL;

    dst->length = src->length;
    if( src->length > 0 )
    {
        if( src->types )
        {
            dst->types = malloc((size_t)src->length);
            if( !dst->types )
                goto fail;
            memcpy(dst->types, src->types, (size_t)src->length);
        }
        if( src->bone_group_lengths )
        {
            dst->bone_group_lengths = malloc((size_t)src->length * sizeof(uint16_t));
            if( !dst->bone_group_lengths )
                goto fail;
            memcpy(
                dst->bone_group_lengths,
                src->bone_group_lengths,
                (size_t)src->length * sizeof(uint16_t));
        }
        if( src->bone_groups )
        {
            dst->bone_groups = calloc((size_t)src->length, sizeof(uint8_t*));
            if( !dst->bone_groups )
                goto fail;
            for( int i = 0; i < src->length; i++ )
            {
                int group_len = src->bone_group_lengths ? src->bone_group_lengths[i] : 0;
                if( group_len <= 0 )
                    continue;
                dst->bone_groups[i] = malloc((size_t)group_len);
                if( !dst->bone_groups[i] )
                    goto fail;
                memcpy(dst->bone_groups[i], src->bone_groups[i], (size_t)group_len);
            }
        }
    }
    return dst;

fail:
    if( dst->bone_groups )
    {
        for( int i = 0; i < dst->length; i++ )
            free(dst->bone_groups[i]);
        free(dst->bone_groups);
    }
    free(dst->bone_group_lengths);
    free(dst->types);
    free(dst);
    return NULL;
}

static bool
tdx_animframe_copy(
    struct ToriDraw_AnimFrame* dst,
    const struct ToriAuxLibCore_AnimFrame* src)
{
    memset(dst, 0, sizeof(*dst));
    dst->id = src->id;
    dst->length = src->length;
    dst->delay = src->delay;
    if( src->length <= 0 )
        return true;

    dst->groups = malloc((size_t)src->length * sizeof(int16_t));
    dst->x = malloc((size_t)src->length * sizeof(int16_t));
    dst->y = malloc((size_t)src->length * sizeof(int16_t));
    dst->z = malloc((size_t)src->length * sizeof(int16_t));
    if( !dst->groups || !dst->x || !dst->y || !dst->z )
    {
        free(dst->groups);
        free(dst->x);
        free(dst->y);
        free(dst->z);
        memset(dst, 0, sizeof(*dst));
        return false;
    }
    memcpy(dst->groups, src->groups, (size_t)src->length * sizeof(int16_t));
    memcpy(dst->x, src->x, (size_t)src->length * sizeof(int16_t));
    memcpy(dst->y, src->y, (size_t)src->length * sizeof(int16_t));
    memcpy(dst->z, src->z, (size_t)src->length * sizeof(int16_t));
    return true;
}

static struct ToriDraw_AnimBase*
tdx_animbase_move_from_cache(struct RSCacheDat1A_AnimBase* cache_base)
{
    if( !cache_base )
        return NULL;

    struct ToriDraw_AnimBase* base = malloc(sizeof(struct ToriDraw_AnimBase));
    if( !base )
        return NULL;

    memset(base, 0, sizeof(struct ToriDraw_AnimBase));
    base->length = cache_base->length;
    base->types = cache_base->types;
    base->bone_groups = cache_base->labels;
    base->bone_group_lengths = cache_base->label_counts;

    cache_base->types = NULL;
    cache_base->labels = NULL;
    cache_base->label_counts = NULL;
    cache_base->length = 0;

    free(cache_base);
    return base;
}

struct ToriDraw_Animation*
ToriAuxLibTD_AnimationNewFromCacheDatAnimbaseframes(struct RSCacheDat1A_AnimBaseFrames* abf)
{
    if( !abf )
        return NULL;

    struct ToriDraw_Animation* anim = malloc(sizeof(struct ToriDraw_Animation));
    if( !anim )
        return NULL;

    memset(anim, 0, sizeof(struct ToriDraw_Animation));
    anim->base = tdx_animbase_move_from_cache(abf->base);
    abf->base = NULL;

    anim->frame_count = abf->frame_count;
    if( abf->frame_count > 0 && abf->frames )
    {
        anim->frames = malloc((size_t)abf->frame_count * sizeof(struct ToriDraw_AnimFrame));
        if( !anim->frames )
        {
            ToriDraw_AnimationFree(anim);
            free(abf->frames);
            free(abf);
            return NULL;
        }

        for( int i = 0; i < abf->frame_count; i++ )
        {
            struct RSCacheDat1A_AnimFrame* cf = &abf->frames[i];
            struct ToriDraw_AnimFrame* tf = &anim->frames[i];
            memset(tf, 0, sizeof(struct ToriDraw_AnimFrame));

            tf->id = cf->id;
            tf->length = cf->length;
            tf->groups = cf->groups;
            tf->x = cf->x;
            tf->y = cf->y;
            tf->z = cf->z;
            tf->delay = cf->delay;

            cf->groups = NULL;
            cf->x = NULL;
            cf->y = NULL;
            cf->z = NULL;
            cf->length = 0;
        }
        free(abf->frames);
        abf->frames = NULL;
        abf->frame_count = 0;
    }

    free(abf);
    return anim;
}

struct ToriDraw_Animation*
ToriAuxLibTD_AnimationNewFromCore(const struct ToriAuxLibCore_Animation* src)
{
    if( !src )
        return NULL;

    struct ToriDraw_Animation* dst = calloc(1, sizeof(struct ToriDraw_Animation));
    if( !dst )
        return NULL;

    dst->base = tdx_animbase_new_from_core(src->base);
    dst->frame_count = src->frame_count;
    if( src->frame_count > 0 && src->frames )
    {
        dst->frames = calloc((size_t)src->frame_count, sizeof(struct ToriDraw_AnimFrame));
        if( !dst->frames )
        {
            ToriDraw_AnimationFree(dst);
            return NULL;
        }
        for( int i = 0; i < src->frame_count; i++ )
        {
            if( !tdx_animframe_copy(&dst->frames[i], &src->frames[i]) )
            {
                ToriDraw_AnimationFree(dst);
                return NULL;
            }
        }
    }
    return dst;
}

struct ToriDraw_Texture*
ToriAuxLibTD_TextureNewFromCore(const struct ToriAuxLibCore_Texture* src)
{
    if( !src || !src->texels )
        return NULL;

    struct ToriDraw_Texture* dst = calloc(1, sizeof(struct ToriDraw_Texture));
    if( !dst )
        return NULL;

    size_t pixel_count = (size_t)src->width * (size_t)src->height;
    dst->texels = malloc(pixel_count * sizeof(int));
    if( !dst->texels )
    {
        free(dst);
        return NULL;
    }
    memcpy(dst->texels, src->texels, pixel_count * sizeof(int));
    dst->width = src->width;
    dst->height = src->height;
    dst->opaque = src->opaque;
    dst->animation_direction = src->animation_direction;
    dst->animation_speed = src->animation_speed;
    return dst;
}

bool
ToriAuxLibTD_ModelReady(
    struct ToriAuxLibTD* td,
    int model_id)
{
    if( !td )
        return false;
    if( ToriDraw_SceneModelHas(td->scene, model_id) )
        return true;
    return ToriAuxLibCore_ModelHas(ToriAuxLibTD_Core(td), model_id);
}

bool
ToriAuxLibTD_SubmitModelFromDat1(
    struct ToriAuxLibTD* td,
    int model_id)
{
    if( ToriAuxLibCore_ModelHas(td->core, model_id) )
        return true;

    if( !dat1_buildcache_model_get(dat1(td->c), model_id) )
        return false;

    ToriAuxLibC_SubmitModelFromDat1(td->c, model_id);
    return ToriAuxLibCore_ModelHas(td->core, model_id);
}

struct ToriDraw_ModelHandle
ToriAuxLibTD_Model(
    struct ToriAuxLibTD* td,
    int model_id)
{
    struct ToriDraw_ModelHandle none = { .kind = TORIDRAWMK_NONE };
    if( !td )
        return none;

    struct ToriDraw_ModelHandle existing = ToriDraw_SceneModelGet(td->scene, model_id);
    if( existing.kind == TORIDRAWMK_MODEL )
        return existing;

    struct ToriAuxLibCore_Model* core_model = ToriAuxLibCore_ModelGet(td->core, model_id);
    if( !core_model )
        return none;

    struct ToriDraw_Model* td_model = ToriAuxLibTD_ModelNewFromCore(core_model);
    if( !td_model )
        return none;

    struct ToriDraw_ModelHandle hnd = {
        .kind = TORIDRAWMK_MODEL,
        .u.model.model = td_model,
    };
    ToriDraw_SceneModelAdd(td->scene, model_id, hnd);
    return hnd;
}

struct ToriDraw_Animation*
ToriAuxLibTD_Animation(
    struct ToriAuxLibTD* td,
    int anim_id)
{
    if( !td )
        return NULL;

    struct ToriDraw_Animation* existing = ToriDraw_SceneAnimationGet(td->scene, anim_id);
    if( existing )
        return existing;

    struct ToriAuxLibCore_Animation* gc_anim =
        ToriAuxLibCore_AnimationGet(ToriAuxLibTD_Core(td), anim_id);
    if( !gc_anim )
        return NULL;

    struct ToriDraw_Animation* td_anim = ToriAuxLibTD_AnimationNewFromCore(gc_anim);
    if( !td_anim )
        return NULL;

    ToriDraw_SceneAnimationAdd(td->scene, anim_id, td_anim);
    return td_anim;
}

struct ToriDraw_Animation*
ToriAuxLibTD_SequenceAnimation(
    struct ToriAuxLibTD* td,
    int seq_id)
{
    if( !td )
        return NULL;

    struct MapEntry_SequenceAnim* entry = (struct MapEntry_SequenceAnim*)ToriDraw_MapSearch(
        td->sequence_anim_hmap, &seq_id, TORIDRAW_MAP_FIND);
    if( entry && entry->animation )
        return entry->animation;

    struct ToriAuxLibCore_Animation* gc_resolved =
        ToriAuxLibCore_SequenceResolvedAnimation(td->core, seq_id);
    if( !gc_resolved )
        return NULL;

    struct ToriDraw_Animation* td_resolved = ToriAuxLibTD_AnimationNewFromCore(gc_resolved);
    if( !td_resolved )
        return NULL;

    entry = (struct MapEntry_SequenceAnim*)ToriDraw_MapSearch(
        td->sequence_anim_hmap, &seq_id, TORIDRAW_MAP_INSERT);
    if( !entry )
    {
        ToriDraw_AnimationFree(td_resolved);
        return NULL;
    }
    entry->id = seq_id;
    entry->animation = td_resolved;
    return td_resolved;
}

struct ToriDraw_Texture*
ToriAuxLibTD_Texture(
    struct ToriAuxLibTD* td,
    int texture_id)
{
    if( !td || texture_id < 0 || texture_id >= 256 )
        return NULL;

    struct ToriDraw_TextureState* tex_state = ToriDraw_SceneTexState(td->scene);
    struct ToriDraw_Texture* existing =
        tex_state ? ToriDraw_TextureMapGet(&tex_state->texture_map, texture_id) : NULL;
    if( existing )
        return existing;

    struct ToriAuxLibCore_Texture* gc_texture =
        ToriAuxLibCore_TextureGet(ToriAuxLibTD_Core(td), texture_id);
    if( !gc_texture )
        return NULL;

    struct ToriDraw_Texture* td_texture = ToriAuxLibTD_TextureNewFromCore(gc_texture);
    if( !td_texture )
        return NULL;

    ToriDraw_SceneSetTexture(td->scene, texture_id, td_texture);
    return td_texture;
}

int
ToriAuxLibTD_ElementAddModel(
    struct ToriAuxLibTD* td,
    int model_id)
{
    if( !td )
        return -1;

    struct ToriDraw_ModelHandle hnd = ToriAuxLibTD_Model(td, model_id);
    if( hnd.kind != TORIDRAWMK_MODEL )
        return -1;

    int element_id = ToriDraw_SceneElementAdd(td->scene);
    if( element_id < 0 )
        return -1;

    ToriDraw_SceneElementSetModel(td->scene, element_id, hnd);
    return element_id;
}

bool
ToriAuxLibTD_ElementSetModelId(
    struct ToriAuxLibTD* td,
    int element_id,
    int model_id)
{
    if( !td )
        return false;

    struct ToriDraw_ModelHandle hnd = ToriAuxLibTD_Model(td, model_id);
    if( hnd.kind != TORIDRAWMK_MODEL )
        return false;

    if( !ToriDraw_SceneElementIsLive(td->scene, element_id) )
        return false;

    ToriDraw_SceneElementSetModel(td->scene, element_id, hnd);
    return true;
}

bool
ToriAuxLibTD_ElementSetSequenceId(
    struct ToriAuxLibTD* td,
    int element_id,
    int seq_id)
{
    if( !td || !ToriDraw_SceneElementIsLive(td->scene, element_id) )
        return false;

    ToriDraw_SceneElementSetAnimationSeq(td->scene, element_id, seq_id);

    struct ToriDraw_Animation* resolved = ToriAuxLibTD_SequenceAnimation(td, seq_id);
    if( !resolved )
        return false;

    ToriDraw_SceneElementSetAnimation(td->scene, element_id, resolved, true);
    return true;
}
