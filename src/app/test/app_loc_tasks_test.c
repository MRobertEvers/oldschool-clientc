/*
 * The locs family's parallel tasks (app/app_tasks_locs.c) on a real root
 * world and builder, a deck view with its own, the real parallel queue and the
 * real pending index, with a fake loader for the assets a loc change waits on.
 *
 * The loc lane's cases and the zone inbox's moved here, restated for what
 * replaced them, plus the faults this step owns:
 *
 *   - one pending change per view, tile, level and layer: a later change to
 *     the key replaces the pending one (a delete replaces a pending add), a
 *     delete applies on the queue's first pass, an add once its config is
 *     here, and another tile's slow add holds nothing (the lane's global
 *     order is gone);
 *   - a zone reset cancels what is pending in its zone and nothing outside
 *     it (L3);
 *   - a LOC_MERGE fires in a deck view as in the root, at its own time (L1);
 *   - zone packets held for a view that is not built wait for THAT view, not
 *     the root, apply in arrival order once it is, and a FULL_FOLLOWS among
 *     them resets in its place (L3, L4); a held packet for a view that died
 *     is dropped, and a logout cancels them;
 *   - a change still loading when its scene is rebuilt is carried into the
 *     new scene by its absolute tile and lands there; one that had landed is
 *     re-placed (L2);
 *   - the driver's rows are the pending changes a packet made.
 *
 * The world the fixture builds holds no cache: a loc's config is put in the
 * cache when its read "comes back", a loc with no models, so an add's apply
 * is observed in the world's loc-change records (pushed by the apply) rather
 * than as a drawn model.
 *
 * Run: make -C src test-loc-tasks
 */

#include "app.h"
#include "app/app_internal.h"
#include "engine/dat2/dat2_buildcache.h"
#include "game/rs_gameproto_exec.h"
#include "net/rev/revpacket.h"
#include "platform/platform_x_io.h"
#include "world/world.h"
#include "world/worldview.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;

#define T_ASSERT(cond, what)                                                                       \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/* ---- the fixture --------------------------------------------------------- */

#define FAKE_MAX 64
#define DECK_WIRE 2009
#define DECK_VIEW 3

struct Fixture
{
    struct App* app;
    struct Dat2BuildCache* bc;
    struct ToriDraw_Scene* scene;
    struct PlatformX_IO* px;
    /* The deck's world and builder: owned by the registry once registered. */
    struct World* deck;
    struct WorldBuilder* deck_builder;
    /* Asset keys the fake loader was asked for (kind, id). */
    struct AssetKey started[FAKE_MAX];
    int started_count;
};

static struct Fixture g_fx;

static enum AssetStart
fake_start(
    void* data,
    struct Assets* assets,
    struct AssetKey key)
{
    struct Fixture* fx = data;

