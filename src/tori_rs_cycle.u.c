#ifndef TORI_RS_CYCLE_U_C
#define TORI_RS_CYCLE_U_C

#include "3rd/lua/lauxlib.h"
#include "3rd/lua/lua.h"
#include "graphics/dash.h"
#include "osrs/buildcachedat.h"
#include "osrs/collision_map.h"
#include "osrs/dash_utils.h"
#include "osrs/game_entity.h"
#include "osrs/gameproto_process.h"
#include "osrs/interface.h"
#include "osrs/isaac.h"
#include "osrs/packetout.h"
#include "osrs/game.h"
#include "osrs/rscache/rsbuf.h"
#include "osrs/painters.h"
#include "osrs/scene.h"
#include "osrs/scenebuilder.h"
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

#define LUA_SCRIPTS_DIR "/Users/matthewevers/Documents/git_repos/3draster/src/osrs/scripts"

static const char*
script_path_for_kind(enum ScriptKind kind)
{
    switch( kind )
    {
    case SCRIPT_LOAD_SCENE_DAT:
        return "load_scene_dat.lua";
    case SCRIPT_LOAD_SCENE:
        return "load_scene.lua";
    case SCRIPT_PKT_NPC_INFO:
        return "pkt_npc_info.lua";
    case SCRIPT_PKT_REBUILD_NORMAL:
        return "pkt_rebuild_normal.lua";
    case SCRIPT_PKT_PLAYER_INFO:
        return "pkt_player_info.lua";
    case SCRIPT_LOAD_INVENTORY_MODELS:
        return "load_inventory_models.lua";
    case SCRIPT_PKT_UPDATE_INV_FULL:
        return "pkt_update_inv_full.lua";
    case SCRIPT_PKT_IF_SETTAB:
        return "pkt_if_settab.lua";
    case SCRIPT_PKT_IF_SETNPCHEAD:
        return "pkt_if_setnpchead.lua";
    case SCRIPT_PKT_IF_SETPLAYERHEAD:
        return "pkt_if_setplayerhead.lua";
    default:
        return NULL;
    }
}

/* Load script by path (under LUA_SCRIPTS_DIR if path has no '/'), run chunk in L to get
 * returned function, move to L_coro, push args for kind + game, resume. Returns lua_resume
 * status (LUA_OK, LUA_YIELD, or error). Optionally writes result count to nres_out. */
static int
start_script_from_item(
    struct GGame* game,
    struct ScriptQueueItem* item,
    int* nres_out)
{
    const char* path = script_path_for_kind(item->args.tag);
    if( !path )
        return LUA_ERRRUN;
    char fullpath[512];
    if( !strchr(path, '/') )
        snprintf(fullpath, sizeof(fullpath), "%s/%s", LUA_SCRIPTS_DIR, path);
    else
        snprintf(fullpath, sizeof(fullpath), "%s", path);

    if( luaL_loadfile(game->L, fullpath) != LUA_OK )
    {
        fprintf(stderr, "Failed to load script %s: %s\n", path, lua_tostring(game->L, -1));
        lua_pop(game->L, 1);
        game->running = false;
        return LUA_ERRRUN;
    }

    int nres;
    int nargs = 0;
    lua_xmove(game->L, game->L_coro, 1);

    switch( item->args.tag )
    {
    case SCRIPT_LOAD_SCENE_DAT:
    {
        struct ScriptArgsLoadSceneDat* a = &item->args.u.load_scene_dat;
        lua_pushinteger(game->L_coro, a->wx_sw);
        lua_pushinteger(game->L_coro, a->wz_sw);
        lua_pushinteger(game->L_coro, a->wx_ne);
        lua_pushinteger(game->L_coro, a->wz_ne);
        lua_pushinteger(game->L_coro, a->size_x);
        lua_pushinteger(game->L_coro, a->size_z);
        nargs = 6;
        break;
    }
    case SCRIPT_LOAD_SCENE:
    {
        struct ScriptArgsLoadScene* a = &item->args.u.load_scene;
        lua_pushinteger(game->L_coro, a->wx_sw);
        lua_pushinteger(game->L_coro, a->wz_sw);
        lua_pushinteger(game->L_coro, a->wx_ne);
        lua_pushinteger(game->L_coro, a->wz_ne);
        lua_pushinteger(game->L_coro, a->size_x);
        lua_pushinteger(game->L_coro, a->size_z);
        nargs = 6;
        break;
    }
    case SCRIPT_PKT_NPC_INFO:
    {
        struct ScriptArgsPktNpcInfo* a = &item->args.u.pkt_npc_info;
        lua_pushlightuserdata(game->L_coro, a->item);
        lua_pushlightuserdata(game->L_coro, a->io);
        nargs = 2;
        break;
    }
    case SCRIPT_PKT_REBUILD_NORMAL:
    {
        struct ScriptArgsPktRebuildNormal* a = &item->args.u.pkt_rebuild_normal;
        lua_pushlightuserdata(game->L_coro, a->item);
        lua_pushlightuserdata(game->L_coro, a->io);
        nargs = 2;
        break;
    }
    case SCRIPT_PKT_PLAYER_INFO:
    {
        struct ScriptArgsPktPlayerInfo* a = &item->args.u.pkt_player_info;
        lua_pushlightuserdata(game->L_coro, a->item);
        lua_pushlightuserdata(game->L_coro, a->io);
        nargs = 2;
        break;
    }
    case SCRIPT_PKT_UPDATE_INV_FULL:
    {
        struct ScriptArgsPktUpdateInvFull* a = &item->args.u.pkt_update_inv_full;
        lua_pushlightuserdata(game->L_coro, a->item);
        lua_pushlightuserdata(game->L_coro, a->io);
        nargs = 2;
        break;
    }
    case SCRIPT_PKT_IF_SETTAB:
    {
        struct ScriptArgsPktIfSetTab* a = &item->args.u.pkt_if_settab;
        lua_pushlightuserdata(game->L_coro, a->item);
        lua_pushlightuserdata(game->L_coro, a->io);
        nargs = 2;
        break;
    }
    case SCRIPT_PKT_IF_SETNPCHEAD:
    {
        struct ScriptArgsPktIfSetNpcHead* a = &item->args.u.pkt_if_setnpchead;
        lua_pushlightuserdata(game->L_coro, a->item);
        lua_pushlightuserdata(game->L_coro, a->io);
        nargs = 2;
        break;
    }
    case SCRIPT_PKT_IF_SETPLAYERHEAD:
    {
        struct ScriptArgsPktIfSetPlayerHead* a = &item->args.u.pkt_if_setplayerhead;
        lua_pushlightuserdata(game->L_coro, a->item);
        lua_pushlightuserdata(game->L_coro, a->io);
        nargs = 2;
        break;
    }
    case SCRIPT_LOAD_INVENTORY_MODELS:
    {
        // No args needed
        nargs = 0;
        break;
    }
    default:
        return LUA_ERRRUN;
    }

    int ret = lua_resume(game->L_coro, game->L, nargs, &nres);
    if( nres_out )
        *nres_out = nres;
    return ret;
}

static void
load_model_animations_dati(
    struct SceneElement* element,
    int sequence_id,
    struct BuildCacheDat* buildcachedat)
{
    struct SceneAnimation* scene_animation = NULL;
    struct CacheDatSequence* sequence = NULL;
    struct CacheAnimframe* animframe = NULL;

    struct DashFrame* dash_frame = NULL;
    struct DashFramemap* dash_framemap = NULL;

    if( sequence_id == -1 || !buildcachedat->sequences_hmap )
        return;

    scene_element_animation_free(element);
    scene_animation = scene_element_animation_new(element, 10);
    sequence = buildcachedat_get_sequence(buildcachedat, sequence_id);
    assert(sequence);

