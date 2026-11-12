#ifndef SRC_EDITOR_PANELS_EDPANEL_OPS_BUILDING_H
#define SRC_EDITOR_PANELS_EDPANEL_OPS_BUILDING_H

/**
 * The Building tool's document ops beside edpanel_ops.h, and the panel
 * actions that run them (editor/editor_building.h plans the pieces).
 *
 * The ops are App-free and world-free like edpanel_ops.h: ABSOLUTE world
 * tiles, a tile whose square is not open skipped and counted. Every one is a
 * list of Editor_Cmd the caller applies as ONE stroke, so a building, a door
 * and a window are each one undo step.
 */

#include "editor/editor_building.h"
#include "edpanel_ops.h"

struct Editor_BuildingApplyResult
{
    /** Loc pieces placed (added, or put in place of what held their tile's
     *  layer), loc pieces already there exactly, and tiles changed. */
    int placed;
    int kept;
    int tiles;
    /** Locs taken off because a piece took their place: another wall on a
     *  wall piece's tile (the world keeps one wall a tile, whatever its
     *  shape), another roof piece on a roof piece's. */
    int removed;
    /** Pieces left out because their square is not open; the first one's
     *  tile. */
    int closed;
    int closed_x;
    int closed_z;
};

/**
 * The commands that lay `pieces`: a LOC command per loc piece (an add; a
 * replace where its key is held; and a remove of every OTHER loc in its
 * layer on that tile when the piece is a wall, shapes 0..3, or a roof piece,
 * 12..21), a TILE command per floor piece that changes its tile. A piece
 * already there exactly makes no command. Tiles first. Returns the count
 * written (at most `max`).
 */
int
Editor_BuildingCommands(
    struct Editor_Doc* doc,
    const struct Editor_BuildingPiece* pieces,
    int count,
    struct Editor_Cmd* out,
    int max,
    struct Editor_BuildingApplyResult* result);

/**
 * The commands that take every roof piece (shapes 12..21) off the tiles
 * (x0..x1, z0..z1) on levels `level_lo`..`level_hi`: what stood where a new
 * storey's walls and floor go. Returns the count written.
 */
int
Editor_BuildingClearRoofCommands(
    struct Editor_Doc* doc,
    int x0,
    int z0,
    int x1,
    int z1,
    int level_lo,
    int level_hi,
    struct Editor_Cmd* out,
    int max);

/**
 * The commands that take every ground decoration (shape 22: grass tufts,
 * dug soil, twigs, flowers) off the tiles (x0..x1, z0..z1) on `level`: the
 * ground a house is built over is indoors now, and the grass a Scatter left
 * there grew through its floor (2026-11-12). Returns the count written.
 */
int
Editor_BuildingClearDecorCommands(
    struct Editor_Doc* doc,
    int x0,
    int z0,
    int x1,
    int z1,
    int level,
    struct Editor_Cmd* out,
    int max);

enum Editor_BuildingWallFind
{
    /** No straight wall on the tile or on an edge of it. */
    EDITOR_BUILDING_WALL_NONE = 0,
    EDITOR_BUILDING_WALL_FOUND,
    /** Walls on two edges of the tile (a room's corner): the click must be
     *  on the piece itself. */
    EDITOR_BUILDING_WALL_AMBIGUOUS,
};

/**
 * The straight wall piece (shape 0) a click on world tile (x, z) means: the
 * one standing on the tile, else the one on a neighbouring tile whose edge
 * is this tile's edge -- a building's walls stand on the ring outside the
 * room, so a click on the floor beside a wall names that wall. `*out` is the
 * piece as a ref.
 */
enum Editor_BuildingWallFind
Editor_BuildingFindWall(
    struct Editor_Doc* doc,
    int world_x,
    int world_z,
    int level,
    struct Editor_LocRef* out);

