#ifndef WORLD_CYCLE_U_C
#define WORLD_CYCLE_U_C

#include "osrs/world.h"

#include "osrs/entity_scenebuild.h"

#include <math.h>

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

/**
 * Bump base_level by 1 when the column at (draw_x, draw_z) is a LinkBelow bridge so the entity
 * lands on the bridge surface — both painter element slevel (for painter_grid_slot_for_cache_level
 * downstream) and heightmap lookup (mirrors Client-TS getAvH groundh[level+1]). Falls back to
 * base_level when the painter is not yet built or the tile is out of scene bounds.
 */
static int
world_cycle_entity_cache_level(
    struct World* world,
    int draw_x,
    int draw_z,
    int base_level)
{
    int sx = draw_x / 128;
    int sz = draw_z / 128;
    if( !world->painter || sx < 0 || sz < 0 || sx >= world->_scene_size ||
        sz >= world->_scene_size )
        return base_level;
    return entity_cache_level_for_column(
        base_level, painter_column_has_link_below(world->painter, sx, sz));
}

/** Client.ts tileLastOccupiedCycle: centered 1x1 entities skip addDynamic when tile claimed this
 * scene_cycle. Returns true when the caller should skip painter registration. */
static bool
world_cycle_tile_center_skip_duplicate(
    struct World* world,
    int wx,
    int wz,
    bool is_npc,
    int npc_size_x)
{
    if( (wx & 0x7f) != 64 || (wz & 0x7f) != 64 )
        return false;
    if( is_npc && npc_size_x != 1 )
        return false;

    int stx = wx >> 7;
    int stz = wz >> 7;
    int ss = world->_scene_size;
    if( !world->tile_last_occupied_scene_cycle || stx < 0 || stz < 0 || stx >= ss ||
        stz >= ss )
        return false;

    int idx = stz * ss + stx;
    if( world->tile_last_occupied_scene_cycle[idx] == world->scene_cycle )
        return true;
    world->tile_last_occupied_scene_cycle[idx] = world->scene_cycle;
    return false;
}

/** Client.ts entityFace (after routeMove): sets dst_yaw from face_entity / face_square. */
static void
world_cycle_entity_face(struct World* world, struct EntityAnimationInfo* info)
{
    struct EntityOrientation* o = info->orientation;
    if( o->turnspeed == 0 )
        return;

    int x = info->draw_position->x;
    int z = info->draw_position->z;
    int fe = o->face_entity;

    if( fe >= 0 && fe < 32768 )
    {
        if( fe < MAX_NPCS )
        {
            struct NPCEntity* t = world_npc(world, fe);
            if( t->alive )
            {
                int dx = x - t->draw_position.x;
                int dz = z - t->draw_position.z;
                if( dx != 0 || dz != 0 )
                {
                    double ang = atan2((double)dx, (double)dz);
                    o->dst_yaw = (uint16_t)((int)(ang * 325.949) & 0x7ff);
                }
            }
        }
    }
    else if( fe >= 32768 )
    {
        int k = fe - 32768;
        int pid = -1;
        if( world->local_player_server_slot >= 0 && k == world->local_player_server_slot )
            pid = ACTIVE_PLAYER_SLOT;
        else if( k >= 0 && k < world->active_player_count )
            pid = world->active_players[k];
        else if( k >= 0 && k < MAX_PLAYERS )
            pid = k;

        if( pid >= 0 )
        {
            struct PlayerEntity* t = world_player(world, pid);
            if( t->alive )
            {
                int dx = x - t->draw_position.x;
                int dz = z - t->draw_position.z;
                if( dx != 0 || dz != 0 )
                {
                    double ang = atan2((double)dx, (double)dz);
                    o->dst_yaw = (uint16_t)((int)(ang * 325.949) & 0x7ff);
                }
            }
        }
    }

    if( (o->face_square_x != 0 || o->face_square_z != 0) &&
        info->pathing->route_length == 0 )
    {
        int bx = world->_base_tile_x;
        int bz = world->_base_tile_z;
        int dx = x - (int)(o->face_square_x - bx - bx) * 64;
        int dz = z - (int)(o->face_square_z - bz - bz) * 64;
        if( dx != 0 || dz != 0 )
        {
            double ang = atan2((double)dx, (double)dz);
            o->dst_yaw = (uint16_t)((int)(ang * 325.949) & 0x7ff);
        }
        o->face_square_x = 0;
        o->face_square_z = 0;
    }
}

