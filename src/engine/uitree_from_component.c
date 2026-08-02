#include "uitree_from_component.h"

#include "torirs_types.h"
#include "ui/uitree.h"
#include "ui/uitree_build.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void
bake_runtime_hook_from_torirs(
    struct UITree* tree,
    int component_id,
    struct UITreeRuntimeScriptHook* slot,
    struct ToriRS_ScriptHook const* src)
{
    int script_id;
    int const* args;
    int arg_count;
    char const* strp[TORIRS_COMPONENT_HOOK_STR_MAX];
    int i;

    assert(tree);
    assert(slot);
    if( !src || src->argc <= 0 )
        return;

    script_id = src->argv[0];
    if( script_id <= 0 )
        return;

    if( src->argc > 1 )
    {
        args = src->argv + 1;
        arg_count = src->argc - 1;
    }
    else
    {
        args = NULL;
        arg_count = 0;
    }
    for( i = 0; i < TORIRS_COMPONENT_HOOK_STR_MAX; i++ )
        strp[i] = src->strv[i];
    /* argv[0] is the script id (always int) — args drop it, so shift str_mask. */
    (void)UITree_ApplyRuntimeHook(
        tree,
        component_id,
        slot,
        script_id,
        args,
        arg_count,
        (uint32_t)(src->str_mask >> 1),
        strp,
        src->str_argc);
}

static void
bake_pack_runtime_hooks(
    struct UITree* tree,
    struct ToriRS_ComponentPack const* pack)
{
    int i;

    assert(tree);
    assert(pack);

    for( i = 0; i < pack->component_count; i++ )
    {
        struct ToriRS_Component const* src = &pack->components[i];
        int32_t idx = UITree_FindByComponentId(tree, src->id);
        struct UITreeComponent* node;

        if( idx < 0 )
            continue;
        node = &tree->components[idx];
        bake_runtime_hook_from_torirs(
            tree, src->id, &node->runtime_hooks.on_click, &src->on_click);
        bake_runtime_hook_from_torirs(
            tree, src->id, &node->runtime_hooks.on_op, &src->on_op);
        bake_runtime_hook_from_torirs(
            tree, src->id, &node->runtime_hooks.on_mouse_over, &src->on_mouse_over);
        bake_runtime_hook_from_torirs(
            tree, src->id, &node->runtime_hooks.on_mouse_leave, &src->on_mouse_leave);
        bake_runtime_hook_from_torirs(
            tree, src->id, &node->runtime_hooks.on_drag, &src->on_drag);
        bake_runtime_hook_from_torirs(
            tree, src->id, &node->runtime_hooks.on_drag_complete, &src->on_drag_complete);
        bake_runtime_hook_from_torirs(
            tree, src->id, &node->runtime_hooks.on_hold, &src->on_hold);
        bake_runtime_hook_from_torirs(
            tree, src->id, &node->runtime_hooks.on_mouse_repeat, &src->on_mouse_repeat);
        bake_runtime_hook_from_torirs(
            tree, src->id, &node->runtime_hooks.on_scroll_wheel, &src->on_scroll_wheel);
        bake_runtime_hook_from_torirs(
            tree, src->id, &node->runtime_hooks.on_timer, &src->on_timer);
    }
}

void
UITree_FillBuildFromToriRS(
    struct UIBuildComponent* dst,
    struct ToriRS_Component const* src)
{
    assert(dst);
    assert(src);

    memset(dst, 0, sizeof(*dst));

    dst->id = src->id;
    dst->parent_id = src->parent_id;
    dst->base_x = src->if3 ? src->base_x : src->rel_x;
    dst->base_y = src->if3 ? src->base_y : src->rel_y;
    dst->base_width = src->if3 ? src->base_width : src->width;
    dst->base_height = src->if3 ? src->base_height : src->height;
    dst->scroll_height = src->scroll_height;
    dst->scroll_width = src->scroll_width;
    dst->no_click_through = src->no_click_through;
    dst->if3 = src->if3;
    dst->x_mode = src->x_mode;
    dst->y_mode = src->y_mode;
    dst->width_mode = src->width_mode;
    dst->height_mode = src->height_mode;
    dst->aspect_w = src->aspect_w;
    dst->aspect_h = src->aspect_h;