    (void)assets;
    assert(fx->started_count < FAKE_MAX);
    fx->started[fx->started_count++] = key;
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

/* The read for loc config `id` came back: a 1x1 loc with no models, put
 * where the loader would have put it. */
static void
config_arrives(int id)
{
    struct CacheProvider* provider = g_fx.app->provider;

    if( !CacheProvider_LocationHas(provider, id) )
    {
        struct ToriRS_Location* loc = calloc(1, sizeof(*loc));

        assert(loc);
        loc->id = id;
        loc->size_x = 1;
        loc->size_z = 1;
        loc->seq_id = -1;
        CacheProvider_LocationAdd(provider, id, loc);
    }
    Assets_Finished(g_fx.app->assets, AssetKey_Make(ASSET_KIND_CONFIG, id, ASSET_CONFIG_LOC), true);
}

/* Step the parallel queue until nothing more can progress, as the frame
 * does. */
static void
pump(void)
{
    for( int i = 0; i < 256; i++ )
        if( TaskRunner_Step(&g_fx.app->frame_runner) != TASK_RUNNER_PROGRESSED )
            return;
    assert(0 && "the parallel queue never settled");
}

static void
cycles(int n)
{
    for( int i = 0; i < n; i++ )
        FrameClockSerial_NoteLogicCycle(&g_fx.app->frame_clock.serial);
}

static struct World*
make_world(struct WorldBuilder** out_builder)
{
    struct World* world = World_New();
    struct WorldBuilder* builder;

    assert(world);
    World_SetScene(world, g_fx.scene);
    builder = WorldBuilder_New(world, dat2_buildcache_as_provider(g_fx.bc), g_fx.scene, &g_fx.app->varps);
    assert(builder);
    WorldBuilder_RebuildCenterzoneBegin(builder, 50, 50, 64);
    *out_builder = builder;
    return world;
}

static void
fixture_init(void)
{
    struct App* app;
    struct AssetLoader loader;
    struct World* root;
    struct WorldBuilder* root_builder;
    static enum AssetKind const faked[] = { ASSET_KIND_CONFIG, ASSET_KIND_MODEL, ASSET_KIND_SEQUENCE };

    memset(&g_fx, 0, sizeof(g_fx));
    ToriDraw_Init();
    g_fx.scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_HIGH_8K);
    assert(g_fx.scene);
    g_fx.bc = dat2_buildcache_new();
    g_fx.px = PlatformX_IO_New();
    app = calloc(1, sizeof(*app));
    assert(app);
    g_fx.app = app;
    app->features.name = "test";
    app->cfg.cache_kind = APP_CACHE_DAT2;
    app->provider = dat2_buildcache_as_provider(g_fx.bc);
    app->scene = g_fx.scene;
    VarPManager_Init(&app->varps);
    RS_EntitySync_Init(&app->esync);
    App_AssetsInit(app);
    App_PendingInit(app);
    loader.start = fake_start;
    loader.resident = fake_resident;
    loader.data = &g_fx;
    for( size_t i = 0; i < sizeof(faked) / sizeof(faked[0]); i++ )
        Assets_SetLoader(app->assets, faked[i], &loader);
    app->packet_runner.queue = ToriRS_TaskQueue_New();
    app->frame_runner.queue = ToriRS_TaskQueue_New();
    app->frame_runner.io = ToriRS_IOBatch_New();
    app->frame_runner.px = g_fx.px;
    app->frame_runner.parallel = 1;

    root = make_world(&root_builder);
    World_SetLoadComplete(root, true);
    app->world = root;
    app->world_builder = root_builder;
    WorldviewRegistry_Init(&app->worldviews);
    WorldviewRegistry_RegisterRoot(&app->worldviews, root, root_builder);

    /* A deck spawned and not built: its REBUILD_WORLDENTITY has not landed. */
    g_fx.deck = make_world(&g_fx.deck_builder);
    World_SetLoadComplete(g_fx.deck, false);
    WorldviewRegistry_Register(
        &app->worldviews, DECK_VIEW, DECK_WIRE, g_fx.deck, g_fx.deck_builder, 6400, 6400, 64, 64,
        WORLDVIEW_ROOT);
}

/* The loc-change record a world keeps for a tile and layer, or NULL. */
static struct World_LocChange const*
record_at(
    struct World const* world,
    int x,
    int z,
    int level,
    int layer)
{
    for( int i = 0; i < world->loc_change_count; i++ )
    {
        struct World_LocChange const* rec = &world->loc_changes[i];
        if( rec->x == x && rec->z == z && rec->level == level && rec->layer == layer )
            return rec;
    }
    return NULL;
}

/* A wall (shape 0, layer 0) and a ground decoration (shape 22, layer 3). */
enum
{
    SHAPE_WALL = 0,
    SHAPE_DECOR = 22,
    LAYER_WALL = 0,
    LAYER_DECOR = 3
};

/* ---- one change per tile and layer ---------------------------------------- */

static void
test_a_delete_applies_at_once_and_an_add_waits(void)
{
    struct App* app = g_fx.app;
    struct World* world = app->world;
    struct World_LocChange const* rec;

    printf("loc tasks: a delete applies on the first pass, an add once its config is here\n");
    App_WorldLocChange(app, 10, 10, 0, 1001, SHAPE_WALL, 0);
    App_WorldLocChange(app, 11, 10, 0, -1, SHAPE_WALL, 0);
    T_ASSERT(app->packet_runner.queue->head == NULL, "nothing on the packet queue");
    pump();
    rec = record_at(world, 11, 10, 0, LAYER_WALL);
    T_ASSERT(rec && rec->new_type == -1, "the delete applied on the first pass");
    T_ASSERT(record_at(world, 10, 10, 0, LAYER_WALL) == NULL, "the add has not");
    T_ASSERT(
        app_loc_change_pending(app, WORLDVIEW_ROOT, 10, 10, 0, LAYER_WALL) != NULL,
        "the add is pending for its tile and layer");
    T_ASSERT(
        app_loc_change_pending(app, WORLDVIEW_ROOT, 11, 10, 0, LAYER_WALL) == NULL,
        "the delete is not");
    config_arrives(1001);
    pump();
    rec = record_at(world, 10, 10, 0, LAYER_WALL);
    T_ASSERT(rec && rec->new_type == 1001, "the add applied once its config came back");
    T_ASSERT(
        app_loc_change_pending(app, WORLDVIEW_ROOT, 10, 10, 0, LAYER_WALL) == NULL,
        "and is no longer pending");
}

