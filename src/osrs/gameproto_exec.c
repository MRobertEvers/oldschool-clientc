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
#include "osrs/revconfig/uitree_load.h"
#include "osrs/player_stats.h"
#include "osrs/scene2.h"
#include "osrs/varp_varbit_manager.h"
#include "osrs/zone_state.h"
#include "osrs/rscache/tables/maps.h"
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
#include <string.h>

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
    int zonex = packet->_map_rebuild.zonex;
    int zonez = packet->_map_rebuild.zonez;

    /* Jagfile + cache lifecycle for configs/versionlist/media is owned by Lua pkt_dispatch
     * (clear/reload around rebuild). Do not clear buildcachedat here or subsequent PLAYER_INFO /
     * NPC_INFO in the same script batch lose idk/npc/obj/models/sequences. */

    /* Compute new base tile to shift game-level state (minimap flag). */
    int zone_padding = 104 / (2 * 8);
    int new_base_x = (zonex - zone_padding) * 8;
    int new_base_z = (zonez - zone_padding) * 8;
    int dx = 0;
    int dz = 0;

    if( !game->world )
    {
        /* First rebuild: create the world. */
        game->world = world_new(game->buildcachedat, game->scene2);
    }
    else
    {
        dx = new_base_x - game->world->_base_tile_x;
        dz = new_base_z - game->world->_base_tile_z;
    }

    gameproto_exec_rebuild_normal_world(game->world, packet);

    /* Shift game-level minimap flag (lives on GGame, not World). */
    if( game->minimap_flag_has && (dx || dz) )
    {
        game->minimap_flag_x -= dx;
        game->minimap_flag_z -= dz;
    }

    LibToriRS_WorldMinimapStaticRebuild(game);
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

    /* Shift loc-entity scene coordinates; discard those now out of bounds. */
    for( int i = world->active_loc_entity_count - 1; i >= 0; i-- )
    {
        int loc_id = world->active_loc_entities[i];
        if( loc_id < 0 )
            continue;
        struct MapBuildLocEntity* loc = world_loc_entity(world, loc_id);
        int new_sx = (int)loc->scene_coord.sx - dx;
        int new_sz = (int)loc->scene_coord.sz - dz;
        if( new_sx < 0 || new_sx >= SCENE_WIDTH || new_sz < 0 || new_sz >= SCENE_WIDTH )
        {
            world_cleanup_map_build_loc_entity(world, loc_id);
        }
        else
        {
            loc->scene_coord.sx = (uint32_t)new_sx;
            loc->scene_coord.sz = (uint32_t)new_sz;
        }
    }

    /* Shift projectile fine-grained draw positions. */
    for( int i = 0; i < world->active_projectile_count; i++ )
    {
        int proj_id = world->active_projectiles[i];
        if( proj_id < 0 )
            continue;
        struct ProjectileEntity* proj = world_projectile(world, proj_id);
        if( !proj->alive )
            continue;
        proj->draw_position.x -= dx * 128;
        proj->draw_position.z -= dz * 128;
    }

    /* Update scene base tile. */
    world->_base_tile_x = new_base_x;
    world->_base_tile_z = new_base_z;
