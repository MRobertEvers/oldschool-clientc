#include "engine/torirs_objtype_from_rscache.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
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

/*
 * The reference ObjType initialises its fifth *inventory* op to the localized
 * "Drop" before decoding a record, so every held item answers oc_iop(obj, 5)
 * with "Drop" unless its config states something else there. No cache carries
 * that op — rev-230 records name only real verbs ("Bury", "Wear") — so a
 * decoder-faithful objtype leaves the slot empty and every consumer has to
 * invent the row for itself. Two already did (the minimenu builder and the
 * scripted backpack's numbered ladder); the CS2 side could not, which is what
 * broke shift-click drop: script6012 reads the op through oc_iop/cc_getop and
 * an empty slot 5 promotes an empty op.
 *
 * Filling it here instead makes the objtype say what the reference's says, and
 * the two existing synthesis sites become no-ops (both are guarded on the slot
 * being empty).
 *
 * Skipped for a bank placeholder: the reference's genPlaceholder drops the op
 * array entirely, and a placeholder is not a droppable item.
 */
static void
torirs_default_inv_drop_op(
    struct ToriRS_Objtype* objtype,
    bool is_placeholder)
{
    if( is_placeholder || objtype->inv_actions[TORIRS_MENU_ACTION_SLOTS - 1][0] != '\0' )
        return;
    snprintf(
        objtype->inv_actions[TORIRS_MENU_ACTION_SLOTS - 1],
        TORIRS_MENU_ACTION_LEN,
        "%s",
        "Drop");
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
    objtype->wearpos = -1;
    objtype->wearpos2 = -1;
    objtype->wearpos3 = -1;
    /* dat1 predates opcode 42: always "unstated", so OC_SHIFTCLICKIOP falls
     * back to the reference's op-slot-4-reads-"Drop" rule. */
    objtype->shift_click_drop_index = -2;

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
    torirs_default_inv_drop_op(objtype, false);
    objtype->stackable = src->stackable ? 1 : 0;
    objtype->cost = src->cost;
    objtype->inventory_model_id = src->model;
    objtype->cert_link = src->certlink;
    objtype->cert_template = src->certtemplate;
    /* dat1 has no placeholder opcodes — the feature postdates the epoch. */
    objtype->placeholder_link = -1;
    objtype->placeholder_template = -1;
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
    assert(src);
    if( src->count <= 0 )
        return;

    dst->param_count = src->count;
    dst->params = calloc((size_t)src->count, sizeof(*dst->params));
    assert(dst->params);
    for( i = 0; i < src->count; i++ )
    {
        dst->params[i].key = src->keys[i];
        if( src->kinds && src->kinds[i] == RSCACHE_PARAM_STRING )
        {
            char const* s = (char const*)src->values[i];
            dst->params[i].string_value = strdup(s ? s : "");
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

struct ToriRS_Objtype*
ToriRS_ObjtypeFromRSCacheDat2(
    int obj_id,
    const struct RSCache_Dat2ConfigObj* src)
{
    struct ToriRS_Objtype* objtype;

    assert(src);

    objtype = calloc(1, sizeof(*objtype));
    assert(objtype);
    objtype->wearpos = src->wearpos_1;
    objtype->wearpos2 = src->wearpos_2;
    objtype->wearpos3 = src->wearpos_3;
    /* rscache defaults this to -2 ("unstated") for the same reason we do. */
    objtype->shift_click_drop_index = src->shift_click_drop_index;

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
    torirs_default_inv_drop_op(objtype, src->placeholder_template_id >= 0);
    /* ObjType.stackingBehaviour is an enum at this revision. Only value 1 is
     * stackable; value 2 has different semantics and the authoritative client
     * compares it to 1 everywhere (CC_SETOBJECT and OC_STACKABLE included).
     * Collapsing every non-zero value made unstackable equipment draw a yellow
     * quantity "1". */
    objtype->stackable = src->stacking_behaviour == 1 ? 1 : 0;
    objtype->category = src->category;
    objtype->cost = src->cost;
    objtype->team = src->team;
    objtype->inventory_model_id = src->inventory_model_id;
    objtype->cert_link = src->noted_id;
    objtype->cert_template = src->noted_template;
    objtype->placeholder_link = src->placeholder_id;
    objtype->placeholder_template = src->placeholder_template_id;
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
