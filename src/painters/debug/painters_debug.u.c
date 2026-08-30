#ifndef PAINTERS_DEBUG_U_C
#define PAINTERS_DEBUG_U_C

/*
 * Everything in the painter that exists only to describe a paint.
 *
 * One file, because that is the only property worth having here. The wedge
 * log, the draw-order dump, the world-entity descent trace and the two
 * debugger traps used to be spread through painters.c and inlined at some
 * thirty call sites in the bucket drain, where a reader looking for the
 * traversal had to walk past three hundred lines of telemetry to find it. Now
 * the drain carries macro calls and nothing else, and every line of the
 * facility is below.
 *
 * COMPILED OUT unless asked for; see painters_debug.h for the gate and for the
 * run-time switches that select within the facility once a build has it.
 *
 * Included by painters.c, once, below the command encoders and the element
 * accessors it reads, and above painters_bucket.u.c /
 * painters_distancemetric.u.c / painters_world3d.u.c, which are where the call
 * sites are. Nothing here reaches forward into those files: the world-entity
 * trace takes plain ints rather than the bucket's cursor and stack types,
 * precisely so this file does not have to be included after them.
 *
 * Everything here is read-only telemetry: it never mutates painter, tile or
 * command state.
 */

#include "painters_debug.h"

#if PAINTERS_DEBUG

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * Debugger traps. Declared in painters_debug.h, because the command trap is
 * used above this file's include point.
 * ---------------------------------------------------------------------- */
int g_trap_command = -1;
int g_trap_x = -1;
int g_trap_z = -1;

/* ---------------------------------------------------------------------------
 * TORIRS_PAINTER_DUMP=1: print the emitted draw order, grouped by the tile each
 * element sits on, for every tile contributing more than one element.
 * Diagnosing "this loc should be under that one" needs the per-tile sequence
 * and each element's slot, neither of which survives into
 * PaintersElementCommand (it carries only the scene entity id).
 *
 * TORIRS_PAINTER_DUMP_TILE=sx,sz restricts output to a single tile column.
 * ------------------------------------------------------------------------ */
static const char*
painter_element_kind_name(enum PaintersElementKind kind)
{
    switch( kind )
    {
    case PNTRELEM_GROUND: return "GROUND";
    case PNTRELEM_SCENERY: return "SCENERY";
    case PNTRELEM_WALL_A: return "WALL_A";
    case PNTRELEM_WALL_B: return "WALL_B";
    case PNTRELEM_GROUND_DECOR: return "GROUND_DECOR";
    case PNTRELEM_WALL_DECOR: return "WALL_DECOR";
    case PNTRELEM_GROUND_OBJECT: return "GROUND_OBJECT";
    default: return "INVALID";
    }
}

static void
painter_dump_command_order(
    struct Painter* painter,
    struct PaintersBuffer* buffer)
{
    /* Resolved once: this sits in the per-frame paint path. */
    static int enabled = -1;
    static int only_sx = -1;
    static int only_sz = -1;

    if( enabled < 0 )
    {
        char const* tile_env;
        enabled = getenv("TORIRS_PAINTER_DUMP") != NULL;
        tile_env = getenv("TORIRS_PAINTER_DUMP_TILE");
        if( !tile_env || sscanf(tile_env, "%d,%d", &only_sx, &only_sz) != 2 )
        {
            only_sx = -1;
            only_sz = -1;
        }
    }

    if( !enabled )
        return;

