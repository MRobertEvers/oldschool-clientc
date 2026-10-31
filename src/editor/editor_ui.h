#ifndef SRC_EDITOR_EDITOR_UI_H
#define SRC_EDITOR_EDITOR_UI_H

/**
 * The editor's panel registry (docs/MAPED_DESIGN.html #chrome, #terrain,
 * #squares, #placement; brief section 1, phase 3 `tools-panels`).
 *
 * The main window is a SET of panel modules -- menu bar, tool rail, terrain
 * brushes, palettes, rectangle select, locs, spawns, inspector, console, the
 * save guard, the square picker and the New square dialog, and whatever the
 * builder and interface owners add -- each one a file under
 * src/editor/panels/ that exports `Editor_<Panel>_Register(struct EditorUi*)`.
 * A module builds its chrome widgets in its Register call and hands this
 * registry a struct EditorUiPanel: what to run per frame, which activations
 * and GRAPH/CODE events are its own, what to do on a shared-state fact.
 *
 * The registry owns nothing a module draws. It owns the one thing every module
 * shares and none may keep a private copy of: the TOOL STATE (which tool, the
 * brush, the palette picks, the level, the multi-selection) -- so the tool
 * rail, the terrain panel, the world click and the ghost read the same answer
 * -- and the per-frame dispatch, because ToriRSChrome hands out ONE activation
 * per frame and one event queue, and two modules each draining them would
 * each see half.
 *
 * App-free by construction: a module that needs the world, the provider or
 * the window reaches it through `hooks` (filled by editor_panel.c for the
 * client, by a test for a fake), or -- the App-bound modules (catalog,
 * inspector, locs) -- through `app`, which only those include app.h to read.
 * That is what lets the picker, the New dialog and the save guard run in a
 * headless test against the real chrome model and an embedded ToriRSMapEd.
 *
 * Contract violations assert (CLAUDE.md). A registry full of panels is a
 * build-time sizing bug, not a runtime state.
 */

#include "editor/editor_cmd.h"
#include "editor/editor_host.h"
#include "editor/editor_layout.h"

#include <stdarg.h>
#include <stdint.h>

struct Editor;
struct ToriRSChrome;
struct ToriRSChromeEvent;

/* ---- tools ----------------------------------------------------------------
 *
 * What the next world click does. TORIRSMAPED_STATE_TOOL carries this value,
 * so every connection of a Client agrees which tool is armed.
 */
enum Editor_Tool
{
    /** Latch what is under the cursor (tile, or a loc through the minimenu). */
    EDITOR_TOOL_SELECT = 0,
    /** Drag a tile rectangle: copy, paste, fill, clear act on it. */
    EDITOR_TOOL_RECT_SELECT,
    /** Height brushes: TILE commands on the `h` token, grouped per stroke. */
    EDITOR_TOOL_RAISE,
    EDITOR_TOOL_LOWER,
    EDITOR_TOOL_FLATTEN,
    EDITOR_TOOL_SMOOTH,
    /** Paint brushes: the `u`, `o` (id, shape, rotation) and `f` tokens. */
    EDITOR_TOOL_PAINT_UNDERLAY,
    EDITOR_TOOL_PAINT_OVERLAY,
    EDITOR_TOOL_PAINT_FLAGS,
    /** Stamp the catalog's picked loc (or npc / obj spawn) at the click. */
    EDITOR_TOOL_LOC_PLACE,
    /** Move the selected loc(s) to the clicked tile. */
    EDITOR_TOOL_LOC_MOVE,
    /** Rotate the loc under the click a quarter turn. */
    EDITOR_TOOL_LOC_ROTATE,
    /** Delete the loc (or spawn) under the click. */
    EDITOR_TOOL_LOC_DELETE,
    EDITOR_TOOL_COUNT
};

/** The tool's name, for the rail tooltips and the console. */
const char*
Editor_ToolName(enum Editor_Tool tool);

/** Height and paint brushes take the brush controls; the rest do not. */
int
Editor_ToolIsBrush(enum Editor_Tool tool);

