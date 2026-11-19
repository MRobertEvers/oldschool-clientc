/*
 * The world camera: key orbit, middle-button orbit, wheel zoom, the cinema
 * camera, and the follow camera.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"
#include "app/app_camera_clamp.h"
#include "render/cam_script.h"
#include "editor/editor_panel.h"
#include "editor/editor_view.h"
#include "editor/panels/edpanel_ops.h"

#include <math.h>

/* Private to this unit, declared up front so definition order is free. */
static void
app_camera_move_forward(
    struct App* app,
    int amount);
static void
app_camera_move_left(
    struct App* app,
    int amount);
static void
app_debug_log_camera(
    struct App* app,
    char const* what,
    int follow_cam);
static void
app_cinema_point(
    struct App* app,
    int local_x,
    int local_z,
    int height,
    int* out_x,
    int* out_y,
    int* out_z);
static void
app_cinema_angles(
    struct App* app,
    int* out_pitch,
    int* out_yaw);
static int
app_world_cam_dist_zoom(struct App* app);
static struct WorldCameraLimits
app_world_camera_limits(struct App const* app);

/* The pitch range while the server has the camera unlocked (CAM_UNLOCK). */
enum
{
    APP_CAM_UNLOCK_PITCH_FLATTEST = 0,
    APP_CAM_UNLOCK_PITCH_STEEPEST = 520,
};

int
app_world_clamp_pitch(
    struct App const* app,
    int pitch)
{
    struct WorldCameraLimits const limits = app_world_camera_limits(app);

    assert(app);
    return WorldCameraLimits_ClampPitch(&limits, pitch);
}

static void
app_camera_move_forward(
    struct App* app,
    int amount)
{
    int direction_x = ToriDraw_Sin(app->world_views.frame->world_camera.yaw);
    int direction_z = ToriDraw_Cos(app->world_views.frame->world_camera.yaw);
    app->world_views.frame->world_camera_pos.x -= (direction_x * amount) >> 16;
    app->world_views.frame->world_camera_pos.z += (direction_z * amount) >> 16;
}

static void
app_camera_move_left(
    struct App* app,
    int amount)
{
    int direction_x = ToriDraw_Cos(app->world_views.frame->world_camera.yaw);
    int direction_z = ToriDraw_Sin(app->world_views.frame->world_camera.yaw);
    app->world_views.frame->world_camera_pos.x += (direction_x * amount) >> 16;
    app->world_views.frame->world_camera_pos.z += (direction_z * amount) >> 16;
}

/* Optional developer camera keys. All bindings come from `[debug:hotkeys]` and
 * are off when omitted. Arrow yaw/pitch remains ordinary game camera input. */
/* The editor's free camera, held over the loaded scene (app_camera_clamp.h).
 * Only with an editor session and a world: the game's own cameras are the
 * server's and the follow step's, and a view with no world has nothing to
 * stay over. The floor is the ground under the clamped tile on the pinned
 * level (level 0 while Auto), half a tile above it. */
static void
app_camera_keep_over_scene(struct App* app)
{
    struct App_WorldView* view;
    int extent;
    int margin;
    int tile_x;
    int tile_z;
    int level;
    int ground;

    assert(app);
    if( !app->editor || !app->world || !app->world_views.frame )
        return;
    /* Only over a scene that is there: before the first load, and while a
     * load is rebuilding it, the world has no extent to hold the camera in
     * (clamping to an empty range threw the eye to minus one tile and the
     * ground lookup read a zero-size world, 2026-11-02). */
    if( app->world->_scene_size <= 0 || WorldLoadState_Inflight(&app->world_load) )
        return;
    view = app->world_views.frame;
    extent = app->world->_scene_size * 128;
    /* A tile INSIDE the edge, not past it: at the edge facing outward the
     * view is the void anyway, and the eye staying over the last tile keeps
     * ground under the pointer and the way back one key away. */
    margin = -128;
    assert(extent + 2 * margin > 0);
    tile_x = app_camera_clamp_axis(view->world_camera_pos.x, 0, extent - 1) / 128;
    tile_z = app_camera_clamp_axis(view->world_camera_pos.z, 0, extent - 1) / 128;
    level = app->editor_shell->editor_panel.ui &&
                    app->editor_shell->editor_panel.ui->tools.edit_level >= 0
                ? app->editor_shell->editor_panel.ui->tools.edit_level
                : 0;
    ground = app_world_height(app, tile_x * 128 + 64, tile_z * 128 + 64, level);
    if( app_camera_clamp_scene(&view->world_camera_pos.x, &view->world_camera_pos.y, &view->world_camera_pos.z,
            extent, margin, ground, 64, -extent) )
        RedrawRequest_Raise(&app->redraw_request);
}

/* ---- the editor's Unity-style scheme -----------------------------------------
 *
 * docs/MAPED_HOTKEYS.md "Camera": with the scheme on (the Hotkeys panel's
 * Camera scheme toggle, remembered in editor_recent.ini) the mouse works as
 * in Unity's scene view -- right-drag looks (yaw / pitch), middle-drag pans
 * the ground plane, the wheel zooms toward the tile under the cursor,
 * Alt+left-drag orbits the selection (else the latched or hovered tile), and
 * F frames the selection (the hotkey table's camera.frame_key, context U).
 * Under the default scheme nothing here runs: the middle button orbits and
 * the wheel dollies as before.
 *
 * The latches are the subsystem's own (one editor window per process, as
 * app_editor_hotkeys.c keeps its): struct App may not grow. A press latches
 * over the world only (the panels, the chrome and an open minimenu keep
 * their pointer); the drag then follows the pointer anywhere until release.
 * Every move ends in app_camera_keep_over_scene: the scheme never leaves the
 * loaded squares.
 */
static struct
{
    int look_active;
    int pan_active;
    int orbit_active;
    /** The pointer the last delta was measured from. */
    int last_x;
    int last_y;
    /** How far the pointer moved since the press (a right CLICK under a
     *  few px keeps the minimenu; past it the menu is the drag's casualty). */
    int travel;
    /** The orbit's pivot: scene fine units and its ground, the eye's
     *  distance from it, and the pose's angles (the camera's, kept here so
     *  the pitch clamp is applied to a running value). */
    int pivot_x;
    int pivot_y;
    int pivot_z;
    int pivot_dist;
    int yaw;
    int pitch;
} g_cam_unity;

/* The grab pan (docs/MAPED_HOTKEYS.md "Pan (Space)"): Space held over the
 * World, then a left-drag, under either scheme -- or Alt+Ctrl+left-drag
 * (Option+Cmd) under the Unity scheme. One latch for both: the press
 * starts it over the world, the drag then keeps the pointer until the left
 * button comes up, whether or not the key is still down. */
static struct
{
    int active;
    int last_x;
    int last_y;
    /** Space has been held since an earlier frame, and whether a grab
     *  started while it was: a tap with no drag under the Place tool says
     *  where Place here went (Space was its chord until 2026-11-07). */
    int space_held;
    int space_dragged;
    /** Under the Unity scheme Alt+Ctrl is held over the world this frame
     *  (app_camera_unity_gate): the cursor shows the pan before a press. */
    int unity_chord_over_world;
} g_cam_grab;

/** The free camera is this frame's: offline, or a cutscene's. The same
 *  split app_world_camera_mouse makes. */
static int
app_camera_grab_free(struct App const* app)
{
    return !(app->net && !CamScript_IsScripted(&app->cam_script));
}

/** The Unity scheme is on and this frame's camera is the editor's free one. */
static int
app_camera_unity_on(struct App const* app)
{
    struct EditorUi const* ui;
    if( !app_editor_hotkeys_owns_camera(app) || !app->world || !app->world_views.frame )
        return 0;
    ui = app->editor_shell->editor_panel.ui;
    return ui->recent.camera_scheme == EDITOR_CAMERA_SCHEME_UNITY;
}

/** The pointer is over the world and nothing else wants it. */
static int
app_camera_unity_over_world(
    struct App* app,
    int x,
    int y)
{
    return !app_chrome_wants_pointer(app, x, y) && app_world_mouse_gate(app, x, y) && WorldLoadState_Active(&app->world_load)
           && app_world_viewport_component_live(app);
}

/** Pitch for a camera that looks around: never past straight down (512),
 *  never under the horizon (64, an eleventh of a turn). */
static int
app_camera_unity_clamp_pitch(int pitch)
{
    return app_camera_clamp_axis(pitch, 64, 512);
}

/** The orbit's pivot: the selection's first loc, else the latched tile,
 *  else the tile under the pointer. 0 with none. */
static int
app_camera_unity_pivot(
    struct App* app,
    int* out_x,
    int* out_y,
    int* out_z)
{
    struct EditorUi const* ui = app->editor_shell->editor_panel.ui;
    int tile_x;
    int tile_z;
    int level = ui->tools.edit_level >= 0 ? ui->tools.edit_level : 0;
    int scene_x;
    int scene_z;

    if( ui->tools.loc_count > 0 )
    {
        Editor_LocRefWorld(&ui->tools.locs[0], &tile_x, &tile_z);
        level = ui->tools.locs[0].level;
    }
    else if( ui->tools.tile_valid )
    {
        tile_x = ui->tools.tile_x;
        tile_z = ui->tools.tile_z;
        level = ui->tools.tile_level;
    }
    else if( Editor_PanelHoverTile(&app->editor_shell->editor_panel, app, &scene_x, &scene_z) )
    {
        tile_x = scene_x + app->world->_base_tile_x;
        tile_z = scene_z + app->world->_base_tile_z;
        level = Editor_PanelEditLevel(&app->editor_shell->editor_panel, app);
    }
    else
        return 0;
    scene_x = (tile_x - app->world->_base_tile_x) * 128 + 64;
    scene_z = (tile_z - app->world->_base_tile_z) * 128 + 64;
    if( scene_x < 0 || scene_z < 0 || scene_x >= app->world->_scene_size * 128
        || scene_z >= app->world->_scene_size * 128 )
        return 0;
    *out_x = scene_x;
    *out_z = scene_z;
    *out_y = app_world_height(app, scene_x, scene_z, level);
    return 1;
}

void
app_camera_unity_gate(
    struct App* app,
    struct LibToriRS_Input* input)
{
    int const mx = input->curr.mouse_x;
    int const my = input->curr.mouse_y;
    int const alt = LibToriRS_Input_IsKeyHeld(input, TORIRSK_ALT);
    int const ctrl = LibToriRS_Input_IsKeyHeld(input, TORIRSK_CTRL);

