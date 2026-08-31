#include "graphics/batch_stats.h"
#include "graphics/dash_restrict.h"
#include "graphics/div3.h"
#include "graphics/proj_census.h"
#include "impl/projection/projection.scalar_reference.h"
#include "graphics/winding.h"
#include "graphics/ysort_order.h"
#include "toridraw_math.h"
#include "toridraw_model_internal.h"
#include "toridraw_raster_batch.h"
#include "toridraw_raster_kernel.h"
#include "toridraw_types.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef VERTEXINT_BITS
#define VERTEXINT_BITS 16
#endif

// clang-format off
#include "impl/projection/projection.perspective.plain.dispatch.u.c"
#include "impl/projection/zdiv/projection.zdiv.dispatch.u.c"
/* The per-ISA screen-box sweep behind toridraw_projected_bound. */
#include "impl/projection/projection.bound.dispatch.u.c"
// clang-format on

/* The prepared-camera lane -- which ISA supplies one, and the two hooks the
 * perspective slots below call. The lane owns the entry points (the AArch64
 * assembly, the SSE2 family) and the inline attributes those slots carry, so
 * nothing here tests the architecture. */
#include "impl/projection/projection.perspective.prepared.dispatch.u.c"

/* toridraw_ignore_priorities(), the depth-sort override the face walk reads.
 * toridraw.c includes this file ahead of this one and it carries an include
 * guard, so this is a no-op in the unity build -- it is here so the file states
 * its own dependency and parses standalone, which is what the editor does. */
#include "triangles/toridraw_triangle_clip.u.c"

/** Far plane for bounding-cylinder frustum cull. */
// #define TORIDRAW_CYLINDER_FAR_PLANE_Z 3500
#define TORIDRAW_CYLINDER_FAR_PLANE_Z 7500

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
 * Everything that exists only to describe a render or a raster pass: the
 * TORIDRAW_SORT_DEBUG and TORIDRAW_RASTER_DEBUG counters and printers, the
 * NDJSON record builders, and the two verification harnesses. None of it is
 * compiled at all in a default build, and what is left at the sites below is a
 * macro that expands to a constant or to nothing.
 *
 * Here rather than higher up: it needs the model accessors,
 * div3_fast_fixedpoint and TORIDRAW_PROJECTED_COORD_LIMIT above, and every
 * site that uses it is below.
 */
#include "toridraw_debug.u.c"

/* sm_depth_offset_all_zero(): the assert-only invariant check the CSR sort
 * below leans on. Its own file for the same reason as the block above -- it is
 * debug code -- but on the plain NDEBUG gate, not the stats gate, because an
 * ordinary debug build has to have it. */
#include "toridraw_render_invariants.u.c"

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
    int const scale = toridraw_projection_scale_from_cot16(camera_cot16);
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
 * Can any vertex of this model land at or past `near_plane_z`?
 *
 * Shared by both projection families because it is geometry, not policy: the
 * families disagree about WHICH plane to test and what else forces the
 * clipping kernel, never about this test. `min_z_depth_any_rotation` is the
 * radius of a sphere about the model origin containing every vertex
 * (toridraw_model_transform.c:736), and a sphere is the right bound precisely
 * because it is rotation-invariant -- model pitch/yaw/roll and camera
 * pitch/yaw/roll all rotate about that origin, so this one test covers the
 * 6DOF and pitch+yaw paths as well as the yaw-only one. The reference's
 * `radiusZ` (Client-TS Model.worldRender:1755) cannot: it folds in cos/sin of
 * the camera pitch and so is only valid for worldRender's yaw-only models.
 *
 * `<` and not `<=`, and a bound-less model answers true: both errors have to
 * fall on the "may clip" side, because the no-clip kernels divide by z
 * unconditionally and a vertex that sneaks below the plane is a sign-flipped
 * projection, or a SIGFPE at z == 0.
 */
