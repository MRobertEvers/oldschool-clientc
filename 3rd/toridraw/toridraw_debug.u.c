#ifndef TORIDRAW_DEBUG_U_C
#define TORIDRAW_DEBUG_U_C

/*
 * Everything that exists only to describe a render.
 *
 * One file, because that is the only property worth having here. The counters,
 * the printers, the NDJSON emitters and the two verification harnesses used to
 * be spread over three files and inlined at fifty call sites in the render and
 * raster paths, where a reader of the sort loop had to skip past accounting
 * that never runs in a shipping build to find the loop. Now the render and
 * raster paths carry macro calls and nothing else, and every line of the
 * facility is below.
 *
 * COMPILED OUT unless asked for; see toridraw_debug.h for the gate. With it
 * off, none of this is compiled, no field it needs exists in any struct, no
 * parameter it needs exists in any signature, and every macro at the bottom is
 * a constant or nothing at all. That is the point: these sites sit per-model
 * and per-face on the render path, and a runtime branch there taxes the thing
 * being measured.
 *
 *   make -C src TORIDRAW_DEBUG_STATS=1   the counters, printers and harnesses
 *   make -C src TORIDRAW_DEBUG_NDJSON=1  the above plus the NDJSON anomaly log
 *   -DTORIDRAW_NEAR_CLIP_STATS           the above plus the near-clip
 *                                        differential (slow: it re-projects
 *                                        every model down the opposite kernel)
 *
 * Then, at run time, in a build that has it -- all of them off by default, so
 * an instrumented binary is still silent until asked:
 *
 *   TORIDRAW_SORT_DEBUG=1|all    per-model face-sort accounting to stderr;
 *                                1 reports anomalies, `all` every model
 *   TORIDRAW_RASTER_DEBUG=1|all  the same for the per-face raster walk
 *   TORIDRAW_DEBUG_NDJSON=1      turn the anomaly log on
 *   TORIDRAW_DEBUG_LOG=<path>    where to append; default toridraw-debug.ndjson
 *                                in the working directory
 *   TORIDRAW_DEBUG_RUN=<label>   goes in every record as runId, to tell two
 *                                runs apart in one file
 *   TORIDRAW_SPAN_RATIO=<path>   faces bucketed vs depth levels walked
 *   TORIRS_RASTER_TEX_DEBUG=1    first and every 500th missing-texture skip
 *   TORIRS_RASTER_TEX_MODE_DEBUG=1  which stock render type a textured face
 *                                   resolved to, first 32 only
 *   TORIDRAW_SAFE_NEAR=0         unrelated to the rest: restores the camera's
 *                                own near plane, so one session can A/B
 *                                whether the raised plane is what removes near
 *                                geometry
 *
 * Included by toridraw_render.u.c, once, above the render path and below the
 * three things it borrows from it (the model accessors, div3_fast_fixedpoint
 * and TORIDRAW_PROJECTED_COORD_LIMIT). Everything else it needs sits further
 * down that file or in toridraw_raster.u.c, and is reached by forward
 * declaration or by a caller-built view -- see the two blocks below. That is
 * the price of one file, and it is a cheaper price than the alternative: an
 * include point per depth of the translation unit, which is what this replaces.
 */

#include "toridraw_debug.h"

#if TORIDRAW_DEBUG_STATS

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Borrowed from further down toridraw_render.u.c.
 *
 * The instrumented bucket sort below is a fifth variant of the four in that
 * file and shares their tail; the near-clip harness runs the OPPOSITE
 * projection kernel over a model to compare against. Both are defined after
 * this file is included, so they are declared here rather than moving this
 * file below them -- the counters and macros it defines are needed above.
 */
static inline int
sm_bucket_sort_finish(struct ToriDraw_Scene* scene, int num_faces, int min_d, int max_d);
static inline void
sm_stash_face_clipped(struct ToriDraw_Scene* scene, int f);
static inline void
sm_stash_face_xy_sorted(
    struct ToriDraw_Scene* scene,
    int f,
    const int* RESTRICT vx,
    const int* RESTRICT vy,
    uint32_t a,
    uint32_t b,
    uint32_t c);
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
    int model_mid_z);
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
    int model_mid_z);
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
    int model_mid_z);
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
    int model_mid_z);

/*
 * The raster pass is described through this rather than through
 * struct ToriDrawModelRasterContext.
 *
 * That struct is declared in toridraw_raster.u.c, hundreds of lines below the
 * point this file is included, and it is the raster path's private working
 * state -- forward-declaring it here would pin its layout and its name into
 * the debug facility for no gain. The five numbers the report actually wants
 * are copied into this by TORIDRAW_DBG_RASTER_PRINT, at the one call site that
 * has the context in hand.
 */
struct ToriDraw_RasterDebugFrame
{
    const void* model;
    int num_faces;
    int num_vertices;
    int ordered_faces;
    int near_plane_z;
    int allow_near_clip;
    int near_clipped;
    int has_face_infos;
};

/* ------------------------------------------------------------------ */
/* The NDJSON anomaly log.                                            */
/* ------------------------------------------------------------------ */

/*
 * One record per anomaly burst, appended as newline-delimited JSON, for
 * questions a printf cannot answer because the interesting models are a
 * handful out of a hundred thousand: which kernel disagreed with exact
 * arithmetic, which model lost faces to the sort, which face order came out of
 * the bucket pass.
 */

#if defined(TORIDRAW_DEBUG_NDJSON) && TORIDRAW_DEBUG_NDJSON

/** Records dropped after this many, so a wedged frame cannot fill a disk. */
#define TORIDRAW_DBG_BUDGET 6000

static inline int
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

