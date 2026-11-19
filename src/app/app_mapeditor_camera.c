/*
 * The map editor's camera: its free camera held over the loaded scene, the
 * Unity-style scheme and the grab pan, and its keys' flight and turns.
 *
 * app_camera.c keeps the game's cameras and asks this unit at three points:
 * the camera keys' flight (app_map_editor_camera_flight), their turn keys
 * (app_map_editor_camera_turn_keys) and the mouse
 * (app_map_editor_camera_mouse). The hotkey dispatcher runs the two gates
 * (app_camera_unity_gate, app_camera_grab_gate). The latches are the map
 * editor's record's (editor/map_editor_camera.h).
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
    if( !app->map_editor || !app->world || !app->world_views.frame )
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
    level = app->map_editor->editor_panel.ui &&
                    app->map_editor->editor_panel.ui->tools.edit_level >= 0
                ? app->map_editor->editor_panel.ui->tools.edit_level
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
 * app_editor_hotkeys.c keeps its), in the map editor's record. A press latches
 * over the world only (the panels, the chrome and an open minimenu keep
 * their pointer); the drag then follows the pointer anywhere until release.
 * Every move ends in app_camera_keep_over_scene: the scheme never leaves the
 * loaded squares.
 */
/* The scheme's latches: struct MapEditorCameraUnity (editor/map_editor_camera.h),
 * App.map_editor->camera.unity. */

/* The grab pan (docs/MAPED_HOTKEYS.md "Pan (Space)"): Space held over the
 * World, then a left-drag, under either scheme -- or Alt+Ctrl+left-drag
 * (Option+Cmd) under the Unity scheme. One latch for both: the press
 * starts it over the world, the drag then keeps the pointer until the left
 * button comes up, whether or not the key is still down. */