/** How a brush's effect fades from its centre to its rim. */
enum Editor_BrushFalloff
{
    EDITOR_BRUSH_FALLOFF_LINEAR = 0,
    EDITOR_BRUSH_FALLOFF_SMOOTH,
    EDITOR_BRUSH_FALLOFF_FLAT,
    EDITOR_BRUSH_FALLOFF_COUNT
};

/** Largest brush radius, in tiles: a radius-16 disc is 33x33 tiles, one
 *  command per tile, inside one stroke. */
#define EDITOR_BRUSH_RADIUS_MAX 16
/** Tiles one brush application can touch, the out-array bound. */
#define EDITOR_BRUSH_TILES_MAX ((2 * EDITOR_BRUSH_RADIUS_MAX + 1) * (2 * EDITOR_BRUSH_RADIUS_MAX + 1))

struct Editor_Brush
{
    /** 0 is one tile. */
    int radius;
    enum Editor_BrushFalloff falloff;
    /** Raise / lower: authored height units (8 world units each) per apply. */
    int step;
    /** Smooth: 0..100, how far toward the neighbours' mean one apply goes. */
    int strength;
};

/** A loc placement by DOCUMENT address -- the selection's handle.
 *
 * Not a scene element id: an element dies on every reshape (a loc change is a
 * delete plus an add) and is per connection, while (square, level, tile,
 * shape) is what Editor_SquareLocFind and WorldEdit_FindPlacement both key
 * on, the same in every mirror and every view. */
struct Editor_LocRef
{
    int map_x;
    int map_z;
    int level;
    int x;
    int z;
    int shape;
    int loc_id;
    int rotation;
};

/** Locs one multi-selection holds. A shift-click set, not a region. */
#define EDITOR_LOC_SELECTION_MAX 256

/** The rectangle the RECT_SELECT tool latched, in ABSOLUTE world tiles,
 *  inclusive on both corners. */
struct Editor_TileRect
{
    int valid;
    int x0;
    int z0;
    int x1;
    int z1;
    int level;
};

/**
 * The shared tool state. One per main window; every module reads it, and the
 * module that owns a control writes it.
 */
struct EditorUiTools
{
    enum Editor_Tool tool;
    struct Editor_Brush brush;

    /** Paint picks: the palette writes them, the brushes read them. */
    int underlay_id;
    /** has_overlay 0 with overlay "none" clears the overlay. */
    int overlay_id;
    int overlay_none;
    int overlay_shape;
    int overlay_rotation;
    /** Flags brush: the `f` bits it writes (1 block, 2 link-below,
     *  4 remove-roof, 8 vis-below). */
    int flags_value;

    /** Edit level: -1 follows the pick, 0..3 pins a plane (the CACHE level). */
    int edit_level;
    /** Vis cap: -1 every level, 0..3 cumulative; `vis_solo` just the one. */
    int vis_level;
    int vis_solo;

    /** Flatten's target: the derived height of the stroke's first tile, set on
     *  press, read on every drag. */
    int flatten_valid;
    int flatten_height;

    /** The Place tool's pose: what the ghost shows and the click stamps. */
    int place_shape;
    int place_rotation;

    /** Multi-selection of locs, by document address. */
    struct Editor_LocRef locs[EDITOR_LOC_SELECTION_MAX];
    int loc_count;

    struct Editor_TileRect rect;

    /** The latched TILE (the select tool's terrain subject), absolute. */
    int tile_valid;
    int tile_x;
    int tile_z;
    int tile_level;

    /** The catalog's pick: kind 0 loc, 1 npc, 2 obj (enum
     *  CacheProvider_CatalogKind order), -1 none; and its display name. */
    int pick_kind;
    int pick_id;
    char pick_name[64];

    /** A drag stroke is open on the authority (the world click opened it on
     *  press): panel edits made meanwhile join it instead of opening their
     *  own. */
    int stroke_open;
};

/* ---- hooks ---------------------------------------------------------------
 *
 * What the panels ask of the window around them. Any may be NULL: the panel
 * then says the action is unavailable in the console rather than doing half
 * of it.
 */
