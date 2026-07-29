#include "entity_pathing.h"
#include "test_harness.h"

#include <math.h>
#include <string.h>

static int
pool_active_matches_iterate(const struct World_EntityPool* pool)
{
    return World_TestPoolIterateCount(pool) == pool->active_count;
}

static int
finite_projectile(const struct WorldEntity_Projectile* p)
{
    return isfinite(p->x) && isfinite(p->y) && isfinite(p->z) && isfinite(p->vx) &&
           isfinite(p->vy) && isfinite(p->vz) && isfinite(p->ay);
}

void
sim_path_followers(void)
{
    printf("SIM: path followers (32 players + 64 npcs, up to 2000 ticks)\n");

    struct World* world = World_TestMakeReady(104);
    struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();

    int players[32];
    int npcs[64];

    for( int i = 0; i < 32; i++ )
    {
        int x = 20 + (i % 8);
        int z = 20 + (i / 8);
        players[i] = World_PlayerSpawn(world, 1000 + i, 0, x, z, idle);
        World_PlayerPathPushStep(world, players[i], WORLD_PATHSTEP_WALK, 4);
        World_PlayerPathPushStep(world, players[i], WORLD_PATHSTEP_RUN, 4);
        World_PlayerPathPushStep(world, players[i], WORLD_PATHSTEP_WALK, 1);
    }

    for( int i = 0; i < 64; i++ )
    {
        int x = 40 + (i % 8);
        int z = 40 + (i / 8);
        npcs[i] = World_NpcSpawn(world, 2000 + i, i, 0, x, z, 1, idle);
        World_NpcPathPushStep(world, npcs[i], WORLD_PATHSTEP_RUN, 6);
        World_NpcPathPushStep(world, npcs[i], WORLD_PATHSTEP_WALK, 3);
    }

    int idle_all = 0;
    for( int t = 0; t < 2000; t++ )
    {
        World_Cycle(world, 1);
        if( (t % 100) == 0 )
        {
            TEST_ASSERT(pool_active_matches_iterate(&world->entities.player), "player iterate");
            TEST_ASSERT(pool_active_matches_iterate(&world->entities.npc), "npc iterate");
        }

        idle_all = 1;
        for( int i = 0; i < 32; i++ )
        {
            struct WorldEntity_Player* p =
                World_EntityPoolGet(&world->entities.player, players[i]);
            if( p->pathing.route_length != 0 )
                idle_all = 0;
        }
        for( int i = 0; i < 64; i++ )
        {
            struct WorldEntity_NPC* n = World_EntityPoolGet(&world->entities.npc, npcs[i]);
            if( n->pathing.route_length != 0 )
                idle_all = 0;
        }
        if( idle_all )
            break;
    }

    TEST_ASSERT(idle_all, "all routes idle within 2000 ticks");

    for( int i = 0; i < 32; i++ )
    {
        struct WorldEntity_Player* p = World_EntityPoolGet(&world->entities.player, players[i]);
        int tile_x = p->pathing.route_x[0];
        int tile_z = p->pathing.route_z[0];
        int dx = (int)p->draw_position.x - (tile_x * 128 + 64);
        int dz = (int)p->draw_position.z - (tile_z * 128 + 64);
        if( dx < 0 )
            dx = -dx;
        if( dz < 0 )
            dz = -dz;
        TEST_ASSERT(dx <= 128 && dz <= 128, "player within 1 tile of auth");
    }

    for( int i = 0; i < 64; i++ )
    {
        struct WorldEntity_NPC* n = World_EntityPoolGet(&world->entities.npc, npcs[i]);
        int tile_x = n->pathing.route_x[0];
        int tile_z = n->pathing.route_z[0];
        int dx = (int)n->draw_position.x - (tile_x * 128 + 64);
        int dz = (int)n->draw_position.z - (tile_z * 128 + 64);
        if( dx < 0 )
            dx = -dx;
        if( dz < 0 )
            dz = -dz;
        TEST_ASSERT(dx <= 128 && dz <= 128, "npc within 1 tile of auth");
    }

    World_Free(world);
}