    /* entity id -> element index. Elements are few and this only runs when the env is set, so a
     * linear scan per command beats threading the element index through the command encoding. */
    for( int ci = 0; ci < buffer->command_count; ci++ )
    {
        struct PaintersElementCommand* cmd = &buffer->commands[ci];
        if( cmd->_bf_kind != PNTR_CMD_ELEMENT )
            continue;

        for( int ei = 0; ei < painter->element_count; ei++ )
        {
            struct PaintersElement* el = &painter->elements[ei];
            int entity = -1;

            switch( el->kind )
            {
            case PNTRELEM_SCENERY: entity = el->_scenery.entity; break;
            case PNTRELEM_WALL_A:
            case PNTRELEM_WALL_B: entity = el->_wall.entity; break;
            case PNTRELEM_GROUND_DECOR: entity = el->_ground_decor.entity; break;
            case PNTRELEM_WALL_DECOR: entity = el->_wall_decor.entity; break;
            case PNTRELEM_GROUND_OBJECT: entity = el->_ground_object.entity; break;
            default: continue;
            }

            if( entity != painter_command_element_id(cmd) )
                continue;
            if( only_sx >= 0 && (el->sx != (uint16_t)only_sx || el->sz != (uint16_t)only_sz) )
                break;

            TORIRS_REPORT("PDUMP cmd=%d tile=(%d,%d,L%d) %s entity=%d elem=%d\n",
                ci,
                (int)el->sx,
                (int)el->sz,
                (int)el->source_level,
                painter_element_kind_name(el->kind),
                entity,
                ei);
            break;
        }
    }
    fflush(stdout);
}

/* ---------------------------------------------------------------------------
 * TORIRS_WEDGELOG=<path> — per-frame DRAW ORDER telemetry.
 *
 * Emits the same 7-column schema as the instrumented official rev-239 client
 * (Deobfuscator/instr/src/WedgeLog.java, hooked into class112):
 *
 *     seq  plane  x  z  drawLevel  renderLevel  what  [extra...]
 *
 *   plane        PaintersTile paintgrid_level — the grid slot the traversal is
 *                walking (official: the plane index of the tile array).
 *   drawLevel    PaintersTile visible_gte_level — the level at or above which
 *                the UI floor reveals this tile (official class112.method4161).
 *   renderLevel  the cache level whose mesh / element is actually drawn
 *                (official class112.method4114). For terrain this is the
 *                emitted mesh level; for elements it is the element's
 *                source_level.
 *   what         MARK / SEED / PUSH / POP for traversal machinery, otherwise a
 *                geometry category using the official's vocabulary
 *                (floor, wall_a, wall_b, wall_back_a, wall_back_b, decor,
 *                decor_alt, decor_back, decor_back_alt, grounddecor, item,
 *                item_back, loc, entity, and the `:bridge` variants).
 *
 * TORIRS_WEDGELOG_AT=<n>      capture on the n-th painter_paint_bucket call
 *                             (default 700 — the camera must have settled).
 * TORIRS_WEDGELOG_FRAMES=<n>  number of consecutive frames to record (default 1).
 * ------------------------------------------------------------------------ */
struct PainterWedgeLog
{
    FILE* fp;
    int enabled; /* -1 = unresolved */
    int armed;
    int paint_calls;
    int at;
    int frames_left;
    long seq;
    long paints;
    int eye_x;
    int eye_y;
    int eye_z;
    int vp_w;
    int vp_h;
    int eye_valid;
    const struct Painter* painter;
    char path[512];
};

static struct PainterWedgeLog g_wedgelog = { NULL, -1, 0, 0, 700, 1, 0, 0, 0, 0, 0, 0, 0, 0, NULL,
                                             { 0 } };

void
painter_wedgelog_set_eye(
    int eye_x,
    int eye_y,
    int eye_z,
    int viewport_w,
    int viewport_h)
{
    if( g_wedgelog.enabled == 0 )
        return;
    if( g_wedgelog.enabled < 0 )
    {
        char const* p = getenv("TORIRS_WEDGELOG");
        char const* at = getenv("TORIRS_WEDGELOG_AT");
        char const* fr = getenv("TORIRS_WEDGELOG_FRAMES");
        g_wedgelog.enabled = (p && p[0]) ? 1 : 0;
        if( g_wedgelog.enabled )
        {
            snprintf(g_wedgelog.path, sizeof(g_wedgelog.path), "%s", p);
            if( at && at[0] )
                g_wedgelog.at = atoi(at);
            if( fr && fr[0] )
                g_wedgelog.frames_left = atoi(fr);
        }
        if( !g_wedgelog.enabled )
            return;
    }
    g_wedgelog.eye_x = eye_x;
    g_wedgelog.eye_y = eye_y;
    g_wedgelog.eye_z = eye_z;
    g_wedgelog.vp_w = viewport_w;
    g_wedgelog.vp_h = viewport_h;
    g_wedgelog.eye_valid = 1;
}

