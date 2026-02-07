#include "gameproto_exec.h"

#include "dash_utils.h"
#include "datatypes/appearances.h"
#include "datatypes/player_appearance.h"
#include "entity_scenebuild.h"
#include "model_transforms.h"
#include "osrs/_light_model_default.u.c"
#include "packets/pkt_npc_info.h"
#include "packets/pkt_player_info.h"
#include "rscache/bitbuffer.h"
#include "rscache/rsbuf.h"
#include "rscache/tables/model.h"

static void
npc_move(
    struct GGame* game,
    int npc_entity_id,
    int x,
    int z)
{
    struct NPCEntity* npc_entity = &game->npcs[npc_entity_id];

    // Hack
    struct SceneElement* scene_element = (struct SceneElement*)npc_entity->scene_element;

    int dx = x / 128 - npc_entity->pathing.route_x[0];
    int dz = z / 128 - npc_entity->pathing.route_z[0];
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
        npc_entity->pathing.route_x[0] = x / 128 + 64;
        npc_entity->pathing.route_z[0] = z / 128 + 64;
        npc_entity->pathing.route_run[0] = 0;
    }
    else
    {
        npc_entity->pathing.route_length = 0;
        npc_entity->pathing.route_x[0] = x / 128;
        npc_entity->pathing.route_z[0] = z / 128;

        npc_entity->position.x = x + 64;
        npc_entity->position.z = z + 64;
    }

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
            printf("PKT_NPC_INFO_OP_ADD_NPC_NEW_OPBITS_PID: %d\n", npc_id);

            break;
        }
        case PKT_NPC_INFO_OP_ADD_NPC_OLD_OPBITS_IDX:
        {
            assert(op->_bitvalue >= game->npc_count);
            npc_id = game->active_npcs[op->_bitvalue];
            game->active_npcs[game->npc_count] = npc_id;
            printf(
                "PKT_NPC_INFO_OP_ADD_NPC_OLD_OPBITS_IDX: %d, %d, %d\n",
                npc_id,
                op->_bitvalue,
                game->npc_count);
            game->npc_count += 1;

            break;
        }
        case PKT_NPC_INFO_OP_SET_NPC_OPBITS_IDX:
        {
            npc_id = game->active_npcs[op->_bitvalue];
            printf("PKT_NPC_INFO_OP_SET_NPC_OPBITS_IDX: %d, %d\n", npc_id, op->_bitvalue);
            break;
        }
        case PKT_NPC_INFO_OP_CLEAR_NPC_OPBITS_IDX:
        {
            npc_id = game->active_npcs[op->_bitvalue];
            printf("PKT_NPC_INFO_OP_CLEAR_NPC_OPBITS_IDX: %d, %d\n", npc_id, op->_bitvalue);
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
            int dz = op->_bitvalue;
            npc->position.z = (dz * 128) + (player->position.z / 128 * 128);
            npc_move(game, npc_id, npc->position.x, npc->position.z);
            break;
        }
        case PKT_NPC_INFO_OPBITS_DX:
        {
            int dx = op->_bitvalue;
            npc->position.x = (dx * 128) + (player->position.x / 128 * 128);
            break;
        }
        case PKT_NPC_INFO_OPBITS_WALKDIR:
        case PKT_NPC_INFO_OPBITS_RUNDIR:
        {
            int direction = op->_bitvalue;
            int next_x = npc->pathing.route_x[0];
            int next_z = npc->pathing.route_z[0];
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

            if( npc->pathing.route_length < 9 )
                npc->pathing.route_length++;

            for( int i = npc->pathing.route_length; i > 0; i-- )
            {
                npc->pathing.route_x[i] = npc->pathing.route_x[i - 1];
                npc->pathing.route_z[i] = npc->pathing.route_z[i - 1];
                npc->pathing.route_run[i] = npc->pathing.route_run[i - 1];
            }

            npc->pathing.route_x[0] = next_x;
            npc->pathing.route_z[0] = next_z;
            npc->pathing.route_run[0] = 0;
            break;
        }
        case PKT_NPC_INFO_OPBITS_NPCTYPE:
        {
            entity_scenebuild_npc_change_type(game, npc_id, op->_bitvalue);
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

        player->position.x = x + 64;
        player->position.z = z + 64;
    }
}

void
add_player_info(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    //
    struct BitBuffer buf;
    struct RSBuffer rsbuf;
    rsbuf_init(&rsbuf, packet->_player_info.data, packet->_player_info.length);
    bitbuffer_init_from_rsbuf(&buf, &rsbuf);
    bits(&buf);

    struct PktPlayerInfoOp ops[2048];

    struct SceneElement* scene_element = NULL;

    int count = pkt_player_info_reader_read(&player_info_reader, &packet->_player_info, ops, 2048);
    int player_id = ACTIVE_PLAYER_SLOT;

    game->player_count = 0;
    for( int i = 0; i < count; i++ )
    {
        struct PktPlayerInfoOp* op = &ops[i];

        struct PlayerEntity* player = entity_scenebuild_player_get(game, player_id);

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
        case PKT_PLAYER_INFO_OPBITS_RUNDIR:
        {
            int direction = op->_bitvalue;
            int next_x = player->pathing.route_x[0];
            int next_z = player->pathing.route_z[0];
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

            if( player->pathing.route_length < 9 )
                player->pathing.route_length++;

            for( int i = player->pathing.route_length; i > 0; i-- )
            {
                player->pathing.route_x[i] = player->pathing.route_x[i - 1];
                player->pathing.route_z[i] = player->pathing.route_z[i - 1];
            }

            player->pathing.route_x[0] = next_x;
            player->pathing.route_z[0] = next_z;
            player->pathing.route_run[0] = 0;
            break;
        }
        case PKT_PLAYER_INFO_OPBITS_DX:
        {
            int x = (game->players[ACTIVE_PLAYER_SLOT].position.x / 128 * 128);

            player->position.x = (op->_bitvalue * 128) + x;
            break;
        }
        case PKT_PLAYER_INFO_OPBITS_DZ:
        {
            int z = (game->players[ACTIVE_PLAYER_SLOT].position.z / 128 * 128);

            player->position.z = (op->_bitvalue * 128) + z;
            player_move(game, player_id, player->position.x, player->position.z);
            break;
        }
        case PKT_PLAYER_INFO_OPBITS_LOCAL_X:
        {
            player->position.x = (op->_bitvalue * 128) + 64;
            break;
        }
        case PKT_PLAYER_INFO_OPBITS_LOCAL_Z:
        {
            player->position.z = (op->_bitvalue * 128) + 64;
            break;
        }
        case PKT_PLAYER_INFO_OP_APPEARANCE:
        {
            struct PlayerAppearance appearance;
            player_appearance_decode(&appearance, op->_appearance.appearance, op->_appearance.len);

            entity_scenebuild_player_change_appearance(game, player_id, &appearance);
        }
        break;
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
    default:
        break;
    }
}