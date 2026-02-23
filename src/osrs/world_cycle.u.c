#ifndef WORLD_CYCLE_U_C
#define WORLD_CYCLE_U_C

#include "osrs/world.h"

// clang-format off
#include "world_painter.u.c"
// clang-format on

struct EntityAnimationInfo
{
    struct EntitySceneElement* scene2_element;
    struct EntityPathing* pathing;
    struct EntityDrawPosition* draw_position;
    struct EntityOrientation* orientation;
    struct EntityAnimation* animation;
    int size_x;
    int size_z;
};

static int
update_entity_movement_and_animation(
    struct World* world,
    struct EntityAnimationInfo* info)
{
    int seqId = info->animation->readyanim;
    int route_length = info->pathing->route_length;
    if( route_length == 0 )
    {
        /* Client.ts entityFace: when idle, still turn toward face_entity */
        // entity_face(game, view, player);
        goto yaw_turn;
    }

    int x = info->draw_position->x;
    int z = info->draw_position->z;
    int dstX = info->pathing->route_x[route_length - 1] * 128 + info->size_x * 64;
    int dstZ = info->pathing->route_z[route_length - 1] * 128 + info->size_z * 64;

    if( dstX - x > 256 || dstX - x < -256 || dstZ - z > 256 || dstZ - z < -256 )
    {
        info->draw_position->x = dstX;
        info->draw_position->z = dstZ;
        return -1;
    }

    /* face_entity takes priority over pathing yaw: set dst_yaw from target first */
    // entity_face(game, view, player);

    /* Only use pathing direction when face_entity did not set dst_yaw */
    if( x < dstX )
    {
        if( z < dstZ )
            info->orientation->dst_yaw = 1280;
        else if( z > dstZ )
            info->orientation->dst_yaw = 1792;
        else
            info->orientation->dst_yaw = 1536;
    }
    else if( x > dstX )
    {
        if( z < dstZ )
            info->orientation->dst_yaw = 768;
        else if( z > dstZ )
            info->orientation->dst_yaw = 256;
        else
            info->orientation->dst_yaw = 512;
    }
    else if( z < dstZ )
        info->orientation->dst_yaw = 1024;
    else
        info->orientation->dst_yaw = 0;

    int deltaYaw = (info->orientation->dst_yaw - info->orientation->yaw) & 0x7ff;
    if( deltaYaw > 1024 )
        deltaYaw -= 2048;

    seqId = info->animation->walkanim_b;
    if( deltaYaw >= -256 && deltaYaw <= 256 )
        seqId = info->animation->walkanim;
    else if( deltaYaw >= 256 && deltaYaw < 768 )
        seqId = info->animation->walkanim_r;
    else if( deltaYaw >= -768 && deltaYaw <= -256 )
        seqId = info->animation->walkanim_l;

    if( seqId == -1 )
        seqId = info->animation->walkanim;

    /* Client.ts routeMove: only reduce speed when turning AND not facing entity AND turnspeed != 0
     */
    int moveSpeed = 4;
    if( info->orientation->yaw != info->orientation->dst_yaw )
        moveSpeed = 2;
    if( route_length > 2 )
        moveSpeed = 6;
    if( route_length > 3 )
        moveSpeed = 8;

    /* When not running, cap speed to walk. */
    if( !info->pathing->route_run[route_length - 1] && moveSpeed > 4 )
        moveSpeed = 4;
    if( info->pathing->route_run[route_length - 1] )
        moveSpeed <<= 0x1;

    if( info->pathing->route_run[route_length - 1] && moveSpeed >= 8 &&
        seqId == info->animation->walkanim && info->animation->runanim != -1 )
        seqId = info->animation->runanim;

    if( x < dstX )
    {
        info->draw_position->x += moveSpeed;
        if( info->draw_position->x > dstX )
            info->draw_position->x = dstX;
    }
    else if( x > dstX )
    {
        info->draw_position->x -= moveSpeed;
        if( info->draw_position->x < dstX )
            info->draw_position->x = dstX;
    }
    if( z < dstZ )
    {
        info->draw_position->z += moveSpeed;
        if( info->draw_position->z > dstZ )
            info->draw_position->z = dstZ;
    }
    else if( z > dstZ )
    {
        info->draw_position->z -= moveSpeed;
        if( info->draw_position->z < dstZ )
            info->draw_position->z = dstZ;
    }

