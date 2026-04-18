#include "ui_dirty.h"

#include "osrs/game.h"
#include "osrs/interface_state.h"
#include "osrs/revconfig/uitree.h"
#include "osrs/rs_component_state.h"
#include "osrs/scene2.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool
frame_sidebar_tab_active_for_walk(
    struct GGame* game,
    struct StaticUIComponent* sidebar)
{
    if( !game || !game->iface )
        return false;
    if( sidebar->type != UIELEM_BUILTIN_SIDEBAR )
        return false;
    if( game->iface->sidebar_interface_id != -1 )
        return false;
    return game->iface->selected_tab == sidebar->u.sidebar.tabno;
}

static bool
frame_uitree_should_descend_walk(
    struct GGame* game,
    struct StaticUIComponent* c)
{
    if( c->first_child < 0 )
        return false;
    if( c->type == UIELEM_BUILTIN_SIDEBAR )
        return frame_sidebar_tab_active_for_walk(game, c);
    return true;
}

static void
redstone_tab_consume_click(struct GGame* game, struct StaticUIComponent* component)
{
    if( !game || !component || component->type != UIELEM_BUILTIN_REDSTONE_TAB )
        return;
    if( !game->iface || !game->ui_scene )
        return;
    if( game->iface->sidebar_interface_id != -1 )
        return;

    int tabno = component->u.redstone_tab.tabno;
    int x = component->position.x;
    int y = component->position.y;
    int w = component->position.width;
    int h = component->position.height;

    if( game->mouse_clicked && !game->interface_consumed_click )
    {
        int cx = game->mouse_clicked_x;
        int cy = game->mouse_clicked_y;
        if( cx >= x && cx < x + w && cy >= y && cy < y + h )
        {
            game->iface->selected_tab = tabno;
            game->interface_consumed_click = 1;
        }
    }
}

static void
walk_redstone_clicks_dfs(struct GGame* game, int32_t idx)
{
    if( idx < 0 || !game->ui_root_buffer )
        return;
    struct StaticUIComponent* c = &game->ui_root_buffer->components[idx];
    if( c->type == UIELEM_BUILTIN_REDSTONE_TAB )
        redstone_tab_consume_click(game, c);
    if( frame_uitree_should_descend_walk(game, c) )
        walk_redstone_clicks_dfs(game, c->first_child);
    walk_redstone_clicks_dfs(game, c->next_sibling);
}

static void
walk_redstone_clicks(struct GGame* game)
{
    if( !game->ui_root_buffer || game->ui_root_buffer->root_index < 0 )
        return;
    int32_t r = game->ui_root_buffer->root_index;
    while( r >= 0 )
    {
        walk_redstone_clicks_dfs(game, r);
        r = game->ui_root_buffer->components[r].next_sibling;
    }
}

static uint64_t
fnv1a64_mix(uint64_t h, uint64_t v)
{
    h ^= v;
    h *= 1099511628211ull;
    return h;
}