#undef SCENE_WIDTH
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

    if( component_id == 65535 )
        component_id = -1;

    if( tab_id >= 0 && tab_id < 14 && game->iface )
    {
        int old = game->iface->tab_interface_id[tab_id];
        game->iface->tab_interface_id[tab_id] = component_id;
        if( old != component_id )
            uitree_expand_sidebar_for_tab(game, tab_id, component_id);
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
        packet->_if_settext.text = NULL;
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
    case PKTIN_LC245_2_IF_OPENCHAT:
        gameproto_exec_if_openchat(game, packet);
        break;
    case PKTIN_LC245_2_IF_OPENMAIN:
        gameproto_exec_if_openmain(game, packet);
        break;
    case PKTIN_LC245_2_IF_OPENSIDE:
        gameproto_exec_if_openside(game, packet);
        break;
    case PKTIN_LC245_2_IF_OPENMAIN_SIDE:
        gameproto_exec_if_openmain_side(game, packet);
        break;
    case PKTIN_LC245_2_IF_CLOSE:
        gameproto_exec_if_close(game, packet);
        break;
    case PKTIN_LC245_2_UPDATE_INV_STOP_TRANSMIT:
        gameproto_exec_update_inv_stop_transmit(game, packet);
        break;
    case PKTIN_LC245_2_UPDATE_INV_PARTIAL:
        gameproto_exec_update_inv_partial(game, packet);
        break;
    case PKTIN_LC245_2_CAM_LOOKAT:
        gameproto_exec_cam_lookat(game, packet);
        break;
    case PKTIN_LC245_2_CAM_MOVETO:
        gameproto_exec_cam_moveto(game, packet);
        break;
    case PKTIN_LC245_2_CAM_SHAKE:
        gameproto_exec_cam_shake(game, packet);
        break;
    case PKTIN_LC245_2_CAM_RESET:
        gameproto_exec_cam_reset(game, packet);
        break;
    case PKTIN_LC245_2_UNSET_MAP_FLAG:
        gameproto_exec_unset_map_flag(game, packet);
        break;
    case PKTIN_LC245_2_UPDATE_RUNWEIGHT:
        gameproto_exec_update_runweight(game, packet);
        break;
    case PKTIN_LC245_2_UPDATE_RUNENERGY:
        gameproto_exec_update_runenergy(game, packet);
        break;
    case PKTIN_LC245_2_UPDATE_STAT:
        gameproto_exec_update_stat(game, packet);
        break;
    case PKTIN_LC245_2_HINT_ARROW:
        gameproto_exec_hint_arrow(game, packet);
        break;
    case PKTIN_LC245_2_RESET_ANIMS:
        gameproto_exec_reset_anims(game, packet);
        break;
    case PKTIN_LC245_2_UPDATE_PID:
        gameproto_exec_update_pid(game, packet);
        break;
    case PKTIN_LC245_2_VARP_SMALL:
        gameproto_exec_varp_small(game, packet);
        break;
    case PKTIN_LC245_2_VARP_LARGE:
        gameproto_exec_varp_large(game, packet);
        break;
    case PKTIN_LC245_2_RESET_CLIENT_VARCACHE:
        gameproto_exec_varp_sync(game, packet);
        break;
    case PKTIN_LC245_2_UPDATE_ZONE_PARTIAL_FOLLOWS:
        gameproto_exec_update_zone_partial_follows(game, packet);
        break;
    case PKTIN_LC245_2_UPDATE_ZONE_FULL_FOLLOWS:
        gameproto_exec_update_zone_full_follows(game, packet);
        break;
    case PKTIN_LC245_2_UPDATE_ZONE_PARTIAL_ENCLOSED:
        gameproto_exec_update_zone_partial_enclosed(game, packet);
        break;
    case PKTIN_LC245_2_LOC_ANIM:
        gameproto_exec_loc_anim(game, packet);
        break;
    case PKTIN_LC245_2_LOC_MERGE:
        gameproto_exec_loc_merge(game, packet);
        break;
    case PKTIN_LC245_2_MAP_ANIM:
        gameproto_exec_map_anim(game, packet);
        break;
    case PKTIN_LC245_2_MAP_PROJANIM:
        gameproto_exec_map_projanim(game, packet);
        break;
    case PKTIN_LC245_2_SET_MULTIWAY:
        gameproto_exec_set_multiway(game, packet);
        break;
    case PKTIN_LC245_2_OBJ_ADD:
        gameproto_exec_obj_add(game, packet, game->zone_base_x, game->zone_base_z);
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
    return game->zone_base_x + ((pos >> 4) & 0x7);
}

static int
zone_tile_z(
    struct GGame* game,
    int pos)
{
    return game->zone_base_z + (pos & 0x7);
}

/* ---- Zone-state helpers ---- */

static struct ObjStackEntry**
game_obj_stacks_ensure(struct GGame* game)
{
    if( !game->obj_stacks )
    {
        int total = MAP_TERRAIN_LEVELS * ZONE_SCENE_SIZE * ZONE_SCENE_SIZE;
        game->obj_stacks = (struct ObjStackEntry***)
            calloc(MAP_TERRAIN_LEVELS, sizeof(struct ObjStackEntry**));
        for( int l = 0; l < MAP_TERRAIN_LEVELS; l++ )
        {
            game->obj_stacks[l] = (struct ObjStackEntry**)
                calloc(ZONE_SCENE_SIZE * ZONE_SCENE_SIZE, sizeof(struct ObjStackEntry*));
        }
        (void)total;
    }
    return (struct ObjStackEntry**)game->obj_stacks;
}

