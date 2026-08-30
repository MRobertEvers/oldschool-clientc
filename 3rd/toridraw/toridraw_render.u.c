#include <stdint.h>
#include <stdlib.h>
#include "graphics/dash_restrict.h"
#include "graphics/proj_census.h"
#include "graphics/projection.h"
#include "graphics/winding.h"
#include "toridraw_math.h"
#include "toridraw_model_internal.h"
#include "toridraw_raster_batch.h"
#include "toridraw_types.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef VERTEXINT_BITS
#define VERTEXINT_BITS 16
#endif

/* The NDJSON anomaly log: compile-time gated, off by default. */
#include "toridraw_debug_log.h"

// clang-format off
#include "graphics/projection16_simd.u.c"
#include "graphics/projection_zdiv_simd.u.c"
/* The per-ISA screen-box sweep behind toridraw_projected_bound. */
#include "graphics/projection_bound.u.c"
// clang-format on

/* The AArch64 prepared-projection entry points, where this lane builds them. */
#include "graphics/projection16.aarch64.h"

/** Far plane for bounding-cylinder frustum cull. */
// #define TORIDRAW_CYLINDER_FAR_PLANE_Z 3500
#define TORIDRAW_CYLINDER_FAR_PLANE_Z 7500

/* z_sum / 3, via the 16.16 reciprocal (21845 == 65536/3). Overflows at
 * z_sum > 98,304 (~32,768 average projected depth per vertex); a wrapped z_sum
 * goes negative and buckets outside the depth table, so the model loses faces
 * from some camera angles and not others. Only reachable with geometry that is
 * already wrong -- guard by range once per model, not by widening here. */
static inline int
div3_fast_fixedpoint(int z_sum)
{
    return (z_sum * 21845) >> 16;
}

/*
 * Largest projected coordinate the raster kernels can carry.
 *
 * They are 32-bit throughout and step edges in 16.16, so a coordinate `x`
 * reaches the kernels as `x << 16`, edge deltas as `(dx << 16) / dy`, and the
 * signed triangle area as a product of two deltas. The area is the binding
 * term: 2*(2*X)^2 must stay under INT_MAX, so X must stay under 16384. 8192
 * leaves that a factor of four of headroom for the gouraud colour numerator,
 * which multiplies a per-vertex colour delta by the same dy, and is still
 * eleven times the half-width of a 1470-pixel viewport — nothing that could
 * plausibly be on screen is affected.
 */
#define TORIDRAW_PROJECTED_COORD_LIMIT 8192
/* The reference per-face pick's cursor dilation (toridraw_mouse_roughly_inside_triangle);
 * defined up here because the projected bound is grown by it too. */
#define TORIDRAW_PICK_SLOP 5

/*
 * The near plane needed to keep this model's projection inside that limit.
 *
 * A perspective vertex projects to `coord * scale / z`, so the only way to
 * bound the result is to bound how small z can get. `radius` is the
 * rotation-invariant sphere about the model origin, so every vertex satisfies
 * |x_cam| <= |center_x| + radius; call that bound M. Clipping at
 * `M * scale / LIMIT` therefore makes `M * scale / z <= LIMIT` for every
 * surviving vertex, and the clipped polygon's own vertices sit exactly on that
 * plane, so they obey it too.
 *
 * This is a no-op for anything the reference client could draw: a 500-unit
 * model a couple of thousand units off-axis resolves to well under the
 * camera's own near plane. It only bites on imported geometry like the 2012
 * QBD, whose 4,791-unit animated sphere swallows the camera whole — and there
 * a vertex one unit past z=50 projects past 90,000, which wraps every edge
 * stepper it touches and paints the streaks it was reported for.
 */

static inline int
toridraw_safe_near_plane_z(
    const struct ToriDraw_BoundsCylinder* bc,
    const struct ProjectedVertex* center,
    int camera_cot16,
    int camera_near_plane_z)
{
    int const scale = toridraw_proj_scale_from_cot16(camera_cot16);
    int abs_center_x;
    int abs_center_y;
    int extent;
    long long required;

    if( !TORIDRAW_DBG_SAFE_NEAR_ENABLED() )
        return camera_near_plane_z;

    if( !bc || scale <= 0 )
        return camera_near_plane_z;

    abs_center_x = center->x < 0 ? -center->x : center->x;
    abs_center_y = center->y < 0 ? -center->y : center->y;
    /* Camera roll mixes x into y, so neither axis alone bounds the other after
     * it. max + min/2 is always at least hypot(x,y) and costs no divide. */
    extent = abs_center_x > abs_center_y ? abs_center_x + (abs_center_y >> 1)
                                         : abs_center_y + (abs_center_x >> 1);
    extent += bc->min_z_depth_any_rotation;

    required = ((long long)extent * scale + TORIDRAW_PROJECTED_COORD_LIMIT - 1) /
               TORIDRAW_PROJECTED_COORD_LIMIT;
    if( required <= camera_near_plane_z )
        return camera_near_plane_z;
    if( required > INT_MAX )
        return INT_MAX;
    return (int)required;
}

/*
 * Everything that exists only to describe a render: the TORIDRAW_SORT_DEBUG
 * counters and printers, and the NDJSON record builders behind them. None of
 * it is on the render path in a default build.
 *
 * Here rather than higher up: it needs the model accessors,
 * div3_fast_fixedpoint, TORIDRAW_PROJECTED_COORD_LIMIT and
 * toridraw_safe_near_plane_z above, and every reporting site is below.
 */
#include "toridraw_debug_render.u.c"

static inline int
ToriDraw_AabbCull(
    struct ToriDraw_AABB* aabb,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera)
{
    (void)camera;
    /*
     * The box is in FRAMEBUFFER coordinates: every corner was projected to a
     * viewport-relative position and then offset by x_center / y_center. So
     * the bounds it is tested against have to be framebuffer coordinates too.
     * They were not -- `width` and `height` are the viewport's EXTENT, which
     * is only the right edge when the viewport starts at the framebuffer
     * origin. The fixed-layout client's 3D view does not:
     *
     *     w=513 h=335 x_center=260 y_center=171 clip=[4,517)x[4,339)
     *
     * so a model was culled once it passed x = 513 while the raster kept
     * drawing to 516, and the last four columns -- exactly clip_left -- were
     * unreachable.
     *
     * Nothing was visibly wrong, because the only box that reaches here is the
     * eight-corner cylinder bound: tens of pixels looser than the geometry
     * inside it, which absorbed the offset. A box derived from a model's own
     * projected vertices does not absorb it, and loses that column in every
     * frame -- which is how this was found.
     *
     * Derived from x_center and width rather than read out of clip_left /
     * clip_right, for two reasons: those are the two fields the projection
     * itself used, so the spaces agree by construction; and not every caller
     * fills the clip rectangle (the D3D9 lane leaves it zero, and reading it
     * there would cull the scene). On this viewport the derivation reproduces
     * the clip rectangle exactly, and it reduces to the old test whenever the
     * viewport does start at the origin.
     */
    int const left = view_port->x_center - view_port->width / 2;
    int const top = view_port->y_center - view_port->height / 2;
    int const right = left + view_port->width;
    int const bottom = top + view_port->height;

    if( aabb->min_screen_x >= right )
        return TORIDRAW_CULL_AABB;
    if( aabb->min_screen_y >= bottom )
        return TORIDRAW_CULL_AABB;
    if( aabb->max_screen_x < left )
        return TORIDRAW_CULL_AABB;
    if( aabb->max_screen_y < top )
        return TORIDRAW_CULL_AABB;

    return TORIDRAW_CULL_VISIBLE;
}

static inline int
ToriDraw_FastCull(
    const struct ToriDraw_Scene* scene,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_Camera* camera,
    struct ProjectedVertex* projected_vertex)
{
    assert(hnd.kind != TORIDRAWMK_NONE);
    assert(ToriDraw_ModelKindIsFull(hnd.kind));
    int scene_x = position->x;
    int scene_y = position->y;
    int scene_z = position->z;
    int near_plane_z = camera->near_plane_z;

    /*
     * The camera's trig and projection scale, off the prepared block when one
     * was published for THIS camera (the same pointer test the kernels make)
     * and off the tables otherwise. The block holds exactly the table values
     * and cot16 >> 1 -- ToriDraw_ScenePrepareProjectionCamera writes them from
     * the same reads -- so the two arms agree bit for bit; the prepared one
     * just spends one cache line instead of four dependent table loads and the
     * cot ladder, once per model.
     */
    int cos_camera_pitch;
    int sin_camera_pitch;
    int cos_camera_yaw;
    int sin_camera_yaw;
    int cot15;
    if( scene->projection_prepared_camera_source == camera )
    {
        const struct ToriDraw_ProjectionPreparedCamera* prep = &scene->projection_prepared_camera;
        cos_camera_pitch = prep->cos_pitch[0];
        sin_camera_pitch = prep->sin_pitch[0];
        cos_camera_yaw = prep->cos_yaw[0];
        sin_camera_yaw = prep->sin_yaw[0];
        cot15 = prep->cot15[0];
    }
    else
    {
        int const camera_pitch = ToriDraw_NormalizeAngle(camera->pitch);
        int const camera_yaw = ToriDraw_NormalizeAngle(camera->yaw);
        cos_camera_pitch = ToriDraw_ReadCosTable(camera_pitch);
        sin_camera_pitch = ToriDraw_ReadSinTable(camera_pitch);
        cos_camera_yaw = ToriDraw_ReadCosTable(camera_yaw);
        sin_camera_yaw = ToriDraw_ReadSinTable(camera_yaw);
        cot15 =
            toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048) >> 1;
    }

    /*
     * The model's origin in camera space: project_orthographic_fast on the
     * point (0, 0, 0). The model rotation of the zero vector is the zero
     * vector, so no model-yaw trig is read and only the camera's two
     * rotations of the translate remain -- the same expressions, the same
     * shifts, in the same order, so the result is the one the general
     * routine gives (projection.u.c project_orthographic_fast_trig).
     */
    {
        int const x_scene = (scene_x * cos_camera_yaw + scene_z * sin_camera_yaw) >> 16;
        int const z_scene = (scene_z * cos_camera_yaw - scene_x * sin_camera_yaw) >> 16;
        projected_vertex->x = x_scene;
        projected_vertex->y = (scene_y * cos_camera_pitch - z_scene * sin_camera_pitch) >> 16;
        projected_vertex->z = (scene_y * sin_camera_pitch + z_scene * cos_camera_pitch) >> 16;
    }

    const struct ToriDraw_BoundsCylinder* bc = model_bounds_cylinder(hnd);
    if( !bc )
        return TORIDRAW_CULL_ERROR;

    int model_edge_radius = bc->radius;

    int mid_z = projected_vertex->z;
    int max_z = model_edge_radius + mid_z;
    if( max_z < near_plane_z )
        return TORIDRAW_CULL_FAST;

    /* worldRender culls midZ>=3500; objRender / inventory icons do not.
     * Icon cameras already use near_plane_z=1 (world uses 50). */
    if( near_plane_z >= 50 && mid_z > TORIDRAW_CYLINDER_FAR_PLANE_Z )
        return TORIDRAW_CULL_FAST;

    int mid_x = projected_vertex->x;
    int mid_y = projected_vertex->y;

    bool const parallel = toridraw_proj_is_parallel(camera->proj_mode);
    int const zoom16 = camera->parallel_zoom16 ? camera->parallel_zoom16 : TORIDRAW_ORTHO_ZOOM_UNIT;

    /* Depth only reaches the perspective extents through the divide; a parallel
     * projection's screen size is the same at any depth, so the clamp below --
     * which exists purely to keep that divisor off the near plane -- would be
     * meaningless there. */
    if( !parallel && mid_z < near_plane_z )
        mid_z = near_plane_z;

    int ortho_screen_x_min = mid_x - model_edge_radius;
    int ortho_screen_x_max = mid_x + model_edge_radius;
    int screen_edge_width = view_port->width >> 1;
    int screen_edge_height = view_port->height >> 1;

/* The y extent, derived only once the x test has passed: two trig reads
 * and two multiplies a model rejected on x never needs. */
