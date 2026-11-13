/*
 * The UI tree's half of a session checkpoint (ui/uitree_checkpoint.h).
 */
#include "ui/uitree_checkpoint.h"

#include "mem/torirs_mem.h"
#include "replay/replay_checkpoint_util.h"
#include "ui/uitree.h"
#include "ui/uitree_component_options.h"
#include "ui/uitree_frame.h"
#include "ui/uitree_hook.h"
#include "ui/uitree_layout.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- one node ------------------------------------------------------------- */

/* `dst` is a byte copy of `src`; give it its own heap. */
static void
ckpt_node_dup_heap(
    struct UITreeComponent* dst,
    struct UITreeComponent const* src)
{
    struct ToriMem_Group* g = ToriMem_Ui();
    struct UITreeBehavior* b = &dst->behavior;
    struct UITreeBehavior const* sb = &src->behavior;

    if( src->freed )
        return;

    /* Lookup caches: rebuilt on demand. */
    dst->visible_children = NULL;
    dst->visible_child_count = 0;
    dst->visible_child_capacity = 0;
    dst->visible_children_valid = 0;
    dst->model_render_cache = NULL;
    dst->child_key_index =
        Ckpt_Dup(g, src->child_key_index, (size_t)src->child_key_index_cap * sizeof(int32_t));

    dst->data_text = Ckpt_StrDup(g, src->data_text);
    dst->plugin_key = Ckpt_StrDup(g, src->plugin_key);
    dst->widget_geometry = UITree_CheckpointGeometryDup(src->widget_geometry);

    dst->params = Ckpt_Dup(g, src->params, (size_t)src->params_capacity * sizeof(src->params[0]));
    for( int i = 0; i < src->params_count; i++ )
        dst->params[i].str = Ckpt_StrDup(g, src->params[i].str);

    if( src->menu_options )
    {
        dst->menu_options = ToriMem_Memdup(g, src->menu_options, sizeof(*src->menu_options));
        assert(dst->menu_options);
        dst->menu_options->submenus =
            Ckpt_Dup(g, src->menu_options->submenus, sizeof(*src->menu_options->submenus));
    }
    dst->op_keys = Ckpt_Dup(g, src->op_keys, sizeof(*src->op_keys));
    if( src->runtime_hooks )
    {
        dst->runtime_hooks = UITree_HooksBlockNew();
        assert(dst->runtime_hooks);
        UITree_HooksBlockCopy(dst->runtime_hooks, src->runtime_hooks);
    }

    if( sb->scripts )
    {
        b->scripts = ToriMem_Calloc(g, (size_t)(sb->scripts_count > 0 ? sb->scripts_count : 1), sizeof(int*));
        assert(b->scripts);
        for( int s = 0; s < sb->scripts_count; s++ )
        {
            int const len = sb->scripts_lengths ? sb->scripts_lengths[s] : 0;

            b->scripts[s] = len > 0 ? Ckpt_Dup(g, sb->scripts[s], (size_t)len * sizeof(int)) : NULL;
        }
    }
    b->scripts_lengths = Ckpt_Dup(g, sb->scripts_lengths, (size_t)sb->scripts_count * sizeof(int));
    b->script_comparator = Ckpt_Dup(g, sb->script_comparator, (size_t)sb->comparator_count * sizeof(int));
    b->script_operand = Ckpt_Dup(g, sb->script_operand, (size_t)sb->comparator_count * sizeof(int));

