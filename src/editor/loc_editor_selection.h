#ifndef SRC_EDITOR_LOC_EDITOR_SELECTION_H
#define SRC_EDITOR_LOC_EDITOR_SELECTION_H

/*
 * What the loc editor is pointed at, and what moving it means.
 *
 * One panel, one subject, and the subject is either a LOC or a TILE. They are
 * exclusive because the panel has one set of rows: leave the old one behind
 * and the readout describes a loc while the nudge buttons move a tile, or the
 * other way round, and every number on screen is about something other than
 * what the user selected.
 *
 * The selection survives everything except an explicit change of it. Opening
 * or closing the panel does not clear it, nor does moving the camera, nor does
 * a string of nudges -- a target stays live until Reselect picks a different
 * one or Deselect drops it. An editor that lost its target whenever the panel
 * was toggled would make every multi-step edit a re-selection.
 *
 * A move reports BOTH ENDS. The scene edit is a remove-then-place pair and the
 * authored record needs the before and the after, so the placement cannot
 * simply advance and leave the caller to reconstruct where it came from --
 * reconstructing it is how a recorded edit ends up describing a move from the
 * tile it landed on to itself, which replays as nothing at all.
 *
 * The hover memo is the subtle one. Reselect targets the last tile the cursor
 * was over WHILE NOT OVER THE PANEL, because by the time a click on a panel
 * row lands the cursor has necessarily moved onto the panel -- and the live
 * hover then names whatever is underneath it, which is not what anyone was
 * pointing at.
 *
 * Nothing here reads a world or edits one. The caller resolves what is under
 * the cursor and applies the move; this decides what is selected and what the
 * move is.
 */

#include <stdbool.h>

enum
{
    /** The four config angles: 0..3 = W/N/E/S (entity_scenery.h). */
    LOC_EDITOR_ANGLE_COUNT = 4,
    LOC_EDITOR_NAME_LEN = 64,
};

/** What the panel is describing. */
enum LocEditorSubject
{
    LOC_EDITOR_SUBJECT_NONE,
    LOC_EDITOR_SUBJECT_LOC,
    /** The ground itself: a readout, which is the whole of what a tile offers. */
    LOC_EDITOR_SUBJECT_TILE
};

/** A loc as the world reports it, at the moment it is selected. */
struct LocEditorLocPlacement
{
    int loc_id;
    int shape;
    int angle;
    int size_x;
    int size_z;
    int interactive;
    /** May be NULL for an unnamed loc. */
    char const* name;
    int scene_x;
    int scene_z;
    int level;
};

/**
 * One edit, with both ends.
 *
 * The caller clears `from` and places at `to`; the two are the same tile for a
 * rotate, and the same angle for a nudge.
 */
struct LocEditorMove
{
    int loc_id;
    int shape;
    int level;
    int from_x;
    int from_z;
    int from_angle;
    int to_x;
    int to_z;
    int to_angle;
};

struct LocEditorSelection
{
    /** -1 when nothing is selected, and also when a TILE is. */
    int loc_id;
    int shape;
    int angle;
    int size_x;
    int size_z;
    int interactive;
    char name[LOC_EDITOR_NAME_LEN];
    /** The subject's CURRENT placement, kept in step with every move. */
    int scene_x;
    int scene_z;
    int level;
    /** The subject is the ground rather than a loc. */
    bool terrain;
    /** Cache (mesh) level of the selected tile -- the plane the map authored
     *  that floor on, which on a bridge deck is not the plane it draws at. */
    int terrain_level;
    /** Last world tile the cursor was over while NOT over the panel; -1 = none. */
    int hover_x;
    int hover_z;
};

/** Nothing selected, nothing hovered. */
void
LocEditorSelection_Reset(struct LocEditorSelection* selection);

enum LocEditorSubject
LocEditorSelection_Subject(struct LocEditorSelection const* selection);

/** Point the panel at a loc. Drops any tile selection with it. */
void
LocEditorSelection_SelectLoc(
    struct LocEditorSelection* selection,
    struct LocEditorLocPlacement const* placement);

/** Point the panel at the ground. Drops any loc selection with it. */
void
LocEditorSelection_SelectTile(
    struct LocEditorSelection* selection,
    int scene_x,
    int scene_z,
    int cache_level);

