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
#include "editor/editor_recent.h"
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
    /** Height brushes: TILE commands on the `h` token, grouped per stroke.
     *  HEIGHT is raise and lower in one: it pulls the ground toward the
     *  target height its grid shows (EditorUiTools.height_target), which
     *  Ctrl/Cmd+wheel moves, the ground under the brush following. */
    EDITOR_TOOL_HEIGHT,
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
    /** Drag a path: the palette's overlay along the drag, its edges shaped to
     *  the line (editor/editor_path.h), one undo step on release. */
    EDITOR_TOOL_PAINT_PATH,
    /** Roughen: a seeded relief under the brush -- hills a chosen number of
     *  tiles across with finer bumps on them, not a draw per tile -- the
     *  step its amplitude, the falloff fading it to the rim (ground that is
     *  not a plane, and reads as ground). */
    EDITOR_TOOL_NOISE,
    /** Scatter: locs from a set (the selection's ids, else the catalog's
     *  pick) dropped on the brush's free tiles at a density, each turned at
     *  random; Alt-drag takes the set's locs off (edpanel_ops.h
     *  Editor_ScatterCommands). */
    EDITOR_TOOL_SCATTER,
    /** Plateau: the brush's disc flattened to the height of the tile the
     *  stroke began on, and a skirt of `plateau_skirt` tiles around it eased
     *  from that height down (or up) to the ground as it is -- a mound or a
     *  hole with a flat top for a building (edpanel_ops.h
     *  Editor_PlateauCommands; the Rect panel's Plateau does a rectangle). */
    EDITOR_TOOL_PLATEAU,
    /** Region brush: a disc swept along the drag in continuous tile space,
     *  the palette's overlay laid with its edge fitted to the client's tile
     *  shapes (editor/editor_region.h); Alt-drag takes it off. On release,
     *  one undo step (editor/editor_panel_region.h). */
    EDITOR_TOOL_REGION,
    /** Lasso: a drawn loop (or clicked corners) filled the same way. */
    EDITOR_TOOL_LASSO,
    /** Tidy: a click refits the run of overlay under it, its steps smoothed. */
    EDITOR_TOOL_TIDY,
    /** Building: a dragged rectangle becomes a house -- walls and corners on
     *  its ring, a floor and the "remove roof" flag in the room, a roof a
     *  level up -- a dragged line an inside wall, a click on a wall piece a
     *  door or a window (editor/editor_building.h plans them;
     *  panels/edpanel_building.c holds the styles). One undo step each. */
    EDITOR_TOOL_BUILDING,
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

/** Which way the Height brush may move a tile: toward its target either
 *  way, or only up to it (fill a hollow without cutting the rise beside
 *  it), or only down to it (cut a rise without filling the hollow). */
enum Editor_HeightMode
{
    EDITOR_HEIGHT_MODE_BOTH = 0,
    EDITOR_HEIGHT_MODE_RAISE,
    EDITOR_HEIGHT_MODE_LOWER,
    EDITOR_HEIGHT_MODE_COUNT
};

/** World units in one authored height unit (the `h` byte's step). */
#define EDITOR_HEIGHT_UNIT 8
/** The highest the Height brush's grid goes: four planes of the tallest
 *  `h` (255 units each), as a derived height (negative up). */