static void
test_a_later_change_replaces_the_pending_one(void)
{
    struct App* app = g_fx.app;
    struct World* world = app->world;
    struct Task_LocChange* add;
    struct World_LocChange const* rec;

    printf("loc tasks: a delete replaces the add pending for its tile and layer\n");
    App_WorldLocChange(app, 20, 10, 0, 1002, SHAPE_WALL, 0);
    add = app_loc_change_pending(app, WORLDVIEW_ROOT, 20, 10, 0, LAYER_WALL);
    T_ASSERT(add != NULL, "the add is pending");
    /* Another layer of the tile is its own key. */
    App_WorldLocChange(app, 20, 10, 0, 1003, SHAPE_DECOR, 0);
    App_WorldLocChange(app, 20, 10, 0, -1, SHAPE_WALL, 0);
    T_ASSERT(add->par.cancelled, "the delete cancelled the add");
    T_ASSERT(
        app_loc_change_pending(app, WORLDVIEW_ROOT, 20, 10, 0, LAYER_DECOR) != NULL,
        "the other layer's add is untouched");
    pump();
    rec = record_at(world, 20, 10, 0, LAYER_WALL);
    T_ASSERT(rec && rec->new_type == -1, "the delete applied");
    config_arrives(1002);
    pump();
    rec = record_at(world, 20, 10, 0, LAYER_WALL);
    T_ASSERT(rec && rec->new_type == -1, "the replaced add never lands after it");
    config_arrives(1003);
    pump();
    rec = record_at(world, 20, 10, 0, LAYER_DECOR);
    T_ASSERT(rec && rec->new_type == 1003, "the other layer's add lands");
}

static void
test_a_slow_change_holds_no_other_tile(void)
{
    struct App* app = g_fx.app;
    struct World* world = app->world;
    struct World_LocChange const* rec;

    printf("loc tasks: one slow add holds no other tile's change\n");
    App_WorldLocChange(app, 30, 10, 0, 1004, SHAPE_WALL, 0);
    App_WorldLocChange(app, 31, 10, 0, -1, SHAPE_WALL, 0);
    App_WorldLocChange(app, 32, 10, 0, 1005, SHAPE_WALL, 0);
    pump();
    config_arrives(1005);
    pump();
    T_ASSERT(record_at(world, 30, 10, 0, LAYER_WALL) == NULL, "the slow add waits");
    rec = record_at(world, 31, 10, 0, LAYER_WALL);
    T_ASSERT(rec && rec->new_type == -1, "the delete behind it did not");
    rec = record_at(world, 32, 10, 0, LAYER_WALL);
    T_ASSERT(rec && rec->new_type == 1005, "nor the add whose config came first");
    config_arrives(1004);
    pump();
    rec = record_at(world, 30, 10, 0, LAYER_WALL);
    T_ASSERT(rec && rec->new_type == 1004, "and the slow add lands in its own time");
}

static void
test_the_driver_reads_the_pending_changes(void)
{
    struct App* app = g_fx.app;
    struct App_DriveLocChange rows[8];
    int n;

    printf("loc tasks: the driver's rows are the pending changes a packet made\n");
    App_WorldLocChange(app, 40, 12, 0, 1006, SHAPE_WALL, 1);
    n = App_DriveLocChangesPending(app, rows, 8);
    T_ASSERT(n == 1, "one pending change");
    T_ASSERT(n == 1 && rows[0].x == app->world->_base_tile_x + 40, "at its absolute tile");
    T_ASSERT(n == 1 && rows[0].loc_id == 1006 && rows[0].angle == 1, "with what it places");
    T_ASSERT(n == 1 && rows[0].layer == LAYER_WALL, "in its layer");
    App_WorldLocChange(app, 40, 12, 0, -1, SHAPE_WALL, 1);
    n = App_DriveLocChangesPending(app, rows, 8);
    T_ASSERT(n == 1 && rows[0].loc_id == -1, "the delete replaced it");
    pump();
    n = App_DriveLocChangesPending(app, rows, 8);
    T_ASSERT(n == 0, "the scene shows it: no row");
}

