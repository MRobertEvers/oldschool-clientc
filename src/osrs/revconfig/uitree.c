#include "uitree.h"

#include "graphics/dash.h"
#include "osrs/buildcachedat.h"
#include "osrs/clientscript_vm.h"
#include "osrs/dash_utils.h"
#include "osrs/entity_scenebuild.h"
#include "osrs/game.h"
#include "osrs/gamecache/gamecache_sequence.h"
#include "osrs/interface.h"
#include "osrs/interface_state.h"
#include "osrs/minimenu_action.h"
#include "osrs/rscache/tables_dat/config_component.h"
#include "osrs/rscache/tables_dat/config_obj.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UITREE_SCROLLBAR_W 16

/** When 1: skip sequence keyframes on interface/chat MODEL meshes (bind pose only). Re-enable with
 *  -UTORIRS_DISABLE_INTERFACE_MODEL_ANIM=0. */
#ifndef TORIRS_DISABLE_INTERFACE_MODEL_ANIM
#define TORIRS_DISABLE_INTERFACE_MODEL_ANIM 0
#endif

static int
uitree_node_depth(
    struct UITree* t,
    int idx)
{
    int d = 0;
    for( ;; )
    {
        int p = t->components[idx].parent;
        if( p < 0 )
            return d;
        d++;
        idx = p;
    }
}

static int
iface_scroll_clamped(
    struct GGame* game,
    int component_id,
    int scroll_height,
    int layer_h)
{
    if( !game->iface || component_id < 0 || component_id >= MAX_IFACE_SCROLL_IDS )
        return 0;
    int sp = game->iface->component_scroll_position[component_id];
    int max_scroll = scroll_height - layer_h;
    if( max_scroll < 0 )
        max_scroll = 0;
    if( sp < 0 )
        sp = 0;
    if( sp > max_scroll )
        sp = max_scroll;
    return sp;
}

static int
scrollbar_hit_region(
    int local_y,
    int layer_h,
    int scroll_pos,
    int scroll_height)
{
    if( scroll_height <= layer_h )
        return 2;
    if( local_y < 16 )
        return 0;
    if( local_y >= layer_h - 16 )
        return 1;
    int track_h = layer_h - 32;
    if( track_h <= 0 )
        return 2;
    int grip_size = (track_h * layer_h) / scroll_height;
    if( grip_size < 8 )
        grip_size = 8;
    if( grip_size > track_h )
        grip_size = track_h;
    int range = scroll_height - layer_h;
    if( range < 0 )
        range = 0;
    int grip_y = range > 0 ? ((track_h - grip_size) * scroll_pos) / range : 0;
    if( grip_y < 0 )
        grip_y = 0;
    if( grip_y > track_h - grip_size )
        grip_y = track_h - grip_size;
    int rel = local_y - 16;
    if( rel >= grip_y && rel < grip_y + grip_size )
        return 3;
    return 2;
}

static void
uitree_visit_scrollbar_candidates(
    struct GGame* game,
    struct UITree* t,
    int idx,
    int mx,
    int my,
    int32_t* best_gutter_idx,
    int* best_gutter_depth,
    int32_t* best_layer_idx,
    int* best_layer_depth)
{
    struct StaticUIComponent* c = &t->components[idx];
    int depth = uitree_node_depth(t, idx);

    if( c->type == UIELEM_RS_LAYER && !c->is_hidden &&
        (!c->u.rs_layer.hide || interface_component_is_overlay_hovered(game, c->component_id)) )
    {
        int lh = c->position.height;
        int sh = c->u.rs_layer.scroll_height;
        int lx = c->position.x;
        int ly = c->position.y;
        int lw = c->position.width;

        if( sh > lh && c->component_id >= 0 )
        {
            int gx0 = lx + lw;
            int gx1 = gx0 + UITREE_SCROLLBAR_W;
            if( mx >= gx0 && mx < gx1 && my >= ly && my < ly + lh && depth > *best_gutter_depth )
            {
                *best_gutter_depth = depth;
                *best_gutter_idx = idx;
            }

            int rx = lx + lw + UITREE_SCROLLBAR_W;
            if( mx >= lx && mx < rx && my >= ly && my < ly + lh && depth > *best_layer_depth )
            {
                *best_layer_depth = depth;
                *best_layer_idx = idx;
            }
        }
    }

    for( int ch = c->first_child; ch >= 0; ch = t->components[ch].next_sibling )
        uitree_visit_scrollbar_candidates(
            game,
            t,
            ch,
            mx,
            my,
            best_gutter_idx,
            best_gutter_depth,
            best_layer_idx,
            best_layer_depth);
}

static int32_t
link_under_parent(
    struct UITree* tree,
    int32_t parent_index,
    int32_t new_index)
{
    struct StaticUIComponent* new_c = &tree->components[new_index];
    new_c->parent = parent_index;
    new_c->first_child = -1;
    new_c->next_sibling = -1;

    if( parent_index < 0 )
    {
        new_c->parent = -1;
        if( tree->root_index < 0 )
        {
            tree->root_index = new_index;
        }
        else
        {
            int32_t walk = tree->root_index;
            while( tree->components[walk].next_sibling >= 0 )
                walk = tree->components[walk].next_sibling;
            tree->components[walk].next_sibling = new_index;
        }
        return new_index;
    }

    struct StaticUIComponent* p = &tree->components[parent_index];
    if( p->first_child < 0 )
    {
        p->first_child = new_index;
    }
    else
    {
        int32_t walk = p->first_child;
        while( tree->components[walk].next_sibling >= 0 )
            walk = tree->components[walk].next_sibling;
        tree->components[walk].next_sibling = new_index;
    }
    return new_index;
}

static int32_t
push_element(
    struct UITree* tree,
    int32_t parent_index)
{
    if( tree->component_count >= tree->component_capacity )
    {
        if( tree->component_capacity == 0 )
            tree->component_capacity = 16;

        uint32_t new_capacity = tree->component_capacity == 0 ? 16 : tree->component_capacity * 2;
        struct StaticUIComponent* new_components =
            realloc(tree->components, new_capacity * sizeof(struct StaticUIComponent));
        if( !new_components )
            return -1;
        tree->components = new_components;
        tree->component_capacity = new_capacity;
    }

    int32_t idx = (int32_t)tree->component_count++;
    struct StaticUIComponent* component = &tree->components[idx];
    memset(component, 0, sizeof(struct StaticUIComponent));
    component->parent = -1;
    component->first_child = -1;
    component->next_sibling = -1;
    component->component_id = -1;

    link_under_parent(tree, parent_index, idx);
    tree->generation++;
    return idx;
}

struct UIInventoryPool*
uitree_inv_pool_new(int capacity)
{
    struct UIInventoryPool* pool = malloc(sizeof(struct UIInventoryPool));
    if( !pool )
        return NULL;
    memset(pool, 0, sizeof(struct UIInventoryPool));
    pool->capacity = capacity > 0 ? capacity : 8;
    pool->inventories = calloc((size_t)pool->capacity, sizeof(struct UIInventory));
    if( !pool->inventories )
    {
        free(pool);
        return NULL;
    }
    return pool;
}

void
uitree_inv_pool_free(struct UIInventoryPool* pool)
{
    if( !pool )
        return;
    free(pool->inventories);
    free(pool);
}

int
uitree_inv_pool_find_by_name(
    struct UIInventoryPool* pool,
    char const* name)
{
    if( !pool || !name || name[0] == '\0' )
        return -1;
    for( int i = 0; i < pool->count; i++ )
    {
        if( strcmp(pool->inventories[i].name, name) == 0 )
            return i;
    }
    return -1;
}

int
uitree_inv_pool_append(
    struct UIInventoryPool* pool,
    struct UIInventory const* inv)
{
    if( !pool || !inv )
        return -1;
    if( pool->count >= pool->capacity )
    {
        int new_cap = pool->capacity * 2;
        struct UIInventory* ni =
            realloc(pool->inventories, (size_t)new_cap * sizeof(struct UIInventory));
        if( !ni )
            return -1;
        pool->inventories = ni;
        pool->capacity = new_cap;
    }
    pool->inventories[pool->count] = *inv;
    return pool->count++;
}

int
uitree_inv_pool_find_or_append_by_component_id(
    struct UIInventoryPool* pool,
    int component_id)
{
    char name[72];
    if( !pool || component_id < 0 )
        return -1;
    if( snprintf(name, sizeof(name), "if_%d", component_id) >= (int)sizeof(name) )
        return -1;
    int idx = uitree_inv_pool_find_by_name(pool, name);
    if( idx >= 0 )
        return idx;
    struct UIInventory inv = { 0 };
    strncpy(inv.name, name, sizeof(inv.name) - 1);
    inv.name[sizeof(inv.name) - 1] = '\0';
    for( int j = 0; j < UI_INVENTORY_MAX_ITEMS; j++ )
        inv.items[j].scene_id = -1;
    return uitree_inv_pool_append(pool, &inv);
}

int32_t
uitree_find_by_component_id(
    const struct UITree* tree,
    int component_id)
{
    if( !tree || component_id < 0 )
        return -1;
    for( uint32_t i = 0; i < tree->component_count; i++ )
        if( tree->components[i].component_id == component_id )
            return (int32_t)i;
    return -1;
}

int
uitree_find_inv_index_by_component_id(
    const struct UITree* tree,
    int component_id,
    int32_t* out_node_idx)
{
    if( out_node_idx )
        *out_node_idx = -1;
    if( !tree || component_id < 0 )
        return -1;
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct StaticUIComponent const* c = &tree->components[i];
        if( (c->type == UIELEM_RS_INV || c->type == UIELEM_RS_INV_TEXT) &&
            c->component_id == component_id )
        {
            if( out_node_idx )
                *out_node_idx = (int32_t)i;
            return c->u.rs_inv.inv_index;
        }
    }
    return -1;
}

char const*
uitree_component_type_str(enum StaticUIComponentType type)
{
    switch( type )
    {
    case UIELEM_BUILTIN_COMPASS:
        return "compass";
    case UIELEM_BUILTIN_MINIMAP:
        return "minimap";
    case UIELEM_BUILTIN_WORLD:
        return "world";
    case UIELEM_BUILTIN_SIDEBAR:
        return "sidebar";
    case UIELEM_BUILTIN_CHAT:
        return "chat";
    case UIELEM_BUILTIN_SPRITE:
        return "sprite";
    case UIELEM_BUILTIN_REDSTONE_TAB:
        return "redstone_tab";
    case UIELEM_BUILTIN_TAB_ICONS:
        return "tab_icons";
    case UIELEM_BUILTIN_HOVER_TOOLTIP:
        return "hover_tooltip";
    case UIELEM_BUILTIN_MINIMENU:
        return "minimenu";
    case UIELEM_BUILTIN_CROSSHAIR:
        return "crosshair";
    case UIELEM_BUILTIN_CHAT_MESSAGES:
        return "chat_messages";
    case UIELEM_RS_TEXT:
        return "rs_text";
    case UIELEM_RS_GRAPHIC:
        return "rs_graphic";
    case UIELEM_RS_MODEL:
        return "rs_model";
    case UIELEM_RS_INV:
        return "rs_inv";
    case UIELEM_RS_INV_TEXT:
        return "rs_inv_text";
    case UIELEM_RS_LAYER:
        return "rs_layer";
    case UIELEM_RS_RECT:
        return "rs_rect";
    case UIELEM_BUILTIN_CHAT_INPUT:
        return "chat_input";
    case UIELEM_BUILTIN_CHAT_PRIVACY:
        return "chat_privacy";
    case UIELEM_BUILTIN_COLLISIONMAP_OVERLAY:
        return "collisionmap_overlay";
    case UIELEM_BUILTIN_CHAT_DIALOG:
        return "chat_dialog";
    case UIELEM_BUILTIN_SIDEBAR_OVERLAY:
        return "sidebar_overlay";
    case UIELEM_BUILTIN_VIEWPORT_OVERLAY:
        return "viewport_overlay";
    }
    return "unknown";
}

