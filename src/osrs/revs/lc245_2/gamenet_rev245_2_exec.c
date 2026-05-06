#include "osrs/revs/lc245_2/gamenet_rev245_2_exec.h"

#include "graphics/dash.h"
#include "osrs/_light_model_default.u.c"
#include "osrs/buildcachedat.h"
#include "osrs/clientscript_vm.h"
#include "osrs/core/serverprot.h"
#include "osrs/core/serverprot_pkt_npcinfo.h"
#include "osrs/core/serverprot_pkt_playerinfo.h"
#include "osrs/dash_utils.h"
#include "osrs/datatypes/appearances.h"
#include "osrs/datatypes/player_appearance.h"
#include "osrs/entity_scenebuild.h"
#include "osrs/game.h"
#include "osrs/game_entity.h"
#include "osrs/interface.h"
#include "osrs/interface_state.h"
#include "osrs/model_transforms.h"
#include "osrs/obj_icon.h"
#include "osrs/player_stats.h"
#include "osrs/revconfig/uiscene.h"
#include "osrs/revconfig/uitree.h"
#include "osrs/revconfig/uitree_load.h"
#include "osrs/rscache/bitbuffer.h"
#include "osrs/rscache/rsbuf.h"
#include "osrs/rscache/tables/maps.h"
#include "osrs/rscache/tables/model.h"
#include "osrs/rscache/tables/string_utils.h"
#include "osrs/rscache/tables_dat/config_component.h"
#include "osrs/rscache/tables_dat/config_obj.h"
#include "osrs/scene2.h"
#include "osrs/world_scenebuild.h"
#include "osrs/zone_state.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** RS build scene width/height in tiles (rebuild_normal / world). */
#define GAMEPROTO_SCENE_WIDTH_TILES 104
/** Smallest half-width in map squares so (2*padding+1)*8 >= scene_size. */
#define GAMEPROTO_ZONE_PADDING_FOR_SCENE(S) ((S) > 8 ? (((S) - 8) + 15) / 16 : 0)

/** RS protocol uses 65535 (u16 -1) for "no interface" / close overlay. */
static int
if_decode_component_id(int raw)
{
    if( raw == 65535 )
        return -1;
    return raw;
}

void
LibToriRS_WorldMinimapStaticRebuild(struct GGame* game);

static struct PktNpcInfoReaderV1 npc_info_reader = { 0 };

void
gamenet_rev245_2_exec_npc_info_raw_v1(
    struct GGame* game,
    void* data,
    int length)
{
    struct RevServerProtPacket packet = { 0 };
    packet.packet_type = SERVERPROT_NPC_INFO_V1;
    packet.u.npc_info_v1.length = length;
    packet.u.npc_info_v1.data = data;
    gamenet_rev245_2_exec_npc_info_v1(game, &packet);
}

void
gamenet_rev245_2_exec_npc_info_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    npc_info_reader.extended_count = 0;
    npc_info_reader.current_op = 0;
    npc_info_reader.max_ops = 2048;
    struct PktNpcInfoOpV1 ops[2048];
    int count = pkt_npc_info_reader_read(
        &npc_info_reader, (struct PktNpcInfoV1*)&packet->u.npc_info_v1, ops, 2048);

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
        struct PktNpcInfoOpV1* op = &ops[i];

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

static struct PktPlayerInfoReaderV1 player_info_reader = { 0 };

static void
player_info_apply_ops_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    struct BitBuffer buf;
    struct RSBuffer rsbuf;
    rsbuf_init(&rsbuf, packet->u.player_info_v1.data, packet->u.player_info_v1.length);
    bitbuffer_init_from_rsbuf(&buf, &rsbuf);
    bits(&buf);

    struct PktPlayerInfoOpV1 ops[2048];

    struct SceneElement* scene_element = NULL;
    struct PlayerEntity* active_player = world_player(game->world, ACTIVE_PLAYER_SLOT);

    int count = pkt_player_info_reader_read(
        &player_info_reader, (struct PktPlayerInfoV1*)&packet->u.player_info_v1, ops, 2048);
    int player_id = -1;

    struct PlayerEntity* player = NULL;

    game->world->active_player_count = 0;
    for( int i = 0; i < count; i++ )
    {
        struct PktPlayerInfoOpV1* op = &ops[i];

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
gamenet_rev245_2_exec_rebuild_normal_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    int zonex = packet->u.map_rebuild_v1.zonex;
    int zonez = packet->u.map_rebuild_v1.zonez;

    /* Jagfile + cache lifecycle for configs/versionlist/media is owned by Lua pkt_dispatch
     * (clear/reload around rebuild). Do not clear buildcachedat here or subsequent PLAYER_INFO /
     * NPC_INFO in the same script batch lose idk/npc/obj/models/sequences. */

    /* Compute new base tile to shift game-level state (minimap flag). */
    int zone_padding = GAMEPROTO_ZONE_PADDING_FOR_SCENE(GAMEPROTO_SCENE_WIDTH_TILES);
    int new_base_x = (zonex - zone_padding) * 8;
    int new_base_z = (zonez - zone_padding) * 8;
    int dx = 0;
    int dz = 0;

    if( !game->world )
    {
        /* First rebuild: create the world. */
        game->world = world_new(game->gamecache, game->scene2);
    }
    else
    {
        dx = new_base_x - game->world->_base_tile_x;
        dz = new_base_z - game->world->_base_tile_z;
    }

    gamenet_rev245_2_exec_rebuild_normal_world_v1(game->world, packet);

    /* Shift game-level minimap flag (lives on GGame, not World). */
    if( game->minimap_flag_has && (dx || dz) )
    {
        game->minimap_flag_x -= dx;
        game->minimap_flag_z -= dz;
    }

    LibToriRS_WorldMinimapStaticRebuild(game);
}

void
gamenet_rev245_2_exec_rebuild_normal_world_v1(
    struct World* world,
    struct RevServerProtPacket* packet)
{
    int zone_padding = GAMEPROTO_ZONE_PADDING_FOR_SCENE(GAMEPROTO_SCENE_WIDTH_TILES);
    int zone_sw_x = packet->u.map_rebuild_v1.zonex - zone_padding;
    int zone_sw_z = packet->u.map_rebuild_v1.zonez - zone_padding;

    int prev_base_x = world->_base_tile_x;
    int prev_base_z = world->_base_tile_z;
    int new_base_x = zone_sw_x * 8;
    int new_base_z = zone_sw_z * 8;
    int dx = new_base_x - prev_base_x;
    int dz = new_base_z - prev_base_z;

    world_buildcachedat_rebuild_centerzone(
        world, packet->u.map_rebuild_v1.zonex, packet->u.map_rebuild_v1.zonez, GAMEPROTO_SCENE_WIDTH_TILES);

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

    /* Static map loc scene_coord.{sx,sz} are rebuilt by world_buildcachedat_rebuild_centerzone
     * (world_to_scene_* in the new viewport). Do not shift them by (dx,dz): that delta is in
     * world base-tile space and does not match scene-local indices; it destroys or culls locs
     * after every rebuild when the base moves (often misreported as missing edge-chunk locs). */

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
}

void
gamenet_rev245_2_exec_player_info_raw_v1(
    struct GGame* game,
    void* data,
    int length)
{
    struct RevServerProtPacket packet = { 0 };
    packet.packet_type = SERVERPROT_PLAYER_INFO_V1;
    packet.u.player_info_v1.data = data;
    packet.u.player_info_v1.length = length;
    player_info_apply_ops_v1(game, &packet);
}

void
gamenet_rev245_2_exec_player_info_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    player_info_apply_ops_v1(game, packet);
}