struct EditorUiHooks
{
    /** Open square (map_x, map_z) from `storage` ("" = maps/) in this window:
     *  `add` 0 replaces what the window shows, 1 adds it to the open set. */
    void (*open_square)(void* user, int map_x, int map_z, const char* storage, int add);
    /** Open the square in a NEW view window (design #windows). */
    void (*open_in_new_view)(void* user, int map_x, int map_z);
    /** A floor config's colour (0xRRGGBB): `is_overlay` 0 underlay, 1 overlay.
     *  0 when the record is not resident. */
    int (*floor_rgb)(void* user, int is_overlay, int id, uint32_t* out_rgb);
    /** Close the editor's chrome (File > Close, after the save guard). */
    void (*close_editor)(void* user);
    /** Bake the cache (File > Bake). */
    void (*bake)(void* user);
    /** The window's view of the world, for the picker's own frame: SW square
     *  and size in squares. 0 when the window has no world. */
    int (*view_window)(void* user, int* out_map_x, int* out_map_z, int* out_w, int* out_h);
    /** The loc multi-selection changed (the window re-latches its primary
     *  selection and its highlight on tools.locs[0]). */
    void (*selection_changed)(void* user);
};

/* ---- panels ---------------------------------------------------------------- */

struct EditorUi;

struct EditorUiPanel
{
    /** Stable lower-case name ("squares"); the View menu and the tests use it. */
    const char* name;
    /** What the View menu calls it; NULL keeps it off the menu. */
    const char* title;
    /** Module-owned state, allocated in Register, freed by `destroy`. */
    void* self;
    /** The chrome panel the View menu toggles; -1 for none. */
    int chrome_panel;

    /** Once per frame while the editor is visible. May be NULL. */
    void (*tick)(struct EditorUi* ui, void* self);
    /** The frame's activated widget: 1 when it was this module's. */
    int (*activate)(struct EditorUi* ui, void* self, int widget);
    /** One queued GRAPH gesture or CODE edit: 1 when it was this module's. */
    int (*event)(struct EditorUi* ui, void* self, const struct ToriRSChromeEvent* event);
    /** The editor opened or closed. The registry already showed or hid
     *  `chrome_panel`; a module with more panels handles the rest here. */
    void (*set_visible)(struct EditorUi* ui, void* self, int visible);
    /** A shared-state fact from this connection's Client (any key). */
    void (*on_state)(struct EditorUi* ui, void* self, uint32_t key, const int32_t* values, int count);
    void (*destroy)(struct EditorUi* ui, void* self);
    /**
     * Optional, for a panel with a part that grows into whatever height the
     * layout gives it (the picker's map well). Returns that part's current
     * height in chrome pixels and states the least it may shrink to and the
     * height it asks for; the default layout measures the panel without it.
     * NULL: the panel is its content's height.
     */
    int (*stretch)(struct EditorUi* ui, void* self, int* out_min_h, int* out_natural_h);
};

/** A box in surface pixels. */
struct EditorUiRect
{
    int x;
    int y;
    int w;
    int h;
};

#define EDITOR_UI_PANELS_MAX 48
/** Chrome windows the default arrangement tracks (the chrome's own cap). */
#define EDITOR_UI_LAYOUT_PANELS_MAX 64

/** Lines the output console keeps, and their width. */
#define EDITOR_UI_CONSOLE_LINES 512
#define EDITOR_UI_CONSOLE_LINE_MAX 160

/** What a guarded action is waiting on: the save/discard modal answers it. */
enum EditorUi_Guarded
{
    EDITOR_UI_GUARD_NONE = 0,
    /** Open a square in place of the current set. */
    EDITOR_UI_GUARD_OPEN_SQUARE,
    /** Close the editor. */
    EDITOR_UI_GUARD_CLOSE,
};

struct EditorUiGuard
{
    enum EditorUi_Guarded action;
    int map_x;
    int map_z;
    char storage[EDITOR_DOC_STORAGE_MAX];
};

