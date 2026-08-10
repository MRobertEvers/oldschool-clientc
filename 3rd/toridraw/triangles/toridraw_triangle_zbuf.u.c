#include "graphics/tori_compat.h"
#include "toridraw_triangle_clip.u.c"

#ifndef TORIDRAW_TRIANGLE_ZBUF_U_C
#define TORIDRAW_TRIANGLE_ZBUF_U_C

/*
 * Face-level entry to the depth-tested kernels.
 *
 * The stock families each carry a pair of these — one for the common case and
 * one that rebuilds a face crossing the near plane. Here there is a single pair
 * for all three shading modes, because everything the modes disagree about
 * (colour, uv, texel rules) is already described by struct ToriDraw_ZbufTriangle
 * and everything they agree about (which corners survive the near plane, and
 * where the new ones land) is the same code three times over in the stock files.
 */

#include "../toridraw_types.h"
#include "graphics/dash_restrict.h"
#include "graphics/raster/zbuffer/zbuf.screen.u.c"
#include "graphics/zdepth.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>

/**
 * Where the depth-tested kernels get their depth.
 *
 * Camera-space z, not screen_vertices_z: that array has the model's mid-z
 * subtracted out, which is harmless for a sort that only compares within one
 * model but is not a distance to the eye, and the perspective key is a
 * reciprocal of a distance to the eye.
 */
static inline float
toridraw_zbuf_vertex_key(
    const int* RESTRICT orthographic_vertices_z,
    int vertex,
    bool parallel)
{
    return toridraw_zdepth_key(orthographic_vertices_z[vertex], parallel);
}

/** Destination and camera state shared by every face of one model. */
struct ToriDraw_ZbufTarget
{
    toripixel_t* pixel_buffer;
    torizdepth_t* zbuffer;
    int stride;
    int screen_width;
    int screen_height;
    int camera_cot16;
    int offset_x;
    int offset_y;
    int near_plane_z;
    /** Parallel projection: depth is linear, so the key is a negated z rather
     *  than a reciprocal. Only changes how the key is derived. */
    bool parallel;
};

/** The model arrays one face is read out of. */
struct ToriDraw_ZbufFaceSource
{
    faceint_t* face_indices_a;
    faceint_t* face_indices_b;
    faceint_t* face_indices_c;
    int* screen_vertices_x;
    int* screen_vertices_y;
    int* orthographic_vertices_x;
    int* orthographic_vertices_y;
    int* orthographic_vertices_z;
};

/**
 * Rebuild a face that crosses the near plane, in the order the stock builders
 * use: for each corner, either the corner itself or the crossing point on each
 * of its two edges. Three surviving corners make a triangle, four a quad, and
 * the caller draws the quad as (0,1,2) plus (0,2,3).
 *
 * Every point the clipper creates sits ON the near plane, so its camera z is
 * near_plane_z and its depth key is the same constant for all of them. That is
 * the only depth arithmetic the clip needs — no lerp, because the plane is where
 * the new vertices are by construction.
 *
 * Returns the number of points written (0 when the face has nothing left).
 */
