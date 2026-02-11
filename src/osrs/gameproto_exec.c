#include "gameproto_exec.h"

#include "dash_utils.h"
#include "datatypes/appearances.h"
#include "datatypes/player_appearance.h"
#include "entity_scenebuild.h"
#include "game_entity.h"
#include "model_transforms.h"
#include "osrs/_light_model_default.u.c"
#include "packets/pkt_npc_info.h"
#include "packets/pkt_player_info.h"
#include "rscache/bitbuffer.h"
#include "rscache/rsbuf.h"
#include "rscache/tables/model.h"

struct StepCoord
{
    int x;
    int z;
};

static void
coord_step(
    struct StepCoord* step,
    int direction)
{
    int next_x = step->x;
    int next_z = step->z;
    if( direction == 0 )
    {
        next_x--;
        next_z++;
    }
    else if( direction == 1 )
    {
        next_z++;
    }
    else if( direction == 2 )
    {
        next_x++;
        next_z++;
    }
    else if( direction == 3 )
    {
        next_x--;
    }
    else if( direction == 4 )
    {
        next_x++;
    }
    else if( direction == 5 )
    {
        next_x--;
        next_z--;
    }
    else if( direction == 6 )
    {
        next_z--;
    }
    else if( direction == 7 )
    {
        next_x++;
        next_z--;
    }

    step->x = next_x;
    step->z = next_z;
}

static void
npc_move(
    struct GGame* game,
    int npc_entity_id,
    int tile_x,
    int tile_z)
{
    struct NPCEntity* npc_entity = &game->npcs[npc_entity_id];

    // Hack
    struct SceneElement* scene_element = (struct SceneElement*)npc_entity->scene_element;

    int dx = tile_x - npc_entity->pathing.route_x[0];
    int dz = tile_z - npc_entity->pathing.route_z[0];
    int animatable_distance = dx >= -8 && dx <= 8 && dz >= -8 && dz <= 8;
    if( animatable_distance )
    {
        if( npc_entity->pathing.route_length < 9 )
            npc_entity->pathing.route_length++;

        for( int i = npc_entity->pathing.route_length; i > 0; i-- )
        {
            npc_entity->pathing.route_x[i] = npc_entity->pathing.route_x[i - 1];
            npc_entity->pathing.route_z[i] = npc_entity->pathing.route_z[i - 1];
        }
        npc_entity->pathing.route_x[0] = tile_x;
        npc_entity->pathing.route_z[0] = tile_z;
        npc_entity->pathing.route_run[0] = 0;
    }
    else
    {
        npc_entity->pathing.route_length = 0;
        npc_entity->pathing.route_x[0] = tile_x;
        npc_entity->pathing.route_z[0] = tile_z;
    }

    npc_entity->position.x = tile_x * 128 + npc_entity->size_x * 64;
    npc_entity->position.z = tile_z * 128 + npc_entity->size_z * 64;

    scene_element->dash_position->x = npc_entity->position.x;
    scene_element->dash_position->z = npc_entity->position.z;
}

static struct PktNpcInfoReader npc_info_reader = { 0 };

