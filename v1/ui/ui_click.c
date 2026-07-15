#include "ui_click.h"

#include "games/runescape.h"
#include "input/libtorirs_input.h"
#include "osrs/client_code.h"
#include "osrs/minimenu_action.h"
#include "osrs/rscache/dat1a/dat1a_config_component.h"
#include "toriauxlib/cache/toriauxlibcache_submit.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/td/toriauxlibtd.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_scene.h"
#include "ui/minimenu_pickset.h"
#include "ui/ui_behavior.h"
#include "ui/ui_chat_minimenu.h"
#include "ui/ui_cross_cursor.h"
#include "ui/ui_debug.h"
#include "ui/ui_input.h"
#include "ui/ui_inv_selection.h"
#include "ui/ui_inv_slot_view.h"
#include "ui/ui_minimenu.h"
#include "ui/ui_scroll.h"
#include "ui/uitree_host.h"
#include "ui/uitree_layout.h"
#include "world/world.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

static bool
ui_click_component_is_menu_debug_relevant(struct StaticUIComponent const* component)
{
    return component && (uitree_component_has_menu_options(component) ||
                         uitree_component_expects_minimenu_rows(component));
}

void
uitree_inv_grid_hit_test_slot(
    struct StaticUIComponent const* component,
    int px,
    int py,
    int scroll_off_x,
    int scroll_off_y,
    int* out_slot)
{
    if( out_slot )
        *out_slot = -1;
    assert(component);
    if( component->type != UIELEM_INV_GRID )
        return;

    int bx = 0;
    int by = 0;
    int bw = 0;
    int bh = 0;
    uitree_layout_get_bounds(&component->position, &bx, &by, &bw, &bh);
    bx -= scroll_off_x;
    by -= scroll_off_y;

    struct UIInvGridLayout layout = {
        .cols = component->u.inv_grid.cols > 0 ? component->u.inv_grid.cols : 4,
        .rows = component->u.inv_grid.rows > 0 ? component->u.inv_grid.rows : 7,
        .margin_x = component->u.inv_grid.margin_x,
        .margin_y = component->u.inv_grid.margin_y,
        .offset_x = component->u.inv_grid.inv_slot_offset_x,
        .offset_y = component->u.inv_grid.inv_slot_offset_y,
    };

    int slot = ui_inv_slot_view_grid_hit_test(bx, by, &layout, px, py);
    if( out_slot )
        *out_slot = slot;
}

static void
uitree_inv_pick_at_point_recursive(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeScrollState const* scroll,
    int32_t node_index,
    int px,
    int py,
    int scroll_off_x,
    int scroll_off_y,
    struct UITreeScrollClip const* clip,
    struct UITreeInvPick* pick)
{
    assert(tree);
    if( !pick || node_index < 0 || (uint32_t)node_index >= tree->component_count )
        return;

    if( clip && clip->clip_w > 0 && clip->clip_h > 0 && !uitree_point_in_clip(px, py, clip) )
        return;

    struct StaticUIComponent const* component = &tree->components[node_index];

    if( !uitree_component_hit_test_visible_host(component, -1, host) )
        return;

    if( component->type == UIELEM_INV_GRID )
    {
        int slot = -1;
        uitree_inv_grid_hit_test_slot(component, px, py, scroll_off_x, scroll_off_y, &slot);
        if( slot >= 0 )
        {
            int obj_id = 0;
            if( host )
            {
                struct UIInvSlotData slot_data;
                struct UITreeHostRequest req = {
                    .kind = UITREE_HOST_GET_INV_SOURCE_SLOT,
                    .u.get_inv_source_slot.source_id = component->u.inv_grid.inv_source_id,
                    .u.get_inv_source_slot.slot = slot,
                    .u.get_inv_source_slot.out = &slot_data,
                };
                if( uitree_host(host, &req) )
                    obj_id = slot_data.obj_id;
            }
            pick->component_index = node_index;
            pick->inv_source_id = component->u.inv_grid.inv_source_id;
            pick->slot = slot;
            pick->obj_id = obj_id;
        }
    }

    if( component->type == UIELEM_INV_SLOT )
    {
        int bx = 0;
        int by = 0;
        int bw = 0;
        int bh = 0;
        uitree_layout_get_bounds(&component->position, &bx, &by, &bw, &bh);
        bx -= scroll_off_x;
        by -= scroll_off_y;
        if( ui_inv_slot_view_hit_test_rect(px, py, bx, by, bw, bh) )
        {
            int obj_id = 0;
            if( host )
            {
                struct UIInvSlotData slot_data;
                struct UITreeHostRequest req = {
                    .kind = UITREE_HOST_GET_INV_SOURCE_SLOT,
                    .u.get_inv_source_slot.source_id = component->u.inv_slot.inv_source_id,
                    .u.get_inv_source_slot.slot = component->u.inv_slot.slot,
                    .u.get_inv_source_slot.out = &slot_data,
                };
                if( uitree_host(host, &req) )
                    obj_id = slot_data.obj_id;
            }
            pick->component_index = node_index;
            pick->inv_source_id = component->u.inv_slot.inv_source_id;
            pick->slot = component->u.inv_slot.slot;
            pick->obj_id = obj_id;
        }
    }

    int child_scroll_x = scroll_off_x;
    int child_scroll_y = scroll_off_y;
    struct UITreeScrollClip child_clip = clip ? *clip : (struct UITreeScrollClip){ 0 };

    if( component->type == UIELEM_RS_LAYER )
    {
        int bx = 0;
        int by = 0;
        int bw = 0;
        int bh = 0;
        uitree_layout_get_bounds(&component->position, &bx, &by, &bw, &bh);
        uitree_scroll_intersect_clip(&child_clip, bx, by, bw, bh);
        if( scroll && uitree_scroll_layer_needs_horizontal(component) &&
            component->component_id >= 0 )
        {
            int sx = 0;
            uitree_scroll_get_pos(scroll, component->component_id, &sx, NULL);
            child_scroll_x += sx;
        }
        if( scroll && uitree_scroll_layer_needs_vertical(component) &&
            component->component_id >= 0 )
        {
            int sy = 0;
            uitree_scroll_get_pos(scroll, component->component_id, NULL, &sy);
            child_scroll_y += sy;
        }
    }

    bool recurse_children = true;
    if( component->type == UIELEM_BUILTIN_SIDEBAR && host )
    {
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_SELECTED_TAB };
        if( uitree_host(host, &req) != component->u.sidebar.tabno )
            recurse_children = false;
    }

    if( recurse_children )
    {
        for( int32_t child = component->first_child; child >= 0;
             child = tree->components[child].next_sibling )
        {
            uitree_inv_pick_at_point_recursive(
                tree,
                host,
                scroll,
                child,
                px,
                py,
                child_scroll_x,
                child_scroll_y,
                component->type == UIELEM_RS_LAYER ? &child_clip : clip,
                pick);
        }
    }
}

bool
uitree_inv_pick_at_point(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeScrollState const* scroll,
    int px,
    int py,
    struct UITreeInvPick* out)
{
    assert(tree);
    assert(out);

    out->component_index = -1;
    out->inv_source_id = UI_INV_SOURCE_INVALID;
    out->slot = -1;
    out->obj_id = 0;

    if( tree->root_index < 0 )
        return false;

    struct UITreeInvPick pick = {
        .component_index = -1,
        .inv_source_id = UI_INV_SOURCE_INVALID,
        .slot = -1,
        .obj_id = 0,
    };

    for( int32_t root = tree->root_index; root >= 0; root = tree->components[root].next_sibling )
    {
        uitree_inv_pick_at_point_recursive(tree, host, scroll, root, px, py, 0, 0, NULL, &pick);
    }

    if( pick.component_index < 0 || pick.slot < 0 )
        return false;

    *out = pick;
    return true;
}

static void
game_set_cross(
    struct GameRunescape* game,
    int x,
    int y,
    int mode)
{
    assert(game);

    ui_cross_cursor_show(&game->cross, (enum UICrossCursorMode)mode, x, y);
}

static void
interaction_state_set_from_minimenu_pick(
    struct InteractionState* state,
    struct MinimenuPick const* pick)
{
    assert(state);
    if( !pick )
        return;

