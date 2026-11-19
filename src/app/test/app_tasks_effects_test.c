/*
 * Projectiles and map graphics exist at their packet; only their bodies wait
 * (app/app_tasks_effects.c).
 *
 * Driven through the packet executor on an App with a real World and the
 * assets table, with a fake spotanim loader that answers when the test says:
 *
 *   - MAP_PROJANIM, SCRIPTEDPROJ_ADD and MAP_ANIM make their World row at
 *     once, bodiless, with one body task pending for it, keyed on the row;
 *   - F1: a SCRIPTEDPROJ_CHANGE right behind its ADD finds the projectile
 *     while its body is still loading;
 *   - F2: a second SCRIPTEDPROJ_ADD for a slot in use is dropped (the
 *     rev-241 reference's `if (var16 == null)`), whether or not the first
 *     has landed, and is not listed as a projectile either;
 *   - a map graphic whose body failed, or whose task was cancelled, takes its
 *     row with it; a projectile's row flies on unseen;
 *   - F3: a MAP_ANIM for a deck is bounded by the deck's scene and lands in
 *     the deck's world; a PROJANIM_SPECIFIC that arrives while the root scene
 *     is rebuilt is placed in the scene it was sent for;
 *   - the effect log hands the packets back as rows, the pair of a
 *     projectile decoded whole.
 */

#include "app.h"
#include "app/app_internal.h"
#include "game/rs_gameproto_exec.h"
#include "game/rs_server_state.h"
#include "net/rev/revpacket.h"
#include "world/world.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;

#define CHECK(cond, what)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/* ---- the fake spotanim loader ------------------------------------------- */

static int g_started[64];
static int g_started_count;

static enum AssetStart
fake_start(
    void* data,
    struct Assets* assets,
    struct AssetKey key)
{
    (void)data;
    (void)assets;
    assert(g_started_count < 64);
    g_started[g_started_count++] = key.id;
    return ASSET_START_LOADING;
}

static bool
fake_resident(
    void* data,
    struct AssetKey key)
{
    (void)data;
    (void)key;
    return false;
}

/* ---- the fixture --------------------------------------------------------- */

struct Fixture
{
    struct App* app;
    struct World* root;
    struct World* deck;
    struct ToriRS_IOBatch* io;
    struct RS_ServerState state;
    struct RS_GameProtoCtx ctx;
};

static char g_dummy_builder[1];

static void
fixture_init(struct Fixture* fx)
{
    struct AssetLoader loader;

    memset(fx, 0, sizeof(*fx));
    fx->app = calloc(1, sizeof(*fx->app));
    assert(fx->app);
    fx->app->frame_runner.queue = ToriRS_TaskQueue_New();
    fx->app->packet_runner.queue = ToriRS_TaskQueue_New();
    fx->app->assets = Assets_New();
    loader.start = fake_start;
    loader.resident = fake_resident;
    loader.data = NULL;
    Assets_SetLoader(fx->app->assets, ASSET_KIND_SPOTANIM, &loader);
    App_PendingInit(fx->app);
    fx->io = ToriRS_IOBatch_New();

    /* The root scene: centred on zone 50,50, so its south-west tile is
     * 352,352, as the accepted REBUILD says. */
    fx->root = World_New();
    World_ResetScene(fx->root, 50, 50, 104);
    World_SetLoadComplete(fx->root, true);
    fx->app->world = fx->root;
    RS_RebuildRegion_SetCentre(&fx->app->rebuild_region, 50, 50);
    WorldviewRegistry_Init(&fx->app->worldviews);
    WorldviewRegistry_RegisterRoot(
        &fx->app->worldviews, fx->root, (struct WorldBuilder*)g_dummy_builder);

    /* A deck: a 16-tile world of its own, in view 3. */
    fx->deck = World_New();
    World_ResetScene(fx->deck, 1, 1, 16);
    World_SetLoadComplete(fx->deck, true);
    WorldviewRegistry_Register(
        &fx->app->worldviews,
        3,
        2009,
        fx->deck,
        (struct WorldBuilder*)g_dummy_builder,
        6400,
        6400,
        8,
        8,
        WORLDVIEW_ROOT);

    App_ServerStateBind(fx->app, &fx->state);
    fx->ctx.state = &fx->state;
    fx->ctx.app = fx->app;
}