void
gameproto_exec_npc_info(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    npc_info_reader.extended_count = 0;
    npc_info_reader.current_op = 0;
    npc_info_reader.max_ops = 2048;
    struct PktNpcInfoOp ops[2048];
    struct StepCoord step = { 0 };
    int count = pkt_npc_info_reader_read(&npc_info_reader, &packet->_npc_info, ops, 2048);

    struct PlayerEntity* player = &game->players[ACTIVE_PLAYER_SLOT];
    if( !player->alive )
        return;

    int npc_id = -1;
    int prev_count = game->npc_count;
    int removed_count = 0;
    game->npc_count = 0;
    struct SceneElement* scene_element = NULL;
    struct NPCEntity* npc = NULL;
    for( int i = 0; i < count; i++ )
    {
        struct PktNpcInfoOp* op = &ops[i];

        if( npc_id != -1 )
        {
            npc = entity_scenebuild_npc_get(game, npc_id);
            scene_element = (struct SceneElement*)npc->scene_element;
        }
        else
        {
            scene_element = NULL;
            npc = NULL;
        }

        switch( op->kind )
        {
        case PKT_NPC_INFO_OP_ADD_NPC_NEW_OPBITS_PID:
        {
            npc_id = op->_bitvalue;
            game->active_npcs[game->npc_count] = npc_id;
            game->npc_count += 1;

            break;
        }
        case PKT_NPC_INFO_OP_ADD_NPC_OLD_OPBITS_IDX:
        {
            assert(op->_bitvalue >= game->npc_count);
            npc_id = game->active_npcs[op->_bitvalue];
            game->active_npcs[game->npc_count] = npc_id;
            game->npc_count += 1;

            break;
        }
        case PKT_NPC_INFO_OP_SET_NPC_OPBITS_IDX:
        {
            npc_id = game->active_npcs[op->_bitvalue];
            break;
        }
        case PKT_NPC_INFO_OP_CLEAR_NPC_OPBITS_IDX:
        {
            npc_id = game->active_npcs[op->_bitvalue];
            entity_scenebuild_npc_release(game, npc_id);
            game->active_npcs[op->_bitvalue] = -1;
            npc_id = -1;
            break;
        }
        case PKT_NPC_INFO_OPBITS_COUNT_RESET:
        {
            for( int idx = op->_bitvalue; idx < prev_count; idx++ )
            {
                entity_scenebuild_npc_release(game, game->active_npcs[idx]);
            }
            break;
        }
        case PKT_NPC_INFO_OPBITS_DZ:
        {
            /* Client.ts: npc.move(..., localPlayer.routeTileX[0] + dx,
             * localPlayer.routeTileZ[0] + dz). */
            int base_z = player->pathing.route_z[0];
            int dz = op->_bitvalue;
            npc->pathing.route_z[0] = base_z + dz;
            npc_move(game, npc_id, npc->pathing.route_x[0], npc->pathing.route_z[0]);
            break;
        }
        case PKT_NPC_INFO_OPBITS_DX:
        {
            int base_x = player->pathing.route_x[0];
            int dx = op->_bitvalue;
            npc->pathing.route_x[0] = base_x + dx;
            break;
        }
        case PKT_NPC_INFO_OPBITS_WALKDIR:
        case PKT_NPC_INFO_OP_RUNDIR:
        {
            int direction;
            if( op->kind == PKT_NPC_INFO_OPBITS_WALKDIR )
            {
                direction = op->_bitvalue;
            }
            else
            {
                direction = op->_rundir.rundir_one;
            }

            step.x = npc->pathing.route_x[0];
            step.z = npc->pathing.route_z[0];
            coord_step(&step, direction);

            if( op->kind == PKT_NPC_INFO_OP_RUNDIR )
                coord_step(&step, op->_rundir.rundir_two);

            if( npc->pathing.route_length < 9 )
                npc->pathing.route_length++;

            for( int i = npc->pathing.route_length; i > 0; i-- )
            {
                npc->pathing.route_x[i] = npc->pathing.route_x[i - 1];
                npc->pathing.route_z[i] = npc->pathing.route_z[i - 1];
                npc->pathing.route_run[i] = npc->pathing.route_run[i - 1];
            }

            /**
             * This is the authoritative position of the npc.
             * Always in route[0]
             */
            npc->pathing.route_x[0] = step.x;
            npc->pathing.route_z[0] = step.z;
            npc->pathing.route_run[0] = op->kind == PKT_NPC_INFO_OP_RUNDIR ? 1 : 0;
            break;
        }
        case PKT_NPC_INFO_OPBITS_NPCTYPE:
        {
            entity_scenebuild_npc_change_type(game, npc_id, op->_bitvalue);
            break;
        }
        case PKT_NPC_INFO_OP_FACE_ENTITY:
        {
            if( !npc )
                break;
            int entity_id = (int)op->_face_entity.entity_id;
            if( entity_id == 65535 )
                entity_id = -1;
            npc->orientation.face_entity = entity_id;
            break;
        }
        case PKT_NPC_INFO_OP_FACE_COORD:
        {
            if( !npc )
                break;
            npc->orientation.face_square_x = (int)op->_face_coord.x;
            npc->orientation.face_square_z = (int)op->_face_coord.z;
            break;
        }
        case PKT_NPC_INFO_OP_SEQUENCE:
        {
            if( !npc )
                break;
            int seq_id = (int)op->_sequence.sequence_id;
            if( seq_id == 65535 )
                seq_id = -1;
            npc->primary_anim = seq_id;
            npc->primary_anim_frame = 0;
            npc->primary_anim_cycle = 0;
            npc->primary_anim_delay = op->_sequence.delay;
            npc->primary_anim_loop = 0;
            break;
        }
        case PKT_NPC_INFO_OP_DAMAGE:
        {
            entity_add_hitmark(
                npc->damage_values,
                npc->damage_types,
                npc->damage_cycles,
                game->cycle,
                op->_damage.damage_type,
                op->_damage.damage);
            npc->combat_cycle = game->cycle + 400;
            npc->health = op->_damage.health;
            npc->total_health = op->_damage.total_health;
            break;
        }
        }
    }
}

