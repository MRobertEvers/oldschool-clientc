/*
 * Minimap dot gating (reference Client.ts minimapDraw).
 *
 * The reference draws an NPC's yellow dot only when its NpcType states
 * `minimap` -- config opcode 93 is a bare flag that *clears* a default-true
 * field, and ~1350 of the osrs239 records use it (Rock Crab's disguised form,
 * butterflies, gulls, the Inferno's Ancestral Glyph and rocky supports...).
 * The client decoded that flag but never carried it past the cache struct, so
 * every one of those npcs painted a dot the reference does not.
 *
 * These tests drive App_MinimapBuildDots itself rather than the flag's
 * plumbing, because the gate is what the player sees.
 */
#include "app.h"
#include "engine/uitree_scene_bridge.h"
#include "game/rs_entity_sync.h"
#include "test_harness.h"
#include "toridraw.h"
#include "ui/uitree_emit.h"
#include "world.h"

#include <stdio.h>
#include <string.h>

int g_failures;

/* Every dot the builder emits carries the scene it draws from; the local
 * player's 3x3 white square is a scene-less rect appended unconditionally, so
 * count by atlas index to say "npc dots" rather than "dots". */
static int
count_dots(
    struct UITreeMinimapDot const* dots,
    int count,
    int scene_id,
    int atlas_index)
{
    int n = 0;
    for( int i = 0; i < count; i++ )
        if( dots[i].scene_id == scene_id && dots[i].atlas_index == atlas_index )
            n++;
    return n;
}

struct DotFixture
{
    struct App app;
    int dots_scene;
};

/* The builder needs a world with a local player, a scene (the sprite lookup
 * that sizes a dot asserts on it) and the mapdots slot filled in. Nothing
 * else: no cache, no renderer, no UI tree. */
static void
fixture_init(struct DotFixture* fx)
{
    struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();
    int idx;

    memset(fx, 0, sizeof(*fx));
    ToriDraw_InitSinTable();
    ToriDraw_InitCosTable();

    fx->app.world = World_TestMakeReady(104);
    fx->app.scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    RS_EntitySync_Init(&fx->app.esync);

    fx->dots_scene = 4242;
    fx->app.bridge.static_sprite_scene[STATIC_SPRITE_MAPDOTS] = fx->dots_scene;
    /* No flag placed: -1 is the "no destination" sentinel the app resets to. */
    fx->app.minimap_flag_x = -1;

    idx = World_PlayerSpawn(fx->app.world, 1, 0, 25, 25, idle);
    {
        struct WorldEntity_Player* local =
            World_EntityPoolGet(&fx->app.world->entities.player, idx);
        local->server_pid = 7;
    }
    fx->app.esync.local_pid = 7;
    fx->app.world->local_pid = 7;
    RS_EntitySync_RegisterPlayer(&fx->app.esync, 7, 1, idx);
}

static void
fixture_free(struct DotFixture* fx)
{
    RS_EntitySync_Free(&fx->app.esync);
    ToriDraw_SceneFree(fx->app.scene);
    World_Free(fx->app.world);
}

/* Spawn an npc a tile away from the local player, inside the dot ring. */
static struct WorldEntity_NPC*
spawn_npc(
    struct DotFixture* fx,
    int element_id,
    int tile_x,
    int tile_z)
{
    struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();
    int idx = World_NpcSpawn(fx->app.world, element_id, 500, 0, tile_x, tile_z, 1, idle);
    return World_EntityPoolGet(&fx->app.world->entities.npc, idx);
}

static void
test_minimap_flag_gates_npc_dot(void)
{
    struct DotFixture fx;
    struct UITreeMinimapDot const* dots = NULL;
    int count;

    printf("TEST: NpcType.minimap=false suppresses the npc's minimap dot\n");

    fixture_init(&fx);
    spawn_npc(&fx, 10, 26, 25);
    spawn_npc(&fx, 11, 24, 25);

    count = App_MinimapBuildDots(&fx.app, &dots);
    TEST_ASSERT(
        count_dots(dots, count, fx.dots_scene, 1) == 2, "both visible npcs draw a dot");

    /* Clear the flag on one of them -- the same copy App_WorldSpawnSyncedNpc
     * makes from ToriRS_Npctype::minimap_visible when the config resolves. */
    {
        struct World_EntityPool* pool = &fx.app.world->entities.npc;
        struct WorldEntity_NPC* npc =
            World_EntityPoolGet(pool, World_EntityPoolHead(pool));
        npc->minimap_visible = false;
    }

    count = App_MinimapBuildDots(&fx.app, &dots);
    TEST_ASSERT(
        count_dots(dots, count, fx.dots_scene, 1) == 1,
        "the npc that clears NpcType.minimap draws no dot");

    {
        struct World_EntityPool* pool = &fx.app.world->entities.npc;
        int first = World_EntityPoolHead(pool);
        struct WorldEntity_NPC* npc =
            World_EntityPoolGet(pool, World_EntityPoolNext(pool, first));
        npc->multinpc_hidden = true;
    }
    count = App_MinimapBuildDots(&fx.app, &dots);
    TEST_ASSERT(
        count_dots(dots, count, fx.dots_scene, 1) == 0,
        "a positional -1 multiNpc child leaves no minimap dot");

    fixture_free(&fx);
}

static void
test_spawn_defaults_minimap_visible(void)
{
    struct DotFixture fx;
    struct WorldEntity_NPC* npc;

    printf("TEST: a fresh npc spawn defaults to minimap-visible\n");

    fixture_init(&fx);
    npc = spawn_npc(&fx, 10, 26, 25);
    /* Opcode 93 only ever clears the flag, so the spawn default has to be on:
     * an npc whose config has not resolved yet must still show its dot, which
     * is what the client did before the flag was honoured at all. */
    TEST_ASSERT(npc->minimap_visible, "World_NpcSpawn leaves the dot enabled");
    fixture_free(&fx);
}

int
main(void)
{
    g_failures = 0;
    test_minimap_flag_gates_npc_dot();
    test_spawn_defaults_minimap_visible();
    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("minimap dot tests passed\n");
    return 0;
}
