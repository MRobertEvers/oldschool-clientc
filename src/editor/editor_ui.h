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
 * inspector, areas) -- through `app_services`, which the App layer fills
 * (src/app/app_map_editor.c). `app` stays opaque: it says only that this
 * window has a client, and no module reads through it.
 * That is what lets the picker, the New dialog and the save guard run in a
 * headless test against the real chrome model and an embedded ToriRSMapEd.
 *
 * Contract violations assert (CLAUDE.md). A registry full of panels is a
 * build-time sizing bug, not a runtime state.
 */

#include "editor/editor_cmd.h"
#include "editor/editor_host.h"
#include "editor/editor_hotkeys.h"
#include "editor/editor_layout.h"
#include "editor/editor_prefs.h"

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
    /** Put the lane's play account on the clicked tile (the Accounts panel:
     *  ::tele online, the save offline; editor/accounts/accounts_panel.h). */
    EDITOR_TOOL_PLACE_PLAYER,
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

/* ---- App services ---------------------------------------------------------
 *
 * What the App-bound modules (catalog, inspector, areas) ask of the client
 * App around them, filled by the App layer (app_map_editor_ui_services in
 * src/app/app_map_editor.c) before the modules register. The modules name no
 * App: this is the whole of what they see of it. Every member may be NULL --
 * a headless test fills none -- and each says what a NULL one reads as.
 */
struct ToriRS_Location;
struct EditorViewCamera;

/** The area id worldmap_area answers for "*": every area at once, on the
 *  true square grid (the picker's Full map). */
#define EDITOR_UI_MAP_AREA_ALL 0x10000
/** Scene sprite slots the picker's map may hold at once (image_publish). */
#define EDITOR_UI_MAP_IMAGE_SLOTS 80
#define EDITOR_UI_MAP_LABEL_MAX 32

/** One map element for the picker (EditorUiAppServices worldmap_icons): an
 *  icon (`scene_id` > 0, `w` x `h` its sprite) or a text label (`label`
 *  set, `text_size` 0 small .. 2 large), at a WORLD tile. */
struct EditorUiMapIcon
{
    int tile_x;
    int tile_z;
    int scene_id;
    int w;
    int h;
    int text_size;
    char label[EDITOR_UI_MAP_LABEL_MAX];
};

struct EditorUiAppServices
{
    void* user_data;
    /** The cache's loc record `loc_id`, or NULL when it is not resident.
     *  NULL member: no record is ever resident. */
    struct ToriRS_Location* (*loc_record)(void* user_data, int loc_id);
    /** Ask for loc `loc_id`'s record to be loaded; loc_record answers on a
     *  later frame. NULL member: nothing is requested. */
    void (*loc_request)(void* user_data, int loc_id);
    /** The tile under the pointer, ABSOLUTE (with a level pinned, the pinned
     *  plane's tile), and the level edits go to. 0 when nothing is hovered.
     *  NULL member: nothing is ever hovered. */
    int (*hover_tile)(void* user_data, int* out_world_x, int* out_world_z, int* out_level);
    /** The loaded scene: its SW absolute tile and its size in tiles. 0 when
     *  the window has no world. NULL member: no world. */
    int (*scene_window)(void* user_data, int* out_base_x, int* out_base_z, int* out_size);
    /** Latch the terrain tile at ABSOLUTE (x, z) on `level` as the selection
     *  (the inspector's "View tile"). NULL member: nothing happens. */
    void (*select_terrain)(void* user_data, int world_x, int world_z, int level);
    /** Put the free camera over ABSOLUTE tile (x, z) on `level`, keeping its
     *  yaw and pitch. 0 when that tile is not in the loaded scene (the caller
     *  opens its square and retries). NULL member: always 0. */
    int (*camera_to_tile)(void* user_data, int tile_x, int tile_z, int level);
    /** Put the catalog's model preview back at its default pose. NULL
     *  member: nothing happens. */
    void (*preview_invalidate)(void* user_data);
    /** Orbit the catalog's model preview by these steps (2048 a turn), as
     *  its orbit keys do: a gesture over the Preview's well. NULL member:
     *  nothing happens. */
    void (*preview_orbit)(void* user_data, int pitch_step, int yaw_step);
    /** Zoom it by a pinch's ratio, 65536 = 1 (> 1 closer). NULL member:
     *  nothing happens. */
    void (*preview_zoom)(void* user_data, int ratio_q16);
    /** 1 while the world is busy: a load in flight, or a square open still
     *  pending. NULL member: never busy. */
    int (*world_busy)(void* user_data);
    /** The window should draw again. NULL member: nothing happens. */
    void (*request_redraw)(void* user_data);
    /** The World panel's camera as a view pose (editor/editor_view.h: the
     *  point looked at, its level, yaw, pitch, distance), read on `level`'s
     *  ground. 0 when the window has no world. NULL member: no camera. */
    int (*world_camera)(void* user_data, int level, struct EditorViewCamera* out);
    /** Open a window of this session that shows registry module `module`
     *  alone, filling it ("world" = the 3D view) -- a popped-out panel --
     *  looking at `camera`. 1 when the window was asked for; 0 when it
     *  cannot be, with the reason in `why` (one line, no newline). NULL
     *  member: nothing can pop out of this window. */
    int (*spawn_panel)(void* user_data, const char* module, const struct EditorViewCamera* camera, char* why,
        int why_size);