/** Relative by default: an absolute path baked in here fails to open on any
 *  host that is not the one it was written on, and every record is then
 *  dropped silently -- which reads exactly like "the instrumentation says
 *  nothing is wrong". */
static inline const char*
toridraw_dbg_log_path(void)
{
    static const char* path = NULL;
    if( !path )
    {
        const char* v = getenv("TORIDRAW_DEBUG_LOG");
        path = (v && v[0] != '\0') ? v : "toridraw-debug.ndjson";
    }
    return path;
}

static inline const char*
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
static inline bool
toridraw_dbg_gate(int* state, int period)
{
    int const n = (*state)++;
    return n < 4 || (period > 0 && (n % period) == 0);
}

static inline void
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
        "{\"runId\":\"%s\",\"hypothesisId\":\"%s\",\"location\":\"%s\","
        "\"message\":\"%s\",\"data\":%s,\"timestamp\":%lld}\n",
        toridraw_dbg_run_id(),
        hypothesis,
        location,
        message,
        data_json,
        (long long)time(NULL) * 1000);
    fclose(f);
}

#define TORIDRAW_DBG_ENABLED()           toridraw_dbg_enabled()
#define TORIDRAW_DBG_GATE(state, period) toridraw_dbg_gate((state), (period))
#define TORIDRAW_DBG_LOG(hyp, loc, msg, data) toridraw_dbg_log((hyp), (loc), (msg), (data))

#else

/* Constants, so every guarded block folds away and the emitters below are
 * never compiled. */
#define TORIDRAW_DBG_ENABLED()                0
#define TORIDRAW_DBG_GATE(state, period)      0
#define TORIDRAW_DBG_LOG(hyp, loc, msg, data) ((void)0)

#endif /* TORIDRAW_DEBUG_NDJSON */

/** TORIDRAW_SAFE_NEAR=0 restores the camera's own near plane. Here rather than
 *  with the log because it is not a measurement: it takes the raised near
 *  plane away, which is the shipping behaviour, so a build without this file
 *  answers 1 and the knob does not exist. */
static inline int
toridraw_dbg_safe_near_enabled(void)
{
    static int on = -1;
    if( on < 0 )
    {
        const char* v = getenv("TORIDRAW_SAFE_NEAR");
        on = (v && v[0] == '0') ? 0 : 1;
    }
    return on;
}

/* ------------------------------------------------------------------ */
/* The render path: face-sort accounting.                             */
/* ------------------------------------------------------------------ */

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

/** TORIDRAW_SORT_DEBUG selects at run time WITHIN a build that has this file:
 * the compile-time gate decides whether the counters exist at all, and this
 * decides whether an instrumented binary is using them, so an ASan
 * reproduction can be turned up and down without rebuilding it again.
 * Pair with TORIDRAW_RASTER_DEBUG to see per-face skip reasons (hidden type,
 * HIDDEN sentinel, alpha, near-clip, texture miss). */
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

/*
 * The remaining TORIDRAW_SORT_DEBUG reporters.
 *
 * Each was an `if( gate ) fprintf(...)` sitting inline in the render path --
 * in the middle of the priority interleave, or in the capacity refusal at the
 * top of ToriDraw_Project. The gate now lives with the message, so the render
 * path carries one call and no formatting.
 */

/** The flexible-priority interleave is decided by three averages that nothing
 *  else prints, and guessing at them is how an afternoon goes. */
static void
toridraw_dbg_report_sort_counts(
    const int* counts,
    const int* flex_prio11_face_to_depth,
    int average_depth1_2,
    int average_depth3_4,
    int average_depth6_8)
{
    int flex_min = 1 << 30;
    int flex_max = -1;

    if( toridraw_sort_debug_level() < 2 )
        return;

    for( int i = 0; i < counts[10]; i++ )
    {
        int d = flex_prio11_face_to_depth[i] & 0xFFFF;
        if( d < flex_min )
            flex_min = d;
        if( d > flex_max )
            flex_max = d;
    }
    fprintf(
        stderr,
        "sort: counts 0..11 = %d %d %d %d %d %d %d %d %d %d %d %d | "
        "avg12=%d avg34=%d avg68=%d | flex depth %d..%d\n",
        counts[0], counts[1], counts[2], counts[3], counts[4], counts[5], counts[6],
        counts[7], counts[8], counts[9], counts[10], counts[11], average_depth1_2,
        average_depth3_4, average_depth6_8, flex_min, flex_max);
}

/** A model too big for the scene's scratch, refused before it can write past
 *  any of the six buffers. Also emits the NDJSON record. */
static void
toridraw_dbg_report_capacity_reject(
    const struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd)
{
    TORIDRAW_DBG_RECORD_CAPACITY_REJECT(scene, hnd);
    if( !toridraw_sort_debug_enabled() )
        return;
    fprintf(
        stderr,
        "sort_capacity: model=%p vertices=%d/%d faces=%d/%d rejected=1\n",
        (void*)model_as_full(hnd),
        model_vertex_count(hnd),
        scene->max_vertices,
        model_face_count(hnd),
        scene->max_faces);
}

/** An undersized depth table, surfaced BEFORE the fast and AABB culls can hide
 *  it: the model still draws, but its faces share buckets and sort coarsely. */