static struct PktPlayerInfoReader player_info_reader = { 0 };

static void
player_move(
    struct GGame* game,
    int player_id,
    int x,
    int z)
{
    struct PlayerEntity* player = &game->players[player_id];

    // Hack
    struct SceneElement* scene_element = (struct SceneElement*)player->scene_element;

    int dx = x / 128 - player->pathing.route_x[0];
    int dz = z / 128 - player->pathing.route_z[0];
    int animatable_distance = dx >= -8 && dx <= 8 && dz >= -8 && dz <= 8;
    if( animatable_distance )
    {
        if( player->pathing.route_length < 9 )
            player->pathing.route_length++;

        for( int i = player->pathing.route_length; i > 0; i-- )
        {
            player->pathing.route_x[i] = player->pathing.route_x[i - 1];
            player->pathing.route_z[i] = player->pathing.route_z[i - 1];
            player->pathing.route_run[i] = player->pathing.route_run[i - 1];
        }
        player->pathing.route_x[0] = x / 128;
        player->pathing.route_z[0] = z / 128;
        player->pathing.route_run[0] = 0;
    }
    else
    {
        player->pathing.route_length = 0;
        player->pathing.route_x[0] = x / 128;
        player->pathing.route_z[0] = z / 128;
        player->pathing.route_run[0] = 0;

        player->position.x = player->pathing.route_x[0] * 128 + 64;
        player->position.z = player->pathing.route_z[0] * 128 + 64;
    }
}

