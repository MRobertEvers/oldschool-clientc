#include "engine/torirs_objtype_from_rscache.h"

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

static void
torirs_objtype_copy_recolors(
    struct ToriRS_Objtype* dst,
    const int* from,
    const int* to,
    int count)
{
    dst->recolor_count = count;
    if( count <= 0 )
        return;

    dst->recolors_from = malloc((size_t)count * sizeof(int));
    dst->recolors_to = malloc((size_t)count * sizeof(int));
    assert(dst->recolors_from);
    assert(dst->recolors_to);
    memcpy(dst->recolors_from, from, (size_t)count * sizeof(int));
    memcpy(dst->recolors_to, to, (size_t)count * sizeof(int));
}

struct ToriRS_Objtype*
ToriRS_ObjtypeFromRSCacheDat1(
    int obj_id,
    const struct RSCache_Dat1ConfigObj* src)
{
    struct ToriRS_Objtype* objtype;

    assert(src);

    objtype = calloc(1, sizeof(*objtype));
    assert(objtype);

    objtype->id = obj_id;
    if( src->name )
    {
        strncpy(objtype->name, src->name, TORIRS_NAME_MAX - 1);
        objtype->name[TORIRS_NAME_MAX - 1] = '\0';
    }

    torirs_copy_menu_actions(objtype->inv_actions, src->iop);
    objtype->stackable = src->stackable ? 1 : 0;
    objtype->inventory_model_id = src->model;
    objtype->zoom2d = src->zoom2d;
    objtype->xan2d = src->xan2d;
    objtype->yan2d = src->yan2d;
    objtype->zan2d = src->zan2d;
    objtype->offset_x2d = src->xof2d;
    objtype->offset_y2d = src->yof2d;
    objtype->resize_x = src->resizex;
    objtype->resize_y = src->resizey;
    objtype->resize_z = src->resizez;
    objtype->ambient = src->ambient;
    objtype->contrast = src->contrast;

    if( src->countobj_count > 0 )
    {
        memcpy(objtype->count_obj, src->countobj, sizeof(objtype->count_obj));
        memcpy(objtype->count_co, src->countco, sizeof(objtype->count_co));
    }

    if( src->recol_count > 0 )
        torirs_objtype_copy_recolors(objtype, src->recol_s, src->recol_d, src->recol_count);

    return objtype;
}

struct ToriRS_Objtype*
ToriRS_ObjtypeFromRSCacheDat2(
    int obj_id,
    const struct RSCache_Dat2ConfigObj* src)
{
    struct ToriRS_Objtype* objtype;

    assert(src);

    objtype = calloc(1, sizeof(*objtype));
    assert(objtype);

    objtype->id = obj_id;
    if( src->name )
    {
        strncpy(objtype->name, src->name, TORIRS_NAME_MAX - 1);
        objtype->name[TORIRS_NAME_MAX - 1] = '\0';
    }

    torirs_copy_menu_actions(objtype->inv_actions, src->if_actions);
    objtype->stackable = src->stacking_behaviour != 0 ? 1 : 0;
    objtype->inventory_model_id = src->inventory_model_id;
    objtype->zoom2d = src->zoom2d;
    objtype->xan2d = src->xan2d;
    objtype->yan2d = src->yan2d;
    objtype->zan2d = src->zan2d;
    objtype->offset_x2d = src->offset_x2d;
    objtype->offset_y2d = src->offset_y2d;
    objtype->resize_x = src->resize_x;
    objtype->resize_y = src->resize_y;
    objtype->resize_z = src->resize_z;
    objtype->ambient = src->ambient;
    objtype->contrast = src->contrast;
    memcpy(objtype->count_obj, src->count_obj, sizeof(objtype->count_obj));
    memcpy(objtype->count_co, src->count_co, sizeof(objtype->count_co));

    if( src->recolor_count > 0 )
        torirs_objtype_copy_recolors(
            objtype, src->recolors_from, src->recolors_to, src->recolor_count);

    return objtype;
}