    assert(app);
    assert(input);
    /* The grab ends with its button under either scheme, and runs before any
     * of the hotkeys' gates, so a text field or a minimenu that opens
     * mid-drag still sees the release; while it runs it owns the pointer. */
    if( g_cam_grab.active && !LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT) )
        g_cam_grab.active = 0;
    if( g_cam_grab.active )
        ToriRS_InputClaimsTakeFrame(&app->input_claims);
    g_cam_grab.unity_chord_over_world = 0;
    if( !app_camera_unity_on(app) )
    {
        g_cam_unity.look_active = 0;
        g_cam_unity.pan_active = 0;
        g_cam_unity.orbit_active = 0;
        return;
    }
    /* Releases first, so a press and a release in one frame cancel. */
    if( g_cam_unity.orbit_active && !LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT) )
        g_cam_unity.orbit_active = 0;
    if( g_cam_unity.look_active && !LibToriRS_Input_IsMouseHeld(input, TORIRSM_RIGHT) )
        g_cam_unity.look_active = 0;
    if( g_cam_unity.pan_active && !LibToriRS_Input_IsMouseHeld(input, TORIRSM_MIDDLE) )
        g_cam_unity.pan_active = 0;

    g_cam_grab.unity_chord_over_world = alt && ctrl && !app->world_views.frame->minimenu->visible
                                        && app_camera_grab_free(app) && app_camera_unity_over_world(app, mx, my);
    /* Alt+Ctrl+left-drag (Option+Cmd: Cmd is reported as Ctrl) is Unity's
     * one-button pan, checked before Alt's orbit so the orbit never sees it. */
    if( !g_cam_grab.active && !g_cam_unity.orbit_active && alt && ctrl && input->curr.mouse_button_down[TORIRSM_LEFT]
        && !app->world_views.frame->minimenu->visible && app_camera_grab_free(app) && app_camera_unity_over_world(app, mx, my) )
    {
        g_cam_grab.active = 1;
        g_cam_grab.last_x = mx;
        g_cam_grab.last_y = my;
    }
    if( !g_cam_grab.active && !g_cam_unity.orbit_active && alt && !ctrl && input->curr.mouse_button_down[TORIRSM_LEFT]
        && !app->world_views.frame->minimenu->visible && app_camera_unity_over_world(app, mx, my) )
    {
        struct App_WorldView const* view = app->world_views.frame;
        int px;
        int py;
        int pz;
        if( app_camera_unity_pivot(app, &px, &py, &pz) )
        {
            long const dx = view->world_camera_pos.x - px;
            long const dy = view->world_camera_pos.y - py;
            long const dz = view->world_camera_pos.z - pz;
            double const d = sqrt((double)(dx * dx + dy * dy + dz * dz));
            g_cam_unity.orbit_active = 1;
            g_cam_unity.pivot_x = px;
            g_cam_unity.pivot_y = py;
            g_cam_unity.pivot_z = pz;
            g_cam_unity.pivot_dist = d < 256 ? 256 : (int)d;
            g_cam_unity.yaw = view->world_camera.yaw & 0x7ff;
            g_cam_unity.pitch = app_camera_unity_clamp_pitch(view->world_camera.pitch);
            g_cam_unity.last_x = mx;
            g_cam_unity.last_y = my;
        }
    }
    if( !g_cam_unity.look_active && input->curr.mouse_button_down[TORIRSM_RIGHT]
        && app_camera_unity_over_world(app, mx, my) )
    {
        g_cam_unity.look_active = 1;
        g_cam_unity.last_x = mx;
        g_cam_unity.last_y = my;
        g_cam_unity.travel = 0;
    }
    if( !g_cam_unity.pan_active && input->curr.mouse_button_down[TORIRSM_MIDDLE]
        && !app->world_views.frame->minimenu->visible && app_camera_unity_over_world(app, mx, my) )
    {
        g_cam_unity.pan_active = 1;
        g_cam_unity.last_x = mx;
        g_cam_unity.last_y = my;
    }
    /* An orbit or pan owns the frame's pointer from its press: the editor's
     * click (which runs after this) must not paint, select or stroke, and the
     * middle press must not start the default scheme's rotate. */
    if( g_cam_unity.orbit_active || g_cam_unity.pan_active || g_cam_grab.active )
        ToriRS_InputClaimsTakeFrame(&app->input_claims);
}

void
app_camera_grab_gate(
    struct App* app,
    struct LibToriRS_Input* input)
{
    int const mx = input->curr.mouse_x;
    int const my = input->curr.mouse_y;
    int const pan = app_editor_hotkeys_active(app, EDITOR_HK_CAM_PAN);

    assert(app);
    assert(input);
    if( pan && !g_cam_grab.space_held )
    {
        g_cam_grab.space_held = 1;
        g_cam_grab.space_dragged = 0;
    }
    else if( !pan && g_cam_grab.space_held )
    {
        struct EditorUi* ui = app->editor_shell->editor_panel.ui;
        g_cam_grab.space_held = 0;
        if( !g_cam_grab.space_dragged && ui->tools.tool == EDITOR_TOOL_LOC_PLACE )
            Editor_UiStatus(ui, "Space pans the camera now (hold it and drag); Return or a click places");
    }
    if( !g_cam_grab.active && pan && input->curr.mouse_button_down[TORIRSM_LEFT]
        && !app->world_views.frame->minimenu->visible && app_camera_grab_free(app) && app_camera_unity_over_world(app, mx, my) )
    {
        g_cam_grab.active = 1;
        g_cam_grab.space_dragged = 1;
        g_cam_grab.last_x = mx;
        g_cam_grab.last_y = my;
    }
    /* The editor's click runs after this: a press that started a grab must
     * not paint, select or stroke. */
    if( g_cam_grab.active )
        ToriRS_InputClaimsTakeFrame(&app->input_claims);
}

int
App_WantsPanCursor(struct App const* app)
{
    assert(app);
    /* The latches and the hover flag are written by the editor's gates,
     * which run only while it owns the camera: off it, nothing pans. */
    if( !app_editor_hotkeys_owns_camera(app) || !app->world || !app->world_views.frame || !app_camera_grab_free(app) )
        return 0;
    if( g_cam_grab.active || g_cam_unity.pan_active )
        return 1;
    /* Held, not yet pressed: the cursor says what a press will do. Space is
     * the hotkeys' answer (wherever it is rebound; over the World only, and
     * nothing while a text field holds the keyboard). */
    return app_editor_hotkeys_active(app, EDITOR_HK_CAM_PAN) || g_cam_grab.unity_chord_over_world;
}

/** Pan the ground plane by a pointer delta: the pointer drags the world with
 *  it, so the eye moves the other way, faster the higher it is (one tile of
 *  ground per ~10 px from 2600 units up). The middle-drag of the Unity
 *  scheme and the grab pan share it. */
static void
app_camera_pan_ground(
    struct App* app,
    int dx,
    int dy)
{
    struct App_WorldView* view = app->world_views.frame;
    int const extent = app->world->_scene_size * 128;
    int const tx = app_camera_clamp_axis(view->world_camera_pos.x, 0, extent - 1);
    int const tz = app_camera_clamp_axis(view->world_camera_pos.z, 0, extent - 1);
    int const level = app->editor_shell->editor_panel.ui->tools.edit_level >= 0
                          ? app->editor_shell->editor_panel.ui->tools.edit_level
                          : 0;
    int const ground = app_world_height(app, tx, tz, level);
    int height = ground - view->world_camera_pos.y;
    int per_px;

    if( height < 200 )
        height = 200;
    per_px = height / 200 + 1;
    /* app_camera_move_left's positive is RIGHT: a drag to the right moves
     * the eye left, so the ground slides right under the pointer. A drag
     * down moves the eye forward, which carries the ground down the screen. */
    app_camera_move_left(app, -dx * per_px);
    app_camera_move_forward(app, dy * per_px);
    view->camera_unlocked = 1;
    RedrawRequest_Raise(&app->redraw_request);
}

/** The drags: one delta each, then the clamp. Runs from
 *  app_world_camera_mouse under the scheme. */
static void
app_camera_unity_mouse(
    struct App* app,
    struct LibToriRS_Input* input,
    struct UIInteractOut const* out)
{
    struct App_WorldView* view = app->world_views.frame;
    int const mx = input->curr.mouse_x;
    int const my = input->curr.mouse_y;
    int const dx = mx - g_cam_unity.last_x;
    int const dy = my - g_cam_unity.last_y;

