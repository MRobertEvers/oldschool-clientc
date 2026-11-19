#ifndef SRC_RENDER_WORLD_VIEWS_H
#define SRC_RENDER_WORLD_VIEWS_H

/*
 * The world views: the cameras looking at the one shared world, which of them
 * the frame being built draws through, and the rectangle of the screen the
 * world is drawn into.
 *
 * There is always one view, views[0], the runner's: a script's verbs read and
 * write it, and headless or with no script attached it is the only one. While
 * a script runs in a client that presents, the split (struct App_ViewSplit)
 * makes a second, views[1], the watcher's, and the presented frame draws that
 * one instead. `frame` is the view the stage being run reads and writes. It is
 * moved where a stage begins -- the presented draw, the emit walk, the
 * offscreen runner frame, the camera follow step -- and put back on views[0]
 * when the stage ends, so every other moment sees the runner's view.
 *
 * `emit_desc` is the WORLD node's emit desc: the gate rect and the exact
 * viewport the render pass draws with, handed to render, pick and the plugin
 * drive. It is shared by every view; `emit_valid` says whether the last emit
 * walk found a WORLD node at all.
 *
 * This does not know the UI tree, the world, a script or a renderer. The
 * caller owns the runner view's menu storage and passes it in, decides when a
 * script attaches and detaches, moves `frame` at the start and end of each
 * stage, and fills the emit desc from its own walk of the tree.
 */

#include "input/torirs_input.h"
#include "render/world_camera_orbit.h"
#include "toridraw_types.h"
#include "ui/uitree_emit.h"
#include "ui/uitree_minimenu.h"
#include "world/world_pickset.h"

#include <stdint.h>

struct PaintersBuffer;
struct ToriRS_CmdBus;

/** The most world views there are: a script's and a watcher's. */
#define APP_WORLD_VIEW_MAX 2

/**
 * One WORLD VIEW: everything a camera looking at the shared world owns, and
 * nothing it shares.
 *
 * The world, the character, the UI tree and its layout, and the game's
 * scripted camera (`cam_script`) are SHARED and are not here, and the
 * viewport rectangle and emit desc are shared by every view (struct
 * WorldViews `emit_desc`). A view holds the camera pose (the follow
 * orbit, the eye it derives, the zoom, the reload hold, the projection scale
 * inside `world_camera`), the camera's input latches, the pointer over the
 * world, the pick results the last drawn frame left at that pointer, and the
 * right-click menu.
 *
 * Headless, and with no script attached, there is exactly ONE:
 * WorldViews_Init points `frame` at `views[0]` and sets `split.view_count`
 * to 1.
 * While a script runs in a client that presents there are two (struct
 * App_ViewSplit: views[0] the AutomationRunner's, views[1] the watcher's
 * PlayerClient view). Every reader and writer reaches these fields through a
 * view pointer -- struct WorldViews `frame`,
 * the view of the frame being built -- so a second view (a script's camera
 * beside the watcher's; docs/minigames/raid_loop/CAMERA_VIEW_TABLE.md names
 * which view each site will take) is a choice made where a stage begins,
 * never a global. A NULL view is a contract violation.
 */
struct App_WorldView
{
    struct ToriDraw_Camera world_camera;
    struct ToriDraw_Position world_camera_pos;
    /* Reference orbit camera (Client-TS followCamera): velocity-driven
     * yaw/pitch, a 1/16-eased anchor that trails the player, and a terrain
     * pitch clamp (cameraPitchClamp, 24.8 fixed) that keeps the eye above
     * nearby ground. cam_key_* latch arrow-held state for the follow step,
     * which runs before key sampling in the frame.
     *
     * orbit_x/orbit_z are FLOAT because the reference's are (rev-239
     * client.field917/field879, eased in client.method1605). An integer
     * `anchor += (target - anchor) / 16` never converges: once the gap drops
     * to 15 the truncated step is 0, so the anchor parks a permanent ~15
     * units short on each axis and the camera orbits a point beside the
     * player instead of the player. */
    /** The follow camera's anchor, angles and terrain clamp. See
     *  render/world_camera_orbit.h. */
    struct WorldCameraOrbit orbit;
    int cam_key_left;
    int cam_key_right;
    int cam_key_up;
    int cam_key_down;
    /* Middle-button rotate (revconfig `[camera] controls=mmb`): the press
     * latches inside the viewport rect and keeps the pointer until release, so
     * a drag that wanders over the sidebar keeps rotating. cam_mmb_x/y is the
     * pointer position the last delta was measured from. */
    int cam_mmb_active;
    int cam_mmb_x;
    int cam_mmb_y;
    /*
     * The follow camera's live ZOOM: the eye sits `pitch * 3 + this` behind
     * the player, in fine units, so this is the additive half of zoom. It
     * starts at `[camera] rest=` (the reference's 600, Client-TS camFollow)
     * and smaller is closer.
     *
     * The wheel and the pinch move it inside `[camera] zoom_closest=` ..
     * `zoom_furthest=`, in `wheel_step=` notches; the settings page's
     * REVCONFIG_CAMERA_WHEEL_PINNED stops them, which is how a player asks for
     * the 2004 camera that does not zoom. The MULTIPLICATIVE half is
     * `[camera] distance_scale=`, which this field knows nothing about. The
     * free camera dollies along the view axis instead and ignores all of it.
     */
    int world_cam_zoom;
    /** U toggles: 1 = the follow camera stands down and W/A/S/D + R/F fly
     *  world_camera_pos freely; relocking eases back onto the player (the
     *  follow's own >500-unit teleport snap handles the return). */
    int camera_unlocked;
    /** Camera hold across an offline world reload. See
     *  render/world_camera_orbit.h. */
    struct WorldCameraHold cam_hold;