    for( int i = 0; i < sequence->frame_count; i++ )
    {
        // Get the frame definition ID from the second 2 bytes of the sequence frame ID The
        //     first 2 bytes are the sequence ID,
        //     the second 2 bytes are the frame file ID
        int frame_id = sequence->frames[i];

        animframe = buildcachedat_get_animframe(buildcachedat, frame_id);
        assert(animframe);

        if( !dash_framemap )
        {
            dash_framemap = dashframemap_new_from_animframe(animframe);
        }

        // From Client-TS 245.2
        int length = sequence->delay[i];
        if( length == 0 )
            length = animframe->delay;

        scene_element_animation_push_frame(
            element, dashframe_new_from_animframe(animframe), dash_framemap, length);
    }
    if( scene_animation )
        scene_animation->_anim_sequence_id = sequence_id;
}

/* Load primary + secondary sequences for walkmerge. Client.ts maskAnimate. */
static void
load_model_animations_walkmerge(
    struct SceneElement* element,
    int primary_sequence_id,
    int secondary_sequence_id,
    struct BuildCacheDat* buildcachedat)
{
    struct SceneAnimation* scene_animation = NULL;
    struct CacheDatSequence* primary_seq = NULL;
    struct CacheDatSequence* secondary_seq = NULL;
    struct CacheAnimframe* animframe = NULL;
    struct DashFramemap* dash_framemap = NULL;

    if( primary_sequence_id == -1 || secondary_sequence_id == -1 || !buildcachedat->sequences_hmap )
        return;

    primary_seq = buildcachedat_get_sequence(buildcachedat, primary_sequence_id);
    secondary_seq = buildcachedat_get_sequence(buildcachedat, secondary_sequence_id);
    if( !primary_seq || !secondary_seq || !primary_seq->walkmerge )
        return;

    scene_element_animation_free(element);
    scene_animation = scene_element_animation_new(element, 10);

    /* Load primary frames */
    for( int i = 0; i < primary_seq->frame_count; i++ )
    {
        int frame_id = primary_seq->frames[i];
        animframe = buildcachedat_get_animframe(buildcachedat, frame_id);
        assert(animframe);
        if( !dash_framemap )
            dash_framemap = dashframemap_new_from_animframe(animframe);
        int length = primary_seq->delay[i];
        if( length == 0 )
            length = animframe->delay;
        scene_element_animation_push_frame(
            element, dashframe_new_from_animframe(animframe), dash_framemap, length);
    }

    /* Load secondary frames */
    scene_animation->dash_frames_secondary =
        malloc((size_t)secondary_seq->frame_count * sizeof(struct DashFrame*));
    scene_animation->frame_count_secondary = secondary_seq->frame_count;
    for( int i = 0; i < secondary_seq->frame_count; i++ )
    {
        int frame_id = secondary_seq->frames[i];
        animframe = buildcachedat_get_animframe(buildcachedat, frame_id);
        assert(animframe);
        if( !dash_framemap )
            dash_framemap = dashframemap_new_from_animframe(animframe);
        scene_animation->dash_frames_secondary[i] = dashframe_new_from_animframe(animframe);
    }
    scene_animation->dash_framemap = dash_framemap;
    scene_animation->_anim_sequence_id = primary_sequence_id;
    scene_animation->_anim_secondary_sequence_id = secondary_sequence_id;
    scene_animation->walkmerge = primary_seq->walkmerge;
}

/* View of common pathing/movement/animation state shared by NPCs and players. */
struct EntityAnimUpdateView
{
    struct EntityPathing* pathing;
    struct EntityPosition* position;
    struct EntityOrientation* orientation;
    struct EntityAnimation* animation;
    int size_x;
    int size_z;
    void* scene_element;
    int* secondary_anim;
    int* secondary_anim_frame;
    int* secondary_anim_cycle;
    int face_entity;
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
    int fe = view->face_entity;
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
                struct NPCEntity* target = &game->npcs[fe];
                if( target->alive )
                {
                    target_x = target->position.x;
                    target_z = target->position.z;
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

            if( pid >= 0 && game->players[pid].alive )
            {
                struct PlayerEntity* target = &game->players[pid];
                target_x = target->position.x;
                target_z = target->position.z;
                has_target = 1;
            }
        }