static void
fixture_free(struct Fixture* fx)
{
    Assets_Close(fx->app->assets);
    ToriRS_TaskQueue_Free(fx->app->frame_runner.queue);
    ToriRS_TaskQueue_Free(fx->app->packet_runner.queue);
    CHECK(RS_Pending_Listed(fx->app->pending) == 0, "every body task unlisted itself");
    App_PendingFree(fx->app);
    Assets_Free(fx->app->assets);
    ToriRS_IOBatch_Free(fx->io);
    World_Free(fx->root);
    World_Free(fx->deck);
    free(fx->app);
}

/* Step every task on the parallel queue until none moves, as the frame's pump
 * does. */
static void
pump(struct Fixture* fx)
{
    struct ToriRS_TaskQueue* queue = fx->app->frame_runner.queue;

    for( int pass = 0; pass < 16; pass++ )
    {
        int moved = 0;
        struct ToriRS_Task* t = queue->head;

        while( t )
        {
            struct ToriRS_Task* next = t->next;
            int stat;

            t->blocked = 0;
            stat = ToriRS_TaskQueue_RunTask(queue, fx->io, t);
            moved |= stat == TORIRS_ASYNCIO_STAT_DONE;
            t = next;
        }
        if( !moved )
            return;
    }
}

static int
count_active(struct World_EntityPool const* pool)
{
    int n = 0;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
        n++;
    return n;
}

static void
exec(
    struct Fixture* fx,
    struct RevPacket* p)
{
    RS_GameProto_Exec(&fx->ctx, p);
}

/* ---- the cases ----------------------------------------------------------- */