/**
 * Close the log for the duration of a paint that is not the bound painter's.
 *
 * Every row the log writes indexes `g_wedgelog.painter->tiles[]`, so a paint
 * context running over a DIFFERENT painter — the descent into a nested world
 * view — must emit no rows at all: its tile indices are meaningless against
 * the bound array, and out of bounds whenever that array is smaller.
 *
 * Disarming for the duration is how that is enforced, rather than a per-row
 * "is this the bound painter" test. The emit helpers already check `armed`,
 * and the drain runs them thousands of times per paint from an always-inlined
 * body; adding a second condition there cost ~5% of the paint stage with the
 * log switched off, which is why it is not done that way even now that the
 * whole facility is compiled out of a shipping build.
 *
 * Pair with painter_wedgelog_resume() on every exit from the nested run.
 */
static inline int
painter_wedgelog_suspend(void)
{
    int saved = g_wedgelog.armed;
    g_wedgelog.armed = 0;
    return saved;
}

static inline void
painter_wedgelog_resume(int saved)
{
    g_wedgelog.armed = saved;
}

/* Opens the sink on the requested paint call and writes the `#frame` /
 * `#path` header. Returns non-zero when this frame is being recorded. */
static int
painter_wedgelog_frame_begin(
    const struct Painter* painter,
    int camera_sx,
    int camera_sz,
    int center_sx,
    int center_sz,
    int min_draw_x,
    int max_draw_x,
    int min_draw_z,
    int max_draw_z,
    int radius,
    unsigned draw_mask)
{
    g_wedgelog.armed = 0;
    if( g_wedgelog.enabled <= 0 )
        return 0;
    g_wedgelog.paint_calls++;
    if( g_wedgelog.paint_calls < g_wedgelog.at )
        return 0;
    if( g_wedgelog.frames_left <= 0 )
        return 0;
    if( !g_wedgelog.fp )
    {
        g_wedgelog.fp = fopen(g_wedgelog.path, "w");
        if( !g_wedgelog.fp )
        {
            g_wedgelog.enabled = 0;
            return 0;
        }
    }
    g_wedgelog.armed = 1;
    g_wedgelog.painter = painter;
    g_wedgelog.seq = 0;
    g_wedgelog.paints = 0;
    g_wedgelog.frames_left--;

    fprintf(g_wedgelog.fp, "#frame %d\n", g_wedgelog.paint_calls);
    fprintf(
        g_wedgelog.fp,
        "#path bucket:painter_paint_bucket dims=%dx%d planes=%d camTile=%d,%d cam=%d,%d,%d "
        "drawCenter=%d,%d window=x[%d,%d)z[%d,%d) drawDist=%d levelMask=0x%x minLevel=%d "
        "vp=%dx%d pitch=%d yaw=%d cullspan=%d occluders=%d\n",
        painter->width,
        painter->height,
        painter->levels,
        camera_sx,
        camera_sz,
        g_wedgelog.eye_x,
        g_wedgelog.eye_y,
        g_wedgelog.eye_z,
        center_sx,
        center_sz,
        min_draw_x,
        max_draw_x,
        min_draw_z,
        max_draw_z,
        radius,
        draw_mask,
        painter->min_level,
        g_wedgelog.vp_w,
        g_wedgelog.vp_h,
        painter->camera_pitch,
        painter->camera_yaw,
        painter->cullspan_active,
        painter->occluders ? painter->occluders->active_count : -1);
    return 1;
}

static void
painter_wedgelog_frame_end(long command_count)
{
    if( !g_wedgelog.armed )
        return;
    fprintf(
        g_wedgelog.fp,
        "#endframe seq=%ld paints=%ld commands=%ld\n",
        g_wedgelog.seq,
        g_wedgelog.paints,
        command_count);
    fflush(g_wedgelog.fp);
    g_wedgelog.armed = 0;
    if( g_wedgelog.frames_left <= 0 )
    {
        fclose(g_wedgelog.fp);
        g_wedgelog.fp = NULL;
        g_wedgelog.enabled = 0;
    }
}