#define EDITOR_HEIGHT_TARGET_MIN (-4 * 255 * EDITOR_HEIGHT_UNIT)

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
    /** Height: authored height units (8 world units each) one wheel notch
     *  moves the target; Roughen: its amplitude. */
    int step;
    /** Height: 0..100, how far toward the target one click or drag
     *  application goes (100 lands on it); Smooth: how far toward the
     *  neighbours' mean. */
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
    /** Paint underlay's mix, 1..100 %: under 100 the brush paints that
     *  share of its tiles (scaled by the falloff, drawn from noise_seed,
     *  moved on per stroke), a dither the client's five-tile blend turns
     *  into a gradient between the underlay there and the one painted. */
    int paint_mix;
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

    /** The Height brush's target, a derived height (world units, negative
     *  up, as Editor_OpsDerivedHeight answers), where its grid stands. 0
     *  valid until a press, a Ctrl/Cmd+click pick, a wheel notch or the Terrain
     *  panel's slider sets it: the grid then stands on the hovered ground. */
    int height_valid;
    int height_target;
    /** enum Editor_HeightMode. */
    int height_mode;
    /** The grid: drawn at all, and its fill's opacity in percent (0..100;
     *  the lines are always drawn while it shows). */
    int height_grid;
    int height_grid_opacity;

    /** The Place tool's pose: what the ghost shows and the click stamps. */
    int place_shape;
    int place_rotation;
    /** Fit walls to neighbours (the G toggle): the Place pose of a wall pick
     *  joins the walls of its own loc around the tile (Editor_LocFitWall).
     *  Shared like the pose (design R7: the Client shares Fit), so every
     *  window's ghost, Locs panel and Catalog pane agree. 1 at boot. */
    int place_fit;

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

    /** The Path tool (editor/editor_path.h): dirt, pavement or river, the
     *  width in half tiles (1..16), dirt's and a river's roughness in
     *  percent, and the seed their wander is drawn from (New edges draws
     *  another). */
    int path_style;
    int path_width_halves;
    int path_roughness;
    uint32_t path_seed;
    /** What the path does to the ground under it (edpanel_ops.h enum
     *  Editor_PathHeightMode: leave, set to, lower by, raise by) and the
     *  amount in EDITOR_HEIGHT_UNITs (for "set to", the height above zero
     *  as the Height brush's grid counts it; Ctrl/Cmd+click under the Path
     *  tool picks it off the ground). */
    int path_height_mode;
    int path_height_units;

    /** The Roughen brush: the seed its relief is drawn from, moved on at
     *  every stroke so the same drag twice does not undo itself; the
     *  relief's feature size in tiles (1..EDITOR_ROUGHEN_SCALE_MAX, 1 a
     *  draw per tile) and how much finer bumps ride on its hills, 0..100 %
     *  (edpanel_ops.h Editor_RoughenRelief). */
    uint32_t noise_seed;
    int noise_scale;
    int noise_detail;
    /** The Scatter brush (edpanel_ops.h Editor_ScatterCommands): the share
     *  of the brush's tiles that take a loc (1..100 %, scaled by the
     *  falloff), whether each is turned at random, and the seed the choices
     *  are drawn from (New seed draws another; each stroke moves it on). */
    int scatter_density;
    int scatter_rotate;
    /** 1: Scatter puts locs on bare ground only (Editor_ScatterOp.bare). */
    int scatter_bare;
    uint32_t scatter_seed;
    /** The Plateau tool and the Rect panel's Plateau: the skirt's width in
     *  tiles (0..EDITOR_PLATEAU_SKIRT_MAX) eased from the flat top to the
     *  ground around it. */
    int plateau_skirt;
    /** The Region tools (editor/editor_panel_region.h): the brush's radius
     *  in half tiles (1..EDITOR_REGION_RADIUS_HALVES_MAX), hardness
     *  (EDITOR_REGION_HARDNESS_MIN..MAX %: how much a shape may spill past
     *  the region against how much of it it may leave bare), half-tile
     *  snap of the brush's points and the lasso's corners, the shape set
     *  (enum Editor_RegionSet), the target (0 the palette's overlay, 1 its
     *  underlay with the rim dithered), the lasso's corner smoothing in
     *  passes (0..3), Tidy's smoothing in half tiles (0..4), the underlay
     *  rim's feather in tiles (0..3) and the dither's seed. Per window, as
     *  the path's settings are. */
    int region_radius_halves;
    int region_hardness;
    int region_snap;
    /** 1: every Region brush stroke is one straight run at 0/45/90 degrees
     *  (Shift at the press does the same for one stroke). */
    int region_straight;
    int region_set;
    int region_underlay;
    int region_lasso_smooth;
    int region_tidy_halves;
    int region_feather;
    uint32_t region_seed;
};

/** The widest Region brush, in half tiles (8 tiles across the radius). */
#define EDITOR_REGION_RADIUS_HALVES_MAX 16

/** The widest plateau skirt, in tiles. */
#define EDITOR_PLATEAU_SKIRT_MAX 8

/* ---- hooks ---------------------------------------------------------------
 *
 * What the panels ask of the window around them. Any may be NULL: the panel
 * then says the action is unavailable in the console rather than doing half
 * of it.
 */