/** Other views' windows, as the picker frames them (STATE_VIEW_SQUARE). */
#define EDITOR_UI_VIEW_FRAMES_MAX 16

struct EditorUiViewFrame
{
    int used;
    int slot;
    int map_x;
    int map_z;
    int w;
    int h;
    int64_t seen_ms;
};

struct EditorUi
{
    struct ToriRSChrome* chrome;
    /** The session; NULL in a window with no editor (every module tolerates
     *  it and says so). */
    struct Editor* editor;
    /** The client App for the App-bound modules; opaque here. */
    void* app;

    struct EditorUiHooks hooks;
    void* hooks_user;

    struct EditorUiTools tools;

    struct EditorUiPanel panels[EDITOR_UI_PANELS_MAX];
    int panel_count;

    int visible;
    /** Set by the host each frame: shift is held (the picker's add-click and
     *  the loc multi-select read it). */
    int shift_held;

    /** Set while a received fact is applied, so a module's own latching does
     *  not publish back what the server just said. */
    int applying_state;

    /** The pending action the save guard is asking about. */
    struct EditorUiGuard guard;

    /** STATE_VIEW_SQUARE frames from every view of this Client. */
    struct EditorUiViewFrame frames[EDITOR_UI_VIEW_FRAMES_MAX];
    /** This window's slot in values[4] of its own STATE_VIEW_SQUARE. */
    int view_slot;
    int view_published[4];
    int64_t view_published_ms;

    /** The output console's ring; newest at (console_head - 1). */
    char console[EDITOR_UI_CONSOLE_LINES][EDITOR_UI_CONSOLE_LINE_MAX];
    int console_head;
    int console_count;
    /** Bumps on every line, so the console panel refreshes only on change. */
    int console_serial;

    /** The default layout ran for the current surface and scale
     *  (Editor_UiTick, the first frame the surface and the panels have
     *  sizes); cleared by a surface or scale change and by Reset layout. */
    int laid_out;
    /** The chrome panels the registered modules created, [panel_first,
     *  panel_end), and the chrome scale their pixel sizes were given in: a
     *  later scale change (the display's density arrives after boot) scales
     *  them to match. */
    int panel_first;
    int panel_end;
    int layout_scale;
    /** The surface the layout was made for. */
    int layout_surface_w;
    int layout_surface_h;
    /** View > Snap to grid (default on): dropped and resized panels snap to
     *  the EDP_GRID cell and to their neighbours' edges. */
    int snap;
    /** The region the default layout left for the world -- where it docks
     *  the World panel (the world rect itself is that panel's body,
     *  Editor_UiWorldRect) -- and the dock column on the right edge (x,
     *  width) and the band it runs in (top, bottom). */
    struct EditorUiRect world;
    int dock_x;
    int dock_w;
    int dock_top;
    int dock_bottom;
    /**
     * Per module window (indexed by chrome panel - panel_first):
     *   member      part of the default set (shown by the first layout, or
     *               opened since and docked by a later one);
     *   boot_member the default set as the first layout found it (Reset);
     *   docked      placed in the column by the last layout;
     *   closed      docked, then closed by the user: a re-layout leaves it shut;
     *   flown       opened while not docked, and placed beside the column.
     */
    unsigned char member[EDITOR_UI_LAYOUT_PANELS_MAX];
    unsigned char boot_member[EDITOR_UI_LAYOUT_PANELS_MAX];
    unsigned char docked[EDITOR_UI_LAYOUT_PANELS_MAX];
    unsigned char closed[EDITOR_UI_LAYOUT_PANELS_MAX];
    unsigned char flown[EDITOR_UI_LAYOUT_PANELS_MAX];
    int booted_layout;

    /** The World panel's chrome panel (module "world"), -1 before it is
     *  registered. Its body is the 3D view: Editor_UiWorldRect. */
    int world_panel;