    switch( pick->kind )
    {
    case MINIMENU_PICK_NPC:
        interaction_state_set_npc(state, pick->id);
        break;
    case MINIMENU_PICK_SCENERY:
        interaction_state_set_scenery(state, pick->id, pick->secondary_id);
        break;
    case MINIMENU_PICK_TERRAIN:
        interaction_state_set_world_tile(
            state, pick->secondary_id, pick->tertiary_id, pick->quaternary_id);
        break;
    case MINIMENU_PICK_INV_SLOT:
        interaction_state_set_inv_slot(state, pick->id, pick->secondary_id, pick->tertiary_id);
        break;
    case MINIMENU_PICK_UI:
        interaction_state_set_ui(state, pick->id);
        break;
    default:
        interaction_state_reset(state);
        break;
    }
}

static struct MinimenuPick const*
minimenu_pickset_first_actionable(struct MinimenuPickSet const* picks)
{
    if( !picks || picks->count <= 0 )
        return NULL;

    for( int i = 0; i < picks->count; i++ )
    {
        enum MinimenuPickKind kind = picks->items[i].kind;
        if( kind == MINIMENU_PICK_NPC || kind == MINIMENU_PICK_SCENERY ||
            kind == MINIMENU_PICK_INV_SLOT )
            return &picks->items[i];
    }

    for( int i = 0; i < picks->count; i++ )
    {
        if( picks->items[i].kind == MINIMENU_PICK_TERRAIN )
            return &picks->items[i];
    }

    return NULL;
}

static struct MinimenuPick const*
minimenu_pickset_first_terrain(struct MinimenuPickSet const* picks)
{
    if( !picks )
        return NULL;

    for( int i = 0; i < picks->count; i++ )
    {
        if( picks->items[i].kind == MINIMENU_PICK_TERRAIN )
            return &picks->items[i];
    }

    return NULL;
}

static int
ui_click_player_combat_level(struct GameRunescape const* game)
{
    (void)game;
    /* Until local player combat level is tracked in src2, never promote Attack. */
    return INT32_MAX;
}

static struct WorldEntity_NPC*
ui_click_npc_for_entity(
    struct GameRunescape* game,
    int entity_id)
{
    int world_index;

    assert(game);
    if( !game->world || RS_ENTITY_KIND_OF(entity_id) != RS_ENTITY_KIND_NPC )
        return NULL;
    assert(game);

    for( int i = 0; i < game->entities.count; i++ )
    {
        if( game->entities.records[i].entity_id != entity_id )
            continue;
        world_index = game->entities.records[i].world_index;
        if( !World_EntityPoolIsActive(&game->world->entities.npc, world_index) )
            return NULL;
        return World_EntityPoolGet(&game->world->entities.npc, world_index);
    }

    return NULL;
}

static void
ui_minimenu_sort_priority_actions(struct UIMinimenuState* menu)
{
    assert(menu);
    if( menu->option_count < 2 )
        return;

    bool sorted = false;
    while( !sorted )
    {
        sorted = true;
        for( int i = 0; i < menu->option_count - 1; i++ )
        {
            if( menu->options[i].action < 1000 && menu->options[i + 1].action > 1000 )
            {
                struct UIMinimenuOption tmp = menu->options[i];
                menu->options[i] = menu->options[i + 1];
                menu->options[i + 1] = tmp;
                sorted = false;
            }
        }
    }
}

static void
ui_click_copy_resolved_actions(
    char out[TORIAUXLIBCORE_MENU_ACTION_SLOTS][TORIAUXLIBCORE_MENU_ACTION_LEN],
    char const src[TORIAUXLIBCORE_MENU_ACTION_SLOTS][TORIAUXLIBCORE_MENU_ACTION_LEN])
{
    for( int i = 0; i < TORIAUXLIBCORE_MENU_ACTION_SLOTS; i++ )
    {
        out[i][0] = '\0';
        if( src[i][0] != '\0' )
        {
            strncpy(out[i], src[i], TORIAUXLIBCORE_MENU_ACTION_LEN - 1);
            out[i][TORIAUXLIBCORE_MENU_ACTION_LEN - 1] = '\0';
        }
    }
}

static bool
ui_click_resolved_actions_nonempty(
    char const actions[TORIAUXLIBCORE_MENU_ACTION_SLOTS][TORIAUXLIBCORE_MENU_ACTION_LEN])
{
    for( int i = 0; i < TORIAUXLIBCORE_MENU_ACTION_SLOTS; i++ )
    {
        if( actions[i][0] != '\0' )
            return true;
    }
    return false;
}

static void
ui_click_resolve_npc_actions(
    struct GameRunescape* game,
    struct WorldEntity_NPC* npc,
    char out[TORIAUXLIBCORE_MENU_ACTION_SLOTS][TORIAUXLIBCORE_MENU_ACTION_LEN])
{
    for( int i = 0; i < TORIAUXLIBCORE_MENU_ACTION_SLOTS; i++ )
        out[i][0] = '\0';

    if( !npc )
        return;

    if( game && npc->npc_id >= 0 )
    {
        struct ToriAuxLibCore* core = game->core;

        if( game->td )
        {
            struct ToriAuxLibCache* cache = ToriAuxLibTD_C(game->td);
            ToriAuxLibCache_EnsureNpctype(cache, npc->npc_id);
            if( !core )
                core = ToriAuxLibCache_Core(cache);
        }

        struct ToriAuxLibCore_Npctype* npctype =
            core ? ToriAuxLibCore_NpctypeGet(core, npc->npc_id) : NULL;
        if( npctype )
        {
            ui_click_copy_resolved_actions(out, npctype->actions);
            if( ui_click_resolved_actions_nonempty(out) )
                return;
        }
    }

    for( int i = 0; i < TORIAUXLIBCORE_MENU_ACTION_SLOTS; i++ )
    {
        if( npc->actions[i].name[0] != '\0' )
        {
            strncpy(out[i], npc->actions[i].name, TORIAUXLIBCORE_MENU_ACTION_LEN - 1);
            out[i][TORIAUXLIBCORE_MENU_ACTION_LEN - 1] = '\0';
        }
    }
}

static void
ui_click_resolve_loc_actions(
    struct GameRunescape* game,
    int loc_id,
    struct WorldEntity_Scenery* scenery,
    char out[TORIAUXLIBCORE_MENU_ACTION_SLOTS][TORIAUXLIBCORE_MENU_ACTION_LEN])
{
    for( int i = 0; i < TORIAUXLIBCORE_MENU_ACTION_SLOTS; i++ )
        out[i][0] = '\0';

    if( loc_id <= 0 && scenery )
        loc_id = scenery->loc_id;

    if( game && game->core && loc_id > 0 )
    {
        struct ToriAuxLibCore_Location* loc = ToriAuxLibCore_LocationGet(game->core, loc_id);
        if( loc )
        {
            ui_click_copy_resolved_actions(out, loc->actions);
            if( ui_click_resolved_actions_nonempty(out) )
                return;
        }
    }

    if( scenery )
    {
        for( int i = 0; i < TORIAUXLIBCORE_MENU_ACTION_SLOTS; i++ )
        {
            if( scenery->actions[i].name[0] != '\0' )
            {
                strncpy(out[i], scenery->actions[i].name, TORIAUXLIBCORE_MENU_ACTION_LEN - 1);
                out[i][TORIAUXLIBCORE_MENU_ACTION_LEN - 1] = '\0';
            }
        }
    }
}

static void
ui_click_ensure_objtype(
    struct GameRunescape* game,
    int obj_id)
{
    assert(game);
    if( !game->td || obj_id <= 0 )
        return;
    ToriAuxLibCache_EnsureObjtype(ToriAuxLibTD_C(game->td), obj_id);
}

static void
ui_click_add_npc_options(
    struct GameRunescape* game,
    struct MinimenuPick const* pick,
    struct UIMinimenuState* menu)
{
    struct WorldEntity_NPC* npc;
    char text[UI_MINIMENU_OPTION_LEN];
    char tooltip[64];
    char actions[TORIAUXLIBCORE_MENU_ACTION_SLOTS][TORIAUXLIBCORE_MENU_ACTION_LEN];
    int player_level;