static inline bool
toridraw_model_reaches_near_plane(
    const struct ToriDraw_BoundsCylinder* bounds,
    const struct ProjectedVertex* center,
    int near_plane_z)
{
    assert(center);
    if( !bounds )
        return true;
    return center->z - bounds->min_z_depth_any_rotation < near_plane_z;
}

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
        cot15 = toridraw_projection_cot16(
                    camera->projection_mode, camera->projection_scale, camera->fov_rpi2048) >>
                1;
    }

    /*
     * The model's origin in camera space: project_orthographic_fast on the
     * point (0, 0, 0). The model rotation of the zero vector is the zero
     * vector, so no model-yaw trig is read and only the camera's two
     * rotations of the translate remain -- the same expressions, the same
     * shifts, in the same order, so the result is the one the general
     * routine gives (projection.u.c project_orthographic_fast_trig).
     */
    int const x_scene = (scene_x * cos_camera_yaw + scene_z * sin_camera_yaw) >> 16;
    int const z_scene = (scene_z * cos_camera_yaw - scene_x * sin_camera_yaw) >> 16;
    projected_vertex->x = x_scene;
    projected_vertex->y = (scene_y * cos_camera_pitch - z_scene * sin_camera_pitch) >> 16;
    projected_vertex->z = (scene_y * sin_camera_pitch + z_scene * cos_camera_pitch) >> 16;

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

    bool const parallel = toridraw_projection_is_parallel(camera->projection_mode);
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
#define TORIDRAW_FASTCULL_Y_EXTENT()                                                               \
    int model_center_to_top_edge = bc->center_to_top_edge;                                         \
    int model_center_to_bottom_edge = (bc->center_to_bottom_edge * cos_camera_pitch >> 16) +       \
                                      (model_edge_radius * sin_camera_pitch >> 16);                \
    int ortho_screen_y_min = mid_y - abs(model_center_to_bottom_edge);                             \
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
 * orbit (graphics/proj_census.h, TORIDRAW_PROJECTION_CENSUS_AABB) put the corner
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

#include "impl/facesort/facesort.bucket.full.scalar.u.c"


#include "impl/facesort/facesort.bucket.small.scalar.u.c"


#include "impl/facesort/facesort.dispatch.u.c"

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
            TORIDRAW_PROJECTION_CENSUS_RECORD(
                TORIDRAW_PROJECTION_K_6DOF_TEX, 1, model_vertex_count(hnd));
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
                toridraw_projection_cot16(
                    camera->projection_mode, camera->projection_scale, camera->fov_rpi2048),
                camera->pitch,
                camera->yaw,
                camera_roll);
        }
        else
        {
            TORIDRAW_PROJECTION_CENSUS_RECORD(
                TORIDRAW_PROJECTION_K_6DOF_NOTEX, 1, model_vertex_count(hnd));
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
                toridraw_projection_cot16(
                    camera->projection_mode, camera->projection_scale, camera->fov_rpi2048),
                camera->pitch,
                camera->yaw,
                camera_roll);
        }
    }
    else if( model_pitch != 0 )
    {
        if( model_has_textures(hnd) )
        {
            TORIDRAW_PROJECTION_CENSUS_RECORD(
                TORIDRAW_PROJECTION_K_PITCHYAW_TEX, 1, model_vertex_count(hnd));
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
                toridraw_projection_cot16(
                    camera->projection_mode, camera->projection_scale, camera->fov_rpi2048),
                camera->pitch,
                camera->yaw);
        }
        else
        {
            TORIDRAW_PROJECTION_CENSUS_RECORD(
                TORIDRAW_PROJECTION_K_PITCHYAW_NOTEX, 1, model_vertex_count(hnd));
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
                toridraw_projection_cot16(
                    camera->projection_mode, camera->projection_scale, camera->fov_rpi2048),
                camera->pitch,
                camera->yaw);
        }
    }
    else if( model_has_textures(hnd) )
    {
        TORIDRAW_PROJECTION_CENSUS_RECORD(
            TORIDRAW_PROJECTION_K_YAW_TEX, 1, model_vertex_count(hnd));
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
            toridraw_projection_cot16(
                camera->projection_mode, camera->projection_scale, camera->fov_rpi2048),
            camera->pitch,
            camera->yaw);
    }
    else
    {
        TORIDRAW_PROJECTION_CENSUS_RECORD(
            TORIDRAW_PROJECTION_K_YAW_NOTEX, 1, model_vertex_count(hnd));
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
            toridraw_projection_cot16(
                camera->projection_mode, camera->projection_scale, camera->fov_rpi2048),
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
 * cannot take -- pitched, rolled, drawn under a camera whose prepared block
 * was never published, or simply not a shape this build's lane implements.
 * That fallback is the reason this is a superset of `_portable` and not an
 * alternative to it.
 *
 * Which lane answers is graphics/projection_prepared.u.c's business, and the
 * hook says only whether it took the model; see projection_prepared.h.
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
    if( toridraw_projection_prepared_eligible(
            scene, camera, model_pitch, model_roll, camera_roll) &&
        toridraw_projection_prepared_clip(scene, hnd, position, camera, model_yaw, model_mid_z) )
        return;

    toridraw_project_vertices_clip_portable(
        scene, hnd, position, camera, model_pitch, model_yaw, model_roll, camera_roll, model_mid_z);
}

