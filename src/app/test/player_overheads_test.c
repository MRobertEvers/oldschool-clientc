/*
 * A player's overheads (app_overlay_build_player, app_overlay_build_hint_arrow)
 * against the rev-239 reference, deob Statics.method6709's player branch:
 *
 *   - N3: the appearance's hidden flag (class60.field551) returns from it
 *     after the chat line and the health bars, so a hidden player shows no
 *     skull, no prayer icon, no hint arrow and no hit splats (method10685 is
 *     the method's last call). It applies to the local player as to anyone,
 *     and no viewer is excepted: the staff exception is the chat line's alone.
 *
 * The builders run for real: a world, a camera looking north at the player,
 * the sprite packs named by scene id. Each kind of item is told apart by the
 * scene it draws from.
 */
#include "app.h"
#include "app/app_internal.h"
#include "engine/cache_provider.h"
#include "engine/toridraw_sprite_from_torirs.h"
#include "engine/uitree_scene_bridge.h"
#include "game/rs_entity_sync.h"
#include "game/rs_hint_arrow.h"
#include "mem/torirs_mem.h"
#include "render/overlay_stage.h"
#include "test_harness.h"
#include "toridraw.h"
#include "toridraw_scene.h"
#include "world.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int g_failures;

enum
{
    SCENE_HITMARKS = 91,
    SCENE_PRAYER = 92,
    SCENE_PK = 93,
    SCENE_HINT = 94,
};

struct Fixture
{
    struct App app;
    struct CacheProvider provider;
    struct CacheProviderVTable vtable;
    struct AppOverheadAssets assets;
    struct WorldEntity_Player* local;
};

/* Two 2x2 frames: the hint pack's frame 0 (npc, coord) and 1 (player). */
static void
add_hint_frames(struct Fixture* fx)
{
    struct ToriRS_Sprite rs;
    struct ToriRS_SpriteFrame frames[2];
    struct ToriDraw_Sprite** sprites;
    int count = 0;

    memset(&rs, 0, sizeof(rs));
    memset(frames, 0, sizeof(frames));
    for( int i = 0; i < 2; i++ )
    {
        /* The conversion takes the pixels (they are the asset group's). */
        frames[i].pixels_argb = ToriMem_Alloc(ToriMem_Assets(), 4 * sizeof(*frames[i].pixels_argb));
        assert(frames[i].pixels_argb);
        for( int p = 0; p < 4; p++ )
            frames[i].pixels_argb[p] = 0xffffffffu;
        frames[i].width = 2;
        frames[i].height = 2;
        frames[i].crop_width = 2;
        frames[i].crop_height = 2;
    }
    rs.frames = frames;
    rs.frame_count = 2;
    sprites = ToriDraw_SpritesFromToriRS(&rs, &count);
    TEST_ASSERT(sprites && count == 2, "the hint pack has its two frames");
    ToriDraw_SceneSpriteAdd(fx->app.scene, SCENE_HINT, sprites, count);
    fx->app.bridge.static_sprite_scene[STATIC_SPRITE_HEADICONS_HINT] = SCENE_HINT;
}

static void
fixture_init(struct Fixture* fx)
{
    struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();
    struct VarPType varp_type = { 0 };
    int idx;

    memset(fx, 0, sizeof(*fx));
    ToriDraw_InitSinTable();
    ToriDraw_InitCosTable();
    App_WorldViewsInit(&fx->app);
    VarPManager_Init(&fx->app.varps);
    (void)VarPManager_SetVarpTypes(&fx->app.varps, &varp_type, 1);
    fx->app.world = World_TestMakeReady(104);
    fx->app.world->cycle = 1000;
    fx->app.scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    fx->provider.vtable = &fx->vtable;
    CacheProvider_InitEngineCaches(&fx->provider);
    fx->app.provider = &fx->provider;
    UITreeSceneBridge_Init(&fx->app.bridge, fx->app.scene, &fx->provider);
    RS_EntitySync_Init(&fx->app.esync);
    OverlayStage_Reset(&fx->app.overlays);

    /* A camera south of tile (25, 25), level with the ground, looking north:
     * every overhead above that tile projects. */
    fx->app.world_views.emit_valid = 1;
    fx->app.world_views.emit_desc.w = 512;
    fx->app.world_views.emit_desc.h = 334;
    fx->app.world_views.frame->world_camera.pitch = 0;
    fx->app.world_views.frame->world_camera.yaw = 0;
    fx->app.world_views.frame->world_camera_pos.x = 25 * 128 + 64;
    fx->app.world_views.frame->world_camera_pos.y = 0;
    fx->app.world_views.frame->world_camera_pos.z = 25 * 128 + 64 - 1500;

    idx = World_PlayerSpawn(fx->app.world, 1, 0, 25, 25, idle);
    fx->local = World_EntityPoolGet(&fx->app.world->entities.player, idx);
    fx->local->server_pid = 7;
    fx->app.esync.local_pid = 7;
    fx->app.world->local_pid = 7;
    RS_EntitySync_RegisterPlayer(&fx->app.esync, 7, 1, idx);

    fx->assets.font_id = -1;
    fx->assets.hitmarks_scene = SCENE_HITMARKS;
    fx->assets.headicons_scene = SCENE_PRAYER;
    fx->assets.pk_scene = SCENE_PK;
    add_hint_frames(fx);
}

