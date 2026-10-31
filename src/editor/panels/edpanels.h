#ifndef SRC_EDITOR_PANELS_EDPANELS_H
#define SRC_EDITOR_PANELS_EDPANELS_H

/**
 * The main window's panel modules (brief section 1, phase 3 `tools-panels`).
 * Each builds its chrome in its Register call and adds itself to the
 * registry (editor/editor_ui.h). Editor_UiRegisterToolsPanels calls them in
 * order; integrate-3 adds the other owners' modules beside them.
 *
 * App-free modules (run headless in tools-panels' tests): console, menu bar,
 * tool rail, terrain, palette, rectangle select, square picker, locs, spawns,
 * New square, save guard. App-bound (they read `ui->app`): catalog,
 * inspector.
 */

#include <stdint.h>

struct EditorUi;

/** Output console: the registry's log ring as a VLIST. "console" */
void
Editor_Console_Register(struct EditorUi* ui);

/** File / Edit / View across the top. "menubar" */
void
Editor_MenuBar_Register(struct EditorUi* ui);

/** The vertical TOOLBAR of tool icons. "toolrail" */
void
Editor_ToolRail_Register(struct EditorUi* ui);

/** Brush radius / falloff / step / strength SLIDERs, flags, level RADIO,
 *  vis cap. "terrain" */
void
Editor_Terrain_Register(struct EditorUi* ui);

/** Underlay and overlay swatches, overlay shape thumbnails and rotation.
 *  "palette" */
void
Editor_Palette_Register(struct EditorUi* ui);

/** The rectangle select's copy / paste / fill / clear. "rect" */
void
Editor_RectSelect_Register(struct EditorUi* ui);

/** The square picker (design #squares). "squares" */
void
Editor_SquarePicker_Register(struct EditorUi* ui);

/** NPC / obj spawns of the selected square: place, delete, suppress,
 *  restore. "spawns" */
void
Editor_Spawns_Register(struct EditorUi* ui);

struct Editor_SpawnRow;

/** The spawns panel's multi-selection size; 0 while the panel is hidden
 *  (so the D key duplicates locs, or nothing, when it is closed). */
int
Editor_Spawns_SelectionCount(struct EditorUi* ui);

/** Duplicate [D]: the selected spawns copied one tile east, or to the
 *  latched tile, as one undo step; the copies become the selection.
 *  Returns the commands the authority accepted. */
int
Editor_Spawns_Duplicate(struct EditorUi* ui);

/** Copy up to `max` selected rows into `out`; returns the selection's size. */
int
Editor_Spawns_Selection(
    struct EditorUi* ui,
    struct Editor_SpawnRow* out,
    int max);

/** The New square / Copy to / Save as new modal. "new_square" */
void
Editor_NewSquare_Register(struct EditorUi* ui);

/** The unsaved-edit guard: Save / Discard / Cancel. "save_guard" */
void
Editor_SaveGuard_Register(struct EditorUi* ui);

/** The asset catalog over the server's index (kind, find, paged results,
 *  the record, Copy to new), with the provider's model well when an App is
 *  bound (the panel runs without one). "catalog" */
void
Editor_Catalog_Register(struct EditorUi* ui);

/** Tile and loc readout: authored vs generated, the loc record. App-bound.
 *  "inspector" */
void
Editor_Inspector_Register(struct EditorUi* ui);

/** The loc multi-selection: nudge, rotate, reshape, duplicate, delete, and
 *  the Place tool's shape and rotation. "locs" */
void
Editor_Locs_Register(struct EditorUi* ui);

/* ---- entry points other modules (and the app) reach ------------------------ */

/** Modes of the New square modal: the full question, or the storage question
 *  alone (design #squares). */
enum Editor_NewSquareMode
{
    /** Coordinates, storage, template. */
    EDITOR_NEW_SQUARE_NEW = 0,
    /** Copy `source` to new coordinates and a storage. */
    EDITOR_NEW_SQUARE_COPY_TO,
    /** `source` again at its own coordinates, in another storage. */
    EDITOR_NEW_SQUARE_SAVE_AS,
};

