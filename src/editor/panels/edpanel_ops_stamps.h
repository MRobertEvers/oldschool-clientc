#ifndef SRC_EDITOR_PANELS_EDPANEL_OPS_STAMPS_H
#define SRC_EDITOR_PANELS_EDPANEL_OPS_STAMPS_H

/**
 * Workbench 6.3's document ops beside edpanel_ops.h (which they build on and
 * never change): stamps captured from a selection or a rectangle and dropped
 * as one undo step, the level move (Ctrl+PgUp / PgDn), Find same; and the
 * panel actions that run them (the Locs panel, the Stamps panel, the gizmo,
 * the hotkeys), each a list of Editor_Cmd through the authority, the
 * selection re-pointed at where the locs now are, every refusal said in the
 * status bar (Editor_UiStatus).
 *
 * The ops are App-free and world-free like edpanel_ops.h: coordinates are
 * ABSOLUTE world tiles and a tile whose square is not open is skipped (and
 * counted), because its edit could not be saved.
 *
 * SPAWNS travel as stamp pieces everywhere something is carried: a saved
 * stamp, the rectangle's clipboard, a square copy. One capture
 * (Editor_StampCaptureSpawns: the edited rows, the generated ones and the
 * quests' and skills', suppressed ones left out) and one drop
 * (Editor_StampDropCommands: each an EDITED row of where it lands, one that
 * already stands there left out).
 */

#include "editor/editor_stamps.h"
#include "edpanel_ops.h"

/* ---- capture ------------------------------------------------------------------ */

/**
 * The selection as a stamp: every live ref a LOC piece relative to refs[0]
 * (the primary: its tile is the origin, its level dlevel 0). `sizes` (may be
 * NULL: every footprint 1x1) holds each ref's PLACED footprint, x then z.
 * With `with_tiles` the stamp also carries a TILE piece for every tile of
 * the footprints' bounding box on each level a piece stands on; with
 * `with_spawns` every spawn standing in that box on those levels
 * (Editor_StampCaptureSpawns). `stamp` is cleared first (its name kept).
 * Returns the pieces.
 */
int
Editor_StampCaptureSelection(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* refs,
    int n,
    const int* sizes,
    int with_tiles,
    int with_spawns,
    struct Editor_Stamp* stamp);

/** The locs of a rectangle (anchored inside it, on its level) as the
 *  unnamed stamp, relative to its SW corner: what Ctrl+C carries beside the
 *  tiles. Returns the locs. */
int
Editor_StampCaptureRect(
    struct Editor_Doc* doc,
    const struct Editor_TileRect* rect,
    struct Editor_Stamp* stamp);

/** The tiles and levels a spawn capture reads, and the origin its pieces'
 *  offsets are counted from. */
struct Editor_StampSpawnBox
{
    /** Absolute tiles, inclusive. */
    int x0;
    int z0;
    int x1;
    int z1;
    /** Bit l set: level l. */
    unsigned levels;
    int origin_x;
    int origin_z;
    int origin_level;
};

/**
 * The spawns standing in `box`, APPENDED to `stamp` as SPAWN pieces: every
 * live npc and ground-item row of each open square the box touches, from all
 * three files a square's spawns come from -- its edited `.spawn`, its
 * generated one, and its quests' and skills' rows (the "spawns:" document).
 * The last two are read-only TEXT documents; they are read where the caller
 * has them open in `doc` (edp_stamp_spawn_docs opens them), and a row of
 * theirs the edited file suppresses is left out, as is every suppression row.
 * A drop makes each piece an EDITED row of the square it lands in: nothing
 * writes a generated file. The one capture a saved stamp, the rectangle's
 * Ctrl+C and a square copy all share (2026-11-09). Returns the pieces added.
 */
int
Editor_StampCaptureSpawns(
    struct Editor_Doc* doc,
    const struct Editor_StampSpawnBox* box,
    struct Editor_Stamp* stamp);

/** Every spawn of square (map_x, map_z), on every level, as the stamp
 *  (cleared first) relative to the square's SW tile on level 0: what a square
 *  copy carries. The square itself need not be open in `doc` -- a copy's
 *  source is whichever square the picker named -- only its spawn documents
 *  are read. Returns the spawns. */
int
Editor_StampCaptureSquareSpawns(
    struct Editor_Doc* doc,
    int map_x,
    int map_z,
    struct Editor_Stamp* stamp);

/* ---- the drop ----------------------------------------------------------------- */

struct Editor_StampDropResult
{
    int tiles;
    int locs;
    int spawns;
    /** Pieces left out: their square is not open, or their level is not
     *  0..3. The first one's absolute tile and level. */
    int skipped;
    int skipped_x;
    int skipped_z;
    int skipped_level;
    /** Placed locs whose key held another loc (the place is a replace). */
    int replaced;
    /** The placed locs, for the selection (the first EDITOR_LOC_SELECTION_MAX). */
    struct Editor_LocRef refs[EDITOR_LOC_SELECTION_MAX];
    int ref_count;
};

