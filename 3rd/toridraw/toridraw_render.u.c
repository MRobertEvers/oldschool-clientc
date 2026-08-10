#include <stdlib.h>
#include "graphics/dash_restrict.h"
#include "graphics/projection.h"
#include "toridraw_math.h"
#include "toridraw_model_internal.h"
#include "toridraw_types.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef VERTEXINT_BITS
#define VERTEXINT_BITS 16
#endif

/* #region agent log */
#define TORIDRAW_DBG_LOG_PATH \
    "/Users/matthewevers/Documents/git_repos/3draster/.cursor/debug-ef81cb.log"
#define TORIDRAW_DBG_SESSION "ef81cb"
#define TORIDRAW_DBG_BUDGET  6000

static int
toridraw_dbg_enabled(void)
{
    static int on = -1;
    if( on < 0 )
    {
        const char* v = getenv("TORIDRAW_DEBUG_NDJSON");
        on = (v && v[0] != '\0' && v[0] != '0') ? 1 : 0;
    }
    return on;
}

/** TORIDRAW_DEBUG_LOG=<path> overrides the sink. The default above is a macOS
 *  absolute path, so on any other host fopen fails and every record is dropped
 *  silently - which reads exactly like "the instrumentation says nothing is
 *  wrong". */
static const char*
toridraw_dbg_log_path(void)
{
    static const char* path = NULL;
    if( !path )
    {
        const char* v = getenv("TORIDRAW_DEBUG_LOG");
        path = (v && v[0] != '\0') ? v : TORIDRAW_DBG_LOG_PATH;
    }
    return path;
}

static const char*
toridraw_dbg_run_id(void)
{
    static const char* run = NULL;
    if( !run )
    {
        const char* v = getenv("TORIDRAW_DEBUG_RUN");
        run = (v && v[0] != '\0') ? v : "run1";
    }
    return run;
}

/** Emit at most one record per anomaly burst: `state` counts calls at this
 *  site, the first few always pass, then one in `period`. */
static bool
toridraw_dbg_gate(int* state, int period)
{
    int const n = (*state)++;
    return n < 4 || (period > 0 && (n % period) == 0);
}

static void
toridraw_dbg_log(
    const char* hypothesis,
    const char* location,
    const char* message,
    const char* data_json)
{
    static int budget = TORIDRAW_DBG_BUDGET;
    FILE* f;

    if( !toridraw_dbg_enabled() || budget <= 0 )
        return;
    budget--;

    f = fopen(toridraw_dbg_log_path(), "a");
    if( !f )
        return;
    fprintf(
        f,
        "{\"sessionId\":\"" TORIDRAW_DBG_SESSION "\",\"runId\":\"%s\","
        "\"hypothesisId\":\"%s\",\"location\":\"%s\",\"message\":\"%s\","
        "\"data\":%s,\"timestamp\":%lld}\n",
        toridraw_dbg_run_id(),
        hypothesis,
        location,
        message,
        data_json,
        (long long)time(NULL) * 1000);
    fclose(f);
}
/* #endregion */

// clang-format off
#include "graphics/projection16_simd.u.c"
#include "graphics/projection_zdiv_simd.u.c"
// clang-format on

/** Far plane for bounding-cylinder frustum cull. */
// #define TORIDRAW_CYLINDER_FAR_PLANE_Z 3500
#define TORIDRAW_CYLINDER_FAR_PLANE_Z 7500

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
/* #region agent log */
/** TORIDRAW_SAFE_NEAR=0 restores the camera's own near plane, so one session
 *  can A/B whether the raised plane is what removes near geometry. */
static int
toridraw_safe_near_enabled(void)
{
    static int on = -1;
    if( on < 0 )
    {
        const char* v = getenv("TORIDRAW_SAFE_NEAR");
        on = (v && v[0] == '0') ? 0 : 1;
    }
    return on;
}
/* #endregion */

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

    /* #region agent log */
    if( !toridraw_safe_near_enabled() )
        return camera_near_plane_z;
    /* #endregion */

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

struct ToriDraw_FaceSortDebugStats
{
    int front_facing;
    int back_facing;
    int degenerate;
    int accepted;
    int depth_out_low;
    int depth_out_high;
    int bucket_overflow;
    int min_depth_seen;
    int max_depth_seen;
    int max_bucket_occupancy;
    int near_clip_candidates;
    int first_depth_out_face;
    int first_depth_out_value;
};

/** TORIDRAW_SORT_DEBUG is intentionally runtime-selectable: the counters below
 * remain disabled on the normal render path, while an ASan reproduction can
 * turn them on without rebuilding a multi-minute client.
 * Pair with TORIDRAW_RASTER_DEBUG (toridraw_raster.u.c) to see per-face skip
 * reasons (hidden type, HIDDEN sentinel, alpha, near-clip, texture miss). */
static inline int
toridraw_sort_debug_level(void)
{
    static int level = -1;
    if( level < 0 )
    {
        const char* value = getenv("TORIDRAW_SORT_DEBUG");
        if( !value || value[0] == '\0' || value[0] == '0' )
            level = 0;
        else if( strcmp(value, "all") == 0 || strcmp(value, "verbose") == 0 ||
                 strcmp(value, "2") == 0 )
            level = 2;
        else
            level = 1;
    }
    return level;
}

static inline bool
toridraw_sort_debug_enabled(void)
{
    /* #region agent log */
    if( toridraw_dbg_enabled() )
        return true;
    /* #endregion */
    return toridraw_sort_debug_level() != 0;
}

static inline void
toridraw_face_sort_debug_init(struct ToriDraw_FaceSortDebugStats* stats)
{
    memset(stats, 0, sizeof(*stats));
    stats->min_depth_seen = INT_MAX;
    stats->max_depth_seen = INT_MIN;
    stats->first_depth_out_face = -1;
    stats->first_depth_out_value = -1;
}