    /* The world map under the square picker (editor/editor_worldmap.h).
     * area / poll / render_tile / image_publish all or none: a NULL member
     * (the headless harness, the web) and the picker draws its coloured
     * cells as before; icons is optional. Coordinates are WORLD squares and
     * tiles; the App maps them through the area's compositemap. */
    /** The cache's world-map area whose internal name is `internal_name`
     *  ("main", "ancient_cavern"): its id, -1 when the cache has none, -2
     *  when the cache's areas are not loaded yet (asked for: ask again).
     *  "*" is EDITOR_UI_MAP_AREA_ALL, every area at once. */
    int (*worldmap_area)(void* user_data, const char* internal_name);
    /** Move area `area_id`'s squares [x0, x0+w) x [z0, z0+h) along for a
     *  zoom of `px_per_square`: queue their loads, bake what is ready (past
     *  64 px a square, again at the zoom's own pixels a tile). Returns the
     *  bake serial (changes when anything new can be drawn), -1 for an
     *  unknown area; `*out_missing` the regions still to come (0: final). */
    int (*worldmap_poll)(
        void* user_data,
        int area_id,
        int square_x0,
        int square_z0,
        int squares_w,
        int squares_h,
        double px_per_square,
        int* out_missing);
    /** Draw one `size` x `size` picture of the map at `px_per_square` (real,
     *  >= 1) from what is baked: the rectangle at (pixel_x0, pixel_y0) of
     *  that zoom's global pixel space (game/rs_worldmap_overview.h
     *  struct RS_WorldMapOverviewTile), 0xFFRRGGBB where the map has a tile,
     *  0 where it has none. 0 for an unknown area (nothing written). */
    int (*worldmap_render_tile)(
        void* user_data,
        int area_id,
        double px_per_square,
        int pixel_x0,
        int pixel_y0,
        int size,
        uint32_t* out_argb);
    /** Make `argb` (w x h, copied) image `slot` (0 ..
     *  EDITOR_UI_MAP_IMAGE_SLOTS - 1) for a GRAPH image item; returns its
     *  scene sprite id, 0 when it cannot. */
    int (*image_publish)(void* user_data, int slot, const uint32_t* argb, int w, int h);
    /** The map elements on squares [x0, x0+w) x [z0, z0+h) of area
     *  `area_id`, what 595 draws over the map: up to `capacity` into `out`.
     *  An element whose config or sprite is not resident yet is asked for
     *  and counted in `*out_pending` (ask again). Returns how many. */
    int (*worldmap_icons)(
        void* user_data,
        int area_id,
        int square_x0,
        int square_z0,
        int squares_w,
        int squares_h,
        struct EditorUiMapIcon* out,
        int capacity,
        int* out_pending);
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

/* ---- panel groups (design #workbench, "Panel groups") ----------------------
 *
 * Several registered modules behind one tab strip. A group is ONE chrome
 * WINDOW panel, the frame: its title bar is the group's (Close and Pop out on
 * it act on the shown tab, a double-click maximises the group) and its first
 * row is a TABSTRIP of the members open in it, each titled with its panel's
 * title and, when the module has a View action, its chord in parentheses
 * ("Catalog (F8)", Editor_HotkeyDisplay). The shown tab's own window lays out
 * directly under the strip, the frame's width, with its title bar taken off
 * (the frame's is the group's); every other member's window is hidden.
 *
 * Why the members keep their own windows rather than moving their rows into
 * the frame: a widget belongs to its panel for life (the chrome refuses a
 * move across panels, ToriRSChrome_WidgetMoveAfter), the modules address
 * their rows and their pages through their own panel (Spawns carries a strip
 * of its own, and a panel has one), and every path that already works on a
 * member's window -- its pop-out, its View toggle, the module showing or
 * hiding itself -- keeps working unchanged. So the strip says which member
 * window lays out under it, and the members' visibility does the rest.
 *
 * The state is the registry's -- which members are open (in the strip), which
 * one is shown -- and it follows the members' windows every frame
 * (Editor_UiTick), so whatever showed or hid a window is obeyed:
 *   - a member window shown from anywhere (View, its hotkey, the module
 *     itself, a pop-in, Reset layout) joins its strip as the shown tab;
 *   - the shown member's window hidden from anywhere (View, Close on the
 *     frame, Pop out, the module itself) leaves the strip, and the tab shown
 *     before it (else a neighbour) comes up; a group with no open member hides
 *     its frame;
 *   - a click on a tab shows that member.
 * Editor_UiLayout docks a group as one window; the layout file keeps it as a
 * [group <name>] section (editor_layout.h).
 *
 * Groups belong to the registry Editor_UiRegisterToolsPanels builds (their
 * frames are counted among its windows); registering one anywhere else is a
 * caller bug and asserts.
 */
#define EDITOR_UI_GROUPS_MAX 8
#define EDITOR_UI_GROUP_MEMBERS_MAX 8
/** A frame title or a tab title: the chrome's label size (asserted equal). */
#define EDITOR_UI_GROUP_TITLE_MAX 64

struct EditorUiGroup
{
    /** Stable lower-case name ("right-top"): the layout file's section. */
    char name[EDITOR_LAYOUT_NAME_MAX];
    /** The frame's title ("Catalog / Locs / Spawns / Inspector"). */
    char title[EDITOR_UI_GROUP_TITLE_MAX];
    /** The frame (a WINDOW) and its TABSTRIP. */
    int chrome_panel;
    int tabs;

