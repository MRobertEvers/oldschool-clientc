/*
 * The interface-assets family's parallel tasks (app/app_tasks_if_assets.c):
 * an interface head (IF_SETMODEL, IF_SETOBJECT, IF_SETNPCHEAD) and an
 * inventory slot's icon, each one task on the parallel queue, on a stack App
 * whose assets table has fake loaders the test answers by hand.
 *
 * Pins:
 *   - U1: IF_SETOBJECT whose third field names a count variant wants the
 *     variant's model chain too (an IF3 widget draws the variant), not only
 *     the base obj's.
 *   - U2: an npc head wants its models' textures (only IF_SETMODEL did); a
 *     head task applies only to the head it was made for -- one replaced
 *     behind it lands nothing; a newer set cancels the older task before it
 *     asks for anything; a forgotten head cancels its task.
 *   - Icons are wanted when a container changes, one task per slot that
 *     lacks one, not duplicated by a second change; a slot emptied cancels
 *     its task; a load that does not come is asked for again after the
 *     INV_ICONS back-off, never given up on.
 *   - Textures: each a built model wants and the provider lacks is one
 *     publish task (app/app_textures.c), not a callback waiter; a repeat
 *     adds none, a failed load ends it and is remembered, a cancel ends it.
 *   - Sprites (moved from engine/test/cache_trim_test.c with the bridge's
 *     own pending list): a node bound to a sprite that is not resident gets a
 *     scene id at once and one fill task (Task_SpriteFill) listed in the
 *     pending index; the sprite loads once through the assets table, is asked
 *     for again only after the SPRITE back-off, fills the reserved id when it
 *     lands; an id the sprites table does not list is never bound, and a
 *     reservation it stops listing ends its task unasked; a provider with no
 *     sprite loader reserves nothing; a logout's cancel is followed by the
 *     reservation being wanted again (the bridge outlives the session).
 *
 * Run: make -C src test-if-assets-tasks
 */
#include "app.h"
#include "app/app_internal.h"
#include "asyncio.h"
#include "engine/asset_requests.h"
#include "engine/cache_provider.h"
#include "engine/uitree_scene_bridge.h"
#include "world/entity_npc.h"
#include "world/world.h"
#include "game/rs_entity_sync.h"
#include "mem/torirs_mem.h"
#include "toridraw_scene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;

#define TEST_ASSERT(cond, what)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/* ---- the fixture: an App with the queue, the tables and a provider ------- */

#define LOADS_MAX 64

struct Loads
{
    struct AssetKey keys[LOADS_MAX];
    int count;
};

static struct Loads g_loads;

static enum AssetStart
fake_start(
    void* data,
    struct Assets* assets,
    struct AssetKey key)
{
    (void)data;
    (void)assets;
    assert(g_loads.count < LOADS_MAX);
    g_loads.keys[g_loads.count++] = key;
    return ASSET_START_LOADING;
}

static bool
fake_resident(
    void* data,
    struct AssetKey key)
{
    return App_AssetResident((struct App*)data, key);
}

static int
loads_of(
    enum AssetKind kind,
    int id,
    int variant)
{
    int n = 0;
    for( int i = 0; i < g_loads.count; i++ )
        if( g_loads.keys[i].kind == (int)kind && g_loads.keys[i].id == id &&
            g_loads.keys[i].variant == variant )
            n++;
    return n;
}

struct Fixture
{
    struct App app;
    struct CacheProvider provider;
    struct CacheProviderVTable vtable;
    struct ToriRS_IOBatch* io;
};

static void
fixture_init(struct Fixture* fx)
{
    static enum AssetKind const kinds[] = {
        ASSET_KIND_MODEL, ASSET_KIND_TEXTURE, ASSET_KIND_OBJ_MODEL, ASSET_KIND_CONFIG
    };
    struct VarPType varp_type = { 0 };

    memset(fx, 0, sizeof(*fx));
    memset(&g_loads, 0, sizeof(g_loads));
    fx->provider.vtable = &fx->vtable;
    CacheProvider_InitEngineCaches(&fx->provider);
    fx->app.provider = &fx->provider;
    VarPManager_Init(&fx->app.varps);
    (void)VarPManager_SetVarpTypes(&fx->app.varps, &varp_type, 1);
    fx->app.frame_runner.queue = ToriRS_TaskQueue_New();
    fx->app.scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    assert(fx->app.scene);
    UITreeSceneBridge_Init(&fx->app.bridge, fx->app.scene, &fx->provider);
    App_AssetsInit(&fx->app);
    App_PendingInit(&fx->app);
    for( size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++ )
        App_AssetsSetLoader(&fx->app, kinds[i], fake_start, fake_resident);
    InvManager_Init(&fx->app.invs);
    fx->io = ToriRS_IOBatch_New();
}

