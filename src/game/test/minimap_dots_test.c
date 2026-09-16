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
#include "ui/minimap_view.h"
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
    int marker_scene;
    int edge_scene;
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
    fx->marker_scene = 4243;
    fx->edge_scene = 4244;
    fx->app.bridge.static_sprite_scene[STATIC_SPRITE_MAPDOTS] = fx->dots_scene;
    /* `mapmarker` carries both the destination flag (frame 0) and the hint
     * arrow (frame 1); `mapedge` is the rim arrow for a hint off the map. */
    fx->app.bridge.static_sprite_scene[STATIC_SPRITE_MAPMARKER] = fx->marker_scene;
    fx->app.bridge.static_sprite_scene[STATIC_SPRITE_MAPEDGE] = fx->edge_scene;
    /* Scene origin: base = (zone - 6) * 8, so zone 6 makes an absolute tile
     * and a scene tile the same number and the hint coords below readable. */
    fx->app.rebuild_zone_x = 6;
    fx->app.rebuild_zone_z = 6;
    /* No flag placed: -1 is the "no destination" sentinel the app resets to. */
    fx->app.minimap.flag_tile_x = -1;

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

/*
 * The other half of the reference's gate, and the half that was missing.
 *
 * Rev 239's `method2403` tests `isMinimapVisible() && isInteractible()`, so an
 * npc that states opcode 107 and nothing else draws no dot. The Theatre of
 * Blood's Nylocas supports are exactly that record — `interactable=no`, no
 * opcode 93 — and they put four dots on the minimap for as long as only the
 * first flag was read.
 */
static void
test_interactable_flag_gates_npc_dot(void)
{
    struct DotFixture fx;
    struct UITreeMinimapDot const* dots = NULL;
    int count;

    printf("TEST: NpcType.interactable=false suppresses the npc's minimap dot\n");

    fixture_init(&fx);
    spawn_npc(&fx, 10, 26, 25);
    spawn_npc(&fx, 11, 24, 25);

    count = App_MinimapBuildDots(&fx.app, &dots);
    TEST_ASSERT(
        count_dots(dots, count, fx.dots_scene, 1) == 2, "both visible npcs draw a dot");

    {
        struct World_EntityPool* pool = &fx.app.world->entities.npc;
        struct WorldEntity_NPC* npc =
            World_EntityPoolGet(pool, World_EntityPoolHead(pool));

        npc->interactable = false;
        /* Left ON, so this proves the SECOND flag is read rather than the
         * first one doing the work again. */
        TEST_ASSERT(npc->minimap_visible, "the opcode 93 flag is still set");
    }

    count = App_MinimapBuildDots(&fx.app, &dots);
    TEST_ASSERT(
        count_dots(dots, count, fx.dots_scene, 1) == 1,
        "the npc that clears NpcType.interactable draws no dot");

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
    /* Opcode 107 is the same shape — it only ever clears — so the spawn
     * default is on for the same reason. */
    TEST_ASSERT(npc->interactable, "World_NpcSpawn leaves the npc interactable");
    fixture_free(&fx);
}

/*
 * The HINT ARROW on the map (reference method2135 / drawMinimapHint).
 *
 * The world arrow over the subject's head can only mark what the camera can
 * see, and the whole reason the server sends a hint is that the player is not
 * there yet -- so for most of a hint's life the map is the only place it can
 * show at all. Everything below is about a marker that is missing rather than
 * wrong, which no screenshot of the world view can tell you.
 */
