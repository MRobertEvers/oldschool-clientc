/*
 * The spotanim tool: a side-app (sideapp/side_app.h).
 *
 * Two keys. `spawn_spotanim` puts a free-standing spotanim on the hovered tile
 * through the same App_WorldSpotanimSpawn the MAP_ANIM zone packet drives;
 * `entity_spotanim` attaches one (the SPOTANIM mask) to every player and npc
 * in the world -- the impact effect a projectile lands on its target. Both
 * take `id=` (TORIRS_SPAWN_SPOTANIM, else 74), `height=`
 * (TORIRS_SPAWN_SPOTANIM_HEIGHT, else 92) and `delay=`
 * (TORIRS_SPAWN_SPOTANIM_DELAY, else 0). The tool keeps no state.
 *
 * Delivered at the world-hotkeys point of the frame (app_world_hotkeys), under
 * that point's gates: the world loaded and its viewport live, no text input
 * focused, a tile hovered (entity_spotanim needs one too, and never reads
 * it); the key bare, not one a revconfig hotkey or the map editor claimed
 * this frame. The two actions take their places in that point's order by
 * rank (app_tools.c lists it).
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Private to this unit, declared up front so definition order is free. */
static void
app_world_spawn_spotanim(
    struct App* app,
    int tile_x,
    int tile_z,
    int level,
    char const* args);
static void
app_world_entity_spotanim_test(
    struct App* app,
    char const* args);
static void
app_tool_spotanim_key(
    void* self,
    struct SideAppHost* host,
    int action,
    char const* args);

enum
{
    APP_TOOL_SPOTANIM_ENTITY,
    APP_TOOL_SPOTANIM_SPAWN
};

static struct SideAppAction const k_app_tool_spotanim_actions[] = {
    [APP_TOOL_SPOTANIM_ENTITY] = { "entity_spotanim", SIDE_APP_PHASE_WORLD_HOTKEYS,
                                   SIDE_APP_GATE_WORLD, SIDE_APP_KEY_PRESS, 4 },
    [APP_TOOL_SPOTANIM_SPAWN] = { "spawn_spotanim", SIDE_APP_PHASE_WORLD_HOTKEYS,
                                  SIDE_APP_GATE_WORLD, SIDE_APP_KEY_PRESS, 5 },
};

static struct SideAppHooks const k_app_tool_spotanim_hooks = {
    .key = app_tool_spotanim_key,
};

void
app_tool_spotanim_register(struct App* app)
{
    assert(app);
    assert(app->side_apps);
    SideApps_Register(
        app->side_apps,
        "spotanim",
        NULL,
        &k_app_tool_spotanim_hooks,
        k_app_tool_spotanim_actions,
        (int)(sizeof(k_app_tool_spotanim_actions) / sizeof(k_app_tool_spotanim_actions[0])));
}

/* A key fired at the world-hotkeys point: its gates have already held, so a
 * tile is hovered. */
static void
app_tool_spotanim_key(
    void* self,
    struct SideAppHost* host,
    int action,
    char const* args)
{
    struct App* app = app_from_side_app_host(host);

    (void)self;
    assert(args);
    if( action == APP_TOOL_SPOTANIM_ENTITY )
        app_world_entity_spotanim_test(app, args);
    else
    {
        assert(action == APP_TOOL_SPOTANIM_SPAWN);
        app_world_spawn_spotanim(
            app,
            app->world_views.frame->world_hover_tile_x,
            app->world_views.frame->world_hover_tile_z,
            app->world_views.frame->world_hover_tile_level,
            args);
    }
}

/* Hotkey 5: spawn a free-standing spotanim on the hovered tile.
 * TORIRS_SPAWN_SPOTANIM / _HEIGHT / _DELAY override the defaults. */
static void
app_world_spawn_spotanim(
    struct App* app,
    int tile_x,
    int tile_z,
    int level,
    char const* args)
{
    int spotanim_id = ToriRS_EnvNamedArgOrEnv(
        args, "id", "TORIRS_SPAWN_SPOTANIM", 74 /* a small, visible default effect */);
    int height = ToriRS_EnvNamedArgOrEnv(args, "height", "TORIRS_SPAWN_SPOTANIM_HEIGHT", 92);
    int delay = ToriRS_EnvNamedArgOrEnv(args, "delay", "TORIRS_SPAWN_SPOTANIM_DELAY", 0);
    App_WorldSpotanimSpawn(app, tile_x, tile_z, level, spotanim_id, height, delay);
}

/* Hotkey 4: apply an attached graphic (SPOTANIM mask) to every spawned entity —
 * the reference impact effect a projectile lands on its target. Exercises the
 * entity-spotanim companion-element path headlessly. TORIRS_SPAWN_SPOTANIM /
 * _HEIGHT / _DELAY reuse the free-standing overrides. */
static void
app_world_entity_spotanim_test(
    struct App* app,
    char const* args)
{
    struct World_EntityPool* pool;
    int spotanim_id = ToriRS_EnvNamedArgOrEnv(args, "id", "TORIRS_SPAWN_SPOTANIM", 74);
    int height = ToriRS_EnvNamedArgOrEnv(args, "height", "TORIRS_SPAWN_SPOTANIM_HEIGHT", 92);
    int delay = ToriRS_EnvNamedArgOrEnv(args, "delay", "TORIRS_SPAWN_SPOTANIM_DELAY", 0);

    if( !app->world )
        return;
    pool = &app->world->entities.player;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
        World_PlayerSetSpotanim(app->world, i, 0, spotanim_id, height, delay, false);
    pool = &app->world->entities.npc;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
        World_NpcSetSpotanim(app->world, i, 0, spotanim_id, height, delay, false);
}
