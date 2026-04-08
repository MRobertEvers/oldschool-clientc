#include "gameproto_exec.h"

#include "dash_utils.h"
#include "datatypes/appearances.h"
#include "datatypes/player_appearance.h"
#include "game_entity.h"
#include "graphics/dash.h"
#include "model_transforms.h"
#include "osrs/_light_model_default.u.c"
#include "osrs/buildcachedat.h"
#include "osrs/game.h"
#include "osrs/interface_state.h"
#include "osrs/scene2.h"
#include "osrs/zone_state.h"
#include "packets/pkt_npc_info.h"
#include "packets/pkt_player_info.h"
#include "rscache/bitbuffer.h"
#include "rscache/rsbuf.h"
#include "rscache/tables/model.h"
#include "rscache/tables_dat/config_component.h"
#include "rscache/tables_dat/config_obj.h"
#include "world_scenebuild.h"

#include <assert.h>
#include <stdlib.h>

void
LibToriRS_WorldMinimapStaticRebuild(struct GGame* game);

static struct PktNpcInfoReader npc_info_reader = { 0 };

void
gameproto_exec_npc_info_raw(
    struct GGame* game,
    void* data,
    int length)
{
    struct RevPacket_LC245_2 packet = { 0 };
    packet._npc_info.length = length;
    packet._npc_info.data = data;
    gameproto_exec_npc_info(game, &packet);
}

void
gameproto_exec_npc_info(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    npc_info_reader.extended_count = 0;
    npc_info_reader.current_op = 0;
    npc_info_reader.max_ops = 2048;
    struct PktNpcInfoOp ops[2048];
    int count = pkt_npc_info_reader_read(
        &npc_info_reader, (struct PktNpcInfo*)&packet->_npc_info, ops, 2048);

    struct PlayerEntity* player = world_player(game->world, ACTIVE_PLAYER_SLOT);
    if( !player->alive )
        return;

    int npc_id = -1;
    int prev_count = game->world->active_npc_count;
    int removed_count = 0;
    game->world->active_npc_count = 0;
    struct NPCEntity* npc = NULL;
    for( int i = 0; i < count; i++ )
    {
        struct PktNpcInfoOp* op = &ops[i];

        if( npc_id != -1 )
        {
            npc = world_npc_ensure_scene_element(game->world, npc_id);
        }
        else
        {
            npc = NULL;
        }

        switch( op->kind )
        {
        case PKT_NPC_INFO_OP_ADD_NPC_NEW_OPBITS_PID:
        {
            npc_id = op->_bitvalue;
            game->world->active_npcs[game->world->active_npc_count] = npc_id;
            game->world->active_npc_count += 1;

            break;
        }
        case PKT_NPC_INFO_OP_ADD_NPC_OLD_OPBITS_IDX:
        {
            assert(op->_bitvalue >= game->world->active_npc_count);
            npc_id = game->world->active_npcs[op->_bitvalue];
            game->world->active_npcs[game->world->active_npc_count] = npc_id;
            game->world->active_npc_count += 1;

            break;
        }
        case PKT_NPC_INFO_OP_SET_NPC_OPBITS_IDX:
        {
            npc_id = game->world->active_npcs[op->_bitvalue];
            break;
        }
        case PKT_NPC_INFO_OP_CLEAR_NPC_OPBITS_IDX:
        {
            npc_id = game->world->active_npcs[op->_bitvalue];
            game->world->active_npcs[op->_bitvalue] = -1;
            world_cleanup_npc_entity(game->world, npc_id);
            npc_id = -1;
            break;
        }
        case PKT_NPC_INFO_OPBITS_COUNT_RESET:
        {
            for( int idx = op->_bitvalue; idx < prev_count; idx++ )
            {
                world_cleanup_npc_entity(game->world, game->world->active_npcs[idx]);
                game->world->active_npcs[idx] = -1;
            }
            break;
        }
        case PKT_NPC_INFO_OP_DELTA_XZ:
        {
            world_npc_entity_path_jump_relative_to_active(
                game->world, npc_id, false, op->_delta_xz.x, op->_delta_xz.z);
            break;
        }
        case PKT_NPC_INFO_OPBITS_WALKDIR:
        case PKT_NPC_INFO_OPBITS_RUNDIR:
        {
            int direction = op->_bitvalue;
            world_npc_entity_path_push_step(
                game->world,
                npc_id,
                op->kind == PKT_NPC_INFO_OPBITS_RUNDIR ? PATHSTEP_RUN : PATHSTEP_WALK,
                direction);
            break;
        }
        case PKT_NPC_INFO_OPBITS_NPCTYPE:
        {
            world_scenebuild_npc_entity_set_npc_type(game->world, npc_id, op->_bitvalue);
            break;
        }
        case PKT_NPC_INFO_OP_FACE_ENTITY:
        {
            if( !npc )
                break;
            int entity_id = (int)op->_face_entity.entity_id;
            if( entity_id == 65535 )
                entity_id = -1;
            // npc->orientation.face_entity = entity_id;
            // printf("npc_face_entity: %d\n", entity_id);
            break;
        }
        case PKT_NPC_INFO_OP_FACE_COORD:
        {
            if( !npc )
                break;
            // npc->orientation.face_square_x = (int)op->_face_coord.x;
            // npc->orientation.face_square_z = (int)op->_face_coord.z;
            // printf(
            //     "npc_face_coord: %d, %d\n",
            //     npc->orientation.face_square_x,
            //     npc->orientation.face_square_z);
            break;
        }
        case PKT_NPC_INFO_OP_SEQUENCE:
        {
            if( !npc )
                break;
            int seq_id = (int)op->_sequence.sequence_id;
            if( seq_id == 65535 )
                seq_id = -1;
            world_npc_entity_set_animation(game->world, npc_id, seq_id, ANIMATION_TYPE_PRIMARY);
            break;
        }
        case PKT_NPC_INFO_OP_DAMAGE:
        {
            // entity_add_hitmark(
            //     npc->damage_values,
            //     npc->damage_types,
            //     npc->damage_cycles,
            //     game->cycle,
            //     op->_damage.damage_type,
            //     op->_damage.damage);
            npc->combat_cycle = game->cycle + 400;
            npc->health = op->_damage.health;
            npc->total_health = op->_damage.total_health;
            break;
        }
        }
    }
}