static void
toridraw_dbg_report_depth_capacity(
    const struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd)
{
    const struct ToriDraw_BoundsCylinder* bounds = model_bounds_cylinder(hnd);
    long long required_levels;

    if( !bounds )
        return;
    required_levels = (long long)bounds->min_z_depth_any_rotation * 2 + 1;
    if( required_levels <= scene->depth_levels )
        return;

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
static void
toridraw_dbg_report_span_ratio(int num_faces, int min_d, int max_d)
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

/*
 * How often the near-clip gate sends a model down the clipping kernel.
 * TORIDRAW_NEAR_CLIP_STATS only; a no-op otherwise.
 */
static void
toridraw_dbg_count_near_clip_gate(bool may_clip)
{
#ifdef TORIDRAW_NEAR_CLIP_STATS
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
#else
    (void)may_clip;
#endif
}
/* ------------------------------------------------------------------ */
/* The render path: the instrumented bucket sort.                     */
/* ------------------------------------------------------------------ */

/*
 * A fifth small-scene bucket sort, and the one variant that keeps its flags.
 *
 * It has to, and that is the point of separating it from the four in
 * toridraw_render.u.c. The counters need to see every face those four discard
 * early -- back-facing, degenerate, out of depth range -- and telling those
 * apart means computing the winding even when the answer is "cull" and keeping
 * the near-clip and stash decisions live at the point each counter is bumped.
 * Paying for that in the production loops is what the split refuses; paying
 * for it here costs nothing, because a run that asks for these numbers is not
 * a run being timed.
 *
 * Reached through TORIDRAW_DBG_SORT_SMALL_TAKEOVER, at the top of the
 * dispatcher that chooses between the other four.
 */
static int
bucket_sort_by_average_depth_small_stats(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_FaceSortDebugStats* debug_stats,
    bool stash_xy,
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

    assert(debug_stats);

    for( int f = 0; f < num_faces; f++ )
    {
        const uint32_t a = face_a[f];
        const uint32_t b = face_b[f];
        const uint32_t c = face_c[f];
        long long winding = 1;
        bool clip_candidate;

        scene->sm_face_depth[f] = -1;

        clip_candidate = near_clipped && (vx[a] == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
                                          vx[b] == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
                                          vx[c] == TORIDRAW_SCREEN_X_NEAR_CLIPPED);
        if( !clip_candidate )
            winding = toridraw_winding_2d(vx[a], vy[a], vx[b], vy[b], vx[c], vy[c]);

        if( !clip_candidate && !toridraw_winding_front_facing(winding) )
        {
            if( winding == 0 )
                debug_stats->degenerate++;
            else
                debug_stats->back_facing++;
            continue;
        }

        {
            int const depth_avg = div3_fast_fixedpoint(vz[a] + vz[b] + vz[c]) + model_min_depth;

            debug_stats->front_facing++;
            if( clip_candidate )
                debug_stats->near_clip_candidates++;
            if( depth_avg < debug_stats->min_depth_seen )
                debug_stats->min_depth_seen = depth_avg;
            if( depth_avg > debug_stats->max_depth_seen )
                debug_stats->max_depth_seen = depth_avg;

            if( (unsigned int)depth_avg >= (unsigned int)depth_levels )
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
                continue;
            }

            scene->sm_face_depth[f] = (faceint_t)depth_avg;
            scene->sm_depth_offset[depth_avg]++;
            if( stash_xy )
            {
                if( clip_candidate )
                    sm_stash_face_clipped(scene, f);
                else
                    sm_stash_face_xy_sorted(scene, f, vx, vy, a, b, c);
            }

            debug_stats->accepted++;
            if( scene->sm_depth_offset[depth_avg] > debug_stats->max_bucket_occupancy )
                debug_stats->max_bucket_occupancy = scene->sm_depth_offset[depth_avg];

            if( depth_avg < min_d )
                min_d = depth_avg;
            if( depth_avg > max_d )
                max_d = depth_avg;
        }
    }

    assert(
        debug_stats->front_facing ==
        debug_stats->accepted + debug_stats->depth_out_low + debug_stats->depth_out_high);

    return sm_bucket_sort_finish(scene, num_faces, min_d, max_d);
}

/* ------------------------------------------------------------------ */
/* The render path: the near-clip gate's verification harness.        */
/* ------------------------------------------------------------------ */

/*
 * The near-clip gate's correctness argument, checked on real scene data.
 *
 * Deliberately NOT done by comparing rendered frames between a gated and a
 * forced build: this client's offline boot is not frame-deterministic, so a
 * frame byte-compare answers "did these two runs load the same things", not
 * "do the two kernels agree". Both kernels run over the same model inside one
 * process here instead, and the gated result is restored afterwards so the
 * rest of the frame renders from the path production would have taken.
 *
 * TORIDRAW_NEAR_CLIP_STATS only.
 */
static void
toridraw_dbg_verify_near_clip_gate(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_Camera* camera,
    const struct ProjectedVertex* center_projection_in,
    const struct ToriDraw_BoundsCylinder* proj_bc,
    bool may_clip,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int camera_roll)
{
#ifdef TORIDRAW_NEAR_CLIP_STATS
    struct ProjectedVertex const center_projection = *center_projection_in;
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
#else
    (void)scene;
    (void)hnd;
    (void)position;
    (void)camera;
    (void)center_projection_in;
    (void)proj_bc;
    (void)may_clip;
    (void)model_pitch;
    (void)model_yaw;
    (void)model_roll;
    (void)camera_roll;
#endif
}

/* ------------------------------------------------------------------ */
/* The raster path: per-face accounting.                              */
/* ------------------------------------------------------------------ */

/** Per-model raster counters: one bucket per skip reason plus drawn faces.
 *  Mirrors the sort-side ToriDraw_FaceSortDebugStats, same env-var pattern. */
struct ToriDraw_RasterDebugStats
{
    int drawn;
    int skipped_type;       /* face_infos raw_type == 2 (hidden) or outside 0..3 */
    int skipped_hidden;     /* color_c == TORIDRAWHSL16_HIDDEN sentinel */
    int skipped_alpha;      /* non-textured face with alpha <= 1 (fully transparent) */
    int skipped_near_clip;  /* near-clipped vertex but !allow_near_clip or no ortho buf */
    int skipped_tex_miss;   /* face_textures != -1 but texture not yet loaded */
    int skipped_tex_coord;  /* malformed explicit coord/frame indices */
    int index_oob;          /* face vertex index >= num_vertices (logic error) */
    int type_hist[16];      /* histogram of raw face_infos values 0..15 */