void
gameproto_exec_obj_add(
    struct GGame* game,
    struct RevPacket_LC245_2* packet,
    int zone_base_x,
    int zone_base_z)
{
    int sx = zone_base_x + ((packet->_obj_add.pos >> 4) & 0x7);
    int sz = zone_base_z + (packet->_obj_add.pos & 0x7);
    int obj_id = packet->_obj_add.obj_id & 0x7fff;
    int count  = packet->_obj_add.count;
    int level  = 0;

    if( sx < 0 || sx >= ZONE_SCENE_SIZE || sz < 0 || sz >= ZONE_SCENE_SIZE )
        return;

    game_obj_stacks_ensure(game);
    struct ObjStackEntry* entry = (struct ObjStackEntry*)malloc(sizeof(struct ObjStackEntry));
    entry->obj_id = obj_id;
    entry->count  = count;
    entry->next   = game->obj_stacks[level][sx * ZONE_SCENE_SIZE + sz];
    game->obj_stacks[level][sx * ZONE_SCENE_SIZE + sz] = entry;
}

void
gameproto_exec_obj_del(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    int sx = zone_tile_x(game, packet->_obj_del.pos);
    int sz = zone_tile_z(game, packet->_obj_del.pos);
    int obj_id = packet->_obj_del.obj_id & 0x7fff;
    int level  = 0;

    if( !game->obj_stacks )
        return;
    if( sx < 0 || sx >= ZONE_SCENE_SIZE || sz < 0 || sz >= ZONE_SCENE_SIZE )
        return;

    struct ObjStackEntry** head = &game->obj_stacks[level][sx * ZONE_SCENE_SIZE + sz];
    for( struct ObjStackEntry* e = *head; e; e = e->next )
    {
        if( e->obj_id == obj_id )
        {
            *head = e->next;
            free(e);
            break;
        }
        head = &e->next;
    }
}

void
gameproto_exec_obj_reveal(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    if( packet->_obj_reveal.receiver == ACTIVE_PLAYER_SLOT )
        return;
    struct RevPacket_LC245_2 add_pkt = { 0 };
    add_pkt._obj_add.pos    = packet->_obj_reveal.pos;
    add_pkt._obj_add.obj_id = packet->_obj_reveal.obj_id;
    add_pkt._obj_add.count  = packet->_obj_reveal.count;
    gameproto_exec_obj_add(game, &add_pkt, game->zone_base_x, game->zone_base_z);
}

void
gameproto_exec_obj_count(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    int sx     = zone_tile_x(game, packet->_obj_count.pos);
    int sz     = zone_tile_z(game, packet->_obj_count.pos);
    int obj_id = packet->_obj_count.obj_id & 0x7fff;
    int old_count = packet->_obj_count.old_count;
    int new_count = packet->_obj_count.new_count;
    int level  = 0;

    if( !game->obj_stacks )
        return;
    if( sx < 0 || sx >= ZONE_SCENE_SIZE || sz < 0 || sz >= ZONE_SCENE_SIZE )
        return;

    for( struct ObjStackEntry* e = game->obj_stacks[level][sx * ZONE_SCENE_SIZE + sz]; e; e = e->next )
    {
        if( e->obj_id == obj_id && e->count == old_count )
        {
            e->count = new_count;
            break;
        }
    }
}