void
add_player_info(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    if( !game->scene )
        return;

    struct BitBuffer buf;
    struct RSBuffer rsbuf;
    struct StepCoord step = { 0 };
    rsbuf_init(&rsbuf, packet->_player_info.data, packet->_player_info.length);
    bitbuffer_init_from_rsbuf(&buf, &rsbuf);
    bits(&buf);

    struct PktPlayerInfoOp ops[2048];

    struct SceneElement* scene_element = NULL;
    struct PlayerEntity* active_player = &game->players[ACTIVE_PLAYER_SLOT];

    int count = pkt_player_info_reader_read(&player_info_reader, &packet->_player_info, ops, 2048);
    int player_id = ACTIVE_PLAYER_SLOT;

    game->player_count = 0;
    for( int i = 0; i < count; i++ )
    {
        struct PktPlayerInfoOp* op = &ops[i];

        struct PlayerEntity* player =
            (player_id >= 0) ? entity_scenebuild_player_get(game, player_id) : NULL;

        switch( op->kind )
        {
        case PKT_PLAYER_INFO_OP_SET_LOCAL_PLAYER:
        {
            player_id = ACTIVE_PLAYER_SLOT;
            break;
        }
        case PKT_PLAYER_INFO_OP_ADD_PLAYER_OLD_OPBITS_IDX:
        {
            player_id = game->active_players[op->_bitvalue];
            game->active_players[game->player_count] = player_id;
            game->player_count += 1;
            break;
        }
        case PKT_PLAYER_INFO_OP_ADD_PLAYER_NEW_OPBITS_PID:
        {
            player_id = op->_bitvalue;
            game->active_players[game->player_count] = player_id;
            game->player_count += 1;
            break;
        }
        case PKT_PLAYER_INFO_OP_SET_PLAYER_OPBITS_IDX:
        {
            player_id = game->active_players[op->_bitvalue];
            break;
        }
        case PKT_PLAYER_INFO_OP_CLEAR_PLAYER_OPBITS_IDX:
        {
            player_id = game->active_players[op->_bitvalue];
            entity_scenebuild_player_release(game, player_id);
            game->active_players[op->_bitvalue] = -1;
            player_id = -1;
            break;
        }
        case PKT_PLAYER_INFO_OPBITS_WALKDIR:
        case PKT_PLAYER_INFO_OP_RUNDIR:
        {
            if( !player )
                break;
            int direction;
            if( op->kind == PKT_PLAYER_INFO_OPBITS_WALKDIR )
            {
                direction = op->_bitvalue;
            }
            else
            {
                direction = op->_rundir.rundir_one;
            }

            step.x = player->pathing.route_x[0];
            step.z = player->pathing.route_z[0];
            coord_step(&step, direction);

            if( op->kind == PKT_PLAYER_INFO_OP_RUNDIR )
            {
                coord_step(&step, op->_rundir.rundir_two);
            }

            if( player->pathing.route_length < 9 )
                player->pathing.route_length++;

            for( int i = player->pathing.route_length; i > 0; i-- )
            {
                player->pathing.route_x[i] = player->pathing.route_x[i - 1];
                player->pathing.route_z[i] = player->pathing.route_z[i - 1];
                player->pathing.route_run[i] = player->pathing.route_run[i - 1];
            }

            player->pathing.route_x[0] = step.x;
            player->pathing.route_z[0] = step.z;
            player->pathing.route_run[0] = op->kind == PKT_PLAYER_INFO_OP_RUNDIR ? 1 : 0;
            break;
        }
        case PKT_PLAYER_INFO_OPBITS_DX:
        {
            if( !player )
                break;
            /* Client.ts: new player tile = localPlayer.routeTileX[0] + dx (same for dz). */
            int base_x = game->players[ACTIVE_PLAYER_SLOT].pathing.route_x[0];
            int dx = (int)op->_bitvalue;
            player->position.x = (base_x + dx) * 128 + 64;
            break;
        }
        case PKT_PLAYER_INFO_OPBITS_DZ:
        {
            if( !player )
                break;
            int base_z = game->players[ACTIVE_PLAYER_SLOT].pathing.route_z[0];
            int dz = (int)op->_bitvalue;
            player->position.z = (base_z + dz) * 128 + 64;
            player_move(game, player_id, player->position.x, player->position.z);
            break;
        }
        case PKT_PLAYER_INFO_OPBITS_LOCAL_X:
        {
            if( !player )
                break;
            int dx = (op->_bitvalue);
            player->pathing.route_x[0] = active_player->pathing.route_x[0] + dx;

            break;
        }
        case PKT_PLAYER_INFO_OPBITS_LOCAL_Z:
        {
            if( !player )
                break;

            int dz = (op->_bitvalue);
            player->pathing.route_length = 1;
            player->pathing.route_z[0] = active_player->pathing.route_z[0] + dz;
            break;
        }
        case PKT_PLAYER_INFO_OPBITS_JUMP:
        {
            /* Client.ts move(teleport=true): clear route, set position to route[0] tile. */
            if( !player || player_id != ACTIVE_PLAYER_SLOT )
                break;
            if( op->_bitvalue == 1 )
            {
                player->pathing.route_length = 0;
                player->pathing.route_x[0] = player->position.x / 128;
                player->pathing.route_z[0] = player->position.z / 128;
                player->position.x = player->pathing.route_x[0] * 128 + 64;
                player->position.z = player->pathing.route_z[0] * 128 + 64;
            }
            break;
        }
        case PKT_PLAYER_INFO_OP_APPEARANCE:
        {
            if( player_id < 0 )
                break;
            struct PlayerAppearance appearance;
            player_appearance_decode(&appearance, op->_appearance.appearance, op->_appearance.len);

            entity_scenebuild_player_change_appearance(game, player_id, &appearance);
        }
        break;
        case PKT_PLAYER_INFO_OP_FACE_ENTITY:
        {
            if( !player )
                break;
            int entity_id = (int)op->_face_entity.entity_id;
            if( entity_id == 65535 )
                entity_id = -1;
            player->orientation.face_entity = entity_id;

            break;
        }
        case PKT_PLAYER_INFO_OP_FACE_COORD:
        {
            if( !player )
                break;
            player->orientation.face_square_x = (int)op->_face_coord.x;
            player->orientation.face_square_z = (int)op->_face_coord.z;
            break;
        }
        case PKT_PLAYER_INFO_OP_SEQUENCE:
        {
            if( !player )
                break;
            /* Client.ts: seqId 65535 -> -1; primaryAnim = seqId, primaryAnimFrame = 0, etc. */
            int seq_id = (int)op->_sequence.sequence_id;
            if( seq_id == 65535 )
                seq_id = -1;
            player->primary_anim = seq_id;
            player->primary_anim_frame = 0;
            player->primary_anim_cycle = 0;
            player->primary_anim_delay = op->_sequence.delay;
            player->primary_anim_loop = 0;
            break;
        }
        case PKT_PLAYER_INFO_OP_DAMAGE:
        {
            if( !player )
                break;
            entity_add_hitmark(
                player->damage_values,
                player->damage_types,
                player->damage_cycles,
                game->cycle,
                op->_damage.damage_type,
                op->_damage.damage);
            player->combat_cycle = game->cycle + 400;
            player->health = op->_damage.health;
            player->total_health = op->_damage.total_health;
            break;
        }
        case PKT_PLAYER_INFO_OP_DAMAGE2:
        {
            if( !player )
                break;
            entity_add_hitmark(
                player->damage_values,
                player->damage_types,
                player->damage_cycles,
                game->cycle,
                op->_damage2.damage_type,
                op->_damage2.damage);
            player->combat_cycle = game->cycle + 400;
            player->health = op->_damage2.health;
            player->total_health = op->_damage2.total_health;
            break;
        }
        }
    }
}