static void
fixture_free(struct Fixture* fx)
{
    TEST_ASSERT(fx->app.frame_runner.queue->head == NULL, "every task ended");
    TEST_ASSERT(RS_Pending_Listed(fx->app.pending) == 0, "and unlisted itself");
    ToriRS_TaskQueue_Free(fx->app.frame_runner.queue);
    App_PendingFree(&fx->app);
    App_AssetsFree(&fx->app);
    ToriRS_IOBatch_Free(fx->io);
    InvManager_Free(&fx->app.invs);
    UIIfModels_Free(&fx->app.if_models);
    UITreeSceneBridge_Free(&fx->app.bridge);
    ToriDraw_SceneFree(fx->app.scene);
    VarPManager_Free(&fx->app.varps);
    CacheProvider_FreeEngineCaches(&fx->provider);
}

/* Step the parallel queue until nothing more can progress, as a frame does. */
static void
pump(struct Fixture* fx)
{
    for( int pass = 0; pass < 8; pass++ )
    {
        struct ToriRS_Task* task = fx->app.frame_runner.queue->head;
        while( task )
        {
            struct ToriRS_Task* next = task->next;
            /* A parked task is not run: only what it parked on readies it. */
            if( !task->parked )
                (void)ToriRS_TaskQueue_RunTask(fx->app.frame_runner.queue, fx->io, task);
            task = next;
        }
    }
}

static int
queued(struct Fixture const* fx)
{
    int n = 0;
    for( struct ToriRS_Task* t = fx->app.frame_runner.queue->head; t; t = t->next )
        n++;
    return n;
}

static void
add_obj(
    struct Fixture* fx,
    int obj_id,
    int model_id,
    int count_co,
    int count_obj)
{
    struct ToriRS_Objtype* obj = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*obj));
    assert(obj);
    obj->inventory_model_id = model_id;
    obj->resize_x = obj->resize_y = obj->resize_z = 128;
    obj->count_co[0] = count_co;
    obj->count_obj[0] = count_obj;
    CacheProvider_ObjtypeAdd(&fx->provider, obj_id, obj);
}

static struct UIIfHead const*
head_of(
    struct Fixture const* fx,
    int com_id)
{
    int i = UIIfModels_HeadIndex(&fx->app.if_models, com_id);
    return i >= 0 ? &fx->app.if_models.heads[i] : NULL;
}

/* ---- U1: the count variant -------------------------------------------- */

static void
test_obj_wants_its_count_variant(void)
{
    struct Fixture fx;
    struct UIIfHead const* head;
    int const com = 0x00c10003;

    printf("TEST: IF_SETOBJECT wants the count variant's chain\n");
    fixture_init(&fx);
    /* Obj 1000 draws as 1001 from a stack of 5. */
    add_obj(&fx, 1000, 50, 5, 1001);
    add_obj(&fx, 1001, 51, 0, 0);

    App_SetInterfaceObjModel(&fx.app, com, 1000, 7);
    TEST_ASSERT(App_IfModelPending(&fx.app, com), "the set made a task");
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_OBJ_MODEL, 1000, 1) == 1, "it wants the base obj's chain");
    TEST_ASSERT(loads_of(ASSET_KIND_OBJ_MODEL, 1000, 7) == 0, "one link at a time");
    Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_OBJ_MODEL, 1000, 1), true);
    pump(&fx);
    TEST_ASSERT(
        loads_of(ASSET_KIND_OBJ_MODEL, 1000, 7) == 1,
        "U1: and the chain of the variant a quantity of 7 draws");
    head = head_of(&fx, com);
    TEST_ASSERT(head && !head->landed, "nothing lands while the variant is out");
    Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_OBJ_MODEL, 1000, 7), false);
    pump(&fx);
    head = head_of(&fx, com);
    TEST_ASSERT(head && head->landed && head->scene_id == -1, "a chain that did not come lands nothing to show");
    TEST_ASSERT(!App_IfModelPending(&fx.app, com), "and the task is gone");

    /* The same set again: it landed nothing to show, so it is asked for
     * again (a set that landed a composite keeps it). */
    App_SetInterfaceObjModel(&fx.app, com, 1000, 7);
    TEST_ASSERT(App_IfModelPending(&fx.app, com), "a head that showed nothing is asked for again when set");

    /* A quantity that names no variant wants the base only. */
    App_SetInterfaceObjModel(&fx.app, com, 1001, 3);
    pump(&fx);
    Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_OBJ_MODEL, 1001, 1), false);
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_OBJ_MODEL, 1001, 3) == 0, "no variant, no second chain");
    fixture_free(&fx);
}

/* ---- U2: textures, and the check at landing ---------------------------- */

