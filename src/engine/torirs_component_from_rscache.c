#include "engine/torirs_component_from_rscache.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
torirs_copy_menu_actions(
    char actions[TORIRS_MENU_ACTION_SLOTS][TORIRS_MENU_ACTION_LEN],
    char* const* src_actions,
    int src_count)
{
    for( int i = 0; i < TORIRS_MENU_ACTION_SLOTS; i++ )
        actions[i][0] = '\0';

    if( !src_actions || src_count <= 0 )
        return;

    int const limit =
        src_count < TORIRS_MENU_ACTION_SLOTS ? src_count : TORIRS_MENU_ACTION_SLOTS;
    for( int i = 0; i < limit; i++ )
    {
        if( src_actions[i] && src_actions[i][0] != '\0' )
        {
            strncpy(actions[i], src_actions[i], TORIRS_MENU_ACTION_LEN - 1);
            actions[i][TORIRS_MENU_ACTION_LEN - 1] = '\0';
        }
    }
}

static enum ToriRS_ComponentType
torirs_component_type_from_raw(int type)
{
    switch( type )
    {
    case TORIRS_COMPONENT_LAYER:
        return TORIRS_COMPONENT_LAYER;
    case TORIRS_COMPONENT_UNUSED:
        return TORIRS_COMPONENT_UNUSED;
    case TORIRS_COMPONENT_INV:
        return TORIRS_COMPONENT_INV;
    case TORIRS_COMPONENT_RECT:
        return TORIRS_COMPONENT_RECT;
    case TORIRS_COMPONENT_TEXT:
        return TORIRS_COMPONENT_TEXT;
    case TORIRS_COMPONENT_GRAPHIC:
        return TORIRS_COMPONENT_GRAPHIC;
    case TORIRS_COMPONENT_MODEL:
        return TORIRS_COMPONENT_MODEL;
    case TORIRS_COMPONENT_INV_TEXT:
        return TORIRS_COMPONENT_INV_TEXT;
    case TORIRS_COMPONENT_LINE:
        return TORIRS_COMPONENT_LINE;
    default:
        fprintf(stderr, "torirs_component_type_from_raw: unknown dat2 type=%d\n", type);
        assert(false && "unknown dat2 component type");
        return TORIRS_COMPONENT_LAYER;
    }
}

static void
torirs_component_apply_graphic_hitbox_only(struct ToriRS_Component* dst)
{
    if( !dst || dst->type != TORIRS_COMPONENT_GRAPHIC )
        return;
    dst->graphic_hitbox_only =
        (dst->sprite_ref[0] == '\0' && dst->sprite_active_ref[0] == '\0') ? 1 : 0;
}