static inline void
toridraw_face_sort_debug_print(
    const struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    const struct ToriDraw_FaceSortDebugStats* stats,
    int ordered)
{
    const struct ToriDraw_BoundsCylinder* bounds = model_bounds_cylinder(hnd);
    int const depth_bias = bounds ? bounds->min_z_depth_any_rotation : 0;
    int const xz_radius = bounds ? bounds->radius : 0;
    int const min_y = bounds ? bounds->min_y : 0;
    int const max_y = bounds ? bounds->max_y : 0;
    long long const conservative_levels = (long long)depth_bias * 2 + 1;
    int const depth_min = stats->min_depth_seen == INT_MAX ? -1 : stats->min_depth_seen;
    int const depth_max = stats->max_depth_seen == INT_MIN ? -1 : stats->max_depth_seen;

    /* #region agent log */
    if( toridraw_dbg_enabled() )
    {
        static int gate_anomaly = 0;
        static int gate_clean = 0;
        bool const anomaly = stats->depth_out_low || stats->depth_out_high ||
                             stats->bucket_overflow || ordered != stats->accepted;
        bool const big = model_face_count(hnd) >= 2000;
        if( (anomaly && toridraw_dbg_gate(&gate_anomaly, 200)) ||
            (!anomaly && big && toridraw_dbg_gate(&gate_clean, 400)) )
        {
            char data[768];
            snprintf(
                data,
                sizeof(data),
                "{\"model\":\"%p\",\"vertices\":%d,\"faces\":%d,\"max_vertices\":%d,"
                "\"max_faces\":%d,\"depth_bias\":%d,\"required_levels\":%lld,"
                "\"depth_levels\":%d,\"depth_stride\":%d,\"observed_min\":%d,"
                "\"observed_max\":%d,\"front\":%d,\"near_clip_cand\":%d,\"accepted\":%d,"
                "\"ordered\":%d,\"back\":%d,\"degenerate\":%d,\"out_low\":%d,"
                "\"out_high\":%d,\"bucket_overflow\":%d,\"max_bucket\":%d}",
                (void*)model_as_full(hnd),
                model_vertex_count(hnd),
                model_face_count(hnd),
                scene->max_vertices,
                scene->max_faces,
                depth_bias,
                conservative_levels,
                scene->depth_levels,
                scene->depth_stride,
                depth_min,
                depth_max,
                stats->front_facing,
                stats->near_clip_candidates,
                stats->accepted,
                ordered,
                stats->back_facing,
                stats->degenerate,
                stats->depth_out_low,
                stats->depth_out_high,
                stats->bucket_overflow,
                stats->max_bucket_occupancy);
            toridraw_dbg_log(
                "C",
                "toridraw_render.u.c:face_sort",
                anomaly ? "face sort dropped or reordered faces" : "face sort clean",
                data);
        }
    }
    /* #endregion */

    /* Level 1 is an anomaly detector suitable for a live client.  `all` (or
     * level 2) emits clean models too when comparing an exact reproduction. */
    if( toridraw_sort_debug_level() < 2 && stats->depth_out_low == 0 &&
        stats->depth_out_high == 0 && stats->bucket_overflow == 0 &&
        ordered == stats->accepted )
        return;

    fprintf(
        stderr,
        "sort_depth: model=%p vertices=%d/%d faces=%d/%d "
        "bounds={y=%d..%d,xz=%d,bias=%d} "
        "bound_levels=%lld depth_levels=%d depth_stride=%d observed=%d..%d "
        "front=%d near_clip=%d accepted=%d ordered=%d back=%d degenerate=%d "
        "out_low=%d out_high=%d bucket_overflow=%d max_bucket=%d "
        "first_out_face=%d first_out_depth=%d\n",
        (void*)model_as_full(hnd),
        model_vertex_count(hnd),
        scene->max_vertices,
        model_face_count(hnd),
        scene->max_faces,
        min_y,
        max_y,
        xz_radius,
        depth_bias,
        conservative_levels,
        scene->depth_levels,
        scene->depth_stride,
        depth_min,
        depth_max,
        stats->front_facing,
        stats->near_clip_candidates,
        stats->accepted,
        ordered,
        stats->back_facing,
        stats->degenerate,
        stats->depth_out_low,
        stats->depth_out_high,
        stats->bucket_overflow,
        stats->max_bucket_occupancy,
        stats->first_depth_out_face,
        stats->first_depth_out_value);
}