    /* Which kernel each drawn face actually reached, so a wrong-pixels report
     * can be attributed to shading or to texturing without guessing. */
    int drawn_gouraud;
    int drawn_flat;
    int drawn_textured;
    /* Largest per-face hsl16 vertex delta among drawn gouraud faces. The
     * colour gradient multiplies this by dy, so it decides whether the
     * barycentric step can overflow. */
    int max_color_delta;
    /* Distinct texture ids the drawn faces referenced (first few only). */
    int tex_ids[8];
    int tex_id_count;
};

static inline void
toridraw_raster_debug_note_texture(struct ToriDraw_RasterDebugStats* s, int texture_id)
{
    for( int i = 0; i < s->tex_id_count; i++ )
        if( s->tex_ids[i] == texture_id )
            return;
    if( s->tex_id_count < (int)(sizeof(s->tex_ids) / sizeof(s->tex_ids[0])) )
        s->tex_ids[s->tex_id_count++] = texture_id;
}

static inline int
toridraw_raster_debug_level(void)
{
    static int level = -1;
    if( level < 0 )
    {
        const char* value = getenv("TORIDRAW_RASTER_DEBUG");
        if( !value || value[0] == '\0' || value[0] == '0' )
            level = 0;
        else if( value[0] == '2' || strcmp(value, "verbose") == 0 || strcmp(value, "all") == 0 )
            level = 2;
        else
            level = 1;
    }
    return level;
}

/*
 * TORIRS_RASTER_TEX_MODE_DEBUG, resolved once.
 *
 * Its call site sits on the per-face non-plane/affine path, so a per-call
 * getenv() walks environ with a strncmp per entry for every textured triangle
 * in the frame. That measured two million calls in four hundred frames of an
 * idle scene, which is the same reason TORIRS_RASTER_TEX_DEBUG is cached a few
 * hundred lines down.
 */
static inline int
toridraw_raster_tex_mode_debug(void)
{
    static int enabled = -1;
    if( enabled < 0 )
        enabled = getenv("TORIRS_RASTER_TEX_MODE_DEBUG") ? 1 : 0;
    return enabled;
}

static inline bool
toridraw_raster_debug_enabled(void)
{
    if( TORIDRAW_DBG_ENABLED() )
        return true;
    return toridraw_raster_debug_level() != 0;
}

#if defined(TORIDRAW_DEBUG_NDJSON) && TORIDRAW_DEBUG_NDJSON

/**
 * Per-model face accounting: what the raster drew, what it dropped and why.
 *
 * `anomaly` is the caller's own summary of the skip counters, reused rather
 * than recomputed so the record and the TORIDRAW_RASTER_DEBUG print below it
 * cannot disagree about what counts as one.
 */
static void
toridraw_dbg_record_raster_faces(
    const struct ToriDraw_RasterDebugStats* s,
    const struct ToriDraw_RasterDebugFrame* frame,
    bool anomaly)
{
    if( toridraw_dbg_enabled() )
    {
        static int gate_nearclip = 0;
        static int gate_texmiss = 0;
        static int gate_recip = 0;
        static int gate_other = 0;
        static int last_recip_oob = 0;
        bool const recip_oob = g_toridraw_clip_recip_oob != last_recip_oob;
        bool const near_clip_loss = s->skipped_near_clip > 0;
        bool const tex_loss = s->skipped_tex_miss > 0;
        bool const big = frame->num_faces >= 2000;
        const char* hyp = recip_oob ? "F" : (near_clip_loss ? "B" : (tex_loss ? "E" : "BE"));
        bool emit = false;

        last_recip_oob = g_toridraw_clip_recip_oob;
        if( recip_oob )
            emit = toridraw_dbg_gate(&gate_recip, 200);
        else if( near_clip_loss )
            emit = toridraw_dbg_gate(&gate_nearclip, 200);
        else if( tex_loss )
            emit = toridraw_dbg_gate(&gate_texmiss, 200);
        else if( big || anomaly )
            emit = toridraw_dbg_gate(&gate_other, 120);

        if( emit )
        {
            char data[900];
            char tex_buf[96];
            int tex_pos = 0;

            tex_buf[0] = '\0';
            for( int i = 0; i < s->tex_id_count && tex_pos < (int)sizeof(tex_buf) - 10; i++ )
                tex_pos += snprintf(
                    tex_buf + tex_pos,
                    sizeof(tex_buf) - (size_t)tex_pos,
                    "%s%d",
                    i ? "," : "",
                    s->tex_ids[i]);

            snprintf(
                data,
                sizeof(data),
                "{\"model\":\"%p\",\"faces\":%d,\"vertices\":%d,\"ordered\":%d,"
                "\"drawn\":%d,\"skip_type\":%d,\"skip_hidden\":%d,\"skip_alpha\":%d,"
                "\"skip_near_clip\":%d,\"skip_tex_miss\":%d,\"skip_tex_coord\":%d,"
                "\"index_oob\":%d,"
                "\"allow_near_clip\":%d,\"near_clipped\":%d,"
                "\"near_plane_z\":%d,\"clip_recip_oob\":%d,"
                "\"drawn_gouraud\":%d,\"drawn_flat\":%d,\"drawn_textured\":%d,"
                "\"max_color_delta\":%d,\"tex_ids\":[%s],"
                "\"tex_plane_max_shift\":%d,\"tex_plane_rejected\":%d}",
                frame->model,
                frame->num_faces,
                frame->num_vertices,
                frame->ordered_faces,
                s->drawn,
                s->skipped_type,
                s->skipped_hidden,
                s->skipped_alpha,
                s->skipped_near_clip,
                s->skipped_tex_miss,
                s->skipped_tex_coord,
                s->index_oob,
                frame->allow_near_clip,
                frame->near_clipped,
                frame->near_plane_z,
                g_toridraw_clip_recip_oob,
                s->drawn_gouraud,
                s->drawn_flat,
                s->drawn_textured,
                s->max_color_delta,
                tex_buf,
                g_toridraw_tex_plane_max_shift,
                g_toridraw_tex_plane_rejected);
            toridraw_dbg_log(
                hyp,
                "toridraw_raster.u.c:raster_faces",
                recip_oob
                    ? "near-clip depth span exceeded the reciprocal table"
                    : (near_clip_loss
                           ? "faces dropped: near-clip not allowed for this model"
                           : (tex_loss ? "faces dropped: texture not resident"
                                       : "raster face accounting")),
                data);
        }
    }
}

