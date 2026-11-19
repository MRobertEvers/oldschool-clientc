/*
 * The free camera's keys: a side-app (sideapp/side_app.h).
 *
 * Six held keys fly the world camera's eye -- `camera_forward` / `_back` /
 * `_left` / `_right` along the ground under the camera's yaw, `camera_up` /
 * `_down` straight up and down -- and `camera_unlock`, pressed, unlocks or
 * relocks it. Unlocked, the follow update stands down (app_world_camera_follow)
 * and the flight keys fly the eye while the arrows rotate it -- the debug
 * flight that only worked offline, available online. Relocking snaps back
 * through the follow's own teleport path. The camera's position and its
 * unlocked flag are shared camera state (struct WorldViews): the map editor,
 * the mouse and the plugins move them too. This tool owns only its keys and
 * keeps no state.
 *
 * Both are delivered inside the camera keys (app_world_camera_keys), once per
 * view the keys steer, past that point's gates: the world loaded and its
 * viewport live, the catalog's model view not focused, and no text input
 * focused unless the map editor owns the camera; the keys with CTRL, not one
 * the map editor claimed this frame. The flight is the camera-flight point:
 * with no text input focused at all (it reads raw keys, so it keeps the
 * chrome-focus gate the editor's own flight does not), before the editor's
 * flight and the clamp over the scene. The unlock is the camera-keys point,
 * after the arrows have turned the camera, first there (app_tools.c's order).
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Private to this unit, declared up front so definition order is free. */
static void
app_tool_free_camera_key(
    void* self,
    struct SideAppHost* host,
    int action,
    char const* args);

enum
{
    APP_TOOL_FREE_CAMERA_FORWARD,
    APP_TOOL_FREE_CAMERA_BACK,
    APP_TOOL_FREE_CAMERA_LEFT,
    APP_TOOL_FREE_CAMERA_RIGHT,
    APP_TOOL_FREE_CAMERA_UP,
    APP_TOOL_FREE_CAMERA_DOWN,
    APP_TOOL_FREE_CAMERA_UNLOCK
};

#define APP_TOOL_FREE_CAMERA_FLY(n) { (n), SIDE_APP_PHASE_CAMERA_FLIGHT, SIDE_APP_GATE_CTRL, SIDE_APP_KEY_HELD, 0 }
static struct SideAppAction const k_app_tool_free_camera_actions[] = {
    [APP_TOOL_FREE_CAMERA_FORWARD] = APP_TOOL_FREE_CAMERA_FLY("camera_forward"),
    [APP_TOOL_FREE_CAMERA_BACK] = APP_TOOL_FREE_CAMERA_FLY("camera_back"),
    [APP_TOOL_FREE_CAMERA_LEFT] = APP_TOOL_FREE_CAMERA_FLY("camera_left"),
    [APP_TOOL_FREE_CAMERA_RIGHT] = APP_TOOL_FREE_CAMERA_FLY("camera_right"),
    [APP_TOOL_FREE_CAMERA_UP] = APP_TOOL_FREE_CAMERA_FLY("camera_up"),
    [APP_TOOL_FREE_CAMERA_DOWN] = APP_TOOL_FREE_CAMERA_FLY("camera_down"),
    [APP_TOOL_FREE_CAMERA_UNLOCK] = { "camera_unlock", SIDE_APP_PHASE_CAMERA_KEYS, SIDE_APP_GATE_CTRL,
                                      SIDE_APP_KEY_PRESS, 0 },
};
#undef APP_TOOL_FREE_CAMERA_FLY

static struct SideAppHooks const k_app_tool_free_camera_hooks = {
    .key = app_tool_free_camera_key,
};

void
app_tool_free_camera_register(struct App* app)
{
    assert(app);
    assert(app->side_apps);
    SideApps_Register(
        app->side_apps,
        "free_camera",
        NULL,
        &k_app_tool_free_camera_hooks,
        k_app_tool_free_camera_actions,
        (int)(sizeof(k_app_tool_free_camera_actions) / sizeof(k_app_tool_free_camera_actions[0])));
}

static void
app_tool_free_camera_key(
    void* self,
    struct SideAppHost* host,
    int action,
    char const* args)
{
    struct App* app = app_from_side_app_host(host);
    int const move = APP_CAMERA_MOVEMENT_SPEED;

    (void)self;
    (void)args;
    assert(args);
    if( action == APP_TOOL_FREE_CAMERA_FORWARD )
        app_camera_move_forward(app, move);
    else if( action == APP_TOOL_FREE_CAMERA_BACK )
        app_camera_move_forward(app, -move);
    else if( action == APP_TOOL_FREE_CAMERA_LEFT )
        app_camera_move_left(app, -move);
    else if( action == APP_TOOL_FREE_CAMERA_RIGHT )
        app_camera_move_left(app, move);
    else if( action == APP_TOOL_FREE_CAMERA_UP )
        app->world_views.frame->world_camera_pos.y -= move;
    else if( action == APP_TOOL_FREE_CAMERA_DOWN )
        app->world_views.frame->world_camera_pos.y += move;
    else
    {
        assert(action == APP_TOOL_FREE_CAMERA_UNLOCK);
        app->world_views.frame->camera_unlocked = !app->world_views.frame->camera_unlocked;
        TORIRS_LOG("camera: %s\n", app->world_views.frame->camera_unlocked ? "UNLOCKED" : "locked");
        RedrawRequest_Raise(&app->redraw_request);
    }
}