    /* World picking: the full pickset refreshes as part of every rendered
     * frame (App_Render hittests visible models at world_mouse_x/y); click
     * handlers consume the last rendered set. */
    struct World_PickSet world_pickset;
    int world_mouse_in_viewport;
    int world_mouse_x; /* last input mouse, canvas coords */
    int world_mouse_y;
    /** No pointer is resting at world_mouse_x/y: the finger that was the
     *  pointer lifted (LibToriRS_Input_PushMouseLeave). The position is kept,
     *  because a popup the tap opened is anchored to it, but everything hover
     *  means -- the tile under the pointer, the world pick, the mouseover line
     *  -- goes quiet until something points at the canvas again. */
    int pointer_absent;
    int world_hover_tile_x; /* scene tile, -1 = none */
    int world_hover_tile_z;
    int world_hover_tile_level;
    /** The hovered DECK tile (world-entity terrain under the pointer), or
     *  view 0 when the pointer is over no deck. Deck-LOCAL tiles at the deck
     *  MESH level. Kept beside — never instead of — the root hover latch
     *  above: the click cross and spawn hotkeys speak root tiles; the
     *  boat-aware consumers (the tile-indicator plugins' hover marker)
     *  prefer this one when set. */
    int world_hover_view;
    int world_hover_view_x;
    int world_hover_view_z;
    int world_hover_view_level;

    /**
     * The right-click menu this view's clicks open and choose from.
     *
     * The STORAGE is the caller's, not this view's: the UI layer's
     * interaction step (ui/uitree_interact.c) opens, steers and closes the
     * menu inside struct UIInteraction, which this seam does not reach into.
     * The app layer reads and writes it only through this pointer, so a
     * second view's menu is a second pointer, set where that view is made.
     */
    struct UIMinimenu* minimenu;
};

/** The view a script's verbs read and write: `views[0]`, always (headless and
 *  with no script attached it is also the only view). */
#define APP_VIEW_RUNNER 0
/** The watcher's view while a script runs in a client that presents:
 *  `views[1]`, live only between App_ViewAttachPlayerClient and
 *  App_ViewDetachPlayerClient. */
#define APP_VIEW_PLAYER_CLIENT 1

/** Physical commands that wait for the runner's gesture to end (see
 *  App_ViewSplit.held). A press, its release and a few keys. */
#define APP_VIEW_SPLIT_HELD_MAX 16
#define APP_VIEW_SPLIT_HELD_PAYLOAD 16
/** Watcher actions kept for the driver to read (`watcher.<what>` rows). */
#define APP_VIEW_SPLIT_WATCHER_MAX 32

struct App_ViewSplitHeld
{
    uint32_t type;
    uint16_t length;
    uint8_t payload[APP_VIEW_SPLIT_HELD_PAYLOAD];
};

struct App_ViewSplitWatcherAction
{
    /** Monotonic serial, from 1; the driver reads past the last one it saw. */
    uint32_t serial;
    /** The world clock's tick when the action reached the game. */
    int tick;
    /** "click", "right_click", "key", "wheel". A fixed string. */
    char const* what;
    int x;
    int y;
    int detail; /* button, key code or wheel notches */
};

/**
 * THE RUNNER CAMERA SPLIT (docs/minigames/raid_loop/CAMERA_TRIAGE_seam2.md,
 * runner_view_split). While a script runs in a client that presents, there
 * are two world views: `views[APP_VIEW_RUNNER]` (AutomationRunner: every
 * driver verb, its pointer, its picks, its menu, its photographs) and
 * `views[APP_VIEW_PLAYER_CLIENT]` (PlayerClient: what is presented, what the
 * physical mouse and keys steer). Everything here is zero and untouched while
 * `attached` is 0 -- a headless run and a client with no script attached pay
 * one predicted branch per stage boundary and nothing else.
 */
