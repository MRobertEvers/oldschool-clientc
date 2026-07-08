#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/core/toriauxlibcore_types.h"

#include "osrs/rscache/dat1a/dat1a_config_obj.h"
#include "osrs/rscache/dat2a/dat2a_config_object.h"

#include <stdlib.h>
#include <string.h>

static void
toriauxlibcache_objtype_copy_menu_actions(
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
    toriauxlibcache_objtype_copy_menu_actions(dst->inv_actions, (char* const*)src->iop, 5);
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
    toriauxlibcache_objtype_copy_menu_actions(dst->inv_actions, (char* const*)src->if_actions, 5);
    dst->stackable = src->stacking_behaviour != 0 ? 1 : 0;
    dst->inventory_model_id = src->inventory_model_id;
    dst->zoom2d = src->zoom2d;
    dst->xan2d = src->xan2d;
    dst->yan2d = src->yan2d;
    dst->zan2d = src->zan2d;
    dst->offset_x2d = src->offset_x2d;
    dst->offset_y2d = src->offset_y2d;
    dst->resize_x = src->resize_x;
    dst->resize_y = src->resize_y;
    dst->resize_z = src->resize_z;
    memcpy(dst->count_obj, src->count_obj, sizeof(dst->count_obj));
    memcpy(dst->count_co, src->count_co, sizeof(dst->count_co));
    dst->ambient = src->ambient;
    dst->contrast = src->contrast;
    dst->recolor_count = src->recolor_count;

    if( src->recolor_count > 0 && src->recolors_from && src->recolors_to )
    {
        dst->recolors_from = malloc((size_t)src->recolor_count * sizeof(int));
        dst->recolors_to = malloc((size_t)src->recolor_count * sizeof(int));
        if( !dst->recolors_from || !dst->recolors_to )
        {
            ToriAuxLibCore_ObjtypeFree(dst);
            return NULL;
        }
        memcpy(dst->recolors_from, src->recolors_from, (size_t)src->recolor_count * sizeof(int));
        memcpy(dst->recolors_to, src->recolors_to, (size_t)src->recolor_count * sizeof(int));
    }

    return dst;
}
