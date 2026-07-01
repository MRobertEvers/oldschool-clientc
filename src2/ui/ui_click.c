#include "ui_click.h"

#include "games/runescape.h"
#include "input/libtorirs_input.h"
#include "osrs/minimenu_action.h"
#include "osrs/rscache/dat1a/dat1a_config_component.h"
#include "osrs/rscache/dat1a/dat1a_config_obj.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_scene.h"
#include "ui/minimenu_pickset.h"
#include "ui/uitree_host.h"
#include "ui/ui_minimenu.h"
#include "ui/uitree_layout.h"
#include "buildcache/dat1_buildcache.h"
#include "buildcache/dat2_buildcache.h"
#include "osrs/rscache/dat2a/dat2a_config_object.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/td/toriauxlibtd.h"
#include "world/world.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

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

    if( !game || !game->world || RS_ENTITY_KIND_OF(entity_id) != RS_ENTITY_KIND_NPC )
        return NULL;
    if( !game->entity_registry )
        return NULL;

    for( int i = 0; i < game->entity_registry_count; i++ )
    {
        if( game->entity_registry[i].entity_id != entity_id )
            continue;
        world_index = game->entity_registry[i].world_index;
        if( !World_EntityPoolIsActive(&game->world->entities.npc, world_index) )
            return NULL;
        return World_EntityPoolGet(&game->world->entities.npc, world_index);
    }

    return NULL;
}