static void
test_npc_head_wants_textures(void)
{
    struct Fixture fx;
    struct ToriRS_Npctype* npc;
    struct ToriRS_Model* model;
    int const com = 0x00e70001;

    printf("TEST: an npc head wants its models' textures\n");
    fixture_init(&fx);
    npc = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*npc));
    assert(npc);
    npc->transform_varbit = -1;
    npc->transform_varp = -1;
    npc->heads_count = 2;
    npc->heads = ToriMem_Alloc(ToriMem_Assets(), 2 * sizeof(*npc->heads));
    assert(npc->heads);
    npc->heads[0] = -1; /* skipped, as NpcType.getHead does */
    npc->heads[1] = 70;
    CacheProvider_NpctypeAdd(&fx.provider, 500, npc);

    App_SetInterfaceNpcHead(&fx.app, com, 500);
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_MODEL, 70, 0) == 1, "it wants the head model");
    TEST_ASSERT(loads_of(ASSET_KIND_MODEL, -1, 0) == 0, "and not the -1 slot");

    /* The model comes, with one textured face. */
    model = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*model));
    assert(model);
    model->face_count = 2;
    model->face_textures = ToriMem_Alloc(ToriMem_Assets(), 2 * sizeof(*model->face_textures));
    assert(model->face_textures);
    model->face_textures[0] = -1;
    model->face_textures[1] = 33;
    CacheProvider_ModelAdd(&fx.provider, 70, model);
    Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_MODEL, 70, 0), true);
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_TEXTURE, 33, 0) == 1, "U2: an npc head wants its texture");
    TEST_ASSERT(head_of(&fx, com) && !head_of(&fx, com)->landed, "and waits for it");

    /* The script binds the widget itself: the head and its task go. */
    app_cs2_widget_model_direct(&fx.app, com);
    TEST_ASSERT(!App_IfModelPending(&fx.app, com), "a forgotten head cancels its task");
    pump(&fx);
    TEST_ASSERT(head_of(&fx, com) == NULL, "and nothing comes back for it");
    fixture_free(&fx);
}

static void
test_model_task_checks_its_head(void)
{
    struct Fixture fx;
    struct UIIfHead const* head;
    int const com = 0x01000002;
    int const other = 0x01000003;

    printf("TEST: a head task applies only to the head it was made for\n");
    fixture_init(&fx);

    App_SetInterfaceModel(&fx.app, com, 80);
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_MODEL, 80, 0) == 1, "it wants the model");
    /* Replaced without the store's set (nothing told the task). */
    (void)UIIfModels_SetHead(&fx.app.if_models, com, UI_IFHEAD_MODEL, 81, 0, 0);
    Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_MODEL, 80, 0), false);
    pump(&fx);
    head = head_of(&fx, com);
    TEST_ASSERT(head && head->npc_id == 81 && !head->landed, "U2: the old task landed nothing on the new head");
    TEST_ASSERT(queued(&fx) == 0, "and ended");

    /* Two sets before a pump: the older is cancelled before it asks. */
    App_SetInterfaceModel(&fx.app, other, 90);
    App_SetInterfaceModel(&fx.app, other, 91);
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_MODEL, 90, 0) == 0, "the replaced set asked for nothing");
    TEST_ASSERT(loads_of(ASSET_KIND_MODEL, 91, 0) == 1, "the newest asked for its model");
    Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_MODEL, 91, 0), false);
    pump(&fx);
    head = head_of(&fx, other);
    TEST_ASSERT(head && head->npc_id == 91 && head->landed, "the newest landed");
    /* The head with no task (replaced behind its back) is wanted again by
     * the bind; here by hand, as the bind would. */
    App_IfModelWant(&fx.app, com);
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_MODEL, 81, 0) == 1, "wanted again, it asks for its own model");
    Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_MODEL, 81, 0), false);
    pump(&fx);
    TEST_ASSERT(head_of(&fx, com)->landed, "and lands");
    fixture_free(&fx);
}

/* ---- inventory icons --------------------------------------------------- */

static void
test_icons_wanted_on_change(void)
{
    struct Fixture fx;
    int ids[3] = { 2000, INV_MANAGER_EMPTY_OBJ_ID, 2000 };
    int counts[3] = { 1, 0, 1 };
    int slot2[1] = { 2 };
    int empty_id[1] = { INV_MANAGER_EMPTY_OBJ_ID };
    int empty_count[1] = { 0 };
    int const backpack = INV_MANAGER_CONTAINER_BACKPACK;

    printf("TEST: inventory icons are wanted when a slot changes\n");
    fixture_init(&fx);
    add_obj(&fx, 2000, 60, 0, 0);
    InvManager_EnsureContainer(&fx.app.invs, backpack, 28, "Backpack");

    InvManager_ApplyFull(&fx.app.invs, backpack, ids, counts, 3);
    App_InvIconsWant(&fx.app, backpack);
    TEST_ASSERT(App_InvIconsPending(&fx.app) == 2, "one task per slot that lacks an icon");
    App_InvIconsWant(&fx.app, backpack);
    TEST_ASSERT(App_InvIconsPending(&fx.app) == 2 && queued(&fx) == 2, "a second change adds none");
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_OBJ_MODEL, 2000, 1) == 1, "one load for the item, however many slots");

    InvManager_ApplyPartial(&fx.app.invs, backpack, slot2, empty_id, empty_count, 1);
    App_InvIconsWant(&fx.app, backpack);
    TEST_ASSERT(App_InvIconsPending(&fx.app) == 1, "an emptied slot's task is cancelled");

    /* The model never comes: the obj's chain is still missing its model. */
    fx.app.host.client.now_ms = 1000;
    Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_OBJ_MODEL, 2000, 1), false);
    pump(&fx);
    TEST_ASSERT(App_InvIconsPending(&fx.app) == 1, "a load that did not come is not given up");
    TEST_ASSERT(loads_of(ASSET_KIND_OBJ_MODEL, 2000, 1) == 1, "and is not asked for at once");
    /* The pause (500 ms) is waited as client cycles of 20 ms: a named
     * cycle wait, PT_PARALLEL_AWAIT_CYCLE. 400 ms in, not yet. */
    fx.app.host.client.now_ms = 1400;
    for( int i = 0; i < 20; i++ )
        FrameClockSerial_NoteLogicCycle(&fx.app.frame_clock.serial);
        App_CycleClockTick(&fx.app);
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_OBJ_MODEL, 2000, 1) == 1, "nor before the pause is over");
    fx.app.host.client.now_ms = 1600;
    for( int i = 0; i < 10; i++ )
        FrameClockSerial_NoteLogicCycle(&fx.app.frame_clock.serial);
        App_CycleClockTick(&fx.app);
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_OBJ_MODEL, 2000, 1) == 2, "but after it, it asks again");

    /* The item goes: its task ends without baking anything. */
    {
        int none[1] = { INV_MANAGER_EMPTY_OBJ_ID };
        int zero[1] = { 0 };
        InvManager_ApplyFull(&fx.app.invs, backpack, none, zero, 1);
    }
    App_InvIconsWant(&fx.app, backpack);
    TEST_ASSERT(App_InvIconsPending(&fx.app) == 0, "an emptied container wants nothing");
    pump(&fx);
    fixture_free(&fx);
}

