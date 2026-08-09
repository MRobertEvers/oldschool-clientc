#include "test_harness.h"

int g_failures;

int
main(void)
{
    test_lifecycle_coords();
    test_entity_pool();
    test_pathing_helpers();
    test_registry();
    test_pickset();
    test_terrain();
    test_player_npc();
    test_projectile();
    test_projectile_target();
    test_spotanim();
    test_scenery();
    test_cycle_movers();
    test_delaymove_gate();
    test_entity_face();
    test_try_route();
    test_try_route_nearest_models();
    test_try_route_op();
    test_try_route_op_forceapproach();
    test_force_approach_rotation();
    test_try_route_op_rect();
    test_try_route_op_exit_strategy();
    test_features_eras();
    test_bfs_path_source_end_truncation();
    test_collision_loc_change_inverse();
    test_route_coordinate_coincidence();
    test_tile_stack_dedup();
    test_minusedlevel_entity_draw();
    test_rebuild_shift();
    test_obj_raise();
    test_line_of_sight();
    test_line_of_sight_asymmetry();
    test_naive_path_safespot();
    test_occupancy_stacking();
    test_route_window();
    test_collision_types();
    test_follow_dance_semantics();
    test_wall_edge_symmetry();

    sim_path_followers();
    sim_projectile_barrage();
    sim_spotanim_wave();
    sim_mixed_churn();
    sim_scene_reset_midflight();

    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }

    printf("All tests passed.\n");
    return 0;
}
