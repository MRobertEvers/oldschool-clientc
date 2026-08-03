#ifndef WORLD_TEST_HARNESS_H
#define WORLD_TEST_HARNESS_H

#include "world.h"

#include <stdio.h>

extern int g_failures;

#define TEST_ASSERT(cond, msg)                                                                     \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                        \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static inline struct WorldEntityFacet_IdleAnimations
World_TestDefaultIdle(void)
{
    return (struct WorldEntityFacet_IdleAnimations){
        .readyanim = 100,
        .walkanim = 200,
        .turnanim = 300,
        .runanim = 400,
        .walkanim_b = 201,
        .walkanim_r = 202,
        .walkanim_l = 203,
    };
}

static inline struct World*
World_TestMakeReady(int scene_size)
{
    struct World* world = World_New();
    World_ResetScene(world, 50, 50, scene_size);
    World_SetLoadComplete(world, true);
    return world;
}

static inline int
World_TestPoolIterateCount(const struct World_EntityPool* pool)
{
    int n = 0;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
        n++;
    return n;
}

static inline int
World_TestDrainRemovedEvents(struct World* world)
{
    int n = World_EventsCount(world);
    for( int i = 0; i < n; i++ )
    {
        const struct World_Event* ev = World_EventsPeek(world, i);
        if( !ev || ev->kind != WorldEventKind_EntityRemoved )
            g_failures++;
    }
    World_EventsClear(world);
    return n;
}

/* Unit tests */
void test_lifecycle_coords(void);
void test_entity_pool(void);
void test_pathing_helpers(void);
void test_registry(void);
void test_pickset(void);
void test_terrain(void);
void test_player_npc(void);
void test_projectile(void);
void test_projectile_target(void);
void test_spotanim(void);
void test_scenery(void);
void test_cycle_movers(void);
void test_entity_face(void);
void test_try_route(void);
void test_try_route_op(void);
void test_try_route_op_forceapproach(void);
void test_force_approach_rotation(void);
void test_try_route_op_rect(void);
void test_features_eras(void);
void test_bfs_path_source_end_truncation(void);
void test_collision_loc_change_inverse(void);
void test_route_coordinate_coincidence(void);
void test_tile_stack_dedup(void);
void test_minusedlevel_entity_draw(void);
void test_rebuild_shift(void);
void test_line_of_sight(void);
void test_line_of_sight_asymmetry(void);
void test_naive_path_safespot(void);
void test_occupancy_stacking(void);
void test_route_window(void);
void test_collision_types(void);
void test_follow_dance_semantics(void);

/* Simulations */
void sim_path_followers(void);
void sim_projectile_barrage(void);
void sim_spotanim_wave(void);
void sim_mixed_churn(void);
void sim_scene_reset_midflight(void);

#endif