    assert(game);
    assert(menu);
    if( !pick )
        return;

    npc = ui_click_npc_for_entity(game, pick->id);
    if( !npc )
        return;

    ui_click_resolve_npc_actions(game, npc, actions);

    player_level = ui_click_player_combat_level(game);
    snprintf(tooltip, sizeof(tooltip), "%s", npc->name[0] ? npc->name : "NPC");
    if( npc->combat_level > 0 )
    {
        snprintf(
            tooltip,
            sizeof(tooltip),
            "%s (level-%d)",
            npc->name[0] ? npc->name : "NPC",
            npc->combat_level);
    }

    for( int i = 4; i >= 0; i-- )
    {
        if( actions[i][0] == '\0' )
            continue;
        if( strcasecmp(actions[i], "attack") == 0 )
            continue;

        snprintf(text, sizeof(text), "%s @yel@ %s", actions[i], tooltip);
        ui_minimenu_add_option_with_pick(
            menu,
            text,
            (enum MinimenuAction)(MINIMENU_ACTION_OPNPC1 + i),
            i,
            pick->kind,
            pick->id,
            pick->secondary_id,
            pick->tertiary_id,
            pick->quaternary_id);
    }

    for( int i = 4; i >= 0; i-- )
    {
        if( actions[i][0] == '\0' )
            continue;
        if( strcasecmp(actions[i], "attack") != 0 )
            continue;

        int const priority = player_level < npc->combat_level ? MINIMENU_ACTION_PRIORITY : 0;
        snprintf(text, sizeof(text), "%s @yel@ %s", actions[i], tooltip);
        ui_minimenu_add_option_with_pick(
            menu,
            text,
            (enum MinimenuAction)minimenu_action_priority(
                (enum MinimenuAction)(MINIMENU_ACTION_OPNPC1 + i), priority),
            i,
            pick->kind,
            pick->id,
            pick->secondary_id,
            pick->tertiary_id,
            pick->quaternary_id);
    }

    snprintf(text, sizeof(text), "Examine @yel@ %s", tooltip);
    ui_minimenu_add_option_with_pick(
        menu,
        text,
        MINIMENU_ACTION_OPNPC6,
        0,
        pick->kind,
        pick->id,
        pick->secondary_id,
        pick->tertiary_id,
        pick->quaternary_id);
}

static void
ui_click_add_scenery_options(
    struct GameRunescape* game,
    struct MinimenuPick const* pick,
    struct UIMinimenuState* menu)
{
    struct WorldEntity_Scenery* scenery;
    char text[UI_MINIMENU_OPTION_LEN];
    char actions[TORIAUXLIBCORE_MENU_ACTION_SLOTS][TORIAUXLIBCORE_MENU_ACTION_LEN];

    assert(game);
    assert(menu);
    if( !pick || !game->world )
        return;

    scenery = world_scenery_get_by_element_id(game->world, pick->id);
    if( !scenery )
        return;

    ui_click_resolve_loc_actions(game, pick->secondary_id, scenery, actions);

    int const rows_before = menu->option_count;
    for( int i = 4; i >= 0; i-- )
    {
        if( actions[i][0] == '\0' )
            continue;

        snprintf(
            text,
            sizeof(text),
            "%s @cya@ %s",
            actions[i],
            scenery->name[0] ? scenery->name : "Scenery");
        ui_minimenu_add_option_with_pick(
            menu,
            text,
            (enum MinimenuAction)(MINIMENU_ACTION_OPLOC1 + i),
            i,
            pick->kind,
            pick->id,
            pick->secondary_id,
            pick->tertiary_id,
            pick->quaternary_id);
    }

    if( ui_minimenu_debug_enabled() && menu->option_count == rows_before )
    {
        ui_minimenu_debug_log(
            "scenery_minimenu no action rows element_id=%d loc_id=%d scenery_loc_id=%d core=%p",
            pick->id,
            pick->secondary_id,
            scenery->loc_id,
            (void*)game->core);
    }

    snprintf(text, sizeof(text), "Examine @cya@ %s", scenery->name[0] ? scenery->name : "Scenery");
    ui_minimenu_add_option_with_pick(
        menu,
        text,
        MINIMENU_ACTION_OPLOC6,
        0,
        pick->kind,
        pick->id,
        pick->secondary_id,
        pick->tertiary_id,
        pick->quaternary_id);
}

static void
ui_click_format_inv_item_option(
    char* out,
    size_t out_size,
    char const* verb,
    char const* obj_name)
{
    snprintf(out, out_size, "%s @lre@ %s", verb, obj_name);
}

static void
ui_click_add_inv_option(
    struct UIMinimenuState* menu,
    struct MinimenuPick const* pick,
    char const* text,
    enum MinimenuAction action,
    int action_index)
{
    ui_minimenu_add_option_with_pick(
        menu,
        text,
        action,
        action_index,
        pick->kind,
        pick->id,
        pick->secondary_id,
        pick->tertiary_id,
        pick->quaternary_id);
}

static int
ui_click_add_menu_ops_rows(
    struct UIMinimenuState* menu,
    struct StaticUIMenuOptions const* opts,
    enum MinimenuPickKind pick_kind,
    int pick_id,
    int pick_secondary_id,
    int pick_tertiary_id,
    int pick_quaternary_id,
    char const* inv_obj_name)
{
    assert(menu);
    if( !opts )
        return 0;

    int const before = menu->option_count;
    char text[UI_MINIMENU_OPTION_LEN];

    for( int i = 4; i >= 0; i-- )
    {
        if( opts->ops[i][0] == '\0' )
            continue;

        if( inv_obj_name && inv_obj_name[0] != '\0' )
            ui_click_format_inv_item_option(text, sizeof(text), opts->ops[i], inv_obj_name);
        else
            snprintf(text, sizeof(text), "%s", opts->ops[i]);
        enum MinimenuAction action = (enum MinimenuAction)(MINIMENU_ACTION_INV_BUTTON1 + i);
        if( opts->op_actions[i] != 0 )
            action = (enum MinimenuAction)opts->op_actions[i];
        ui_minimenu_add_option_with_pick(
            menu,
            text,
            action,
            i,
            pick_kind,
            pick_id,
            pick_secondary_id,
            pick_tertiary_id,
            pick_quaternary_id);
    }

    return menu->option_count - before;
}

static void
ui_click_add_inv_obj_options(
    struct MinimenuPick const* pick,
    struct UIMinimenuState* menu,
    struct ToriAuxLibCore_Objtype const* obj)
{
    char text[UI_MINIMENU_OPTION_LEN];
    char const* obj_name = (obj && obj->name[0] != '\0') ? obj->name : "item";

    for( int op = 4; op >= 3; op-- )
    {
        if( obj && obj->inv_actions[op][0] != '\0' )
        {
            ui_click_format_inv_item_option(text, sizeof(text), obj->inv_actions[op], obj_name);
            ui_click_add_inv_option(
                menu, pick, text, (enum MinimenuAction)(MINIMENU_ACTION_OPHELD1 + op), op);
        }
        else if( op == 4 )
        {
            ui_click_format_inv_item_option(text, sizeof(text), "Drop", obj_name);
            ui_click_add_inv_option(menu, pick, text, MINIMENU_ACTION_OPHELD5, 4);
        }
    }

    ui_click_format_inv_item_option(text, sizeof(text), "Use", obj_name);
    ui_click_add_inv_option(menu, pick, text, MINIMENU_ACTION_OPHELDT_START, 0);

    for( int op = 2; op >= 0; op-- )
    {
        if( obj && obj->inv_actions[op][0] != '\0' )
        {
            ui_click_format_inv_item_option(text, sizeof(text), obj->inv_actions[op], obj_name);
            ui_click_add_inv_option(
                menu, pick, text, (enum MinimenuAction)(MINIMENU_ACTION_OPHELD1 + op), op);
        }
    }

    ui_click_format_inv_item_option(text, sizeof(text), "Examine", obj_name);
    ui_click_add_inv_option(menu, pick, text, MINIMENU_ACTION_OPHELD6, 0);
}