#define TORIDRAW_DBG_RECORD_RASTER_FACES(s, frame, anomaly) \
    toridraw_dbg_record_raster_faces((s), (frame), (anomaly))

#else

#define TORIDRAW_DBG_RECORD_RASTER_FACES(s, frame, anomaly) \
    ((void)(s), (void)(frame), (void)(anomaly))

#endif /* TORIDRAW_DEBUG_NDJSON */

/*
 * The TORIDRAW_RASTER_DEBUG printer.
 *
 * After the record above, because it calls it: it emits its NDJSON record
 * first and its human line second, from the same counters.
 */
static inline void
toridraw_raster_debug_print(
    const struct ToriDraw_RasterDebugStats* s,
    const struct ToriDraw_RasterDebugFrame* frame)
{
    /* Level 1: only report models with anomalies.
     * Level 2 (verbose): report every model. */
    bool anomaly = s->skipped_type > 0 || s->skipped_hidden > 0 || s->skipped_alpha > 0 ||
                   s->skipped_near_clip > 0 || s->skipped_tex_miss > 0 ||
                   s->skipped_tex_coord > 0 || s->index_oob > 0;

    TORIDRAW_DBG_RECORD_RASTER_FACES(s, frame, anomaly);

    if( toridraw_raster_debug_level() < 2 && !anomaly )
        return;

    /* Compact type histogram: only print buckets that are non-zero. */
    char hist_buf[128];
    int hist_pos = 0;
    hist_buf[0] = '\0';
    for( int t = 0; t < 16 && hist_pos < (int)sizeof(hist_buf) - 12; t++ )
    {
        if( s->type_hist[t] )
            hist_pos +=
                snprintf(hist_buf + hist_pos, sizeof(hist_buf) - (size_t)hist_pos,
                         " t%d=%d", t, s->type_hist[t]);
    }

    fprintf(
        stderr,
        "raster_face: model=%p faces=%d vertices=%d "
        "drawn=%d skip_type=%d skip_hidden=%d skip_alpha=%d "
        "skip_near_clip=%d skip_tex_miss=%d skip_tex_coord=%d index_oob=%d "
        "allow_near_clip=%d near_clipped=%d face_infos=%s type_hist[%s]\n",
        frame->model,
        frame->num_faces,
        frame->num_vertices,
        s->drawn,
        s->skipped_type,
        s->skipped_hidden,
        s->skipped_alpha,
        s->skipped_near_clip,
        s->skipped_tex_miss,
        s->skipped_tex_coord,
        s->index_oob,
        frame->allow_near_clip,
        frame->near_clipped,
        frame->has_face_infos ? "yes" : "no",
        hist_buf);
}

/*
 * The remaining TORIDRAW_RASTER_DEBUG reporters.
 *
 * Each was an `if( gate ) fprintf(...)` inline in the per-face path -- one of
 * them in the middle of texture preparation, where the formatting was longer
 * than the code it described. The gate now lives with the message.
 */

#ifndef TORIDRAW_PIXEL16
static inline void
toridraw_raster_note_texture_miss(int texture_id)
{
    static int skip_tally[TORIDRAW_TEXTURE_ID_CAPACITY];
    static int skip_total;
    static int debug_enabled = -1;

    if( debug_enabled < 0 )
        debug_enabled = getenv("TORIRS_RASTER_TEX_DEBUG") ? 1 : 0;
    if( !debug_enabled || texture_id < 0 || texture_id >= TORIDRAW_TEXTURE_ID_CAPACITY )
        return;

    if( ++skip_tally[texture_id] == 1 )
        fprintf(stderr, "raster_tex_skip: first miss id=%d\n", texture_id);
    if( ++skip_total % 500 == 1 )
        fprintf(stderr,
                "raster_tex_skip: total=%d id=%d (count=%d)\n",
                skip_total,
                texture_id,
                skip_tally[texture_id]);
}
#endif

/** A face index outside the model. A logic error, not a content problem, so it
 *  is reported at verbose level and counted either way. */
static void
toridraw_dbg_report_index_oob(
    struct ToriDraw_RasterDebugStats* dbg,
    const void* model_key,
    int num_vertices,
    int face,
    const int* vertex)
{
    if( !dbg )
        return;
    if( vertex[0] >= 0 && vertex[0] < num_vertices && vertex[1] >= 0 &&
        vertex[1] < num_vertices && vertex[2] >= 0 && vertex[2] < num_vertices )
        return;

    dbg->index_oob++;
    if( toridraw_raster_debug_level() < 2 )
        return;
    fprintf(
        stderr,
        "raster_index_oob: model=%p face=%d indices=(%d,%d,%d) num_vertices=%d\n",
        model_key,
        face,
        vertex[0],
        vertex[1],
        vertex[2],
        num_vertices);
}

