#include "engine/torirs_component_from_rscache.h"

#include "engine/torirs_component_hook.h"

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
    assert(dst);
    if( dst->type != TORIRS_COMPONENT_GRAPHIC )
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
    if( out_size == 0 )
        return;
    assert(out);
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
    assert(src);
    if( src->type != TORIRS_COMPONENT_INV )
        return;
    assert(dst);

    struct ToriRS_ComponentInvSlots* slots = ToriRS_ComponentInvSlotsEnsure(dst);
    for( int i = 0; i < TORIRS_INV_SLOT_MAX; i++ )
    {
        if( src->invSlotOffsetX )
            slots->offset_x[i] = src->invSlotOffsetX[i];
        if( src->invSlotOffsetY )
            slots->offset_y[i] = src->invSlotOffsetY[i];
        if( src->invSlotGraphicId )
        {
            slots->graphic_id[i] = src->invSlotGraphicId[i];
            torirs_dat2_sprite_ref_from_id(
                src->invSlotGraphicId[i],
                slots->sprite_ref[i],
                sizeof(slots->sprite_ref[i]));
        }
    }
}

static void
torirs_component_copy_scripts(
    struct ToriRS_Component* dst,
    int scripts_count,
    int** scripts,
    int* scripts_lengths,
    int comparator_count,
    int* script_comparator,
    int* script_operand)
{
    dst->scripts_count = scripts_count;
    dst->comparator_count = comparator_count;
    if( scripts_count <= 0 || !scripts )
        return;

    dst->scripts = calloc((size_t)scripts_count, sizeof(int*));
    dst->scripts_lengths = calloc((size_t)scripts_count, sizeof(int));
    assert(dst->scripts);
    assert(dst->scripts_lengths);

    for( int i = 0; i < scripts_count; i++ )
    {
        if( !scripts[i] )
            continue;

        assert(scripts_lengths && scripts_lengths[i] > 0);
        int len = scripts_lengths[i];

        dst->scripts[i] = malloc((size_t)len * sizeof(int));
        assert(dst->scripts[i]);
        memcpy(dst->scripts[i], scripts[i], (size_t)len * sizeof(int));
        dst->scripts_lengths[i] = len;
    }

    /* The cache sizes the comparator arrays by their own count, which can differ
     * from scripts_count — copying scripts_count entries would over-read. */
    if( comparator_count <= 0 )
        return;

    if( script_comparator )
    {
        dst->script_comparator = malloc((size_t)comparator_count * sizeof(int));
        assert(dst->script_comparator);
        memcpy(
            dst->script_comparator, script_comparator, (size_t)comparator_count * sizeof(int));
    }

    if( script_operand )
    {
        dst->script_operand = malloc((size_t)comparator_count * sizeof(int));
        assert(dst->script_operand);
        memcpy(dst->script_operand, script_operand, (size_t)comparator_count * sizeof(int));
    }
}

static void
torirs_copy_script_hook(
    struct ToriRS_ScriptHook* dst,
    struct RSCache_Dat2ComponentScriptVar* src,
    int32_t src_len)
{
    assert(dst);
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
        {
            dst->argv[i] = src[i].value.i;
        }
        else
        {
            dst->argv[i] = 0;
            dst->str_mask |= (uint64_t)1 << i;
            if( dst->str_argc < TORIRS_COMPONENT_HOOK_STR_MAX )
            {
                char* out = dst->strv[dst->str_argc];
                strncpy(out, src[i].value.s ? src[i].value.s : "", TORIRS_COMPONENT_HOOK_STR_LEN - 1);
                out[TORIRS_COMPONENT_HOOK_STR_LEN - 1] = '\0';
            }
            /* Past the pool cap the position stays marked in str_mask so int/
             * string local alignment holds; the value degrades to "". */
            dst->str_argc++;
        }
    }
    if( dst->str_argc > TORIRS_COMPONENT_HOOK_STR_MAX )
        dst->str_argc = TORIRS_COMPONENT_HOOK_STR_MAX;
}

