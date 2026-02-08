#ifndef TORI_RS_CYCLE_U_C
#define TORI_RS_CYCLE_U_C

#include "3rd/lua/lauxlib.h"
#include "3rd/lua/lua.h"
#include "osrs/buildcachedat.h"
#include "osrs/dash_utils.h"
#include "osrs/gameproto_process.h"
#include "osrs/interface.h"
#include "osrs/scenebuilder.h"
#include "osrs/script_queue.h"
#include "tori_rs.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
}

static void
update_npc_anim(
    struct GGame* game,
    int npc_entity_id)
{
    struct NPCEntity* npc_entity = &game->npcs[npc_entity_id];

    int seqId = npc_entity->animation.readyanim;
    int route_length = npc_entity->pathing.route_length;
    if( route_length == 0 )
        goto anim;

    int x = npc_entity->position.x;
    int z = npc_entity->position.z;
    int dstX = npc_entity->pathing.route_x[route_length - 1] * 128 + npc_entity->size_x * 64;
    int dstZ = npc_entity->pathing.route_z[route_length - 1] * 128 + npc_entity->size_z * 64;

    if( dstX - x > 256 || dstX - x < -256 || dstZ - z > 256 || dstZ - z < -256 )
    {
        npc_entity->position.x = dstX;
        npc_entity->position.z = dstZ;
        return;
    }

    if( x < dstX )
    {
        if( z < dstZ )
        {
            npc_entity->orientation.dst_yaw = 1280;
        }
        else if( z > dstZ )
        {
            npc_entity->orientation.dst_yaw = 1792;
        }
        else
        {
            npc_entity->orientation.dst_yaw = 1536;
        }
    }
    else if( x > dstX )
    {
        if( z < dstZ )
        {
            npc_entity->orientation.dst_yaw = 768;
        }
        else if( z > dstZ )
        {
            npc_entity->orientation.dst_yaw = 256;
        }
        else
        {
            npc_entity->orientation.dst_yaw = 512;
        }
    }
    else if( z < dstZ )
    {
        npc_entity->orientation.dst_yaw = 1024;
    }
    else
    {
        npc_entity->orientation.dst_yaw = 0;
    }

    int deltaYaw = (npc_entity->orientation.dst_yaw - npc_entity->orientation.yaw) & 0x7ff;
    if( deltaYaw > 1024 )
    {
        deltaYaw -= 2048;
    }

    seqId = npc_entity->animation.walkanim_b;
    if( deltaYaw >= -256 && deltaYaw <= 256 )
    {
        seqId = npc_entity->animation.walkanim;
    }
    else if( deltaYaw >= 256 && deltaYaw < 768 )
    {
        seqId = npc_entity->animation.walkanim_r;
    }
    else if( deltaYaw >= -768 && deltaYaw <= -256 )
    {
        seqId = npc_entity->animation.walkanim_l;
    }

    if( seqId == -1 )
    {
        seqId = npc_entity->animation.walkanim;
    }

    // e.secondarySeqId = seqId;

    int moveSpeed = 4;
    if( npc_entity->orientation.yaw != npc_entity->orientation.dst_yaw )
    {
        moveSpeed = 2;
    }

    if( route_length > 2 )
    {
        moveSpeed = 6;
    }

    if( route_length > 3 )
    {
        moveSpeed = 8;
    }

    // if( npc_entity->animation.seq_delay_move > 0 && route_length > 1 )
    // {
    //     moveSpeed = 8;
    //     npc_entity->animation.seq_delay_move--;
    // }

    if( npc_entity->pathing.route_run[route_length - 1] )
    {
        moveSpeed <<= 0x1;
    }

    if( moveSpeed >= 8 && seqId == npc_entity->animation.walkanim &&
        npc_entity->animation.runanim != -1 )
    {
        seqId = npc_entity->animation.runanim;
    }

    if( x < dstX )
    {
        npc_entity->position.x += moveSpeed;
        if( npc_entity->position.x > dstX )
        {
            npc_entity->position.x = dstX;
        }
    }
    else if( x > dstX )
    {
        npc_entity->position.x -= moveSpeed;
        if( npc_entity->position.x < dstX )
        {
            npc_entity->position.x = dstX;
        }
    }
    if( z < dstZ )
    {
        npc_entity->position.z += moveSpeed;
        if( npc_entity->position.z > dstZ )
        {
            npc_entity->position.z = dstZ;
        }
    }
    else if( z > dstZ )
    {
        npc_entity->position.z -= moveSpeed;
        if( npc_entity->position.z < dstZ )
        {
            npc_entity->position.z = dstZ;
        }
    }

    if( npc_entity->position.x == dstX && npc_entity->position.z == dstZ )
    {
        npc_entity->pathing.route_length--;
    }

    int remainingYaw = (npc_entity->orientation.dst_yaw - npc_entity->orientation.yaw) & 0x7ff;
    if( remainingYaw != 0 )
    {
        if( remainingYaw < 32 || remainingYaw > 2016 )
        {
            npc_entity->orientation.yaw = npc_entity->orientation.dst_yaw;
        }
        else if( remainingYaw > 1024 )
        {
            npc_entity->orientation.yaw -= 32;
        }
        else
        {
            npc_entity->orientation.yaw += 32;
        }

        npc_entity->orientation.yaw &= 0x7ff;

        if( seqId == npc_entity->animation.readyanim &&
            npc_entity->orientation.yaw != npc_entity->orientation.dst_yaw )
        {
            // if( npc_entity->animation.turnanim != -1 )
            // {
            //     seqId = npc_entity->animation.turnanim;
            // }
            // else
            {
                seqId = npc_entity->animation.walkanim;
            }
        }
    }

anim:;
    struct SceneElement* scene_element = (struct SceneElement*)npc_entity->scene_element;
    if( npc_entity->curranim != seqId && seqId != -1 )
    {
        npc_entity->curranim = seqId;

        load_model_animations_dati(scene_element, seqId, game->buildcachedat);
    }
    else if( seqId == -1 )
    {
        npc_entity->curranim = -1;
        scene_element_animation_free(scene_element);
    }
}

