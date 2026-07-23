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
    if( src->desc )
    {
        strncpy(objtype->desc, src->desc, TORIRS_DESC_MAX - 1);
        objtype->desc[TORIRS_DESC_MAX - 1] = '\0';
    }

    torirs_copy_menu_actions(objtype->inv_actions, src->iop);
    torirs_copy_menu_actions(objtype->ground_actions, src->op);
    objtype->stackable = src->stackable ? 1 : 0;
    objtype->inventory_model_id = src->model;
    objtype->cert_link = src->certlink;
    objtype->cert_template = src->certtemplate;
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

    objtype->manwear = src->manwear;
    objtype->manwear2 = src->manwear2;
    objtype->manwear3 = src->manwear3;
    objtype->womanwear = src->womanwear;
    objtype->womanwear2 = src->womanwear2;
    objtype->womanwear3 = src->womanwear3;
    objtype->manwear_offset_y = src->manwearOffsetY;
    objtype->womanwear_offset_y = src->womanwearOffsetY;
    objtype->manhead = src->manhead;
    objtype->manhead2 = src->manhead2;
    objtype->womanhead = src->womanhead;
    objtype->womanhead2 = src->womanhead2;

    return objtype;
}

static void
torirs_objtype_copy_params(
    struct ToriRS_Objtype* dst,
    struct RSCache_Params const* src)
{
    int i;
    assert(dst);
    if( !src || src->count <= 0 )
        return;

    dst->param_count = src->count;
    dst->params = calloc((size_t)src->count, sizeof(*dst->params));
    assert(dst->params);
    for( i = 0; i < src->count; i++ )
    {
        dst->params[i].key = src->keys[i];
        if( src->is_string && src->is_string[i] )
        {
            char const* s = (char const*)src->values[i];
            dst->params[i].string_value = strdup(s ? s : "");
            assert(dst->params[i].string_value);
        }
        else if( src->values[i] )
        {
            dst->params[i].int_value = *(int*)src->values[i];
        }
    }
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
    /* dat2 (OSRS) names the examine string `examine`; the reference ObjType.desc. */
    if( src->examine )
    {
        strncpy(objtype->desc, src->examine, TORIRS_DESC_MAX - 1);
        objtype->desc[TORIRS_DESC_MAX - 1] = '\0';
    }

    torirs_copy_menu_actions(objtype->inv_actions, src->if_actions);
    torirs_copy_menu_actions(objtype->ground_actions, src->actions);
    objtype->stackable = src->stacking_behaviour != 0 ? 1 : 0;
    objtype->inventory_model_id = src->inventory_model_id;
    objtype->cert_link = src->noted_id;
    objtype->cert_template = src->noted_template;
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

    torirs_objtype_copy_params(objtype, &src->params);

    objtype->manwear = src->male_model_0;
    objtype->manwear2 = src->male_model_1;
    objtype->manwear3 = src->male_model_2;
    objtype->womanwear = src->female_model_0;
    objtype->womanwear2 = src->female_model_1;
    objtype->womanwear3 = src->female_model_2;
    objtype->manhead = src->male_head_model;
    objtype->manhead2 = src->male_head_model_2;
    objtype->womanhead = src->female_head_model;
    objtype->womanhead2 = src->female_head_model_2;

    return objtype;
}
