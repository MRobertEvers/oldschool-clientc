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

#endif