    if( g_cam_unity.orbit_active )
    {
        struct EditorViewCamera pose;
        if( dx || dy )
        {
            g_cam_unity.yaw = (g_cam_unity.yaw - dx * APP_WORLD_MMB_YAW_PER_PX) & 0x7ff;
            g_cam_unity.pitch = app_camera_unity_clamp_pitch(g_cam_unity.pitch + dy * APP_WORLD_MMB_PITCH_PER_PX);
        }
        memset(&pose, 0, sizeof(pose));
        pose.x = g_cam_unity.pivot_x;
        pose.z = g_cam_unity.pivot_z;
        pose.yaw = g_cam_unity.yaw;
        pose.pitch = g_cam_unity.pitch;
        pose.zoom = g_cam_unity.pivot_dist;
        EditorView_PoseEye(&pose, 0, 0, g_cam_unity.pivot_y, &view->world_camera_pos);
        view->world_camera.yaw = g_cam_unity.yaw;
        view->world_camera.pitch = g_cam_unity.pitch;
        view->camera_unlocked = 1;
        if( dx || dy )
            RedrawRequest_Raise(&app->redraw_request);
    }
    else if( g_cam_unity.look_active && (dx || dy) )
    {
        g_cam_unity.travel += (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        /* A right click stays a click (the minimenu opened on the press);
         * a drag past a few px is a look, and the menu goes. */
        if( g_cam_unity.travel > 4 )
        {
            if( view->minimenu->visible )
                UIMinimenu_Hide(view->minimenu);
            view->world_camera.yaw = ToriDraw_AddAngle(view->world_camera.yaw, -dx * APP_WORLD_MMB_YAW_PER_PX);
            view->world_camera.pitch =
                app_camera_unity_clamp_pitch(view->world_camera.pitch + dy * APP_WORLD_MMB_PITCH_PER_PX);
            view->camera_unlocked = 1;
            RedrawRequest_Raise(&app->redraw_request);
        }
    }
    else if( g_cam_unity.pan_active && (dx || dy) )
        app_camera_pan_ground(app, dx, dy);
    g_cam_unity.last_x = mx;
    g_cam_unity.last_y = my;

    /* The wheel zooms TOWARD the tile under the cursor: the eye slides along
     * the line to that tile's ground point, a sixth of the way per notch in,
     * back out along it per notch out, never closer than two tiles. */
    if( input->curr.mouse_wheel_y != 0 && !out->wheel_consumed && !view->minimenu->visible
        && app_camera_unity_over_world(app, mx, my) )
    {
        int scene_x;
        int scene_z;
        if( Editor_PanelHoverTile(&app->editor_shell->editor_panel, app, &scene_x, &scene_z) )
        {
            int const level = Editor_PanelEditLevel(&app->editor_shell->editor_panel, app);
            int const fx = scene_x * 128 + 64;
            int const fz = scene_z * 128 + 64;
            int const fy = app_world_height(app, fx, fz, level);
            long const vx = fx - view->world_camera_pos.x;
            long const vy = fy - view->world_camera_pos.y;
            long const vz = fz - view->world_camera_pos.z;
            double const dist = sqrt((double)(vx * vx + vy * vy + vz * vz));
            int notches = input->curr.mouse_wheel_y;
            double k = 0.0;
            while( notches > 0 && dist * (1.0 - k) > 256.0 )
            {
                k += (1.0 - k) / 6.0;
                notches--;
            }
            while( notches < 0 )
            {
                k -= (1.0 - k) / 5.0;
                notches++;
            }
            view->world_camera_pos.x += (int)(vx * k);
            view->world_camera_pos.y += (int)(vy * k);
            view->world_camera_pos.z += (int)(vz * k);
            view->camera_unlocked = 1;
            app_debug_log_camera(app, "zoom-to-cursor", 0);
            RedrawRequest_Raise(&app->redraw_request);
        }
    }
    app_camera_keep_over_scene(app);
}

void
app_world_camera_keys(
    struct App* app,
    struct LibToriRS_Input* input,
    struct UIInteractOut const* out)
{
    const int move = APP_CAMERA_MOVEMENT_SPEED;
    const int rotate = APP_CAMERA_ROTATION_SPEED;

    /* TORIRS_KEY_DEBUG: why a world/debug key did nothing. Every gate below
     * silently swallows the whole key set, and "the hotkey is broken" and
     * "the hotkey never ran" look identical from outside. Keyed off the raw
     * key state rather than key_event_count — that counter only fills when a
     * component carries an onKey hook, so it is 0 for exactly the debug keys
     * this is meant to explain. */
    if( torirs_env_key_debug() &&
        (app_debug_key_down(app, input, APP_DEBUG_HOTKEY_PAINT_TOGGLE) ||
         app_debug_key_down(app, input, APP_DEBUG_HOTKEY_PAINT_MORE) ||
         app_debug_key_down(app, input, APP_DEBUG_HOTKEY_PAINT_LESS) ||
         app_debug_key_down(app, input, APP_DEBUG_HOTKEY_PAINT_MORE_100) ||
         app_debug_key_down(app, input, APP_DEBUG_HOTKEY_PAINT_LESS_100)) )
        TORIRS_LOG(
            "camera_keys: paint-cap key seen; world_active=%d view_valid=%d "
            "chat=%d/%d/%d iface_input=%d\n",
            WorldLoadState_Active(&app->world_load),
            app->world_views.emit_valid,
            UIChatLine_InputActive(&app->chat_line),
            app->chat.social_input_open,
            app->chat.dialog_input_open,
            app_iface_text_input_focused(app));

    /* No key_target gating: the reference broadcasts every key to onKey
     * scripts AND moves the camera in the same frame; there is no focused
     * text-input concept to defer to yet. The viewport still has to be on
     * screen — with no world drawn these keys belong to the interface. */
    if( !WorldLoadState_Active(&app->world_load) || !app_world_viewport_component_live(app) )
    {
        /* Do not leave held arrow ownership feeding the follow-camera tick
         * after its viewport became display:none. */
        app->world_views.frame->cam_key_left = 0;
        app->world_views.frame->cam_key_right = 0;
        app->world_views.frame->cam_key_up = 0;
        app->world_views.frame->cam_key_down = 0;
        return;
    }
    /* Suppressed while the catalog's model view holds focus, whose WASD/EF
     * orbit the preview, not the world -- and, outside the map editor, while
     * any chrome widget has focus (the chat line, a modal prompt, a panel's
     * search box), so typing never flies the camera.
     *
     * The map editor decides for itself: its hotkey table is shut by a
     * focused text field and stays open under a focused list, slider or well
     * (app_editor_hotkeys.c), and every editor camera action below reads that
     * table. Returning here on any chrome focus (until 2026-11-09) left the
     * camera dead after a click on a Catalog row until the world was clicked
     * again. */
    if( app_modelview_focused(app) )
        return;
    if( !app_editor_hotkeys_owns_camera(app) && app_text_input_focused(app) )
        return;
    (void)out;

    /* The debug flight reads raw keys, so it keeps the chrome-focus gate. */
    if( !app_text_input_focused(app) )
    {
        if( app_debug_key_held(app, input, APP_DEBUG_HOTKEY_CAMERA_FORWARD) )
            app_camera_move_forward(app, move);
        if( app_debug_key_held(app, input, APP_DEBUG_HOTKEY_CAMERA_BACK) )
            app_camera_move_forward(app, -move);
        if( app_debug_key_held(app, input, APP_DEBUG_HOTKEY_CAMERA_LEFT) )
            app_camera_move_left(app, -move);
        if( app_debug_key_held(app, input, APP_DEBUG_HOTKEY_CAMERA_RIGHT) )
            app_camera_move_left(app, move);
        if( app_debug_key_held(app, input, APP_DEBUG_HOTKEY_CAMERA_UP) )
            app->world_views.frame->world_camera_pos.y -= move;
        if( app_debug_key_held(app, input, APP_DEBUG_HOTKEY_CAMERA_DOWN) )
            app->world_views.frame->world_camera_pos.y += move;
    }
    /* The map editor's own flight (bare W/A/S/D/Q/E, held; Fly faster on top)
     * -- rebindable actions of its hotkey table, resolved and gated by
     * app_editor_hotkeys.c, moving the same camera the debug flight does. */
    if( app_editor_hotkeys_owns_camera(app) )
    {
        int const fly = app_editor_hotkeys_active(app, EDITOR_HK_CAM_FAST) ? move * 4 : move;
        if( app_editor_hotkeys_active(app, EDITOR_HK_CAM_FORWARD) )
            app_camera_move_forward(app, fly);
        if( app_editor_hotkeys_active(app, EDITOR_HK_CAM_BACK) )
            app_camera_move_forward(app, -fly);
        if( app_editor_hotkeys_active(app, EDITOR_HK_CAM_LEFT) )
            app_camera_move_left(app, -fly);
        if( app_editor_hotkeys_active(app, EDITOR_HK_CAM_RIGHT) )
            app_camera_move_left(app, fly);
        if( app_editor_hotkeys_active(app, EDITOR_HK_CAM_UP) )
            app->world_views.frame->world_camera_pos.y -= fly;
        if( app_editor_hotkeys_active(app, EDITOR_HK_CAM_DOWN) )
            app->world_views.frame->world_camera_pos.y += fly;
    }
    /* Whichever flight moved it, the editor's camera stays over the scene it
     * can see (app_camera_clamp.h): a few tiles past the loaded squares at
     * most, never under the ground, never higher than the scene is wide. */
    app_camera_keep_over_scene(app);