    /** Registry indices of the members, in strip order, and per member slot:
     *  open (in the strip), and the member window's own title while the
     *  frame wears the group's (`dressed`). */
    int members[EDITOR_UI_GROUP_MEMBERS_MAX];
    int member_count;
    unsigned char open[EDITOR_UI_GROUP_MEMBERS_MAX];
    unsigned char dressed[EDITOR_UI_GROUP_MEMBERS_MAX];
    char member_titles[EDITOR_UI_GROUP_MEMBERS_MAX][EDITOR_UI_GROUP_TITLE_MAX];
    /** The member slot whose window shows under the strip, -1 for none, and
     *  the one shown before it (what comes back when it closes). */
    int shown;
    int prev_shown;
    /** Hidden by the layout for want of room, its members still open: the
     *  next layout with room (or View on a member) brings it back as it was. */
    int parked;

    /** The group's box in surface pixels: the frame's origin and width and
     *  the shown window's height under it (0 = sized to its content), and the
     *  member window last put there (-1 none: the next one shown is placed).
     *  `pending_h` is a whole-group height from the layout file, resolved
     *  once the frame's own height is known. */
    int x;
    int y;
    int w;
    int body_h;
    int pending_h;
    int placed;
    /** The frame's height (title bar and strip) as last built, 0 unknown. */
    int frame_h;

    /** The strip's rows (borrowed by the TABSTRIP): one per open member, in
     *  member order, and the member slot each names. */
    char tab_labels[EDITOR_UI_GROUP_MEMBERS_MAX][EDITOR_UI_GROUP_TITLE_MAX];
    const char* tab_rows[EDITOR_UI_GROUP_MEMBERS_MAX];
    int tab_slots[EDITOR_UI_GROUP_MEMBERS_MAX];
    int tab_count;
    /** What the rows were made from (open set, shown, the hotkeys' serial). */
    uint32_t tabs_digest;
    int tabs_built;