#define TORIDRAW_FASTCULL_Y_EXTENT()                                                  \
    int model_center_to_top_edge = bc->center_to_top_edge;                            \
    int model_center_to_bottom_edge =                                                 \
        (bc->center_to_bottom_edge * cos_camera_pitch >> 16) +                        \
        (model_edge_radius * sin_camera_pitch >> 16);                                 \
    int ortho_screen_y_min = mid_y - abs(model_center_to_bottom_edge);                \
    int ortho_screen_y_max = mid_y + abs(model_center_to_top_edge)

    if( parallel )
    {
        int screen_x_min_unoffset = (ortho_screen_x_min * zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        int screen_x_max_unoffset = (ortho_screen_x_max * zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        if( screen_x_min_unoffset > screen_edge_width ||
            screen_x_max_unoffset < -screen_edge_width )
            return TORIDRAW_CULL_FAST;

        TORIDRAW_FASTCULL_Y_EXTENT();
        int screen_y_min_unoffset = (ortho_screen_y_min * zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        int screen_y_max_unoffset = (ortho_screen_y_max * zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        if( screen_y_min_unoffset > screen_edge_height ||
            screen_y_max_unoffset < -screen_edge_height )
            return TORIDRAW_CULL_FAST;

        return TORIDRAW_CULL_VISIBLE;
    }

    /*
     * Four perspective divides, asked as four multiplies.
     *
     * The old form projected each extent -- project_divide, i.e.
     * trunc(((p * cot15) >> 15) * 512 / mid_z) -- and compared the pixel
     * against the half-width. Nothing consumed the pixel; the comparison was
     * the whole product, and this is the only site in the client that ran an
     * integer divide per model on the hot path. So the divide is cancelled
     * across the inequality instead. With z > 0, W >= 0 and truncation toward
     * zero:
     *
     *     trunc(n / z) >  W   <=>   n >=  (W + 1) * z
     *     trunc(n / z) < -W   <=>   n <= -(W + 1) * z
     *
     * (the first: a positive quotient truncates down, so it exceeds W exactly
     * when the real quotient reaches W + 1; the second is its mirror). Both
     * sides are exact integers, so this is the same predicate the divide
     * answered, bit for bit, on every input -- not an approximation of it.
     * `n` is formed by the same int arithmetic project_divide used, wrapping
     * and all, so a value that overflowed there overflows identically here.
     *
     * mid_z is at least near_plane_z after the clamp above, and a perspective
     * camera's near plane is at least 1 (ToriDraw_Project guards the < 1 case
     * onto the clipping family, and project_divide asserted z > 0); a
     * non-positive z would have been a divide by zero on the old path, so it
     * is asserted rather than handled.
     */
    assert(mid_z > 0);
    {
        long long const z_w = (long long)(screen_edge_width + 1) * mid_z;
        long long const z_h = (long long)(screen_edge_height + 1) * mid_z;
        int const qx_min = (ortho_screen_x_min * cot15) >> 15;
        int const qx_max = (ortho_screen_x_max * cot15) >> 15;
        long long const nx_min = SCALE_UNIT(qx_min);
        long long const nx_max = SCALE_UNIT(qx_max);

        if( nx_min >= z_w || nx_max <= -z_w )
            return TORIDRAW_CULL_FAST;

        TORIDRAW_FASTCULL_Y_EXTENT();
        int const qy_min = (ortho_screen_y_min * cot15) >> 15;
        int const qy_max = (ortho_screen_y_max * cot15) >> 15;
        long long const ny_min = SCALE_UNIT(qy_min);
        long long const ny_max = SCALE_UNIT(qy_max);

        if( ny_min >= z_h || ny_max <= -z_h )
            return TORIDRAW_CULL_FAST;
    }
#undef TORIDRAW_FASTCULL_Y_EXTENT

    return TORIDRAW_CULL_VISIBLE;
}

/*
 * The model's screen box, off the coordinates the projection just wrote.
 *
 * This replaced ToriDraw_CalculateCylinderAabb8point, which projected eight
 * cylinder corners per model BEFORE the model was projected, to cull it. Two
 * measurements retired it. A projection census on the Grand Exchange ground
 * orbit (graphics/proj_census.h, TORIDRAW_PROJ_CENSUS_AABB) put the corner
 * box's reject rate at 0.05% of the models that reached it -- 0.00% for most
 * vertex counts, never above 0.9% -- because the cylinder test in
 * ToriDraw_FastCull has already answered the question it asks. And
 * test-proj-model-bench put the corner box at 30 of the 56 ns a four-vertex
 * tile spent being projected, which is 60% of all models. A bound that costs
 * eight projected corners and rejects one model in two thousand is not a cull;
 * it is a tax. Counting projected points for a model of V vertices:
 *
 *     corner box:   8 + V  (survives, 99.95%)      8  (rejected, 0.05%)
 *     this:             V                          V
 *
 * which favours this for every V below 8 / 0.0005 = 16,000 vertices; the
 * largest model in the cache is a tenth of that.
 *
 * WHAT THE BOX IS FOR, and why it is dilated. Two consumers:
 *
 *   - ToriDraw_AabbCull: a model is dropped when the box is wholly off the
 *     viewport. A triangle's screen image is the triangle of its projected
 *     vertices (test-raster-overshoot: the raster never paints outside that
 *     hull), so a box over the vertices is exact for the pixels. Exact is
 *     ALSO the reason for the slop below.
 *   - ToriDraw_ProjectedModelContainsAabb: the prefilter in front of the
 *     per-face pick, whose ROUGH test accepts a cursor up to
 *     TORIDRAW_PICK_SLOP outside a face's own box. The old corner box was
 *     tens of pixels looser than the geometry and absorbed that; an exact
 *     box would not, and a click four pixels outside a fence post that used
 *     to pick would stop picking. So the box is grown by the slop, once,
 *     here: every prefilter that passed before still passes, and the cull
 *     keeps a model whose silhouette is within five pixels of the edge --
 *     which is the model a click at the edge could still pick.
 *
 * A near-clipped vertex is parked at TORIDRAW_SCREEN_X_NEAR_CLIPPED with an
 * UNDIVIDED y, so no box over it means anything; and the near-clip rebuild
 * (toridraw_triangle_clip.u.c) can put pixels well outside the in-front
 * vertices. Any such vertex makes the box the whole plane -- the same
 * concession the corner box made when a corner sat behind the near plane.
 * Only the clipping family can write the sentinel, and the no-clip family
 * can legitimately project a real -5000, so the test is gated on
 * scene->near_clipped exactly as every other sentinel consumer is.
 *
 * Four lanes at a time where the ISA has a lane-wise min/max; the lanes
 * themselves live in graphics/projection_bound.u.c and the ISA files behind
 * it, so nothing arch-specific is inlined here. Everything below the sweep --
 * the scalar tail, the sentinel test, the dilation, the viewport offset -- is
 * the same code on every lane.
 */
static inline void
toridraw_projected_bound(
    struct ToriDraw_Scene* scene,
    const struct ToriDraw_ViewPort* view_port,
    int vertex_count)
{
    const int* const svx = scene->screen_vertices_x;
    const int* const svy = scene->screen_vertices_y;
    struct ToriDraw_AABB* const aabb = &scene->aabb;
    struct ToriDraw_ScreenBound box;
    int box_clipped = 0;
    int i;

    assert(scene);
    assert(view_port);

    aabb->kind = TORIDRAW_AABB_KIND_VERTICES;

    if( vertex_count <= 0 )
    {
        /* Nothing to bound and nothing to draw; stays visible, as it did
         * under the corner box, and picks nothing. */
        aabb->min_screen_x = INT_MIN / 2;
        aabb->max_screen_x = INT_MAX / 2;
        aabb->min_screen_y = INT_MIN / 2;
        aabb->max_screen_y = INT_MAX / 2;
        return;
    }

    box.min_x = svx[0];
    box.max_x = svx[0];
    box.min_y = svy[0];
    box.max_y = svy[0];

    if( scene->projection_bound_vertices > 0 )
    {
        /* A prepared kernel already reduced every full block into four
         * lanes; fold the lanes, then the tail below finishes the last 0..3. */
        assert((scene->projection_bound_vertices & 3) == 0);
        assert(scene->projection_bound_vertices <= vertex_count);
        toridraw_bound_fold_prepared(&scene->projection_bound[0][0], &box);
        i = scene->projection_bound_vertices;
    }
    else
    {
        /* Zero where the lane declined, and the tail below is then the whole
         * sweep; otherwise the count of leading vertices it consumed, having
         * written every field of `box` from exactly those. */
        i = toridraw_bound_sweep(svx, svy, vertex_count, &box);
    }

    /* The scalar tail -- or the whole model, where no lane path applied.
     * `else if` is right: min <= max always, so a new minimum cannot also be
     * a new maximum. */
    for( ; i < vertex_count; i++ )
    {
        int const sx = svx[i];
        int const sy = svy[i];
        if( sx < box.min_x )
            box.min_x = sx;
        else if( sx > box.max_x )
            box.max_x = sx;
        if( sy < box.min_y )
            box.min_y = sy;
        else if( sy > box.max_y )
            box.max_y = sy;
    }

    /* The sentinel is the smallest x any clipped model can hold, so if one
     * is present it IS the minimum -- one compare after the sweep, not one
     * per vertex. */
    if( scene->near_clipped && box.min_x == TORIDRAW_SCREEN_X_NEAR_CLIPPED )
        box_clipped = 1;

    if( box_clipped )
    {
        aabb->min_screen_x = INT_MIN / 2;
        aabb->max_screen_x = INT_MAX / 2;
        aabb->min_screen_y = INT_MIN / 2;
        aabb->max_screen_y = INT_MAX / 2;
    }
    else
    {
        aabb->min_screen_x = box.min_x + view_port->x_center - TORIDRAW_PICK_SLOP;
        aabb->max_screen_x = box.max_x + view_port->x_center + TORIDRAW_PICK_SLOP;
        aabb->min_screen_y = box.min_y + view_port->y_center - TORIDRAW_PICK_SLOP;
        aabb->max_screen_y = box.max_y + view_port->y_center + TORIDRAW_PICK_SLOP;
    }
}


/*
 * Split so the shift is chosen ONCE, not per face.
 *
 * `depth_shift` is zero for every ordinary model, and the wrapper below calls
 * this with a literal 0 in that case: the `>> 0` folds away and the inner loop
 * is byte-for-byte what it was before the large-model support existed. Only a
 * model that actually needs coarser buckets reaches the variable-shift
 * instantiation. Marked always_inline because the whole point is that the
 * constant reaches the loop body.
 */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((always_inline))
#endif
static inline int
bucket_sort_by_average_depth_impl(
    faceint_t* RESTRICT face_depth_buckets,
    faceint_t* RESTRICT face_depth_bucket_counts,
    int depth_levels,
    int depth_stride,
    int depth_shift,
    struct ToriDraw_FaceSortDebugStats* debug_stats,
    bool near_clipped,
    int model_min_depth,
    int num_faces,
    const int* RESTRICT vx,
    const int* RESTRICT vy,
    const int* RESTRICT vz,
    const faceint_t* RESTRICT face_a,
    const faceint_t* RESTRICT face_b,
    const faceint_t* RESTRICT face_c)
{
    int min_d = depth_levels;
    int max_d = 0;

    for( int f = 0; f < num_faces; f++ )
    {
        const uint32_t a = face_a[f];
        const uint32_t b = face_b[f];
        const uint32_t c = face_c[f];

        bool const clip_candidate =
            near_clipped &&
            (vx[a] == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
             vx[b] == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
             vx[c] == TORIDRAW_SCREEN_X_NEAR_CLIPPED);
        long long winding = 1;
        if( !clip_candidate )
            winding = toridraw_winding_2d(
                vx[a], vy[a], vx[b], vy[b], vx[c], vy[c]);

        /* A clipped vertex has sentinel x and undivided y, so this triangle's
         * screen-space winding does not exist yet. The reference buckets it
         * unconditionally and performs the real winding test after building
         * the near-plane polygon. */
        if( clip_candidate || toridraw_winding_front_facing(winding) )
        {
            int z_sum = vz[a] + vz[b] + vz[c];
            /* `depth_shift` makes the table a RESOLUTION budget rather than a
             * hard range limit -- see the note at its computation. Zero for any
             * model that already fits, so those bucket bit-identically to
             * before. */
            int depth_avg = (div3_fast_fixedpoint(z_sum) + model_min_depth) >> depth_shift;

            if( debug_stats )
            {
                debug_stats->front_facing++;
                if( clip_candidate )
                    debug_stats->near_clip_candidates++;
                if( depth_avg < debug_stats->min_depth_seen )
                    debug_stats->min_depth_seen = depth_avg;
                if( depth_avg > debug_stats->max_depth_seen )
                    debug_stats->max_depth_seen = depth_avg;
            }

            if( (unsigned int)depth_avg < (unsigned int)depth_levels )
            {
                const int count = face_depth_bucket_counts[depth_avg];
                /* The configured stride fixes bucket capacity per depth level.
                 * Nothing bounded `count` before, so a model with more than
                 * 512 front-facing triangles at one quantized depth (a large
                 * flat wall seen edge-on, a terrain patch) wrote into the next
                 * depth's bucket and silently corrupted the draw order. Drop
                 * the overflow instead. */
                if( count < depth_stride )
                {
                    /* Every depth bucket is a slice of one allocation, so an
                     * overrun corrupts the next depth's faces rather than
                     * tripping a sanitizer. Assert the slice, not the block. */
                    assert(count >= 0 && count < depth_stride);
                    assert(f >= 0 && f <= 0x7FFF && "face index must fit faceint_t");

                    face_depth_bucket_counts[depth_avg] = count + 1;
                    face_depth_buckets[depth_avg * depth_stride + count] = (faceint_t)f;

                    if( debug_stats )
                    {
                        debug_stats->accepted++;
                        if( count + 1 > debug_stats->max_bucket_occupancy )
                            debug_stats->max_bucket_occupancy = count + 1;
                    }

                    if( depth_avg < min_d )
                        min_d = depth_avg;
                    if( depth_avg > max_d )
                        max_d = depth_avg;
                }
                else if( debug_stats )
                {
                    debug_stats->bucket_overflow++;
                }
            }
            else if( debug_stats )
            {
                if( depth_avg < 0 )
                    debug_stats->depth_out_low++;
                else
                    debug_stats->depth_out_high++;
                if( debug_stats->first_depth_out_face < 0 )
                {
                    debug_stats->first_depth_out_face = f;
                    debug_stats->first_depth_out_value = depth_avg;
                }
            }
        }
        else if( debug_stats )
        {
            if( winding == 0 )
                debug_stats->degenerate++;
            else
                debug_stats->back_facing++;
        }
    }

    if( debug_stats )
        assert(
            debug_stats->front_facing ==
            debug_stats->accepted + debug_stats->depth_out_low +
                debug_stats->depth_out_high + debug_stats->bucket_overflow);

    if( min_d > max_d )
        return 0;
    return (min_d) | (max_d << 16);
}

/* The early switch: ordinary models take the unshifted instantiation. */
static inline int
bucket_sort_by_average_depth(
    faceint_t* RESTRICT face_depth_buckets,
    faceint_t* RESTRICT face_depth_bucket_counts,
    int depth_levels,
    int depth_stride,
    int depth_shift,
    struct ToriDraw_FaceSortDebugStats* debug_stats,
    bool near_clipped,
    int model_min_depth,
    int num_faces,
    const int* RESTRICT vx,
    const int* RESTRICT vy,
    const int* RESTRICT vz,
    const faceint_t* RESTRICT face_a,
    const faceint_t* RESTRICT face_b,
    const faceint_t* RESTRICT face_c)
{
    if( depth_shift == 0 )
        return bucket_sort_by_average_depth_impl(
            face_depth_buckets, face_depth_bucket_counts, depth_levels, depth_stride, 0,
            debug_stats, near_clipped, model_min_depth, num_faces, vx, vy, vz, face_a, face_b,
            face_c);
    return bucket_sort_by_average_depth_impl(
        face_depth_buckets, face_depth_bucket_counts, depth_levels, depth_stride, depth_shift,
        debug_stats, near_clipped, model_min_depth, num_faces, vx, vy, vz, face_a, face_b,
        face_c);
}

/**
 * One traversal of the depth buckets that does everything the old
 * parition_faces_by_priority() + the accumulation half of
 * sort_face_draw_order() did between them: fills the per-priority face
 * buckets, sums the per-priority depths, and lays out the two flexible-priority
 * arrays. Both old loops visited the same faces in the same order and unpacked
 * the same priority nibble, so folding them is order-preserving; the running
 * index they each used (face_priority_bucket_counts[prio] and counts[prio])
 * was always the same number.
 */
static inline void
partition_and_accumulate_faces_by_priority(
    faceint_t* face_priority_buckets,
    faceint_t* face_priority_bucket_counts,
    int* priority_depths,
    int* flex_prio11_face_to_depth,
    int* flex_prio12_face_to_depth,
    int* counts,
    int depth_levels,
    int depth_stride,
    int priority_stride,
    int flex_capacity,
    int model_face_count,
    faceint_t* face_depth_buckets,
    faceint_t* face_depth_bucket_counts,
    const uint8_t* face_priorities,
    int depth_lower_bound,
    int depth_upper_bound)
{
    if( depth_upper_bound >= depth_levels )
        depth_upper_bound = depth_levels - 1;

    for( int depth = depth_upper_bound; depth >= depth_lower_bound; depth-- )
    {
        int face_count = (int)face_depth_bucket_counts[depth];
        if( face_count == 0 )
            continue;

        /* A count above the stride means the bucket writer already spilled
         * into the next depth level's slice of the same allocation. */
        assert(face_count > 0 && face_count <= depth_stride);

        faceint_t* faces = &face_depth_buckets[depth * depth_stride];
        for( int i = 0; i < face_count; i++ )
        {
            faceint_t face_idx = faces[i];
            int prio = faceprio_unpack(face_priorities, face_idx);
            int n;

            assert(face_idx >= 0 && face_idx < model_face_count);
            assert(prio >= 0 && prio < 12 && "face priority indexes counts[12]");

            n = counts[prio];
            /* Same shape of hazard as the depth buckets: the twelve priority
             * runs are slices of one block, so overflowing one silently
             * rewrites the next priority's faces. */
            assert(n >= 0 && n < priority_stride);

            face_priority_buckets[prio * priority_stride + n] = face_idx;

            if( prio < 10 )
            {
                priority_depths[prio] += depth;
            }
            else
            {
                /* depth occupies the low 16 bits and the face index the high
                 * 16, so both have to be representable or the pair decodes as
                 * a different face at a different depth. */
                assert(depth >= 0 && depth <= 0xFFFF);
                assert(n < flex_capacity);

                if( prio == 10 )
                    flex_prio11_face_to_depth[n] = depth | (face_idx << 16);
                else
                    flex_prio12_face_to_depth[n] = depth | (face_idx << 16);
            }

            counts[prio] = n + 1;
            face_priority_bucket_counts[prio] = (faceint_t)(n + 1);
        }
    }
}

/**
 * Emission half of the old sort_face_draw_order(): interleaves the flexible
 * priorities with the fixed priority runs. Takes the counts and depth sums that
 * partition_and_accumulate_faces_by_priority() already produced.
 */
static inline int
sort_face_draw_order(
    int* priority_depths,
    int* flex_prio11_face_to_depth,
    int* flex_prio12_face_to_depth,
    int* face_draw_order,
    faceint_t* face_priority_buckets,
    int* counts,
    int priority_stride,
    int flex_capacity,
    int max_faces)
{
    int average_depth1_2 = 0;
    int count1_2 = counts[1] + counts[2];
    if( count1_2 > 0 )
        average_depth1_2 = (priority_depths[1] + priority_depths[2]) / count1_2;
    int average_depth3_4 = 0;
    int count3_4 = counts[3] + counts[4];
    if( count3_4 > 0 )
        average_depth3_4 = (priority_depths[3] + priority_depths[4]) / count3_4;
    int average_depth6_8 = 0;
    int count6_8 = counts[6] + counts[8];
    if( count6_8 > 0 )
        average_depth6_8 = (priority_depths[6] + priority_depths[8]) / count6_8;

    /* Priority 11 is appended onto priority 10 and the pair is then walked as
     * one run, so the merged length has to fit the array that receives it. */
    assert(counts[10] >= 0 && counts[11] >= 0);
    assert(counts[10] + counts[11] <= flex_capacity);

    for( int i = 0; i < counts[11]; i++ )
    {
        flex_prio11_face_to_depth[counts[10] + i] = flex_prio12_face_to_depth[i];
    }
    counts[10] += counts[11];

    int flexible_face_index = 0;
    int order_index = 0;

    /* The flexible-priority interleave is decided by three averages that
     * nothing else prints, and guessing at them is how an afternoon goes. */
    if( toridraw_sort_debug_level() >= 2 )
    {
        int flex_min = 1 << 30, flex_max = -1;
        for( int i = 0; i < counts[10]; i++ )
        {
            int d = flex_prio11_face_to_depth[i] & 0xFFFF;
            if( d < flex_min ) flex_min = d;
            if( d > flex_max ) flex_max = d;
        }
        fprintf(stderr,
            "sort: counts 0..11 = %d %d %d %d %d %d %d %d %d %d %d %d | "
            "avg12=%d avg34=%d avg68=%d | flex depth %d..%d\n",
            counts[0],counts[1],counts[2],counts[3],counts[4],counts[5],
            counts[6],counts[7],counts[8],counts[9],counts[10],counts[11],
            average_depth1_2, average_depth3_4, average_depth6_8,
            flex_min, flex_max);
    }

    while( flexible_face_index < counts[10] &&
           (flex_prio11_face_to_depth[flexible_face_index] & 0xFFFF) > average_depth1_2 )
    {
        face_draw_order[order_index++] = flex_prio11_face_to_depth[flexible_face_index] >> 16;
        flexible_face_index++;
    }

    for( int prio = 0; prio < 3; prio++ )
    {
        for( int i = 0; i < counts[prio]; i++ )
        {
            face_draw_order[order_index++] =
                face_priority_buckets[prio * priority_stride + i];
        }
    }

    while( flexible_face_index < counts[10] &&
           (flex_prio11_face_to_depth[flexible_face_index] & 0xFFFF) > average_depth3_4 )
    {
        face_draw_order[order_index++] = flex_prio11_face_to_depth[flexible_face_index] >> 16;
        flexible_face_index++;
    }

    for( int prio = 3; prio < 5; prio++ )
    {
        for( int i = 0; i < counts[prio]; i++ )
        {
            face_draw_order[order_index++] =
                face_priority_buckets[prio * priority_stride + i];
        }
    }

    while( flexible_face_index < counts[10] &&
           (flex_prio11_face_to_depth[flexible_face_index] & 0xFFFF) > average_depth6_8 )
    {
        face_draw_order[order_index++] = flex_prio11_face_to_depth[flexible_face_index] >> 16;
        flexible_face_index++;
    }

    for( int prio = 5; prio < 10; prio++ )
    {
        for( int i = 0; i < counts[prio]; i++ )
        {
            face_draw_order[order_index++] =
                face_priority_buckets[prio * priority_stride + i];
        }
    }

    while( flexible_face_index < counts[10] )
    {
        face_draw_order[order_index++] = flex_prio11_face_to_depth[flexible_face_index] >> 16;
        flexible_face_index++;
    }

    /* The order array is sized for the model's faces. Exceeding it means some
     * face was emitted more than once, which is also how a face goes missing. */
    assert(order_index <= max_faces);

    return order_index;
}


static inline void
ToriDraw_ComputeProjectedFaceOrder(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    bool presort)
{
    /* Full mode has no sm_face_x4/y4 to fill: the buffer is allocated only
     * for a small-mode scene (toridraw.c), and only the small sorter
     * stamps it. Saying so here is what stops the batched walk reading a
     * NULL pointer, or a stash left behind by an earlier small model. */
    (void)presort;
    scene->sm_face_xy_valid = 0;
    struct ToriDraw_FaceSortDebugStats debug_stats_storage;
    struct ToriDraw_FaceSortDebugStats* debug_stats = NULL;
    faceint_t* fia = NULL;
    faceint_t* fib = NULL;
    faceint_t* fic = NULL;
    uint8_t* face_priorities = NULL;
    int face_count = 0;

    switch( hnd.kind )
    {
    case TORIDRAWMK_MODEL:
    case TORIDRAWMK_MODEL_HD:
    case TORIDRAWMK_MODEL_SHARED:
    case TORIDRAWMK_MODEL_LENT_FACES:
    {
        struct ToriDraw_Model* m = model_as_full(hnd);
        fia = m->face_indices_a;
        fib = m->face_indices_b;
        fic = m->face_indices_c;
        /* A model that resolves itself per pixel has no use for face render
         * priorities, and honouring them actively defeats the depth test: a
         * priority pins a face into a draw band regardless of depth, which is
         * the painter's-algorithm crutch the z-buffer exists to replace. The
         * two together give the priority's answer, not the depth test's -- so
         * opting a model in drops them. See TORIDRAW_MODEL_FLAG_ZBUFFER.
         *
         * TORIDRAW_MODEL_FLAG_NO_FACE_PRIORITY drops them on its own, for an
         * imported model whose priorities its authoring client never read. */
        face_priorities =
            (toridraw_ignore_priorities() ||
             (m->flags & (TORIDRAW_MODEL_FLAG_ZBUFFER | TORIDRAW_MODEL_FLAG_NO_FACE_PRIORITY)))
                ? NULL
                : m->face_priorities;
        face_count = m->face_count;
        break;
    }
    default:
        assert(0);
        break;
    }

    const struct ToriDraw_BoundsCylinder* bc = model_bounds_cylinder(hnd);
    int model_min_depth = bc ? bc->min_z_depth_any_rotation : 0;
    /* model_min_depth is reused below to carry the observed depth range, so
     * keep the bias the bucket sort actually applied. */
    int const bias = model_min_depth;

    if( toridraw_sort_debug_enabled() )
    {
        toridraw_face_sort_debug_init(&debug_stats_storage);
        debug_stats = &debug_stats_storage;
    }

    /* No clear here. The bucket-count table arrives all-zero -- calloc'd at
     * scene creation, and each sort below re-zeroes exactly the buckets it
     * dirtied once its consumer has walked them, so the invariant holds from
     * model to model. The reference engine bounds this clear by the model's
     * depth diameter for the same reason: a full-width clear is
     * depth_levels-sized, and on the 16K tier that is 32KB zeroed per model
     * to bucket a median of ~19 faces -- ~30MB of memset a frame, 6.9% of
     * steady-state CPU on the XP lane, all of it evicting 2x a P4's L1D. */

    /*
     * How much depth precision this model has to give up to be sortable.
     *
     * The bucket sort quantises a face's average depth into a fixed table, and
     * a model spans [0, 2*bias] of it -- so a model whose bias exceeds half the
     * table cannot be represented AT ALL: every face falls outside the buckets,
     * the sort emits nothing, and the model vanishes while still picking. That
     * is a hard cliff, and it is reached by exactly the things least able to
     * afford it: physically large imports, and any model an animation has
     * stretched (the QBD hit a bias of 54,402 against a 16,384-level table).
     *
     * Shifting the quantisation right by just enough makes the table a budget
     * on PRECISION instead of a limit on SIZE. A large model sorts into coarser
     * depth bands -- the only cost is that two faces closer together than one
     * band may tie, which is a sort-order nicety, not a visibility cliff -- and
     * everything that already fit keeps a shift of zero and buckets exactly as
     * it did before, bit for bit.
     *
     * Derived rather than tuned: the smallest shift for which 2*bias+1 fits.
     */
    int depth_shift = 0;
    {
        long long span = (long long)model_min_depth * 2 + 1;
        while( span > (long long)scene->depth_levels && depth_shift < 30 )
        {
            span >>= 1;
            depth_shift++;
        }
    }

    int bounds = bucket_sort_by_average_depth(
        scene->tmp_depth_faces,
        scene->tmp_depth_face_count,
        scene->depth_levels,
        scene->depth_stride,
        depth_shift,
        debug_stats,
        scene->near_clipped,
        model_min_depth,
        face_count,
        scene->screen_vertices_x,
        scene->screen_vertices_y,
        scene->screen_vertices_z,
        fia,
        fib,
        fic);

    model_min_depth = bounds & 0xFFFF;
    int model_max_depth = bounds >> 16;

    if( !face_priorities )
    {
        int order_index = 0;
        for( int depth = model_max_depth;
             depth < scene->depth_levels && depth >= model_min_depth;
             depth-- )
        {
            int bucket_count = (int)scene->tmp_depth_face_count[depth];
            if( bucket_count == 0 )
                continue;

            faceint_t* faces = &scene->tmp_depth_faces[depth * scene->depth_stride];
            for( int j = 0; j < bucket_count; j++ )
            {
                scene->tmp_face_order[order_index++] = faces[j];
            }
        }
        scene->tmp_face_order_count = order_index;

        /* Restore the all-zero invariant: re-zero exactly the buckets this
         * model dirtied. The sort's returned bounds are the ACTUAL touched
         * range -- every accepted bucket write updated them -- not the
         * bias-derived estimate, which animation can stretch past. An empty
         * sort returns 0 (min=max=0): a 2-byte clear of an already-zero
         * bucket. */
        assert(model_min_depth >= 0);
        assert(model_max_depth < scene->depth_levels);
        assert(model_min_depth <= model_max_depth);
        memset(
            &scene->tmp_depth_face_count[model_min_depth],
            0,
            (size_t)(model_max_depth - model_min_depth + 1) *
                sizeof(scene->tmp_depth_face_count[0]));

        if( debug_stats )
            toridraw_face_sort_debug_print(scene, hnd, debug_stats, order_index);
        TORIDRAW_DBG_CHECK_FACE_ORDER(scene, hnd, face_priorities, face_count, bias);
        return;
    }

    memset(scene->tmp_priority_depth_sum, 0, 12 * sizeof(int));
    memset(scene->tmp_priority_face_count, 0, 12 * sizeof(faceint_t));

    int counts[12] = { 0 };

    partition_and_accumulate_faces_by_priority(
        scene->tmp_priority_faces,
        scene->tmp_priority_face_count,
        scene->tmp_priority_depth_sum,
        scene->tmp_flex_prio11_face_to_depth,
        scene->tmp_flex_prio12_face_to_depth,
        counts,
        scene->depth_levels,
        scene->depth_stride,
        scene->priority_stride,
        scene->flex_prio_capacity,
        face_count,
        scene->tmp_depth_faces,
        scene->tmp_depth_face_count,
        face_priorities,
        model_min_depth,
        model_max_depth);

    /* Same invariant restore as the no-priority path: the partition above only
     * reads the bucket counts over [model_min_depth, model_max_depth], so once
     * it returns the dirtied range can be re-zeroed. */
    assert(model_min_depth >= 0);
    assert(model_max_depth < scene->depth_levels);
    assert(model_min_depth <= model_max_depth);
    memset(
        &scene->tmp_depth_face_count[model_min_depth],
        0,
        (size_t)(model_max_depth - model_min_depth + 1) *
            sizeof(scene->tmp_depth_face_count[0]));

    scene->tmp_face_order_count = sort_face_draw_order(
        scene->tmp_priority_depth_sum,
        scene->tmp_flex_prio11_face_to_depth,
        scene->tmp_flex_prio12_face_to_depth,
        scene->tmp_face_order,
        scene->tmp_priority_faces,
        counts,
        scene->priority_stride,
        scene->flex_prio_capacity,
        scene->max_faces);
    if( debug_stats )
        toridraw_face_sort_debug_print(
            scene, hnd, debug_stats, scene->tmp_face_order_count);
    TORIDRAW_DBG_CHECK_FACE_ORDER(scene, hnd, face_priorities, face_count, bias);
}

#ifndef NDEBUG
/* The CSR sort below no longer clears sm_depth_offset, so it depends on the
 * array arriving all-zero. Verifying that is O(depth_levels) -- the very cost
 * the windowing exists to avoid -- so it is an assert and nothing else: gone
 * in NDEBUG, and in a debug or test build it fails at the model that broke
 * the invariant instead of at the frame that renders wrong. */
static bool
sm_depth_offset_all_zero(const struct ToriDraw_Scene* scene)
{
    assert(scene);
    assert(scene->sm_depth_offset);

    for( int d = 0; d <= scene->depth_levels; d++ )
    {
        if( scene->sm_depth_offset[d] != 0 )
            return false;
    }
    return true;
}
#endif

/*
 * Restore the all-zero invariant over exactly the buckets one model dirtied:
 * [min_depth, max_depth] from the counting pass, plus the end sentinel the
 * prefix sum wrote at max_depth + 1. Call this once every consumer of
 * sm_depth_offset has walked it, on every exit that reached the prefix sum.
 */
static inline void
sm_depth_offset_restore(struct ToriDraw_Scene* scene, int min_depth, int max_depth)
{
    assert(scene);
    assert(scene->sm_depth_offset);
    assert(min_depth >= 0);
    assert(max_depth < scene->depth_levels);
    assert(min_depth <= max_depth);

    memset(
        &scene->sm_depth_offset[min_depth],
        0,
        (size_t)(max_depth - min_depth + 2) * sizeof(int));
}

/* Models whose sort left the y ordering behind; see the increment below. */
static long g_toridraw_presort_models;

static inline int
bucket_sort_by_average_depth_small(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_FaceSortDebugStats* debug_stats,
    bool presort,
    bool near_clipped,
    int model_min_depth,
    int num_faces,
    const int* RESTRICT vx,
    const int* RESTRICT vy,
    const int* RESTRICT vz,
    const faceint_t* RESTRICT face_a,
    const faceint_t* RESTRICT face_b,
    const faceint_t* RESTRICT face_c)
{
    const int depth_levels = scene->depth_levels;
    int min_d = depth_levels;
    int max_d = 0;
    /*
     * Hoisted: one getenv-cached read for the model, not one per face.
     *
     * `presort` is the caller's, and it is the half that matters. The stash
     * has exactly one consumer -- the batched software raster walk -- and a
     * caller that will not run it (every D3D9 renderer sorts back-to-front on
     * the CPU and then hands the faces to the GPU) would otherwise pay seven
     * stores and a six-way compare per drawn face to fill a buffer nobody
     * loads.
     */
    int const stash_xy = presort && toridraw_raster_batch_armed();

    /* Recorded, not re-derived downstream: the walk that reads sm_face_x4/y4
     * asks this rather than asking the same three questions again and
     * possibly answering one of them differently. */
    scene->sm_face_xy_valid = stash_xy;
    /* Reported by TORIDRAW_BATCH_STATS. A GPU lane must show zero here: it
     * sorts for the GPU and never reads the store, so a non-zero count is
     * exactly the regression this split exists to prevent. */
    if( stash_xy )
        g_toridraw_presort_models++;

    /* No clear here, and none of depth_levels width anywhere below. The
     * counting pass only touches buckets in this model's depth span, so the
     * table arrives all-zero -- calloc'd at scene creation, and every exit
     * that dirties it re-zeroes that span once its consumers are done. A
     * full-width clear is 64KB per model at DEPTH_16K, to bucket a median of
     * ~19 faces, and it evicts 8x a P4's L1D on the way past. */
    assert(sm_depth_offset_all_zero(scene));

    for( int f = 0; f < num_faces; f++ )
    {
        scene->sm_face_depth[f] = -1;

        const uint32_t a = face_a[f];
        const uint32_t b = face_b[f];
        const uint32_t c = face_c[f];

        bool const clip_candidate =
            near_clipped &&
            (vx[a] == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
             vx[b] == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
             vx[c] == TORIDRAW_SCREEN_X_NEAR_CLIPPED);
        long long winding = 1;
        if( !clip_candidate )
            winding = toridraw_winding_2d(
                vx[a], vy[a], vx[b], vy[b], vx[c], vy[c]);

        if( clip_candidate || toridraw_winding_front_facing(winding) )
        {
            int z_sum = vz[a] + vz[b] + vz[c];
            int depth_avg = div3_fast_fixedpoint(z_sum) + model_min_depth;

            if( debug_stats )
            {
                debug_stats->front_facing++;
                if( clip_candidate )
                    debug_stats->near_clip_candidates++;
                if( depth_avg < debug_stats->min_depth_seen )
                    debug_stats->min_depth_seen = depth_avg;
                if( depth_avg > debug_stats->max_depth_seen )
                    debug_stats->max_depth_seen = depth_avg;
            }

            if( (unsigned int)depth_avg < (unsigned int)depth_levels )
            {
                scene->sm_face_depth[f] = (faceint_t)depth_avg;
                scene->sm_depth_offset[depth_avg]++;

                /*
                 * Hand the raster pass what this loop already has.
                 *
                 * Every one of these six values was just loaded to compute the
                 * winding, and every one of them used to be read a second time
                 * further down the frame -- through face_indices_a/b/c into
                 * screen_vertices_x/y, which is three loads to get an index and
                 * six dependent loads to use it, per face, to recover what was
                 * sitting in registers here. So does clip_candidate, which the
                 * raster pass re-derived from the same three vertex_x entries
                 * tested above.
                 *
                 * A clip candidate never had its coordinates read -- the
                 * winding is skipped for it -- so only the flag is written, and
                 * the flag is what stops anything reading the rest.
                 *
                 * Only when something will READ it. The batched walk is the
                 * one consumer -- the per-face walk goes back to the index
                 * arrays and gets its near-clip answer from vertex_x -- so
                 * with the batcher disarmed this is seven stores and a
                 * six-way compare per face, to fill a buffer nobody loads.
                 * Leaving that in would put it in the A/B baseline, where it
                 * would read as a cost of the OLD pipeline and be credited to
                 * the new one.
                 */
                if( stash_xy )
                {
                int* const x4 = &scene->sm_face_x4[(size_t)f * 4];
                int* const y4 = &scene->sm_face_y4[(size_t)f * 4];
                x4[3] = clip_candidate ? 1 : 0;
                if( !clip_candidate )
                {
                    /*
                     * ORDERED BY Y, here, once, for every kernel downstream.
                     *
                     * Every raster kernel used to open with a six-way compare
                     * ladder to put the three vertices in y order, and then a
                     * permuting copy to act on the answer. Both are deleted by
                     * doing it here: these three y values are already in
                     * registers -- the winding above needed them -- and the
                     * ladder is a mispredict per triangle on a part that pays
                     * twenty pipeline stages for one.
                     *
                     * The `<=` tie-breaks are transcribed exactly from the C
                     * wrappers the kernels came from. Two triangles that tie
                     * differently stop tiling with each other, so this is part
                     * of the contract and not a comparison order to tidy up.
                     */
                    int const ya = vy[a];
                    int const yb = vy[b];
                    int const yc = vy[c];
                    int const perm = (ya <= yb && ya <= yc)
                                         ? ((yb <= yc) ? 0 : 1)
                                     : (yb <= yc) ? ((yc <= ya) ? 2 : 3)
                                                  : ((ya <= yb) ? 4 : 5);
                    static const unsigned char order[6][3] = {
                        { 0, 1, 2 }, { 0, 2, 1 }, { 1, 2, 0 },
                        { 1, 0, 2 }, { 2, 0, 1 }, { 2, 1, 0 }
                    };
                    unsigned char const* const o = order[perm];
                    int const px[3] = { vx[a], vx[b], vx[c] };
                    int const py[3] = { ya, yb, yc };

                    x4[0] = px[o[0]];
                    x4[1] = px[o[1]];
                    x4[2] = px[o[2]];
                    y4[0] = py[o[0]];
                    y4[1] = py[o[1]];
                    y4[2] = py[o[2]];
                    /* The permutation itself, for the consumers that carry
                     * per-vertex data of their own -- gouraud's three colours,
                     * and the texture frame's three vertex indices. */
                    y4[3] = perm;
                }
                }

                if( debug_stats )
                {
                    debug_stats->accepted++;
                    if( scene->sm_depth_offset[depth_avg] > debug_stats->max_bucket_occupancy )
                        debug_stats->max_bucket_occupancy = scene->sm_depth_offset[depth_avg];
                }

                if( depth_avg < min_d )
                    min_d = depth_avg;
                if( depth_avg > max_d )
                    max_d = depth_avg;
            }
            else if( debug_stats )
            {
                if( depth_avg < 0 )
                    debug_stats->depth_out_low++;
                else
                    debug_stats->depth_out_high++;
                if( debug_stats->first_depth_out_face < 0 )
                {
                    debug_stats->first_depth_out_face = f;
                    debug_stats->first_depth_out_value = depth_avg;
                }
            }
        }
        else if( debug_stats )
        {
            if( winding == 0 )
                debug_stats->degenerate++;
            else
                debug_stats->back_facing++;
        }
    }

    if( debug_stats )
        assert(
            debug_stats->front_facing ==
            debug_stats->accepted + debug_stats->depth_out_low +
                debug_stats->depth_out_high);

    if( min_d > max_d )
        return 0;

    /*
     * TORIDRAW_SPAN_RATIO=1: faces bucketed vs depth levels walked to get them
     * back out again.
     *
     * Every consumer of this table is a loop over [min_d, max_d] reading two
     * ints per level -- the prefix sum below, the cursor seed, the priority
     * partition, the restore. If a model's depth span is much wider than its
     * face count, that is four passes over a range whose length has nothing to
     * do with how much work the model represents, and the bucket sort is being
     * paid for a resolution it is not using. This counter is what says whether
     * that is happening or whether the span is tight.
     */
    {
        static char const* out_path = NULL;
        static int armed = -1;
        static long long faces_total;
        static long long span_total;
        static long long models;

        if( armed < 0 )
        {
            out_path = getenv("TORIDRAW_SPAN_RATIO");
            armed = (out_path && out_path[0]) ? 1 : 0;
        }
        if( armed )
        {
            faces_total += num_faces;
            span_total += (max_d - min_d + 1);
            models++;
            /* Rewritten in place every so often rather than dumped at exit:
             * the measurement harness ends an arm with taskkill /F, and an
             * end-of-run dump is a dump that never happens. */
            if( models % 20000 == 0 )
            {
                FILE* f = fopen(out_path, "wb");
                if( f )
                {
                    fprintf(f,
                        "models=%lld faces/model=%.1f span/model=%.1f "
                        "span-per-face=%.1fx\n",
                        models,
                        (double)faces_total / (double)models,
                        (double)span_total / (double)models,
                        (double)span_total / (double)(faces_total ? faces_total : 1));
                    fclose(f);
                }
            }
        }
    }

    /* Prefix sum over the model's span only. Buckets outside it are zero and
     * stay zero; no consumer reads them, because every consumer is bounded by
     * the same [min_d, max_d] this returns. */
    int total = 0;
    for( int d = min_d; d <= max_d; d++ )
    {
        int count = scene->sm_depth_offset[d];
        scene->sm_depth_offset[d] = total;
        total += count;
    }

    /* End sentinel. Consumers read sm_depth_offset[depth + 1] for depth up to
     * max_d, so max_d + 1 must hold the end of the last bucket -- which is why
     * the array is calloc'd depth_levels + 1 long. */
    assert(max_d + 1 <= depth_levels);
    scene->sm_depth_offset[max_d + 1] = total;

    /* The scatter below bumps sm_depth_cursor only over [min_d, max_d], so
     * that is all that needs seeding. */
    memcpy(
        &scene->sm_depth_cursor[min_d],
        &scene->sm_depth_offset[min_d],
        (size_t)(max_d - min_d + 1) * sizeof(int));

    for( int f = 0; f < num_faces; f++ )
    {
        int depth_avg = scene->sm_face_depth[f];
        if( depth_avg < 0 )
            continue;

        int write = scene->sm_depth_cursor[depth_avg]++;
        scene->sm_faces_by_depth[write] = (faceint_t)f;
    }

    return (min_d) | (max_d << 16);
}

#include "toridraw_face_sort_flat.u.c"

/**
 * Small-scene twin of partition_and_accumulate_faces_by_priority(): the same
 * fold of the old parition_faces_by_priority_small() and the accumulation half
 * of sort_face_draw_order_small() into one traversal.
 */
static inline void
partition_and_accumulate_faces_by_priority_small(
    struct ToriDraw_Scene* scene,
    int* priority_depths,
    int* counts,
    const uint8_t* face_priorities,
    int depth_lower_bound,
    int depth_upper_bound)
{
    const int depth_levels = scene->depth_levels;
    const int max_faces = scene->max_faces;

    if( depth_upper_bound >= depth_levels )
        depth_upper_bound = depth_levels - 1;

    memset(scene->sm_prio_count, 0, sizeof(scene->sm_prio_count));

    for( int depth = depth_upper_bound; depth >= depth_lower_bound; depth-- )
    {
        int start = scene->sm_depth_offset[depth];
        int end = scene->sm_depth_offset[depth + 1];
        for( int i = start; i < end; i++ )
        {
            faceint_t face_idx = scene->sm_faces_by_depth[i];
            int prio = faceprio_unpack(face_priorities, face_idx);
            int n;

            assert(face_idx >= 0 && face_idx < max_faces);
            assert(prio >= 0 && prio < 12 && "face priority indexes counts[12]");

            n = counts[prio];
            /* One allocation, thirteen slices: an overrun here rewrites the
             * next priority's faces where no sanitizer can see it. */
            assert(n >= 0 && n < max_faces);

            scene->sm_prio_faces[prio * max_faces + n] = face_idx;

            if( prio < 10 )
            {
                priority_depths[prio] += depth;
            }
            else
            {
                assert(depth >= 0 && depth <= 0xFFFF);
                assert(n < scene->flex_prio_capacity);

                if( prio == 10 )
                    scene->sm_flex_prio11_face_to_depth[n] = depth | (face_idx << 16);
                else
                    scene->sm_flex_prio12_face_to_depth[n] = depth | (face_idx << 16);
            }

            counts[prio] = n + 1;
            scene->sm_prio_count[prio] = n + 1;
        }
    }
}

static inline int
sort_face_draw_order_small(
    struct ToriDraw_Scene* scene,
    int* face_draw_order,
    int* priority_depths,
    int* counts)
{
    const int max_faces = scene->max_faces;

    int average_depth1_2 = 0;
    int count1_2 = counts[1] + counts[2];
    if( count1_2 > 0 )
        average_depth1_2 = (priority_depths[1] + priority_depths[2]) / count1_2;
    int average_depth3_4 = 0;
    int count3_4 = counts[3] + counts[4];
    if( count3_4 > 0 )
        average_depth3_4 = (priority_depths[3] + priority_depths[4]) / count3_4;
    int average_depth6_8 = 0;
    int count6_8 = counts[6] + counts[8];
    if( count6_8 > 0 )
        average_depth6_8 = (priority_depths[6] + priority_depths[8]) / count6_8;

    assert(counts[10] >= 0 && counts[11] >= 0);
    assert(counts[10] + counts[11] <= scene->flex_prio_capacity);

    for( int i = 0; i < counts[11]; i++ )
    {
        scene->sm_flex_prio11_face_to_depth[counts[10] + i] =
            scene->sm_flex_prio12_face_to_depth[i];
    }
    counts[10] += counts[11];

    int flexible_face_index = 0;
    int order_index = 0;

    while( flexible_face_index < counts[10] &&
           (scene->sm_flex_prio11_face_to_depth[flexible_face_index] & 0xFFFF) > average_depth1_2 )
    {
        face_draw_order[order_index++] =
            scene->sm_flex_prio11_face_to_depth[flexible_face_index] >> 16;
        flexible_face_index++;
    }

    for( int prio = 0; prio < 3; prio++ )
    {
        for( int i = 0; i < counts[prio]; i++ )
        {
            face_draw_order[order_index++] = scene->sm_prio_faces[prio * max_faces + i];
        }
    }

    while( flexible_face_index < counts[10] &&
           (scene->sm_flex_prio11_face_to_depth[flexible_face_index] & 0xFFFF) > average_depth3_4 )
    {
        face_draw_order[order_index++] =
            scene->sm_flex_prio11_face_to_depth[flexible_face_index] >> 16;
        flexible_face_index++;
    }

    for( int prio = 3; prio < 5; prio++ )
    {
        for( int i = 0; i < counts[prio]; i++ )
        {
            face_draw_order[order_index++] = scene->sm_prio_faces[prio * max_faces + i];
        }
    }

    while( flexible_face_index < counts[10] &&
           (scene->sm_flex_prio11_face_to_depth[flexible_face_index] & 0xFFFF) > average_depth6_8 )
    {
        face_draw_order[order_index++] =
            scene->sm_flex_prio11_face_to_depth[flexible_face_index] >> 16;
        flexible_face_index++;
    }

    for( int prio = 5; prio < 10; prio++ )
    {
        for( int i = 0; i < counts[prio]; i++ )
        {
            face_draw_order[order_index++] = scene->sm_prio_faces[prio * max_faces + i];
        }
    }

    while( flexible_face_index < counts[10] )
    {
        face_draw_order[order_index++] =
            scene->sm_flex_prio11_face_to_depth[flexible_face_index] >> 16;
        flexible_face_index++;
    }

    assert(order_index <= max_faces);

    return order_index;
}

static inline void
toridraw_compute_projected_face_order_small(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    bool presort,
    int flat)
{
    struct ToriDraw_FaceSortDebugStats debug_stats_storage;
    struct ToriDraw_FaceSortDebugStats* debug_stats = NULL;
    faceint_t* fia = NULL;
    faceint_t* fib = NULL;
    faceint_t* fic = NULL;
    uint8_t* face_priorities = NULL;
    int face_count = 0;
    /* -1 = not a two-triangle terrain tile; see ToriDraw_Model.tile_sort_kernel. */
    int tile2_rot = -1;

    switch( hnd.kind )
    {
    case TORIDRAWMK_MODEL:
    case TORIDRAWMK_MODEL_HD:
    case TORIDRAWMK_MODEL_SHARED:
    case TORIDRAWMK_MODEL_LENT_FACES:
    {
        struct ToriDraw_Model* m = model_as_full(hnd);
        fia = m->face_indices_a;
        fib = m->face_indices_b;
        fic = m->face_indices_c;
        /* A model that resolves itself per pixel has no use for face render
         * priorities, and honouring them actively defeats the depth test: a
         * priority pins a face into a draw band regardless of depth, which is
         * the painter's-algorithm crutch the z-buffer exists to replace. The
         * two together give the priority's answer, not the depth test's -- so
         * opting a model in drops them. See TORIDRAW_MODEL_FLAG_ZBUFFER.
         *
         * TORIDRAW_MODEL_FLAG_NO_FACE_PRIORITY drops them on its own, for an
         * imported model whose priorities its authoring client never read. */
        face_priorities =
            (toridraw_ignore_priorities() ||
             (m->flags & (TORIDRAW_MODEL_FLAG_ZBUFFER | TORIDRAW_MODEL_FLAG_NO_FACE_PRIORITY)))
                ? NULL
                : m->face_priorities;
        face_count = m->face_count;
        /* TORIDRAWMK_MODEL only. The tile kernel reads vx[0..3] on the promise
         * that this model's four projected vertices are its own and that its two
         * faces are the tile triples -- a promise world_decode_tile makes about a
         * model it owns outright, and not one the lent-faces or the shared regimes
         * are in a position to keep. */
        if( hnd.kind == TORIDRAWMK_MODEL && m->tile_sort_kernel &&
            toridraw_face_sort_tile2_armed() )
        {
            assert(m->vertex_count == 4);
            assert(m->face_count == 2);
            tile2_rot = m->tile_sort_kernel - 1;
        }
        break;
    }
    default:
        assert(0);
        break;
    }

    const struct ToriDraw_BoundsCylinder* bc = model_bounds_cylinder(hnd);
    int model_min_depth = bc ? bc->min_z_depth_any_rotation : 0;

    if( toridraw_sort_debug_enabled() )
    {
        toridraw_face_sort_debug_init(&debug_stats_storage);
        debug_stats = &debug_stats_storage;
    }

    /* The flat sort: SIMD cull into composite keys, then bitonic or radix.
     * The debug counters live in the bucket sort, so a run that asks for
     * them goes there; everything else takes this. */
    if( !debug_stats && flat )
    {
        int const n = toridraw_face_sort_flat(
            scene, presort, scene->near_clipped, model_min_depth, face_count, tile2_rot,
            scene->screen_vertices_x, scene->screen_vertices_y, scene->screen_vertices_z,
            fia, fib, fic);
        const uint32_t* keys = scene->sm_sort_keys;
        int i;

        if( !face_priorities )
        {
            for( i = 0; i < n; i++ )
                scene->tmp_face_order[i] = (int)(keys[i] & 0xFFFF);
            scene->tmp_face_order_count = n;
            return;
        }

        {
            int priority_depths[12] = { 0 };
            int counts[12] = { 0 };

            partition_and_accumulate_faces_by_priority_keys(
                scene, keys, n, priority_depths, counts, face_priorities);
            scene->tmp_face_order_count = sort_face_draw_order_small(
                scene, scene->tmp_face_order, priority_depths, counts);
        }
        return;
    }

    int bounds = bucket_sort_by_average_depth_small(
        scene,
        debug_stats,
        presort,
        scene->near_clipped,
        model_min_depth,
        face_count,
        scene->screen_vertices_x,
        scene->screen_vertices_y,
        scene->screen_vertices_z,
        fia,
        fib,
        fic);

    model_min_depth = bounds & 0xFFFF;
    int model_max_depth = bounds >> 16;

    if( bounds == 0 )
    {
        /* bounds == 0 is ambiguous: it means "nothing accepted", and it is
         * also what a model wholly inside bucket 0 encodes. In that second
         * case the prefix sum ran and dirtied [0, 1], so restore regardless --
         * zeroing two already-zero ints in the first case is free. */
        sm_depth_offset_restore(scene, model_min_depth, model_max_depth);
        scene->tmp_face_order_count = 0;
        if( debug_stats )
            toridraw_face_sort_debug_print(scene, hnd, debug_stats, 0);
        return;
    }

    if( !face_priorities )
    {
        int order_index = 0;
        for( int depth = model_max_depth; depth < scene->depth_levels && depth >= model_min_depth;
             depth-- )
        {
            int start = scene->sm_depth_offset[depth];
            int end = scene->sm_depth_offset[depth + 1];
            for( int j = start; j < end; j++ )
                scene->tmp_face_order[order_index++] = scene->sm_faces_by_depth[j];
        }
        scene->tmp_face_order_count = order_index;

        sm_depth_offset_restore(scene, model_min_depth, model_max_depth);
        if( debug_stats )
            toridraw_face_sort_debug_print(scene, hnd, debug_stats, order_index);
        return;
    }

    int priority_depths[12] = { 0 };
    int counts[12] = { 0 };

    partition_and_accumulate_faces_by_priority_small(
        scene, priority_depths, counts, face_priorities, model_min_depth, model_max_depth);

    /* Last reader of sm_depth_offset; the sort below works from sm_prio_faces
     * and counts. */
    sm_depth_offset_restore(scene, model_min_depth, model_max_depth);

    scene->tmp_face_order_count =
        sort_face_draw_order_small(scene, scene->tmp_face_order, priority_depths, counts);
    if( debug_stats )
        toridraw_face_sort_debug_print(
            scene, hnd, debug_stats, scene->tmp_face_order_count);
}

/* The plain entry: the sort the environment / ToriDraw_FaceSortSetFlat name. */
static inline void
ToriDraw_ComputeProjectedFaceOrderSmall(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    bool presort)
{
    toridraw_compute_projected_face_order_small(
        scene, hnd, presort, toridraw_face_sort_flat_armed());
}

/*
 * The model-shape dispatch, written out once per near-clip family.
 *
 * The near-clip question is answered once per model in ToriDraw_Project and
 * the answer picks one of these two; below this point every kernel is
 * specialized, so no `may_clip` argument is threaded down and nothing tests it
 * per vertex. Passing a flag down instead and trusting the optimizer to
 * unswitch it was measurably worse: it held at -O2/-O3 but left the scalar
 * path ~20%% slower than the code it replaced at -O1, where the flag stopped
 * being a constant and blocked auto-vectorization.
 */
/* Models that can reach behind the near plane, portable ladder only. */
static inline void
toridraw_project_vertices_clip_portable(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_Camera* camera,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int camera_roll,
    int model_mid_z)
{

    /* Full 6DOF when model/camera roll is set (obj-icon zan2d, etc.). yaw-only and
     * pitch+yaw keep the SIMD fused paths; array6_fused matches v0 Dash. */
    if( model_roll != 0 || camera_roll != 0 )
    {
        if( model_has_textures(hnd) )
        {
            TORIDRAW_PROJ_CENSUS_RECORD(TORIDRAW_PROJ_K_6DOF_TEX, 1, model_vertex_count(hnd));
            project_vertices_array6_fused_clip(
                scene->orthographic_vertices_x,
                scene->orthographic_vertices_y,
                scene->orthographic_vertices_z,
                scene->screen_vertices_x,
                scene->screen_vertices_y,
                scene->screen_vertices_z,
                model_vertices_x(hnd),
                model_vertices_y(hnd),
                model_vertices_z(hnd),
                model_vertex_count(hnd),
                model_pitch,
                model_yaw,
                model_roll,
                model_mid_z,
                position->x,
                position->y,
                position->z,
                scene->projection_near_plane_z,
                toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048),
                camera->pitch,
                camera->yaw,
                camera_roll);
        }
        else
        {
            TORIDRAW_PROJ_CENSUS_RECORD(TORIDRAW_PROJ_K_6DOF_NOTEX, 1, model_vertex_count(hnd));
            project_vertices_array6_fused_notex_clip(
                scene->screen_vertices_x,
                scene->screen_vertices_y,
                scene->screen_vertices_z,
                model_vertices_x(hnd),
                model_vertices_y(hnd),
                model_vertices_z(hnd),
                model_vertex_count(hnd),
                model_pitch,
                model_yaw,
                model_roll,
                model_mid_z,
                position->x,
                position->y,
                position->z,
                scene->projection_near_plane_z,
                toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048),
                camera->pitch,
                camera->yaw,
                camera_roll);
        }
    }
    else if( model_pitch != 0 )
    {
        if( model_has_textures(hnd) )
        {
            TORIDRAW_PROJ_CENSUS_RECORD(TORIDRAW_PROJ_K_PITCHYAW_TEX, 1, model_vertex_count(hnd));
            project_vertices_array_pitchyaw_fused_clip(
                scene->orthographic_vertices_x,
                scene->orthographic_vertices_y,
                scene->orthographic_vertices_z,
                scene->screen_vertices_x,
                scene->screen_vertices_y,
                scene->screen_vertices_z,
                model_vertices_x(hnd),
                model_vertices_y(hnd),
                model_vertices_z(hnd),
                model_vertex_count(hnd),
                model_pitch,
                model_yaw,
                model_mid_z,
                position->x,
                position->y,
                position->z,
                scene->projection_near_plane_z,
                toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048),
                camera->pitch,
                camera->yaw);
        }
        else
        {
            TORIDRAW_PROJ_CENSUS_RECORD(TORIDRAW_PROJ_K_PITCHYAW_NOTEX, 1, model_vertex_count(hnd));
            project_vertices_array_pitchyaw_fused_notex_clip(
                scene->screen_vertices_x,
                scene->screen_vertices_y,
                scene->screen_vertices_z,
                model_vertices_x(hnd),
                model_vertices_y(hnd),
                model_vertices_z(hnd),
                model_vertex_count(hnd),
                model_pitch,
                model_yaw,
                model_mid_z,
                position->x,
                position->y,
                position->z,
                scene->projection_near_plane_z,
                toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048),
                camera->pitch,
                camera->yaw);
        }
    }
    else if( model_has_textures(hnd) )
    {
        TORIDRAW_PROJ_CENSUS_RECORD(TORIDRAW_PROJ_K_YAW_TEX, 1, model_vertex_count(hnd));
        project_vertices_array_fused_clip(
            scene->orthographic_vertices_x,
            scene->orthographic_vertices_y,
            scene->orthographic_vertices_z,
            scene->screen_vertices_x,
            scene->screen_vertices_y,
            scene->screen_vertices_z,
            model_vertices_x(hnd),
            model_vertices_y(hnd),
            model_vertices_z(hnd),
            model_vertex_count(hnd),
            model_yaw,
            model_mid_z,
            position->x,
            position->y,
            position->z,
            scene->projection_near_plane_z,
            toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048),
            camera->pitch,
            camera->yaw);
    }
    else
    {
        TORIDRAW_PROJ_CENSUS_RECORD(TORIDRAW_PROJ_K_YAW_NOTEX, 1, model_vertex_count(hnd));
        project_vertices_array_fused_notex_clip(
            scene->screen_vertices_x,
            scene->screen_vertices_y,
            scene->screen_vertices_z,
            model_vertices_x(hnd),
            model_vertices_y(hnd),
            model_vertices_z(hnd),
            model_vertex_count(hnd),
            model_yaw,
            model_mid_z,
            position->x,
            position->y,
            position->z,
            scene->projection_near_plane_z,
            toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048),
            camera->pitch,
            camera->yaw);
    }

}