/**
 * The commands that drop `stamp` with its origin at (world_x, world_z) on
 * `level`: a TILE command per tile piece that changes its tile, a LOC place
 * per loc piece (onto a taken key a replace, whose undo restores the
 * occupant, as Editor_LocOpsPlace), a SPAWN add per spawn piece (its
 * document is the landing square's edited .spawn; the caller opens it, and
 * the square's two read-only spawn documents: edp_stamp_spawn_docs). A spawn
 * piece whose spawn already stands on its landing tile -- an equal live row
 * in any of the square's three files, as far as `doc` holds them -- is left
 * out like a tile piece that changes nothing, so a stamp dropped twice or a
 * paste in place does not stand a second npc on every tile.
 * Tiles first, then locs, then spawns: apply them as ONE stroke. Returns the
 * count written (at most `max`).
 */
int
Editor_StampDropCommands(
    struct Editor_Doc* doc,
    const struct Editor_Stamp* stamp,
    int world_x,
    int world_z,
    int level,
    struct Editor_Cmd* out,
    int max,
    struct Editor_StampDropResult* result);

/** Piece `index` (a LOC) of `stamp` dropped at the origin: its absolute tile,
 *  and whether its key is held in the document now (`*out_holder` the
 *  holder's loc id, -1 when free). 0 when its square is not open or its
 *  level is not 0..3. */
int
Editor_StampPieceLanding(
    struct Editor_Doc* doc,
    const struct Editor_Stamp* stamp,
    int index,
    int world_x,
    int world_z,
    int level,
    int* out_x,
    int* out_z,
    int* out_level,
    int* out_holder);

/* ---- the level move -------------------------------------------------------------- */

enum Editor_LevelMoveRefusal
{
    EDITOR_LEVEL_MOVE_OK = 0,
    /** The destination is not a level (below 0, above 3). */
    EDITOR_LEVEL_MOVE_NO_LEVEL,
    /** The loc's square is not open in the document. */
    EDITOR_LEVEL_MOVE_NOT_OPEN,
    /** The destination key holds a loc the selection does not (no swap
     *  across levels: the holder has no tile to go to). */
    EDITOR_LEVEL_MOVE_TAKEN,
    /** Nothing selected is in the document any more. */
    EDITOR_LEVEL_MOVE_GONE,
};

struct Editor_LevelMoveResult
{
    enum Editor_LevelMoveRefusal refusal;
    /** The ref that refused (absolute tile, its level), and for TAKEN the
     *  holder's loc id. */
    int at_x;
    int at_z;
    int at_level;
    int holder_loc_id;
    /** The selection after the move, in the input's order. */
    struct Editor_LocRef refs[EDITOR_LOC_SELECTION_MAX];
    int ref_count;
};

/**
 * Move every selected loc `delta` levels (each keeps its own tile, shape and
 * rotation; a selection over several levels stays several). ALL OR NOTHING:
 * one loc that cannot go refuses the whole move (`result->refusal`) and no
 * command is written. Locs that left the document are skipped. `out` needs
 * n slots. Returns the count written.
 */
int
Editor_LocOpsLevelMove(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* refs,
    int n,
    int delta,
    struct Editor_Cmd* out,
    int max,
    struct Editor_LevelMoveResult* result);

/** Every loc of id `loc_id` on `level` in the document's open squares, as
 *  refs (at most `max`); `*out_total` (may be NULL) every one, past `max`
 *  too. Returns the count written. */
int
Editor_LocOpsFindSame(
    struct Editor_Doc* doc,
    int loc_id,
    int level,
    struct Editor_LocRef* out,
    int max,
    int* out_total);

/* ---- the panel actions --------------------------------------------------------------
 *
 * Each needs ui->editor (else it says so), submits one undo step, re-points
 * the selection and says what happened; a refusal is a status sentence.
 */

/** Ctrl+PgUp / PgDn: the selection `delta` levels. */
int
edp_locs_level_move(
    struct EditorUi* ui,
    int delta);

/** The gizmo's Alt-arrow: copies of the selection at (dx, dz); the copies
 *  become the selection. */
int
edp_locs_duplicate_by(
    struct EditorUi* ui,
    int dx,
    int dz);

/** The gizmo's ring: the selection turned `quarters` (1..3). */
int
edp_locs_rotate_by(
    struct EditorUi* ui,
    int quarters);

/** Find same: every loc of the primary's id on its level in the open
 *  squares becomes the selection (the primary first). */
int
edp_locs_find_same(struct EditorUi* ui);

/** Save as stamp: the selection into editor/stamps/<name>.stamp (and the
 *  index), one undo step, then saved. A name taken by another stamp is
 *  overwritten only with `overwrite`. */