static int
update_entity_movement_and_animation(
    struct World* world,
    struct EntityAnimationInfo* info)
{
    int seqId = info->animation->readyanim;
    int route_length = info->pathing->route_length;
    if( route_length == 0 )
        goto before_yaw;

    int x = info->draw_position->x;
    int z = info->draw_position->z;
    int dstX = info->pathing->route_x[route_length - 1] * 128 + info->size_x * 64;
    int dstZ = info->pathing->route_z[route_length - 1] * 128 + info->size_z * 64;

    if( dstX - x > 256 || dstX - x < -256 || dstZ - z > 256 || dstZ - z < -256 )
    {
        info->draw_position->x = dstX;
        info->draw_position->z = dstZ;
        world_cycle_entity_face(world, info);
        return -1;
    }

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

before_yaw:;
    world_cycle_entity_face(world, info);

yaw_turn:;
    int ts = (int)info->orientation->turnspeed;
    int remainingYaw = (info->orientation->dst_yaw - info->orientation->yaw) & 0x7ff;
    if( ts != 0 && remainingYaw != 0 )
    {
        if( remainingYaw < ts || remainingYaw > 2048 - ts )
            info->orientation->yaw = info->orientation->dst_yaw;
        else
        {
            int y = (int)info->orientation->yaw;
            if( remainingYaw > 1024 )
                y -= ts;
            else
                y += ts;
            info->orientation->yaw = (uint16_t)(y & 0x7ff);
        }

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

/** Returns true when a non-wrapping sequence finished (caller should clear primary track). */
static bool
world_cycle_step_animation(
    struct EntityAnimationStep* animation_step,
    struct Scene2Frames* frames,
    int cycles_elapsed,
    bool wrap_at_end)
{
    if( !animation_step || !frames || frames->count == 0 )
        return false;

    assert(frames->lengths != NULL);
    assert(frames->frames != NULL);

    for( int i = 0; i < cycles_elapsed; i++ )
    {
        if( animation_step->frame >= frames->count )
        {
            if( !wrap_at_end )
                return true;
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
                if( !wrap_at_end )
                    return true;
                animation_step->frame = 0;
                animation_step->cycle = 0;
            }
        }
    }
    return false;
}

/** Primary/secondary sequence id; 0 = memset/unset, (uint16_t)-1 = cleared via set_animation(-1).
 */
static bool
world_cycle_entity_anim_track_enabled(uint16_t anim_id)
{
    return anim_id != 0 && anim_id != (uint16_t)-1;
}

static bool
world_cycle_step_element_animations(
    struct EntityAnimation* animation,
    struct Scene2Element* scene_element,
    int cycles_elapsed,
    bool primary_wrap)
{
    if( !animation )
        return false;

    struct Scene2Frames* primary = scene2_element_primary_frames(scene_element);
    struct Scene2Frames* secondary = scene2_element_secondary_frames(scene_element);
    bool primary_done = false;
    /* Step only tracks the entity actually uses. Scene2 can still hold stale frame lists for a
     * cleared slot; stepping those would advance timers twice per 50Hz tick (notably projectiles
     * that only drive primary). */
    if( primary && world_cycle_entity_anim_track_enabled(animation->primary_anim.anim_id) )
        primary_done = world_cycle_step_animation(
            &animation->primary_anim, primary, cycles_elapsed, primary_wrap);
    if( secondary && world_cycle_entity_anim_track_enabled(animation->secondary_anim.anim_id) )
        (void)world_cycle_step_animation(
            &animation->secondary_anim, secondary, cycles_elapsed, true);
    return primary_done;
}

static void
world_cycle_advance_map_build_loc_entity_animation(
    struct World* world,
    int entity_id,
    int cycles_elapsed)
{
    if( entity_id < 0 || entity_id >= entity_vec_count(&world->map_build_loc_entities) )
        return;
    struct MapBuildLocEntity* entity = world_loc_entity(world, entity_id);
    struct Scene2Element* scene_element = NULL;
    if( entity->scene_element.element_id != -1 )
    {
        scene_element = scene2_element_at(world->scene2, entity->scene_element.element_id);
        scene2_element_expect(scene_element, "world_cycle map_build_loc primary");
        world_cycle_step_element_animations(
            &entity->animation, scene_element, cycles_elapsed, true);
    }

    if( entity->scene_element_two.element_id != -1 )
    {
        scene_element = scene2_element_at(world->scene2, entity->scene_element_two.element_id);
        scene2_element_expect(scene_element, "world_cycle map_build_loc secondary");
        world_cycle_step_element_animations(
            &entity->animation_two, scene_element, cycles_elapsed, true);
    }
}

static void
world_cycle_update_map_build_loc_entities(
    struct World* world,
    int cycles_elapsed)
{
    for( int i = 0; i < world->active_loc_entity_count; i++ )
    {
        int entity_id = world->active_loc_entities[i];
        if( entity_id == -1 )
            continue;
        world_cycle_advance_map_build_loc_entity_animation(world, entity_id, cycles_elapsed);
    }
}

static void
world_cycle_update_player_movement_and_animation(
    struct World* world,
    int player_id)
{
    struct PlayerEntity* player = world_player(world, player_id);
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
        world_player_entity_set_animation(world, player_id, -1, ANIMATION_TYPE_SECONDARY, 0);
    }
    else if( seqId != info.animation->secondary_anim.anim_id )
    {
        world_player_entity_set_animation(world, player_id, seqId, ANIMATION_TYPE_SECONDARY, 0);
    }
}

