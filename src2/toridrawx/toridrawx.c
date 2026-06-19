#include "toridrawx.h"

#include "buildcache/dat1_buildcache.h"
#include "gamecache/gamecache.h"
#include "gamecache/gamecache_submit.h"
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

struct ToriDrawX
{
    struct GameCacheL* gamecache_l;
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

struct ToriDrawX*
ToriDrawX_New(
    struct GameCacheL* gamecache_l,
    struct ToriDraw_Scene* scene)
{
    struct ToriDrawX* tdx = calloc(1, sizeof(struct ToriDrawX));
    if( !tdx )
        return NULL;

    tdx->gamecache_l = gamecache_l;
    tdx->scene = scene;
    tdx->sequence_anim_hmap = tdx_map_new(sizeof(struct MapEntry_SequenceAnim), 1024);
    if( !tdx->sequence_anim_hmap )
    {
        free(tdx);
        return NULL;
    }
    return tdx;
}

void
ToriDrawX_Free(struct ToriDrawX* tdx)
{
    if( !tdx )
        return;
    tdx_free_sequence_anims(tdx->sequence_anim_hmap);
    tdx_map_free(tdx->sequence_anim_hmap);
    free(tdx);
}

struct GameCacheL*
ToriDrawX_GamecacheL(struct ToriDrawX* tdx)
{
    return tdx ? tdx->gamecache_l : NULL;
}

struct GameCache*
ToriDrawX_Gamecache(struct ToriDrawX* tdx)
{
    return tdx && tdx->gamecache_l ? gamecache(tdx->gamecache_l) : NULL;
}

struct ToriDraw_Scene*
ToriDrawX_Scene(struct ToriDrawX* tdx)
{
    return tdx ? tdx->scene : NULL;
}

static struct ToriDraw_Bones*
tdx_bones_new_from_gamecache(const struct GameCache_Bones* src)
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
ToriDrawX_ModelNewFromGamecache(const struct GameCache_Model* src)
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
    TORIDRAW_MODEL_COPY(dst, textured_p_coordinate, src->textured_p_coordinate, src->textured_face_count);
    TORIDRAW_MODEL_COPY(dst, textured_m_coordinate, src->textured_m_coordinate, src->textured_face_count);
    TORIDRAW_MODEL_COPY(dst, textured_n_coordinate, src->textured_n_coordinate, src->textured_face_count);
    TORIDRAW_MODEL_COPY(dst, face_texture_coords, src->face_texture_coords, src->face_count);

    if( src->face_priorities && src->face_count > 0 )
    {
        size_t nbytes = (size_t)((src->face_count + 1) / 2);
        dst->face_priorities = ToriDraw_BufCopy(src->face_priorities, nbytes, 1);
        if( !dst->face_priorities )
            goto fail;
    }

    dst->vertex_bones = tdx_bones_new_from_gamecache(src->vertex_bones);
    dst->face_bones = tdx_bones_new_from_gamecache(src->face_bones);

    if( src->bounds_cylinder )
    {
        dst->bounds_cylinder = ToriDraw_BufCopy(src->bounds_cylinder, 1, sizeof(struct ToriDraw_BoundsCylinder));
        if( !dst->bounds_cylinder )
            goto fail;
    }

    return dst;

fail:
    ToriDraw_ModelFree(dst);
    return NULL;
}

static struct ToriDraw_AnimBase*
tdx_animbase_new_from_gamecache(const struct GameCache_AnimBase* src)
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
    const struct GameCache_AnimFrame* src)
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