int
edp_stamp_save_selection(
    struct EditorUi* ui,
    const char* name,
    int with_tiles,
    int with_spawns,
    int overwrite);

/** The stamps the index lists, parsed into `file` (cleared first), the
 *  index opened when it is not yet. 0 when there is no session. */
int
edp_stamps_list(
    struct EditorUi* ui,
    struct Editor_StampsFile* file);

/** Arm stamp `name` for the Place tool: read, parsed, the tool set to Place.
 *  The ghost shows it, Q turns it, a click drops it. */
int
edp_stamp_arm(
    struct EditorUi* ui,
    const char* name);

/** The armed stamp, or NULL: none armed, or the tool left Place or the
 *  catalog pick changed since (which disarms it). */
const struct Editor_Stamp*
edp_stamp_armed(struct EditorUi* ui);

/** Drop the armed stamp, if any. */
void
edp_stamp_disarm(struct EditorUi* ui);

/** Q with a stamp armed: the whole stamp a quarter clockwise. */
void
edp_stamp_turn(
    struct EditorUi* ui,
    int quarters);

/** A click (or Space) with a stamp armed: the drop at the tile, one undo
 *  step; the placed locs become the selection. */
int
edp_stamp_drop(
    struct EditorUi* ui,
    int world_x,
    int world_z,
    int level);

/** Open, in this window's mirror, the spawn documents of every open square
 *  the tiles (x0..x1, z0..z1) touch: its edited `.spawn` and the two
 *  read-only ones (generated; quests' and skills'). What a spawn capture
 *  reads and a spawn drop checks and writes; a square with no such file
 *  simply has none. */
void
edp_stamp_spawn_docs(
    struct EditorUi* ui,
    int x0,
    int z0,
    int x1,
    int z1);

/** The same three documents of one square (map_x, map_z), open in this
 *  window's world or not: a square copy's source and destination, opened
 *  before the copy's stroke begins. */
void
edp_square_spawn_docs(
    struct EditorUi* ui,
    int map_x,
    int map_z);

/* The unnamed stamp: the rectangle's locs and the spawns standing in it,
 * which Ctrl+C copies beside its tiles (edp_rect_copy) and Ctrl+V pastes
 * after them in the same step. */

/** Copy the rectangle's locs and, with `with_spawns`, its spawns (without,
 *  the clipboard holds none); returns the locs. */
int
edp_stamp_clip_copy(
    struct EditorUi* ui,
    const struct Editor_TileRect* rect,
    int with_spawns);

/** The clipboard's loc commands for a paste with the SW corner at (world_x,
 *  world_z, level), appended at out[0..max). Returns the count. */
int
edp_stamp_clip_paste(
    struct EditorUi* ui,
    int world_x,
    int world_z,
    int level,
    struct Editor_Cmd* out,
    int max);

/** The clipboard's spawn commands for the same paste: a SPAWN add per copied
 *  spawn that does not already stand there, each an edited row of the square
 *  it lands in (whose spawn documents this opens). Apply them after the locs,
 *  in the paste's stroke. Returns the count. */
int
edp_stamp_clip_paste_spawns(
    struct EditorUi* ui,
    int world_x,
    int world_z,
    int level,
    struct Editor_Cmd* out,
    int max);

/** Locs the clipboard holds. */
int
edp_stamp_clip_locs(void);

/** Spawns the clipboard holds. */
int
edp_stamp_clip_spawns(void);

/**
 * A square copy's "with its spawns": every spawn standing in open square
 * (from_map_x, from_map_z), on every level (Editor_StampCaptureSpawns), as
 * SPAWN adds on the same local tiles of open square (to_map_x, to_map_z),
 * written at out[0..max); the spawn documents of both are opened first.
 * `*out_rows` (may be NULL) gets how many the source holds. Returns the
 * count written: apply them in the copy's stroke.
 */
int
edp_square_spawn_commands(
    struct EditorUi* ui,
    int from_map_x,
    int from_map_z,
    int to_map_x,
    int to_map_z,
    struct Editor_Cmd* out,
    int max,
    int* out_rows);

/** Release the clipboard and the armed stamp (Editor_UiFree). */
void
edp_stamps_free(void);

/* ---- the Stamps panel (edpanel_stamps.c) ---------------------------------------- */

/** Module "stamps": the saved stamps, searchable, Enter / Place arms one.
 *  Registered with the Catalog (Editor_Catalog_Register). */
void
Editor_Stamps_Register(struct EditorUi* ui);

/** The Catalog's "Stamps..." chip on `catalog_panel` (the Stamps kind): a
 *  click opens the Stamps panel. Call before Editor_Stamps_Register, in the
 *  row order the Catalog wants it. */
void
Editor_Stamps_CatalogChip(
    struct EditorUi* ui,
    int catalog_panel);

#endif
