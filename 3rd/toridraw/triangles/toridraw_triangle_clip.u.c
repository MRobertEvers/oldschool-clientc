#ifndef TORIDRAW_TRIANGLE_CLIP_U_C
#define TORIDRAW_TRIANGLE_CLIP_U_C

#include "../toridraw_math.h"
#include "impl/projection/projection.scalar_reference.h"
#include "graphics/winding.h"
#include "graphics/tori_compat.h"
#include "../toridraw_debug.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

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
/** TORIDRAW_IGNORE_PRIORITIES=1: drop face render priorities and sort every
 *  face purely by average depth.
 *
 *  Face priorities are a painter's-algorithm crutch: the artist pins a face to
 *  a draw band because there is no depth buffer to resolve it. A model authored
 *  for a z-buffered client (the rs2012 QBD) can carry priorities that were
 *  never used to order anything, and honouring them here overrides the depth
 *  sort with values that mean nothing - which looks like a model sorting
 *  inside-out. See docs/qbd_toridraw_streaks_debug.md. */
static int
toridraw_ignore_priorities(void)
{
    static int on = -1;
    if( on < 0 )
    {
        const char* v = getenv("TORIDRAW_IGNORE_PRIORITIES");
        on = (v && v[0] && v[0] != '0') ? 1 : 0;
    }
    return on;
}

static inline bool
ToriDraw_TriangleClipFrontFacing(int clipped_count)
{
    if( clipped_count < 3 )
        return false;

    return toridraw_winding_2d_front_facing(
        g_toridraw_triangle_clip_x[0],
        g_toridraw_triangle_clip_y[0],
        g_toridraw_triangle_clip_x[1],
        g_toridraw_triangle_clip_y[1],
        g_toridraw_triangle_clip_x[2],
        g_toridraw_triangle_clip_y[2]);
}

/** Times ToriDraw_TriangleSlopei saw a depth span the reciprocal table cannot
 *  index. Reported per model by the raster debug line. */
TORIDRAW_DBG_COUNTER(g_toridraw_clip_recip_oob);

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

    TORIDRAW_DBG_COUNT(g_toridraw_clip_recip_oob);

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
    return dz > 0 ? ((dnear << 16) / dz) : 0;
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

/**
 * Projects a vertex the clipper just created onto the near plane.
 *
 * Must be the same arithmetic the projection kernels apply to a vertex they
 * keep - `x * (camera_cot16 >> 1) >> 6`, then `/ z` (projection16_simd.u.c).
 * `SCALE_UNIT(p) / z` is that formula frozen at scale 512, which is what this
 * used to do: correct only for a camera projecting at exactly 512. The live
 * world camera derives its scale from the viewport height (app.c
 * app_world_apply_proj_scale), so every clipped vertex landed 512/scale times
 * too far from the screen centre while the unclipped vertices it shares edges
 * with landed correctly - a face stretched into a streak, and only on models
 * near enough to clip. 9d4b97a9 parameterised every other projection site and
 * missed this one.
 */
static inline int
ToriDraw_TriangleLerpPlaneProjecti(
    int camera_cot16,
    int near_plane_z,
    int lerp_slope,
    int pa,
    int pb)
{
    int lerp_p = ToriDraw_TriangleLerpPlanei(near_plane_z, lerp_slope, pa, pb);
    long long const scaled = (((long long)lerp_p * (camera_cot16 >> 1)) >> 6);

    /* The 64-bit *product* is load-bearing: `lerp_p * (camera_cot16 >> 1)`
     * overflows `int` at ordinary camera scales, which is why this is a
     * `long long` at all. The *divide* is not. After the `>> 6` the value is
     * back inside `int` for every projection the client actually issues, and on
     * i686 a 32-bit `idiv` is one instruction where a 64-bit divide is a call to
     * `__divdi3`.
     *
     * This one line was 110 of the 114 `__divdi3` call sites in
     * `toridraw_unity.o` -- every near-clipped vertex of every clipped face
     * paid it, on the lane where `render` is 8.2 ms of a 10.0 ms frame.
     *
     * The wide divide is kept as the fallback rather than asserted away: a
     * large enough `near_plane_z` can leave `scaled` outside `int` while the
     * quotient still fits, and that is a legitimate camera, not a caller bug. */
    if( scaled >= INT32_MIN && scaled <= INT32_MAX )
        return (int)scaled / near_plane_z;
    return (int)(scaled / near_plane_z);
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

/** Float twin of ToriDraw_TriangleLerpPlaneProjecti; same scale rule.
 *  `camera_cot16 >> 1` is scale << 6, so dividing by 64 recovers the scale
 *  including its fractional part, which `>> TORIDRAW_PROJECTION_COT16_SHIFT` would
 *  throw away. */
static inline float
ToriDraw_TriangleLerpPlaneProjectf(
    int camera_cot16,
    float near_plane_z,
    float lerp_slope,
    float pa,
    float pb)
{
    float lerp_p = ToriDraw_TriangleLerpPlanef(near_plane_z, lerp_slope, pa, pb);

    return (lerp_p * ((float)(camera_cot16 >> 1) / 64.0f)) / near_plane_z;
}

#endif