static void
torirs_dat2_sprite_ref_from_id(
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
torirs_component_copy_inv_slots_dat2(
    struct ToriRS_Component* dst,
    const struct RSCache_Dat2Component* src)
{
    if( !dst || !src || src->type != TORIRS_COMPONENT_INV )
        return;

    for( int i = 0; i < TORIRS_INV_SLOT_MAX; i++ )
    {
        if( src->invSlotOffsetX )
            dst->inv_slot_offset_x[i] = src->invSlotOffsetX[i];
        if( src->invSlotOffsetY )
            dst->inv_slot_offset_y[i] = src->invSlotOffsetY[i];
        if( src->invSlotGraphicId )
        {
            dst->inv_slot_graphic_id[i] = src->invSlotGraphicId[i];
            torirs_dat2_sprite_ref_from_id(
                src->invSlotGraphicId[i],
                dst->inv_slot_sprite_ref[i],
                sizeof(dst->inv_slot_sprite_ref[i]));
        }
    }
}

static void
torirs_component_copy_scripts(
    struct ToriRS_Component* dst,
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

        assert(scripts_lengths && scripts_lengths[i] > 0);
        int len = scripts_lengths[i];

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
torirs_copy_script_hook(
    struct ToriRS_ScriptHook* dst,
    struct RSCache_Dat2ComponentScriptVar* src,
    int32_t src_len)
{
    if( !dst )
        return;
    memset(dst, 0, sizeof(*dst));
    if( !src || src_len <= 0 )
        return;

    int n = src_len;
    if( n > TORIRS_COMPONENT_HOOK_ARG_MAX )
        n = TORIRS_COMPONENT_HOOK_ARG_MAX;
    dst->argc = n;
    for( int i = 0; i < n; i++ )
    {
        if( src[i].type == RSCACHE_DAT2_COMPONENT_SCRIPT_VAR_INT )
            dst->argv[i] = src[i].value.i;
        else
            dst->argv[i] = 0;
    }
}

static void
torirs_component_copy_dat2_cs1_scripts(
    struct ToriRS_Component* dst,
    const struct RSCache_Dat2Component* src)
{
    if( !dst || !src || src->cs1ScriptsLen <= 0 || !src->cs1Scripts )
        return;

    torirs_component_copy_scripts(
        dst,
        src->cs1ScriptsLen,
        src->cs1Scripts,
        src->cs1ScriptsLengths,
        src->cs1ComparisonOpcodes,
        src->cs1ComparisonOperands);
    dst->script_kind = 0;
}

static void
torirs_component_copy_dat2_hooks(
    struct ToriRS_Component* dst,
    const struct RSCache_Dat2Component* src)
{
    if( !dst || !src || !src->if3 )
        return;

    torirs_copy_script_hook(&dst->on_load, src->onLoad, src->onLoadLen);
    torirs_copy_script_hook(&dst->on_click, src->onClick, src->onClickLen);
    torirs_copy_script_hook(&dst->on_op, src->onOp, src->onOpLen);
    torirs_copy_script_hook(&dst->on_mouse_over, src->onMouseOver, src->onMouseOverLen);
    torirs_copy_script_hook(&dst->on_mouse_leave, src->onMouseLeave, src->onMouseLeaveLen);
    torirs_copy_script_hook(&dst->on_varp_transmit, src->onVarpTransmit, src->onVarpTransmitLen);
    torirs_copy_script_hook(&dst->on_inv_transmit, src->onInvTransmit, src->onInvTransmitLen);

    if( src->inventoryTriggers && src->inventoryTriggersLen > 0 )
    {
        int n = src->inventoryTriggersLen;
        if( n > TORIRS_INVENTORY_TRIGGER_MAX )
            n = TORIRS_INVENTORY_TRIGGER_MAX;
        dst->inventory_triggers_count = n;
        for( int i = 0; i < n; i++ )
            dst->inventory_triggers[i] = src->inventoryTriggers[i];
    }

    if( src->varpTriggers && src->varpTriggersLen > 0 )
    {
        int n = src->varpTriggersLen;
        if( n > TORIRS_VARP_TRIGGER_MAX )
            n = TORIRS_VARP_TRIGGER_MAX;
        dst->varp_triggers_count = n;
        for( int i = 0; i < n; i++ )
            dst->varp_triggers[i] = src->varpTriggers[i];
    }

    if( dst->scripts_count <= 0 &&
        (dst->on_load.argc > 0 || dst->on_click.argc > 0 || dst->on_op.argc > 0 ||
         dst->on_mouse_over.argc > 0 || dst->on_mouse_leave.argc > 0 ||
         dst->on_varp_transmit.argc > 0 || dst->on_inv_transmit.argc > 0) )
        dst->script_kind = 1;
}

struct ToriRS_Component*
ToriRS_ComponentFromRSCacheDat2(const struct RSCache_Dat2Component* src)
{
    if( !src )
        return NULL;

    struct ToriRS_Component* dst = calloc(1, sizeof(struct ToriRS_Component));
    if( !dst )
        return NULL;

    dst->id = src->id;
    dst->type = torirs_component_type_from_raw(src->type);
    dst->width = src->baseWidth;
    dst->height = src->baseHeight;
    dst->model_type = src->modelType;
    dst->model_id = src->modelId;
    dst->model_zoom = src->modelZoom;
    dst->model_xan = src->modelXAngle;
    dst->model_yan = src->modelYAngle;
    dst->model_zan = src->modelZAngle;
    dst->model_x_offset = src->modelXOffset;
    dst->model_y_offset = src->modelYOffset;
    dst->model_orthog = src->modelOrthographic ? 1 : 0;
    dst->model_fixed_zoom = src->aBoolean411 ? 1 : 0;
    if( src->type == TORIRS_COMPONENT_MODEL )
    {
        dst->model_cache_short50 = src->aShort50;
        dst->model_cache_short49 = src->aShort49;
        dst->model_cache_an5957 = src->anInt5957;
        dst->model_cache_an5920 = src->anInt5920;
    }
    dst->color = src->color;
    dst->filled = src->fill ? 1 : 0;
    dst->font_id = src->textFont;
    dst->center = src->textHorizontalAlignment != 0 ? 1 : 0;
    dst->shadowed = src->textShadow ? 1 : 0;
    dst->inv_cols = src->baseWidth;
    dst->inv_rows = src->baseHeight;
    dst->margin_x = src->marginX;
    dst->margin_y = src->marginY;
    if( src->type == TORIRS_COMPONENT_LINE )
    {
        dst->line_width = src->lineWidth > 0 ? src->lineWidth : 1;
        dst->filled = src->lineDirection ? 1 : 0;
    }
    dst->hide = src->hidden ? 1 : 0;
    dst->button_type = src->buttonType;
    dst->client_code = src->clientCode;
    dst->click_mask = src->clickMask;
    dst->over_color = src->overColour;
    dst->active_color = src->activeColour;
    dst->active_over_color = src->activeOverColour;
    dst->over_layer_id = src->linkedComponentId;
    dst->parent_id = src->layer;
    dst->tiled = src->tiled ? 1 : 0;
    dst->base_x = src->baseX;
    dst->base_y = src->baseY;
    dst->base_width = src->baseWidth;
    dst->base_height = src->baseHeight;
    dst->if3 = src->if3 ? 1 : 0;
    if( src->if3 )
    {
        dst->x_mode = src->xMode;
        dst->y_mode = src->yMode;
        dst->width_mode = src->widthMode;
        dst->height_mode = src->heightMode;
        dst->aspect_w = src->baseWidth > 0 ? src->baseWidth : 1;
        dst->aspect_h = src->baseHeight > 0 ? src->baseHeight : 1;
    }
    dst->graphic = src->graphic;
    dst->graphic_active = src->activeGraphic;
    dst->outline = src->outline;
    dst->graphic_shadow = src->graphicShadow;
    dst->sprite_angle = src->angle;
    dst->horizontal_flip = src->horizontalFlip ? 1 : 0;
    dst->vertical_flip = src->verticalFlip ? 1 : 0;
    dst->transparency = src->transparency;
    dst->text_h_align = src->textHorizontalAlignment;
    dst->text_v_align = src->textVerticalAlignment;
    dst->text_line_height = src->textLineHeight;
    dst->drag_dead_zone = src->dragDeadZone;
    dst->drag_dead_time = src->dragDeadTime;
    if( src->type == TORIRS_COMPONENT_LAYER )
    {
        dst->scroll_height = src->scrollHeight;
        dst->scroll_width = src->scrollWidth;
    }
    if( src->type == TORIRS_COMPONENT_LINE )
        dst->line_horizontal = src->lineDirection ? 1 : 0;

    torirs_dat2_sprite_ref_from_id(src->graphic, dst->sprite_ref, sizeof(dst->sprite_ref));
    torirs_dat2_sprite_ref_from_id(
        src->activeGraphic, dst->sprite_active_ref, sizeof(dst->sprite_active_ref));

    if( src->text )
        strncpy(dst->text, src->text, sizeof(dst->text) - 1);
    if( src->activeText )
        strncpy(dst->active_text, src->activeText, sizeof(dst->active_text) - 1);
    if( src->option )
        strncpy(dst->option, src->option, sizeof(dst->option) - 1);
    if( src->type == TORIRS_COMPONENT_INV && src->objOps )
        torirs_copy_menu_actions(dst->ops, src->objOps, 5);
    else
        torirs_copy_menu_actions(dst->ops, src->ops, src->opsLen);

    torirs_component_apply_graphic_hitbox_only(dst);
    torirs_component_copy_inv_slots_dat2(dst, src);
    torirs_component_copy_dat2_cs1_scripts(dst, src);
    torirs_component_copy_dat2_hooks(dst, src);

    return dst;
}

static void
torirs_component_pack_apply_layout(
    struct ToriRS_ComponentPack* pack,
    const struct RSCache_Dat2ComponentPack* src)
{
    assert(pack);
    assert(src);

    for( int i = 0; i < pack->component_count; i++ )
    {
        const struct RSCache_Dat2Component* rs = src->components[i];
        struct ToriRS_Component* dst = &pack->components[i];
        if( !rs )
            continue;

        int rel_x = rs->if3 ? rs->baseX : rs->x;
        int rel_y = rs->if3 ? rs->baseY : rs->y;
        ToriRS_ComponentApplyWalkLayout(dst, rs->layer, rel_x, rel_y);
        if( rs->baseWidth > 0 )
            dst->width = rs->baseWidth;
        if( rs->baseHeight > 0 )
            dst->height = rs->baseHeight;
    }
}

struct ToriRS_ComponentPack*
ToriRS_ComponentPackFromRSCacheDat2(const struct RSCache_Dat2ComponentPack* src)
{
    if( !src || src->component_count <= 0 || !src->components )
        return NULL;

    struct ToriRS_ComponentPack* pack = calloc(1, sizeof(struct ToriRS_ComponentPack));
    if( !pack )
        return NULL;

    pack->component_count = src->component_count;
    pack->components = calloc((size_t)pack->component_count, sizeof(struct ToriRS_Component));
    if( !pack->components )
    {
        free(pack);
        return NULL;
    }

    for( int i = 0; i < pack->component_count; i++ )
    {
        const struct RSCache_Dat2Component* rs = src->components[i];
        struct ToriRS_Component* converted = ToriRS_ComponentFromRSCacheDat2(rs);
        if( !converted )
            continue;
        pack->components[i] = *converted;
        free(converted);
    }

    torirs_component_pack_apply_layout(pack, src);
    return pack;
}