/*
 * The same models, with the prepared-camera kernel in front.
 *
 * Two functions rather than one with a flag, for the reason the comment above
 * gives: below this point every kernel is specialized, and a runtime test here
 * would be a branch on a value that is constant for the whole frame. The
 * projection kernel a caller selects picks the entry; nothing downstream asks
 * again.
 *
 * Falls through to the portable ladder for every model the prepared kernel
 * cannot take -- pitched, rolled, or drawn under a camera whose prepared block
 * was never published. That fallback is the reason this is a superset of
 * `_portable` and not an alternative to it.
 */
static inline void
toridraw_project_vertices_clip_prepared(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_Camera* camera,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int camera_roll,
    int model_mid_z)
{
#if defined(TORIDRAW_SSE2_PREPARED_PROJECTION)
    /* Same gate as the Apple prepared path: yaw-only geometry, and a prepared
     * block that was published for this exact camera. */
    if( model_pitch == 0 && model_roll == 0 && camera_roll == 0 &&
        scene->projection_prepared_camera_source == camera )
    {
        if( model_has_textures(hnd) )
        {
            TORIDRAW_PROJ_CENSUS_RECORD(TORIDRAW_PROJ_K_YAW_TEX, 1, model_vertex_count(hnd));
            ToriDraw_ProjPreparedClip(
                scene,
                model_vertices_x(hnd),
                model_vertices_y(hnd),
                model_vertices_z(hnd),
                model_vertex_count(hnd),
                camera->yaw,
                model_yaw,
                model_mid_z,
                position);
        }
        else
        {
            TORIDRAW_PROJ_CENSUS_RECORD(TORIDRAW_PROJ_K_YAW_NOTEX, 1, model_vertex_count(hnd));
            ToriDraw_ProjPreparedNotexClip(
                scene,
                model_vertices_x(hnd),
                model_vertices_y(hnd),
                model_vertices_z(hnd),
                model_vertex_count(hnd),
                camera->yaw,
                model_yaw,
                model_mid_z,
                position);
        }
        /* The kernel bounded every full block; the sweep takes the tail. */
        scene->projection_bound_vertices = model_vertex_count(hnd) & ~3;
        return;
    }
#endif

    toridraw_project_vertices_clip_portable(
        scene,
        hnd,
        position,
        camera,
        model_pitch,
        model_yaw,
        model_roll,
        camera_roll,
        model_mid_z);
}