    /* The union members that own heap -- the same list uitree.c frees. */
    switch( src->type )
    {
    case UIELEM_RS_TEXT:
        dst->u.rs_text.text = Ckpt_StrDup(g, src->u.rs_text.text);
        dst->u.rs_text.text_active = Ckpt_StrDup(g, src->u.rs_text.text_active);
        break;
    case UIELEM_RS_INV:
        dst->u.rs_inv.slots = Ckpt_Dup(g, src->u.rs_inv.slots, sizeof(*src->u.rs_inv.slots));
        break;
    case UIELEM_BUILTIN_CHAT:
        dst->u.chat = Ckpt_Dup(g, src->u.chat, sizeof(*src->u.chat));
        break;
    case UIELEM_BUILTIN_DEBUG_OVERLAY:
        dst->u.debug_overlay = Ckpt_Dup(g, src->u.debug_overlay, sizeof(*src->u.debug_overlay));
        break;
    case UIELEM_BUILTIN_CHAT_BUTTON:
        dst->u.chat_button = Ckpt_Dup(g, src->u.chat_button, sizeof(*src->u.chat_button));
        break;
    case UIELEM_BUILTIN_LOGIN_INPUT:
        dst->u.login_input = Ckpt_Dup(g, src->u.login_input, sizeof(*src->u.login_input));
        break;
    default:
        break;
    }
}

static size_t
ckpt_node_heap_bytes(struct UITreeComponent const* c)
{
    size_t bytes = 0;

    if( c->freed )
        return 0;
    bytes += (size_t)c->child_key_index_cap * sizeof(int32_t);
    bytes += c->data_text ? strlen(c->data_text) + 1u : 0u;
    bytes += c->plugin_key ? strlen(c->plugin_key) + 1u : 0u;
    bytes += (size_t)c->params_capacity * sizeof(c->params[0]);
    for( int i = 0; i < c->params_count; i++ )
        bytes += c->params[i].str ? strlen(c->params[i].str) + 1u : 0u;
    if( c->menu_options )
        bytes += sizeof(*c->menu_options) + (c->menu_options->submenus ? sizeof(*c->menu_options->submenus) : 0u);
    bytes += c->op_keys ? sizeof(*c->op_keys) : 0u;
    if( c->runtime_hooks )
    {
        bytes += sizeof(*c->runtime_hooks);
        for( int i = 0; i < UITree_HooksSlotCount(); i++ )
        {
            struct UITreeRuntimeScriptHook const* h = UITree_HooksSlotAtConst(c->runtime_hooks, i);

            bytes += (size_t)h->argc * sizeof(int) + (size_t)h->str_argc * sizeof(char*);
            for( int s = 0; s < h->str_argc; s++ )
                bytes += h->strv[s] ? strlen(h->strv[s]) + 1u : 0u;
        }
    }
    bytes += (size_t)c->behavior.scripts_count * (sizeof(int*) + sizeof(int));
    for( int s = 0; c->behavior.scripts && s < c->behavior.scripts_count; s++ )
        bytes += c->behavior.scripts_lengths ? (size_t)c->behavior.scripts_lengths[s] * sizeof(int) : 0u;
    bytes += (size_t)c->behavior.comparator_count * 2u * sizeof(int);
    if( c->type == UIELEM_RS_TEXT )
    {
        bytes += c->u.rs_text.text ? strlen(c->u.rs_text.text) + 1u : 0u;
        bytes += c->u.rs_text.text_active ? strlen(c->u.rs_text.text_active) + 1u : 0u;
    }
    if( c->type == UIELEM_RS_INV && c->u.rs_inv.slots )
        bytes += sizeof(*c->u.rs_inv.slots);
    return bytes;
}

/* --- the tree's own tables ------------------------------------------------ */

static void
ckpt_set_dup(struct UITreeNodeSet* set)
{
    struct ToriMem_Group* g = ToriMem_Ui();

    set->slots = Ckpt_Dup(g, set->slots, (size_t)set->cap * sizeof(int32_t));
    set->pos = Ckpt_Dup(g, set->pos, (size_t)set->pos_cap * sizeof(int32_t));
}

static size_t
ckpt_set_bytes(struct UITreeNodeSet const* set)
{
    return ((size_t)set->cap + (size_t)set->pos_cap) * sizeof(int32_t);
}