/** The most squares an Open squares sweep takes: 3x3. */
#define EDITOR_UI_OPEN_SET_MAX 9

/** One square of a working set: where, and its storage ("" = maps/). */
struct EditorUiSquareRef
{
    int map_x;
    int map_z;
    char storage[EDITOR_DOC_STORAGE_MAX];
};

struct EditorUiHooks
{
    /** Open square (map_x, map_z) from `storage` ("" = maps/) in this window:
     *  `add` 0 replaces what the window shows, 1 adds it to the open set. */
    void (*open_square)(void* user, int map_x, int map_z, const char* storage, int add);
    /** Open `count` squares (1..EDITOR_UI_OPEN_SET_MAX) as this window's
     *  whole working set, in place of what it shows: the Square map's Open
     *  squares. */
    void (*open_square_set)(void* user, const struct EditorUiSquareRef* squares, int count);
    /** Close square (map_x, map_z) of `storage` in this window: the world
     *  reloads with the rest of what it shows (workbench 6.4, the picker's
     *  open-set strip). The save guard has already asked. */
    void (*close_square)(void* user, int map_x, int map_z, const char* storage);
    /** Open the square in a NEW view window (design #windows). */
    void (*open_in_new_view)(void* user, int map_x, int map_z);
    /** A floor config's colour (0xRRGGBB): `is_overlay` 0 underlay, 1 overlay.
     *  0 when the record is not resident. */
    int (*floor_rgb)(void* user, int is_overlay, int id, uint32_t* out_rgb);
    /** Ask for floor config `id`'s record to be loaded (the window loads
     *  them one id at a time, as its squares need them); floor_rgb answers
     *  once it is resident. An id past the records is a quiet miss. */
    void (*floor_request)(void* user, int is_overlay, int id);
    /** Put floor config `id` in the window's own table as the tree states
     *  it, in place of whatever the window held: a record made in the tree
     *  is in no cache until the next build, and without this its swatch is a
     *  hatched cell and the ground painted with it a hole. `rgb` is the
     *  record's `colour`; an overlay also carries its `texture` (-1 none),
     *  `blendcolour` (-1 none) and `hideunderlay`. `rebake` 1 when ground
     *  the window shows may wear the record, so the world is baked again
     *  when the record is new to the window or changed. NULL in a window
     *  with no world (a controller, a headless harness). */
    void (*floor_define)(
        void* user, int is_overlay, int id, uint32_t rgb, int texture, int blend_rgb, int hide_underlay, int rebake);
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
struct Editor_CamEye;

/** The area id worldmap_area answers for "*": every area at once, on the
 *  true square grid (the picker's Full map). */
#define EDITOR_UI_MAP_AREA_ALL 0x10000
/** Scene sprite slots the picker's map may hold at once (image_publish). */
#define EDITOR_UI_MAP_IMAGE_SLOTS 80
/** Screenshots the quest board shows at once (png_publish). */
#define EDITOR_UI_SHOT_SLOTS 4
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
    /** A 16 x 16 top-down picture of loc `loc_id` at `shape` (workbench
     *  6.2, the Catalog's row thumbnails), rastered through the Preview's
     *  pipeline and registered as a scene sprite: its id, or 0 while it is
     *  still to come (the config or a model loading: asked for, ask again
     *  next frame). Cached by (id, shape); the Catalog asks for the rows on
     *  screen only. NULL member: the rows draw the empty cell. */
    int (*loc_thumbnail)(void* user_data, int loc_id, int shape);
    /** A record's picture at `px` square (the Gallery's tiles, the
     *  Catalog's npc rows): a loc's or npc's model rastered with the
     *  Preview's camera, an obj's inventory icon; a scene sprite id, 0
     *  while its assets load, -1 for a record with no picture. */
    int (*record_thumb)(void* user_data, uint32_t kind, int id, int px);
    /** Obj `obj_id`'s inventory icon at stack `count` (the game's 32 x 32:
     *  model, outline, the count variant, a note or a placeholder drawn as
     *  the game draws them) as a scene sprite: its id, 0 while a config or a
     *  model is still loading (asked for: ask again next frame), -1 when the
     *  obj has nothing to draw. `out_name` (NULL: none wanted) gets the
     *  obj's name once its config is resident, "" before. NULL member: the
     *  wells draw the slot's id instead (a headless harness, the web). */
    int (*obj_icon)(void* user_data, int obj_id, int count, char* out_name, int name_size);
    /** Cache sprite archive `archive`, frame 0, as a scene sprite: its id, 0
     *  while it loads (asked for), -1 when the cache has none. The Accounts
     *  panel's worn slots draw the game's own silhouettes (156..166) on its
     *  slot plate (170). NULL member: the wells draw plain boxes. */
    int (*cache_sprite)(void* user_data, int archive);
    /** Overlay `id`'s (a `.jm2` id: config id + 1) ground texture as a
     *  24 x 24 scene sprite: its id, 0 while the overlay's record or the
     *  texture is still loading (asked for: ask again next frame, and an id
     *  past the records answers 0 for ever), -1 when the overlay lays no
     *  texture. `out_texture` gets the texture's id, -1 with none. The
     *  Palette's Textures well is these. NULL member: the well stays empty
     *  (a headless harness, the web). */
    int (*overlay_texture)(void* user_data, int id, int* out_texture);
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
    /** Put the World panel's camera at `camera` (the orbit pose world_camera
     *  reads: the point looked at, its level, yaw, pitch, distance), as a
     *  scene's Enter does (workbench 6.5). 0 when the point is not in the
     *  loaded scene yet (the caller opens its squares and asks again once
     *  the world is not busy). NULL member: no camera to put. */
    int (*world_camera_set)(void* user_data, const struct EditorViewCamera* camera);
    /** The World panel's camera EXACTLY as it stands (the Camera path panel,
     *  editor/editor_campath.h): the eye in ABSOLUTE fine units with the
     *  scene's y (down), its yaw and pitch -- not an orbit pose, which is
     *  re-derived from the ground and comes back a few units off on a
     *  slope. 0 when the window has no world. NULL member: no camera. */
    int (*world_eye)(void* user_data, struct Editor_CamEye* out);
    /** Put it exactly there, angles and all (a level or climbing view has no
     *  orbit point to state). 0 when the eye is outside the loaded scene.
     *  NULL member: no camera to put. */
    int (*world_eye_set)(void* user_data, const struct Editor_CamEye* eye);
    /** The ground's y (the scene's, down) under ABSOLUTE fine (x, z) on
     *  `level`, as the world draws it: what cam_moveto and cam_lookat
     *  measure their heights from. 0 outside the loaded scene (nothing
     *  written). NULL member: no world. */
    int (*world_ground)(void* user_data, int fine_x, int fine_z, int level, int* out_y);
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
    /** The quest board's screenshots (workbench 6.7): decode the PNG at
     *  `path` (absolute) into shot slot `slot` (0 ..
     *  EDITOR_UI_SHOT_SLOTS - 1), box-filtered down to fit `max_w` x
     *  `max_h` (never up), and return its scene sprite id with the size it
     *  was drawn at; 0 when it cannot (no such file, not a PNG the client's
     *  decoder reads, no scene). NULL member (a headless harness, the web):
     *  the board shows the shot's path. */
    int (*png_publish)(
        void* user_data,
        int slot,
        const char* path,
        int max_w,
        int max_h,
        int* out_w,
        int* out_h);
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
    /** A monotonic clock, microseconds: what the picker's map times its
     *  background loads by (EDITOR_WORLDMAP_PREFETCH_US a frame). NULL
     *  member: one slice a tick, untimed. */
    uint64_t (*now_us)(void* user_data);
    /** The last frame's work, microseconds (App_LastFrameUs): what the
     *  picker's map reports its cold open against. NULL member or 0: not
     *  measured. */
    uint64_t (*last_frame_us)(void* user_data);