/** Which stock render type a textured face resolved to. Capped at 32 lines:
 *  the question is which types a scene contains, not how many faces have one. */
static void
toridraw_dbg_report_tex_mode(int face, int coord, unsigned int render_type)
{
    static int complex_debug_count;

    if( render_type == 0 || !toridraw_raster_tex_mode_debug() )
        return;
    if( complex_debug_count++ >= 32 )
        return;
    fprintf(
        stderr,
        "raster_tex_mode: face=%d coord=%d type=%u affine=1\n",
        face,
        coord >= 0 ? coord : face,
        render_type);
}

/* ------------------------------------------------------------------ */
/* The call-site macros.                                              */
/* ------------------------------------------------------------------ */

/*
 * What the render and raster paths actually contain, in place of the code
 * above. Every one of them has a twin in the #else below that expands to a
 * constant or to nothing, which is how the facility leaves no branch, no
 * struct field and no parameter behind in a build that did not ask for it.
 *
 * Some are declaration slots rather than statements -- they carry their own
 * semicolon and are written bare on a line. TORIDRAW_DBG_SORT_PARAM and
 * TORIDRAW_DBG_SORT_ARG are a matched pair: they add and pass the same
 * parameter, and the counters below take it back as `dbg`, so nothing in a
 * disabled build names a variable that is no longer declared.
 */

#define TORIDRAW_DBG_SAFE_NEAR_ENABLED() toridraw_dbg_safe_near_enabled()

/* -- the face sort: signature, storage, and the per-face counters -- */

#define TORIDRAW_DBG_SORT_PARAM struct ToriDraw_FaceSortDebugStats* debug_stats,
#define TORIDRAW_DBG_SORT_ARG   debug_stats,
#define TORIDRAW_DBG_SORT_LOCALS \
    struct ToriDraw_FaceSortDebugStats debug_stats_storage; \
    struct ToriDraw_FaceSortDebugStats* debug_stats = NULL;
#define TORIDRAW_DBG_SORT_ARM() \
    do \
    { \
        if( toridraw_sort_debug_enabled() ) \
        { \
            toridraw_face_sort_debug_init(&debug_stats_storage); \
            debug_stats = &debug_stats_storage; \
        } \
    } while( 0 )
#define TORIDRAW_DBG_SORT_ARMED() (debug_stats != NULL)

#define TORIDRAW_DBG_SORT_FRONT(dbg, clip_candidate, depth_avg) \
    do \
    { \
        if( dbg ) \
        { \
            (dbg)->front_facing++; \
            if( clip_candidate ) \
                (dbg)->near_clip_candidates++; \
            if( (depth_avg) < (dbg)->min_depth_seen ) \
                (dbg)->min_depth_seen = (depth_avg); \
            if( (depth_avg) > (dbg)->max_depth_seen ) \
                (dbg)->max_depth_seen = (depth_avg); \
        } \
    } while( 0 )

#define TORIDRAW_DBG_SORT_ACCEPTED(dbg, occupancy) \
    do \
    { \
        if( dbg ) \
        { \
            (dbg)->accepted++; \
            if( (occupancy) > (dbg)->max_bucket_occupancy ) \
                (dbg)->max_bucket_occupancy = (occupancy); \
        } \
    } while( 0 )

#define TORIDRAW_DBG_SORT_OVERFLOW(dbg) \
    do \
    { \
        if( dbg ) \
            (dbg)->bucket_overflow++; \
    } while( 0 )

#define TORIDRAW_DBG_SORT_DEPTH_OUT(dbg, face, depth_avg) \
    do \
    { \
        if( dbg ) \
        { \
            if( (depth_avg) < 0 ) \
                (dbg)->depth_out_low++; \
            else \
                (dbg)->depth_out_high++; \
            if( (dbg)->first_depth_out_face < 0 ) \
            { \
                (dbg)->first_depth_out_face = (face); \
                (dbg)->first_depth_out_value = (depth_avg); \
            } \
        } \
    } while( 0 )

#define TORIDRAW_DBG_SORT_CULLED(dbg, winding) \
    do \
    { \
        if( dbg ) \
        { \
            if( (winding) == 0 ) \
                (dbg)->degenerate++; \
            else \
                (dbg)->back_facing++; \
        } \
    } while( 0 )

/** Every front-facing face landed in exactly one bucket. */
#define TORIDRAW_DBG_SORT_TOTALS(dbg) \
    do \
    { \
        if( dbg ) \
            assert( \
                (dbg)->front_facing == (dbg)->accepted + (dbg)->depth_out_low + \
                                           (dbg)->depth_out_high + (dbg)->bucket_overflow); \
    } while( 0 )

#define TORIDRAW_DBG_SORT_PRINT(dbg, scene, hnd, ordered) \
    do \
    { \
        if( dbg ) \
            toridraw_face_sort_debug_print((scene), (hnd), (dbg), (ordered)); \
    } while( 0 )

/** Hands the whole model to the instrumented loop and RETURNS its answer, so
 *  the four production variants below it are never entered. */
#define TORIDRAW_DBG_SORT_SMALL_TAKEOVER( \
    dbg, scene, stash_xy, near_clipped, model_min_depth, num_faces, vx, vy, vz, fa, fb, fc) \
    do \
    { \
        if( dbg ) \
            return bucket_sort_by_average_depth_small_stats( \
                (scene), (dbg), (stash_xy), (near_clipped), (model_min_depth), (num_faces), \
                (vx), (vy), (vz), (fa), (fb), (fc)); \
    } while( 0 )