    /**
     * The saved layout (editor_layout.h). `layout_hold` is set by the host
     * while the file's read is in flight: the default layout waits for it,
     * so the panels do not flash at their default places first.
     * `layout_pending` holds the file the read returned until the next
     * Editor_UiTick applies it, just before the first (or next) layout.
     */
    int layout_hold;
    int layout_pending;
    /** The applied file was made for this very surface and scale: after the
     *  next default layout has placed the column, every panel takes the
     *  file's box (Editor_UiLayoutApply). */
    int layout_restore;
    struct EditorLayoutFile layout_saved;
};

/** Zero the registry over `chrome`; no panels yet. `editor` may be NULL. */
void
Editor_UiInit(
    struct EditorUi* ui,
    struct ToriRSChrome* chrome,
    struct Editor* editor);

/** Destroy every registered panel's state. The chrome's widgets are the
 *  chrome's; a Reset of it removes them. */
void
Editor_UiFree(struct EditorUi* ui);

/** Add one panel. Returns its index. */
int
Editor_UiAddPanel(
    struct EditorUi* ui,
    const struct EditorUiPanel* panel);

/** The panel registered as `name`, or NULL. */
struct EditorUiPanel*
Editor_UiFindPanel(
    struct EditorUi* ui,
    const char* name);

/** The module state of the panel registered as `name`, or NULL. */
void*
Editor_UiPanelSelf(
    struct EditorUi* ui,
    const char* name);

/**
 * Register every tools-panels module (menu bar, tool rail, terrain, palette,
 * rectangle select, locs, spawns, inspector, console, save guard, square
 * picker, New square). The App-bound modules (catalog, inspector, locs) need
 * `ui->app`; with it NULL they are skipped.
 *
 * integrate-3 adds the other owners' Register calls beside this one.
 */
void
Editor_UiRegisterToolsPanels(struct EditorUi* ui);

void
Editor_UiSetVisible(
    struct EditorUi* ui,
    int visible);

/**
 * One frame: hand the frame's activation to the module that owns it, drain
 * the GRAPH/CODE events to their modules, run every tick, refresh this
 * window's STATE_VIEW_SQUARE. Call once per frame while visible.
 */
void
Editor_UiTick(struct EditorUi* ui);

/**
 * The default layout, for the surface the chrome has now: the menu bar
 * across the top, the tool rail at the left edge, one column of panels docked
 * to the right edge (Terrain, Palette, Locs, Spawns, Rectangle, Inspector,
 * Squares, Catalog, in that order, each given at least a few rows and the
 * rest of the column shared top-down), the output console as a strip along
 * the bottom of the world when it is short enough, and the World panel
 * docked over the rectangle that is left -- the largest on the surface (its
 * body is Editor_UiWorldRect). Panels of the default
 * set that do not fit the column are hidden (the View menu opens them, beside
 * the column). Panels the user dragged or resized keep their place.
 * Editor_UiTick runs it on the first frame and whenever the surface or the
 * chrome scale changes.
 */
void
Editor_UiLayout(struct EditorUi* ui);

/** View > Reset layout: every module window back in the default layout for
 *  the current surface, the default set shown again, drags forgotten. */
void
Editor_UiResetLayout(struct EditorUi* ui);

/** View > Snap to grid. */
void
Editor_UiSetSnap(
    struct EditorUi* ui,
    int on);

/**
 * The world view: the World panel's BODY (its box minus title bar and
 * borders) in surface pixels. Everything that draws or picks in the world --
 * the 3D viewport, the hover pick, the brush footprint, the ghosts, the
 * selection marks -- reads it through the App's world rect
 * (App_MapEditorWorldView, app_map_editor.c), so all of them follow the panel
 * as it moves and resizes. All zero before the first layout, and while the
 * World panel is hidden (View > World): then nothing is drawn or picked.
 * A registry without a World panel (a test) answers the region the default
 * layout left for it.
 */
struct EditorUiRect
Editor_UiWorldRect(const struct EditorUi* ui);

/** The World panel is open (and the layout has run). */
int
Editor_UiWorldShown(const struct EditorUi* ui);

/* ---- the saved layout (editor_layout.h has the file and the policy) ------- */

