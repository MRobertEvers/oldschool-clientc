#include "ui_click.h"

#include "games/runescape.h"
#include "input/libtorirs_input.h"
#include "osrs/rscache/dat1a/dat1a_config_obj.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_scene.h"
#include "ui/minimenu_pickset.h"
#include "ui/uitree_host.h"
#include "ui/uitree_layout.h"
#include "buildcache/dat1_buildcache.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/td/toriauxlibtd.h"

#include <stdio.h>
#include <string.h>

void
uitree_inv_hit_test_slot(
    struct StaticUIComponent const* component,
    int px,
    int py,
    int* out_slot)
{
    if( out_slot )
        *out_slot = -1;
    if( !component || component->type != UIELEM_RS_INV )
        return;

    int bx = 0;
    int by = 0;
    int bw = 0;
    int bh = 0;
    uitree_layout_get_bounds(&component->position, &bx, &by, &bw, &bh);

    int cols = component->u.rs_inv.cols > 0 ? component->u.rs_inv.cols : 4;
    int rows = component->u.rs_inv.rows > 0 ? component->u.rs_inv.rows : 7;
    int margin_x = component->u.rs_inv.margin_x;
    int margin_y = component->u.rs_inv.margin_y;
    int total = cols * rows;
    if( total > UI_INV_SLOT_OFFSET_MAX )
        total = UI_INV_SLOT_OFFSET_MAX;

    for( int slot = 0; slot < total; slot++ )
    {
        int col = slot % cols;
        int row = slot / cols;
        int slot_x = bx + col * (margin_x + 32);
        int slot_y = by + row * (margin_y + 32);
        if( slot < UI_INV_SLOT_OFFSET_MAX )
        {
            slot_x += component->u.rs_inv.inv_slot_offset_x[slot];
            slot_y += component->u.rs_inv.inv_slot_offset_y[slot];
        }
        if( px >= slot_x && px < slot_x + 32 && py >= slot_y && py < slot_y + 32 )
        {
            if( out_slot )
                *out_slot = slot;
            return;
        }
    }
}

static void
game_set_cross(
    struct GameRunescape* game,
    int x,
    int y,
    int mode)
{
    if( !game )
        return;
    game->cross_x = x;
    game->cross_y = y;
    game->cross_mode = mode;
    game->cross_cycle = 0;
}

static void
interaction_state_set_from_minimenu_pick(
    struct InteractionState* state,
    struct MinimenuPick const* pick)
{
    if( !state || !pick )
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

static void
ui_click_add_pick_options(
    struct GameRunescape* game,
    struct MinimenuPick const* pick,
    struct UIMinimenuState* menu)
{
    if( !game || !pick || !menu )
        return;