    /** The default membership, in strip order: what Reset groups restores. */
    int home[EDITOR_UI_GROUP_MEMBERS_MAX];
    int home_count;
};

/** Lines the output console keeps, and their width. */
#define EDITOR_UI_CONSOLE_LINES 512
#define EDITOR_UI_CONSOLE_LINE_MAX 160
/** How long the status bar shows a status sentence, on the chrome's frame
 *  clock (design #rules R8: "the status bar shows it for 6 s"). */
#define EDITOR_UI_STATUS_MS 6000

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
    /** The picker's index of `storage`, what a relayed open carries (every
     *  window mirrors the same atlas, so the index names the same storage). */
    int storage_index;
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

/** TORIRSMAPED_TOOLS_VALUES (torirsmaped/torirs_maped.h), asserted equal in
 *  editor_ui.c. */
#define EDITOR_UI_TOOLS_VALUES 14

struct EditorUi
{
    struct ToriRSChrome* chrome;
    /** The session; NULL in a window with no editor (every module tolerates
     *  it and says so). */
    struct Editor* editor;
    /** The client App; opaque here, and read by nobody: non-NULL only says
     *  this window has a client, which is what registers the App-bound
     *  modules. What they ask of it goes through `app_services`. */
    void* app;
    /** Filled by the App layer before registration; zeroed by Editor_UiInit
     *  (every member NULL) for a window without one. */
    struct EditorUiAppServices app_services;

    struct EditorUiHooks hooks;
    void* hooks_user;

    struct EditorUiTools tools;

    struct EditorUiPanel panels[EDITOR_UI_PANELS_MAX];
    int panel_count;

    int visible;
    /** Set by the host each frame: shift is held (the picker's add-click and
     *  the loc multi-select read it). */
    int shift_held;
    /**
     * Set by the host's key dispatcher each frame it resolves the keys: the
     * EDITOR_HK_CTX_* mask it resolved them in (Editor_HotkeyContextsFrom
     * over the App's facts: the World panel shown, the pointer in its rect
     * and no chrome wanting it). `hotkey_contexts_valid` 0 until a dispatcher
     * has set it (a headless test), when the status bar derives the mask
     * from what the editor knows itself. The status bar's hint reads it, so
     * its rows are the keys' contexts, not a second guess at them.
     */
    unsigned hotkey_contexts;
    int hotkey_contexts_valid;
    /**
     * The last pointer input was a finger (Editor_UiSetTouchInput, from the
     * input's last-event kind each frame): the hint strip then spells the
     * gestures ("2 fingers: pan, pinch: zoom, hold: menu") instead of the
     * mouse words. 0 in a headless test and on a mouse.
     */
    int input_touch;

    /** Set while a received fact is applied, so a module's own latching does
     *  not publish back what the server just said. */
    int applying_state;

    /** The pending action the save guard is asking about. */
    struct EditorUiGuard guard;
    /** Serial of the last TORIRSMAPED_STATE_OPEN_SQUARE this window asked. */
    uint32_t open_square_serial;

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
    /** The last status sentence (Editor_UiStatus), when it was said on the
     *  chrome's clock, and a serial that moves on every sentence. */
    char status[EDITOR_UI_CONSOLE_LINE_MAX];
    int64_t status_ms;
    uint32_t status_serial;

    /** The default layout ran for the current surface and scale
     *  (Editor_UiTick, the first frame the surface and the panels have
     *  sizes); cleared by a surface or scale change and by Reset layout. */
    int laid_out;
    /** The chrome panels the registered modules created, [panel_first,
     *  panel_end), and the chrome scale their pixel sizes were given in, in
     *  quarter units (Editor_ChromeScaleQ): a later scale change (the
     *  display's density arrives after boot, View > Chrome scale) scales
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
    /** The saved layout says this panel was maximised: maximised once the
     *  default layout (and a same-surface restore) has placed its box. */
    unsigned char maximise_pending[EDITOR_UI_LAYOUT_PANELS_MAX];
    int booted_layout;

    /** The World panel's chrome panel (module "world"), -1 before it is
     *  registered. Its body is the 3D view: Editor_UiWorldRect. */
    int world_panel;
    /** Registry panels (by registry index) popped out into windows of their
     *  own (Editor_UiPanelPopOut): hidden here until the View menu shows
     *  them again, which is "pop in". */
    unsigned char popped[EDITOR_UI_PANELS_MAX];
    /** This window shows ONE registry panel filling its surface -- it is a
     *  popped-out window (Editor_UiSetSolo) -- and this is its chrome panel;
     *  -1 in an ordinary window. */
    int solo_panel;
    /** STATE_TOOLS (the brush, palette, flags, placement and view level the
     *  Client's windows share): what this window last published or applied
     *  -- a tick whose tools differ publishes them -- and the publishes
     *  whose echo has not come back yet, oldest first, so this window's own
     *  echo never undoes a newer change. `tools_known` is 0 until the first
     *  tick took its baseline (a window that boots never overwrites the
     *  Client's tools with its defaults). */
    int32_t tools_shared[EDITOR_UI_TOOLS_VALUES];
    int tools_known;
    int32_t tools_echo[32][EDITOR_UI_TOOLS_VALUES];
    int tools_echo_count;
    /** A docked panel the user just showed again: the next layout shows it
     *  even when the column has no room (it then flies out beside it). -1
     *  for none. */
    int layout_reopened;
    /** Editor_UiLayout runs so far: a panel tells a relayout's hiding from
     *  the user's close by it (edpanel_preview.c). */
    unsigned layout_runs;

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