static void
test_scripted_projectile_exists_at_its_add(void)
{
    struct Fixture fx;
    struct RevPacket p;
    struct WorldEntity_Projectile* proj;
    struct App_DriveProjectile rows[8];
    int idx;

    printf("SCRIPTEDPROJ_ADD: the projectile exists at once; CHANGE finds it; a second ADD is dropped\n");
    fixture_init(&fx);
    RS_ZoneInbox_SetBase(&fx.app->zone_inbox, 16, 24, 0);

    memset(&p, 0, sizeof(p));
    p.packet_type = PKT_NAME_SCRIPTEDPROJ_ADD;
    p._scriptedproj_add.pos = (3 << 4) | 5; /* tile 19,29 */
    p._scriptedproj_add.slot = 7;
    p._scriptedproj_add.spotanim = 1500;
    p._scriptedproj_add.target_x = 352 + 30;
    p._scriptedproj_add.target_z = 352 + 31;
    p._scriptedproj_add.start_delay = 10;
    p._scriptedproj_add.end_delay = 70;
    p._scriptedproj_add.curve_h = 100;
    p._scriptedproj_add.curve_a = 101;
    p._scriptedproj_add.curve_t = 102;
    exec(&fx, &p);

    idx = World_ProjectileFindScriptedSlot(fx.root, 7);
    CHECK(idx >= 0, "the slot's projectile is in the world at its packet");
    proj = idx >= 0 ? World_EntityPoolGet(&fx.root->entities.projectile, idx) : NULL;
    CHECK(proj && proj->element_id == -1, "bodiless: no scene element yet");
    CHECK(proj && proj->spotanim_id == 1500, "it knows its graphic");
    CHECK(proj && proj->serial != 0, "it has a spawn serial");
    CHECK(proj && proj->src_x == 19 * 128 + 64 && proj->src_z == 29 * 128 + 64, "from the zone tile");
    CHECK(proj && proj->wire_dst_x == 30 * 128 + 64, "to the target, in the root scene's frame");
    CHECK(proj && !proj->curves_ready, "its curves are not run before they load");
    CHECK(proj && RS_Pending_Find(fx.app->pending, App_EffectsProjectileBodyKind(),
                      RS_PendingKey_Projectile(idx, proj->serial)) != NULL,
        "its body is pending, keyed on the row");
    CHECK(fx.app->packet_runner.queue->head == NULL, "nothing holds the packet queue");
    pump(&fx);
    CHECK(g_started_count >= 1 && g_started[g_started_count - 1] == 1500,
        "the pump started the graphic's load");

    /* F1: the CHANGE right behind it finds it. */
    memset(&p, 0, sizeof(p));
    p.packet_type = PKT_NAME_SCRIPTEDPROJ_CHANGE;
    p._scriptedproj_change.slot = 7;
    p._scriptedproj_change.target_x = 352 + 40;
    p._scriptedproj_change.target_z = 352 + 41;
    p._scriptedproj_change.freeze_cycles = -1;
    exec(&fx, &p);
    CHECK(proj && proj->retarget_pending, "F1: CHANGE retargets a projectile still loading");
    CHECK(proj && proj->retarget_dst_x == 40 * 128 + 64, "F1: to the new target");

    /* F2: a second ADD for the slot keeps the first, and is not listed. */
    memset(&p, 0, sizeof(p));
    p.packet_type = PKT_NAME_SCRIPTEDPROJ_ADD;
    p._scriptedproj_add.pos = (1 << 4) | 1;
    p._scriptedproj_add.slot = 7;
    p._scriptedproj_add.spotanim = 1501;
    p._scriptedproj_add.target_x = 352 + 5;
    p._scriptedproj_add.target_z = 352 + 5;
    p._scriptedproj_add.end_delay = 40;
    exec(&fx, &p);
    CHECK(count_active(&fx.root->entities.projectile) == 1, "F2: one projectile for the slot");
    CHECK(World_ProjectileFindScriptedSlot(fx.root, 7) == idx, "F2: the first one");
    CHECK(RS_Pending_Count(fx.app->pending, App_EffectsProjectileBodyKind()) == 1,
        "F2: one body pending");
    CHECK(App_DriveEffectProjectiles(fx.app, rows, 8) == 1, "F2: the dropped ADD is not listed");
    CHECK(rows[0].spotanim == 1500 && rows[0].src_x == 352 + 19 && rows[0].src_z == 352 + 29,
        "the log's row is the first ADD, in absolute tiles");
    CHECK(rows[0].dst_x == 352 + 30 && rows[0].start_delay == 10 && rows[0].end_delay == 70,
        "with its destination and both delays");

    /* The body fails: the projectile flies on unseen, its task is gone. */
    Assets_Finished(fx.app->assets, AssetKey_Make(ASSET_KIND_SPOTANIM, 1500, 0), false);
    pump(&fx);
    CHECK(World_ProjectileFindScriptedSlot(fx.root, 7) == idx, "a failed body leaves the flight");
    CHECK(proj && proj->element_id == -1, "unseen");
    CHECK(RS_Pending_Count(fx.app->pending, App_EffectsProjectileBodyKind()) == 0,
        "and its task ended");
    fixture_free(&fx);
    g_started_count = 0;
    printf("PASS\n");
}