/**
 * Explicit Deselect: drop the target without touching the world.
 *
 * Both subjects, from the one row -- or Deselect appears to do nothing while a
 * tile is up.
 */
void
LocEditorSelection_Clear(struct LocEditorSelection* selection);

/**
 * Move the selected loc by whole tiles.
 *
 * False when there is nothing that can be moved, which includes a TILE
 * selection: the ground does not go anywhere. The placement advances only when
 * the move is real, so the next nudge starts from where this one finished.
 */
bool
LocEditorSelection_Nudge(
    struct LocEditorSelection* selection,
    int dx,
    int dz,
    struct LocEditorMove* out_move);

/**
 * Turn the selected loc to the next config angle.
 *
 * Same tile, so the caller needs no remove leg. False for anything that is not
 * a loc.
 */
bool
LocEditorSelection_Rotate(struct LocEditorSelection* selection, struct LocEditorMove* out_move);

/**
 * Offer the tile under the cursor as the next Reselect target.
 *
 * Ignored while the cursor is over the panel, and while the world reports no
 * tile at all. Call it every frame, panel open or not, so a click on Reselect
 * always has a last-known-good world tile to read.
 */
void
LocEditorSelection_NoteHover(
    struct LocEditorSelection* selection,
    bool cursor_over_panel,
    int tile_x,
    int tile_z);

/** The remembered tile, if there has been one. */
bool
LocEditorSelection_HoverTile(
    struct LocEditorSelection const* selection,
    int* out_tile_x,
    int* out_tile_z);

/* ---- the move gizmo (workbench 6.3, design #placing "Move, rotate, delete") --
 *
 * On the primary selected loc under Select or Move: four arrows, a ring and a
 * centre handle. An arrow drags the whole selection along its axis a tile at
 * a time -- a step each time the pointer passes into the next tile, never
 * back past where it started -- and a click on it is one step; with Alt held
 * the release DUPLICATES the selection at that offset instead of moving it.
 * The ring turns it: the pointer's sweep about the centre, a quarter per 90
 * degrees, clockwise as the screen shows it, and a click is one quarter. The
 * centre drags the selection freely to the tile under the pointer.
 *
 * Nothing moves until the release: the drag only PROPOSES an offset (which
 * the overlay previews as destination footprints, orange where a key is
 * taken and the move would swap), and the release turns it into one op, one
 * undo step. The geometry is computed here in world units and hit-tested in
 * the screen points the caller projected, so the gizmo follows the panel's
 * projection and this file still knows nothing of a camera.
 */

enum LocEditorGizmoHandle
{
    LOC_EDITOR_GIZMO_NONE = 0,
    LOC_EDITOR_GIZMO_ARROW_N,
    LOC_EDITOR_GIZMO_ARROW_S,
    LOC_EDITOR_GIZMO_ARROW_W,
    LOC_EDITOR_GIZMO_ARROW_E,
    LOC_EDITOR_GIZMO_RING,
    LOC_EDITOR_GIZMO_CENTRE,
};

enum
{
    /** Fine world units a tile. */
    LOC_EDITOR_GIZMO_TILE = 128,
    /** Points the ring is drawn and hit-tested through. */
    LOC_EDITOR_GIZMO_RING_POINTS = 16,
    /** Screen pixels a handle reaches past its drawn line (at 1x). */
    LOC_EDITOR_GIZMO_SLOP = 6,
};

/** The gizmo on the ground, in fine world units (scene tile * 128): the
 *  footprint's centre, each arrow's base just outside the footprint and its
 *  tip a tile further (N, S, W, E), the ring's radius. */
struct LocEditorGizmoWorld
{
    int centre_x;
    int centre_z;
    int arrow_base_x[4];
    int arrow_base_z[4];
    int arrow_tip_x[4];
    int arrow_tip_z[4];
    int ring_radius;
};

/** The same, projected by the caller: what the overlay draws and the click
 *  hit-tests. A point the projection could not place leaves its handle out
 *  (`arrow_ok` 0, a ring with fewer points). */