void
gameproto_exec_loc_add_change(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    int x    = zone_tile_x(game, packet->_loc_add_change.pos);
    int z    = zone_tile_z(game, packet->_loc_add_change.pos);
    int info = packet->_loc_add_change.info;
    int shape = info >> 2;
    int angle = info & 0x3;
    int loc_id = packet->_loc_add_change.loc_id;

    if( x < 0 || x >= ZONE_SCENE_SIZE || z < 0 || z >= ZONE_SCENE_SIZE )
        return;

    struct LocChangeEntry* entry = (struct LocChangeEntry*)malloc(sizeof(struct LocChangeEntry));
    entry->level     = 0;
    entry->x         = x;
    entry->z         = z;
    entry->layer     = 0; /* Layer from LocShape would require config lookup */
    entry->old_type  = -1;
    entry->new_type  = loc_id;
    entry->old_shape = 0;
    entry->new_shape = shape;
    entry->old_angle = 0;
    entry->new_angle = angle;
    entry->start_time = 0;
    entry->end_time   = -1;
    entry->anim_seq_id = -1;
    entry->next       = game->loc_changes_head;
    game->loc_changes_head = entry;
}

void
gameproto_exec_loc_del(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    int x    = zone_tile_x(game, packet->_loc_del.pos);
    int z    = zone_tile_z(game, packet->_loc_del.pos);
    int info = packet->_loc_del.info;
    int shape = info >> 2;
    int angle = info & 0x3;

    if( x < 0 || x >= ZONE_SCENE_SIZE || z < 0 || z >= ZONE_SCENE_SIZE )
        return;

    struct LocChangeEntry* entry = (struct LocChangeEntry*)malloc(sizeof(struct LocChangeEntry));
    entry->level     = 0;
    entry->x         = x;
    entry->z         = z;
    entry->layer     = 0;
    entry->old_type  = 0;
    entry->new_type  = -1;
    entry->old_shape = shape;
    entry->new_shape = 0;
    entry->old_angle = angle;
    entry->new_angle = 0;
    entry->start_time = 0;
    entry->end_time   = -1;
    entry->anim_seq_id = -1;
    entry->next       = game->loc_changes_head;
    game->loc_changes_head = entry;
}

void
gameproto_exec_if_openchat(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    if( game->iface )
        game->iface->chat_interface_id = packet->_if_openchat.component_id;
}

void
gameproto_exec_if_openmain(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    if( game->iface )
        game->iface->viewport_interface_id = packet->_if_openmain.component_id;
}

void
gameproto_exec_if_openside(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    if( game->iface )
        game->iface->sidebar_interface_id = packet->_if_openside.component_id;
}

void
gameproto_exec_if_openmain_side(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    if( game->iface )
    {
        game->iface->viewport_interface_id = packet->_if_openmain_side.main_component_id;
        game->iface->sidebar_interface_id  = packet->_if_openmain_side.side_component_id;
    }
}

void
gameproto_exec_if_close(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    (void)packet;
    if( game->iface )
    {
        game->iface->viewport_interface_id = -1;
        game->iface->sidebar_interface_id  = -1;
        game->iface->chat_interface_id     = -1;
    }
}

void
gameproto_exec_update_inv_stop_transmit(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    int component_id = packet->_update_inv_stop_transmit.component_id;
    struct CacheDatConfigComponent* component =
        buildcachedat_get_component(game->buildcachedat, component_id);
    if( !component )
        return;
    int max_slots = component->width * component->height;
    for( int i = 0; i < max_slots; i++ )
    {
        if( component->invSlotObjId )
            component->invSlotObjId[i] = 0;
        if( component->invSlotObjCount )
            component->invSlotObjCount[i] = 0;
    }
}

void
gameproto_exec_update_inv_partial(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    int component_id = packet->_update_inv_partial.component_id;
    struct CacheDatConfigComponent* component =
        buildcachedat_get_component(game->buildcachedat, component_id);
    if( !component || !component->invSlotObjId || !component->invSlotObjCount )
        return;
    int max_slots = component->width * component->height;
    for( int i = 0; i < packet->_update_inv_partial.count; i++ )
    {
        int slot   = packet->_update_inv_partial.entries[i].slot;
        int obj_id = packet->_update_inv_partial.entries[i].obj_id;
        int count  = packet->_update_inv_partial.entries[i].count;
        if( slot >= 0 && slot < max_slots )
        {
            component->invSlotObjId[slot]    = obj_id;
            component->invSlotObjCount[slot] = count;
        }
    }
}