#define CKPT_TREE_SETS(F)                                                                          \
    F(models) F(timer_hooks) F(key_hooks) F(wheel_hooks) F(opkeys) F(client_code) F(resize_hooks)  \
    F(sub_change_hooks) F(scroll_layers) F(debug_overlays)

/* `dst` is a byte copy of `src`, which is a live tree or a checkpoint; give it
 * its own copy of everything the checkpoint keeps, and none of the caches. */
static void
ckpt_tree_dup(
    struct UITree* dst,
    struct UITree const* src)
{
    struct ToriMem_Group* g = ToriMem_Ui();

    /* The slots in use only: capacity is the high-water a grow left behind,
     * and a restored tree grows again on demand. */
    dst->components = Ckpt_Dup(g, src->components, (size_t)src->component_count * sizeof(src->components[0]));
    dst->component_capacity = src->component_count;
    /* tree-walk-exempt: session checkpoint copy; taken every few hundred ticks, not per frame */
    for( uint32_t i = 0; i < src->component_count; i++ )
        ckpt_node_dup_heap(&dst->components[i], &src->components[i]);
#define CKPT_SET_DUP(s) ckpt_set_dup(&dst->s);
    CKPT_TREE_SETS(CKPT_SET_DUP)
#undef CKPT_SET_DUP
    dst->group_map = Ckpt_Dup(g, src->group_map, (size_t)src->group_map_cap * sizeof(src->group_map[0]));
    for( uint32_t i = 0; i < src->group_map_cap; i++ )
        ckpt_set_dup(&dst->group_map[i].nodes);
    for( int i = 0; i < src->parked_edit_count; i++ )
        dst->parked_edits[i].edits = UITree_CheckpointGeometryDup(src->parked_edits[i].edits);
    dst->anchor_nodes = Ckpt_Dup(g, src->anchor_nodes, (size_t)src->anchor_node_capacity * sizeof(int32_t));

    /* Caches: invalid in the copy, rebuilt on the first ask. */
    dst->id_index_keys = NULL;
    dst->id_index_vals = NULL;
    dst->id_index_cap = 0;
    dst->id_index_tombs = 0;
    dst->id_index_valid = 0;
    dst->layout_order = NULL;
    dst->layout_depth = NULL;
    dst->layout_changed = NULL;
    dst->layout_cap = 0;
    dst->layout_order_count = 0;
    dst->layout_order_valid = 0;
    dst->layout_dirty = NULL;
    dst->layout_dirty_count = 0;
    dst->layout_dirty_cap = 0;
    dst->layout_dirty_overflow = 1;
    dst->layout_force_full = 1;
    dst->layout_stale = 1;
    dst->layout_resolved_valid = 0;
    dst->canvas_candidate_ids = NULL;
    dst->canvas_candidate_count = 0;
    dst->canvas_candidate_capacity = 0;
    dst->canvas_candidates_valid = 0;
    dst->anchor_plan = NULL;
    /* Scratch the live tree keeps for itself. */
    dst->emit_visited = NULL;
    dst->emit_visited_cap = 0;
    memset(dst->walk_scratch, 0, sizeof(dst->walk_scratch));
    dst->geometry_audit = NULL;
}

struct UITree*
UITree_CheckpointTake(struct UITree const* tree)
{
    struct UITree* cp;

    assert(tree);
    UITREE_SCAN_METER(tree);
    cp = ToriMem_Memdup(ToriMem_Ui(), tree, sizeof(*cp));
    assert(cp);
    ckpt_tree_dup(cp, tree);
    /* A copy runs no frame binder. */
    cp->frame_binder = NULL;
    cp->frame_binder_user = NULL;
    return cp;
}

void
UITree_CheckpointRestore(
    struct UITree* tree,
    struct UITree const* cp)
{
    struct UITree live;

