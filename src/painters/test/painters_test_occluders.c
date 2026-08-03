/**
 * Unit tests for planar occluders: plane geometry, camera dead zone, area
 * thresholds on the greedy merge, and merge extents on a synthetic wall run.
 *
 * Build/run: make -C src test-painters-occluders
 */
#include "engine/world_builder/heightmap.h"
#include "engine/world_builder/occluder_buildmap.h"
#include "painters/scene_occluders.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;

static void
expect(
    int cond,
    const char* msg)
{
    if( !cond )
    {
        fprintf(stderr, "FAIL: %s\n", msg);
        g_failures++;
    }
}

static void
fill_flat_heights(
    struct Heightmap* hm,
    int16_t y)
{
    int n = hm->size_x * hm->size_z * hm->levels;
    int i;
    for( i = 0; i < n; i++ )
        hm->heights[i] = y;
}

static void
test_point_hidden_behind_wall(void)
{
    struct SceneOccluders* occ = scene_occluders_new(32, 32, 4);
    int16_t* heights;
    int gw = 33;
    int gh = 33;
    int n = gw * gh * 4;
    int i;

    heights = calloc((size_t)n, sizeof(int16_t));
    assert(heights);
    for( i = 0; i < n; i++ )
        heights[i] = 0;
    scene_occluders_set_ground_heights(occ, heights, gw, gh, 4);
    free(heights);

    /* Constant-X wall at x=10*128, spanning z=8..18 tiles, y from -240..0. */
    expect(
        scene_occluders_add(
            occ,
            3,
            OCCLUDER_PLANE_CONSTANT_X,
            10 * 128,
            -240,
            8 * 128,
            10 * 128,
            0,
            18 * 128 + 128) == 0,
        "add wall plane");

    /* Camera east of the wall (x > 10*128 + dead zone). */
    scene_occluders_select_for_camera(
        occ, 14 * 128 + 64, -100, 13 * 128 + 64, 3, NULL, NULL, 0, 0);

    expect(occ->active_count == 1, "wall activates when camera is east");
    expect(
        occ->active[0]->hides == OCCLUDER_HIDES_WEST_OF_PLANE,
        "hides west of plane");

    /* Point west of the wall, in its shadow. */
    expect(
        scene_occluders_point_hidden(occ, 5 * 128 + 64, -100, 13 * 128 + 64),
        "point behind wall is hidden");
    /* Point east of the wall (camera side) is visible. */
    expect(
        !scene_occluders_point_hidden(occ, 16 * 128 + 64, -100, 13 * 128 + 64),
        "point in front of wall is visible");

    scene_occluders_free(occ);
}

static void
test_camera_dead_zone(void)
{
    struct SceneOccluders* occ = scene_occluders_new(32, 32, 4);
    int16_t* heights;
    int gw = 33;
    int gh = 33;
    int n = gw * gh * 4;
    int i;

    heights = calloc((size_t)n, sizeof(int16_t));
    assert(heights);
    for( i = 0; i < n; i++ )
        heights[i] = 0;
    scene_occluders_set_ground_heights(occ, heights, gw, gh, 4);
    free(heights);

    expect(
        scene_occluders_add(
            occ,
            3,
            OCCLUDER_PLANE_CONSTANT_X,
            10 * 128,
            -240,
            8 * 128,
            10 * 128,
            0,
            18 * 128 + 128) == 0,
        "add wall plane");

    /* Eye within OCCLUDER_CAMERA_DEAD_ZONE (32) of the plane → rejected. */
    scene_occluders_select_for_camera(
        occ, 10 * 128 + 16, -100, 13 * 128 + 64, 3, NULL, NULL, 0, 0);
    expect(occ->active_count == 0, "dead zone rejects near-plane eye");

    scene_occluders_free(occ);
}

