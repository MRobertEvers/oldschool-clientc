#ifndef TORIDRAW_TRIANGLE_CLIP_U_C
#define TORIDRAW_TRIANGLE_CLIP_U_C

#include "../toridraw_math.h"
#include "graphics/projection.h"
#include "graphics/tori_compat.h"

#include <assert.h>

static int g_toridraw_triangle_clip_x[10] = { 0 };
static int g_toridraw_triangle_clip_y[10] = { 0 };

static int g_toridraw_triangle_clip_color[10] = { 0 };
static const int g_toridraw_triangle_reciprocol_shift = 16;

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