    /* The NPC editor's minimap and route legs (docs/NPC_EDITOR_DESIGN.md
     * 3.4): the loaded scene's collision, as the client built it. */
    /** The collision flags of ABSOLUTE tile (x, z) on `level`
     *  (engine/world_builder/collision_map.h COLL_FLAG_*), 1 when the tile
     *  is in the loaded scene; 0 when it is not (nothing written). NULL
     *  member: no scene. */
    int (*tile_flags)(void* user_data, int world_x, int world_z, int level, int* out_flags);
    /** Steps of the shortest walk a 1x1 mover takes from ABSOLUTE (ax, az)
     *  to (bx, bz) on `level` through the loaded scene's collision
     *  (collision_map_bfs_path): 0 or more; -1 when no path exists; -2
     *  when either tile is outside the loaded scene. NULL member: -2. */
    int (*path_steps)(void* user_data, int ax, int az, int bx, int bz, int level);
    /** One step of a `size` npc on `level` from ABSOLUTE (x, z) by (dx, dz),
     *  each -1..1, through the loaded scene's collision as the server's npc
     *  walk tests it (collision_map_can_travel_typed, normal movement): 1
     *  open, 0 blocked, -1 when either tile is outside the loaded scene.
     *  NULL member: -1. The NPC editor walks a patrol's legs with it. */
    int (*can_step)(void* user_data, int x, int z, int dx, int dz, int level, int size);
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
    /** The strip as the first layout found it (which members were open, which
     *  was in front, -1 none): the Build workspace's default gives it back. */
    unsigned char boot_open[EDITOR_UI_GROUP_MEMBERS_MAX];
    int boot_shown;
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
    /** Close one square of this window's open set (the picker's strip,
     *  Shift+click): the world reloads without it. */
    EDITOR_UI_GUARD_CLOSE_SQUARE,
    /** Enter a scene (workbench 6.5, edpanel_scenes.c): its squares replace
     *  the open set, so unsaved edits are asked about first. */
    EDITOR_UI_GUARD_ENTER_SCENE,
    /** Open a working set (the Square map's Open squares) in place of the
     *  current set. */
    EDITOR_UI_GUARD_OPEN_SET,
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
    /** ENTER_SCENE: the scene's name (editor_scenes.h's EDITOR_SCENE_NAME_MAX). */
    char scene[64];
    /** OPEN_SET: the squares. */
    int set_count;
    struct EditorUiSquareRef set[EDITOR_UI_OPEN_SET_MAX];
};