    assert(tree);
    assert(cp);
    /* Only into the tree it came from: its refs (in the CS2 host's hooks, the
     * mount records) carry this tree's identity. */
    assert(cp->instance_id == tree->instance_id);
    UITREE_SCAN_METER(tree);

    memcpy(&live, tree, sizeof(live));
    UITree_CheckpointFreeContents(tree);
    ToriMem_Free(ToriMem_Ui(), tree->emit_visited);
    memcpy(tree, cp, sizeof(*tree));
    ckpt_tree_dup(tree, cp);

    /* What the live tree keeps: its scratch, its binder, its audit. */
    memcpy(tree->walk_scratch, live.walk_scratch, sizeof(tree->walk_scratch));
    tree->walk_node_visits = live.walk_node_visits;
    tree->frame_binder = live.frame_binder;
    tree->frame_binder_user = live.frame_binder_user;
    tree->geometry_audit = live.geometry_audit;
    tree->emit_epoch = live.emit_epoch;

    /* Every serial a cache keys on moves past where the live tree had it, so
     * no lookup made before the restore is trusted after it. */
    tree->generation = live.generation + 1u;
    tree->id_generation = live.id_generation + 1u;
    tree->frame_stamp_serial = live.frame_stamp_serial + 1u;
    tree->dirty_gen = live.dirty_gen + 1u;
    tree->anchor_generation = live.anchor_generation + 1u;
    tree->layout_resolve_seq = live.layout_resolve_seq + 1u;
    tree->overlay_motion_dirty = live.overlay_motion_dirty + 1u;
    tree->overlay_motion_layout = live.overlay_motion_layout + 1u;
    tree->overlay_motion_tracking = live.overlay_motion_tracking;
    /* tree-walk-exempt: one pass after a restore, never per frame */
    for( uint32_t i = 0; i < tree->component_count; i++ )
        tree->components[i].is_dirty = 1;
    UITree_LayoutInvalidate(tree);
    if( tree->geometry_audit )
        UITree_GeometryAuditCheck(tree, "checkpoint restore");
}

void
UITree_CheckpointFree(struct UITree* cp)
{
    if( !cp )
        return;
    UITree_CheckpointFreeContents(cp);
    ToriMem_Free(ToriMem_Ui(), cp);
}

size_t
UITree_CheckpointBytes(struct UITree const* cp)
{
    size_t bytes;

    assert(cp);
    bytes = sizeof(*cp) + (size_t)cp->component_count * sizeof(cp->components[0]);
    /* tree-walk-exempt: a size report for the checkpoint, never per frame */
    for( uint32_t i = 0; i < cp->component_count; i++ )
        bytes += ckpt_node_heap_bytes(&cp->components[i]);
#define CKPT_SET_BYTES(s) bytes += ckpt_set_bytes(&cp->s);
    CKPT_TREE_SETS(CKPT_SET_BYTES)
#undef CKPT_SET_BYTES
    bytes += (size_t)cp->group_map_cap * sizeof(cp->group_map[0]);
    for( uint32_t i = 0; i < cp->group_map_cap; i++ )
        bytes += ckpt_set_bytes(&cp->group_map[i].nodes);
    bytes += (size_t)cp->anchor_node_capacity * sizeof(int32_t);
    return bytes;
}

/* --- equality ------------------------------------------------------------- */

/* The authored half of a node's position: what scripts and packets set. The
 * abs_* box and layout_resolved are the layout's. */
#define CKPT_POSITION_FIELDS(F)                                                                    \
    F(position.kind) F(position.x) F(position.y) F(position.width) F(position.height)              \
    F(position.relative_flags) F(position.anchor_x) F(position.anchor_y) F(position.left)          \
    F(position.top) F(position.right) F(position.bottom) F(position.x_mode) F(position.y_mode)     \
    F(position.width_mode) F(position.height_mode) F(position.aspect_w) F(position.aspect_h)       \
    F(position.safe_area_source) F(position.safe_area_flags) F(position.safe_area_margin)