static void
ui_minimenu_sort_priority_actions(struct UIMinimenuState* menu)
{
    if( !menu || menu->option_count < 2 )
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
ui_click_add_npc_options(
    struct GameRunescape* game,
    struct MinimenuPick const* pick,
    struct UIMinimenuState* menu)
{
    struct WorldEntity_NPC* npc;
    char text[UI_MINIMENU_OPTION_LEN];
    char tooltip[64];
    int player_level;

    if( !game || !pick || !menu )
        return;

    npc = ui_click_npc_for_entity(game, pick->id);
    if( !npc )
        return;

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
        if( npc->actions[i].name[0] == '\0' )
            continue;
        if( strcasecmp(npc->actions[i].name, "attack") == 0 )
            continue;

        snprintf(text, sizeof(text), "%s @yel@ %s", npc->actions[i].name, tooltip);
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
        if( npc->actions[i].name[0] == '\0' )
            continue;
        if( strcasecmp(npc->actions[i].name, "attack") != 0 )
            continue;

        int const priority = player_level < npc->combat_level ? MINIMENU_ACTION_PRIORITY : 0;
        snprintf(text, sizeof(text), "%s @yel@ %s", npc->actions[i].name, tooltip);
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
}

static void
ui_click_add_scenery_options(
    struct GameRunescape* game,
    struct MinimenuPick const* pick,
    struct UIMinimenuState* menu)
{
    struct WorldEntity_Scenery* scenery;
    char text[UI_MINIMENU_OPTION_LEN];

    if( !game || !pick || !menu || !game->world )
        return;

    scenery = world_scenery_get_by_element_id(game->world, pick->id);
    if( !scenery )
        return;

    for( int i = 4; i >= 0; i-- )
    {
        if( scenery->actions[i].name[0] == '\0' )
            continue;

        snprintf(
            text,
            sizeof(text),
            "%s @cya@ %s",
            scenery->actions[i].name,
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

    snprintf(
        text,
        sizeof(text),
        "Examine @cya@ %s",
        scenery->name[0] ? scenery->name : "Scenery");
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

static void
ui_click_add_inv_container_ops(
    struct GameRunescape* game,
    struct MinimenuPick const* pick,
    struct UIMinimenuState* menu)
{
    if( !game || !pick || !menu || !game->ui_tree )
        return;
    if( pick->quaternary_id < 0 || (uint32_t)pick->quaternary_id >= game->ui_tree->component_count )
        return;

    struct StaticUIMenuOptions const* copts =
        &game->ui_tree->components[pick->quaternary_id].menu_options;
    char text[UI_MINIMENU_OPTION_LEN];

    for( int i = 4; i >= 0; i-- )
    {
        if( copts->ops[i][0] == '\0' )
            continue;

        snprintf(text, sizeof(text), "%s", copts->ops[i]);
        ui_click_add_inv_option(
            menu,
            pick,
            text,
            (enum MinimenuAction)(MINIMENU_ACTION_INV_BUTTON1 + i),
            i);
    }
}

static void
ui_click_add_inv_obj_options_dat1(
    struct MinimenuPick const* pick,
    struct UIMinimenuState* menu,
    struct RSCacheDat1A_ConfigObj* obj)
{
    char text[UI_MINIMENU_OPTION_LEN];
    char const* obj_name = obj->name ? obj->name : "item";

    for( int op = 4; op >= 3; op-- )
    {
        if( obj->iop[op] && obj->iop[op][0] != '\0' )
        {
            snprintf(text, sizeof(text), "%s", obj->iop[op]);
            ui_click_add_inv_option(
                menu, pick, text, (enum MinimenuAction)(MINIMENU_ACTION_OPHELD1 + op), op);
        }
        else if( op == 4 )
        {
            snprintf(text, sizeof(text), "Drop @lre@ %s", obj_name);
            ui_click_add_inv_option(menu, pick, text, MINIMENU_ACTION_OPHELD5, 4);
        }
    }

    snprintf(text, sizeof(text), "Use");
    ui_click_add_inv_option(menu, pick, text, MINIMENU_ACTION_OPHELDT_START, 0);

    for( int op = 2; op >= 0; op-- )
    {
        if( obj->iop[op] && obj->iop[op][0] != '\0' )
        {
            snprintf(text, sizeof(text), "%s", obj->iop[op]);
            ui_click_add_inv_option(
                menu, pick, text, (enum MinimenuAction)(MINIMENU_ACTION_OPHELD1 + op), op);
        }
    }

    snprintf(text, sizeof(text), "Examine @cya@ %s", obj_name);
    ui_click_add_inv_option(menu, pick, text, MINIMENU_ACTION_OPHELD6, 0);
}

static void
ui_click_add_inv_obj_options_dat2(
    struct MinimenuPick const* pick,
    struct UIMinimenuState* menu,
    struct RSCacheDat2A_ConfigObject* obj)
{
    char text[UI_MINIMENU_OPTION_LEN];
    char const* obj_name = obj->name ? obj->name : "item";

    for( int op = 4; op >= 3; op-- )
    {
        if( obj->if_actions[op] && obj->if_actions[op][0] != '\0' )
        {
            snprintf(text, sizeof(text), "%s", obj->if_actions[op]);
            ui_click_add_inv_option(
                menu, pick, text, (enum MinimenuAction)(MINIMENU_ACTION_OPHELD1 + op), op);
        }
        else if( op == 4 )
        {
            snprintf(text, sizeof(text), "Drop @lre@ %s", obj_name);
            ui_click_add_inv_option(menu, pick, text, MINIMENU_ACTION_OPHELD5, 4);
        }
    }

    snprintf(text, sizeof(text), "Use");
    ui_click_add_inv_option(menu, pick, text, MINIMENU_ACTION_OPHELDT_START, 0);

    for( int op = 2; op >= 0; op-- )
    {
        if( obj->if_actions[op] && obj->if_actions[op][0] != '\0' )
        {
            snprintf(text, sizeof(text), "%s", obj->if_actions[op]);
            ui_click_add_inv_option(
                menu, pick, text, (enum MinimenuAction)(MINIMENU_ACTION_OPHELD1 + op), op);
        }
    }

    snprintf(text, sizeof(text), "Examine @cya@ %s", obj_name);
    ui_click_add_inv_option(menu, pick, text, MINIMENU_ACTION_OPHELD6, 0);
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

static void
ui_click_add_ui_options(
    struct GameRunescape* game,
    struct MinimenuPick const* pick,
    struct UIMinimenuState* menu)
{
    if( !game || !pick || !menu || !game->ui_tree )
        return;
    if( pick->id < 0 || (uint32_t)pick->id >= game->ui_tree->component_count )
        return;

    struct StaticUIComponent const* component = &game->ui_tree->components[pick->id];
    struct StaticUIMenuOptions const* opts = &component->menu_options;
    char text[UI_MINIMENU_OPTION_LEN];

    for( int i = 4; i >= 0; i-- )
    {
        if( opts->ops[i][0] == '\0' )
            continue;

        snprintf(text, sizeof(text), "%s", opts->ops[i]);
        ui_minimenu_add_option_with_pick(
            menu,
            text,
            (enum MinimenuAction)(MINIMENU_ACTION_INV_BUTTON1 + i),
            i,
            pick->kind,
            pick->id,
            pick->secondary_id,
            pick->tertiary_id,
            pick->quaternary_id);
    }

    if( opts->option[0] != '\0' )
    {
        snprintf(text, sizeof(text), "%s", opts->option);
        ui_minimenu_add_option_with_pick(
            menu,
            text,
            ui_click_if_button_action_for_type(component->behavior.button_type),
            0,
            pick->kind,
            pick->id,
            pick->secondary_id,
            pick->tertiary_id,
            pick->quaternary_id);
    }
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
        ui_click_add_npc_options(game, pick, menu);
        break;
    case MINIMENU_PICK_SCENERY:
        ui_click_add_scenery_options(game, pick, menu);
        break;
    case MINIMENU_PICK_INV_SLOT:
    {
        int obj_id = pick->tertiary_id;
        if( obj_id <= 0 )
            break;

        struct ToriAuxLibCache* cache = game->td ? ToriAuxLibTD_C(game->td) : NULL;
        if( !cache )
            break;

        if( ToriAuxLibCache_Mode(cache) == TORIAUXLIBCACHE_MODE_DAT2 )
        {
            struct RSCacheDat2A_ConfigObject* obj =
                dat2_buildcache_object_get(dat2(cache), obj_id);
            if( !obj )
                break;
            ui_click_add_inv_obj_options_dat2(pick, menu, obj);
        }
        else
        {
            struct RSCacheDat1A_ConfigObj* obj = dat1_buildcache_obj_get(dat1(cache), obj_id);
            if( !obj )
                break;
            ui_click_add_inv_obj_options_dat1(pick, menu, obj);
        }

        ui_click_add_inv_container_ops(game, pick, menu);
        break;
    }
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
    if( !menu )
        return;

    ui_minimenu_reset(menu);
    if( !picks )
        return;

    ui_minimenu_add_option(menu, "Cancel", MINIMENU_ACTION_CANCEL, -1);

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
            inv_slot_to_minimenu_pickset(inv_index, slot, obj_id, hit, out_picks);
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
        ui_click_show_minimenu_at(game, click_x, click_y);
        return;
    }

    int32_t hit = GameRunescape_UIHitTest(game, click_x, click_y);
    if( hit >= 0 )
    {
        struct StaticUIComponent* component = &game->ui_tree->components[hit];
        if( component->type == UIELEM_BUILTIN_MINIMENU )
            return;

        ui_component_to_minimenu_pickset(hit, &picks);
        ui_click_build_minimenu_from_pickset(game, &picks, false, &game->minimenu);
        ui_click_show_minimenu_at(game, click_x, click_y);
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
    ui_click_show_minimenu_at(game, click_x, click_y);
}
