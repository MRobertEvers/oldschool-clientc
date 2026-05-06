#include "world_options.h"

#include "game.h"
#include "game_entity.h"
#include "minimenu_action.h"
#include "rscache/tables/string_utils.h"

#include <assert.h>
#include <string.h>

static void
options_add_loc(
    struct World* world,
    struct WorldOptionSet* option_set,
    int x,
    int z,
    int entity_type,
    int entity_id)
{
    assert(entity_type == ENTITY_KIND_MAP_BUILD_LOC && "Entity type must be map build loc");
    if( option_set->option_count >= WORLD_OPTION_SET_CAPACITY - 2 )
        return;
    if( entity_id < 0 || entity_id >= entity_vec_count(&world->map_build_loc_entities) )
        return;

    char text[64];

    struct MapBuildLocEntity* map_build_loc_entity = world_loc_entity(world, entity_id);
    struct WorldOption* option = NULL;

    for( int i = 4; i >= 0; i-- )
    {
        if( map_build_loc_entity->actions && map_build_loc_entity->actions[i].code != 0 )
        {
            option = &option_set->options[option_set->option_count];
            memset(option, 0, sizeof(*option));

            snprintf(
                text,
                sizeof(text),
                "%s @cya@ %s",
                map_build_loc_entity->actions[i].name,
                map_build_loc_entity->name.name);

            strncpy(option->text, text, sizeof(option->text));
            option->param_a = entity_id;
            option->param_b = x;
            option->param_c = z;

            switch( i )
            {
            case 0:
                option->action = MINIMENU_ACTION_OPLOC1;
                break;
            case 1:
                option->action = MINIMENU_ACTION_OPLOC2;
                break;
            case 2:
                option->action = MINIMENU_ACTION_OPLOC3;
                break;
            case 3:
                option->action = MINIMENU_ACTION_OPLOC4;
                break;
            case 4:
                option->action = MINIMENU_ACTION_OPLOC5;
                break;
            default:
                assert(0 && "Invalid action index");
                break;
            }

            option_set->option_count += 1;
        }
    }

    option = &option_set->options[option_set->option_count];
    memset(option, 0, sizeof(*option));

    snprintf(text, sizeof(text), "Examine @cya@ %s", map_build_loc_entity->name.name);
    strncpy(option->text, text, sizeof(option->text));
    option->action = MINIMENU_ACTION_OPLOC6;
    option->param_a = entity_id;
    option->param_b = x;
    option->param_c = z;

    option_set->option_count += 1;
}

static char const*
options_npc_combat_level_color_tag(
    int viewer_combat_level,
    int npc_combat_level)
{
    int diff = viewer_combat_level - npc_combat_level;
    if( diff < -9 )
    {
        return "@red@";
    }
    else if( diff < -6 )
    {
        return "@or3@";
    }
    else if( diff < -3 )
    {
        return "@or2@";
    }
    else if( diff < 0 )
    {
        return "@or1@";
    }
    else if( diff > 9 )
    {
        return "@gre@";
    }
    else if( diff > 6 )
    {
        return "@gr3@";
    }
    else if( diff > 3 )
    {
        return "@gr2@";
    }
    else if( diff > 0 )
    {
        return "@gr1@";
    }
    else
    {
        return "@yel@";
    }
}