static inline void
toridraw_projection_debug_print(
    const struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    const struct ToriDraw_Position* position,
    const struct ToriDraw_ViewPort* view_port,
    const struct ToriDraw_Camera* camera,
    int center_z,
    bool may_clip)
{
    const struct ToriDraw_Model* model = model_as_full(hnd);
    int min_x = INT_MAX;
    int max_x = INT_MIN;
    int min_y = INT_MAX;
    int max_y = INT_MIN;
    int min_z = INT_MAX;
    int max_z = INT_MIN;
    int clipped_vertices = 0;
    int fixed16_vertices = 0;
    int fixed16_faces = 0;
    int clipped_faces = 0;
    long long max_abs_edge_dx = 0;
    /* #region agent log */
    /* Exact-arithmetic differential against whatever kernel actually ran. The
     * perspective kernels all compute `x_scene * (cot16 >> 1) >> 6`, which is
     * mathematically `x_scene * scale` but forms a product six bits larger on
     * the way. In the NEON path that product is a vmulq_s32 lane, which wraps
     * silently and which no sanitizer instruments. A wrapped lane yields a
     * small but wrong screen coordinate, so only a comparison against exact
     * arithmetic can see it. */
    int const dbg_cot15 =
        toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048) >> 1;
    int const dbg_ortho_limit = dbg_cot15 > 0 ? INT_MAX / dbg_cot15 : INT_MAX;
    bool const dbg_yaw_only = !toridraw_proj_is_parallel(camera->proj_mode) &&
                              ToriDraw_NormalizeAngle(position->pitch) == 0 &&
                              ToriDraw_NormalizeAngle(position->roll) == 0 &&
                              ToriDraw_NormalizeAngle(camera->roll) == 0;
    int ortho_overflow = 0;
    int screen_mismatch = 0;
    long long worst_screen_delta = 0;
    int worst_ortho_coord = 0;
    /* #endregion */

    if( !toridraw_sort_debug_enabled() )
        return;

    /* #region agent log */
    if( dbg_yaw_only )
    {
        int const near_z = scene->projection_near_plane_z;
        int const yaw = ToriDraw_NormalizeAngle(position->yaw);
        const vertexint_t* mvx = model_vertices_x(hnd);
        const vertexint_t* mvy = model_vertices_y(hnd);
        const vertexint_t* mvz = model_vertices_z(hnd);

        for( int vi = 0; vi < model->vertex_count; vi++ )
        {
            struct ProjectedVertex pv;
            long long exact_x;
            long long exact_y;
            int abs_x;
            int abs_y;

            project_orthographic_fast(
                &pv, mvx[vi], mvy[vi], mvz[vi], yaw,
                position->x, position->y, position->z,
                ToriDraw_NormalizeAngle(camera->pitch),
                ToriDraw_NormalizeAngle(camera->yaw));

            abs_x = pv.x < 0 ? -pv.x : pv.x;
            abs_y = pv.y < 0 ? -pv.y : pv.y;
            if( abs_x > dbg_ortho_limit || abs_y > dbg_ortho_limit )
            {
                ortho_overflow++;
                if( (abs_x > abs_y ? abs_x : abs_y) > worst_ortho_coord )
                    worst_ortho_coord = abs_x > abs_y ? abs_x : abs_y;
            }

            if( pv.z < near_z )
                continue;

            exact_x = (((long long)pv.x * dbg_cot15) >> 6) / pv.z;
            exact_y = (((long long)pv.y * dbg_cot15) >> 6) / pv.z;
            if( exact_x == TORIDRAW_SCREEN_X_NEAR_CLIPPED )
                exact_x = TORIDRAW_SCREEN_X_NEAR_CLIPPED_NUDGE;

            {
                long long const dx = exact_x - scene->screen_vertices_x[vi];
                long long const dy = exact_y - scene->screen_vertices_y[vi];
                long long const adx = dx < 0 ? -dx : dx;
                long long const ady = dy < 0 ? -dy : dy;
                long long const worst = adx > ady ? adx : ady;
                if( worst > 1 )
                {
                    screen_mismatch++;
                    if( worst > worst_screen_delta )
                    {
                        worst_screen_delta = worst;
                        if( abs_x > worst_ortho_coord )
                            worst_ortho_coord = abs_x;
                    }
                }
            }
        }
    }
    /* #endregion */

    for( int vi = 0; vi < model->vertex_count; vi++ )
    {
        int const x = scene->screen_vertices_x[vi];
        int const y = scene->screen_vertices_y[vi];
        int const z = scene->screen_vertices_z[vi];
        if( x < min_x ) min_x = x;
        if( x > max_x ) max_x = x;
        if( y < min_y ) min_y = y;
        if( y > max_y ) max_y = y;
        if( z < min_z ) min_z = z;
        if( z > max_z ) max_z = z;
        /* A clipped vertex carries the sentinel in x and an undivided y, so
         * neither is a projected coordinate yet; only the survivors are. */
        if( may_clip && x == TORIDRAW_SCREEN_X_NEAR_CLIPPED )
            clipped_vertices++;
        else if(
            x < -TORIDRAW_PROJECTED_COORD_LIMIT || x > TORIDRAW_PROJECTED_COORD_LIMIT ||
            y < -TORIDRAW_PROJECTED_COORD_LIMIT || y > TORIDRAW_PROJECTED_COORD_LIMIT )
            fixed16_vertices++;
    }

    for( int face = 0; face < model->face_count; face++ )
    {
        uint32_t const a = model->face_indices_a[face];
        uint32_t const b = model->face_indices_b[face];
        uint32_t const c = model->face_indices_c[face];
        if( a >= (uint32_t)model->vertex_count || b >= (uint32_t)model->vertex_count ||
            c >= (uint32_t)model->vertex_count )
            continue;

        int const xa = scene->screen_vertices_x[a];
        int const xb = scene->screen_vertices_x[b];
        int const xc = scene->screen_vertices_x[c];
        bool const clipped = may_clip &&
                             (xa == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
                              xb == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
                              xc == TORIDRAW_SCREEN_X_NEAR_CLIPPED);
        if( clipped )
        {
            clipped_faces++;
            continue;
        }

        long long const dx_ab = (long long)xa - xb;
        long long const dx_bc = (long long)xb - xc;
        long long const dx_ca = (long long)xc - xa;
        long long const abs_ab = dx_ab < 0 ? -dx_ab : dx_ab;
        long long const abs_bc = dx_bc < 0 ? -dx_bc : dx_bc;
        long long const abs_ca = dx_ca < 0 ? -dx_ca : dx_ca;
        long long const face_max =
            abs_ab > abs_bc ? (abs_ab > abs_ca ? abs_ab : abs_ca)
                            : (abs_bc > abs_ca ? abs_bc : abs_ca);
        if( face_max > max_abs_edge_dx )
            max_abs_edge_dx = face_max;
        if( xa < -TORIDRAW_PROJECTED_COORD_LIMIT || xa > TORIDRAW_PROJECTED_COORD_LIMIT ||
            xb < -TORIDRAW_PROJECTED_COORD_LIMIT || xb > TORIDRAW_PROJECTED_COORD_LIMIT ||
            xc < -TORIDRAW_PROJECTED_COORD_LIMIT || xc > TORIDRAW_PROJECTED_COORD_LIMIT ||
            face_max > 2 * TORIDRAW_PROJECTED_COORD_LIMIT )
            fixed16_faces++;
    }

    /* #region agent log */
    if( toridraw_dbg_enabled() )
    {
        static int gate_fixed16 = 0;
        static int gate_simd = 0;
        static int gate_raise = 0;
        static int gate_clip = 0;
        static int gate_clean = 0;
        bool const simd_wrap = screen_mismatch > 0 || ortho_overflow > 0;
        const struct ToriDraw_BoundsCylinder* pbc = model_bounds_cylinder(hnd);
        int const radius = pbc ? pbc->min_z_depth_any_rotation : 0;
        bool const overflow = fixed16_vertices > 0 || fixed16_faces > 0 ||
                              max_abs_edge_dx > 2 * TORIDRAW_PROJECTED_COORD_LIMIT;
        /* Geometry this model only loses because the plane was raised: at the
         * camera's own near plane nothing here would have been behind it. */
        bool const raised_cut = clipped_faces > 0 &&
                                scene->projection_near_plane_z > camera->near_plane_z &&
                                center_z - radius >= camera->near_plane_z;
        /* An untextured model has no camera-space scratch, so the triangle
         * dispatchers drop these faces instead of clipping them. */
        bool const near_clip_loss = clipped_faces > 0 && model->textured_face_count == 0;
        bool const big = model->face_count >= 2000;
        const char* hyp = simd_wrap
                              ? "I"
                              : (overflow ? "A"
                                          : (raised_cut ? "G" : (near_clip_loss ? "B" : "AB")));
        bool emit = false;

        if( simd_wrap )
            emit = toridraw_dbg_gate(&gate_simd, 150);
        else if( overflow )
            emit = toridraw_dbg_gate(&gate_fixed16, 200);
        else if( raised_cut )
            emit = toridraw_dbg_gate(&gate_raise, 150);
        else if( near_clip_loss )
            emit = toridraw_dbg_gate(&gate_clip, 150);
        else if( big )
            emit = toridraw_dbg_gate(&gate_clean, 400);

        if( emit )
        {
            char data[1024];
            snprintf(
                data,
                sizeof(data),
                "{\"model\":\"%p\",\"vertices\":%d,\"faces\":%d,"
                "\"textured_faces\":%d,\"allow_near_clip\":%d,\"may_clip\":%d,"
                "\"screen_x_min\":%d,\"screen_x_max\":%d,\"screen_y_min\":%d,"
                "\"screen_y_max\":%d,\"screen_z_min\":%d,\"screen_z_max\":%d,"
                "\"center_z\":%d,\"radius\":%d,\"near_plane_z\":%d,"
                "\"near_plane_z_eff\":%d,\"raised_cut\":%d,"
                "\"coord_limit\":%d,\"clipped_vertices\":%d,"
                "\"clipped_faces\":%d,\"fixed16_vertices\":%d,\"fixed16_faces\":%d,"
                "\"max_abs_edge_dx\":%lld,\"viewport_w\":%d,\"viewport_h\":%d,"
                "\"pos_x\":%d,\"pos_y\":%d,\"pos_z\":%d,"
                "\"yaw_only\":%d,\"cot15\":%d,\"ortho_limit\":%d,"
                "\"ortho_overflow\":%d,\"screen_mismatch\":%d,"
                "\"worst_screen_delta\":%lld,\"worst_ortho_coord\":%d}",
                (void*)model,
                model->vertex_count,
                model->face_count,
                model->textured_face_count,
                model->textured_face_count > 0 ? 1 : 0,
                (int)may_clip,
                min_x,
                max_x,
                min_y,
                max_y,
                min_z,
                max_z,
                center_z,
                radius,
                camera->near_plane_z,
                scene->projection_near_plane_z,
                (int)raised_cut,
                TORIDRAW_PROJECTED_COORD_LIMIT,
                clipped_vertices,
                clipped_faces,
                fixed16_vertices,
                fixed16_faces,
                max_abs_edge_dx,
                view_port->width,
                view_port->height,
                position->x,
                position->y,
                position->z,
                (int)dbg_yaw_only,
                dbg_cot15,
                dbg_ortho_limit,
                ortho_overflow,
                screen_mismatch,
                worst_screen_delta,
                worst_ortho_coord);
            toridraw_dbg_log(
                hyp,
                "toridraw_render.u.c:project_range",
                simd_wrap
                    ? "projection disagrees with exact arithmetic (lane wrap)"
                    : overflow
                    ? "projected coords exceed raster 16.16 range"
                    : (raised_cut
                           ? "raised near plane cut geometry the camera plane kept"
                           : (near_clip_loss ? "near-clipped faces on untextured model"
                                             : "projection range clean")),
                data);
        }
    }
    /* #endregion */

    /* Every raster path converts x and dx to signed 16.16 with `<< 16`.
     * Report models that exceed that representable range; ASan cannot see
     * this class of arithmetic wrap because it stays inside allocated memory. */
    if( toridraw_sort_debug_level() < 2 && fixed16_vertices == 0 && fixed16_faces == 0 &&
        !(model->face_count >= 8000 && clipped_vertices > 0) )
        return;

    fprintf(
        stderr,
        "project_range: model=%p vertices=%d faces=%d "
        "screen={x=%d..%d,y=%d..%d,z=%d..%d} center_z=%d near=%d scale=%d "
        "may_clip=%d clipped_vertices=%d fixed16_vertices=%d "
        "fixed16_faces=%d max_abs_edge_dx=%lld "
        "viewport={size=%dx%d,clip=%d,%d..%d,%d,center=%d,%d,stride=%d} "
        "position={x=%d,y=%d,z=%d,pitch=%d,yaw=%d,roll=%d}\n",
        (void*)model,
        model->vertex_count,
        model->face_count,
        min_x,
        max_x,
        min_y,
        max_y,
        min_z,
        max_z,
        center_z,
        camera->near_plane_z,
        toridraw_proj_scale_from_cot16(toridraw_proj_cot16(
            camera->proj_mode, camera->proj_scale, camera->fov_rpi2048)),
        (int)may_clip,
        clipped_vertices,
        fixed16_vertices,
        fixed16_faces,
        max_abs_edge_dx,
        view_port->width,
        view_port->height,
        view_port->clip_left,
        view_port->clip_top,
        view_port->clip_right,
        view_port->clip_bottom,
        view_port->x_center,
        view_port->y_center,
        view_port->stride,
        position->x,
        position->y,
        position->z,
        position->pitch,
        position->yaw,
        position->roll);
}