void
sim_projectile_barrage(void)
{
    printf("SIM: projectile barrage (200 projectiles)\n");

    struct World* world = World_TestMakeReady(104);
    int removed = 0;

    for( int i = 0; i < 200; i++ )
    {
        int t1 = i % 5;
        int t2 = t1 + 5 + (i % 10);
        int src = 128 + (i % 40) * 8;
        int dst = src + 64 + (i % 20) * 4;
        int idx = World_ProjectileSpawn(
            world, 3000 + i, 0, src, src, dst, dst, 150, 20, t1, t2, 5 + (i % 15), 0,
            WORLD_PROJECTILE_TARGET_NONE);
        TEST_ASSERT(idx >= 0, "proj spawn");
    }

    TEST_ASSERT(world->entities.projectile.active_count == 200, "200 active");

    for( int t = 0; t < 500 && world->entities.projectile.active_count > 0; t += 5 )
    {
        World_Cycle(world, 5);
        removed += World_TestDrainRemovedEvents(world);

        for( int i = World_EntityPoolHead(&world->entities.projectile); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(&world->entities.projectile, i) )
        {
            struct WorldEntity_Projectile* p =
                World_EntityPoolGet(&world->entities.projectile, i);
            TEST_ASSERT(finite_projectile(p), "finite projectile");
        }
    }

    TEST_ASSERT(world->entities.projectile.active_count == 0, "all projectiles gone");
    removed += World_TestDrainRemovedEvents(world);
    TEST_ASSERT(removed == 200, "200 removal events");

    World_Free(world);
}

void
sim_spotanim_wave(void)
{
    printf("SIM: spotanim wave (100)\n");

    struct World* world = World_TestMakeReady(104);
    int removed = 0;

    for( int i = 0; i < 100; i++ )
    {
        int idle = i % 10;
        int life = 5 + (i % 15);
        int x = (10 + i % 50) * 128;
        int z = (10 + i / 50) * 128;
        World_SpotanimSpawn(world, 4000 + i, 0, x, z, 0, 0, idle, life);
    }

    TEST_ASSERT(world->entities.spotanim.active_count == 100, "100 spotanims");

    for( int t = 0; t < 1000 && world->entities.spotanim.active_count > 0; t++ )
    {
        World_Cycle(world, 1);
        if( (t % 20) == 0 )
            removed += World_TestDrainRemovedEvents(world);
    }

    removed += World_TestDrainRemovedEvents(world);
    TEST_ASSERT(world->entities.spotanim.active_count == 0, "all spotanims gone");
    TEST_ASSERT(removed == 100, "100 spotanim removal events");

    World_Free(world);
}

void
sim_mixed_churn(void)
{
    printf("SIM: mixed churn (5000 ticks)\n");

    struct World* world = World_TestMakeReady(104);
    struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();
    int scene_size = world->_scene_size;

    int long_lived[8];
    for( int i = 0; i < 8; i++ )
    {
        long_lived[i] = World_PlayerSpawn(world, 5000 + i, 0, 30 + i, 30, idle);
        World_PlayerPathPushStep(world, long_lived[i], WORLD_PATHSTEP_WALK, 4);
    }

    int churn_player = -1;
    int churn_npc = -1;
    int element = 6000;

    for( int t = 0; t < 5000; t++ )
    {
        if( (t % 10) == 0 )
        {
            if( churn_player >= 0 && World_EntityPoolIsActive(&world->entities.player, churn_player) )
                World_PlayerDespawn(world, churn_player);
            churn_player = World_PlayerSpawn(world, element++, 0, 15, 15 + (t % 20), idle);

            if( churn_npc >= 0 && World_EntityPoolIsActive(&world->entities.npc, churn_npc) )
                World_NpcDespawn(world, churn_npc);
            churn_npc = World_NpcSpawn(world, element++, 1, 0, 16, 16 + (t % 20), 1, idle);
        }

        if( (t % 20) == 0 )
        {
            World_ProjectileSpawn(
                world, element++, 0, 200, 200, 400, 400, 100, 10, 0, 8, 8, 0,
                WORLD_PROJECTILE_TARGET_NONE);
        }

        if( (t % 50) == 0 )
        {
            for( int i = 0; i < 8; i++ )
            {
                if( !World_EntityPoolIsActive(&world->entities.player, long_lived[i]) )
                    continue;
                struct WorldEntity_Player* p =
                    World_EntityPoolGet(&world->entities.player, long_lived[i]);
                if( p->pathing.route_length == 0 )
                    World_PlayerPathPushStep(world, long_lived[i], WORLD_PATHSTEP_WALK, t % 8);
            }
        }

        World_Cycle(world, 1);

        if( (t % 25) == 0 )
            World_EventsClear(world);

        if( (t % 100) == 0 )
        {
            TEST_ASSERT(world->_scene_size == scene_size, "scene size stable");
            TEST_ASSERT(pool_active_matches_iterate(&world->entities.player), "player pool");
            TEST_ASSERT(pool_active_matches_iterate(&world->entities.npc), "npc pool");
            TEST_ASSERT(pool_active_matches_iterate(&world->entities.projectile), "proj pool");
            for( int i = World_EntityPoolHead(&world->entities.projectile); i != WORLD_ENTITY_NIL;
                 i = World_EntityPoolNext(&world->entities.projectile, i) )
            {
                TEST_ASSERT(
                    finite_projectile(World_EntityPoolGet(&world->entities.projectile, i)),
                    "finite in churn");
            }
        }
    }

    World_Free(world);
}

