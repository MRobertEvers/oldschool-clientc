#include "editor/loc_editor_selection.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

void
LocEditorSelection_Reset(struct LocEditorSelection* selection)
{
    assert(selection);
    memset(selection, 0, sizeof(*selection));
    selection->loc_id = -1;
    selection->shape = -1;
    selection->scene_x = -1;
    selection->scene_z = -1;
    selection->hover_x = -1;
    selection->hover_z = -1;
}

enum LocEditorSubject
LocEditorSelection_Subject(struct LocEditorSelection const* selection)
{
    assert(selection);
    if( selection->terrain )
        return LOC_EDITOR_SUBJECT_TILE;
    return selection->loc_id >= 0 ? LOC_EDITOR_SUBJECT_LOC : LOC_EDITOR_SUBJECT_NONE;
}

void
LocEditorSelection_SelectLoc(
    struct LocEditorSelection* selection,
    struct LocEditorLocPlacement const* placement)
{
    assert(selection);
    assert(placement);

    /* The two subjects are exclusive: one panel, one set of rows. */
    selection->terrain = false;
    selection->loc_id = placement->loc_id;
    selection->shape = placement->shape;
    selection->angle = placement->angle;
    selection->size_x = placement->size_x;
    selection->size_z = placement->size_z;
    selection->interactive = placement->interactive;
    snprintf(selection->name, sizeof(selection->name), "%s", placement->name ? placement->name : "");
    selection->scene_x = placement->scene_x;
    selection->scene_z = placement->scene_z;
    selection->level = placement->level;
}

void
LocEditorSelection_SelectTile(
    struct LocEditorSelection* selection,
    int scene_x,
    int scene_z,
    int cache_level)
{
    assert(selection);

    selection->terrain = true;
    /* No loc id, so every rule below that asks "is there a loc to move" says
     * no without needing to know about tiles at all. */
    selection->loc_id = -1;
    selection->scene_x = scene_x;
    selection->scene_z = scene_z;
    selection->terrain_level = cache_level;
    selection->level = cache_level;
}

void
LocEditorSelection_Clear(struct LocEditorSelection* selection)
{
    assert(selection);
    selection->loc_id = -1;
    selection->terrain = false;
}

bool
LocEditorSelection_Nudge(
    struct LocEditorSelection* selection,
    int dx,
    int dz,
    struct LocEditorMove* out_move)
{
    assert(selection);
    assert(out_move);

    if( selection->loc_id < 0 )
        return false;

    out_move->loc_id = selection->loc_id;
    out_move->shape = selection->shape;
    out_move->level = selection->level;
    out_move->from_x = selection->scene_x;
    out_move->from_z = selection->scene_z;
    out_move->from_angle = selection->angle;
    out_move->to_x = selection->scene_x + dx;
    out_move->to_z = selection->scene_z + dz;
    /* A nudge does not turn anything. Reported anyway so the caller places
     * with one field rather than choosing between two. */
    out_move->to_angle = selection->angle;

    /* Advanced only now that the move is described, so the next nudge starts
     * from where this one finished. */
    selection->scene_x = out_move->to_x;
    selection->scene_z = out_move->to_z;
    return true;
}

bool
LocEditorSelection_Rotate(struct LocEditorSelection* selection, struct LocEditorMove* out_move)
{
    assert(selection);
    assert(out_move);

    if( selection->loc_id < 0 )
        return false;

    out_move->loc_id = selection->loc_id;
    out_move->shape = selection->shape;
    out_move->level = selection->level;
    out_move->from_x = selection->scene_x;
    out_move->from_z = selection->scene_z;
    out_move->from_angle = selection->angle;
    /* Same tile: placing over it replaces whatever is there, so no remove leg
     * is needed and reporting a different `to` would delete the loc. */
    out_move->to_x = selection->scene_x;
    out_move->to_z = selection->scene_z;
    out_move->to_angle = (selection->angle + 1) % LOC_EDITOR_ANGLE_COUNT;

    selection->angle = out_move->to_angle;
    return true;
}

void
LocEditorSelection_NoteHover(
    struct LocEditorSelection* selection,
    bool cursor_over_panel,
    int tile_x,
    int tile_z)
{
    assert(selection);

    /* Over the panel the world hover names whatever the panel is sitting on
     * top of, which is not what the user was pointing at -- and by the time a
     * click on Reselect lands, the cursor is necessarily over the panel. */
    if( cursor_over_panel )
        return;
    /* Off the world entirely -- the sky, or a gap -- is not a new target
     * either. Taking it would drop the last real one for a tile that is not
     * there. */
    if( tile_x < 0 || tile_z < 0 )
        return;
    selection->hover_x = tile_x;
    selection->hover_z = tile_z;
}

bool
LocEditorSelection_HoverTile(
    struct LocEditorSelection const* selection,
    int* out_tile_x,
    int* out_tile_z)
{
    assert(selection);
    assert(out_tile_x);
    assert(out_tile_z);

    if( selection->hover_x < 0 || selection->hover_z < 0 )
        return false;
    *out_tile_x = selection->hover_x;
    *out_tile_z = selection->hover_z;
    return true;
}