/**
 * The editor lane this window's server serves (torirsmaped/
 * torirs_maped_lanes.h), as the Lanes module last heard it (workbench 6.5):
 * FACT_LANE_INFO, then a FACT_LANE_COUNT every EDITOR_UI_LANE_COUNT_MS and
 * after every save, publish and discard (the server answers it from its
 * count cache: no walk of the mirror unless a write came since). The status
 * bar's lane chip reads it
 * ("working | 3 changed", Editor_Lanes_ChipText); the Scenes panel saves its
 * name into a scene and compares a scene's lane with it.
 */
#define EDITOR_UI_LANE_NAME_MAX 48
#define EDITOR_UI_LANE_COUNT_MS 15000

/** A lane's content root (TORIRSMAPED_LANE_PATH). */
#define EDITOR_UI_LANE_CONTENT_MAX 1024

struct EditorUiLane
{
    /** FACT_LANE_INFO (or a FACT_LANE_COUNT) heard. */
    int known;
    char name[EDITOR_UI_LANE_NAME_MAX];
    int is_main;
    int writable;
    /** The change count against the base: modified + added + deleted. */
    int changes_known;
    int changes;
    /** The lane build/lanes/lane.ini names for the next boot ("" none):
     *  a Switch this server could not make while it runs. */
    char next_boot[EDITOR_UI_LANE_NAME_MAX];
    /** lane.ini names THIS lane: every launch opens it, whatever the
     *  manifest asks (a Switch or Scratch lane made earlier). */
    int pinned;
    /** The lane the boot asked for before lane.ini had its say (the
     *  manifest's lane= / --lane; "" from a server that does not say). */
    char asked[EDITOR_UI_LANE_NAME_MAX];
    /** The content tree the lane's server serves, as it spells it
     *  (FACT_LANE_INFO): build/lanes/<name>/content, or the tree itself on
     *  main. Where a file the panels show, open or reveal is on disk -- the
     *  manifest's content_dir is the BASE of a lane, never the lane. */
    char content[EDITOR_UI_LANE_CONTENT_MAX];
    /** Moves whenever any field above does. */
    uint32_t serial;
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
#define EDITOR_UI_TOOLS_VALUES 16

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
    /** The lane, for every module (the Lanes module fills it). */
    struct EditorUiLane lane;
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
    /** The tool rail's own padding (its box less its rows of buttons), as
     *  measured while it was its content's height, and the chrome scale it
     *  was measured at: the layout gives the rail the column's whole height,
     *  after which its box no longer says what the padding is. -1: not
     *  measured yet. */
    int rail_pad;
    int rail_pad_scale;
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