/* Node fields compared by value. Left out: incarnation, the dirty marks, the
 * lookup caches (last_child_hint, child_key_*, visible_children*), the icon
 * handles (item_scene_id/atlas), the model render cache, the drag visual (the
 * pointer's), the frame binder's stamps (slot_tag, frame_member_plus1,
 * role_id), the layout's culling (screen_hidden, projection_hidden) and the
 * plugin op serial (a process clock). */
#define CKPT_NODE_FIELDS(F)                                                                        \
    F(type) F(parent) F(first_child) F(next_sibling) F(component_id) F(trans) F(if3) F(freed)      \
    F(free_next) F(dynamic) F(dynamic_child_index) F(item_id) F(item_count) F(item_num_mode)       \
    F(colour) F(fill_colour) F(no_click_through) F(draggable) F(drag_behavior) F(drag_dead_zone)   \
    F(drag_dead_time) F(model_transparent) F(drag_render_area_uid)                                 \
    F(drag_render_area_child_index) F(cs1_active) F(native_hide) F(mount_hidden) F(cs1_values)     \
    F(target_priority) F(force_left_click) F(plugin_owner) F(plugin_canvas_pinned)                 \
    F(plugin_canvas_x) F(plugin_canvas_y) F(widget_hidden) F(widget_text_outline)                  \
    F(hotkey_effects) F(scroll_x) F(scroll_y) F(trans_bot) F(behavior.hide)                        \
    F(behavior.script_kind) F(behavior.scripts_count) F(behavior.comparator_count)                 \
    F(behavior.button_type) F(behavior.client_code) F(behavior.click_mask)                         \
    F(behavior.target_mask) F(behavior.over_layer_id) F(behavior.over_color)                       \
    F(behavior.active_color) F(behavior.active_over_color)

static int
ckpt_hooks_equal(
    struct UITreeRuntimeHooks const* a,
    struct UITreeRuntimeHooks const* b,
    char const** slot)
{
    for( int i = 0; i < UITree_HooksSlotCount(); i++ )
    {
        struct UITreeRuntimeScriptHook const* ha = a ? UITree_HooksSlotAtConst(a, i) : NULL;
        struct UITreeRuntimeScriptHook const* hb = b ? UITree_HooksSlotAtConst(b, i) : NULL;
        int const sa = ha ? ha->script_id : 0;
        int const sb = hb ? hb->script_id : 0;

        *slot = UITree_HooksSlotName(i);
        if( sa != sb )
            return 0;
        if( sa == 0 )
            continue;
        if( !UITree_HookEquals(ha, hb->script_id, hb->argv, hb->argc, hb->str_mask,
                               (char const* const*)hb->strv, hb->str_argc) )
            return 0;
    }
    return 1;
}