/**
 * Update a single UIInventoryItem slot: releases any prior UIScene sprite element,
 * loads a fresh icon via obj_icon_get, and registers it in the UIScene.
 *
 * The protocol never streams inventory models; only obj ids (via UPDATE_INV_* after the
 * server transmits a component, e.g. script inv_transmit). Icons are built locally here
 * (magic spellbook rune stones included).
 *
 * obj_id_0based: 0-based item id for obj_icon_get; pass 0 to clear the slot.
 * count: stack count used for multi-stack icon variants (e.g. coins).
 */
static void
inv_sync_load_item_sprite(
    struct GGame* game,
    struct UIInventoryItem* item,
    int obj_id_0based,
    int count)
{
    if( item->scene_id >= 0 && game->ui_scene )
        uiscene_element_release(game->ui_scene, item->scene_id);
    item->scene_id = -1;
    item->atlas_index = 0;

    if( obj_id_0based <= 0 || !game->ui_scene )
    {
        item->obj_id = 0;
        item->obj_count = 0;
        return;
    }

    /* Store 1-based so rs_gfx_inv_step's (obj_id > 0) check treats 0 as empty. */
    item->obj_id = obj_id_0based + 1;
    item->obj_count = count > 0 ? count : 1;

    struct DashSprite* cached = obj_icon_get(game, obj_id_0based, count > 0 ? count : 1);
    if( !cached )
        return;

    /* UIScene owns and frees sprites on release; obj_icon_get may return a cache pointer that LRU
     * eviction frees while slots still reference it — clone for exclusive ownership. */
    struct DashSprite* sp = dashsprite_clone(cached);
    if( !sp )
        return;

    struct DashSprite** arr = malloc(sizeof(struct DashSprite*));
    if( !arr )
    {
        dashsprite_free(sp);
        return;
    }
    arr[0] = sp;

    int eid = uiscene_element_acquire(game->ui_scene, -1);
    if( eid < 0 )
    {
        free(arr);
        dashsprite_free(sp);
        return;
    }

    struct UISceneElement* el = uiscene_element_at(game->ui_scene, eid);
    if( !el )
    {
        free(arr);
        dashsprite_free(sp);
        uiscene_element_release(game->ui_scene, eid);
        return;
    }

    el->dash_sprites = arr;
    el->dash_sprites_count = 1;
    el->dash_sprites_borrowed = false;
    item->scene_id = eid;
    item->atlas_index = 0;
}

/**
 * Apply PKTIN_LC245_2_UPDATE_INV_FULL: wire carries 1-based obj ids per slot only (no models).
 * Magic tab spell rows behave like any other TYPE_INV — slots stay empty until the server
 * transmits those components (then UPDATE_INV_*); we sync buildcachedat and attach local
 * sprites via inv_sync_load_item_sprite / obj_icon_get.
 */
void
gamenet_rev245_2_exec_update_inv_full_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    int component_id = packet->u.update_inv_full_v1.component_id;
    int size = packet->u.update_inv_full_v1.size;

    struct GameCacheComponent* component = gamecache_get_component(game->gamecache, component_id);

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

    /* Update buildcachedat slots (software-raster / interface.c path). */
    for( int i = 0; i < size && i < max_slots; i++ )
    {
        component->invSlotObjId[i] = packet->u.update_inv_full_v1.obj_ids[i];
        component->invSlotObjCount[i] = packet->u.update_inv_full_v1.obj_counts[i];
    }
    for( int i = size; i < max_slots; i++ )
    {
        component->invSlotObjId[i] = 0;
        component->invSlotObjCount[i] = 0;
    }

    /* Sync UIInventoryPool for the ToriRS / rs_gfx_inv_step + uitree_fill_inv_slot_options.
     * Use UITree RS_INV inv_index when loaded; else same pool slot as uitree_load
     * (uitree_inv_pool_find_or_append_by_component_id) so data exists before IF_OPEN. */
    if( game->inv_pool )
    {
        int32_t node_idx = -1;
        int inv_index = -1;
        int from_uitree = 0;
        if( game->ui_root_buffer )
        {
            inv_index = uitree_find_inv_index_by_component_id(
                game->ui_root_buffer, component_id, &node_idx);
            if( inv_index >= 0 )
                from_uitree = 1;
        }
        if( inv_index < 0 )
            inv_index =
                uitree_inv_pool_find_or_append_by_component_id(game->inv_pool, component_id);

        if( inv_index < 0 || inv_index >= game->inv_pool->count )
        {
            printf(
                "UPDATE_INV_FULL exec: component_id=%d FAILED to resolve inv pool (inv_index=%d "
                "pool_count=%d)\n",
                component_id,
                inv_index,
                game->inv_pool->count);
        }
        else
        {
            struct UIInventory* inv = &game->inv_pool->inventories[inv_index];
            int item_limit = size < UI_INVENTORY_MAX_ITEMS ? size : UI_INVENTORY_MAX_ITEMS;

            for( int i = 0; i < item_limit; i++ )
            {
                /* obj_ids[i] is 1-based wire value; obj_icon_get expects 0-based. */
                int obj_id_wire = packet->u.update_inv_full_v1.obj_ids[i];
                int obj_id_0base = obj_id_wire > 0 ? obj_id_wire - 1 : 0;
                int count = packet->u.update_inv_full_v1.obj_counts[i];
                inv_sync_load_item_sprite(game, &inv->items[i], obj_id_0base, count);
            }
            for( int i = item_limit; i < UI_INVENTORY_MAX_ITEMS; i++ )
                inv_sync_load_item_sprite(game, &inv->items[i], 0, 0);

            inv->item_count = item_limit;

            int32_t dirty_idx = -1;
            if( node_idx >= 0 )
            {
                dirty_idx = node_idx;
                game->ui_root_buffer->components[node_idx].is_dirty = 1;
            }
            else if( game->ui_root_buffer )
            {
                int32_t idx = uitree_find_by_component_id(game->ui_root_buffer, component_id);
                if( idx >= 0 )
                {
                    dirty_idx = idx;
                    game->ui_root_buffer->components[idx].is_dirty = 1;
                }
            }

            printf(
                "UPDATE_INV_FULL exec: component_id=%d type=%d grid=%dx%d pkt_size=%d "
                "inv_index=%d pool=\"%s\" sync_src=%s uitree_node_idx=%d dirty_idx=%d\n",
                component_id,
                component->type,
                component->width,
                component->height,
                size,
                inv_index,
                inv->name[0] ? inv->name : "(unnamed)",
                from_uitree ? "RS_INV_node" : "if_component_pool",
                (int)node_idx,
                (int)dirty_idx);
            if( !from_uitree && dirty_idx < 0 && game->ui_root_buffer )
                printf(
                    "  hint: inventory data is in pool \"%s\" but no UITree node matched "
                    "component_id yet; open magic tab (IF_SETTAB) so RS_INV binds this pool for "
                    "drawing.\n",
                    inv->name[0] ? inv->name : "(unnamed)");

            {
                int const show = item_limit < 24 ? item_limit : 24;
                for( int i = 0; i < show; i++ )
                {
                    int w = packet->u.update_inv_full_v1.obj_ids[i];
                    int z = w > 0 ? w - 1 : 0;
                    printf(
                        "  slot %d: wire=%d 0base=%d cnt=%d -> item.obj_id=%d scene_id=%d\n",
                        i,
                        w,
                        z,
                        packet->u.update_inv_full_v1.obj_counts[i],
                        inv->items[i].obj_id,
                        inv->items[i].scene_id);
                }
            }
        }
    }
}