/* ---- the zone reset (L3) --------------------------------------------------- */

static void
test_a_zone_reset_cancels_what_is_pending_in_its_zone(void)
{
    struct App* app = g_fx.app;
    struct World* world = app->world;
    int cancelled;

    printf("loc tasks: a zone reset cancels the changes pending in its zone\n");
    App_WorldLocChange(app, 17, 18, 0, 1007, SHAPE_WALL, 0);  /* zone 16,16 */
    App_WorldLocChange(app, 25, 18, 0, 1008, SHAPE_WALL, 0);  /* zone 24,16 */
    App_WorldLocChange(app, 17, 18, 1, 1009, SHAPE_WALL, 0);  /* plane 1 */
    cancelled = App_LocChangesCancelZone(app, WORLDVIEW_ROOT, 16, 16, 0);
    T_ASSERT(cancelled == 1, "one change was pending in the zone on its plane");
    T_ASSERT(
        app_loc_change_pending(app, WORLDVIEW_ROOT, 17, 18, 0, LAYER_WALL) == NULL,
        "it is gone");
    T_ASSERT(
        app_loc_change_pending(app, WORLDVIEW_ROOT, 25, 18, 0, LAYER_WALL) != NULL,
        "another zone's stays");
    T_ASSERT(
        app_loc_change_pending(app, WORLDVIEW_ROOT, 17, 18, 1, LAYER_WALL) != NULL,
        "another plane's stays");
    pump();
    config_arrives(1007);
    config_arrives(1008);
    config_arrives(1009);
    pump();
    T_ASSERT(record_at(world, 17, 18, 0, LAYER_WALL) == NULL, "the cancelled change never lands");
    T_ASSERT(record_at(world, 25, 18, 0, LAYER_WALL) != NULL, "the other zone's does");
}

/* ---- LOC_MERGE in a deck (L1) ---------------------------------------------- */

static void
test_a_merge_fires_in_its_own_view(void)
{
    struct App* app = g_fx.app;
    struct World_LocChange const* rec;

    printf("loc tasks: a LOC_MERGE hides and restores in the view it was sent to\n");
    /* The deck is built for this one. */
    World_SetLoadComplete(g_fx.deck, true);
    WorldviewRegistry_SetActive(&app->worldviews, DECK_VIEW);
    App_WorldLocMerge(app, 5, 6, 0, 2001, SHAPE_WALL, 0, 2, 5, 77 /* no such player */);
    WorldviewRegistry_SetActive(&app->worldviews, WORLDVIEW_ROOT);
    pump();
    T_ASSERT(record_at(g_fx.deck, 5, 6, 0, LAYER_WALL) == NULL, "not before its start");
    cycles(3);
    pump();
    rec = record_at(g_fx.deck, 5, 6, 0, LAYER_WALL);
    T_ASSERT(rec && rec->new_type == -1, "the deck's loc is hidden at start+1");
    T_ASSERT(rec && rec->end_time >= 0, "by a merge's record, which a rebuild drops");
    T_ASSERT(record_at(app->world, 5, 6, 0, LAYER_WALL) == NULL, "the root's tile is untouched");
    cycles(3);
    pump();
    rec = record_at(g_fx.deck, 5, 6, 0, LAYER_WALL);
    T_ASSERT(rec && rec->end_time < 0, "restored at end+1: what stood before is back");
    World_SetLoadComplete(g_fx.deck, false);
}

/* ---- zone packets for an unbuilt view (L3, L4) ------------------------------ */

static void
hold(
    enum GameProtoPktName name,
    int pos,
    int loc_id,
    int info,
    int view)
{
    struct PktZoneSubPacket pkt;

    memset(&pkt, 0, sizeof(pkt));
    pkt.name = name;
    if( name == PKT_NAME_LOC_ADD_CHANGE )
    {
        pkt._loc_add_change.pos = pos;
        pkt._loc_add_change.loc_id = loc_id;
        pkt._loc_add_change.info = info;
        pkt._loc_add_change.op_flags = 0x1f;
    }
    else if( name == PKT_NAME_LOC_DEL )
    {
        pkt._loc_del.pos = pos;
        pkt._loc_del.info = info;
    }
    App_ZonePacketHold(g_fx.app, &pkt, 8, 8, 0, view);
}

