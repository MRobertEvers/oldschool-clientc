#include "uitree.h"

#include "osrs/buildcachedat.h"
#include "osrs/game.h"
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

static int
uitree_node_depth(struct UITree* t, int idx)
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
iface_scroll_clamped(struct GGame* game, int component_id, int scroll_height, int layer_h)
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
scrollbar_hit_region(int local_y, int layer_h, int scroll_pos, int scroll_height)
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
        (!c->u.rs_layer.hide ||
         interface_component_is_overlay_hovered(game, c->component_id)) )
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

int32_t
uitree_find_by_component_id(const struct UITree* tree, int component_id)
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
        if( c->type == UIELEM_RS_INV && c->component_id == component_id )
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
    case UIELEM_RS_LAYER:
        return "rs_layer";
    case UIELEM_RS_RECT:
        return "rs_rect";
    case UIELEM_BUILTIN_CHAT_INPUT:
        return "chat_input";
    case UIELEM_BUILTIN_CHAT_PRIVACY:
        return "chat_privacy";
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
    tree->root_index               = -1;
    tree->ui_layer_stack_top       = -1;
    tree->ui_scrollbar_drag_component_id = -1;
    tree->ui_scrollbar0_element_id = -1;
    tree->ui_scrollbar1_element_id = -1;
    tree->ui_minimap_mapdots0_element_id = -1;
    tree->ui_minimap_mapdots1_element_id = -1;
    tree->ui_minimap_mapdots3_element_id = -1;
    tree->ui_minimap_mapdots4_element_id = -1;
    tree->ui_minimap_mapmarker2_element_id = -1;
    tree->ui_minimap_mapmarker_element_id = -1;
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
            printf(
                "       rs_inv inv_index=%d cols=%d rows=%d margin=(%d,%d)\n",
                c->u.rs_inv.inv_index,
                c->u.rs_inv.cols,
                c->u.rs_inv.rows,
                c->u.rs_inv.margin_x,
                c->u.rs_inv.margin_y);
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
                "       rs_text font_id=%d font_idx=%d colors=%d/%d/%d/%d center=%d text=%s active=%s\n",
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
            printf("       rs_model scene2_element_id=%d\n", c->u.rs_model.scene2_element_id);
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
    int scene2_element_id,
    int x,
    int y,
    int width,
    int height)
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
    component->u.rs_model.scene2_element_id = scene2_element_id;
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

/** Orphaned RS nodes stay in the dense array; hide the whole subtree so any stray visit skips draw. */
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
uitree_sidebar_tab_active_pick(struct GGame* game, struct StaticUIComponent* sidebar)
{
    if( !sidebar || sidebar->type != UIELEM_BUILTIN_SIDEBAR )
        return false;
    if( !game || !game->iface )
        return false;
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
    stack[*stack_top].clip_x       = layer->position.x;
    stack[*stack_top].clip_y       = layer->position.y;
    stack[*stack_top].clip_w       = layer->position.width;
    stack[*stack_top].clip_h       = lh;
}

static bool
uitree_pick_should_descend_layer(struct GGame* game, struct StaticUIComponent* c)
{
    if( c->type != UIELEM_RS_LAYER )
        return false;
    if( c->is_hidden )
        return false;
    if( c->u.rs_layer.hide &&
        !interface_component_is_overlay_hovered(game, c->component_id) )
        return false;
    if( c->first_child < 0 )
        return false;
    return true;
}

static bool
uitree_point_in_all_clips(int mx, int my, struct UIPickFrame const* stack, int stack_top)
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
    int base_x   = inv->position.x;
    int base_y   = inv->position.y - scroll_off;
    int i        = 0;
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
uitree_inv_slot_has_menu(
    struct GGame* game,
    struct StaticUIComponent* inv,
    int slot)
{
    if( slot < 0 )
        return false;
    int obj_id = 0;
    int inv_i = inv->u.rs_inv.inv_index;
    if( game && game->inv_pool && inv_i >= 0 && inv_i < game->inv_pool->count &&
        slot < UI_INVENTORY_MAX_ITEMS )
        obj_id = game->inv_pool->inventories[inv_i].items[slot].obj_id;
    if( obj_id > 0 )
        return true;
    for( int i = 0; i < 5; i++ )
    {
        if( inv->iop[i] && inv->iop[i][0] )
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
                int t       = os->order[i];
                os->order[i]     = os->order[i + 1];
                os->order[i + 1] = t;
                sorted           = false;
            }
        }
        if( sorted )
            break;
    }
}

static void
uitree_fill_simple_component_options(
    struct GGame* game,
    struct StaticUIComponent* c,
    struct UITreeOptionSet* os)
{
    (void)game;
    int comp = c->component_id >= 0 ? c->component_id : 0;

    if( c->target_verb && c->target_verb[0] )
    {
        char line[96];
        if( c->target_text && c->target_text[0] )
            snprintf(line, sizeof(line), "%s %s", c->target_verb, c->target_text);
        else
            snprintf(line, sizeof(line), "%s", c->target_verb);
        uitree_opt_append(os, (enum MinimenuAction)MINIMENU_ACTION_IF_BUTTON, line, comp, 0, 0);
    }
    else if( c->option && c->option[0] )
    {
        uitree_opt_append(os, (enum MinimenuAction)MINIMENU_ACTION_IF_BUTTON, c->option, comp, 0, 0);
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
            uitree_opt_append(os, act, c->iop[i], comp, 0, 0);
        }
    }

    if( os->option_count > 0 )
        return;

    if( c->type == UIELEM_RS_TEXT && c->u.rs_text.text && c->u.rs_text.text[0] )
    {
        uitree_opt_append(
            os,
            (enum MinimenuAction)MINIMENU_ACTION_IF_BUTTON,
            c->u.rs_text.text,
            comp,
            0,
            0);
        return;
    }

    if( c->button_kind == UI_BUTTON_CLOSE )
        uitree_opt_append(os, MINIMENU_ACTION_CLOSE_MODAL, "Close", comp, 0, 0);
    else if( c->button_kind == UI_BUTTON_CONTINUE )
        uitree_opt_append(os, MINIMENU_ACTION_RESUME_PAUSEBUTTON, "Continue", comp, 0, 0);
    else if( uitree_is_clickable_button_ui(c) )
        uitree_opt_append(os, MINIMENU_ACTION_IF_BUTTON, "Ok", comp, 0, 0);
}