void
gamenet_rev245_2_exec_if_settab_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    int component_id = if_decode_component_id(packet->u.if_settab_v1.component_id);
    int tab_id = packet->u.if_settab_v1.tab_id;

    if( tab_id >= 0 && tab_id < 14 && game->iface )
    {
        game->iface->tab_interface_id[tab_id] = component_id;
        /* Always expand: duplicate IF_SETTAB is common; skipping when old==id left an empty
         * subtree if the first expand ran before interfaces were in buildcachedat. */
        uitree_expand_sidebar_for_tab(game, tab_id, component_id);
        uitree_debug_log_sidebar_state(game, "IF_SETTAB", tab_id, component_id);
    }
}

void
gamenet_rev245_2_exec_if_settab_active_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    int tab_id = packet->u.if_settab_active_v1.tab_id;

    if( tab_id >= 0 && tab_id < 14 && game->iface )
    {
        game->iface->selected_tab = tab_id;
        uitree_debug_log_sidebar_state(game, "IF_SETTAB_ACTIVE", tab_id, -1);
        printf("IF_SETTAB_ACTIVE: Set active tab to %d\n", tab_id);
        if( tab_id == 6 )
        {
            interface_magic_tab_request_inv_transmit_if_configured(game);
        }
    }
    else
    {
        printf("IF_SETTAB_ACTIVE: Invalid tab_id %d (must be 0-13)\n", tab_id);
    }
}

void
gamenet_rev245_2_exec_if_setcolour_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    int component_id = packet->u.if_setcolour_v1.component_id;
    int colour15 = packet->u.if_setcolour_v1.colour;

    if( !game->ui_root_buffer )
        return;
    int32_t idx = uitree_find_by_component_id(game->ui_root_buffer, component_id);
    if( idx < 0 )
        return;
    struct StaticUIComponent* c = &game->ui_root_buffer->components[idx];

    int r = (colour15 >> 10) & 0x1f;
    int g = (colour15 >> 5) & 0x1f;
    int b = colour15 & 0x1f;
    int rgb = (r << 19) | (g << 11) | (b << 3);

    switch( c->type )
    {
    case UIELEM_RS_TEXT:
        c->u.rs_text.color = rgb;
        break;
    case UIELEM_RS_RECT:
        c->u.rs_rect.color = rgb;
        break;
    default:
        break;
    }
}

void
gamenet_rev245_2_exec_if_sethide_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    int component_id = packet->u.if_sethide_v1.component_id;
    int hide_val = packet->u.if_sethide_v1.hide;

    if( game->ui_root_buffer )
        uitree_set_component_hidden(game->ui_root_buffer, component_id, hide_val);
}

void
gamenet_rev245_2_exec_if_setobject_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    int component_id = packet->u.if_setobject_v1.component_id;
    int obj_id = packet->u.if_setobject_v1.obj_id;
    int zoom = packet->u.if_setobject_v1.zoom;

    struct GameCacheComponent* component = gamecache_get_component(game->gamecache, component_id);
    if( !component )
        return;

    struct GameCacheObj* obj = gamecache_get_obj(game->gamecache, obj_id);
    if( !obj )
        return;

    component->modelType = 4;
    component->model = obj_id;
    component->xan = obj->xan2d;
    component->yan = obj->yan2d;
    component->zoom = (obj->zoom2d * 100) / zoom;
}

void
gamenet_rev245_2_exec_if_setmodel_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    int component_id = packet->u.if_setmodel_v1.component_id;
    int model_id = packet->u.if_setmodel_v1.model_id;

    struct GameCacheComponent* component = gamecache_get_component(game->gamecache, component_id);
    if( !component )
        return;

    component->modelType = 1;
    component->model = model_id;
}

void
gamenet_rev245_2_exec_if_setanim_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    int component_id = packet->u.if_setanim_v1.component_id;
    int anim_id = packet->u.if_setanim_v1.anim_id;

    if( !game->ui_root_buffer )
        return;
    int32_t idx = uitree_find_by_component_id(game->ui_root_buffer, component_id);
    if( idx >= 0 )
        game->ui_root_buffer->components[idx].anim_id = anim_id;
}

void
gamenet_rev245_2_exec_if_setplayerhead_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    int component_id = packet->u.if_setplayerhead_v1.component_id;

    struct GameCacheComponent* component = gamecache_get_component(game->gamecache, component_id);
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
gamenet_rev245_2_exec_if_settext_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    int component_id = packet->u.if_settext_v1.component_id;
    char* new_text = packet->u.if_settext_v1.text;

    if( !game->ui_root_buffer )
    {
        free(new_text);
        packet->u.if_settext_v1.text = NULL;
        return;
    }
    int32_t idx = uitree_find_by_component_id(game->ui_root_buffer, component_id);
    if( idx < 0 )
    {
        free(new_text);
        packet->u.if_settext_v1.text = NULL;
        return;
    }
    struct StaticUIComponent* c = &game->ui_root_buffer->components[idx];
    if( c->type == UIELEM_RS_TEXT )
    {
        free((void*)c->u.rs_text.text);
        c->u.rs_text.text = new_text;
        packet->u.if_settext_v1.text = NULL;
    }
    else
    {
        free(new_text);
        packet->u.if_settext_v1.text = NULL;
    }
}

void
gamenet_rev245_2_exec_if_setnpchead_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    int component_id = packet->u.if_setnpchead_v1.component_id;
    int npc_id = packet->u.if_setnpchead_v1.npc_id;

    struct GameCacheComponent* component = gamecache_get_component(game->gamecache, component_id);
    if( !component )
        return;

    component->modelType = 2;
    component->model = npc_id;
}

void
gamenet_rev245_2_exec_if_setposition_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    int component_id = packet->u.if_setposition_v1.component_id;
    int x = packet->u.if_setposition_v1.x;
    int z = packet->u.if_setposition_v1.z;

    if( !game->ui_root_buffer )
        return;
    int32_t idx = uitree_find_by_component_id(game->ui_root_buffer, component_id);
    if( idx < 0 )
        return;
    struct StaticUIComponent* c = &game->ui_root_buffer->components[idx];
    c->position.x = x;
    c->position.y = z;
}

void
gamenet_rev245_2_exec_if_setscrollpos_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    int component_id = packet->u.if_setscrollpos_v1.component_id;
    int pos = packet->u.if_setscrollpos_v1.pos;

    if( !game->iface || component_id < 0 || component_id >= MAX_IFACE_SCROLL_IDS )
        return;

    /* Clamp using StaticUIComponent scroll data if available. */
    if( game->ui_root_buffer )
    {
        int32_t idx = uitree_find_by_component_id(game->ui_root_buffer, component_id);
        if( idx >= 0 )
        {
            struct StaticUIComponent* c = &game->ui_root_buffer->components[idx];
            if( c->type == UIELEM_RS_LAYER )
            {
                if( pos < 0 )
                    pos = 0;
                int max_scroll = c->u.rs_layer.scroll_height - c->position.height;
                if( max_scroll > 0 && pos > max_scroll )
                    pos = max_scroll;
            }
        }
    }

    game->iface->component_scroll_position[component_id] = pos;
}

/* Forward declarations for handlers defined after this function. */
static void
gamenet_rev245_2_exec_message_game_v1(
    struct GGame*,
    struct RevServerProtPacket*);
static void
gamenet_rev245_2_exec_message_private_v1(
    struct GGame*,
    struct RevServerProtPacket*);
static void
gamenet_rev245_2_exec_chat_filter_settings_v1(
    struct GGame*,
    struct RevServerProtPacket*);