/* -- the render path: the once-per-model reports -- */

#define TORIDRAW_DBG_SORT_COUNTS(counts, flex, avg12, avg34, avg68) \
    toridraw_dbg_report_sort_counts((counts), (flex), (avg12), (avg34), (avg68))
#define TORIDRAW_DBG_SPAN_RATIO(num_faces, min_d, max_d) \
    toridraw_dbg_report_span_ratio((num_faces), (min_d), (max_d))
#define TORIDRAW_DBG_CAPACITY_REJECT(scene, hnd) \
    toridraw_dbg_report_capacity_reject((scene), (hnd))
#define TORIDRAW_DBG_DEPTH_CAPACITY(scene, hnd) \
    do \
    { \
        if( toridraw_sort_debug_enabled() ) \
            toridraw_dbg_report_depth_capacity((scene), (hnd)); \
    } while( 0 )
#define TORIDRAW_DBG_NEAR_CLIP_GATE(may_clip) toridraw_dbg_count_near_clip_gate(may_clip)
#define TORIDRAW_DBG_PROJECTION_PRINT( \
    scene, hnd, position, view_port, camera, center_z, may_clip) \
    toridraw_projection_debug_print( \
        (scene), (hnd), (position), (view_port), (camera), (center_z), (may_clip))
#define TORIDRAW_DBG_VERIFY_NEAR_CLIP( \
    scene, hnd, position, camera, center, proj_bc, may_clip, mpitch, myaw, mroll, croll) \
    toridraw_dbg_verify_near_clip_gate( \
        (scene), (hnd), (position), (camera), (center), (proj_bc), (may_clip), (mpitch), \
        (myaw), (mroll), (croll))

/* -- the raster path: the context slot, the storage, and the counters -- */

/* Non-NULL only while the raster counters are armed; ordered_faces is what the
 * per-face walk published for the report to describe. */
#define TORIDRAW_DBG_RASTER_CONTEXT_FIELDS \
    struct ToriDraw_RasterDebugStats* raster_debug; \
    int ordered_faces;
#define TORIDRAW_DBG_RASTER_STORAGE       struct ToriDraw_RasterDebugStats raster_debug_storage;
#define TORIDRAW_DBG_RASTER_STORAGE_PARAM , struct ToriDraw_RasterDebugStats* raster_debug_storage
#define TORIDRAW_DBG_RASTER_STORAGE_ARG   , &raster_debug_storage
#define TORIDRAW_DBG_RASTER_ARM(ctx) \
    do \
    { \
        if( toridraw_raster_debug_enabled() ) \
        { \
            memset(raster_debug_storage, 0, sizeof(*raster_debug_storage)); \
            (ctx)->raster_debug = raster_debug_storage; \
        } \
        else \
            (ctx)->raster_debug = NULL; \
    } while( 0 )
#define TORIDRAW_DBG_RASTER_DISARM(ctx)     ((ctx)->raster_debug = NULL)
#define TORIDRAW_DBG_RASTER_ARMED(ctx)      ((ctx)->raster_debug != NULL)
#define TORIDRAW_DBG_RASTER_LOCAL(ctx)      struct ToriDraw_RasterDebugStats* dbg = (ctx)->raster_debug;
#define TORIDRAW_DBG_RASTER_ORDERED(ctx, n) ((ctx)->ordered_faces = (n))

#define TORIDRAW_DBG_RASTER_TYPE(dbg, raw_type) \
    do \
    { \
        if( (dbg) && (raw_type) >= 0 && (raw_type) < 16 ) \
            (dbg)->type_hist[(raw_type)]++; \
    } while( 0 )

#define TORIDRAW_DBG_RASTER_BUMP(dbg, field) \
    do \
    { \
        if( dbg ) \
            (dbg)->field++; \
    } while( 0 )

#define TORIDRAW_DBG_RASTER_INDEX_OOB(dbg, ctx, face, vertex) \
    toridraw_dbg_report_index_oob( \
        (dbg), (const void*)(ctx)->face_indices_a, (ctx)->num_vertices, (face), (vertex))
#define TORIDRAW_DBG_RASTER_TEX_MODE(face, coord, render_type) \
    toridraw_dbg_report_tex_mode((face), (coord), (render_type))
#define TORIDRAW_DBG_RASTER_TEX_MISS(texture_id) toridraw_raster_note_texture_miss(texture_id)

#define TORIDRAW_DBG_RASTER_DREW_TEXTURED(dbg, texture_id) \
    do \
    { \
        if( dbg ) \
        { \
            (dbg)->drawn++; \
            (dbg)->drawn_textured++; \
            toridraw_raster_debug_note_texture((dbg), (texture_id)); \
        } \
    } while( 0 )

#define TORIDRAW_DBG_RASTER_DREW_SHADED(dbg, flat, color_a, color_b, color_c) \
    do \
    { \
        if( dbg ) \
        { \
            (dbg)->drawn++; \
            if( flat ) \
                (dbg)->drawn_flat++; \
            else \
            { \
                int const dbg_dab_ = abs((color_a) - (color_b)); \
                int const dbg_dbc_ = abs((color_b) - (color_c)); \
                int const dbg_dca_ = abs((color_c) - (color_a)); \
                int const dbg_worst_ = dbg_dab_ > dbg_dbc_ \
                                           ? (dbg_dab_ > dbg_dca_ ? dbg_dab_ : dbg_dca_) \
                                           : (dbg_dbc_ > dbg_dca_ ? dbg_dbc_ : dbg_dca_); \
                (dbg)->drawn_gouraud++; \
                if( dbg_worst_ > (dbg)->max_color_delta ) \
                    (dbg)->max_color_delta = dbg_worst_; \
            } \
        } \
    } while( 0 )