    /**
     * The hotkey table (editor_hotkeys.h): the live bindings every hint is
     * labelled from (menu rows, tool tips, the Level buttons, the Hotkeys
     * panel). `hotkeys.serial` moves on every rebind; a module re-labels when
     * it does. The App glue (src/app/app_editor_hotkeys.c) reads the keys and
     * resolves them against this table.
     */
    struct Editor_Hotkeys hotkeys;
    /** The tool armed before the current one (Previous tool swaps them). */
    enum Editor_Tool previous_tool;
    /** View > Hide / show panels: 1 while hidden, and which registry
     *  panels were open when they were hidden (restored by the next press). */
    int panels_hidden;
    unsigned char panels_hidden_open[EDITOR_UI_PANELS_MAX];

    /** The panel groups (Editor_UiRegisterGroup), and per registry panel the
     *  group it is in now, plus one (0: not in a group). */
    struct EditorUiGroup groups[EDITOR_UI_GROUPS_MAX];
    int group_count;
    unsigned char group_plus1[EDITOR_UI_PANELS_MAX];

    /**
     * View > Chrome scale and View > Text size (workbench 6.0b): this
     * device's choices (editor_prefs.h), never shared with the Client.
     * `prefs_serial` moves on every change the user makes, so the host
     * writes editor_prefs.ini when it differs from what it last wrote; a
     * change applied from the file (Editor_UiApplyPrefs) does not move it.
     * `scale_auto_q` is what the host's Auto resolved to on the last frame
     * (Editor_UiChromeScaleFor), so the Auto row can name it.
     */
    struct Editor_Prefs prefs;
    uint32_t prefs_serial;
    int scale_auto_q;
    /** The host said this is a touch device on the last frame
     *  (Editor_UiChromeScaleFor): View rows under 2x are held at 2x, and the
     *  menu says so on them. */
    int scale_touch;
    /** The theme's own row face, kept while Large wears the Menu face, so
     *  Normal gives it back. */
    int text_face_normal;