static void
options_add_player(
    struct GGame* game,
    struct World* world,
    struct WorldOptionSet* option_set,
    int x,
    int z,
    int entity_id)
{
    if( !game || option_set->option_count >= WORLD_OPTION_SET_CAPACITY - 2 )
        return;

    if( entity_id == ACTIVE_PLAYER_SLOT )
        return;

    struct PlayerEntity* target = world_player(world, entity_id);
    if( !target || !target->alive )
        return;

    struct PlayerEntity* local = world_player(world, ACTIVE_PLAYER_SLOT);
    if( !local || !local->alive )
        return;

    /* Client.ts addPlayerOptions: tooltip = name + combatColourCode(...) + '(level-N)' */
    char tooltip[128];
    {
        const char* name = (target->name.name[0] != '\0') ? target->name.name : "Player";
        char const* color_tag = options_npc_combat_level_color_tag(
            local->visible_level.level, target->visible_level.level);
        snprintf(
            tooltip,
            sizeof(tooltip),
            "%s%s (level-%d)",
            name,
            color_tag,
            target->visible_level.level);
    }

    for( int i = 4; i >= 0; i-- )
    {
        const char* op = game->player_menu_op[i];
        if( op[0] == '\0' )
            continue;

        if( option_set->option_count >= WORLD_OPTION_SET_CAPACITY - 1 )
            return;

        int is_priority = (strcasecmp(op, "attack") == 0 &&
                           target->visible_level.level > local->visible_level.level) ||
                          game->player_menu_op_deprioritize[i];

        enum MinimenuAction base;
        switch( i )
        {
        case 0:
            base = MINIMENU_ACTION_OPPLAYER1;
            break;
        case 1:
            base = MINIMENU_ACTION_OPPLAYER2;
            break;
        case 2:
            base = MINIMENU_ACTION_OPPLAYER3;
            break;
        case 3:
            base = MINIMENU_ACTION_OPPLAYER4;
            break;
        case 4:
            base = MINIMENU_ACTION_OPPLAYER5;
            break;
        default:
            assert(0 && "Invalid player op slot");
            base = MINIMENU_ACTION_OPPLAYER1;
            break;
        }

        struct WorldOption* opt = &option_set->options[option_set->option_count];
        memset(opt, 0, sizeof(*opt));

        char text[64];
        snprintf(text, sizeof(text), "%s @whi@ %s", op, tooltip);
        strncpy(opt->text, text, sizeof(opt->text));
        opt->action = minimenu_action_priority(base, is_priority ? MINIMENU_ACTION_PRIORITY : 0);
        opt->param_a = entity_id;
        opt->param_b = x;
        opt->param_c = z;
        option_set->option_count++;
    }
}

static void
options_add_npc(
    struct World* world,
    struct WorldOptionSet* option_set,
    int x,
    int z,
    int entity_type,
    int entity_id)
{
    assert(entity_type == ENTITY_KIND_NPC && "Entity type must be npc");

    if( option_set->option_count >= WORLD_OPTION_SET_CAPACITY - 2 )
        return;

    char text[64];
    char tooltip[32];

    struct NPCEntity* npc = world_npc(world, entity_id);

    struct WorldOption* option = &option_set->options[option_set->option_count];
    struct PlayerEntity* player = world_player(world, ACTIVE_PLAYER_SLOT);

    {
        char const* color_tag = options_npc_combat_level_color_tag(
            player->visible_level.level, npc->visible_level.level);
        char* ptr = tooltip;
        ptr += snprintf(ptr, sizeof(tooltip) - (ptr - tooltip), "%s", npc->name ? npc->name : "");
        if( npc->visible_level.level != 0 )
        {
            ptr += snprintf(
                ptr,
                sizeof(tooltip) - (ptr - tooltip),
                " %s (level-%d)",
                color_tag,
                npc->visible_level.level);
        }
        for( int i = 4; npc->actions && i >= 0; i-- )
        {
            if( npc->actions[i].name[0] == '\0' )
                continue;
            if( strcasecmp(npc->actions[i].name, "attack") != 0 )
            {
                snprintf(text, sizeof(text), "%s @yel@ %s", npc->actions[i].name, tooltip);

                option = &option_set->options[option_set->option_count];
                memset(option, 0, sizeof(*option));

                strncpy(option->text, text, sizeof(option->text));
                option->param_a = entity_id;
                option->param_b = x;
                option->param_c = z;
                option_set->option_count += 1;

                switch( i )
                {
                case 0:
                    option->action = MINIMENU_ACTION_OPNPC1;
                    break;
                case 1:
                    option->action = MINIMENU_ACTION_OPNPC2;
                    break;
                case 2:
                    option->action = MINIMENU_ACTION_OPNPC3;
                    break;
                case 3:
                    option->action = MINIMENU_ACTION_OPNPC4;
                    break;
                case 4:
                    option->action = MINIMENU_ACTION_OPNPC5;
                    break;
                default:
                    assert(0 && "Invalid action index");
                    break;
                }
            }
        }

        for( int i = 4; npc->actions && i >= 0; i-- )
        {
            if( npc->actions[i].name[0] == '\0' )
                continue;
            if( strcasecmp(npc->actions[i].name, "attack") == 0 )
            {
                int priority = player->visible_level.level < npc->visible_level.level
                                   ? MINIMENU_ACTION_PRIORITY
                                   : 0;

                snprintf(text, sizeof(text), "%s @yel@ %s", npc->actions[i].name, tooltip);

                option = &option_set->options[option_set->option_count];
                memset(option, 0, sizeof(*option));

                strncpy(option->text, text, sizeof(option->text));
                option->param_a = entity_id;
                option->param_b = x;
                option->param_c = z;
                option_set->option_count += 1;

                switch( i )
                {
                case 0:
                    option->action = minimenu_action_priority(MINIMENU_ACTION_OPNPC1, priority);
                    break;
                case 1:
                    option->action = minimenu_action_priority(MINIMENU_ACTION_OPNPC2, priority);
                    break;
                case 2:
                    option->action = minimenu_action_priority(MINIMENU_ACTION_OPNPC3, priority);
                    break;
                case 3:
                    option->action = minimenu_action_priority(MINIMENU_ACTION_OPNPC4, priority);
                    break;
                case 4:
                    option->action = minimenu_action_priority(MINIMENU_ACTION_OPNPC5, priority);
                    break;
                default:
                    assert(0 && "Invalid action index");
                    break;
                }
            }
        }

        snprintf(text, sizeof(text), "Examine @yel@ %s", tooltip);

        option = &option_set->options[option_set->option_count];
        memset(option, 0, sizeof(*option));

        strncpy(option->text, text, sizeof(option->text));
        option->action = MINIMENU_ACTION_OPNPC6;
        option->param_a = entity_id;
        option->param_b = x;
        option->param_c = z;
        option_set->option_count += 1;
    }
}