static void
world_cycle_advance_player_animation(
    struct World* world,
    int player_id,
    int cycles_elapsed)
{
    struct PlayerEntity* player = world_player(world, player_id);
    struct Scene2Element* scene_element =
        scene2_element_at(world->scene2, player->scene_element2.element_id);
    scene2_element_expect(scene_element, "world_cycle_advance_player_animation");

    player->needs_forward_draw_padding = false;

    if( world_cycle_entity_anim_track_enabled(player->animation.primary_anim.anim_id) &&
        player->animation.primary_anim.delay > 0 )
    {
        for( int i = 0; i < cycles_elapsed; i++ )
        {
            if( player->animation.primary_anim.delay == 0 )
                break;
            player->animation.primary_anim.delay--;
            if( player->animation.primary_anim.delay == 0 )
                world_player_entity_refresh_scene2_appearance_mesh(world, player_id);
        }
        return;
    }

    if( world_cycle_step_element_animations(
            &player->animation, scene_element, cycles_elapsed, false) )
        world_player_entity_set_animation(world, player_id, -1, ANIMATION_TYPE_PRIMARY, 0);

    if( world->gamecache &&
        world_cycle_entity_anim_track_enabled(player->animation.primary_anim.anim_id) &&
        player->animation.primary_anim.delay == 0 )
    {
        struct GameCacheSequence* seq = gamecache_get_sequence(
            world->gamecache, (int)player->animation.primary_anim.anim_id);
        if( seq )
            player->needs_forward_draw_padding = seq->stretches;
    }
}

static void
world_cycle_update_players(
    struct World* world,
    int cycles_elapsed)
{
    struct PlayerEntity* player = NULL;
    for( int cycle = 0; cycle < cycles_elapsed; cycle++ )
    {
        player = world_player(world, ACTIVE_PLAYER_SLOT);
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
            struct PlayerEntity* player = world_player(world, player_id);
            if( player->alive && player->scene_element2.element_id != -1 )
            {
                world_cycle_update_player_movement_and_animation(world, player_id);
                world_cycle_advance_player_animation(world, player_id, 1);
            }
        }
    }
}

static void
world_cycle_minimap_one_player(struct World* world, struct PlayerEntity* p)
{
    if( !world->minimap )
        return;
    int wx = p->draw_position.x;
    int wz = p->draw_position.z;
    int sx = wx / 128;
    int sz = wz / 128;
    if( sx >= 0 && sx < world->minimap->width && sz >= 0 && sz < world->minimap->height )
        minimap_add_loc(world->minimap, sx, sz, wx, wz, MINIMAP_LOC_TYPE_PLAYER);
}