/* Models that provably cannot; no sentinel, no near-plane test. */
TORIDRAW_PROJECTION_SLOT_NEVER_INLINE
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
            TORIDRAW_PROJECTION_CENSUS_RECORD(
                TORIDRAW_PROJECTION_K_6DOF_TEX, 0, model_vertex_count(hnd));
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
                toridraw_projection_cot16(
                    camera->projection_mode, camera->projection_scale, camera->fov_rpi2048),
                camera->pitch,
                camera->yaw,
                camera_roll);
        }
        else
        {
            TORIDRAW_PROJECTION_CENSUS_RECORD(
                TORIDRAW_PROJECTION_K_6DOF_NOTEX, 0, model_vertex_count(hnd));
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
                toridraw_projection_cot16(
                    camera->projection_mode, camera->projection_scale, camera->fov_rpi2048),
                camera->pitch,
                camera->yaw,
                camera_roll);
        }
    }
    else if( model_pitch != 0 )
    {
        if( model_has_textures(hnd) )
        {
            TORIDRAW_PROJECTION_CENSUS_RECORD(
                TORIDRAW_PROJECTION_K_PITCHYAW_TEX, 0, model_vertex_count(hnd));
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
                toridraw_projection_cot16(
                    camera->projection_mode, camera->projection_scale, camera->fov_rpi2048),
                camera->pitch,
                camera->yaw);
        }
        else
        {
            TORIDRAW_PROJECTION_CENSUS_RECORD(
                TORIDRAW_PROJECTION_K_PITCHYAW_NOTEX, 0, model_vertex_count(hnd));
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
                toridraw_projection_cot16(
                    camera->projection_mode, camera->projection_scale, camera->fov_rpi2048),
                camera->pitch,
                camera->yaw);
        }
    }
    else if( model_has_textures(hnd) )
    {
        TORIDRAW_PROJECTION_CENSUS_RECORD(
            TORIDRAW_PROJECTION_K_YAW_TEX, 0, model_vertex_count(hnd));
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
            toridraw_projection_cot16(
                camera->projection_mode, camera->projection_scale, camera->fov_rpi2048),
            camera->pitch,
            camera->yaw);
    }
    else
    {
        TORIDRAW_PROJECTION_CENSUS_RECORD(
            TORIDRAW_PROJECTION_K_YAW_NOTEX, 0, model_vertex_count(hnd));
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
            toridraw_projection_cot16(
                camera->projection_mode, camera->projection_scale, camera->fov_rpi2048),
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
TORIDRAW_PROJECTION_SLOT_ALWAYS_INLINE
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
    if( toridraw_projection_prepared_eligible(
            scene, camera, model_pitch, model_roll, camera_roll) &&
        toridraw_projection_prepared_noclip(scene, hnd, position, camera, model_yaw, model_mid_z) )
        return;

    toridraw_project_vertices_noclip_portable(
        scene, hnd, position, camera, model_pitch, model_yaw, model_roll, camera_roll, model_mid_z);
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
                zoom16,
                camera->pitch,
                camera->yaw,
                camera_roll);
        else
            project_vertices_array_ortho6_fused_notex_clip(
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
                zoom16,
                camera->pitch,
                camera->yaw,
                camera_roll);
    }
    else if( model_has_textures(hnd) )
    {
        project_vertices_array_ortho_fused_clip(
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
            zoom16,
            camera->pitch,
            camera->yaw);
    }
    else
    {
        project_vertices_array_ortho_fused_notex_clip(
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
            zoom16,
            camera->pitch,
            camera->yaw);
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
                zoom16,
                camera->pitch,
                camera->yaw,
                camera_roll);
        else
            project_vertices_array_ortho6_fused_notex_noclip(
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
                zoom16,
                camera->pitch,
                camera->yaw,
                camera_roll);
    }
    else if( model_has_textures(hnd) )
    {
        project_vertices_array_ortho_fused_noclip(
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
            zoom16,
            camera->pitch,
            camera->yaw);
    }
    else
    {
        project_vertices_array_ortho_fused_notex_noclip(
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
            zoom16,
            camera->pitch,
            camera->yaw);
    }
}