    /* Arrows drive the orbit camera (reference keyHeld[1..4]); the follow
     * step consumes these next frame. When the follow cam is off (offline or
     * scripted) fall back to the free-cam direct rotate. */
    if( app_editor_hotkeys_owns_camera(app) )
    {
        /* The editor's turn / tilt actions: the bare arrows, always (the
         * nudges are Ctrl+arrows), and rebindable. */
        app->world_views.frame->cam_key_left = app_editor_hotkeys_active(app, EDITOR_HK_CAM_TURN_LEFT);
        app->world_views.frame->cam_key_right = app_editor_hotkeys_active(app, EDITOR_HK_CAM_TURN_RIGHT);
        app->world_views.frame->cam_key_up = app_editor_hotkeys_active(app, EDITOR_HK_CAM_TILT_UP);
        app->world_views.frame->cam_key_down = app_editor_hotkeys_active(app, EDITOR_HK_CAM_TILT_DOWN);
    }
    else if( app->revconfig_profile.camera.controls & REVCONFIG_CAMERA_CONTROL_ARROW_KEYS )
    {
        app->world_views.frame->cam_key_left = LibToriRS_Input_IsKeyHeld(input, TORIRSK_LEFT);
        app->world_views.frame->cam_key_right = LibToriRS_Input_IsKeyHeld(input, TORIRSK_RIGHT);
        app->world_views.frame->cam_key_up = LibToriRS_Input_IsKeyHeld(input, TORIRSK_UP);
        app->world_views.frame->cam_key_down = LibToriRS_Input_IsKeyHeld(input, TORIRSK_DOWN);
    }
    else
    {
        /* Cleared, not merely left alone: the follow step reads these every
         * cycle and a latch held from before the profile said no would keep
         * accelerating the yaw for as long as the key stayed down. */
        app->world_views.frame->cam_key_left = 0;
        app->world_views.frame->cam_key_right = 0;
        app->world_views.frame->cam_key_up = 0;
        app->world_views.frame->cam_key_down = 0;
    }
    /* The free camera's keys (deob Statics.method1034's mode 1 arm); Escape
     * leaves it and tells the server (method6646). */
    if( CamScript_FreeCamera(&app->cam_script) )
    {
        CamScript_OculusKeys(
            &app->cam_script,
            LibToriRS_Input_IsKeyHeld(input, TORIRSK_W),
            LibToriRS_Input_IsKeyHeld(input, TORIRSK_S),
            LibToriRS_Input_IsKeyHeld(input, TORIRSK_A),
            LibToriRS_Input_IsKeyHeld(input, TORIRSK_D),
            LibToriRS_Input_IsKeyHeld(input, TORIRSK_R),
            LibToriRS_Input_IsKeyHeld(input, TORIRSK_F),
            LibToriRS_Input_IsKeyHeld(input, TORIRSK_SHIFT));
        if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_ESCAPE) )
        {
            App_SendExitFreecam(app);
            CamScript_ExitFreeCamera(&app->cam_script);
        }
    }
    if( CamScript_IsScripted(&app->cam_script) || !app->net || app->world_views.frame->camera_unlocked )
    {
        if( app->world_views.frame->cam_key_left )
            app->world_views.frame->world_camera.yaw =
                ToriDraw_AddAngle(app->world_views.frame->world_camera.yaw, rotate);
        if( app->world_views.frame->cam_key_right )
            app->world_views.frame->world_camera.yaw =
                ToriDraw_AddAngle(app->world_views.frame->world_camera.yaw, -rotate);
        if( app->world_views.frame->cam_key_up )
            app->world_views.frame->world_camera.pitch =
                ToriDraw_AddAngle(app->world_views.frame->world_camera.pitch, rotate);
        if( app->world_views.frame->cam_key_down )
            app->world_views.frame->world_camera.pitch =
                ToriDraw_AddAngle(app->world_views.frame->world_camera.pitch, -rotate);
    }

    /* Unlock / relock the camera. Unlocked, the follow update stands down
     * (app_world_camera_follow) and the configured movement bindings fly the
     * eye while arrows rotate it — the debug flight that only worked offline,
     * available online.
     * Relocking snaps back through the follow's own teleport path. */
    if( app_debug_key_down(app, input, APP_DEBUG_HOTKEY_CAMERA_UNLOCK) )
    {
        app->world_views.frame->camera_unlocked = !app->world_views.frame->camera_unlocked;
        TORIRS_LOG("camera: %s\n", app->world_views.frame->camera_unlocked ? "UNLOCKED" : "locked");
        RedrawRequest_Raise(&app->redraw_request);
    }

    /* Reload the world through the task system (assets cached -> fast;
     * rebuild clears world scene elements incl. spawned entities). */
    if( app_debug_key_down(app, input, APP_DEBUG_HOTKEY_WORLD_RELOAD) &&
        App_WorldNodeIndex(app) >= 0 )
        app_world_load_begin(app, NULL, 0);

    /* Painter-command stepping, the v0 client's debug (docs/ORANGE_WEDGE.md):
     * I toggles the cap (unlimited <-> 0), J/K step it +-1, L/, +-100.  The
     * raster then draws exactly the first N painter commands, which is how a
     * draw-order artefact is walked to the command that paints it.
     *
     * The steppers repeat while HELD, like the W/A/S/D camera keys and unlike
     * the one-shot debug keys above: a scene is ~1700 commands, so finding the
     * one that paints a pixel by tapping J is not a thing anyone will do. Only
     * the toggle is edge-triggered — held, it would flip every frame. */
    {
        int limit = ToriRS_Frame_PaintLimitGet();
        int next = limit;
        int stepping = 0;
        int toggled = 0;
        /* One log line per gesture, not per frame: while a stepper is held the
         * value changes every frame, and a hundred lines a second buries the
         * number you are trying to read. Printed when the keys settle.
         * Seeded with the starting cap so an untouched client says nothing. */
        static int logged = INT_MIN;
        if( logged == INT_MIN )
            logged = limit;

        if( app_debug_key_down(app, input, APP_DEBUG_HOTKEY_PAINT_TOGGLE) )
        {
            next = limit < 0 ? 0 : -1;
            toggled = 1;
        }
        if( app_debug_key_held(app, input, APP_DEBUG_HOTKEY_PAINT_MORE) )
        {
            next = (next < 0 ? 0 : next) + 1;
            stepping = 1;
        }
        if( app_debug_key_held(app, input, APP_DEBUG_HOTKEY_PAINT_LESS) )
        {
            next = (next < 0 ? 0 : next) - 1;
            stepping = 1;
        }
        if( app_debug_key_held(app, input, APP_DEBUG_HOTKEY_PAINT_MORE_100) )
        {
            next = (next < 0 ? 0 : next) + 100;
            stepping = 1;
        }
        if( app_debug_key_held(app, input, APP_DEBUG_HOTKEY_PAINT_LESS_100) )
        {
            next = (next < 0 ? 0 : next) - 100;
            stepping = 1;
        }

        if( next != limit )
        {
            if( next < -1 )
                next = -1;
            ToriRS_Frame_PaintLimitSet(next);
            RedrawRequest_Raise(&app->redraw_request);
        }
        if( (toggled || !stepping) && ToriRS_Frame_PaintLimitGet() != logged )
        {
            logged = ToriRS_Frame_PaintLimitGet();
            TORIRS_LOG("paintlimit: %d\n", logged);
        }
    }
}

/*
 * Does the follow camera zoom right now? Both halves have to say yes.
 *
 * `wheel` is the SWITCH -- the settings page's "Zoom" row. Pinned, the eye
 * stays at `[camera] rest=` with nothing the player does moving it, which is
 * the 2004 camera. `zoom_closest < zoom_furthest` is the ROOM: a profile may
 * state both ends the same and leave the wheel nowhere to go even when the
 * switch is on.
 *
 * Reading only the band was what made the "Zoom" row a no-op on the two
 * behaviours it appeared to name -- it moved no wheel and only changed the
 * projection, which is the one thing it should never have touched.
 */
int
app_world_camera_zooms(struct App const* app)
{
    assert(app);
    if( app->revconfig_profile.camera.wheel != REVCONFIG_CAMERA_WHEEL_LIVE )
        return 0;
    return app->revconfig_profile.camera.zoom_closest < app->revconfig_profile.camera.zoom_furthest;
}

/* TORIRS_CAM_DEBUG=1: one line whenever a mouse gesture moves the camera.
 * Prints whichever camera is live, since the follow cam and the free cam keep
 * their angles in different fields. */
static void
app_debug_log_camera(
    struct App* app,
    char const* what,
    int follow_cam)
{
    if( !getenv("TORIRS_CAM_DEBUG") )
        return;
    TORIRS_LOG(
        "cam_%s: %s yaw=%d pitch=%d height=%d eye=%d,%d,%d\n",
        what,
        follow_cam ? "orbit" : "free",
        follow_cam ? app->world_views.frame->orbit.yaw : app->world_views.frame->world_camera.yaw,
        follow_cam ? app->world_views.frame->orbit.pitch : app->world_views.frame->world_camera.pitch,
        app->world_views.frame->world_cam_zoom,
        app->world_views.frame->world_camera_pos.x,
        app->world_views.frame->world_camera_pos.y,
        app->world_views.frame->world_camera_pos.z);
}

/* Middle-button rotate and wheel zoom over the world viewport. Both gestures
 * are properties of the REVISION (revconfig `[camera] controls=` and the
 * band), not of the viewport widget: a camera that cannot zoom cannot zoom over any
 * viewport. The emit desc is still what app_world_mouse_gate reads to decide
 * the pointer is on the scene rather than on the interface.
 *
 * Neither gesture exists in the reference client, so there is nothing to match.
 * The rotate uses the drag-the-camera convention every OSRS client with a
 * middle-button camera uses: the view turns the way the pointer moves, so the
 * scene slides the OPPOSITE way. In projection terms (app_world_project) screen
 * x grows with camera yaw and the scene rises as pitch grows, hence yaw -= dx
 * and pitch += dy. Both cameras get the identical screen-space rule; matching
 * each one's arrow keys instead is not an option, since the free cam's arrows
 * and the orbit cam's turn the camera in opposite directions. */
void
app_world_camera_mouse(
    struct App* app,
    struct LibToriRS_Input* input,
    struct UIInteractOut const* out)
{
    int mouse_x = input->curr.mouse_x;
    int mouse_y = input->curr.mouse_y;
    int follow_cam;

    if( !WorldLoadState_Active(&app->world_load) || !app_world_viewport_component_live(app) )
    {
        app->world_views.frame->cam_mmb_active = 0;
        return;
    }
    /* The same split app_world_camera_keys makes: online and out of a cutscene
     * the orbit follow cam owns the angles, otherwise the free camera does. */
    follow_cam = app->net && !CamScript_IsScripted(&app->cam_script);