/* ---- the move gizmo -------------------------------------------------------- */

#define GIZMO_PI 3.14159265358979323846

static int
gizmo_is_arrow(enum LocEditorGizmoHandle handle)
{
    return handle >= LOC_EDITOR_GIZMO_ARROW_N && handle <= LOC_EDITOR_GIZMO_ARROW_E;
}

void
LocEditorGizmo_ArrowAxis(
    enum LocEditorGizmoHandle handle,
    int* out_dx,
    int* out_dz)
{
    static const int k_axis[4][2] = { { 0, 1 }, { 0, -1 }, { -1, 0 }, { 1, 0 } };

    assert(gizmo_is_arrow(handle));
    assert(out_dx);
    assert(out_dz);
    *out_dx = k_axis[handle - LOC_EDITOR_GIZMO_ARROW_N][0];
    *out_dz = k_axis[handle - LOC_EDITOR_GIZMO_ARROW_N][1];
}

void
LocEditorGizmo_World(
    int tile_x,
    int tile_z,
    int size_x,
    int size_z,
    struct LocEditorGizmoWorld* out)
{
    int const t = LOC_EDITOR_GIZMO_TILE;
    int half_x;
    int half_z;
    int reach;

    assert(size_x >= 1);
    assert(size_z >= 1);
    assert(out);

    half_x = size_x * t / 2;
    half_z = size_z * t / 2;
    out->centre_x = tile_x * t + half_x;
    out->centre_z = tile_z * t + half_z;
    for( int a = 0; a < 4; a++ )
    {
        int dx;
        int dz;
        int half;
        LocEditorGizmo_ArrowAxis((enum LocEditorGizmoHandle)(LOC_EDITOR_GIZMO_ARROW_N + a), &dx, &dz);
        half = dx != 0 ? half_x : half_z;
        /* The base a quarter tile outside the footprint, so the arrow never
         * sits over the model it moves; the tip one tile further, which is
         * the step the arrow makes. */
        out->arrow_base_x[a] = out->centre_x + dx * (half + t / 4);
        out->arrow_base_z[a] = out->centre_z + dz * (half + t / 4);
        out->arrow_tip_x[a] = out->arrow_base_x[a] + dx * t;
        out->arrow_tip_z[a] = out->arrow_base_z[a] + dz * t;
    }
    /* The ring between the footprint and the arrows' bases. */
    reach = half_x > half_z ? half_x : half_z;
    out->ring_radius = reach + t / 8;
}

/** Squared distance from (px, py) to the segment (x0, y0)-(x1, y1). */
static double
gizmo_segment_d2(
    int px,
    int py,
    int x0,
    int y0,
    int x1,
    int y1)
{
    double const vx = (double)(x1 - x0);
    double const vy = (double)(y1 - y0);
    double const wx = (double)(px - x0);
    double const wy = (double)(py - y0);
    double const len2 = vx * vx + vy * vy;
    double t = len2 > 0.0 ? (wx * vx + wy * vy) / len2 : 0.0;
    double ex;
    double ey;

    if( t < 0.0 )
        t = 0.0;
    if( t > 1.0 )
        t = 1.0;
    ex = wx - t * vx;
    ey = wy - t * vy;
    return ex * ex + ey * ey;
}

enum LocEditorGizmoHandle
LocEditorGizmo_HitTest(
    const struct LocEditorGizmoScreen* screen,
    int x,
    int y,
    int slop)
{
    double const reach2 = (double)slop * (double)slop;
    double const centre_reach = (double)slop * 1.5;

    assert(screen);
    assert(slop > 0);
    if( !screen->valid )
        return LOC_EDITOR_GIZMO_NONE;
    {
        double const dx = (double)(x - screen->centre_x);
        double const dy = (double)(y - screen->centre_y);
        if( dx * dx + dy * dy <= centre_reach * centre_reach )
            return LOC_EDITOR_GIZMO_CENTRE;
    }
    for( int a = 0; a < 4; a++ )
        if( screen->arrow_ok[a]
            && gizmo_segment_d2(x, y, screen->arrow_x0[a], screen->arrow_y0[a], screen->arrow_x1[a],
                   screen->arrow_y1[a])
                   <= reach2 )
            return (enum LocEditorGizmoHandle)(LOC_EDITOR_GIZMO_ARROW_N + a);
    for( int i = 0; screen->ring_count >= 2 && i < screen->ring_count; i++ )
    {
        int const next = (i + 1) % screen->ring_count;
        if( gizmo_segment_d2(x, y, screen->ring_x[i], screen->ring_y[i], screen->ring_x[next], screen->ring_y[next])
            <= reach2 )
            return LOC_EDITOR_GIZMO_RING;
    }
    return LOC_EDITOR_GIZMO_NONE;
}