/*
 * The perspective near-clip rule.
 *
 * Two things only this family knows. First, it divides by z, so a vertex that
 * survives at a very small z projects to `coord * scale / z` -- unbounded, and
 * past TORIDRAW_PROJECTED_COORD_LIMIT it wraps every edge stepper it touches.
 * toridraw_safe_near_plane_z raises the plane far enough to bound that, and
 * everything downstream clips against the raised value rather than the
 * camera's own.
 *
 * Second, a near plane below 1 would admit a zero or negative divisor, so the
 * clipping kernel has to take over whatever the model's bound says. That is a
 * safety rule about division, which is why it lives here and not in the shell:
 * the parallel family has no divisor to protect and would be paying for the
 * more expensive kernel for nothing.
 */
static void
toridraw_projection_near_clip_perspective(
    const struct ToriDraw_Camera* camera,
    const struct ToriDraw_BoundsCylinder* bounds,
    const struct ProjectedVertex* center,
    int* out_near_plane_z,
    bool* out_may_clip)
{
    assert(camera);
    assert(center);
    assert(out_near_plane_z);
    assert(out_may_clip);

    int const near_plane_z = toridraw_safe_near_plane_z(
        bounds,
        center,
        toridraw_projection_cot16(
            camera->projection_mode, camera->projection_scale, camera->fov_rpi2048),
        camera->near_plane_z);

    *out_near_plane_z = near_plane_z;
    *out_may_clip =
        camera->near_plane_z < 1 || toridraw_model_reaches_near_plane(bounds, center, near_plane_z);
}

/*
 * The parallel near-clip rule.
 *
 * Nothing here divides by z, so no vertex can leave the 16.16 domain however
 * close it comes and there is no plane to raise -- the camera's value stands.
 * For the same reason a near plane behind the camera is a perfectly ordinary
 * request rather than a hazard: it is how a map editor says "never hide
 * anything", and such a camera correctly never takes the clipping kernel at
 * all.
 *
 * So the gate here means only "is anything near enough the view plane to need
 * hiding", where under perspective it also meant "is anything about to be
 * divided by a number too small to survive".
 */
static void
toridraw_projection_near_clip_parallel(
    const struct ToriDraw_Camera* camera,
    const struct ToriDraw_BoundsCylinder* bounds,
    const struct ProjectedVertex* center,
    int* out_near_plane_z,
    bool* out_may_clip)
{
    assert(camera);
    assert(center);
    assert(out_near_plane_z);
    assert(out_may_clip);

    *out_near_plane_z = camera->near_plane_z;
    *out_may_clip = toridraw_model_reaches_near_plane(bounds, center, camera->near_plane_z);
}

/*
 * The stock projection families, and the two vtables that name them. One file
 * each, in families/.
 *
 * Here rather than beside the kernels/ files that wrap them: a family is data
 * about the static functions above, so it can only be written once those are
 * in scope, and ToriDraw_Project below reads the prepared vtable directly.
 * kernels/projection.{prepared,portable}.u.c, included later from toridraw.c,
 * turn these two vtables into the ToriDraw_ProjectionKernel objects a caller
 * selects.
 *
 * Parallel first: it is the shared slot the other two name.
 */
// clang-format off
#include "families/projection.parallel.u.c"
#include "families/projection.perspective_prepared.u.c"
#include "families/projection.perspective_portable.u.c"
// clang-format on