void
gameproto_exec_cam_lookat(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    if( !game->world )
        return;
    int tile_x = game->world->_base_tile_x + packet->_cam_lookat.local_x;
    int tile_z = game->world->_base_tile_z + packet->_cam_lookat.local_z;
    game->camera_world_x = tile_x * 128 + 64;
    game->camera_world_z = tile_z * 128 + 64;
    game->camera_world_y = -packet->_cam_lookat.height;
}

void
gameproto_exec_cam_moveto(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    if( !game->world )
        return;
    int tile_x = game->world->_base_tile_x + packet->_cam_moveto.local_x;
    int tile_z = game->world->_base_tile_z + packet->_cam_moveto.local_z;
    game->camera_world_x = tile_x * 128 + 64;
    game->camera_world_z = tile_z * 128 + 64;
    game->camera_world_y = -packet->_cam_moveto.height;
}

void
gameproto_exec_cam_shake(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    (void)game;
    (void)packet;
}

void
gameproto_exec_cam_reset(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    (void)game;
    (void)packet;
}

void
gameproto_exec_unset_map_flag(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    (void)packet;
    game->minimap_flag_has = 0;
    game->minimap_flag_x   = 0;
    game->minimap_flag_z   = 0;
}

void
gameproto_exec_update_runweight(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    game->run_weight = packet->_update_runweight.run_weight;
}

void
gameproto_exec_update_runenergy(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    game->run_energy = packet->_update_run_energy.run_energy;
}

void
gameproto_exec_update_stat(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    int stat = packet->_update_stat.stat;
    if( stat < 0 || stat >= PLAYER_STAT_COUNT )
        return;
    game->player_stat_xp[stat]    = packet->_update_stat.xp;
    game->player_stat_level[stat] = packet->_update_stat.level;
}

void
gameproto_exec_hint_arrow(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    game->hint_arrow_type      = packet->_hint_arrow.type;
    game->hint_arrow_entity_id = packet->_hint_arrow.id;
    game->hint_arrow_tile_x    = packet->_hint_arrow.id;
    game->hint_arrow_tile_z    = packet->_hint_arrow.z;
}

void
gameproto_exec_reset_anims(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    (void)packet;
    if( !game->world )
        return;
    for( int i = 0; i < game->world->active_npc_count; i++ )
    {
        int npc_id = game->world->active_npcs[i];
        if( npc_id < 0 )
            continue;
        struct NPCEntity* npc = world_npc(game->world, npc_id);
        if( npc && npc->alive )
            world_npc_entity_set_animation(game->world, npc_id, -1, ANIMATION_TYPE_PRIMARY);
    }
    world_player_entity_set_animation(
        game->world, ACTIVE_PLAYER_SLOT, -1, ANIMATION_TYPE_PRIMARY);
}

void
gameproto_exec_update_pid(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    (void)game;
    (void)packet;
}

void
gameproto_exec_varp_small(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    varp_varbit_apply_small(
        &game->varp_varbit, packet->_varp_small.variable, packet->_varp_small.value);
}

void
gameproto_exec_varp_large(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    varp_varbit_apply_large(
        &game->varp_varbit, packet->_varp_large.variable, packet->_varp_large.value);
}

void
gameproto_exec_varp_sync(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    (void)packet;
    varp_varbit_apply_sync(&game->varp_varbit);
}

void
gameproto_exec_update_zone_partial_follows(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    game->zone_base_x = packet->_update_zone_partial_follows.base_x * 8;
    game->zone_base_z = packet->_update_zone_partial_follows.base_z * 8;
}

void
gameproto_exec_update_zone_full_follows(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    game->zone_base_x = packet->_update_zone_full_follows.base_x * 8;
    game->zone_base_z = packet->_update_zone_full_follows.base_z * 8;
}

void
gameproto_exec_update_zone_partial_enclosed(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    game->zone_base_x = packet->_update_zone_partial_follows.base_x * 8;
    game->zone_base_z = packet->_update_zone_partial_follows.base_z * 8;
}