static void
fixture_free(struct Fixture* fx)
{
    RS_EntitySync_Free(&fx->app.esync);
    UITreeSceneBridge_Free(&fx->app.bridge);
    ToriDraw_SceneFree(fx->app.scene);
    CacheProvider_FreeEngineCaches(&fx->provider);
    World_Free(fx->app.world);
    VarPManager_Free(&fx->app.varps);
}

/* A player beside the local one, with everything an overhead can show: a
 * health bar (the fallback rectangle type), a splat, a skull, a prayer icon. */
static struct WorldEntity_Player*
spawn_marked(
    struct Fixture* fx,
    int pid)
{
    struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();
    int idx = World_PlayerSpawn(fx->app.world, 2, 0, 25, 25, idle);
    struct WorldEntity_Player* player = World_EntityPoolGet(&fx->app.world->entities.player, idx);
    int const cycle = fx->app.world->cycle;

    player->server_pid = pid;
    player->skull_icon = 0;
    player->headicon = 1 << 2;
    player->combat.healthbar_type = 0;
    player->combat.healthbar_start_cycle = 0;
    player->combat.healthbar_start_fill = 30;
    player->combat.healthbar_end_fill = 15;
    player->combat.healthbar_end_cycle = cycle + 100;
    player->combat.healthbar_held_fill = -1;
    player->combat.damage_types[0] = 0;
    player->combat.damage_values[0] = 3;
    player->combat.damage_start_cycles[0] = 0;
    player->combat.damage_cycles[0] = cycle + 70;
    return player;
}

static int
items_of_scene(
    struct Fixture* fx,
    int scene_id)
{
    int n = 0;
    for( int i = 0; i < fx->app.overlays.world_count; i++ )
        if( fx->app.overlays.world[i].kind == UITREE_ENTITY_OVERLAY_SPRITE &&
            fx->app.overlays.world[i].scene_id == scene_id )
            n++;
    return n;
}

static int
rects(struct Fixture* fx)
{
    int n = 0;
    for( int i = 0; i < fx->app.overlays.world_count; i++ )
        if( fx->app.overlays.world[i].kind == UITREE_ENTITY_OVERLAY_RECT )
            n++;
    return n;
}

static void
build(
    struct Fixture* fx,
    struct WorldEntity_Player const* player,
    int* out_lift)
{
    OverlayStage_Reset(&fx->app.overlays);
    *out_lift = app_overlay_build_player(&fx->app, player, &fx->assets);
}

static void
test_a_hidden_player_keeps_only_its_bars(void)
{
    struct Fixture fx;
    struct WorldEntity_Player* player;
    int lift = 0;

    printf("TEST: a hidden player's overheads stop after its health bar\n");
    fixture_init(&fx);
    player = spawn_marked(&fx, 9);

    build(&fx, player, &lift);
    TEST_ASSERT(rects(&fx) == 2, "visible: the bar's two halves");
    TEST_ASSERT(items_of_scene(&fx, SCENE_HITMARKS) == 1, "its splat");
    TEST_ASSERT(items_of_scene(&fx, SCENE_PK) == 1, "its skull");
    TEST_ASSERT(items_of_scene(&fx, SCENE_PRAYER) == 1, "its prayer icon");
    TEST_ASSERT(lift == 5 + 25 + 25, "and the stack above the bar: 5, the skull's 25, the icon's 25");

    player->hidden = 1;
    build(&fx, player, &lift);
    TEST_ASSERT(rects(&fx) == 2, "hidden: the bar is still drawn (before the reference's return)");
    TEST_ASSERT(items_of_scene(&fx, SCENE_HITMARKS) == 0, "no splat");
    TEST_ASSERT(items_of_scene(&fx, SCENE_PK) == 0, "no skull");
    TEST_ASSERT(items_of_scene(&fx, SCENE_PRAYER) == 0, "no prayer icon");
    TEST_ASSERT(lift == 5, "and nothing stacked above the bar");

    /* The local player, no exception. */
    fx.local->skull_icon = 0;
    fx.local->hidden = 1;
    build(&fx, fx.local, &lift);
    TEST_ASSERT(items_of_scene(&fx, SCENE_PK) == 0, "a hidden local player shows no skull either");
    fixture_free(&fx);
}

static void
test_no_hint_arrow_over_a_hidden_player(void)
{
    struct Fixture fx;
    struct WorldEntity_Player* player;

    printf("TEST: no hint arrow over a hidden player\n");
    fixture_init(&fx);
    player = spawn_marked(&fx, 9);
    fx.app.hint_arrow.type = RS_HINT_ARROW_PLAYER;
    fx.app.hint_arrow.target = 9;

    OverlayStage_Reset(&fx.app.overlays);
    app_overlay_build_hint_arrow(&fx.app, 5);
    TEST_ASSERT(items_of_scene(&fx, SCENE_HINT) == 1, "a visible player's arrow is drawn");

    player->hidden = 1;
    OverlayStage_Reset(&fx.app.overlays);
    app_overlay_build_hint_arrow(&fx.app, 5);
    TEST_ASSERT(items_of_scene(&fx, SCENE_HINT) == 0, "a hidden player's is not");
    fixture_free(&fx);
}

int
main(void)
{
    ToriMem_Init();
    g_failures = 0;
    test_a_hidden_player_keeps_only_its_bars();
    test_no_hint_arrow_over_a_hidden_player();
    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("player overheads tests passed\n");
    return 0;
}