    switch( src->type )
    {
    case TORIRS_COMPONENT_LAYER:
    case TORIRS_COMPONENT_UNUSED:
        dst->type = UIBUILD_LAYER;
        break;
    case TORIRS_COMPONENT_INV:
        dst->type = UIBUILD_INV;
        break;
    case TORIRS_COMPONENT_RECT:
        dst->type = UIBUILD_RECT;
        break;
    case TORIRS_COMPONENT_TEXT:
        dst->type = UIBUILD_TEXT;
        break;
    case TORIRS_COMPONENT_GRAPHIC:
        dst->type = UIBUILD_GRAPHIC;
        break;
    case TORIRS_COMPONENT_MODEL:
        dst->type = UIBUILD_MODEL;
        break;
    case TORIRS_COMPONENT_INV_TEXT:
        dst->type = UIBUILD_INV_TEXT;
        break;
    case TORIRS_COMPONENT_LINE:
        dst->type = UIBUILD_LINE;
        break;
    }

    dst->graphic = src->graphic;
    dst->graphic_active = src->graphic_active;
    dst->outline = src->outline;
    dst->graphic_shadow = src->graphic_shadow;
    dst->horizontal_flip = src->horizontal_flip;
    dst->vertical_flip = src->vertical_flip;
    dst->tiled = src->tiled;
    dst->graphic_hitbox_only = src->graphic_hitbox_only;
    dst->transparency = src->transparency;
    dst->color = src->color;
    dst->filled = src->filled;
    dst->font_id = src->font_id;
    dst->text_h_align = src->text_h_align;
    dst->text_v_align = src->text_v_align;
    dst->text_line_height = src->text_line_height;
    dst->shadowed = src->shadowed;
    dst->text = src->text[0] ? src->text : NULL;
    dst->text_active = src->active_text[0] ? src->active_text : NULL;
    /* Reference IfType.getModel(type, id) returns null for modelType 0 — only
     * type 1 names a cache model. dat2 also encodes "unset" as modelId -1, but
     * dat1 encodes it as modelType 0 with modelId 0, and downstream treats any
     * id >= 0 as a real cache model. Without this gate the dat1 player-design
     * preview (iface 3559 com 3650: modelType 0, clientCode 327) resolved to
     * cache model 0 instead of falling through to the clientCode compositor,
     * so the widget rendered nothing. Types 2/3/4 (npc head / player head /
     * obj) are supplied at runtime by IF_SETNPCHEAD, IF_SETPLAYERHEAD and the
     * CS2 setmodel ops, never by the pack decode. */
    dst->model_id = src->model_type == 1 ? src->model_id : -1;
    /* Same "no model" gate as above, applied to the active variant. Both
     * spellings of none survive it: dat1's type 0 and dat2's id -1. */
    dst->model_active_id = src->active_model_type == 1 ? src->active_model_id : -1;
    dst->model_seq_id = src->model_seq_id;
    dst->model_zoom = src->model_zoom;
    dst->model_xan = src->model_xan;
    dst->model_yan = src->model_yan;
    dst->model_zan = src->model_zan;
    dst->model_x_offset = src->model_x_offset;
    dst->model_y_offset = src->model_y_offset;
    dst->model_orthog = src->model_orthog;
    dst->model_fixed_zoom = src->model_fixed_zoom;
    dst->inv_cols = src->inv_cols;
    dst->inv_rows = src->inv_rows;
    dst->inv_can_drag = src->inv_can_drag;
    dst->inv_obj_ops = src->inv_obj_ops;
    dst->inv_obj_use = src->inv_obj_use;
    dst->margin_x = src->margin_x;
    dst->margin_y = src->margin_y;
    dst->line_width = src->line_width;
    dst->line_horizontal = src->line_horizontal;

    for( int i = 0; i < UI_INV_SLOT_OFFSET_MAX && i < TORIRS_INV_SLOT_MAX; i++ )
    {
        dst->inv_slot_offset_x[i] = src->inv_slot_offset_x[i];
        dst->inv_slot_offset_y[i] = src->inv_slot_offset_y[i];
        dst->inv_slot_graphic_id[i] = src->inv_slot_graphic_id[i];
    }