/** Client.ts addPlayers: register one player with painter + DashPosition when not skipped. */
static void
world_cycle_push_one_player_painter(
    struct World* world,
    int player_id,
    bool minimap_dot)
{
    struct PlayerEntity* player = world_player(world, player_id);
    struct Scene2Element* scene_element = NULL;
    struct PainterPadding padding = { 0 };

    if( !player->alive || player->scene_element2.element_id == -1 )
        return;

    int wx = player->draw_position.x;
    int wz = player->draw_position.z;
    if( !world_cycle_tile_center_skip_duplicate(world, wx, wz, false, 0) )
    {
        entity_calculate_painter_padding(
            world,
            &player->draw_position,
            60,
            player->orientation.yaw,
            player->needs_forward_draw_padding,
            &padding);

        int slevel = world_cycle_entity_cache_level(world, wx, wz, 0);

        painter_add_normal_scenery(
            world->painter,
            padding.x_sw,
            padding.z_sw,
            slevel,
            player->scene_element2.element_id,
            padding.x_size,
            padding.z_size);

        scene_element = scene2_element_at(world->scene2, player->scene_element2.element_id);
        scene2_element_expect(scene_element, "world_cycle_push_one_player_painter");
        struct DashPosition* ppos = scene2_element_dash_position(scene_element);
        ppos->yaw = player->orientation.yaw;
        ppos->x = wx;
        ppos->z = wz;
        ppos->y = heightmap_get_interpolated(world->heightmap, wx, wz, slevel);
    }

    if( minimap_dot )
        world_cycle_minimap_one_player(world, player);
}

/** Client.ts addNpcs(alwaysontop): one NPC pass with tile-center dedup for size-1. */
static void
world_cycle_push_one_npc_painter(
    struct World* world,
    int npc_id,
    bool want_alwaysontop_pass)
{
    struct NPCEntity* npc = world_npc(world, npc_id);
    struct Scene2Element* scene_element = NULL;
    struct PainterPadding padding = { 0 };

    if( !npc->alive || npc->scene_element2.element_id == -1 )
        return;

    struct GameCacheNpc* cfg =
        world->gamecache ? gamecache_get_npc(world->gamecache, npc->config_npc_id) : NULL;
    bool alwaysontop = cfg && cfg->alwaysontop;
    if( want_alwaysontop_pass != alwaysontop )
        return;

    int wx = npc->draw_position.x;
    int wz = npc->draw_position.z;
    if( world_cycle_tile_center_skip_duplicate(world, wx, wz, true, (int)npc->size.x) )
        return;

    int padding_size = 60 + (npc->size.x - 1) * 64;
    entity_calculate_painter_padding(
        world,
        &npc->draw_position,
        padding_size,
        npc->orientation.yaw,
        npc->needs_forward_draw_padding,
        &padding);

    int slevel = world_cycle_entity_cache_level(world, wx, wz, 0);

    painter_add_normal_scenery(
        world->painter,
        padding.x_sw,
        padding.z_sw,
        slevel,
        npc->scene_element2.element_id,
        padding.x_size,
        padding.z_size);

    scene_element = scene2_element_at(world->scene2, npc->scene_element2.element_id);
    scene2_element_expect(scene_element, "world_cycle_push_one_npc_painter");
    struct DashPosition* npos = scene2_element_dash_position(scene_element);
    npos->yaw = npc->orientation.yaw;
    npos->x = wx;
    npos->z = wz;
    npos->y = heightmap_get_interpolated(world->heightmap, wx, wz, slevel);

    if( world->minimap && !npc->minimap_hidden )
    {
        int sx = wx / 128;
        int sz = wz / 128;
        if( sx >= 0 && sx < world->minimap->width && sz >= 0 && sz < world->minimap->height )
            minimap_add_loc(world->minimap, sx, sz, wx, wz, MINIMAP_LOC_TYPE_NPC);
    }
}

/** Client.ts gameDrawMain order: sceneCycle++, local players, alwaysontop NPCs, remote players,
 * other NPCs. */