static int
ui_click_add_inv_slot_options(
    struct GameRunescape* game,
    struct MinimenuPick const* pick,
    struct UIMinimenuState* menu)
{
    assert(game && pick && menu && game->ui_tree);
    assert(
        pick->quaternary_id >= 0 &&
        (uint32_t)pick->quaternary_id < game->ui_tree->component_count &&
        "inv slot pick missing RS_INV component index in quaternary_id");

    int obj_id = pick->tertiary_id;
    if( obj_id <= 0 )
        return 0;

    int const before = menu->option_count;
    struct StaticUIComponent const* component = &game->ui_tree->components[pick->quaternary_id];
    ui_click_ensure_objtype(game, obj_id);
    struct ToriAuxLibCore_Objtype* obj =
        game->core ? ToriAuxLibCore_ObjtypeGet(game->core, obj_id) : NULL;

    ui_click_add_inv_obj_options(pick, menu, obj);
    char const* obj_name = (obj && obj->name[0] != '\0') ? obj->name : "item";
    ui_click_add_menu_ops_rows(
        menu,
        &component->menu_options,
        pick->kind,
        pick->id,
        pick->secondary_id,
        pick->tertiary_id,
        pick->quaternary_id,
        obj_name);

    int const added = menu->option_count - before;
    ui_minimenu_debug_log_inv_slot_ops("inv_right_click", pick, component, obj, added, 0, -1);
    return added;
}

static enum MinimenuAction
ui_click_if_button_action_for_type(int button_type)
{
    switch( button_type )
    {
    case COMPONENT_BUTTON_TYPE_TOGGLE:
        return MINIMENU_ACTION_IF_BUTTON_TOGGLE;
    case COMPONENT_BUTTON_TYPE_SELECT:
        return MINIMENU_ACTION_IF_BUTTON_SELECT;
    case COMPONENT_BUTTON_TYPE_CONTINUE:
        return MINIMENU_ACTION_RESUME_PAUSEBUTTON;
    case COMPONENT_BUTTON_TYPE_CLOSE:
        return MINIMENU_ACTION_CLOSE_MODAL;
    case COMPONENT_BUTTON_TYPE_OK:
    default:
        return MINIMENU_ACTION_IF_BUTTON;
    }
}

static bool
ui_click_add_social_options(
    struct GameRunescape* game,
    struct StaticUIComponent const* component,
    int32_t component_index,
    struct UIMinimenuState* menu)
{
    assert(game);
    assert(component);
    assert(menu);

    int client_code = component->behavior.client_code;
    if( client_code_is_friend_list_entry(client_code) )
    {
        int slot = client_code_friend_slot_index(client_code);
        if( slot < 0 || slot >= game->chat.friend_count )
            return false;

        char const* username = game->chat.friend_username[slot];
        char text[UI_MINIMENU_OPTION_LEN];

        snprintf(text, sizeof(text), "Remove @whi@%s", username);
        ui_minimenu_add_option_with_pick(
            menu,
            text,
            MINIMENU_ACTION_FRIENDLIST_DEL,
            slot,
            MINIMENU_PICK_UI,
            component_index,
            0,
            0,
            0);

        snprintf(text, sizeof(text), "Message @whi@%s", username);
        ui_minimenu_add_option_with_pick(
            menu,
            text,
            MINIMENU_ACTION_MESSAGE_PRIVATE,
            slot,
            MINIMENU_PICK_UI,
            component_index,
            0,
            0,
            0);
        return true;
    }

    if( client_code_is_ignore_list_entry(client_code) )
    {
        char const* label = NULL;
        if( component->type == UIELEM_RS_TEXT && component->u.rs_text.text )
            label = component->u.rs_text.text;
        if( !label || label[0] == '\0' )
            return false;

        char text[UI_MINIMENU_OPTION_LEN];
        snprintf(text, sizeof(text), "Remove @whi@%s", label);
        ui_minimenu_add_option_with_pick(
            menu,
            text,
            MINIMENU_ACTION_IGNORELIST_DEL,
            client_code_ignore_slot_index(client_code),
            MINIMENU_PICK_UI,
            component_index,
            0,
            0,
            0);
        return true;
    }

    return false;
}

static int
ui_click_add_component_menu_rows(
    struct GameRunescape* game,
    struct StaticUIComponent const* component,
    int32_t component_index,
    struct UIMinimenuState* menu)
{
    assert(component && menu);
    assert(component_index >= 0);

    int const before = menu->option_count;

    if( ui_click_add_social_options(game, component, component_index, menu) )
        return menu->option_count - before;

    struct StaticUIMenuOptions const* opts = &component->menu_options;
    char text[UI_MINIMENU_OPTION_LEN];

    ui_click_add_menu_ops_rows(menu, opts, MINIMENU_PICK_UI, component_index, 0, 0, 0, NULL);

    char const* label = NULL;
    if( opts->option[0] != '\0' )
        label = opts->option;
    else if( component->behavior.button_type == COMPONENT_BUTTON_TYPE_CLOSE )
        label = "Close";
    else if(
        component->behavior.button_type == COMPONENT_BUTTON_TYPE_OK &&
        component->behavior.client_code == 0 )
        label = "Ok";
    else if(
        component->behavior.button_type == COMPONENT_BUTTON_TYPE_TOGGLE ||
        component->behavior.button_type == COMPONENT_BUTTON_TYPE_SELECT ||
        component->behavior.button_type == COMPONENT_BUTTON_TYPE_CONTINUE )
    {
        assert(
            opts->option[0] != '\0' &&
            "toggle/select/continue button missing option label in menu_options");
    }

    if( label )
    {
        snprintf(text, sizeof(text), "%s", label);
        enum MinimenuAction action =
            opts->option_action != 0
                ? (enum MinimenuAction)opts->option_action
                : ui_click_if_button_action_for_type(component->behavior.button_type);
        ui_minimenu_add_option_with_pick(
            menu, text, action, 0, MINIMENU_PICK_UI, component_index, 0, 0, 0);
    }

    int const added = menu->option_count - before;
    if( uitree_component_expects_minimenu_rows(component) )
    {
        assert(
            added > 0 &&
            "component expected minimenu rows but ui_click_add_component_menu_rows added none");
    }

    if( ui_minimenu_debug_enabled() &&
        (ui_click_component_is_menu_debug_relevant(component) || added > 0) )
    {
        char const* implicit = "none";
        if( opts->option[0] != '\0' )
            implicit = "option";
        else if( component->behavior.button_type == COMPONENT_BUTTON_TYPE_CLOSE )
            implicit = "Close";
        else if(
            component->behavior.button_type == COMPONENT_BUTTON_TYPE_OK &&
            component->behavior.client_code == 0 )
            implicit = "Ok";

        ui_minimenu_debug_log_menu_options(
            "add_rows",
            component->component_id,
            component_index,
            (int)component->type,
            opts,
            component->behavior.button_type,
            component->behavior.client_code);
        ui_minimenu_debug_log(
            "add_rows node_idx=%d implicit_label=%s added=%d", component_index, implicit, added);
    }

    return added;
}

static int32_t
ui_click_find_interface_root(
    struct UITree const* tree,
    struct UITreeHost const* host,
    int32_t hit_idx)
{
    assert(tree);
    if( hit_idx < 0 || (uint32_t)hit_idx >= tree->component_count )
        return -1;

    int32_t idx = hit_idx;
    while( idx >= 0 )
    {
        struct StaticUIComponent const* component = &tree->components[idx];
        if( component->type == UIELEM_BUILTIN_SIDEBAR )
        {
            struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_SELECTED_TAB };
            if( host && uitree_host(host, &req) != component->u.sidebar.tabno )
                return -1;
            return component->first_child >= 0 ? component->first_child : hit_idx;
        }
        if( component->parent < 0 )
            break;
        idx = component->parent;
    }

    return hit_idx;
}

static void
ui_click_add_rs_interface_options_recursive(
    struct GameRunescape* game,
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeScrollState const* scroll,
    int32_t node_idx,
    int px,
    int py,
    int scroll_off_x,
    int scroll_off_y,
    struct UITreeScrollClip const* clip,
    struct UIMinimenuState* menu)
{
    assert(tree);
    assert(menu);
    if( node_idx < 0 || (uint32_t)node_idx >= tree->component_count )
        return;