struct UITree*
uitree_new(uint32_t hint)
{
    (void)hint;
    struct UITree* tree = malloc(sizeof(struct UITree));
    if( !tree )
        return NULL;
    memset(tree, 0, sizeof(struct UITree));
    tree->root_index = -1;
    tree->ui_layer_stack_top = -1;
    tree->ui_scrollbar_drag_component_id = -1;
    tree->ui_scrollbar0_element_id = -1;
    tree->ui_scrollbar1_element_id = -1;
    tree->ui_minimap_mapdots0_element_id = -1;
    tree->ui_minimap_mapdots1_element_id = -1;
    tree->ui_minimap_mapdots3_element_id = -1;
    tree->ui_minimap_mapdots4_element_id = -1;
    tree->ui_minimap_mapmarker2_element_id = -1;
    tree->ui_minimap_mapmarker_element_id = -1;
    tree->hover_inv_component_id = -1;
    tree->hover_inv_slot = -1;
    return tree;
}

void
uitree_free(struct UITree* tree)
{
    if( !tree )
        return;
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct StaticUIComponent* c = &tree->components[i];
        if( c->type == UIELEM_RS_TEXT )
        {
            if( c->u.rs_text.text )
                free((void*)c->u.rs_text.text);
            if( c->u.rs_text.active_text )
                free((void*)c->u.rs_text.active_text);
        }
        /* Agnostic fields. */
        if( c->scripts )
        {
            for( int s = 0; s < c->scripts_count; s++ )
                free(c->scripts[s].code);
            free(c->scripts);
        }
        free(c->script_comparator);
        free(c->script_operand);
        for( int k = 0; k < 5; k++ )
            free(c->iop[k]);
        free(c->option);
        free(c->target_verb);
        free(c->target_text);
    }
    uitree_textpool_free(&tree->text_pool);
    free(tree->components);
    free(tree);
}

void
uitree_mark_all_dirty(struct UITree* tree)
{
    if( !tree )
        return;
    for( uint32_t i = 0; i < tree->component_count; i++ )
        tree->components[i].is_dirty = 1;
}

int
uitree_validate_sidebar_tab_layout(struct UITree const* tree)
{
    if( !tree )
        return -1;
    int counts[14];
    for( int t = 0; t < 14; t++ )
        counts[t] = 0;
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct StaticUIComponent const* c = &tree->components[i];
        if( c->type != UIELEM_BUILTIN_SIDEBAR )
            continue;
        int tn = c->u.sidebar.tabno;
        if( tn < 0 || tn > 13 )
        {
            fprintf(
                stderr,
                "uitree_validate_sidebar_tab_layout: sidebar uitree[%u] has invalid tabno=%d\n",
                (unsigned)i,
                tn);
            return -1;
        }
        counts[tn]++;
    }
    int ok = 0;
    for( int t = 0; t < 14; t++ )
    {
        if( counts[t] > 1 )
        {
            fprintf(
                stderr,
                "uitree_validate_sidebar_tab_layout: duplicate tabno %d (%d sidebars)\n",
                t,
                counts[t]);
            ok = -1;
        }
    }
    return ok;
}

void
uitree_print_nodes(struct UITree const* tree)
{
    return;
    if( !tree )
    {
        printf("uitree_print_nodes: tree is NULL\n");
        return;
    }
    printf("uitree: %u nodes, root_index=%d\n", tree->component_count, (int)tree->root_index);
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct StaticUIComponent const* c = &tree->components[i];
        printf(
            "  [%u] type=%s parent=%d first_child=%d next_sibling=%d component_id=%d "
            "pos kind=%d xy=(%d,%d) wh=(%d,%d)\n",
            i,
            uitree_component_type_str(c->type),
            (int)c->parent,
            (int)c->first_child,
            (int)c->next_sibling,
            (int)c->component_id,
            (int)c->position.kind,
            c->position.x,
            c->position.y,
            c->position.width,
            c->position.height);
        switch( c->type )
        {
        case UIELEM_RS_INV:
        case UIELEM_RS_INV_TEXT:
            printf(
                "       rs_inv inv_index=%d cols=%d rows=%d margin=(%d,%d)\n",
                c->u.rs_inv.inv_index,
                c->u.rs_inv.cols,
                c->u.rs_inv.rows,
                c->u.rs_inv.margin_x,
                c->u.rs_inv.margin_y);
            if( c->type == UIELEM_RS_INV_TEXT )
            {
                printf(
                    "       rs_inv_text font_id=%d color=%d center=%d shadow=%d\n",
                    c->inv_text_font_id,
                    c->inv_text_color,
                    c->inv_text_center,
                    c->inv_text_shadowed);
            }
            for( int si = 0; si < UI_INV_SLOT_OFFSET_MAX; si++ )
            {
                if( c->u.rs_inv.inv_slot_bg_scene_id[si] >= 0 )
                {
                    printf(
                        "       rs_inv slot_bg[%d] scene_id=%d atlas=%d\n",
                        si,
                        c->u.rs_inv.inv_slot_bg_scene_id[si],
                        c->u.rs_inv.inv_slot_bg_atlas_index[si]);
                }
            }
            break;
        case UIELEM_RS_GRAPHIC:
            printf(
                "       rs_graphic scene_id=%d atlas=%d active_scene=%d active_atlas=%d\n",
                c->u.rs_graphic.scene_id,
                c->u.rs_graphic.atlas_index,
                c->u.rs_graphic.scene_id_active,
                c->u.rs_graphic.atlas_index_active);
            break;
        case UIELEM_RS_TEXT:
            printf(
                "       rs_text font_id=%d font_idx=%d colors=%d/%d/%d/%d center=%d text=%s "
                "active=%s\n",
                c->u.rs_text.font_id,
                c->u.rs_text.font_idx,
                c->u.rs_text.color,
                c->u.rs_text.active_color,
                c->u.rs_text.over_color,
                c->u.rs_text.active_over_color,
                c->u.rs_text.center,
                c->u.rs_text.text ? c->u.rs_text.text : "(null)",
                c->u.rs_text.active_text ? c->u.rs_text.active_text : "(null)");
            break;
        case UIELEM_RS_LAYER:
            printf(
                "       rs_layer scroll_height=%d hide=%d\n",
                c->u.rs_layer.scroll_height,
                c->u.rs_layer.hide);
            break;
        case UIELEM_RS_RECT:
            printf(
                "       rs_rect colors=%d/%d/%d/%d alpha=%d fill=%u\n",
                c->u.rs_rect.color,
                c->u.rs_rect.active_color,
                c->u.rs_rect.over_color,
                c->u.rs_rect.active_over_color,
                c->u.rs_rect.alpha,
                (unsigned)c->u.rs_rect.fill);
            break;
        case UIELEM_RS_MODEL:
            printf("       rs_model scene_id=%d\n", c->u.rs_model.scene_id);
            break;
        case UIELEM_BUILTIN_SPRITE:
            printf(
                "       sprite scene_id=%d atlas_index=%d\n",
                c->u.sprite.scene_id,
                c->u.sprite.atlas_index);
            break;
        case UIELEM_BUILTIN_SIDEBAR:
            printf(
                "       sidebar tabno=%d componentno=%d inv_index=%d\n",
                c->u.sidebar.tabno,
                c->u.sidebar.componentno,
                c->u.sidebar.inv_index);
            break;
        default:
            break;
        }
    }
}

static void
uitree_debug_log_subtree_rec(
    struct UITree const* tree,
    int32_t idx,
    int depth)
{
    if( !tree || idx < 0 || (uint32_t)idx >= tree->component_count )
        return;
    struct StaticUIComponent const* c = &tree->components[idx];
    fprintf(stderr, "[uitree_subtree] ");
    for( int i = 0; i < depth * 2; i++ )
        fputc(' ', stderr);
    fprintf(
        stderr,
        "[%d] type=%s parent=%d first_child=%d next_sibling=%d component_id=%d "
        "pos kind=%d xy=(%d,%d) wh=(%d,%d)\n",
        (int)idx,
        uitree_component_type_str(c->type),
        (int)c->parent,
        (int)c->first_child,
        (int)c->next_sibling,
        (int)c->component_id,
        (int)c->position.kind,
        c->position.x,
        c->position.y,
        c->position.width,
        c->position.height);
    for( int32_t ch = c->first_child; ch >= 0; ch = tree->components[ch].next_sibling )
        uitree_debug_log_subtree_rec(tree, ch, depth + 1);
}

void
uitree_debug_log_subtree_for_component_id(
    struct UITree const* tree,
    int component_id)
{
    // if( !tree || component_id < 0 )
    //     return;
    // int32_t root = uitree_find_by_component_id(tree, component_id);
    // if( root < 0 )
    // {
    //     fprintf(
    //         stderr,
    //         "[uitree_subtree] component_id=%d not found in UITree (%u nodes)\n",
    //         component_id,
    //         tree->component_count);
    //     return;
    // }
    // fprintf(
    //     stderr,
    //     "[uitree_subtree] subtree for component_id=%d root_index=%d\n",
    //     component_id,
    //     (int)root);
    // uitree_debug_log_subtree_rec(tree, root, 0);
}

int32_t
uitree_push_sprite_xy(
    struct UITree* tree,
    int32_t parent_index,
    int sprite_id,
    int atlas_index,
    int x,
    int y,
    int width,
    int height)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_BUILTIN_SPRITE;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.sprite.scene_id = sprite_id;
    component->u.sprite.atlas_index = atlas_index;
    return idx;
}

int32_t
uitree_push_sprite_relative(
    struct UITree* tree,
    int32_t parent_index,
    int sprite_id,
    int atlas_index,
    int flags,
    int top,
    int right,
    int bottom,
    int left,
    int width,
    int height)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_BUILTIN_SPRITE;
    component->position.kind = UIPOS_RELATIVE;
    component->position.relative_flags = flags;
    if( (flags & STATIC_UI_RELATIVE_FLAG_LEFT) != 0 )
        component->position.left = left;
    if( (flags & STATIC_UI_RELATIVE_FLAG_TOP) != 0 )
        component->position.top = top;
    if( (flags & STATIC_UI_RELATIVE_FLAG_RIGHT) != 0 )
        component->position.right = right;
    if( (flags & STATIC_UI_RELATIVE_FLAG_BOTTOM) != 0 )
        component->position.bottom = bottom;
    component->position.width = width;
    component->position.height = height;
    component->u.sprite.scene_id = sprite_id;
    component->u.sprite.atlas_index = atlas_index;
    return idx;
}

int32_t
uitree_push_world(
    struct UITree* tree,
    int32_t parent_index,
    int x,
    int y,
    int width,
    int height,
    uint8_t level_mask)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_BUILTIN_WORLD;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.world.level_mask = level_mask;
    return idx;
}

int32_t
uitree_push_compass(
    struct UITree* tree,
    int32_t parent_index,
    int sprite_id,
    int atlas_index,
    int x,
    int y,
    int width,
    int height,
    int anchor_x,
    int anchor_y)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_BUILTIN_COMPASS;
    component->position.kind = UIPOS_XY;
    component->u.sprite.scene_id = sprite_id;
    component->u.sprite.atlas_index = atlas_index;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->position.anchor_x = anchor_x;
    component->position.anchor_y = anchor_y;
    return idx;
}

int32_t
uitree_push_minimap(
    struct UITree* tree,
    int32_t parent_index,
    int x,
    int y,
    int width,
    int height,
    int anchor_x,
    int anchor_y)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_BUILTIN_MINIMAP;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->position.anchor_x = anchor_x;
    component->position.anchor_y = anchor_y;
    return idx;
}

