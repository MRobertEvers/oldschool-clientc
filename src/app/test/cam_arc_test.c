/*
 * CAM_MOVETO_ARC's path (app/app_cam_arc.h, deob class561): the eye starts where
 * it is, passes the via point, ends on the destination, and stays on one circle;
 * collinear points have no circle and the eye goes straight to the end.
 */
#include "app/app_cam_arc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int g_fail;
static int g_case_fail;

#define CHECK(c, what)                                                                             \
    do                                                                                             \
    {                                                                                              \
        if( !(c) )                                                                                 \
        {                                                                                          \
            if( !g_case_fail++ )                                                                   \
                printf("FAIL: %s (%s:%d)\n", what, __FILE__, __LINE__);                            \
            g_fail++;                                                                              \
        }                                                                                          \
    } while( 0 )

static int
near(int a, int b)
{
    return abs(a - b) <= 1;
}

static void
arc_case(double x0, double z0, double vx, double vz, double x1, double z1)
{
    struct App_CamArc arc;
    int x, z;
    int passed_via = 0;

    g_case_fail = 0;
    App_CamArcFit(&arc, x0, z0, vx, vz, x1, z1);
    App_CamArcAt(&arc, 0.0, &x, &z);
    CHECK(near(x, (int)x0) && near(z, (int)z0), "starts at the eye");
    App_CamArcAt(&arc, 1.0, &x, &z);
    CHECK(near(x, (int)x1) && near(z, (int)z1), "ends on the destination");
    for( int i = 0; i <= 1000; i++ )
    {
        App_CamArcAt(&arc, i / 1000.0, &x, &z);
        CHECK(fabs(hypot(x - arc.cx, z - arc.cz) - arc.radius) <= 1.0, "stays on the circle");
        if( hypot(x - vx, z - vz) <= arc.radius * 0.01 + 2.0 )
            passed_via = 1;
    }
    CHECK(passed_via, "passes the via point");
}

int
main(void)
{
    struct App_CamArc arc;
    int x, z;

    /* Quarter, half and three-quarter turns, both senses, fine coordinates. */
    arc_case(1000, 2000, 2000, 3000, 3000, 2000);
    arc_case(3000, 2000, 2000, 3000, 1000, 2000);
    arc_case(1000, 2000, 2000, 1000, 3000, 2000);
    arc_case(6464, 6464, 7000, 5000, 8000, 6800);
    arc_case(8000, 6800, 7000, 5000, 6464, 6464);
    /* The via point on the long way round: the sweep must not take the short one. */
    arc_case(2000, 100, -2000, -100, 100, 2000);

    /* A chord with no z change: the deob's slope is infinite, its centre NaN,
     * and Java rounds NaN to 0 -- the eye lands on 0, 0. */
    App_CamArcFit(&arc, 1000, 2000, 2000, 2000, 3000, 3000);
    App_CamArcAt(&arc, 0.5, &x, &z);
    CHECK(x == 0 && z == 0, "a level chord rounds to 0, 0, as the deob's Math.round(NaN)");

    /* Collinear: no circle, the path is the destination. */
    App_CamArcFit(&arc, 0, 0, 1000, 1000, 2000, 2000);
    App_CamArcAt(&arc, 0.0, &x, &z);
    CHECK(x == 2000 && z == 2000, "collinear points go straight to the end (deob)");

    if( g_fail )
    {
        printf("cam_arc: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("cam_arc: all tests passed\n");
    return 0;
}
