#ifndef TORIDRAW_DEBUG_RENDER_U_C
#define TORIDRAW_DEBUG_RENDER_U_C

/*
 * NDJSON emitters for the render path.
 *
 * Included by toridraw_render.u.c once the model accessors, the depth helper
 * and struct ToriDraw_FaceSortDebugStats are in scope. Nothing here is on the
 * render path when the log is compiled out: the macros at the bottom become
 * argument-consuming no-ops and none of these functions are compiled at all.
 *
 * The two structs are defined unconditionally, because the caller accumulates
 * into them either way -- the projected-range figures also feed the
 * TORIDRAW_SORT_DEBUG fprintf, which is a separate, deliberately
 * runtime-selectable facility (see toridraw_sort_debug_level).
 */

#include "toridraw_debug_log.h"

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
    if( TORIDRAW_DBG_ENABLED() )
        return true;
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

/** Exact-arithmetic differential against whatever projection kernel ran. */
struct ToriDraw_DbgProjectDifferential
{
    bool yaw_only;
    int cot15;
    int ortho_limit;
    int ortho_overflow;
    int screen_mismatch;
    long long worst_screen_delta;
    int worst_ortho_coord;
};

/** The projected extents and 16.16-range counters for one model. */
struct ToriDraw_DbgProjectRange
{
    int min_x;
    int max_x;
    int min_y;
    int max_y;
    int min_z;
    int max_z;
    int clipped_vertices;
    int clipped_faces;
    int fixed16_vertices;
    int fixed16_faces;
    long long max_abs_edge_dx;
};

#if defined(TORIDRAW_DEBUG_NDJSON) && TORIDRAW_DEBUG_NDJSON

/**
 * Did the bucket sort keep every face it accepted, in the order it meant to?
 */
static void
toridraw_dbg_record_face_sort(
    const struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    const struct ToriDraw_FaceSortDebugStats* stats,
    int ordered)
{
    const struct ToriDraw_BoundsCylinder* bounds = model_bounds_cylinder(hnd);
    int const depth_bias = bounds ? bounds->min_z_depth_any_rotation : 0;
    long long const conservative_levels = (long long)depth_bias * 2 + 1;
    int const depth_min = stats->min_depth_seen == INT_MAX ? -1 : stats->min_depth_seen;
    int const depth_max = stats->max_depth_seen == INT_MIN ? -1 : stats->max_depth_seen;

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
}

/**
 * The perspective kernels all compute `x_scene * (cot16 >> 1) >> 6`, which is
 * mathematically `x_scene * scale` but forms a product six bits larger on the
 * way. In the NEON path that product is a vmulq_s32 lane, which wraps silently
 * and which no sanitizer instruments. A wrapped lane yields a small but wrong
 * screen coordinate, so only a comparison against exact arithmetic can see it.
 */
static void
toridraw_dbg_project_differential(
    const struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    const struct ToriDraw_Position* position,
    const struct ToriDraw_Camera* camera,
    struct ToriDraw_DbgProjectDifferential* out)
{
    const struct ToriDraw_Model* model = model_as_full(hnd);
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

    out->yaw_only = dbg_yaw_only;
    out->cot15 = dbg_cot15;
    out->ortho_limit = dbg_ortho_limit;
    out->ortho_overflow = ortho_overflow;
    out->screen_mismatch = screen_mismatch;
    out->worst_screen_delta = worst_screen_delta;
    out->worst_ortho_coord = worst_ortho_coord;
}

/**
 * One record per model whose projection left the raster's 16.16 range, lost
 * geometry to the raised near plane, or disagreed with exact arithmetic.
 */
static void
toridraw_dbg_record_project_range(
    const struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    const struct ToriDraw_Position* position,
    const struct ToriDraw_ViewPort* view_port,
    const struct ToriDraw_Camera* camera,
    int center_z,
    bool may_clip,
    const struct ToriDraw_DbgProjectRange* range,
    const struct ToriDraw_DbgProjectDifferential* diff)
{
    const struct ToriDraw_Model* model = model_as_full(hnd);
    int const min_x = range->min_x;
    int const max_x = range->max_x;
    int const min_y = range->min_y;
    int const max_y = range->max_y;
    int const min_z = range->min_z;
    int const max_z = range->max_z;
    int const clipped_vertices = range->clipped_vertices;
    int const clipped_faces = range->clipped_faces;
    int const fixed16_vertices = range->fixed16_vertices;
    int const fixed16_faces = range->fixed16_faces;
    long long const max_abs_edge_dx = range->max_abs_edge_dx;
    bool const dbg_yaw_only = diff->yaw_only;
    int const dbg_cot15 = diff->cot15;
    int const dbg_ortho_limit = diff->ortho_limit;
    int const ortho_overflow = diff->ortho_overflow;
    int const screen_mismatch = diff->screen_mismatch;
    long long const worst_screen_delta = diff->worst_screen_delta;
    int const worst_ortho_coord = diff->worst_ortho_coord;

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
}

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