/* Models that provably cannot; no sentinel, no near-plane test. */
#if defined(TORIDRAW_APPLE_NEON_PROJECTION_ASM)
__attribute__((noinline))
#endif
static void
toridraw_project_vertices_noclip_portable(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_Camera* camera,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int camera_roll,
    int model_mid_z)
{
    /* Full 6DOF when model/camera roll is set (obj-icon zan2d, etc.). yaw-only and
     * pitch+yaw keep the SIMD fused paths; array6_fused matches v0 Dash. */
    if( model_roll != 0 || camera_roll != 0 )
    {
        if( model_has_textures(hnd) )
        {
            TORIDRAW_PROJ_CENSUS_RECORD(TORIDRAW_PROJ_K_6DOF_TEX, 0, model_vertex_count(hnd));
            project_vertices_array6_fused_noclip(
                scene->orthographic_vertices_x,
                scene->orthographic_vertices_y,
                scene->orthographic_vertices_z,
                scene->screen_vertices_x,
                scene->screen_vertices_y,
                scene->screen_vertices_z,
                model_vertices_x(hnd),
                model_vertices_y(hnd),
                model_vertices_z(hnd),
                model_vertex_count(hnd),
                model_pitch,
                model_yaw,
                model_roll,
                model_mid_z,
                position->x,
                position->y,
                position->z,
                camera->near_plane_z,
                toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048),
                camera->pitch,
                camera->yaw,
                camera_roll);
        }
        else
        {
            TORIDRAW_PROJ_CENSUS_RECORD(TORIDRAW_PROJ_K_6DOF_NOTEX, 0, model_vertex_count(hnd));
            project_vertices_array6_fused_notex_noclip(
                scene->screen_vertices_x,
                scene->screen_vertices_y,
                scene->screen_vertices_z,
                model_vertices_x(hnd),
                model_vertices_y(hnd),
                model_vertices_z(hnd),
                model_vertex_count(hnd),
                model_pitch,
                model_yaw,
                model_roll,
                model_mid_z,
                position->x,
                position->y,
                position->z,
                camera->near_plane_z,
                toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048),
                camera->pitch,
                camera->yaw,
                camera_roll);
        }
    }
    else if( model_pitch != 0 )
    {
        if( model_has_textures(hnd) )
        {
            TORIDRAW_PROJ_CENSUS_RECORD(TORIDRAW_PROJ_K_PITCHYAW_TEX, 0, model_vertex_count(hnd));
            project_vertices_array_pitchyaw_fused_noclip(
                scene->orthographic_vertices_x,
                scene->orthographic_vertices_y,
                scene->orthographic_vertices_z,
                scene->screen_vertices_x,
                scene->screen_vertices_y,
                scene->screen_vertices_z,
                model_vertices_x(hnd),
                model_vertices_y(hnd),
                model_vertices_z(hnd),
                model_vertex_count(hnd),
                model_pitch,
                model_yaw,
                model_mid_z,
                position->x,
                position->y,
                position->z,
                camera->near_plane_z,
                toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048),
                camera->pitch,
                camera->yaw);
        }
        else
        {
            TORIDRAW_PROJ_CENSUS_RECORD(TORIDRAW_PROJ_K_PITCHYAW_NOTEX, 0, model_vertex_count(hnd));
            project_vertices_array_pitchyaw_fused_notex_noclip(
                scene->screen_vertices_x,
                scene->screen_vertices_y,
                scene->screen_vertices_z,
                model_vertices_x(hnd),
                model_vertices_y(hnd),
                model_vertices_z(hnd),
                model_vertex_count(hnd),
                model_pitch,
                model_yaw,
                model_mid_z,
                position->x,
                position->y,
                position->z,
                camera->near_plane_z,
                toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048),
                camera->pitch,
                camera->yaw);
        }
    }
    else if( model_has_textures(hnd) )
    {
        TORIDRAW_PROJ_CENSUS_RECORD(TORIDRAW_PROJ_K_YAW_TEX, 0, model_vertex_count(hnd));
        project_vertices_array_fused_noclip(
            scene->orthographic_vertices_x,
            scene->orthographic_vertices_y,
            scene->orthographic_vertices_z,
            scene->screen_vertices_x,
            scene->screen_vertices_y,
            scene->screen_vertices_z,
            model_vertices_x(hnd),
            model_vertices_y(hnd),
            model_vertices_z(hnd),
            model_vertex_count(hnd),
            model_yaw,
            model_mid_z,
            position->x,
            position->y,
            position->z,
            camera->near_plane_z,
            toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048),
            camera->pitch,
            camera->yaw);
    }
    else
    {
        TORIDRAW_PROJ_CENSUS_RECORD(TORIDRAW_PROJ_K_YAW_NOTEX, 0, model_vertex_count(hnd));
        project_vertices_array_fused_notex_noclip(
            scene->screen_vertices_x,
            scene->screen_vertices_y,
            scene->screen_vertices_z,
            model_vertices_x(hnd),
            model_vertices_y(hnd),
            model_vertices_z(hnd),
            model_vertex_count(hnd),
            model_yaw,
            model_mid_z,
            position->x,
            position->y,
            position->z,
            camera->near_plane_z,
            toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048),
            camera->pitch,
            camera->yaw);
    }

}

