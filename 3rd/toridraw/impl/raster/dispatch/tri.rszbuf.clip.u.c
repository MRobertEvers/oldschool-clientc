#ifndef TORIDRAW_TRIANGLE_RSZBUF_CLIP_U_C
#define TORIDRAW_TRIANGLE_RSZBUF_CLIP_U_C

/*
 * The rev-241 near clip [class174.method5742], factored the way tri.clip.u.c
 * factors the painter's: the clipped polygon lives in file-scope arrays, the
 * slope, the lerp and the projection of a point on the plane are one helper
 * each, and the per-kind NearClipImpl in tri.rszbuf.u.c gathers the polygon
 * here and draws it.
 *
 * Where it differs from tri.clip.u.c the difference is the client's:
 *
 *  - The reciprocal table stops at 2047 [class169.field2315 = new int[2048]].
 *    A longer edge indexes past it, the client throws, and the model draw is
 *    abandoned -- so the slope reports that rather than widening the table.
 *  - A point made on the plane keeps depth 50.0, and its shade is lerped along
 *    the same edge, whatever kind the face is.
 *  - No winding test on the polygon: the walk already culled the face in
 *    camera space [class174.method5800].
 */

#include "graphics/int_wrap.h"
#include "graphics/raster/rszbuf/rszbuf_model.h"
#include "graphics/raster/rszbuf/rszbuf_raster.h"

#include <assert.h>
#include <stdbool.h>

/** Entries in the client's reciprocal table [class169.field2315]. */
#define RSZBUF_RECIPROCAL_COUNT 2048

/* The clipped polygon: screen position, camera depth and shade of each point
 * [class174.field2400 / field2373 / field2387 / field2360, statics there too].
 * A triangle with one corner behind the plane becomes four points, with two
 * behind it becomes three. */
static int g_toridraw_rszbuf_clip_x[4] = { 0 };
static int g_toridraw_rszbuf_clip_y[4] = { 0 };
static float g_toridraw_rszbuf_clip_depth[4] = { 0 };
static int g_toridraw_rszbuf_clip_shade[4] = { 0 };

/**
 * How far along the edge from a corner behind the plane to one in front the
 * plane lies, in 16.16. False where the edge is longer than the client's
 * reciprocal table: the client throws there.
 */
static inline bool
ToriDraw_RsZbufTriangleSlopei(
    int near_plane_z,
    int z_behind,
    int z_front,
    int* out_slope)
{
    int const span = z_front - z_behind;

    assert(out_slope);
    assert(span > 0);
    if( span >= RSZBUF_RECIPROCAL_COUNT )
        return false;
    *out_slope = toridraw_wrap_mul(near_plane_z - z_behind, 65536 / span);
    return true;
}

static inline int
ToriDraw_RsZbufTriangleLerpPlanei(
    int lerp_slope,
    int p_behind,
    int p_front)
{
    int32_t const product = toridraw_wrap_mul(toridraw_wrap_sub(p_front, p_behind), lerp_slope);

    return toridraw_wrap_add(rszbuf_sar((uint32_t)product, 16), p_behind);
}

/** A point on the plane, projected with the client's integer divide. */
static inline int
ToriDraw_RsZbufTriangleLerpPlaneProjecti(
    int center,
    int projection_scale,
    int near_plane_z,
    int lerp_slope,
    int p_behind,
    int p_front)
{
    int const on_plane = ToriDraw_RsZbufTriangleLerpPlanei(lerp_slope, p_behind, p_front);

    return toridraw_wrap_add(
        center, rszbuf_java_div(toridraw_wrap_mul(on_plane, projection_scale), near_plane_z));
}

/** A corner in front of the plane, as it was projected. */
static inline void
toridraw_rszbuf_clip_keep(
    const struct ToriDraw_RsZbufModel* rz,
    int* clipped_count,
    int vertex,
    int shade)
{
    int const at = (*clipped_count)++;

    g_toridraw_rszbuf_clip_x[at] = rszbuf_java_int(rz->screen_x[vertex]);
    g_toridraw_rszbuf_clip_y[at] = rszbuf_java_int(rz->screen_y[vertex]);
    g_toridraw_rszbuf_clip_depth[at] = rz->depth[vertex];
    g_toridraw_rszbuf_clip_shade[at] = shade;
}

