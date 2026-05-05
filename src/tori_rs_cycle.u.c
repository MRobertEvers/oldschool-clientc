#ifndef TORI_RS_CYCLE_U_C
#define TORI_RS_CYCLE_U_C

#include "3rd/lua/lauxlib.h"
#include "3rd/lua/lua.h"
#include "graphics/dash.h"
#include "osrs/buildcachedat.h"
#include "osrs/collision_map.h"
#include "osrs/dash_utils.h"
#include "osrs/game.h"
#include "osrs/game_entity.h"
#include "osrs/gameproto_process.h"
#include "osrs/interface.h"
#include "osrs/isaac.h"
#include "osrs/core/clientprot_core.h"
#include "osrs/packetout.h"
#include "osrs/painters.h"
#include "osrs/rscache/rsbuf.h"
#include "osrs/script_queue.h"
#include "osrs/wordpack.h"
#include "tori_rs.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Client.ts ClientProt.MOVE_GAMECLICK = 182 (index 255) */
#define MOVE_GAMECLICK_OPCODE 182

/* Client.ts ClientCode.CC_LOGOUT = 205 */
#define CC_LOGOUT 205

// static void
// load_model_animations_dati(
//     struct SceneElement* element,
//     int sequence_id,
//     struct BuildCacheDat* buildcachedat)
// {
//     // struct SceneAnimation* scene_animation = NULL;
//     // struct CacheDatSequence* sequence = NULL;
//     // struct CacheAnimframe* animframe = NULL;

//     // struct DashFrame* dash_frame = NULL;
//     // struct DashFramemap* dash_framemap = NULL;

//     // if( sequence_id == -1 || !buildcachedat->sequences_hmap )
//     //     return;

//     // scene_element_animation_free(element);
//     // scene_animation = scene_element_animation_new(element, 10);
//     // sequence = buildcachedat_get_sequence(buildcachedat, sequence_id);
//     // assert(sequence);

//     // for( int i = 0; i < sequence->frame_count; i++ )
//     // {
//     //     // Get the frame definition ID from the second 2 bytes of the sequence frame ID The
//     //     //     first 2 bytes are the sequence ID,
//     //     //     the second 2 bytes are the frame file ID
//     //     int frame_id = sequence->frames[i];

//     //     animframe = buildcachedat_get_animframe(buildcachedat, frame_id);
//     //     assert(animframe);

//     //     if( !dash_framemap )
//     //     {
//     //         dash_framemap = dashframemap_new_from_animframe(animframe);
//     //     }

//     //     // From Client-TS 245.2
//     //     int length = sequence->delay[i];
//     //     if( length == 0 )
//     //         length = animframe->delay;

//     //     // scene_element_animation_push_frame(
//     //     //     element, dashframe_new_from_animframe(animframe), dash_framemap, length);
//     // }
//     // // if( scene_animation )
//     // //     scene_animation->_anim_sequence_id = sequence_id;
// }

// /* Load primary + secondary sequences for walkmerge. Client.ts maskAnimate. */
// static void
// load_model_animations_walkmerge(
//     struct SceneElement* element,
//     int primary_sequence_id,
//     int secondary_sequence_id,
//     struct BuildCacheDat* buildcachedat)
// {
//     struct SceneAnimation* scene_animation = NULL;
//     struct CacheDatSequence* primary_seq = NULL;
//     struct CacheDatSequence* secondary_seq = NULL;
//     struct CacheAnimframe* animframe = NULL;
//     struct DashFramemap* dash_framemap = NULL;

//     if( primary_sequence_id == -1 || secondary_sequence_id == -1 ||
//     !buildcachedat->sequences_hmap )
//         return;

//     primary_seq = buildcachedat_get_sequence(buildcachedat, primary_sequence_id);
//     secondary_seq = buildcachedat_get_sequence(buildcachedat, secondary_sequence_id);
//     if( !primary_seq || !secondary_seq || !primary_seq->walkmerge )
//         return;

//     scene_element_animation_free(element);
//     scene_animation = scene_element_animation_new(element, 10);

//     /* Load primary frames */
//     for( int i = 0; i < primary_seq->frame_count; i++ )
//     {
//         int frame_id = primary_seq->frames[i];
//         animframe = buildcachedat_get_animframe(buildcachedat, frame_id);
//         assert(animframe);
//         if( !dash_framemap )
//             dash_framemap = dashframemap_new_from_animframe(animframe);
//         int length = primary_seq->delay[i];
//         if( length == 0 )
//             length = animframe->delay;
//         scene_element_animation_push_frame(
//             element, dashframe_new_from_animframe(animframe), dash_framemap, length);
//     }