/*
 * The prepared-camera entry for models that provably cannot reach behind the
 * near plane. Its portable twin is toridraw_project_vertices_noclip_portable
 * above, which this falls through to for every model the prepared kernels
 * cannot take.
 */
#if defined(TORIDRAW_APPLE_NEON_PROJECTION_ASM)
__attribute__((always_inline))
#endif
static inline void
toridraw_project_vertices_noclip_prepared(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_Camera* camera,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int camera_roll,
    int model_mid_z)
{
#if defined(TORIDRAW_APPLE_NEON_PROJECTION_ASM)
    int const num_vertices = model_vertex_count(hnd);

    if( model_pitch == 0 && model_roll == 0 && camera_roll == 0 &&
        scene->projection_prepared_camera_source == camera && num_vertices >= 4 )
    {
        if( model_has_textures(hnd) )
        {
            TORIDRAW_PROJ_CENSUS_RECORD(TORIDRAW_PROJ_K_YAW_TEX, 0, num_vertices);
            toridraw_project_vertices_fused_neon_noclip_native_prepared_aarch64(
                &scene->screen_vertices_x,
                model_vertices_x(hnd),
                model_vertices_y(hnd),
                model_vertices_z(hnd),
                num_vertices,
                model_yaw,
                model_mid_z,
                position);
        }
        else
        {
            TORIDRAW_PROJ_CENSUS_RECORD(TORIDRAW_PROJ_K_YAW_NOTEX, 0, num_vertices);
            toridraw_project_vertices_fused_neon_notex_noclip_native_prepared_aarch64(
                &scene->screen_vertices_x,
                model_vertices_x(hnd),
                model_vertices_y(hnd),
                model_vertices_z(hnd),
                num_vertices,
                model_yaw,
                model_mid_z,
                position);
        }
        /* The exact-four body (num_vertices == 4) does not touch the bound
         * block -- four outputs are one vector load each to sweep. The
         * generic loop covers every full block and leaves the tail to the
         * sweep. */
        scene->projection_bound_vertices = num_vertices == 4 ? 0 : (num_vertices & ~3);
        return;
    }
#endif

#if defined(TORIDRAW_SSE2_PREPARED_PROJECTION)
    /* Same gate as the Apple prepared path: yaw-only geometry, and a prepared
     * block that was published for this exact camera. */
    if( model_pitch == 0 && model_roll == 0 && camera_roll == 0 &&
        scene->projection_prepared_camera_source == camera )
    {
        if( model_has_textures(hnd) )
        {
            TORIDRAW_PROJ_CENSUS_RECORD(TORIDRAW_PROJ_K_YAW_TEX, 0, model_vertex_count(hnd));
            ToriDraw_ProjPreparedNoclip(
                scene,
                model_vertices_x(hnd),
                model_vertices_y(hnd),
                model_vertices_z(hnd),
                model_vertex_count(hnd),
                camera->yaw,
                model_yaw,
                model_mid_z,
                position);
        }
        else
        {
            TORIDRAW_PROJ_CENSUS_RECORD(TORIDRAW_PROJ_K_YAW_NOTEX, 0, model_vertex_count(hnd));
            ToriDraw_ProjPreparedNotexNoclip(
                scene,
                model_vertices_x(hnd),
                model_vertices_y(hnd),
                model_vertices_z(hnd),
                model_vertex_count(hnd),
                camera->yaw,
                model_yaw,
                model_mid_z,
                position);
        }
        /* The kernel bounded every full block; the sweep takes the tail. */
        scene->projection_bound_vertices = model_vertex_count(hnd) & ~3;
        return;
    }
#endif

    toridraw_project_vertices_noclip_portable(
        scene,
        hnd,
        position,
        camera,
        model_pitch,
        model_yaw,
        model_roll,
        camera_roll,
        model_mid_z);
}

