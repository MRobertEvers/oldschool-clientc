#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/core/toriauxlibcore_types.h"
#include "vm/csvm.h"
#include "vm/cs2vm.h"

#include "buildcache/dat2_buildcache.h"
#include "osrs/rscache/dat2a/dat2a_component.h"
#include "osrs/rscache/dat2a/dat2a_config_floortype.h"
#include "osrs/rscache/dat2a/dat2a_config_locs.h"
#include "osrs/rscache/dat2a/dat2a_config_sequence.h"
#include "osrs/rscache/dat2a/dat2a_frame.h"
#include "osrs/rscache/dat2a/dat2a_framemap.h"
#include "osrs/rscache/dat2a/dat2a_maps.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "osrs/rscache/dat2a/dat2a_sprites.h"
#include "osrs/rscache/dat2a/dat2a_textures.h"
#include "osrs/rscache/dat1a/dat1a_anim_frame.h"
#include "osrs/rscache/dat1a/dat1a_config_npc.h"
#include "osrs/rscache/dat1a/dat1a_config_obj.h"
#include "osrs/rscache/dat2a/dat2a_config_npctype.h"
#include "osrs/rscache/dat2a/dat2a_config_object.h"
#include "osrs/rscache/dat1a/dat1a_config_component.h"
#include "osrs/rscache/dat1a/dat1a_config_textures.h"
#include "osrs/palette.h"
#include "osrs/texture.h"
#include "toridraw/toridraw_model.h"

#include "ui/ui_debug.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void
toriauxlibcache_copy_menu_actions(
    char actions[TORIAUXLIBCORE_MENU_ACTION_SLOTS][TORIAUXLIBCORE_MENU_ACTION_LEN],
    char* const* src_actions,
    int src_count)
{
    for( int i = 0; i < TORIAUXLIBCORE_MENU_ACTION_SLOTS; i++ )
        actions[i][0] = '\0';

    if( !src_actions || src_count <= 0 )
        return;

    int const limit =
        src_count < TORIAUXLIBCORE_MENU_ACTION_SLOTS ? src_count : TORIAUXLIBCORE_MENU_ACTION_SLOTS;
    for( int i = 0; i < limit; i++ )
    {
        if( src_actions[i] && src_actions[i][0] != '\0' )
        {
            strncpy(actions[i], src_actions[i], TORIAUXLIBCORE_MENU_ACTION_LEN - 1);
            actions[i][TORIAUXLIBCORE_MENU_ACTION_LEN - 1] = '\0';
        }
    }
}

static void
toriauxlibcache_copy_menu_actions_from_loc(
    char actions[TORIAUXLIBCORE_MENU_ACTION_SLOTS][TORIAUXLIBCORE_MENU_ACTION_LEN],
    char* const* src_actions,
    int src_count)
{
    for( int i = 0; i < TORIAUXLIBCORE_MENU_ACTION_SLOTS; i++ )
        actions[i][0] = '\0';

    if( !src_actions )
        return;

    int const limit = src_count > 0 ? src_count : 10;
    for( int i = 0; i < TORIAUXLIBCORE_MENU_ACTION_SLOTS && i < limit; i++ )
    {
        if( src_actions[i] && src_actions[i][0] != '\0' )
        {
            strncpy(actions[i], src_actions[i], TORIAUXLIBCORE_MENU_ACTION_LEN - 1);
            actions[i][TORIAUXLIBCORE_MENU_ACTION_LEN - 1] = '\0';
        }
    }
}

static bool
toriauxlibcache_src_actions_nonempty(char* const* actions, int count)
{
    if( !actions || count <= 0 )
        return false;
    for( int i = 0; i < count; i++ )
    {
        if( actions[i] && actions[i][0] != '\0' )
            return true;
    }
    return false;
}

static bool
toriauxlibcache_core_ops_nonempty(
    char const ops[TORIAUXLIBCORE_MENU_ACTION_SLOTS][TORIAUXLIBCORE_MENU_ACTION_LEN])
{
    for( int i = 0; i < TORIAUXLIBCORE_MENU_ACTION_SLOTS; i++ )
    {
        if( ops[i][0] != '\0' )
            return true;
    }
    return false;
}

static void
toriauxlibcache_debug_log_dat1_convert(
    struct RSCacheDat1A_ConfigComponent const* src,
    struct ToriAuxLibCore_Component const* dst)
{
    if( !ui_minimenu_debug_enabled() || !src || !dst )
        return;

