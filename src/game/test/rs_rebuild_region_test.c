/*
 * The rebuild region: the centre zone of the last accepted REBUILD, and the
 * template grid kept for the scene that is shown.
 *
 * A centre that is not forgotten when a build is abandoned skips the next
 * REBUILD naming that zone; a scene base off by a zone puts every projectile,
 * hint arrow and map flag eight tiles away; a template lookup off by one zone
 * gives the CoX helper the wrong room.
 *
 *   make -C src test-rs-rebuild-region
 */
#include "game/rs_rebuild_region.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define CHECK(condition, ...)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if( !(condition) )                                                                         \
        {                                                                                          \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static void
test_the_centre_and_scene_base(void)
{
    struct RS_RebuildRegion region;

    printf("TEST: the centre is -1 until set, and the scene base is six zones off it\n");

    memset(&region, 0, sizeof(region));
    RS_RebuildRegion_ForgetCentre(&region);
    CHECK(RS_RebuildRegion_CentreX(&region) == -1, "centre x %d", RS_RebuildRegion_CentreX(&region));
    CHECK(RS_RebuildRegion_CentreZ(&region) == -1, "centre z %d", RS_RebuildRegion_CentreZ(&region));
    CHECK(!RS_RebuildRegion_IsCentre(&region, 0, 0), "a forgotten centre matched zone 0,0");

    RS_RebuildRegion_SetCentre(&region, 400, 425);
    CHECK(RS_RebuildRegion_IsCentre(&region, 400, 425), "the set centre did not match");
    CHECK(!RS_RebuildRegion_IsCentre(&region, 400, 426), "a different zone matched");
    CHECK(RS_RebuildRegion_SceneBaseX(&region) == (400 - 6) * 8, "base x %d",
        RS_RebuildRegion_SceneBaseX(&region));
    CHECK(RS_RebuildRegion_SceneBaseZ(&region) == (425 - 6) * 8, "base z %d",
        RS_RebuildRegion_SceneBaseZ(&region));
}

static void
test_forgetting_the_centre_keeps_the_grid(void)
{
    struct RS_RebuildRegion region;
    int32_t zones[RS_REBUILD_REGION_ZONES];

    printf("TEST: forgetting the centre leaves the kept grid alone\n");

    memset(&region, 0, sizeof(region));
    memset(zones, 0, sizeof(zones));
    zones[0] = 77;
    RS_RebuildRegion_KeepInstance(&region, zones, 100, 200);
    RS_RebuildRegion_SetCentre(&region, 100, 200);
    RS_RebuildRegion_ForgetCentre(&region);
    CHECK(RS_RebuildRegion_InstanceActive(&region), "the grid went inactive");
    CHECK(RS_RebuildRegion_InstanceTemplate(&region, 0, 94, 194) == 77, "cell 0 read %d",
        (int)RS_RebuildRegion_InstanceTemplate(&region, 0, 94, 194));
}

static void
test_the_template_lookup(void)
{
    struct RS_RebuildRegion region;
    int32_t zones[RS_REBUILD_REGION_ZONES];

    printf("TEST: the template lookup is indexed from six zones south-west of its own centre\n");

    memset(&region, 0, sizeof(region));
    for( int i = 0; i < RS_REBUILD_REGION_ZONES; i++ )
        zones[i] = i + 1;
    RS_RebuildRegion_KeepInstance(&region, zones, 50, 60);
    CHECK(RS_RebuildRegion_InstanceActive(&region), "a kept grid was not active");
    /* level 2, dx 3, dz 5 */
    CHECK(RS_RebuildRegion_InstanceTemplate(&region, 2, 44 + 3, 54 + 5) ==
              2 * 169 + 3 * 13 + 5 + 1,
        "cell read %d", (int)RS_RebuildRegion_InstanceTemplate(&region, 2, 47, 59));
    CHECK(RS_RebuildRegion_InstanceTemplate(&region, 3, 44 + 12, 54 + 12) == RS_REBUILD_REGION_ZONES,
        "last cell read %d", (int)RS_RebuildRegion_InstanceTemplate(&region, 3, 56, 66));
    CHECK(RS_RebuildRegion_InstanceTemplate(&region, 0, 43, 54) == 0, "west of the grid read");
    CHECK(RS_RebuildRegion_InstanceTemplate(&region, 0, 44, 67) == 0, "north of the grid read");
    CHECK(RS_RebuildRegion_InstanceTemplate(&region, -1, 44, 54) == 0, "level -1 read");
    CHECK(RS_RebuildRegion_InstanceTemplate(&region, 4, 44, 54) == 0, "level 4 read");
    CHECK(RS_RebuildRegion_InstanceGrid(&region)[10] == 11, "grid cell 10 is %d",
        (int)RS_RebuildRegion_InstanceGrid(&region)[10]);

    /* A scene that is not an instance: inactive, nothing answers. */
    RS_RebuildRegion_KeepInstance(&region, NULL, 52, 62);
    CHECK(!RS_RebuildRegion_InstanceActive(&region), "a NULL grid stayed active");
    CHECK(RS_RebuildRegion_InstanceTemplate(&region, 2, 47, 59) == 0, "an inactive grid answered");
}

static void
test_a_forced_rebuild_is_taken_once(void)
{
    struct RS_RebuildRegion region;

    printf("TEST: a force is taken by the next REBUILD alone, and survives forgetting the centre\n");

    memset(&region, 0, sizeof(region));
    CHECK(!RS_RebuildRegion_TakeForced(&region), "a zeroed region was forced");
    RS_RebuildRegion_Force(&region);
    RS_RebuildRegion_Force(&region);
    RS_RebuildRegion_ForgetCentre(&region);
    RS_RebuildRegion_SetCentre(&region, 50, 50);
    CHECK(RS_RebuildRegion_TakeForced(&region), "the force was lost");
    CHECK(!RS_RebuildRegion_TakeForced(&region), "two forces were two REBUILDs");
    CHECK(RS_RebuildRegion_IsCentre(&region, 50, 50), "taking the force moved the centre");
}

int
main(void)
{
    test_the_centre_and_scene_base();
    test_forgetting_the_centre_keeps_the_grid();
    test_the_template_lookup();
    test_a_forced_rebuild_is_taken_once();

    if( g_failures )
    {
        printf("rs_rebuild_region_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("rs_rebuild_region_test: OK\n");
    return 0;
}
