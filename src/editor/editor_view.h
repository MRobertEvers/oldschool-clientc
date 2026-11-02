#ifndef SRC_EDITOR_EDITOR_VIEW_H
#define SRC_EDITOR_EDITOR_VIEW_H

/**
 * One VIEW of the edited world (docs/MAPED_DESIGN.html #windows): one World
 * through one camera, with its own level cap, layer filters and toolbar.
 *
 * A spawned window is a viewer PROCESS -- the same client binary booted with
 * the editor manifest and
 *
 *     --editor-view --maped tcp://host:port --maped-group <id>
 *     --camera x,z,level,yaw,pitch,zoom
 *
 * (or the web's ?editor_view=1&maped=wss://...&group=<id>) -- that opens no
 * game connection, joins the main window's Client as ROLE_VIEWER, builds the
 * EDIT world from the text the server sends, and shows the thin toolbar this
 * file builds instead of the command panels: level cap, layer toggles, follow
 * selection, camera presets, broadcast/follow camera, "Open panels".
 *
 * WHAT IS PER VIEW (here, never on the wire unless published): the camera
 * pose, the level cap and solo, the layer filters. WHAT IS SHARED (the
 * Client's state store): the selection, which "follow selection" frames; and
 * a view's camera, but only after it opts in to "broadcast my camera" --
 * then it is published under STATE_CAMERA_BASE + its connection index
 * (torirsmaped/torirs_maped_view.h), and any other view may follow that key.
 * A view learns its connection index by asking the server (STATE_VIEW_SELF).
 *
 * TWO HALVES. The core (EditorView_*) holds the per-view state, talks to the
 * session through Editor_StateSet and Editor's state facts, and builds the
 * toolbar on a ToriRSChrome -- no App, no renderer, so a test drives three of
 * them against one embedded server. The APP GLUE (EditorView_App*, in
 * src/app/app_editor_view.c, declared in app/app_internal.h) is what a viewer
 * process calls: it moves the App's free camera to the view's pose and reads
 * it back, feeds the level mask to the painter, and spawns new views (launch
 * service natively, a tab on the web).
 *
 * The camera pose is an ORBIT pose: the point looked at (absolute fine
 * units, 128 per tile), its level, yaw, pitch and the distance to the eye.
 * Two processes whose scenes are based at different tiles agree on it, which
 * an eye position in scene coordinates would not.
 */

#include "editor/editor.h"
#include "torirsmaped/torirs_maped_view.h"

#include <stdint.h>

struct ToriRSChrome;

/** An orbit camera pose; the TORIRSMAPED_CAMERA_* layout, named. */
struct EditorViewCamera
{
    /** Absolute fine units (tile * 128) of the point looked at. */
    int32_t x;
    int32_t z;
    /** The plane that point stands on, 0..3. */
    int32_t level;
    /** Client angle units, 0..2047. */
    int32_t yaw;
    int32_t pitch;
    /** Eye distance from the point, fine units. */
    int32_t zoom;
};

void
EditorView_CameraToValues(
    const struct EditorViewCamera* camera,
    int32_t* out_values);

void
EditorView_CameraFromValues(
    const int32_t* values,
    struct EditorViewCamera* out_camera);

/** A view's layer filters (bits). TERRAIN..OBJS are what the world draws;
 *  COLLISION, TILE_GRID and ZONE_GRID are overlays. */
enum EditorViewLayer
{
    EDITOR_VIEW_LAYER_TERRAIN = 1u << 0,
    EDITOR_VIEW_LAYER_LOCS = 1u << 1,
    EDITOR_VIEW_LAYER_NPCS = 1u << 2,
    EDITOR_VIEW_LAYER_OBJS = 1u << 3,
    EDITOR_VIEW_LAYER_COLLISION = 1u << 4,
    EDITOR_VIEW_LAYER_TILE_GRID = 1u << 5,
    EDITOR_VIEW_LAYER_ZONE_GRID = 1u << 6,
    EDITOR_VIEW_LAYER_COUNT = 7,
    EDITOR_VIEW_LAYER_DEFAULT = EDITOR_VIEW_LAYER_TERRAIN | EDITOR_VIEW_LAYER_LOCS
                                | EDITOR_VIEW_LAYER_NPCS | EDITOR_VIEW_LAYER_OBJS,
};

/** The toolbar's camera presets. */
enum EditorViewPreset
{
    /** Straight-ish down from far, yaw kept: a plan view. */
    EDITOR_VIEW_PRESET_TOP = 0,
    /** 45 degrees round, 45 down: the classic three-quarter view. */
    EDITOR_VIEW_PRESET_ISO,
    /** Yaw 0 (north up), pitch and distance kept. */
    EDITOR_VIEW_PRESET_NORTH,
    /** Low and close: what a player standing there sees. */
    EDITOR_VIEW_PRESET_GROUND,
    EDITOR_VIEW_PRESET_COUNT,
};