int32_t
uitree_push_hover_tooltip(
    struct UITree* tree,
    int32_t parent_index,
    int x,
    int y,
    int width,
    int height)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_BUILTIN_HOVER_TOOLTIP;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.hover_tooltip.font_id = -1;
    return idx;
}

int32_t
uitree_push_minimenu(
    struct UITree* tree,
    int32_t parent_index)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_BUILTIN_MINIMENU;
    component->position.kind = UIPOS_XY;
    component->position.x = 0;
    component->position.y = 0;
    component->position.width = 0;
    component->position.height = 0;
    component->u.minimenu.font_id = -1;
    return idx;
}

int32_t
uitree_push_crosshair(
    struct UITree* tree,
    int32_t parent_index)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_BUILTIN_CROSSHAIR;
    component->position.kind = UIPOS_XY;
    component->position.x = 0;
    component->position.y = 0;
    component->position.width = 0;
    component->position.height = 0;
    component->u.crosshair.scene_id = -1;
    component->u.crosshair.hotspot_offset = 8;
    return idx;
}

int32_t
uitree_push_chat_messages(
    struct UITree* tree,
    int32_t parent_index,
    int x,
    int y,
    int width,
    int height)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_BUILTIN_CHAT_MESSAGES;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.chat_messages.font_id = -1;
    return idx;
}

int32_t
uitree_push_chat_input(
    struct UITree* tree,
    int32_t parent_index,
    int x,
    int y,
    int width,
    int height)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_BUILTIN_CHAT_INPUT;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.chat_input.font_id = -1;
    return idx;
}

int32_t
uitree_push_chat_privacy(
    struct UITree* tree,
    int32_t parent_index,
    int x,
    int y,
    int width,
    int height)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_BUILTIN_CHAT_PRIVACY;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.chat_privacy.font_id = -1;
    return idx;
}

int32_t
uitree_push_builtin_chat_dialog(
    struct UITree* tree,
    int32_t parent_index,
    int x,
    int y,
    int width,
    int height)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_BUILTIN_CHAT_DIALOG;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.chat_dialog.reserved = 0;
    return idx;
}

int32_t
uitree_push_builtin_sidebar_overlay(
    struct UITree* tree,
    int32_t parent_index,
    int x,
    int y,
    int width,
    int height)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_BUILTIN_SIDEBAR_OVERLAY;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.sidebar_overlay.reserved = 0;
    return idx;
}

int32_t
uitree_push_builtin_viewport_overlay(
    struct UITree* tree,
    int32_t parent_index,
    int x,
    int y,
    int width,
    int height)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_BUILTIN_VIEWPORT_OVERLAY;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.viewport_overlay.reserved = 0;
    return idx;
}

int32_t
uitree_push_collisionmap_overlay(
    struct UITree* tree,
    int32_t parent_index)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_BUILTIN_COLLISIONMAP_OVERLAY;
    component->position.kind = UIPOS_XY;
    component->position.x = 0;
    component->position.y = 0;
    component->position.width = 0;
    component->position.height = 0;
    return idx;
}

int32_t
uitree_push_redstone_tab(
    struct UITree* tree,
    int32_t parent_index,
    int tabno,
    int sprite_id,
    int atlas_index,
    int sprite_active_id,
    int atlas_active_index,
    int x,
    int y,
    int width,
    int height)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_BUILTIN_REDSTONE_TAB;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.redstone_tab.tabno = tabno;
    component->u.redstone_tab.scene_id = sprite_id;
    component->u.redstone_tab.atlas_index = atlas_index;
    component->u.redstone_tab.scene_id_active = sprite_active_id;
    component->u.redstone_tab.atlas_index_active = atlas_active_index;
    return idx;
}

int32_t
uitree_push_builtin_sidebar(
    struct UITree* tree,
    int32_t parent_index,
    int tabno,
    int componentno,
    int inv_index,
    int x,
    int y,
    int width,
    int height)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_BUILTIN_SIDEBAR;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.sidebar.tabno = tabno;
    component->u.sidebar.componentno = componentno;
    component->u.sidebar.inv_index = inv_index;
    return idx;
}

int32_t
uitree_push_sidebar_component(
    struct UITree* tree,
    int32_t parent_index,
    int tabno,
    int componentno,
    int inv_index,
    int x,
    int y,
    int width,
    int height)
{
    return uitree_push_builtin_sidebar(
        tree, parent_index, tabno, componentno, inv_index, x, y, width, height);
}

void
uitree_set_component_hidden(
    struct UITree* tree,
    int component_id,
    int hidden)
{
    if( !tree || component_id < 0 )
        return;
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        if( tree->components[i].component_id == component_id )
            tree->components[i].is_hidden = hidden ? 1u : 0u;
    }
}

int32_t
uitree_push_rs_layer(
    struct UITree* tree,
    int32_t parent_index,
    int component_id,
    int scroll_height,
    int hide,
    int x,
    int y,
    int width,
    int height)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_RS_LAYER;
    component->component_id = component_id;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.rs_layer.scroll_height = scroll_height;
    component->u.rs_layer.hide = hide;
    return idx;
}

int32_t
uitree_push_rs_rect(
    struct UITree* tree,
    int32_t parent_index,
    int component_id,
    int color,
    int active_color,
    int over_color,
    int active_over_color,
    int alpha,
    int fill,
    int x,
    int y,
    int width,
    int height)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];
    component->type = UIELEM_RS_RECT;
    component->component_id = component_id;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.rs_rect.color = color;
    component->u.rs_rect.active_color = active_color;
    component->u.rs_rect.over_color = over_color;
    component->u.rs_rect.active_over_color = active_over_color;
    component->u.rs_rect.alpha = alpha;
    component->u.rs_rect.fill = (uint8_t)(fill ? 1u : 0u);
    return idx;
}

int32_t
uitree_push_rs_text(
    struct UITree* tree,
    int32_t parent_index,
    int component_id,
    int font_idx,
    int color,
    int active_color,
    int over_color,
    int active_over_color,
    int center,
    int shadowed,
    char const* text,
    char const* active_text,
    int x,
    int y,
    int width,
    int height)
{
    char* text_owned = NULL;
    char* active_owned = NULL;
    if( text )
    {
        text_owned = strdup(text);
        if( !text_owned )
            return -1;
    }
    if( active_text )
    {
        active_owned = strdup(active_text);
        if( !active_owned )
        {
            free(text_owned);
            return -1;
        }
    }

    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
    {
        free(text_owned);
        free(active_owned);
        return -1;
    }
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_RS_TEXT;
    component->component_id = component_id;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.rs_text.font_id = -1;
    component->u.rs_text.font_idx = font_idx;
    component->u.rs_text.color = color;
    component->u.rs_text.active_color = active_color;
    component->u.rs_text.over_color = over_color;
    component->u.rs_text.active_over_color = active_over_color;
    component->u.rs_text.center = center;
    component->u.rs_text.shadowed = shadowed;
    component->u.rs_text.text = text_owned;
    component->u.rs_text.active_text = active_owned;
    return idx;
}

int32_t
uitree_push_rs_graphic(
    struct UITree* tree,
    int32_t parent_index,
    int component_id,
    int scene_id,
    int atlas_index,
    int scene_id_active,
    int atlas_index_active,
    int x,
    int y,
    int width,
    int height)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_RS_GRAPHIC;
    component->component_id = component_id;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.rs_graphic.scene_id = scene_id;
    component->u.rs_graphic.atlas_index = atlas_index;
    component->u.rs_graphic.scene_id_active = scene_id_active;
    component->u.rs_graphic.atlas_index_active = atlas_index_active;
    return idx;
}

int32_t
uitree_push_rs_model(
    struct UITree* tree,
    int32_t parent_index,
    int component_id,
    int scene_id,
    int x,
    int y,
    int width,
    int height,
    int model_zoom,
    int model_xan,
    int model_yan)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_RS_MODEL;
    component->component_id = component_id;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.rs_model.scene_id = scene_id;
    component->u.rs_model.model_zoom = model_zoom;
    component->u.rs_model.model_xan = model_xan;
    component->u.rs_model.model_yan = model_yan;
    component->u.rs_model.rs_model_cached_sequence_id = -1;
    return idx;
}