    switch( pick->kind )
    {
    case MINIMENU_PICK_NPC:
        ui_minimenu_add_option_with_pick(
            menu, "Attack", MINIMENU_ACTION_OPNPC1, 0, pick->kind, pick->id, 0, 0, 0);
        ui_minimenu_add_option_with_pick(
            menu, "Talk-to", MINIMENU_ACTION_OPNPC3, 0, pick->kind, pick->id, 0, 0, 0);
        break;
    case MINIMENU_PICK_SCENERY:
    {
        char examine[96];
        snprintf(examine, sizeof(examine), "Examine -> loc %d", pick->secondary_id);
        ui_minimenu_add_option_with_pick(
            menu, "Use", MINIMENU_ACTION_OPLOC1, 0, pick->kind, pick->id, pick->secondary_id, 0, 0);
        ui_minimenu_add_option_with_pick(
            menu, examine, MINIMENU_ACTION_OPLOC6, 0, pick->kind, pick->id, pick->secondary_id, 0, 0);
        break;
    }
    case MINIMENU_PICK_INV_SLOT:
    {
        int obj_id = pick->tertiary_id;
        if( obj_id > 0 )
        {
            struct Dat1BuildCache* bc =
                game->td ? dat1(ToriAuxLibTD_C(game->td)) : NULL;
            if( bc )
            {
                struct RSCacheDat1A_ConfigObj* obj = dat1_buildcache_obj_get(bc, obj_id);
                if( obj )
                {
                    for( int i = 0; i < 5; i++ )
                    {
                        if( obj->iop[i] && obj->iop[i][0] != '\0' )
                        {
                            ui_minimenu_add_option_with_pick(
                                menu,
                                obj->iop[i],
                                MINIMENU_ACTION_OPHELD1 + i,
                                i,
                                pick->kind,
                                pick->id,
                                pick->secondary_id,
                                pick->tertiary_id,
                                pick->quaternary_id);
                        }
                    }
                }
            }
        }
        char examine[96];
        snprintf(examine, sizeof(examine), "Examine -> %d", obj_id);
        ui_minimenu_add_option_with_pick(
            menu,
            examine,
            MINIMENU_ACTION_OPHELD6,
            0,
            pick->kind,
            pick->id,
            pick->secondary_id,
            pick->tertiary_id,
            pick->quaternary_id);
        break;
    }
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
    if( !menu )
        return;

    ui_minimenu_reset(menu);
    if( !picks )
        return;

    for( int i = 0; i < picks->count; i++ )
        ui_click_add_pick_options(game, &picks->items[i], menu);

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

    ui_minimenu_add_option(menu, "Cancel", MINIMENU_ACTION_CANCEL, -1);
}

void
ui_click_use_minimenu_option(
    struct GameRunescape* game,
    int option_index)
{
    if( !game || option_index < 0 || option_index >= game->minimenu.option_count )
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
        game_set_cross(game, game->cross_x, game->cross_y, RUNESCAPE_CROSS_MODE_WALK);
    else
        game_set_cross(game, game->cross_x, game->cross_y, RUNESCAPE_CROSS_MODE_INTERACT);

    ui_minimenu_hide(&game->minimenu);
    printf(
        "ui_click: selected action %d index %d target kind %d\n",
        (int)opt->action,
        opt->action_index,
        (int)game->interaction.kind);
}

static void
ui_click_apply_default_pick(
    struct GameRunescape* game,
    struct MinimenuPick const* pick,
    int click_x,
    int click_y)
{
    if( !game || !pick )
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
    if( !game || !game->ui_tree )
        return false;

    int32_t hit = GameRunescape_UIHitTest(game, click_x, click_y);
    if( hit < 0 || (uint32_t)hit >= game->ui_tree->component_count )
        return false;

    struct StaticUIComponent* component = &game->ui_tree->components[hit];
    if( component->type != UIELEM_RS_INV )
        return false;

    int slot = -1;
    uitree_inv_hit_test_slot(component, click_x, click_y, &slot);
    if( slot < 0 )
        return false;

    int inv_index = component->u.rs_inv.inv_index;
    int obj_id = 0;
    if( game->ui_inv_pool && inv_index >= 0 && inv_index < game->ui_inv_pool->count )
    {
        struct UIInventory* inv = &game->ui_inv_pool->inventories[inv_index];
        if( slot < inv->item_count )
            obj_id = inv->items[slot].obj_id;
    }

    if( right_click )
    {
        if( out_picks )
            inv_slot_to_minimenu_pickset(inv_index, slot, obj_id, out_picks);
        game->cross_x = click_x;
        game->cross_y = click_y;
        return true;
    }

    if( game->selected_inv_index == inv_index && game->selected_inv_slot == slot )
    {
        game->selected_inv_index = -1;
        game->selected_inv_slot = -1;
    }
    else if(
        game->selected_inv_index >= 0 && game->selected_inv_slot >= 0 &&
        (game->selected_inv_index != inv_index || game->selected_inv_slot != slot) &&
        game->ui_inv_pool )
    {
        struct UIInventory* src =
            &game->ui_inv_pool->inventories[game->selected_inv_index];
        struct UIInventory* dst = &game->ui_inv_pool->inventories[inv_index];
        if( game->selected_inv_slot < src->item_count && slot < dst->item_count )
        {
            struct UIInventoryItem tmp = src->items[game->selected_inv_slot];
            src->items[game->selected_inv_slot] = dst->items[slot];
            dst->items[slot] = tmp;
        }
        game->selected_inv_index = -1;
        game->selected_inv_slot = -1;
    }
    else
    {
        game->selected_inv_index = inv_index;
        game->selected_inv_slot = slot;
        interaction_state_set_inv_slot(&game->interaction, inv_index, slot, obj_id);
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
    if( !game || !game->ui_tree || !game->ui_tree_ready )
        return;

    if( game->minimenu.visible )
    {
        int opt = ui_minimenu_hit_option(&game->minimenu, click_x, click_y);
        if( opt >= 0 )
        {
            ui_click_use_minimenu_option(game, opt);
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

    if( game->mouse_in_viewport )
    {
        struct MinimenuPickSet picks;
        world_pickset_to_minimenu_pickset(game, &picks);
        struct MinimenuPick const* pick = minimenu_pickset_first_actionable(&picks);
        if( pick )
            ui_click_apply_default_pick(game, pick, click_x, click_y);
    }
}

void
ui_click_handle_right(
    struct GameRunescape* game,
    struct LibToriRS_Input* input,
    int click_x,
    int click_y)
{
    (void)input;
    if( !game || !game->ui_tree || !game->ui_tree_ready )
        return;

    struct MinimenuPickSet picks;
    minimenu_pickset_reset(&picks);

    if( game_try_inv_click(game, click_x, click_y, true, &picks) )
    {
        ui_click_build_minimenu_from_pickset(game, &picks, false, &game->minimenu);
        ui_minimenu_show_at(
            &game->minimenu,
            click_x,
            click_y,
            game->view_port ? game->view_port->width : 765,
            game->view_port ? game->view_port->height : 503);
        return;
    }

    int32_t hit = GameRunescape_UIHitTest(game, click_x, click_y);
    if( hit >= 0 )
    {
        struct StaticUIComponent* component = &game->ui_tree->components[hit];
        if( component->type == UIELEM_BUILTIN_MINIMENU )
            return;
    }

    if( game->mouse_in_viewport )
    {
        world_pickset_to_minimenu_pickset(game, &picks);
        game->cross_x = click_x;
        game->cross_y = click_y;
    }

    ui_click_build_minimenu_from_pickset(
        game, &picks, game->mouse_in_viewport, &game->minimenu);
    ui_minimenu_show_at(
        &game->minimenu,
        click_x,
        click_y,
        game->view_port ? game->view_port->width : 765,
        game->view_port ? game->view_port->height : 503);
}