    if( clip && clip->clip_w > 0 && clip->clip_h > 0 && !uitree_point_in_clip(px, py, clip) )
        return;

    struct StaticUIComponent const* component = &tree->components[node_idx];
    bool const debug_relevant = ui_click_component_is_menu_debug_relevant(component);

    if( !uitree_component_hit_test_visible_host(component, -1, host) )
    {
        if( debug_relevant )
        {
            ui_minimenu_debug_log(
                "skip invisible node_idx=%d rs_id=%d type=%s",
                node_idx,
                component->component_id,
                ui_minimenu_debug_uielem_name((int)component->type));
        }
        return;
    }

    if( component->type == UIELEM_RS_LAYER || component->type == UIELEM_BUILTIN_SIDEBAR )
    {
        bool recurse_children = true;
        if( component->type == UIELEM_BUILTIN_SIDEBAR && host )
        {
            struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_SELECTED_TAB };
            int selected_tab = uitree_host(host, &req);
            if( selected_tab != component->u.sidebar.tabno )
            {
                recurse_children = false;
                if( ui_minimenu_debug_enabled() )
                {
                    ui_minimenu_debug_log(
                        "skip sidebar wrong tab node_idx=%d tabno=%d selected=%d",
                        node_idx,
                        component->u.sidebar.tabno,
                        selected_tab);
                }
            }
        }

        if( recurse_children )
        {
            int child_scroll_x = scroll_off_x;
            int child_scroll_y = scroll_off_y;
            struct UITreeScrollClip child_clip = clip ? *clip : (struct UITreeScrollClip){ 0 };

            if( component->type == UIELEM_RS_LAYER )
            {
                int bx = 0;
                int by = 0;
                int bw = 0;
                int bh = 0;
                uitree_layout_get_bounds(&component->position, &bx, &by, &bw, &bh);
                uitree_scroll_intersect_clip(&child_clip, bx, by, bw, bh);
                if( scroll && uitree_scroll_layer_needs_horizontal(component) &&
                    component->component_id >= 0 )
                {
                    int sx = 0;
                    uitree_scroll_get_pos(scroll, component->component_id, &sx, NULL);
                    child_scroll_x += sx;
                }
                if( scroll && uitree_scroll_layer_needs_vertical(component) &&
                    component->component_id >= 0 )
                {
                    int sy = 0;
                    uitree_scroll_get_pos(scroll, component->component_id, NULL, &sy);
                    child_scroll_y += sy;
                }
            }

            for( int32_t child = component->first_child; child >= 0;
                 child = tree->components[child].next_sibling )
            {
                ui_click_add_rs_interface_options_recursive(
                    game,
                    tree,
                    host,
                    scroll,
                    child,
                    px,
                    py,
                    child_scroll_x,
                    child_scroll_y,
                    component->type == UIELEM_RS_LAYER ? &child_clip : clip,
                    menu);
            }
        }
        return;
    }

    if( component->type == UIELEM_INV_GRID )
    {
        if( debug_relevant )
        {
            ui_minimenu_debug_log(
                "skip inv node_idx=%d rs_id=%d (item rows use inv path)",
                node_idx,
                component->component_id);
        }
        return;
    }

    int bx = 0;
    int by = 0;
    int bw = 0;
    int bh = 0;
    uitree_layout_get_bounds(&component->position, &bx, &by, &bw, &bh);
    if( !uitree_point_in_scrolled_bounds(px, py, bx, by, bw, bh, scroll_off_x, scroll_off_y) )
    {
        if( debug_relevant )
        {
            ui_minimenu_debug_log(
                "skip out of bounds node_idx=%d rs_id=%d point=(%d,%d) bounds=(%d,%d,%d,%d) "
                "scroll=(%d,%d)",
                node_idx,
                component->component_id,
                px,
                py,
                bx,
                by,
                bw,
                bh,
                scroll_off_x,
                scroll_off_y);
        }
        return;
    }

    if( debug_relevant )
    {
        ui_minimenu_debug_log(
            "add rows node_idx=%d rs_id=%d type=%s point=(%d,%d)",
            node_idx,
            component->component_id,
            ui_minimenu_debug_uielem_name((int)component->type),
            px,
            py);
    }

    ui_click_add_component_menu_rows(game, component, node_idx, menu);
}

static bool
ui_click_point_in_chat_main_lines(
    struct GameRunescape* game,
    int px,
    int py)
{
    assert(game);
    assert(game);

    int32_t chat_idx = game->ui_hover.chat_node;
    if( chat_idx < 0 || (uint32_t)chat_idx >= game->ui_tree->component_count ||
        game->ui_tree->components[chat_idx].type != UIELEM_BUILTIN_CHAT )
        chat_idx = uitree_find_chat_builtin_node(game->ui_tree);
    if( chat_idx < 0 )
        return false;

    struct StaticUIComponent const* chat = &game->ui_tree->components[chat_idx];
    int bx = 0;
    int by = 0;
    int bw = 0;
    int bh = 0;
    uitree_layout_get_bounds(&chat->position, &bx, &by, &bw, &bh);
    if( bw <= 0 || bh <= 0 )
        return false;

    int const main_h = bh > 19 ? bh - 19 : bh;
    return px >= bx && px < bx + bw && py >= by && py < by + main_h;
}

static bool
ui_click_point_expects_ui_rows_recursive(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeScrollState const* scroll,
    int32_t node_idx,
    int px,
    int py,
    int scroll_off_x,
    int scroll_off_y,
    struct UITreeScrollClip const* clip)
{
    assert(tree);
    if( node_idx < 0 || (uint32_t)node_idx >= tree->component_count )
        return false;

    if( clip && clip->clip_w > 0 && clip->clip_h > 0 && !uitree_point_in_clip(px, py, clip) )
        return false;

    struct StaticUIComponent const* component = &tree->components[node_idx];
    if( !uitree_component_hit_test_visible_host(component, -1, host) )
        return false;

    if( component->type == UIELEM_RS_LAYER || component->type == UIELEM_BUILTIN_SIDEBAR )
    {
        bool recurse_children = true;
        if( component->type == UIELEM_BUILTIN_SIDEBAR && host )
        {
            struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_SELECTED_TAB };
            if( uitree_host(host, &req) != component->u.sidebar.tabno )
                recurse_children = false;
        }

        if( recurse_children )
        {
            int child_scroll_x = scroll_off_x;
            int child_scroll_y = scroll_off_y;
            struct UITreeScrollClip child_clip = clip ? *clip : (struct UITreeScrollClip){ 0 };

            if( component->type == UIELEM_RS_LAYER )
            {
                int bx = 0;
                int by = 0;
                int bw = 0;
                int bh = 0;
                uitree_layout_get_bounds(&component->position, &bx, &by, &bw, &bh);
                uitree_scroll_intersect_clip(&child_clip, bx, by, bw, bh);
                if( scroll && uitree_scroll_layer_needs_horizontal(component) &&
                    component->component_id >= 0 )
                {
                    int sx = 0;
                    uitree_scroll_get_pos(scroll, component->component_id, &sx, NULL);
                    child_scroll_x += sx;
                }
                if( scroll && uitree_scroll_layer_needs_vertical(component) &&
                    component->component_id >= 0 )
                {
                    int sy = 0;
                    uitree_scroll_get_pos(scroll, component->component_id, NULL, &sy);
                    child_scroll_y += sy;
                }
            }

            for( int32_t child = component->first_child; child >= 0;
                 child = tree->components[child].next_sibling )
            {
                if( ui_click_point_expects_ui_rows_recursive(
                        tree,
                        host,
                        scroll,
                        child,
                        px,
                        py,
                        child_scroll_x,
                        child_scroll_y,
                        component->type == UIELEM_RS_LAYER ? &child_clip : clip) )
                    return true;
            }
        }
        return false;
    }

    if( component->type == UIELEM_INV_GRID )
        return false;

    int bx = 0;
    int by = 0;
    int bw = 0;
    int bh = 0;
    uitree_layout_get_bounds(&component->position, &bx, &by, &bw, &bh);
    if( !uitree_point_in_scrolled_bounds(px, py, bx, by, bw, bh, scroll_off_x, scroll_off_y) )
        return false;

    return uitree_component_expects_minimenu_rows(component);
}