struct LocEditorGizmoScreen
{
    int valid;
    int centre_x;
    int centre_y;
    int arrow_ok[4];
    int arrow_x0[4];
    int arrow_y0[4];
    int arrow_x1[4];
    int arrow_y1[4];
    int ring_count;
    int ring_x[LOC_EDITOR_GIZMO_RING_POINTS];
    int ring_y[LOC_EDITOR_GIZMO_RING_POINTS];
};

/** A press on a handle until its release. */
struct LocEditorGizmoDrag
{
    enum LocEditorGizmoHandle handle;
    /** Alt was held at the press: an arrow's release copies. */
    int duplicate;
    /** The tile under the pointer at the press (scene or world tiles, the
     *  caller's choice; only differences are read). */
    int start_x;
    int start_z;
    /** The offset proposed now (arrows, centre). */
    int dx;
    int dz;
    /** The ring: the screen centre, the pointer's last position and the
     *  angle swept since the press, radians, clockwise on screen positive. */
    int centre_x;
    int centre_y;
    int last_x;
    int last_y;
    double swept;
};

enum LocEditorGizmoAction
{
    LOC_EDITOR_GIZMO_DO_NOTHING = 0,
    /** Move the selection by (dx, dz) (the swap rule applies). */
    LOC_EDITOR_GIZMO_DO_MOVE,
    /** Copy the selection at (dx, dz); the copies become the selection. */
    LOC_EDITOR_GIZMO_DO_DUPLICATE,
    /** Turn the selection by `quarters` (1 = 90 degrees clockwise). */
    LOC_EDITOR_GIZMO_DO_ROTATE,
};

struct LocEditorGizmoResult
{
    enum LocEditorGizmoAction action;
    int dx;
    int dz;
    int quarters;
};

/** The world axis of an arrow handle: N (0, +1), S (0, -1), W (-1, 0),
 *  E (+1, 0). Asserts the handle is an arrow. */
void
LocEditorGizmo_ArrowAxis(
    enum LocEditorGizmoHandle handle,
    int* out_dx,
    int* out_dz);

/** The gizmo of a loc whose footprint is `size_x` x `size_z` tiles from its
 *  SW tile (tile_x, tile_z). Sizes below 1 assert. */
void
LocEditorGizmo_World(
    int tile_x,
    int tile_z,
    int size_x,
    int size_z,
    struct LocEditorGizmoWorld* out);

/** The handle under screen (x, y), `slop` pixels either side of a line:
 *  the centre first (it sits inside the ring), then an arrow, then the
 *  ring. NONE when the gizmo is not valid or nothing is near. */
enum LocEditorGizmoHandle
LocEditorGizmo_HitTest(
    const struct LocEditorGizmoScreen* screen,
    int x,
    int y,
    int slop);

/** A press on `handle` (not NONE) over tile (tile_x, tile_z) at screen
 *  (pointer_x, pointer_y), the gizmo's centre at (centre_x, centre_y). */
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
    int duplicate);

/** The pointer is over tile (tile_x, tile_z) at screen (pointer_x,
 *  pointer_y): an arrow proposes the whole tiles passed along its axis
 *  (never negative), the centre the tile difference, the ring adds the
 *  angle swept. Returns 1 when the proposal (offset or quarters) changed. */
int
LocEditorGizmo_DragUpdate(
    struct LocEditorGizmoDrag* drag,
    int tile_x,
    int tile_z,
    int pointer_x,
    int pointer_y);

/** Quarter turns a sweep of `radians` rounds to (clockwise positive). */
int
LocEditorGizmo_RingQuarters(double radians);

/** What the release does: an arrow moves (or with Alt copies) by its offset,
 *  one step when it never left its first tile (a click); the ring turns by
 *  its quarters, one when it never swept a quarter (a click); the centre
 *  moves by its offset, nothing when it never left its tile. */
void
LocEditorGizmo_DragResult(
    const struct LocEditorGizmoDrag* drag,
    struct LocEditorGizmoResult* out);

/** The plane a selection on `level` lands on moved by `delta` levels: -1
 *  when that is outside 0..3 (Ctrl+PgUp / PgDn refuses and says so). */
int
LocEditorSelection_LevelTarget(
    int level,
    int delta);

#endif
