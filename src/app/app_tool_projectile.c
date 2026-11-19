/*
 * The projectile source-tile tool: a side-app (sideapp/side_app.h).
 *
 * A key bound to `spawn_projectile` pressed over a world tile twice: the
 * first press marks the hovered tile as the source, the second launches a
 * projectile from it to the tile hovered then, and a second press on the
 * source clears it. It takes no world click and swallows nothing. Its one
 * piece of state is the latch (world/proj_spawn_latch.h), held here and
 * nowhere else; App_Init's registration (app_tools.c) makes it, the registry's
 * teardown frees it.
 *
 * Delivered at the world-hotkeys point of the frame (app_world_hotkeys), under
 * that point's gates: the world loaded and its viewport live, no text input
 * focused, a tile hovered; the key bare, not one a revconfig hotkey or the map
 * editor claimed this frame. Last at that point, by rank (app_tools.c lists
 * the order).
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "mem/torirs_mem.h"
#include "app/app_internal.h"
#include "world/proj_spawn_latch.h"

struct AppToolProjectile
{
    /** The source tile, marked by the first press and spent by the second. */
    struct ProjSpawnLatch latch;
};

/* Private to this unit, declared up front so definition order is free. */
static int
app_world_npc_target_at_tile(
    struct App* app,
    int tile_x,
    int tile_z,
    int level);
static void
app_world_spawn_projectile(
    struct AppToolProjectile* tool,
    struct App* app,
    int tile_x,
    int tile_z,
    int level,
    char const* args);
static void
app_tool_projectile_key(
    void* self,
    struct SideAppHost* host,
    int action,
    char const* args);
static void
app_tool_projectile_free(void* self);

enum
{
    APP_TOOL_PROJECTILE_SPAWN
};

static struct SideAppAction const k_app_tool_projectile_actions[] = {
    [APP_TOOL_PROJECTILE_SPAWN] = { "spawn_projectile", SIDE_APP_PHASE_WORLD_HOTKEYS,
                                    SIDE_APP_GATE_WORLD, SIDE_APP_KEY_PRESS, 7 },
};

static struct SideAppHooks const k_app_tool_projectile_hooks = {
    .key = app_tool_projectile_key,
    .free = app_tool_projectile_free,
};

void
app_tool_projectile_register(struct App* app)
{
    struct AppToolProjectile* tool;

    assert(app);
    assert(app->side_apps);
    tool = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*tool));
    ProjSpawnLatch_Reset(&tool->latch);
    SideApps_Register(
        app->side_apps,
        "projectile",
        tool,
        &k_app_tool_projectile_hooks,
        k_app_tool_projectile_actions,
        (int)(sizeof(k_app_tool_projectile_actions) / sizeof(k_app_tool_projectile_actions[0])));
}

/* The key fired at the world-hotkeys point: its gates have already held, so
 * a tile is hovered. */
static void
app_tool_projectile_key(
    void* self,
    struct SideAppHost* host,
    int action,
    char const* args)
{
    struct AppToolProjectile* tool = self;
    struct App* app = app_from_side_app_host(host);

    assert(tool);
    assert(action == APP_TOOL_PROJECTILE_SPAWN);
    assert(args);
    app_world_spawn_projectile(
        tool,
        app,
        app->world_views.frame->world_hover_tile_x,
        app->world_views.frame->world_hover_tile_z,
        app->world_views.frame->world_hover_tile_level,
        args);
}

static void
app_tool_projectile_free(void* self)
{
    ToriMem_Free(ToriMem_Process(), self);
}

/* Wire target-entity id (npc slot + 1) for a *synced* npc standing on a tile,
 * WORLD_PROJECTILE_TARGET_NONE when there is none. Only server-synced npcs can
 * be named: the wire encoding is the server's slot space, and offline spawns
 * deliberately sit outside it with server_slot -1. */
static int
app_world_npc_target_at_tile(
    struct App* app,
    int tile_x,
    int tile_z,
    int level)
{
    struct World_EntityPool* pool;

    if( !app->world )
        return WORLD_PROJECTILE_TARGET_NONE;

    pool = &app->world->entities.npc;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, i);
        if( !npc || npc->server_slot < 0 )
            continue;
        if( npc->grid_position.x == tile_x && npc->grid_position.z == tile_z &&
            npc->grid_position.level == level )
            return npc->server_slot + 1;
    }
    return WORLD_PROJECTILE_TARGET_NONE;
}

/* Hotkey 0, two-press latch: first press marks the hovered tile as source,
 * second launches source -> hovered (same-tile press clears the latch). Firing
 * onto a synced npc targets *that entity*, so the arc follows it as it walks —
 * the tracking the MAP_PROJANIM target id drives against a live server. */
static void
app_world_spawn_projectile(
    struct AppToolProjectile* tool,
    struct App* app,
    int tile_x,
    int tile_z,
    int level,
    char const* args)
{
    struct Task_AppSpawn* task;
    int src_x = -1;
    int src_z = -1;
    int src_level = 0;
    enum ProjSpawnLatchAction const action =
        ProjSpawnLatch_Press(&tool->latch, tile_x, tile_z, level, &src_x, &src_z, &src_level);

    if( action == PROJ_SPAWN_LATCH_MARKED )
    {
        TORIRS_LOG("spawn_projectile: source latched at %d,%d\n", tile_x, tile_z);
        return;
    }
    if( action == PROJ_SPAWN_LATCH_CLEARED )
    {
        TORIRS_LOG("spawn_projectile: latch cleared\n");
        return;
    }

    task = app_spawn_task_new(app, APP_SPAWN_PROJECTILE, tile_x, tile_z, level);
    task->model_id = ToriRS_EnvNamedArgOrEnv(
        args, "model", "TORIRS_SPAWN_PROJ_MODEL", 3081 /* v1 spawn-test spotanim model */);
    task->seq_id = ToriRS_EnvNamedArgOrEnv(
        args,
        "seq",
        "TORIRS_SPAWN_PROJ_SEQ",
        659 /* v1 spawn-test spotanim sequence (RUNESCAPE_PROJECTILE_SEQ_ID) */);
    task->src_tile_x = src_x;
    task->src_tile_z = src_z;
    task->src_level = src_level;
    task->proj_target = app_world_npc_target_at_tile(app, tile_x, tile_z, level);
    app_spawn_effect_queue(app, task);
}
