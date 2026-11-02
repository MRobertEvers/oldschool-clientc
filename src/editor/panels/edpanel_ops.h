#ifndef SRC_EDITOR_PANELS_EDPANEL_OPS_H
#define SRC_EDITOR_PANELS_EDPANEL_OPS_H

/**
 * The edits the tools make, as COMMANDS -- built against a document, never
 * applied here. App-free and world-free: a brush stroke, a rectangle paste, a
 * loc nudge and a spawn suppression are each a list of Editor_Cmd the caller
 * submits inside one stroke (Editor_StrokeBegin / Editor_Apply /
 * Editor_StrokeEnd), so the authority records one undo step and every mirror
 * applies the same echo. tools-panels' tests drive these straight against an
 * Editor_Doc.
 *
 * Coordinates are ABSOLUTE world tiles (map square * 64 + local), the one
 * address every connection agrees on. A tile whose square is not open in the
 * document is skipped: its edit could not be saved.
 *
 * Heights. A tile's `h` token is a raw step of 8 world units above the level
 * below (Editor_TileDeriveColumn): raw 0 is absent (Perlin noise on level 0,
 * the level below minus 240 above it), raw 1 is height 0, raw r >= 2 is r
 * units. So the representable heights are 0, 16, 24, 32, ... units of 8
 * above the base -- 8 cannot be spelled -- and these ops work in that ladder.
 * Derived heights are the renderer's (negative is up).
 */

#include "editor/editor_cmd.h"
#include "editor/editor_doc.h"
#include "editor/editor_spawn.h"
#include "editor/editor_ui.h"

/* ---- heights ---------------------------------------------------------------- */

/** The derived (rendered) height of world tile (x, z) on `level`, from the
 *  document alone. 0 when its square is not open. */
int
Editor_OpsDerivedHeight(
    struct Editor_Doc* doc,
    int world_x,
    int world_z,
    int level,
    int* out_height);

/** The raw `h` byte that puts a tile at `height` (derived, negative up) on a
 *  base of `base` (0 on level 0, the derived height below above that):
 *  the nearest representable step, a tie going up. 1..255. */
int
Editor_OpsRawForHeight(
    int base,
    int height);

/* ---- brushes ------------------------------------------------------------------ */

/** One brush application at one centre tile. */
struct Editor_BrushOp
{
    /** RAISE, LOWER, FLATTEN, SMOOTH, PAINT_UNDERLAY, PAINT_OVERLAY,
     *  PAINT_FLAGS. */
    enum Editor_Tool tool;
    int world_x;
    int world_z;
    int level;
    struct Editor_Brush brush;
    /** FLATTEN: the derived height every tile in the brush goes to. */
    int flatten_height;
    int underlay_id;
    int overlay_id;
    int overlay_none;
    int overlay_shape;
    int overlay_rotation;
    int flags_value;
};

/** The brush's weight at offset (dx, dz) from its centre: 0 outside the disc,
 *  1 at the centre, per the falloff in between. */
double
Editor_BrushWeight(
    const struct Editor_Brush* brush,
    int dx,
    int dz);

/**
 * The TILE commands one application makes: one per tile it changes, none for
 * a tile it would leave as it is. Every target is computed from the document
 * BEFORE any of them applies, so smoothing reads one consistent surface.
 * Returns the count (at most `max`, and at most EDITOR_BRUSH_TILES_MAX).
 */
int
Editor_BrushCommands(
    struct Editor_Doc* doc,
    const struct Editor_BrushOp* op,
    struct Editor_Cmd* out,
    int max);

/* ---- the rectangle ------------------------------------------------------------ */

/** Tiles a rectangle op may cover: 64 x 64, one square's worth. */
#define EDITOR_RECT_TILES_MAX (64 * 64)

/** A copied rectangle of authored tiles. */
struct Editor_TileClip
{
    int w;
    int h;
    /** w * h, row-major from the SW corner; `present` 0 where the square was
     *  not open (the paste leaves those tiles alone). */
    struct Editor_Tile tiles[EDITOR_RECT_TILES_MAX];
    unsigned char present[EDITOR_RECT_TILES_MAX];
    /** The SW tile's height in units above its base, so a paste shifts the
     *  authored heights by the difference to where it lands. */
    int anchor_units;
    int valid;
};

/** Normalised (x0 <= x1, z0 <= z1) and clamped to EDITOR_RECT_TILES_MAX.
 *  0 when the rectangle is invalid or too large. */
int
Editor_TileRectNormalise(
    const struct Editor_TileRect* in,
    struct Editor_TileRect* out);

/** Copy the rectangle's authored tiles. Returns the tiles copied. */
int
Editor_TileClipCopy(
    struct Editor_Doc* doc,
    const struct Editor_TileRect* rect,
    struct Editor_TileClip* clip);

/** Paste `clip` with its SW corner at (world_x, world_z) on `level`: authored
 *  heights move by the anchor difference, absent heights stay absent. */
int
Editor_TileClipPaste(
    struct Editor_Doc* doc,
    const struct Editor_TileClip* clip,
    int world_x,
    int world_z,
    int level,
    struct Editor_Cmd* out,
    int max);

/** Paint the rectangle with the palette's picks (`tools`): underlay, and the
 *  overlay (or its removal). */
int
Editor_TileRectFill(
    struct Editor_Doc* doc,
    const struct Editor_TileRect* rect,
    const struct EditorUiTools* tools,
    struct Editor_Cmd* out,
    int max);

/** Every tile of the rectangle back to absent: no tokens at all. */
int
Editor_TileRectClear(
    struct Editor_Doc* doc,
    const struct Editor_TileRect* rect,
    struct Editor_Cmd* out,
    int max);