static inline int
ToriDraw_AabbCull(
    struct ToriDraw_AABB* aabb,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera)
{
    (void)camera;
    int screen_width = view_port->width;
    int screen_height = view_port->height;

    if( aabb->min_screen_x >= screen_width )
        return TORIDRAW_CULL_AABB;
    if( aabb->min_screen_y >= screen_height )
        return TORIDRAW_CULL_AABB;
    if( aabb->max_screen_x < 0 )
        return TORIDRAW_CULL_AABB;
    if( aabb->max_screen_y < 0 )
        return TORIDRAW_CULL_AABB;

    return TORIDRAW_CULL_VISIBLE;
}

static inline int
ToriDraw_FastCull(
    struct ToriDraw_AABB* aabb,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_Camera* camera,
    struct ProjectedVertex* projected_vertex)
{
    assert(hnd.kind != TORIDRAWMK_NONE);
    int model_yaw = ToriDraw_NormalizeAngle(position->yaw);
    int scene_x = position->x;
    int scene_y = position->y;
    int scene_z = position->z;

    int camera_pitch = ToriDraw_NormalizeAngle(camera->pitch);
    int camera_yaw = ToriDraw_NormalizeAngle(camera->yaw);
    int near_plane_z = camera->near_plane_z;

    int cull_mx = 0;
    int cull_my = 0;
    int cull_mz = 0;
    assert(hnd.kind == TORIDRAWMK_MODEL);

