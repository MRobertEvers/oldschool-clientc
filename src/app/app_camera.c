/*
 * The world camera: key orbit, middle-button orbit, wheel zoom, the cinema camera, and the follow camera.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

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
app_cinema_ease(
    int current,
    int target,
    int rate,
    int rate2);
static int
app_world_cam_dist_zoom(struct App* app);
static struct WorldCameraLimits
app_world_camera_limits(struct App const* app);

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
    int direction_x = ToriDraw_Sin(app->world_camera.yaw);
    int direction_z = ToriDraw_Cos(app->world_camera.yaw);
    app->world_camera_pos.x -= (direction_x * amount) >> 16;
    app->world_camera_pos.z += (direction_z * amount) >> 16;
}

static void
app_camera_move_left(
    struct App* app,
    int amount)
{
    int direction_x = ToriDraw_Cos(app->world_camera.yaw);
    int direction_z = ToriDraw_Sin(app->world_camera.yaw);
    app->world_camera_pos.x += (direction_x * amount) >> 16;
    app->world_camera_pos.z += (direction_z * amount) >> 16;
}

/* Optional developer camera keys. All bindings come from `[debug:hotkeys]` and
 * are off when omitted. Arrow yaw/pitch remains ordinary game camera input. */
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
            app->world_active,
            app->world_view_valid,
            app->chat_input_active,
            app->chat.social_input_open,
            app->chat.dialog_input_open,
            app_iface_text_input_focused(app));

    /* No key_target gating: the reference broadcasts every key to onKey
     * scripts AND moves the camera in the same frame; there is no focused
     * text-input concept to defer to yet. The viewport still has to be on
     * screen — with no world drawn these keys belong to the interface. */
    if( !app->world_active || !app_world_viewport_component_live(app) )
    {
        /* Do not leave held arrow ownership feeding the follow-camera tick
         * after its viewport became display:none. */
        app->cam_key_left = 0;
        app->cam_key_right = 0;
        app->cam_key_up = 0;
        app->cam_key_down = 0;
        return;
    }
    /* Suppressed while any text input has focus (the chat line, a modal
     * prompt, a panel's search box), so typing never flies the camera -- and
     * while the catalog's model view holds focus, whose WASD/EF orbit the
     * preview, not the world. */
    if( app_text_input_focused(app) || app_modelview_focused(app) )
        return;
    (void)out;

    if( app_debug_key_held(app, input, APP_DEBUG_HOTKEY_CAMERA_FORWARD) )
        app_camera_move_forward(app, move);
    if( app_debug_key_held(app, input, APP_DEBUG_HOTKEY_CAMERA_BACK) )
        app_camera_move_forward(app, -move);
    if( app_debug_key_held(app, input, APP_DEBUG_HOTKEY_CAMERA_LEFT) )
        app_camera_move_left(app, -move);
    if( app_debug_key_held(app, input, APP_DEBUG_HOTKEY_CAMERA_RIGHT) )
        app_camera_move_left(app, move);
    if( app_debug_key_held(app, input, APP_DEBUG_HOTKEY_CAMERA_UP) )
        app->world_camera_pos.y -= move;
    if( app_debug_key_held(app, input, APP_DEBUG_HOTKEY_CAMERA_DOWN) )
        app->world_camera_pos.y += move;
    /* Arrows drive the orbit camera (reference keyHeld[1..4]); the follow
     * step consumes these next frame. When the follow cam is off (offline or
     * scripted) fall back to the free-cam direct rotate. */
    if( app->revconfig_profile.camera.controls & REVCONFIG_CAMERA_CONTROL_ARROW_KEYS )
    {
        app->cam_key_left = LibToriRS_Input_IsKeyHeld(input, TORIRSK_LEFT);
        app->cam_key_right = LibToriRS_Input_IsKeyHeld(input, TORIRSK_RIGHT);
        app->cam_key_up = LibToriRS_Input_IsKeyHeld(input, TORIRSK_UP);
        app->cam_key_down = LibToriRS_Input_IsKeyHeld(input, TORIRSK_DOWN);
    }
    else
    {
        /* Cleared, not merely left alone: the follow step reads these every
         * cycle and a latch held from before the profile said no would keep
         * accelerating the yaw for as long as the key stayed down. */
        app->cam_key_left = 0;
        app->cam_key_right = 0;
        app->cam_key_up = 0;
        app->cam_key_down = 0;
    }
    if( app->cam_script.scripted || !app->net || app->camera_unlocked )
    {
        if( app->cam_key_left )
            app->world_camera.yaw = ToriDraw_AddAngle(app->world_camera.yaw, rotate);
        if( app->cam_key_right )
            app->world_camera.yaw = ToriDraw_AddAngle(app->world_camera.yaw, -rotate);
        if( app->cam_key_up )
            app->world_camera.pitch = ToriDraw_AddAngle(app->world_camera.pitch, rotate);
        if( app->cam_key_down )
            app->world_camera.pitch = ToriDraw_AddAngle(app->world_camera.pitch, -rotate);
    }

    /* Unlock / relock the camera. Unlocked, the follow update stands down
     * (app_world_camera_follow) and the configured movement bindings fly the
     * eye while arrows rotate it — the debug flight that only worked offline,
     * available online.
     * Relocking snaps back through the follow's own teleport path. */
    if( app_debug_key_down(app, input, APP_DEBUG_HOTKEY_CAMERA_UNLOCK) )
    {
        app->camera_unlocked = !app->camera_unlocked;
        TORIRS_LOG("camera: %s\n", app->camera_unlocked ? "UNLOCKED" : "locked");
        app->need_redraw = 1;
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
            app->need_redraw = 1;
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
        follow_cam ? app->orbit.yaw : app->world_camera.yaw,
        follow_cam ? app->orbit.pitch : app->world_camera.pitch,
        app->world_cam_zoom,
        app->world_camera_pos.x,
        app->world_camera_pos.y,
        app->world_camera_pos.z);
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

    if( !app->world_active || !app_world_viewport_component_live(app) )
    {
        app->cam_mmb_active = 0;
        return;
    }
    /* The same split app_world_camera_keys makes: online and out of a cutscene
     * the orbit follow cam owns the angles, otherwise the free camera does. */
    follow_cam = app->net && !app->cam_script.scripted;

    /* `controls=` is the revision's answer for a MOUSE; app->touch_camera is
     * the platform's answer for a FINGER, and the two are different questions.
     * @see App.touch_camera. */
    if( (app->revconfig_profile.camera.controls & REVCONFIG_CAMERA_CONTROL_MMB) ||
        app->touch_camera )
    {
        /* Only the press has to land on the scene; once latched the drag keeps
         * the pointer until release, so sweeping over the sidebar mid-rotate
         * does not stall the camera. */
        if( !app->cam_mmb_active && input->curr.mouse_button_down[TORIRSM_MIDDLE] &&
            !app->interact.minimenu.visible && app_world_mouse_gate(app, mouse_x, mouse_y) )
        {
            app->cam_mmb_active = 1;
            app->cam_mmb_x = mouse_x;
            app->cam_mmb_y = mouse_y;
        }

        if( app->cam_mmb_active )
        {
            int dx = mouse_x - app->cam_mmb_x;
            int dy = mouse_y - app->cam_mmb_y;

            app->cam_mmb_x = mouse_x;
            app->cam_mmb_y = mouse_y;

            if( dx != 0 || dy != 0 )
            {
                if( follow_cam )
                {
                    /* The key path eases through a velocity; a drag is already
                     * a position delta, so it writes the angle and zeroes the
                     * velocity rather than fighting the decay next frame. */
                    app->orbit.yaw = (app->orbit.yaw - dx * APP_WORLD_MMB_YAW_PER_PX) & 0x7ff;
                    app->orbit.pitch = app_world_clamp_pitch(
                        app, app->orbit.pitch + dy * APP_WORLD_MMB_PITCH_PER_PX);
                    app->orbit.yaw_velocity = 0;
                    app->orbit.pitch_velocity = 0;
                }
                else
                {
                    app->world_camera.yaw =
                        ToriDraw_AddAngle(app->world_camera.yaw, -dx * APP_WORLD_MMB_YAW_PER_PX);
                    app->world_camera.pitch =
                        ToriDraw_AddAngle(app->world_camera.pitch, dy * APP_WORLD_MMB_PITCH_PER_PX);
                }
                app_debug_log_camera(app, "rotate", follow_cam);
                app->need_redraw = 1;
            }
        }

        if( !LibToriRS_Input_IsMouseHeld(input, TORIRSM_MIDDLE) ||
            input->curr.mouse_button_up[TORIRSM_MIDDLE] )
            app->cam_mmb_active = 0;
    }
    else
        app->cam_mmb_active = 0;

    /* Wheel up (positive) zooms in. Gated on the pointer being over the scene
     * and on no widget having already taken this notch, so a wheel over a
     * scroll pane drawn across the viewport still belongs to that pane —
     * app_world_mouse_gate alone only rejects *interactive* nodes, and an IF1
     * scroll layer is pass-through. */
    if( input->curr.mouse_wheel_y != 0 && !out->wheel_consumed && !app->interact.minimenu.visible &&
        /* The chrome's claim is checked HERE, not inferred from consumed
         * flags: this runs long after the overlay handled input, when
         * input_frame_consumed is 1 on every frame. A wheel over a panel or
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
            app->world_cam_zoom = RevConfigProfile_CameraClampZoom(
                &app->revconfig_profile,
                app->world_cam_zoom -
                    input->curr.mouse_wheel_y * app->revconfig_profile.camera.wheel_step);
        }
        else
            return;
        app_debug_log_camera(app, "zoom", follow_cam);
        app->need_redraw = 1;
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

    if( app->aboard_view != WORLDVIEW_ROOT && Wevs_IsLive(&app->wevs, app->aboard_view) )
        return Wevs_Get(&app->wevs, app->aboard_view)->parent_level;
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

/* Pitch/yaw that point the eye at the look-at target. Reference cinemaCamera
 * (Client-TS 3542): note the yaw multiplier is *negative* 325.949 — the scene
 * turns the opposite way to the mathematical angle. */
static void
app_cinema_angles(
    struct App* app,
    int* out_pitch,
    int* out_yaw)
{
    int tx, ty, tz, dx, dy, dz, distance, pitch;

    app_cinema_point(
        app,
        app->cam_script.look_lx,
        app->cam_script.look_lz,
        app->cam_script.look_height,
        &tx,
        &ty,
        &tz);

    dx = tx - app->world_camera_pos.x;
    dy = ty - app->world_camera_pos.y;
    dz = tz - app->world_camera_pos.z;
    distance = (int)sqrt((double)dx * dx + (double)dz * dz);

    pitch = (int)(atan2((double)dy, (double)distance) * 325.949) & 0x7ff;
    /* The scripted camera tips no further than the player's own may: the
     * profile's range, not a second copy of the reference's numbers. */
    pitch = app_world_clamp_pitch(app, pitch);

    *out_pitch = pitch;
    *out_yaw = (int)(atan2((double)dx, (double)dz) * -325.949) & 0x7ff;
}

void
App_CinemaCameraSnapPosition(struct App* app)
{
    app_cinema_point(
        app,
        app->cam_script.move_lx,
        app->cam_script.move_lz,
        app->cam_script.move_height,
        &app->world_camera_pos.x,
        &app->world_camera_pos.y,
        &app->world_camera_pos.z);
}

void
App_CinemaCameraSnapAngle(struct App* app)
{
    app_cinema_angles(app, &app->world_camera.pitch, &app->world_camera.yaw);
}

/* Ease one axis toward its target: a flat `rate` plus `rate2`/1000 of what is
 * left, never overshooting. */
static int
app_cinema_ease(
    int current,
    int target,
    int rate,
    int rate2)
{
    if( current < target )
    {
        current += rate + (target - current) * rate2 / 1000;
        if( current > target )
            current = target;
    }
    else if( current > target )
    {
        current -= rate + (current - target) * rate2 / 1000;
        if( current < target )
            current = target;
    }
    return current;
}

/* Reference cinemaCamera (Client-TS 3542). Runs every frame the script is up:
 * the camera walks toward the move-to point and turns toward the look-at
 * point, so a rate2 under 100 glides instead of cutting. */
void
app_world_camera_cinema(struct App* app)
{
    int tx, ty, tz, pitch, yaw, delta;
    int rate = app->cam_script.move_rate;
    int rate2 = app->cam_script.move_rate2;

    if( !app->cam_script.scripted || !app->world )
        return;

    app_cinema_point(
        app,
        app->cam_script.move_lx,
        app->cam_script.move_lz,
        app->cam_script.move_height,
        &tx,
        &ty,
        &tz);

    app->world_camera_pos.x = app_cinema_ease(app->world_camera_pos.x, tx, rate, rate2);
    app->world_camera_pos.y = app_cinema_ease(app->world_camera_pos.y, ty, rate, rate2);
    app->world_camera_pos.z = app_cinema_ease(app->world_camera_pos.z, tz, rate, rate2);

    app_cinema_angles(app, &pitch, &yaw);
    rate = app->cam_script.look_rate;
    rate2 = app->cam_script.look_rate2;
    app->world_camera.pitch = app_cinema_ease(app->world_camera.pitch, pitch, rate, rate2);

    /* Yaw wraps, so ease along the short way round and stop when the sign of
     * the remaining turn flips — the linear helper cannot see past the seam. */
    delta = yaw - app->world_camera.yaw;
    if( delta > 1024 )
        delta -= 2048;
    else if( delta < -1024 )
        delta += 2048;

    if( delta > 0 )
        app->world_camera.yaw = (app->world_camera.yaw + rate + delta * rate2 / 1000) & 0x7ff;
    else if( delta < 0 )
        app->world_camera.yaw = (app->world_camera.yaw - rate - -delta * rate2 / 1000) & 0x7ff;

    if( delta != 0 )
    {
        int remaining = yaw - app->world_camera.yaw;
        if( remaining > 1024 )
            remaining -= 2048;
        else if( remaining < -1024 )
            remaining += 2048;
        if( (remaining < 0 && delta > 0) || (remaining > 0 && delta < 0) )
            app->world_camera.yaw = yaw;
    }

    /* TORIRS_CAM_DEBUG=1: trace the scripted camera. */
    if( getenv("TORIRS_CAM_DEBUG") )
        TORIRS_LOG(
            "cam eye=%d,%d,%d pitch=%d yaw=%d -> move=%d,%d h=%d look=%d,%d h=%d "
            "shake=%d%d%d%d%d\n",
            app->world_camera_pos.x,
            app->world_camera_pos.y,
            app->world_camera_pos.z,
            app->world_camera.pitch,
            app->world_camera.yaw,
            app->cam_script.move_lx,
            app->cam_script.move_lz,
            app->cam_script.move_height,
            app->cam_script.look_lx,
            app->cam_script.look_lz,
            app->cam_script.look_height,
            app->cam_script.shake[0],
            app->cam_script.shake[1],
            app->cam_script.shake[2],
            app->cam_script.shake[3],
            app->cam_script.shake[4]);

    app->need_redraw = 1;
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
    d = (app->world_view_valid ? app->world_emit_desc.h : 334) - 334;
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
    limits.pitch_distance = app->revconfig_profile.camera.pitch_distance;
    limits.rest_zoom = app->world_cam_zoom;
    limits.viewport_zoom_256 =
        app->revconfig_profile.camera.viewport_zoom ? app_world_cam_dist_zoom((struct App*)app) : 0;
    limits.distance_scale_percent = app->revconfig_profile.camera.distance_scale;
    limits.near_plane_z = app->world_camera.near_plane_z;
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
    if( app->camera_unlocked )
        return;
    if( app->cam_script.scripted || !app->net )
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
            app->orbit.yaw = cam_yaw & 0x7ff;
            app->orbit.yaw_velocity = 0;
            if( cam_pitch >= 0 )
            {
                app->orbit.pitch = app_world_clamp_pitch(app, cam_pitch);
                app->orbit.pitch_velocity = 0;
            }
            if( cam_zoom > 0 )
                /* A percentage of THIS revision's rest, not of the reference
                 * 600. The server is saying "this much closer than normal",
                 * and normal is wherever this camera rests -- reading it
                 * against a constant makes the same packet mean two different
                 * views on two lanes. */
                app->world_cam_zoom = RevConfigProfile_CameraClampZoom(
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
    if( app->aboard_view != WORLDVIEW_ROOT && player->view_placement.view_id == app->aboard_view &&
        app_wev_actor_root_fine(app, &player->view_placement, &target_x, &target_z) )
    {
        struct Wev* wev = Wevs_Get(&app->wevs, app->aboard_view);
        struct Worldview* view = WorldviewRegistry_Get(&app->worldviews, app->aboard_view);
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
    Wev_SmoothCameraFocus(&app->orbit.anchor_x, &app->orbit.anchor_z, target_x, target_z);

    /* Arrow keys -> yaw/pitch velocity. @see WorldCameraOrbit_StepAngles. */
    {
        struct WorldCameraLimits const limits = app_world_camera_limits(app);
        struct WorldCameraKeys const keys = { .left = app->cam_key_left != 0,
                                              .right = app->cam_key_right != 0,
                                              .up = app->cam_key_up != 0,
                                              .down = app->cam_key_down != 0 };

        WorldCameraOrbit_StepAngles(&app->orbit, &keys, &limits);
    }

    /* Terrain pitch clamp: scan the 9x9 tile block around the anchor for
     * ground higher than the anchor's; raise the minimum pitch so the eye
     * clears it. cameraPitchClamp is 24.8 fixed (clamp/256 = pitch units). */
    {
        struct Heightmap* hm = app->world ? app->world->heightmap : NULL;
        int level = player->grid_position.level;
        int orbit_ix = (int)app->orbit.anchor_x;
        int orbit_iz = (int)app->orbit.anchor_z;
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
        WorldCameraOrbit_EaseTerrainClamp(&app->orbit, &limits, max_y * 192 / 256);
    }

    pitch = WorldCameraOrbit_EffectivePitch(&app->orbit);
    yaw = app->orbit.yaw & 0x7ff;
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
    target_x = (int)app->orbit.anchor_x;
    target_z = (int)app->orbit.anchor_z;

    ToriRS_OrbitCameraEye(
        target_x, target_y, target_z, pitch, yaw, distance, &app->world_camera_pos);
    app->world_camera.pitch = pitch;
    app->world_camera.yaw = yaw;

    /* TORIRS_ORBIT_DEBUG: the anchor's residual against the player it follows.
     * The orbit point is what the whole eye is built around, and an offset one
     * is invisible in a still frame — it only shows as the model swinging in a
     * circle while you rotate, which reads as "the camera orbits the tile".
     * This is the number that says whether it does: settled, it must go to 0. */
    if( torirs_env_orbit_debug() )
        TORIRS_LOG(
            "orbit: anchor=(%.3f,%.3f) player=(%d,%d) residual=(%.3f,%.3f) "
            "pitch=%d yaw=%d dist=%d look_y=%d eye=(%d,%d,%d)\n",
            (double)app->orbit.anchor_x,
            (double)app->orbit.anchor_z,
            (int)player->draw_position.x,
            (int)player->draw_position.z,
            (double)((float)(int)player->draw_position.x - app->orbit.anchor_x),
            (double)((float)(int)player->draw_position.z - app->orbit.anchor_z),
            pitch,
            yaw,
            distance,
            target_y,
            app->world_camera_pos.x,
            app->world_camera_pos.y,
            app->world_camera_pos.z);
}