/** Traversal machinery row: `seq plane x z - - KIND extra`. */
static void
painter_wedgelog_event(
    int ti,
    const char* kind,
    const char* extra)
{
    const struct PaintersTile* t;
    if( !g_wedgelog.armed )
        return;
    t = &g_wedgelog.painter->tiles[ti];
    fprintf(
        g_wedgelog.fp,
        "%ld %d %d %d - - %s%s%s\n",
        ++g_wedgelog.seq,
        (int)painters_tile_get_paintgrid_level(t),
        (int)t->sx,
        (int)t->sz,
        kind,
        extra ? " " : "",
        extra ? extra : "");
}

/**
 * The same row, with the `extra` column formatted here.
 *
 * The three traversal sites that carry one (PUSH, SEED, POP) each used to
 * declare a char buffer and snprintf into it inline, in the middle of the
 * drain. The formatting is the log's business, so it lives with the log: the
 * armed test comes first, and a build without the facility does not evaluate
 * the arguments at all.
 */
static void
painter_wedgelog_eventf(
    int ti,
    const char* kind,
    const char* fmt,
    ...)
{
    char extra[64];
    va_list ap;
    if( !g_wedgelog.armed )
        return;
    va_start(ap, fmt);
    vsnprintf(extra, sizeof(extra), fmt, ap);
    va_end(ap);
    painter_wedgelog_event(ti, kind, extra);
}

/** Geometry row: `seq plane x z drawLevel renderLevel what p=<paint#> ...`. */
static void
painter_wedgelog_paint(
    int ti,
    const char* what,
    int render_level,
    int entity,
    int element_idx)
{
    const struct PaintersTile* t;
    if( !g_wedgelog.armed )
        return;
    t = &g_wedgelog.painter->tiles[ti];
    fprintf(
        g_wedgelog.fp,
        "%ld %d %d %d %d %d %s p=%ld ent=%d elem=%d spans=0x%x flags=0x%x\n",
        ++g_wedgelog.seq,
        (int)painters_tile_get_paintgrid_level(t),
        (int)t->sx,
        (int)t->sz,
        (int)painters_tile_get_visible_gte_level(t),
        render_level,
        what,
        ++g_wedgelog.paints,
        entity,
        element_idx,
        (unsigned)t->spans,
        (unsigned)painters_tile_get_flags(t));
}

/* ---------------------------------------------------------------------------
 * `TORIRS_WEV_DEBUG=1` — trace every world-entity descent that is asked for
 * and every one that completes.
 *
 * A world entity that renders nothing is otherwise indistinguishable at four
 * different stages: the pseudo-loc never emitted a BEGIN_WORLD, the request
 * arrived but was refused (unbound view, cycle, full stack), the descent ran
 * but the child painter emitted no commands, or it emitted commands that the
 * drain later dropped. The request/done pair separates the first three.
 *
 * Plain ints rather than the descent's own types, so this file can sit above
 * painters_bucket.u.c rather than below it. @see the file header.
 *
 * Read once, not per descent: this sits on the paint path, where even the
 * getenv is charged every frame of every entity.
 * @see app_wev_debug_enabled
 * ------------------------------------------------------------------------ */
static int
painter_wev_debug_enabled(void)
{
    static int cached = -1;

    if( cached < 0 )
    {
        char const* v = getenv("TORIRS_WEV_DEBUG");

        cached = (v && v[0] && v[0] != '0') ? 1 : 0;
    }
    return cached;
}

static void
painter_wev_debug_request(
    int view_id,
    int active,
    int dup_id,
    int dup_painter,
    int depth,
    int camera_sx,
    int camera_sz,
    int commands)
{
    if( !painter_wev_debug_enabled() )
        return;
    fprintf(
        stderr,
        "wev: DESCEND request view %d active=%d dup_id=%d dup_painter=%d "
        "depth=%d cam %d,%d cmds=%d\n",
        view_id,
        active,
        dup_id,
        dup_painter,
        depth,
        camera_sx,
        camera_sz,
        commands);
}

static void
painter_wev_debug_done(
    int view_id,
    int commands)
{
    if( !painter_wev_debug_enabled() )
        return;
    fprintf(stderr, "wev: DESCEND done view %d emitted %d command(s)\n", view_id, commands);
}

