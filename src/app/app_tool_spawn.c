/*
 * The spawn tool: a side-app (sideapp/side_app.h).
 *
 * Three keys, each pressed over a world tile: `spawn_player` puts a player
 * there, `spawn_npc` an npc (`id=`, TORIRS_SPAWN_NPC, else 3106) and
 * `spawn_obj` a ground item (`id=`, TORIRS_SPAWN_OBJ, else 1265). Each press
 * queues one spawn task; the tool keeps no state. The npc and obj queues are
 * the shared ones the map editor places through (app_world_edit.c); only the
 * player queue is this tool's own.
 *
 * Delivered at the world-hotkeys point of the frame (app_world_hotkeys), under
 * that point's gates: the world loaded and its viewport live, no text input
 * focused, a tile hovered; the key bare, not one a revconfig hotkey or the map
 * editor claimed this frame. The three actions take their places in that
 * point's order by rank (app_tools.c lists it).
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Private to this unit, declared up front so definition order is free. */
static void
app_world_spawn_player(
    struct App* app,
    int tile_x,
    int tile_z,
    int level);
static void
app_tool_spawn_key(
    void* self,
    struct SideAppHost* host,
    int action,
    char const* args);

enum
{
    APP_TOOL_SPAWN_PLAYER,
    APP_TOOL_SPAWN_NPC,
    APP_TOOL_SPAWN_OBJ
};

static struct SideAppAction const k_app_tool_spawn_actions[] = {
    [APP_TOOL_SPAWN_PLAYER] = { "spawn_player", SIDE_APP_PHASE_WORLD_HOTKEYS, SIDE_APP_GATE_WORLD,
                                SIDE_APP_KEY_PRESS, 1 },
    [APP_TOOL_SPAWN_NPC] = { "spawn_npc", SIDE_APP_PHASE_WORLD_HOTKEYS, SIDE_APP_GATE_WORLD,
                             SIDE_APP_KEY_PRESS, 2 },
    [APP_TOOL_SPAWN_OBJ] = { "spawn_obj", SIDE_APP_PHASE_WORLD_HOTKEYS, SIDE_APP_GATE_WORLD,
                             SIDE_APP_KEY_PRESS, 6 },
};

static struct SideAppHooks const k_app_tool_spawn_hooks = {
    .key = app_tool_spawn_key,
};

void
app_tool_spawn_register(struct App* app)
{
    assert(app);
    assert(app->side_apps);
    SideApps_Register(
        app->side_apps,
        "spawn",
        NULL,
        &k_app_tool_spawn_hooks,
        k_app_tool_spawn_actions,
        (int)(sizeof(k_app_tool_spawn_actions) / sizeof(k_app_tool_spawn_actions[0])));
}

/* A key fired at the world-hotkeys point: its gates have already held, so a
 * tile is hovered. */
static void
app_tool_spawn_key(
    void* self,
    struct SideAppHost* host,
    int action,
    char const* args)
{
    struct App* app = app_from_side_app_host(host);
    int const tile_x = app->world_views.frame->world_hover_tile_x;
    int const tile_z = app->world_views.frame->world_hover_tile_z;
    int const level = app->world_views.frame->world_hover_tile_level;

    (void)self;
    (void)args;
    assert(args);
    if( action == APP_TOOL_SPAWN_PLAYER )
        app_world_spawn_player(app, tile_x, tile_z, level);
    else if( action == APP_TOOL_SPAWN_NPC )
        app_world_spawn_npc(app, tile_x, tile_z, level, args);
    else
    {
        assert(action == APP_TOOL_SPAWN_OBJ);
        app_world_spawn_obj(app, tile_x, tile_z, level, args);
    }
}

static void
app_world_spawn_player(
    struct App* app,
    int tile_x,
    int tile_z,
    int level)
{
    struct Task_AppSpawn* task = app_spawn_task_new(app, APP_SPAWN_PLAYER, tile_x, tile_z, level);
    TaskRunner_AddRenderBlockingSerialTask(&app->packet_runner, &task->task);
}