/** A model too big for the scene's scratch arrays; rejected before it can
 *  write past any of the six buffers. */
static void
toridraw_dbg_record_capacity_reject(
    const struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd)
{
    static int gate = 0;

    if( !toridraw_dbg_enabled() || !toridraw_dbg_gate(&gate, 200) )
        return;
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

#define TORIDRAW_DBG_RECORD_FACE_SORT(scene, hnd, stats, ordered) \
    toridraw_dbg_record_face_sort((scene), (hnd), (stats), (ordered))
#define TORIDRAW_DBG_PROJECT_DIFFERENTIAL(scene, hnd, position, camera, out) \
    toridraw_dbg_project_differential((scene), (hnd), (position), (camera), (out))
#define TORIDRAW_DBG_RECORD_PROJECT_RANGE( \
    scene, hnd, position, view_port, camera, center_z, may_clip, range, diff) \
    toridraw_dbg_record_project_range( \
        (scene), (hnd), (position), (view_port), (camera), (center_z), (may_clip), \
        (range), (diff))
#define TORIDRAW_DBG_CHECK_FACE_ORDER(scene, hnd, priorities, face_count, bias) \
    toridraw_dbg_check_face_order((scene), (hnd), (priorities), (face_count), (bias))
#define TORIDRAW_DBG_RECORD_CAPACITY_REJECT(scene, hnd) \
    toridraw_dbg_record_capacity_reject((scene), (hnd))

#else

/* Argument-consuming, so a local that only the log reads is still "used". */
#define TORIDRAW_DBG_RECORD_FACE_SORT(scene, hnd, stats, ordered) \
    ((void)(scene), (void)(stats), (void)(ordered))
#define TORIDRAW_DBG_PROJECT_DIFFERENTIAL(scene, hnd, position, camera, out) \
    ((void)(scene), (void)(position), (void)(camera), (void)(out))
#define TORIDRAW_DBG_RECORD_PROJECT_RANGE( \
    scene, hnd, position, view_port, camera, center_z, may_clip, range, diff) \
    ((void)(scene), (void)(position), (void)(view_port), (void)(camera), \
     (void)(center_z), (void)(may_clip), (void)(range), (void)(diff))
#define TORIDRAW_DBG_CHECK_FACE_ORDER(scene, hnd, priorities, face_count, bias) \
    ((void)(scene), (void)(priorities), (void)(face_count), (void)(bias))
#define TORIDRAW_DBG_RECORD_CAPACITY_REJECT(scene, hnd) ((void)(scene))

#endif /* TORIDRAW_DEBUG_NDJSON */

/*
 * The TORIDRAW_SORT_DEBUG printers.
 *
 * Last, because they call the record macros above: a printer emits its
 * NDJSON record first and its human line second, so both describe the
 * same model from the same counters.
 */
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

    TORIDRAW_DBG_RECORD_FACE_SORT(scene, hnd, stats, ordered);

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
    /* First, before any of the derivations below: this runs once per
     * projected model, and the tail of the prologue holds an integer divide
     * (INT_MAX / cot15) the optimizer is not obliged to sink past the gate. */
    if( !toridraw_sort_debug_enabled() )
        return;

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
    /* Zeroed, so the record below reads well-defined fields on a build with
     * the log compiled out (where the call is a no-op). */
    struct ToriDraw_DbgProjectDifferential diff = { 0 };

    TORIDRAW_DBG_PROJECT_DIFFERENTIAL(scene, hnd, position, camera, &diff);

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

    {
        struct ToriDraw_DbgProjectRange const range = {
            min_x, max_x, min_y, max_y, min_z, max_z,
            clipped_vertices, clipped_faces, fixed16_vertices, fixed16_faces,
            max_abs_edge_dx
        };
        TORIDRAW_DBG_RECORD_PROJECT_RANGE(
            scene, hnd, position, view_port, camera, center_z, may_clip, &range, &diff);
    }

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

#endif /* TORIDRAW_DEBUG_RENDER_U_C */