    bool const menu_relevant = src->buttonType != 0 || (src->option && src->option[0] != '\0') ||
                               toriauxlibcache_src_actions_nonempty(src->iop, 5);
    if( !menu_relevant )
        return;

    ui_minimenu_debug_log(
        "convert dat1 id=%d type=%d button_type=%d option='%s' iop=[%s%s%s%s%s] -> core "
        "option='%s' ops_nonempty=%d",
        src->id,
        src->type,
        src->buttonType,
        src->option ? src->option : "",
        (src->iop && src->iop[0] && src->iop[0][0]) ? src->iop[0] : "",
        (src->iop && src->iop[1] && src->iop[1][0]) ? src->iop[1] : "",
        (src->iop && src->iop[2] && src->iop[2][0]) ? src->iop[2] : "",
        (src->iop && src->iop[3] && src->iop[3][0]) ? src->iop[3] : "",
        (src->iop && src->iop[4] && src->iop[4][0]) ? src->iop[4] : "",
        dst->option,
        toriauxlibcache_core_ops_nonempty(dst->ops));
}

static void
toriauxlibcache_debug_log_dat2_convert(
    Component const* src,
    struct ToriAuxLibCore_Component const* dst)
{
    if( !ui_minimenu_debug_enabled() || !src || !dst )
        return;

    bool const has_obj_ops =
        src->type == COMPONENT_TYPE_INV && toriauxlibcache_src_actions_nonempty(src->objOps, 5);
    bool const has_ops = toriauxlibcache_src_actions_nonempty(src->ops, src->opsLen);
    bool const menu_relevant = src->buttonType != 0 || (src->option && src->option[0] != '\0') ||
                               has_obj_ops || has_ops;
    if( !menu_relevant )
        return;

    ui_minimenu_debug_log(
        "convert dat2 id=%d type=%d button_type=%d option='%s' objOps=[%s%s%s%s%s] opsLen=%d -> "
        "core option='%s' ops_nonempty=%d",
        src->id,
        src->type,
        src->buttonType,
        src->option ? src->option : "",
        (src->objOps && src->objOps[0] && src->objOps[0][0]) ? src->objOps[0] : "",
        (src->objOps && src->objOps[1] && src->objOps[1][0]) ? src->objOps[1] : "",
        (src->objOps && src->objOps[2] && src->objOps[2][0]) ? src->objOps[2] : "",
        (src->objOps && src->objOps[3] && src->objOps[3][0]) ? src->objOps[3] : "",
        (src->objOps && src->objOps[4] && src->objOps[4][0]) ? src->objOps[4] : "",
        src->opsLen,
        dst->option,
        toriauxlibcache_core_ops_nonempty(dst->ops));
}

static struct ToriAuxLibCore_Texture*
toriauxlibcache_texture_new_from_toridraw(const struct ToriDraw_Texture* src)
{
    if( !src || !src->texels || src->width <= 0 || src->height <= 0 )
        return NULL;

    struct ToriAuxLibCore_Texture* texture = malloc(sizeof(struct ToriAuxLibCore_Texture));
    if( !texture )
        return NULL;

    size_t pixel_count = (size_t)src->width * (size_t)src->height;
    int* texels = malloc(pixel_count * sizeof(int));
    if( !texels )
    {
        free(texture);
        return NULL;
    }
    memcpy(texels, src->texels, pixel_count * sizeof(int));

    memset(texture, 0, sizeof(struct ToriAuxLibCore_Texture));
    texture->texels = texels;
    texture->width = src->width;
    texture->height = src->height;
    texture->opaque = src->opaque;
    texture->animation_direction = src->animation_direction;
    texture->animation_speed = src->animation_speed;
    return texture;
}