static int
toridraw_zbuf_build_near_clipped(
    const struct ToriDraw_ZbufTarget* target,
    const struct ToriDraw_ZbufFaceSource* src,
    int face,
    int shade_a,
    int shade_b,
    int shade_c,
    int out_x[4],
    int out_y[4],
    int out_shade[4],
    float out_key[4])
{
    int const a = src->face_indices_a[face];
    int const b = src->face_indices_b[face];
    int const c = src->face_indices_c[face];

    int const za = src->orthographic_vertices_z[a];
    int const zb = src->orthographic_vertices_z[b];
    int const zc = src->orthographic_vertices_z[c];

    float const plane_key = toridraw_zdepth_key(target->near_plane_z, target->parallel);
    int const near_plane_z = target->near_plane_z;
    int const cot16 = target->camera_cot16;

    /* Indexed the same way in all three loops below, so the corner walk can be
     * written once instead of unrolled three times. */
    int const vertex[3] = { a, b, c };
    int const depth[3] = { za, zb, zc };
    int const shade[3] = { shade_a, shade_b, shade_c };
    /* Which neighbour each corner tests first. The stock builders check A
     * against C then B, B against A then C, C against B then A; the order
     * decides the winding of the rebuilt polygon, so it is copied exactly. */
    int const first[3] = { 2, 0, 1 };
    int const second[3] = { 1, 2, 0 };

    int count = 0;

    for( int i = 0; i < 3; i++ )
    {
        int const v = vertex[i];

        if( src->screen_vertices_x[v] != TORIDRAW_SCREEN_X_NEAR_CLIPPED )
        {
            out_x[count] = src->screen_vertices_x[v];
            out_y[count] = src->screen_vertices_y[v];
            out_shade[count] = shade[i];
            out_key[count] = toridraw_zbuf_vertex_key(
                src->orthographic_vertices_z, v, target->parallel);
            count++;
            continue;
        }

        {
            int const ox = src->orthographic_vertices_x[v];
            int const oy = src->orthographic_vertices_y[v];
            int const neighbour[2] = { first[i], second[i] };

            for( int n = 0; n < 2; n++ )
            {
                int const j = neighbour[n];
                int const w = vertex[j];
                int lerp_slope;

                if( src->screen_vertices_x[w] == TORIDRAW_SCREEN_X_NEAR_CLIPPED )
                    continue;
                if( depth[j] - depth[i] < 0 )
                    continue;
                if( count >= 4 )
                    break;

                lerp_slope = ToriDraw_TriangleSlopei(near_plane_z, depth[j], depth[i]);
                out_x[count] = ToriDraw_TriangleLerpPlaneProjecti(
                    cot16, near_plane_z, lerp_slope, src->orthographic_vertices_x[w], ox);
                out_y[count] = ToriDraw_TriangleLerpPlaneProjecti(
                    cot16, near_plane_z, lerp_slope, src->orthographic_vertices_y[w], oy);
                out_shade[count] =
                    ToriDraw_TriangleLerpPlanei(near_plane_z, lerp_slope, shade[j], shade[i]);
                out_key[count] = plane_key;
                count++;
            }
        }
    }

    if( count < 3 )
        return 0;

    /* Same winding rule the unclipped bucketing applies, so a clipped face is
     * not culled by the opposite test from its unclipped neighbours. */
    {
        int64_t const dx01 = (int64_t)out_x[0] - out_x[1];
        int64_t const dy01 = (int64_t)out_y[0] - out_y[1];
        int64_t const dx21 = (int64_t)out_x[2] - out_x[1];
        int64_t const dy21 = (int64_t)out_y[2] - out_y[1];

        if( !toridraw_winding_front_facing(dx01 * dy21 - dy01 * dx21) )
            return 0;
    }

    return count;
}

/**
 * Draw one face through the depth-tested kernels.
 *
 * `mode`, `tex_kind`, `texels` and `texture_width` describe the shading; the
 * caller has already resolved them, because it is the caller that owns the
 * texture cache. So are the three corner shades — flat and flat-shaded-textured
 * faces pass one value three times, and only the caller knows which of
 * colors_a/b/c is a colour rather than a selector. `alpha` is the face's
 * coverage in the untextured modes and is ignored in the textured one, which is
 * what the stock dispatcher does too.
 */