void
LocEditorGizmo_DragBegin(
    struct LocEditorGizmoDrag* drag,
    enum LocEditorGizmoHandle handle,
    int tile_x,
    int tile_z,
    int centre_x,
    int centre_y,
    int pointer_x,
    int pointer_y,
    int duplicate)
{
    assert(drag);
    assert(handle != LOC_EDITOR_GIZMO_NONE);

    memset(drag, 0, sizeof(*drag));
    drag->handle = handle;
    /* Only an arrow copies: Alt on the ring or the centre is a plain turn
     * or move. */
    drag->duplicate = duplicate && gizmo_is_arrow(handle);
    drag->start_x = tile_x;
    drag->start_z = tile_z;
    drag->centre_x = centre_x;
    drag->centre_y = centre_y;
    drag->last_x = pointer_x;
    drag->last_y = pointer_y;
}

int
LocEditorGizmo_DragUpdate(
    struct LocEditorGizmoDrag* drag,
    int tile_x,
    int tile_z,
    int pointer_x,
    int pointer_y)
{
    int old_dx;
    int old_dz;
    int old_quarters;

    assert(drag);
    assert(drag->handle != LOC_EDITOR_GIZMO_NONE);
    old_dx = drag->dx;
    old_dz = drag->dz;
    old_quarters = LocEditorGizmo_RingQuarters(drag->swept);

    if( gizmo_is_arrow(drag->handle) )
    {
        int ax;
        int az;
        int steps;
        LocEditorGizmo_ArrowAxis(drag->handle, &ax, &az);
        /* Whole tiles passed along the axis; the other axis is ignored, so a
         * shaky hand does not walk the selection sideways. */
        steps = (tile_x - drag->start_x) * ax + (tile_z - drag->start_z) * az;
        if( steps < 0 )
            steps = 0;
        drag->dx = ax * steps;
        drag->dz = az * steps;
    }
    else if( drag->handle == LOC_EDITOR_GIZMO_CENTRE )
    {
        drag->dx = tile_x - drag->start_x;
        drag->dz = tile_z - drag->start_z;
    }
    else
    {
        /* The angle swept since the last point, the short way round, so the
         * total follows the pointer through any number of turns. */
        double const a0 = atan2((double)(drag->last_y - drag->centre_y), (double)(drag->last_x - drag->centre_x));
        double const a1 = atan2((double)(pointer_y - drag->centre_y), (double)(pointer_x - drag->centre_x));
        double delta = a1 - a0;
        int const at_centre = pointer_x == drag->centre_x && pointer_y == drag->centre_y;
        while( delta > GIZMO_PI )
            delta -= 2.0 * GIZMO_PI;
        while( delta <= -GIZMO_PI )
            delta += 2.0 * GIZMO_PI;
        /* Over the centre the angle means nothing; keep the last point. */
        if( !at_centre )
        {
            drag->swept += delta;
            drag->last_x = pointer_x;
            drag->last_y = pointer_y;
        }
    }
    return drag->dx != old_dx || drag->dz != old_dz || LocEditorGizmo_RingQuarters(drag->swept) != old_quarters;
}

int
LocEditorGizmo_RingQuarters(double radians)
{
    double const q = radians / (GIZMO_PI / 2.0);
    return (int)(q < 0.0 ? q - 0.5 : q + 0.5);
}

void
LocEditorGizmo_DragResult(
    const struct LocEditorGizmoDrag* drag,
    struct LocEditorGizmoResult* out)
{
    assert(drag);
    assert(out);

    memset(out, 0, sizeof(*out));
    if( gizmo_is_arrow(drag->handle) )
    {
        out->action = drag->duplicate ? LOC_EDITOR_GIZMO_DO_DUPLICATE : LOC_EDITOR_GIZMO_DO_MOVE;
        out->dx = drag->dx;
        out->dz = drag->dz;
        /* A click on an arrow is its one step. */
        if( out->dx == 0 && out->dz == 0 )
            LocEditorGizmo_ArrowAxis(drag->handle, &out->dx, &out->dz);
        return;
    }
    if( drag->handle == LOC_EDITOR_GIZMO_RING )
    {
        int const raw = LocEditorGizmo_RingQuarters(drag->swept);
        /* A click on the ring (no quarter swept either way) is its one
         * quarter; a whole turn swept is no turn at all. */
        out->quarters = raw == 0 ? 1 : ((raw % 4) + 4) % 4;
        if( out->quarters != 0 )
            out->action = LOC_EDITOR_GIZMO_DO_ROTATE;
        return;
    }
    assert(drag->handle == LOC_EDITOR_GIZMO_CENTRE);
    if( drag->dx == 0 && drag->dz == 0 )
        return;
    out->action = LOC_EDITOR_GIZMO_DO_MOVE;
    out->dx = drag->dx;
    out->dz = drag->dz;
}

int
LocEditorSelection_LevelTarget(
    int level,
    int delta)
{
    int const target = level + delta;

    assert(level >= 0);
    assert(level < 4);
    if( target < 0 || target >= 4 )
        return -1;
    return target;
}