    /* The grab pan (Space, or Unity's Alt+Ctrl) under either scheme: the
     * left-drag moves the ground with the pointer, then the scheme's own
     * handling runs as usual (its wheel, its keys). A grab whose button came
     * up, whose editor closed or whose camera became the follow cam ends. */
    if( g_cam_grab.active )
    {
        if( follow_cam || !app_editor_hotkeys_owns_camera(app) || !LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT) )
            g_cam_grab.active = 0;
        else
        {
            int const dx = mouse_x - g_cam_grab.last_x;
            int const dy = mouse_y - g_cam_grab.last_y;
            if( dx || dy )
            {
                app_camera_pan_ground(app, dx, dy);
                app_camera_keep_over_scene(app);
                app_debug_log_camera(app, "grab-pan", 0);
            }
            g_cam_grab.last_x = mouse_x;
            g_cam_grab.last_y = mouse_y;
        }
    }

    /* The editor's Unity-style scheme takes the mouse whole: its own drags
     * and its zoom-to-cursor wheel, none of the default scheme's below. */
    if( !follow_cam && app_camera_unity_on(app) )
    {
        app->world_views.frame->cam_mmb_active = 0;
        app_camera_unity_mouse(app, input, out);
        return;
    }

    /* `controls=` is the revision's answer for a MOUSE; the device's
     * touch_camera is the platform's answer for a FINGER, and the two are
     * different questions. @see platform/device_caps.h. */
    if( (app->revconfig_profile.camera.controls & REVCONFIG_CAMERA_CONTROL_MMB) ||
        DeviceCaps_TouchCamera(&app->device_caps) )
    {
        /* Only the press has to land on the scene; once latched the drag keeps
         * the pointer until release, so sweeping over the sidebar mid-rotate
         * does not stall the camera. */
        if( !app->world_views.frame->cam_mmb_active && input->curr.mouse_button_down[TORIRSM_MIDDLE] &&
            !app->world_views.frame->minimenu->visible && app_world_mouse_gate(app, mouse_x, mouse_y) )
        {
            app->world_views.frame->cam_mmb_active = 1;
            app->world_views.frame->cam_mmb_x = mouse_x;
            app->world_views.frame->cam_mmb_y = mouse_y;
        }

        if( app->world_views.frame->cam_mmb_active )
        {
            int dx = mouse_x - app->world_views.frame->cam_mmb_x;
            int dy = mouse_y - app->world_views.frame->cam_mmb_y;

            app->world_views.frame->cam_mmb_x = mouse_x;
            app->world_views.frame->cam_mmb_y = mouse_y;

            if( dx != 0 || dy != 0 )
            {
                if( follow_cam )
                {
                    /* The key path eases through a velocity; a drag is already
                     * a position delta, so it writes the angle and zeroes the
                     * velocity rather than fighting the decay next frame. */
                    app->world_views.frame->orbit.yaw =
                        (app->world_views.frame->orbit.yaw - dx * APP_WORLD_MMB_YAW_PER_PX) & 0x7ff;
                    app->world_views.frame->orbit.pitch = app_world_clamp_pitch(
                        app, app->world_views.frame->orbit.pitch + dy * APP_WORLD_MMB_PITCH_PER_PX);
                    app->world_views.frame->orbit.yaw_velocity = 0;
                    app->world_views.frame->orbit.pitch_velocity = 0;
                }
                else
                {
                    app->world_views.frame->world_camera.yaw = ToriDraw_AddAngle(
                        app->world_views.frame->world_camera.yaw, -dx * APP_WORLD_MMB_YAW_PER_PX);
                    app->world_views.frame->world_camera.pitch = ToriDraw_AddAngle(
                        app->world_views.frame->world_camera.pitch, dy * APP_WORLD_MMB_PITCH_PER_PX);
                }
                app_debug_log_camera(app, "rotate", follow_cam);
                RedrawRequest_Raise(&app->redraw_request);
            }
        }

        if( !LibToriRS_Input_IsMouseHeld(input, TORIRSM_MIDDLE) ||
            input->curr.mouse_button_up[TORIRSM_MIDDLE] )
            app->world_views.frame->cam_mmb_active = 0;
    }
    else
        app->world_views.frame->cam_mmb_active = 0;

    /* A trackpad's two-finger scroll flies the free camera on BOTH axes, by
     * the exact deltas: up/down dollies forward/back at the wheel's rate per
     * notch's worth of pixels, left/right strafes at the same rate, so a
     * diagonal swipe flies diagonally and a slow one moves slowly instead of
     * waiting out a whole notch. The platform's "natural" flip is taken back
     * out as it is for mouse_wheel_y: fingers away fly forward and fingers
     * right fly right, whatever the setting. The same swipe is also in
     * mouse_wheel_y; it is not dollied twice. */
    if( !follow_cam && input->curr.scroll_trackpad
        && (input->curr.scroll_dx != 0.0f || input->curr.scroll_dy != 0.0f) && !out->wheel_consumed
        && !app->world_views.frame->minimenu->visible && !app_chrome_wants_pointer(app, mouse_x, mouse_y)
        && app_world_mouse_gate(app, mouse_x, mouse_y) )
    {
        float const sign = input->curr.scroll_flipped ? -1.0f : 1.0f;
        float per_px;

        assert(input->curr.scroll_px_per_notch > 0);
        per_px = (float)APP_WORLD_ZOOM_FREECAM_STEP / (float)input->curr.scroll_px_per_notch;
        app_camera_move_forward(app, (int)lroundf(sign * input->curr.scroll_dy * per_px));
        /* app_camera_move_left's positive is RIGHT (the Right key's +fly). */
        app_camera_move_left(app, (int)lroundf(sign * input->curr.scroll_dx * per_px));
        app_debug_log_camera(app, "trackpad", 0);
        RedrawRequest_Raise(&app->redraw_request);
        return;
    }

    /* Wheel up (positive) zooms in. Gated on the pointer being over the scene
     * and on no widget having already taken this notch, so a wheel over a
     * scroll pane drawn across the viewport still belongs to that pane —
     * app_world_mouse_gate alone only rejects *interactive* nodes, and an IF1
     * scroll layer is pass-through. */
    if( input->curr.mouse_wheel_y != 0 && !out->wheel_consumed &&
        !app->world_views.frame->minimenu->visible &&
        /* The chrome's claim is checked HERE, not inferred from consumed
         * flags: this runs long after the overlay handled input, when
         * the frame claim (input_claims) is taken on every frame. A wheel over a panel or
         * an open dropdown belongs to the chrome even when the chrome had
         * nothing to do with it -- consumed into nothing beats zooming the
         * world behind a panel. */
        !app_chrome_wants_pointer(app, mouse_x, mouse_y) &&
        app_world_mouse_gate(app, mouse_x, mouse_y) )
    {
        /* Only the FOLLOW camera is the revision's. The free camera is this
         * client's own debug flight -- the one U unlocks and W/A/S/D drives --
         * so a revision that does not zoom does not take its dolly away, or an
         * offline rev-254 boot (which has no follow camera at all) would lose
         * the only way it has of moving in or out. */
        if( !follow_cam )
        {
            app_camera_move_forward(app, input->curr.mouse_wheel_y * APP_WORLD_ZOOM_FREECAM_STEP);
        }
        else if( app_world_camera_zooms(app) )
        {
            app->world_views.frame->world_cam_zoom = RevConfigProfile_CameraClampZoom(
                &app->revconfig_profile,
                app->world_views.frame->world_cam_zoom -
                    input->curr.mouse_wheel_y * app->revconfig_profile.camera.wheel_step);
        }
        else
            return;
        app_debug_log_camera(app, "zoom", follow_cam);
        RedrawRequest_Raise(&app->redraw_request);
    }
}

/* Per-frame world step: sim cycles, event drain (entity removals -> scene),
 * position sync, animation ticks. Runs every frame (cycles may be 0) so the
 * painter dynamic set stays fresh, and forces a redraw while active — but only
 * while a viewport is actually on screen; an unshown world does not tick. */
/* The level cutscene heights are measured against (reference minusedlevel) —
 * and the plane every ROOT-world judgment about the local player uses: mover
 * heights, the pick's reach filter, the minimap bake. ABOARD, the player's
 * own level is a DECK plane (the planking is authored at plane 1) and means
 * nothing to the root — the effective root plane is the HULL's. Without this
 * a level-1 rider had every shore npc's height sampled from the level-1
 * heightmap: the whole town floating on the wall tops. */
int
app_cinema_level(struct App* app)
{
    int world_idx;
    struct WorldEntity_Player* player;

    /* Aboard: the ROOT view's own level (deob field3266.field1400, which
     * Statics.java's root-world height samples read), what Jagex's
     * SET_ACTIVE_WORLD for the root names while the player stands on a deck
     * (00 in RSProx 4281, where the deck's own is 01). */
    if( Wevs_AboardView(&app->wevs) != WORLDVIEW_ROOT && Wevs_IsLive(&app->wevs, Wevs_AboardView(&app->wevs)) )
        return WorldviewRegistry_Get(&app->worldviews, WORLDVIEW_ROOT)->level;
    if( !RS_EntitySync_FindPlayer(
            &app->esync,
            app->esync.local_pid >= 0 ? app->esync.local_pid : 2047,
            &world_idx,
            NULL) )
        return 0;
    player = World_EntityPoolGet(&app->world->entities.player, world_idx);
    return player ? player->grid_position.level : 0;
}

/* Scene-space position of a cutscene target. `height` is measured up from the
 * ground under the tile, and up is -y. */
static void
app_cinema_point(
    struct App* app,
    int local_x,
    int local_z,
    int height,
    int* out_x,
    int* out_y,
    int* out_z)
{
    int x = local_x * 128 + 64;
    int z = local_z * 128 + 64;
    *out_x = x;
    *out_z = z;
    *out_y = app_world_height(app, x, z, app_cinema_level(app)) - height;
}

/* Pitch/yaw that point the frame view's eye at scene point (tx, ty, tz). The
 * yaw multiplier is *negative* 325.949 -- the scene turns the opposite way to
 * the mathematical angle. */
static void
app_cinema_angles_to(
    struct App* app,
    int tx,
    int ty,
    int tz,
    int* out_pitch,
    int* out_yaw)
{
    int dx, dy, dz, distance, pitch;

    dx = tx - app->world_views.frame->world_camera_pos.x;
    dy = ty - app->world_views.frame->world_camera_pos.y;
    dz = tz - app->world_views.frame->world_camera_pos.z;
    distance = (int)sqrt((double)dx * dx + (double)dz * dz);

    pitch = (int)(atan2((double)dy, (double)distance) * 325.949) & 0x7ff;
    /* The scripted camera tips no further than the player's own may: the
     * profile's range, not a second copy of the reference's numbers. */
    pitch = app_world_clamp_pitch(app, pitch);

    *out_pitch = pitch;
    *out_yaw = (int)(atan2((double)dx, (double)dz) * -325.949) & 0x7ff;
}

/* Pitch/yaw that point the eye at the look-at target (deob Statics.method234's
 * aim, eased by Statics.method4719). */
static void
app_cinema_angles(
    struct App* app,
    int* out_pitch,
    int* out_yaw)
{
    int tx, ty, tz;
    int look_lx, look_lz, look_height;

    CamScript_LookTarget(&app->cam_script, &look_lx, &look_lz, &look_height);
    app_cinema_point(app, look_lx, look_lz, look_height, &tx, &ty, &tz);
    app_cinema_angles_to(app, tx, ty, tz, out_pitch, out_yaw);
}

static int
app_cam_cycle(struct App* app)
{
    return app->world ? app->world->cycle : 0;
}

/*
 * CAM_MOVETO_CYCLES at arrival. The interpolation starts where the eye IS, as
 * the deob's class568 does: in absolute scene y when `ignore_terrain`, else in
 * height above the ground under the eye -- so the path follows the terrain.
 */
void
App_CinemaCameraMoveCycles(
    struct App* app,
    int local_x,
    int local_z,
    int height,
    int cycles,
    int ignore_terrain,
    int easing,
    int height_relative)
{
    int level, cx, cy, cz, x1, z1, y0, y1;

    assert(app);
    if( !app->world )
        return;
    level = app_cinema_level(app);
    cx = app->world_views.frame->world_camera_pos.x;
    cy = app->world_views.frame->world_camera_pos.y;
    cz = app->world_views.frame->world_camera_pos.z;
    x1 = local_x * 128 + 64;
    z1 = local_z * 128 + 64;
    if( ignore_terrain )
    {
        y0 = cy;
        y1 = height_relative ? cy - height : app_world_height(app, x1, z1, level) - height;
    }
    else
    {
        y0 = app_world_height(app, cx, cz, level) - cy;
        y1 = CamScript_MoveHeightFor(&app->cam_script, height, height_relative);
    }
    CamScript_MoveCycles(
        &app->cam_script, cx, cz, y0, x1, z1, y1, ignore_terrain, app_cam_cycle(app), cycles, easing);
}

