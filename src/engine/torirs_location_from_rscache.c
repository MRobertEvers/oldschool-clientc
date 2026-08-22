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

    if( count <= 0 )
        return NULL;
    assert(src);

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
    /* A loc encoded with opcode 5 is a single-model loc: the decoder sets a
     * count of 1 and leaves `shapes` NULL on purpose, because shape_select is
     * not used for it. Absent in, absent out — every reader of this field
     * already answers NULL with the implicit shape (world_scenery.u.c,
     * task_world_load.c, task_dat2_loc_load.c). `lengths` carries no such
     * case: both decode paths allocate it whenever the count is set, so a NULL
     * there is a decoder bug and the assert inside must keep catching it. */
    dst->shapes = src->shapes
                      ? torirs_dup_int_array(src->shapes, src->shapes_and_model_count)
                      : NULL;
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


/*
 * The record's params (opcode 249), carried whole for CS2's `lc_param`.
 *
 * The rscache decoder has always parsed these; this conversion simply did not
 * bring them across, so `lc_param` had nothing to read and no implementation. The
 * cache's own "Highlight entities on mouse-over" (clientscript 5350) is what
 * needs them: it reads `param_2312` off the hovered record to decide which
 * highlight group the thing belongs in.
 */
static void
torirs_location_copy_params(struct ToriRS_Location* dst, struct RSCache_Params const* src)
{
    assert(dst);
    assert(src);
    if( src->count <= 0 )
        return;

    dst->param_count = src->count;
    dst->params = calloc((size_t)src->count, sizeof(*dst->params));
    assert(dst->params);
    for( int i = 0; i < src->count; i++ )
    {
        dst->params[i].key = src->keys[i];
        if( src->kinds && src->kinds[i] == RSCACHE_PARAM_STRING )
        {
            char const* str = (char const*)src->values[i];
            dst->params[i].string_value = strdup(str ? str : "");
            assert(dst->params[i].string_value);
        }
        else if( src->values[i] )
        {
            if( src->kinds && src->kinds[i] == RSCACHE_PARAM_LONG )
                dst->params[i].int_value = (int)*(int64_t*)src->values[i];
            else
                dst->params[i].int_value = *(int*)src->values[i];
        }
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
    loc->occlude = src->occlude;
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

    /* Ambient sound: copied rather than referenced because the decoded config
     * record is freed once the adaptor returns. */
    loc->ambient_sound_id = src->ambient_sound_id;
    loc->ambient_sound_distance = src->ambient_sound_distance;
    loc->ambient_sound_retain = src->ambient_sound_retain;
    loc->ambient_sound_ticks_min = src->ambient_sound_ticks_min;
    loc->ambient_sound_ticks_max = src->ambient_sound_ticks_max;
    if( src->ambient_sound_id_count > 0 && src->ambient_sound_ids )
    {
        loc->ambient_sound_ids = malloc((size_t)src->ambient_sound_id_count * sizeof(int));
        assert(loc->ambient_sound_ids);
        memcpy(
            loc->ambient_sound_ids,
            src->ambient_sound_ids,
            (size_t)src->ambient_sound_id_count * sizeof(int));
        loc->ambient_sound_id_count = src->ambient_sound_id_count;
    }
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

    /* LocType.raiseobject (opcode 75). Finish has already defaulted -1. */
    loc->raiseobject = src->support_items;
    torirs_location_copy_params(loc, &src->params);

    return loc;
}