//     /* Load secondary frames */
//     scene_animation->dash_frames_secondary =
//         malloc((size_t)secondary_seq->frame_count * sizeof(struct DashFrame*));
//     scene_animation->frame_count_secondary = secondary_seq->frame_count;
//     for( int i = 0; i < secondary_seq->frame_count; i++ )
//     {
//         int frame_id = secondary_seq->frames[i];
//         animframe = buildcachedat_get_animframe(buildcachedat, frame_id);
//         assert(animframe);
//         if( !dash_framemap )
//             dash_framemap = dashframemap_new_from_animframe(animframe);
//         scene_animation->dash_frames_secondary[i] = dashframe_new_from_animframe(animframe);
//     }
//     scene_animation->dash_framemap = dash_framemap;
//     scene_animation->_anim_sequence_id = primary_sequence_id;
//     scene_animation->_anim_secondary_sequence_id = secondary_sequence_id;
//     scene_animation->walkmerge = primary_seq->walkmerge;
// }

/* View of common pathing/movement/animation state shared by NPCs and players. */
struct EntityAnimUpdateView
{
    struct EntitySceneElement* scene2_element;
    struct EntityPathing* pathing;
    struct EntityDrawPosition* draw_position;
    struct EntityOrientation* orientation;
    struct EntityAnimation* animation;
    int entity_id;
    int size_x;
    int size_z;
};

/* Client.ts entityFace: set dstYaw from faceEntity or faceSquare target. 325.949 ≈ 2048/(2π) for
 * rad->yaw. */
static void
entity_face(
    struct GGame* game,
    struct EntityAnimUpdateView* view,
    bool is_player)
{
    (void)is_player;
    int fe = -1;
    if( fe >= 0 )
    {
        int target_x = 0;
        int target_z = 0;
        int has_target = 0;

        if( fe < 32768 )
        {
            /* NPC target */
            if( fe < MAX_NPCS )
            {
                struct NPCEntity* target = world_npc(game->world, fe);
                if( target->alive )
                {
                    target_x = target->draw_position.x;
                    target_z = target->draw_position.z;
                    has_target = 1;
                }
            }
        }
        else
        {
            /* Player target: index = fe - 32768. Client.ts: if index === selfSlot, use LOCAL_PLAYER
             */
            int pid = fe - 32768;
            if( pid == ACTIVE_PLAYER_SLOT )
                pid = ACTIVE_PLAYER_SLOT;
            else if( pid < MAX_PLAYERS )
                pid = pid;
            else
                pid = -1;

            if( pid >= 0 && world_player(game->world, pid)->alive )
            {
                struct PlayerEntity* target = world_player(game->world, pid);
                target_x = target->draw_position.x;
                target_z = target->draw_position.z;
                has_target = 1;
            }
        }

        if( has_target )
        {
            int dst_x = view->draw_position->x - target_x;
            int dst_z = view->draw_position->z - target_z;
            if( dst_x != 0 || dst_z != 0 )
            {
                /* Client.ts: dstYaw = ((Math.atan2(dstX, dstZ) * 325.949) | 0) & 0x7ff */
                double angle = atan2((double)dst_x, (double)dst_z);
                int yaw = (int)(angle * 325.949) & 0x7ff;
                if( yaw < 0 )
                    yaw += 2048;
                view->orientation->dst_yaw = yaw;
            }
        }
    }

    /* Client.ts FACESQUARE: face tile; cleared after use */
    // if( view->orientation->face_square_x != 0 || view->orientation->face_square_z != 0 )
    // {
    //     /* Client: dstX = e.x - (faceSquareX - mapBuildBaseX) * 64; same for Z */
    //     int base_x = game->scene_base_tile_x;
    //     int base_z = game->scene_base_tile_z;
    //     int target_x = (view->orientation->face_square_x - base_x) * 128;
    //     int target_z = (view->orientation->face_square_z - base_z) * 128;
    //     int dst_x = view->position->x - target_x;
    //     int dst_z = view->position->z - target_z;
    //     if( dst_x != 0 || dst_z != 0 )
    //     {
    //         double angle = atan2((double)dst_x, (double)dst_z);
    //         int yaw = (int)(angle * 325.949) & 0x7ff;
    //         if( yaw < 0 )
    //             yaw += 2048;
    //         view->orientation->dst_yaw = yaw;
    //     }
    // view->orientation->face_square_x = 0;
    // view->orientation->face_square_z = 0;
    // }
}

