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

/* #region agent log */
/** Times ToriDraw_TriangleSlopei saw a depth span the reciprocal table cannot
 *  index. Reported per model by the raster debug line. */
static int g_toridraw_clip_recip_oob = 0;
/* #endregion */

static inline int
ToriDraw_TriangleSlopei(
    int near_plane_z,
    int za,
    int zb)
{
    int dz = za - zb;
    int dnear = za - near_plane_z;

    assert(dz >= 0);

    if( (unsigned int)dz < 4096u )
        return dnear * g_reciprocal16[dz];

    /* #region agent log */
    g_toridraw_clip_recip_oob++;
    /* #endregion */

    /*
     * g_reciprocal16 only covers a 4,096-unit depth span. Ordinary models are
     * far smaller than that, but an imported one like the 2012 QBD spans about
     * 9,600 units, so the table lookup above would read past its end. The
     * clipped vertex lies between za and zb, so dnear <= dz and the quotient is
     * a 16.16 fraction; halving both keeps the shift inside 32 bits at the cost
     * of a bit of precision that the lerp cannot resolve anyway.
     */
    while( dnear > 32767 )
    {
        dnear >>= 1;
        dz >>= 1;
    }
    return dz > 0 ? (dnear << 16) / dz : 0;
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
