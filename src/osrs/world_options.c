#include "world_options.h"

#include "game.h"
#include "game_entity.h"
#include "minimenu_action.h"
#include "osrs/gamecache/gamecache.h"
#include "osrs/interface_state.h"
#include "rscache/tables/string_utils.h"

#include <assert.h>
#include <string.h>

/** Max ground items per tile to enumerate for minimenu (tail→head); excess oldest are skipped. */
#define WORLD_OPTIONS_OBJ_STACK_LIST_MAX 128

static void
options_add_obj_stack_at_tile(
    struct GGame* game,
    struct World* world,
    struct WorldOptionSet* option_set,
    int sx,
    int sz,
    int level)
{
    if( !game || !world || !option_set || !game->gamecache )
        return;
    if( level < 0 || level >= MAP_TERRAIN_LEVELS )
        return;
    if( sx < 0 || sx >= ZONE_SCENE_SIZE || sz < 0 || sz >= ZONE_SCENE_SIZE )
        return;
    if( !world->obj_stacks || !world->obj_stacks[level] )
        return;

    struct ObjStackEntry* head = world->obj_stacks[level][sx * ZONE_SCENE_SIZE + sz];
    if( !head )
        return;

    int total = 0;
    for( struct ObjStackEntry* e = head; e; e = e->next )
        total++;

    struct ObjStackEntry* forward[WORLD_OPTIONS_OBJ_STACK_LIST_MAX];
    int n = 0;
    int skip = total > WORLD_OPTIONS_OBJ_STACK_LIST_MAX ? total - WORLD_OPTIONS_OBJ_STACK_LIST_MAX : 0;
    for( struct ObjStackEntry* e = head; e; e = e->next )
    {
        if( skip > 0 )
        {
            skip--;
            continue;
        }
        if( n >= WORLD_OPTIONS_OBJ_STACK_LIST_MAX )
            break;
        forward[n++] = e;
    }

    struct InterfaceState* iface = game->iface;

    for( int li = n - 1; li >= 0; li-- )
    {
        struct ObjStackEntry* entry = forward[li];
        struct GameCacheObj* o = gamecache_get_obj(game->gamecache, entry->obj_id);
        if( !o || !o->name || !o->name[0] )
            continue;
        if( option_set->option_count >= WORLD_OPTION_SET_CAPACITY - 1 )
            return;

        if( iface && iface->inv_use_mode )
        {
            struct WorldOption* opt = &option_set->options[option_set->option_count];
            memset(opt, 0, sizeof(*opt));
            snprintf(
                opt->text,
                sizeof(opt->text),
                "Use %s with @lre@%s",
                iface->inv_sel_obj_name[0] ? iface->inv_sel_obj_name : "",
                o->name);
            opt->action  = MINIMENU_ACTION_OPOBJU;
            opt->param_a = entry->obj_id;
            opt->param_b = sx;
            opt->param_c = sz;
            option_set->option_count += 1;
        }
        else if( iface && iface->inv_target_mode )
        {
            if( (iface->inv_target_mask & 0x1) == 0 )
                continue;
            struct WorldOption* opt = &option_set->options[option_set->option_count];
            memset(opt, 0, sizeof(*opt));
            snprintf(
                opt->text,
                sizeof(opt->text),
                "%s @lre@%s",
                iface->inv_target_op[0] ? iface->inv_target_op : "",
                o->name);
            opt->action  = MINIMENU_ACTION_OPOBJT;
            opt->param_a = entry->obj_id;
            opt->param_b = sx;
            opt->param_c = sz;
            option_set->option_count += 1;
        }
        else
        {
            for( int op = 4; op >= 0; op-- )
            {
                if( option_set->option_count >= WORLD_OPTION_SET_CAPACITY - 1 )
                    return;
                char const* opname = o->iop[op];
                if( opname && opname[0] != '\0' )
                {
                    struct WorldOption* opt = &option_set->options[option_set->option_count];
                    memset(opt, 0, sizeof(*opt));
                    snprintf(opt->text, sizeof(opt->text), "%s @lre@%s", opname, o->name);
                    switch( op )
                    {
                    case 0:
                        opt->action = MINIMENU_ACTION_OPOBJ1;
                        break;
                    case 1:
                        opt->action = MINIMENU_ACTION_OPOBJ2;
                        break;
                    case 2:
                        opt->action = MINIMENU_ACTION_OPOBJ3;
                        break;
                    case 3:
                        opt->action = MINIMENU_ACTION_OPOBJ4;
                        break;
                    case 4:
                        opt->action = MINIMENU_ACTION_OPOBJ5;
                        break;
                    default:
                        opt->action = MINIMENU_ACTION_OPOBJ1;
                        break;
                    }
                    opt->param_a = entry->obj_id;
                    opt->param_b = sx;
                    opt->param_c = sz;
                    option_set->option_count += 1;
                }
                else if( op == 2 )
                {
                    struct WorldOption* opt = &option_set->options[option_set->option_count];
                    memset(opt, 0, sizeof(*opt));
                    snprintf(opt->text, sizeof(opt->text), "Take @lre@%s", o->name);
                    opt->action  = MINIMENU_ACTION_OPOBJ3;
                    opt->param_a = entry->obj_id;
                    opt->param_b = sx;
                    opt->param_c = sz;
                    option_set->option_count += 1;
                }
            }

            if( option_set->option_count >= WORLD_OPTION_SET_CAPACITY - 1 )
                return;
            struct WorldOption* opt = &option_set->options[option_set->option_count];
            memset(opt, 0, sizeof(*opt));
            snprintf(opt->text, sizeof(opt->text), "Examine @lre@%s", o->name);
            opt->action  = MINIMENU_ACTION_OPOBJ6;
            opt->param_a = entry->obj_id;
            opt->param_b = sx;
            opt->param_c = sz;
            option_set->option_count += 1;
        }
    }
}