static void
world_cycle_push_dynamic_scenery(struct World* world)
{
    world->scene_cycle++;

    /* 1) Local player */
    world_cycle_push_one_player_painter(world, ACTIVE_PLAYER_SLOT, false);

    /* 2) alwaysontop NPCs */
    for( int i = 0; i < world->active_npc_count; i++ )
    {
        int npc_id = world->active_npcs[i];
        if( npc_id == -1 )
            continue;
        world_cycle_push_one_npc_painter(world, npc_id, true);
    }

    /* 3) Remote players */
    for( int i = 0; i < world->active_player_count; i++ )
    {
        int player_id = world->active_players[i];
        if( player_id < 0 || player_id == ACTIVE_PLAYER_SLOT )
            continue;
        world_cycle_push_one_player_painter(world, player_id, true);
    }

    /* 4) Remaining NPCs */
    for( int i = 0; i < world->active_npc_count; i++ )
    {
        int npc_id = world->active_npcs[i];
        if( npc_id == -1 )
            continue;
        world_cycle_push_one_npc_painter(world, npc_id, false);
    }
}

static void
world_cycle_update_npc_movement_and_animation(
    struct World* world,
    int npc_id)
{
    struct NPCEntity* npc = world_npc(world, npc_id);
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
    struct NPCEntity* npc = world_npc(world, npc_id);
    struct Scene2Element* scene_element =
        scene2_element_at(world->scene2, npc->scene_element2.element_id);
    scene2_element_expect(scene_element, "world_cycle_advance_npc_animation");

    npc->needs_forward_draw_padding = false;

    if( world_cycle_step_element_animations(
            &npc->animation, scene_element, cycles_elapsed, false) )
        world_npc_entity_set_animation(world, npc_id, -1, ANIMATION_TYPE_PRIMARY);

    if( world->gamecache &&
        world_cycle_entity_anim_track_enabled(npc->animation.primary_anim.anim_id) &&
        npc->animation.primary_anim.delay == 0 )
    {
        struct GameCacheSequence* seq = gamecache_get_sequence(
            world->gamecache, (int)npc->animation.primary_anim.anim_id);
        if( seq )
            npc->needs_forward_draw_padding = seq->stretches;
    }
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
            struct NPCEntity* npc = world_npc(world, npc_id);
            if( npc->alive && npc->scene_element2.element_id != -1 )
            {
                world_cycle_update_npc_movement_and_animation(world, npc_id);
                world_cycle_advance_npc_animation(world, npc_id, 1);
            }
        }
    }
}

static void
world_cycle_active_projectile_remove_at(
    struct World* world,
    int index)
{
    int projectile_id = world->active_projectiles[index];
    world_cleanup_projectile_entity(world, projectile_id);
    world->active_projectiles[index] =
        world->active_projectiles[world->active_projectile_count - 1];
    world->active_projectile_count--;
}

static void
world_cycle_update_projectiles(
    struct World* world,
    int cycles_elapsed)
{
    if( cycles_elapsed <= 0 )
        return;

    for( int i = world->active_projectile_count - 1; i >= 0; i-- )
    {
        int projectile_id = world->active_projectiles[i];
        struct ProjectileEntity* p = world_projectile(world, projectile_id);
        if( !p->alive || p->scene_element2.element_id == -1 )
            continue;
        struct Scene2Element* scene_element =
            scene2_element_at(world->scene2, p->scene_element2.element_id);
        if( !scene_element )
            continue;

        p->draw_position.x += cycles_elapsed;
        if( (p->draw_position.x / 128) > 103 )
        {
            world_cycle_active_projectile_remove_at(world, i);
            continue;
        }

        world_cycle_step_element_animations(
            &p->animation, scene_element, cycles_elapsed, true);
    }
}