/* ---- textures: one publish task per texture a model wants --------------- */

static void
test_texture_publish_is_a_task(void)
{
    struct Fixture fx;
    int const ids[] = { 41, 41, 42 };
    int const again[] = { 41 };

    printf("TEST: a texture a model wants is one parallel task until it lands\n");
    fixture_init(&fx);
    App_TexturesWant(&fx.app, ids, 3);
    TEST_ASSERT(App_TexturePublishPending(&fx.app, 41), "41 is a pending task");
    TEST_ASSERT(App_TexturePublishPending(&fx.app, 42), "and so is 42");
    TEST_ASSERT(queued(&fx) == 2, "one task each, the repeat added nothing");
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_TEXTURE, 41, 0) == 1, "one load for 41");
    App_TexturesWant(&fx.app, again, 1);
    TEST_ASSERT(queued(&fx) == 2, "a rebuilt model asks nothing more while it loads");

    /* 41 does not come: the table remembers it, the task ends, and a later
     * model naming it asks for nothing. */
    Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_TEXTURE, 41, 0), false);
    pump(&fx);
    TEST_ASSERT(!App_TexturePublishPending(&fx.app, 41), "a failed load ends its task");
    TEST_ASSERT(
        Assets_State(fx.app.assets, AssetKey_Make(ASSET_KIND_TEXTURE, 41, 0)) == ASSET_STATE_FAILED,
        "and is remembered");
    App_TexturesWant(&fx.app, again, 1);
    TEST_ASSERT(!App_TexturePublishPending(&fx.app, 41), "so it is not asked for again");
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_TEXTURE, 41, 0) == 1, "still one load");

    /* A logout cancels what is still waiting. */
    RS_Pending_Cancel(fx.app.pending, RS_PendingScope_All());
    pump(&fx);
    TEST_ASSERT(!App_TexturePublishPending(&fx.app, 42), "a cancel ends the other");
    fixture_free(&fx);
}

/* ---- sprites: a reservation is one fill task ---------------------------- */

static struct ToriRS_Task*
no_sprite_task(
    struct CacheProvider* provider,
    int sprite_id)
{
    (void)provider;
    (void)sprite_id;
    assert(0 && "the assets table's loader is the test's: the provider is never asked");
    return NULL;
}

static struct ToriRS_Sprite*
drawable_sprite(void)
{
    struct ToriRS_Sprite* sprite = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*sprite));
    assert(sprite);
    sprite->frames = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*sprite->frames));
    assert(sprite->frames);
    sprite->frame_count = 1;
    sprite->frames[0].pixels_argb =
        ToriMem_Alloc(ToriMem_Assets(), sizeof(*sprite->frames[0].pixels_argb));
    assert(sprite->frames[0].pixels_argb);
    sprite->frames[0].pixels_argb[0] = 0xff55aa33u;
    sprite->frames[0].width = 1;
    sprite->frames[0].height = 1;
    sprite->frames[0].crop_width = 1;
    sprite->frames[0].crop_height = 1;
    return sprite;
}

static void
sprite_fixture_init(struct Fixture* fx)
{
    fixture_init(fx);
    fx->vtable.Task_SpriteLoad = no_sprite_task;
    App_AssetsSetLoader(&fx->app, ASSET_KIND_SPRITE, fake_start, fake_resident);
    App_IfAssetsBindBridge(&fx->app);
}

static void
cycles(
    struct Fixture* fx,
    int n)
{
    for( int i = 0; i < n; i++ )
        FrameClockSerial_NoteLogicCycle(&fx->app.frame_clock.serial);
    App_CycleClockTick(&fx->app);
}