static void
test_merge_wall_area_threshold(void)
{
    struct OccluderBuildmap* map = occluder_buildmap_new(20, 20, 4);
    struct Heightmap* hm = heightmap_new(21, 21, 4);
    struct SceneOccluders* occ = scene_occluders_new(20, 20, 4);
    int z;

    fill_flat_heights(hm, 0);

    /* 7-tile wall run along Z at x=5 — area 7 < 8, must not emit. */
    for( z = 2; z < 9; z++ )
        occluder_buildmap_or_mark(
            map, 5, z, 0, OCCLUDER_MARK_WALL_ALONG_X_ALL_LEVELS);

    occluder_buildmap_build_occluders(map, hm, occ);
    expect(occ->level_occluder_count[0] == 0, "wall area 7 rejected");
    expect(occ->level_occluder_count[3] == 0, "wall area 7 rejected at top");

    /* Extend to 8 tiles. */
    occluder_buildmap_or_mark(map, 5, 9, 0, OCCLUDER_MARK_WALL_ALONG_X_ALL_LEVELS);
    occluder_buildmap_build_occluders(map, hm, occ);
    expect(occ->level_occluder_count[0] >= 1, "wall area 8 accepted at level 0");
    expect(occ->level_occluder_count[3] >= 1, "wall area 8 accepted at top");

    {
        struct SceneOccluder* o = &occ->level_occluders[0][0];
        expect(o->plane == OCCLUDER_PLANE_CONSTANT_X, "merged plane is constant-X");
        expect(o->tile_min_x == 5 && o->tile_max_x == 5, "wall x extent");
        /* max_z = max_tz*128+128 → tile_max_z = max_tz+1 (Client-TS setOcclude). */
        expect(o->tile_min_z == 2 && o->tile_max_z == 10, "wall z run 2..9 → tile_max 10");
        expect(o->min_x == 5 * 128 && o->max_x == 5 * 128, "world x collapsed");
        expect(o->min_z == 2 * 128 && o->max_z == 9 * 128 + 128, "wall world z extent");
    }

    /* Consumed bits cleared — rebuild on same map yields no new planes. */
    {
        int before = occ->level_occluder_count[0];
        occluder_buildmap_build_occluders(map, hm, occ);
        expect(
            occ->level_occluder_count[0] == 0,
            "consumed wall bits cleared after merge");
        (void)before;
    }

    scene_occluders_free(occ);
    heightmap_free(hm);
    occluder_buildmap_free(map);
}

static void
test_merge_floor_area_threshold(void)
{
    struct OccluderBuildmap* map = occluder_buildmap_new(20, 20, 4);
    struct Heightmap* hm = heightmap_new(21, 21, 4);
    struct SceneOccluders* occ = scene_occluders_new(20, 20, 4);
    int x;
    int z;

    fill_flat_heights(hm, -100);

    /* 2x1 floor on level 1 — area 2 < 4. */
    for( x = 4; x < 6; x++ )
        occluder_buildmap_or_mark(map, x, 4, 1, OCCLUDER_MARK_FLOOR_ALL_LEVELS);

    occluder_buildmap_build_occluders(map, hm, occ);
    expect(occ->level_occluder_count[1] == 0, "floor area 2 rejected");

    /* Grow to 2x2 = area 4. */
    for( x = 4; x < 6; x++ )
        for( z = 4; z < 6; z++ )
            occluder_buildmap_or_mark(map, x, z, 1, OCCLUDER_MARK_FLOOR_ALL_LEVELS);

    occluder_buildmap_build_occluders(map, hm, occ);
    expect(occ->level_occluder_count[1] >= 1, "floor area 4 accepted");
    {
        struct SceneOccluder* o = &occ->level_occluders[1][0];
        expect(o->plane == OCCLUDER_PLANE_CONSTANT_Y, "floor is constant-Y");
        expect(o->min_y == o->max_y, "floor y collapsed");
        expect(o->min_y == -100, "floor sits at ground height");
    }

    scene_occluders_free(occ);
    heightmap_free(hm);
    occluder_buildmap_free(map);
}

int
main(void)
{
    test_point_hidden_behind_wall();
    test_camera_dead_zone();
    test_merge_wall_area_threshold();
    test_merge_floor_area_threshold();

    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("OK: painters_test_occluders\n");
    return 0;
}