/* CAM_MOVETO_ARC at arrival: the cycles move above, with x/z on the arc
 * through the via tile (render/cam_arc.h, deob class561; client.java's
 * class243.field3062/field3063 handlers build it as field3064's builds the
 * cycles move). */
void
App_CinemaCameraMoveArc(
    struct App* app,
    int local_x,
    int local_z,
    int via_local_x,
    int via_local_z,
    int height,
    int cycles,
    int ignore_terrain,
    int easing,
    int height_relative)
{
    assert(app);
    App_CinemaCameraMoveCycles(
        app, local_x, local_z, height, cycles, ignore_terrain, easing, height_relative);
    CamScript_MoveArc(&app->cam_script, via_local_x * 128 + 64, via_local_z * 128 + 64);
}

/* The free camera's focus for one client cycle (deob Statics.method1034, mode
 * 1): field1180 eases toward the held speed, and the focus moves that way in
 * the camera's frame. R and F lift it, never below the ground line the focus
 * started on (field5690 stays <= 0 in the deob's units, here oculus_y). */
static void
app_world_camera_oculus_step(
    struct App* app,
    int player_x,
    int player_z,
    int level)
{
    struct WorldCameraOrbit* orbit = &app->world_views.frame->orbit;
    int step, angle;
    int const yaw = app->world_views.frame->world_camera.yaw;

    if( CamScript_OculusRecentrePending(&app->cam_script) )
    {
        if( player_x >= 0 && player_z >= 0 )
        {
            int const ground = app_world_height(app, player_x, player_z, level) - 50;

            orbit->anchor_x = (float)player_x;
            orbit->anchor_z = (float)player_z;
            CamScript_OculusRecentred(&app->cam_script, ground);
        }
    }

    if( CamScript_OculusStep(&app->cam_script, &step, &angle) && angle >= 0 )
    {
        double const radians = (double)((angle - yaw) & 0x7ff) * 2.0 * M_PI / 2048.0;

        orbit->anchor_x += (float)(step * sin(radians));
        orbit->anchor_z += (float)(step * cos(radians));
    }
    RedrawRequest_Raise(&app->redraw_request);
}

/*
 * CAM_ROTATETO_COORDINATE at arrival: the pitch and yaw that face the tile
 * from where the eye is now, eased toward from the current ones. The yaw is
 * taken the short way round (the deob's class563 interpolates the raw ints).
 */
void
App_CinemaCameraRotateCycles(
    struct App* app,
    int local_x,
    int local_z,
    int height,
    int cycles,
    int easing,
    int height_relative)
{
    int tx, ty, tz, pitch, yaw, look_height;

    assert(app);
    if( !app->world )
        return;
    tx = local_x * 128 + 64;
    tz = local_z * 128 + 64;
    look_height = CamScript_LookHeightFor(&app->cam_script, height, height_relative);
    ty = app_world_height(app, tx, tz, app_cinema_level(app)) - look_height;
    app_cinema_angles_to(app, tx, ty, tz, &pitch, &yaw);
    CamScript_RotateCycles(
        &app->cam_script,
        app->world_views.frame->world_camera.pitch,
        pitch,
        app->world_views.frame->world_camera.yaw,
        yaw,
        look_height,
        app_cam_cycle(app),
        cycles,
        easing);
}

/* CAM_ROTATEBY at arrival (deob client.java class259.field3190): an axis asked
 * to move 0 keeps whatever ease it has. The yaw is not taken the short way: a
 * delta names its own direction. */
void
App_CinemaCameraRotateBy(
    struct App* app,
    int yaw_delta,
    int pitch_delta,
    int cycles,
    int easing)
{
    int pitch, yaw;

    assert(app);
    pitch = app->world_views.frame->world_camera.pitch;
    yaw = app->world_views.frame->world_camera.yaw;
    if( yaw_delta != 0 )
        CamScript_StartYaw(&app->cam_script, yaw, yaw + yaw_delta, app_cam_cycle(app), cycles, easing);
    if( pitch_delta != 0 )
        CamScript_StartPitch(
            &app->cam_script,
            pitch,
            app_world_clamp_pitch(app, pitch + pitch_delta),
            app_cam_cycle(app),
            cycles,
            easing);
    CamScript_RotateBegun(&app->cam_script);
}

/* CAM_ROTATETO at arrival (deob client.java class259.field3187): a negative
 * angle keeps that axis's ease. */
void
App_CinemaCameraRotateTo(
    struct App* app,
    int yaw,
    int pitch,
    int cycles,
    int easing)
{
    assert(app);
    if( yaw >= 0 )
        CamScript_StartYawShortWay(
            &app->cam_script,
            app->world_views.frame->world_camera.yaw,
            yaw & 0x7ff,
            app_cam_cycle(app),
            cycles,
            easing);
    if( pitch >= 0 )
        CamScript_StartPitch(
            &app->cam_script,
            app->world_views.frame->world_camera.pitch,
            app_world_clamp_pitch(app, pitch),
            app_cam_cycle(app),
            cycles,
            easing);
    CamScript_RotateBegun(&app->cam_script);
}

/* A CAM_* snap takes every attached view (owner decision 2): the scripted
 * camera is the game's, not one watcher's. */
void
App_CinemaCameraSnapPosition(struct App* app)
{
    int move_lx, move_lz, move_height;

    assert(app);
    CamScript_MoveTarget(&app->cam_script, &move_lx, &move_lz, &move_height);
    for( int view = 0; view < app->world_views.split.view_count; view++ )
        app_cinema_point(
            app,
            move_lx,
            move_lz,
            move_height,
            &app->world_views.views[view].world_camera_pos.x,
            &app->world_views.views[view].world_camera_pos.y,
            &app->world_views.views[view].world_camera_pos.z);
}

void
App_CinemaCameraSnapAngle(struct App* app)
{
    struct App_WorldView* saved;

    assert(app);
    saved = app->world_views.frame;
    for( int view = 0; view < app->world_views.split.view_count; view++ )
    {
        /* app_cinema_angles reads the eye of the view it aims (world_views.frame). */
        app->world_views.frame = &app->world_views.views[view];
        app_cinema_angles(
            app, &app->world_views.views[view].world_camera.pitch, &app->world_views.views[view].world_camera.yaw);
    }
    app->world_views.frame = saved;
}

/* Deob Statics.method234 (ar.bz). Runs every frame the script is up: the eye
 * walks toward the move-to point (Statics.method10397) and turns toward the
 * look-at point (Statics.method4719), a flat rate plus rate2/1000 of what is
 * left, so a rate2 under 100 glides instead of cutting. A half whose rates are
 * 0 (as CAM_RESET leaves them) holds still. */
void
app_world_camera_cinema(struct App* app)
{
    int tx, ty, tz, pitch, yaw;
    int rate, rate2;
    int mx, mz, my, ignore_terrain;
    int move_lx, move_lz, move_height;
    int look_lx, look_lz, look_height;

    CamScript_MoveRates(&app->cam_script, &rate, &rate2);
    if( !CamScript_IsScripted(&app->cam_script) || !app->world )
        return;
    /* client.java's frame step: `if (field698) method3868 else if (field904)
     * method234`. The glide needs the follow camera's answer, so it runs there. */
    if( !CamScript_HoldsEye(&app->cam_script) )
        return;

    if( CamScript_MoveCyclesAt(&app->cam_script, app_cam_cycle(app), &mx, &mz, &my, &ignore_terrain) )
    {
        /* The eased move owns the eye; the rate walk below waits for the next
         * CAM_MOVETO. */
        app->world_views.frame->world_camera_pos.x = mx;
        app->world_views.frame->world_camera_pos.z = mz;
        app->world_views.frame->world_camera_pos.y =
            ignore_terrain ? my : app_world_height(app, mx, mz, app_cinema_level(app)) - my;
    }
    else
    {
        CamScript_MoveTarget(&app->cam_script, &move_lx, &move_lz, &move_height);
        app_cinema_point(app, move_lx, move_lz, move_height, &tx, &ty, &tz);

        app->world_views.frame->world_camera_pos.x =
            CamScript_Walk(app->world_views.frame->world_camera_pos.x, tx, rate, rate2);
        app->world_views.frame->world_camera_pos.y =
            CamScript_Walk(app->world_views.frame->world_camera_pos.y, ty, rate, rate2);
        app->world_views.frame->world_camera_pos.z =
            CamScript_Walk(app->world_views.frame->world_camera_pos.z, tz, rate, rate2);
    }

    if( CamScript_Rotating(&app->cam_script) )
    {
        if( CamScript_RotatePitchAt(&app->cam_script, app_cam_cycle(app), &pitch) )
            app->world_views.frame->world_camera.pitch = app_world_clamp_pitch(app, pitch);
        if( CamScript_RotateYawAt(&app->cam_script, app_cam_cycle(app), &yaw) )
            app->world_views.frame->world_camera.yaw = yaw;
        RedrawRequest_Raise(&app->redraw_request);
        return;
    }

    /* Deob Statics.method234's field1243 branch: the look-at point eases and the
     * ordinary turn follows it, at a flat rate of 100 in the deob's 14-bit angles
     * (class61.field571) that outlasts the ease; on arrival the ordinary look-at
     * resumes on the same point. */
    if( CamScript_LookCyclesStep(&app->cam_script, app_cam_cycle(app), &tx, &tz, &look_height) )
    {
        ty = app_world_height(app, tx, tz, app_cinema_level(app)) - look_height;
        app_cinema_angles_to(app, tx, ty, tz, &pitch, &yaw);
    }
    else
    {
        app_cinema_angles(app, &pitch, &yaw);
    }
    CamScript_LookRates(&app->cam_script, &rate, &rate2);
    app->world_views.frame->world_camera.pitch =
        CamScript_Walk(app->world_views.frame->world_camera.pitch, pitch, rate, rate2);

    app->world_views.frame->world_camera.yaw =
        CamScript_WalkYaw(app->world_views.frame->world_camera.yaw, yaw, rate, rate2);

    /* TORIRS_CAM_DEBUG=1: trace the scripted camera. */
    if( getenv("TORIRS_CAM_DEBUG") )
    {
        CamScript_MoveTarget(&app->cam_script, &move_lx, &move_lz, &move_height);
        CamScript_LookTarget(&app->cam_script, &look_lx, &look_lz, &look_height);
        TORIRS_LOG(
            "cam eye=%d,%d,%d pitch=%d yaw=%d -> move=%d,%d h=%d look=%d,%d h=%d "
            "shake=%d%d%d%d%d\n",
            app->world_views.frame->world_camera_pos.x,
            app->world_views.frame->world_camera_pos.y,
            app->world_views.frame->world_camera_pos.z,
            app->world_views.frame->world_camera.pitch,
            app->world_views.frame->world_camera.yaw,
            move_lx,
            move_lz,
            move_height,
            look_lx,
            look_lz,
            look_height,
            CamScript_ShakeOn(&app->cam_script, 0),
            CamScript_ShakeOn(&app->cam_script, 1),
            CamScript_ShakeOn(&app->cam_script, 2),
            CamScript_ShakeOn(&app->cam_script, 3),
            CamScript_ShakeOn(&app->cam_script, 4));
    }

    RedrawRequest_Raise(&app->redraw_request);
}