/**
 * Open the New square modal. `source_key` ("m50_50", may carry its storage)
 * is the square a COPY template or the two copy modes copy, NULL for none;
 * (map_x, map_z) prefill the coordinates, -1 for none.
 */
void
Editor_NewSquare_Open(
    struct EditorUi* ui,
    enum Editor_NewSquareMode mode,
    const char* source_key,
    int map_x,
    int map_z);

/** The picker's current selection as a square key with storage (what the
 *  New dialog's "copy of selection" copies). 0 when nothing is selected. */
int
Editor_SquarePicker_SelectionKey(
    struct EditorUi* ui,
    char* out,
    int out_size);

/** The empty cell the picker last chose (a click on no square, or a search
 *  that named one): where New would create. 0 when there is none. */
int
Editor_SquarePicker_EmptyCell(
    struct EditorUi* ui,
    int* out_map_x,
    int* out_map_z);

/** What the catalog's model well shows: the pick, or the chosen multiloc
 *  rung (-1 nothing). -1 without a catalog. */
int
Editor_Catalog_PreviewId(struct EditorUi* ui);

/** The catalog's model-view widget, -1 without a catalog. */
int
Editor_Catalog_ModelView(struct EditorUi* ui);

/**
 * Copy the loc / npc / obj definition `name` (`kind` a
 * TORIRSMAPED_CATALOG_LOC..OBJ) to `new_name` with a fresh id: SAVE_AS on
 * the authority, one undo step (Editor_CopyRecord). On success the copy is
 * the pick and is open in this mirror; the console says what happened.
 * Returns 1 when the copy was made. The catalog's and the shell editor's
 * "Copy to new..." both end here.
 */
int
Editor_Catalog_CopyRecord(
    struct EditorUi* ui,
    uint32_t kind,
    const char* name,
    const char* new_name);

struct Editor_CatalogModel;

/** The catalog panel's model (edpanel_catalog_model.h), NULL without the
 *  panel: what the tests read. */
const struct Editor_CatalogModel*
Editor_Catalog_Model(struct EditorUi* ui);

/** The catalog follows a selected loc: kind Locs, pick = that loc. */
void
Editor_Catalog_Adopt(
    struct EditorUi* ui,
    int loc_id);

/** Ask the picker to refetch the atlas (after a DOC_CREATE that this window
 *  made, before the broadcast lands). */
void
Editor_SquarePicker_Refresh(struct EditorUi* ui);

/* ---- editing feedback: brush footprint, spawn placement, history ---------- */

struct Editor_Brush;

/** One tile a brush application touches: its offset from the centre and the
 *  weight the stroke gives it (Editor_BrushWeight, the same call). */
struct Editor_BrushTile
{
    int dx;
    int dz;
    double weight;
};

/** The tiles the next application of `brush` touches, centre first: what
 *  the footprint preview draws and exactly the set Editor_BrushCommands
 *  visits. Returns the count (at most `max`). edpanel_terrain.c */
int
Editor_Terrain_BrushFootprint(
    const struct Editor_Brush* brush,
    struct Editor_BrushTile* out,
    int max);

/** Whether `tool`'s effect fades with the brush weight (raise, lower,
 *  smooth); the others treat every footprint tile alike. */
int
Editor_Terrain_ToolUsesFalloff(int tool);

/** Place the catalog's npc / obj pick (tools.pick_kind 1 / 2) as a spawn row
 *  at absolute (world_x, world_z, level): the click the spawn ghost
 *  previews. The pick is resolved to its pack symbol through the server's
 *  catalog. Returns 1 when the authority took the row. edpanel_locs.c */
int
Editor_Locs_PlaceSpawn(
    struct EditorUi* ui,
    int world_x,
    int world_z,
    int level);

/** The session's undo groups, newest first, click to undo / redo to one;
 *  filter by document key. "history" */
void
Editor_History_Register(struct EditorUi* ui);

/** Undo / redo until `target_groups` of the history's groups are applied
 *  (0 = everything this window saw undone). Returns the steps taken. */
