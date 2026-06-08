#include "gamecache_types.h"
#include "osrs/rscache/tables/config_floortype.h"
#include "osrs/rscache/tables/config_locs.h"
#include "osrs/rscache/tables/config_sequence.h"
#include "osrs/rscache/tables/maps.h"

#include <stdlib.h>
#include <string.h>

void
gamecache_map_terrain_free(struct GameCache_MapTerrain* terrain)
{
    free(terrain);
}

void
gamecache_map_locs_free(struct GameCache_MapLocs* locs)
{
    if( !locs )
        return;
    free(locs->locs);
    free(locs);
}

void
gamecache_flotype_free(struct GameCache_Flotype* flotype)
{
    free(flotype);
}

static void
gamecache_location_free_models(struct GameCache_Location* loc)
{
    if( !loc || !loc->models )
        return;
    for( int i = 0; i < loc->shapes_and_model_count; i++ )
        free(loc->models[i]);
    free(loc->models);
    loc->models = NULL;
}

void
gamecache_location_free(struct GameCache_Location* loc)
{
    if( !loc )
        return;
    free(loc->shapes);
    gamecache_location_free_models(loc);
    free(loc->lengths);
    free(loc->recolors_from);
    free(loc->recolors_to);
    free(loc->retextures_from);
    free(loc->retextures_to);
    free(loc);
}

void
gamecache_sequence_free(struct GameCache_Sequence* seq)
{
    if( !seq )
        return;
    free(seq->frames);
    free(seq->iframes);
    free(seq->delay);
    free(seq);
}

struct GameCache_MapTerrain*
gamecache_map_terrain_new_from_cache_map_terrain(const void* cache_terrain_ptr)
{
    const struct CacheMapTerrain* src = cache_terrain_ptr;
    if( !src )
        return NULL;

    struct GameCache_MapTerrain* dst = malloc(sizeof(struct GameCache_MapTerrain));
    if( !dst )
        return NULL;

    dst->map_x = src->map_x;
    dst->map_z = src->map_z;
    for( int i = 0;
         i < GAMECACHE_MAP_TERRAIN_X * GAMECACHE_MAP_TERRAIN_Z * GAMECACHE_MAP_TERRAIN_LEVELS;
         i++ )
    {
        const struct CacheMapFloor* s = &src->tiles_xyz[i];
        struct GameCache_MapFloor* d = &dst->tiles_xyz[i];
        d->overlay_id = s->overlay_id;
        d->underlay_id = s->underlay_id;
        d->height = s->height;
        d->settings = s->settings;
        d->shape = s->shape;
        d->rotation = s->rotation;
    }
    return dst;
}

struct GameCache_MapLocs*
gamecache_map_locs_new_from_cache_map_locs(const void* cache_locs_ptr)
{
    const struct CacheMapLocs* src = cache_locs_ptr;
    if( !src )
        return NULL;

    struct GameCache_MapLocs* dst = malloc(sizeof(struct GameCache_MapLocs));
    if( !dst )
        return NULL;

    dst->chunk_mapx = src->_chunk_mapx;
    dst->chunk_mapz = src->_chunk_mapz;
    dst->locs_count = src->locs_count;
    dst->locs = NULL;

    if( src->locs_count > 0 )
    {
        dst->locs = malloc((size_t)src->locs_count * sizeof(struct GameCache_MapLoc));
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

struct GameCache_Flotype*
gamecache_flotype_new_from_cache_config_overlay(
    const void* cache_overlay_ptr,
    int id)
{
    const struct CacheConfigOverlay* src = cache_overlay_ptr;
    if( !src )
        return NULL;

    struct GameCache_Flotype* dst = malloc(sizeof(struct GameCache_Flotype));
    if( !dst )
        return NULL;

    dst->id = id;
    dst->rgb_color = src->rgb_color;
    dst->texture = src->texture;
    dst->secondary_rgb_color = src->secondary_rgb_color;
    dst->hide_underlay = src->hide_underlay;
    return dst;
}

struct GameCache_Location*
gamecache_location_new_from_cache_config_location(const void* cache_loc_ptr)
{
    const struct CacheConfigLocation* src = cache_loc_ptr;
    if( !src )
        return NULL;

    struct GameCache_Location* dst = calloc(1, sizeof(struct GameCache_Location));
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
    gamecache_location_free(dst);
    return NULL;
}

struct GameCache_Sequence*
gamecache_sequence_new_from_cache_dat_sequence(
    const void* cache_seq_ptr,
    int id)
{
    const struct CacheDatSequence* src = cache_seq_ptr;
    if( !src )
        return NULL;

    struct GameCache_Sequence* dst = calloc(1, sizeof(struct GameCache_Sequence));
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
    gamecache_sequence_free(dst);
    return NULL;
}