/*
 * Project one model, dispatching its vertices through `vtable`.
 *
 * Everything here -- the capacity refusal, the fast cull, the screen box, the
 * AABB cull -- is the same whichever projection kernel was selected, and
 * whichever family the camera resolves to. What is NOT the same is the near
 * plane and the near-clip gate: this shell reads the camera once to pick a
 * family and then asks that family for both, rather than spelling one
 * family's rules out and excepting the other from them.
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
    if( model_vertex_count(hnd) > scene->max_vertices || model_face_count(hnd) > scene->max_faces )
    {
        TORIDRAW_DBG_CAPACITY_REJECT(scene, hnd);
        return TORIDRAW_CULL_ERROR;
    }

    /* Surface an undersized depth table before fast/AABB culling can hide the
     * model from the face-sort diagnostics.  The bound is deliberately
     * rotation-independent: every sorted face depth lies within [-bias,+bias]
     * before the sorter adds bias, hence the required diameter is 2*bias+1. */
    TORIDRAW_DBG_DEPTH_CAPACITY(scene, hnd);

    int cull = TORIDRAW_CULL_VISIBLE;

    cull = ToriDraw_FastCull(scene, view_port, hnd, position, camera, &center_projection);
    if( cull != TORIDRAW_CULL_VISIBLE )
    {
        if( cull == TORIDRAW_CULL_ERROR )
            TORIDRAW_PROJECTION_CENSUS_COUNT(cull_error);
        else
            TORIDRAW_PROJECTION_CENSUS_COUNT(cull_fast);
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
     * How that question is asked belongs to the family that answers it; see
     * toridraw_projection_near_clip_perspective / _parallel above and the
     * bound they share, toridraw_model_reaches_near_plane.
     */
    struct ToriDraw_BoundsCylinder const* const proj_bc = model_bounds_cylinder(hnd);

    /*
     * Which family, and then everything the family knows. The camera read
     * below is the only projection-shape question this shell answers itself;
     * the near plane everything downstream clips against and the gate that
     * picks between the family's two vertex kernels are the family's to
     * answer, because both have a different meaning under a projection that
     * divides by z than under one that does not.
     */
    struct ToriDraw_ProjectionFamily const* const family =
        toridraw_projection_is_parallel(camera->projection_mode) ? vtable->parallel
                                                                 : vtable->perspective;
    assert(family);
    assert(family->near_clip);

    bool may_clip;
    family->near_clip(
        camera, proj_bc, &center_projection, &scene->projection_near_plane_z, &may_clip);

#ifdef TORIDRAW_NEAR_CLIP_FORCE_ALL
    /* Build with -DTORIDRAW_NEAR_CLIP_FORCE_ALL=1 to send every model down the
     * clipping kernel, i.e. the behaviour from before the gate existed. Frames
     * rendered by the two builds must be byte-identical: that equality is the
     * whole correctness argument for the gate, so keep this switch working. */
    may_clip = true;
#endif

    TORIDRAW_DBG_NEAR_CLIP_GATE(may_clip);

    scene->near_clipped = may_clip;
    /* Only the prepared kernels (AArch64 assembly, SSE2 fused-yaw) fill the
     * bound block; every other path leaves this zero and the bound is swept
     * from the outputs. */
    scene->projection_bound_vertices = 0;

    /* The slot the family's own gate selected. */
    assert(family->project[may_clip]);
    family->project[may_clip](
        scene,
        hnd,
        position,
        camera,
        model_pitch,
        model_yaw,
        model_roll,
        camera_roll,
        center_projection.z);

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
    TORIDRAW_PROJECTION_CENSUS_AABB(bound_vertex_count, cull != TORIDRAW_CULL_VISIBLE);
    if( cull != TORIDRAW_CULL_VISIBLE )
    {
        TORIDRAW_PROJECTION_CENSUS_COUNT(cull_aabb);
        return cull;
    }

    TORIDRAW_PROJECTION_CENSUS_COUNT(projected);

    TORIDRAW_DBG_PROJECTION_PRINT(
        scene, hnd, position, view_port, camera, center_projection.z, may_clip);

    /* The gate's correctness argument, on real scene data: both kernels run
     * over the same model inside one process and the results are compared.
     * Compiled out unless TORIDRAW_NEAR_CLIP_STATS. */
    TORIDRAW_DBG_VERIFY_NEAR_CLIP(
        scene,
        hnd,
        position,
        camera,
        &center_projection,
        proj_bc,
        may_clip,
        model_pitch,
        model_yaw,
        model_roll,
        camera_roll);

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
    if( y + TORIDRAW_PICK_SLOP < y1 && y + TORIDRAW_PICK_SLOP < y2 && y + TORIDRAW_PICK_SLOP < y3 )
        return false;
    if( y - TORIDRAW_PICK_SLOP > y1 && y - TORIDRAW_PICK_SLOP > y2 && y - TORIDRAW_PICK_SLOP > y3 )
        return false;
    if( x + TORIDRAW_PICK_SLOP < x1 && x + TORIDRAW_PICK_SLOP < x2 && x + TORIDRAW_PICK_SLOP < x3 )
        return false;
    if( x - TORIDRAW_PICK_SLOP > x1 && x - TORIDRAW_PICK_SLOP > x2 && x - TORIDRAW_PICK_SLOP > x3 )
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
        scene,
        hnd,
        view_port,
        screen_x,
        screen_y,
        TORIDRAW_PICKTEST_EXACT,
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
        scene,
        hnd,
        view_port,
        screen_x,
        screen_y,
        TORIDRAW_PICKTEST_ROUGH,
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
        scene,
        hnd,
        view_port,
        screen_x,
        screen_y,
        TORIDRAW_PICKTEST_REFERENCE_TILE,
        /* include_hidden */ true);
}
