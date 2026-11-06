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
 * New square, save guard. App-bound (they ask the App through
 * `ui->app_services`, editor_ui.h): catalog, inspector, and the areas
 * panel's camera moves.
 */

#include "editor/editor_hotkeys.h"

#include <stdint.h>

struct EditorUi;
struct EditorHost_CatalogSquares;

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

/** The spawns panel's multi-selection size; 0 while the panel is closed
 *  (so the D key duplicates locs, or nothing, when it is closed). A Spawns
 *  tab behind another of its group's strip is open, and counts. */
int
Editor_Spawns_SelectionCount(struct EditorUi* ui);

/** Delete / suppress the spawn row selected in the list (the panel's
 *  Delete button; Edit > Delete selection with no loc selected). Returns 1
 *  when a command was applied. */
int
Editor_Spawns_DeleteSelected(struct EditorUi* ui);

/** Duplicate (Ctrl+D): the selected spawns copied one tile east, or to the
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

/** The instance helper (design section 13, edpanel_instance.c): source
 *  squares, what to copy, a free destination, storage, name, map area,
 *  entry trigger; Create as one undo step. A modal. "instance" */
void
Editor_Instance_Register(struct EditorUi* ui);

/** Open it over the picker's selection (the picker's Instance...). */
void
Editor_Instance_Open(struct EditorUi* ui);

/** Find free...: the lowest free box of the source's size in instance
 *  space, typed into Destination and marked in the picker. 1 when found;
 *  a refusal says why in the status bar. */
int
Editor_Instance_FindFree(struct EditorUi* ui);

/** Create, as the button does. 1 when the instance was made (one undo
 *  step); 0 refused, the reason in the dialog and the status bar. */
int
Editor_Instance_Create(struct EditorUi* ui);

/** The dialog's widgets, for a headless driver. */
enum Editor_InstanceWidget
{
    EDITOR_INSTANCE_W_PANEL = 0,
    EDITOR_INSTANCE_W_SOURCE,
    EDITOR_INSTANCE_W_SOURCE_ROW,
    EDITOR_INSTANCE_W_TERRAIN,
    EDITOR_INSTANCE_W_LOCS,
    EDITOR_INSTANCE_W_SPAWNS,
    EDITOR_INSTANCE_W_AREAS,
    EDITOR_INSTANCE_W_LEVELS,
    EDITOR_INSTANCE_W_DEST,
    EDITOR_INSTANCE_W_FIND,
    EDITOR_INSTANCE_W_FREE,
    EDITOR_INSTANCE_W_STORAGE,
    EDITOR_INSTANCE_W_LANE,
    EDITOR_INSTANCE_W_NAME,
    EDITOR_INSTANCE_W_MAP_ADD,
    EDITOR_INSTANCE_W_MAP_NONE,
    EDITOR_INSTANCE_W_ENTRY_TELEPORT,
    EDITOR_INSTANCE_W_ENTRY_RUNTIME,
    EDITOR_INSTANCE_W_ENTRY_NONE,
    EDITOR_INSTANCE_W_LOC,
    EDITOR_INSTANCE_W_COORD,
    EDITOR_INSTANCE_W_CHECK,
    EDITOR_INSTANCE_W_CREATE,
    EDITOR_INSTANCE_W_CANCEL,
};

int
Editor_Instance_Widget(
    struct EditorUi* ui,
    enum Editor_InstanceWidget which);

/** The unsaved-edit guard: Save / Discard / Cancel. "save_guard" */
void
Editor_SaveGuard_Register(struct EditorUi* ui);

/** The asset catalog over the server's index (kind, find, paged results,
 *  the record, Copy to new; the multiloc rungs when an App is bound -- the
 *  panel runs without one). Registers the Record and Preview panels with
 *  it. "catalog" */
void
Editor_Catalog_Register(struct EditorUi* ui);

/** The model view of the Catalog's pick, a dock panel of its own
 *  (edpanel_preview.c). Registered by Editor_Catalog_Register. "preview" */
void
Editor_Preview_Register(struct EditorUi* ui);

/**
 * What the Preview shows: the Catalog's pick (`*out_kind` a
 * CacheProvider_CatalogKind, `*out_id` the pick, or for a multiloc the rung the
 * Catalog chose), else -- while nothing is picked -- the primary selected loc.
 * 0 with -1 / -1 when there is nothing to show.
 */