/** Where the edge from a corner behind the plane to one in front crosses it. */
static inline bool
toridraw_rszbuf_clip_cross(
    const struct ToriDraw_RsZbufModel* rz,
    int center_x,
    int center_y,
    int projection_scale,
    int* clipped_count,
    int behind,
    int front,
    int shade_behind,
    int shade_front)
{
    int lerp_slope;
    int at;

    if( !ToriDraw_RsZbufTriangleSlopei(
            RSZBUF_NEAR_PLANE, rz->camera_z[behind], rz->camera_z[front], &lerp_slope) )
        return false;

    at = (*clipped_count)++;
    g_toridraw_rszbuf_clip_x[at] = ToriDraw_RsZbufTriangleLerpPlaneProjecti(
        center_x, projection_scale, RSZBUF_NEAR_PLANE, lerp_slope, rz->camera_x[behind],
        rz->camera_x[front]);
    g_toridraw_rszbuf_clip_y[at] = ToriDraw_RsZbufTriangleLerpPlaneProjecti(
        center_y, projection_scale, RSZBUF_NEAR_PLANE, lerp_slope, rz->camera_y[behind],
        rz->camera_y[front]);
    g_toridraw_rszbuf_clip_depth[at] = (float)RSZBUF_NEAR_PLANE;
    g_toridraw_rszbuf_clip_shade[at] =
        ToriDraw_RsZbufTriangleLerpPlanei(lerp_slope, shade_behind, shade_front);
    return true;
}

/**
 * Build a face's polygon at the near plane into g_toridraw_rszbuf_clip_*.
 *
 * Each corner in front is kept; each corner behind is replaced by its crossings
 * toward the corners in front -- A toward C then B, B toward A then C, C toward
 * B then A, which is the order that fixes the winding. `shade_*` are the
 * client's colour words for the face (a flat face's B is 0 and its C is -1,
 * and they are lerped like any other).
 *
 * Returns the point count: 3 or 4, or 0 when every corner is behind. -1 is the
 * client throwing, part way through, on an edge past its reciprocal table.
 */
static inline int
ToriDraw_RsZbufTriangleClip(
    const struct ToriDraw_RsZbufModel* rz,
    int center_x,
    int center_y,
    int projection_scale,
    int a,
    int b,
    int c,
    int shade_a,
    int shade_b,
    int shade_c)
{
    int clipped_count = 0;
    bool const a_front = rz->camera_z[a] >= RSZBUF_NEAR_PLANE;
    bool const b_front = rz->camera_z[b] >= RSZBUF_NEAR_PLANE;
    bool const c_front = rz->camera_z[c] >= RSZBUF_NEAR_PLANE;

#define RSZBUF_CROSS_OR_THROW(behind, front, shade_behind, shade_front)                            \
    do                                                                                             \
    {                                                                                              \
        if( !toridraw_rszbuf_clip_cross(                                                           \
                rz, center_x, center_y, projection_scale, &clipped_count, (behind), (front),       \
                (shade_behind), (shade_front)) )                                                   \
            return -1;                                                                             \
    } while( 0 )

    if( a_front )
        toridraw_rszbuf_clip_keep(rz, &clipped_count, a, shade_a);
    else
    {
        if( c_front )
            RSZBUF_CROSS_OR_THROW(a, c, shade_a, shade_c);
        if( b_front )
            RSZBUF_CROSS_OR_THROW(a, b, shade_a, shade_b);
    }

    if( b_front )
        toridraw_rszbuf_clip_keep(rz, &clipped_count, b, shade_b);
    else
    {
        if( a_front )
            RSZBUF_CROSS_OR_THROW(b, a, shade_b, shade_a);
        if( c_front )
            RSZBUF_CROSS_OR_THROW(b, c, shade_b, shade_c);
    }

    if( c_front )
        toridraw_rszbuf_clip_keep(rz, &clipped_count, c, shade_c);
    else
    {
        if( b_front )
            RSZBUF_CROSS_OR_THROW(c, b, shade_c, shade_b);
        if( a_front )
            RSZBUF_CROSS_OR_THROW(c, a, shade_c, shade_a);
    }

#undef RSZBUF_CROSS_OR_THROW

    return clipped_count;
}

/**
 * The depth keys of the polygon's first three points, with the face's bias --
 * and its fourth point's RAW camera depth, which is what the client hands the
 * rasterizer for the second half of a quad where every other corner gets a
 * key. That is what ships, so it is what is drawn.
 */
static inline void
ToriDraw_RsZbufTriangleClipKeys(
    int depth_bias,
    float key[4])
{
    RSZBUF_NO_FMA
    for( int i = 0; i < 3; i++ )
        key[i] = rszbuf_depth_key(g_toridraw_rszbuf_clip_depth[i] - (float)depth_bias);
    key[3] = g_toridraw_rszbuf_clip_depth[3];
}

/** Whether any point of the polygon is off the left or right of the viewport. */
static inline bool
ToriDraw_RsZbufTriangleClipSpansX(
    int clipped_count,
    int clip_width)
{
    for( int i = 0; i < clipped_count; i++ )
        if( g_toridraw_rszbuf_clip_x[i] < 0 || g_toridraw_rszbuf_clip_x[i] > clip_width )
            return true;
    return false;
}

#endif