int32_t
uitree_push_rs_inv(
    struct UITree* tree,
    int32_t parent_index,
    int component_id,
    int inv_index,
    int cols,
    int rows,
    int margin_x,
    int margin_y,
    int const* inv_slot_offset_x,
    int const* inv_slot_offset_y,
    int const* inv_slot_bg_scene_id,
    int const* inv_slot_bg_atlas_index,
    int x,
    int y,
    int width,
    int height)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_RS_INV;
    component->component_id = component_id;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.rs_inv.inv_index = inv_index;
    component->u.rs_inv.cols = cols;
    component->u.rs_inv.rows = rows;
    component->u.rs_inv.margin_x = margin_x;
    component->u.rs_inv.margin_y = margin_y;
    if( inv_slot_offset_x && inv_slot_offset_y )
    {
        memcpy(
            component->u.rs_inv.inv_slot_offset_x,
            inv_slot_offset_x,
            (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
        memcpy(
            component->u.rs_inv.inv_slot_offset_y,
            inv_slot_offset_y,
            (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
    }
    if( inv_slot_bg_scene_id && inv_slot_bg_atlas_index )
    {
        memcpy(
            component->u.rs_inv.inv_slot_bg_scene_id,
            inv_slot_bg_scene_id,
            (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
        memcpy(
            component->u.rs_inv.inv_slot_bg_atlas_index,
            inv_slot_bg_atlas_index,
            (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
    }
    else
    {
        for( int i = 0; i < UI_INV_SLOT_OFFSET_MAX; i++ )
        {
            component->u.rs_inv.inv_slot_bg_scene_id[i] = -1;
            component->u.rs_inv.inv_slot_bg_atlas_index[i] = 0;
        }
    }
    return idx;
}

int32_t
uitree_push_rs_inv_text(
    struct UITree* tree,
    int32_t parent_index,
    int component_id,
    int inv_index,
    int cols,
    int rows,
    int margin_x,
    int margin_y,
    int const* inv_slot_offset_x,
    int const* inv_slot_offset_y,
    int const* inv_slot_bg_scene_id,
    int const* inv_slot_bg_atlas_index,
    int x,
    int y,
    int width,
    int height)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_RS_INV_TEXT;
    component->component_id = component_id;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.rs_inv.inv_index = inv_index;
    component->u.rs_inv.cols = cols;
    component->u.rs_inv.rows = rows;
    component->u.rs_inv.margin_x = margin_x;
    component->u.rs_inv.margin_y = margin_y;
    if( inv_slot_offset_x && inv_slot_offset_y )
    {
        memcpy(
            component->u.rs_inv.inv_slot_offset_x,
            inv_slot_offset_x,
            (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
        memcpy(
            component->u.rs_inv.inv_slot_offset_y,
            inv_slot_offset_y,
            (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
    }
    if( inv_slot_bg_scene_id && inv_slot_bg_atlas_index )
    {
        memcpy(
            component->u.rs_inv.inv_slot_bg_scene_id,
            inv_slot_bg_scene_id,
            (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
        memcpy(
            component->u.rs_inv.inv_slot_bg_atlas_index,
            inv_slot_bg_atlas_index,
            (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
    }
    else
    {
        for( int i = 0; i < UI_INV_SLOT_OFFSET_MAX; i++ )
        {
            component->u.rs_inv.inv_slot_bg_scene_id[i] = -1;
            component->u.rs_inv.inv_slot_bg_atlas_index[i] = 0;
        }
    }
    component->inv_text_font_id = -1;
    component->inv_text_color = 0;
    component->inv_text_center = 0;
    component->inv_text_shadowed = 0;
    component->inv_text_peer_inv_component_id = -1;
    return idx;
}

/** Orphaned RS nodes stay in the dense array; hide the whole subtree so any stray visit skips draw.
 */
static void
uitree_subtree_set_hidden_r(
    struct UITree* tree,
    int32_t idx,
    unsigned hidden)
{
    if( !tree || idx < 0 || (uint32_t)idx >= tree->component_count )
        return;
    struct StaticUIComponent* c = &tree->components[idx];
    c->is_hidden = hidden ? 1u : 0u;
    for( int32_t ch = c->first_child; ch >= 0; ch = tree->components[ch].next_sibling )
        uitree_subtree_set_hidden_r(tree, ch, hidden);
}

void
uitree_clear_sidebar_children(
    struct UITree* tree,
    int32_t sidebar_idx)
{
    if( !tree || sidebar_idx < 0 || (uint32_t)sidebar_idx >= tree->component_count )
        return;
    struct StaticUIComponent* c = &tree->components[sidebar_idx];
    if( c->type != UIELEM_BUILTIN_SIDEBAR )
        return;
    if( c->first_child >= 0 )
        uitree_subtree_set_hidden_r(tree, c->first_child, 1u);
    c->first_child = -1;
    c->is_dirty = 1;
    tree->generation++;
}

void
uitree_clear_chat_dialog_children(
    struct UITree* tree,
    int32_t chat_dialog_idx)
{
    if( !tree || chat_dialog_idx < 0 || (uint32_t)chat_dialog_idx >= tree->component_count )
        return;
    struct StaticUIComponent* c = &tree->components[chat_dialog_idx];
    if( c->type != UIELEM_BUILTIN_CHAT_DIALOG )
        return;
    if( c->first_child >= 0 )
        uitree_subtree_set_hidden_r(tree, c->first_child, 1u);
    c->first_child = -1;
    c->is_dirty = 1;
    tree->generation++;
}

void
uitree_clear_sidebar_overlay_children(
    struct UITree* tree,
    int32_t sidebar_overlay_idx)
{
    if( !tree || sidebar_overlay_idx < 0 || (uint32_t)sidebar_overlay_idx >= tree->component_count )
        return;
    struct StaticUIComponent* c = &tree->components[sidebar_overlay_idx];
    if( c->type != UIELEM_BUILTIN_SIDEBAR_OVERLAY )
        return;
    if( c->first_child >= 0 )
        uitree_subtree_set_hidden_r(tree, c->first_child, 1u);
    c->first_child = -1;
    c->is_dirty = 1;
    tree->generation++;
}

void
uitree_clear_viewport_overlay_children(
    struct UITree* tree,
    int32_t viewport_overlay_idx)
{
    if( !tree || viewport_overlay_idx < 0 ||
        (uint32_t)viewport_overlay_idx >= tree->component_count )
        return;
    struct StaticUIComponent* c = &tree->components[viewport_overlay_idx];
    if( c->type != UIELEM_BUILTIN_VIEWPORT_OVERLAY )
        return;
    if( c->first_child >= 0 )
        uitree_subtree_set_hidden_r(tree, c->first_child, 1u);
    c->first_child = -1;
    c->is_dirty = 1;
    tree->generation++;
}

#define UITREE_PICK_CHILD_MAX 512

typedef struct UIPickFrame
{
    int scroll_y_total;
    int clip_x;
    int clip_y;
    int clip_w;
    int clip_h;
} UIPickFrame;

static bool
uitree_sidebar_tab_active_pick(
    struct GGame* game,
    struct StaticUIComponent* sidebar)
{
    if( !sidebar || sidebar->type != UIELEM_BUILTIN_SIDEBAR )
        return false;
    if( !game || !game->iface )
        return false;
    /* Tab sidebar hidden while IF_OPENSIDE owns the sidebar rect; picking uses
     * interface_sidebar_overlay_try_fill_uitree_options on build-cache instead of UITree RS_INV. */
    if( game->iface->sidebar_interface_id != -1 )
        return false;
    return game->iface->selected_tab == sidebar->u.sidebar.tabno;
}

static void
uitree_push_pick_layer(
    struct UIPickFrame* stack,
    int* stack_top,
    struct GGame* game,
    struct StaticUIComponent* layer)
{
    int lh = layer->position.height;
    int sh = layer->u.rs_layer.scroll_height;
    int sp = iface_scroll_clamped(game, layer->component_id, sh, lh);
    int prev = *stack_top >= 0 ? stack[*stack_top].scroll_y_total : 0;
    (*stack_top)++;
    stack[*stack_top].scroll_y_total = prev + sp;
    stack[*stack_top].clip_x = layer->position.x;
    stack[*stack_top].clip_y = layer->position.y;
    stack[*stack_top].clip_w = layer->position.width;
    stack[*stack_top].clip_h = lh;
}

static bool
uitree_pick_should_descend_layer(
    struct GGame* game,
    struct StaticUIComponent* c)
{
    if( c->type != UIELEM_RS_LAYER )
        return false;
    if( c->is_hidden )
        return false;
    if( c->u.rs_layer.hide && !interface_component_is_overlay_hovered(game, c->component_id) )
        return false;
    if( c->first_child < 0 )
        return false;
    return true;
}

static bool
uitree_point_in_all_clips(
    int mx,
    int my,
    struct UIPickFrame const* stack,
    int stack_top)
{
    for( int i = 0; i <= stack_top; i++ )
    {
        struct UIPickFrame const* e = &stack[i];
        if( mx < e->clip_x || mx >= e->clip_x + e->clip_w || my < e->clip_y ||
            my >= e->clip_y + e->clip_h )
            return false;
    }
    return true;
}

static bool
uitree_point_in_component_scr(
    struct StaticUIComponent const* c,
    int mx,
    int my,
    int scroll_off)
{
    if( c->position.kind != UIPOS_XY )
        return false;
    int x = c->position.x;
    int y = c->position.y - scroll_off;
    int w = c->position.width;
    int h = c->position.height;
    if( w <= 0 || h <= 0 )
        return false;
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

static bool
uitree_is_clickable_button_ui(struct StaticUIComponent const* c)
{
    if( c->client_code > 0 )
        return true;
    if( c->button_kind == UI_BUTTON_OK || c->button_kind == UI_BUTTON_TOGGLE ||
        c->button_kind == UI_BUTTON_SELECT || c->button_kind == UI_BUTTON_CLOSE ||
        c->button_kind == UI_BUTTON_CONTINUE )
        return true;
    return false;
}

static bool
uitree_component_has_menu_candidate(struct StaticUIComponent const* c)
{
    if( c->target_verb && c->target_verb[0] )
        return true;
    if( c->option && c->option[0] )
        return true;
    for( int i = 0; i < 5; i++ )
    {
        if( c->iop[i] && c->iop[i][0] )
            return true;
    }
    if( c->type == UIELEM_RS_TEXT && c->u.rs_text.text && c->u.rs_text.text[0] )
        return true;
    if( uitree_is_clickable_button_ui(c) )
        return true;
    return false;
}

static bool
uitree_inv_slot_contains_mouse(
    struct StaticUIComponent* inv,
    int mx,
    int my,
    int scroll_off,
    int* out_slot)
{
    int cols = inv->u.rs_inv.cols > 0 ? inv->u.rs_inv.cols : 4;
    int rows = inv->u.rs_inv.rows;
    int margin_x = inv->u.rs_inv.margin_x;
    int margin_y = inv->u.rs_inv.margin_y;
    int base_x = inv->position.x;
    int base_y = inv->position.y - scroll_off;
    int i = 0;
    for( int row = 0; row < rows; row++ )
    {
        for( int col = 0; col < cols; col++, i++ )
        {
            int slot_x = base_x + col * (margin_x + 32);
            int slot_y = base_y + row * (margin_y + 32);
            if( i < UI_INV_SLOT_OFFSET_MAX )
            {
                slot_x += inv->u.rs_inv.inv_slot_offset_x[i];
                slot_y += inv->u.rs_inv.inv_slot_offset_y[i];
            }
            if( mx >= slot_x && mx < slot_x + 32 && my >= slot_y && my < slot_y + 32 )
            {
                *out_slot = i;
                return true;
            }
        }
    }
    return false;
}

static bool
uitree_inv_text_slot_contains_mouse(
    struct StaticUIComponent* inv,
    int mx,
    int my,
    int scroll_off,
    int* out_slot)
{
    int cols = inv->u.rs_inv.cols > 0 ? inv->u.rs_inv.cols : 4;
    int rows = inv->u.rs_inv.rows;
    int margin_x = inv->u.rs_inv.margin_x;
    int margin_y = inv->u.rs_inv.margin_y;
    int base_x = inv->position.x;
    int base_y = inv->position.y - scroll_off;
    int const cell_w = 115;
    int const cell_h = 12;
    int i = 0;
    for( int row = 0; row < rows; row++ )
    {
        for( int col = 0; col < cols; col++, i++ )
        {
            int slot_x = base_x + col * (margin_x + cell_w);
            int slot_y = base_y + row * (margin_y + cell_h);
            if( i < UI_INV_SLOT_OFFSET_MAX )
            {
                slot_x += inv->u.rs_inv.inv_slot_offset_x[i];
                slot_y += inv->u.rs_inv.inv_slot_offset_y[i];
            }
            if( mx >= slot_x && mx < slot_x + cell_w && my >= slot_y && my < slot_y + cell_h )
            {
                *out_slot = i;
                return true;
            }
        }
    }
    return false;
}

/** 0-based object def id: inv_pool first, then cache invSlotObjId (Client.ts linkObjType). */
static int
uitree_inv_slot_obj_def_id(
    struct GGame* game,
    struct StaticUIComponent* inv,
    int slot)
{
    if( slot < 0 || slot >= UI_INVENTORY_MAX_ITEMS )
        return 0;
    int inv_i = inv->u.rs_inv.inv_index;
    if( game && game->inv_pool && inv_i >= 0 && inv_i < game->inv_pool->count )
    {
        int id = game->inv_pool->inventories[inv_i].items[slot].obj_id;
        /* Pool uses 1-based wire ids (UIInventoryItem); match cache branch and Client.ts
         * linkObjType-1. */
        if( id > 0 )
            return id - 1;
    }
    int comp_id = inv->component_id >= 0 ? inv->component_id : 0;
    if( inv->type == UIELEM_RS_INV_TEXT && inv->inv_text_peer_inv_component_id >= 0 )
        comp_id = inv->inv_text_peer_inv_component_id;
    if( game && game->gamecache && comp_id >= 0 )
    {
        struct GameCacheComponent* cc = gamecache_get_component(game->gamecache, comp_id);
        if( cc && (cc->type == COMPONENT_TYPE_INV || cc->type == COMPONENT_TYPE_INV_TEXT) &&
            cc->invSlotObjId )
        {
            int nslots = cc->width * cc->height;
            if( slot < nslots && cc->invSlotObjId[slot] > 0 )
                return cc->invSlotObjId[slot] - 1;
        }
    }
    return 0;
}

static bool
uitree_inv_slot_has_menu(
    struct GGame* game,
    struct StaticUIComponent* inv,
    int slot)
{
    if( slot < 0 )
        return false;
    if( uitree_inv_slot_obj_def_id(game, inv, slot) > 0 )
        return true;
    for( int i = 0; i < 5; i++ )
    {
        if( inv->iop[i] && inv->iop[i][0] )
            return true;
    }
    return false;
}

static int
uitree_inv_scroll_offset_from_root(
    struct GGame* game,
    struct UITree* t,
    int inv_idx)
{
    int layers[32];
    int nl = 0;
    int idx = t->components[inv_idx].parent;
    while( idx >= 0 && nl < 32 )
    {
        if( t->components[idx].type == UIELEM_RS_LAYER )
            layers[nl++] = idx;
        idx = t->components[idx].parent;
    }
    int total = 0;
    for( int j = nl - 1; j >= 0; j-- )
    {
        struct StaticUIComponent* L = &t->components[layers[j]];
        int lh = L->position.height;
        int sh = L->u.rs_layer.scroll_height;
        total += iface_scroll_clamped(game, L->component_id, sh, lh);
    }
    return total;
}

/** Primary pick can return BUILTIN_WORLD when its rect covers UI; prefer deepest RS_INV under
 * cursor. */
static bool
uitree_fallback_pick_rs_inv(
    struct GGame* game,
    struct UITree* t,
    int mx,
    int my,
    int* out_hit,
    int* out_slot)
{
    int best_depth = -1;
    int best_i = -1;
    int best_slot = -1;
    for( uint32_t i = 0; i < t->component_count; i++ )
    {
        struct StaticUIComponent* c = &t->components[i];
        if( (c->type != UIELEM_RS_INV && c->type != UIELEM_RS_INV_TEXT) || c->is_hidden )
            continue;
        if( c->position.kind != UIPOS_XY )
            continue;
        int scroll_off = uitree_inv_scroll_offset_from_root(game, t, (int)i);
        if( !uitree_point_in_component_scr(c, mx, my, scroll_off) )
            continue;
        int slot = -1;
        bool in_slot = false;
        if( c->type == UIELEM_RS_INV_TEXT )
            in_slot = uitree_inv_text_slot_contains_mouse(c, mx, my, scroll_off, &slot);
        else
            in_slot = uitree_inv_slot_contains_mouse(c, mx, my, scroll_off, &slot);
        if( !in_slot )
            continue;
        if( !uitree_inv_slot_has_menu(game, c, slot) )
            continue;
        int d = uitree_node_depth(t, (int)i);
        if( d > best_depth )
        {
            best_depth = d;
            best_i = (int)i;
            best_slot = slot;
        }
    }
    if( best_i >= 0 )
    {
        *out_hit = best_i;
        *out_slot = best_slot;
        return true;
    }
    return false;
}

static bool
uitree_node_skipped_for_pick(enum StaticUIComponentType ty)
{
    switch( ty )
    {
    case UIELEM_BUILTIN_HOVER_TOOLTIP:
    case UIELEM_BUILTIN_MINIMENU:
    case UIELEM_BUILTIN_CROSSHAIR:
        return true;
    default:
        return false;
    }
}

static bool
uitree_opt_append(
    struct UITreeOptionSet* os,
    enum MinimenuAction action,
    char const* text,
    int pa,
    int pb,
    int pc)
{
    if( !os || os->option_count >= UITREE_OPTION_SET_CAPACITY )
        return false;
    struct UITreeOption* o = &os->options[os->option_count];
    memset(o, 0, sizeof(*o));
    o->action = action;
    if( text )
    {
        strncpy(o->text, text, sizeof(o->text) - 1);
        o->text[sizeof(o->text) - 1] = '\0';
    }
    o->param_a = pa;
    o->param_b = pb;
    o->param_c = pc;
    os->option_count++;
    return true;
}

static struct GameCacheComponent*
uitree_cache_comp(
    struct GGame* game,
    int component_id)
{
    if( !game || !game->gamecache || component_id < 0 )
        return NULL;
    return gamecache_get_component(game->gamecache, component_id);
}

static void
uitree_option_set_sort_ts(struct UITreeOptionSet* os)
{
    int n = os->option_count;
    if( n <= 0 )
        return;
    for( int i = 0; i < n; i++ )
        os->order[i] = i;
    for( ;; )
    {
        bool sorted = true;
        for( int i = 0; i < n - 1; i++ )
        {
            int idx0 = os->order[i];
            int idx1 = os->order[i + 1];
            if( os->options[idx0].action < 1000 && os->options[idx1].action > 1000 )
            {
                int t = os->order[i];
                os->order[i] = os->order[i + 1];
                os->order[i + 1] = t;
                sorted = false;
            }
        }
        if( sorted )
            break;
    }
}

/** Shared inventory minimenu lines for RS_INV (UITree) and IF_OPENSIDE cache subtree (Client.ts
 * addComponentOptions type-2). useMode/targetMode: see uitree_fill_inv_slot_options.
 * Menu params match Client.ts menuParamA/B/C: obj_def_id, slot, component_id. */
static void
uitree_fill_inv_slot_common(
    struct GGame* game,
    int comp_id,
    int slot,
    int obj_def_id,
    struct GameCacheObj const* obj,
    uint8_t interactable,
    uint8_t usable,
    char const* comp_iop[5],
    struct UITreeOptionSet* os)
{
    char const* oname = (obj && obj->name) ? obj->name : "";
    char line[96];
    char tbuf[72];
    char opi[128];
    struct GameCacheComponent* inv_cd = uitree_cache_comp(game, comp_id);

    if( game && game->iface && interactable )
    {
        struct InterfaceState* iface = game->iface;
        if( iface->inv_use_mode )
        {
            if( comp_id != iface->inv_sel_comp_id || slot != iface->inv_sel_slot )
            {
                if( obj && obj->name && obj->name[0] )
                {
                    char uline[128];
                    snprintf(
                        uline,
                        sizeof(uline),
                        "Use %s with @lre@%s",
                        iface->inv_sel_obj_name[0] ? iface->inv_sel_obj_name : "item",
                        obj->name);
                    uitree_opt_append(
                        os, MINIMENU_ACTION_OPHELDU, uline, obj_def_id, slot, comp_id);
                    uitree_option_set_sort_ts(os);
                }
            }
            return;
        }
        if( iface->inv_target_mode )
        {
            if( (iface->inv_target_mask & 0x10) == 16 && obj && obj->name && obj->name[0] )
            {
                char tline[160];
                snprintf(
                    tline,
                    sizeof(tline),
                    "%s @lre@%s",
                    iface->inv_target_op[0] ? iface->inv_target_op : "",
                    obj->name);
                uitree_opt_append(os, MINIMENU_ACTION_TGT_HELD, tline, obj_def_id, slot, comp_id);
                uitree_option_set_sort_ts(os);
            }
            return;
        }
    }

    if( obj )
    {
        if( interactable )
        {
            for( int op = 4; op >= 3; op-- )
            {
                if( obj->iop[op] )
                {
                    snprintf(line, sizeof(line), "%s @lre@%s", obj->iop[op], oname);
                    enum MinimenuAction act =
                        (op == 4) ? MINIMENU_ACTION_OPHELD5 : MINIMENU_ACTION_OPHELD4;
                    uitree_opt_append(os, act, line, obj_def_id, slot, comp_id);
                }
                else if( op == 4 )
                {
                    snprintf(tbuf, sizeof(tbuf), "Drop @lre@%s", oname);
                    uitree_opt_append(os, MINIMENU_ACTION_OPHELD5, tbuf, obj_def_id, slot, comp_id);
                }
            }
        }

        if( usable )
        {
            snprintf(line, sizeof(line), "Use @lre@%s", oname);
            uitree_opt_append(os, MINIMENU_ACTION_OPHELDT_START, line, obj_def_id, slot, comp_id);
        }

        if( interactable )
        {
            for( int op = 2; op >= 0; op-- )
            {
                if( obj->iop[op] )
                {
                    snprintf(line, sizeof(line), "%s @lre@%s", obj->iop[op], oname);
                    enum MinimenuAction act = MINIMENU_ACTION_OPHELD1;
                    if( op == 1 )
                        act = MINIMENU_ACTION_OPHELD2;
                    else if( op == 2 )
                        act = MINIMENU_ACTION_OPHELD3;
                    uitree_opt_append(os, act, line, obj_def_id, slot, comp_id);
                }
            }
        }

        for( int op = 4; op >= 0; op-- )
        {
            if( !comp_iop[op] || !comp_iop[op][0] )
                continue;
            interface_expand_if_text_placeholders(
                game, inv_cd, comp_iop[op], opi, (int)sizeof(opi));
            snprintf(line, sizeof(line), "%s @lre@%s", opi, oname);
            enum MinimenuAction act = MINIMENU_ACTION_INV_BUTTON1;
            if( op == 1 )
                act = MINIMENU_ACTION_INV_BUTTON2;
            else if( op == 2 )
                act = MINIMENU_ACTION_INV_BUTTON3;
            else if( op == 3 )
                act = MINIMENU_ACTION_INV_BUTTON4;
            else if( op == 4 )
                act = MINIMENU_ACTION_INV_BUTTON5;
            uitree_opt_append(os, act, line, obj_def_id, slot, comp_id);
        }

        snprintf(line, sizeof(line), "Examine @lre@%s", oname);
        uitree_opt_append(os, MINIMENU_ACTION_OPHELD6, line, obj_def_id, slot, comp_id);
    }
    else
    {
        for( int op = 4; op >= 0; op-- )
        {
            if( !comp_iop[op] || !comp_iop[op][0] )
                continue;
            enum MinimenuAction act = MINIMENU_ACTION_INV_BUTTON1;
            if( op == 1 )
                act = MINIMENU_ACTION_INV_BUTTON2;
            else if( op == 2 )
                act = MINIMENU_ACTION_INV_BUTTON3;
            else if( op == 3 )
                act = MINIMENU_ACTION_INV_BUTTON4;
            else if( op == 4 )
                act = MINIMENU_ACTION_INV_BUTTON5;
            interface_expand_if_text_placeholders(
                game, inv_cd, comp_iop[op], opi, (int)sizeof(opi));
            uitree_opt_append(os, act, opi, 0, slot, comp_id);
        }
    }

    if( os->option_count > 0 )
        uitree_option_set_sort_ts(os);
}

static void
uitree_fill_simple_component_options(
    struct GGame* game,
    struct StaticUIComponent* c,
    struct UITreeOptionSet* os)
{
    int comp = c->component_id >= 0 ? c->component_id : 0;
    struct GameCacheComponent* cd =
        c->component_id >= 0 ? uitree_cache_comp(game, c->component_id) : NULL;
    char exp[192];

    if( c->button_kind == UI_BUTTON_TARGET && game->iface && game->iface->inv_target_mode == 0 )
    {
        char line[96];
        char prefix[72];
        prefix[0] = '\0';
        if( c->target_verb && c->target_verb[0] )
        {
            char const* sp = strchr(c->target_verb, ' ');
            if( sp && sp != c->target_verb )
            {
                size_t n = (size_t)(sp - c->target_verb);
                if( n > sizeof(prefix) - 1 )
                    n = sizeof(prefix) - 1;
                memcpy(prefix, c->target_verb, n);
                prefix[n] = '\0';
            }
            else
                strncpy(prefix, c->target_verb, sizeof(prefix) - 1);
        }
        snprintf(
            line,
            sizeof(line),
            "%s @gre@%s",
            prefix,
            (c->target_text && c->target_text[0]) ? c->target_text : "");
        interface_expand_if_text_placeholders(game, cd, line, exp, (int)sizeof(exp));
        uitree_opt_append(os, MINIMENU_ACTION_OPHELDT_SELECT, exp, 0, 0, comp);
        if( os->option_count > 0 )
            uitree_option_set_sort_ts(os);
        return;
    }

    /* CLOSE / CONTINUE before target_verb / option / RS_TEXT fallback: pause-dialog continue
     * must send RESUME_PAUSEBUTTON (Client.ts PAUSE_BUTTON), not IF_BUTTON on the label text. */
    if( c->button_kind == UI_BUTTON_CLOSE )
    {
        uitree_opt_append(os, MINIMENU_ACTION_CLOSE_MODAL, "Close", comp, 0, 0);
        if( os->option_count > 0 )
            uitree_option_set_sort_ts(os);
        return;
    }
    if( c->button_kind == UI_BUTTON_CONTINUE )
    {
        char const* disp = "Continue";
        if( c->option && c->option[0] )
        {
            interface_expand_if_text_placeholders(game, cd, c->option, exp, (int)sizeof(exp));
            disp = exp;
        }
        else if( c->type == UIELEM_RS_TEXT && c->u.rs_text.text && c->u.rs_text.text[0] )
        {
            interface_expand_if_text_placeholders(
                game, cd, c->u.rs_text.text, exp, (int)sizeof(exp));
            disp = exp;
        }
        uitree_opt_append(os, MINIMENU_ACTION_RESUME_PAUSEBUTTON, disp, comp, 0, 0);
        if( os->option_count > 0 )
            uitree_option_set_sort_ts(os);
        return;
    }

    if( c->target_verb && c->target_verb[0] )
    {
        char line[96];
        if( c->target_text && c->target_text[0] )
            snprintf(line, sizeof(line), "%s %s", c->target_verb, c->target_text);
        else
            snprintf(line, sizeof(line), "%s", c->target_verb);
        interface_expand_if_text_placeholders(game, cd, line, exp, (int)sizeof(exp));
        uitree_opt_append(os, (enum MinimenuAction)MINIMENU_ACTION_IF_BUTTON, exp, comp, 0, 0);
    }
    else if( c->option && c->option[0] )
    {
        interface_expand_if_text_placeholders(game, cd, c->option, exp, (int)sizeof(exp));
        uitree_opt_append(os, (enum MinimenuAction)MINIMENU_ACTION_IF_BUTTON, exp, comp, 0, 0);
    }
    else
    {
        for( int i = 0; i < 5; i++ )
        {
            if( !c->iop[i] || !c->iop[i][0] )
                continue;
            enum MinimenuAction act = MINIMENU_ACTION_INV_BUTTON1;
            if( i == 1 )
                act = MINIMENU_ACTION_INV_BUTTON2;
            else if( i == 2 )
                act = MINIMENU_ACTION_INV_BUTTON3;
            else if( i == 3 )
                act = MINIMENU_ACTION_INV_BUTTON4;
            else if( i == 4 )
                act = MINIMENU_ACTION_INV_BUTTON5;
            interface_expand_if_text_placeholders(game, cd, c->iop[i], exp, (int)sizeof(exp));
            uitree_opt_append(os, act, exp, comp, 0, 0);
        }
    }

    if( os->option_count > 0 )
    {
        uitree_option_set_sort_ts(os);
        return;
    }

    if( c->type == UIELEM_RS_TEXT && c->u.rs_text.text && c->u.rs_text.text[0] )
    {
        interface_expand_if_text_placeholders(game, cd, c->u.rs_text.text, exp, (int)sizeof(exp));
        uitree_opt_append(os, (enum MinimenuAction)MINIMENU_ACTION_IF_BUTTON, exp, comp, 0, 0);
        uitree_option_set_sort_ts(os);
        return;
    }

    if( uitree_is_clickable_button_ui(c) )
        uitree_opt_append(os, MINIMENU_ACTION_IF_BUTTON, "Ok", comp, 0, 0);

    if( os->option_count > 0 )
        uitree_option_set_sort_ts(os);
}

static void
uitree_fill_inv_slot_options(
    struct GGame* game,
    struct StaticUIComponent* inv,
    int slot,
    struct UITreeOptionSet* os)
{
    int comp_id = inv->component_id >= 0 ? inv->component_id : 0;

    int obj_id = uitree_inv_slot_obj_def_id(game, inv, slot);

    struct GameCacheObj* obj =
        obj_id > 0 && game->gamecache ? gamecache_get_obj(game->gamecache, obj_id) : NULL;

    if( game->iface && inv->interactable )
    {
        if( game->iface->inv_use_mode )
        {
            if( comp_id != game->iface->inv_sel_comp_id || slot != game->iface->inv_sel_slot )
            {
                if( obj && obj->name )
                {
                    char line[128];
                    snprintf(
                        line,
                        sizeof(line),
                        "Use %s with @lre@%s",
                        game->iface->inv_sel_obj_name[0] ? game->iface->inv_sel_obj_name : "item",
                        obj->name);
                    uitree_opt_append(os, MINIMENU_ACTION_OPHELDU, line, obj_id, slot, comp_id);
                    uitree_option_set_sort_ts(os);
                }
            }
            return;
        }
        if( game->iface->inv_target_mode )
        {
            if( (game->iface->inv_target_mask & 0x10) == 16 && obj && obj->name )
            {
                char line[160];
                snprintf(
                    line,
                    sizeof(line),
                    "%s @lre@%s",
                    game->iface->inv_target_op[0] ? game->iface->inv_target_op : "",
                    obj->name);
                uitree_opt_append(os, MINIMENU_ACTION_TGT_HELD, line, obj_id, slot, comp_id);
                uitree_option_set_sort_ts(os);
            }
            return;
        }
    }

    char const* ciop[5];
    for( int i = 0; i < 5; i++ )
        ciop[i] = inv->iop[i];

    uitree_fill_inv_slot_common(
        game, comp_id, slot, obj_id, obj, inv->interactable, inv->usable, ciop, os);
}

void
uitree_fill_inv_slot_options_from_cache_inv(
    struct GGame* game,
    struct GameCacheComponent* inv,
    int slot,
    struct UITreeOptionSet* os)
{
    int obj_def_id = 0;
    int nslots = inv->width * inv->height;
    if( slot >= 0 && slot < nslots && inv->invSlotObjId && slot < UI_INVENTORY_MAX_ITEMS &&
        inv->invSlotObjId[slot] > 0 )
        obj_def_id = inv->invSlotObjId[slot] - 1;

    struct GameCacheObj* obj =
        obj_def_id > 0 && game->gamecache ? gamecache_get_obj(game->gamecache, obj_def_id) : NULL;

    char const* ciop[5];
    if( inv->iop )
    {
        for( int i = 0; i < 5; i++ )
            ciop[i] = inv->iop[i];
    }
    else
    {
        for( int i = 0; i < 5; i++ )
            ciop[i] = NULL;
    }

    uitree_fill_inv_slot_common(
        game,
        inv->id,
        slot,
        obj_def_id,
        obj,
        inv->interactable ? (uint8_t)1 : (uint8_t)0,
        inv->usable ? (uint8_t)1 : (uint8_t)0,
        ciop,
        os);
}

static void
uitree_fill_hit_options(
    struct GGame* game,
    struct UITree* tree,
    int hit_idx,
    int inv_slot)
{
    struct StaticUIComponent* c = &tree->components[hit_idx];
    struct UITreeOptionSet* os = &tree->uitree_optionset;

    if( c->type == UIELEM_BUILTIN_WORLD )
        return;

    if( (c->type == UIELEM_RS_INV || c->type == UIELEM_RS_INV_TEXT) && inv_slot >= 0 )
        uitree_fill_inv_slot_options(game, c, inv_slot, os);
    else
        uitree_fill_simple_component_options(game, c, os);
}

static void
uitree_pick_descend(
    struct GGame* game,
    struct UITree* t,
    int idx,
    int mx,
    int my,
    struct UIPickFrame* stack,
    int* stack_top,
    int* out_hit,
    int* out_slot)
{
    struct StaticUIComponent* c = &t->components[idx];
    if( c->is_hidden )
        return;

    if( c->type == UIELEM_BUILTIN_SIDEBAR && !uitree_sidebar_tab_active_pick(game, c) )
        return;

    if( c->type == UIELEM_BUILTIN_CHAT_DIALOG &&
        (!game->iface || game->iface->chat_interface_id < 0) )
        return;

    if( c->type == UIELEM_BUILTIN_SIDEBAR_OVERLAY &&
        (!game->iface || game->iface->sidebar_interface_id < 0) )
        return;

    if( c->type == UIELEM_BUILTIN_VIEWPORT_OVERLAY &&
        (!game->iface || game->iface->viewport_interface_id < 0) )
        return;

    bool pushed_layer = false;
    if( c->type == UIELEM_RS_LAYER && uitree_pick_should_descend_layer(game, c) )
    {
        if( *stack_top + 1 >= UIFRAME_LAYER_STACK_MAX )
            return;
        uitree_push_pick_layer(stack, stack_top, game, c);
        pushed_layer = true;
    }

    int children[UITREE_PICK_CHILD_MAX];
    int nc = 0;
    for( int ch = c->first_child; ch >= 0 && nc < UITREE_PICK_CHILD_MAX;
         ch = t->components[ch].next_sibling )
        children[nc++] = ch;

    for( int i = nc - 1; i >= 0; i-- )
    {
        uitree_pick_descend(game, t, children[i], mx, my, stack, stack_top, out_hit, out_slot);
        if( *out_hit >= 0 )
        {
            if( pushed_layer )
                (*stack_top)--;
            return;
        }
    }

    int scroll_for_hit = *stack_top >= 0 ? stack[*stack_top].scroll_y_total : 0;

    if( uitree_node_skipped_for_pick(c->type) )
    {
        if( pushed_layer )
            (*stack_top)--;
        return;
    }

    if( !uitree_point_in_component_scr(c, mx, my, scroll_for_hit) ||
        !uitree_point_in_all_clips(mx, my, stack, *stack_top) )
    {
        if( pushed_layer )
            (*stack_top)--;
        return;
    }

    if( c->type == UIELEM_RS_INV || c->type == UIELEM_RS_INV_TEXT )
    {
        int slot = -1;
        bool in_slot = false;
        if( c->type == UIELEM_RS_INV_TEXT )
            in_slot = uitree_inv_text_slot_contains_mouse(c, mx, my, scroll_for_hit, &slot);
        else
            in_slot = uitree_inv_slot_contains_mouse(c, mx, my, scroll_for_hit, &slot);
        if( in_slot && uitree_inv_slot_has_menu(game, c, slot) )
        {
            *out_hit = idx;
            *out_slot = slot;
        }
    }
    else if( c->type == UIELEM_BUILTIN_WORLD )
    {
        *out_hit = idx;
        *out_slot = -1;
    }
    else if( c->type != UIELEM_RS_LAYER && uitree_component_has_menu_candidate(c) )
    {
        *out_hit = idx;
        *out_slot = -1;
    }

    if( pushed_layer )
        (*stack_top)--;
}

static void
uitree_debug_log_inv_menu(
    struct GGame* game,
    struct UITree* tree,
    int inv_slot)
{
    char const* e = getenv("TORI_DEBUG_INV_MENU");
    if( !e || e[0] == '\0' || strcmp(e, "0") == 0 || strcmp(e, "false") == 0 )
        return;

    int hit = tree->hover_node_index;
    int nopt = tree->uitree_optionset.option_count;
    fprintf(
        stderr,
        "[TORI_DEBUG_INV_MENU] hover=%d opts=%d inv_slot=%d mx=%d my=%d\n",
        hit,
        nopt,
        inv_slot,
        game->mouse.cursor_x,
        game->mouse.cursor_y);
    if( hit >= 0 && (uint32_t)hit < tree->component_count )
    {
        struct StaticUIComponent* c = &tree->components[hit];
        fprintf(stderr, "  hit type=%d comp_id=%d\n", (int)c->type, c->component_id);
        if( (c->type == UIELEM_RS_INV || c->type == UIELEM_RS_INV_TEXT) && inv_slot >= 0 &&
            game->gamecache )
        {
            int inv_i = c->u.rs_inv.inv_index;
            int pool_wire = 0;
            if( game->inv_pool && inv_i >= 0 && inv_i < game->inv_pool->count &&
                inv_slot < UI_INVENTORY_MAX_ITEMS )
                pool_wire = game->inv_pool->inventories[inv_i].items[inv_slot].obj_id;
            int pool_0base = pool_wire > 0 ? pool_wire - 1 : 0;
            int cwire = 0;
            struct GameCacheComponent* cc =
                gamecache_get_component(game->gamecache, c->component_id);
            if( cc && cc->invSlotObjId && inv_slot >= 0 && inv_slot < cc->width * cc->height )
                cwire = cc->invSlotObjId[inv_slot];
            fprintf(
                stderr,
                "  inv_index=%d pool_obj_wire=%d pool_obj_0base=%d cache_inv_wire=%d\n",
                inv_i,
                pool_wire,
                pool_0base,
                cwire);
        }
    }
}

/** Client.ts addComponentOptions on build-cache IfType for viewport main modal when mouse is in
 * root bounds — shared by uitree_fill_viewport_modal_options_from_cache. */
static void
uitree_append_cache_button_options(
    struct GGame* game,
    struct GameCacheComponent* child,
    struct UITreeOptionSet* os)
{
    char exp[192];

    if( child->buttonType == COMPONENT_BUTTON_TYPE_OK )
    {
        char const* raw = NULL;
        if( child->text && child->text[0] )
            raw = child->text;
        else if( child->option && child->option[0] )
            raw = child->option;
        if( !raw )
            return;
        interface_expand_if_text_placeholders(game, child, raw, exp, (int)sizeof(exp));
        uitree_opt_append(os, MINIMENU_ACTION_IF_BUTTON, exp, child->id, 0, 0);
    }
    else if(
        child->buttonType == COMPONENT_BUTTON_TYPE_TARGET && game->iface &&
        game->iface->inv_target_mode == 0 )
    {
        char line[96];
        char prefix[72];
        prefix[0] = '\0';
        if( child->targetVerb && child->targetVerb[0] )
        {
            char const* sp = strchr(child->targetVerb, ' ');
            if( sp && sp != child->targetVerb )
            {
                size_t n = (size_t)(sp - child->targetVerb);
                if( n > sizeof(prefix) - 1 )
                    n = sizeof(prefix) - 1;
                memcpy(prefix, child->targetVerb, n);
                prefix[n] = '\0';
            }
            else
                strncpy(prefix, child->targetVerb, sizeof(prefix) - 1);
        }
        snprintf(
            line,
            sizeof(line),
            "%s @gre@%s",
            prefix,
            (child->targetText && child->targetText[0]) ? child->targetText : "");
        interface_expand_if_text_placeholders(game, child, line, exp, (int)sizeof(exp));
        uitree_opt_append(os, MINIMENU_ACTION_OPHELDT_SELECT, exp, 0, 0, child->id);
    }
    else if( child->buttonType == COMPONENT_BUTTON_TYPE_CLOSE )
    {
        uitree_opt_append(os, MINIMENU_ACTION_CLOSE_MODAL, "Close", child->id, 0, 0);
    }
    else if( child->buttonType == COMPONENT_BUTTON_TYPE_TOGGLE && child->text && child->text[0] )
    {
        interface_expand_if_text_placeholders(game, child, child->text, exp, (int)sizeof(exp));
        uitree_opt_append(os, MINIMENU_ACTION_IF_BUTTON_TOGGLE, exp, child->id, 0, 0);
    }
    else if( child->buttonType == COMPONENT_BUTTON_TYPE_SELECT && child->text && child->text[0] )
    {
        interface_expand_if_text_placeholders(game, child, child->text, exp, (int)sizeof(exp));
        uitree_opt_append(os, MINIMENU_ACTION_IF_BUTTON_SELECT, exp, child->id, 0, 0);
    }
    else if( child->buttonType == COMPONENT_BUTTON_TYPE_CONTINUE )
    {
        char const* disp = NULL;
        if( child->option && child->option[0] )
        {
            interface_expand_if_text_placeholders(
                game, child, child->option, exp, (int)sizeof(exp));
            disp = exp;
        }
        else if( child->text && child->text[0] )
        {
            interface_expand_if_text_placeholders(game, child, child->text, exp, (int)sizeof(exp));
            disp = exp;
        }
        else
            disp = "Continue";
        uitree_opt_append(os, MINIMENU_ACTION_RESUME_PAUSEBUTTON, disp, child->id, 0, 0);
    }
}

static void
uitree_add_component_options_cache_rec(
    struct GGame* game,
    struct GameCacheComponent* com,
    int mx,
    int my,
    int x,
    int y,
    int scroll_y,
    struct UITreeOptionSet* os)
{
    if( !game || !game->iface || !game->gamecache || !com || !os )
        return;
    if( com->type != COMPONENT_TYPE_LAYER || !com->children || !com->childX || !com->childY ||
        com->hide || mx < x || my < y || mx > x + com->width || my > y + com->height )
        return;

    for( int i = 0; i < com->children_count; i++ )
    {
        int child_id = com->children[i];
        struct GameCacheComponent* child = gamecache_get_component(game->gamecache, child_id);
        if( !child || child->hide )
            continue;

        int childX = com->childX[i] + x;
        int childY = com->childY[i] + y - scroll_y;
        childX += child->x;
        childY += child->y;

        if( child->type == COMPONENT_TYPE_LAYER )
        {
            int scroll_pos = 0;
            if( child_id >= 0 && child_id < MAX_IFACE_SCROLL_IDS )
                scroll_pos = game->iface->component_scroll_position[child_id];
            int max_scroll = child->scroll - child->height;
            if( max_scroll < 0 )
                max_scroll = 0;
            if( scroll_pos > max_scroll )
                scroll_pos = max_scroll;
            if( scroll_pos < 0 )
                scroll_pos = 0;
            uitree_add_component_options_cache_rec(
                game, child, mx, my, childX, childY, scroll_pos, os);
        }
        else if( child->type == COMPONENT_TYPE_INV )
        {
            int slot = 0;
            int const nslots = child->width * child->height;
            bool filled_inv_row = false;
            for( int row = 0; row < child->height && !filled_inv_row; row++ )
            {
                for( int col = 0; col < child->width && !filled_inv_row; col++ )
                {
                    int slotX = childX + col * (child->marginX + 32);
                    int slotY = childY + row * (child->marginY + 32);
                    if( slot < 20 && child->invSlotOffsetX && child->invSlotOffsetY )
                    {
                        slotX += child->invSlotOffsetX[slot];
                        slotY += child->invSlotOffsetY[slot];
                    }
                    if( mx >= slotX && my >= slotY && mx < slotX + 32 && my < slotY + 32 &&
                        child->invSlotObjId && slot < nslots && child->invSlotObjId[slot] > 0 )
                    {
                        uitree_fill_inv_slot_options_from_cache_inv(game, child, slot, os);
                        filled_inv_row = true;
                    }
                    slot++;
                }
            }
        }
        else if(
            mx >= childX && my >= childY && mx < childX + child->width &&
            my < childY + child->height )
            uitree_append_cache_button_options(game, child, os);
    }
}

static void
uitree_fill_viewport_modal_options_from_cache(
    struct GGame* game,
    int mx,
    int my,
    struct UITreeOptionSet* os)
{
    if( !game || !game->iface || !game->gamecache || !os )
        return;
    int root_id = game->iface->viewport_interface_id;
    if( root_id < 0 )
        return;
    struct GameCacheComponent* root = gamecache_get_component(game->gamecache, root_id);
    if( !root )
        return;
    uitree_add_component_options_cache_rec(game, root, mx, my, 4, 4, 0, os);
    if( os->option_count > 0 )
        uitree_option_set_sort_ts(os);
}

void
uitree_sync_hover_option_set(
    struct GGame* game,
    int pick_mx,
    int pick_my)
{
    struct UITree* tree = game ? game->ui_root_buffer : NULL;
    if( !game || !tree )
        return;

    int mx = pick_mx;
    int my = pick_my;

    tree->hover_node_index = -1;
    tree->uitree_optionset.option_count = 0;
    tree->hover_inv_component_id = -1;
    tree->hover_inv_slot = -1;

    if( interface_sidebar_overlay_try_fill_uitree_options(
            game,
            mx,
            my,
            &tree->uitree_optionset,
            &tree->hover_inv_component_id,
            &tree->hover_inv_slot) )
    {
        uitree_debug_log_inv_menu(game, tree, -1);
        return;
    }

    struct UIPickFrame stack[UIFRAME_LAYER_STACK_MAX];
    int stack_top = -1;
    int hit = -1;
    int slot = -1;

    int roots[UITREE_PICK_CHILD_MAX];
    int nr = 0;
    for( int r = tree->root_index; r >= 0 && nr < UITREE_PICK_CHILD_MAX;
         r = tree->components[r].next_sibling )
        roots[nr++] = r;

    for( int i = nr - 1; i >= 0; i-- )
    {
        uitree_pick_descend(game, tree, roots[i], mx, my, stack, &stack_top, &hit, &slot);
        if( hit >= 0 )
            break;
    }

    tree->hover_node_index = hit;
    if( hit >= 0 )
        uitree_fill_hit_options(game, tree, hit, slot);

    int log_slot = slot;
    if( hit >= 0 && (uint32_t)hit < tree->component_count &&
        (tree->components[hit].type == UIELEM_RS_INV ||
         tree->components[hit].type == UIELEM_RS_INV_TEXT) )
    {
        tree->hover_inv_component_id = tree->components[hit].component_id;
        tree->hover_inv_slot = slot;
    }

    if( hit < 0 || ((uint32_t)hit < tree->component_count &&
                    tree->components[hit].type == UIELEM_BUILTIN_WORLD) )
    {
        int fh = -1;
        int fs = -1;
        if( uitree_fallback_pick_rs_inv(game, tree, mx, my, &fh, &fs) )
        {
            tree->hover_node_index = fh;
            tree->uitree_optionset.option_count = 0;
            uitree_fill_hit_options(game, tree, fh, fs);
            log_slot = fs;
            tree->hover_inv_component_id = (fh >= 0 && (uint32_t)fh < tree->component_count)
                                               ? tree->components[fh].component_id
                                               : -1;
            tree->hover_inv_slot = fs;
        }
    }

    /* Client.ts buildMinimenu (2772–2777): when mainModalId !== -1 the viewport adds only
     * addComponentOptions(mainModal), not addWorldOptions. UITree pick often lands on
     * UIELEM_BUILTIN_WORLD with no rows — rebuild lines from build-cache root at viewport (4,4). */
    if( game->iface && game->iface->viewport_interface_id >= 0 && mx > 4 && my > 4 && mx < 516 &&
        my < 338 )
    {
        bool hit_world = hit >= 0 && (uint32_t)hit < tree->component_count &&
                         tree->components[hit].type == UIELEM_BUILTIN_WORLD;
        if( hit_world || tree->uitree_optionset.option_count == 0 )
        {
            tree->uitree_optionset.option_count = 0;
            uitree_fill_viewport_modal_options_from_cache(game, mx, my, &tree->uitree_optionset);
        }
    }

    uitree_debug_log_inv_menu(game, tree, log_slot);
}

static void
uitree_run_scrollbar_search(
    struct GGame* game,
    int mx,
    int my,
    int32_t* best_gutter,
    int* best_gd,
    int32_t* best_layer,
    int* best_ld)
{
    struct UITree* t = game->ui_root_buffer;
    *best_gutter = -1;
    *best_gd = -1;
    *best_layer = -1;
    *best_ld = -1;
    for( int r = t->root_index; r >= 0; r = t->components[r].next_sibling )
        uitree_visit_scrollbar_candidates(
            game, t, r, mx, my, best_gutter, best_gd, best_layer, best_ld);
}

bool
uitree_find_scrollbar_at(
    struct GGame* game,
    int mouse_x,
    int mouse_y,
    struct UITreeScrollbarHit* out)
{
    if( !game || !out || !game->ui_root_buffer || !game->iface )
        return false;
    int32_t best;
    int best_d;
    int32_t best_layer;
    int best_ld;
    uitree_run_scrollbar_search(game, mouse_x, mouse_y, &best, &best_d, &best_layer, &best_ld);
    (void)best_layer;
    (void)best_ld;
    if( best < 0 )
        return false;
    struct UITree* t = game->ui_root_buffer;
    struct StaticUIComponent* c = &t->components[best];
    int lh = c->position.height;
    int sh = c->u.rs_layer.scroll_height;
    int sp = iface_scroll_clamped(game, c->component_id, sh, lh);
    int max_sc = sh - lh;
    if( max_sc < 0 )
        max_sc = 0;
    int local_y = mouse_y - c->position.y;
    out->layer_idx = best;
    out->component_id = c->component_id;
    out->layer_x = c->position.x;
    out->layer_y = c->position.y;
    out->layer_width = c->position.width;
    out->layer_height = lh;
    out->scroll_height = sh;
    out->scroll_pos = sp;
    out->max_scroll = max_sc;
    out->region = scrollbar_hit_region(local_y, lh, sp, sh);
    return true;
}

int32_t
uitree_innermost_scroll_layer_at(
    struct GGame* game,
    int mouse_x,
    int mouse_y)
{
    if( !game || !game->ui_root_buffer )
        return -1;
    int32_t best_gutter;
    int best_gd;
    int32_t best_layer;
    int best_ld;
    uitree_run_scrollbar_search(
        game, mouse_x, mouse_y, &best_gutter, &best_gd, &best_layer, &best_ld);
    (void)best_gutter;
    (void)best_gd;
    return best_layer;
}

/** Client.ts IfType.getTempModel sequence id + readyanim fallback (effective head mesh type/id). */
int
uitree_interface_resolved_sequence_id(
    struct GGame* game,
    struct GameCacheComponent* gcc,
    bool active,
    int effective_mt,
    int effective_mid)
{
    if( !game || !gcc )
        return -1;
    int sequence_id = active ? gcc->activeAnim : gcc->anim;
    if( sequence_id >= 0 )
        return sequence_id;
    if( effective_mt == 2 && game->gamecache )
    {
        struct GameCacheNpc* npc = gamecache_get_npc(game->gamecache, effective_mid);
        if( npc )
            return npc->readyanim;
    }
    else if( effective_mt == 3 && game->world )
    {
        struct PlayerEntity* local = world_player(game->world, ACTIVE_PLAYER_SLOT);
        if( local && local->alive )
            return local->animation.readyanim;
    }
    return -1;
}

static struct GameCacheComponent*
uitree_rs_model_get_gcc(
    struct GGame* game,
    struct StaticUIComponent* c)
{
    if( !game || !game->gamecache || !c || c->component_id < 0 )
        return NULL;
    return gamecache_get_component(game->gamecache, c->component_id);
}

static int
uitree_rs_model_resolve_sequence_id(
    struct GGame* game,
    struct StaticUIComponent* c,
    struct GameCacheComponent* gcc)
{
    int sequence_id = -1;
    if( gcc && (gcc->modelType == 2 || gcc->modelType == 3) && game->clientscript_vm )
    {
        bool active = clientscript_vm_if_active(game->clientscript_vm, game, gcc);
        int mt = gcc->modelType;
        int mid = gcc->model;
        if( active && gcc->activeModelType != 0 )
        {
            mt = gcc->activeModelType;
            mid = gcc->activeModel;
        }
        sequence_id = uitree_interface_resolved_sequence_id(game, gcc, active, mt, mid);
    }
    else
    {
        struct ClientScriptVM* vm = game->clientscript_vm;
        bool active = clientscript_vm_active(
            vm, c->scripts, c->scripts_count, c->script_comparator, c->script_operand);
        sequence_id = active ? c->active_anim_id : c->anim_id;

        if( sequence_id < 0 && gcc )
        {
            if( gcc->modelType == 2 && game->gamecache )
            {
                struct GameCacheNpc* npc = gamecache_get_npc(game->gamecache, gcc->model);
                if( npc )
                    sequence_id = npc->readyanim;
            }
            else if( gcc->modelType == 3 && game->world )
            {
                struct PlayerEntity* local = world_player(game->world, ACTIVE_PLAYER_SLOT);
                if( local && local->alive )
                    sequence_id = local->animation.readyanim;
            }
        }
    }
    return sequence_id;
}

void
uitree_rs_model_ensure_sequence_precached(
    struct GGame* game,
    struct StaticUIComponent* c)
{
    if( !game || !c || c->type != UIELEM_RS_MODEL )
        return;

    struct GameCacheComponent* gcc = uitree_rs_model_get_gcc(game, c);
    int sequence_id = uitree_rs_model_resolve_sequence_id(game, c, gcc);

    if( sequence_id < 0 || !game->gamecache || !game->gamecache->sequences_hmap )
    {
        c->u.rs_model.rs_model_cached_sequence_id = -1;
        return;
    }

    struct GameCacheSequence* seq = gamecache_get_sequence(game->gamecache, sequence_id);
    if( !seq || seq->frame_count <= 0 || !seq->frames )
    {
        c->u.rs_model.rs_model_cached_sequence_id = -1;
        return;
    }

    if( c->u.rs_model.scene_id < 0 )
    {
        c->u.rs_model.rs_model_cached_sequence_id = -1;
        return;
    }

    if( sequence_id == c->u.rs_model.rs_model_cached_sequence_id )
        return;

    c->u.rs_model.rs_model_cached_sequence_id = sequence_id;
}

static void
uitree_dashmodel_apply_animframe_by_id(
    struct GGame* game,
    struct DashModel* model,
    int anim_frame_id)
{
    if( anim_frame_id == -1 || !game || !model || !game->gamecache )
        return;
    struct GameCacheAnimframe* animframe = gamecache_get_animframe(game->gamecache, anim_frame_id);
    if( !animframe )
        return;
    struct DashFrame* dash_frame = dashframe_new_from_gamecache_animframe(animframe);
    struct DashFramemap* dash_framemap = dashframemap_new_from_gamecache_animframe(animframe);
    if( dash_frame && dash_framemap )
    {
        dashmodel_animate(model, dash_frame, dash_framemap);
        dashframe_free(dash_frame);
        dashframemap_free(dash_framemap);
    }
}

void
uitree_interface_apply_sequence_keyframe_to_model(
    struct GGame* game,
    struct DashModel* model,
    struct GameCacheSequence* seq,
    int frame_index,
    struct StaticUIComponent* rs_model_ui_comp_nullable)
{
    if( !game || !model || !seq || frame_index < 0 || frame_index >= seq->frame_count ||
        !seq->frames )
        return;

#if TORIRS_DISABLE_INTERFACE_MODEL_ANIM
    (void)rs_model_ui_comp_nullable;
    return;
#endif

    uitree_dashmodel_apply_animframe_by_id(game, model, seq->frames[frame_index]);
    int sid = seq->iframes ? seq->iframes[frame_index] : -1;
    uitree_dashmodel_apply_animframe_by_id(game, model, sid);
}

/** Client.ts SeqType.getDuration for interface stepping (Client.ts animateInterface). */
static int
uitree_seq_frame_duration(
    struct GameCache* gc,
    struct GameCacheSequence* seq,
    int frame_idx)
{
    if( !seq || frame_idx < 0 || frame_idx >= seq->frame_count || !seq->frames )
        return 1;

    int duration = 0;
    if( seq->delay )
        duration = seq->delay[frame_idx];

    if( duration == 0 && gc )
    {
        struct GameCacheAnimframe* af = gamecache_get_animframe(gc, seq->frames[frame_idx]);
        if( af && af->delay > 0 )
            duration = af->delay;
    }
    if( duration <= 0 )
        duration = 1;
    return duration;
}

static void
uitree_step_rs_model_sequence_one(
    struct GGame* game,
    struct StaticUIComponent* c,
    int cycles_elapsed)
{
    if( !game || !c || cycles_elapsed <= 0 )
        return;

    struct GameCacheComponent* gcc = uitree_rs_model_get_gcc(game, c);

    uitree_rs_model_ensure_sequence_precached(game, c);

    int sequence_id = uitree_rs_model_resolve_sequence_id(game, c, gcc);

    if( sequence_id < 0 || !game->gamecache || !game->gamecache->sequences_hmap )
        return;

    struct GameCacheSequence* seq = gamecache_get_sequence(game->gamecache, sequence_id);
    if( !seq || seq->frame_count <= 0 || !seq->frames )
        return;

    if( c->seq_frame < 0 )
        c->seq_frame = 0;
    if( c->seq_frame >= seq->frame_count )
        c->seq_frame = 0;

    c->seq_cycle += cycles_elapsed;
    while( c->seq_cycle > uitree_seq_frame_duration(game->gamecache, seq, c->seq_frame) )
    {
        int dur = uitree_seq_frame_duration(game->gamecache, seq, c->seq_frame);
        c->seq_cycle -= dur + 1;
        c->seq_frame++;
        if( c->seq_frame >= seq->frame_count )
        {
            c->seq_frame -= seq->loops;
            if( c->seq_frame < 0 || c->seq_frame >= seq->frame_count )
                c->seq_frame = 0;
        }
    }
}

void
uitree_step_rs_model_animations(
    struct GGame* game,
    int cycles_elapsed)
{
    if( !game || cycles_elapsed <= 0 )
        return;
    struct UITree* tree = game->ui_root_buffer;
    if( !tree )
        return;

    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct StaticUIComponent* c = &tree->components[i];
        if( c->type != UIELEM_RS_MODEL || c->is_hidden )
            continue;
        uitree_step_rs_model_sequence_one(game, c, cycles_elapsed);
    }
}

/** Client IfType.getTempModel parity: correct model1/model2, dual iframe animate, then lighting.
 * Caller must dashmodel_free when done. */
struct DashModel*
uitree_chat_head_dashmodel_for_frame(
    struct GGame* game,
    struct GameCacheComponent* gcc,
    struct StaticUIComponent* ui_comp)
{
    if( !game || !gcc || !ui_comp || !game->clientscript_vm )
        return NULL;

    bool active = clientscript_vm_if_active(game->clientscript_vm, game, gcc);
    int mt = gcc->modelType;
    int mid = gcc->model;
    if( active && gcc->activeModelType != 0 )
    {
        mt = gcc->activeModelType;
        mid = gcc->activeModel;
    }
    if( mt != 2 && mt != 3 )
        return NULL;

    int* slots = NULL;
    int* colors = NULL;
    if( mt == 3 && game->world )
    {
        struct PlayerEntity* local = world_player(game->world, ACTIVE_PLAYER_SLOT);
        if( !local || !local->alive )
            return NULL;
        slots = local->appearance.slots;
        colors = local->appearance.colors;
    }

    struct DashModel* head =
        entity_scenebuild_head_model_for_component_lights(game, mt, mid, slots, colors, false);
    if( !head )
        return NULL;

    uitree_rs_model_ensure_sequence_precached(game, ui_comp);

    int sequence_id = uitree_interface_resolved_sequence_id(game, gcc, active, mt, mid);
    if( sequence_id >= 0 && game->gamecache->sequences_hmap )
    {
        struct GameCacheSequence* seq = gamecache_get_sequence(game->gamecache, sequence_id);
        if( seq && seq->frame_count > 0 && seq->frames )
        {
            int fi = ui_comp->seq_frame;
            if( fi < 0 )
                fi = 0;
            if( fi >= seq->frame_count )
                fi %= seq->frame_count;
            uitree_interface_apply_sequence_keyframe_to_model(game, head, seq, fi, ui_comp);
        }
    }

    entity_scenebuild_interface_head_apply_default_light(head);
    return head;
}