void
ui_click_build_ui_minimenu_at_point(
    struct GameRunescape* game,
    int click_x,
    int click_y,
    int32_t hit_idx,
    struct UIMinimenuState* menu)
{
    assert(game && menu && game->ui_tree);

    ui_minimenu_reset(menu);
    ui_minimenu_add_option(menu, "Cancel", MINIMENU_ACTION_CANCEL, -1);

    ui_chat_minimenu_add_private_strip(game, click_x, click_y, menu);

    int32_t root = ui_click_find_interface_root(game->ui_tree, &game->ui_host, hit_idx);
    if( root < 0 )
        root = hit_idx;

    int const rows_before_rs = menu->option_count;
    struct UITreeScrollState scroll = {
        .scroll_x = game->ui_scroll.scroll_x,
        .scroll_y = game->ui_scroll.scroll_y,
    };
    ui_click_add_rs_interface_options_recursive(
        game, game->ui_tree, &game->ui_host, &scroll, root, click_x, click_y, 0, 0, NULL, menu);

    if( rows_before_rs == menu->option_count &&
        ui_click_point_in_chat_main_lines(game, click_x, click_y) )
    {
        ui_chat_minimenu_add_main_box(game, click_x, click_y, NULL, menu);
    }

    if( ui_click_point_expects_ui_rows_recursive(
            game->ui_tree, &game->ui_host, &scroll, root, click_x, click_y, 0, 0, NULL) )
    {
        assert(
            menu->option_count > 1 && "UI point expected minimenu rows but only Cancel was built");
    }

    ui_minimenu_sort_priority_actions(menu);

    if( ui_minimenu_debug_enabled() )
    {
        ui_minimenu_debug_log(
            "build_ui_minimenu_at_point click=(%d,%d) hit_idx=%d root=%d option_count=%d",
            click_x,
            click_y,
            hit_idx,
            root,
            menu->option_count);
        for( int i = 0; i < menu->option_count; i++ )
        {
            ui_minimenu_debug_log("  option[%d]='%s'", i, menu->options[i].text);
        }
    }
}

static void
ui_click_add_ui_options(
    struct GameRunescape* game,
    struct MinimenuPick const* pick,
    struct UIMinimenuState* menu)
{
    assert(game && pick && menu && game->ui_tree);
    assert(pick->id >= 0 && (uint32_t)pick->id < game->ui_tree->component_count);

    if( ui_minimenu_debug_enabled() )
    {
        ui_minimenu_debug_log(
            "MINIMENU_PICK_UI component_idx=%d rs_id=%d",
            pick->id,
            game->ui_tree->components[pick->id].component_id);
    }

    ui_click_add_component_menu_rows(game, &game->ui_tree->components[pick->id], pick->id, menu);
}

static void
ui_click_add_pick_options(
    struct GameRunescape* game,
    struct MinimenuPick const* pick,
    struct UIMinimenuState* menu)
{
    assert(game);
    assert(menu);
    if( !pick )
        return;

    switch( pick->kind )
    {
    case MINIMENU_PICK_NPC:
        ui_click_add_npc_options(game, pick, menu);
        break;
    case MINIMENU_PICK_SCENERY:
        ui_click_add_scenery_options(game, pick, menu);
        break;
    case MINIMENU_PICK_INV_SLOT:
        ui_click_add_inv_slot_options(game, pick, menu);
        break;
    case MINIMENU_PICK_UI:
        ui_click_add_ui_options(game, pick, menu);
        break;
    case MINIMENU_PICK_TERRAIN:
        break;
    default:
        break;
    }
}

void
ui_click_build_minimenu_from_pickset(
    struct GameRunescape* game,
    struct MinimenuPickSet const* picks,
    bool include_walk,
    struct UIMinimenuState* menu)
{
    assert(menu);

    ui_minimenu_reset(menu);
    if( !picks )
        return;

    ui_minimenu_add_option(menu, "Cancel", MINIMENU_ACTION_CANCEL, -1);

    if( game )
        ui_chat_minimenu_add_private_strip(
            game, game->world_pick.mouse_x, game->world_pick.mouse_y, menu);

    if( include_walk )
    {
        struct MinimenuPick const* terrain = minimenu_pickset_first_terrain(picks);
        if( terrain )
        {
            ui_minimenu_add_option_with_pick(
                menu,
                "Walk here",
                MINIMENU_ACTION_WALK,
                0,
                terrain->kind,
                terrain->id,
                terrain->secondary_id,
                terrain->tertiary_id,
                terrain->quaternary_id);
        }
        else
        {
            ui_minimenu_add_option(menu, "Walk here", MINIMENU_ACTION_WALK, 0);
        }
    }

    for( int i = 0; i < picks->count; i++ )
        ui_click_add_pick_options(game, &picks->items[i], menu);

    ui_minimenu_sort_priority_actions(menu);

    if( ui_minimenu_debug_enabled() && picks && picks->count > 0 )
    {
        bool viewport = false;
        for( int i = 0; i < picks->count; i++ )
        {
            if( picks->items[i].kind == MINIMENU_PICK_NPC ||
                picks->items[i].kind == MINIMENU_PICK_SCENERY ||
                picks->items[i].kind == MINIMENU_PICK_TERRAIN )
            {
                viewport = true;
                break;
            }
        }
        if( viewport )
        {
            ui_minimenu_debug_log("viewport_minimenu option_count=%d", menu->option_count);
            for( int i = 0; i < menu->option_count; i++ )
                ui_minimenu_debug_log("  [%d]='%s'", i, menu->options[i].text);
        }
    }
}

void
ui_click_use_minimenu_option(
    struct GameRunescape* game,
    int option_index,
    int click_x,
    int click_y)
{
    assert(game);
    if( option_index < 0 || option_index >= game->minimenu.option_count )
        return;

    struct UIMinimenuOption const* opt = &game->minimenu.options[option_index];

    if( opt->pick_kind != MINIMENU_PICK_NONE )
    {
        struct MinimenuPick pick = {
            .kind = opt->pick_kind,
            .id = opt->pick_id,
            .secondary_id = opt->pick_secondary_id,
            .tertiary_id = opt->pick_tertiary_id,
            .quaternary_id = opt->pick_quaternary_id,
        };
        interaction_state_set_from_minimenu_pick(&game->interaction, &pick);
        game->click_target = game->interaction;
    }

    game->interaction.pending_action = opt->action;
    game->interaction.pending_action_index = opt->action_index;

    if( opt->action == MINIMENU_ACTION_CANCEL )
    {
        interaction_state_reset(&game->interaction);
        ui_minimenu_hide(&game->minimenu);
        game_set_cross(game, 0, 0, RUNESCAPE_CROSS_MODE_OFF);
        return;
    }

    if( opt->action == MINIMENU_ACTION_WALK )
        game_set_cross(game, click_x, click_y, RUNESCAPE_CROSS_MODE_WALK);
    else
        game_set_cross(game, click_x, click_y, RUNESCAPE_CROSS_MODE_INTERACT);

    ui_minimenu_hide(&game->minimenu);
    printf(
        "ui_click: selected action %d index %d target kind %d\n",
        (int)opt->action,
        opt->action_index,
        (int)game->interaction.kind);

    if( (opt->action == MINIMENU_ACTION_IF_BUTTON ||
         opt->action == MINIMENU_ACTION_IF_BUTTON_TOGGLE ||
         opt->action == MINIMENU_ACTION_IF_BUTTON_SELECT ||
         opt->action == MINIMENU_ACTION_RESUME_PAUSEBUTTON) &&
        game->interaction.kind == INTERACTION_TARGET_UI_COMPONENT && game->ui_tree &&
        game->interaction.ui_component_index >= 0 &&
        (uint32_t)game->interaction.ui_component_index < game->ui_tree->component_count )
    {
        struct StaticUIComponent const* component =
            &game->ui_tree->components[game->interaction.ui_component_index];
        struct UITreeHostRequest req = {
            .kind = UITREE_HOST_APPLY_BUTTON_CLICK,
            .u.apply_button_click.component = component,
        };
        uitree_host(&game->ui_host, &req);
    }

    if( opt->pick_kind == MINIMENU_PICK_INV_SLOT && game->ui_tree && opt->pick_quaternary_id >= 0 &&
        (uint32_t)opt->pick_quaternary_id < game->ui_tree->component_count )
    {
        struct MinimenuPick pick = {
            .kind = opt->pick_kind,
            .id = opt->pick_id,
            .secondary_id = opt->pick_secondary_id,
            .tertiary_id = opt->pick_tertiary_id,
            .quaternary_id = opt->pick_quaternary_id,
        };
        struct StaticUIComponent const* component =
            &game->ui_tree->components[opt->pick_quaternary_id];
        struct ToriAuxLibCore_Objtype* obj = NULL;
        if( game->core && pick.tertiary_id > 0 )
            obj = ToriAuxLibCore_ObjtypeGet(game->core, pick.tertiary_id);
        ui_minimenu_debug_log_inv_slot_ops(
            "inv_use_option", &pick, component, obj, 0, (int)opt->action, opt->action_index);
    }
}