/** What a toolbar activation asks the HOST to do (everything else the view
 *  does itself). */
enum EditorViewAction
{
    /** The widget was not the toolbar's. */
    EDITOR_VIEW_ACTION_NONE = 0,
    /** Handled inside the view (a toggle, a preset, a follow). */
    EDITOR_VIEW_ACTION_HANDLED,
    /** "Open panels": promote this window to a full client of the Client --
     *  the host shows the command panels beside the view. */
    EDITOR_VIEW_ACTION_OPEN_PANELS,
};

enum
{
    /** Camera publishes at most this often while it moves (ms). */
    EDITOR_VIEW_PUBLISH_MS = 33,
    /** Rows of the follow menu: one per other connection. */
    EDITOR_VIEW_PEER_MAX = TORIRSMAPED_STATE_CAMERA_COUNT,
};

/** The toolbar's chrome handles; -1 until built. */
struct EditorViewToolbar
{
    int built;
    int panel;
    int bt_level;
    int bt_layers;
    int bt_camera;
    int bt_follow_selection;
    int bt_open_panels;
    /** The three popup menus the buttons open. */
    int menu_level;
    int menu_layers;
    int menu_camera;
    int level_items[5];
    int item_solo;
    int layer_items[EDITOR_VIEW_LAYER_COUNT];
    int preset_items[EDITOR_VIEW_PRESET_COUNT];
    int item_broadcast;
    int item_unfollow;
    int follow_items[EDITOR_VIEW_PEER_MAX];
};

struct EditorView
{
    /** The session this view mirrors through. Borrowed. */
    struct Editor* editor;
    /** This connection's index on the server (STATE_VIEW_SELF), -1 until the
     *  answer arrives; its camera key is ToriRSMapEd_CameraKey(self_index). */
    int self_index;
    uint32_t group;

    /** The view's pose. */
    struct EditorViewCamera camera;
    int camera_valid;
    /** The pose changed from INSIDE the view (follow, preset, selection) and
     *  the host has not applied it yet: EditorView_TakeCamera. */
    int camera_dirty;

    /** -1 = every level; else the cap (or the only level, with solo). */
    int level_cap;
    int level_solo;
    /** EDITOR_VIEW_LAYER_* bits shown. */
    uint32_t layers;
    /** Frame the Client's selection when it changes. */
    int follow_selection;
    /** STATE facts still to come from the boot's STATE_SYNC replay
     *  (EditorView_ExpectSync). While nonzero, a STATE_EDIT_LEVEL is adopted
     *  as the level cap: a new view starts at the level its Client has
     *  pinned (a popped-out World looks at the main window's pinned level),
     *  then keeps its own cap. */
    int sync_pending;

    /** Publish this view's camera under its key. */
    int broadcast;
    /** The connection index whose camera this view follows, -1 = none. */
    int follow_index;

    /** Publish bookkeeping. */
    struct EditorViewCamera published;
    int published_valid;
    int64_t published_ms;

    /** What the Client's other views are broadcasting. */
    struct
    {
        int live;
        struct EditorViewCamera camera;
    } peers[EDITOR_VIEW_PEER_MAX];

    struct EditorViewToolbar toolbar;
};

/* ---- the core ------------------------------------------------------------ */

/** A view over `editor`'s session: every level, the default layers, no
 *  broadcast, no follow, no pose yet. */
void
EditorView_Init(
    struct EditorView* view,
    struct Editor* editor);

/** Ask the server which connection this is (STATE_VIEW_SELF). The answer
 *  arrives as a state fact; until then the view cannot broadcast. Returns 1
 *  when sent. */
int
EditorView_AskSelf(struct EditorView* view);

/**
 * A shared-state fact from this connection's Client -- hand every one here
 * (the host's Editor state callback). Returns 1 when the key was the view's
 * alone (VIEW_SELF, a camera key) and nobody else need see it; 0 for keys
 * other subscribers want too (the selection, which follow selection reads).
 */
int
EditorView_OnState(
    struct EditorView* view,
    uint32_t key,
    const int32_t* values,
    int count);

/** The host asked the server to replay this Client's state (STATE_SYNC) and
 *  `count` facts are coming: the level the Client has pinned among them
 *  becomes this view's cap. */
void
EditorView_ExpectSync(
    struct EditorView* view,
    int count);

/** The host's camera moved (input, flight): record the pose. Marks it for
 *  publishing when broadcasting. */
void
EditorView_SetCamera(
    struct EditorView* view,
    const struct EditorViewCamera* camera);

/** 1 and `out` when the view changed its own pose (follow, preset,
 *  selection) since the last call; the host applies it to its camera. */