static void
gamenet_rev245_2_exec_tut_flash_v1(
    struct GGame*,
    struct RevServerProtPacket*);
static void
gamenet_rev245_2_exec_tut_open_v1(
    struct GGame*,
    struct RevServerProtPacket*);
static void
gamenet_rev245_2_exec_logout_v1(
    struct GGame*,
    struct RevServerProtPacket*);
static void
gamenet_rev245_2_exec_p_countdialog_v1(
    struct GGame*,
    struct RevServerProtPacket*);
static void
gamenet_rev245_2_exec_reboot_timer_v1(
    struct GGame*,
    struct RevServerProtPacket*);
static void
gamenet_rev245_2_exec_update_friendlist_v1(
    struct GGame*,
    struct RevServerProtPacket*);
static void
gamenet_rev245_2_exec_update_ignorelist_v1(
    struct GGame*,
    struct RevServerProtPacket*);
static void
gamenet_rev245_2_exec_if_openoverlay_v1(
    struct GGame*,
    struct RevServerProtPacket*);
static void
gamenet_rev245_2_exec_synth_sound_v1(
    struct GGame*,
    struct RevServerProtPacket*);
static void
gamenet_rev245_2_exec_midi_song_v1(
    struct GGame*,
    struct RevServerProtPacket*);
static void
gamenet_rev245_2_exec_midi_jingle_v1(
    struct GGame*,
    struct RevServerProtPacket*);

void
gamenet_rev245_2_exec_dispatch_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    switch( packet->packet_type )
    {
    case SERVERPROT_REBUILD_NORMAL_V1:
        gamenet_rev245_2_exec_rebuild_normal_v1(game, packet);
        break;
    case SERVERPROT_NPC_INFO_V1:
        gamenet_rev245_2_exec_npc_info_v1(game, packet);
        break;
    case SERVERPROT_PLAYER_INFO_V1:
        gamenet_rev245_2_exec_player_info_v1(game, packet);
        break;
    case SERVERPROT_UPDATE_INV_FULL_V1:
        gamenet_rev245_2_exec_update_inv_full_v1(game, packet);
        break;
    case SERVERPROT_IF_SETTAB_V1:
        gamenet_rev245_2_exec_if_settab_v1(game, packet);
        break;
    case SERVERPROT_IF_SETTAB_ACTIVE_V1:
        gamenet_rev245_2_exec_if_settab_active_v1(game, packet);
        break;
    case SERVERPROT_IF_SETCOLOUR_V1:
        gamenet_rev245_2_exec_if_setcolour_v1(game, packet);
        break;
    case SERVERPROT_IF_SETHIDE_V1:
        gamenet_rev245_2_exec_if_sethide_v1(game, packet);
        break;
    case SERVERPROT_IF_SETOBJECT_V1:
        gamenet_rev245_2_exec_if_setobject_v1(game, packet);
        break;
    case SERVERPROT_IF_SETMODEL_V1:
        gamenet_rev245_2_exec_if_setmodel_v1(game, packet);
        break;
    case SERVERPROT_IF_SETANIM_V1:
        gamenet_rev245_2_exec_if_setanim_v1(game, packet);
        break;
    case SERVERPROT_IF_SETPLAYERHEAD_V1:
        gamenet_rev245_2_exec_if_setplayerhead_v1(game, packet);
        break;
    case SERVERPROT_IF_SETTEXT_V1:
        gamenet_rev245_2_exec_if_settext_v1(game, packet);
        break;
    case SERVERPROT_IF_SETNPCHEAD_V1:
        gamenet_rev245_2_exec_if_setnpchead_v1(game, packet);
        break;
    case SERVERPROT_IF_SETPOSITION_V1:
        gamenet_rev245_2_exec_if_setposition_v1(game, packet);
        break;
    case SERVERPROT_IF_SETSCROLLPOS_V1:
        gamenet_rev245_2_exec_if_setscrollpos_v1(game, packet);
        break;
    case SERVERPROT_IF_OPENCHAT_V1:
        gamenet_rev245_2_exec_if_openchat_v1(game, packet);
        break;
    case SERVERPROT_IF_OPENMAIN_V1:
        gamenet_rev245_2_exec_if_openmain_v1(game, packet);
        break;
    case SERVERPROT_IF_OPENSIDE_V1:
        gamenet_rev245_2_exec_if_openside_v1(game, packet);
        break;
    case SERVERPROT_IF_OPENMAIN_SIDE_V1:
        gamenet_rev245_2_exec_if_openmain_side_v1(game, packet);
        break;
    case SERVERPROT_IF_CLOSE_V1:
        gamenet_rev245_2_exec_if_close_v1(game, packet);
        break;
    case SERVERPROT_UPDATE_INV_STOP_TRANSMIT_V1:
        gamenet_rev245_2_exec_update_inv_stop_transmit_v1(game, packet);
        break;
    case SERVERPROT_UPDATE_INV_PARTIAL_V1:
        gamenet_rev245_2_exec_update_inv_partial_v1(game, packet);
        break;
    case SERVERPROT_CAM_LOOKAT_V1:
        gamenet_rev245_2_exec_cam_lookat_v1(game, packet);
        break;
    case SERVERPROT_CAM_MOVETO_V1:
        gamenet_rev245_2_exec_cam_moveto_v1(game, packet);
        break;
    case SERVERPROT_CAM_SHAKE_V1:
        gamenet_rev245_2_exec_cam_shake_v1(game, packet);
        break;
    case SERVERPROT_CAM_RESET_V1:
        gamenet_rev245_2_exec_cam_reset_v1(game, packet);
        break;
    case SERVERPROT_UNSET_MAP_FLAG_V1:
        gamenet_rev245_2_exec_unset_map_flag_v1(game, packet);
        break;
    case SERVERPROT_UPDATE_RUNWEIGHT_V1:
        gamenet_rev245_2_exec_update_runweight_v1(game, packet);
        break;
    case SERVERPROT_UPDATE_RUNENERGY_V1:
        gamenet_rev245_2_exec_update_runenergy_v1(game, packet);
        break;
    case SERVERPROT_UPDATE_STAT_V1:
        gamenet_rev245_2_exec_update_stat_v1(game, packet);
        break;
    case SERVERPROT_HINT_ARROW_V1:
        gamenet_rev245_2_exec_hint_arrow_v1(game, packet);
        break;
    case SERVERPROT_RESET_ANIMS_V1:
        gamenet_rev245_2_exec_reset_anims_v1(game, packet);
        break;
    case SERVERPROT_UPDATE_PID_V1:
        gamenet_rev245_2_exec_update_pid_v1(game, packet);
        break;
    case SERVERPROT_VARP_SMALL_V1:
        gamenet_rev245_2_exec_varp_small_v1(game, packet);
        break;
    case SERVERPROT_VARP_LARGE_V1:
        gamenet_rev245_2_exec_varp_large_v1(game, packet);
        break;
    case SERVERPROT_VARP_SYNC_V1:
        gamenet_rev245_2_exec_varp_sync_v1(game, packet);
        break;
    case SERVERPROT_UPDATE_ZONE_PARTIAL_FOLLOWS_V1:
        gamenet_rev245_2_exec_update_zone_partial_follows_v1(game, packet);
        break;
    case SERVERPROT_UPDATE_ZONE_FULL_FOLLOWS_V1:
        gamenet_rev245_2_exec_update_zone_full_follows_v1(game, packet);
        break;
    case SERVERPROT_UPDATE_ZONE_PARTIAL_ENCLOSED_V1:
        gamenet_rev245_2_exec_update_zone_partial_enclosed_v1(game, packet);
        break;
    case SERVERPROT_LOC_ANIM_V1:
        gamenet_rev245_2_exec_loc_anim_v1(game, packet);
        break;
    case SERVERPROT_LOC_MERGE_V1:
        gamenet_rev245_2_exec_loc_merge_v1(game, packet);
        break;
    case SERVERPROT_MAP_ANIM_V1:
        gamenet_rev245_2_exec_map_anim_v1(game, packet);
        break;
    case SERVERPROT_MAP_PROJANIM_V1:
        gamenet_rev245_2_exec_map_projanim_v1(game, packet);
        break;
    case SERVERPROT_SET_MULTIWAY_V1:
        gamenet_rev245_2_exec_set_multiway_v1(game, packet);
        break;
    case SERVERPROT_SET_PLAYER_OP_V1:
        gamenet_rev245_2_exec_set_player_op_v1(game, packet);
        break;
    case SERVERPROT_MESSAGE_GAME_V1:
        gamenet_rev245_2_exec_message_game_v1(game, packet);
        break;
    case SERVERPROT_MESSAGE_PRIVATE_V1:
        gamenet_rev245_2_exec_message_private_v1(game, packet);
        break;
    case SERVERPROT_CHAT_FILTER_SETTINGS_V1:
        gamenet_rev245_2_exec_chat_filter_settings_v1(game, packet);
        break;
    case SERVERPROT_TUT_FLASH_V1:
        gamenet_rev245_2_exec_tut_flash_v1(game, packet);
        break;
    case SERVERPROT_TUT_OPEN_V1:
        gamenet_rev245_2_exec_tut_open_v1(game, packet);
        break;
    case SERVERPROT_LOGOUT_V1:
        gamenet_rev245_2_exec_logout_v1(game, packet);
        break;
    case SERVERPROT_P_COUNTDIALOG_V1:
        gamenet_rev245_2_exec_p_countdialog_v1(game, packet);
        break;
    case SERVERPROT_UPDATE_REBOOT_TIMER_V1:
        gamenet_rev245_2_exec_reboot_timer_v1(game, packet);
        break;
    case SERVERPROT_UPDATE_FRIENDLIST_V1:
        gamenet_rev245_2_exec_update_friendlist_v1(game, packet);
        break;
    case SERVERPROT_UPDATE_IGNORELIST_V1:
        gamenet_rev245_2_exec_update_ignorelist_v1(game, packet);
        break;
    case SERVERPROT_SYNTH_SOUND_V1:
        gamenet_rev245_2_exec_synth_sound_v1(game, packet);
        break;
    case SERVERPROT_MIDI_SONG_V1:
        gamenet_rev245_2_exec_midi_song_v1(game, packet);
        break;
    case SERVERPROT_MIDI_JINGLE_V1:
        gamenet_rev245_2_exec_midi_jingle_v1(game, packet);
        break;
    case SERVERPROT_FINISH_TRACKING_V1:
    case SERVERPROT_ENABLE_TRACKING_V1:
    case SERVERPROT_LAST_LOGIN_INFO_V1:
        /* No client state change needed. */
        break;
    case SERVERPROT_OBJ_ADD_V1:
        gamenet_rev245_2_exec_obj_add_v1(game, packet, game->zone_base_x, game->zone_base_z);
        break;
    case SERVERPROT_OBJ_DEL_V1:
        gamenet_rev245_2_exec_obj_del_v1(game, packet);
        break;
    case SERVERPROT_OBJ_REVEAL_V1:
        gamenet_rev245_2_exec_obj_reveal_v1(game, packet);
        break;
    case SERVERPROT_OBJ_COUNT_V1:
        gamenet_rev245_2_exec_obj_count_v1(game, packet);
        break;
    case SERVERPROT_LOC_ADD_CHANGE_V1:
        gamenet_rev245_2_exec_loc_add_change_v1(game, packet);
        break;
    case SERVERPROT_LOC_DEL_V1:
        gamenet_rev245_2_exec_loc_del_v1(game, packet);
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

void
gamenet_rev245_2_exec_obj_add_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet,
    int zone_base_x,
    int zone_base_z)
{
    int sx = zone_base_x + ((packet->u.obj_add_v1.pos >> 4) & 0x7);
    int sz = zone_base_z + (packet->u.obj_add_v1.pos & 0x7);
    int obj_id = packet->u.obj_add_v1.obj_id & 0x7fff;
    int count = packet->u.obj_add_v1.count;
    int level = 0;

    if( sx < 0 || sx >= ZONE_SCENE_SIZE || sz < 0 || sz >= ZONE_SCENE_SIZE )
        return;
    if( !game->world )
        return;

    world_obj_stacks_ensure(game->world);
    struct ObjStackEntry* entry = (struct ObjStackEntry*)malloc(sizeof(struct ObjStackEntry));
    entry->obj_id = obj_id;
    entry->count = count;
    entry->next = game->world->obj_stacks[level][sx * ZONE_SCENE_SIZE + sz];
    game->world->obj_stacks[level][sx * ZONE_SCENE_SIZE + sz] = entry;

    entity_scenebuild_obj_stack_update_tile(game, level, sx, sz);
}