int
Editor_Preview_Subject(
    struct EditorUi* ui,
    int* out_kind,
    int* out_id);

/** The Preview panel's model-view widget; -1 when the panel is absent or
 *  closed (nothing to render into). */
int
Editor_Preview_ModelView(struct EditorUi* ui);

/** How many times the Preview opened itself beside the Catalog (tests). */
int
Editor_Preview_AutoOpens(struct EditorUi* ui);

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

/** What the Preview shows of the catalog's pick: the pick, or the chosen
 *  multiloc rung (-1 nothing). -1 without a catalog. */
int
Editor_Catalog_PreviewId(struct EditorUi* ui);

/** The multiloc rung the Catalog's variant dropdown (the Shell editor's
 *  readout) chose for a loc pick: its loc id, -2 for a value with nothing at
 *  it, -1 when none is chosen (or no catalog). */
int
Editor_Catalog_PreviewRung(struct EditorUi* ui);

/**
 * Pick from world (Shift+I, tool.pick_loc; design section 6): the hovered
 * placement's loc becomes the pick AT ITS EXACT POSE (`shape`, `rotation`
 * become Place's), Place is armed, Find is set to its record name (its id
 * without a catalog) with the chips off, and its row is selected and
 * scrolled into view once the page holding it lands. 0 (said in the status
 * bar) without a catalog.
 */
int
Editor_Catalog_PickFromWorld(
    struct EditorUi* ui,
    int loc_id,
    int shape,
    int rotation);

/** The pick is in editor/favourites.txt (editor_favourites.h). */
int
Editor_Catalog_IsFavourite(struct EditorUi* ui);

/** The favourite star: the pick into editor/favourites.txt (a loc with
 *  Place's pose), or out of it when it is there; one TEXT_SPLICE through the
 *  authority, one undo step. 1 a favourite now, 0 not, -1 refused (said). */
int
Editor_Catalog_ToggleFavourite(struct EditorUi* ui);

/** Find in world: every placement of the loc pick in the open squares
 *  selected (the first EDITOR_LOC_SELECTION_MAX), the camera over the first.
 *  Returns how many there are; 0 is said. */
int
Editor_Catalog_FindInWorld(struct EditorUi* ui);

/** The pick's record in the Record panel. 0 when refused (said). */
int
Editor_Catalog_EditPick(struct EditorUi* ui);

/** Copy the pick to the Catalog's New name (Editor_Catalog_CopyRecord). */
int
Editor_Catalog_CopyPick(struct EditorUi* ui);

/** The loc pick's placeable shapes: its resident config's, else the
 *  catalog's summary fields'. 0 for no loc pick. */
uint32_t
Editor_Catalog_PickShapes(struct EditorUi* ui);

struct Editor_CatalogRecents;

/** The session's recent picks (the tests read them), NULL without a
 *  catalog. */
const struct Editor_CatalogRecents*
Editor_Catalog_Recents(struct EditorUi* ui);

/**
 * Make record `name` of `kind` (TORIRSMAPED_CATALOG_LOC..OBJ) the pick, as a
 * click on its row does (published to the Client; a loc arms Place), and show
 * it in the Catalog: kind set, Find = the name, the row selected with its
 * record once the page lands. 0 when there is no catalog or no such record
 * (the console says which). TORIRS_EDITOR_PREVIEW drives it.
 */
int
Editor_Catalog_PickRecord(
    struct EditorUi* ui,
    uint32_t kind,
    const char* name);

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

/* ---- named areas and square collections (edpanel_areas.c) -----------------
 *
 * The Areas panel over the AREAS document (editor/editor_areas.h) every
 * window mirrors: named areas of the world (rectangles on a level) and named
 * collections of squares. "areas" */

struct Editor_AreasFile;

void
Editor_Areas_Register(struct EditorUi* ui);

/** The parse of this mirror's AREAS document, current with its text (parsed
 *  again only when the text's revision moves); NULL without the document or
 *  when it does not parse. `out_serial` (may be NULL) bumps on every
 *  re-parse: what a reader that derives from it (the picker's collection
 *  dropdown) rebuilds on. */
const struct Editor_AreasFile*
Editor_Areas_File(
    struct EditorUi* ui,
    uint32_t* out_serial);