static void
test_the_hint_marks_its_subject(void)
{
    struct DotFixture fx;
    struct UITreeMinimapDot const* dots = NULL;
    int count;
    struct WorldEntity_NPC* npc;

    printf("TEST: a hint on an npc draws mapmarker frame 1 over it\n");

    fixture_init(&fx);
    npc = spawn_npc(&fx, 10, 28, 25);
    npc->server_slot = 41;

    /* No hint: the pack is bound and the npc is there, and still nothing --
     * so the marker below is the hint's and not some other pass's. */
    count = App_MinimapBuildDots(&fx.app, &dots);
    TEST_ASSERT(
        count_dots(dots, count, fx.marker_scene, 1) == 0, "no hint, no marker");

    fx.app.hint_arrow.type = APP_HINT_ARROW_NPC;
    fx.app.hint_arrow.target = 41;
    count = App_MinimapBuildDots(&fx.app, &dots);
    TEST_ASSERT(
        count_dots(dots, count, fx.marker_scene, 1) == 1, "the hinted npc gets a marker");
    /* Frame 0 is the destination flag and no walk is under way -- a hint drawn
     * with that frame would put a flag on the map the player never placed. */
    TEST_ASSERT(
        count_dots(dots, count, fx.marker_scene, 0) == 0, "and it is not the flag's frame");

    /* A slot the world does not hold: a hint outliving its npc is ordinary
     * (it walked out of the loaded window), and the map says nothing. */
    fx.app.hint_arrow.target = 999;
    count = App_MinimapBuildDots(&fx.app, &dots);
    TEST_ASSERT(
        count_dots(dots, count, fx.marker_scene, 1) == 0,
        "a hint whose subject is not here draws nothing");

    fixture_free(&fx);
}

/* Ten cycles shown, ten hidden. The blink is the reference's and it is what
 * makes the marker read as an instruction rather than one more icon. */
static void
test_the_hint_blinks(void)
{
    struct DotFixture fx;
    struct UITreeMinimapDot const* dots = NULL;
    struct WorldEntity_NPC* npc;
    int shown = 0;
    int hidden = 0;

    printf("TEST: the hint marker blinks ten cycles on, ten off\n");

    fixture_init(&fx);
    npc = spawn_npc(&fx, 10, 28, 25);
    npc->server_slot = 41;
    fx.app.hint_arrow.type = APP_HINT_ARROW_NPC;
    fx.app.hint_arrow.target = 41;

    for( int cycle = 0; cycle < 40; cycle++ )
    {
        int count;

        fx.app.logic_cycle = (uint64_t)cycle;
        count = App_MinimapBuildDots(&fx.app, &dots);
        if( count_dots(dots, count, fx.marker_scene, 1) )
            shown++;
        else
            hidden++;
    }
    TEST_ASSERT(shown == 20 && hidden == 20, "half of forty cycles show it");

    fx.app.logic_cycle = 9;
    TEST_ASSERT(
        count_dots(dots,
                   App_MinimapBuildDots(&fx.app, &dots),
                   fx.marker_scene,
                   1) == 1,
        "the tenth cycle of the period still shows it");
    fx.app.logic_cycle = 10;
    TEST_ASSERT(
        count_dots(dots,
                   App_MinimapBuildDots(&fx.app, &dots),
                   fx.marker_scene,
                   1) == 0,
        "the eleventh does not");

    fixture_free(&fx);
}

/*
 * A subject off the map gets an arrow on the rim instead -- the case that
 * separates a hint from a dot, and the one a "draw it where it is" port loses
 * silently: the marker is simply culled and the player is told nothing.
 */