    /**
     * The Dock button (design #workbench "Every panel pops out"): this
     * popped-out window's Dock was pressed and the leader was told
     * (TORIRSMAPED_STATE_DOCK_PANEL); the host closes the window. And the
     * serial of the last dock this window asked for.
     */
    int dock_exit;
    uint32_t dock_serial;
};

/* ---- chrome scale and text size (workbench 6.0b) ---------------------------
 *
 * The chrome scale is in QUARTER units (ui/torirs_chrome_scale_q.h: 4 = 1x,
 * 6 = 1.5x). The chrome model takes it exactly, through
 * ToriRSChrome_SetScaleQ; ToriRSChrome_Scale is then the integer tier at or
 * above it (the baked bitmaps the scale is drawn down from). Every editor
 * metric goes through Editor_UiPx (edp_px), so the editor's layout rounds
 * once, where the chrome rounds its own.
 */
#define EDITOR_UI_SCALEQ_ONE 4
/** View > Text size Large: rows of this many chrome pixels at 1x. */
#define EDITOR_UI_LARGE_ROW_H 22

/** Set `chrome`'s scale to quarter value `q` (TORIRS_CHROME_SCALEQ_MIN..MAX,
 *  asserted); 1 when it moved. */
int
Editor_ChromeSetScaleQ(
    struct ToriRSChrome* chrome,
    int q);

/** `chrome`'s scale in quarter units (ToriRSChrome_ScaleQ). */
int
Editor_ChromeScaleQ(const struct ToriRSChrome* chrome);

/** The integer scale App_SetChromeScale is handed beside quarter value `q`:
 *  the one ToriRSChrome_Scale then answers (the tier at or above it), so the integer and
 *  the quarter setter never undo each other. */
int
Editor_ChromeScaleInteger(int q);

/** A 1x-authored editor metric in this window's device pixels, rounded once
 *  (ToriRSChrome_Px). */
int
Editor_UiPx(
    const struct EditorUi* ui,
    int px);

/** The chrome scale this window wants when the host's Auto is `auto_q`: the
 *  View > Chrome scale choice, or `auto_q` for Auto (remembered for the Auto
 *  row's label). On a `touch` device a choice under 2x is held at 2x
 *  (TORIRS_CHROME_SCALEQ_TOUCH_MIN: "a touch device never goes under 2x");
 *  the host's Auto already is. */
int
Editor_UiChromeScaleFor(
    struct EditorUi* ui,
    int auto_q,
    int touch);

/** View > Chrome scale: EDITOR_PREFS_SCALE_AUTO or a step
 *  (ToriRSChromeScaleQ_StepIndex >= 0, asserted). The host applies the scale
 *  on its next frame; the layout re-runs when it lands. Moves prefs_serial. */
void
Editor_UiSetChromeScalePref(
    struct EditorUi* ui,
    int q);

/** View > Text size: 0 Normal (the Body face, 18 px rows), 1 Large (the
 *  Menu face as the body face, EDITOR_UI_LARGE_ROW_H rows), through the
 *  chrome's theme (`font_row`, `row_h`); the layout re-runs. Moves
 *  prefs_serial. */
void
Editor_UiSetTextLarge(
    struct EditorUi* ui,
    int large);

/** View > Text rendering: 0 bitmap at whole scales, 1 smooth always (the
 *  synthesized vector face at 100, 200 and 300 % too). Device-local, beside
 *  the chrome scale; nothing reflows (the faces measure alike). The host hands
 *  it to the chrome models on its next frame (Editor_UiTextSmooth). Moves
 *  prefs_serial. */
void
Editor_UiSetTextSmooth(
    struct EditorUi* ui,
    int smooth);

/** The text-rendering choice the host hands the chrome: 0 or 1. */
int
Editor_UiTextSmooth(const struct EditorUi* ui);

/** Apply what editor_prefs.ini says (the host's load), without moving
 *  prefs_serial: nothing to write back. */
void
Editor_UiApplyPrefs(
    struct EditorUi* ui,
    const struct Editor_Prefs* prefs);

/* ---- the Dock button (design #workbench "Every panel pops out") ------------ */

/**
 * Dock this popped-out window back into its leader: tell the Client
 * (TORIRSMAPED_STATE_DOCK_PANEL: the module's registry index, a serial, the
 * panel's scroll and its first list's first row) so the window that shows the
 * panel set opens the panel again exactly as View > <panel> does, scrolled
 * where this window left it, and set `dock_exit` for the host to close this window. Asserts
 * the window is solo. Returns 1 when the leader was told; 0 (with a console
 * line, the window left open) when there is no session to tell it through.
 * The title-bar Dock button lands here.
 */
int
Editor_UiDockBack(struct EditorUi* ui);

/** Whether a window popped out to show module `module` alone holds a world:
 *  "world" and "preview" draw one; every other panel loads none
 *  (app_editor_view.c skips the world load around its camera). */
int
Editor_UiModuleNeedsWorld(const char* module);

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
 * Register every tools-panels module (menu bar, status bar, tool rail,
 * terrain, palette, rectangle select, locs, spawns, inspector, console, save
 * guard, square picker, New square). The App-bound modules (catalog,
 * inspector) need
 * `ui->app`; with it NULL they are skipped. They reach the App through
 * `ui->app_services` only, filled before this is called.
 *
 * Then the default panel groups, each holding whichever of its modules this
 * window registered: "right-top" (Catalog, Locs, Spawns, Inspector),
 * "right-mid" (Terrain, Palette, Catalog palette) and "bottom" (Console,
 * History). Every other module is a window of its own, as before.
 *
 * integrate-3 adds the other owners' Register calls beside this one.
 */
void
Editor_UiRegisterToolsPanels(struct EditorUi* ui);

/* ---- panel groups (the model is above, at struct EditorUiGroup) ----------- */

/**
 * Add an empty group `name` whose frame is titled `title`, hidden until a
 * member opens. Called while Editor_UiRegisterToolsPanels runs, or straight
 * after it before any other chrome panel is added (the frame must be the next
 * window of the registry's). Asserts a new name and a free slot. Returns its
 * index.
 */
int
Editor_UiRegisterGroup(
    struct EditorUi* ui,
    const char* name,
    const char* title);

/**
 * Put registry module `module` into group `group`, at the end of its strip;
 * the first group a module joins is its default (Reset groups). Open in the
 * strip when its window is shown. Asserts both exist, the module is a titled
 * WINDOW of this registry, it is in no group yet, and the group has room.
 */
void
Editor_UiJoinGroup(
    struct EditorUi* ui,
    const char* module,
    const char* group);

/**
 * Take `module` out of its group: a window of its own again, with its title
 * bar back, open when it was open in the strip, and laid out as a standalone
 * panel (the design's "drag a tab out of its strip"). Lasts until Reset groups
 * or the next launch. Asserts it is in a group.
 */
void
Editor_UiLeaveGroup(
    struct EditorUi* ui,
    const char* module);

/** View > Reset groups: every module back in its default group, in its
 *  default place in the strip, and the layout run again. */
void
Editor_UiResetGroups(struct EditorUi* ui);

/** The group named `name`, or NULL. */
struct EditorUiGroup*
Editor_UiFindGroup(
    struct EditorUi* ui,
    const char* name);

/** Index of the group registry module `module` is in, or -1 (also for a name
 *  that is no module). */
int
Editor_UiGroupOf(
    const struct EditorUi* ui,
    const char* module);

/** 1 while registry module `module` is open in its group's strip, shown or
 *  behind another tab; 0 when it is not, or is in no group. */
int
Editor_UiPanelInStrip(
    const struct EditorUi* ui,
    const char* module);

/** The registry module whose tab group `group` shows, or NULL (none open). */
const char*
Editor_UiGroupShown(
    const struct EditorUi* ui,
    int group);

/** Bring `module`'s tab to the front of its group (what a click on the tab
 *  does), opening it in the strip if it was not. Asserts it is in a group. */
void
Editor_UiGroupShow(
    struct EditorUi* ui,
    const char* module);

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

struct LibToriRS_Gesture;

/**
 * Offer one gesture frame (input/torirs_gesture.h) to the chrome the editor
 * draws on (ToriRSChrome_Gesture: a GRAPH pans and zooms, a MODELVIEW
 * orbits and zooms, a list scrolls by pixels), whenever any of its panels
 * is up. Returns 1 when the chrome took it; 0 leaves it to the World.
 * The events it queues are the modules' on the next Editor_UiTick.
 */
int
Editor_UiGesture(
    struct EditorUi* ui,
    const struct LibToriRS_Gesture* frame);

/** The touch flag: whether the last pointer input was a finger (@see
 *  EditorUi::input_touch). The host sets it each frame from its input. */
void
Editor_UiSetTouchInput(
    struct EditorUi* ui,
    int touch);

/**
 * The default layout, for the surface the chrome has now: the menu bar
 * across the top, the tool rail at the left edge, one column of panels docked
 * to the right edge (Terrain, Palette, Locs, Spawns, Rectangle, Inspector,
 * Squares, Catalog, in that order, each given at least a few rows and the
 * rest of the column shared top-down), the status bar across the foot (the
 * rest stops above it), the output console as a strip along
 * the bottom of the world when it is short enough, and the World panel
 * docked over the rectangle that is left -- the largest on the surface (its
 * body is Editor_UiWorldRect). Panels of the default
 * set that do not fit the column are hidden (the View menu opens them, beside
 * the column). Panels the user dragged or resized keep their place.
 * A panel group takes ONE place: in the column where its first member in that
 * order would go, or as the bottom strip when it holds the console; its
 * height is its strip's plus its largest member's (the members all have the
 * group's box, the shown one showing). A group with no room is hidden with
 * its tabs kept, as a panel with no room is.
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
 *  View-menu panel's visibility, placement flag and geometry (at scale 1);
 *  every group with members as a [group] section, and each member's [panel]
 *  as only whether it is in its strip. */
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

/**
 * Pop registry module `name` out of this window: ask the App for a window of
 * this session that shows that panel ALONE, filling it ("world" = the 3D
 * view, looking exactly where the World panel looks: same pose, the pinned
 * level), and -- once it is asked for -- hide the panel here so the layout's
 * space is free. View > <panel> shows it here again (pop in); the other
 * window keeps running until it is closed. One console line each way; a
 * refusal says why in the console and leaves the panel where it was.
 * The title-bar Pop out button and the hotkey both land here. Returns 1 when
 * the window was asked for.
 */
int
Editor_UiPanelPopOut(
    struct EditorUi* ui,
    const char* name);

/** Editor_UiPanelPopOut(ui, "world"). */
int
Editor_UiWorldPopOut(struct EditorUi* ui);

/** 1 while registry module `name` is popped out of this window (and not
 *  shown here again since). */
int
Editor_UiPanelPoppedOut(
    const struct EditorUi* ui,
    const char* name);

/**
 * The registry module under surface point (x, y) -- the topmost visible
 * titled panel whose box holds it -- or NULL. What a "pop out the panel
 * under the pointer" hotkey passes to Editor_UiPanelPopOut.
 */
const char*
Editor_UiPanelAt(
    struct EditorUi* ui,
    int x,
    int y);

/**
 * Make this window a popped-out window of registry module `name`: from the
 * next layout every other registry panel (the menu bar, the rail and World
 * included) is hidden and `name`'s panel fills the surface, with no title-bar
 * Close / Pop out (the OS window's close ends it). 0 when there is no such
 * titled module.
 */
int
Editor_UiSetSolo(
    struct EditorUi* ui,
    const char* name);

/** Leave the solo layout (a popped-out World's "Open panels"): every panel
 *  back, laid out as the main window's are. Asserts the window was solo. */
void
Editor_UiClearSolo(struct EditorUi* ui);

/** Show registry module `name`'s panel against the side of module `anchor`'s
 *  (left of it, top-aligned; right of it when the left has no room), as a
 *  flyout the column leaves be -- a panel that belongs with another opening
 *  beside it. With `anchor` hidden or absent it is a plain show (the layout
 *  puts it beside the column). Asserts `name` is a module with a panel. */
void
Editor_UiPanelOpenBeside(
    struct EditorUi* ui,
    const char* name,
    const char* anchor);

/** Show or hide the chrome panel of registry module `name` (the View menu's
 *  toggle). Returns 1 when it is open afterwards, -1 when there is no such
 *  module in this window. */
/**
 * Open a square (`add` 1 keeps what is open) in the window that shows a
 * world: this one when its World panel is shown, else every window of the
 * Client that shows one, through TORIRSMAPED_STATE_OPEN_SQUARE -- a popped-out
 * Squares panel has a world of its own that nobody sees, and an Open there
 * used to land in it (owner, 2026-11-02). `storage_index` is the picker's
 * index of `storage` (-1 when unknown: the content tree's maps/).
 */
void
Editor_UiOpenSquare(
    struct EditorUi* ui,
    int map_x,
    int map_z,
    const char* storage,
    int storage_index,
    int add);

int
Editor_UiTogglePanel(
    struct EditorUi* ui,
    const char* name);

/** View > Hide / show panels: hide every View-menu panel but World and
 *  remember which were open; the next call opens those again. Returns 1
 *  when the panels are hidden afterwards. */
int
Editor_UiToggleAllPanels(struct EditorUi* ui);

/** Arm a tool: tools state, STATE_TOOL, console. The tool it replaces
 *  becomes `previous_tool`. */
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
 * Say one sentence in the status bar (design #rules R8, "nothing happens
 * without a word": a refused action says why, "Place refused: tile 3222,3218
 * holds a wall of that shape; press E to swap"). The console keeps it -- it
 * is logged exactly as Editor_UiLog logs -- and the status bar shows it for
 * EDITOR_UI_STATUS_MS of the chrome's frame clock, until a newer one.
 */
void
Editor_UiStatus(
    struct EditorUi* ui,
    const char* format,
    ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

/** The status sentence the bar shows now: the last one, or "" once it is
 *  EDITOR_UI_STATUS_MS old on the chrome's clock (or none was said). */
const char*
Editor_UiStatusText(const struct EditorUi* ui);

/**
 * The status bar (module "statusbar", src/editor/panels/edpanel_statusbar.c;
 * design #workbench "Status bar and hint strip"): the strip along the foot of
 * the window, always on while the editor shows. Left, the armed tool's live
 * chords (Editor_HotkeysHint); middle, the hover readout; right, the status
 * sentence and the server's write state. Editor_UiLayout reserves its band:
 * the world, the dock column and the console stop above it.
 */
void
Editor_StatusBar_Register(struct EditorUi* ui);

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