    /**
     * Compact panels (View > Compact panels, Editor_UiSetCompact): the dock
     * column's members are every default panel, each either expanded or
     * COLLAPSED to its title bar (ToriRSChrome_PanelSetCollapsed), so a
     * small surface still shows the whole set and the world keeps its share.
     * `compact_mode` is the setting (-1 automatic: on below
     * EDP_COMPACT_H / EDP_COMPACT_W logical pixels; 0 off; 1 on) and
     * `compact` what the last layout did with it. Per member: `expanded` is
     * the user's wish (a title click toggles it; all 1 at boot), and
     * `expand_serial` when it was last asked for -- when the column cannot
     * hold every wished-for panel at its minimum, the least recently asked
     * for are collapsed first.
     */
    int compact_mode;
    int compact;
    unsigned char expanded[EDITOR_UI_LAYOUT_PANELS_MAX];
    unsigned expand_serial[EDITOR_UI_LAYOUT_PANELS_MAX];
    unsigned expand_clock;
    /** The OS window's box in window points, as the host last told it
     *  (Editor_UiSetWindowBox); written to the layout file so the next run
     *  opens the window there. w 0 = unknown. */
    int window_x;
    int window_y;
    int window_w;
    int window_h;

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
    /** The column's splitter (editor_ui.c ui_split_follow): the docked
     *  window whose top or bottom edge the chrome is resizing (-1 none, -2 a
     *  resize that is not a split), which edge (TORIRS_CHROME_EDGE_*), the
     *  docked window on the other side of it (-1 none) and that window's
     *  box when the drag began. */
    int split_root;
    int split_edge;
    int split_other;
    int split_other_y;
    int split_other_h;
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

    /** The squares opened most recently and the camera scheme
     *  (editor_recent.h): File > Recent squares, the command palette, the
     *  camera's mouse. Noted by Editor_UiOpenSquare; the App persists it. */
    struct Editor_Recent recent;
    /** A tile the next Editor_UiOpenSquare should bring the camera to once
     *  the square's load lands (the palette's "3222,3218"), instead of the
     *  square's centre; consumed by the open hook. */
    int open_goto_valid;
    int open_goto_x;
    int open_goto_z;
    int open_goto_level;

    /** The ground tile grid and the open squares' borders (the World's
     *  overlay, app_overlay.c; enum Editor_TileGridMode in
     *  panels/edpanel_common.h): 0 AUTO, on a square that holds no loc yet,
     *  where a flat fill is one green plane with no edge and no north; 1 ON,
     *  everywhere; 2 OFF. Per window, not shared state. */
    int tile_grid;
    /** Npc and ground-item spawns this window has stood in the world since
     *  the Spawns panel last looked (edp_spawns_note_added: a placed spawn,
     *  and the ones a paste, a stamp or a square copy brings). The panel
     *  turns "Show spawns in the world" on for them and says so: a spawn
     *  placed with the preview off was a console line and nothing in the
     *  world. */
    int spawns_added_npc;
    int spawns_added_obj;

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

    /**
     * Workspaces (workbench 6.8, design section 3): the current one
     * (EDITOR_UI_WS_*), each one's layout as it was left -- `has` 0 until it
     * was captured or read from the layout file, when switching to it gives
     * its default instead -- and a serial that moves on every switch (the
     * host saves the layout file when the fingerprint moves).
     */
    int workspace;
    struct EditorLayoutWorkspace workspace_saved[4];
    unsigned char workspace_has[4];
    uint32_t workspace_serial;
};