/** New from rect: the Rect tool's rectangle becomes an area named `name`
 *  ("" or NULL: "Area <n>") on the rectangle's level, one undo step ("Mark
 *  area <name>"), and the selection. 0 (and the panel says so) when the
 *  Rect tool has no rectangle, the name is taken or not a name. */
int
Editor_Areas_NewFromRect(
    struct EditorUi* ui,
    const char* name);

/** The selected area: add the Rect tool's rectangle to it, rename it, set
 *  its level (-1 all), move the camera to its centre (opening its square
 *  when it is not loaded). Each edit is one undo step. */
int
Editor_Areas_AddRect(struct EditorUi* ui);

int
Editor_Areas_Rename(
    struct EditorUi* ui,
    const char* new_name);

int
Editor_Areas_SetLevel(
    struct EditorUi* ui,
    int level);

int
Editor_Areas_GoTo(struct EditorUi* ui);

/** The free camera to absolute tile (x, z) on `level`, the way Go to moves
 *  it (keeps yaw and pitch, eye 2600 units out). 0 when the tile is not in
 *  the loaded scene or the window has no world (no
 *  `ui->app_services.camera_to_tile`). */
int
Editor_Areas_CameraTo(
    struct EditorUi* ui,
    int tile_x,
    int tile_z,
    int level);

/** Select an area / a collection by name (0 when there is none). */
int
Editor_Areas_Select(
    struct EditorUi* ui,
    const char* name);

const char*
Editor_Areas_Selected(struct EditorUi* ui);

int
Editor_Areas_SelectCollection(
    struct EditorUi* ui,
    const char* name);

const char*
Editor_Areas_SelectedCollection(struct EditorUi* ui);

/** A new collection of the squares open in this mirror / selected in the
 *  picker (shift-click), named `name` (NULL: the Name field, else
 *  "Collection <n>"). One undo step; it becomes the selection. */
int
Editor_Areas_CollectionFromOpen(
    struct EditorUi* ui,
    const char* name);

int
Editor_Areas_CollectionFromPicker(
    struct EditorUi* ui,
    const char* name);

/** Add (`add` 1) or remove the current square -- the picker's focus, else
 *  the latched tile's square -- to / from the selected collection. */
int
Editor_Areas_CollectionAddCurrent(
    struct EditorUi* ui,
    int add);

/** Open every square of the selected collection: as documents now, into the
 *  world one load at a time. Past 16 squares it asks first (returns -1 and
 *  shows the confirm) unless `confirmed`. Returns the documents opened. */
int
Editor_Areas_OpenAll(
    struct EditorUi* ui,
    int confirmed);

/** Show the Areas panel. */
void
Editor_Areas_Show(struct EditorUi* ui);

/** One rectangle the world overlay draws: ABSOLUTE tiles, inclusive. */
struct Editor_AreaOverlay
{
    int x0;
    int z0;
    int x1;
    int z1;
    /** 0..3, -1 every level. */
    int level;
    uint32_t rgb;
    /** The selected area: drawn brighter. */
    int selected;
    /** The area's first rectangle: carries the label at its top-left. */
    int first;
    char label[64];
};

/** What the world overlay draws this frame: every area's rectangles (only
 *  the selected area's under Hide others), none while Show outlines is off.
 *  Returns the count (at most `max`). app_overlay.c draws them. */
int
Editor_Areas_Overlay(
    struct EditorUi* ui,
    struct Editor_AreaOverlay* out,
    int max);

/** The picker shows only the squares of collection `name` ("" or NULL:
 *  every square, the Area dropdown's page). Returns 1 when the filter is
 *  on. edpanel_squares.c */
int
Editor_SquarePicker_SetCollection(
    struct EditorUi* ui,
    const char* name);

/** The collection the picker is filtered to ("" when none). */
const char*
Editor_SquarePicker_Collection(struct EditorUi* ui);

/* ---- the picker's navigation and panes (workbench 6.4, edpanel_squares.c) ---- */

/** The focused square's outline: white (design section 5, "Navigation"). */
#define EDITOR_SQUARES_FOCUS_RGB 0xffffffu

/**
 * Run a Picker action (EDITOR_HK_PICKER_*: the focus N / S / W / E, open,
 * add, back, clear Find) as the well's key does. The Squares well resolves
 * its own keys against the K rows (Editor_HotkeysMatch); Editor_HotkeyRun
 * ends here for a dispatcher that fires one. Returns 1 when the window has a
 * picker (a refusal says why in the status bar), 0 when it has none.
 */
