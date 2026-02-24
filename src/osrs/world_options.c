#include "world_options.h"

#include "game_entity.h"
#include "minimenu_action.h"

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

    char text[64];

    struct MapBuildLocEntity* map_build_loc_entity = &world->map_build_loc_entities[entity_id];

    struct WorldOption* option = &option_set->options[option_set->option_count];

    for( int i = 4; i >= 0; i-- )
    {
        if( map_build_loc_entity->actions[i].code != 0 )
        {
            snprintf(
                text,
                sizeof(text),
                "%s @cya@ %s",
                map_build_loc_entity->actions[i].name,
                map_build_loc_entity->name.name);

            option->action = map_build_loc_entity->actions[i].code;
            strncpy(option->text, text, sizeof(option->text));
            option->param_a = entity_id;
            option->param_b = x;
            option->param_c = z;

            option_set->option_count += 1;

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
        }
    }

    option = &option_set->options[option_set->option_count];
    snprintf(text, sizeof(text), "Examine @cya@ %s", map_build_loc_entity->name.name);
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
        return '@red@';
    }
    else if( diff < -6 )
    {
        return '@or3@';
    }
    else if( diff < -3 )
    {
        return '@or2@';
    }
    else if( diff < 0 )
    {
        return '@or1@';
    }
    else if( diff > 9 )
    {
        return '@gre@';
    }
    else if( diff > 6 )
    {
        return '@gr3@';
    }
    else if( diff > 3 )
    {
        return '@gr2@';
    }
    else if( diff > 0 )
    {
        return '@gr1@';
    }
    else
    {
        return '@yel@';
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

    char text[64];
    char tooltip[32];

    struct NPCEntity* npc = &world->npcs[entity_id];

    struct WorldOption* option = &option_set->options[option_set->option_count];
    struct PlayerEntity* player = &world->players[ACTIVE_PLAYER_SLOT];

    if( x == 64 && z == 64 && npc->size.x == 1 && npc->size.z == 1 )
    {
        // TODO: Only 1 1x1 npc is drawn on a tile, so
        // actually need a stack map to get all the npcs on the tile.
    }
    else
    {
        char const* color_tag = options_npc_combat_level_color_tag(
            player->visible_level.level, npc->visible_level.level);
        snprintf(
            tooltip,
            sizeof(tooltip),
            "%s %s (level-%d)",
            npc->name.name,
            color_tag,
            npc->visible_level.level);

        for( int i = 4; i >= 0; i-- )
        {
            if( npc->actions[i].code != 0 )
            {
                snprintf(text, sizeof(text), "%s @yel@ %s", npc->actions[i].name, tooltip);

                option = &option_set->options[option_set->option_count];
                strncat(option->text, text, sizeof(option->text));
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

        for( int i = 4; i >= 0; i-- )
        {
            if( strcmp(npc->actions[i].name, "attack") == 0 )
            {
                int priority = player->visible_level.level < npc->visible_level.level
                                   ? MINIMENU_ACTION_PRIORITY
                                   : 0;

                snprintf(text, sizeof(text), "%s @yel@ %s", npc->actions[i].name, tooltip);

                option = &option_set->options[option_set->option_count];
                strncat(option->text, text, sizeof(option->text));
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

        option = &option_set->options[option_set->option_count];
        snprintf(text, sizeof(text), "Examine @yel@ %s", tooltip);
        option->action = MINIMENU_ACTION_OPNPC6;
        option->param_a = entity_id;
        option->param_b = x;
        option->param_c = z;
        option_set->option_count += 1;
    }
}

void
world_options_add_pickset_options(
    struct World* world,
    struct WorldPickSet* pickset,
    struct WorldOptionSet* option_set)
{
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
        }
    }
}