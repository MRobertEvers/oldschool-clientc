/*
 * The side-apps: the one list of the client's debug tools.
 *
 * Each tool is one app_tool_<name>.c holding its state and hooks behind one
 * registration function; the registry (sideapp/side_app.h) is the only thing
 * that calls them. Registration order is the order every hook is delivered
 * in -- within a phase after the rank an action carries, where one tool's
 * actions interleave with another's -- so a tool is added here where its
 * place in the frame is, and nowhere else in the client names it.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

void
app_tools_register(struct App* app)
{
    assert(app);
    assert(!app->side_apps);
    app->side_apps = SideApps_New(&app->cfg.side_app_bindings);

    /* The hotkeys whose tools have not moved onto the registry yet: their
     * names, answered by polling (app_hotkeys.c). */
    app_debug_hotkeys_register(app);

    /* The world-hotkeys point, in the rank its actions carry:
     *   1 spawn_player, 2 spawn_npc (spawn); 3 damage_test;
     *   4 entity_spotanim, 5 spawn_spotanim (spotanim); 6 spawn_obj (spawn);
     *   7 spawn_projectile (projectile). */
    app_tool_spawn_register(app);
    app_tool_damage_test_register(app);
    app_tool_spotanim_register(app);
    app_tool_projectile_register(app);

    /* The camera keys, in registration order: the flight at the
     * camera-flight point; then at the camera-keys point the unlock, the
     * world reload and last the painter stepper, whose keys_done applies
     * what its keys built. */
    app_tool_free_camera_register(app);
    app_tool_world_reload_register(app);
    app_tool_paint_stepper_register(app);
}

void
app_tools_free(struct App* app)
{
    assert(app);
    SideApps_Free(app->side_apps);
    app->side_apps = NULL;
}

int
App_ToolsBindingsResolved(struct App const* app)
{
    int count;

    assert(app);
    assert(app->side_apps);
    count = SideApps_UnresolvedCount(app->side_apps);
    for( int i = 0; i < count; i++ )
    {
        struct SideAppUnresolved const u = SideApps_UnresolvedAt(app->side_apps, i);
        if( u.source[0] )
            TORIRS_ERR("bootmanifest: [action:%s] has unknown target '%s'\n", u.source, u.action);
        else
            TORIRS_ERR("bootmanifest: [debug:hotkeys] binds unknown target '%s'\n", u.action);
    }
    return count == 0;
}