static void
ui_click_apply_default_pick(
    struct GameRunescape* game,
    struct MinimenuPick const* pick,
    int click_x,
    int click_y)
{
    assert(game);
    if( !pick )
        return;

    interaction_state_set_from_minimenu_pick(&game->click_target, pick);
    game->interaction = game->click_target;

    switch( pick->kind )
    {
    case MINIMENU_PICK_NPC:
    case MINIMENU_PICK_SCENERY:
    case MINIMENU_PICK_INV_SLOT:
        game_set_cross(game, click_x, click_y, RUNESCAPE_CROSS_MODE_INTERACT);
        break;
    case MINIMENU_PICK_TERRAIN:
        game_set_cross(game, click_x, click_y, RUNESCAPE_CROSS_MODE_WALK);
        break;
    default:
        break;
    }
}

static bool
game_try_inv_click(
    struct GameRunescape* game,
    int click_x,
    int click_y,
    bool right_click,
    struct MinimenuPickSet* out_picks)
{
    assert(game);
    assert(game);

    struct UITreeScrollState scroll = {
        .scroll_x = game->ui_scroll.scroll_x,
        .scroll_y = game->ui_scroll.scroll_y,
    };
    struct UITreeInvPick inv_pick;
    if( !uitree_inv_pick_at_point(
            game->ui_tree, &game->ui_host, &scroll, click_x, click_y, &inv_pick) )
        return false;

    if( right_click && inv_pick.obj_id <= 0 )
        return false;

    int32_t const hit = inv_pick.component_index;
    int const inv_source_id = inv_pick.inv_source_id;
    int const slot = inv_pick.slot;
    int const obj_id = inv_pick.obj_id;
    struct StaticUIComponent* component = &game->ui_tree->components[hit];

    if( right_click )
    {
        if( out_picks )
            inv_slot_to_minimenu_pickset(inv_source_id, slot, obj_id, hit, out_picks);
        game->cross.x = click_x;
        game->cross.y = click_y;
        return true;
    }

    if( ui_minimenu_debug_enabled() )
    {
        struct MinimenuPick pick = {
            .kind = MINIMENU_PICK_INV_SLOT,
            .id = inv_source_id,
            .secondary_id = slot,
            .tertiary_id = obj_id,
            .quaternary_id = hit,
        };
        struct ToriAuxLibCore_Objtype* obj = NULL;
        if( game->core && obj_id > 0 )
            obj = ToriAuxLibCore_ObjtypeGet(game->core, obj_id);
        ui_minimenu_debug_log_inv_slot_ops("inv_left_click", &pick, component, obj, 0, 0, -1);
    }

    if( ui_inv_selection_matches(&game->inv_selection, inv_source_id, slot) )
    {
        ui_inv_selection_clear(&game->inv_selection);
    }
    else if(
        ui_inv_selection_is_set(&game->inv_selection) &&
        !ui_inv_selection_matches(&game->inv_selection, inv_source_id, slot) )
    {
        struct UIInvSlotData src_data;
        struct UIInvSlotData dst_data;
        struct UITreeHostRequest get_src = {
            .kind = UITREE_HOST_GET_INV_SOURCE_SLOT,
            .u.get_inv_source_slot.source_id = game->inv_selection.source_id,
            .u.get_inv_source_slot.slot = game->inv_selection.slot,
            .u.get_inv_source_slot.out = &src_data,
        };
        struct UITreeHostRequest get_dst = {
            .kind = UITREE_HOST_GET_INV_SOURCE_SLOT,
            .u.get_inv_source_slot.source_id = inv_source_id,
            .u.get_inv_source_slot.slot = slot,
            .u.get_inv_source_slot.out = &dst_data,
        };
        if( uitree_host(&game->ui_host, &get_src) && uitree_host(&game->ui_host, &get_dst) )
        {
            struct UITreeHostRequest set_src = {
                .kind = UITREE_HOST_SET_INV_SOURCE_SLOT,
                .u.set_inv_source_slot.source_id = game->inv_selection.source_id,
                .u.set_inv_source_slot.slot = game->inv_selection.slot,
                .u.set_inv_source_slot.data = &dst_data,
            };
            struct UITreeHostRequest set_dst = {
                .kind = UITREE_HOST_SET_INV_SOURCE_SLOT,
                .u.set_inv_source_slot.source_id = inv_source_id,
                .u.set_inv_source_slot.slot = slot,
                .u.set_inv_source_slot.data = &src_data,
            };
            uitree_host(&game->ui_host, &set_src);
            uitree_host(&game->ui_host, &set_dst);
        }
        ui_inv_selection_clear(&game->inv_selection);
    }
    else
    {
        ui_inv_selection_set(&game->inv_selection, inv_source_id, slot);
        interaction_state_set_inv_slot(&game->interaction, inv_source_id, slot, obj_id);
    }

    return true;
}

void
ui_click_handle_left(
    struct GameRunescape* game,
    struct LibToriRS_Input* input,
    int click_x,
    int click_y)
{
    (void)input;
    assert(game);
    if( !game->ui_tree || !game->ui_tree_ready )
        return;

    if( game->minimenu.visible )
    {
        int opt = ui_minimenu_hit_option(&game->minimenu, click_x, click_y);
        if( opt >= 0 )
        {
            ui_click_use_minimenu_option(game, opt, click_x, click_y);
            return;
        }
        ui_minimenu_hide(&game->minimenu);
        return;
    }

    if( game_try_inv_click(game, click_x, click_y, false, NULL) )
        return;

    int32_t clicked = GameRunescape_UIHitTest(game, click_x, click_y);
    if( clicked >= 0 )
    {
        struct StaticUIComponent* component = &game->ui_tree->components[clicked];
        if( component->type == UIELEM_BUILTIN_MINIMENU )
            return;
        uitree_behavior_handle_click_host(&game->ui_host, game->ui_tree, clicked);
        interaction_state_set_ui(&game->interaction, clicked);
        return;
    }

    if( game->world_pick.mouse_in_viewport )
    {
        struct MinimenuPickSet picks;
        GameRunescape_RefreshPicksetAtMouse(game, click_x, click_y);
        world_pickset_to_minimenu_pickset(game, &picks);
        struct MinimenuPick const* pick = minimenu_pickset_first_actionable(&picks);
        if( pick )
            ui_click_apply_default_pick(game, pick, click_x, click_y);
    }
}

static void
ui_click_show_minimenu_at(
    struct GameRunescape* game,
    int click_x,
    int click_y)
{
    struct UIMinimenuLayout layout;
    int content_width = 0;
    if( !GameRunescape_MinimenuPrepareShow(game, &layout, &content_width) )
        layout = ui_minimenu_layout_from_line_height(UI_MINIMENU_DEFAULT_LINE_HEIGHT);

    ui_minimenu_show_at(
        &game->minimenu,
        layout,
        content_width,
        click_x,
        click_y,
        game->view_port ? game->view_port->width : 765,
        game->view_port ? game->view_port->height : 503);
}