static void
test_sprite_reservation_fills_when_it_lands(void)
{
    struct Fixture fx;
    struct AssetKey const key88 = AssetKey_Make(ASSET_KIND_SPRITE, 88, 0);
    uint64_t revision;
    int scene_id;

    printf("TEST: a sprite bound before it is resident is one fill task until it lands\n");
    sprite_fixture_init(&fx);

    scene_id = UITreeSceneBridge_BindSprite(&fx.app.bridge, 88);
    TEST_ASSERT(scene_id > 0, "a sprite that is not resident is bound");
    TEST_ASSERT(!ToriDraw_SceneSpriteHas(fx.app.scene, scene_id), "the reservation holds nothing yet");
    TEST_ASSERT(UITreeSceneBridge_EnsureSprite(&fx.app.bridge, 88) == scene_id,
        "a second lookup finds the reservation");
    TEST_ASSERT(App_SpriteFillPending(&fx.app, 88), "its fill is a pending task");
    TEST_ASSERT(UITreeSceneBridge_BindSprite(&fx.app.bridge, 88) == scene_id, "a rebind keeps the id");
    TEST_ASSERT(App_SpriteFillsPending(&fx.app) == 1 && queued(&fx) == 1, "and adds no second fill");

    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_SPRITE, 88, 0) == 1, "the first pump asks once");
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_SPRITE, 88, 0) == 1, "and not again while it loads");

    /* The read comes back empty: asked again only after the pause. */
    Assets_Finished(fx.app.assets, key88, false);
    pump(&fx);
    TEST_ASSERT(App_SpriteFillPending(&fx.app, 88), "a load that did not come is not given up");
    TEST_ASSERT(loads_of(ASSET_KIND_SPRITE, 88, 0) == 1, "nor asked for at once");
    cycles(&fx, 20); /* 400 ms of the 500 */
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_SPRITE, 88, 0) == 1, "nor inside the pause");
    cycles(&fx, 10);
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_SPRITE, 88, 0) == 2, "but after it, it asks again");

    /* It lands: the reservation is filled, the revision moves, the task ends. */
    revision = UITreeSceneBridge_AssetRevision(&fx.app.bridge);
    CacheProvider_SpriteAdd(&fx.provider, 88, drawable_sprite());
    Assets_Finished(fx.app.assets, key88, true);
    pump(&fx);
    TEST_ASSERT(ToriDraw_SceneSpriteHas(fx.app.scene, scene_id), "the landed sprite fills its scene id");
    TEST_ASSERT(!App_SpriteFillPending(&fx.app, 88), "and its task has ended");
    TEST_ASSERT(UITreeSceneBridge_AssetRevision(&fx.app.bridge) != revision,
        "filling a reservation advances the revision");
    TEST_ASSERT(UITreeSceneBridge_BindSprite(&fx.app.bridge, 88) == scene_id, "a rebind keeps the id");
    TEST_ASSERT(App_SpriteFillsPending(&fx.app) == 0, "and asks for nothing");
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_SPRITE, 88, 0) == 2, "a filled sprite is not loaded again");

    /* Absent from the sprites table: never bound; and a reservation made
     * before the table answered ends its task without asking again. */
    CacheProvider_SpriteMarkAbsent(&fx.provider, 99);
    TEST_ASSERT(UITreeSceneBridge_BindSprite(&fx.app.bridge, 99) == -1, "an absent sprite is not bound");
    TEST_ASSERT(!App_SpriteFillPending(&fx.app, 99), "and has no fill");
    scene_id = UITreeSceneBridge_BindSprite(&fx.app.bridge, 100);
    TEST_ASSERT(scene_id > 0, "sprite 100 is bound");
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_SPRITE, 100, 0) == 1, "and asked for");
    CacheProvider_SpriteMarkAbsent(&fx.provider, 100);
    Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_SPRITE, 100, 0), false);
    pump(&fx);
    TEST_ASSERT(!App_SpriteFillPending(&fx.app, 100), "an absent sprite's fill ends");
    cycles(&fx, 400);
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_SPRITE, 100, 0) == 1, "and it is never asked for again");
    TEST_ASSERT(UITreeSceneBridge_BindSprite(&fx.app.bridge, -1) == -1, "-1 is not bound");

    fixture_free(&fx);
}

static void
test_sprite_reservation_outlives_a_logout(void)
{
    struct Fixture fx;
    int scene_id;

    printf("TEST: a logout's cancel is followed by the reservation wanted again\n");
    sprite_fixture_init(&fx);
    scene_id = UITreeSceneBridge_BindSprite(&fx.app.bridge, 88);
    TEST_ASSERT(scene_id > 0, "bound");
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_SPRITE, 88, 0) == 1, "asked for once");

    /* What App_Logout does: cancel everything, then hand the bridge's
     * reservations back to the filler. */
    TEST_ASSERT(RS_Pending_Cancel(fx.app.pending, RS_PendingScope_All()) == 1, "the fill is cancelled");
    TEST_ASSERT(!App_SpriteFillPending(&fx.app, 88), "and no longer pending");
    App_IfAssetsAfterLogout(&fx.app);
    TEST_ASSERT(App_SpriteFillPending(&fx.app, 88), "the reservation has a fill again");
    App_IfAssetsAfterLogout(&fx.app);
    TEST_ASSERT(App_SpriteFillsPending(&fx.app) == 1, "and only one");
    pump(&fx);
    TEST_ASSERT(queued(&fx) == 1, "the cancelled task has ended");
    TEST_ASSERT(loads_of(ASSET_KIND_SPRITE, 88, 0) == 1, "the load in flight is the one awaited");

    CacheProvider_SpriteAdd(&fx.provider, 88, drawable_sprite());
    Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_SPRITE, 88, 0), true);
    pump(&fx);
    TEST_ASSERT(ToriDraw_SceneSpriteHas(fx.app.scene, scene_id), "the reservation is filled");
    App_IfAssetsAfterLogout(&fx.app);
    TEST_ASSERT(App_SpriteFillsPending(&fx.app) == 0, "a filled reservation is not wanted again");
    fixture_free(&fx);
}