void
gamenet_rev245_2_exec_obj_del_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    int sx = zone_tile_x(game, packet->u.obj_del_v1.pos);
    int sz = zone_tile_z(game, packet->u.obj_del_v1.pos);
    int obj_id = packet->u.obj_del_v1.obj_id & 0x7fff;
    int level = 0;

    if( !game->world || !game->world->obj_stacks )
        return;
    if( sx < 0 || sx >= ZONE_SCENE_SIZE || sz < 0 || sz >= ZONE_SCENE_SIZE )
        return;

    struct ObjStackEntry** head = &game->world->obj_stacks[level][sx * ZONE_SCENE_SIZE + sz];
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

    entity_scenebuild_obj_stack_update_tile(game, level, sx, sz);
}

void
gamenet_rev245_2_exec_obj_reveal_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    if( packet->u.obj_reveal_v1.receiver == ACTIVE_PLAYER_SLOT )
        return;
    struct RevServerProtPacket add_pkt = { 0 };
    add_pkt.u.obj_add_v1.pos = packet->u.obj_reveal_v1.pos;
    add_pkt.u.obj_add_v1.obj_id = packet->u.obj_reveal_v1.obj_id;
    add_pkt.u.obj_add_v1.count = packet->u.obj_reveal_v1.count;
    gamenet_rev245_2_exec_obj_add_v1(game, &add_pkt, game->zone_base_x, game->zone_base_z);
}

void
gamenet_rev245_2_exec_obj_count_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    int sx = zone_tile_x(game, packet->u.obj_count_v1.pos);
    int sz = zone_tile_z(game, packet->u.obj_count_v1.pos);
    int obj_id = packet->u.obj_count_v1.obj_id & 0x7fff;
    int old_count = packet->u.obj_count_v1.old_count;
    int new_count = packet->u.obj_count_v1.new_count;
    int level = 0;

    if( !game->world || !game->world->obj_stacks )
        return;
    if( sx < 0 || sx >= ZONE_SCENE_SIZE || sz < 0 || sz >= ZONE_SCENE_SIZE )
        return;

    for( struct ObjStackEntry* e = game->world->obj_stacks[level][sx * ZONE_SCENE_SIZE + sz]; e;
         e = e->next )
    {
        if( e->obj_id == obj_id && e->count == old_count )
        {
            e->count = new_count;
            break;
        }
    }

    entity_scenebuild_obj_stack_update_tile(game, level, sx, sz);
}