/* Deob Statics.method3868, one view's step: the eye and its angles walk toward
 * where the follow camera would put them (class71's scene draw keeps that
 * target current while the script is up, Statics.method13640) at the rates the
 * last CAM_MOVETO and CAM_LOOKAT left. A view on all five has arrived. */
static void
app_world_camera_smooth_reset_step(
    struct App* app,
    struct ToriDraw_Position const* eye,
    int pitch,
    int yaw)
{
    struct App_WorldView* view = app->world_views.frame;
    int move_rate, move_rate2, look_rate, look_rate2;

    CamScript_MoveRates(&app->cam_script, &move_rate, &move_rate2);
    CamScript_LookRates(&app->cam_script, &look_rate, &look_rate2);
    view->world_camera_pos.x = CamScript_Walk(view->world_camera_pos.x, eye->x, move_rate, move_rate2);
    view->world_camera_pos.y = CamScript_Walk(view->world_camera_pos.y, eye->y, move_rate, move_rate2);
    view->world_camera_pos.z = CamScript_Walk(view->world_camera_pos.z, eye->z, move_rate, move_rate2);
    view->world_camera.pitch = CamScript_Walk(view->world_camera.pitch, pitch, look_rate, look_rate2);
    view->world_camera.yaw = CamScript_WalkYaw(view->world_camera.yaw, yaw, look_rate, look_rate2);

    if( view->world_camera_pos.x == eye->x && view->world_camera_pos.y == eye->y &&
        view->world_camera_pos.z == eye->z && view->world_camera.pitch == pitch &&
        view->world_camera.yaw == yaw )
        CamScript_GlideArrived(&app->cam_script);

    /* TORIRS_CAM_DEBUG=1: trace the scripted camera, the glide included. */
    if( getenv("TORIRS_CAM_DEBUG") )
        TORIRS_LOG(
            "cam glide eye=%d,%d,%d pitch=%d yaw=%d -> eye=%d,%d,%d pitch=%d yaw=%d rates=%d,%d %d,%d\n",
            view->world_camera_pos.x,
            view->world_camera_pos.y,
            view->world_camera_pos.z,
            view->world_camera.pitch,
            view->world_camera.yaw,
            eye->x,
            eye->y,
            eye->z,
            pitch,
            yaw,
            move_rate,
            move_rate2,
            look_rate,
            look_rate2);

    RedrawRequest_Raise(&app->redraw_request);
}

/* The tail of Statics.method3868, once per frame: every attached view shares
 * the one script, so it ends when the last of them is home. */
void
app_world_camera_smooth_reset_end(struct App* app)
{
    assert(app);
    CamScript_GlideEnd(&app->cam_script, app->world_views.split.view_count);
}

/*
 * Orbit distance zoom (reference Statics.method6352, and the identical
 * expression inlined in client.method2068 and client.method2066).
 *
 * The follow camera's `pitch * 3 + 600` is a FIXED-VIEWPORT distance: the
 * reference then scales it by an endpoint pair interpolated over the world
 * viewport HEIGHT, exactly the way the projection scale is
 * (class159.method5357), and over the same `height - 334` in [0,100] band:
 *
 *     zoom = (far - near) * clamp(vpH - 334, 0, 100) / 100 + near
 *     distance = (pitch * 3 + 600) * zoom / 256
 *
 * near/far are client.field780/field747 — CS2 VIEWPORT_SETZOOM (6201), default
 * 256 and 320. A fixed 334-high viewport therefore leaves the distance alone,
 * and a resizable one (503 here) pulls the eye a full 25% further out.
 *
 * Missing this term is what docs/ORANGE_WEDGE.md 7(b) measured as "the C eye
 * sits ~15% too close" and left open. Distance is not just closeness: the eye is
 * `pivot - distance` along pitch/yaw, so a short distance lowers the eye by the
 * same fraction. That is the "camera is too low" symptom.
 */
static int
app_world_cam_dist_zoom(struct App* app)
{
    int near_zoom = app->host.viewport_zoom;
    int far_zoom = app->host.viewport_zoom_max;
    int d;

    if( near_zoom <= 0 )
        near_zoom = 256;
    if( far_zoom <= 0 )
        far_zoom = 320;
    /* No laid-out viewport yet: 334 is the reference's fixed height, which puts
     * the interpolation on its near endpoint and leaves the distance unscaled. */
    d = (app->world_views.emit_valid ? app->world_views.emit_desc.h : 334) - 334;
    if( d < 0 )
        d = 0;
    if( d > 100 )
        d = 100;
    return (far_zoom - near_zoom) * d / 100 + near_zoom;
}

/* Reference followCamera + camFollow (Client-TS 3459/4669): a 1/16-eased
 * orbit anchor trails the player, arrow keys accumulate yaw/pitch velocity,
 * a terrain scan raises pitch so the eye stays above nearby ground, then the
 * eye is placed `pitch*3+600` behind the anchor along pitch/yaw. Sin/cos are
 * 16.16 (same tables as Pix3D). */
static struct WorldCameraLimits
app_world_camera_limits(struct App const* app)
{
    struct WorldCameraLimits limits;

    assert(app);
    memset(&limits, 0, sizeof(limits));
    limits.pitch_flattest = app->revconfig_profile.camera.pitch_flattest;
    limits.pitch_steepest = app->revconfig_profile.camera.pitch_steepest;
    /* CAM_UNLOCK: the deob's limits go from 1024..3064 to 1..4160 in eighths
     * of a pitch unit (class112.method3876/3877), i.e. 0..520 here. */
    if( CamScript_Unlocked(&app->cam_script) )
    {
        limits.pitch_flattest = APP_CAM_UNLOCK_PITCH_FLATTEST;
        limits.pitch_steepest = APP_CAM_UNLOCK_PITCH_STEEPEST;
    }
    limits.pitch_distance = app->revconfig_profile.camera.pitch_distance;
    limits.rest_zoom = app->world_views.frame->world_cam_zoom;
    limits.viewport_zoom_256 =
        app->revconfig_profile.camera.viewport_zoom ? app_world_cam_dist_zoom((struct App*)app) : 0;
    limits.distance_scale_percent = app->revconfig_profile.camera.distance_scale;
    limits.near_plane_z = app->world_views.frame->world_camera.near_plane_z;
    return limits;
}

