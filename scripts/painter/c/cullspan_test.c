/* Brute-force superset check for painters_cullspan_build.
 *
 * For a sweep of pitch × yaw × zoom × viewport, every tile the discrete
 * 4-corner × Y-sweep frustum test marks visible must also fall inside the
 * analytic span. Over-inclusion is reported but not fatal.
 *
 * Build: make cullspan_test && ./cullspan_test
 */
#include "painters.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Compile the span builder into this TU. */
#include "painters_cullspan.u.c"

static int
sn(int a)
{
    return (int)(sin(a * 2.0 * M_PI / 2048.0) * 65536.0);
}

static int
cs(int a)
{
    return (int)(cos(a * 2.0 * M_PI / 2048.0) * 65536.0);
}

static int
brute_visible(
    int dx,
    int dz,
    int pitch,
    int yaw,
    int ph,
    int near_z,
    int far_z,
    int w,
    int h,
    int y0,
    int y1)
{
    int sP = sn(pitch);
    int cP = cs(pitch);
    int sY = sn(yaw);
    int cY = cs(yaw);
    int corners[4][2] = { { 0, 0 }, { 128, 0 }, { 128, 128 }, { 0, 128 } };
    int ci;
    int fy;

    for( ci = 0; ci < 4; ci++ )
    {
        int X = dx * 128 + corners[ci][0];
        int Z = dz * 128 + corners[ci][1];
        for( fy = y0; fy <= y1; fy += 128 )
        {
            int Yw = ph + fy;
            int xs = (X * cY + Z * sY) >> 16;
            int zs = (Z * cY - X * sY) >> 16;
            int ysc = (Yw * cP - zs * sP) >> 16;
            int zf = (Yw * sP + zs * cP) >> 16;
            int px;
            int py;
            if( zf < near_z || zf > far_z )
                continue;
            px = (xs << 9) / zf + (w >> 1);
            py = (ysc << 9) / zf + (h >> 1);
            if( px >= 0 && px < w && py >= 0 && py < h )
                return 1;
        }
    }
    return 0;
}

int
main(void)
{
    const int rad = 25;
    const int near_z = 50;
    const int far_z = 100000;
    const int w = 1024;
    const int h = 700;
    const int y0 = PCULL_FRUSTUM_Y_START;
    const int y1 = PCULL_FRUSTUM_Y_END;
    const int zooms[] = { 40, 100, 200, 300 };
    const int n_zooms = (int)(sizeof(zooms) / sizeof(zooms[0]));

    long long tot = 0;
    long long vis_b = 0;
    long long vis_a = 0;
    long long missed = 0;
    int pitch;
    int yaw;
    int zi;

    printf(
        "cullspan_test: rad=%d viewport=%dx%d near=%d far=%d y=[%d,%d]\n",
        rad,
        w,
        h,
        near_z,
        far_z,
        y0,
        y1);

    for( pitch = 128; pitch < 512; pitch += 29 )
    {
        for( yaw = 0; yaw < 2048; yaw += 53 )
        {
            for( zi = 0; zi < n_zooms; zi++ )
            {
                int zoom = zooms[zi];
                int dist = (pitch * 3 + 600) * zoom / 100;
                int ph = (int)(dist * sin(pitch * 2.0 * M_PI / 2048.0));
                struct PaintersCullSpanParams params;
                struct PaintersCullSpan span;
                int dz;
                int dx;

                memset(&params, 0, sizeof(params));
                params.pitch = pitch;
                params.yaw = yaw;
                params.eye_height = ph;
                params.y_lo = y0;
                params.y_hi = y1;
                params.near_clip = near_z;
                params.far_clip = far_z;
                params.screen_width = w;
                params.screen_height = h;
                params.fov_rpi2048 = 512;
                params.dz_min = -rad;
                params.dz_max = rad;
                painters_cullspan_build(&span, &params);

                for( dz = -rad; dz <= rad; dz++ )
                {
                    for( dx = -rad; dx <= rad; dx++ )
                    {
                        int bt = brute_visible(
                            dx, dz, pitch, yaw, ph, near_z, far_z, w, h, y0, y1);
                        int an = painters_cullspan_visible(&span, dx, dz);
                        tot++;
                        vis_b += bt;
                        vis_a += an;
                        if( bt && !an )
                            missed++;
                    }
                }
            }
        }
    }

    printf(
        "cells=%lld brute_visible=%lld analytic_visible=%lld MISSED=%lld "
        "over_ratio=%.3f\n",
        tot,
        vis_b,
        vis_a,
        missed,
        vis_b > 0 ? (double)vis_a / (double)vis_b : 0.0);

    if( missed != 0 )
    {
        fprintf(stderr, "FAIL: analytic span missed %lld brute-visible cells\n", missed);
        return 1;
    }
    printf("PASS\n");
    return 0;
}