static int
ckpt_union_equal(
    struct UITreeComponent const* a,
    struct UITreeComponent const* b,
    char const** field)
{
    switch( a->type )
    {
    case UIELEM_RS_TEXT:
        *field = "u.rs_text";
        return a->u.rs_text.font_id == b->u.rs_text.font_id && a->u.rs_text.color == b->u.rs_text.color &&
               a->u.rs_text.center == b->u.rs_text.center && a->u.rs_text.y_align == b->u.rs_text.y_align &&
               a->u.rs_text.baseline == b->u.rs_text.baseline &&
               a->u.rs_text.line_height == b->u.rs_text.line_height &&
               a->u.rs_text.shadowed == b->u.rs_text.shadowed && a->u.rs_text.input == b->u.rs_text.input &&
               a->u.rs_text.caret == b->u.rs_text.caret &&
               a->u.rs_text.input_wrap_width == b->u.rs_text.input_wrap_width &&
               Ckpt_StrEq(a->u.rs_text.text, b->u.rs_text.text) &&
               Ckpt_StrEq(a->u.rs_text.text_active, b->u.rs_text.text_active);
    case UIELEM_RS_MODEL:
    {
        /* Less the animation phase, which the frame clock moves. */
        struct UITreeComponent ma;
        struct UITreeComponent mb;

        *field = "u.rs_model";
        memcpy(&ma.u, &a->u, sizeof(ma.u));
        memcpy(&mb.u, &b->u, sizeof(mb.u));
        ma.u.rs_model.anim_frame = mb.u.rs_model.anim_frame = 0;
        ma.u.rs_model.anim_frame_cycle = mb.u.rs_model.anim_frame_cycle = 0;
        return memcmp(&ma.u.rs_model, &mb.u.rs_model, sizeof(ma.u.rs_model)) == 0;
    }
    case UIELEM_RS_INV:
        *field = "u.rs_inv";
        if( a->u.rs_inv.inv_source_id != b->u.rs_inv.inv_source_id || a->u.rs_inv.cols != b->u.rs_inv.cols ||
            a->u.rs_inv.rows != b->u.rs_inv.rows || a->u.rs_inv.margin_x != b->u.rs_inv.margin_x ||
            a->u.rs_inv.margin_y != b->u.rs_inv.margin_y || a->u.rs_inv.can_drag != b->u.rs_inv.can_drag ||
            a->u.rs_inv.obj_ops != b->u.rs_inv.obj_ops || a->u.rs_inv.obj_use != b->u.rs_inv.obj_use )
            return 0;
        if( !a->u.rs_inv.slots || !b->u.rs_inv.slots )
            return a->u.rs_inv.slots == b->u.rs_inv.slots;
        return memcmp(a->u.rs_inv.slots->offset_x, b->u.rs_inv.slots->offset_x,
                      sizeof(a->u.rs_inv.slots->offset_x)) == 0 &&
               memcmp(a->u.rs_inv.slots->offset_y, b->u.rs_inv.slots->offset_y,
                      sizeof(a->u.rs_inv.slots->offset_y)) == 0;
    case UIELEM_BUILTIN_CHAT:
        *field = "u.chat";
        if( !a->u.chat || !b->u.chat )
            return a->u.chat == b->u.chat;
        return memcmp(a->u.chat, b->u.chat, sizeof(*a->u.chat)) == 0;
    case UIELEM_BUILTIN_DEBUG_OVERLAY:
        *field = "u.debug_overlay";
        if( !a->u.debug_overlay || !b->u.debug_overlay )
            return a->u.debug_overlay == b->u.debug_overlay;
        return memcmp(a->u.debug_overlay, b->u.debug_overlay, sizeof(*a->u.debug_overlay)) == 0;
    case UIELEM_BUILTIN_CHAT_BUTTON:
        *field = "u.chat_button";
        if( !a->u.chat_button || !b->u.chat_button )
            return a->u.chat_button == b->u.chat_button;
        return memcmp(a->u.chat_button, b->u.chat_button, sizeof(*a->u.chat_button)) == 0;
    case UIELEM_BUILTIN_LOGIN_INPUT:
        *field = "u.login_input";
        if( !a->u.login_input || !b->u.login_input )
            return a->u.login_input == b->u.login_input;
        return memcmp(a->u.login_input, b->u.login_input, sizeof(*a->u.login_input)) == 0;
    default:
        *field = "u";
        return memcmp(&a->u, &b->u, sizeof(a->u)) == 0;
    }
}