/* A provider with no numeric sprite loader (a dat1 cache names its sprites),
 * or a bridge with no filler: nothing would ever fill a reservation, so
 * nothing is reserved. */
static void
test_bind_without_a_loader_is_unbound(void)
{
    struct Fixture fx;

    printf("TEST: no sprite loader, or no filler, reserves nothing\n");
    fixture_init(&fx);
    App_IfAssetsBindBridge(&fx.app);
    TEST_ASSERT(UITreeSceneBridge_BindSprite(&fx.app.bridge, 88) == -1, "bound with no loader to fill it");
    TEST_ASSERT(App_SpriteFillsPending(&fx.app) == 0, "pending with no loader");
    fx.vtable.Task_SpriteLoad = no_sprite_task;
    UITreeSceneBridge_SetSpriteFiller(&fx.app.bridge, NULL, NULL);
    TEST_ASSERT(UITreeSceneBridge_BindSprite(&fx.app.bridge, 88) == -1, "bound with no filler");
    TEST_ASSERT(App_SpriteFillsPending(&fx.app) == 0, "pending with no filler");
    fixture_free(&fx);
}

/* E12: an npc's overhead from a sprite group other than the client's prayer
 * pack (a server headicon customisation naming its own group) is bound by
 * its id and loads through the assets table; every set slot is drawn, in
 * slot order; the prayer group draws from the client's own pack; and a type
 * with no customisation still draws its one icon. */
static void
test_npc_headicon_groups_load(void)
{
    struct Fixture fx;
    struct WorldEntity_NPC npc;
    struct ToriRS_Npctype type;
    struct AppNpcHeadicon icons[WORLD_NPC_HEADICON_SLOTS];
    int const prayer_scene = 4321;
    int n;

    printf("TEST: an npc headicon of any sprite group is bound and loads through the assets table\n");
    sprite_fixture_init(&fx);
    memset(&npc, 0, sizeof(npc));
    memset(&type, 0, sizeof(type));
    type.head_icon_group = -1;
    type.head_icon_index = -1;
    for( int i = 0; i < WORLD_NPC_HEADICON_SLOTS; i++ )
    {
        npc.headicon_group[i] = -1;
        npc.headicon_index[i] = -1;
    }
    npc.headicon_custom = true;
    npc.headicon_group[1] = 88;
    npc.headicon_index[1] = 0;
    npc.headicon_group[3] = APP_HEADICONS_PRAYER_GROUP;
    npc.headicon_index[3] = 2;
    npc.headicon_group[5] = 77; /* a group with no frame: not drawn */
    npc.headicon_index[5] = -1;

    n = app_overlay_npc_headicons(&fx.app, &type, &npc, prayer_scene, APP_HEADICONS_PRAYER_GROUP, icons);
    TEST_ASSERT(n == 2, "both set slots are drawn");
    TEST_ASSERT(n >= 1 && icons[0].scene_id > 0 && icons[0].scene_id != prayer_scene && icons[0].frame == 0,
        "group 88 first, bound by its id");
    TEST_ASSERT(n >= 2 && icons[1].scene_id == prayer_scene && icons[1].frame == 2,
        "the prayer group from the client's own pack");
    TEST_ASSERT(App_SpriteFillPending(&fx.app, 88), "group 88 loads through its fill task");
    TEST_ASSERT(!App_SpriteFillPending(&fx.app, 77), "a slot with no frame binds nothing");
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_SPRITE, 88, 0) == 1, "asked of the assets table once");

    /* It lands with one frame: frame 0 draws, frame 1 of it would not. */
    CacheProvider_SpriteAdd(&fx.provider, 88, drawable_sprite());
    if( loads_of(ASSET_KIND_SPRITE, 88, 0) == 1 )
        Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_SPRITE, 88, 0), true);
    pump(&fx);
    TEST_ASSERT(!App_SpriteFillPending(&fx.app, 88), "and it landed");
    npc.headicon_index[1] = 1;
    n = app_overlay_npc_headicons(&fx.app, &type, &npc, prayer_scene, APP_HEADICONS_PRAYER_GROUP, icons);
    TEST_ASSERT(n == 1 && icons[0].scene_id == prayer_scene, "a frame the group lacks is not drawn");

    /* No customisation: the type's one icon, an older cache's frame-only
     * icon from the prayer pack. */
    npc.headicon_custom = false;
    type.head_icon_index = 4;
    n = app_overlay_npc_headicons(&fx.app, &type, &npc, prayer_scene, APP_HEADICONS_PRAYER_GROUP, icons);
    TEST_ASSERT(n == 1 && icons[0].scene_id == prayer_scene && icons[0].frame == 4, "the type's icon");
    fixture_free(&fx);
}

