/*
 * The damage test: a side-app (sideapp/side_app.h).
 *
 * A key bound to `damage_test` hits every player and npc in the world for a
 * test hitsplat and gives each an overhead chat line (and every player two
 * stacked headicons), so the whole overlay pass -- health bars, hitmarks,
 * overhead chat -- can be exercised offline. It keeps no state.
 *
 * Delivered at the world-hotkeys point of the frame (app_world_hotkeys), under
 * that point's gates: the world loaded and its viewport live, no text input
 * focused, a tile hovered (needed, never read); the key bare, not one a
 * revconfig hotkey or the map editor claimed this frame. Its place in that
 * point's order is its rank (app_tools.c lists it).
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Private to this unit, declared up front so definition order is free. */
static void
app_world_damage_test(struct App* app);
static void
app_tool_damage_test_key(
    void* self,
    struct SideAppHost* host,
    int action,
    char const* args);

enum
{
    APP_TOOL_DAMAGE_TEST
};

static struct SideAppAction const k_app_tool_damage_test_actions[] = {
    [APP_TOOL_DAMAGE_TEST] = { "damage_test", SIDE_APP_PHASE_WORLD_HOTKEYS, SIDE_APP_GATE_WORLD,
                               SIDE_APP_KEY_PRESS, 3 },
};

static struct SideAppHooks const k_app_tool_damage_test_hooks = {
    .key = app_tool_damage_test_key,
};

void
app_tool_damage_test_register(struct App* app)
{
    assert(app);
    assert(app->side_apps);
    SideApps_Register(
        app->side_apps,
        "damage_test",
        NULL,
        &k_app_tool_damage_test_hooks,
        k_app_tool_damage_test_actions,
        (int)(sizeof(k_app_tool_damage_test_actions) / sizeof(k_app_tool_damage_test_actions[0])));
}

static void
app_tool_damage_test_key(
    void* self,
    struct SideAppHost* host,
    int action,
    char const* args)
{
    (void)self;
    (void)action;
    (void)args;
    assert(action == APP_TOOL_DAMAGE_TEST);
    assert(args);
    app_world_damage_test(app_from_side_app_host(host));
}

/* Hotkey 6: hit every live player/npc for a test hitsplat + half health and
 * give each an overhead chat line, so the whole overlay pass (health bars,
 * hitmarks and overhead chat) can be exercised offline. Goes through the same
 * World_*AddHitmark / World_*SetChat the NPC_INFO/PLAYER_INFO ops use. */
static void
app_world_damage_test(struct App* app)
{
    struct World_EntityPool* pool;
    int damage = 1 + (FrameClockSerial_LogicCycle(&app->frame_clock.serial) % 30);
    /* Rev 239 does not use the legacy type-0/type-1 convention: its canonical
     * red damage splat is type 28 (sprite 1359). Keep type 0 only as the
     * supported fallback for older cache families with no rev-239 record. */
    int hitsplat_type =
        app->hitsplats.count > RS_HITSPLAT_OSRS239_DAMAGE ? RS_HITSPLAT_OSRS239_DAMAGE : 0;

    if( !app->world )
        return;
    pool = &app->world->entities.player;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        World_PlayerAddHitmark(app->world, i, hitsplat_type, damage, 5, 10);
        World_PlayerSetChat(app->world, i, "Hello there!", 0, 0);
        /* Exercise the overhead headicon pass too: icons 0 + 2 stacked. */
        {
            struct WorldEntity_Player* tpl = World_EntityPoolGet(pool, i);
            if( tpl )
                tpl->headicon = 0x5;
        }
    }
    pool = &app->world->entities.npc;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        World_NpcAddHitmark(app->world, i, hitsplat_type, damage, 5, 10);
        World_NpcSetChat(app->world, i, "Grrr!", 0, 0);
    }
}