static void
ToriDraw_TriangleFaceZBuffered(
    const struct ToriDraw_ZbufTarget* target,
    const struct ToriDraw_ZbufFaceSource* src,
    int face,
    int mode,
    int shade_a,
    int shade_b,
    int shade_c,
    int alpha,
    int tex_kind,
    int tp_vertex,
    int tm_vertex,
    int tn_vertex,
    int* texels,
    int texture_width,
    bool allow_near_clip,
    bool near_clipped)
{
    struct ToriDraw_ZbufTriangle tri;
    int const a = src->face_indices_a[face];
    int const b = src->face_indices_b[face];
    int const c = src->face_indices_c[face];

    memset(&tri, 0, sizeof(tri));
    tri.mode = mode;
    tri.alpha = alpha;
    tri.tex_kind = tex_kind;
    tri.texels = texels;
    tri.texture_width = texture_width;

    if( mode == TORIDRAW_ZBUF_MODE_TEXTURE )
    {
        int const uv[3] = { tp_vertex, tm_vertex, tn_vertex };

        for( int i = 0; i < 3; i++ )
        {
            tri.uv_x[i] = src->orthographic_vertices_x[uv[i]];
            tri.uv_y[i] = src->orthographic_vertices_y[uv[i]];
            tri.uv_z[i] = src->orthographic_vertices_z[uv[i]];
        }
    }

    if( near_clipped &&
        (src->screen_vertices_x[a] == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
         src->screen_vertices_x[b] == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
         src->screen_vertices_x[c] == TORIDRAW_SCREEN_X_NEAR_CLIPPED) )
    {
        int clip_x[4];
        int clip_y[4];
        int clip_shade[4];
        float clip_key[4];
        int count;

        if( !allow_near_clip || !src->orthographic_vertices_x )
            return;

        count = toridraw_zbuf_build_near_clipped(
            target, src, face, shade_a, shade_b, shade_c, clip_x, clip_y, clip_shade,
            clip_key);
        if( count < 3 )
            return;

        for( int i = 0; i < count; i++ )
        {
            clip_x[i] += target->offset_x;
            clip_y[i] += target->offset_y;
        }

        /* Fan the polygon: (0,1,2), then (0,2,3) when the clip produced a quad.
         * Both halves share the depth plane they were cut from, so the seam
         * between them tests identically from either side. */
        for( int t = 0; t + 2 < count; t++ )
        {
            int const corner[3] = { 0, t + 1, t + 2 };

            for( int i = 0; i < 3; i++ )
            {
                tri.x[i] = clip_x[corner[i]];
                tri.y[i] = clip_y[corner[i]];
                tri.shade[i] = clip_shade[corner[i]];
                tri.key[i] = clip_key[corner[i]];
            }
            raster_zbuf_screen(
                target->pixel_buffer,
                target->zbuffer,
                target->stride,
                target->screen_width,
                target->screen_height,
                target->camera_cot16,
                &tri);
        }
        return;
    }

    {
        int const corner[3] = { a, b, c };
        int const shade[3] = { shade_a, shade_b, shade_c };

        for( int i = 0; i < 3; i++ )
        {
            tri.x[i] = src->screen_vertices_x[corner[i]] + target->offset_x;
            tri.y[i] = src->screen_vertices_y[corner[i]] + target->offset_y;
            tri.shade[i] = shade[i];
            tri.key[i] = toridraw_zbuf_vertex_key(
                src->orthographic_vertices_z, corner[i], target->parallel);
        }
    }

    raster_zbuf_screen(
        target->pixel_buffer,
        target->zbuffer,
        target->stride,
        target->screen_width,
        target->screen_height,
        target->camera_cot16,
        &tri);
}

/**
 * Reset the region of the z-buffer a model is about to draw into.
 *
 * This reset is the whole reason a model's depths cannot reach the next model,
 * so it has to cover every pixel that model can touch and no fewer. It is done
 * per model rather than per frame precisely so the buffer means "this model" and
 * not "everything drawn so far".
 *
 * The rect is the model's projected vertex bounds, which is why the cost tracks
 * the model's size on screen rather than the viewport's. A near-clipped model is
 * the exception: its faces are rebuilt onto the near plane and can land outside
 * the bounds of any vertex the projection wrote, so that case resets the whole
 * clip rect instead of a box it cannot trust.
 */
static void
toridraw_zbuf_reset(
    torizdepth_t* RESTRICT zbuffer,
    int stride,
    int screen_width,
    int screen_height,
    const int* RESTRICT screen_vertices_x,
    const int* RESTRICT screen_vertices_y,
    int vertex_count,
    int offset_x,
    int offset_y,
    bool near_clipped,
    bool parallel)
{
    torizdepth_t const clear = (torizdepth_t)toridraw_zdepth_clear_key(parallel);
    int min_x = 0;
    int min_y = 0;
    int max_x = screen_width;
    int max_y = screen_height;

    if( !near_clipped && vertex_count > 0 )
    {
        int lo_x = 1 << 30;
        int lo_y = 1 << 30;
        int hi_x = -(1 << 30);
        int hi_y = -(1 << 30);

        for( int v = 0; v < vertex_count; v++ )
        {
            int const x = screen_vertices_x[v] + offset_x;
            int const y = screen_vertices_y[v] + offset_y;

            if( x < lo_x )
                lo_x = x;
            if( x > hi_x )
                hi_x = x;
            if( y < lo_y )
                lo_y = y;
            if( y > hi_y )
                hi_y = y;
        }

        /* One pixel of slack on each side: the spans round outward from the
         * fixed-point edges, and a reset that stops exactly at a vertex leaves
         * a stale row for the row the raster still writes. */
        min_x = lo_x - 1;
        min_y = lo_y - 1;
        max_x = hi_x + 2;
        max_y = hi_y + 2;
    }

    if( min_x < 0 )
        min_x = 0;
    if( min_y < 0 )
        min_y = 0;
    if( max_x > screen_width )
        max_x = screen_width;
    if( max_y > screen_height )
        max_y = screen_height;
    if( min_x >= max_x || min_y >= max_y )
        return;

    for( int y = min_y; y < max_y; y++ )
    {
        torizdepth_t* row = zbuffer + (size_t)y * (size_t)stride;

        for( int x = min_x; x < max_x; x++ )
            row[x] = clear;
    }
}

#endif
