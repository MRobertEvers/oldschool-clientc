/* The editor free camera's scene clamp (app/app_camera_clamp.h). */
#include "app/app_camera_clamp.h"

#include <stdio.h>

static int g_checks;
static int g_failed;

#define CHECK(cond, what)                                                                          \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if( !(cond) )                                                                              \
        {                                                                                          \
            g_failed++;                                                                            \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, what);                                  \
        }                                                                                          \
    } while( 0 )

int
main(void)
{
    int const extent = 104 * 128;
    int const margin = 4 * 128;
    int x, y, z;

    /* Inside: untouched. */
    x = 5000;
    y = -2000;
    z = 7000;
    CHECK(!app_camera_clamp_scene(&x, &y, &z, extent, margin, 0, 64, -extent), "inside the scene nothing moves");
    CHECK(x == 5000 && y == -2000 && z == 7000, "and the position is as it was");

    /* Past the east edge: held at the margin. */
    x = extent + 10000;
    CHECK(app_camera_clamp_scene(&x, &y, &z, extent, margin, 0, 64, -extent), "past the east edge is clamped");
    CHECK(x == extent + margin, "to the edge plus the margin");

    /* Before the south edge: held at minus the margin. */
    z = -9999;
    app_camera_clamp_scene(&x, &y, &z, extent, margin, 0, 64, -extent);
    CHECK(z == -margin, "before the south edge is held at minus the margin");

    /* Below the ground: lifted to above_min over it (y grows downward). */
    y = 500;
    app_camera_clamp_scene(&x, &y, &z, extent, margin, 240, 64, -extent);
    CHECK(y == 240 - 64, "below the ground is lifted to just above it");

    /* Too high: held at the ceiling. */
    y = -extent * 3;
    app_camera_clamp_scene(&x, &y, &z, extent, margin, 240, 64, -extent);
    CHECK(y == -extent, "too high is held at the ceiling");

    /* A raised ground (a hill, y more negative) lifts the floor with it. */
    y = -100;
    app_camera_clamp_scene(&x, &y, &z, extent, margin, -800, 64, -extent);
    CHECK(y == -864, "the floor follows a hill under the camera");

    printf("cam_clamp_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