/* ── Local try_move implementation ──────────────────────────────────────────
 * Mirrors Client.ts tryMove (type=0 walk, type=1 minimap) but locally:
 * - Runs BFS via collision_map_bfs_path
 * - Loads result into the local player EntityPathing
 * - Sets game->minimap_flag_x/z to the destination
 * - Does NOT send any packet to the server (all packet calls are no-ops per guard)
 *
 * Returns true if a path was found and loaded.
 */
#define GAME_TRY_MOVE_MAX_PATH 25 /* mirrors TS bufferSize = min(length, 25) */

static bool
game_try_move(
    struct GGame* game,
    int dst_tile_x,
    int dst_tile_z)
{
    if( !game->world || !game->world->collision_map )
        return false;

    struct PlayerEntity* player = world_player(game->world, ACTIVE_PLAYER_SLOT);
    if( !player || !player->alive )
        return false;

    /* Current tile from draw_position (same as routeX[0] in TS). */
    int src_x = player->draw_position.x >> 7;
    int src_z = player->draw_position.z >> 7;

    /* Clamp destination to collision map bounds. */
    struct CollisionMap* cm = game->world->collision_map;
    if( dst_tile_x < 0 || dst_tile_x >= cm->size_x || dst_tile_z < 0 || dst_tile_z >= cm->size_z )
        return false;

    if( src_x == dst_tile_x && src_z == dst_tile_z )
        return true;

    int path_x[GAME_TRY_MOVE_MAX_PATH];
    int path_z[GAME_TRY_MOVE_MAX_PATH];

    int path_len = collision_map_bfs_path(cm, src_x, src_z, dst_tile_x, dst_tile_z, path_x, path_z, GAME_TRY_MOVE_MAX_PATH);
    if( path_len <= 0 )
        return false;

    /* Load path into player pathing.  BFS returns path[0] = first step from src,
     * path[path_len-1] = destination.  entity_pathing_push_xz prepends to route[0],
     * so we push path[path_len-1] first and path[0] last so that:
     *   route_x[route_length - 1] = path[0]  (first step processed by update_entity)
     *   route_x[0]               = path[path_len-1]  (destination, last to process)
     */
    player->pathing.route_length = 0;
    player->pathing.route_x[0] = src_x;
    player->pathing.route_z[0] = src_z;

    /* Push in reverse (destination first, first-step last). */
    int push_count = path_len < 10 ? path_len : 9; /* route_length max is 9 steps */
    for( int i = push_count - 1; i >= 0; i-- )
    {
        entity_pathing_push_xz(&player->pathing, path_x[i], path_z[i], PATHSTEP_WALK);
    }

    /* Update minimap flag to the destination tile. */
    game->minimap_flag_x = dst_tile_x;
    game->minimap_flag_z = dst_tile_z;
    game->minimap_flag_has = 1;

    return true;
}

void
LibToriRS_GameNetProcess(struct GGame* game)
{
    gameproto_process(game);

    if( game->cycle >= game->next_notimeout_cycle && GAME_NET_STATE_GAME == game->net_state )
    {
        game->next_notimeout_cycle = game->cycle + 50;
        clientprot_no_timeout(game);
    }
}

void
LibToriRS_GameStep(
    struct GGame* game,
    struct GInput* input,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    struct GameTask* task = NULL;

    if( input->quit )
    {
        game->running = false;
        return;
    }

    if( game->tick_ms >= game->next_camera_save_ms )
    {
        game->next_camera_save_ms = game->tick_ms + 1000;
    }

    LibToriRS_GameProcessInput(game, input);

    /* Terrain tile click: run local BFS and load path into player pathing.
     * Mirrors Client.ts tryMove(routeTileX[0],routeTileZ[0], x, z, true, ..., type=0/1).
     * Packets are dropped by clientprot_core_emit guard so nothing reaches the wire. */
    if( game->tile_clicked_x >= 0 && game->world )
    {
        game_try_move(game, game->tile_clicked_x, game->tile_clicked_z);
        game->tile_clicked_x = -1;
        game->tile_clicked_z = -1;
        game->tile_clicked_level = -1;
    }

    if( game->world )
        world_cycle(game->world, game->cycles_elapsed);

    dash_animate_textures(game->sys_dash, game->cycles_elapsed);

    /* Scene dynamic elements (players/NPCs) are updated earlier in GameStep so they
     * are always present for rendering even when a script is yielded. */

    game->cycle += game->cycles_elapsed;
    game->cycles_elapsed = 0;
}

#endif