        if( has_target )
        {
            int dst_x = view->position->x - target_x;
            int dst_z = view->position->z - target_z;
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
    if( view->orientation->face_square_x != 0 || view->orientation->face_square_z != 0 )
    {
        /* Client: dstX = e.x - (faceSquareX - mapBuildBaseX) * 64; same for Z */
        int base_x = game->scene_base_tile_x;
        int base_z = game->scene_base_tile_z;
        int target_x = (view->orientation->face_square_x - base_x) * 128;
        int target_z = (view->orientation->face_square_z - base_z) * 128;
        int dst_x = view->position->x - target_x;
        int dst_z = view->position->z - target_z;
        if( dst_x != 0 || dst_z != 0 )
        {
            double angle = atan2((double)dst_x, (double)dst_z);
            int yaw = (int)(angle * 325.949) & 0x7ff;
            if( yaw < 0 )
                yaw += 2048;
            view->orientation->dst_yaw = yaw;
        }
        view->orientation->face_square_x = 0;
        view->orientation->face_square_z = 0;
    }
}

static void
update_entity_anim(
    struct GGame* game,
    struct EntityAnimUpdateView* view,
    bool player)
{
    int seqId = view->animation->readyanim;
    int route_length = view->pathing->route_length;
    if( route_length == 0 )
    {
        /* Client.ts entityFace: when idle, still turn toward face_entity */
        entity_face(game, view, player);
        goto yaw_turn;
    }

    int x = view->position->x;
    int z = view->position->z;
    int dstX = view->pathing->route_x[route_length - 1] * 128 + view->size_x * 64;
    int dstZ = view->pathing->route_z[route_length - 1] * 128 + view->size_z * 64;

    if( dstX - x > 256 || dstX - x < -256 || dstZ - z > 256 || dstZ - z < -256 )
    {
        view->position->x = dstX;
        view->position->z = dstZ;
        return;
    }

    if( x < dstX )
    {
        if( z < dstZ )
            view->orientation->dst_yaw = 1280;
        else if( z > dstZ )
            view->orientation->dst_yaw = 1792;
        else
            view->orientation->dst_yaw = 1536;
    }
    else if( x > dstX )
    {
        if( z < dstZ )
            view->orientation->dst_yaw = 768;
        else if( z > dstZ )
            view->orientation->dst_yaw = 256;
        else
            view->orientation->dst_yaw = 512;
    }
    else if( z < dstZ )
        view->orientation->dst_yaw = 1024;
    else
        view->orientation->dst_yaw = 0;

    int deltaYaw = (view->orientation->dst_yaw - view->orientation->yaw) & 0x7ff;
    if( deltaYaw > 1024 )
        deltaYaw -= 2048;

    seqId = view->animation->walkanim_b;
    if( deltaYaw >= -256 && deltaYaw <= 256 )
        seqId = view->animation->walkanim;
    else if( deltaYaw >= 256 && deltaYaw < 768 )
        seqId = view->animation->walkanim_r;
    else if( deltaYaw >= -768 && deltaYaw <= -256 )
        seqId = view->animation->walkanim_l;

    if( seqId == -1 )
        seqId = view->animation->walkanim;

    /* Client.ts routeMove: only reduce speed when turning AND not facing entity AND turnspeed != 0
     */
    int moveSpeed = 4;
    if( view->orientation->yaw != view->orientation->dst_yaw && view->face_entity < 0 &&
        view->orientation->face_square_x == 0 && view->orientation->face_square_z == 0 )
        moveSpeed = 2;
    if( route_length > 2 )
        moveSpeed = 6;
    if( route_length > 3 )
        moveSpeed = 8;

    /* When not running, cap speed to walk. */
    if( !view->pathing->route_run[route_length - 1] && moveSpeed > 4 )
        moveSpeed = 4;
    if( view->pathing->route_run[route_length - 1] )
        moveSpeed <<= 0x1;

    if( view->pathing->route_run[route_length - 1] && moveSpeed >= 8 &&
        seqId == view->animation->walkanim && view->animation->runanim != -1 )
        seqId = view->animation->runanim;

    if( x < dstX )
    {
        view->position->x += moveSpeed;
        if( view->position->x > dstX )
            view->position->x = dstX;
    }
    else if( x > dstX )
    {
        view->position->x -= moveSpeed;
        if( view->position->x < dstX )
            view->position->x = dstX;
    }
    if( z < dstZ )
    {
        view->position->z += moveSpeed;
        if( view->position->z > dstZ )
            view->position->z = dstZ;
    }
    else if( z > dstZ )
    {
        view->position->z -= moveSpeed;
        if( view->position->z < dstZ )
            view->position->z = dstZ;
    }

    if( view->position->x == dstX && view->position->z == dstZ )
    {
        view->pathing->route_length--;
        if( view->pathing->route_length < 0 )
            view->pathing->route_length = 0;
    }

    /* Client.ts entityFace: overwrite dstYaw when face_entity is set */
    entity_face(game, view, player);

yaw_turn:;
    int remainingYaw = (view->orientation->dst_yaw - view->orientation->yaw) & 0x7ff;
    if( remainingYaw != 0 )
    {
        if( remainingYaw < 32 || remainingYaw > 2016 )
            view->orientation->yaw = view->orientation->dst_yaw;
        else if( remainingYaw > 1024 )
            view->orientation->yaw -= 32;
        else
            view->orientation->yaw += 32;
        view->orientation->yaw &= 0x7ff;

        if( seqId == view->animation->readyanim &&
            view->orientation->yaw != view->orientation->dst_yaw )
        {
            if( view->animation->turnanim != -1 )
                seqId = view->animation->turnanim;
            else
                seqId = view->animation->walkanim;
        }
    }

anim:;
    {
        /* Client.ts routeMove: secondaryAnim = seqId (readyanim, walkanim, runanim, turnanim) */
        *view->secondary_anim = seqId;
        if( seqId == -1 )
        {
            *view->secondary_anim_frame = 0;
            *view->secondary_anim_cycle = 0;
            struct SceneElement* scene_element = (struct SceneElement*)view->scene_element;
            scene_element_animation_free(scene_element);
        }
        /* Scene sync happens in entity_advance_anim; we don't load here to avoid overwriting
         * primary. */
    }
}

static void
update_npc_anim(
    struct GGame* game,
    int npc_entity_id)
{
    struct NPCEntity* npc_entity = &game->npcs[npc_entity_id];
    struct EntityAnimUpdateView view = {
        .pathing = &npc_entity->pathing,
        .position = &npc_entity->position,
        .orientation = &npc_entity->orientation,
        .animation = &npc_entity->animation,
        .size_x = npc_entity->size_x,
        .size_z = npc_entity->size_z,
        .scene_element = npc_entity->scene_element,
        .secondary_anim = &npc_entity->secondary_anim,
        .secondary_anim_frame = &npc_entity->secondary_anim_frame,
        .secondary_anim_cycle = &npc_entity->secondary_anim_cycle,
        .face_entity = npc_entity->orientation.face_entity,
    };
    update_entity_anim(game, &view, false);
}

static void
update_player_anim(
    struct GGame* game,
    int player_entity_id)
{
    struct PlayerEntity* player_entity = &game->players[player_entity_id];
    struct EntityAnimUpdateView view = {
        .pathing = &player_entity->pathing,
        .position = &player_entity->position,
        .orientation = &player_entity->orientation,
        .animation = &player_entity->animation,
        .size_x = 1,
        .size_z = 1,
        .scene_element = player_entity->scene_element,
        .secondary_anim = &player_entity->secondary_anim,
        .secondary_anim_frame = &player_entity->secondary_anim_frame,
        .secondary_anim_cycle = &player_entity->secondary_anim_cycle,
        .face_entity = player_entity->orientation.face_entity,
    };
    update_entity_anim(game, &view, true);
}

static int
sequence_get_frame_duration(
    struct BuildCacheDat* buildcachedat,
    struct CacheDatSequence* seq,
    int frame_i)
{
    if( !seq || frame_i < 0 || frame_i >= seq->frame_count )
        return 1;
    int d = seq->delay[frame_i];
    if( d == 0 )
    {
        struct CacheAnimframe* af =
            buildcachedat_get_animframe(buildcachedat, seq->frames[frame_i]);
        d = af ? af->delay : 1;
    }
    return d > 0 ? d : 1;
}

/* Client.ts entityAnim: advance primary and secondary anim cycles/frames. */
static void
entity_advance_anim(
    struct GGame* game,
    int* primary_anim,
    int* primary_anim_frame,
    int* primary_anim_cycle,
    int* primary_anim_delay,
    int* primary_anim_loop,
    int secondary_anim,
    int* secondary_anim_frame,
    int* secondary_anim_cycle,
    int cycles)
{
    struct CacheDatSequence* seq = NULL;

    for( int c = 0; c < cycles; c++ )
    {
        /* Secondary: Client.ts e.secondaryAnimCycle++, advance frame when cycle > duration */
        if( secondary_anim >= 0 )
        {
            seq = buildcachedat_get_sequence(game->buildcachedat, secondary_anim);
            if( seq )
            {
                (*secondary_anim_cycle)++;
                int dur =
                    sequence_get_frame_duration(game->buildcachedat, seq, *secondary_anim_frame);
                if( *secondary_anim_cycle > dur )
                {
                    *secondary_anim_cycle = 0;
                    (*secondary_anim_frame)++;
                }
                if( *secondary_anim_frame >= seq->frame_count )
                {
                    *secondary_anim_cycle = 0;
                    *secondary_anim_frame = 0;
                }
            }
        }

        /* Primary: decrement delay first; when 0, advance cycle/frame */
        if( *primary_anim_delay > 0 )
        {
            (*primary_anim_delay)--;
            continue;
        }

        if( *primary_anim < 0 )
            continue;

        seq = buildcachedat_get_sequence(game->buildcachedat, *primary_anim);
        if( !seq )
            continue;

        (*primary_anim_cycle)++;
        while( *primary_anim_frame < seq->frame_count &&
               *primary_anim_cycle >
                   sequence_get_frame_duration(game->buildcachedat, seq, *primary_anim_frame) )
        {
            *primary_anim_cycle -=
                sequence_get_frame_duration(game->buildcachedat, seq, *primary_anim_frame);
            (*primary_anim_frame)++;
        }

        if( *primary_anim_frame >= seq->frame_count )
        {
            int loops = seq->loops >= 0 ? seq->loops : seq->frame_count;
            *primary_anim_frame -= loops;
            (*primary_anim_loop)++;
            if( *primary_anim_loop >= seq->maxloops )
            {
                *primary_anim = -1;
                continue;
            }
            if( *primary_anim_frame < 0 || *primary_anim_frame >= seq->frame_count )
            {
                *primary_anim = -1;
                continue;
            }
        }
    }
}

/* Advance scene animation frame for static scenery (non-entity) elements. */
static void
advance_animation(
    struct SceneAnimation* animation,
    int cycles)
{
    if( !animation )
        return;

    for( int i = 0; i < cycles; i++ )
    {
        animation->cycle++;
        if( animation->cycle >= animation->frame_lengths[animation->frame_index] )
        {
            animation->cycle = 0;
            animation->frame_index++;
            if( animation->frame_index >= animation->frame_count )
            {
                animation->frame_index = 0;
            }
        }
    }
}

/* Pick active anim (primary if valid and delay==0, else secondary) and sync to scene.
 * Client.ts: when primary has walkmerge and secondary != primary, use maskAnimate. */
static void
entity_sync_anim_to_scene(
    struct GGame* game,
    struct SceneElement* scene_element,
    int primary_anim,
    int primary_anim_frame,
    int primary_anim_delay,
    int secondary_anim,
    int secondary_anim_frame,
    int secondary_anim_cycle)
{
    if( secondary_anim < 0 )
    {
        scene_element_animation_free(scene_element);
        return;
    }

    struct CacheDatSequence* primary_seq =
        (primary_anim >= 0) ? buildcachedat_get_sequence(game->buildcachedat, primary_anim) : NULL;
    int use_walkmerge =
        (primary_anim >= 0 && primary_anim_delay == 0 && secondary_anim >= 0 &&
         secondary_anim != primary_anim && primary_seq && primary_seq->walkmerge);

    if( use_walkmerge )
    {
        /* Client.ts maskAnimate: blend primary + secondary using walkmerge */
        struct SceneAnimation* anim = scene_element->animation;
        int need_load =
            (anim == NULL || anim->_anim_sequence_id != primary_anim ||
             anim->_anim_secondary_sequence_id != secondary_anim);

        if( need_load )
            load_model_animations_walkmerge(
                scene_element, primary_anim, secondary_anim, game->buildcachedat);

        anim = scene_element->animation;
        if( anim && anim->dash_frames_secondary )
        {
            anim->frame_index = primary_anim_frame;
            anim->frame_index_secondary = secondary_anim_frame;
            if( primary_anim_frame < anim->frame_count )
                anim->cycle = 0;
        }
    }
    else
    {
        int active = secondary_anim;
        int active_frame = secondary_anim_frame;
        int active_cycle = secondary_anim_cycle;
        if( primary_anim >= 0 && primary_anim_delay == 0 )
        {
            active = primary_anim;
            active_frame = primary_anim_frame;
            active_cycle = 0;
        }

        struct SceneAnimation* anim = scene_element->animation;
        int need_load = (anim == NULL || anim->_anim_sequence_id != active);

        if( need_load )
            load_model_animations_dati(scene_element, active, game->buildcachedat);

        anim = scene_element->animation;
        if( anim )
        {
            anim->frame_index = active_frame;
            if( active_frame < anim->frame_count )
                anim->cycle = active_cycle;
        }
    }
}

#define SCRIPTS_IN_PROGRESS 1
#define SCRIPTS_IDLE 0

static int
step_scripts(struct GGame* game)
{
    if( !game->L || !game->L_coro )
        return SCRIPTS_IDLE;

    int lua_ret = 0;
    int nres = 0;
    int ran_lua = 0;

step:;
    if( lua_status(game->L_coro) == LUA_YIELD )
    {
        lua_ret = lua_resume(game->L_coro, game->L, 1, &nres);
        ran_lua = 1;
    }
    else if( !script_queue_empty(&game->script_queue) )
    {
        /* No script running; start one from queue. */
        struct ScriptQueueItem* item = script_queue_pop(&game->script_queue);
        game->lua_current_script_item = item;
        lua_ret = start_script_from_item(game, item, &nres);
        ran_lua = 1;
    }

    if( ran_lua )
    {
        switch( lua_ret )
        {
        case LUA_YIELD:
            return SCRIPTS_IN_PROGRESS;
        case LUA_OK:
        {
            lua_pop(game->L_coro, nres);
            if( game->lua_current_script_item )
            {
                script_queue_free_item(game->lua_current_script_item);
                game->lua_current_script_item = NULL;
            }
            /* Attempt to run the next script from queue. */
            if( !script_queue_empty(&game->script_queue) )
                goto step;

            return SCRIPTS_IDLE;
        }
        default:
        lua_error:
        {
            const char* err = lua_tostring(game->L_coro, -1);
            const char* file_info = "unknown script";
            if( game->lua_current_script_item )
            {
                const char* path = script_path_for_kind(game->lua_current_script_item->args.tag);
                if( path )
                    file_info = path;
            }
            /* Walk stack to find a Lua source (skip C frames like =[C]) */
            {
                lua_Debug ar;
                int level = 0;
                const char* loc_src = NULL;
                int loc_line = 0;
                while( lua_getstack(game->L_coro, level, &ar) && level < 16 )
                {
                    if( lua_getinfo(game->L_coro, "Sl", &ar) && ar.source && ar.source[0] )
                    {
                        /* Lua file chunks are @path; C chunks are =[C] or =name */
                        if( ar.source[0] == '@' )
                        {
                            loc_src = ar.source + 1;
                            loc_line = ar.currentline;
                            break;
                        }
                    }
                    level++;
                }
                if( loc_src && loc_src[0] )
                {
                    if( loc_line > 0 )
                        fprintf(
                            stderr,
                            "Error in Lua coroutine (%s) at %s:%d: %s\n",
                            file_info,
                            loc_src,
                            loc_line,
                            err ? err : "unknown");
                    else
                        fprintf(
                            stderr,
                            "Error in Lua coroutine (%s) in %s: %s\n",
                            file_info,
                            loc_src,
                            err ? err : "unknown");
                }
                else
                    fprintf(
                        stderr,
                        "Error in Lua coroutine (%s): %s\n",
                        file_info,
                        err ? err : "unknown");
            }
            lua_pop(game->L_coro, 1);
            if( game->lua_current_script_item )
            {
                script_queue_free_item(game->lua_current_script_item);
                game->lua_current_script_item = NULL;
            }
            game->running = false;
            return SCRIPTS_IDLE;
        }
        }
    }

    return SCRIPTS_IDLE;
}

void
LibToriRS_GameStep(
    struct GGame* game,
    struct GInput* input,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    struct GameTask* task = NULL;
    struct GIOMessage message = { 0 };

    if( input->quit )
    {
        game->running = false;
        return;
    }

    /* Chat input (Client.ts handleInputKey when chatInterfaceId === -1).
     * chat_input_focused: 0=unfocused (keys go to movement); 1=focused (typing).
     * Enter when unfocused: focus. Enter when focused+empty: unfocus. Enter when focused+text: send. */
    if( game->chat_interface_id == -1 && GAME_NET_STATE_GAME == game->net_state )
    {
        if( input->chat_key_return )
        {
            if( !game->chat_input_focused )
                game->chat_input_focused = 1;
            else if( !game->chat_typed[0] )
                game->chat_input_focused = 0;
        }
        if( game->chat_input_focused )
        {
        if( input->chat_key_backspace )
        {
            size_t len = strlen(game->chat_typed);
            if( len > 0 )
                game->chat_typed[len - 1] = '\0';
        }
        if( input->chat_key_char && strlen(game->chat_typed) < (size_t)(GAME_CHAT_TYPED_LEN - 1) )
        {
            char c = (char)input->chat_key_char;
            if( (c >= 32 && c <= 122) || (c >= 48 && c <= 57) )
            {
                size_t len = strlen(game->chat_typed);
                game->chat_typed[len] = c;
                game->chat_typed[len + 1] = '\0';
            }
        }
        if( input->chat_key_return && game->chat_typed[0] )
        {
            int color = 0;
            int effect = 0;
            uint8_t temp_buf[256];
            struct RSBuffer buf;
            rsbuf_init(&buf, (int8_t*)temp_buf, sizeof(temp_buf));
            wordpack_pack(&buf, game->chat_typed);
            int wordpack_len = buf.position;
            int payload_len = 2 + wordpack_len;
            if( game->outbound_size + 2 + payload_len <= (int)sizeof(game->outbound_buffer) )
            {
                uint32_t op = (PKTOUT_LC245_2_MESSAGE_PUBLIC + isaac_next(game->random_out)) & 0xff;
                game->outbound_buffer[game->outbound_size++] = (uint8_t)op;
                game->outbound_buffer[game->outbound_size++] = 0;
                int start = game->outbound_size;
                game->outbound_buffer[game->outbound_size++] = (uint8_t)color;
                game->outbound_buffer[game->outbound_size++] = (uint8_t)effect;
                memcpy(game->outbound_buffer + game->outbound_size, temp_buf, (size_t)wordpack_len);
                game->outbound_size += wordpack_len;
                game->outbound_buffer[start - 1] = (uint8_t)payload_len;
                game_add_message(game, 2, game->chat_typed, "Player");
                game->chat_typed[0] = '\0';
                game->chat_input_focused = 0;
            }
        }
        }
    }

    gameproto_process(game, game->io);

    int scripts_status = step_scripts(game);
    if( scripts_status == SCRIPTS_IN_PROGRESS )
        return;

    LibToriRS_GameProcessInput(game, input);

    /* Always update scene with current players/NPCs so they are drawn even when a script
     * is yielded (e.g. interface loading). Otherwise we return early below and never push
     * dynamic elements, so the renderer sees an empty scene. */
    if( game->scenebuilder && game->scene )
    {
        scenebuilder_reset_dynamic_elements(game->scenebuilder, game->scene);

        for( int i = 0; i < game->npc_count; i++ )
        {
            int npc_id = game->active_npcs[i];
            if( npc_id == -1 )
                continue;
            struct NPCEntity* npc = &game->npcs[game->active_npcs[i]];
            if( npc->alive && npc->scene_element )
            {
                /* Client.ts: one updateMovement per loop cycle; move at most moveSpeed per cycle
                 * and stop at each waypoint. */
                for( int c = 0; c < game->cycles_elapsed; c++ )
                    update_npc_anim(game, game->active_npcs[i]);

                struct SceneElement* scene_element = (struct SceneElement*)npc->scene_element;
                entity_advance_anim(
                    game,
                    &npc->primary_anim,
                    &npc->primary_anim_frame,
                    &npc->primary_anim_cycle,
                    &npc->primary_anim_delay,
                    &npc->primary_anim_loop,
                    npc->secondary_anim,
                    &npc->secondary_anim_frame,
                    &npc->secondary_anim_cycle,
                    game->cycles_elapsed);
                entity_sync_anim_to_scene(
                    game,
                    scene_element,
                    npc->primary_anim,
                    npc->primary_anim_frame,
                    npc->primary_anim_delay,
                    npc->secondary_anim,
                    npc->secondary_anim_frame,
                    npc->secondary_anim_cycle);

                scene_element->dash_position->yaw = npc->orientation.yaw;
                scene_element->dash_position->x = npc->position.x;
                scene_element->dash_position->z = npc->position.z;
                scene_element->dash_position->y = scene_terrain_height_at_interpolated(
                    game->scene, npc->position.x, npc->position.z, 0);
                scene_element->entity_ptr = npc;
                scene_element->entity_kind = 1;
                scene_element->entity_npc_type_id = npc->npc_type_id;
                scenebuilder_push_dynamic_element(
                    game->scenebuilder,
                    game->scene,
                    npc->position.x / 128,
                    npc->position.z / 128,
                    0,
                    npc->size_x,
                    npc->size_z,
                    npc->scene_element);
            }
        }

        for( int i = 0; i < game->player_count; i++ )
        {
            int player_id = game->active_players[i];
            /* Active player is updated and pushed in the block below; avoid double movement. */
            if( player_id == ACTIVE_PLAYER_SLOT )
                continue;
            struct PlayerEntity* player = &game->players[player_id];
            if( player->alive && player->scene_element )
            {
                for( int c = 0; c < game->cycles_elapsed; c++ )
                    update_player_anim(game, player_id);

                struct SceneElement* scene_element = (struct SceneElement*)player->scene_element;
                entity_advance_anim(
                    game,
                    &player->primary_anim,
                    &player->primary_anim_frame,
                    &player->primary_anim_cycle,
                    &player->primary_anim_delay,
                    &player->primary_anim_loop,
                    player->secondary_anim,
                    &player->secondary_anim_frame,
                    &player->secondary_anim_cycle,
                    game->cycles_elapsed);
                entity_sync_anim_to_scene(
                    game,
                    scene_element,
                    player->primary_anim,
                    player->primary_anim_frame,
                    player->primary_anim_delay,
                    player->secondary_anim,
                    player->secondary_anim_frame,
                    player->secondary_anim_cycle);

                scene_element->dash_position->yaw = player->orientation.yaw;
                scene_element->dash_position->x = player->position.x;
                scene_element->dash_position->z = player->position.z;
                scene_element->dash_position->y = scene_terrain_height_at_interpolated(
                    game->scene, player->position.x, player->position.z, 0);
                scene_element->entity_ptr = player;
                scene_element->entity_kind = 2;
                scenebuilder_push_dynamic_element(
                    game->scenebuilder,
                    game->scene,
                    player->position.x / 128,
                    player->position.z / 128,
                    0,
                    1,
                    1,
                    player->scene_element);
            }
        }

        if( game->players[ACTIVE_PLAYER_SLOT].alive &&
            game->players[ACTIVE_PLAYER_SLOT].scene_element )
        {
            struct PlayerEntity* ap = &game->players[ACTIVE_PLAYER_SLOT];
            for( int c = 0; c < game->cycles_elapsed; c++ )
                update_player_anim(game, ACTIVE_PLAYER_SLOT);

            struct SceneElement* scene_element = (struct SceneElement*)ap->scene_element;
            entity_advance_anim(
                game,
                &ap->primary_anim,
                &ap->primary_anim_frame,
                &ap->primary_anim_cycle,
                &ap->primary_anim_delay,
                &ap->primary_anim_loop,
                ap->secondary_anim,
                &ap->secondary_anim_frame,
                &ap->secondary_anim_cycle,
                game->cycles_elapsed);
            entity_sync_anim_to_scene(
                game,
                scene_element,
                ap->primary_anim,
                ap->primary_anim_frame,
                ap->primary_anim_delay,
                ap->secondary_anim,
                ap->secondary_anim_frame,
                ap->secondary_anim_cycle);

            scene_element->dash_position->yaw = ap->orientation.yaw;
            scene_element->dash_position->x = ap->position.x;
            scene_element->dash_position->z = ap->position.z;
            scene_element->dash_position->y = scene_terrain_height_at_interpolated(
                game->scene, ap->position.x, ap->position.z, 0);
            scene_element->entity_ptr = ap;
            scene_element->entity_kind = 2;
            scenebuilder_push_dynamic_element(
                game->scenebuilder,
                game->scene,
                ap->position.x / 128,
                ap->position.z / 128,
                0,
                1,
                1,
                ap->scene_element);
        }
    }

    /* Scrollbar arrow hold: while mouse is down and over up/down arrow, scroll at rate *
     * game_cycles */
    if( game->mouse_button_down && game->mouse_x >= 553 && game->mouse_x < 763 &&
        game->mouse_y >= 205 && game->mouse_y < 498 )
    {
        struct CacheDatConfigComponent* hold_component = NULL;
        int hold_root_x = 553, hold_root_y = 205;
        if( game->sidebar_interface_id != -1 )
        {
            hold_component =
                buildcachedat_get_component(game->buildcachedat, game->sidebar_interface_id);
        }
        else if(
            game->selected_tab >= 0 && game->selected_tab < 14 &&
            game->tab_interface_id[game->selected_tab] != -1 )
        {
            hold_component = buildcachedat_get_component(
                game->buildcachedat, game->tab_interface_id[game->selected_tab]);
        }
        if( hold_component )
        {
            int sb_y = 0, sb_height = 0, sb_scroll_height = 0;
            int scrollbar_hit = interface_find_scrollbar_at(
                game,
                hold_component,
                hold_root_x,
                hold_root_y,
                game->mouse_x,
                game->mouse_y,
                &sb_y,
                &sb_height,
                &sb_scroll_height);
            if( scrollbar_hit >= 0 )
            {
                int local_y = game->mouse_y - sb_y;
                int max_scroll = sb_scroll_height - sb_height;
                if( max_scroll < 0 )
                    max_scroll = 0;
                if( local_y < 16 )
                {
                    int delta = game->cycles_elapsed - game->scroll_arrow_hold_cycles_last;
                    int step = 4 * (delta > 0 ? delta : 1);
                    interface_handle_scrollbar_arrow_step(game, scrollbar_hit, max_scroll, 1, step);
                    game->scroll_arrow_hold_cycles_last = game->cycles_elapsed;
                }
                else if( local_y >= sb_height - 16 )
                {
                    int delta = game->cycles_elapsed - game->scroll_arrow_hold_cycles_last;
                    int step = 4 * (delta > 0 ? delta : 1);
                    interface_handle_scrollbar_arrow_step(game, scrollbar_hit, max_scroll, 0, step);
                    game->scroll_arrow_hold_cycles_last = game->cycles_elapsed;
                }
                else
                    game->scroll_arrow_hold_cycles_last = game->cycles_elapsed;
            }
            else
                game->scroll_arrow_hold_cycles_last = game->cycles_elapsed;
        }
        else
            game->scroll_arrow_hold_cycles_last = game->cycles_elapsed;
    }
    else
        game->scroll_arrow_hold_cycles_last = game->cycles_elapsed;

    // Handle interface clicks (tab bar, then inventory items, etc.)
    game->interface_consumed_click = 0;
    if( game->mouse_clicked )
    {
        int mouse_x = game->mouse_clicked_x;
        int mouse_y = game->mouse_clicked_y;
        int panel_h = 50;
        int panel_top = (game->iface_view_port && game->iface_view_port->height > panel_h)
                            ? (game->iface_view_port->height - panel_h)
                            : 453;

        // Tab bar click (Client.ts handleTabInput 3018-3072): same bounds, only if tab has
        // interface
        int tab_clicked = -1;
        if( mouse_x >= 539 && mouse_x <= 573 && mouse_y >= 169 && mouse_y < 205 &&
            game->tab_interface_id[0] != -1 )
            tab_clicked = 0;
        else if(
            mouse_x >= 569 && mouse_x <= 599 && mouse_y >= 168 && mouse_y < 205 &&
            game->tab_interface_id[1] != -1 )
            tab_clicked = 1;
        else if(
            mouse_x >= 597 && mouse_x <= 627 && mouse_y >= 168 && mouse_y < 205 &&
            game->tab_interface_id[2] != -1 )
            tab_clicked = 2;
        else if(
            mouse_x >= 625 && mouse_x <= 669 && mouse_y >= 168 && mouse_y < 203 &&
            game->tab_interface_id[3] != -1 )
            tab_clicked = 3;
        else if(
            mouse_x >= 666 && mouse_x <= 696 && mouse_y >= 168 && mouse_y < 205 &&
            game->tab_interface_id[4] != -1 )
            tab_clicked = 4;
        else if(
            mouse_x >= 694 && mouse_x <= 724 && mouse_y >= 168 && mouse_y < 205 &&
            game->tab_interface_id[5] != -1 )
            tab_clicked = 5;
        else if(
            mouse_x >= 722 && mouse_x <= 756 && mouse_y >= 169 && mouse_y < 205 &&
            game->tab_interface_id[6] != -1 )
            tab_clicked = 6;
        else if(
            mouse_x >= 540 && mouse_x <= 574 && mouse_y >= 466 && mouse_y < 502 &&
            game->tab_interface_id[7] != -1 )
            tab_clicked = 7;
        else if(
            mouse_x >= 572 && mouse_x <= 602 && mouse_y >= 466 && mouse_y < 503 &&
            game->tab_interface_id[8] != -1 )
            tab_clicked = 8;
        else if(
            mouse_x >= 599 && mouse_x <= 629 && mouse_y >= 466 && mouse_y < 503 &&
            game->tab_interface_id[9] != -1 )
            tab_clicked = 9;
        else if(
            mouse_x >= 627 && mouse_x <= 671 && mouse_y >= 467 && mouse_y < 502 &&
            game->tab_interface_id[10] != -1 )
            tab_clicked = 10;
        else if(
            mouse_x >= 669 && mouse_x <= 699 && mouse_y >= 466 && mouse_y < 503 &&
            game->tab_interface_id[11] != -1 )
            tab_clicked = 11;
        else if(
            mouse_x >= 696 && mouse_x <= 726 && mouse_y >= 466 && mouse_y < 503 &&
            game->tab_interface_id[12] != -1 )
            tab_clicked = 12;
        else if(
            mouse_x >= 724 && mouse_x <= 758 && mouse_y >= 466 && mouse_y < 502 &&
            game->tab_interface_id[13] != -1 )
            tab_clicked = 13;
        if( tab_clicked >= 0 )
        {
            game->interface_consumed_click = 1;
            game->mouse_cycle = -1; /* No cross when clicking on 2D interface */
            game->selected_tab = tab_clicked;
            /* Client.ts does not send a packet for tab change; server sets tab via IF_SETTAB_ACTIVE */
        }
        /* Chat area click: focus chat input when no chat interface (Client.ts chat at 17,357 size ~536x96) */
        else if( game->chat_interface_id == -1 && mouse_x >= 17 && mouse_x < 553 &&
                 mouse_y >= 357 && mouse_y < 453 )
        {
            game->interface_consumed_click = 1;
            game->chat_input_focused = 1;
        }
        /* Chat interface buttons: when chat_interface_id is set, hit-test chat component for clicks */
        else if( game->chat_interface_id != -1 )
        {
            int chat_y = (game->view_port && game->view_port->height > 0)
                             ? (game->view_port->height + 19)
                             : 357;
            int chat_height = 96;
            if( mouse_x >= 17 && mouse_x < 553 && mouse_y >= chat_y &&
                mouse_y < chat_y + chat_height && mouse_y < panel_top )
            {
                game->interface_consumed_click = 1;
                game->mouse_cycle = -1;
                struct CacheDatConfigComponent* chat_component =
                    buildcachedat_get_component(game->buildcachedat, game->chat_interface_id);
                if( chat_component )
                {
                    int sb_y = 0, sb_height = 0, sb_scroll_height = 0;
                    int scrollbar_hit = interface_find_scrollbar_at(
                        game,
                        chat_component,
                        17,
                        chat_y,
                        mouse_x,
                        mouse_y,
                        &sb_y,
                        &sb_height,
                        &sb_scroll_height);
                    if( scrollbar_hit >= 0 )
                    {
                        int local_y = mouse_y - sb_y;
                        int max_scroll = sb_scroll_height - sb_height;
                        if( max_scroll < 0 )
                            max_scroll = 0;
                        if( local_y < 16 )
                            interface_handle_scrollbar_arrow(game, scrollbar_hit, max_scroll, 1);
                        else if( local_y >= sb_height - 16 )
                            interface_handle_scrollbar_arrow(game, scrollbar_hit, max_scroll, 0);
                        else
                            interface_handle_scrollbar_click(
                                game, scrollbar_hit, sb_y, sb_height, sb_scroll_height, mouse_y);
                    }
                    else
                    {
                        int hit_component_id = -1;
                        int hit_client_code = 0;
                        int button_action = IF_BUTTON_ACTION_IF_BUTTON;
                        int menu_param_a = 0, menu_param_b = 0, menu_param_c = 0;
                        if( interface_find_button_click_at(
                                game,
                                chat_component,
                                17,
                                chat_y,
                                mouse_x,
                                mouse_y,
                                &hit_component_id,
                                &hit_client_code,
                                &button_action,
                                &menu_param_a,
                                &menu_param_b,
                                &menu_param_c) )
                        {
                            if( hit_client_code == CC_LOGOUT )
                            {
                                int opcode = PKTOUT_LC245_2_LOGOUT;
                                uint32_t op = (opcode + isaac_next(game->random_out)) & 0xff;
                                game->outbound_buffer[game->outbound_size++] = (uint8_t)op;
                                uint16_t c = menu_param_c;
                                game->outbound_buffer[game->outbound_size++] = (c >> 8) & 0xFF;
                                game->outbound_buffer[game->outbound_size++] = c & 0xFF;
                            }
                            else if( button_action == IF_BUTTON_ACTION_CLOSE_MODAL )
                            {
                                int opcode = PKTOUT_LC245_2_CLOSE_MODAL;
                                uint32_t op = (opcode + isaac_next(game->random_out)) & 0xff;
                                game->outbound_buffer[game->outbound_size++] = (uint8_t)op;
                            }
                            else if( button_action == IF_BUTTON_ACTION_RESUME_PAUSEBUTTON )
                            {
                                int opcode = PKTOUT_LC245_2_RESUME_PAUSEBUTTON;
                                uint32_t op = (opcode + isaac_next(game->random_out)) & 0xff;
                                game->outbound_buffer[game->outbound_size++] = (uint8_t)op;
                                uint16_t c = menu_param_c;
                                game->outbound_buffer[game->outbound_size++] = (c >> 8) & 0xFF;
                                game->outbound_buffer[game->outbound_size++] = c & 0xFF;
                            }
                            else
                            {
                                int opcode = PKTOUT_LC245_2_IF_BUTTON;
                                uint32_t op = (opcode + isaac_next(game->random_out)) & 0xff;
                                game->outbound_buffer[game->outbound_size++] = (uint8_t)op;
                                uint16_t c = menu_param_c;
                                game->outbound_buffer[game->outbound_size++] = (c >> 8) & 0xFF;
                                game->outbound_buffer[game->outbound_size++] = c & 0xFF;
                                interface_apply_button_click_varp_optimistic(game, menu_param_c);
                            }
                        }
                    }
                }
            }
        }
        /* Client.ts handleChatModeInput: four buttons in bottom strip (panel matches platform
         * privacy_panel_y = bottom 50px when height < 503, else y 453). */
        else if( mouse_y >= panel_top && mouse_y < panel_top + panel_h )
        {
            game->interface_consumed_click = 1;
            game->mouse_cycle = -1; /* No cross when clicking on 2D interface */
            if( mouse_x >= 6 && mouse_x <= 106 )
            {
                game->chat_public_mode = (game->chat_public_mode + 1) % 4;
            }
            else if( mouse_x >= 135 && mouse_x <= 235 )
            {
                game->chat_private_mode = (game->chat_private_mode + 1) % 3;
            }
            else if( mouse_x >= 273 && mouse_x <= 373 )
            {
                game->chat_trade_mode = (game->chat_trade_mode + 1) % 3;
            }
            else if( mouse_x >= 412 && mouse_x <= 512 )
            {
                /* TODO: open report abuse interface (clientCode 600) */
            }
        }
        /* Viewport overlay (e.g. bank, inventory): hit-test buttons and inventory, else block 3D.
         * Sidebar starts at x 553. */
        else if( game->viewport_interface_id != -1 && mouse_x < 553 )
        {
            game->interface_consumed_click = 1;
            game->mouse_cycle = -1; /* No cross when clicking on 2D interface */
            struct CacheDatConfigComponent* viewport_component =
                buildcachedat_get_component(game->buildcachedat, game->viewport_interface_id);
            if( viewport_component )
            {
                int sb_y = 0, sb_height = 0, sb_scroll_height = 0;
                int scrollbar_hit = interface_find_scrollbar_at(
                    game,
                    viewport_component,
                    0,
                    0,
                    mouse_x,
                    mouse_y,
                    &sb_y,
                    &sb_height,
                    &sb_scroll_height);
                if( scrollbar_hit >= 0 )
                {
                    int local_y = mouse_y - sb_y;
                    int max_scroll = sb_scroll_height - sb_height;
                    if( max_scroll < 0 )
                        max_scroll = 0;
                    if( local_y < 16 )
                        interface_handle_scrollbar_arrow(game, scrollbar_hit, max_scroll, 1);
                    else if( local_y >= sb_height - 16 )
                        interface_handle_scrollbar_arrow(game, scrollbar_hit, max_scroll, 0);
                    else
                        interface_handle_scrollbar_click(
                            game, scrollbar_hit, sb_y, sb_height, sb_scroll_height, mouse_y);
                }
                else
                {
                    int hit_component_id = -1;
                    int hit_client_code = 0;
                    int button_action = IF_BUTTON_ACTION_IF_BUTTON;
                    int menu_param_a = 0, menu_param_b = 0, menu_param_c = 0;
                    if( interface_find_button_click_at(
                            game,
                            viewport_component,
                            0,
                            0,
                            mouse_x,
                            mouse_y,
                            &hit_component_id,
                            &hit_client_code,
                            &button_action,
                            &menu_param_a,
                            &menu_param_b,
                            &menu_param_c) )
                    {
                        if( hit_client_code == CC_LOGOUT )
                        {
                            int opcode = PKTOUT_LC245_2_LOGOUT;
                            uint32_t op = (opcode + isaac_next(game->random_out)) & 0xff;
                            game->outbound_buffer[game->outbound_size++] = (uint8_t)op;
                            uint16_t c = menu_param_c;
                            game->outbound_buffer[game->outbound_size++] = (c >> 8) & 0xFF;
                            game->outbound_buffer[game->outbound_size++] = c & 0xFF;
                        }
                        else if( button_action == IF_BUTTON_ACTION_CLOSE_MODAL )
                        {
                            int opcode = PKTOUT_LC245_2_CLOSE_MODAL;
                            uint32_t op = (opcode + isaac_next(game->random_out)) & 0xff;
                            game->outbound_buffer[game->outbound_size++] = (uint8_t)op;
                        }
                        else if( button_action == IF_BUTTON_ACTION_RESUME_PAUSEBUTTON )
                        {
                            int opcode = PKTOUT_LC245_2_RESUME_PAUSEBUTTON;
                            uint32_t op = (opcode + isaac_next(game->random_out)) & 0xff;
                            game->outbound_buffer[game->outbound_size++] = (uint8_t)op;
                            uint16_t c = menu_param_c;
                            game->outbound_buffer[game->outbound_size++] = (c >> 8) & 0xFF;
                            game->outbound_buffer[game->outbound_size++] = c & 0xFF;
                        }
                        else
                        {
                            int opcode = PKTOUT_LC245_2_IF_BUTTON;
                            uint32_t op = (opcode + isaac_next(game->random_out)) & 0xff;
                            game->outbound_buffer[game->outbound_size++] = (uint8_t)op;
                            uint16_t c = menu_param_c;
                            game->outbound_buffer[game->outbound_size++] = (c >> 8) & 0xFF;
                            game->outbound_buffer[game->outbound_size++] = c & 0xFF;
                            /* Client.ts doAction: optimistic varp update for TOGGLE/SELECT */
                            interface_apply_button_click_varp_optimistic(game, menu_param_c);
                        }
                    }
                    else if( viewport_component->type == COMPONENT_TYPE_LAYER && viewport_component->children )
                    {
                        for( int i = 0; i < viewport_component->children_count; i++ )
                        {
                            if( !viewport_component->childX || !viewport_component->childY )
                                continue;
                            int child_id = viewport_component->children[i];
                            int childX = viewport_component->childX[i];
                            int childY = viewport_component->childY[i];
                            struct CacheDatConfigComponent* child =
                                buildcachedat_get_component(game->buildcachedat, child_id);
                            if( !child )
                                continue;
                            childX += child->x;
                            childY += child->y;
                            if( child->type == COMPONENT_TYPE_INV )
                            {
                                int slot = interface_check_inv_click(
                                    game, child, childX, childY, mouse_x, mouse_y);
                                if( slot != -1 )
                                {
                                    int obj_id = child->invSlotObjId[slot] - 1;
                                    int action = interface_get_inv_default_action(
                                        game, child, obj_id, slot);
                                    interface_handle_inv_button(
                                        game, action, obj_id, slot, child_id);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
        else if( mouse_x >= 553 && mouse_x < 763 && mouse_y >= 205 && mouse_y < 498 )
        {
            game->interface_consumed_click = 1;
            game->mouse_cycle = -1; /* No cross when clicking on 2D interface */

            // Determine which interface to check
            int component_id = -1;
            struct CacheDatConfigComponent* component = NULL;

            if( game->sidebar_interface_id != -1 )
            {
                component_id = game->sidebar_interface_id;
                component = buildcachedat_get_component(game->buildcachedat, component_id);
            }
            else if(
                game->selected_tab >= 0 && game->selected_tab < 14 &&
                game->tab_interface_id[game->selected_tab] != -1 )
            {
                component_id = game->tab_interface_id[game->selected_tab];
                component = buildcachedat_get_component(game->buildcachedat, component_id);
            }
            if( component )
            {
                int sb_y = 0, sb_height = 0, sb_scroll_height = 0;
                int scrollbar_hit = interface_find_scrollbar_at(
                    game,
                    component,
                    553,
                    205,
                    mouse_x,
                    mouse_y,
                    &sb_y,
                    &sb_height,
                    &sb_scroll_height);
                if( scrollbar_hit >= 0 )
                {
                    /* Client.ts: top 16px = up arrow, bottom 16px = down arrow, else track */
                    int local_y = mouse_y - sb_y;
                    int max_scroll = sb_scroll_height - sb_height;
                    if( max_scroll < 0 )
                        max_scroll = 0;
                    if( local_y < 16 )
                        interface_handle_scrollbar_arrow(game, scrollbar_hit, max_scroll, 1);
                    else if( local_y >= sb_height - 16 )
                        interface_handle_scrollbar_arrow(game, scrollbar_hit, max_scroll, 0);
                    else
                        interface_handle_scrollbar_click(
                            game, scrollbar_hit, sb_y, sb_height, sb_scroll_height, mouse_y);
                }
                else
                {
                    // Client.ts: when button is clicked, send packet per addComponentOptions +
                    // doAction. IF_BUTTON/TOGGLE/SELECT send IF_BUTTON p2(c). CLOSE sends CLOSE_MODAL.
                    // PAUSE sends RESUME_PAUSEBUTTON p2(c). LOGOUT sends LOGOUT p2(c).
                    int hit_component_id = -1;
                    int hit_client_code = 0;
                    int button_action = IF_BUTTON_ACTION_IF_BUTTON;
                    int menu_param_a = 0, menu_param_b = 0, menu_param_c = 0;
                    if( interface_find_button_click_at(
                            game,
                            component,
                            553,
                            205,
                            mouse_x,
                            mouse_y,
                            &hit_component_id,
                            &hit_client_code,
                            &button_action,
                            &menu_param_a,
                            &menu_param_b,
                            &menu_param_c) )
                    {
                        if( hit_client_code == CC_LOGOUT )
                        {
                            int opcode = PKTOUT_LC245_2_LOGOUT;
                            uint32_t op = (opcode + isaac_next(game->random_out)) & 0xff;
                            game->outbound_buffer[game->outbound_size++] = (uint8_t)op;
                            uint16_t c = menu_param_c;
                            game->outbound_buffer[game->outbound_size++] = (c >> 8) & 0xFF;
                            game->outbound_buffer[game->outbound_size++] = c & 0xFF;
                        }
                        else if( button_action == IF_BUTTON_ACTION_CLOSE_MODAL )
                        {
                            int opcode = PKTOUT_LC245_2_CLOSE_MODAL;
                            uint32_t op = (opcode + isaac_next(game->random_out)) & 0xff;
                            game->outbound_buffer[game->outbound_size++] = (uint8_t)op;
                        }
                        else if( button_action == IF_BUTTON_ACTION_RESUME_PAUSEBUTTON )
                        {
                            int opcode = PKTOUT_LC245_2_RESUME_PAUSEBUTTON;
                            uint32_t op = (opcode + isaac_next(game->random_out)) & 0xff;
                            game->outbound_buffer[game->outbound_size++] = (uint8_t)op;
                            uint16_t c = menu_param_c;
                            game->outbound_buffer[game->outbound_size++] = (c >> 8) & 0xFF;
                            game->outbound_buffer[game->outbound_size++] = c & 0xFF;
                        }
                        else
                        {
                            /* IF_BUTTON, TOGGLE_BUTTON, SELECT_BUTTON: all send IF_BUTTON p2(c) */
                            int opcode = PKTOUT_LC245_2_IF_BUTTON;
                            uint32_t op = (opcode + isaac_next(game->random_out)) & 0xff;
                            game->outbound_buffer[game->outbound_size++] = (uint8_t)op;
                            uint16_t c = menu_param_c;
                            game->outbound_buffer[game->outbound_size++] = (c >> 8) & 0xFF;
                            game->outbound_buffer[game->outbound_size++] = c & 0xFF;
                            /* Client.ts doAction: optimistic varp update for TOGGLE/SELECT */
                            interface_apply_button_click_varp_optimistic(game, menu_param_c);
                        }
                    }
                    else
                    {
                        // If it's a layer, check children for inventory components
                        if( component->type == COMPONENT_TYPE_LAYER && component->children )
                        {
                            for( int i = 0; i < component->children_count; i++ )
                            {
                                if( !component->childX || !component->childY )
                                    continue;

                                int child_id = component->children[i];
                                int childX = 553 + component->childX[i];
                                int childY = 205 + component->childY[i];

                                struct CacheDatConfigComponent* child =
                                    buildcachedat_get_component(game->buildcachedat, child_id);

                                if( !child )
                                    continue;

                                childX += child->x;
                                childY += child->y;

                                if( child->type == COMPONENT_TYPE_INV )
                                {
                                    int slot = interface_check_inv_click(
                                        game, child, childX, childY, mouse_x, mouse_y);

                                    if( slot != -1 )
                                    {
                                        int obj_id = child->invSlotObjId[slot] - 1;
                                        int action = interface_get_inv_default_action(
                                            game, child, obj_id, slot);

                                        interface_handle_inv_button(
                                            game, action, obj_id, slot, child_id);
                                        break;
                                    }
                                }
                            }
                        }
                        else if( component->type == COMPONENT_TYPE_INV )
                        {
                            // Direct inventory component (not in a layer)
                            int slot = interface_check_inv_click(
                                game, component, 553, 205, mouse_x, mouse_y);

                            if( slot != -1 )
                            {
                                int obj_id = component->invSlotObjId[slot] - 1;
                                int action =
                                    interface_get_inv_default_action(game, component, obj_id, slot);

                                interface_handle_inv_button(
                                    game, action, obj_id, slot, component_id);
                            }
                        }
                    }
                }
            }
        }
    }

    /* Terrain tile click: send MOVE_GAMECLICK. Client.ts tryMove(routeTileX[0], routeTileZ[0],
     * x, z, 0, ..., true). Payload: p1(size), p1(run), p2(startX+sceneBase), p2(startZ+sceneBase),
     * then for i=1..bufferSize-1: p1(bfsStepX-startX), p1(bfsStepZ-startZ) (signed byte offset
     * from start). bufferSize = min(length, 25); start = first path point (route head). */

    dash_animate_textures(game->sys_dash, game->cycles_elapsed);
    if( game->cycle >= game->next_notimeout_cycle && GAME_NET_STATE_GAME == game->net_state )
    {
        game->next_notimeout_cycle = game->cycle + 50;
        int opcode = 206;
        uint32_t op = (opcode + isaac_next(game->random_out)) & 0xff;
        game->outbound_buffer[game->outbound_size++] = op;
    }

    /* Scene dynamic elements (players/NPCs) are updated earlier in GameStep so they
     * are always present for rendering even when a script is yielded. */

    for( int i = 0; i < game->cycles_elapsed; i++ )
    {
        game->cycle++;

        for( int i = 0; i < game->scene->scenery->elements_length; i++ )
        {
            struct SceneElement* element = scene_element_at(game->scene->scenery, i);
            if( element->animation )
            {
                advance_animation(element->animation, 1);
            }
        }
    }
    game->cycles_elapsed = 0;

    // Flush outbound buffer to network
    if( game->outbound_size > 0 && GAME_NET_STATE_GAME == game->net_state )
    {
        printf("Flushing %d bytes to network\n", game->outbound_size);
        ringbuf_write(game->netout, game->outbound_buffer, game->outbound_size);
        game->outbound_size = 0;
    }
}

#endif