int
Editor_SquarePicker_Key(
    struct EditorUi* ui,
    int action);

/** Hand the picker an atlas, as CATALOG_SQUARES would (a headless window, a
 *  test's synthetic one). */
void
Editor_SquarePicker_TakeAtlas(
    struct EditorUi* ui,
    const struct EditorHost_CatalogSquares* atlas);

/** Run Find on `text` as Enter in the Find box does. Returns 1 when it
 *  matched. */
int
Editor_SquarePicker_Find(
    struct EditorUi* ui,
    const char* text);

/** Rows of the detail pane (the heading first, then Square, Area, Tiles,
 *  Contents, State, Collections). */
#define EDITOR_SQUARES_PANE_ROWS 7

/**
 * Detail pane row `row` as shown: its chrome widget, its full text (what the
 * tooltip says when it was cut), and the track the shown text was measured
 * against (device px, the font the row draws in). 0 past the last row.
 */
int
Editor_SquarePicker_PaneRow(
    struct EditorUi* ui,
    int row,
    int* out_widget,
    const char** out_full,
    int* out_track);

/** The open-set strip: its GRAPH, how many chips it holds, how many show
 *  (the rest are its "+N"). 0 without a picker. */
int
Editor_SquarePicker_Strip(
    struct EditorUi* ui,
    int* out_graph,
    int* out_count,
    int* out_shown);

/** The Recent list, newest first: `count` keys. */
int
Editor_SquarePicker_Recent(
    struct EditorUi* ui,
    const char** out_keys,
    int max);

/* ---- what the instance helper reads and marks (workbench 6.5) ---------------- */

enum Editor_SquarePickerSources
{
    /** The shift-clicked squares, else the focused one. */
    EDITOR_SQUARE_PICKER_SELECTION = 0,
    /** The open-set strip, in its order. */
    EDITOR_SQUARE_PICKER_OPEN_SET,
};

/** Up to `max` full square keys of `which` into `out`; returns how many. 0
 *  without a picker or an atlas. */
int
Editor_SquarePicker_Sources(
    struct EditorUi* ui,
    enum Editor_SquarePickerSources which,
    char (*out)[EDITOR_DOC_KEY_MAX],
    int max);

/** Mark squares [x, x + w) x [z, z + h) in the picker (outlined at every
 *  zoom) and centre the view on them; w 0 clears the mark. */
void
Editor_SquarePicker_Mark(
    struct EditorUi* ui,
    int map_x,
    int map_z,
    int w,
    int h);

/* ---- the Triggers panel (edpanel_triggers.c) --------------------------------- */

struct Builder_SscResult;

/** The script hooks of a loc / npc / obj (or of one file): the catalog's
 *  TRIGGER rows, Add trigger, Open in editor, Refresh, Check, Run. Scripts
 *  are edited in the user's own editor, not here. Hidden until asked for
 *  (View > Triggers, Ctrl+E, the Record panel's Triggers...). "triggers" */
void
Editor_Triggers_Register(struct EditorUi* ui);

/** Where the files are: the content root (Open in editor's absolute paths,
 *  Refresh's disk reads) and, for Check, sscompile and a work dir outside
 *  the tree (NULL: whatever TORIRS_BUILDER_* set up at registration stays).
 *  Returns 1 when Check can run. */
int
Editor_Triggers_SetPaths(
    struct EditorUi* ui,
    const char* content_root,
    const char* sscompile,
    const char* work_dir);

/** The panel's modes ("Show"). */
enum Editor_TriggersMode
{
    /** The Record panel's record, else the selected loc / spawn, else the
     *  catalog's pick. */
    EDITOR_TRIGGERS_FOLLOW = 0,
    /** A record name typed in Subject. */
    EDITOR_TRIGGERS_NAME,
    /** Every trigger in one file (File, under server/scripts). */
    EDITOR_TRIGGERS_FILE,
};

/** Show the triggers of record `name` (type found through the catalog when
 *  `type` is NULL or ""). Shows the panel. Returns the row count. */
int
Editor_Triggers_ShowSubject(
    struct EditorUi* ui,
    const char* type,
    const char* name);