/* ---- workspaces (workbench 6.8, design section 3 "Workspaces") ------------
 *
 * Four named layouts. A workspace is the panels it shows, their boxes, and
 * which tab of each group is in front; the dock column takes the
 * workspace's own panels first (the default layout's order, per workspace).
 *
 *   Build     the modules' default set (the editor's boot layout): Catalog,
 *             Locs, Spawns, Inspector; Preview; Palette slots; Console,
 *             History
 *   Terrain   Terrain, Palette (Terrain in front); Squares; Inspector;
 *             Console, History
 *   Script    Record, Triggers, Vars; Loc builder, NPC builder; Shell;
 *             Problems, Console, Dialogue preview
 *   Playtest  Live, Accounts, Scenes; Encounter; Live log, Console
 *
 * A panel the table names that this window has not registered (Vars,
 * Problems, Dialogue preview, Scenes, Encounter, Live log in this tree) is
 * skipped, and the switch says which in one console line.
 *
 * Switching captures the workspace being left (Editor_UiLayoutCapture's
 * panels and groups), then gives the target its saved layout when it has
 * one, else its default. Reset layout resets the current workspace to its
 * default. The layout file keeps every workspace's layout (editor_layout.h,
 * [workspace <name>]).
 */
enum Editor_UiWorkspace
{
    EDITOR_UI_WS_BUILD = 0,
    EDITOR_UI_WS_TERRAIN,
    EDITOR_UI_WS_SCRIPT,
    EDITOR_UI_WS_PLAYTEST,
    EDITOR_UI_WORKSPACES
};

/** "build", "terrain", "script", "playtest": the layout file's section. */
const char*
Editor_UiWorkspaceName(int workspace);

/** "Build", "Terrain", ...: the menu bar's chip and the palette's row. */
const char*
Editor_UiWorkspaceTitle(int workspace);

/** The workspace named `name` (its section name), or -1. */
int
Editor_UiWorkspaceFind(const char* name);

/** The registry modules the design's table names for `workspace`, in the
 *  column's order (NULL-terminated; Build's is NULL: the default set), the
 *  ones this tree has not got included -- what a switch skips. */
const char* const*
Editor_UiWorkspacePanels(int workspace);

/**
 * Switch to `workspace` (EDITOR_UI_WS_*): the current one's layout captured
 * first, then the target's saved layout or its default, a console line, and
 * the layout run again. The same workspace again: nothing. Returns the
 * number of the table's panels this window skipped (absent).
 */
int
Editor_UiSetWorkspace(
    struct EditorUi* ui,
    int workspace);

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
 *  the current surface, the default set shown again, drags forgotten. The
 *  default set is the current WORKSPACE's (Build's is the modules' own). */
void
Editor_UiResetLayout(struct EditorUi* ui);

/** View > Compact panels: `mode` -1 automatic, 0 off, 1 on. Lays out again. */
void
Editor_UiSetCompact(
    struct EditorUi* ui,
    int mode);

/** 1 when the dock is compact now (the setting resolved against the surface:
 *  EDP_COMPACT_H / EDP_COMPACT_W at the chrome's scale). */
int
Editor_UiCompact(const struct EditorUi* ui);

/** The OS window's box in window points, for the layout file. */
void
Editor_UiSetWindowBox(
    struct EditorUi* ui,
    int x,
    int y,
    int w,
    int h);

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

/** Open `count` squares (1..EDITOR_UI_OPEN_SET_MAX) together as this
 *  window's working set, in place of what it shows. Only the window that
 *  shows the world can (its open_square_set hook); elsewhere it says so. */
void
Editor_UiOpenSquareSet(
    struct EditorUi* ui,
    const struct EditorUiSquareRef* squares,
    int count);

int
Editor_UiTogglePanel(
    struct EditorUi* ui,
    const char* name);

/** View > Hide / show panels: hide every View-menu panel but World and
 *  remember which were open; the next call opens those again. Returns 1
 *  when the panels are hidden afterwards. */
int
Editor_UiToggleAllPanels(struct EditorUi* ui);

/** Stand the Height brush's grid at derived height `height` (negative up),
 *  clamped to 0..EDITOR_HEIGHT_TARGET_MIN, and say where in the status
 *  line. The wheel, Ctrl/Cmd+click and the Terrain panel's slider all land here. */
void
Editor_UiSetHeightTarget(
    struct EditorUi* ui,
    int height);

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
