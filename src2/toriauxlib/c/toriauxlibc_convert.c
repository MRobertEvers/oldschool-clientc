#include "toriauxlib/c/toriauxlibc.h"
#include "toriauxlib/core/toriauxlibcore_types.h"

#include "osrs/rscache/tables/config_floortype.h"
#include "osrs/rscache/tables/config_locs.h"
#include "osrs/rscache/tables/config_sequence.h"
#include "osrs/rscache/tables/maps.h"
#include "osrs/rscache/tables_dat/animframe.h"
#include "osrs/rscache/tables_dat/config_textures.h"
#include "osrs/texture.h"

#include <stdlib.h>
#include <string.h>

static struct ToriAuxLibCore_AnimBase*
toriauxlibc_animbase_move_from_cache(struct CacheAnimBase* cache_base)
{
    if( !cache_base )
        return NULL;

    struct ToriAuxLibCore_AnimBase* base = malloc(sizeof(struct ToriAuxLibCore_AnimBase));
    if( !base )
        return NULL;

    memset(base, 0, sizeof(struct ToriAuxLibCore_AnimBase));
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

struct ToriAuxLibCore_Animation*
ToriAuxLibC_AnimationNewFromCacheDatAnimbaseframes(const void* abf_ptr)
{
    struct CacheDatAnimBaseFrames* abf = (struct CacheDatAnimBaseFrames*)abf_ptr;
    if( !abf )
        return NULL;

    struct ToriAuxLibCore_Animation* anim = malloc(sizeof(struct ToriAuxLibCore_Animation));
    if( !anim )
        return NULL;

    memset(anim, 0, sizeof(struct ToriAuxLibCore_Animation));
    anim->base = toriauxlibc_animbase_move_from_cache(abf->base);
    abf->base = NULL;

    anim->frame_count = abf->frame_count;
    if( abf->frame_count > 0 && abf->frames )
    {
        anim->frames = malloc((size_t)abf->frame_count * sizeof(struct ToriAuxLibCore_AnimFrame));
        if( !anim->frames )
        {
            ToriAuxLibCore_AnimationFree(anim);
            free(abf->frames);
            free(abf);
            return NULL;
        }

        for( int i = 0; i < abf->frame_count; i++ )
        {
            struct CacheAnimframe* cf = &abf->frames[i];
            struct ToriAuxLibCore_AnimFrame* tf = &anim->frames[i];
            memset(tf, 0, sizeof(struct ToriAuxLibCore_AnimFrame));

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

struct ToriAuxLibCore_Texture*
ToriAuxLibC_TextureNewFromCacheDatTexture(
    const void* cache_texture_ptr,
    int animation_direction,
    int animation_speed)
{
    struct CacheDatTexture* cache_texture = (struct CacheDatTexture*)cache_texture_ptr;
    if( !cache_texture )
        return NULL;

    struct DashTexture* dash = texture_new_from_texture_sprite(
        cache_texture, animation_direction, animation_speed, false, false);
    if( !dash )
        return NULL;

    struct ToriAuxLibCore_Texture* texture = malloc(sizeof(struct ToriAuxLibCore_Texture));
    if( !texture )
    {
        texture_free(dash);
        return NULL;
    }

    memset(texture, 0, sizeof(struct ToriAuxLibCore_Texture));
    texture->texels = dash->texels;
    texture->width = dash->width;
    texture->height = dash->height;
    texture->opaque = dash->opaque;
    texture->animation_direction = dash->animation_direction;
    texture->animation_speed = dash->animation_speed;
    free(dash);
    return texture;
}

struct ToriAuxLibCore_MapTerrain*
ToriAuxLibC_MapTerrainNewFromCacheMapTerrain(const void* cache_terrain_ptr)
{
    const struct CacheMapTerrain* src = cache_terrain_ptr;
    if( !src )
        return NULL;

    struct ToriAuxLibCore_MapTerrain* dst = malloc(sizeof(struct ToriAuxLibCore_MapTerrain));
    if( !dst )
        return NULL;

    dst->map_x = src->map_x;
    dst->map_z = src->map_z;
    for( int i = 0;
         i < TORIAUXLIBCORE_MAP_TERRAIN_X * TORIAUXLIBCORE_MAP_TERRAIN_Z * TORIAUXLIBCORE_MAP_TERRAIN_LEVELS;
         i++ )
    {
        const struct CacheMapFloor* s = &src->tiles_xyz[i];
        struct ToriAuxLibCore_MapFloor* d = &dst->tiles_xyz[i];
        d->overlay_id = s->overlay_id;
        d->underlay_id = s->underlay_id;
        d->height = s->height;
        d->settings = s->settings;
        d->shape = s->shape;
        d->rotation = s->rotation;
    }
    return dst;
}

struct ToriAuxLibCore_MapLocs*
ToriAuxLibC_MapLocsNewFromCacheMapLocs(const void* cache_locs_ptr)
{
    const struct CacheMapLocs* src = cache_locs_ptr;
    if( !src )
        return NULL;

    struct ToriAuxLibCore_MapLocs* dst = malloc(sizeof(struct ToriAuxLibCore_MapLocs));
    if( !dst )
        return NULL;

    dst->chunk_mapx = src->_chunk_mapx;
    dst->chunk_mapz = src->_chunk_mapz;
    dst->locs_count = src->locs_count;
    dst->locs = NULL;

    if( src->locs_count > 0 )
    {
        dst->locs = malloc((size_t)src->locs_count * sizeof(struct ToriAuxLibCore_MapLoc));
        if( !dst->locs )
        {
            free(dst);
            return NULL;
        }
        for( int i = 0; i < src->locs_count; i++ )
        {
            dst->locs[i].loc_id = src->locs[i].loc_id;
            dst->locs[i].shape_select = src->locs[i].shape_select;
            dst->locs[i].orientation = src->locs[i].orientation;
            dst->locs[i].chunk_pos_x = src->locs[i].chunk_pos_x;
            dst->locs[i].chunk_pos_z = src->locs[i].chunk_pos_z;
            dst->locs[i].chunk_pos_level = src->locs[i].chunk_pos_level;
        }
    }
    return dst;
}

struct ToriAuxLibCore_Flotype*
ToriAuxLibC_FlotypeNewFromCacheConfigOverlay(
    const void* cache_overlay_ptr,
    int id)
{
    const struct CacheConfigOverlay* src = cache_overlay_ptr;
    if( !src )
        return NULL;

    struct ToriAuxLibCore_Flotype* dst = malloc(sizeof(struct ToriAuxLibCore_Flotype));
    if( !dst )
        return NULL;

    dst->id = id;
    dst->rgb_color = src->rgb_color;
    dst->texture = src->texture;
    dst->secondary_rgb_color = src->secondary_rgb_color;
    dst->hide_underlay = src->hide_underlay;
    return dst;
}

struct ToriAuxLibCore_Location*
ToriAuxLibC_LocationNewFromCacheConfigLocation(const void* cache_loc_ptr)
{
    const struct CacheConfigLocation* src = cache_loc_ptr;
    if( !src )
        return NULL;

    struct ToriAuxLibCore_Location* dst = calloc(1, sizeof(struct ToriAuxLibCore_Location));
    if( !dst )
        return NULL;

    dst->id = src->_id;
    dst->map_scene_id = -1;
    dst->shapes_and_model_count = src->shapes_and_model_count;
    dst->size_x = src->size_x;
    dst->size_z = src->size_z;
    dst->blocks_walk = src->blocks_walk;
    dst->blocks_projectiles = src->blocks_projectiles;
    dst->wall_width = src->wall_width;
    dst->seq_id = src->seq_id;
    dst->contoured_ground = src->contoured_ground;
    dst->contour_ground_type = src->contour_ground_type;
    dst->contour_ground_param = src->contour_ground_param;
    dst->sharelight = src->sharelight;
    dst->shadowed = src->shadowed;
    dst->ambient = src->ambient;
    dst->contrast = src->contrast;
    dst->mirrored = src->mirrored;
    dst->resize_x = src->resize_x;
    dst->resize_height = src->resize_height;
    dst->resize_z = src->resize_z;
    dst->offset_x = src->offset_x;
    dst->offset_y = src->offset_y;
    dst->offset_z = src->offset_z;
    dst->recolor_count = src->recolor_count;
    dst->retexture_count = src->retexture_count;
    dst->map_scene_id = src->map_scene_id;

    if( src->shapes && src->shapes_and_model_count > 0 )
    {
        dst->shapes = malloc((size_t)src->shapes_and_model_count * sizeof(int));
        if( !dst->shapes )
            goto fail;
        memcpy(dst->shapes, src->shapes, (size_t)src->shapes_and_model_count * sizeof(int));
    }

    if( src->lengths && src->shapes_and_model_count > 0 )
    {
        dst->lengths = malloc((size_t)src->shapes_and_model_count * sizeof(int));
        if( !dst->lengths )
            goto fail;
        memcpy(dst->lengths, src->lengths, (size_t)src->shapes_and_model_count * sizeof(int));
    }

    if( src->models && src->shapes_and_model_count > 0 )
    {
        dst->models = calloc((size_t)src->shapes_and_model_count, sizeof(int*));
        if( !dst->models )
            goto fail;
        for( int i = 0; i < src->shapes_and_model_count; i++ )
        {
            int count = src->lengths ? src->lengths[i] : 0;
            if( count <= 0 )
                continue;
            dst->models[i] = malloc((size_t)count * sizeof(int));
            if( !dst->models[i] )
                goto fail;
            memcpy(dst->models[i], src->models[i], (size_t)count * sizeof(int));
        }
    }

    if( src->recolor_count > 0 && src->recolors_from && src->recolors_to )
    {
        dst->recolors_from = malloc((size_t)src->recolor_count * sizeof(int));
        dst->recolors_to = malloc((size_t)src->recolor_count * sizeof(int));
        if( !dst->recolors_from || !dst->recolors_to )
            goto fail;
        memcpy(dst->recolors_from, src->recolors_from, (size_t)src->recolor_count * sizeof(int));
        memcpy(dst->recolors_to, src->recolors_to, (size_t)src->recolor_count * sizeof(int));
    }

    if( src->retexture_count > 0 && src->retextures_from && src->retextures_to )
    {
        dst->retextures_from = malloc((size_t)src->retexture_count * sizeof(int));
        dst->retextures_to = malloc((size_t)src->retexture_count * sizeof(int));
        if( !dst->retextures_from || !dst->retextures_to )
            goto fail;
        memcpy(
            dst->retextures_from, src->retextures_from, (size_t)src->retexture_count * sizeof(int));
        memcpy(dst->retextures_to, src->retextures_to, (size_t)src->retexture_count * sizeof(int));
    }

    return dst;

fail:
    ToriAuxLibCore_LocationFree(dst);
    return NULL;
}

struct ToriAuxLibCore_Sequence*
ToriAuxLibC_SequenceNewFromCacheDatSequence(
    const void* cache_seq_ptr,
    int id)
{
    const struct CacheDatSequence* src = cache_seq_ptr;
    if( !src )
        return NULL;

    struct ToriAuxLibCore_Sequence* dst = calloc(1, sizeof(struct ToriAuxLibCore_Sequence));
    if( !dst )
        return NULL;

    dst->id = id;
    dst->frame_count = src->frame_count;
    dst->loops = src->loops;
    dst->stretches = src->stretches;
    dst->priority = src->priority;
    dst->replaceheldleft = src->replaceheldleft;
    dst->replaceheldright = src->replaceheldright;
    dst->maxloops = src->maxloops;
    dst->preanim_move = src->preanim_move;
    dst->postanim_move = src->postanim_move;
    dst->duplicate_behavior = src->duplicate_behavior;

    if( src->frame_count > 0 )
    {
        if( src->frames )
        {
            dst->frames = malloc((size_t)src->frame_count * sizeof(int));
            if( !dst->frames )
                goto fail;
            memcpy(dst->frames, src->frames, (size_t)src->frame_count * sizeof(int));
        }
        if( src->iframes )
        {
            dst->iframes = malloc((size_t)src->frame_count * sizeof(int));
            if( !dst->iframes )
                goto fail;
            memcpy(dst->iframes, src->iframes, (size_t)src->frame_count * sizeof(int));
        }
        if( src->delay )
        {
            dst->delay = malloc((size_t)src->frame_count * sizeof(int));
            if( !dst->delay )
                goto fail;
            memcpy(dst->delay, src->delay, (size_t)src->frame_count * sizeof(int));
        }
    }
    return dst;

fail:
    ToriAuxLibCore_SequenceFree(dst);
    return NULL;
}