void
gamenet_rev245_2_exec_loc_add_change_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    int x = zone_tile_x(game, packet->u.loc_add_change_v1.pos);
    int z = zone_tile_z(game, packet->u.loc_add_change_v1.pos);
    int info = packet->u.loc_add_change_v1.info;
    int shape = info >> 2;
    int angle = info & 0x3;
    int loc_id = packet->u.loc_add_change_v1.loc_id;

    if( x < 0 || x >= ZONE_SCENE_SIZE || z < 0 || z >= ZONE_SCENE_SIZE )
        return;

    struct LocChangeEntry* entry = (struct LocChangeEntry*)malloc(sizeof(struct LocChangeEntry));
    entry->level = 0;
    entry->x = x;
    entry->z = z;
    entry->layer = 0; /* Layer from LocShape would require config lookup */
    entry->old_type = -1;
    entry->new_type = loc_id;
    entry->old_shape = 0;
    entry->new_shape = shape;
    entry->old_angle = 0;
    entry->new_angle = angle;
    entry->start_time = 0;
    entry->end_time = -1;
    entry->anim_seq_id = -1;
    entry->next = game->loc_changes_head;
    game->loc_changes_head = entry;
}

void
gamenet_rev245_2_exec_loc_del_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    int x = zone_tile_x(game, packet->u.loc_del_v1.pos);
    int z = zone_tile_z(game, packet->u.loc_del_v1.pos);
    int info = packet->u.loc_del_v1.info;
    int shape = info >> 2;
    int angle = info & 0x3;

    if( x < 0 || x >= ZONE_SCENE_SIZE || z < 0 || z >= ZONE_SCENE_SIZE )
        return;

    struct LocChangeEntry* entry = (struct LocChangeEntry*)malloc(sizeof(struct LocChangeEntry));
    entry->level = 0;
    entry->x = x;
    entry->z = z;
    entry->layer = 0;
    entry->old_type = 0;
    entry->new_type = -1;
    entry->old_shape = shape;
    entry->new_shape = 0;
    entry->old_angle = angle;
    entry->new_angle = 0;
    entry->start_time = 0;
    entry->end_time = -1;
    entry->anim_seq_id = -1;
    entry->next = game->loc_changes_head;
    game->loc_changes_head = entry;
}

void
gamenet_rev245_2_exec_if_openchat_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    if( game->iface )
        game->iface->chat_interface_id = if_decode_component_id(packet->u.if_openchat_v1.component_id);
}

void
gamenet_rev245_2_exec_if_openmain_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    if( game->iface )
        game->iface->viewport_interface_id =
            if_decode_component_id(packet->u.if_openmain_v1.component_id);
}

void
gamenet_rev245_2_exec_if_openside_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    if( game->iface )
        game->iface->sidebar_interface_id =
            if_decode_component_id(packet->u.if_openside_v1.component_id);
}

void
gamenet_rev245_2_exec_if_openmain_side_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    if( game->iface )
    {
        game->iface->viewport_interface_id =
            if_decode_component_id(packet->u.if_openmain_side_v1.main_component_id);
        game->iface->sidebar_interface_id =
            if_decode_component_id(packet->u.if_openmain_side_v1.side_component_id);
    }
}

void
gamenet_rev245_2_exec_if_close_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    (void)packet;
    if( game->iface )
    {
        game->iface->viewport_interface_id = -1;
        game->iface->sidebar_interface_id = -1;
        game->iface->chat_interface_id = -1;

        /* Clear modal selection state (mirrors Client.ts CLOSE_MODAL clearing). */
        game->iface->selected_area = 0;
        game->iface->selected_item = 0;
        game->iface->selected_interface = -1;
        game->iface->selected_cycle = 0;
        game->iface->inv_use_mode = 0;
        game->iface->inv_target_mode = 0;
        game->iface->inv_sel_obj_id = 0;
        game->iface->inv_sel_slot = 0;
        game->iface->inv_sel_comp_id = -1;
        game->iface->inv_sel_obj_name[0] = '\0';
        game->iface->inv_target_src_comp_id = -1;
        game->iface->inv_target_mask = 0;
        game->iface->inv_target_op[0] = '\0';
    }
    /* Release scrollbar drag if any. */
    if( game->ui_root_buffer )
        game->ui_root_buffer->ui_scrollbar_drag_component_id = -1;
}

void
gamenet_rev245_2_exec_update_inv_stop_transmit_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    int component_id = packet->u.update_inv_stop_transmit_v1.component_id;
    struct GameCacheComponent* component = gamecache_get_component(game->gamecache, component_id);
    if( !component )
        return;

    /* Clear buildcachedat slots (software-raster / interface.c path). */
    int max_slots = component->width * component->height;
    for( int i = 0; i < max_slots; i++ )
    {
        if( component->invSlotObjId )
            component->invSlotObjId[i] = 0;
        if( component->invSlotObjCount )
            component->invSlotObjCount[i] = 0;
    }

    /* Clear UIInventoryPool for the ToriRS / rs_gfx_inv_step render path. */
    if( game->inv_pool )
    {
        int32_t node_idx = -1;
        int inv_index = -1;
        if( game->ui_root_buffer )
            inv_index = uitree_find_inv_index_by_component_id(
                game->ui_root_buffer, component_id, &node_idx);
        if( inv_index < 0 )
            inv_index =
                uitree_inv_pool_find_or_append_by_component_id(game->inv_pool, component_id);

        if( inv_index >= 0 && inv_index < game->inv_pool->count )
        {
            struct UIInventory* inv = &game->inv_pool->inventories[inv_index];
            for( int i = 0; i < UI_INVENTORY_MAX_ITEMS; i++ )
                inv_sync_load_item_sprite(game, &inv->items[i], 0, 0);
            inv->item_count = 0;

            if( node_idx >= 0 )
                game->ui_root_buffer->components[node_idx].is_dirty = 1;
            else if( game->ui_root_buffer )
            {
                int32_t idx = uitree_find_by_component_id(game->ui_root_buffer, component_id);
                if( idx >= 0 )
                    game->ui_root_buffer->components[idx].is_dirty = 1;
            }
        }
    }
}

void
gamenet_rev245_2_exec_update_inv_partial_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    int component_id = packet->u.update_inv_partial_v1.component_id;
    struct GameCacheComponent* component = gamecache_get_component(game->gamecache, component_id);
    if( !component || !component->invSlotObjId || !component->invSlotObjCount )
        return;
    int max_slots = component->width * component->height;

    /* Update buildcachedat slots (software-raster / interface.c path).
     * entries[].obj_id is 0-based (parse subtracts 1 from wire g2); cache stores 1-based wire
     * like UPDATE_INV_FULL and Client.ts linkObjType — 0 means empty. */
    for( int i = 0; i < packet->u.update_inv_partial_v1.count; i++ )
    {
        int slot = packet->u.update_inv_partial_v1.entries[i].slot;
        int obj_id_0base = packet->u.update_inv_partial_v1.entries[i].obj_id;
        int count = packet->u.update_inv_partial_v1.entries[i].count;
        if( slot >= 0 && slot < max_slots )
        {
            component->invSlotObjId[slot] = obj_id_0base < 0 ? 0 : (obj_id_0base + 1);
            component->invSlotObjCount[slot] = count;
        }
    }

    /* Sync UIInventoryPool for the ToriRS / rs_gfx_inv_step render path. */
    if( game->inv_pool )
    {
        int32_t node_idx = -1;
        int inv_index = -1;
        if( game->ui_root_buffer )
            inv_index = uitree_find_inv_index_by_component_id(
                game->ui_root_buffer, component_id, &node_idx);
        if( inv_index < 0 )
            inv_index =
                uitree_inv_pool_find_or_append_by_component_id(game->inv_pool, component_id);

        if( inv_index >= 0 && inv_index < game->inv_pool->count )
        {
            struct UIInventory* inv = &game->inv_pool->inventories[inv_index];

            for( int i = 0; i < packet->u.update_inv_partial_v1.count; i++ )
            {
                int slot = packet->u.update_inv_partial_v1.entries[i].slot;
                int count = packet->u.update_inv_partial_v1.entries[i].count;
                /* PARTIAL parse already subtracts 1: obj_id is 0-based (real item id). */
                int obj_id_0base = packet->u.update_inv_partial_v1.entries[i].obj_id;

                if( slot >= 0 && slot < UI_INVENTORY_MAX_ITEMS )
                    inv_sync_load_item_sprite(game, &inv->items[slot], obj_id_0base, count);
            }

            if( node_idx >= 0 )
                game->ui_root_buffer->components[node_idx].is_dirty = 1;
            else if( game->ui_root_buffer )
            {
                int32_t idx = uitree_find_by_component_id(game->ui_root_buffer, component_id);
                if( idx >= 0 )
                    game->ui_root_buffer->components[idx].is_dirty = 1;
            }
        }
    }
}