/* E12 HEAD_CUSTOM: an IF_SETNPCHEAD_ACTIVE head of an npc carrying a
 * chathead customisation loads the override's models, not the type's heads,
 * keyed by the override (a second override is a second head). */
static void
test_npc_head_custom_loads_its_models(void)
{
    struct Fixture fx;
    struct ToriRS_Npctype* type;
    struct WorldEntity_NPC* npc;
    struct UIIfHead const* head;
    int const com = 0x00e70002;
    int const models[2] = { 71, 72 };
    int idx;

    printf("TEST: a chathead customisation's head loads the override's models\n");
    fixture_init(&fx);
    type = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*type));
    assert(type);
    type->transform_varbit = -1;
    type->transform_varp = -1;
    type->heads_count = 1;
    type->heads = ToriMem_Alloc(ToriMem_Assets(), sizeof(*type->heads));
    assert(type->heads);
    type->heads[0] = 70;
    CacheProvider_NpctypeAdd(&fx.provider, 500, type);
    fx.app.world = World_New();
    RS_EntitySync_Init(&fx.app.esync);
    idx = World_NpcSpawn(fx.app.world, 9, 500, 0, 10, 10, 1, (struct WorldEntityFacet_IdleAnimations){ 0 });
    npc = World_EntityPoolGet(&fx.app.world->entities.npc, idx);
    npc->server_slot = 12;
    RS_EntitySync_RegisterNpc(&fx.app.esync, 12, 9, idx);
    {
        struct WorldNpcOverrideIn in = { .models_sent = true, .model_count = 2, .models = models };
        World_NpcSetHeadCustom(fx.app.world, idx, &in);
    }

    App_SetInterfaceNpcHeadCustom(&fx.app, com, idx);
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_MODEL, 71, 0) == 1, "it wants the override's first model");
    if( loads_of(ASSET_KIND_MODEL, 71, 0) == 1 )
        Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_MODEL, 71, 0), false);
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_MODEL, 72, 0) == 1, "then its second");
    TEST_ASSERT(loads_of(ASSET_KIND_MODEL, 70, 0) == 0, "and not the type's head");
    head = head_of(&fx, com);
    TEST_ASSERT(head && head->custom_serial == npc->head_custom.serial && head->npc_id == 500,
        "the head names the override");

    /* A new override of the same npc is a new head. */
    {
        int const other[1] = { 73 };
        struct WorldNpcOverrideIn in = { .models_sent = true, .model_count = 1, .models = other };
        World_NpcSetHeadCustom(fx.app.world, idx, &in);
    }
    App_SetInterfaceNpcHeadCustom(&fx.app, com, idx);
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_MODEL, 73, 0) == 1, "a new override wants its own models");
    if( loads_of(ASSET_KIND_MODEL, 72, 0) == 1 )
        Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_MODEL, 72, 0), false);
    if( loads_of(ASSET_KIND_MODEL, 73, 0) == 1 )
        Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_MODEL, 73, 0), false);
    pump(&fx);
    head = head_of(&fx, com);
    TEST_ASSERT(head && head->landed && head->scene_id < 0, "parts that did not come land nothing to show");
    RS_EntitySync_Free(&fx.app.esync);
    World_Free(fx.app.world);
    fx.app.world = NULL;
    fixture_free(&fx);
}

/*
 * A chathead customisation that arrives while the head is shown (NPC_INFO
 * HEAD_CUSTOM after IF_SETNPCHEAD_ACTIVE) shows at once: the reference's
 * IfType.getModel resolves model type 6 against the npc on every draw
 * (class308.method7279), so the widget follows the npc's override. Before,
 * the override was captured when the head was set and a later one never
 * reached the widget.
 */