int
EditorView_TakeCamera(
    struct EditorView* view,
    struct EditorViewCamera* out);

/** Start or stop broadcasting this view's camera. Stopping publishes the
 *  key with no values, so followers let go. Returns 0 when the view does
 *  not know its connection index yet (it remembers the wish and publishes
 *  once the answer comes). */
int
EditorView_SetBroadcast(
    struct EditorView* view,
    int broadcast);

/** Follow connection `index`'s camera (-1 stops). Adopts its pose at once
 *  when it is broadcasting. Following yourself is refused (returns 0). */
int
EditorView_Follow(
    struct EditorView* view,
    int index);

void
EditorView_ApplyPreset(
    struct EditorView* view,
    enum EditorViewPreset preset);

/** Per frame: publish the camera when it changed and EDITOR_VIEW_PUBLISH_MS
 *  passed since the last publish. Returns 1 when it published. */
int
EditorView_Tick(
    struct EditorView* view,
    int64_t now_ms);

/** The level mask the painter should use: 0 = every level (the game's own
 *  rule), else the cap's levels -- cumulative 0..cap, or the cap alone with
 *  solo, which is the reading VIS_BELOW tiles come out right under. */
uint8_t
EditorView_LevelMask(const struct EditorView* view);

void
EditorView_SetLevelCap(
    struct EditorView* view,
    int level_cap,
    int solo);

int
EditorView_LayerShown(
    const struct EditorView* view,
    enum EditorViewLayer layer);

void
EditorView_SetLayer(
    struct EditorView* view,
    enum EditorViewLayer layer,
    int shown);

/* ---- pose <-> eye ---------------------------------------------------------- */

struct ToriDraw_Position;

/** The eye a pose puts a camera at, in a scene based at (base_x, base_z)
 *  absolute fine units, with the looked-at point's ground at `ground_y`
 *  (y grows down). */
void
EditorView_PoseEye(
    const struct EditorViewCamera* pose,
    int base_x,
    int base_z,
    int ground_y,
    struct ToriDraw_Position* out_eye);

/** The pose of a free camera: the point its view ray meets the ground under
 *  the eye (`ground_y`), at the distance that makes it so -- or at
 *  `fallback_zoom` when the eye is not above the ground or looks level. */
void
EditorView_PoseFromEye(
    const struct ToriDraw_Position* eye,
    int yaw,
    int pitch,
    int ground_y,
    int base_x,
    int base_z,
    int level,
    int fallback_zoom,
    struct EditorViewCamera* out);

/* ---- the thin toolbar ------------------------------------------------------ */

/** Build the toolbar (a TORIRS_CHROME_PANEL_TOOLBAR strip at the top of the
 *  surface, five buttons, three popup menus) on `ui`. Once per view. */
void
EditorView_ToolbarBuild(
    struct EditorView* view,
    struct ToriRSChrome* ui);

/** Bring the toolbar's checks and labels up to the view's state (call after
 *  state changes the view did not make through the toolbar). */
void
EditorView_ToolbarRefresh(
    struct EditorView* view,
    struct ToriRSChrome* ui);

/** The frame's activated widget (ToriRSChrome_TakeActivated): act on it when
 *  it is the toolbar's. */
enum EditorViewAction
EditorView_ToolbarActivate(
    struct EditorView* view,
    struct ToriRSChrome* ui,
    int widget);

/** The launch service's `view` body (platform/launch_sessions.h): the
 *  `maped=`, `group=` and `camera=` lines, and `panel=` when `panel` names a
 *  module other than "world" (a popped-out panel window), into `out`.
 *  `panel` NULL = the 3D view. Returns its length, or -1 when it does not
 *  fit. */
int
EditorView_SpawnBody(
    const char* maped_url,
    uint32_t group,
    const struct EditorViewCamera* camera,
    const char* panel,
    char* out,
    int out_size);

/* ---- the boot pose (one per process) ------------------------------------ */

/** The pose the command line (`--camera`) or the manifest (`camera=`) asked
 *  this process's view to start at; main.c sets it before App_Init. NULL
 *  clears it. */
void
EditorView_SetBootCamera(const struct EditorViewCamera* camera);

/** 1 and `out` when a boot pose was set. */
int
EditorView_BootCamera(struct EditorViewCamera* out);

/** The editor panel module (EditorUiPanel::name) the command line
 *  (`--editor-panel`) asked this viewer process to show ALONE, filling the
 *  window -- a popped-out panel. "world" or NULL = the 3D view (today's
 *  viewer). main.c sets it before App_Init; the name is copied. */
void
EditorView_SetBootPanel(const char* module);

/** The boot panel, or NULL for the 3D view. Never "world". */
const char*
EditorView_BootPanel(void);


#endif