/** The one site that has the raster context in hand, so it is the one site
 *  that copies out of it -- see struct ToriDraw_RasterDebugFrame. */
#define TORIDRAW_DBG_RASTER_PRINT(ctx, model_ptr) \
    do \
    { \
        if( (ctx)->raster_debug ) \
        { \
            struct ToriDraw_RasterDebugFrame const dbg_frame_ = { \
                (model_ptr),          (ctx)->num_faces,           (ctx)->num_vertices, \
                (ctx)->ordered_faces, (ctx)->near_plane_z,        (int)(ctx)->allow_near_clip, \
                (int)(ctx)->near_clipped, (ctx)->face_infos ? 1 : 0 \
            }; \
            toridraw_raster_debug_print((ctx)->raster_debug, &dbg_frame_); \
        } \
    } while( 0 )

#else /* !TORIDRAW_DEBUG_STATS */

/*
 * Every macro above, as a constant or as nothing.
 *
 * Nothing here may consume `dbg` or `debug_stats`: in this build those
 * variables were never declared, and that is the whole point. Arguments are
 * consumed only where they are ordinary locals that nothing else reads --
 * `bias` is computed for the draw-order check and for nothing else, so a build
 * without the check must still count it as used.
 *
 * TORIDRAW_DBG_SAFE_NEAR_ENABLED is 1 rather than 0: the raised near plane is
 * the SHIPPING behaviour, and the env knob only ever took it away.
 */

#define TORIDRAW_DBG_SAFE_NEAR_ENABLED() 1

#define TORIDRAW_DBG_SORT_PARAM
#define TORIDRAW_DBG_SORT_ARG
#define TORIDRAW_DBG_SORT_LOCALS
#define TORIDRAW_DBG_SORT_ARM()   ((void)0)
#define TORIDRAW_DBG_SORT_ARMED() 0

#define TORIDRAW_DBG_SORT_FRONT(dbg, clip_candidate, depth_avg)  ((void)0)
#define TORIDRAW_DBG_SORT_ACCEPTED(dbg, occupancy)               ((void)0)
#define TORIDRAW_DBG_SORT_OVERFLOW(dbg)                          ((void)0)
#define TORIDRAW_DBG_SORT_DEPTH_OUT(dbg, face, depth_avg)        ((void)0)
#define TORIDRAW_DBG_SORT_CULLED(dbg, winding)                   ((void)0)
#define TORIDRAW_DBG_SORT_TOTALS(dbg)                            ((void)0)
#define TORIDRAW_DBG_SORT_PRINT(dbg, scene, hnd, ordered)        ((void)0)
#define TORIDRAW_DBG_SORT_SMALL_TAKEOVER( \
    dbg, scene, stash_xy, near_clipped, model_min_depth, num_faces, vx, vy, vz, fa, fb, fc) \
    ((void)0)

#define TORIDRAW_DBG_SORT_COUNTS(counts, flex, avg12, avg34, avg68) ((void)0)
#define TORIDRAW_DBG_SPAN_RATIO(num_faces, min_d, max_d)            ((void)0)
#define TORIDRAW_DBG_CAPACITY_REJECT(scene, hnd)                    ((void)0)
#define TORIDRAW_DBG_DEPTH_CAPACITY(scene, hnd)                     ((void)0)
#define TORIDRAW_DBG_NEAR_CLIP_GATE(may_clip)                       ((void)0)
#define TORIDRAW_DBG_PROJECTION_PRINT( \
    scene, hnd, position, view_port, camera, center_z, may_clip) \
    ((void)0)
#define TORIDRAW_DBG_VERIFY_NEAR_CLIP( \
    scene, hnd, position, camera, center, proj_bc, may_clip, mpitch, myaw, mroll, croll) \
    ((void)0)
/** `bias` exists only for this check; consume it so the build stays quiet. */
#define TORIDRAW_DBG_CHECK_FACE_ORDER(scene, hnd, priorities, face_count, bias) ((void)(bias))

#define TORIDRAW_DBG_RASTER_CONTEXT_FIELDS
#define TORIDRAW_DBG_RASTER_STORAGE
#define TORIDRAW_DBG_RASTER_STORAGE_PARAM
#define TORIDRAW_DBG_RASTER_STORAGE_ARG
#define TORIDRAW_DBG_RASTER_ARM(ctx)        ((void)0)
#define TORIDRAW_DBG_RASTER_DISARM(ctx)     ((void)0)
#define TORIDRAW_DBG_RASTER_ARMED(ctx)      0
#define TORIDRAW_DBG_RASTER_LOCAL(ctx)
#define TORIDRAW_DBG_RASTER_ORDERED(ctx, n) ((void)0)

#define TORIDRAW_DBG_RASTER_TYPE(dbg, raw_type)                       ((void)0)
#define TORIDRAW_DBG_RASTER_BUMP(dbg, field)                          ((void)0)
#define TORIDRAW_DBG_RASTER_INDEX_OOB(dbg, ctx, face, vertex)         ((void)0)
#define TORIDRAW_DBG_RASTER_TEX_MODE(face, coord, render_type)        ((void)0)
#define TORIDRAW_DBG_RASTER_TEX_MISS(texture_id)                      ((void)0)
#define TORIDRAW_DBG_RASTER_DREW_TEXTURED(dbg, texture_id)            ((void)0)
#define TORIDRAW_DBG_RASTER_DREW_SHADED(dbg, flat, ca, cb, cc)        ((void)0)
#define TORIDRAW_DBG_RASTER_PRINT(ctx, model_ptr)                     ((void)0)

#endif /* TORIDRAW_DEBUG_STATS */

#endif /* TORIDRAW_DEBUG_U_C */
