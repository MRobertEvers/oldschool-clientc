#include "engine/torirs_location_from_rscache.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void
torirs_copy_menu_actions(
    char actions[TORIRS_MENU_ACTION_SLOTS][TORIRS_MENU_ACTION_LEN],
    char* const* src_actions)
{
    for( int i = 0; i < TORIRS_MENU_ACTION_SLOTS; i++ )
        actions[i][0] = '\0';

    if( !src_actions )
        return;

    for( int i = 0; i < TORIRS_MENU_ACTION_SLOTS; i++ )
    {
        if( src_actions[i] && src_actions[i][0] != '\0' )
        {
            strncpy(actions[i], src_actions[i], TORIRS_MENU_ACTION_LEN - 1);
            actions[i][TORIRS_MENU_ACTION_LEN - 1] = '\0';
        }
    }
}

static int*
torirs_dup_int_array(
    const int* src,
    int count)
{
    int* dst;

    if( count <= 0 || !src )
        return NULL;

    dst = malloc((size_t)count * sizeof(int));
    assert(dst);
    memcpy(dst, src, (size_t)count * sizeof(int));
    return dst;
}

static void
torirs_location_copy_shapes_models(
    struct ToriRS_Location* dst,
    const struct RSCache_Dat2ConfigLoc* src)
{
    dst->shapes_and_model_count = src->shapes_and_model_count;
    dst->shapes = torirs_dup_int_array(src->shapes, src->shapes_and_model_count);
    dst->lengths = torirs_dup_int_array(src->lengths, src->shapes_and_model_count);

    if( !src->models || src->shapes_and_model_count <= 0 )
        return;

    dst->models = calloc((size_t)src->shapes_and_model_count, sizeof(int*));
    assert(dst->models);
    for( int i = 0; i < src->shapes_and_model_count; i++ )
    {
        int len = src->lengths ? src->lengths[i] : 0;
        dst->models[i] = torirs_dup_int_array(src->models[i], len);
    }
}

struct ToriRS_Location*
ToriRS_LocationFromRSCacheDat2(
    int loc_id,
    const struct RSCache_Dat2ConfigLoc* src)
{
    struct ToriRS_Location* loc;

    assert(src);

    loc = calloc(1, sizeof(*loc));
    assert(loc);

    loc->id = loc_id;
    if( src->name )
    {
        strncpy(loc->name, src->name, TORIRS_NAME_MAX - 1);
        loc->name[TORIRS_NAME_MAX - 1] = '\0';
    }
    if( src->desc )
    {
        strncpy(loc->desc, src->desc, TORIRS_DESC_MAX - 1);
        loc->desc[TORIRS_DESC_MAX - 1] = '\0';
    }

    torirs_copy_menu_actions(loc->actions, src->actions);
    torirs_location_copy_shapes_models(loc, src);

    /* LocType.active (Client-TS LocType.ts:248-256): the decoder already
     * derives it when opcode 19 is absent. A non-active loc gets its scene
     * typecode negated in the reference, which makes Model.draw skip pick
     * registration entirely — walls, gravel and floor decor never reach the
     * right-click menu. */
    loc->is_interactive = src->is_interactive ? 1 : 0;

    loc->size_x = src->size_x;
    loc->size_z = src->size_z;
    loc->blocks_walk = src->blocks_walk;
    loc->blocks_projectiles = src->blocks_projectiles;
    /* LocType.forceapproach (config opcode 69), still in the loc's unrotated
     * frame — the placed angle is applied where the loc is registered. */
    loc->force_approach = src->force_approach;
    loc->wall_width = src->wall_width;
    loc->seq_id = src->seq_id;
    loc->contoured_ground = src->contoured_ground;
    loc->contour_ground_type = src->contour_ground_type;
    loc->contour_ground_param = src->contour_ground_param;
    loc->sharelight = src->sharelight;
    loc->shadowed = src->shadowed;
    loc->ambient = src->ambient;
    loc->contrast = src->contrast;
    loc->mirrored = src->mirrored;
    loc->resize_x = src->resize_x;
    loc->resize_height = src->resize_height;
    loc->resize_z = src->resize_z;
    loc->offset_x = src->offset_x;
    loc->offset_y = src->offset_y;
    loc->offset_z = src->offset_z;
    loc->map_scene_id = src->map_scene_id;
    loc->map_function_id = src->map_function_id;
    loc->transform_varbit = src->transform_varbit;
    loc->transform_varp = src->transform_varp;

    loc->recolor_count = src->recolor_count;
    loc->recolors_from = torirs_dup_int_array(src->recolors_from, src->recolor_count);
    loc->recolors_to = torirs_dup_int_array(src->recolors_to, src->recolor_count);

    loc->retexture_count = src->retexture_count;
    loc->retextures_from = torirs_dup_int_array(src->retextures_from, src->retexture_count);
    loc->retextures_to = torirs_dup_int_array(src->retextures_to, src->retexture_count);

    loc->transform_count = src->transform_count;
    loc->transforms = torirs_dup_int_array(src->transforms, src->transform_count);

    return loc;
}