static void
torirs_component_copy_dat2_cs1_scripts(
    struct ToriRS_Component* dst,
    const struct RSCache_Dat2Component* src)
{
    assert(src);
    if( src->cs1ScriptsLen <= 0 || !src->cs1Scripts )
        return;
    assert(dst);

    torirs_component_copy_scripts(
        dst,
        src->cs1ScriptsLen,
        src->cs1Scripts,
        src->cs1ScriptsLengths,
        src->cs1ComparisonLen,
        src->cs1ComparisonOpcodes,
        src->cs1ComparisonOperands);
    dst->script_kind = 0;
}

static void
torirs_component_copy_dat2_hooks(
    struct ToriRS_Component* dst,
    const struct RSCache_Dat2Component* src)
{
    assert(src);
    if( !src->if3 )
        return;
    assert(dst);

    /* One line per kind, and each allocates only when the cache actually
     * carried that hook — see engine/torirs_component_hook.h. */
#define COPY_HOOK(kind, field)                                                                     \
    do                                                                                             \
    {                                                                                              \
        if( src->field##Len > 0 && src->field )                                                    \
        {                                                                                          \
            struct ToriRS_ScriptHook* slot = ToriRS_ComponentHookInit(dst, (kind));                 \
            if( slot )                                                                             \
                torirs_copy_script_hook(slot, src->field, src->field##Len);                        \
        }                                                                                          \
    } while( 0 )

    COPY_HOOK(TORIRS_COMPONENT_HOOK_LOAD, onLoad);
    COPY_HOOK(TORIRS_COMPONENT_HOOK_CLICK, onClick);
    COPY_HOOK(TORIRS_COMPONENT_HOOK_OP, onOp);
    COPY_HOOK(TORIRS_COMPONENT_HOOK_MOUSE_OVER, onMouseOver);
    COPY_HOOK(TORIRS_COMPONENT_HOOK_MOUSE_LEAVE, onMouseLeave);
    COPY_HOOK(TORIRS_COMPONENT_HOOK_DRAG, onDrag);
    COPY_HOOK(TORIRS_COMPONENT_HOOK_DRAG_COMPLETE, onDragComplete);
    COPY_HOOK(TORIRS_COMPONENT_HOOK_HOLD, onHold);
    COPY_HOOK(TORIRS_COMPONENT_HOOK_MOUSE_REPEAT, onMouseRepeat);
    COPY_HOOK(TORIRS_COMPONENT_HOOK_SCROLL_WHEEL, onScrollWheel);
    COPY_HOOK(TORIRS_COMPONENT_HOOK_TIMER, onTimer);
    COPY_HOOK(TORIRS_COMPONENT_HOOK_CLICK_REPEAT, onClickRepeat);
    COPY_HOOK(TORIRS_COMPONENT_HOOK_RELEASE, onRelease);
    COPY_HOOK(TORIRS_COMPONENT_HOOK_TARGET_ENTER, onTargetEnter);
    COPY_HOOK(TORIRS_COMPONENT_HOOK_TARGET_LEAVE, onTargetLeave);
    COPY_HOOK(TORIRS_COMPONENT_HOOK_VARP_TRANSMIT, onVarpTransmit);
    COPY_HOOK(TORIRS_COMPONENT_HOOK_INV_TRANSMIT, onInvTransmit);
    COPY_HOOK(TORIRS_COMPONENT_HOOK_STAT_TRANSMIT, onStatTransmit);
#undef COPY_HOOK

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

    if( src->statTriggers && src->statTriggersLen > 0 )
    {
        int n = src->statTriggersLen;
        if( n > TORIRS_VARP_TRIGGER_MAX )
            n = TORIRS_VARP_TRIGGER_MAX;
        dst->stat_triggers_count = n;
        for( int i = 0; i < n; i++ )
            dst->stat_triggers[i] = src->statTriggers[i];
    }

    /*
     * Exactly the twelve kinds this test named before the hooks moved behind
     * accessors — NOT "any hook". `script_kind` picks the CS1-vs-CS2 handling
     * for the component, and widening it to include drag/hold/timer/wheel
     * silently reclassified components that had only those, which cost an
     * interface its mount (60 components, 14 var hooks, 8 timers).
     */
    {
        static enum ToriRS_ComponentHookKind const k_cs2_kinds[] = {
            TORIRS_COMPONENT_HOOK_LOAD,          TORIRS_COMPONENT_HOOK_CLICK,
            TORIRS_COMPONENT_HOOK_OP,            TORIRS_COMPONENT_HOOK_MOUSE_OVER,
            TORIRS_COMPONENT_HOOK_MOUSE_LEAVE,   TORIRS_COMPONENT_HOOK_CLICK_REPEAT,
            TORIRS_COMPONENT_HOOK_RELEASE,       TORIRS_COMPONENT_HOOK_TARGET_ENTER,
            TORIRS_COMPONENT_HOOK_TARGET_LEAVE,  TORIRS_COMPONENT_HOOK_VARP_TRANSMIT,
            TORIRS_COMPONENT_HOOK_INV_TRANSMIT,  TORIRS_COMPONENT_HOOK_STAT_TRANSMIT,
        };
        int any = 0;
        for( size_t i = 0; i < sizeof(k_cs2_kinds) / sizeof(k_cs2_kinds[0]) && !any; i++ )
        {
            struct ToriRS_ScriptHook const* h = ToriRS_ComponentHookPeek(dst, k_cs2_kinds[i]);
            any = h && h->argc > 0;
        }
        if( dst->scripts_count <= 0 && any )
            dst->script_kind = 1;
    }
}

struct ToriRS_Component*
ToriRS_ComponentFromRSCacheDat2(const struct RSCache_Dat2Component* src)
{
    assert(src);

    struct ToriRS_Component* dst = calloc(1, sizeof(struct ToriRS_Component));
    assert(dst);

    dst->id = src->id;
    dst->type = torirs_component_type_from_raw(src->type);
    dst->width = src->baseWidth;
    dst->height = src->baseHeight;
    dst->model_type = src->modelType;
    dst->model_id = src->modelId;
    /* Dat2's IF1-format MODEL branch forces modelType 1 and encodes "none" as
     * -1 (dat1 uses type 0 instead); the downstream gate accepts both. */
    dst->active_model_type = src->activeModelId >= 0 ? src->modelType : 0;
    dst->active_model_id = src->activeModelId;
    dst->model_seq_id = src->modelSeqId;
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
    /* IF3 has no objSwap boolean — dragging is driven by drag dead-zone/time +
     * the onDragComplete script, so the item drag machine stays enabled. */
    dst->inv_can_drag = 1;
    /* IF3 has no objOps/objUse booleans (its item ops live in the objOps op
     * array, handled below); keep the pre-gate behaviour of showing every held
     * row. Per-component IF3 gating is a follow-up if a dat2 shop needs it. */
    dst->inv_obj_ops = 1;
    dst->inv_obj_use = 1;
    if( src->type == TORIRS_COMPONENT_LINE )
    {
        dst->line_width = src->lineWidth > 0 ? src->lineWidth : 1;
        dst->filled = src->lineDirection ? 1 : 0;
    }
    dst->hide = src->hidden ? 1 : 0;
    dst->button_type = src->buttonType;
    dst->client_code = src->clientCode;
    dst->click_mask = src->clickMask;
    /* dat2 has no standalone target-mask field in either widget family: IF1
     * folds its 6-bit button-2 mask in at bit 11 on the way through the decoder
     * (dat2_component.c, `clickMask |= local808 << 11`) and IF3 writes those
     * same six bits straight into its 3-byte events word — which is why the IF3
     * decoder gates its target triplet on exactly this field. */
    dst->target_mask =
        (src->clickMask >> TORIRS_TARGET_MASK_IF3_SHIFT) & TORIRS_TARGET_MASK_IF3_BITS;
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
        dst->no_click_through = src->noClickThrough ? 1 : 0;
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
    if( src->targetVerb )
        strncpy(dst->target_verb, src->targetVerb, sizeof(dst->target_verb) - 1);
    if( src->targetText )
        strncpy(dst->target_base, src->targetText, sizeof(dst->target_base) - 1);

    torirs_component_apply_graphic_hitbox_only(dst);
    torirs_component_copy_inv_slots_dat2(dst, src);
    torirs_component_copy_dat2_cs1_scripts(dst, src);
    torirs_component_copy_dat2_hooks(dst, src);

    return dst;
}

struct ToriRS_Component*
ToriRS_ComponentFromRSCacheDat1(const struct RSCache_Dat1ConfigComponent* src)
{
    assert(src);

    struct ToriRS_Component* dst = calloc(1, sizeof(struct ToriRS_Component));
    assert(dst);

    dst->id = src->id;
    dst->type = torirs_component_type_from_raw(src->type);
    dst->width = src->width;
    dst->height = src->height;
    dst->if3 = 0;
    dst->transparency = src->alpha;
    dst->over_layer_id = src->overlayer;
    dst->hide = src->hide ? 1 : 0;
    dst->button_type = src->buttonType;
    dst->client_code = src->clientCode;
    /*
     * dat1 keeps the mask as its own field, decoded only for a BUTTON_TARGET or
     * an INV, and left at -1 ("never read") everywhere else. -1 is not a mask:
     * passed on as one it answers yes to every bit there is.
     *
     * BOTH fields need saying, and only one of them used to say it. As a
     * CLICK mask, -1 sets the drag-depth bits, so UITree_ComponentIsDraggable
     * answered yes for every ordinary IF1 widget in the cache -- an era whose
     * interfaces have no drag concept at all -- and most of a 2004 gameframe
     * could be picked up and thrown around. It also makes every one of them a
     * drop target and a click target, which is the same mistake wearing three
     * hats.
     */
    dst->click_mask = src->targetMask > 0 ? src->targetMask : 0;
    dst->target_mask = src->targetMask > 0 ? src->targetMask : 0;
    dst->parent_id = src->layer;

    if( src->type == RSCACHE_DAT1_COMPONENT_TYPE_LAYER )
        dst->scroll_height = src->scroll;

    dst->color = src->colour;
    dst->active_color = src->activeColour;
    dst->over_color = src->overColour;
    dst->active_over_color = src->activeOverColour;
    dst->filled = src->fill ? 1 : 0;

    /* Dat1 font is a title-font slot 0-3 (not an archive id); the builder's
     * ResolveFontArchive passes unmatched ids through unchanged. */
    dst->font_id = src->font;
    dst->center = src->center ? 1 : 0;
    dst->shadowed = src->shadowed ? 1 : 0;
    dst->text_h_align = src->center ? 1 : 0;

    if( src->text )
        strncpy(dst->text, src->text, sizeof(dst->text) - 1);
    if( src->activeText )
        strncpy(dst->active_text, src->activeText, sizeof(dst->active_text) - 1);
    if( src->option )
        strncpy(dst->option, src->option, sizeof(dst->option) - 1);

    /* Sprite refs stay names; ids resolve at dat1 pack load. */
    dst->graphic = -1;
    dst->graphic_active = -1;
    if( src->graphic )
        strncpy(dst->sprite_ref, src->graphic, sizeof(dst->sprite_ref) - 1);
    if( src->activeGraphic )
        strncpy(dst->sprite_active_ref, src->activeGraphic, sizeof(dst->sprite_active_ref) - 1);

    dst->model_type = src->modelType;
    dst->model_id = src->model;
    dst->active_model_type = src->activeModelType;
    dst->active_model_id = src->activeModel;
    dst->model_seq_id = src->anim;
    dst->model_zoom = src->zoom;
    dst->model_xan = src->xan;
    dst->model_yan = src->yan;

    dst->margin_x = src->marginX;
    dst->margin_y = src->marginY;
    if( src->type == RSCACHE_DAT1_COMPONENT_TYPE_INV ||
        src->type == RSCACHE_DAT1_COMPONENT_TYPE_INV_TEXT )
    {
        dst->inv_cols = src->width;
        dst->inv_rows = src->height;
        /* Reference arms the item drag only on objSwap || objReplace; the
         * dat1 booleans decode in that order as draggable/interactable/usable/
         * swappable, so objSwap = draggable and objReplace = swappable. */
        dst->inv_can_drag = (src->draggable || src->swappable) ? 1 : 0;
        /* dat1 booleans decode draggable/interactable/usable/swappable =
         * objSwap/objOps/objUse/objReplace, so interactable = objOps (show
         * item ops) and usable = objUse (show "Use"). */
        dst->inv_obj_ops = src->interactable ? 1 : 0;
        dst->inv_obj_use = src->usable ? 1 : 0;
        struct ToriRS_ComponentInvSlots* slots = ToriRS_ComponentInvSlotsEnsure(dst);
        for( int i = 0; i < TORIRS_INV_SLOT_MAX; i++ )
        {
            slots->graphic_id[i] = -1;
            if( src->invSlotOffsetX )
                slots->offset_x[i] = src->invSlotOffsetX[i];
            if( src->invSlotOffsetY )
                slots->offset_y[i] = src->invSlotOffsetY[i];
            if( src->invSlotGraphic && src->invSlotGraphic[i] )
                strncpy(
                    slots->sprite_ref[i],
                    src->invSlotGraphic[i],
                    sizeof(slots->sprite_ref[i]) - 1);
        }
    }

    if( src->type == RSCACHE_DAT1_COMPONENT_TYPE_LINE )
    {
        dst->line_width = 1;
        /* Dat1 LINE direction has no INI/cache flag; fill mirrors the dat2 mapping. */
        dst->line_horizontal = src->fill ? 1 : 0;
    }

    torirs_copy_menu_actions(dst->ops, src->iop, 5);
    if( src->targetVerb )
        strncpy(dst->target_verb, src->targetVerb, sizeof(dst->target_verb) - 1);
    if( src->targetText )
        strncpy(dst->target_base, src->targetText, sizeof(dst->target_base) - 1);

    torirs_component_apply_graphic_hitbox_only(dst);
    torirs_component_copy_scripts(
        dst,
        src->scripts_count,
        src->scripts,
        src->scripts_lengths,
        src->comparator_count,
        src->scriptComparator,
        src->scriptOperand);
    dst->script_kind = 0;

    return dst;
}

struct ToriRS_ComponentPack*
ToriRS_ComponentPackFromRSCacheDat1(
    const struct RSCache_Dat1ConfigComponentList* list,
    int root_id)
{
    struct ToriRS_ComponentPack* pack;
    struct RSCache_Dat1ConfigComponent const* root;
    uint8_t* visited;
    int* stack;
    int* parent_of;
    int* rel_x_of;
    int* rel_y_of;
    int* order;
    int stack_top;
    int order_count;

    assert(list);
    if( !list->components || root_id < 0 || root_id >= list->components_count )
        return NULL;
    root = list->components[root_id];
    if( !root )
        return NULL;

    visited = calloc((size_t)list->components_count, sizeof(uint8_t));
    stack = calloc((size_t)list->components_count, sizeof(int));
    parent_of = calloc((size_t)list->components_count, sizeof(int));
    rel_x_of = calloc((size_t)list->components_count, sizeof(int));
    rel_y_of = calloc((size_t)list->components_count, sizeof(int));
    order = calloc((size_t)list->components_count, sizeof(int));
    assert(visited);
    assert(stack);
    assert(parent_of);
    assert(rel_x_of);
    assert(rel_y_of);
    assert(order);

    /* Preorder DFS with an explicit stack; children pushed reversed so they pop
     * in declaration order (v1 dat1_component_walk). */
    stack_top = 0;
    order_count = 0;
    stack[stack_top++] = root_id;
    parent_of[root_id] = -1;
    rel_x_of[root_id] = root->x;
    rel_y_of[root_id] = root->y;
    visited[root_id] = 1;

    while( stack_top > 0 )
    {
        int comp_id = stack[--stack_top];
        struct RSCache_Dat1ConfigComponent const* comp = list->components[comp_id];
        if( !comp )
            continue;

        order[order_count++] = comp_id;

        if( comp->type == RSCACHE_DAT1_COMPONENT_TYPE_LAYER && comp->children )
        {
            for( int i = comp->children_count - 1; i >= 0; i-- )
            {
                int child_id = comp->children[i];
                struct RSCache_Dat1ConfigComponent const* child;
                if( child_id < 0 || child_id >= list->components_count )
                    continue;
                child = list->components[child_id];
                if( !child || visited[child_id] )
                    continue;
                visited[child_id] = 1;
                parent_of[child_id] = comp_id;
                rel_x_of[child_id] = comp->childX[i] + child->x;
                rel_y_of[child_id] = comp->childY[i] + child->y;
                stack[stack_top++] = child_id;
            }
        }
    }

    pack = calloc(1, sizeof(struct ToriRS_ComponentPack));
    assert(pack);
    pack->component_count = order_count;
    pack->components = calloc((size_t)order_count, sizeof(struct ToriRS_Component));
    assert(pack->components);

    for( int i = 0; i < order_count; i++ )
    {
        int comp_id = order[i];
        struct ToriRS_Component* converted =
            ToriRS_ComponentFromRSCacheDat1(list->components[comp_id]);
        if( !converted )
            continue;
        pack->components[i] = *converted;
        free(converted);
        ToriRS_ComponentApplyWalkLayout(
            &pack->components[i], parent_of[comp_id], rel_x_of[comp_id], rel_y_of[comp_id]);
    }

    free(visited);
    free(stack);
    free(parent_of);
    free(rel_x_of);
    free(rel_y_of);
    free(order);
    return pack;
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

        /*
         * `baseX`/`baseY` for BOTH formats. `x`/`y` on the decoded component
         * are the reference's RUNTIME position — a field the server writes —
         * and the dat2 decoder never populates them: every one of its three
         * paths, IF1 included, reads the authored position into `baseX`.
         *
         * Reading `x` for an if3=0 component therefore read a zero, and every
         * IF1 component in the cache laid out at its parent's origin. It is
         * quiet because the legacy interfaces are few and mostly small:
         * `messagescroll`'s scroll model sat at (0,0) instead of (220,166),
         * and 65 interfaces in the corpus were wrong the same way.
         */
        ToriRS_ComponentApplyWalkLayout(dst, rs->layer, rs->baseX, rs->baseY);
        if( rs->baseWidth > 0 )
            dst->width = rs->baseWidth;
        if( rs->baseHeight > 0 )
            dst->height = rs->baseHeight;
    }
}

struct ToriRS_ComponentPack*
ToriRS_ComponentPackFromRSCacheDat2(const struct RSCache_Dat2ComponentPack* src)
{
    assert(src);
    if( src->component_count <= 0 || !src->components )
        return NULL;

    struct ToriRS_ComponentPack* pack = calloc(1, sizeof(struct ToriRS_ComponentPack));
    assert(pack);

    pack->component_count = src->component_count;
    pack->components = calloc((size_t)pack->component_count, sizeof(struct ToriRS_Component));
    assert(pack->components);

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