/*
 * Parallel-projection dispatch, one per near-clip family, mirroring the
 * perspective pair above. Same split for the same reason: the choice is made
 * once per model in ToriDraw_Project and everything below is specialized.
 */
static inline void
toridraw_project_vertices_parallel_clip(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_Camera* camera,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int camera_roll,
    int model_mid_z)
{
    int const zoom16 = camera->parallel_zoom16 ? camera->parallel_zoom16 : TORIDRAW_ORTHO_ZOOM_UNIT;

    if( model_pitch != 0 || model_roll != 0 || camera_roll != 0 )
    {
        if( model_has_textures(hnd) )
            project_vertices_array_ortho6_fused_clip(
                scene->orthographic_vertices_x, scene->orthographic_vertices_y,
                scene->orthographic_vertices_z, scene->screen_vertices_x,
                scene->screen_vertices_y, scene->screen_vertices_z,
                model_vertices_x(hnd), model_vertices_y(hnd), model_vertices_z(hnd),
                model_vertex_count(hnd), model_pitch, model_yaw, model_roll, model_mid_z,
                position->x, position->y, position->z, scene->projection_near_plane_z,
                zoom16, camera->pitch, camera->yaw, camera_roll);
        else
            project_vertices_array_ortho6_fused_notex_clip(
                scene->screen_vertices_x, scene->screen_vertices_y, scene->screen_vertices_z,
                model_vertices_x(hnd), model_vertices_y(hnd), model_vertices_z(hnd),
                model_vertex_count(hnd), model_pitch, model_yaw, model_roll, model_mid_z,
                position->x, position->y, position->z, scene->projection_near_plane_z,
                zoom16, camera->pitch, camera->yaw, camera_roll);
    }
    else if( model_has_textures(hnd) )
    {
        project_vertices_array_ortho_fused_clip(
            scene->orthographic_vertices_x, scene->orthographic_vertices_y,
            scene->orthographic_vertices_z, scene->screen_vertices_x,
            scene->screen_vertices_y, scene->screen_vertices_z,
            model_vertices_x(hnd), model_vertices_y(hnd), model_vertices_z(hnd),
            model_vertex_count(hnd), model_yaw, model_mid_z,
            position->x, position->y, position->z, scene->projection_near_plane_z,
            zoom16, camera->pitch, camera->yaw);
    }
    else
    {
        project_vertices_array_ortho_fused_notex_clip(
            scene->screen_vertices_x, scene->screen_vertices_y, scene->screen_vertices_z,
            model_vertices_x(hnd), model_vertices_y(hnd), model_vertices_z(hnd),
            model_vertex_count(hnd), model_yaw, model_mid_z,
            position->x, position->y, position->z, scene->projection_near_plane_z,
            zoom16, camera->pitch, camera->yaw);
    }
}

static inline void
toridraw_project_vertices_parallel_noclip(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_Camera* camera,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int camera_roll,
    int model_mid_z)
{
    int const zoom16 = camera->parallel_zoom16 ? camera->parallel_zoom16 : TORIDRAW_ORTHO_ZOOM_UNIT;

    if( model_pitch != 0 || model_roll != 0 || camera_roll != 0 )
    {
        if( model_has_textures(hnd) )
            project_vertices_array_ortho6_fused_noclip(
                scene->orthographic_vertices_x, scene->orthographic_vertices_y,
                scene->orthographic_vertices_z, scene->screen_vertices_x,
                scene->screen_vertices_y, scene->screen_vertices_z,
                model_vertices_x(hnd), model_vertices_y(hnd), model_vertices_z(hnd),
                model_vertex_count(hnd), model_pitch, model_yaw, model_roll, model_mid_z,
                position->x, position->y, position->z,
                zoom16, camera->pitch, camera->yaw, camera_roll);
        else
            project_vertices_array_ortho6_fused_notex_noclip(
                scene->screen_vertices_x, scene->screen_vertices_y, scene->screen_vertices_z,
                model_vertices_x(hnd), model_vertices_y(hnd), model_vertices_z(hnd),
                model_vertex_count(hnd), model_pitch, model_yaw, model_roll, model_mid_z,
                position->x, position->y, position->z,
                zoom16, camera->pitch, camera->yaw, camera_roll);
    }
    else if( model_has_textures(hnd) )
    {
        project_vertices_array_ortho_fused_noclip(
            scene->orthographic_vertices_x, scene->orthographic_vertices_y,
            scene->orthographic_vertices_z, scene->screen_vertices_x,
            scene->screen_vertices_y, scene->screen_vertices_z,
            model_vertices_x(hnd), model_vertices_y(hnd), model_vertices_z(hnd),
            model_vertex_count(hnd), model_yaw, model_mid_z,
            position->x, position->y, position->z,
            zoom16, camera->pitch, camera->yaw);
    }
    else
    {
        project_vertices_array_ortho_fused_notex_noclip(
            scene->screen_vertices_x, scene->screen_vertices_y, scene->screen_vertices_z,
            model_vertices_x(hnd), model_vertices_y(hnd), model_vertices_z(hnd),
            model_vertex_count(hnd), model_yaw, model_mid_z,
            position->x, position->y, position->z,
            zoom16, camera->pitch, camera->yaw);
    }
}