void
world_options_add_pickset_options(
    struct GGame* game,
    struct World* world,
    struct WorldPickSet* pickset,
    struct WorldOptionSet* option_set)
{
    /* World-only (Client.ts addWorldOptions + Model.picked*). Inventory/bank menus never use
     * the spatial pickset — they come from UITree + inv_pool via uitree_sync_hover_option_set,
     * mirroring Client.ts addComponentOptions on IfType (inventory type 2). */
    /* Mirror Client.ts addWorldOptions (9510-9514): Walk here is appended before picked
     * entities so the same bubble-sort as buildMinimenu (2816-2844) applies. */
    if( option_set->option_count < WORLD_OPTION_SET_CAPACITY )
    {
        struct WorldOption* walk = &option_set->options[option_set->option_count];
        memset(walk, 0, sizeof(*walk));
        strncpy(walk->text, "Walk here", sizeof(walk->text));
        walk->action = MINIMENU_ACTION_WALK;
        walk->param_a = 0;
        walk->param_b = 0;
        walk->param_c = 0;
        option_set->option_count++;
    }

    for( int i = 0; i < pickset->count; i++ )
    {
        struct PickedEntity* picked_entity = &pickset->entities[i];
        switch( picked_entity->entity_type )
        {
        case ENTITY_KIND_MAP_BUILD_LOC:
            options_add_loc(
                world,
                option_set,
                picked_entity->x,
                picked_entity->z,
                picked_entity->entity_type,
                picked_entity->entity_id);
            break;
        case ENTITY_KIND_NPC:
            options_add_npc(
                world,
                option_set,
                picked_entity->x,
                picked_entity->z,
                picked_entity->entity_type,
                picked_entity->entity_id);
            break;
        case ENTITY_KIND_PLAYER:
            options_add_player(
                game,
                world,
                option_set,
                picked_entity->x,
                picked_entity->z,
                picked_entity->entity_id);
            break;
        }
    }

    /* Client.ts addPlayerOptions: rewrite Walk here to include @whi@ + tooltip for hovered player.
     */
    if( game && option_set->option_count > 0 &&
        option_set->options[0].action == MINIMENU_ACTION_WALK )
    {
        for( int pi = 0; pi < pickset->count; pi++ )
        {
            struct PickedEntity* pe = &pickset->entities[pi];
            if( pe->entity_type != ENTITY_KIND_PLAYER )
                continue;
            if( pe->entity_id == ACTIVE_PLAYER_SLOT )
                continue;
            struct PlayerEntity* t = world_player(world, pe->entity_id);
            struct PlayerEntity* l = world_player(world, ACTIVE_PLAYER_SLOT);
            if( !t || !t->alive || !l || !l->alive )
                continue;
            char tt[128];
            const char* nm = (t->name.name[0] != '\0') ? t->name.name : "Player";
            char const* color_tag =
                options_npc_combat_level_color_tag(l->visible_level.level, t->visible_level.level);
            snprintf(tt, sizeof(tt), "%s%s (level-%d)", nm, color_tag, t->visible_level.level);
            snprintf(
                option_set->options[0].text,
                sizeof(option_set->options[0].text),
                "Walk here @whi@ %s",
                tt);
            break;
        }
    }

    /* Identity permutation: full [Cancel + world] sort runs in minimenu_game_show
     * (Client.ts buildMinimenu 2816-2844) so it matches the reference client. */
    for( int i = 0; i < option_set->option_count; i++ )
        option_set->order[i] = i;
}