struct Editor_BuildingSwapResult
{
    /** 1: the piece was turned into the door / given the window; 0: it was
     *  one already and was put back (the door a wall again, the window
     *  taken off). */
    int added;
    /** The loc now on the wall's tile (the door, or the wall put back). */
    int loc_id;
    struct Editor_LocRef ref;
    /** 1: a gate had no second fence piece to open on -- neither the piece
     *  clockwise of the click nor the one before it is a plain straight
     *  piece of the run -- and nothing was changed. */
    int no_leaf;
};

/**
 * Door: the straight wall piece `wall` (a ref that resolves) becomes loc
 * `door_id`, same tile, shape and rotation; a piece that is `door_id`
 * already goes back to a wall -- the id of the wall beside it along its
 * line, else `wall_id`.
 *
 * A gate (`door2_id` >= 0) is two leaves: the piece beside `wall`
 * (Editor_BuildingGateBeside), when it is a straight piece on the same edge,
 * becomes `door2_id` in the same step, and a click on either leaf puts both
 * back. When that piece is not one to open (the run's last piece, a gate
 * already there) the gate opens the other way: the piece before `wall` is
 * the first leaf and `wall` the second. With neither, nothing is changed and
 * result->no_leaf says why (2026-11-11: a click beside a gate hung one lone
 * leaf against it).
 *
 * `out` needs 2 slots. Returns the commands (0 when the ref no longer
 * resolves).
 */
int
Editor_BuildingDoorCommands(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* wall,
    int door_id,
    int door2_id,
    int wall_id,
    struct Editor_Cmd* out,
    int max,
    struct Editor_BuildingSwapResult* result);

/**
 * Window: the wall piece `wall` gains loc `window_id` as two wall
 * decorations, the outside face on its own tile (shape 5, its rotation) and
 * the inside face on the tile across it (shape 4, turned half round; left
 * out when that tile's square is not open). A wall that carries the window
 * already has both taken off. `out` needs 2 slots. Returns the commands.
 */
int
Editor_BuildingWindowCommands(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* wall,
    int window_id,
    struct Editor_Cmd* out,
    int max,
    struct Editor_BuildingSwapResult* result);

/* ---- the panel actions (edpanel_building.c) --------------------------------------
 *
 * Module "building": the Building tool's panel. It opens when the tool is
 * armed (tool.building) and holds what the next gesture makes.
 */

/** What a gesture with the Building tool does. */
enum Editor_BuildingMode
{
    /** Drag a rectangle: walls, floor, roof flag and roof. */
    EDITOR_BUILDING_MODE_SHELL = 0,
    /** Drag a line: a straight inside wall. */
    EDITOR_BUILDING_MODE_WALL,
    /** Click a wall piece: a door (again: the wall back). */
    EDITOR_BUILDING_MODE_DOOR,
    /** Click a wall piece: a window (again: off). */
    EDITOR_BUILDING_MODE_WINDOW,
    EDITOR_BUILDING_MODE_COUNT
};

struct EditorUi;

enum Editor_BuildingMode
edp_building_mode(const struct EditorUi* ui);

/** Set the mode (the panel's radios follow) and say what the next gesture
 *  does in the status bar. */
void
edp_building_set_mode(
    struct EditorUi* ui,
    enum Editor_BuildingMode mode);

/** 1 when a press with the Building tool opens a rectangle drag (Shell and
 *  Wall line); 0 when it is a click (Door, Window). */
int
edp_building_drags(const struct EditorUi* ui);

/** The drag's release (and the panel's "Build on rectangle"): the shell or
 *  the wall line over `rect`, one undo step; refusals are status sentences.
 *  Returns the edits applied. */
int
edp_building_build(
    struct EditorUi* ui,
    const struct Editor_TileRect* rect);

/** A click on world tile (x, z) in Door or Window mode: `pointed` is the loc
 *  under the pointer (NULL when none), taken when it is a straight wall,
 *  else the tile's wall (Editor_BuildingFindWall). Returns the edits
 *  applied. */
int
edp_building_click(
    struct EditorUi* ui,
    int world_x,
    int world_z,
    int level,
    const struct Editor_LocRef* pointed);

#endif
