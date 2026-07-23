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
    test_spotanim();
    test_scenery();
    test_cycle_movers();
    test_entity_face();
    test_try_route();
    test_try_route_op();
    test_route_coordinate_coincidence();
    test_rebuild_shift();

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