    if( info->draw_position->x == dstX && info->draw_position->z == dstZ )
    {
        info->pathing->route_length--;
        if( info->pathing->route_length < 0 )
            info->pathing->route_length = 0;
    }

yaw_turn:;
    int remainingYaw = (info->orientation->dst_yaw - info->orientation->yaw) & 0x7ff;
    if( remainingYaw != 0 )
    {
        if( remainingYaw < 32 || remainingYaw > 2016 )
            info->orientation->yaw = info->orientation->dst_yaw;
        else if( remainingYaw > 1024 )
            info->orientation->yaw -= 32;
        else
            info->orientation->yaw += 32;
        info->orientation->yaw &= 0x7ff;

        if( seqId == info->animation->readyanim &&
            info->orientation->yaw != info->orientation->dst_yaw )
        {
            if( info->animation->turnanim != -1 )
                seqId = info->animation->turnanim;
            else
                seqId = info->animation->walkanim;
        }
    }

    return seqId;
anim:;
}

static void
world_cycle_step_animation(
    struct EntityAnimationStep* animation_step,
    struct Scene2Frames* frames,
    int cycles_elapsed)
{
    if( !animation_step || !frames || frames->count == 0 )
        return;

    assert(frames->lengths != NULL);
    assert(frames->frames != NULL);

    for( int i = 0; i < cycles_elapsed; i++ )
    {
        if( animation_step->frame >= frames->count )
        {
            animation_step->frame = 0;
            animation_step->cycle = 0;
        }

        animation_step->cycle++;
        if( animation_step->cycle >= frames->lengths[animation_step->frame] )
        {
            animation_step->cycle = 0;
            animation_step->frame++;

            if( animation_step->frame >= frames->count )
            {
                animation_step->frame = 0;
                animation_step->cycle = 0;
            }
        }
    }
}

static void
world_cycle_step_element_animations(
    struct EntityAnimation* animation,
    struct Scene2Element* scene_element,
    int cycles_elapsed)
{
    if( !animation )
        return;

    world_cycle_step_animation(
        &animation->primary_anim, &scene_element->primary_frames, cycles_elapsed);
    world_cycle_step_animation(
        &animation->secondary_anim, &scene_element->secondary_frames, cycles_elapsed);
}

static void
world_cycle_update_player_movement_and_animation(
    struct World* world,
    int player_id)
{
    struct PlayerEntity* player = &world->players[player_id];
    struct EntityAnimationInfo info = {
        .scene2_element = &player->scene_element2,
        .pathing = &player->pathing,
        .draw_position = &player->draw_position,
        .orientation = &player->orientation,
        .animation = &player->animation,
        .size_x = 1,
        .size_z = 1,
    };

    int seqId = update_entity_movement_and_animation(world, &info);
    if( seqId == -1 )
    {
        world_player_entity_set_animation(world, player_id, -1, ANIMATION_TYPE_SECONDARY);
    }
    else if( seqId != info.animation->secondary_anim.anim_id )
    {
        world_player_entity_set_animation(world, player_id, seqId, ANIMATION_TYPE_SECONDARY);
    }
}

static void
world_cycle_advance_player_animation(
    struct World* world,
    int player_id,
    int cycles_elapsed)
{
    struct PlayerEntity* player = &world->players[player_id];
    struct Scene2Element* scene_element =
        scene2_element_at(world->scene2, player->scene_element2.element_id);
    world_cycle_step_element_animations(&player->animation, scene_element, cycles_elapsed);
}

static void
world_cycle_update_players(
    struct World* world,
    int cycles_elapsed)
{
    struct PlayerEntity* player = NULL;
    for( int cycle = 0; cycle < cycles_elapsed; cycle++ )
    {
        player = &world->players[ACTIVE_PLAYER_SLOT];
        if( player->alive && player->scene_element2.element_id != -1 )
        {
            world_cycle_update_player_movement_and_animation(world, ACTIVE_PLAYER_SLOT);
            world_cycle_advance_player_animation(world, ACTIVE_PLAYER_SLOT, 1);
        }

        for( int i = 0; i < world->active_player_count; i++ )
        {
            int player_id = world->active_players[i];
            if( player_id == -1 )
                continue;
            struct PlayerEntity* player = &world->players[player_id];
            if( player->alive && player->scene_element2.element_id != -1 )
            {
                world_cycle_update_player_movement_and_animation(world, player_id);
                world_cycle_advance_player_animation(world, player_id, 1);
            }
        }
    }
}

