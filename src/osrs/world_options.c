#include "world_options.h"

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

    char text[64];

    struct MapBuildLocEntity* map_build_loc_entity = &world->map_build_loc_entities[entity_id];
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
                map_build_loc_entity->name);

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

    snprintf(text, sizeof(text), "Examine @cya@ %s", map_build_loc_entity->name);
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
        char* ptr = tooltip;
        ptr += snprintf(ptr, sizeof(tooltip) - (ptr - tooltip), "%s", npc->name.name);
        if( npc->visible_level.level != 0 )
        {
            ptr += snprintf(
                ptr,
                sizeof(tooltip) - (ptr - tooltip),
                " %s (level-%d)",
                color_tag,
                npc->visible_level.level);
        }
        for( int i = 4; i >= 0; i-- )
        {
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

        for( int i = 4; i >= 0; i-- )
        {
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
        case ENTITY_KIND_NPC:
            options_add_npc(
                world,
                option_set,
                picked_entity->x,
                picked_entity->z,
                picked_entity->entity_type,
                picked_entity->entity_id);
            break;
        }
    }

    // Sort the options

    // let sorted: boolean = false;
    // while (!sorted) {
    //     sorted = true;

    //     for (let i: number = 0; i < this.menuNumEntries - 1; i++) {
    //         if (this.menuAction[i] < 1000 && this.menuAction[i + 1] > 1000) {
    //             const tmp0: string = this.menuOption[i];
    //             this.menuOption[i] = this.menuOption[i + 1];
    //             this.menuOption[i + 1] = tmp0;

    //             const tmp1: number = this.menuAction[i];
    //             this.menuAction[i] = this.menuAction[i + 1];
    //             this.menuAction[i + 1] = tmp1;

    //             const tmp2: number = this.menuParamB[i];
    //             this.menuParamB[i] = this.menuParamB[i + 1];
    //             this.menuParamB[i + 1] = tmp2;

    //             const tmp3: number = this.menuParamC[i];
    //             this.menuParamC[i] = this.menuParamC[i + 1];
    //             this.menuParamC[i + 1] = tmp3;

    //             const tmp4: number = this.menuParamA[i];
    //             this.menuParamA[i] = this.menuParamA[i + 1];
    //             this.menuParamA[i + 1] = tmp4;

    //             sorted = false;
    //         }
    //     }
    // }

    for( int i = 0; i < option_set->option_count; i++ )
        option_set->order[i] = i;

    bool sorted = false;
    while( !sorted )
    {
        sorted = true;
        for( int i = 0; i < option_set->option_count - 1; i++ )
        {
            int idx0 = option_set->order[i];
            int idx1 = option_set->order[i + 1];
            if( option_set->options[idx0].action < 1000 && option_set->options[idx1].action > 1000 )
            {
                sorted = false;

                int tmp = option_set->order[i];
                option_set->order[i] = option_set->order[i + 1];
                option_set->order[i + 1] = tmp;
            }
        }
    }
}