/* ---- locs ----------------------------------------------------------------------- */

/** The loc a ref names in the document: its index in the square's list, or
 *  -1 (square not open, nothing there, or -- when ref->loc_id >= 0 -- a
 *  different loc there now). */
int
Editor_LocRefResolve(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* ref);

/** A ref for world tile (x, z): fills map and local coordinates. */
void
Editor_LocRefAt(
    struct Editor_LocRef* ref,
    int world_x,
    int world_z,
    int level,
    int shape,
    int loc_id,
    int rotation);

/** The ref's absolute tile. */
void
Editor_LocRefWorld(
    const struct Editor_LocRef* ref,
    int* out_world_x,
    int* out_world_z);

/** The next shape in the loc's own family (walls 0,1,2,3,9; wall decor
 *  4..8; ground 10,11; roofs 12..21; ground decor 22 stays). */
int
Editor_LocShapeCycle(int shape);

/** What a selection op did: the commands, and where each selected loc is now. */
struct Editor_LocOpResult
{
    int command_count;
    /** The selection after the op, in the input's order (a delete leaves it
     *  empty; a duplicate selects the copies). */
    struct Editor_LocRef refs[EDITOR_LOC_SELECTION_MAX];
    int ref_count;
};

/** Place one loc. `out` needs 1 slot. Onto a tile+shape already placed the
 *  command is a replace (before = the occupant), so its undo restores it. */
int
Editor_LocOpsPlace(
    struct Editor_Doc* doc,
    int loc_id,
    int shape,
    int rotation,
    int world_x,
    int world_z,
    int level,
    struct Editor_Cmd* out,
    int max,
    struct Editor_LocOpResult* result);

/** Move every selected loc by (dx, dz) tiles. A loc that changes square is a
 *  delete from one and an add to the other, in the same stroke. A loc whose
 *  landing tile+shape is held by an unselected loc stays (the document
 *  refuses a second loc of a shape on a tile); so do the rotate, reshape and
 *  duplicate below. `out` needs 2 * n slots. */
int
Editor_LocOpsNudge(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* refs,
    int n,
    int dx,
    int dz,
    struct Editor_Cmd* out,
    int max,
    struct Editor_LocOpResult* result);

/** Move the selection so its first loc lands on (world_x, world_z). */
int
Editor_LocOpsMoveTo(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* refs,
    int n,
    int world_x,
    int world_z,
    struct Editor_Cmd* out,
    int max,
    struct Editor_LocOpResult* result);

/** Turn every selected loc by `quarters` (1 = 90 degrees). */
int
Editor_LocOpsRotate(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* refs,
    int n,
    int quarters,
    struct Editor_Cmd* out,
    int max,
    struct Editor_LocOpResult* result);

/** Cycle every selected loc's shape within its family. */
int
Editor_LocOpsReshape(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* refs,
    int n,
    struct Editor_Cmd* out,
    int max,
    struct Editor_LocOpResult* result);

/** Copy every selected loc offset by (dx, dz); the copies become the
 *  selection. */
int
Editor_LocOpsDuplicate(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* refs,
    int n,
    int dx,
    int dz,
    struct Editor_Cmd* out,
    int max,
    struct Editor_LocOpResult* result);

/** Delete every selected loc. */
int
Editor_LocOpsDelete(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* refs,
    int n,
    struct Editor_Cmd* out,
    int max,
    struct Editor_LocOpResult* result);

/* ---- spawns ------------------------------------------------------------------- */

/** The SPAWN document key of the square holding world tile (x, z). */
void
Editor_SpawnDocKeyAt(
    char out[EDITOR_DOC_KEY_MAX],
    int world_x,
    int world_z);

/** The TEXT key of a square's GENERATED spawn file
 *  (server/scripts/areas/world/configs/m<x>_<z>.spawn), read only. */
void
Editor_SpawnGeneratedKey(
    char out[EDITOR_DOC_KEY_MAX],
    int map_x,
    int map_z);

/** A spawn row as a SPAWN (or SPAWN_SUPPRESS) command: `adding` 1 adds the
 *  row, 0 removes it. */
void
Editor_SpawnCommand(
    struct Editor_Cmd* out,
    const struct Editor_SpawnRow* row,
    int adding);

/**
 * Delete a spawn the panel lists. `edited` is the square's edited `.spawn`
 * (NULL when it has none yet). A row the edited file holds is removed from
 * it; a row only the GENERATED file holds gets a suppression row instead
 * (design #placement), because a write into the generated file would be
 * undone by its next regeneration. Returns 1 and fills `out`.
 */
int
Editor_SpawnDeleteCommand(
    const struct Editor_SpawnFile* edited,
    const struct Editor_SpawnRow* row,
    struct Editor_Cmd* out);

/**
 * Duplicate spawn rows: each of `rows` copied by (dx, dz, dlevel) as a plain
 * SPAWN add (a copy of a generated row is an EDITED row: the generated file
 * is never written), into the edited document of the square the copy lands
 * in — which may differ from the original's. `out_rows` (n of them, may be
 * NULL) gets each copy, for the selection. Returns the commands written;
 * a copy that would leave the world (negative tile, level outside 0..3) is
 * skipped.
 */
int
Editor_SpawnOpsDuplicate(
    const struct Editor_SpawnRow* rows,
    int n,
    int dx,
    int dz,
    int dlevel,
    struct Editor_Cmd* out,
    int max,
    struct Editor_SpawnRow* out_rows);

/** Whether `edited` suppresses the generated `row`. */
int
Editor_SpawnIsSuppressed(
    const struct Editor_SpawnFile* edited,
    const struct Editor_SpawnRow* row);

#endif