/* The grab pan's latch: struct MapEditorCameraGrab, App.map_editor->camera.grab. */

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
    ui = app->map_editor->editor_panel.ui;
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
    struct EditorUi const* ui = app->map_editor->editor_panel.ui;
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
    else if( Editor_PanelHoverTile(&app->map_editor->editor_panel, app, &scene_x, &scene_z) )
    {
        tile_x = scene_x + app->world->_base_tile_x;
        tile_z = scene_z + app->world->_base_tile_z;
        level = Editor_PanelEditLevel(&app->map_editor->editor_panel, app);
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
    if( app->map_editor->camera.grab.active && !LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT) )
        app->map_editor->camera.grab.active = 0;
    if( app->map_editor->camera.grab.active )
        ToriRS_InputClaimsTakeFrame(&app->input_claims);
    app->map_editor->camera.grab.unity_chord_over_world = 0;
    if( !app_camera_unity_on(app) )
    {
        app->map_editor->camera.unity.look_active = 0;
        app->map_editor->camera.unity.pan_active = 0;
        app->map_editor->camera.unity.orbit_active = 0;
        return;
    }
    /* Releases first, so a press and a release in one frame cancel. */
    if( app->map_editor->camera.unity.orbit_active && !LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT) )
        app->map_editor->camera.unity.orbit_active = 0;
    if( app->map_editor->camera.unity.look_active && !LibToriRS_Input_IsMouseHeld(input, TORIRSM_RIGHT) )
        app->map_editor->camera.unity.look_active = 0;
    if( app->map_editor->camera.unity.pan_active && !LibToriRS_Input_IsMouseHeld(input, TORIRSM_MIDDLE) )
        app->map_editor->camera.unity.pan_active = 0;

    app->map_editor->camera.grab.unity_chord_over_world = alt && ctrl && !app->world_views.frame->minimenu->visible
                                        && app_camera_grab_free(app) && app_camera_unity_over_world(app, mx, my);
    /* Alt+Ctrl+left-drag (Option+Cmd: Cmd is reported as Ctrl) is Unity's
     * one-button pan, checked before Alt's orbit so the orbit never sees it. */
    if( !app->map_editor->camera.grab.active && !app->map_editor->camera.unity.orbit_active && alt && ctrl && input->curr.mouse_button_down[TORIRSM_LEFT]
        && !app->world_views.frame->minimenu->visible && app_camera_grab_free(app) && app_camera_unity_over_world(app, mx, my) )
    {
        app->map_editor->camera.grab.active = 1;
        app->map_editor->camera.grab.last_x = mx;
        app->map_editor->camera.grab.last_y = my;
    }
    if( !app->map_editor->camera.grab.active && !app->map_editor->camera.unity.orbit_active && alt && !ctrl && input->curr.mouse_button_down[TORIRSM_LEFT]
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
            app->map_editor->camera.unity.orbit_active = 1;
            app->map_editor->camera.unity.pivot_x = px;
            app->map_editor->camera.unity.pivot_y = py;
            app->map_editor->camera.unity.pivot_z = pz;
            app->map_editor->camera.unity.pivot_dist = d < 256 ? 256 : (int)d;
            app->map_editor->camera.unity.yaw = view->world_camera.yaw & 0x7ff;
            app->map_editor->camera.unity.pitch = app_camera_unity_clamp_pitch(view->world_camera.pitch);
            app->map_editor->camera.unity.last_x = mx;
            app->map_editor->camera.unity.last_y = my;
        }
    }
    if( !app->map_editor->camera.unity.look_active && input->curr.mouse_button_down[TORIRSM_RIGHT]
        && app_camera_unity_over_world(app, mx, my) )
    {
        app->map_editor->camera.unity.look_active = 1;
        app->map_editor->camera.unity.last_x = mx;
        app->map_editor->camera.unity.last_y = my;
        app->map_editor->camera.unity.travel = 0;
    }
    if( !app->map_editor->camera.unity.pan_active && input->curr.mouse_button_down[TORIRSM_MIDDLE]
        && !app->world_views.frame->minimenu->visible && app_camera_unity_over_world(app, mx, my) )
    {
        app->map_editor->camera.unity.pan_active = 1;
        app->map_editor->camera.unity.last_x = mx;
        app->map_editor->camera.unity.last_y = my;
    }
    /* An orbit or pan owns the frame's pointer from its press: the editor's
     * click (which runs after this) must not paint, select or stroke, and the
     * middle press must not start the default scheme's rotate. */
    if( app->map_editor->camera.unity.orbit_active || app->map_editor->camera.unity.pan_active || app->map_editor->camera.grab.active )
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
    if( pan && !app->map_editor->camera.grab.space_held )
    {
        app->map_editor->camera.grab.space_held = 1;
        app->map_editor->camera.grab.space_dragged = 0;
    }
    else if( !pan && app->map_editor->camera.grab.space_held )
    {
        struct EditorUi* ui = app->map_editor->editor_panel.ui;
        app->map_editor->camera.grab.space_held = 0;
        if( !app->map_editor->camera.grab.space_dragged && ui->tools.tool == EDITOR_TOOL_LOC_PLACE )
            Editor_UiStatus(ui, "Space pans the camera now (hold it and drag); Return or a click places");
    }
    if( !app->map_editor->camera.grab.active && pan && input->curr.mouse_button_down[TORIRSM_LEFT]
        && !app->world_views.frame->minimenu->visible && app_camera_grab_free(app) && app_camera_unity_over_world(app, mx, my) )
    {
        app->map_editor->camera.grab.active = 1;
        app->map_editor->camera.grab.space_dragged = 1;
        app->map_editor->camera.grab.last_x = mx;
        app->map_editor->camera.grab.last_y = my;
    }
    /* The editor's click runs after this: a press that started a grab must
     * not paint, select or stroke. */
    if( app->map_editor->camera.grab.active )
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
    if( app->map_editor->camera.grab.active || app->map_editor->camera.unity.pan_active )
        return 1;
    /* Held, not yet pressed: the cursor says what a press will do. Space is
     * the hotkeys' answer (wherever it is rebound; over the World only, and
     * nothing while a text field holds the keyboard). */
    return app_editor_hotkeys_active(app, EDITOR_HK_CAM_PAN) || app->map_editor->camera.grab.unity_chord_over_world;
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
    int const level = app->map_editor->editor_panel.ui->tools.edit_level >= 0
                          ? app->map_editor->editor_panel.ui->tools.edit_level
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
    int const dx = mx - app->map_editor->camera.unity.last_x;
    int const dy = my - app->map_editor->camera.unity.last_y;

    if( app->map_editor->camera.unity.orbit_active )
    {
        struct EditorViewCamera pose;
        if( dx || dy )
        {
            app->map_editor->camera.unity.yaw = (app->map_editor->camera.unity.yaw - dx * APP_WORLD_MMB_YAW_PER_PX) & 0x7ff;
            app->map_editor->camera.unity.pitch = app_camera_unity_clamp_pitch(app->map_editor->camera.unity.pitch + dy * APP_WORLD_MMB_PITCH_PER_PX);
        }
        memset(&pose, 0, sizeof(pose));
        pose.x = app->map_editor->camera.unity.pivot_x;
        pose.z = app->map_editor->camera.unity.pivot_z;
        pose.yaw = app->map_editor->camera.unity.yaw;
        pose.pitch = app->map_editor->camera.unity.pitch;
        pose.zoom = app->map_editor->camera.unity.pivot_dist;
        EditorView_PoseEye(&pose, 0, 0, app->map_editor->camera.unity.pivot_y, &view->world_camera_pos);
        view->world_camera.yaw = app->map_editor->camera.unity.yaw;
        view->world_camera.pitch = app->map_editor->camera.unity.pitch;
        view->camera_unlocked = 1;
        if( dx || dy )
            RedrawRequest_Raise(&app->redraw_request);
    }
    else if( app->map_editor->camera.unity.look_active && (dx || dy) )
    {
        app->map_editor->camera.unity.travel += (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        /* A right click stays a click (the minimenu opened on the press);
         * a drag past a few px is a look, and the menu goes. */
        if( app->map_editor->camera.unity.travel > 4 )
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
    else if( app->map_editor->camera.unity.pan_active && (dx || dy) )
        app_camera_pan_ground(app, dx, dy);
    app->map_editor->camera.unity.last_x = mx;
    app->map_editor->camera.unity.last_y = my;

    /* The wheel zooms TOWARD the tile under the cursor: the eye slides along
     * the line to that tile's ground point, a sixth of the way per notch in,
     * back out along it per notch out, never closer than two tiles. */
    if( input->curr.mouse_wheel_y != 0 && !out->wheel_consumed && !view->minimenu->visible
        && app_camera_unity_over_world(app, mx, my) )
    {
        int scene_x;
        int scene_z;
        if( Editor_PanelHoverTile(&app->map_editor->editor_panel, app, &scene_x, &scene_z) )
        {
            int const level = Editor_PanelEditLevel(&app->map_editor->editor_panel, app);
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
app_map_editor_camera_flight(
    struct App* app,
    int move)
{
    assert(app);
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
}

int
app_map_editor_camera_turn_keys(struct App* app)
{
    assert(app);
    if( !app_editor_hotkeys_owns_camera(app) )
        return 0;
    /* The editor's turn / tilt actions: the bare arrows, always (the
     * nudges are Ctrl+arrows), and rebindable. */
    app->world_views.frame->cam_key_left = app_editor_hotkeys_active(app, EDITOR_HK_CAM_TURN_LEFT);
    app->world_views.frame->cam_key_right = app_editor_hotkeys_active(app, EDITOR_HK_CAM_TURN_RIGHT);
    app->world_views.frame->cam_key_up = app_editor_hotkeys_active(app, EDITOR_HK_CAM_TILT_UP);
    app->world_views.frame->cam_key_down = app_editor_hotkeys_active(app, EDITOR_HK_CAM_TILT_DOWN);
    return 1;
}

int
app_map_editor_camera_mouse(
    struct App* app,
    struct LibToriRS_Input* input,
    struct UIInteractOut const* out,
    int follow_cam)
{
    int const mouse_x = input->curr.mouse_x;
    int const mouse_y = input->curr.mouse_y;

    assert(app);
    assert(input);
    /* No editor, no latch: nothing below can have been armed. */
    if( !app->map_editor )
        return 0;
    /* The grab pan (Space, or Unity's Alt+Ctrl) under either scheme: the
     * left-drag moves the ground with the pointer, then the scheme's own
     * handling runs as usual (its wheel, its keys). A grab whose button came
     * up, whose editor closed or whose camera became the follow cam ends. */
    if( app->map_editor->camera.grab.active )
    {
        if( follow_cam || !app_editor_hotkeys_owns_camera(app) || !LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT) )
            app->map_editor->camera.grab.active = 0;
        else
        {
            int const dx = mouse_x - app->map_editor->camera.grab.last_x;
            int const dy = mouse_y - app->map_editor->camera.grab.last_y;
            if( dx || dy )
            {
                app_camera_pan_ground(app, dx, dy);
                app_camera_keep_over_scene(app);
                app_debug_log_camera(app, "grab-pan", 0);
            }
            app->map_editor->camera.grab.last_x = mouse_x;
            app->map_editor->camera.grab.last_y = mouse_y;
        }
    }

    /* The editor's Unity-style scheme takes the mouse whole: its own drags
     * and its zoom-to-cursor wheel, none of the default scheme's after it. */
    if( !follow_cam && app_camera_unity_on(app) )
    {
        app->world_views.frame->cam_mmb_active = 0;
        app_camera_unity_mouse(app, input, out);
        return 1;
    }
    return 0;
}