/** Show every trigger declared in `rel_path` (under server/scripts). */
int
Editor_Triggers_ShowFile(
    struct EditorUi* ui,
    const char* rel_path);

/** Follow the Record panel / the selection again, and show the panel. */
void
Editor_Triggers_Follow(struct EditorUi* ui);

/** One trigger: where it is declared. */
struct Editor_ScriptRef
{
    char kind[24];
    char subject[128];
    /** Under server/scripts. */
    char rel[124];
    /** 1-based. */
    int line;
    /** Read from this window's unsaved document, not the disk. */
    int unsaved;
};

/** The rows the panel shows now. */
int
Editor_Triggers_Rows(
    struct EditorUi* ui,
    struct Editor_ScriptRef* out,
    int max);

/** The subject shown ("" in file mode or none), and its type. */
const char*
Editor_Triggers_Subject(
    struct EditorUi* ui,
    const char** out_type);

/**
 * Every trigger whose subject is `subject`: the catalog's TRIGGER rows (as
 * the tree was last indexed: it re-stats changed files at most every 2 s),
 * with the subject's edited script read from this window's document instead
 * when it is open (so a trigger added and not saved shows).
 */
int
Editor_Triggers_ForSubject(
    struct EditorUi* ui,
    const char* subject,
    struct Editor_ScriptRef* out,
    int max);

/**
 * Add trigger `kind` to the subject shown: when the kind answers a menu
 * entry the record lacks, the field (op<N> / ifop<N> = `op_label`, through
 * the Record panel, written where it would write it) and the template
 * appended to server/scripts/edited/<s>/scripts/<s>.rs2 (opened empty when
 * it has no file), ONE undo step, one History row "Add [kind,s] (op3=X)".
 * A trigger the tree already declares is refused (Open in editor shows it).
 * Returns 1 when added.
 */
int
Editor_Triggers_AddTrigger(
    struct EditorUi* ui,
    const char* kind,
    const char* op_label);

/** Hand server/scripts/`rel_path` at `line` to the user's editor
 *  (Editor_HostLocalOpenInEditor). A document of that file with unsaved
 *  edits here is saved first, so the editor opens what the session holds.
 *  Returns 1 when handed over; 0 when it could not be (the panel shows the
 *  path to copy). */
int
Editor_Triggers_OpenInEditor(
    struct EditorUi* ui,
    const char* rel_path,
    int line);

/** Re-read the shown files from disk and ask the catalog again; a document
 *  of one of them open here whose text differs from the disk is marked
 *  "edited outside". Returns how many are. */
int
Editor_Triggers_Refresh(struct EditorUi* ui);

/** Make every "edited outside" document hold the disk's text again (one
 *  undo step per file). Returns the files reloaded. */
int
Editor_Triggers_ReloadFromDisk(struct EditorUi* ui);

/** sscompile over the scripts ON DISK (synchronous; unsaved documents are
 *  not in it). Returns the errors, -1 when it could not run. */
int
Editor_Triggers_Check(struct EditorUi* ui);

/** Show a compile result as Check does (the Problems rows, the status). */
void
Editor_Triggers_ShowResult(
    struct EditorUi* ui,
    const struct Builder_SscResult* result);

/* ---- hotkeys (edpanel_hotkeys.c, docs/MAPED_HOTKEYS.md) -------------------- */

/** The Hotkeys panel: every action's group, name, context and chord, a
 *  search box, click a chord to rebind it, clashes in red, Reset to
 *  defaults. Hidden until View > Hotkeys panel (F1). "hotkeys" */
void
Editor_Hotkeys_Register(struct EditorUi* ui);

/**
 * Run `action` the way its menu row or button does: the ONE entry point the
 * menu bar and the App's hotkey dispatcher share. The App-bound actions (the
 * camera, the eyedroppers, Select the square's locs, Apply to selection, the
 * click accelerators) answer 0: the App runs those itself. Returns 1 when
 * the action was run here.
 */
int
Editor_HotkeyRun(
    struct EditorUi* ui,
    enum Editor_HotkeyAction action);

/** Force the dock's grid snap while the hold key (view.snap_hold) is down;
 *  the View toggle is untouched. Called once per frame by the dispatcher. */
void
Editor_HotkeySnapHold(
    struct EditorUi* ui,
    int held);