struct ToriDraw_Animation*
ToriDrawX_AnimationNewFromGamecache(const struct GameCache_Animation* src)
{
    if( !src )
        return NULL;

    struct ToriDraw_Animation* dst = calloc(1, sizeof(struct ToriDraw_Animation));
    if( !dst )
        return NULL;

    dst->base = tdx_animbase_new_from_gamecache(src->base);
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
ToriDrawX_TextureNewFromGamecache(const struct GameCache_Texture* src)
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
ToriDrawX_ModelReady(
    struct ToriDrawX* tdx,
    int model_id)
{
    if( !tdx )
        return false;
    if( ToriDraw_SceneModelHas(tdx->scene, model_id) )
        return true;
    return gamecache_model_has(ToriDrawX_Gamecache(tdx), model_id);
}

bool
ToriDrawX_SubmitModelFromDat1(
    struct ToriDrawX* tdx,
    int model_id)
{
    struct GameCache* gc = ToriDrawX_Gamecache(tdx);
    struct Dat1BuildCache* dat1 = dat1(tdx->gamecache_l);

    if( gamecache_model_has(gc, model_id) )
        return true;

    if( !dat1_buildcache_model_get(dat1, model_id) )
        return false;

    gamecache_submit_model_from_dat1(gc, dat1, model_id);
    return gamecache_model_has(gc, model_id);
}

struct ToriDraw_ModelHandle
ToriDrawX_Model(
    struct ToriDrawX* tdx,
    int model_id)
{
    struct ToriDraw_ModelHandle none = { .kind = TORIDRAWMK_NONE };
    if( !tdx )
        return none;

    struct ToriDraw_ModelHandle existing = ToriDraw_SceneModelGet(tdx->scene, model_id);
    if( existing.kind == TORIDRAWMK_MODEL )
        return existing;

    struct GameCache* gc = ToriDrawX_Gamecache(tdx);
    struct GameCache_Model* gc_model = gamecache_model_get(gc, model_id);
    if( !gc_model )
        return none;

    struct ToriDraw_Model* td_model = ToriDrawX_ModelNewFromGamecache(gc_model);
    if( !td_model )
        return none;

    struct ToriDraw_ModelHandle hnd = {
        .kind = TORIDRAWMK_MODEL,
        .u.model.model = td_model,
    };
    ToriDraw_SceneModelAdd(tdx->scene, model_id, hnd);
    return hnd;
}

struct ToriDraw_Animation*
ToriDrawX_Animation(
    struct ToriDrawX* tdx,
    int anim_id)
{
    if( !tdx )
        return NULL;

    struct ToriDraw_Animation* existing = ToriDraw_SceneAnimationGet(tdx->scene, anim_id);
    if( existing )
        return existing;

    struct GameCache_Animation* gc_anim =
        gamecache_animation_get(ToriDrawX_Gamecache(tdx), anim_id);
    if( !gc_anim )
        return NULL;

    struct ToriDraw_Animation* td_anim = ToriDrawX_AnimationNewFromGamecache(gc_anim);
    if( !td_anim )
        return NULL;

    ToriDraw_SceneAnimationAdd(tdx->scene, anim_id, td_anim);
    return td_anim;
}

struct ToriDraw_Animation*
ToriDrawX_SequenceAnimation(
    struct ToriDrawX* tdx,
    int seq_id)
{
    if( !tdx )
        return NULL;

    struct MapEntry_SequenceAnim* entry = (struct MapEntry_SequenceAnim*)ToriDraw_MapSearch(
        tdx->sequence_anim_hmap, &seq_id, TORIDRAW_MAP_FIND);
    if( entry && entry->animation )
        return entry->animation;

    struct GameCache* gc = ToriDrawX_Gamecache(tdx);
    struct GameCache_Animation* gc_resolved = gamecache_sequence_resolved_animation(gc, seq_id);
    if( !gc_resolved )
        return NULL;

    struct ToriDraw_Animation* td_resolved = ToriDrawX_AnimationNewFromGamecache(gc_resolved);
    if( !td_resolved )
        return NULL;

    entry = (struct MapEntry_SequenceAnim*)ToriDraw_MapSearch(
        tdx->sequence_anim_hmap, &seq_id, TORIDRAW_MAP_INSERT);
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
ToriDrawX_Texture(
    struct ToriDrawX* tdx,
    int texture_id)
{
    if( !tdx || texture_id < 0 || texture_id >= 256 )
        return NULL;

    struct ToriDraw_TextureState* tex_state = ToriDraw_SceneTexState(tdx->scene);
    struct ToriDraw_Texture* existing = tex_state
        ? ToriDraw_TextureMapGet(&tex_state->texture_map, texture_id)
        : NULL;
    if( existing )
        return existing;

    struct GameCache_Texture* gc_texture =
        gamecache_texture_get(ToriDrawX_Gamecache(tdx), texture_id);
    if( !gc_texture )
        return NULL;

    struct ToriDraw_Texture* td_texture = ToriDrawX_TextureNewFromGamecache(gc_texture);
    if( !td_texture )
        return NULL;

    ToriDraw_SceneSetTexture(tdx->scene, texture_id, td_texture);
    return td_texture;
}

int
ToriDrawX_ElementAddModel(
    struct ToriDrawX* tdx,
    int model_id)
{
    if( !tdx )
        return -1;

    struct ToriDraw_ModelHandle hnd = ToriDrawX_Model(tdx, model_id);
    if( hnd.kind != TORIDRAWMK_MODEL )
        return -1;

    int element_id = ToriDraw_SceneElementAdd(tdx->scene);
    if( element_id < 0 )
        return -1;

    ToriDraw_SceneElementSetModel(tdx->scene, element_id, hnd);
    return element_id;
}

bool
ToriDrawX_ElementSetModelId(
    struct ToriDrawX* tdx,
    int element_id,
    int model_id)
{
    if( !tdx )
        return false;

    struct ToriDraw_ModelHandle hnd = ToriDrawX_Model(tdx, model_id);
    if( hnd.kind != TORIDRAWMK_MODEL )
        return false;

    if( !ToriDraw_SceneElementIsLive(tdx->scene, element_id) )
        return false;

    ToriDraw_SceneElementSetModel(tdx->scene, element_id, hnd);
    return true;
}

bool
ToriDrawX_ElementSetSequenceId(
    struct ToriDrawX* tdx,
    int element_id,
    int seq_id)
{
    if( !tdx || !ToriDraw_SceneElementIsLive(tdx->scene, element_id) )
        return false;

    ToriDraw_SceneElementSetAnimationSeq(tdx->scene, element_id, seq_id);

    struct ToriDraw_Animation* resolved = ToriDrawX_SequenceAnimation(tdx, seq_id);
    if( !resolved )
        return false;

    ToriDraw_SceneElementSetAnimation(tdx->scene, element_id, resolved, true);
    return true;
}