static void
ui_click_append_world_options_to_menu(
    struct GameRunescape* game,
    struct MinimenuPickSet const* picks,
    struct UIMinimenuState* menu)
{
    assert(menu);

    struct MinimenuPick const* terrain = minimenu_pickset_first_terrain(picks);
    if( terrain )
    {
        ui_minimenu_add_option_with_pick(
            menu,
            "Walk here",
            MINIMENU_ACTION_WALK,
            0,
            terrain->kind,
            terrain->id,
            terrain->secondary_id,
            terrain->tertiary_id,
            terrain->quaternary_id);
    }
    else
    {
        ui_minimenu_add_option(menu, "Walk here", MINIMENU_ACTION_WALK, 0);
    }

    assert(game);
    if( !picks )
        return;

    for( int i = 0; i < picks->count; i++ )
        ui_click_add_pick_options(game, &picks->items[i], menu);
}

static void
ui_click_add_rs_options_at_root(
    struct GameRunescape* game,
    int click_x,
    int click_y,
    int32_t root_idx,
    struct UIMinimenuState* menu)
{
    if( !game || !game->ui_tree || !menu || root_idx < 0 ||
        (uint32_t)root_idx >= game->ui_tree->component_count )
        return;

    struct UITreeScrollState scroll = {
        .scroll_x = game->ui_scroll.scroll_x,
        .scroll_y = game->ui_scroll.scroll_y,
    };
    ui_click_add_rs_interface_options_recursive(
        game, game->ui_tree, &game->ui_host, &scroll, root_idx, click_x, click_y, 0, 0, NULL, menu);
}

static void
ui_click_build_minimenu_client_ts(
    struct GameRunescape* game,
    int click_x,
    int click_y,
    struct MinimenuPickSet* picks,
    char const** out_path)
{
    bool const tree_ready = game->ui_tree && game->ui_tree_ready;
    bool const world_ready = game->world && game->world->load_complete;
    char const* path = "none";

    ui_minimenu_reset(&game->minimenu);
    ui_minimenu_add_option(&game->minimenu, "Cancel", MINIMENU_ACTION_CANCEL, -1);
    ui_chat_minimenu_add_private_strip(game, click_x, click_y, &game->minimenu);

    GameRunescape_UpdateWorldViewport(game);

    if( GameRunescape_PointInMainHoverRegion(game, click_x, click_y) )
    {
        path = "main";
        if( !tree_ready || game->ui_hover.over_main_com_id < 0 )
        {
            if( world_ready )
            {
                GameRunescape_RefreshPicksetAtMouse(game, click_x, click_y);
                world_pickset_to_minimenu_pickset(game, picks);
                ui_click_append_world_options_to_menu(game, picks, &game->minimenu);
                game->cross.x = click_x;
                game->cross.y = click_y;
            }
            else if( !tree_ready && ui_minimenu_debug_enabled() )
            {
                path = "main_not_ready";
            }
        }
        else if( tree_ready )
        {
            int32_t root =
                GameRunescape_UITreeIndexForComponentId(game, game->ui_hover.over_main_com_id);
            if( root >= 0 )
            {
                root = ui_click_find_interface_root(game->ui_tree, &game->ui_host, root);
                if( root < 0 )
                    root = GameRunescape_UITreeIndexForComponentId(
                        game, game->ui_hover.over_main_com_id);
                ui_click_add_rs_options_at_root(game, click_x, click_y, root, &game->minimenu);
            }
        }
    }
    else if( tree_ready && GameRunescape_PointInSidebarHoverRegion(click_x, click_y) )
    {
        path = "sidebar";
        int32_t sidebar_idx = GameRunescape_UISelectedSidebarIndex(game);
        if( sidebar_idx >= 0 )
        {
            int32_t root = game->ui_tree->components[sidebar_idx].first_child;
            if( root < 0 )
                root = sidebar_idx;
            else
                root = ui_click_find_interface_root(game->ui_tree, &game->ui_host, root);
            ui_click_add_rs_options_at_root(game, click_x, click_y, root, &game->minimenu);
        }
    }
    else if( tree_ready && GameRunescape_PointInChatHoverRegion(click_x, click_y) )
    {
        path = "chat";
        if( game->ui_hover.over_chat_com_id >= 0 )
        {
            int32_t root =
                GameRunescape_UITreeIndexForComponentId(game, game->ui_hover.over_chat_com_id);
            if( root >= 0 )
            {
                root = ui_click_find_interface_root(game->ui_tree, &game->ui_host, root);
                ui_click_add_rs_options_at_root(game, click_x, click_y, root, &game->minimenu);
            }
        }
        if( ui_click_point_in_chat_main_lines(game, click_x, click_y) )
            ui_chat_minimenu_add_main_box(game, click_x, click_y, NULL, &game->minimenu);
    }

    ui_minimenu_sort_priority_actions(&game->minimenu);

    if( ui_minimenu_debug_enabled() && picks && picks->count > 0 )
    {
        bool viewport = false;
        for( int i = 0; i < picks->count; i++ )
        {
            if( picks->items[i].kind == MINIMENU_PICK_NPC ||
                picks->items[i].kind == MINIMENU_PICK_SCENERY ||
                picks->items[i].kind == MINIMENU_PICK_TERRAIN )
            {
                viewport = true;
                break;
            }
        }
        if( viewport )
        {
            ui_minimenu_debug_log("viewport_minimenu option_count=%d", game->minimenu.option_count);
            for( int i = 0; i < game->minimenu.option_count; i++ )
                ui_minimenu_debug_log("  [%d]='%s'", i, game->minimenu.options[i].text);
        }
    }

    if( out_path )
        *out_path = path;
}

void
ui_click_handle_right(
    struct GameRunescape* game,
    struct LibToriRS_Input* input,
    int click_x,
    int click_y)
{
    (void)input;
    assert(game);

    bool const tree_ready = game->ui_tree && game->ui_tree_ready;
    bool const world_ready = game->world && game->world->load_complete;
    if( !tree_ready && !world_ready )
        return;

    if( tree_ready && game->minimenu.visible )
    {
        int32_t hit = GameRunescape_UIHitTest(game, click_x, click_y);
        if( hit >= 0 && game->ui_tree->components[hit].type == UIELEM_BUILTIN_MINIMENU )
        {
            ui_minimenu_hide(&game->minimenu);
            if( ui_minimenu_debug_enabled() )
            {
                ui_minimenu_debug_log("right-click (%d,%d) dismiss minimenu", click_x, click_y);
            }
        }
    }

    struct MinimenuPickSet picks;
    minimenu_pickset_reset(&picks);

    if( tree_ready && game_try_inv_click(game, click_x, click_y, true, &picks) )
    {
        if( ui_minimenu_debug_enabled() )
        {
            ui_minimenu_debug_log(
                "right-click (%d,%d) path=inv pick_count=%d", click_x, click_y, picks.count);
        }
        ui_click_build_minimenu_from_pickset(game, &picks, false, &game->minimenu);
        ui_click_show_minimenu_at(game, click_x, click_y);
        return;
    }

    char const* path = "none";
    ui_click_build_minimenu_client_ts(game, click_x, click_y, &picks, &path);

    if( ui_minimenu_debug_enabled() )
    {
        ui_minimenu_debug_log(
            "right-click (%d,%d) path=%s in_viewport=%d pick_count=%d option_count=%d",
            click_x,
            click_y,
            path,
            game->world_pick.mouse_in_viewport ? 1 : 0,
            picks.count,
            game->minimenu.option_count);
        for( int i = 0; i < picks.count; i++ )
        {
            ui_minimenu_debug_log(
                "  pick[%d] kind=%d id=%d secondary=%d",
                i,
                (int)picks.items[i].kind,
                picks.items[i].id,
                picks.items[i].secondary_id);
        }
        for( int i = 0; i < game->minimenu.option_count; i++ )
            ui_minimenu_debug_log("  option[%d]='%s'", i, game->minimenu.options[i].text);
    }

    ui_click_show_minimenu_at(game, click_x, click_y);
}
