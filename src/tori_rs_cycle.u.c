#ifndef TORI_RS_CYCLE_U_C
#define TORI_RS_CYCLE_U_C

#include "osrs/dash_utils.h"
#include "osrs/gameproto_process.h"
#include "osrs/scenebuilder.h"
#include "tori_rs.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static struct SceneAnimation*
load_model_animations_dati(
    int sequence_id,
    struct BuildCacheDat* buildcachedat)
{
    struct SceneAnimation* scene_animation = NULL;
    struct CacheDatSequence* sequence = NULL;
    struct CacheAnimframe* animframe = NULL;

    struct DashFrame* dash_frame = NULL;
    struct DashFramemap* dash_framemap = NULL;

    if( sequence_id == -1 || !buildcachedat->sequences_hmap )
        return NULL;

    scene_animation = malloc(sizeof(struct SceneAnimation));
    memset(scene_animation, 0, sizeof(struct SceneAnimation));

    sequence = buildcachedat_get_sequence(buildcachedat, sequence_id);
    assert(sequence);

    scene_animation->frame_lengths = malloc(sequence->frame_count * sizeof(int));
    memset(scene_animation->frame_lengths, 0, sequence->frame_count * sizeof(int));

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
        scene_animation->frame_lengths[i] = length;

        scene_animation_push_frame(
            scene_animation, dashframe_new_from_animframe(animframe), dash_framemap);
    }

    return scene_animation;
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
        scene_element->animation = load_model_animations_dati(seqId, game->buildcachedat);
    }
    else if( seqId == -1 )
    {
        npc_entity->curranim = -1;
        scene_element->animation = NULL;
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
        scene_element->animation = load_model_animations_dati(seqId, game->buildcachedat);
    }
    else if( seqId == -1 )
    {
        player_entity->curranim = -1;
        scene_element->animation = NULL;
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

static void
on_completed_task(
    struct GGame* game,
    struct GInput* input,
    struct ToriRSRenderCommandBuffer* render_command_buffer,
    struct GameTask* task)
{
    switch( task->kind )
    {
    case GAMETASK_KIND_INIT_IO:
        break;
    case GAMETASK_KIND_INIT_SCENE:
        // game->model = gtask_init_scene_value(task->_init_scene);
        break;
    }
}

static void
LibToriRS_GameStepTasks(
    struct GGame* game,
    struct GInput* input,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    struct GameTask* task = game->tasks_nullable;
    enum GameTaskStatus status = GAMETASK_STATUS_FAILED;
    while( task )
    {
        status = gametask_step(task);
        if( status != GAMETASK_STATUS_COMPLETED )
            break;

        on_completed_task(game, input, render_command_buffer, task);

        game->tasks_nullable = game->tasks_nullable->next;
        gametask_free(task);
        task = game->tasks_nullable;
    }
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

    LibToriRS_GameStepTasks(game, input, render_command_buffer);
    task = game->tasks_nullable;
    if( task && task->status != GAMETASK_STATUS_COMPLETED )
    {
        return;
    }

    LibToriRS_GameProcessInput(game, input);

    dash_animate_textures(game->sys_dash, game->cycles_elapsed);
    if( game->cycle >= game->next_notimeout_cycle && GAME_NET_STATE_GAME == game->net_state )
    {
        game->next_notimeout_cycle = game->cycle + 50;
        int opcode = 206;
        uint32_t op = (opcode + isaac_next(game->random_out)) & 0xff;
        game->outbound_buffer[game->outbound_size++] = op;
    }

    scenebuilder_reset_dynamic_elements(game->scenebuilder, game->scene);

    for( int i = 0; i < game->npc_count; i++ )
    {
        int npc_id = game->active_npcs[i];
        if( npc_id == -1 )
            continue;
        struct NPCEntity* npc = &game->npcs[game->active_npcs[i]];
        if( npc->alive && npc->scene_element )
        {
            scenebuilder_push_dynamic_element(
                game->scenebuilder,
                game->scene,
                npc->position.x / 128,
                npc->position.z / 128,
                0,
                npc->size_x,
                npc->size_z,
                npc->scene_element);

            if( game->cycles_elapsed != 0 )
            {
                update_npc_anim(game, game->active_npcs[i]);
            }
            struct SceneElement* scene_element = (struct SceneElement*)npc->scene_element;
            scene_element->dash_position->yaw = npc->orientation.yaw;
            scene_element->dash_position->x = npc->position.x;
            scene_element->dash_position->z = npc->position.z;
            scene_element->isnpc = true;

            advance_animation(scene_element->animation, game->cycles_elapsed);
        }
    }

    for( int i = 0; i < game->player_count; i++ )
    {
        int player_id = game->active_players[i];
        struct PlayerEntity* player = &game->players[player_id];
        if( player->alive && player->scene_element )
        {
            scenebuilder_push_dynamic_element(
                game->scenebuilder,
                game->scene,
                player->position.x / 128,
                player->position.z / 128,
                0,
                1,
                1,
                player->scene_element);

            if( game->cycles_elapsed != 0 )
            {
                update_player_anim(game, player_id);
            }
            struct SceneElement* scene_element = (struct SceneElement*)player->scene_element;
            scene_element->dash_position->yaw = player->orientation.yaw;
            scene_element->dash_position->x = player->position.x;
            scene_element->dash_position->z = player->position.z;
            scene_element->isnpc = false;

            advance_animation(scene_element->animation, game->cycles_elapsed);
        }
    }

    if( game->players[ACTIVE_PLAYER_SLOT].alive && game->players[ACTIVE_PLAYER_SLOT].scene_element )
    {
        scenebuilder_push_dynamic_element(
            game->scenebuilder,
            game->scene,
            game->players[ACTIVE_PLAYER_SLOT].position.x / 128,
            game->players[ACTIVE_PLAYER_SLOT].position.z / 128,
            0,
            1,
            1,
            game->players[ACTIVE_PLAYER_SLOT].scene_element);

        if( game->cycles_elapsed != 0 )
        {
            update_player_anim(game, ACTIVE_PLAYER_SLOT);
        }
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

    return;
}

#endif