/* -------------------------------------------------------------------------
 * The call-site façade. Everything above is reached through these and nothing
 * else, so the disabled branch below is the whole cost of the facility in a
 * shipping build.
 * ---------------------------------------------------------------------- */
#define PAINTER_DBG_DUMP_ORDER(painter, buffer) painter_dump_command_order((painter), (buffer))

#define PAINTER_DBG_WEDGE_FRAME_BEGIN( \
    painter, camera_sx, camera_sz, center_sx, center_sz, min_x, max_x, min_z, max_z, radius, \
    draw_mask) \
    ((void)painter_wedgelog_frame_begin( \
        (painter), (camera_sx), (camera_sz), (center_sx), (center_sz), (min_x), (max_x), \
        (min_z), (max_z), (radius), (draw_mask)))
#define PAINTER_DBG_WEDGE_FRAME_END(command_count) \
    painter_wedgelog_frame_end((long)(command_count))
#define PAINTER_DBG_WEDGE_EVENT(ti, kind)      painter_wedgelog_event((ti), (kind), NULL)
#define PAINTER_DBG_WEDGE_EVENTF(ti, kind, ...) painter_wedgelog_eventf((ti), (kind), __VA_ARGS__)
#define PAINTER_DBG_WEDGE_PAINT(ti, what, render_level, entity, element_idx) \
    painter_wedgelog_paint((ti), (what), (render_level), (entity), (element_idx))
#define PAINTER_DBG_WEDGE_SUSPEND()      painter_wedgelog_suspend()
#define PAINTER_DBG_WEDGE_RESUME(saved)  painter_wedgelog_resume((saved))

#define PAINTER_DBG_WEV_REQUEST( \
    view_id, active, dup_id, dup_painter, depth, camera_sx, camera_sz, commands) \
    painter_wev_debug_request( \
        (view_id), (active), (dup_id), (dup_painter), (depth), (camera_sx), (camera_sz), \
        (commands))
#define PAINTER_DBG_WEV_DONE(view_id, commands) \
    painter_wev_debug_done((view_id), (commands))

#else /* !PAINTERS_DEBUG */

/* Argument-consuming, so a local that only the log reads is still "used".
 * SUSPEND is a constant rather than nothing: its caller stores the result and
 * hands it back to RESUME, and that pairing is worth keeping visible. */
#define PAINTER_DBG_DUMP_ORDER(painter, buffer) ((void)(painter), (void)(buffer))

#define PAINTER_DBG_WEDGE_FRAME_BEGIN( \
    painter, camera_sx, camera_sz, center_sx, center_sz, min_x, max_x, min_z, max_z, radius, \
    draw_mask) \
    ((void)(painter), (void)(camera_sx), (void)(camera_sz), (void)(center_sx), \
     (void)(center_sz), (void)(min_x), (void)(max_x), (void)(min_z), (void)(max_z), \
     (void)(radius), (void)(draw_mask))
#define PAINTER_DBG_WEDGE_FRAME_END(command_count) ((void)(command_count))
#define PAINTER_DBG_WEDGE_EVENT(ti, kind)         ((void)(ti), (void)(kind))
#define PAINTER_DBG_WEDGE_EVENTF(ti, kind, ...)   ((void)(ti), (void)(kind))
#define PAINTER_DBG_WEDGE_PAINT(ti, what, render_level, entity, element_idx) \
    ((void)(ti), (void)(what), (void)(render_level), (void)(entity), (void)(element_idx))
#define PAINTER_DBG_WEDGE_SUSPEND()     0
#define PAINTER_DBG_WEDGE_RESUME(saved) ((void)(saved))

#define PAINTER_DBG_WEV_REQUEST( \
    view_id, active, dup_id, dup_painter, depth, camera_sx, camera_sz, commands) \
    ((void)(view_id), (void)(active), (void)(dup_id), (void)(dup_painter), (void)(depth), \
     (void)(camera_sx), (void)(camera_sz), (void)(commands))
#define PAINTER_DBG_WEV_DONE(view_id, commands) ((void)(view_id), (void)(commands))

#endif /* PAINTERS_DEBUG */

#endif /* PAINTERS_DEBUG_U_C */