static struct PktPlayerInfoReader player_info_reader = { 0 };

void
add_player_info(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    struct BitBuffer buf;
    struct RSBuffer rsbuf;
    rsbuf_init(&rsbuf, packet->_player_info.data, packet->_player_info.length);
    bitbuffer_init_from_rsbuf(&buf, &rsbuf);
    bits(&buf);

    struct PktPlayerInfoOp ops[2048];

    struct SceneElement* scene_element = NULL;
    struct PlayerEntity* active_player = world_player(game->world, ACTIVE_PLAYER_SLOT);

    int count = pkt_player_info_reader_read(
        &player_info_reader, (struct PktPlayerInfo*)&packet->_player_info, ops, 2048);
    int player_id = -1;

    struct PlayerEntity* player = NULL;

    game->world->active_player_count = 0;
    for( int i = 0; i < count; i++ )
    {
        struct PktPlayerInfoOp* op = &ops[i];

        player =
            (player_id >= 0) ? world_player_ensure_scene_element(game->world, player_id) : NULL;

        switch( op->kind )
        {
        case PKT_PLAYER_INFO_OP_SET_LOCAL_PLAYER:
        {
            player_id = ACTIVE_PLAYER_SLOT;
            break;
        }
        case PKT_PLAYER_INFO_OP_ADD_PLAYER_OLD_OPBITS_IDX:
        {
            player_id = game->world->active_players[op->_bitvalue];
            game->world->active_players[game->world->active_player_count] = player_id;
            game->world->active_player_count += 1;
            break;
        }
        case PKT_PLAYER_INFO_OP_ADD_PLAYER_NEW_OPBITS_PID:
        {
            player_id = op->_bitvalue;
            game->world->active_players[game->world->active_player_count] = player_id;
            game->world->active_player_count += 1;
            break;
        }
        case PKT_PLAYER_INFO_OP_SET_PLAYER_OPBITS_IDX:
        {
            player_id = game->world->active_players[op->_bitvalue];
            break;
        }
        case PKT_PLAYER_INFO_OP_CLEAR_PLAYER_OPBITS_IDX:
        {
            player_id = game->world->active_players[op->_bitvalue];
            world_cleanup_player_entity(game->world, player_id);
            game->world->active_players[op->_bitvalue] = -1;
            player_id = -1;
            break;
        }
        case PKT_PLAYER_INFO_OPBITS_WALKDIR:
        case PKT_PLAYER_INFO_OPBITS_RUNDIR:
        {
            if( !player )
                break;
            int direction = op->_bitvalue;

            world_player_entity_path_push_step(
                game->world,
                player_id,
                op->kind == PKT_PLAYER_INFO_OPBITS_RUNDIR ? PATHSTEP_RUN : PATHSTEP_WALK,
                direction);
            break;
        }
        case PKT_PLAYER_INFO_OP_DELTA_XZ:
        {
            if( !player )
                break;

            world_player_entity_path_jump_relative_to_active(
                game->world, player_id, op->_delta_xz.jump, op->_delta_xz.dx, op->_delta_xz.dz);
            break;
        }
        case PKT_PLAYER_INFO_OP_LOCAL_XZLEVEL:
        {
            if( !player )
                break;

            world_player_entity_path_jump(
                game->world,
                player_id,
                op->_local_xz_level.jump,
                op->_local_xz_level.x,
                op->_local_xz_level.z);

            break;
        }
        case PKT_PLAYER_INFO_OP_APPEARANCE:
        {
            if( player_id < 0 )
                break;
            struct PlayerAppearance appearance;
            player_appearance_decode(&appearance, op->_appearance.appearance, op->_appearance.len);

            world_player_entity_set_appearance(game->world, player_id, &appearance);
        }
        break;
        case PKT_PLAYER_INFO_OP_FACE_ENTITY:
        {
            if( !player )
                break;
            int entity_id = (int)op->_face_entity.entity_id;
            if( entity_id == 65535 )
                entity_id = -1;
            // player->orientation.face_entity = entity_id;

            break;
        }
        case PKT_PLAYER_INFO_OP_FACE_COORD:
        {
            if( !player )
                break;
            // player->orientation.face_square_x = (int)op->_face_coord.x;
            // player->orientation.face_square_z = (int)op->_face_coord.z;
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
            world_player_entity_set_animation(
                game->world, player_id, seq_id, ANIMATION_TYPE_PRIMARY);
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
    int zone_center_x = packet->_map_rebuild.zonex;
    int zone_center_z = packet->_map_rebuild.zonez;
    int zone_sw_x = zone_center_x - zone_padding;
    int zone_sw_z = zone_center_z - zone_padding;

    int levels = MAP_TERRAIN_LEVELS;

    // /* Client.ts: when scene base changes, update entity coordinates (route + position) */
    // int prev_base_x = game->scene_base_tile_x;
    // int prev_base_z = game->scene_base_tile_z;
    // int new_base_x = zone_sw_x * 8;
    // int new_base_z = zone_sw_z * 8;
    // int dx = new_base_x - prev_base_x;
    // int dz = new_base_z - prev_base_z;

    // world_buildcachedat_rebuild_centerzone(game->world, zone_center_x, zone_center_z,
    // SCENE_WIDTH);

    // // game->sys_painter = painter_new(SCENE_WIDTH, SCENE_WIDTH, levels);
    // // game->sys_painter_buffer = painter_buffer_new();
    // // game->sys_minimap =
    // //     minimap_new(zone_sw_x * 8, zone_sw_z * 8, zone_sw_x * 8 + 104, zone_sw_z * 8 + 104,
    // //     levels);
    // // game->scenebuilder = scenebuilder_new_painter(game->sys_painter, game->sys_minimap);

    // /* REBUILD_NORMAL: zone is in 8-tile units (pkt_rebuild_normal.lua wx_sw = zone_sw_x * 8).
    //  */
    // game->scene_base_tile_x = new_base_x;
    // game->scene_base_tile_z = new_base_z;

    LibToriRS_WorldMinimapStaticRebuild(game);

    // /* Clear dynamic zone state on rebuild (Client.ts clears objStacks and locChanges) */
    // for( int level = 0; level < ZONE_LEVELS; level++ )
    // {
    //     for( int x = 0; x < ZONE_SCENE_SIZE; x++ )
    //     {
    //         for( int z = 0; z < ZONE_SCENE_SIZE; z++ )
    //         {
    //             if( game->obj_stack_elements[level][x][z] )
    //             {
    //                 scene_element_free(game->obj_stack_elements[level][x][z]);
    //                 game->obj_stack_elements[level][x][z] = NULL;
    //             }
    //             struct ObjStackEntry* entry = game->obj_stacks[level][x][z];
    //             while( entry )
    //             {
    //                 struct ObjStackEntry* next = entry->next;
    //                 free(entry);
    //                 entry = next;
    //             }
    //             game->obj_stacks[level][x][z] = NULL;
    //         }
    //     }
    // }
    // struct LocChangeEntry* loc = game->loc_changes_head;
    // while( loc )
    // {
    //     struct LocChangeEntry* next = loc->next;
    //     free(loc);
    //     loc = next;
    // }
    // game->loc_changes_head = NULL;

    // game->scene = scenebuilder_load_from_buildcachedat(
    //     game->scenebuilder,
    //     zone_sw_x * 8,
    //     zone_sw_z * 8,
    //     zone_ne_x * 8,
    //     zone_ne_z * 8,
    //     104,
    //     104,
    //     game->buildcachedat);

    // /* Client.ts: npc.routeX[j] -= dx; npc.routeZ[j] -= dz; npc.x -= dx*128; npc.z -= dz*128 */
    // for( int i = 0; i < game->npc_count; i++ )
    // {
    //     int npc_id = game->active_npcs[i];
    //     if( npc_id < 0 )
    //         continue;
    //     struct NPCEntity* npc = &game->npcs[npc_id];
    //     if( !npc->alive )
    //         continue;
    //     for( int j = 0; j < 10; j++ )
    //     {
    //         npc->pathing.route_x[j] -= dx;
    //         npc->pathing.route_z[j] -= dz;
    //     }
    //     npc->position.x -= dx * 128;
    //     npc->position.z -= dz * 128;
    // }

    // /* Client.ts: same for players (iterate all like Client does) */
    // for( int i = 0; i < MAX_PLAYERS; i++ )
    // {
    //     struct PlayerEntity* player = &game->players[i];
    //     if( !player->alive )
    //         continue;
    //     for( int j = 0; j < 10; j++ )
    //     {
    //         player->pathing.route_x[j] -= dx;
    //         player->pathing.route_z[j] -= dz;
    //     }
    //     player->position.x -= dx * 128;
    //     player->position.z -= dz * 128;
    // }
}

void
gameproto_exec_rebuild_normal_world(
    struct World* world,
    struct RevPacket_LC245_2* packet)
{
#define SCENE_WIDTH 104
    int zone_padding = SCENE_WIDTH / (2 * 8);
    int zone_sw_x = packet->_map_rebuild.zonex - zone_padding;
    int zone_sw_z = packet->_map_rebuild.zonez - zone_padding;

    int prev_base_x = world->_base_tile_x;
    int prev_base_z = world->_base_tile_z;
    int new_base_x = zone_sw_x * 8;
    int new_base_z = zone_sw_z * 8;
    int dx = new_base_x - prev_base_x;
    int dz = new_base_z - prev_base_z;

    world_buildcachedat_rebuild_centerzone(
        world, packet->_map_rebuild.zonex, packet->_map_rebuild.zonez, 104);

    for( int i = 0; i < world->active_npc_count; i++ )
    {
        int npc_id = world->active_npcs[i];
        if( npc_id < 0 )
            continue;
        struct NPCEntity* npc = world_npc(world, npc_id);
        if( !npc->alive )
            continue;
        for( int j = 0; j < 10; j++ )
        {
            npc->pathing.route_x[j] -= dx;
            npc->pathing.route_z[j] -= dz;
        }
        npc->draw_position.x -= dx * 128;
        npc->draw_position.z -= dz * 128;
    }

    for( int i = 0; i < MAX_PLAYERS; i++ )
    {
        struct PlayerEntity* player = world_player(world, i);
        if( !player->alive )
            continue;
        for( int j = 0; j < 10; j++ )
        {
            player->pathing.route_x[j] -= dx;
            player->pathing.route_z[j] -= dz;
        }
        player->draw_position.x -= dx * 128;
        player->draw_position.z -= dz * 128;
    }
}

void
gameproto_exec_player_info_raw(
    struct GGame* game,
    void* data,
    int length)
{
    struct RevPacket_LC245_2 packet = { 0 };
    packet.packet_type = PKTIN_LC245_2_PLAYER_INFO;
    packet._player_info.data = data;
    packet._player_info.length = length;
    add_player_info(game, &packet);
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

    if( tab_id >= 0 && tab_id < 14 && game->iface )
    {
        game->iface->tab_interface_id[tab_id] = component_id;
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

    if( tab_id >= 0 && tab_id < 14 && game->iface )
    {
        game->iface->selected_tab = tab_id;
        printf("IF_SETTAB_ACTIVE: Set active tab to %d\n", tab_id);
    }
    else
    {
        printf("IF_SETTAB_ACTIVE: Invalid tab_id %d (must be 0-13)\n", tab_id);
    }
}

void
gameproto_exec_if_setcolour(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    int component_id = packet->_if_setcolour.component_id;
    int colour15 = packet->_if_setcolour.colour;

    struct CacheDatConfigComponent* component =
        buildcachedat_get_component(game->buildcachedat, component_id);
    if( !component )
        return;

    int r = (colour15 >> 10) & 0x1f;
    int g = (colour15 >> 5) & 0x1f;
    int b = colour15 & 0x1f;
    component->colour = (r << 19) | (g << 11) | (b << 3);
}

void
gameproto_exec_if_sethide(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    int component_id = packet->_if_sethide.component_id;
    int hide_val = packet->_if_sethide.hide;

    struct CacheDatConfigComponent* component =
        buildcachedat_get_component(game->buildcachedat, component_id);
    if( !component )
        return;

    component->hide = (hide_val == 1);
}

void
gameproto_exec_if_setobject(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    int component_id = packet->_if_setobject.component_id;
    int obj_id = packet->_if_setobject.obj_id;
    int zoom = packet->_if_setobject.zoom;

    struct CacheDatConfigComponent* component =
        buildcachedat_get_component(game->buildcachedat, component_id);
    if( !component )
        return;

    struct CacheDatConfigObj* obj = buildcachedat_get_obj(game->buildcachedat, obj_id);
    if( !obj )
        return;

    component->modelType = 4;
    component->model = obj_id;
    component->xan = obj->xan2d;
    component->yan = obj->yan2d;
    component->zoom = (obj->zoom2d * 100) / zoom;
}

void
gameproto_exec_if_setmodel(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    int component_id = packet->_if_setmodel.component_id;
    int model_id = packet->_if_setmodel.model_id;

    struct CacheDatConfigComponent* component =
        buildcachedat_get_component(game->buildcachedat, component_id);
    if( !component )
        return;

    component->modelType = 1;
    component->model = model_id;
}

void
gameproto_exec_if_setanim(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    int component_id = packet->_if_setanim.component_id;
    int anim_id = packet->_if_setanim.anim_id;

    struct CacheDatConfigComponent* component =
        buildcachedat_get_component(game->buildcachedat, component_id);
    if( !component )
        return;

    component->anim = anim_id;
}

void
gameproto_exec_if_setplayerhead(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    int component_id = packet->_if_setplayerhead.component_id;

    struct CacheDatConfigComponent* component =
        buildcachedat_get_component(game->buildcachedat, component_id);
    if( !component )
        return;

    struct PlayerEntity* local_player = world_player(game->world, ACTIVE_PLAYER_SLOT);
    if( !local_player->alive )
        return;

    int* slots = local_player->appearance.slots;
    int* colors = local_player->appearance.colors;

    component->modelType = 3;
    component->model =
        (slots[8] << 6) + (slots[0] << 12) + (colors[0] << 24) + (colors[4] << 18) + slots[11];
}

void
gameproto_exec_if_settext(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    int component_id = packet->_if_settext.component_id;
    char* new_text = packet->_if_settext.text;

    struct CacheDatConfigComponent* component =
        buildcachedat_get_component(game->buildcachedat, component_id);
    if( !component )
    {
        free(new_text);
        return;
    }

    free(component->text);
    component->text = new_text;
    packet->_if_settext.text = NULL;
}

void
gameproto_exec_if_setnpchead(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    int component_id = packet->_if_setnpchead.component_id;
    int npc_id = packet->_if_setnpchead.npc_id;

    struct CacheDatConfigComponent* component =
        buildcachedat_get_component(game->buildcachedat, component_id);
    if( !component )
        return;

    component->modelType = 2;
    component->model = npc_id;
}

void
gameproto_exec_if_setposition(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    int component_id = packet->_if_setposition.component_id;
    int x = packet->_if_setposition.x;
    int z = packet->_if_setposition.z;

    struct CacheDatConfigComponent* component =
        buildcachedat_get_component(game->buildcachedat, component_id);
    if( !component )
        return;

    component->x = x;
    component->y = z;
}

void
gameproto_exec_if_setscrollpos(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    int component_id = packet->_if_setscrollpos.component_id;
    int pos = packet->_if_setscrollpos.pos;

    if( !game->iface || component_id < 0 || component_id >= MAX_IFACE_SCROLL_IDS )
        return;

    struct CacheDatConfigComponent* component =
        buildcachedat_get_component(game->buildcachedat, component_id);
    if( !component )
        return;

    if( component->type == COMPONENT_TYPE_LAYER )
    {
        if( pos < 0 )
            pos = 0;
        int max_scroll = component->scroll - component->height;
        if( max_scroll > 0 && pos > max_scroll )
            pos = max_scroll;
    }

    game->iface->component_scroll_position[component_id] = pos;
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
    case PKTIN_LC245_2_IF_SETCOLOUR:
        gameproto_exec_if_setcolour(game, packet);
        break;
    case PKTIN_LC245_2_IF_SETHIDE:
        gameproto_exec_if_sethide(game, packet);
        break;
    case PKTIN_LC245_2_IF_SETOBJECT:
        gameproto_exec_if_setobject(game, packet);
        break;
    case PKTIN_LC245_2_IF_SETMODEL:
        gameproto_exec_if_setmodel(game, packet);
        break;
    case PKTIN_LC245_2_IF_SETANIM:
        gameproto_exec_if_setanim(game, packet);
        break;
    case PKTIN_LC245_2_IF_SETPLAYERHEAD:
        gameproto_exec_if_setplayerhead(game, packet);
        break;
    case PKTIN_LC245_2_IF_SETTEXT:
        gameproto_exec_if_settext(game, packet);
        break;
    case PKTIN_LC245_2_IF_SETNPCHEAD:
        gameproto_exec_if_setnpchead(game, packet);
        break;
    case PKTIN_LC245_2_IF_SETPOSITION:
        gameproto_exec_if_setposition(game, packet);
        break;
    case PKTIN_LC245_2_IF_SETSCROLLPOS:
        gameproto_exec_if_setscrollpos(game, packet);
        break;
    case PKTIN_LC245_2_OBJ_ADD:
        // gameproto_exec_obj_add(game, packet, game->zone_base_x, game->zone_base_z);
        break;
    case PKTIN_LC245_2_OBJ_DEL:
        gameproto_exec_obj_del(game, packet);
        break;
    case PKTIN_LC245_2_OBJ_REVEAL:
        gameproto_exec_obj_reveal(game, packet);
        break;
    case PKTIN_LC245_2_OBJ_COUNT:
        gameproto_exec_obj_count(game, packet);
        break;
    case PKTIN_LC245_2_LOC_ADD_CHANGE:
        gameproto_exec_loc_add_change(game, packet);
        break;
    case PKTIN_LC245_2_LOC_DEL:
        gameproto_exec_loc_del(game, packet);
        break;
    default:
        break;
    }
}

static int
zone_tile_x(
    struct GGame* game,
    int pos)
{
    return 0;
    // return game->zone_base_x + ((pos >> 4) & 0x7);
}

static int
zone_tile_z(
    struct GGame* game,
    int pos)
{
    return 0;
    // return game->zone_base_z + (pos & 0x7);
}

void
gameproto_exec_obj_add(
    struct GGame* game,
    struct RevPacket_LC245_2* packet,
    int zone_base_x,
    int zone_base_z)
{
    // /* Client.ts: x = baseX + (pos>>4)&7, z = baseZ + pos&7; use directly as scene indices */
    // int sx = zone_base_x + ((packet->_obj_add.pos >> 4) & 0x7);
    // int sz = zone_base_z + (packet->_obj_add.pos & 0x7);
    // int obj_id = packet->_obj_add.obj_id & 0x7fff;
    // int count = packet->_obj_add.count;
    // int level = 0; /* TODO: use current level */

    // if( sx < 0 || sx >= ZONE_SCENE_SIZE || sz < 0 || sz >= ZONE_SCENE_SIZE )
    //     return;

    // struct ObjStackEntry* entry = malloc(sizeof(struct ObjStackEntry));
    // entry->obj_id = obj_id;
    // entry->count = count;
    // entry->next = game->obj_stacks[level][sx][sz];
    // game->obj_stacks[level][sx][sz] = entry;
}

void
gameproto_exec_obj_del(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    // int sx = zone_tile_x(game, packet->_obj_del.pos);
    // int sz = zone_tile_z(game, packet->_obj_del.pos);
    // int obj_id = packet->_obj_del.obj_id & 0x7fff;
    // int level = 0;

    // if( sx < 0 || sx >= ZONE_SCENE_SIZE || sz < 0 || sz >= ZONE_SCENE_SIZE )
    //     return;

    // struct ObjStackEntry** prev = &game->obj_stacks[level][sx][sz];
    // for( struct ObjStackEntry* e = *prev; e; prev = &e->next, e = e->next )
    // {
    //     if( e->obj_id == obj_id )
    //     {
    //         *prev = e->next;
    //         free(e);
    //         break;
    //     }
    // }
}

void
gameproto_exec_obj_reveal(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    if( packet->_obj_reveal.receiver == ACTIVE_PLAYER_SLOT )
        return; /* Client.ts: skip if receiver is local player */
    struct RevPacket_LC245_2 add_pkt = { 0 };
    add_pkt._obj_add.pos = packet->_obj_reveal.pos;
    add_pkt._obj_add.obj_id = packet->_obj_reveal.obj_id;
    add_pkt._obj_add.count = packet->_obj_reveal.count;
    // gameproto_exec_obj_add(game, &add_pkt, game->zone_base_x, game->zone_base_z);
}

void
gameproto_exec_obj_count(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    int sx = zone_tile_x(game, packet->_obj_count.pos);
    int sz = zone_tile_z(game, packet->_obj_count.pos);
    int obj_id = packet->_obj_count.obj_id & 0x7fff;
    int old_count = packet->_obj_count.old_count;
    int new_count = packet->_obj_count.new_count;
    int level = 0;

    // if( sx < 0 || sx >= ZONE_SCENE_SIZE || sz < 0 || sz >= ZONE_SCENE_SIZE )
    //     return;

    // for( struct ObjStackEntry* e = game->obj_stacks[level][sx][sz]; e; e = e->next )
    // {
    //     if( e->obj_id == obj_id && e->count == old_count )
    //     {
    //         e->count = new_count;
    //         break;
    //     }
    // }
}

void
gameproto_exec_loc_add_change(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    int x = zone_tile_x(game, packet->_loc_add_change.pos);
    int z = zone_tile_z(game, packet->_loc_add_change.pos);
    int info = packet->_loc_add_change.info;
    int shape = info >> 2;
    int angle = info & 0x3;
    int loc_id = packet->_loc_add_change.loc_id;
    /* TODO: resolve layer from LocShape; add to loc_changes_head; apply when models ready */
    (void)shape;
    (void)angle;
    (void)loc_id;

    // if( x < 0 || x >= ZONE_SCENE_SIZE || z < 0 || z >= ZONE_SCENE_SIZE )
    //     return;

    // struct LocChangeEntry* entry = malloc(sizeof(struct LocChangeEntry));
    // entry->level = 0;
    // entry->x = x;
    // entry->z = z;
    // entry->layer = 0; /* TODO: LocShape.of(shape).layer */
    // entry->old_type = -1;
    // entry->new_type = loc_id;
    // entry->old_shape = 0;
    // entry->new_shape = shape;
    // entry->old_angle = 0;
    // entry->new_angle = angle;
    // entry->start_time = 0;
    // entry->end_time = -1;
    // entry->next = game->loc_changes_head;
    // game->loc_changes_head = entry;
}

void
gameproto_exec_loc_del(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    int x = zone_tile_x(game, packet->_loc_del.pos);
    int z = zone_tile_z(game, packet->_loc_del.pos);
    int info = packet->_loc_del.info;
    int shape = info >> 2;
    int angle = info & 0x3;
    /* TODO: add LocChangeEntry with new_type=-1 to remove */
    (void)shape;
    (void)angle;

    // if( x < 0 || x >= ZONE_SCENE_SIZE || z < 0 || z >= ZONE_SCENE_SIZE )
    //     return;

    // struct LocChangeEntry* entry = malloc(sizeof(struct LocChangeEntry));
    // entry->level = 0;
    // entry->x = x;
    // entry->z = z;
    // entry->layer = 0;
    // entry->old_type = 0; /* TODO: get from scene */
    // entry->new_type = -1;
    // entry->old_shape = shape;
    // entry->new_shape = 0;
    // entry->old_angle = angle;
    // entry->new_angle = 0;
    // entry->start_time = 0;
    // entry->end_time = -1;
    // entry->next = game->loc_changes_head;
    // game->loc_changes_head = entry;
}
