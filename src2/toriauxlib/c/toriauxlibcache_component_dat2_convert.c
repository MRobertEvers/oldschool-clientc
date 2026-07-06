#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/core/toriauxlibcore_types.h"
#include "vm/cs1vm.h"
#include "vm/cs2vm.h"

#include "osrs/rscache/dat1a/dat1a_config_component.h"
#include "osrs/rscache/dat2a/dat2a_component.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    case COMPONENT_TYPE_LINE:
        return TORIAUXLIBCORE_COMPONENT_LINE;
    default:
        fprintf(stderr, "toriauxlibcache_component_type_from_raw: unknown dat2 type=%d\n", type);
        assert(false && "unknown dat2 component type");
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
toriauxlibcache_component_copy_inv_slots_dat2(
    struct ToriAuxLibCore_Component* dst,
    const RSCacheDat2A_Component* src)
{
    if( !dst || !src || src->type != COMPONENT_TYPE_INV )
        return;

    for( int i = 0; i < TORIAUXLIBCORE_INV_SLOT_MAX; i++ )
    {
        if( src->invSlotOffsetX )
            dst->inv_slot_offset_x[i] = src->invSlotOffsetX[i];
        if( src->invSlotOffsetY )
            dst->inv_slot_offset_y[i] = src->invSlotOffsetY[i];
        if( src->invSlotGraphicId )
        {
            dst->inv_slot_graphic_id[i] = src->invSlotGraphicId[i];
            toriauxlibcache_dat2_sprite_ref_from_id(
                src->invSlotGraphicId[i],
                dst->inv_slot_sprite_ref[i],
                sizeof(dst->inv_slot_sprite_ref[i]));
        }
    }
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
                      : cs1vm_script_length(scripts[i]);
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
    const RSCacheDat2A_Component* src)
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
    dst->script_kind = CS1VM_SCRIPT_KIND_CS1;
}

static void
toriauxlibcache_component_copy_dat2_hooks(
    struct ToriAuxLibCore_Component* dst,
    const RSCacheDat2A_Component* src)
{
    if( !dst || !src || !src->if3 )
        return;

    toriauxlibcache_copy_script_hook(&dst->on_load, src->onLoad, src->onLoadLen);
    toriauxlibcache_copy_script_hook(&dst->on_click, src->onClick, src->onClickLen);
    toriauxlibcache_copy_script_hook(
        &dst->on_varp_transmit, src->onVarpTransmit, src->onVarpTransmitLen);
    toriauxlibcache_copy_script_hook(
        &dst->on_inv_transmit, src->onInvTransmit, src->onInvTransmitLen);

    if( src->inventoryTriggers && src->inventoryTriggersLen > 0 )
    {
        int n = src->inventoryTriggersLen;
        if( n > TORIAUXLIBCORE_INVENTORY_TRIGGER_MAX )
            n = TORIAUXLIBCORE_INVENTORY_TRIGGER_MAX;
        dst->inventory_triggers_count = n;
        for( int i = 0; i < n; i++ )
            dst->inventory_triggers[i] = src->inventoryTriggers[i];
    }

    if( src->varpTriggers && src->varpTriggersLen > 0 )
    {
        int n = src->varpTriggersLen;
        if( n > TORIAUXLIBCORE_VARP_TRIGGER_MAX )
            n = TORIAUXLIBCORE_VARP_TRIGGER_MAX;
        dst->varp_triggers_count = n;
        for( int i = 0; i < n; i++ )
            dst->varp_triggers[i] = src->varpTriggers[i];
    }

    if( dst->scripts_count <= 0 &&
        (dst->on_load.argc > 0 || dst->on_click.argc > 0 || dst->on_varp_transmit.argc > 0 ||
         dst->on_inv_transmit.argc > 0) )
        dst->script_kind = CS1VM_SCRIPT_KIND_CS2;
}

struct ToriAuxLibCore_Component*
ToriAuxLibCache_ComponentNewFromCacheDat2Component(const void* cache_component_ptr)
{
    const RSCacheDat2A_Component* src = cache_component_ptr;
    if( !src )
        return NULL;

    struct ToriAuxLibCore_Component* dst = calloc(1, sizeof(struct ToriAuxLibCore_Component));
    if( !dst )
        return NULL;

    dst->id = src->id;
    dst->type = toriauxlibcache_component_type_from_raw(src->type);
    dst->width = src->baseWidth;
    dst->height = src->baseHeight;
    dst->model_type = src->modelType;
    dst->model_id = src->modelId;
    dst->model_zoom = src->modelZoom;
    dst->model_xan = src->modelXAngle;
    dst->model_yan = src->modelYAngle;
    dst->color = src->color;
    dst->filled = src->fill ? 1 : 0;
    dst->font_id = src->textFont;
    dst->center = src->textHorizontalAlignment != 0 ? 1 : 0;
    dst->shadowed = src->textShadow ? 1 : 0;
    dst->inv_cols = src->baseWidth;
    dst->inv_rows = src->baseHeight;
    dst->margin_x = src->marginX;
    dst->margin_y = src->marginY;
    if( src->type == COMPONENT_TYPE_LINE )
    {
        dst->line_width = src->lineWidth > 0 ? src->lineWidth : 1;
        dst->filled = src->lineDirection ? 1 : 0;
    }
    dst->hide = src->hidden ? 1 : 0;
    dst->button_type = src->buttonType;
    dst->client_code = src->clientCode;
    dst->over_color = src->overColour;
    dst->active_color = src->activeColour;
    dst->active_over_color = src->activeOverColour;
    dst->over_layer_id = src->linkedComponentId;
    dst->parent_id = -1;
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
        dst->aspect_w = src->aspect_ratio_w > 0 ? src->aspect_ratio_w : 1;
        dst->aspect_h = src->aspect_ratio_h > 0 ? src->aspect_ratio_h : 1;
    }
    dst->graphic = src->graphic;
    dst->transparency = src->transparency;
    dst->text_h_align = src->textHorizontalAlignment;
    dst->text_v_align = src->textVerticalAlignment;
    dst->text_line_height = src->textLineHeight;
    dst->drag_dead_zone = src->dragDeadZone;
    dst->drag_dead_time = src->dragDeadTime;
    if( src->type == COMPONENT_TYPE_LAYER )
    {
        dst->scroll_height = src->scrollHeight;
        dst->scroll_width = src->scrollWidth;
    }
    if( src->type == COMPONENT_TYPE_LINE )
        dst->line_horizontal = src->lineDirection ? 1 : 0;

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
    toriauxlibcache_component_copy_inv_slots_dat2(dst, src);
    toriauxlibcache_component_copy_dat2_cs1_scripts(dst, src);
    toriauxlibcache_component_copy_dat2_hooks(dst, src);

    return dst;
}