void
app_world_camera_follow(struct App* app)
{
    int world_idx;
    struct WorldEntity_Player* player;
    int target_x, target_y, target_z;
    int pitch, yaw, distance;
    int aboard_y = 0;
    int aboard_y_valid = 0;

    /* U unlocked the camera: the follow update stands down and the W/A/S/D +
     * R/F debug keys own world_camera_pos until U relocks. Without this gate
     * the follow overwrites the eye every frame, which is why free flight only
     * ever worked offline. */
    if( app->world_views.frame->camera_unlocked )
    {
        /* A debug-flown view has nowhere to glide to; it must not hold the
         * script open for the others. */
        CamScript_GlideArrived(&app->cam_script);
        return;
    }
    /* A CAM_SMOOTHRESET's glide is still scripted, and needs all of the below
     * (the anchor especially): only the last step changes hands. */
    if( CamScript_HoldsEye(&app->cam_script) || !app->net )
        return;

    /*
     * TORIRS_ORBIT_CAM=yaw[,pitch[,zoom_pct]] — pin the follow camera's angles
     * so a headless capture frames a chosen subject instead of wherever the
     * login left the camera.
     *
     * Distinct from TORIRS_WEDGE_CAM, which pins the eye in world coordinates:
     * that needs the subject's position, this only needs its direction from the
     * player, and it keeps the real follow path (anchor easing, pitch clamp,
     * zoom) so what is captured is the camera the game actually uses. Applied
     * before the step, every frame, with the easing velocities zeroed so the
     * angles cannot drift back.
     *
     * yaw is 0..2047, pitch is clamped into the profile's own
     * `[camera] pitch_flattest=`..`pitch_steepest=` (the same range the
     * middle-button drag allows), zoom is a percentage of `[camera] rest=` —
     * still a percentage here rather than a raw distance, because that is the
     * spelling every recorded TORIRS_ORBIT_CAM string in the tree uses. It is
     * clamped into the `zoom_closest=`..`zoom_furthest=` band, so a profile
     * that states a band of one ignores it exactly as the wheel does.
     */
    {
        static int resolved = 0;
        static int have = 0;
        static int cam_yaw = 0, cam_pitch = 0, cam_zoom = 0, cam_spin = 0;
        if( !resolved )
        {
            char const* spec = getenv("TORIRS_ORBIT_CAM");
            resolved = 1;
            if( spec )
            {
                cam_pitch = -1;
                cam_zoom = -1;
                have = sscanf(spec, "%d,%d,%d,%d", &cam_yaw, &cam_pitch, &cam_zoom, &cam_spin) >= 1;
            }
        }
        /* A fourth field spins the camera by that many yaw units per frame.
         * Finding the angle a subject sits at otherwise costs one boot per
         * guess; with a spin and TORIRS_BMP_SERIES a single boot returns a
         * filmstrip all the way round. */
        cam_yaw += cam_spin;
        if( have )
        {
            app->world_views.frame->orbit.yaw = cam_yaw & 0x7ff;
            app->world_views.frame->orbit.yaw_velocity = 0;
            if( cam_pitch >= 0 )
            {
                app->world_views.frame->orbit.pitch = app_world_clamp_pitch(app, cam_pitch);
                app->world_views.frame->orbit.pitch_velocity = 0;
            }
            if( cam_zoom > 0 )
                /* A percentage of THIS revision's rest, not of the reference
                 * 600. The server is saying "this much closer than normal",
                 * and normal is wherever this camera rests -- reading it
                 * against a constant makes the same packet mean two different
                 * views on two lanes. */
                app->world_views.frame->world_cam_zoom = RevConfigProfile_CameraClampZoom(
                    &app->revconfig_profile, app->revconfig_profile.camera.rest * cam_zoom / 100);
        }
    }
    if( !RS_EntitySync_FindPlayer(
            &app->esync,
            app->esync.local_pid >= 0 ? app->esync.local_pid : 2047,
            &world_idx,
            NULL) )
        return;
    player = World_EntityPoolGet(&app->world->entities.player, world_idx);
    if( !player )
        return;

    target_x = (int)player->draw_position.x;
    target_z = (int)player->draw_position.z;

    /*
     * Aboard (SAILING_PLAN C5.3): the player's own coordinates are deck-local,
     * so the camera follows their deck position pushed through the hull
     * transform into root scene space — recomputed every frame, which is what
     * glues the focus to a gliding, turning hull. The look-at height composes
     * the same way the emit path does: the hull's y (root terrain under the
     * boat, kept by the interpolator) plus the deck's own height under the
     * actor. Nested hulls would need the parent chain composed; the camera
     * handles the root-parented case, which is every hull that exists today.
     */
    aboard_y_valid = 0;
    if( Wevs_AboardView(&app->wevs) != WORLDVIEW_ROOT && player->view_placement.view_id == Wevs_AboardView(&app->wevs) &&
        app_wev_actor_root_fine(app, &player->view_placement, &target_x, &target_z) )
    {
        struct Wev* wev = Wevs_Get(&app->wevs, Wevs_AboardView(&app->wevs));
        struct Worldview* view = WorldviewRegistry_Get(&app->worldviews, Wevs_AboardView(&app->wevs));
        /* The rider's OWN plane, same rule as their placement — the camera
         * plane in the deob is focus.getPlane(), the wire plane. */
        int cam_level = player->grid_position.level;

        if( cam_level < 0 )
            cam_level = 0;
        if( cam_level >= COLLISION_LEVELS )
            cam_level = COLLISION_LEVELS - 1;
        aboard_y = wev->y +
                   World_HeightAt(
                       view->world, player->view_placement.x, player->view_placement.z, cam_level) -
                   8 - 50;
        aboard_y_valid = 1;
    }

    /* Anchor: snap when >500 units out (teleport), else ease 1/16 — in FLOAT,
     * per the reference's client.method1605 (rev-239 deob):
     *
     *     field917 = (targetX - field917) * (dtNanos / 3.2e8) + field917;
     *
     * dt is one 20 ms client cycle here, and 2e7 / 3.2e8 is exactly 1/16, so
     * the rate is the reference's. What matters is the type: the old integer
     * form `orbit_x += (target_x - orbit_x) / 16` truncates the step to 0 once
     * the gap falls below 16, so the anchor stopped a permanent ~15 units short
     * on each axis (~21 units diagonally, a sixth of a tile) in whichever
     * direction the player last walked. The eye is built around that anchor, so
     * the player model swung round a point beside itself while orbiting — the
     * camera appeared to orbit the tile rather than the player. */
    if( CamScript_FreeCamera(&app->cam_script) )
        app_world_camera_oculus_step(app, target_x, target_z, player->grid_position.level);
    else
        Wev_SmoothCameraFocus(
            &app->world_views.frame->orbit.anchor_x, &app->world_views.frame->orbit.anchor_z, target_x, target_z);

    /* Arrow keys -> yaw/pitch velocity. @see WorldCameraOrbit_StepAngles. */
    {
        struct WorldCameraLimits const limits = app_world_camera_limits(app);
        struct WorldCameraKeys const keys = { .left = app->world_views.frame->cam_key_left != 0,
                                              .right = app->world_views.frame->cam_key_right != 0,
                                              .up = app->world_views.frame->cam_key_up != 0,
                                              .down = app->world_views.frame->cam_key_down != 0 };

        WorldCameraOrbit_StepAngles(&app->world_views.frame->orbit, &keys, &limits);
    }

    /* Terrain pitch clamp: scan the 9x9 tile block around the anchor for
     * ground higher than the anchor's; raise the minimum pitch so the eye
     * clears it. cameraPitchClamp is 24.8 fixed (clamp/256 = pitch units). */
    {
        struct Heightmap* hm = app->world ? app->world->heightmap : NULL;
        int level = player->grid_position.level;
        int orbit_ix = (int)app->world_views.frame->orbit.anchor_x;
        int orbit_iz = (int)app->world_views.frame->orbit.anchor_z;
        int orbit_tile_x = orbit_ix >> 7;
        int orbit_tile_z = orbit_iz >> 7;
        int orbit_y = app_world_height(app, orbit_ix, orbit_iz, level);
        int max_y = 0;
        struct WorldCameraLimits const limits = app_world_camera_limits(app);

        if( hm && orbit_tile_x > 3 && orbit_tile_z > 3 && orbit_tile_x < hm->size_x - 4 &&
            orbit_tile_z < hm->size_z - 4 )
        {
            for( int x = orbit_tile_x - 4; x <= orbit_tile_x + 4; x++ )
                for( int z = orbit_tile_z - 4; z <= orbit_tile_z + 4; z++ )
                {
                    /* Reference also bumps to level+1 on VisBelow (bridge)
                     * tiles; tile flags are applied at build time here, so
                     * the stored heights already match what is drawn. */
                    int y = orbit_y - heightmap_get(hm, x, z, level);
                    if( y > max_y )
                        max_y = y;
                }
        }
        /* 192 pitch units per unit of height above the anchor -- the
         * reference's own figure, and the reason a ridge raises the eye
         * rather than the eye sinking into it. */
        WorldCameraOrbit_EaseTerrainClamp(&app->world_views.frame->orbit, &limits, max_y * 192 / 256);
    }

    pitch = WorldCameraOrbit_EffectivePitch(&app->world_views.frame->orbit);
    yaw = app->world_views.frame->orbit.yaw & 0x7ff;
    /*
     * Reference distance is `pitch * 3 + 600` (Client-TS camFollow) -- here
     * `pitch * pitch_distance + rest`, both stated by the profile -- later
     * scaled by a viewport-height zoom (`* viewportZoom / 256`,
     * client.method2068). `world_cam_zoom` is that 600 -- `[camera] rest=` --
     * moved by the wheel inside the `zoom_closest=`..`zoom_furthest=` band.
     *
     * `[camera] viewport_zoom=no` skips the interpolation: it is a later
     * client's way of zooming, and a revision that says it is the 2004 client
     * has said its camera has none. `rest=600` + `viewport_zoom=no` is then
     * Client-TS's expression exactly.
     *
     * The revision states that key and the player does not: a wheel switched
     * on in the settings moves world_cam_zoom inside its band and leaves this
     * term exactly as the revision left it.
     */
    {
        struct WorldCameraLimits const limits = app_world_camera_limits(app);
        distance = WorldCameraOrbit_Distance(&limits, pitch);
    }
    /* Look-at height: the reference samples the ground under the ACTOR (not
     * under the eased anchor), takes the minimum over its footprint, then
     * drops 8, then the camera's own 50 — client.method1605:
     *   var14 = method1569(wv, actor.x, actor.z, level, footprintSize) - 8
     *   field753 = var14 - field999          (field999 defaults to 50)
     * method1569 degenerates to a single method1812 sample for a size-1
     * footprint, which the local player always has. */
    target_y =
        aboard_y_valid
            ? aboard_y
            : app_world_height(app, target_x, target_z, player->grid_position.level) - 8 - 50;
    target_x = (int)app->world_views.frame->orbit.anchor_x;
    target_z = (int)app->world_views.frame->orbit.anchor_z;
    if( CamScript_FreeCamera(&app->cam_script) )
        target_y = CamScript_OculusFocusY(&app->cam_script, target_y);

    if( CamScript_IsScripted(&app->cam_script) )
    {
        struct ToriDraw_Position eye;

        ToriRS_OrbitCameraEye(target_x, target_y, target_z, pitch, yaw, distance, &eye);
        app_world_camera_smooth_reset_step(app, &eye, pitch, yaw);
        return;
    }
    ToriRS_OrbitCameraEye(
        target_x, target_y, target_z, pitch, yaw, distance, &app->world_views.frame->world_camera_pos);
    app->world_views.frame->world_camera.pitch = pitch;
    app->world_views.frame->world_camera.yaw = yaw;

    /* TORIRS_ORBIT_DEBUG: the anchor's residual against the player it follows.
     * The orbit point is what the whole eye is built around, and an offset one
     * is invisible in a still frame — it only shows as the model swinging in a
     * circle while you rotate, which reads as "the camera orbits the tile".
     * This is the number that says whether it does: settled, it must go to 0. */
    if( torirs_env_orbit_debug() )
        TORIRS_LOG(
            "orbit: anchor=(%.3f,%.3f) player=(%d,%d) residual=(%.3f,%.3f) "
            "pitch=%d yaw=%d dist=%d look_y=%d eye=(%d,%d,%d)\n",
            (double)app->world_views.frame->orbit.anchor_x,
            (double)app->world_views.frame->orbit.anchor_z,
            (int)player->draw_position.x,
            (int)player->draw_position.z,
            (double)((float)(int)player->draw_position.x - app->world_views.frame->orbit.anchor_x),
            (double)((float)(int)player->draw_position.z - app->world_views.frame->orbit.anchor_z),
            pitch,
            yaw,
            distance,
            target_y,
            app->world_views.frame->world_camera_pos.x,
            app->world_views.frame->world_camera_pos.y,
            app->world_views.frame->world_camera_pos.z);
}