void
gameproto_exec_rebuild_normal(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
#define SCENE_WIDTH 104
    int zone_padding = SCENE_WIDTH / (2 * 8);
    int zone_sw_x = packet->_map_rebuild.zonex - zone_padding;
    int zone_sw_z = packet->_map_rebuild.zonez - zone_padding;
    int zone_ne_x = packet->_map_rebuild.zonex + zone_padding;
    int zone_ne_z = packet->_map_rebuild.zonez + zone_padding;

    int levels = MAP_TERRAIN_LEVELS;

    game->sys_painter = painter_new(SCENE_WIDTH, SCENE_WIDTH, levels);
    game->sys_painter_buffer = painter_buffer_new();
    game->sys_minimap =
        minimap_new(zone_sw_x * 8, zone_sw_z * 8, zone_sw_x * 8 + 104, zone_sw_z * 8 + 104, levels);
    game->scenebuilder = scenebuilder_new_painter(game->sys_painter, game->sys_minimap);

    /* REBUILD_NORMAL: zone is in 8-tile units (pkt_rebuild_normal.lua wx_sw = zone_sw_x * 8).
     */
    game->scene_base_tile_x = zone_sw_x * 8;
    game->scene_base_tile_z = zone_sw_z * 8;

    game->scene = scenebuilder_load_from_buildcachedat(
        game->scenebuilder,
        zone_sw_x * 8,
        zone_sw_z * 8,
        zone_ne_x * 8,
        zone_ne_z * 8,
        104,
        104,
        game->buildcachedat);
}