/* 1 when node `i` is the same in both; else 0 and the field in `*field`. */
static int
ckpt_node_equal(
    struct UITreeComponent const* a,
    struct UITreeComponent const* b,
    int world_placed,
    char const** field)
{
#define CKPT_NODE_CMP(f)                                                                           \
    if( memcmp(&a->f, &b->f, sizeof(a->f)) != 0 )                                                 \
    {                                                                                              \
        static char detail[96];                                                                    \
                                                                                                   \
        if( sizeof(a->f) == sizeof(int) )                                                          \
        {                                                                                          \
            int va;                                                                                \
            int vb;                                                                                \
                                                                                                   \
            memcpy(&va, &a->f, sizeof(va));                                                        \
            memcpy(&vb, &b->f, sizeof(vb));                                                        \
            snprintf(detail, sizeof(detail), "%s %d != %d", #f, va, vb);                           \
            *field = detail;                                                                       \
        }                                                                                          \
        else                                                                                       \
            *field = #f;                                                                           \
        return 0;                                                                                  \
    }
    CKPT_NODE_FIELDS(CKPT_NODE_CMP)
    if( a->freed )
        return 1;
    /* A scripted entity overlay's layer is placed every frame at its subject's
     * projected screen position (app_entity_overlay_layout): the world's, and
     * the camera's, not the session's. */
    if( !world_placed )
    {
        CKPT_POSITION_FIELDS(CKPT_NODE_CMP)
    }
#undef CKPT_NODE_CMP
    *field = "data_text";
    if( !Ckpt_StrEq(a->data_text, b->data_text) )
        return 0;
    *field = "plugin_key";
    if( !Ckpt_StrEq(a->plugin_key, b->plugin_key) )
        return 0;
    *field = "params";
    if( a->params_count != b->params_count )
        return 0;
    for( int i = 0; i < a->params_count; i++ )
        if( a->params[i].id != b->params[i].id || a->params[i].value != b->params[i].value ||
            !Ckpt_StrEq(a->params[i].str, b->params[i].str) )
            return 0;
    *field = "menu_options";
    if( !a->menu_options || !b->menu_options )
    {
        if( a->menu_options != b->menu_options )
            return 0;
    }
    else
    {
        struct UITreeMenuOptions ma;
        struct UITreeMenuOptions mb;

        memcpy(&ma, a->menu_options, sizeof(ma));
        memcpy(&mb, b->menu_options, sizeof(mb));
        ma.submenus = mb.submenus = NULL;
        if( memcmp(&ma, &mb, sizeof(ma)) != 0 )
            return 0;
        *field = "menu_options.submenus";
        if( !a->menu_options->submenus || !b->menu_options->submenus )
        {
            if( a->menu_options->submenus != b->menu_options->submenus )
                return 0;
        }
        else if( memcmp(a->menu_options->submenus, b->menu_options->submenus,
                        sizeof(*a->menu_options->submenus)) != 0 )
            return 0;
    }
    *field = "op_keys";
    if( memcmp(UITree_OpKeys(a), UITree_OpKeys(b), sizeof(struct UITreeOpKeys)) != 0 )
        return 0;
    if( !ckpt_hooks_equal(a->runtime_hooks, b->runtime_hooks, field) )
        return 0;
    *field = "behavior.scripts";
    for( int s = 0; s < a->behavior.scripts_count; s++ )
    {
        int const la = a->behavior.scripts_lengths ? a->behavior.scripts_lengths[s] : 0;
        int const lb = b->behavior.scripts_lengths ? b->behavior.scripts_lengths[s] : 0;

        if( la != lb )
            return 0;
        if( la > 0 && memcmp(a->behavior.scripts[s], b->behavior.scripts[s], (size_t)la * sizeof(int)) != 0 )
            return 0;
    }
    *field = "behavior.script_comparator";
    if( a->behavior.comparator_count > 0 &&
        ((!a->behavior.script_comparator) != (!b->behavior.script_comparator) ||
         (!a->behavior.script_operand) != (!b->behavior.script_operand) ||
         (a->behavior.script_comparator &&
          memcmp(a->behavior.script_comparator, b->behavior.script_comparator,
                 (size_t)a->behavior.comparator_count * sizeof(int)) != 0) ||
         (a->behavior.script_operand &&
          memcmp(a->behavior.script_operand, b->behavior.script_operand,
                 (size_t)a->behavior.comparator_count * sizeof(int)) != 0)) )
        return 0;
    *field = "widget_geometry";
    if( !UITree_CheckpointGeometryEqual(a->widget_geometry, b->widget_geometry) )
        return 0;
    return ckpt_union_equal(a, b, field);
}

int
UITree_CheckpointEqual(
    struct UITree const* a,
    struct UITree const* b,
    char* why,
    int why_cap)
{
    assert(a);
    assert(b);
    CKPT_EQ_INT(a, b, root_index, why, why_cap);
    CKPT_EQ_INT(a, b, last_root_index, why, why_cap);
    CKPT_EQ_INT(a, b, free_head, why, why_cap);
    CKPT_EQ_INT(a, b, cs1_script_nodes, why, why_cap);
    CKPT_EQ_INT(a, b, group_close_serial, why, why_cap);
    CKPT_EQ_INT(a, b, interface_parent_count, why, why_cap);
    for( int i = 0; i < a->interface_parent_count; i++ )
    {
        if( a->interface_parents[i].container_uid != b->interface_parents[i].container_uid ||
            a->interface_parents[i].group_id != b->interface_parents[i].group_id ||
            a->interface_parents[i].type != b->interface_parents[i].type )
        {
            Ckpt_Why(why, why_cap, "interface_parents[%d] (group %d on 0x%08x)", i,
                     a->interface_parents[i].group_id, (unsigned)a->interface_parents[i].container_uid);
            return 0;
        }
    }
    CKPT_EQ_INT(a, b, hotkey_count, why, why_cap);
    for( int i = 0; i < a->hotkey_count; i++ )
    {
        if( a->hotkeys[i].osrs_key != b->hotkeys[i].osrs_key ||
            a->hotkeys[i].node_index != b->hotkeys[i].node_index ||
            a->hotkeys[i].effect != b->hotkeys[i].effect )
        {
            Ckpt_Why(why, why_cap, "hotkeys[%d]", i);
            return 0;
        }
    }
    CKPT_EQ_INT(a, b, input_focus.index, why, why_cap);
    CKPT_EQ_INT(a, b, pause_pending.index, why, why_cap);
    CKPT_EQ_INT(a, b, parked_edit_count, why, why_cap);
    CKPT_EQ_INT(a, b, owned_count_entries, why, why_cap);
    /* tree-walk-exempt: the checkpoint check's comparison, never per frame */
    for( uint32_t i = 0; i < a->component_count && i < b->component_count; i++ )
    {
        struct UITreeComponent const* ca = &a->components[i];
        char const* field = "";

        int const world_placed = ca->type == UIELEM_RS_LAYER && a->entity_overlay_index >= 0 &&
                                 ca->parent == a->entity_overlay_index;

        if( !ckpt_node_equal(ca, &b->components[i], world_placed, &field) )
        {
            Ckpt_Why(why, why_cap, "node %u (com 0x%08x %s, parent %d com 0x%08x) %s", i, (unsigned)ca->component_id,
                     UITree_ComponentTypeStr(ca->type), ca->parent,
                     ca->parent >= 0 && (uint32_t)ca->parent < a->component_count
                         ? (unsigned)a->components[ca->parent].component_id
                         : 0xffffffffu,
                     field);
            return 0;
        }
        if( !ca->freed && ca->component_id >= 0 )
        {
            int const group = (ca->component_id >> 16) & 0xffff;

            if( UITree_GroupCloseSerial(a, group) != UITree_GroupCloseSerial(b, group) )
            {
                Ckpt_Why(why, why_cap, "group %d close serial %u != %u", group, UITree_GroupCloseSerial(a, group),
                         UITree_GroupCloseSerial(b, group));
                return 0;
            }
        }
    }
    /* After the nodes, so a difference names the node rather than the count. */
    CKPT_EQ_INT(a, b, component_count, why, why_cap);
    CKPT_EQ_INT(a, b, next_dynamic_uid, why, why_cap);
    return 1;
}
