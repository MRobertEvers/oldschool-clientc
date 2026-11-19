/*
 * The world reload: a side-app (sideapp/side_app.h).
 *
 * A key bound to `world_reload` (CTRL held) reloads the world the client is
 * on through the task system: its assets are cached, so it is fast, and the
 * rebuild clears the world's scene elements, spawned entities included. It
 * does nothing while the client is on no world node. It keeps no state.
 *
 * Delivered at the camera-keys point of the frame (app_world_camera_keys,
 * once per view the keys steer), under that point's gates: the world loaded
 * and its viewport live, the catalog's model view not focused, and no text
 * input focused unless the map editor owns the camera; the key with CTRL, not
 * one the map editor claimed this frame. After the camera unlock, before the
 * painter stepper (app_tools.c's order).
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Private to this unit, declared up front so definition order is free. */
static void
app_tool_world_reload_key(
    void* self,
    struct SideAppHost* host,
    int action,
    char const* args);

enum
{
    APP_TOOL_WORLD_RELOAD
};

static struct SideAppAction const k_app_tool_world_reload_actions[] = {
    [APP_TOOL_WORLD_RELOAD] = { "world_reload", SIDE_APP_PHASE_CAMERA_KEYS, SIDE_APP_GATE_CTRL,
                                SIDE_APP_KEY_PRESS, 0 },
};

static struct SideAppHooks const k_app_tool_world_reload_hooks = {
    .key = app_tool_world_reload_key,
};

void
app_tool_world_reload_register(struct App* app)
{
    assert(app);
    assert(app->side_apps);
    SideApps_Register(
        app->side_apps,
        "world_reload",
        NULL,
        &k_app_tool_world_reload_hooks,
        k_app_tool_world_reload_actions,
        (int)(sizeof(k_app_tool_world_reload_actions) / sizeof(k_app_tool_world_reload_actions[0])));
}

/* Reload the world through the task system (assets cached -> fast; rebuild
 * clears world scene elements incl. spawned entities). */
static void
app_tool_world_reload_key(
    void* self,
    struct SideAppHost* host,
    int action,
    char const* args)
{
    struct App* app = app_from_side_app_host(host);

    (void)self;
    (void)action;
    (void)args;
    assert(action == APP_TOOL_WORLD_RELOAD);
    assert(args);
    if( App_WorldNodeIndex(app) >= 0 )
        app_world_load_begin(app, NULL, 0);
}