static void
test_a_hint_off_the_map_points_from_the_rim(void)
{
    struct DotFixture fx;
    struct UITreeMinimapDot const* dots = NULL;
    int count;
    struct WorldEntity_NPC* npc;

    printf("TEST: a hint past the map's edge becomes a rim arrow\n");

    fixture_init(&fx);
    /* 25 tiles due east: 100 map pixels, well past the 65 the marker band
     * ends at and well short of the 300 the rim gives up at. */
    npc = spawn_npc(&fx, 10, 50, 25);
    npc->server_slot = 41;
    fx.app.hint_arrow.type = APP_HINT_ARROW_NPC;
    fx.app.hint_arrow.target = 41;

    /* The dat1 lane's radii are Client.ts's own two constants. */
    fx.app.cfg.cache_kind = APP_CACHE_DAT1;
    count = App_MinimapBuildDots(&fx.app, &dots);
    TEST_ASSERT(
        count_dots(dots, count, fx.marker_scene, 1) == 0, "no marker out there");
    TEST_ASSERT(count_dots(dots, count, fx.edge_scene, 0) == 1, "a rim arrow instead");
    for( int i = 0; i < count; i++ )
    {
        if( dots[i].scene_id != fx.edge_scene )
            continue;
        /* Due east of the player, so it sits on the map's right at the across
         * radius, and the art is turned a quarter to point that way. */
        TEST_ASSERT(dots[i].dx == 63 - dots[i].w / 2, "on the rim toward its subject");
        TEST_ASSERT(dots[i].rotate == 512, "turned to point at it");
    }

    /* The dat2 lane derives one radius from the map widget instead, so it
     * needs a published box -- and says nothing rather than guessing when the
     * first frame has not published one yet. */
    fx.app.cfg.cache_kind = APP_CACHE_DAT2;
    count = App_MinimapBuildDots(&fx.app, &dots);
    TEST_ASSERT(
        count_dots(dots, count, fx.edge_scene, 0) == 0,
        "with no map box yet, no invented rim");
    MinimapView_Publish(&fx.app.minimap, 550, 4, 152, 152, 0);
    count = App_MinimapBuildDots(&fx.app, &dots);
    for( int i = 0; i < count; i++ )
        if( dots[i].scene_id == fx.edge_scene )
            TEST_ASSERT(dots[i].dx == 152 / 2 - 25 - dots[i].w / 2,
                        "and the widget's own half-width less 25 when there is one");

    /* Far enough that a direction is not directions any more. */
    {
        struct WorldEntity_NPC* far_npc;

        fx.app.hint_arrow.target = 42;
        far_npc = spawn_npc(&fx, 11, 25, 25);
        far_npc->server_slot = 42;
        far_npc->draw_position.x = (float)(25 * 128 + 64 + 300 * 32);
        count = App_MinimapBuildDots(&fx.app, &dots);
        TEST_ASSERT(count_dots(dots, count, fx.edge_scene, 0) == 0,
                    "past three hundred pixels the map stops pointing");
        TEST_ASSERT(count_dots(dots, count, fx.marker_scene, 1) == 0, "and marks nothing");
    }

    fixture_free(&fx);
}

/*
 * The COORD form, which is the one the quest helpers actually send: an
 * absolute tile plus where in it to float, converted through the same scene
 * origin the world pass uses.
 */
static void
test_a_coord_hint_marks_the_tile_it_names(void)
{
    struct DotFixture fx;
    struct UITreeMinimapDot const* dots = NULL;
    int count;
    int centred_dx = 0;

    printf("TEST: a coord hint lands on the tile the server named\n");

    fixture_init(&fx);
    fx.app.hint_arrow.type = APP_HINT_ARROW_COORD;
    fx.app.hint_arrow.target = 30;
    fx.app.hint_arrow.tile_z = 25;
    fx.app.hint_arrow.offset_x = 64;
    fx.app.hint_arrow.offset_z = 64;

    count = App_MinimapBuildDots(&fx.app, &dots);
    TEST_ASSERT(count_dots(dots, count, fx.marker_scene, 1) == 1, "the tile gets a marker");
    for( int i = 0; i < count; i++ )
        if( dots[i].scene_id == fx.marker_scene && dots[i].atlas_index == 1 )
            centred_dx = dots[i].dx;
    /* Five tiles east of the player at 4 px a tile, sprite-centred. */
    TEST_ASSERT(centred_dx == 5 * 4 - 4 / 2, "five tiles east of the player");

    /* Types 3..6 anchor the arrow to an edge of that same tile rather than its
     * centre; the wire says which by the type and the exec folds it to this
     * pair. Half a tile west is half a tile on the map too. */
    fx.app.hint_arrow.offset_x = 0;
    count = App_MinimapBuildDots(&fx.app, &dots);
    for( int i = 0; i < count; i++ )
        if( dots[i].scene_id == fx.marker_scene && dots[i].atlas_index == 1 )
            TEST_ASSERT(dots[i].dx == centred_dx - 2, "the west anchor draws half a tile west");

    fixture_free(&fx);
}

int
main(void)
{
    g_failures = 0;
    test_minimap_flag_gates_npc_dot();
    test_interactable_flag_gates_npc_dot();
    test_spawn_defaults_minimap_visible();
    test_the_hint_marks_its_subject();
    test_the_hint_blinks();
    test_a_hint_off_the_map_points_from_the_rim();
    test_a_coord_hint_marks_the_tile_it_names();
    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("minimap dot tests passed\n");
    return 0;
}