struct App_ViewSplit
{
    /** How many world views are live: 1, or 2 while a script's view is
     *  attached (WorldViews::views). */
    int view_count;
    /** views[1] is live. */
    int attached;
    /** The watcher's client presents frames: set by the host (main.c) once a
     *  window that draws exists and render skip is off; attach refuses
     *  without it ("nothing is presented, so there is no watcher"). */
    int presentable;
    /** Why the presented lane cannot carry a second view, or NULL. Set by
     *  the host for a lane this split is not built or tested on. */
    char const* lane_refusal;
    /** THE INTERACT SWITCH (owner decision 1). 0 when a script starts: the
     *  physical mouse and keys are a spectator's (orbit, zoom, hover-inspect,
     *  the plugin chrome); 1: they act on the game through the PlayerClient
     *  view. */
    int interact;

    /** The runner's pointer (the last position a runner event carried) and
     *  whether one has arrived since attach. */
    int runner_x;
    int runner_y;
    int runner_pointer_valid;
    /** Runner buttons held (bit per LibToriRS_MouseButton): a gesture in
     *  flight that physical presses must not interleave with. */
    int runner_buttons_held;
    /** The runner pushed an input event this frame. */
    int runner_events_this_frame;
    /** The physical pointer and who moved last (hover follows it). */
    int physical_x;
    int physical_y;
    int physical_moved_last;
    /** This frame's game input carries a physical gesture: the input stage
     *  runs through the PlayerClient view and its menu. */
    int physical_frame;

    /** Every physical event, always: the PlayerClient camera's keys, middle
     *  button and wheel, and its pointer. */
    struct LibToriRS_Input physical_input;
    /** The driver's own bus while attached (PluginDriveCore_CmdBus answers
     *  it): its events are the runner's. Allocated at attach. */
    struct ToriRS_CmdBus* runner_bus;
    /** Physical commands waiting for the runner's gesture to end. */
    struct App_ViewSplitHeld held[APP_VIEW_SPLIT_HELD_MAX];
    int held_count;

    /** The PlayerClient view's menu storage (views[0]'s is interact.minimenu;
     *  App_ViewSplitBindMenu swaps the two while a physical gesture is
     *  handled so the UI interaction step drives the right one). */
    struct UIMinimenu player_client_menu;
    int menu_bound_to_player_client;

    /** THE OFFSCREEN RUNNER FRAME (app_render.c). */
    int runner_fresh;
    int runner_catch_up_ok;
    int runner_draw_owed;
    int offscreen; /* an offscreen frame is being drawn */
    int* runner_pixels;
    int runner_pixels_count;
    struct PaintersBuffer* runner_painter_buffer;

    /** Watcher actions while Interact is on (ring; the driver reads them). */
    struct App_ViewSplitWatcherAction watcher[APP_VIEW_SPLIT_WATCHER_MAX];
    uint32_t watcher_serial;

    /** Counters (the status read; the cost proof). */
    uint64_t offscreen_frames;
    uint64_t offscreen_frames_for_reads;
    uint64_t offscreen_frames_for_shots;
    uint64_t presented_frames;
    uint64_t physical_delivered;
    uint64_t physical_dropped;
    uint64_t physical_held;
    uint64_t attaches;
};

struct WorldViews
{
    /** views[0] always exists; `split.view_count` is how many are live. */
    struct App_WorldView views[APP_WORLD_VIEW_MAX];
    /** The view the frame being built reads and writes (never NULL once
     *  WorldViews_Init has run). */
    struct App_WorldView* frame;
    /** The runner/watcher split; all zero while no script is attached. */
    struct App_ViewSplit split;
    /** The WORLD node's emit desc, shared by every view, and whether the last
     *  emit walk found one. */
    struct UITreeEmitDesc emit_desc;
    int emit_valid;
};

/**
 * Make the one view there is at start: `views[0]`, `split.view_count` 1,
 * `frame` pointing at it, and its menu pointer at `runner_menu`. Touches
 * nothing else.
 */
void
WorldViews_Init(
    struct WorldViews* wv,
    struct UIMinimenu* runner_menu);

/** The view a script's verbs act through (views[0], always). */
struct App_WorldView*
WorldViews_Runner(struct WorldViews* wv);

/** The view the presented frame draws: the PlayerClient view while a script
 *  is attached, otherwise the only one. */
struct App_WorldView*
WorldViews_Presented(struct WorldViews* wv);

#endif