static void
world_cycle_push_players(struct World* world)
{
    struct Scene2Element* scene_element = NULL;
    struct PlayerEntity* player = &world->players[ACTIVE_PLAYER_SLOT];
    struct PainterPadding padding = { 0 };

    if( player->alive && player->scene_element2.element_id != -1 )
    {
        entity_calculate_painter_padding(world, &player->draw_position, 60, &padding);

        painter_add_normal_scenery(
            world->painter,
            padding.x_sw,
            padding.z_sw,
            0,
            player->scene_element2.element_id,
            padding.x_size,
            padding.z_size);

        scene_element = scene2_element_at(world->scene2, player->scene_element2.element_id);
        scene_element->dash_position->yaw = player->orientation.yaw;
        scene_element->dash_position->x = player->draw_position.x;
        scene_element->dash_position->z = player->draw_position.z;
        scene_element->dash_position->y = heightmap_get_interpolated(
            world->heightmap, player->draw_position.x, player->draw_position.z, 0);
    }
}

static void
world_cycle_update_npc_movement_and_animation(
    struct World* world,
    int npc_id)
{
    struct NPCEntity* npc = &world->npcs[npc_id];
    struct EntityAnimationInfo info = {
        .scene2_element = &npc->scene_element2,
        .pathing = &npc->pathing,
        .draw_position = &npc->draw_position,
        .orientation = &npc->orientation,
        .animation = &npc->animation,
        .size_x = npc->size.x,
        .size_z = npc->size.z,
    };

    int seqId = update_entity_movement_and_animation(world, &info);
    if( seqId == -1 )
    {
        world_npc_entity_set_animation(world, npc_id, -1, ANIMATION_TYPE_SECONDARY);
    }
    else if( seqId != info.animation->secondary_anim.anim_id )
    {
        world_npc_entity_set_animation(world, npc_id, seqId, ANIMATION_TYPE_SECONDARY);
    }
}

static void
world_cycle_advance_npc_animation(
    struct World* world,
    int npc_id,
    int cycles_elapsed)
{
    struct NPCEntity* npc = &world->npcs[npc_id];
    struct Scene2Element* scene_element =
        scene2_element_at(world->scene2, npc->scene_element2.element_id);
    world_cycle_step_element_animations(&npc->animation, scene_element, cycles_elapsed);
}

static void
world_cycle_update_npcs(
    struct World* world,
    int cycles_elapsed)
{
    for( int cycle = 0; cycle < cycles_elapsed; cycle++ )
    {
        for( int i = 0; i < world->active_npc_count; i++ )
        {
            int npc_id = world->active_npcs[i];
            if( npc_id == -1 )
                continue;
            struct NPCEntity* npc = &world->npcs[npc_id];
            if( npc->alive && npc->scene_element2.element_id != -1 )
            {
                world_cycle_update_npc_movement_and_animation(world, npc_id);
                world_cycle_advance_npc_animation(world, npc_id, 1);
            }
        }
    }
}

static void
world_cycle_push_npcs(struct World* world)
{
    struct Scene2Element* scene_element = NULL;
    for( int i = 0; i < world->active_npc_count; i++ )
    {
        int npc_id = world->active_npcs[i];
        if( npc_id == -1 )
            continue;
        struct NPCEntity* npc = &world->npcs[npc_id];
        if( npc->alive && npc->scene_element2.element_id != -1 )
        {
            struct PainterPadding padding = { 0 };
            int padding_size = 60 + (npc->size.x - 1) * 64;
            entity_calculate_painter_padding(world, &npc->draw_position, padding_size, &padding);

            painter_add_normal_scenery(
                world->painter,
                padding.x_sw,
                padding.z_sw,
                0,
                npc->scene_element2.element_id,
                padding.x_size,
                padding.z_size);
        }

        scene_element = scene2_element_at(world->scene2, npc->scene_element2.element_id);
        scene_element->dash_position->yaw = npc->orientation.yaw;
        scene_element->dash_position->x = npc->draw_position.x;
        scene_element->dash_position->z = npc->draw_position.z;
        scene_element->dash_position->y = heightmap_get_interpolated(
            world->heightmap, npc->draw_position.x, npc->draw_position.z, 0);
    }
}

static void
world_cycle_begin(struct World* world)
{
    painter_reset_to_static(world->painter);
}

static void
world_cycle_end(struct World* world)
{}

void
world_cycle(
    struct World* world,
    int cycles_elapsed)
{
    world_cycle_begin(world);

    world_cycle_update_players(world, cycles_elapsed);
    world_cycle_push_players(world);

    world_cycle_update_npcs(world, cycles_elapsed);
    world_cycle_push_npcs(world);

    world_cycle_end(world);
}

#endif