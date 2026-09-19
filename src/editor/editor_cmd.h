#ifndef SRC_EDITOR_EDITOR_CMD_H
#define SRC_EDITOR_EDITOR_CMD_H

/**
 * Edits as undoable commands.
 *
 * Every change to the document goes through here, so that undo, save-dirty
 * tracking and rebuild scheduling cannot drift apart: applying a command is
 * the one place that mutates a tile or a placement, and it is the one place
 * that marks the square dirty.
 *
 * Two things this deliberately does NOT do, both learned from the reference
 * editor:
 *
 *   - It stores deltas, not snapshots. Snapshotting the whole document on
 *     every edit (a 4x64x64 tile array plus three entity lists, 30 deep) makes
 *     a brush stroke quadratic and puts a hard ceiling on history.
 *   - History survives changing level and changing square. Clearing it on a
 *     view change throws away work the user can still see.
 *
 * The commands carry AUTHORED tile records, never derived ones. Undoing a
 * height edit on a procedurally-heighted tile has to restore *absence* — the
 * state where the file says nothing and the noise routine owns the tile — and
 * a snapshot of the resolved height cannot express that.
 */

#include "editor_types.h"

struct Editor_Doc;

enum Editor_CmdKind
{
    /** One tile's authored record, before -> after. */
    EDITOR_CMD_TILE = 0,
    /**
     * One placement. `has_before`/`has_after` spell which kind of edit it is:
     * add (after only), delete (before only), or move/rotate/replace (both).
     */
    EDITOR_CMD_LOC,
};

struct Editor_Cmd
{
    enum Editor_CmdKind kind;
    int map_x;
    int map_z;

    /* EDITOR_CMD_TILE */
    int level;
    int x;
    int z;
    struct Editor_Tile tile_before;
    struct Editor_Tile tile_after;

    /* EDITOR_CMD_LOC */
    struct Editor_Loc loc_before;
    struct Editor_Loc loc_after;
    int has_before;
    int has_after;
};

/** Commands held. A stroke can be thousands, so this is sized for work, not
 *  for a demo; at ~80 bytes each this is a few megabytes. */
#define EDITOR_UNDO_MAX 65536

struct Editor_UndoStack
{
    struct Editor_Cmd commands[EDITOR_UNDO_MAX];
    /** 1 where a command begins an undo group (a brush stroke, a paste). */
    unsigned char stroke_start[EDITOR_UNDO_MAX];

    /** Commands recorded. */
    int count;
    /**
     * How many of them are currently applied. Undo moves this down without
     * discarding anything, which is what makes redo possible; the next new
     * command truncates the tail above it, as every editor does.
     */
    int applied;

    /** Set between StrokeBegin and StrokeEnd. */
    int in_stroke;
    int stroke_opened;
};

/* ---- applying ------------------------------------------------------------ */

enum Editor_CmdDirection
{
    EDITOR_CMD_FORWARD = 0,
    EDITOR_CMD_INVERSE,
};

/**
 * Apply one command to the document and mark the square dirty.
 *
 * Undo runs the same function with EDITOR_CMD_INVERSE rather than a separate
 * path, so a reverted edit dirties and repaints exactly like a forward one.
 * Returns 1 when the document changed.
 */
int
Editor_CmdApply(
    struct Editor_Doc* doc,
    const struct Editor_Cmd* command,
    enum Editor_CmdDirection direction);

/**
 * The squares whose meshes this command invalidates, as (map_x, map_z) pairs.
 *
 * Usually just the edited square, but not always, and the exceptions are why
 * this is a function rather than an assumption at the call site. Terrain is
 * not built per tile in isolation:
 *
 *   - Tile meshes sample a 65x65 grid of corner heights, so a height change on
 *     an edge tile moves vertices belonging to the neighbouring square.
 *   - Terrain colour blends underlays across a sliding window several tiles
 *     wide, so an underlay change near a border shifts colours on the far side
 *     of it.
 *
 * The reference editor never had to solve this because it can only open one
 * square; an editor that shows a region has to, or edits along a seam leave a
 * visible crack until the neighbour is reloaded.
 *
 * Writes at most `max` pairs and returns the count.
 */
int
Editor_CmdRebuildSpan(
    const struct Editor_Cmd* command,
    int* out_coords,
    int max);

/* ---- history ------------------------------------------------------------- */

void
Editor_UndoInit(struct Editor_UndoStack* stack);

/**
 * Open an undo group. Every command pushed until StrokeEnd undoes as one step
 * — a brush drag is one Ctrl+Z, not four hundred.
 */
void
Editor_UndoStrokeBegin(struct Editor_UndoStack* stack);

void
Editor_UndoStrokeEnd(struct Editor_UndoStack* stack);

/**
 * Record an already-applied command. Truncates any redo tail.
 * Returns 1 when it was recorded, 0 when the stack is full.
 */
int
Editor_UndoPush(
    struct Editor_UndoStack* stack,
    const struct Editor_Cmd* command);

/**
 * Revert the newest group. Returns the number of commands reverted, 0 when
 * there is nothing to undo. Each reverted command is passed to `on_reverted`
 * (may be NULL) so the caller can collect the squares needing a rebuild.
 */
int
Editor_UndoUndo(
    struct Editor_UndoStack* stack,
    struct Editor_Doc* doc,
    void (*on_reverted)(void* user_data, const struct Editor_Cmd* command),
    void* user_data);

/** Re-apply the group undone last. Same reporting contract as Undo. */
int
Editor_UndoRedo(
    struct Editor_UndoStack* stack,
    struct Editor_Doc* doc,
    void (*on_reapplied)(void* user_data, const struct Editor_Cmd* command),
    void* user_data);

#endif