/** Zone OBJ packets may only populate level 0 while terrain hover uses another plane — scan
 * obj_stack_entity_by_tile / obj_stacks so Take/Examine rows match data (plan: hover lookup). */
static int
world_options_resolve_obj_stack_level(
    struct World* world,
    int sx,
    int sz,
    int hint_level)
{
    if( !world || !world->obj_stacks || !world->obj_stack_entity_by_tile )
        return (hint_level >= 0 && hint_level < MAP_TERRAIN_LEVELS) ? hint_level : 0;

    for( int lev = 0; lev < MAP_TERRAIN_LEVELS; lev++ )
    {
        int flat = world_obj_stack_tile_flat(lev, sx, sz);
        int eid  = world->obj_stack_entity_by_tile[flat];
        if( eid >= 0 && eid < entity_vec_count(&world->obj_stack_entities) )
        {
            struct ObjStackEntity* ost = world_obj_stack_entity(world, eid);
            if( ost && ost->alive )
                return (ost->level >= 0 && ost->level < MAP_TERRAIN_LEVELS) ? ost->level : lev;
        }
    }

    for( int lev = 0; lev < MAP_TERRAIN_LEVELS; lev++ )
    {
        if( !world->obj_stacks[lev] )
            continue;
        struct ObjStackEntry* head = world->obj_stacks[lev][sx * ZONE_SCENE_SIZE + sz];
        if( head )
            return lev;
    }

    if( hint_level >= 0 && hint_level < MAP_TERRAIN_LEVELS )
        return hint_level;
    return 0;
}

/** Client.ts addWorldOptions entityType === 3; dedupe when multiple stack layers hit pickset. */
static void
options_add_obj_stack(
    struct GGame* game,
    struct World* world,
    struct WorldPickSet* pickset,
    int pick_index,
    struct WorldOptionSet* option_set)
{
    if( !pickset || pick_index < 0 || pick_index >= pickset->count )
        return;

    struct PickedEntity* picked_entity = &pickset->entities[pick_index];
    assert(picked_entity->entity_type == ENTITY_KIND_OBJ_STACK && "Entity type must be obj stack");

    for( int j = 0; j < pick_index; j++ )
    {
        if( pickset->entities[j].entity_type == ENTITY_KIND_OBJ_STACK &&
            pickset->entities[j].x == picked_entity->x &&
            pickset->entities[j].z == picked_entity->z )
            return;
    }

    int hint = (game && game->mouse_tile_level >= 0 && game->mouse_tile_level < MAP_TERRAIN_LEVELS)
                   ? game->mouse_tile_level
                   : 0;
    int lev =
        world_options_resolve_obj_stack_level(world, picked_entity->x, picked_entity->z, hint);
    options_add_obj_stack_at_tile(
        game,
        world,
        option_set,
        picked_entity->x,
        picked_entity->z,
        lev);
}

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

    /* Loc actions are packed (world_map_build_loc_entity_push_action); .code is config slot 0..9.
     * OPLOC packets only support slots 0..4; scan packed rows per slot so slot 0 (code==0) works. */
    for( int slot = 4; slot >= 0; slot-- )
    {
        if( !map_build_loc_entity->actions )
            break;

        struct EntityAction const* row = NULL;
        for( int j = 0; j < map_build_loc_entity->action_count; j++ )
        {
            struct EntityAction const* a = &map_build_loc_entity->actions[j];
            if( (int)a->code == slot && a->name[0] != '\0' )
            {
                row = a;
                break;
            }
        }
        if( !row )
            continue;

        option = &option_set->options[option_set->option_count];
        memset(option, 0, sizeof(*option));

        snprintf(text, sizeof(text), "%s @cya@ %s", row->name, map_build_loc_entity->name.name);

        strncpy(option->text, text, sizeof(option->text));
        option->param_a = entity_id;
        option->param_b = x;
        option->param_c = z;

        switch( slot )
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
            assert(0 && "Invalid loc op slot");
            break;
        }

        option_set->option_count += 1;
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
        case ENTITY_KIND_OBJ_STACK:
            options_add_obj_stack(game, world, pickset, i, option_set);
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