/*
 * The two stock projection vtables.
 *
 * They differ in exactly two slots. A parallel camera has no prepared family
 * -- the prepared block is a perspective cotangent and a yaw/pitch pair, and
 * the orthographic kernels use neither -- so both tables name the same two
 * parallel kernels, and only the perspective pair changes.
 *
 * Here rather than in kernels/: these are data about the static functions
 * above, so they belong beside them. The kernels/ files wrap them into the
 * ToriDraw_ProjectionKernel objects a caller selects.
 */
static const struct ToriDraw_ProjectionKernelVTable g_projection_prepared_vtable = {
    .project = {
        [TORIDRAW_PROJECTION_PERSPECTIVE_CLIP] = toridraw_project_vertices_clip_prepared,
        [TORIDRAW_PROJECTION_PERSPECTIVE_NOCLIP] = toridraw_project_vertices_noclip_prepared,
        [TORIDRAW_PROJECTION_PARALLEL_CLIP] = toridraw_project_vertices_parallel_clip,
        [TORIDRAW_PROJECTION_PARALLEL_NOCLIP] = toridraw_project_vertices_parallel_noclip,
    },
};

static const struct ToriDraw_ProjectionKernelVTable g_projection_portable_vtable = {
    .project = {
        [TORIDRAW_PROJECTION_PERSPECTIVE_CLIP] = toridraw_project_vertices_clip_portable,
        [TORIDRAW_PROJECTION_PERSPECTIVE_NOCLIP] = toridraw_project_vertices_noclip_portable,
        [TORIDRAW_PROJECTION_PARALLEL_CLIP] = toridraw_project_vertices_parallel_clip,
        [TORIDRAW_PROJECTION_PARALLEL_NOCLIP] = toridraw_project_vertices_parallel_noclip,
    },
};

/* Which slot this model and camera resolve to. */
static inline enum ToriDraw_ProjectionShape
toridraw_projection_shape(bool parallel, bool may_clip)
{
    if( parallel )
        return may_clip ? TORIDRAW_PROJECTION_PARALLEL_CLIP
                        : TORIDRAW_PROJECTION_PARALLEL_NOCLIP;
    return may_clip ? TORIDRAW_PROJECTION_PERSPECTIVE_CLIP
                    : TORIDRAW_PROJECTION_PERSPECTIVE_NOCLIP;
}

/*
 * Project one model, dispatching its vertices through `vtable`.
 *
 * Everything here -- the capacity refusal, the fast cull, the safe near
 * plane, the screen box, the AABB cull -- is the same whichever projection
 * kernel was selected. The kernel decides ONE thing: which of the four
 * specialized vertex kernels runs, and that is the vtable call below.
 */
static inline int
ToriDraw_ProjectWithVTable(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    const struct ToriDraw_ProjectionKernelVTable* vtable)
{
    assert(vtable);
    struct ProjectedVertex center_projection;

    /* Refined below once the model's camera-space centre is known. Set here so
     * an early cull cannot leave a stale plane behind for the next caller. */
    scene->projection_near_plane_z = camera->near_plane_z;

    /* Every projection and sort scratch array is scene-capacity bounded. A
     * 2012 QBD is 6,223 vertices / 9,012 faces after its two model parts are
     * merged, so the old 4,096 assumptions wrote beyond six separate buffers.
     * Refuse an unsupported model before touching any array; full scenes now
     * carry a large enough declared capacity for the imported encounter. */
    if( model_vertex_count(hnd) > scene->max_vertices ||
        model_face_count(hnd) > scene->max_faces )
    {
        TORIDRAW_DBG_RECORD_CAPACITY_REJECT(scene, hnd);
        if( toridraw_sort_debug_enabled() )
            fprintf(
                stderr,
                "sort_capacity: model=%p vertices=%d/%d faces=%d/%d rejected=1\n",
                (void*)model_as_full(hnd),
                model_vertex_count(hnd),
                scene->max_vertices,
                model_face_count(hnd),
                scene->max_faces);
        return TORIDRAW_CULL_ERROR;
    }

    /* Surface an undersized depth table before fast/AABB culling can hide the
     * model from the face-sort diagnostics.  The bound is deliberately
     * rotation-independent: every sorted face depth lies within [-bias,+bias]
     * before the sorter adds bias, hence the required diameter is 2*bias+1. */
    if( toridraw_sort_debug_enabled() )
    {
        const struct ToriDraw_BoundsCylinder* bounds = model_bounds_cylinder(hnd);
        if( bounds )
        {
            long long const required_levels =
                (long long)bounds->min_z_depth_any_rotation * 2 + 1;
            if( required_levels > scene->depth_levels )
                fprintf(
                    stderr,
                    "sort_depth_capacity: model=%p vertices=%d faces=%d "
                    "bounds={y=%d..%d,xz=%d,bias=%d} required=%lld "
                    "depth_levels=%d rejected=0\n",
                    (void*)model_as_full(hnd),
                    model_vertex_count(hnd),
                    model_face_count(hnd),
                    bounds->min_y,
                    bounds->max_y,
                    bounds->radius,
                    bounds->min_z_depth_any_rotation,
                    required_levels,
                    scene->depth_levels);
        }
    }

    int cull = TORIDRAW_CULL_VISIBLE;

    cull = ToriDraw_FastCull(scene, view_port, hnd, position, camera, &center_projection);
    if( cull != TORIDRAW_CULL_VISIBLE )
    {
        if( cull == TORIDRAW_CULL_ERROR )
            TORIDRAW_PROJ_CENSUS_COUNT(cull_error);
        else
            TORIDRAW_PROJ_CENSUS_COUNT(cull_fast);
        return cull;
    }

    scene->projected_vertex = center_projection;

    /*
     * No screen box is built before the projection. The model is bounded
     * AFTER it, off its own projected vertices (toridraw_projected_bound
     * below), which is where the eight-corner cylinder box used to be and
     * why it is gone.
     */
    int const bound_vertex_count = model_vertex_count(hnd);

    int const model_pitch = ToriDraw_NormalizeAngle(position->pitch);
    int const model_yaw = ToriDraw_NormalizeAngle(position->yaw);
    int const model_roll = ToriDraw_NormalizeAngle(position->roll);
    int const camera_roll = ToriDraw_NormalizeAngle(camera->roll);

    /*
     * Decide once, for the whole model, whether any vertex can land behind the
     * near plane. The reference asks the same question at the same point
     * (Client-TS Model.worldRender:1755 `clipped = midZ - radiusZ <= 50`) and
     * uses the answer to skip every per-face sentinel test in render2:1876 —
     * which is why it never has to defend a genuine -5000. We do the same, and
     * additionally hand the answer to the projection kernels so the per-vertex
     * clip compare, the sentinel blend and the -5001 nudge all disappear from
     * the common path rather than merely being ignored downstream.
     *
     * `min_z_depth_any_rotation` is max(center_to_top_edge, center_to_bottom_edge),
     * i.e. the radius of a sphere about the model origin containing every
     * vertex (toridraw_model_transform.c:736). A sphere is the right bound
     * here precisely because it is rotation-invariant: model pitch/yaw/roll and
     * camera pitch/yaw/roll all rotate about that origin, so this one test
     * covers the 6DOF and pitch+yaw paths as well as the yaw-only one. The
     * reference's `radiusZ` cannot: it folds in cos/sin of the camera pitch and
     * so is only valid for worldRender's yaw-only models.
     *
     * Must be conservative in the "may clip" direction: the no-clip kernel
     * divides by z unconditionally, so a vertex that sneaks below the near
     * plane would be a sign-flipped projection, or a SIGFPE at z == 0. Hence
     * `<` against near_plane_z (not `<=`), and the near_plane_z < 1 guard for
     * cameras that would otherwise admit a zero divisor.
     */
    struct ToriDraw_BoundsCylinder const* const proj_bc = model_bounds_cylinder(hnd);

    /*
     * Everything downstream clips against this rather than the camera's own
     * near plane. Parallel projection never divides by z, so nothing there can
     * leave the 16.16 domain and the camera's value stands.
     */
    scene->projection_near_plane_z =
        toridraw_proj_is_parallel(camera->proj_mode)
            ? camera->near_plane_z
            : toridraw_safe_near_plane_z(
                  proj_bc,
                  &center_projection,
                  toridraw_proj_cot16(
                      camera->proj_mode, camera->proj_scale, camera->fov_rpi2048),
                  camera->near_plane_z);

    /*
     * The near_plane_z < 1 guard is a PERSPECTIVE-only safety rule: there it
     * would admit a zero or negative divisor, so the clipping family has to
     * take over. Parallel projection never divides, so a near plane behind the
     * camera is a perfectly ordinary request -- it is how a map editor says
     * "never hide anything" -- and forcing the clipping family on it would cost
     * every model the more expensive kernel for no reason.
     */
    bool may_clip =
        !proj_bc ||
        (!toridraw_proj_is_parallel(camera->proj_mode) && camera->near_plane_z < 1) ||
        center_projection.z - proj_bc->min_z_depth_any_rotation <
            scene->projection_near_plane_z;

#ifdef TORIDRAW_NEAR_CLIP_FORCE_ALL
    /* Build with -DTORIDRAW_NEAR_CLIP_FORCE_ALL=1 to send every model down the
     * clipping kernel, i.e. the behaviour from before the gate existed. Frames
     * rendered by the two builds must be byte-identical: that equality is the
     * whole correctness argument for the gate, so keep this switch working. */
    may_clip = true;
#endif

#ifdef TORIDRAW_NEAR_CLIP_STATS
    {
        static long clipped_models = 0;
        static long total_models = 0;
        total_models++;
        if( may_clip )
            clipped_models++;
        if( (total_models % 100000) == 0 )
            fprintf(
                stderr,
                "near_clip_stats: %ld/%ld models took the clipping kernel (%.2f%%)\n",
                clipped_models,
                total_models,
                100.0 * (double)clipped_models / (double)total_models);
    }
#endif

    scene->near_clipped = may_clip;
    /* Only the prepared kernels (AArch64 assembly, SSE2 fused-yaw) fill the
     * bound block; every other path leaves this zero and the bound is swept
     * from the outputs. */
    scene->projection_bound_vertices = 0;

    /*
     * Same near-clip gate, two different meanings. Under perspective it is a
     * safety requirement -- a vertex behind the plane has no projection. Under
     * a parallel camera there is no singularity to avoid, so it only says
     * whether any vertex is near enough the view plane to need hiding; a
     * camera with a very negative near_plane_z never takes the clipping family
     * at all, which is the map editor's normal configuration.
     */
    vtable->project[toridraw_projection_shape(
        toridraw_proj_is_parallel(camera->proj_mode), may_clip)](
        scene, hnd, position, camera,
        model_pitch, model_yaw, model_roll, camera_roll, center_projection.z);

    /*
     * The model's bound, off the coordinates just written; see
     * toridraw_projected_bound for why it is exact, dilated, and after the
     * projection rather than before it.
     *
     * Placed BEFORE the debug print so a culled model stays as silent as it
     * was when the cull happened earlier.
     */
    toridraw_projected_bound(scene, view_port, bound_vertex_count);

    cull = ToriDraw_AabbCull(&scene->aabb, view_port, camera);
    TORIDRAW_PROJ_CENSUS_AABB(bound_vertex_count, cull != TORIDRAW_CULL_VISIBLE);
    if( cull != TORIDRAW_CULL_VISIBLE )
    {
        TORIDRAW_PROJ_CENSUS_COUNT(cull_aabb);
        return cull;
    }

    TORIDRAW_PROJ_CENSUS_COUNT(projected);

    toridraw_projection_debug_print(
        scene, hnd, position, view_port, camera, center_projection.z, may_clip);

#ifdef TORIDRAW_NEAR_CLIP_STATS
    /*
     * Verification build. Two properties are checked here, on real scene data.
     *
     * Deliberately NOT done by comparing rendered frames between a gated and a
     * forced build: this client's offline boot is not frame-deterministic (the
     * same binary run twice produces different frames — async asset loads
     * settle differently), so a frame byte-compare answers "did these two runs
     * load the same things", not "do the two kernels agree". Everything below
     * runs both kernels over the same model inside one process instead.
     *
     *   1. Conservativeness. If any vertex really did land in front of the near
     *      plane, the gate must have said so. The reverse — gate says "may
     *      clip", nothing actually clips — is merely a missed optimization.
     *      A failure here means the no-clip kernel divided by a z it should
     *      not have, the one way this change can corrupt geometry.
     *   2. Equivalence. Whenever nothing actually clipped, both kernels must
     *      produce identical vertices. Checked regardless of which way the gate
     *      went, so the boundary case (gate says "may clip", reality says no)
     *      is covered too, not just the easy interior.
     *
     * The kernels write screen z as (camera-space z - model_mid_z), so the
     * camera-space z is recoverable exactly and neither check needs the
     * projection to hand anything extra back.
     */
    {
        int const vcount = model_vertex_count(hnd);
        bool actually_clipped = false;
        for( int vi = 0; vi < vcount; vi++ )
        {
            if( scene->screen_vertices_z[vi] + center_projection.z < camera->near_plane_z )
            {
                actually_clipped = true;
                break;
            }
        }

        if( actually_clipped && !may_clip )
        {
            fprintf(
                stderr,
                "near_clip_bound_violation: model clipped but the gate said it could not "
                "(mid_z=%d sphere_r=%d near=%d)\n",
                center_projection.z,
                proj_bc ? proj_bc->min_z_depth_any_rotation : -1,
                camera->near_plane_z);
            assert(0 && "near-clip gate was not conservative");
        }

        if( !actually_clipped )
        {
            static int* verify_x = NULL;
            static int* verify_y = NULL;
            static int* verify_z = NULL;
            static int verify_cap = 0;
            static long compared_models = 0;
            static long nudge_divergences = 0;

            if( vcount > verify_cap )
            {
                verify_cap = vcount;
                verify_x = (int*)realloc(verify_x, (size_t)verify_cap * sizeof(int));
                verify_y = (int*)realloc(verify_y, (size_t)verify_cap * sizeof(int));
                verify_z = (int*)realloc(verify_z, (size_t)verify_cap * sizeof(int));
                assert(verify_x && verify_y && verify_z);
            }
            memcpy(verify_x, scene->screen_vertices_x, (size_t)vcount * sizeof(int));
            memcpy(verify_y, scene->screen_vertices_y, (size_t)vcount * sizeof(int));
            memcpy(verify_z, scene->screen_vertices_z, (size_t)vcount * sizeof(int));

            /* Re-project down the opposite arm and compare. */
            bool const par = toridraw_proj_is_parallel(camera->proj_mode);
            if( may_clip && par )
                toridraw_project_vertices_parallel_noclip(
                    scene, hnd, position, camera,
                    model_pitch, model_yaw, model_roll, camera_roll, center_projection.z);
            else if( par )
                toridraw_project_vertices_parallel_clip(
                    scene, hnd, position, camera,
                    model_pitch, model_yaw, model_roll, camera_roll, center_projection.z);
            else if( may_clip )
                toridraw_project_vertices_noclip_prepared(
                    scene, hnd, position, camera,
                    model_pitch, model_yaw, model_roll, camera_roll, center_projection.z);
            else
                toridraw_project_vertices_clip_prepared(
                    scene, hnd, position, camera,
                    model_pitch, model_yaw, model_roll, camera_roll, center_projection.z);

            compared_models++;
            for( int vi = 0; vi < vcount; vi++ )
            {
                /* The one legitimate divergence: the clipping kernel nudges a
                 * genuinely projected -5000 to -5001 and the no-clip kernel
                 * does not. Counted rather than failed — it is the documented
                 * consequence of dropping the nudge, and the count says how
                 * often it is reachable at all. */
                int const lo = verify_x[vi] < scene->screen_vertices_x[vi]
                                   ? verify_x[vi]
                                   : scene->screen_vertices_x[vi];
                int const hi = verify_x[vi] < scene->screen_vertices_x[vi]
                                   ? scene->screen_vertices_x[vi]
                                   : verify_x[vi];
                if( lo == TORIDRAW_SCREEN_X_NEAR_CLIPPED_NUDGE &&
                    hi == TORIDRAW_SCREEN_X_NEAR_CLIPPED &&
                    verify_y[vi] == scene->screen_vertices_y[vi] &&
                    verify_z[vi] == scene->screen_vertices_z[vi] )
                {
                    nudge_divergences++;
                    continue;
                }
                if( verify_x[vi] != scene->screen_vertices_x[vi] ||
                    verify_y[vi] != scene->screen_vertices_y[vi] ||
                    verify_z[vi] != scene->screen_vertices_z[vi] )
                {
                    fprintf(
                        stderr,
                        "near_clip_mismatch: gate=%d vertex %d/%d "
                        "gated=(%d,%d,%d) other=(%d,%d,%d) "
                        "[mpitch=%d myaw=%d mroll=%d croll=%d tex=%d mid_z=%d near=%d]\n",
                        (int)may_clip,
                        vi,
                        vcount,
                        verify_x[vi],
                        verify_y[vi],
                        verify_z[vi],
                        scene->screen_vertices_x[vi],
                        scene->screen_vertices_y[vi],
                        scene->screen_vertices_z[vi],
                        model_pitch,
                        model_yaw,
                        model_roll,
                        camera_roll,
                        (int)model_has_textures(hnd),
                        center_projection.z,
                        camera->near_plane_z);
                    assert(0 && "the two near-clip kernels disagreed");
                }
            }

            /* Restore the gated result: the rest of the frame must render from
             * the path production would actually have taken. */
            memcpy(scene->screen_vertices_x, verify_x, (size_t)vcount * sizeof(int));
            memcpy(scene->screen_vertices_y, verify_y, (size_t)vcount * sizeof(int));
            memcpy(scene->screen_vertices_z, verify_z, (size_t)vcount * sizeof(int));

            if( (compared_models % 100000) == 0 )
                fprintf(
                    stderr,
                    "near_clip_verify: %ld models compared both kernels, "
                    "%ld nudge-only divergences\n",
                    compared_models,
                    nudge_divergences);
        }
    }
#endif

    return TORIDRAW_CULL_VISIBLE;
}