static void
uitree_fill_inv_slot_options(
    struct GGame* game,
    struct StaticUIComponent* inv,
    int slot,
    struct UITreeOptionSet* os)
{
    int comp_id = inv->component_id >= 0 ? inv->component_id : 0;

    int obj_id = 0;
    int inv_i    = inv->u.rs_inv.inv_index;
    if( game->inv_pool && inv_i >= 0 && inv_i < game->inv_pool->count && slot >= 0 &&
        slot < UI_INVENTORY_MAX_ITEMS )
        obj_id = game->inv_pool->inventories[inv_i].items[slot].obj_id;

    struct CacheDatConfigObj* obj =
        obj_id > 0 && game->buildcachedat ? buildcachedat_get_obj(game->buildcachedat, obj_id)
                                          : NULL;

    char const* oname = (obj && obj->name) ? obj->name : "";

    /* Mirror interface_get_inv_default_action ordering (fields live on StaticUIComponent). */
    if( obj )
    {
        if( inv->interactable )
        {
            for( int op = 4; op >= 3; op-- )
            {
                if( obj->iop[op] )
                {
                    enum MinimenuAction act =
                        (op == 4) ? MINIMENU_ACTION_OPHELD5 : MINIMENU_ACTION_OPHELD4;
                    uitree_opt_append(os, act, obj->iop[op], comp_id, slot, obj_id);
                }
                else if( op == 4 )
                {
                    char tbuf[64];
                    snprintf(tbuf, sizeof(tbuf), "Drop @lre@%s", oname);
                    uitree_opt_append(os, MINIMENU_ACTION_OPHELD5, tbuf, comp_id, slot, obj_id);
                }
            }
        }

        if( inv->usable )
            uitree_opt_append(os, MINIMENU_ACTION_OPHELDT_START, "Use", comp_id, slot, obj_id);

        if( inv->interactable )
        {
            for( int op = 2; op >= 0; op-- )
            {
                if( obj->iop[op] )
                {
                    enum MinimenuAction act = MINIMENU_ACTION_OPHELD1;
                    if( op == 1 )
                        act = MINIMENU_ACTION_OPHELD2;
                    else if( op == 2 )
                        act = MINIMENU_ACTION_OPHELD3;
                    uitree_opt_append(os, act, obj->iop[op], comp_id, slot, obj_id);
                }
            }
        }

        {
            for( int op = 4; op >= 0; op-- )
            {
                if( !inv->iop[op] )
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
                uitree_opt_append(os, act, inv->iop[op], comp_id, slot, obj_id);
            }
        }

        {
            char ex[72];
            snprintf(ex, sizeof(ex), "Examine @lre@%s", oname);
            uitree_opt_append(os, MINIMENU_ACTION_OPHELD6, ex, comp_id, slot, obj_id);
        }
    }
    else
    {
        for( int op = 4; op >= 0; op-- )
        {
            if( !inv->iop[op] )
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
            uitree_opt_append(os, act, inv->iop[op], comp_id, slot, 0);
        }
    }

    if( os->option_count > 0 )
        uitree_option_set_sort_ts(os);
}

static void
uitree_fill_hit_options(
    struct GGame* game,
    struct UITree* tree,
    int hit_idx,
    int inv_slot)
{
    struct StaticUIComponent* c = &tree->components[hit_idx];
    struct UITreeOptionSet* os  = &tree->uitree_optionset;

    if( c->type == UIELEM_BUILTIN_WORLD )
        return;

    if( c->type == UIELEM_RS_INV && inv_slot >= 0 )
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

    if( c->type == UIELEM_RS_INV )
    {
        int slot = -1;
        if( uitree_inv_slot_contains_mouse(c, mx, my, scroll_for_hit, &slot) &&
            uitree_inv_slot_has_menu(game, c, slot) )
        {
            *out_hit  = idx;
            *out_slot = slot;
        }
    }
    else if( c->type == UIELEM_BUILTIN_WORLD )
    {
        *out_hit  = idx;
        *out_slot = -1;
    }
    else if( c->type != UIELEM_RS_LAYER && uitree_component_has_menu_candidate(c) )
    {
        *out_hit  = idx;
        *out_slot = -1;
    }

    if( pushed_layer )
        (*stack_top)--;
}

void
uitree_sync_hover_option_set(struct GGame* game)
{
    struct UITree* tree = game ? game->ui_root_buffer : NULL;
    if( !game || !tree )
        return;

    tree->hover_node_index           = -1;
    tree->uitree_optionset.option_count = 0;

    struct UIPickFrame stack[UIFRAME_LAYER_STACK_MAX];
    int stack_top = -1;
    int hit       = -1;
    int slot      = -1;
    int mx        = game->mouse_x;
    int my        = game->mouse_y;

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
    uitree_run_scrollbar_search(game, mouse_x, mouse_y, &best_gutter, &best_gd, &best_layer, &best_ld);
    (void)best_gutter;
    (void)best_gd;
    return best_layer;
}