void
gamenet_rev245_2_exec_cam_lookat_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    if( !game->world )
        return;
    int tile_x = game->world->_base_tile_x + packet->u.cam_lookat_v1.local_x;
    int tile_z = game->world->_base_tile_z + packet->u.cam_lookat_v1.local_z;
    game->camera_world_x = tile_x * 128 + 64;
    game->camera_world_z = tile_z * 128 + 64;
    game->camera_world_y = -packet->u.cam_lookat_v1.height;
}

void
gamenet_rev245_2_exec_cam_moveto_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    if( !game->world )
        return;
    int tile_x = game->world->_base_tile_x + packet->u.cam_moveto_v1.local_x;
    int tile_z = game->world->_base_tile_z + packet->u.cam_moveto_v1.local_z;
    game->camera_world_x = tile_x * 128 + 64;
    game->camera_world_z = tile_z * 128 + 64;
    game->camera_world_y = -packet->u.cam_moveto_v1.height;
}

void
gamenet_rev245_2_exec_cam_shake_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    (void)game;
    (void)packet;
}

void
gamenet_rev245_2_exec_cam_reset_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    (void)game;
    (void)packet;
}

void
gamenet_rev245_2_exec_unset_map_flag_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    (void)packet;
    game->minimap_flag_has = 0;
    game->minimap_flag_x = 0;
    game->minimap_flag_z = 0;
}

void
gamenet_rev245_2_exec_update_runweight_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    game->run_weight = packet->u.update_runweight_v1.run_weight;
}

void
gamenet_rev245_2_exec_update_runenergy_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    game->run_energy = packet->u.update_run_energy_v1.run_energy;
}

void
gamenet_rev245_2_exec_update_stat_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    int stat = packet->u.update_stat_v1.stat;
    if( stat < 0 || stat >= PLAYER_STAT_COUNT )
        return;
    game->player_stat_xp[stat] = packet->u.update_stat_v1.xp;
    game->player_stat_level[stat] = packet->u.update_stat_v1.level;
}

void
gamenet_rev245_2_exec_hint_arrow_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    game->hint_arrow_type = packet->u.hint_arrow_v1.type;
    game->hint_arrow_entity_id = packet->u.hint_arrow_v1.id;
    game->hint_arrow_tile_x = packet->u.hint_arrow_v1.id;
    game->hint_arrow_tile_z = packet->u.hint_arrow_v1.z;
}

void
gamenet_rev245_2_exec_reset_anims_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
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
    world_player_entity_set_animation(game->world, ACTIVE_PLAYER_SLOT, -1, ANIMATION_TYPE_PRIMARY);
}

void
gamenet_rev245_2_exec_update_pid_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    (void)game;
    (void)packet;
}

void
gamenet_rev245_2_exec_varp_small_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    clientscript_vm_apply_varp_small(
        game->clientscript_vm, packet->u.varp_small_v1.variable, packet->u.varp_small_v1.value);
}

void
gamenet_rev245_2_exec_varp_large_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    clientscript_vm_apply_varp_large(
        game->clientscript_vm, packet->u.varp_large_v1.variable, packet->u.varp_large_v1.value);
}

void
gamenet_rev245_2_exec_varp_sync_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    (void)packet;
    clientscript_vm_apply_varp_sync(game->clientscript_vm);
}

void
gamenet_rev245_2_exec_update_zone_partial_follows_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    game->zone_base_x = packet->u.update_zone_partial_follows_v1.base_x * 8;
    game->zone_base_z = packet->u.update_zone_partial_follows_v1.base_z * 8;
}

void
gamenet_rev245_2_exec_update_zone_full_follows_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    game->zone_base_x = packet->u.update_zone_full_follows_v1.base_x * 8;
    game->zone_base_z = packet->u.update_zone_full_follows_v1.base_z * 8;
}

void
gamenet_rev245_2_exec_update_zone_partial_enclosed_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    game->zone_base_x = packet->u.update_zone_partial_follows_v1.base_x * 8;
    game->zone_base_z = packet->u.update_zone_partial_follows_v1.base_z * 8;
}

void
gamenet_rev245_2_exec_loc_anim_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    int x = zone_tile_x(game, packet->u.loc_anim_v1.pos);
    int z = zone_tile_z(game, packet->u.loc_anim_v1.pos);
    int seq_id = packet->u.loc_anim_v1.seq_id;

    if( x < 0 || x >= ZONE_SCENE_SIZE || z < 0 || z >= ZONE_SCENE_SIZE )
        return;

    /* Record as a LocChangeEntry with anim_seq_id for the tick loop to apply. */
    struct LocChangeEntry* entry = (struct LocChangeEntry*)malloc(sizeof(struct LocChangeEntry));
    memset(entry, 0, sizeof(*entry));
    entry->level = 0;
    entry->x = x;
    entry->z = z;
    entry->old_type = -1;
    entry->new_type = -1;
    entry->end_time = -1;
    entry->anim_seq_id = seq_id;
    entry->next = game->loc_changes_head;
    game->loc_changes_head = entry;
}

void
gamenet_rev245_2_exec_loc_merge_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    /* LOC_MERGE: add/change with associated player entity merging.
     * Treated as LOC_ADD_CHANGE with the given loc_id; the pid reference
     * is stored as start/end time for future entity-attach logic. */
    int x = zone_tile_x(game, packet->u.loc_merge_v1.pos);
    int z = zone_tile_z(game, packet->u.loc_merge_v1.pos);
    int info = packet->u.loc_merge_v1.info;
    int shape = info >> 2;
    int angle = info & 0x3;
    int loc_id = packet->u.loc_merge_v1.loc_id;

    if( x < 0 || x >= ZONE_SCENE_SIZE || z < 0 || z >= ZONE_SCENE_SIZE )
        return;

    struct LocChangeEntry* entry = (struct LocChangeEntry*)malloc(sizeof(struct LocChangeEntry));
    memset(entry, 0, sizeof(*entry));
    entry->level = 0;
    entry->x = x;
    entry->z = z;
    entry->old_type = -1;
    entry->new_type = loc_id;
    entry->old_shape = 0;
    entry->new_shape = shape;
    entry->old_angle = 0;
    entry->new_angle = angle;
    entry->start_time = packet->u.loc_merge_v1.start;
    entry->end_time = packet->u.loc_merge_v1.end;
    entry->anim_seq_id = -1;
    entry->next = game->loc_changes_head;
    game->loc_changes_head = entry;
}