/** The picker's storage at `storage_index` as a document storage ("" for the
 *  content tree's maps/): what a relayed open resolves its index through.
 *  Returns 0, with `out` empty, when this window has no Squares panel or the
 *  index is outside its atlas. */
int
Editor_Squares_StorageName(
    struct EditorUi* ui,
    int storage_index,
    char out[EDITOR_DOC_STORAGE_MAX]);

/** The action the Hotkeys panel is capturing a chord for, or -1. */
int
Editor_Hotkeys_Capturing(struct EditorUi* ui);

/** Start capturing a chord for `action` (a click on its row). */
void
Editor_Hotkeys_BeginCapture(
    struct EditorUi* ui,
    enum Editor_HotkeyAction action);

/**
 * While capturing: this frame's keys go to the capture, not the table. Esc
 * cancels, Backspace unbinds, any other chord (a modifier alone only for Fly
 * faster) binds and the capture ends. Returns 1 while capturing (the frame's
 * keys are the panel's), 0 when not capturing.
 */
int
Editor_Hotkeys_Feed(
    struct EditorUi* ui,
    const struct Editor_HotkeyInput* input);

/* ---- the Catalog palette (edpanel_catalog_palette.c) ----------------------
 *
 * Named strips of up to ten loc / npc / obj records over the PALETTES
 * document (editor/editor_palette.h) every window mirrors: click a slot to
 * make it the catalog pick and arm Place. App-bound (it picks through the
 * Catalog). "catalog_palette" */

/** Thumbnail cache entries; the App draws entry `slot` as scene sprite
 *  EDITOR_CATALOG_PALETTE_SCENE_BASE + slot, EDITOR_CATALOG_PALETTE_THUMB_PX
 *  square (one size, so an atlas renderer reuses its place). */
#define EDITOR_CATALOG_PALETTE_THUMBS 32
#define EDITOR_CATALOG_PALETTE_THUMB_PX 64
/** Free in engine/uitree_scene_bridge.h's registry: past the picker map's
 *  80 slots at 0x4C000000. */
#define EDITOR_CATALOG_PALETTE_SCENE_BASE 0x4C001000

void
Editor_CatalogPalette_Register(struct EditorUi* ui);

/** Slot `slot` (1..10) of the shown palette, as a click on it: the catalog
 *  pick (published) and the Place tool armed, a loc with the slot's pose.
 *  0 (said in the console) when the slot is empty or the catalog has no
 *  such record. */
int
Editor_CatalogPalette_UseSlot(
    struct EditorUi* ui,
    int slot);

/** The catalog's pick into slot `slot` (1..10, replacing what it held) of the
 *  shown palette, or `slot` 0: the first free one; a loc with the Place
 *  tool's pose. No palette yet: a new one, "Palette 1". Returns the slot it
 *  went to, 0 when it did not (said in the console). */
int
Editor_CatalogPalette_AddPick(
    struct EditorUi* ui,
    int slot);

/** Show palette `name`. 0 when there is no such palette. */
int
Editor_CatalogPalette_Show(
    struct EditorUi* ui,
    const char* name);

/** Show the next (`step` 1) or previous (-1) palette, wrapping. 0 when there
 *  is no other palette. */
int
Editor_CatalogPalette_Step(
    struct EditorUi* ui,
    int step);

/** The shown palette's name, "" when none. */
const char*
Editor_CatalogPalette_Current(struct EditorUi* ui);

/** For the App: a thumbnail the strip wants drawn (record `kind` / `id` into
 *  cache entry `slot`). 0 when none is wanted or the panel is closed. */
int
Editor_CatalogPalette_ThumbWanted(
    struct EditorUi* ui,
    int* out_kind,
    int* out_id,
    int* out_slot);

/** The App drew cache entry `slot` as scene sprite `scene_id` (0: the record
 *  has no model; the cell keeps its name). */
void
Editor_CatalogPalette_ThumbDone(
    struct EditorUi* ui,
    int slot,
    int scene_id);

/* ---- Publish (edpanel_publish.c): the editor lane and where its changes go ---- */

/** Register the Publish panel (module "publish"); needs ui->editor. */
void
Editor_Publish_Register(struct EditorUi* ui);

/** File > Publish... (Ctrl+Shift+U): toggle the panel. Returns what
 *  Editor_UiTogglePanel does; 0 with a log line when there is none. */
int
Editor_Publish_Open(struct EditorUi* ui);

#endif