static void
test_projanim_and_map_anim_exist_at_once(void)
{
    struct Fixture fx;
    struct RevPacket p;
    struct App_DriveMapAnim anims[8];
    struct App_DriveProjectile shots[8];
    int idx;

    printf("MAP_PROJANIM and MAP_ANIM: rows at once; a map graphic's failed body takes its row\n");
    fixture_init(&fx);
    RS_ZoneInbox_SetBase(&fx.app->zone_inbox, 40, 48, 0);

    memset(&p, 0, sizeof(p));
    p.packet_type = PKT_NAME_MAP_PROJANIM;
    p._map_projanim.pos = (2 << 4) | 3; /* 42,51 */
    p._map_projanim.dx_offset = 4;
    p._map_projanim.dz_offset = -1;
    p._map_projanim.spotanim = 91;
    p._map_projanim.start_delay = 31;
    p._map_projanim.end_delay = 60;
    p._map_projanim.target = 12;
    exec(&fx, &p);
    CHECK(count_active(&fx.root->entities.projectile) == 1, "the projectile is in the world");
    idx = World_EntityPoolHead(&fx.root->entities.projectile);
    {
        struct WorldEntity_Projectile const* proj =
            World_EntityPoolGet(&fx.root->entities.projectile, idx);
        CHECK(proj->element_id == -1 && proj->spotanim_id == 91, "bodiless, its graphic known");
        CHECK(proj->t1 == 31 && proj->t2 == 60 && proj->cycle == 0, "its flight counts from now");
        CHECK(proj->wire_dst_x == 46 * 128 + 64 && proj->wire_dst_z == 50 * 128 + 64, "aimed");
    }
    CHECK(App_DriveEffectProjectiles(fx.app, shots, 8) == 1, "listed");
    CHECK(shots[0].src_x == 352 + 42 && shots[0].dst_x == 352 + 46 && shots[0].target == 12,
        "as the packet said it");

    memset(&p, 0, sizeof(p));
    p.packet_type = PKT_NAME_MAP_ANIM;
    p._map_anim.pos = (1 << 4) | 2; /* 41,50 */
    p._map_anim.id = 92;
    p._map_anim.delay = 25;
    exec(&fx, &p);
    CHECK(count_active(&fx.root->entities.spotanim) == 1, "the map graphic is in the world");
    {
        int s_idx = World_EntityPoolHead(&fx.root->entities.spotanim);
        struct WorldEntity_Spotanim const* s = World_EntityPoolGet(&fx.root->entities.spotanim, s_idx);
        CHECK(s->element_id == -1 && s->spotanim_id == 92, "bodiless, its graphic known");
        CHECK(s->idle_cycles == 25 && !s->active, "its delay counts from now");
        CHECK(s->lifetime == WORLD_SPOTANIM_LIFETIME_PENDING, "its lifetime is the body's to say");
        CHECK(RS_Pending_Find(fx.app->pending, App_EffectsMapAnimBodyKind(),
                  RS_PendingKey_Id(s_idx, s->serial)) != NULL,
            "its body is pending, keyed on the row");
    }
    CHECK(App_DriveEffectMapAnims(fx.app, anims, 8) == 1, "listed");
    CHECK(anims[0].x == 352 + 41 && anims[0].z == 352 + 50 && anims[0].spotanim == 92,
        "in absolute tiles");

    /* A failed graphic would never draw nor expire: its row goes. The
     * projectile's flies on. */
    pump(&fx);
    Assets_Finished(fx.app->assets, AssetKey_Make(ASSET_KIND_SPOTANIM, 92, 0), false);
    Assets_Finished(fx.app->assets, AssetKey_Make(ASSET_KIND_SPOTANIM, 91, 0), false);
    pump(&fx);
    CHECK(count_active(&fx.root->entities.spotanim) == 0, "a failed map graphic is removed");
    CHECK(count_active(&fx.root->entities.projectile) == 1, "a failed projectile flies on");
    CHECK(fx.app->frame_runner.queue->head == NULL, "both tasks ended");

    /* A logout cancels a pending graphic: it takes its row with it. */
    memset(&p, 0, sizeof(p));
    p.packet_type = PKT_NAME_MAP_ANIM;
    p._map_anim.pos = (1 << 4) | 2;
    p._map_anim.id = 93;
    exec(&fx, &p);
    CHECK(count_active(&fx.root->entities.spotanim) == 1, "a second graphic");
    CHECK(RS_Pending_Cancel(fx.app->pending, RS_PendingScope_All()) == 1, "cancelled");
    pump(&fx);
    CHECK(count_active(&fx.root->entities.spotanim) == 0, "a cancelled graphic is removed");
    fixture_free(&fx);
    g_started_count = 0;
    printf("PASS\n");
}