static void
test_active_head_follows_its_npc(void)
{
    struct Fixture fx;
    struct ToriRS_Npctype* type;
    struct WorldEntity_NPC* npc;
    struct UIIfHead const* head;
    int const com = 0x00e70002;
    int const first[1] = { 81 };
    int const second[1] = { 82 };
    int idx;

    printf("TEST: an active chathead follows a customisation that arrives while it is shown\n");
    fixture_init(&fx);
    fx.app.tree = UITree_New(16);
    type = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*type));
    assert(type);
    type->transform_varbit = -1;
    type->transform_varp = -1;
    type->heads_count = 1;
    type->heads = ToriMem_Alloc(ToriMem_Assets(), sizeof(*type->heads));
    assert(type->heads);
    type->heads[0] = 80;
    CacheProvider_NpctypeAdd(&fx.provider, 501, type);
    fx.app.world = World_New();
    RS_EntitySync_Init(&fx.app.esync);
    idx = World_NpcSpawn(fx.app.world, 9, 501, 0, 10, 10, 1, (struct WorldEntityFacet_IdleAnimations){ 0 });
    npc = World_EntityPoolGet(&fx.app.world->entities.npc, idx);
    npc->server_slot = 13;
    RS_EntitySync_RegisterNpc(&fx.app.esync, 13, 9, idx);
    {
        struct WorldNpcOverrideIn in = { .models_sent = true, .model_count = 1, .models = first };
        World_NpcSetHeadCustom(fx.app.world, idx, &in);
    }
    App_SetInterfaceNpcHeadCustom(&fx.app, com, idx);
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_MODEL, 81, 0) == 1, "the shown override's model is wanted");
    if( loads_of(ASSET_KIND_MODEL, 81, 0) == 1 )
        Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_MODEL, 81, 0), false);
    pump(&fx);

    /* HEAD_CUSTOM again while the head is up: a new override. */
    {
        struct WorldNpcOverrideIn in = { .models_sent = true, .model_count = 1, .models = second };
        World_NpcSetHeadCustom(fx.app.world, idx, &in);
    }
    app_if_head_poll(&fx.app);
    pump(&fx);
    head = head_of(&fx, com);
    TEST_ASSERT(head && head->custom_serial == npc->head_custom.serial,
        "the shown head names the npc's newer override, not the one it was set with");
    TEST_ASSERT(loads_of(ASSET_KIND_MODEL, 82, 0) == 1, "the new override's model is not wanted");
    if( loads_of(ASSET_KIND_MODEL, 82, 0) == 1 )
        Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_MODEL, 82, 0), false);
    pump(&fx);

    /* The customisation reset (NULL, the block's 0x1): the type's head. */
    World_NpcSetHeadCustom(fx.app.world, idx, NULL);
    app_if_head_poll(&fx.app);
    pump(&fx);
    head = head_of(&fx, com);
    TEST_ASSERT(head && head->custom_serial == 0 && head->npc_id == 501, "a reset shows the type's head");
    TEST_ASSERT(loads_of(ASSET_KIND_MODEL, 80, 0) == 1, "and wants the type's head model");
    if( loads_of(ASSET_KIND_MODEL, 80, 0) == 1 )
        Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_MODEL, 80, 0), false);
    pump(&fx);

    /* A plain active head (no customisation when it was set) takes one that
     * arrives later. */
    App_SetInterfaceNpcHeadActive(&fx.app, com + 1, idx);
    pump(&fx);
    {
        int const third[1] = { 83 };
        struct WorldNpcOverrideIn in = { .models_sent = true, .model_count = 1, .models = third };
        World_NpcSetHeadCustom(fx.app.world, idx, &in);
    }
    app_if_head_poll(&fx.app);
    pump(&fx);
    head = head_of(&fx, com + 1);
    TEST_ASSERT(head && head->custom_serial == npc->head_custom.serial, "a plain active head takes a later override");
    TEST_ASSERT(loads_of(ASSET_KIND_MODEL, 83, 0) == 1, "and wants its model");
    if( loads_of(ASSET_KIND_MODEL, 83, 0) == 1 )
        Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_MODEL, 83, 0), false);
    pump(&fx);

    /* A plain IF_SETNPCHEAD on the widget names a type: it follows nothing,
     * and a later customisation of the npc leaves it alone. */
    App_SetInterfaceNpcHead(&fx.app, com + 1, 501);
    pump(&fx);
    if( loads_of(ASSET_KIND_MODEL, 80, 0) == 2 )
        Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_MODEL, 80, 0), false);
    pump(&fx);
    {
        int const fourth[1] = { 84 };
        struct WorldNpcOverrideIn in = { .models_sent = true, .model_count = 1, .models = fourth };
        World_NpcSetHeadCustom(fx.app.world, idx, &in);
    }
    app_if_head_poll(&fx.app);
    pump(&fx);
    head = head_of(&fx, com + 1);
    TEST_ASSERT(head && head->custom_serial == 0 && head->active_npc_slot < 0, "a type's head follows no npc");
    /* The first widget is still active: it takes the override too. */
    if( loads_of(ASSET_KIND_MODEL, 84, 0) >= 1 )
        Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_MODEL, 84, 0), false);
    pump(&fx);
    head = head_of(&fx, com);
    TEST_ASSERT(head && head->custom_serial == npc->head_custom.serial, "while the active one follows it");

    RS_EntitySync_Free(&fx.app.esync);
    World_Free(fx.app.world);
    fx.app.world = NULL;
    UITree_Free(fx.app.tree);
    fx.app.tree = NULL;
    fixture_free(&fx);
}

int
main(void)
{
    test_obj_wants_its_count_variant();
    test_npc_head_wants_textures();
    test_model_task_checks_its_head();
    test_icons_wanted_on_change();
    test_texture_publish_is_a_task();
    test_sprite_reservation_fills_when_it_lands();
    test_sprite_reservation_outlives_a_logout();
    test_bind_without_a_loader_is_unbound();
    test_npc_headicon_groups_load();
    test_npc_head_custom_loads_its_models();
    test_active_head_follows_its_npc();
    if( g_failures )
    {
        printf("if_assets_tasks: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("if_assets_tasks: all passed\n");
    return 0;
}
