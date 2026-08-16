#ifndef TORIDRAW_TRIANGLE_CLIP_U_C
#define TORIDRAW_TRIANGLE_CLIP_U_C

#include "../toridraw_math.h"
#include "graphics/projection.h"
#include "graphics/tori_compat.h"

#include <assert.h>
#include <stdint.h>

static int g_toridraw_triangle_clip_x[10] = { 0 };
static int g_toridraw_triangle_clip_y[10] = { 0 };

static int g_toridraw_triangle_clip_color[10] = { 0 };
static const int g_toridraw_triangle_reciprocol_shift = 16;

/**
 * The projection scratch uses -5000/undivided-y sentinels for vertices behind
 * the near plane, so a face cannot be culled until its clipped polygon exists.
 * Match the reference client's render3ZClip test on the first triangle of that
 * polygon. Use 64-bit products because near-plane projection can put otherwise
 * small model edges far outside the viewport.
 */
static inline bool
ToriDraw_TriangleClipFrontFacing(int clipped_count)
{
    int64_t dx01;
    int64_t dy01;
    int64_t dx21;
    int64_t dy21;

    if( clipped_count < 3 )
        return false;

    dx01 = (int64_t)g_toridraw_triangle_clip_x[0] - g_toridraw_triangle_clip_x[1];
    dy01 = (int64_t)g_toridraw_triangle_clip_y[0] - g_toridraw_triangle_clip_y[1];
    dx21 = (int64_t)g_toridraw_triangle_clip_x[2] - g_toridraw_triangle_clip_x[1];
    dy21 = (int64_t)g_toridraw_triangle_clip_y[2] - g_toridraw_triangle_clip_y[1];
    return dx01 * dy21 - dy01 * dx21 > 0;
}

static inline int
ToriDraw_TriangleSlopei(
    int near_plane_z,
    int za,
    int zb)
{
    assert(za - zb >= 0);
    return (za - near_plane_z) * g_reciprocal16[za - zb];
}

static inline int
ToriDraw_TriangleLerpPlanei(
    int near_plane_z,
    int lerp_slope,
    int pa,
    int pb)
{
    int lerp_p = pa + (((pb - pa) * lerp_slope) >> g_toridraw_triangle_reciprocol_shift);

    return lerp_p;
}

static inline int
ToriDraw_TriangleLerpPlaneProjecti(
    int near_plane_z,
    int lerp_slope,
    int pa,
    int pb)
{
    int lerp_p = ToriDraw_TriangleLerpPlanei(near_plane_z, lerp_slope, pa, pb);

    return SCALE_UNIT(lerp_p) / near_plane_z;
}

static inline float
ToriDraw_TriangleSlopef(
    float near_plane_z,
    float za,
    float zb)
{
    return (za - near_plane_z) / (za - zb);
}

static inline float
ToriDraw_TriangleLerpPlanef(
    float near_plane_z,
    float lerp_slope,
    float pa,
    float pb)
{
    return pa + (((pb - pa) * lerp_slope));
}

static inline float
ToriDraw_TriangleLerpPlaneProjectf(
    float near_plane_z,
    float lerp_slope,
    float pa,
    float pb)
{
    float lerp_p = ToriDraw_TriangleLerpPlanef(near_plane_z, lerp_slope, pa, pb);

    return SCALE_UNIT(lerp_p) / near_plane_z;
}

#endif