void
gamenet_rev245_2_exec_map_anim_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    int x = zone_tile_x(game, packet->u.map_anim_v1.pos);
    int z = zone_tile_z(game, packet->u.map_anim_v1.pos);
    int spotanim = packet->u.map_anim_v1.id;
    int height = packet->u.map_anim_v1.height;
    int delay = packet->u.map_anim_v1.delay;

    if( x < 0 || x >= ZONE_SCENE_SIZE || z < 0 || z >= ZONE_SCENE_SIZE )
        return;

    struct MapAnimEntry* entry = (struct MapAnimEntry*)malloc(sizeof(struct MapAnimEntry));
    entry->x = x;
    entry->z = z;
    entry->level = 0;
    entry->spotanim_id = spotanim;
    entry->height = height;
    entry->delay = delay;
    entry->next = game->map_anims_head;
    game->map_anims_head = entry;
}

void
gamenet_rev245_2_exec_map_projanim_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    int src_x = zone_tile_x(game, packet->u.map_projanim_v1.pos);
    int src_z = zone_tile_z(game, packet->u.map_projanim_v1.pos);
    int dst_x = src_x + packet->u.map_projanim_v1.dx_offset;
    int dst_z = src_z + packet->u.map_projanim_v1.dz_offset;

    struct MapProjAnimEntry* entry =
        (struct MapProjAnimEntry*)malloc(sizeof(struct MapProjAnimEntry));
    entry->src_x = src_x;
    entry->src_z = src_z;
    entry->dst_x = dst_x;
    entry->dst_z = dst_z;
    entry->level = 0;
    entry->spotanim_id = packet->u.map_projanim_v1.spotanim;
    entry->src_height = packet->u.map_projanim_v1.src_height;
    entry->dst_height = packet->u.map_projanim_v1.dst_height;
    entry->start_delay = packet->u.map_projanim_v1.start_delay;
    entry->end_delay = packet->u.map_projanim_v1.end_delay;
    entry->target = packet->u.map_projanim_v1.target;
    entry->peak = packet->u.map_projanim_v1.peak;
    entry->arc = packet->u.map_projanim_v1.arc;
    entry->next = game->map_projanims_head;
    game->map_projanims_head = entry;
}

void
gamenet_rev245_2_exec_set_multiway_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    game->in_multiway = packet->u.set_multiway_v1.multiway;
}

void
gamenet_rev245_2_exec_set_player_op_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    int idx = packet->u.set_player_op_v1.op_index;
    if( idx < 1 || idx > 5 )
        return;

    int slot = idx - 1;
    /* Client.ts: playerOpPriority[index - 1] = priority === 0 */
    game->player_menu_op_deprioritize[slot] = (packet->u.set_player_op_v1.priority == 0);

    char* op = packet->u.set_player_op_v1.op_text;
    if( !op || strcasecmp(op, "null") == 0 )
    {
        game->player_menu_op[slot][0] = '\0';
        return;
    }

    strncpy(game->player_menu_op[slot], op, sizeof(game->player_menu_op[slot]) - 1);
    game->player_menu_op[slot][sizeof(game->player_menu_op[slot]) - 1] = '\0';
}

static void
gamenet_rev245_2_exec_message_game_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    if( packet->u.message_game_v1.text )
        game_add_message(game, 1 /* type: game */, packet->u.message_game_v1.text, NULL);
}

static void
gamenet_rev245_2_exec_message_private_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    /* PM deduplication: ignore if message_id already seen. */
    struct Chat* ch = game->chat;
    int mid = packet->u.message_private_v1.message_id;
    if( ch )
    {
        for( int i = 0; i < ch->pm_count; i++ )
        {
            if( ch->pm_ids[i] == mid )
                return;
        }
        if( ch->pm_count < 100 )
            ch->pm_ids[ch->pm_count++] = mid;
    }

    /* Decode base37 username to a readable string. */
    char sender[16] = { 0 };
    int64_t u = packet->u.message_private_v1.from;
    if( u > 0 )
    {
        int pos = 0;
        for( long long i = u; i != 0; i /= 37 )
        {
            int c = (int)(i % 37);
            sender[pos++] = (char)(c == 0 ? '_' : c <= 26 ? 'a' + c - 1 : '0' + c - 27);
        }
        /* Reverse */
        for( int a = 0, b = pos - 1; a < b; a++, b-- )
        {
            char tmp = sender[a];
            sender[a] = sender[b];
            sender[b] = tmp;
        }
        sender[pos] = '\0';
    }

    if( packet->u.message_private_v1.text )
        game_add_message(game, 3 /* type: private from */, packet->u.message_private_v1.text, sender);
}

static void
gamenet_rev245_2_exec_chat_filter_settings_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    if( game->chat )
    {
        game->chat->chat_public_mode = packet->u.chat_filter_settings_v1.chat_public_mode;
        game->chat->chat_private_mode = packet->u.chat_filter_settings_v1.chat_private_mode;
        game->chat->chat_trade_mode = packet->u.chat_filter_settings_v1.chat_trade_mode;
    }
}

static void
gamenet_rev245_2_exec_tut_flash_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    game->tutorial_tab_flash = packet->u.tut_flash_v1.tab_id;
}

static void
gamenet_rev245_2_exec_tut_open_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    game->tutorial_interface = packet->u.tut_open_v1.component_id;
}

static void
gamenet_rev245_2_exec_logout_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    (void)packet;
    game->net_state = GAME_NET_STATE_DISCONNECTED;
}

static void
gamenet_rev245_2_exec_p_countdialog_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    (void)packet;
    /* Signal the UI to open the count input dialog. */
    if( game->chat )
        game->chat->dialog_input_open = 1;
}

static void
gamenet_rev245_2_exec_reboot_timer_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    game->reboot_timer_ticks = packet->u.reboot_timer_v1.ticks;
}

static void
gamenet_rev245_2_exec_update_friendlist_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    int64_t un = packet->u.update_friendlist_v1.username;
    int world = packet->u.update_friendlist_v1.world;

    /* Update existing entry if present. */
    for( int i = 0; i < game->friend_count; i++ )
    {
        if( game->friend_usernames[i] == un )
        {
            game->friend_worlds[i] = world;
            return;
        }
    }
    /* New friend slot. */
    if( game->friend_count < GAME_SOCIAL_LIST_MAX )
    {
        game->friend_usernames[game->friend_count] = un;
        game->friend_worlds[game->friend_count] = world;
        game->friend_count++;
    }
}

static void
gamenet_rev245_2_exec_update_ignorelist_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    /* Replace entire ignore list. */
    int n = packet->u.update_ignorelist_v1.count;
    if( n > GAME_SOCIAL_LIST_MAX )
        n = GAME_SOCIAL_LIST_MAX;
    game->ignore_count = n;
    for( int i = 0; i < n; i++ )
        game->ignore_usernames[i] = packet->u.update_ignorelist_v1.usernames[i];
}

static void
gamenet_rev245_2_exec_if_openoverlay_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    int cid = if_decode_component_id(packet->u.if_openoverlay_v1.component_id);
    (void)game;
    (void)cid;
    /* Overlay interfaces rendered on top of main viewport; stored for draw pass. */
}

/* Audio stubs: hardware/OS audio not yet integrated. */
static void
gamenet_rev245_2_exec_synth_sound_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    (void)game;
    (void)packet;
}

static void
gamenet_rev245_2_exec_midi_song_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    (void)game;
    (void)packet;
}

static void
gamenet_rev245_2_exec_midi_jingle_v1(
    struct GGame* game,
    struct RevServerProtPacket* packet)
{
    (void)game;
    (void)packet;
}