    dst->hide = src->hide;
    _Static_assert(
        sizeof(dst->option) == sizeof(src->option) && sizeof(dst->ops) == sizeof(src->ops),
        "ui menu option mirrors out of sync with torirs");
    memcpy(dst->option, src->option, sizeof(dst->option));
    memcpy(dst->ops, src->ops, sizeof(dst->ops));
    _Static_assert(
        sizeof(dst->target_verb) == sizeof(src->target_verb) &&
            sizeof(dst->target_base) == sizeof(src->target_base),
        "ui target verb mirrors out of sync with torirs");
    memcpy(dst->target_verb, src->target_verb, sizeof(dst->target_verb));
    memcpy(dst->target_base, src->target_base, sizeof(dst->target_base));
    dst->button_type = src->button_type;
    dst->client_code = src->client_code;
    dst->click_mask = src->click_mask;
    dst->over_layer_id = src->over_layer_id;
    dst->over_color = src->over_color;
    dst->active_color = src->active_color;
    dst->active_over_color = src->active_over_color;
    dst->drag_dead_zone = src->drag_dead_zone;
    dst->drag_dead_time = src->drag_dead_time;

    /* CS1 value scripts pass through as borrowed views; the tree deep-copies. */
    dst->scripts_count = src->scripts_count;
    dst->scripts = src->scripts;
    dst->scripts_lengths = src->scripts_lengths;
    dst->comparator_count = src->comparator_count;
    dst->script_comparator = src->script_comparator;
    dst->script_operand = src->script_operand;
    dst->script_kind = src->script_kind;
}

struct pack_adapter_ctx
{
    struct ToriRS_ComponentPack const* pack;
    struct UIBuildComponent* build_cache;
    int (*resolve_sprite)(
        void*,
        int);
    int (*resolve_font)(
        void*,
        int);
    void* ud;
};

static struct UIBuildComponent const*
pack_get_component(
    void* ud,
    int index)
{
    struct pack_adapter_ctx* ctx = (struct pack_adapter_ctx*)ud;
    assert(index >= 0 && index < ctx->pack->component_count);
    UITree_FillBuildFromToriRS(&ctx->build_cache[index], &ctx->pack->components[index]);
    return &ctx->build_cache[index];
}

static int
pack_get_parent_id(
    void* ud,
    int index)
{
    struct pack_adapter_ctx* ctx = (struct pack_adapter_ctx*)ud;
    return ctx->pack->components[index].parent_id;
}

static int
pack_resolve_sprite(
    void* ud,
    int graphic_id)
{
    struct pack_adapter_ctx* ctx = (struct pack_adapter_ctx*)ud;
    if( ctx->resolve_sprite )
        return ctx->resolve_sprite(ctx->ud, graphic_id);
    return graphic_id;
}

static int
pack_resolve_font(
    void* ud,
    int font_id)
{
    struct pack_adapter_ctx* ctx = (struct pack_adapter_ctx*)ud;
    if( ctx->resolve_font )
        return ctx->resolve_font(ctx->ud, font_id);
    return font_id;
}

int
UITree_BuildFromComponentPack(
    struct UITree* tree,
    struct ToriRS_ComponentPack const* pack,
    int (*resolve_sprite)(
        void*,
        int),
    int (*resolve_font)(
        void*,
        int),
    void* ud)
{
    assert(tree);
    assert(pack);

    if( pack->component_count <= 0 )
        return 0;

    struct UIBuildComponent* build_cache = (struct UIBuildComponent*)calloc(
        (size_t)pack->component_count, sizeof(struct UIBuildComponent));
    assert(build_cache);

    struct pack_adapter_ctx ctx;
    ctx.pack = pack;
    ctx.build_cache = build_cache;
    ctx.resolve_sprite = resolve_sprite;
    ctx.resolve_font = resolve_font;
    ctx.ud = ud;

    struct UITreeBuildSource source;
    memset(&source, 0, sizeof(source));
    source.count = pack->component_count;
    source.get_component = pack_get_component;
    source.get_parent_id = pack_get_parent_id;
    source.resolve_sprite = pack_resolve_sprite;
    source.resolve_font = pack_resolve_font;
    source.ud = &ctx;

    int result = UITree_BuildFromSource(tree, &source);
    if( result >= 0 )
        bake_pack_runtime_hooks(tree, pack);
    free(build_cache);
    return result;
}