    project_orthographic_fast(
        projected_vertex,
        cull_mx,
        cull_my,
        cull_mz,
        model_yaw,
        scene_x,
        scene_y,
        scene_z,
        camera_pitch,
        camera_yaw);

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

    int screen_x_min_unoffset;
    int screen_x_max_unoffset;
    if( parallel )
    {
        screen_x_min_unoffset = (ortho_screen_x_min * zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        screen_x_max_unoffset = (ortho_screen_x_max * zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
    }
    else
    {
        screen_x_min_unoffset = project_divide(
            ortho_screen_x_min,
            mid_z,
            toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048));
        screen_x_max_unoffset = project_divide(
            ortho_screen_x_max,
            mid_z,
            toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048));
    }
    int screen_edge_width = view_port->width >> 1;

    if( screen_x_min_unoffset > screen_edge_width || screen_x_max_unoffset < -screen_edge_width )
        return TORIDRAW_CULL_FAST;

    int model_center_to_top_edge = bc->center_to_top_edge;

    int model_center_to_bottom_edge =
        (bc->center_to_bottom_edge * ToriDraw_ReadCosTable(camera_pitch) >> 16) +
        (model_edge_radius * ToriDraw_ReadSinTable(camera_pitch) >> 16);

    int screen_y_min_unoffset;
    int screen_y_max_unoffset;
    if( parallel )
    {
        screen_y_min_unoffset =
            ((mid_y - abs(model_center_to_bottom_edge)) * zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        screen_y_max_unoffset =
            ((mid_y + abs(model_center_to_top_edge)) * zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
    }
    else
    {
        screen_y_min_unoffset = project_divide(
            mid_y - abs(model_center_to_bottom_edge),
            mid_z,
            toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048));
        screen_y_max_unoffset = project_divide(
            mid_y + abs(model_center_to_top_edge),
            mid_z,
            toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048));
    }
    int screen_edge_height = view_port->height >> 1;
    if( screen_y_min_unoffset > screen_edge_height || screen_y_max_unoffset < -screen_edge_height )
        return TORIDRAW_CULL_FAST;

    aabb->min_screen_x = screen_x_min_unoffset + view_port->x_center;
    aabb->min_screen_y = screen_y_min_unoffset + view_port->y_center;
    aabb->max_screen_x = screen_x_max_unoffset + view_port->x_center;
    aabb->max_screen_y = screen_y_max_unoffset + view_port->y_center;
    aabb->kind = TORIDRAW_AABB_KIND_CYLINDER_4POINT;

    return TORIDRAW_CULL_VISIBLE;
}

static void
ToriDraw_CalculateCylinderAabb8point(
    struct ToriDraw_AABB* aabb,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera)
{
    const struct ToriDraw_BoundsCylinder* bcyl = model_bounds_cylinder(hnd);
    assert(bcyl);
    int model_edge_radius = bcyl->radius;
    int model_min_y = bcyl->min_y;
    int model_max_y = bcyl->max_y;