/*
 * The stock entry: the prepared-camera kernels, with the portable ladder
 * behind them. What every caller got before the vtable existed, and what
 * ToriDraw_ProjectionKernelGetDefault still selects.
 */
static inline int
ToriDraw_Project(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera)
{
    return ToriDraw_ProjectWithVTable(
        scene, hnd, position, view_port, camera, &g_projection_prepared_vtable);
}

/**
 * Sign-only barycentric containment. Dividing all three coordinates by the
 * same `denominator` cannot change which of them are negative, so folding the
 * denominator's sign in and comparing the raw numerators against zero gives
 * the same answer with no divides and no float at all.
 *
 * This is the geometrically honest answer to "is the point on this triangle",
 * which is what ToriDraw_ProjectedModelContainsPoint promises. It is NOT what
 * the reference uses to pick a model under the cursor — see
 * toridraw_mouse_roughly_inside_triangle below.
 */
static inline bool
toridraw_triangle_contains_point(
    int x1,
    int y1,
    int x2,
    int y2,
    int x3,
    int y3,
    int x,
    int y)
{
    int denominator = (y2 - y3) * (x1 - x3) + (x3 - x2) * (y1 - y3);
    if( denominator == 0 )
        return false;

    int a_num = (y2 - y3) * (x - x3) + (x3 - x2) * (y - y3);
    int b_num = (y3 - y1) * (x - x3) + (x1 - x3) * (y - y3);
    int c_num = denominator - a_num - b_num; /* c = 1 - a - b, unnormalized */

    if( denominator < 0 )
    {
        a_num = -a_num;
        b_num = -b_num;
        c_num = -c_num;
    }

    return a_num >= 0 && b_num >= 0 && c_num >= 0;
}

/*
 * Which triangle predicate a pick walk uses. Three, because the reference has
 * three answers: a loose box for models, exact containment for ground, and —
 * ours alone — a true barycentric test for callers that want geometry rather
 * than parity (ToriDraw_ProjectedModelContainsPoint).
 */
enum ToriDrawPickTest
{
    TORIDRAW_PICKTEST_ROUGH = 0,          /* deob class144.method4915 */
    TORIDRAW_PICKTEST_REFERENCE_TILE = 1, /* deob class112.method4206 */
    TORIDRAW_PICKTEST_EXACT = 2,          /* not a reference rule */
};

/*
 * The GROUND pick's containment test, ported statement for statement from deob
 * class112.method4206 (reached as class155 -> method3946), which is also
 * Client-TS World3D.insideTriangle.
 *
 * Not the same routine as toridraw_triangle_contains_point above, and the
 * difference is not academic:
 *
 *  - The barycentric version divides the winding out through a `denominator`
 *    and REFUSES a triangle whose projected area is zero. The reference has no
 *    denominator: it takes the three edge cross-products and asks whether they
 *    share a sign, so a face that projects to a line — a tile seen edge-on,
 *    which is every wall of a pit and every far tile whose vertices round onto
 *    one row — still picks. Bailing on those is how a hole in the floor
 *    becomes unclickable.
 *  - Zero is on the inside of every comparison here, so a click exactly on a
 *    shared edge picks both tiles rather than falling between them.
 *
 * The int arithmetic is deliberate: the reference overflows the same products
 * on the same inputs, and a widened version would answer differently where it
 * does.
 */
static inline bool
toridraw_reference_triangle_contains_point(
    int x1,
    int y1,
    int x2,
    int y2,
    int x3,
    int y3,
    int x,
    int y)
{
    if( y < y1 && y < y2 && y < y3 )
        return false;
    if( y > y1 && y > y2 && y > y3 )
        return false;
    if( x < x1 && x < x2 && x < x3 )
        return false;
    if( x > x1 && x > x2 && x > x3 )
        return false;

    int e0 = (y - y1) * (x2 - x1) - (x - x1) * (y2 - y1);
    int e1 = (y - y2) * (x3 - x2) - (x - x2) * (y3 - y2);
    int e2 = (y - y3) * (x1 - x3) - (x - x3) * (y1 - y3);

    if( e0 != 0 )
        return e0 < 0 ? (e1 <= 0 && e2 <= 0) : (e1 >= 0 && e2 >= 0);
    if( e1 == 0 )
        return true;
    if( e1 < 0 )
        return e2 <= 0;
    return e2 >= 0;
}

/**
 * The reference's per-face hit test is NOT containment: it asks whether the
 * cursor, grown by `TORIDRAW_PICK_SLOP` in each direction, overlaps the
 * triangle's screen bounding box. Client-TS names it exactly what it is —
 * Model.isMouseRoughlyInsideTriangle (Model.ts:2413) — and the 239 deob
 * inlines the same four comparisons (class144.method4915:1625-1646) around a
 * slop of 5.
 *
 * The looseness is the whole point. A rock, a fence or a set of climbing
 * handholds is a handful of small triangles with gaps between them; exact
 * containment makes the gaps dead space and those locs nearly unclickable,
 * which is precisely the agility-obstacle complaint. Face bboxes overlap far
 * enough to fill the silhouette.
 */

static inline bool
toridraw_mouse_roughly_inside_triangle(
    int x1,
    int y1,
    int x2,
    int y2,
    int x3,
    int y3,
    int x,
    int y)
{
    if( y + TORIDRAW_PICK_SLOP < y1 && y + TORIDRAW_PICK_SLOP < y2 &&
        y + TORIDRAW_PICK_SLOP < y3 )
        return false;
    if( y - TORIDRAW_PICK_SLOP > y1 && y - TORIDRAW_PICK_SLOP > y2 &&
        y - TORIDRAW_PICK_SLOP > y3 )
        return false;
    if( x + TORIDRAW_PICK_SLOP < x1 && x + TORIDRAW_PICK_SLOP < x2 &&
        x + TORIDRAW_PICK_SLOP < x3 )
        return false;
    if( x - TORIDRAW_PICK_SLOP > x1 && x - TORIDRAW_PICK_SLOP > x2 &&
        x - TORIDRAW_PICK_SLOP > x3 )
        return false;
    return true;
}

bool
ToriDraw_ProjectedModelContainsAabb(
    struct ToriDraw_Scene* scene,
    int screen_x,
    int screen_y)
{
    struct ToriDraw_AABB* aabb = &scene->aabb;
    return screen_x >= aabb->min_screen_x && screen_x <= aabb->max_screen_x &&
           screen_y >= aabb->min_screen_y && screen_y <= aabb->max_screen_y;
}

/**
 * Shared face walk behind both per-face tests. Which faces are eligible is the
 * same question for any of them — only the triangle predicate differs, so
 * `test` picks between them at the bottom.
 *
 * `include_hidden` lifts the hidden-face filter for ground tiles, which the
 * reference picks down a different path entirely — a path that is also exact
 * rather than rough, so the two flags move together. See
 * ToriDraw_ProjectedTileMouseHitTest.
 */
static bool
toridraw_projected_model_hit_face(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_ViewPort* view_port,
    int screen_x,
    int screen_y,
    enum ToriDrawPickTest test,
    bool include_hidden)
{
    if( !ToriDraw_ProjectedModelContainsAabb(scene, screen_x, screen_y) )
        return false;

    int adjusted_screen_x = screen_x - view_port->x_center;
    int adjusted_screen_y = screen_y - view_port->y_center;

    faceint_t* fia = NULL;
    faceint_t* fib = NULL;
    faceint_t* fic = NULL;
    hsl16_t* colors_c = NULL;
    int face_count = 0;

    switch( hnd.kind )
    {
    case TORIDRAWMK_MODEL:
    case TORIDRAWMK_MODEL_HD:
    case TORIDRAWMK_MODEL_SHARED:
    case TORIDRAWMK_MODEL_LENT_FACES:
    {
        struct ToriDraw_Model* m = model_as_full(hnd);
        fia = m->face_indices_a;
        fib = m->face_indices_b;
        fic = m->face_indices_c;
        colors_c = m->face_colors_c;
        face_count = m->face_count;
        break;
    }
    default:
        return false;
    }

    for( int i = 0; i < face_count; i++ )
    {
        /*
         * Hidden faces do not pick. `faceColourC == -2` is the reference's one
         * hidden marker (class144.method4915:1575, Client-TS Model.ts:1856
         * spells the same thing as faceRenderType -1 because it hides on the
         * lit model instead), and ModelData.light stamps it for render type 2:
         * a fully transparent face, and every face mergeNormals removed where
         * two adjacent locs share all three vertices (class136:644/652). Here
         * that sentinel is TORIDRAWHSL16_HIDDEN, written by ToriDraw_Light.
         *
         * Note what is deliberately NOT filtered: the reference runs this pick
         * *after* computing the winding cull and ignores it (class144:1616 vs
         * :1618), so a backfacing face still picks, and so does a face the
         * depth sort later drops.
         */
        if( !include_hidden && colors_c && colors_c[i] == TORIDRAWHSL16_HIDDEN )
            continue;

        int face_a = fia[i];
        int face_b = fib[i];
        int face_c = fic[i];

        int x1 = scene->screen_vertices_x[face_a];
        int x2 = scene->screen_vertices_x[face_b];
        int x3 = scene->screen_vertices_x[face_c];

        /*
         * Near-clipped faces do not pick either (the reference reaches its
         * pick only down the not-near-clipped arm, class144:1615). Mandatory
         * rather than cosmetic for the rough test: the projection parks a
         * behind-the-eye vertex at screen x -5000 and leaves its y
         * *undivided*, so a bounding box over it spans the screen and would
         * swallow every click behind the model.
         *
         * Gated on near_clipped exactly as the reference gates on `clipped`
         * (Model.render2:1876). Not just a saving: when the flag is clear the
         * projection ran its no-clip kernel, which skips the -5001 nudge, so a
         * legitimately projected -5000 is possible and testing for it here
         * would drop a pickable face.
         */
        if( scene->near_clipped &&
            (x1 == TORIDRAW_SCREEN_X_NEAR_CLIPPED || x2 == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
             x3 == TORIDRAW_SCREEN_X_NEAR_CLIPPED) )
            continue;

        int y1 = scene->screen_vertices_y[face_a];
        int y2 = scene->screen_vertices_y[face_b];
        int y3 = scene->screen_vertices_y[face_c];

        bool hit;
        switch( test )
        {
        case TORIDRAW_PICKTEST_ROUGH:
            hit = toridraw_mouse_roughly_inside_triangle(
                x1, y1, x2, y2, x3, y3, adjusted_screen_x, adjusted_screen_y);
            break;
        case TORIDRAW_PICKTEST_REFERENCE_TILE:
            hit = toridraw_reference_triangle_contains_point(
                x1, y1, x2, y2, x3, y3, adjusted_screen_x, adjusted_screen_y);
            break;
        default:
            hit = toridraw_triangle_contains_point(
                x1, y1, x2, y2, x3, y3, adjusted_screen_x, adjusted_screen_y);
            break;
        }
        if( hit )
            return true;
    }

    return false;
}

bool
ToriDraw_ProjectedModelContainsPoint(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_ViewPort* view_port,
    int screen_x,
    int screen_y)
{
    return toridraw_projected_model_hit_face(
        scene, hnd, view_port, screen_x, screen_y, TORIDRAW_PICKTEST_EXACT,
        /* include_hidden */ false);
}

bool
ToriDraw_ProjectedModelMouseHitTest(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_ViewPort* view_port,
    int screen_x,
    int screen_y)
{
    return toridraw_projected_model_hit_face(
        scene, hnd, view_port, screen_x, screen_y, TORIDRAW_PICKTEST_ROUGH,
        /* include_hidden */ false);
}

bool
ToriDraw_ProjectedTileMouseHitTest(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_ViewPort* view_port,
    int screen_x,
    int screen_y)
{
    /* The ground pick's own reference routine — containment, not the model
     * test's slop, and NOT the barycentric one, which refuses a face that
     * projects to zero area. See toridraw_reference_triangle_contains_point. */
    return toridraw_projected_model_hit_face(
        scene, hnd, view_port, screen_x, screen_y, TORIDRAW_PICKTEST_REFERENCE_TILE,
        /* include_hidden */ true);
}