static void
update_player_anim(
    struct GGame* game,
    int player_entity_id)
{
    struct PlayerEntity* player_entity = &game->players[player_entity_id];

    int seqId = player_entity->animation.readyanim;
    int route_length = player_entity->pathing.route_length;
    if( route_length == 0 )
        goto anim;

    int x = player_entity->position.x;
    int z = player_entity->position.z;
    int dstX = player_entity->pathing.route_x[route_length - 1] * 128 + 1 * 64;
    int dstZ = player_entity->pathing.route_z[route_length - 1] * 128 + 1 * 64;

    if( dstX - x > 256 || dstX - x < -256 || dstZ - z > 256 || dstZ - z < -256 )
    {
        player_entity->position.x = dstX;
        player_entity->position.z = dstZ;
        return;
    }

    if( x < dstX )
    {
        if( z < dstZ )
        {
            player_entity->orientation.dst_yaw = 1280;
        }
        else if( z > dstZ )
        {
            player_entity->orientation.dst_yaw = 1792;
        }
        else
        {
            player_entity->orientation.dst_yaw = 1536;
        }
    }
    else if( x > dstX )
    {
        if( z < dstZ )
        {
            player_entity->orientation.dst_yaw = 768;
        }
        else if( z > dstZ )
        {
            player_entity->orientation.dst_yaw = 256;
        }
        else
        {
            player_entity->orientation.dst_yaw = 512;
        }
    }
    else if( z < dstZ )
    {
        player_entity->orientation.dst_yaw = 1024;
    }
    else
    {
        player_entity->orientation.dst_yaw = 0;
    }

    int deltaYaw = (player_entity->orientation.dst_yaw - player_entity->orientation.yaw) & 0x7ff;
    if( deltaYaw > 1024 )
    {
        deltaYaw -= 2048;
    }

    seqId = player_entity->animation.walkanim_b;
    if( deltaYaw >= -256 && deltaYaw <= 256 )
    {
        seqId = player_entity->animation.walkanim;
    }
    else if( deltaYaw >= 256 && deltaYaw < 768 )
    {
        seqId = player_entity->animation.walkanim_r;
    }
    else if( deltaYaw >= -768 && deltaYaw <= -256 )
    {
        seqId = player_entity->animation.walkanim_l;
    }

    if( seqId == -1 )
    {
        seqId = player_entity->animation.walkanim;
    }

    // e.secondarySeqId = seqId;

    int moveSpeed = 4;
    if( player_entity->orientation.yaw != player_entity->orientation.dst_yaw )
    {
        moveSpeed = 2;
    }

    if( route_length > 2 )
    {
        moveSpeed = 6;
    }

    if( route_length > 3 )
    {
        moveSpeed = 8;
    }

    // if( npc_entity->animation.seq_delay_move > 0 && route_length > 1 )
    // {
    //     moveSpeed = 8;
    //     npc_entity->animation.seq_delay_move--;
    // }

    if( player_entity->pathing.route_run[route_length - 1] )
    {
        moveSpeed <<= 0x1;
    }

    if( moveSpeed >= 8 && seqId == player_entity->animation.walkanim &&
        player_entity->animation.runanim != -1 )
    {
        seqId = player_entity->animation.runanim;
    }

    if( x < dstX )
    {
        player_entity->position.x += moveSpeed;
        if( player_entity->position.x > dstX )
        {
            player_entity->position.x = dstX;
        }
    }
    else if( x > dstX )
    {
        player_entity->position.x -= moveSpeed;
        if( player_entity->position.x < dstX )
        {
            player_entity->position.x = dstX;
        }
    }
    if( z < dstZ )
    {
        player_entity->position.z += moveSpeed;
        if( player_entity->position.z > dstZ )
        {
            player_entity->position.z = dstZ;
        }
    }
    else if( z > dstZ )
    {
        player_entity->position.z -= moveSpeed;
        if( player_entity->position.z < dstZ )
        {
            player_entity->position.z = dstZ;
        }
    }

    if( player_entity->position.x == dstX && player_entity->position.z == dstZ )
    {
        player_entity->pathing.route_length--;
    }

    int remainingYaw =
        (player_entity->orientation.dst_yaw - player_entity->orientation.yaw) & 0x7ff;
    if( remainingYaw != 0 )
    {
        if( remainingYaw < 32 || remainingYaw > 2016 )
        {
            player_entity->orientation.yaw = player_entity->orientation.dst_yaw;
        }
        else if( remainingYaw > 1024 )
        {
            player_entity->orientation.yaw -= 32;
        }
        else
        {
            player_entity->orientation.yaw += 32;
        }

        player_entity->orientation.yaw &= 0x7ff;

        if( seqId == player_entity->animation.readyanim &&
            player_entity->orientation.yaw != player_entity->orientation.dst_yaw )
        {
            if( player_entity->animation.turnanim != -1 )
            {
                seqId = player_entity->animation.turnanim;
            }
            else
            {
                seqId = player_entity->animation.walkanim;
            }
        }
    }

anim:;
    struct SceneElement* scene_element = (struct SceneElement*)player_entity->scene_element;
    if( player_entity->curranim != seqId && seqId != -1 )
    {
        player_entity->curranim = seqId;
        load_model_animations_dati(scene_element, seqId, game->buildcachedat);
    }
    else if( seqId == -1 )
    {
        player_entity->curranim = -1;
        scene_element_animation_free(scene_element);
    }
}

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
            fprintf(stderr, "Error in Lua coroutine: %s\n", err ? err : "unknown");
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

    gameproto_process(game, game->io);

    int scripts_status = step_scripts(game);
    if( scripts_status == SCRIPTS_IN_PROGRESS )
        return;

    LibToriRS_GameProcessInput(game, input);
    
    // Handle interface clicks (inventory items, etc.)
    if( game->mouse_clicked )
    {
        int mouse_x = game->mouse_clicked_x;
        int mouse_y = game->mouse_clicked_y;
        
        printf("Mouse clicked at: (%d, %d)\n", mouse_x, mouse_y);
        
        // Check if click is in sidebar area (553, 205, 210x293)
        if( mouse_x >= 553 && mouse_x < 763 && mouse_y >= 205 && mouse_y < 498 )
        {
            printf("Click detected in sidebar area\n");
            
            // Determine which interface to check
            int component_id = -1;
            struct CacheDatConfigComponent* component = NULL;
            
            if( game->sidebar_interface_id != -1 )
            {
                component_id = game->sidebar_interface_id;
                component = buildcachedat_get_component(game->buildcachedat, component_id);
                printf("Using sidebar_interface_id: %d, component=%p\n", component_id, (void*)component);
            }
            else if( game->selected_tab >= 0 && game->selected_tab < 14 &&
                    game->tab_interface_id[game->selected_tab] != -1 )
            {
                component_id = game->tab_interface_id[game->selected_tab];
                component = buildcachedat_get_component(game->buildcachedat, component_id);
                printf("Using tab_interface_id[%d]: %d, component=%p\n", 
                       game->selected_tab, component_id, (void*)component);
            }
            else
            {
                printf("No active interface found\n");
            }
            
            if( component )
            {
                printf("Component type: %d (LAYER=%d, INV=%d)\n", 
                       component->type, COMPONENT_TYPE_LAYER, COMPONENT_TYPE_INV);
                
                // If it's a layer, check children for inventory components
                if( component->type == COMPONENT_TYPE_LAYER && component->children )
                {
                    printf("Checking %d children for inventory\n", component->children_count);
                    
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
                        
                        printf("  Child %d: id=%d, type=%d, pos=(%d,%d)\n", 
                               i, child_id, child->type, childX, childY);
                        
                        if( child->type == COMPONENT_TYPE_INV )
                        {
                            int slot = interface_check_inv_click(
                                game, child, childX, childY, mouse_x, mouse_y);
                            
                            if( slot != -1 )
                            {
                                int obj_id = child->invSlotObjId[slot] - 1;
                                int action = 602; // INV_BUTTON1
                                
                                printf("Inventory click detected: slot=%d, obj_id=%d, child=%d\n", 
                                       slot, obj_id, child_id);
                                
                                interface_handle_inv_button(game, action, obj_id, slot, child_id);
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
                    
                    printf("Slot clicked: %d\n", slot);
                    
                    if( slot != -1 )
                    {
                        int obj_id = component->invSlotObjId[slot] - 1;
                        int action = 602; // INV_BUTTON1
                        
                        printf("Inventory click detected: slot=%d, obj_id=%d, component=%d\n", 
                               slot, obj_id, component_id);
                        
                        interface_handle_inv_button(game, action, obj_id, slot, component_id);
                    }
                }
                else
                {
                    printf("Component is not an inventory or layer type\n");
                }
            }
        }
        else
        {
            printf("Click outside sidebar area\n");
        }
    }

    dash_animate_textures(game->sys_dash, game->cycles_elapsed);
    if( game->cycle >= game->next_notimeout_cycle && GAME_NET_STATE_GAME == game->net_state )
    {
        game->next_notimeout_cycle = game->cycle + 50;
        int opcode = 206;
        uint32_t op = (opcode + isaac_next(game->random_out)) & 0xff;
        game->outbound_buffer[game->outbound_size++] = op;
    }

    if( game->scenebuilder )
        scenebuilder_reset_dynamic_elements(game->scenebuilder, game->scene);

    for( int i = 0; i < game->npc_count; i++ )
    {
        int npc_id = game->active_npcs[i];
        if( npc_id == -1 )
            continue;
        struct NPCEntity* npc = &game->npcs[game->active_npcs[i]];
        if( npc->alive && npc->scene_element )
        {
            if( game->cycles_elapsed != 0 )
            {
                update_npc_anim(game, game->active_npcs[i]);
            }

            scenebuilder_push_dynamic_element(
                game->scenebuilder,
                game->scene,
                npc->position.x / 128,
                npc->position.z / 128,
                0,
                npc->size_x,
                npc->size_z,
                npc->scene_element);

            struct SceneElement* scene_element = (struct SceneElement*)npc->scene_element;
            scene_element->dash_position->yaw = npc->orientation.yaw;
            scene_element->dash_position->x = npc->position.x;
            scene_element->dash_position->z = npc->position.z;

            advance_animation(scene_element->animation, game->cycles_elapsed);
        }
    }

    for( int i = 0; i < game->player_count; i++ )
    {
        int player_id = game->active_players[i];
        struct PlayerEntity* player = &game->players[player_id];
        if( player->alive && player->scene_element )
        {
            if( game->cycles_elapsed != 0 )
            {
                update_player_anim(game, player_id);
            }

            scenebuilder_push_dynamic_element(
                game->scenebuilder,
                game->scene,
                player->position.x / 128,
                player->position.z / 128,
                0,
                1,
                1,
                player->scene_element);

            struct SceneElement* scene_element = (struct SceneElement*)player->scene_element;
            scene_element->dash_position->yaw = player->orientation.yaw;
            scene_element->dash_position->x = player->position.x;
            scene_element->dash_position->z = player->position.z;

            advance_animation(scene_element->animation, game->cycles_elapsed);
        }
    }

    if( game->players[ACTIVE_PLAYER_SLOT].alive && game->players[ACTIVE_PLAYER_SLOT].scene_element )
    {
        if( game->cycles_elapsed != 0 )
        {
            update_player_anim(game, ACTIVE_PLAYER_SLOT);
        }

        scenebuilder_push_dynamic_element(
            game->scenebuilder,
            game->scene,
            game->players[ACTIVE_PLAYER_SLOT].position.x / 128,
            game->players[ACTIVE_PLAYER_SLOT].position.z / 128,
            0,
            1,
            1,
            game->players[ACTIVE_PLAYER_SLOT].scene_element);

        struct SceneElement* scene_element =
            (struct SceneElement*)game->players[ACTIVE_PLAYER_SLOT].scene_element;
        scene_element->dash_position->yaw = game->players[ACTIVE_PLAYER_SLOT].orientation.yaw;
        scene_element->dash_position->x = game->players[ACTIVE_PLAYER_SLOT].position.x;
        scene_element->dash_position->z = game->players[ACTIVE_PLAYER_SLOT].position.z;

        advance_animation(scene_element->animation, game->cycles_elapsed);
    }

    while( game->cycles_elapsed > 0 )
    {
        game->cycles_elapsed--;
        if( !game->scene )
            continue;

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
    
    // Flush outbound buffer to network
    if( game->outbound_size > 0 && GAME_NET_STATE_GAME == game->net_state )
    {
        printf("Flushing %d bytes to network\n", game->outbound_size);
        ringbuf_write(game->netout, game->outbound_buffer, game->outbound_size);
        game->outbound_size = 0;
    }
}

#endif