/** The live layout as a file: the surface, the scale, snap, and every
 *  View-menu panel's visibility, placement flag and geometry (at scale 1). */
void
Editor_UiLayoutCapture(
    const struct EditorUi* ui,
    struct EditorLayoutFile* out);

/**
 * Apply a saved layout now (the policy in editor_layout.h): snap and
 * visibility always, geometry for the panels the user placed, the rest left
 * to the default layout, which runs again on the next tick. Remembers the
 * modules' own default set first, so View > Reset layout still returns to
 * the default and not to the file.
 */
void
Editor_UiLayoutApply(
    struct EditorUi* ui,
    const struct EditorLayoutFile* file);

/** The host has started reading the layout file: hold the default layout. */
void
Editor_UiLayoutHold(struct EditorUi* ui);

/** The read finished: `file` (NULL when there was none) is applied by the
 *  next Editor_UiTick, before its layout pass. Releases the hold. */
void
Editor_UiLayoutLoaded(
    struct EditorUi* ui,
    const struct EditorLayoutFile* file);

/** A cheap digest of everything Capture writes (the registry's panels, not
 *  the UI tree): the host saves when it moves. */
uint32_t
Editor_UiLayoutFingerprint(const struct EditorUi* ui);

/** Not a moment to save: a drag, resize or splitter gesture is open, the
 *  layout has not run, or the saved one is still being read. */
int
Editor_UiLayoutBusy(const struct EditorUi* ui);

/**
 * A window whose panels are hidden (a viewer, or the main window with its
 * panels closed) still shows a part of the world: keep its STATE_VIEW_SQUARE
 * frame published, so every picker frames it. Call once per frame while the
 * panels are hidden; Editor_UiTick does the same while they show.
 */
void
Editor_UiPublishView(struct EditorUi* ui);

/** Forward a shared-state fact to every module (and keep the view frames). */
void
Editor_UiApplyState(
    struct EditorUi* ui,
    uint32_t key,
    const int32_t* values,
    int count);

/** Publish one shared-state field, unless a received fact is being applied
 *  (or there is no session). Returns 1 when sent. */
int
Editor_UiPublish(
    struct EditorUi* ui,
    uint32_t key,
    const int32_t* values,
    int count);

/** Arm a tool: tools state, STATE_TOOL, console. */
void
Editor_UiSetTool(
    struct EditorUi* ui,
    enum Editor_Tool tool);

/** Append a console line (also narrated through TORIRS_LOG). */
void
Editor_UiLog(
    struct EditorUi* ui,
    const char* format,
    ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

void
Editor_UiLogV(
    struct EditorUi* ui,
    const char* format,
    va_list args);

/** The `index`th newest console line (0 newest), or NULL past the end. */
const char*
Editor_UiConsoleLine(
    const struct EditorUi* ui,
    int index);

/**
 * Run `guard` now when nothing is unsaved, else hold it and open the save
 * guard modal (Save / Discard / Cancel), which runs or drops it. Returns 1
 * when it ran immediately.
 */
int
Editor_UiGuarded(
    struct EditorUi* ui,
    const struct EditorUiGuard* guard);

/** Run a guard's action (the save guard's Save and Discard end here). */
void
Editor_UiRunGuarded(
    struct EditorUi* ui,
    const struct EditorUiGuard* guard);

/* ---- the loc multi-selection ---------------------------------------------- */

/** Replace the selection with one loc (or clear it with NULL). */
void
Editor_UiSelectLoc(
    struct EditorUi* ui,
    const struct Editor_LocRef* ref);

/** Add `ref` to the selection, or take it out when already in (shift-click).
 *  Returns 1 when it is selected afterwards. */
int
Editor_UiToggleLoc(
    struct EditorUi* ui,
    const struct Editor_LocRef* ref);

/** Index of `ref` (same square, level, tile and shape) in the selection, or -1. */
int
Editor_UiFindLoc(
    const struct EditorUi* ui,
    const struct Editor_LocRef* ref);

#endif
