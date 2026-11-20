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
 *
 * Run: make -C src test-if-assets-tasks
 */
#include "app.h"
#include "app/app_internal.h"
#include "asyncio.h"
#include "engine/asset_requests.h"
#include "engine/cache_provider.h"
#include "engine/uitree_scene_bridge.h"
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

int
main(void)
{
    test_obj_wants_its_count_variant();
    test_npc_head_wants_textures();
    test_model_task_checks_its_head();
    test_icons_wanted_on_change();
    test_texture_publish_is_a_task();
    if( g_failures )
    {
        printf("if_assets_tasks: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("if_assets_tasks: all passed\n");
    return 0;
}