static void
world_cycle_push_projectiles(struct World* world)
{
    for( int i = 0; i < world->active_projectile_count; i++ )
    {
        int projectile_id = world->active_projectiles[i];
        struct ProjectileEntity* p = world_projectile(world, projectile_id);
        if( !p->alive || p->scene_element2.element_id == -1 )
            continue;

        struct Scene2Element* scene_element =
            scene2_element_at(world->scene2, p->scene_element2.element_id);
        if( !scene_element || !scene2_element_is_active(scene_element) )
            continue;

        struct DashPosition* position = scene2_element_dash_position(scene_element);
        if( !position )
            continue;

        int lev = world_cycle_entity_cache_level(
            world, p->draw_position.x, p->draw_position.z, (int)p->visible_level.level);
        painter_add_normal_scenery(
            world->painter,
            p->draw_position.x / 128,
            p->draw_position.z / 128,
            lev,
            p->scene_element2.element_id,
            1,
            1);
        position->yaw = p->orientation.yaw;
        position->x = p->draw_position.x;
        position->z = p->draw_position.z;
        position->y = heightmap_get_interpolated(
            world->heightmap, p->draw_position.x, p->draw_position.z, lev);
    }
}

static void
world_cycle_push_obj_stack_entities(struct World* world)
{
    if( !world->painter )
        return;

    for( int i = 0; i < world->active_obj_stack_entity_count; i++ )
    {
        int eid = (int)world->active_obj_stack_entities[i];
        struct ObjStackEntity* e = world_obj_stack_entity(world, eid);
        if( !e->alive )
            continue;

        if( world->gamecache )
            entity_scenebuild_obj_stack_reload_scene2_for_active_entity(
                world, world->gamecache, eid);

        /* scene-local tile indices (same space as zone OBJ sx/sz), not world->_base_tile_* */
        int psx = e->world_tile_x;
        int psz = e->world_tile_z;
        if( psx < 0 || psz < 0 || psx >= world->_scene_size || psz >= world->_scene_size )
            continue;

        bool link = painter_column_has_link_below(world->painter, psx, psz);
        int slevel = entity_cache_level_for_column(e->level, link);

        bool minimap_done = false;

        for( int layer = 0; layer < 3; layer++ )
        {
            int sid = e->scene_element[layer].element_id;
            int total_el = scene2_elements_total(world->scene2);
            if( sid < 0 || sid >= total_el )
                continue;

            struct Scene2Element* se = scene2_element_at(world->scene2, sid);
            if( !se || !scene2_element_is_active(se) )
                continue;

            painter_add_ground_object(
                world->painter, psx, psz, slevel, sid, layer);

            if( world->heightmap )
            {
                struct DashPosition* pos = scene2_element_dash_position(se);
                if( pos )
                {
                    int wx = e->world_tile_x * 128 + 64;
                    int wz = e->world_tile_z * 128 + 64;
                    pos->x = wx;
                    pos->z = wz;
                    pos->y = heightmap_get_interpolated(
                        world->heightmap, wx, wz, slevel);
                }
            }

            if( world->minimap && !minimap_done )
            {
                int wx = e->world_tile_x * 128 + 64;
                int wz = e->world_tile_z * 128 + 64;
                int mtx = wx / 128;
                int mtz = wz / 128;
                if( mtx >= 0 && mtx < world->minimap->width && mtz >= 0 &&
                    mtz < world->minimap->height )
                    minimap_add_loc(world->minimap, mtx, mtz, wx, wz, MINIMAP_LOC_TYPE_OBJECT);
                minimap_done = true;
            }
        }
    }
}

static void
world_cycle_begin(struct World* world)
{
    assert(world && world->painter != NULL);
    painter_reset_to_static(world->painter);
    if( world->minimap )
        minimap_clear_locs(world->minimap);
}

static void
world_cycle_end(struct World* world)
{}

void
world_cycle(
    struct World* world,
    int cycles_elapsed)
{
    if( !world || !world->load_complete )
        return;

    world_cycle_begin(world);

    world_cycle_update_map_build_loc_entities(world, cycles_elapsed);

    world_cycle_update_players(world, cycles_elapsed);
    world_cycle_update_npcs(world, cycles_elapsed);
    world_cycle_update_projectiles(world, cycles_elapsed);

    world_cycle_push_dynamic_scenery(world);
    world_cycle_push_projectiles(world);
    world_cycle_push_obj_stack_entities(world);

    world_cycle_end(world);
}

#endif