static void
test_deck_map_anim_uses_the_deck(void)
{
    struct Fixture fx;
    struct RevPacket p;
    struct App_DriveMapAnim anims[8];

    printf("F3: a deck's MAP_ANIM is bounded by and lands in the deck's world\n");
    fixture_init(&fx);
    WorldviewRegistry_SetActive(&fx.app->worldviews, 3);

    /* Tile 20,20 is in the root scene but not on a 16-tile deck. */
    RS_ZoneInbox_SetBase(&fx.app->zone_inbox, 16, 16, 0);
    memset(&p, 0, sizeof(p));
    p.packet_type = PKT_NAME_MAP_ANIM;
    p._map_anim.pos = (4 << 4) | 4;
    p._map_anim.id = 92;
    exec(&fx, &p);
    CHECK(count_active(&fx.deck->entities.spotanim) == 0, "F3: off the deck: dropped");
    CHECK(count_active(&fx.root->entities.spotanim) == 0, "F3: and not put in the root");

    RS_ZoneInbox_SetBase(&fx.app->zone_inbox, 0, 8, 0);
    memset(&p, 0, sizeof(p));
    p.packet_type = PKT_NAME_MAP_ANIM;
    p._map_anim.pos = (5 << 4) | 1; /* 5,9 */
    p._map_anim.id = 92;
    exec(&fx, &p);
    CHECK(count_active(&fx.deck->entities.spotanim) == 1, "F3: on the deck: the deck's row");
    CHECK(count_active(&fx.root->entities.spotanim) == 0, "F3: none in the root");
    CHECK(App_DriveEffectMapAnims(fx.app, anims, 8) == 1, "listed");
    CHECK(anims[0].x == fx.deck->_base_tile_x + 5 && anims[0].z == fx.deck->_base_tile_z + 9,
        "F3: in the deck's frame, not the root's");
    WorldviewRegistry_SetActive(&fx.app->worldviews, WORLDVIEW_ROOT);
    (void)RS_Pending_Cancel(fx.app->pending, RS_PendingScope_All());
    pump(&fx);
    fixture_free(&fx);
    g_started_count = 0;
    printf("PASS\n");
}

static void
test_projanim_specific_held_for_the_new_scene(void)
{
    struct Fixture fx;
    struct RevPacket p;
    struct WorldEntity_Projectile const* proj;

    printf("F3: a PROJANIM_SPECIFIC held through a rebuild lands in the scene it was sent for\n");
    fixture_init(&fx);

    /* A REBUILD to zone 60,60 was accepted; the root world still holds the
     * old scene (base 352) and is being rebuilt. */
    RS_RebuildRegion_SetCentre(&fx.app->rebuild_region, 60, 60);
    World_SetLoadComplete(fx.root, false);
    memset(&p, 0, sizeof(p));
    p.packet_type = PKT_NAME_PROJANIM_SPECIFIC;
    p._projanim_specific.x = 432 + 10;
    p._projanim_specific.z = 432 + 12;
    p._projanim_specific.dst_x = 432 + 14;
    p._projanim_specific.dst_z = 432 + 12;
    p._projanim_specific.spotanim = 91;
    p._projanim_specific.end_delay = 50;
    exec(&fx, &p);
    CHECK(RS_ZoneInbox_HeldCount(&fx.app->zone_inbox) == 1, "held while the scene builds");
    CHECK(count_active(&fx.root->entities.projectile) == 0, "nothing in the old scene");

    World_ResetScene(fx.root, 60, 60, 104);
    World_SetLoadComplete(fx.root, true);
    RS_GameProto_FlushPendingZone(&fx.ctx);
    CHECK(count_active(&fx.root->entities.projectile) == 1, "applied once the scene is built");
    proj = World_EntityPoolGet(&fx.root->entities.projectile, World_EntityPoolHead(&fx.root->entities.projectile));
    CHECK(proj->src_x == 10 * 128 + 64 && proj->src_z == 12 * 128 + 64,
        "F3: from the tile in the new scene, not one measured against the old");
    CHECK(proj->wire_dst_x == 14 * 128 + 64, "F3: to the new scene's tile");
    (void)RS_Pending_Cancel(fx.app->pending, RS_PendingScope_All());
    pump(&fx);
    fixture_free(&fx);
    g_started_count = 0;
    printf("PASS\n");
}

int
main(void)
{
    test_scripted_projectile_exists_at_its_add();
    test_projanim_and_map_anim_exist_at_once();
    test_deck_map_anim_uses_the_deck();
    test_projanim_specific_held_for_the_new_scene();
    if( g_failures )
    {
        printf("app_tasks_effects_test: %d check(s) FAILED\n", g_failures);
        return 1;
    }
    printf("app_tasks_effects_test: all cases passed\n");
    return 0;
}
