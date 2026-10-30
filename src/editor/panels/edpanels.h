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

/** The New square / Copy to / Save as new modal. "new_square" */
void
Editor_NewSquare_Register(struct EditorUi* ui);

/** The unsaved-edit guard: Save / Discard / Cancel. "save_guard" */
void
Editor_SaveGuard_Register(struct EditorUi* ui);

/** The provider-backed loc / npc / obj catalog with the model well.
 *  App-bound. "catalog" */
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

/** The catalog follows a selected loc: kind Locs, pick = that loc. */
void
Editor_Catalog_Adopt(
    struct EditorUi* ui,
    int loc_id);

/** Ask the picker to refetch the atlas (after a DOC_CREATE that this window
 *  made, before the broadcast lands). */
void
Editor_SquarePicker_Refresh(struct EditorUi* ui);

#endif