static void
test_held_zone_packets_wait_for_their_own_view(void)
{
    struct App* app = g_fx.app;
    struct World_LocChange const* rec;
    int const pos = (2 << 4) | 3; /* tile 10,11 of zone 8,8 */

    printf("loc tasks: zone packets held for an unbuilt deck wait for THAT view\n");
    /* add, then a FULL_FOLLOWS reset of the zone, then a delete: in arrival
     * order the reset clears the add's change and the delete lands last. */
    hold(PKT_NAME_LOC_ADD_CHANGE, pos, 3001, SHAPE_WALL << 2, DECK_VIEW);
    {
        struct PktZoneSubPacket reset;

        memset(&reset, 0, sizeof(reset));
        reset.name = PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS;
        App_ZonePacketHold(app, &reset, 8, 8, 0, DECK_VIEW);
    }
    hold(PKT_NAME_LOC_ADD_CHANGE, (5 << 4) | 5, 3002, SHAPE_WALL << 2, DECK_VIEW);
    T_ASSERT(App_ZonePacketsHeld(app, DECK_VIEW), "the deck has packets held");
    T_ASSERT(!App_ZonePacketsHeld(app, WORLDVIEW_ROOT), "the root has none");
    pump();
    T_ASSERT(app->world->load_complete, "the root is built");
    T_ASSERT(
        app_loc_change_pending(app, DECK_VIEW, 10, 11, 0, LAYER_WALL) == NULL &&
            app_loc_change_pending(app, DECK_VIEW, 13, 13, 0, LAYER_WALL) == NULL,
        "and the deck's packets did not apply to it");
    T_ASSERT(App_ZonePacketsHeld(app, DECK_VIEW), "they still wait");

    World_SetLoadComplete(g_fx.deck, true);
    pump();
    T_ASSERT(!App_ZonePacketsHeld(app, DECK_VIEW), "built: every held packet ran");
    /* The first add was cancelled by the reset behind it, before its config
     * came back; the second is pending in the deck. */
    T_ASSERT(
        app_loc_change_pending(app, DECK_VIEW, 10, 11, 0, LAYER_WALL) == NULL,
        "the reset cancelled the add held before it");
    T_ASSERT(
        app_loc_change_pending(app, DECK_VIEW, 13, 13, 0, LAYER_WALL) != NULL,
        "the add held after it is the deck's pending change");
    config_arrives(3001);
    config_arrives(3002);
    pump();
    T_ASSERT(record_at(g_fx.deck, 10, 11, 0, LAYER_WALL) == NULL, "the reset add never lands");
    rec = record_at(g_fx.deck, 13, 13, 0, LAYER_WALL);
    T_ASSERT(rec && rec->new_type == 3002, "the later one lands in the deck");
    T_ASSERT(record_at(app->world, 13, 13, 0, LAYER_WALL) == NULL, "not in the root");

    /* L5: a deck's rebuild runs the root's finish. Rebuilt from its map, the
     * deck's landed change is put back on it. */
    World_SetLoadComplete(g_fx.deck, true);
    App_WorldViewLoadFinish(app, DECK_VIEW, 1);
    T_ASSERT(
        app_loc_change_pending(app, DECK_VIEW, 13, 13, 0, LAYER_WALL) != NULL,
        "the deck's finish re-places its landed change");
    pump();
    config_arrives(3002);
    pump();
    rec = record_at(g_fx.deck, 13, 13, 0, LAYER_WALL);
    T_ASSERT(rec && rec->new_type == 3002, "and it lands in the rebuilt deck");

    /* A packet for a deck that is built and has nothing held applies at once
     * (the live path asks App_ZonePacketsHeld); one held for a view that dies
     * before it is built is dropped. */
    World_SetLoadComplete(g_fx.deck, false);
    hold(PKT_NAME_LOC_DEL, (1 << 4) | 1, -1, SHAPE_WALL << 2, DECK_VIEW);
    T_ASSERT(App_ZonePacketsHeld(app, DECK_VIEW), "held again");
    (void)RS_Pending_Cancel(app->pending, RS_PendingScope_All());
    pump();
    T_ASSERT(!App_ZonePacketsHeld(app, DECK_VIEW), "a logout's cancel ends it");
    World_SetLoadComplete(g_fx.deck, true);
    pump();
    T_ASSERT(record_at(g_fx.deck, 9, 9, 0, LAYER_WALL) == NULL, "and it never applies");
}

/* ---- the rebuild's carry-over (L2) ---------------------------------------- */