int
Editor_History_Jump(
    struct EditorUi* ui,
    int target_groups);

/** The History panel's row `row` (0 newest): what a click asks for (applied
 *  groups) and its text; -1 / NULL past the end. For tests. */
int
Editor_History_RowTarget(
    struct EditorUi* ui,
    int row);

const char*
Editor_History_RowText(
    struct EditorUi* ui,
    int row);

/** How many times the History panel rebuilt its rows (tests). */
int
Editor_History_Rebuilds(struct EditorUi* ui);

/* The Spawns panel's edit form, move and wander preview (edpanel_spawns.c). */

struct MapEditorWanderRect;

/** A "Move: click a tile" is armed with a selection to move. */
int
Editor_Spawns_MoveArmed(struct EditorUi* ui);

/** The armed move lands: the selection re-tiled so its first row stands on
 *  (world_x, world_z, level), the rest at their offsets, as one undo step
 *  ("move spawn"). Disarms. Returns the commands the authority took. */
int
Editor_Spawns_MoveTo(
    struct EditorUi* ui,
    int world_x,
    int world_z,
    int level);

/** Fill the edit form and Apply it, as the widgets would (tests, scripts). */
int
Editor_Spawns_SetEditForm(
    struct EditorUi* ui,
    const char* name,
    int is_obj,
    int x,
    int z,
    int level,
    int count,
    int suppressed);

/** Replace the selection (rows as the list shows them). */
int
Editor_Spawns_Select(
    struct EditorUi* ui,
    const struct Editor_SpawnRow* rows,
    int count);

/**
 * The wander preview's rectangles this frame, ABSOLUTE world tiles
 * (map_editor_ghost.h): the spawn ghost's (`ghost_npc_id` >= 0, at the
 * hover), every selected npc spawn's, and a pending move's destination.
 * Empty when "Show wander range" is off. The App draws them
 * (app_overlay.c app_overlay_build_editor_ghosts).
 */
int
Editor_Spawns_WanderRects(
    struct EditorUi* ui,
    int hover_valid,
    int hover_x,
    int hover_z,
    int hover_level,
    int ghost_npc_id,
    struct MapEditorWanderRect* out,
    int max);

/* The Record panel (edpanel_catalog.c): one config record across every file
 * that states it -- configs/all.<type> and each server overlay -- every field
 * editable with undo. "record" */

void
Editor_Record_Register(struct EditorUi* ui);

/** Open `name` of catalog kind `kind` (loc / npc / obj) in the Record panel
 *  and show it. Returns 1 when its documents opened. */
int
Editor_Record_Open(
    struct EditorUi* ui,
    uint32_t kind,
    const char* name);

/** The Record panel's resolved wanderrange / maxrange when it holds
 *  `type`:`name` (unsaved edits included); 0 otherwise. A value no layer
 *  states is the engine default (5 / 7). */
int
Editor_Record_WanderValues(
    struct EditorUi* ui,
    const char* type,
    const char* name,
    int* out_wander,
    int* out_leash);

/** Set field (`key`, `sub`) of the open record to `value` ("" removes) in
 *  layer `layer` (-1 where it is stated), as one undo step ("edit record").
 *  Returns the commands the authority took, -1 when it cannot be said. */
int
Editor_Record_SetField(
    struct EditorUi* ui,
    const char* key,
    const char* sub,
    const char* value,
    int layer);

/** Put the Record panel at (x, y), w x h, and show it (a photograph's layout). */
void
Editor_Record_Place(
    struct EditorUi* ui,
    int x,
    int y,
    int w,
    int h);

/** Select field (`key`, `sub`) of the open record in the list, scrolled to
 *  it, its value in the form. Returns the row, -1 when absent. */
int
Editor_Record_SelectField(
    struct EditorUi* ui,
    const char* key,
    const char* sub);

struct Editor_CfgRecord;

/** The open record (tests, the inspector); NULL when none. */
const struct Editor_CfgRecord*
Editor_Record_Current(struct EditorUi* ui);

#endif