void
gameproto_exec_player_info(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    add_player_info(game, packet);
}

void
gameproto_exec_update_inv_full(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    int component_id = packet->_update_inv_full.component_id;
    int size = packet->_update_inv_full.size;

    struct CacheDatConfigComponent* component =
        buildcachedat_get_component(game->buildcachedat, component_id);

    if( !component )
    {
        printf("UPDATE_INV_FULL: Component %d not found\n", component_id);
        return;
    }

    if( !component->invSlotObjId || !component->invSlotObjCount )
    {
        printf("UPDATE_INV_FULL: Component %d is not an inventory component\n", component_id);
        return;
    }

    int max_slots = component->width * component->height;

    // Update inventory slots with new data
    for( int i = 0; i < size && i < max_slots; i++ )
    {
        component->invSlotObjId[i] = packet->_update_inv_full.obj_ids[i];
        component->invSlotObjCount[i] = packet->_update_inv_full.obj_counts[i];
    }

    // Clear remaining slots
    for( int i = size; i < max_slots; i++ )
    {
        component->invSlotObjId[i] = 0;
        component->invSlotObjCount[i] = 0;
    }

    printf("UPDATE_INV_FULL: Updated component %d with %d items\n", component_id, size);

    // Debug: Print first few items to verify
    printf("UPDATE_INV_FULL: First 5 items in component %d:\n", component_id);
    for( int i = 0; i < 5 && i < size; i++ )
    {
        printf(
            "  Slot %d: ID=%d, Count=%d\n",
            i,
            component->invSlotObjId[i],
            component->invSlotObjCount[i]);
    }
}

void
gameproto_exec_if_settab(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    int component_id = packet->_if_settab.component_id;
    int tab_id = packet->_if_settab.tab_id;

    struct CacheDatConfigComponent* component =
        buildcachedat_get_component(game->buildcachedat, component_id);

    if( !component )
    {
        printf("IF_SETTAB: Component %d not found\n", component_id);
        return;
    }

    // Store the component in the tab interface array
    if( tab_id >= 0 && tab_id < 14 )
    {
        game->tab_interface_id[tab_id] = component_id;
        printf("IF_SETTAB: Set tab %d to component %d\n", tab_id, component_id);
    }
    else
    {
        printf("IF_SETTAB: Invalid tab_id %d (must be 0-13)\n", tab_id);
    }
}

void
gameproto_exec_if_settab_active(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    int tab_id = packet->_if_settab_active.tab_id;

    if( tab_id >= 0 && tab_id < 14 )
    {
        game->selected_tab = tab_id;
        printf("IF_SETTAB_ACTIVE: Set active tab to %d\n", tab_id);
    }
    else
    {
        printf("IF_SETTAB_ACTIVE: Invalid tab_id %d (must be 0-13)\n", tab_id);
    }
}

void
gameproto_exec_lc245_2(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    switch( packet->packet_type )
    {
    case PKTIN_LC245_2_REBUILD_NORMAL:
        gameproto_exec_rebuild_normal(game, packet);
        break;
    case PKTIN_LC245_2_NPC_INFO:
        gameproto_exec_npc_info(game, packet);
        break;
    case PKTIN_LC245_2_PLAYER_INFO:
        gameproto_exec_player_info(game, packet);
        break;
    case PKTIN_LC245_2_UPDATE_INV_FULL:
        gameproto_exec_update_inv_full(game, packet);
        break;
    case PKTIN_LC245_2_IF_SETTAB:
        gameproto_exec_if_settab(game, packet);
        break;
    case PKTIN_LC245_2_IF_SETTAB_ACTIVE:
        gameproto_exec_if_settab_active(game, packet);
        break;
    default:
        break;
    }
}