static void
test_a_rebuild_carries_a_pending_change_over(void)
{
    struct App* app = g_fx.app;
    struct World* world = app->world;
    struct World_LocChange const* rec;
    struct Task_LocChange* old;
    struct Task_LocChange* carried;
    int const old_base_x = world->_base_tile_x;

    printf("loc tasks: a change loading across a rebuild lands in the new scene\n");
    world->loc_change_count = 0;
    /* One change landed, one still loading. */
    App_WorldLocChange(app, 50, 50, 0, 1012, SHAPE_WALL, 0);
    pump();
    config_arrives(1012);
    App_WorldLocChange(app, 52, 50, 0, 1010, SHAPE_WALL, 0);
    pump();
    rec = record_at(world, 50, 50, 0, LAYER_WALL);
    T_ASSERT(rec && rec->new_type == 1012, "the first add landed");
    old = app_loc_change_pending(app, WORLDVIEW_ROOT, 52, 50, 0, LAYER_WALL);
    T_ASSERT(old != NULL, "the second is loading");

    /* The rebuild, as the REBUILD's task runs it: the scene goes; the second
     * add's config comes back meanwhile, and it waits for the rebuild rather
     * than being dropped. */
    World_SetLoadComplete(world, false);
    config_arrives(1010);
    pump();
    T_ASSERT(!old->par.cancelled, "mid-rebuild the loaded change waits");
    T_ASSERT(record_at(world, 52, 50, 0, LAYER_WALL) == NULL, "and has not applied");
    /* The build moves the base 8 tiles east and completes, the kept records
     * shift with it, and the finish runs -- before the queue runs again. */
    world->_base_tile_x = old_base_x + 8;
    World_SetLoadComplete(world, true);
    for( int i = 0; i < world->loc_change_count; i++ )
        world->loc_changes[i].x -= 8;
    App_WorldViewLoadFinish(app, WORLDVIEW_ROOT, 1);
    T_ASSERT(old->par.cancelled, "the old scene's task is cancelled");
    carried = app_loc_change_pending(app, WORLDVIEW_ROOT, 44, 50, 0, LAYER_WALL);
    T_ASSERT(carried != NULL, "and carried to its tile in the new scene");
    T_ASSERT(carried && carried->carried, "marked as carried");
    T_ASSERT(
        app_loc_change_pending(app, WORLDVIEW_ROOT, 42, 50, 0, LAYER_WALL) != NULL,
        "the landed add is re-placed in the new scene");
    T_ASSERT(world->loc_change_count == 0, "from its record, which the re-place takes");
    pump();
    config_arrives(1012);
    config_arrives(1010);
    pump();
    rec = record_at(world, 42, 50, 0, LAYER_WALL);
    T_ASSERT(rec && rec->new_type == 1012, "the re-placed add stands in the new scene");
    rec = record_at(world, 44, 50, 0, LAYER_WALL);
    T_ASSERT(rec && rec->new_type == 1010, "the carried add lands in the new scene");
    world->_base_tile_x = old_base_x;
}

/* ---- teardown ------------------------------------------------------------- */

static void
test_a_logout_cancels_every_pending_change(void)
{
    struct App* app = g_fx.app;

    printf("loc tasks: a logout's cancel ends every pending change and merge\n");
    App_WorldLocChange(app, 60, 60, 0, 1011, SHAPE_WALL, 0);
    App_WorldLocMerge(app, 61, 60, 0, 2002, SHAPE_WALL, 0, 50, 90, 77);
    T_ASSERT(RS_Pending_Listed(app->pending) >= 2, "a change and a merge are pending");
    (void)RS_Pending_Cancel(app->pending, RS_PendingScope_All());
    pump();
    T_ASSERT(RS_Pending_Listed(app->pending) == 0, "every task exited and unlisted itself");
    T_ASSERT(app->frame_runner.queue->head == NULL, "the parallel queue is empty");
}

int
main(void)
{
    fixture_init();
    test_a_delete_applies_at_once_and_an_add_waits();
    test_a_later_change_replaces_the_pending_one();
    test_a_slow_change_holds_no_other_tile();
    test_the_driver_reads_the_pending_changes();
    test_a_zone_reset_cancels_what_is_pending_in_its_zone();
    test_a_merge_fires_in_its_own_view();
    test_held_zone_packets_wait_for_their_own_view();
    test_a_rebuild_carries_a_pending_change_over();
    test_a_logout_cancels_every_pending_change();
    if( g_failures )
    {
        printf("loc_tasks_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("loc_tasks_test: ok\n");
    return 0;
}