static bool
ui_point_in_component(struct StaticUIComponent const* c, int mx, int my)
{
    if( !c )
        return false;
    if( c->position.width <= 0 || c->position.height <= 0 )
        return false;
    int x = c->position.x;
    int y = c->position.y;
    int w = c->position.width;
    int h = c->position.height;
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

static uint64_t
parent_layer_sig_xor(struct GGame* game, int32_t node_index)
{
    uint64_t acc = 0;
    if( !game || !game->ui_root_buffer || !game->rs_component_state )
        return 0;
    int32_t p = game->ui_root_buffer->components[node_index].parent;
    while( p >= 0 )
    {
        struct StaticUIComponent* pc = &game->ui_root_buffer->components[p];
        if( pc->type == UIELEM_RS_LAYER && pc->component_id >= 0 )
        {
            struct RSLayerDynState* ls =
                rs_layer_state(game->rs_component_state, pc->component_id);
            if( ls )
                acc = fnv1a64_mix(acc, (uint64_t)ls->version);
        }
        p = pc->parent;
    }
    return acc;
}

static bool
ui_dirty_type_is_always_dirty(enum StaticUIComponentType t)
{
    switch( t )
    {
    case UIELEM_BUILTIN_WORLD:
    case UIELEM_BUILTIN_MINIMAP:
    case UIELEM_BUILTIN_COMPASS:
        return true;
    default:
        return false;
    }
}

static bool
ui_dirty_type_is_hoverable(struct GGame* game, struct StaticUIComponent const* c)
{
    (void)game;
    if( !c )
        return false;
    if( c->type == UIELEM_BUILTIN_REDSTONE_TAB )
        return true;
    if( c->type == UIELEM_RS_GRAPHIC && c->u.rs_graphic.scene_id_active >= 0 )
        return true;
    return false;
}

static uint64_t
ui_dirty_content_sig(struct GGame* game, struct StaticUIComponent* c, int32_t node_index)
{
    uint64_t h = 14695981039346656037ull;
    if( !c || !game )
        return h;

    h = fnv1a64_mix(h, (uint64_t)c->type);
    h = fnv1a64_mix(h, (uint64_t)c->position.x);
    h = fnv1a64_mix(h, (uint64_t)c->position.y);
    h = fnv1a64_mix(h, (uint64_t)c->position.width);
    h = fnv1a64_mix(h, (uint64_t)c->position.height);
    h = fnv1a64_mix(h, parent_layer_sig_xor(game, node_index));

    switch( c->type )
    {
    case UIELEM_BUILTIN_SPRITE:
        h = fnv1a64_mix(h, (uint64_t)c->u.sprite.scene_id);
        h = fnv1a64_mix(h, (uint64_t)c->u.sprite.atlas_index);
        break;
    case UIELEM_BUILTIN_REDSTONE_TAB:
        h = fnv1a64_mix(h, (uint64_t)c->u.redstone_tab.tabno);
        if( game->iface )
            h = fnv1a64_mix(h, (uint64_t)game->iface->selected_tab);
        break;
    case UIELEM_BUILTIN_MINIMAP:
        h = fnv1a64_mix(h, (uint64_t)game->camera_yaw);
        h = fnv1a64_mix(h, (uint64_t)game->camera_world_x);
        h = fnv1a64_mix(h, (uint64_t)game->camera_world_z);
        break;
    case UIELEM_BUILTIN_COMPASS:
        h = fnv1a64_mix(h, (uint64_t)game->camera_yaw);
        break;
    case UIELEM_BUILTIN_WORLD:
        h = fnv1a64_mix(h, (uint64_t)game->camera_yaw);
        h = fnv1a64_mix(h, (uint64_t)game->camera_pitch);
        h = fnv1a64_mix(h, (uint64_t)game->cc);
        break;
    case UIELEM_RS_TEXT:
    {
        h = fnv1a64_mix(h, (uint64_t)(uintptr_t)c->u.rs_text.text);
        h = fnv1a64_mix(h, (uint64_t)c->u.rs_text.font_id);
        h = fnv1a64_mix(h, (uint64_t)c->u.rs_text.color);
        h = fnv1a64_mix(h, (uint64_t)c->u.rs_text.center);
        if( c->component_id >= 0 && game->rs_component_state )
        {
            struct RSTextDynState* ts =
                rs_text_state(game->rs_component_state, c->component_id);
            if( ts )
            {
                h = fnv1a64_mix(h, (uint64_t)ts->version);
                h = fnv1a64_mix(h, (uint64_t)(uintptr_t)ts->text_override);
                h = fnv1a64_mix(h, (uint64_t)ts->active_state);
            }
        }
    }
    break;
    case UIELEM_RS_GRAPHIC:
        h = fnv1a64_mix(h, (uint64_t)c->u.rs_graphic.scene_id);
        h = fnv1a64_mix(h, (uint64_t)c->u.rs_graphic.atlas_index);
        h = fnv1a64_mix(h, (uint64_t)c->u.rs_graphic.scene_id_active);
        h = fnv1a64_mix(h, (uint64_t)c->u.rs_graphic.atlas_index_active);
        if( ui_point_in_component(c, game->mouse_x, game->mouse_y) &&
            c->u.rs_graphic.scene_id_active >= 0 )
        {
            h = fnv1a64_mix(h, 1ull); /* hover variant */
        }
        else
        {
            h = fnv1a64_mix(h, 0ull);
        }
        break;
    case UIELEM_RS_MODEL:
    {
        int eid = c->u.rs_model.scene2_element_id;
        h = fnv1a64_mix(h, (uint64_t)eid);
        if( game->world && game->world->scene2 && eid >= 0 &&
            eid < scene2_elements_total(game->world->scene2) )
        {
            struct Scene2Element* se = scene2_element_at(game->world->scene2, eid);
            if( se )
            {
                h = fnv1a64_mix(h, (uint64_t)scene2_element_active_anim_id(se));
                h = fnv1a64_mix(h, (uint64_t)scene2_element_active_frame(se));
                h = fnv1a64_mix(h, (uint64_t)scene2_element_dash_model_gpu_id(se));
            }
        }
        if( c->component_id >= 0 && game->rs_component_state )
        {
            struct RSModelDynState* ms =
                rs_model_state(game->rs_component_state, c->component_id);
            if( ms )
                h = fnv1a64_mix(h, (uint64_t)ms->version);
        }
    }
    break;
    case UIELEM_RS_INV:
    {
        int inv_i = c->u.rs_inv.inv_index;
        h = fnv1a64_mix(h, (uint64_t)inv_i);
        h = fnv1a64_mix(h, (uint64_t)c->u.rs_inv.cols);
        h = fnv1a64_mix(h, (uint64_t)c->u.rs_inv.rows);
        if( game->inv_pool && inv_i >= 0 && inv_i < game->inv_pool->count )
        {
            struct UIInventory* inv = &game->inv_pool->inventories[inv_i];
            int n = c->u.rs_inv.cols * c->u.rs_inv.rows;
            if( n > UI_INVENTORY_MAX_ITEMS )
                n = UI_INVENTORY_MAX_ITEMS;
            for( int i = 0; i < n; i++ )
                h = fnv1a64_mix(h, (uint64_t)inv->items[i].obj_id);
        }
        if( c->component_id >= 0 && game->rs_component_state )
        {
            struct RSInvDynState* iv =
                rs_inv_state(game->rs_component_state, c->component_id);
            if( iv )
                h = fnv1a64_mix(h, (uint64_t)iv->version);
        }
    }
    break;
    case UIELEM_RS_LAYER:
        if( c->component_id >= 0 && game->rs_component_state )
        {
            struct RSLayerDynState* ls =
                rs_layer_state(game->rs_component_state, c->component_id);
            if( ls )
            {
                h = fnv1a64_mix(h, (uint64_t)ls->version);
                h = fnv1a64_mix(h, ls->hidden ? 1ull : 0ull);
                h = fnv1a64_mix(h, (uint64_t)ls->scroll_position);
            }
        }
        break;
    default:
        break;
    }
    return h;
}

void
ui_runtime_ensure_capacity(struct GGame* game, uint32_t needed_count)
{
    if( !game )
        return;
    if( needed_count <= game->ui_runtime_capacity )
        return;

    uint32_t new_cap = game->ui_runtime_capacity ? game->ui_runtime_capacity : 16;
    while( new_cap < needed_count )
        new_cap *= 2;

    struct UINodeRuntime* nr =
        realloc(game->ui_runtime, (size_t)new_cap * sizeof(struct UINodeRuntime));
    if( !nr )
        return;

    for( uint32_t i = game->ui_runtime_capacity; i < new_cap; i++ )
    {
        memset(&nr[i], 0, sizeof(struct UINodeRuntime));
        nr[i].dirty_this_frame = 1;
    }

    game->ui_runtime = nr;
    game->ui_runtime_capacity = new_cap;
}

void
ui_dirty_invalidate_all(struct GGame* game)
{
    if( !game || !game->ui_root_buffer || !game->ui_runtime )
        return;
    for( uint32_t i = 0; i < game->ui_root_buffer->component_count; i++ )
        game->ui_runtime[i].dirty_this_frame = 1;
}

bool
ui_dirty_node(struct GGame* game, struct StaticUIComponent const* c)
{
    if( !game || !game->ui_runtime || !game->ui_root_buffer || !c )
        return true;
    ptrdiff_t off = c - game->ui_root_buffer->components;
    if( off < 0 )
        return true;
    uint32_t idx = (uint32_t)off;
    if( idx >= game->ui_runtime_capacity )
        return true;
    return game->ui_runtime[idx].dirty_this_frame != 0;
}

void
ui_dirty_pre_pass(struct GGame* game)
{
    if( !game || !game->ui_root_buffer )
        return;

    struct UITree* tree = game->ui_root_buffer;
    ui_runtime_ensure_capacity(game, tree->component_count);

    /* Process redstone clicks FIRST so that selected_tab is updated before the
     * global_dirty comparison below.  Otherwise the prev != current check sees the
     * old value and ui_dirty_invalidate_all() never fires on the click frame. */
    walk_redstone_clicks(game);

    bool global_dirty = false;

    if( game->ui_prev_ui_root_generation != tree->generation )
        global_dirty = true;

    if( game->view_port &&
        (game->ui_prev_view_port_w != game->view_port->width ||
         game->ui_prev_view_port_h != game->view_port->height) )
        global_dirty = true;

    if( game->iface )
    {
        if( game->ui_prev_selected_tab != game->iface->selected_tab )
            global_dirty = true;
        if( game->ui_prev_sidebar_interface_id != game->iface->sidebar_interface_id )
            global_dirty = true;
        if( game->ui_prev_viewport_interface_id != game->iface->viewport_interface_id )
            global_dirty = true;
        if( game->ui_prev_chat_interface_id != game->iface->chat_interface_id )
            global_dirty = true;
    }

    if( global_dirty )
        ui_dirty_invalidate_all(game);

    int mx = game->mouse_x;
    int my = game->mouse_y;

    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct StaticUIComponent* c = &tree->components[i];
        struct UINodeRuntime* r = &game->ui_runtime[i];

        bool ad = ui_dirty_type_is_always_dirty(c->type) || c->always_dirty;
        uint64_t sig = ui_dirty_content_sig(game, c, (int32_t)i);

        bool dirty = global_dirty || ad;
        if( !dirty && sig != r->content_sig )
            dirty = true;

        bool hv = false;
        if( ui_dirty_type_is_hoverable(game, c) )
        {
            hv = ui_point_in_component(c, mx, my);
            if( !dirty && (hv ? 1u : 0u) != (unsigned)r->prev_hovered )
                dirty = true;
        }

        r->dirty_this_frame = dirty ? 1u : 0u;
        r->always_dirty = ad ? 1u : 0u;

        if( ui_dirty_type_is_hoverable(game, c) )
            r->prev_hovered = hv ? 1u : 0u;

        r->content_sig = sig;
    }

    /* Parent dirty forces all descendants dirty (paint order). */
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        int32_t p = tree->components[i].parent;
        if( p >= 0 && (uint32_t)p < game->ui_runtime_capacity &&
            game->ui_runtime[(uint32_t)p].dirty_this_frame )
            game->ui_runtime[i].dirty_this_frame = 1;
    }

    if( game->iface )
    {
        game->ui_prev_selected_tab = game->iface->selected_tab;
        game->ui_prev_sidebar_interface_id = game->iface->sidebar_interface_id;
        game->ui_prev_viewport_interface_id = game->iface->viewport_interface_id;
        game->ui_prev_chat_interface_id = game->iface->chat_interface_id;
    }
    game->ui_prev_ui_root_generation = tree->generation;
    if( game->view_port )
    {
        game->ui_prev_view_port_w = game->view_port->width;
        game->ui_prev_view_port_h = game->view_port->height;
    }
}