void
sim_scene_reset_midflight(void)
{
    printf("SIM: scene reset midflight\n");

    struct World* world = World_TestMakeReady(104);
    struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();

    int pi = World_PlayerSpawn(world, 1, 0, 10, 10, idle);
    int ni = World_NpcSpawn(world, 2, 99, 0, 12, 12, 1, idle);
    World_TerrainSet(world, 111, 5, 5, 0);
    World_ProjectileSpawn(
        world, 3, 0, 128, 128, 256, 256, 100, 0, 0, 50, 0, 0, WORLD_PROJECTILE_TARGET_NONE);
    World_SpotanimSpawn(world, 4, 0, 20 * 128, 20 * 128, 0, 0, 0, 100);
    World_SceneryRegister(world, 5, 50, 1, 1, 0, 1, 1, 0, 0, 0, "Rock", NULL, 1);
    World_RegisterSceneryPick(world, 5, 50);

    World_PlayerPathPushStep(world, pi, WORLD_PATHSTEP_WALK, 4);
    for( int t = 0; t < 100; t++ )
        World_Cycle(world, 1);

    /* Clear dynamics before reset (ResetScene only clears terrain/picks/events). */
    while( World_EntityPoolHead(&world->entities.player) != WORLD_ENTITY_NIL )
        World_PlayerDespawn(world, World_EntityPoolHead(&world->entities.player));
    while( World_EntityPoolHead(&world->entities.npc) != WORLD_ENTITY_NIL )
        World_NpcDespawn(world, World_EntityPoolHead(&world->entities.npc));
    while( World_EntityPoolHead(&world->entities.projectile) != WORLD_ENTITY_NIL )
        World_ProjectileDespawn(world, World_EntityPoolHead(&world->entities.projectile));
    while( World_EntityPoolHead(&world->entities.spotanim) != WORLD_ENTITY_NIL )
        World_SpotanimDespawn(world, World_EntityPoolHead(&world->entities.spotanim));
    while( World_EntityPoolHead(&world->entities.scenery) != WORLD_ENTITY_NIL )
        World_EntityPoolRelease(
            &world->entities.scenery, World_EntityPoolHead(&world->entities.scenery));
    World_EventsClear(world);

    World_ResetScene(world, 45, 45, 88);
    TEST_ASSERT(!world->load_complete, "load_complete false after reset");
    TEST_ASSERT(world->_scene_size == 88, "new scene size");
    TEST_ASSERT(world->scenery_pick_count == 0, "picks cleared");
    TEST_ASSERT(World_TerrainElementAt(world, 5, 5, 0) == -1, "terrain cleared");
    TEST_ASSERT(world->entities.player.active_count == 0, "players cleared by us");

    World_SetLoadComplete(world, true);
    pi = World_PlayerSpawn(world, 10, 0, 8, 8, idle);
    ni = World_NpcSpawn(world, 11, 1, 0, 9, 9, 1, idle);
    World_TerrainSet(world, 222, 2, 2, 1);
    World_PlayerPathPushStep(world, pi, WORLD_PATHSTEP_RUN, 1);

    for( int t = 0; t < 100; t++ )
        World_Cycle(world, 1);

    TEST_ASSERT(World_TerrainElementAt(world, 2, 2, 1) == 222, "terrain after reseed");
    TEST_ASSERT(World_EntityPoolIsActive(&world->entities.player, pi), "player alive");
    TEST_ASSERT(World_EntityPoolIsActive(&world->entities.npc, ni), "npc alive");
    TEST_ASSERT(pool_active_matches_iterate(&world->entities.player), "player iterate");

    World_Free(world);
}