static struct ToriAuxLibCore_AnimBase*
toriauxlibcache_animbase_move_from_cache(struct RSCacheDat1A_AnimBase* cache_base)
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
ToriAuxLibCache_AnimationNewFromCacheDatAnimbaseframes(const void* abf_ptr)
{
    struct RSCacheDat1A_AnimBaseFrames* abf = (struct RSCacheDat1A_AnimBaseFrames*)abf_ptr;
    if( !abf )
        return NULL;

    struct ToriAuxLibCore_Animation* anim = malloc(sizeof(struct ToriAuxLibCore_Animation));
    if( !anim )
        return NULL;

    memset(anim, 0, sizeof(struct ToriAuxLibCore_Animation));
    anim->base = toriauxlibcache_animbase_move_from_cache(abf->base);
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
            struct RSCacheDat1A_AnimFrame* cf = &abf->frames[i];
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
ToriAuxLibCache_TextureNewFromCacheDatTexture(
    const void* cache_texture_ptr,
    int animation_direction,
    int animation_speed)
{
    struct RSCacheDat1A_ConfigTexture* cache_texture = (struct RSCacheDat1A_ConfigTexture*)cache_texture_ptr;
    if( !cache_texture )
        return NULL;

    struct ToriDraw_Texture* td_texture = texture_new_toridraw_from_texture_sprite(
        cache_texture, animation_direction, animation_speed, false, false);
    if( !td_texture )
        return NULL;

    struct ToriAuxLibCore_Texture* texture = toriauxlibcache_texture_new_from_toridraw(td_texture);
    ToriDraw_TextureFree(td_texture);
    return texture;
}

struct ToriAuxLibCore_Texture*
ToriAuxLibCache_TextureNewFromDat2Definition(
    struct RSCacheDat2A_Texture* def,
    struct RSCacheDat2A_SpritePack** packs,
    int animation_direction,
    int animation_speed)
{
    if( !def )
        return NULL;

    struct ToriDraw_Texture* td_texture =
        texture_new_toridraw_from_definition_packs(def, packs);
    if( !td_texture )
        return NULL;

    struct ToriAuxLibCore_Texture* texture = toriauxlibcache_texture_new_from_toridraw(td_texture);
    if( !texture )
    {
        ToriDraw_TextureFree(td_texture);
        return NULL;
    }

    texture->animation_direction = animation_direction;
    texture->animation_speed = animation_speed;
    texture->average_hsl = def->average_hsl;
    if( texture->average_hsl == 0 && texture->texels && texture->width > 0 && texture->height > 0 )
    {
        int red = 0;
        int green = 0;
        int blue = 0;
        int colour_count = texture->width * texture->height;
        for( int i = 0; i < colour_count; i++ )
        {
            red += (texture->texels[i] >> 16) & 0xff;
            green += (texture->texels[i] >> 8) & 0xff;
            blue += texture->texels[i] & 0xff;
        }
        int average_rgb = ((red / colour_count) << 16) + ((green / colour_count) << 8) +
                          ((blue / colour_count) | 0);
        texture->average_hsl = palette_rgb_to_hsl16(average_rgb);
    }

    ToriDraw_TextureFree(td_texture);
    return texture;
}

struct ToriAuxLibCore_MapTerrain*
ToriAuxLibCache_MapTerrainNewFromCacheMapTerrain(const void* cache_terrain_ptr)
{
    const struct RSCacheDat2A_MapTerrain* src = cache_terrain_ptr;
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
        const struct RSCacheDat2A_MapFloor* s = &src->tiles_xyz[i];
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
ToriAuxLibCache_MapLocsNewFromCacheMapLocs(const void* cache_locs_ptr)
{
    const struct RSCacheDat2A_MapLocs* src = cache_locs_ptr;
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
ToriAuxLibCache_FlotypeNewFromCacheConfigOverlay(
    const void* cache_overlay_ptr,
    int id)
{
    const struct RSCacheDat2A_ConfigOverlay* src = cache_overlay_ptr;
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
ToriAuxLibCache_LocationNewFromCacheConfigLocation(const void* cache_loc_ptr)
{
    const struct RSCacheDat2A_ConfigLocation* src = cache_loc_ptr;
    if( !src )
        return NULL;

    struct ToriAuxLibCore_Location* dst = calloc(1, sizeof(struct ToriAuxLibCore_Location));
    if( !dst )
        return NULL;

    dst->id = src->_id;
    dst->map_scene_id = -1;
    if( src->name )
    {
        strncpy(dst->name, src->name, TORIAUXLIBCORE_NAME_MAX - 1);
        dst->name[TORIAUXLIBCORE_NAME_MAX - 1] = '\0';
    }
    toriauxlibcache_copy_menu_actions_from_loc(dst->actions, src->actions, 10);
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
    dst->transform_varbit = src->transform_varbit;
    dst->transform_varp = src->transform_varp;
    dst->transform_count = src->transform_count;

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

    if( src->transform_count > 0 && src->transforms )
    {
        dst->transforms = malloc((size_t)src->transform_count * sizeof(int));
        if( !dst->transforms )
            goto fail;
        memcpy(dst->transforms, src->transforms, (size_t)src->transform_count * sizeof(int));
    }

    return dst;

fail:
    ToriAuxLibCore_LocationFree(dst);
    return NULL;
}

struct ToriAuxLibCore_Npctype*
ToriAuxLibCache_NpctypeNewFromDat1ConfigNpc(
    const void* cache_npc_ptr,
    int npc_id)
{
    const struct RSCacheDat1A_ConfigNpc* src = cache_npc_ptr;
    if( !src )
        return NULL;

    struct ToriAuxLibCore_Npctype* dst = calloc(1, sizeof(struct ToriAuxLibCore_Npctype));
    if( !dst )
        return NULL;

    dst->id = npc_id;
    if( src->name )
    {
        strncpy(dst->name, src->name, TORIAUXLIBCORE_NAME_MAX - 1);
        dst->name[TORIAUXLIBCORE_NAME_MAX - 1] = '\0';
    }
    toriauxlibcache_copy_menu_actions(dst->actions, (char* const*)src->op, 5);
    dst->combat_level = src->vislevel;
    dst->size = src->size > 0 ? src->size : 1;
    return dst;
}

struct ToriAuxLibCore_Npctype*
ToriAuxLibCache_NpctypeNewFromDat2ConfigNpctype(
    const void* cache_npc_ptr,
    int npc_id)
{
    const struct RSCacheDat2A_ConfigNpctype* src = cache_npc_ptr;
    if( !src )
        return NULL;

    struct ToriAuxLibCore_Npctype* dst = calloc(1, sizeof(struct ToriAuxLibCore_Npctype));
    if( !dst )
        return NULL;

    dst->id = npc_id;
    if( src->name )
    {
        strncpy(dst->name, src->name, TORIAUXLIBCORE_NAME_MAX - 1);
        dst->name[TORIAUXLIBCORE_NAME_MAX - 1] = '\0';
    }
    toriauxlibcache_copy_menu_actions(dst->actions, (char* const*)src->actions, 5);
    dst->combat_level = src->combat_level;
    dst->size = src->size > 0 ? src->size : 1;
    return dst;
}

struct ToriAuxLibCore_Objtype*
ToriAuxLibCache_ObjtypeNewFromDat1ConfigObj(
    const void* cache_obj_ptr,
    int obj_id)
{
    const struct RSCacheDat1A_ConfigObj* src = cache_obj_ptr;
    if( !src )
        return NULL;

    struct ToriAuxLibCore_Objtype* dst = calloc(1, sizeof(struct ToriAuxLibCore_Objtype));
    if( !dst )
        return NULL;

    dst->id = obj_id;
    if( src->name )
    {
        strncpy(dst->name, src->name, TORIAUXLIBCORE_NAME_MAX - 1);
        dst->name[TORIAUXLIBCORE_NAME_MAX - 1] = '\0';
    }
    toriauxlibcache_copy_menu_actions(dst->inv_actions, (char* const*)src->iop, 5);
    dst->stackable = src->stackable ? 1 : 0;
    return dst;
}

struct ToriAuxLibCore_Objtype*
ToriAuxLibCache_ObjtypeNewFromDat2ConfigObject(
    const void* cache_obj_ptr,
    int obj_id)
{
    const struct RSCacheDat2A_ConfigObject* src = cache_obj_ptr;
    if( !src )
        return NULL;

    struct ToriAuxLibCore_Objtype* dst = calloc(1, sizeof(struct ToriAuxLibCore_Objtype));
    if( !dst )
        return NULL;

    dst->id = obj_id;
    if( src->name )
    {
        strncpy(dst->name, src->name, TORIAUXLIBCORE_NAME_MAX - 1);
        dst->name[TORIAUXLIBCORE_NAME_MAX - 1] = '\0';
    }
    toriauxlibcache_copy_menu_actions(dst->inv_actions, (char* const*)src->if_actions, 5);
    dst->stackable = src->stacking_behaviour != 0 ? 1 : 0;
    return dst;
}

struct ToriAuxLibCore_Sequence*
ToriAuxLibCache_SequenceNewFromCacheDatSequence(
    const void* cache_seq_ptr,
    int id)
{
    const struct RSCacheDat1A_ConfigSequence* src = cache_seq_ptr;
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

struct ToriAuxLibCore_Flotype*
ToriAuxLibCache_UnderlayNewFromCacheConfigUnderlay(
    const void* cache_underlay_ptr,
    int id)
{
    const struct RSCacheDat2A_ConfigUnderlay* src = cache_underlay_ptr;
    if( !src )
        return NULL;

    struct ToriAuxLibCore_Flotype* dst = malloc(sizeof(struct ToriAuxLibCore_Flotype));
    if( !dst )
        return NULL;

    dst->id = id;
    dst->rgb_color = src->rgb_color;
    dst->texture = -1;
    dst->secondary_rgb_color = 0;
    dst->hide_underlay = false;
    return dst;
}

struct ToriAuxLibCore_Sequence*
ToriAuxLibCache_SequenceNewFromCacheDat2Sequence(
    const void* cache_seq_ptr,
    int id)
{
    const struct RSCacheDat2A_ConfigSequence* src = cache_seq_ptr;
    if( !src )
        return NULL;

    struct ToriAuxLibCore_Sequence* dst = calloc(1, sizeof(struct ToriAuxLibCore_Sequence));
    if( !dst )
        return NULL;

    dst->id = id;
    dst->frame_count = src->frame_count;
    dst->loops = src->max_loops;
    dst->stretches = src->stretches;
    dst->priority = src->priority;
    dst->replaceheldleft = src->left_hand_item;
    dst->replaceheldright = src->right_hand_item;
    dst->maxloops = src->max_loops;
    dst->preanim_move = src->precedence_animating;
    dst->postanim_move = src->reply_mode;
    dst->anim_maya_id = src->anim_maya_id;
    dst->anim_maya_start = src->anim_maya_start;
    dst->anim_maya_end = src->anim_maya_end;

    if( src->frame_count > 0 )
    {
        if( src->frame_ids )
        {
            dst->frames = malloc((size_t)src->frame_count * sizeof(int));
            if( !dst->frames )
                goto fail;
            memcpy(dst->frames, src->frame_ids, (size_t)src->frame_count * sizeof(int));
        }
        if( src->frame_lengths )
        {
            dst->delay = malloc((size_t)src->frame_count * sizeof(int));
            if( !dst->delay )
                goto fail;
            memcpy(dst->delay, src->frame_lengths, (size_t)src->frame_count * sizeof(int));
        }
    }
    return dst;

fail:
    ToriAuxLibCore_SequenceFree(dst);
    return NULL;
}

static enum ToriAuxLibCore_ComponentType
toriauxlibcache_component_type_from_raw(int type)
{
    switch( type )
    {
    case COMPONENT_TYPE_LAYER:
        return TORIAUXLIBCORE_COMPONENT_LAYER;
    case COMPONENT_TYPE_INV:
        return TORIAUXLIBCORE_COMPONENT_INV;
    case COMPONENT_TYPE_RECT:
        return TORIAUXLIBCORE_COMPONENT_RECT;
    case COMPONENT_TYPE_TEXT:
        return TORIAUXLIBCORE_COMPONENT_TEXT;
    case COMPONENT_TYPE_GRAPHIC:
        return TORIAUXLIBCORE_COMPONENT_GRAPHIC;
    case COMPONENT_TYPE_MODEL:
        return TORIAUXLIBCORE_COMPONENT_MODEL;
    case COMPONENT_TYPE_INV_TEXT:
        return TORIAUXLIBCORE_COMPONENT_INV_TEXT;
    default:
        return TORIAUXLIBCORE_COMPONENT_LAYER;
    }
}

static void
toriauxlibcache_component_apply_graphic_hitbox_only(struct ToriAuxLibCore_Component* dst)
{
    if( !dst || dst->type != TORIAUXLIBCORE_COMPONENT_GRAPHIC )
        return;
    dst->graphic_hitbox_only =
        (dst->sprite_ref[0] == '\0' && dst->sprite_active_ref[0] == '\0') ? 1 : 0;
}

static void
toriauxlibcache_dat2_sprite_ref_from_id(
    int sprite_id,
    char* out,
    size_t out_size)
{
    if( !out || out_size == 0 )
        return;
    if( sprite_id < 0 )
    {
        out[0] = '\0';
        return;
    }
    snprintf(out, out_size, "spr:%d", sprite_id);
}

static void
toriauxlibcache_component_copy_scripts(
    struct ToriAuxLibCore_Component* dst,
    int scripts_count,
    int** scripts,
    int* scripts_lengths,
    int* script_comparator,
    int* script_operand)
{
    dst->scripts_count = scripts_count;
    if( scripts_count <= 0 || !scripts )
        return;

    dst->scripts = calloc((size_t)scripts_count, sizeof(int*));
    dst->scripts_lengths = calloc((size_t)scripts_count, sizeof(int));
    if( !dst->scripts || !dst->scripts_lengths )
        return;

    for( int i = 0; i < scripts_count; i++ )
    {
        if( !scripts[i] )
            continue;

        int len = (scripts_lengths && scripts_lengths[i] > 0)
                      ? scripts_lengths[i]
                      : csvm_script_length(scripts[i]);
        if( len <= 0 )
            continue;

        dst->scripts[i] = malloc((size_t)len * sizeof(int));
        if( !dst->scripts[i] )
            continue;
        memcpy(dst->scripts[i], scripts[i], (size_t)len * sizeof(int));
        dst->scripts_lengths[i] = len;
    }

    if( script_comparator )
    {
        dst->script_comparator = malloc((size_t)scripts_count * sizeof(int));
        if( dst->script_comparator )
            memcpy(dst->script_comparator, script_comparator, (size_t)scripts_count * sizeof(int));
    }

    if( script_operand )
    {
        dst->script_operand = malloc((size_t)scripts_count * sizeof(int));
        if( dst->script_operand )
            memcpy(dst->script_operand, script_operand, (size_t)scripts_count * sizeof(int));
    }
}

static void
toriauxlibcache_copy_script_hook(
    struct ToriAuxLibCore_ScriptHook* dst,
    ComponentScriptVar* src,
    int32_t src_len)
{
    if( !dst )
        return;
    memset(dst, 0, sizeof(*dst));
    if( !src || src_len <= 0 )
        return;

    int n = src_len;
    if( n > TORIAUXLIBCORE_COMPONENT_HOOK_ARG_MAX )
        n = TORIAUXLIBCORE_COMPONENT_HOOK_ARG_MAX;
    dst->argc = n;
    for( int i = 0; i < n; i++ )
    {
        if( src[i].type == SCRIPT_VAR_INT )
            dst->argv[i] = src[i].value.i;
        else
            dst->argv[i] = 0;
    }
}

static void
toriauxlibcache_component_copy_dat2_cs1_scripts(
    struct ToriAuxLibCore_Component* dst,
    const Component* src)
{
    if( !dst || !src || src->cs1ScriptsLen <= 0 || !src->cs1Scripts )
        return;

    int** scripts = (int**)src->cs1Scripts;
    int* lengths = src->cs1ScriptsLengths;
    toriauxlibcache_component_copy_scripts(
        dst,
        src->cs1ScriptsLen,
        scripts,
        lengths,
        src->cs1ComparisonOpcodes,
        src->cs1ComparisonOperands);
    dst->script_kind = CS2VM_SCRIPT_KIND_CS1;
}

static void
toriauxlibcache_component_copy_dat2_hooks(
    struct ToriAuxLibCore_Component* dst,
    const Component* src)
{
    if( !dst || !src || !src->if3 )
        return;

    toriauxlibcache_copy_script_hook(&dst->on_load, src->onLoad, src->onLoadLen);
    toriauxlibcache_copy_script_hook(&dst->on_click, src->onClick, src->onClickLen);
    toriauxlibcache_copy_script_hook(
        &dst->on_varp_transmit, src->onVarpTransmit, src->onVarpTransmitLen);

    if( dst->scripts_count <= 0 &&
        (dst->on_load.argc > 0 || dst->on_click.argc > 0 || dst->on_varp_transmit.argc > 0) )
        dst->script_kind = CS2VM_SCRIPT_KIND_CS2;
}

struct ToriAuxLibCore_Component*
ToriAuxLibCache_ComponentNewFromCacheComponent(const void* cache_component_ptr)
{
    const struct RSCacheDat1A_ConfigComponent* src = cache_component_ptr;
    if( !src )
        return NULL;

    struct ToriAuxLibCore_Component* dst = calloc(1, sizeof(struct ToriAuxLibCore_Component));
    if( !dst )
        return NULL;

    dst->id = src->id;
    dst->type = toriauxlibcache_component_type_from_raw(src->type);
    dst->width = src->width;
    dst->height = src->height;
    dst->model_id = src->model;
    dst->color = src->colour;
    dst->filled = src->fill ? 1 : 0;
    dst->font_id = src->font;
    dst->center = src->center ? 1 : 0;
    dst->shadowed = src->shadowed ? 1 : 0;
    dst->inv_cols = src->width;
    dst->inv_rows = src->height;
    dst->margin_x = src->marginX;
    dst->margin_y = src->marginY;
    dst->hide = src->hide ? 1 : 0;
    dst->button_type = src->buttonType;
    dst->client_code = src->clientCode;
    dst->over_color = src->overColour;
    dst->active_color = src->activeColour;
    dst->active_over_color = src->activeOverColour;
    dst->over_layer_id = src->overlayer;
    dst->parent_id = -1;
    if( src->type == COMPONENT_TYPE_LAYER )
        dst->scroll_height = src->scroll;

    if( src->graphic )
        strncpy(dst->sprite_ref, src->graphic, sizeof(dst->sprite_ref) - 1);
    if( src->activeGraphic )
        strncpy(dst->sprite_active_ref, src->activeGraphic, sizeof(dst->sprite_active_ref) - 1);
    if( src->text )
        strncpy(dst->text, src->text, sizeof(dst->text) - 1);
    if( src->activeText )
        strncpy(dst->active_text, src->activeText, sizeof(dst->active_text) - 1);
    if( src->option )
        strncpy(dst->option, src->option, sizeof(dst->option) - 1);
    toriauxlibcache_copy_menu_actions(dst->ops, src->iop, 5);

    toriauxlibcache_component_apply_graphic_hitbox_only(dst);

    toriauxlibcache_component_copy_scripts(
        dst,
        src->scripts_count,
        src->scripts,
        src->scripts_lengths,
        src->scriptComparator,
        src->scriptOperand);
    dst->script_kind = CS2VM_SCRIPT_KIND_CS1;

    toriauxlibcache_debug_log_dat1_convert(src, dst);

    return dst;
}

struct ToriAuxLibCore_Component*
ToriAuxLibCache_ComponentNewFromCacheDat2Component(const void* cache_component_ptr)
{
    const Component* src = cache_component_ptr;
    if( !src )
        return NULL;

    struct ToriAuxLibCore_Component* dst = calloc(1, sizeof(struct ToriAuxLibCore_Component));
    if( !dst )
        return NULL;

    dst->id = src->id;
    dst->type = toriauxlibcache_component_type_from_raw(src->type);
    dst->width = src->baseWidth;
    dst->height = src->baseHeight;
    dst->model_id = src->modelId;
    dst->color = src->color;
    dst->filled = src->fill ? 1 : 0;
    dst->font_id = src->textFont;
    dst->center = src->textHorizontalAlignment != 0 ? 1 : 0;
    dst->shadowed = src->textShadow ? 1 : 0;
    dst->inv_cols = src->baseWidth;
    dst->inv_rows = src->baseHeight;
    dst->margin_x = src->marginX;
    dst->margin_y = src->marginY;
    dst->hide = src->hidden ? 1 : 0;
    dst->button_type = src->buttonType;
    dst->client_code = src->clientCode;
    dst->over_color = src->overColour;
    dst->active_color = src->activeColour;
    dst->active_over_color = src->activeOverColour;
    dst->over_layer_id = src->linkedComponentId;
    dst->parent_id = -1;
    if( src->type == COMPONENT_TYPE_LAYER )
    {
        dst->scroll_height = src->scrollHeight;
        dst->scroll_width = src->scrollWidth;
    }

    toriauxlibcache_dat2_sprite_ref_from_id(src->graphic, dst->sprite_ref, sizeof(dst->sprite_ref));
    toriauxlibcache_dat2_sprite_ref_from_id(
        src->activeGraphic, dst->sprite_active_ref, sizeof(dst->sprite_active_ref));

    if( src->text )
        strncpy(dst->text, src->text, sizeof(dst->text) - 1);
    if( src->activeText )
        strncpy(dst->active_text, src->activeText, sizeof(dst->active_text) - 1);
    if( src->option )
        strncpy(dst->option, src->option, sizeof(dst->option) - 1);
    if( src->type == COMPONENT_TYPE_INV && src->objOps )
        toriauxlibcache_copy_menu_actions(dst->ops, src->objOps, 5);
    else
        toriauxlibcache_copy_menu_actions(dst->ops, src->ops, src->opsLen);

    if( src->type == COMPONENT_TYPE_INV && src->objOps )
    {
        bool src_has_obj_ops = false;
        for( int i = 0; i < 5; i++ )
        {
            if( src->objOps[i] && src->objOps[i][0] != '\0' )
            {
                src_has_obj_ops = true;
                break;
            }
        }
        if( src_has_obj_ops )
        {
            bool copied = false;
            for( int i = 0; i < TORIAUXLIBCORE_MENU_ACTION_SLOTS; i++ )
            {
                if( dst->ops[i][0] != '\0' )
                {
                    copied = true;
                    break;
                }
            }
            assert(
                copied &&
                "dat2 INV objOps present in cache but core component ops[] empty after convert");
        }
    }

    toriauxlibcache_component_apply_graphic_hitbox_only(dst);

    toriauxlibcache_component_copy_dat2_cs1_scripts(dst, src);
    toriauxlibcache_component_copy_dat2_hooks(dst, src);

    toriauxlibcache_debug_log_dat2_convert(src, dst);

    return dst;
}

struct ToriAuxLibCore_Animation*
ToriAuxLibCache_AnimationNewFromDat2FramesArchive(
    const struct Dat2BuildCache_FramesArchive* fa,
    int archive_id)
{
    if( !fa || !fa->framemap || fa->frame_count <= 0 )
        return NULL;

    struct ToriAuxLibCore_Animation* anim = calloc(1, sizeof(struct ToriAuxLibCore_Animation));
    if( !anim )
        return NULL;

    /* Base: copy framemap types and bone_groups */
    struct ToriAuxLibCore_AnimBase* base = calloc(1, sizeof(struct ToriAuxLibCore_AnimBase));
    if( !base )
        goto fail;
    anim->base = base;

    const struct RSCacheDat2A_Framemap* fm = fa->framemap;
    base->length = fm->length;
    if( fm->length > 0 )
    {
        base->types = malloc((size_t)fm->length);
        base->bone_group_lengths = malloc((size_t)fm->length * sizeof(uint16_t));
        base->bone_groups = calloc((size_t)fm->length, sizeof(uint8_t*));
        if( !base->types || !base->bone_group_lengths || !base->bone_groups )
            goto fail;

        for( int i = 0; i < fm->length; i++ )
        {
            base->types[i] = (uint8_t)fm->types[i];
            int glen = fm->bone_groups_lengths[i];
            base->bone_group_lengths[i] = (uint16_t)glen;
            if( glen > 0 && fm->bone_groups[i] )
            {
                base->bone_groups[i] = malloc((size_t)glen);
                if( !base->bone_groups[i] )
                    goto fail;
                for( int j = 0; j < glen; j++ )
                    base->bone_groups[i][j] = (uint8_t)fm->bone_groups[i][j];
            }
        }
    }

    /* Frames */
    anim->frame_count = fa->frame_count;
    anim->frames = calloc((size_t)fa->frame_count, sizeof(struct ToriAuxLibCore_AnimFrame));
    if( !anim->frames )
        goto fail;

    for( int i = 0; i < fa->frame_count; i++ )
    {
        const struct RSCacheDat2A_Frame* src = fa->frames[i];
        struct ToriAuxLibCore_AnimFrame* dst = &anim->frames[i];

        if( !src )
        {
            dst->id = (archive_id << 16) | i;
            continue;
        }

        dst->id = src->_id;
        dst->length = src->translator_count;

        if( src->translator_count > 0 )
        {
            dst->groups = malloc((size_t)src->translator_count * sizeof(int16_t));
            dst->x      = malloc((size_t)src->translator_count * sizeof(int16_t));
            dst->y      = malloc((size_t)src->translator_count * sizeof(int16_t));
            dst->z      = malloc((size_t)src->translator_count * sizeof(int16_t));
            if( !dst->groups || !dst->x || !dst->y || !dst->z )
                goto fail;

            for( int j = 0; j < src->translator_count; j++ )
            {
                dst->groups[j] = (int16_t)src->index_frame_ids[j];
                dst->x[j]      = (int16_t)src->translator_arg_x[j];
                dst->y[j]      = (int16_t)src->translator_arg_y[j];
                dst->z[j]      = (int16_t)src->translator_arg_z[j];
            }
        }
    }

    return anim;

fail:
    ToriAuxLibCore_AnimationFree(anim);
    return NULL;
}

struct ToriAuxLibCore_SkeletalAnim*
ToriAuxLibCache_SkeletalAnimNewFromBakedPalette(
    int    anim_id,
    float* palette,
    int    frame_count,
    int    bone_count)
{
    if( !palette || frame_count <= 0 || bone_count <= 0 )
    {
        free(palette);
        return NULL;
    }

    struct ToriAuxLibCore_SkeletalAnim* dst = calloc(1, sizeof(struct ToriAuxLibCore_SkeletalAnim));
    if( !dst )
    {
        free(palette);
        return NULL;
    }

    dst->id          = anim_id;
    dst->bone_count  = bone_count;
    dst->frame_count = frame_count;
    dst->matrices    = palette; /* ownership transferred */
    return dst;
}