    int mx = 0;
    int mz = 0;

    vertexint_t bb_x[8] = {
        (vertexint_t)(mx + model_edge_radius), (vertexint_t)(mx + model_edge_radius),
        (vertexint_t)(mx + model_edge_radius), (vertexint_t)(mx + model_edge_radius),
        (vertexint_t)(mx - model_edge_radius), (vertexint_t)(mx - model_edge_radius),
        (vertexint_t)(mx - model_edge_radius), (vertexint_t)(mx - model_edge_radius)
    };
    vertexint_t bb_y[8] = { (vertexint_t)model_min_y, (vertexint_t)model_min_y,
                            (vertexint_t)model_max_y, (vertexint_t)model_max_y,
                            (vertexint_t)model_min_y, (vertexint_t)model_min_y,
                            (vertexint_t)model_max_y, (vertexint_t)model_max_y };
    vertexint_t bb_z[8] = {
        (vertexint_t)(mz + model_edge_radius), (vertexint_t)(mz - model_edge_radius),
        (vertexint_t)(mz + model_edge_radius), (vertexint_t)(mz - model_edge_radius),
        (vertexint_t)(mz + model_edge_radius), (vertexint_t)(mz - model_edge_radius),
        (vertexint_t)(mz + model_edge_radius), (vertexint_t)(mz - model_edge_radius)
    };

    int sc_x[8];
    int sc_y[8];
    int sc_z[8];

    int const model_pitch = ToriDraw_NormalizeAngle(position->pitch);
    int const model_yaw = ToriDraw_NormalizeAngle(position->yaw);
    int const model_roll = ToriDraw_NormalizeAngle(position->roll);
    int const camera_roll = ToriDraw_NormalizeAngle(camera->roll);

    /*
     * The clipping family, unlike ToriDraw_Project: the min/max sweep below
     * reads sc_x directly, so a corner behind the near plane has to come back
     * as the sentinel to drag min_screen_x out to -5000 and keep the box
     * conservative. Eight vertices, so the per-vertex cost is noise.
     */
    if( toridraw_proj_is_parallel(camera->proj_mode) )
    {
        /* No sentinel needed: with no divide a corner behind the near plane
         * still projects to a real coordinate, so the min/max sweep below can
         * use it directly and the box is tighter than the perspective one. */
        int const zoom16 =
            camera->parallel_zoom16 ? camera->parallel_zoom16 : TORIDRAW_ORTHO_ZOOM_UNIT;
        if( model_pitch != 0 || model_roll != 0 || camera_roll != 0 )
        {
            project_vertices_array_ortho6_fused_notex_noclip(
                sc_x, sc_y, sc_z, bb_x, bb_y, bb_z, 8,
                model_pitch, model_yaw, model_roll, 0,
                position->x, position->y, position->z,
                zoom16, camera->pitch, camera->yaw, camera_roll);
        }
        else
        {
            project_vertices_array_ortho_fused_notex_noclip(
                sc_x, sc_y, sc_z, bb_x, bb_y, bb_z, 8,
                model_yaw, 0,
                position->x, position->y, position->z,
                zoom16, camera->pitch, camera->yaw);
        }
    }
    else if( model_pitch != 0 || model_roll != 0 || camera_roll != 0 )
    {
        project_vertices_array6_fused_notex_clip(
            sc_x,
            sc_y,
            sc_z,
            bb_x,
            bb_y,
            bb_z,
            8,
            model_pitch,
            model_yaw,
            model_roll,
            0,
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
        project_vertices_array_fused_notex_clip(
            sc_x,
            sc_y,
            sc_z,
            bb_x,
            bb_y,
            bb_z,
            8,
            model_yaw,
            0,
            position->x,
            position->y,
            position->z,
            camera->near_plane_z,
            toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048),
            camera->pitch,
            camera->yaw);
    }

    int min_sx = sc_x[0];
    int max_sx = sc_x[0];
    int min_sy = sc_y[0];
    int max_sy = sc_y[0];

    for( int i = 1; i < 8; i++ )
    {
        int sx = sc_x[i];
        int sy = sc_y[i];

        if( sx < min_sx )
            min_sx = sx;
        else if( sx > max_sx )
            max_sx = sx;

        if( sy < min_sy )
            min_sy = sy;
        else if( sy > max_sy )
            max_sy = sy;
    }

    int cx = view_port->x_center;
    int cy = view_port->y_center;

    aabb->min_screen_x = min_sx + cx;
    aabb->max_screen_x = max_sx + cx;
    aabb->min_screen_y = min_sy + cy;
    aabb->max_screen_y = max_sy + cy;

    aabb->kind = TORIDRAW_AABB_KIND_CYLINDER_8POINT;
}