void
gameproto_exec_loc_anim(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    int x     = zone_tile_x(game, packet->_loc_anim.pos);
    int z     = zone_tile_z(game, packet->_loc_anim.pos);
    int seq_id = packet->_loc_anim.seq_id;

    if( x < 0 || x >= ZONE_SCENE_SIZE || z < 0 || z >= ZONE_SCENE_SIZE )
        return;

    /* Record as a LocChangeEntry with anim_seq_id for the tick loop to apply. */
    struct LocChangeEntry* entry = (struct LocChangeEntry*)malloc(sizeof(struct LocChangeEntry));
    memset(entry, 0, sizeof(*entry));
    entry->level      = 0;
    entry->x          = x;
    entry->z          = z;
    entry->old_type   = -1;
    entry->new_type   = -1;
    entry->end_time   = -1;
    entry->anim_seq_id = seq_id;
    entry->next        = game->loc_changes_head;
    game->loc_changes_head = entry;
}

void
gameproto_exec_loc_merge(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    /* LOC_MERGE: add/change with associated player entity merging.
     * Treated as LOC_ADD_CHANGE with the given loc_id; the pid reference
     * is stored as start/end time for future entity-attach logic. */
    int x     = zone_tile_x(game, packet->_loc_merge.pos);
    int z     = zone_tile_z(game, packet->_loc_merge.pos);
    int info  = packet->_loc_merge.info;
    int shape = info >> 2;
    int angle = info & 0x3;
    int loc_id = packet->_loc_merge.loc_id;

    if( x < 0 || x >= ZONE_SCENE_SIZE || z < 0 || z >= ZONE_SCENE_SIZE )
        return;

    struct LocChangeEntry* entry = (struct LocChangeEntry*)malloc(sizeof(struct LocChangeEntry));
    memset(entry, 0, sizeof(*entry));
    entry->level      = 0;
    entry->x          = x;
    entry->z          = z;
    entry->old_type   = -1;
    entry->new_type   = loc_id;
    entry->old_shape  = 0;
    entry->new_shape  = shape;
    entry->old_angle  = 0;
    entry->new_angle  = angle;
    entry->start_time = packet->_loc_merge.start;
    entry->end_time   = packet->_loc_merge.end;
    entry->anim_seq_id = -1;
    entry->next        = game->loc_changes_head;
    game->loc_changes_head = entry;
}

void
gameproto_exec_map_anim(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    int x        = zone_tile_x(game, packet->_map_anim.pos);
    int z        = zone_tile_z(game, packet->_map_anim.pos);
    int spotanim = packet->_map_anim.id;
    int height   = packet->_map_anim.height;
    int delay    = packet->_map_anim.delay;

    if( x < 0 || x >= ZONE_SCENE_SIZE || z < 0 || z >= ZONE_SCENE_SIZE )
        return;

    struct MapAnimEntry* entry = (struct MapAnimEntry*)malloc(sizeof(struct MapAnimEntry));
    entry->x          = x;
    entry->z          = z;
    entry->level      = 0;
    entry->spotanim_id = spotanim;
    entry->height     = height;
    entry->delay      = delay;
    entry->next       = game->map_anims_head;
    game->map_anims_head = entry;
}

void
gameproto_exec_map_projanim(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    int src_x = zone_tile_x(game, packet->_map_projanim.pos);
    int src_z = zone_tile_z(game, packet->_map_projanim.pos);
    int dst_x = src_x + packet->_map_projanim.dx_offset;
    int dst_z = src_z + packet->_map_projanim.dz_offset;

    struct MapProjAnimEntry* entry = (struct MapProjAnimEntry*)malloc(sizeof(struct MapProjAnimEntry));
    entry->src_x      = src_x;
    entry->src_z      = src_z;
    entry->dst_x      = dst_x;
    entry->dst_z      = dst_z;
    entry->level      = 0;
    entry->spotanim_id = packet->_map_projanim.spotanim;
    entry->src_height = packet->_map_projanim.src_height;
    entry->dst_height = packet->_map_projanim.dst_height;
    entry->start_delay = packet->_map_projanim.start_delay;
    entry->end_delay   = packet->_map_projanim.end_delay;
    entry->target     = packet->_map_projanim.target;
    entry->peak       = packet->_map_projanim.peak;
    entry->arc        = packet->_map_projanim.arc;
    entry->next       = game->map_projanims_head;
    game->map_projanims_head = entry;
}

void
gameproto_exec_set_multiway(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    game->in_multiway = packet->_set_multiway.multiway;
}