static inline int
bucket_sort_by_average_depth(
    faceint_t* RESTRICT face_depth_buckets,
    faceint_t* RESTRICT face_depth_bucket_counts,
    int depth_levels,
    int depth_stride,
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
        {
            const long long dx1 = (long long)vx[a] - vx[b];
            const long long dy1 = (long long)vy[a] - vy[b];
            const long long dx2 = (long long)vx[c] - vx[b];
            const long long dy2 = (long long)vy[c] - vy[b];
            winding = dx1 * dy2 - dy1 * dx2;
        }

        /* A clipped vertex has sentinel x and undivided y, so this triangle's
         * screen-space winding does not exist yet. The reference buckets it
         * unconditionally and performs the real winding test after building
         * the near-plane polygon. */
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

/* #region agent log */
/**
 * Integrity of the emitted draw order, which the drop counters cannot see.
 *
 * Three independent things can go wrong once the depth buckets are folded into
 * priority runs, and each produces visible sorting artefacts rather than
 * missing geometry:
 *   - a face index written through the wrong stride appears twice, which means
 *     another accepted face never appears at all;
 *   - an index lands outside the model;
 *   - a priority run stops being back-to-front, so near faces paint first and
 *     far ones paint over them.
 * Depth is recomputed the same way the bucket sort computed it.
 */
static void
toridraw_dbg_check_face_order(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    const uint8_t* face_priorities,
    int face_count,
    int model_depth_bias)
{
    static uint8_t seen[16384];
    static int gate_bad = 0;
    static int gate_ok = 0;
    const struct ToriDraw_Model* m = model_as_full(hnd);
    const faceint_t* fa = m->face_indices_a;
    const faceint_t* fb = m->face_indices_b;
    const faceint_t* fc = m->face_indices_c;
    const int* vz = scene->screen_vertices_z;
    int const ordered = scene->tmp_face_order_count;
    int duplicates = 0;
    int out_of_range = 0;
    int distinct = 0;
    int prio_inversions = 0;
    int worst_inversion = 0;
    int prev_prio = -1;
    int prev_depth = 0;

    if( face_count > (int)sizeof(seen) )
        return;
    memset(seen, 0, (size_t)face_count);

    for( int i = 0; i < ordered; i++ )
    {
        int const f = scene->tmp_face_order[i];
        int prio;
        int depth;

        if( f < 0 || f >= face_count )
        {
            out_of_range++;
            continue;
        }
        if( seen[f] )
            duplicates++;
        else
        {
            seen[f] = 1;
            distinct++;
        }

        depth = div3_fast_fixedpoint(vz[fa[f]] + vz[fb[f]] + vz[fc[f]]) + model_depth_bias;
        prio = face_priorities ? faceprio_unpack(face_priorities, (faceint_t)f) : 0;

        if( prio == prev_prio && depth > prev_depth )
        {
            prio_inversions++;
            if( depth - prev_depth > worst_inversion )
                worst_inversion = depth - prev_depth;
        }
        prev_prio = prio;
        prev_depth = depth;
    }

    {
        bool const bad = duplicates > 0 || out_of_range > 0 || prio_inversions > 0 ||
                         distinct != ordered;
        bool const big = face_count >= 2000;
        bool emit = false;

        if( bad )
            emit = toridraw_dbg_gate(&gate_bad, 150);
        else if( big )
            emit = toridraw_dbg_gate(&gate_ok, 400);

        if( emit )
        {
            char data[512];
            snprintf(
                data,
                sizeof(data),
                "{\"model\":\"%p\",\"faces\":%d,\"ordered\":%d,\"distinct\":%d,"
                "\"duplicates\":%d,\"out_of_range\":%d,\"prio_inversions\":%d,"
                "\"worst_inversion\":%d,\"depth_bias\":%d,\"depth_levels\":%d,"
                "\"depth_stride\":%d,\"priority_stride\":%d,\"max_faces\":%d,"
                "\"max_vertices\":%d,\"has_priorities\":%d}",
                (void*)model_as_full(hnd),
                face_count,
                ordered,
                distinct,
                duplicates,
                out_of_range,
                prio_inversions,
                worst_inversion,
                model_depth_bias,
                scene->depth_levels,
                scene->depth_stride,
                scene->priority_stride,
                scene->max_faces,
                scene->max_vertices,
                face_priorities ? 1 : 0);
            toridraw_dbg_log(
                "H",
                "toridraw_render.u.c:face_order",
                bad ? "draw order is not a back-to-front permutation"
                    : "draw order integrity clean",
                data);
        }
    }
}
/* #endregion */

static inline void
ToriDraw_ComputeProjectedFaceOrder(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd)
{
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
    {
        struct ToriDraw_Model* m = model_as_full(hnd);
        fia = m->face_indices_a;
        fib = m->face_indices_b;
        fic = m->face_indices_c;
        face_priorities = toridraw_ignore_priorities() ? NULL : m->face_priorities;
        face_count = m->face_count;
        break;
    }
    default:
        assert(0);
        break;
    }

    const struct ToriDraw_BoundsCylinder* bc = model_bounds_cylinder(hnd);
    int model_min_depth = bc ? bc->min_z_depth_any_rotation : 0;
    /* #region agent log */
    /* model_min_depth is reused below to carry the observed depth range, so
     * keep the bias the bucket sort actually applied. */
    int const bias = model_min_depth;
    /* #endregion */

    if( toridraw_sort_debug_enabled() )
    {
        toridraw_face_sort_debug_init(&debug_stats_storage);
        debug_stats = &debug_stats_storage;
    }

    memset(
        scene->tmp_depth_face_count,
        0,
        (size_t)scene->depth_levels * sizeof(scene->tmp_depth_face_count[0]));

    int bounds = bucket_sort_by_average_depth(
        scene->tmp_depth_faces,
        scene->tmp_depth_face_count,
        scene->depth_levels,
        scene->depth_stride,
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
        if( debug_stats )
            toridraw_face_sort_debug_print(scene, hnd, debug_stats, order_index);
        /* #region agent log */
        if( toridraw_dbg_enabled() )
            toridraw_dbg_check_face_order(scene, hnd, face_priorities, face_count, bias);
        /* #endregion */
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
    /* #region agent log */
    if( toridraw_dbg_enabled() )
        toridraw_dbg_check_face_order(scene, hnd, face_priorities, face_count, bias);
    /* #endregion */
}

static inline int
bucket_sort_by_average_depth_small(
    struct ToriDraw_Scene* scene,
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
    const int depth_levels = scene->depth_levels;
    int min_d = depth_levels;
    int max_d = 0;

    memset(scene->sm_depth_offset, 0, (size_t)depth_levels * sizeof(int));

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
        {
            const long long dx1 = (long long)vx[a] - vx[b];
            const long long dy1 = (long long)vy[a] - vy[b];
            const long long dx2 = (long long)vx[c] - vx[b];
            const long long dy2 = (long long)vy[c] - vy[b];
            winding = dx1 * dy2 - dy1 * dx2;
        }

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

    int total = 0;
    for( int d = 0; d < depth_levels; d++ )
    {
        int count = scene->sm_depth_offset[d];
        scene->sm_depth_offset[d] = total;
        total += count;
    }
    scene->sm_depth_offset[depth_levels] = total;

    memcpy(scene->sm_depth_cursor, scene->sm_depth_offset, (size_t)depth_levels * sizeof(int));

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
ToriDraw_ComputeProjectedFaceOrderSmall(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd)
{
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
    {
        struct ToriDraw_Model* m = model_as_full(hnd);
        fia = m->face_indices_a;
        fib = m->face_indices_b;
        fic = m->face_indices_c;
        face_priorities = toridraw_ignore_priorities() ? NULL : m->face_priorities;
        face_count = m->face_count;
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

    int bounds = bucket_sort_by_average_depth_small(
        scene,
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

    if( bounds == 0 )
    {
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
        if( debug_stats )
            toridraw_face_sort_debug_print(scene, hnd, debug_stats, order_index);
        return;
    }

    int priority_depths[12] = { 0 };
    int counts[12] = { 0 };

    partition_and_accumulate_faces_by_priority_small(
        scene, priority_depths, counts, face_priorities, model_min_depth, model_max_depth);

    scene->tmp_face_order_count =
        sort_face_draw_order_small(scene, scene->tmp_face_order, priority_depths, counts);
    if( debug_stats )
        toridraw_face_sort_debug_print(
            scene, hnd, debug_stats, scene->tmp_face_order_count);
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
/* Models that can reach behind the near plane. */
static inline void
toridraw_project_vertices_clip(
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

/* Models that provably cannot; no sentinel, no near-plane test. */
static inline void
toridraw_project_vertices_noclip(
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

static inline int
ToriDraw_Project(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera)
{
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
        /* #region agent log */
        if( toridraw_dbg_enabled() )
        {
            static int gate = 0;
            if( toridraw_dbg_gate(&gate, 200) )
            {
                char data[256];
                snprintf(
                    data,
                    sizeof(data),
                    "{\"model\":\"%p\",\"vertices\":%d,\"max_vertices\":%d,"
                    "\"faces\":%d,\"max_faces\":%d}",
                    (void*)model_as_full(hnd),
                    model_vertex_count(hnd),
                    scene->max_vertices,
                    model_face_count(hnd),
                    scene->max_faces);
                toridraw_dbg_log(
                    "D",
                    "toridraw_render.u.c:project_capacity",
                    "model rejected: exceeds scene scratch capacity",
                    data);
            }
        }
        /* #endregion */
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

    cull = ToriDraw_FastCull(
        &scene->cylinder_fast_aabb, view_port, hnd, position, camera, &center_projection);
    if( cull != TORIDRAW_CULL_VISIBLE )
        return cull;

    scene->projected_vertex = center_projection;

    ToriDraw_CalculateCylinderAabb8point(&scene->aabb, hnd, position, view_port, camera);

    cull = ToriDraw_AabbCull(&scene->aabb, view_port, camera);
    if( cull != TORIDRAW_CULL_VISIBLE )
        return cull;

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

    if( toridraw_proj_is_parallel(camera->proj_mode) )
    {
        /*
         * Same near-clip gate, different meaning. Parallel projection has no
         * singularity to avoid, so may_clip here is not a safety requirement --
         * it just says whether any vertex is near enough the view plane to need
         * hiding. A camera with a very negative near_plane_z therefore never
         * takes the clipping family at all, which is the map editor's normal
         * configuration.
         */
        if( may_clip )
            toridraw_project_vertices_parallel_clip(
                scene, hnd, position, camera,
                model_pitch, model_yaw, model_roll, camera_roll, center_projection.z);
        else
            toridraw_project_vertices_parallel_noclip(
                scene, hnd, position, camera,
                model_pitch, model_yaw, model_roll, camera_roll, center_projection.z);
    }
    else if( may_clip )
        toridraw_project_vertices_clip(
            scene, hnd, position, camera,
            model_pitch, model_yaw, model_roll, camera_roll, center_projection.z);
    else
        toridraw_project_vertices_noclip(
            scene, hnd, position, camera,
            model_pitch, model_yaw, model_roll, camera_roll, center_projection.z);

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
                toridraw_project_vertices_noclip(
                    scene, hnd, position, camera,
                    model_pitch, model_yaw, model_roll, camera_roll, center_projection.z);
            else
                toridraw_project_vertices_clip(
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
#define TORIDRAW_PICK_SLOP 5

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
 * same question for either one — only the triangle predicate differs, so
 * `rough` picks between them at the bottom.
 */
static bool
toridraw_projected_model_hit_face(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_ViewPort* view_port,
    int screen_x,
    int screen_y,
    bool rough)
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
        if( colors_c && colors_c[i] == TORIDRAWHSL16_HIDDEN )
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

        bool hit = rough ? toridraw_mouse_roughly_inside_triangle(
                               x1, y1, x2, y2, x3, y3, adjusted_screen_x, adjusted_screen_y)
                         : toridraw_triangle_contains_point(
                               x1, y1, x2, y2, x3, y3, adjusted_screen_x, adjusted_screen_y);
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
        scene, hnd, view_port, screen_x, screen_y, /* rough */ false);
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
        scene, hnd, view_port, screen_x, screen_y, /* rough */ true);
}
