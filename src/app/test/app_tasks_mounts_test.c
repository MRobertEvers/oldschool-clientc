/*
 * The visual waits that left the main queue (step lo-mounts): what a mount, a
 * root build, IF_SETPLAYERMODEL_BODYTYPE, a developer or plugin spawn and a
 * plugin's loose head wait for only to DRAW is a parallel task, on a stack
 * App whose assets table has fake loaders the test answers by hand.
 *
 * Pins, for each conversion: the task is on the parallel queue and listed in
 * the pending index, nothing waits on the packet queue while its model is
 * out (a later packet applies at once), and the close of what it was for --
 * the interface group, the old-format slot, the root, a logout -- cancels it.
 *
 *   - A sub mount (App_MountVisualsAdd): one task a model a group's widget
 *     waits for, keyed on the node's component and its group's close serial;
 *     a remount adds none twice; the group's close cancels them.
 *   - An old-format (dat1) slot mount (App_SlotVisualsAdd): the nodes under
 *     the slot, found by the subtree (a dat1 component id carries no group);
 *     App_SlotVisualsCancel, before the slot is cleared, cancels them.
 *   - The root build (App_RootVisualsAdd): every model node of the tree;
 *     App_MountsTreeCleared (the next root) cancels them.
 *   - A model that arrives binds; a node replaced while it loaded binds
 *     nothing.
 *   - IF_SETPLAYERMODEL_BODYTYPE waits on the packet queue for the idk
 *     configs only, never the default appearance's kit models.
 *   - A plugin object's spawn is a parallel task, not a packet-queue one.
 *   - app_if_head_enqueue: a head with no component is a parallel task keyed
 *     on what it loads, added once while on its way, asked again after it
 *     came with nothing.
 *
 * Run: make -C src test-tasks-mounts
 */
#include "app.h"
#include "app/app_internal.h"
#include "app/app_gameproto_exec.h"
#include "asyncio.h"
#include "engine/asset_requests.h"
#include "engine/cache_provider.h"
#include "engine/uitree_builder/task_interface_open.h"
#include "engine/uitree_builder/task_slot_mount.h"
#include "engine/uitree_scene_bridge.h"
#include "game/rs_server_state.h"
#include "mem/torirs_mem.h"
#include "net/rev/revpacket.h"
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

/* ---- the fixture: an App with the queues, the tables, a tree ------------- */

#define LOADS_MAX 256

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

static int
loads_of_kind(enum AssetKind kind)
{
    int n = 0;
    for( int i = 0; i < g_loads.count; i++ )
        if( g_loads.keys[i].kind == (int)kind )
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
    fx->app.packet_runner.queue = ToriRS_TaskQueue_New();
    fx->app.scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    assert(fx->app.scene);
    UITreeSceneBridge_Init(&fx->app.bridge, fx->app.scene, &fx->provider);
    App_AssetsInit(&fx->app);
    App_PendingInit(&fx->app);
    for( size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++ )
        App_AssetsSetLoader(&fx->app, kinds[i], fake_start, fake_resident);
    InvManager_Init(&fx->app.invs);
    fx->app.tree = UITree_New(32);
    assert(fx->app.tree);
    fx->io = ToriRS_IOBatch_New();
}

static void
fixture_free(struct Fixture* fx)
{
    TEST_ASSERT(fx->app.frame_runner.queue->head == NULL, "every parallel task ended");
    TEST_ASSERT(RS_Pending_Listed(fx->app.pending) == 0, "and unlisted itself");
    ToriRS_TaskQueue_Free(fx->app.frame_runner.queue);
    ToriRS_TaskQueue_Free(fx->app.packet_runner.queue);
    App_PendingFree(&fx->app);
    App_AssetsFree(&fx->app);
    ToriRS_IOBatch_Free(fx->io);
    InvManager_Free(&fx->app.invs);
    UIIfModels_Free(&fx->app.if_models);
    UIIfTextStore_Free(&fx->app.if_texts);
    UIIfIntStore_Free(&fx->app.if_hides);
    UIIfIntStore_Free(&fx->app.if_colours);
    UIIfEventTable_Free(&fx->app.if_events);
    UITree_Free(fx->app.tree);
    UITreeSceneBridge_Free(&fx->app.bridge);
    ToriDraw_SceneFree(fx->app.scene);
    VarPManager_Free(&fx->app.varps);
    CacheProvider_FreeEngineCaches(&fx->provider);
}

/* Step a queue until nothing more can progress, as a frame does. */
static void
pump_queue(
    struct Fixture* fx,
    struct ToriRS_TaskQueue* queue)
{
    for( int pass = 0; pass < 8; pass++ )
    {
        struct ToriRS_Task* task = queue->head;
        while( task )
        {
            struct ToriRS_Task* next = task->next;
            /* A parked task is not run: only what it parked on readies it. */
            if( !task->parked )
                (void)ToriRS_TaskQueue_RunTask(queue, fx->io, task);
            task = next;
        }
    }
}

static void
pump(struct Fixture* fx)
{
    pump_queue(fx, fx->app.frame_runner.queue);
}

static int
queued(struct ToriRS_TaskQueue const* queue)
{
    int n = 0;
    for( struct ToriRS_Task* t = queue->head; t; t = t->next )
        n++;
    return n;
}

/* A model widget, `component_id`, showing cache model `model_id`, under
 * `parent` (-1: a root). */
static int32_t
push_model(
    struct Fixture* fx,
    int32_t parent,
    int component_id,
    int model_id)
{
    struct UITreeNodeSpec spec;
    int32_t idx;

    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_MODEL;
    spec.component_id = component_id;
    spec.width = 32;
    spec.height = 32;
    spec.u.rs_model.gamecache_model_id = model_id;
    spec.u.rs_model.active_model_id = -1;
    idx = UITree_Push(fx->app.tree, parent, &spec);
    assert(idx >= 0);
    return idx;
}

static int32_t
push_layer(
    struct Fixture* fx,
    int32_t parent,
    int component_id)
{
    struct UITreeNodeSpec spec;
    int32_t idx;

    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LAYER;
    spec.component_id = component_id;
    spec.width = 100;
    spec.height = 100;
    idx = UITree_Push(fx->app.tree, parent, &spec);
    assert(idx >= 0);
    return idx;
}

/* A model the cache now holds (no faces: it binds, and draws nothing). */
static void
add_model(
    struct Fixture* fx,
    int model_id)
{
    struct ToriRS_Model* model = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*model));
    assert(model);
    CacheProvider_ModelAdd(&fx->provider, model_id, model);
}

/* A later packet that writes to the tree, applied at once: an IF_SETTEXT
 * onto `component_id`, a TEXT node the test made. */
static void
later_packet_applies(
    struct Fixture* fx,
    int component_id)
{
    struct UITreeNodeSpec spec;
    struct RS_ServerState state;
    struct RevPacket p;
    char text[] = "later";
    int32_t idx;

    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_TEXT;
    spec.component_id = component_id;
    spec.width = 100;
    spec.height = 14;
    spec.u.rs_text.font_id = 1;
    spec.u.rs_text.text = "before";
    idx = UITree_Push(fx->app.tree, -1, &spec);
    assert(idx >= 0);
    App_ServerStateBind(&fx->app, &state);
    state.rev_number = -1;
    memset(&p, 0, sizeof(p));
    p.packet_type = PKT_NAME_IF_SETTEXT;
    p._if_settext.component_id = component_id;
    p._if_settext.text = text;
    App_GameProto_Exec(&fx->app, &state, &p);
    idx = UITree_FindByComponentId(fx->app.tree, component_id);
    TEST_ASSERT(
        idx >= 0 && fx->app.tree->components[idx].u.rs_text.text &&
            strcmp(fx->app.tree->components[idx].u.rs_text.text, "later") == 0,
        "a later packet applies while the model is out");
    TEST_ASSERT(fx->app.packet_runner.queue->head == NULL, "and nothing waits on the packet queue");
}

static struct RS_PendingKey
node_key(
    struct Fixture const* fx,
    int32_t idx)
{
    int const cid = fx->app.tree->components[idx].component_id;
    return RS_PendingKey_Component(cid, UITree_GroupCloseSerial(fx->app.tree, (cid >> 16) & 0xffff));
}

/* ---- a sub mount ---------------------------------------------------------- */

static void
test_group_mount(void)
{
    struct Fixture fx;
    int const group = 500;
    int32_t root;
    int32_t a;
    int32_t b;

    printf("TEST: a sub mount's models are parallel tasks, one a model; the close cancels them\n");
    fixture_init(&fx);
    root = push_layer(&fx, -1, (group << 16) | 0);
    a = push_model(&fx, root, (group << 16) | 1, 70);
    b = push_model(&fx, root, (group << 16) | 2, 71);
    add_model(&fx, 71); /* here already: the bake bound it */
    /* A chathead an IF_SETNPCHEAD bound: a scene composite, not a cache
     * model, and its own task's. */
    (void)push_model(&fx, root, (group << 16) | 3, UITREE_SCENE_NPC_HEAD_BASE | 1012);

    TEST_ASSERT(App_MountVisualsAdd(&fx.app, group) == 1, "one task, for the one model not here");
    TEST_ASSERT(RS_Pending_Find(fx.app.pending, APP_TASK_MOUNT_MODEL, node_key(&fx, a)) != NULL,
                "keyed on the node's component and its group's close serial");
    TEST_ASSERT(RS_Pending_Find(fx.app.pending, APP_TASK_MOUNT_MODEL, node_key(&fx, b)) == NULL,
                "none for the model that is here");
    TEST_ASSERT(App_MountVisualsAdd(&fx.app, group) == 0, "a remount adds none twice");
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_MODEL, 70, 0) == 1, "it asked for the model");
    TEST_ASSERT(loads_of_kind(ASSET_KIND_MODEL) == 1, "and for no scene composite as a cache model");
    later_packet_applies(&fx, 0x7f000001);

    /* The model comes: it binds and the task ends. */
    add_model(&fx, 70);
    Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_MODEL, 70, 0), true);
    pump(&fx);
    TEST_ASSERT(RS_Pending_Count(fx.app.pending, APP_TASK_MOUNT_MODEL) == 0, "the bind ended the task");

    /* Mounted again with a model not here; then the group closes. */
    fx.app.tree->components[a].u.rs_model.gamecache_model_id = 72;
    TEST_ASSERT(App_MountVisualsAdd(&fx.app, group) == 1, "the widget's new model is a task");
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_MODEL, 72, 0) == 1, "it asked for it");
    TEST_ASSERT(RS_Pending_Find(fx.app.pending, APP_TASK_MOUNT_MODEL, node_key(&fx, a)) != NULL, "pending");
    App_MountsGroupClosed(&fx.app, group);
    TEST_ASSERT(RS_Pending_Find(fx.app.pending, APP_TASK_MOUNT_MODEL, node_key(&fx, a)) == NULL,
                "closing the group cancels it");
    pump(&fx);
    TEST_ASSERT(queued(fx.app.frame_runner.queue) == 0, "and it ended without its model");
    fixture_free(&fx);
}

/* ---- a node replaced while its model loaded -------------------------------- */

static void
test_replaced_node_binds_nothing(void)
{
    struct Fixture fx;
    int const group = 501;
    int32_t root;
    int32_t a;

    printf("TEST: a widget replaced while its model loaded gets nothing bound\n");
    fixture_init(&fx);
    root = push_layer(&fx, -1, (group << 16) | 0);
    a = push_model(&fx, root, (group << 16) | 1, 80);
    TEST_ASSERT(App_MountVisualsAdd(&fx.app, group) == 1, "a task");
    pump(&fx);
    /* The node goes (a script deleted it); the index is reused. */
    UITree_ClearChildren(fx.app.tree, root);
    (void)push_model(&fx, root, (group << 16) | 1, 81);
    add_model(&fx, 80);
    Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_MODEL, 80, 0), true);
    pump(&fx);
    TEST_ASSERT(queued(fx.app.frame_runner.queue) == 0, "the task ended");
    TEST_ASSERT(UITree_ResolveRef(fx.app.tree, UITree_RefAt(fx.app.tree, a)) < 0 ||
                    fx.app.tree->components[a].u.rs_model.gamecache_model_id == 81,
                "and the new node keeps what it shows");
    fixture_free(&fx);
}

/* ---- an old-format slot mount --------------------------------------------- */

static void
test_dat1_slot_mount(void)
{
    struct Fixture fx;
    int32_t owner;
    int32_t layer;
    int32_t a;
    int32_t nodes[4];

    printf("TEST: a dat1 slot mount's models are found under the slot and cancelled with it\n");
    fixture_init(&fx);
    owner = push_layer(&fx, -1, (TORIRS_REVCONFIG_GROUP << 16) | 9);
    /* dat1 component ids are flat: no group in their high half. */
    layer = push_layer(&fx, owner, 3213);
    a = push_model(&fx, layer, 3214, 90);
    (void)push_model(&fx, owner, 3215, 91);
    TEST_ASSERT(!UITree_GroupPresent(fx.app.tree, 3213),
                "the group lookup finds nothing for a dat1 interface");
    TEST_ASSERT(InterfaceOpen_SubtreeModelNodes(fx.app.tree, owner, nodes, 4) == 2,
                "the subtree finds both model widgets");

    TEST_ASSERT(App_SlotVisualsAdd(&fx.app, owner) == 2, "one task a model");
    TEST_ASSERT(RS_Pending_Find(fx.app.pending, APP_TASK_MOUNT_MODEL, node_key(&fx, a)) != NULL, "listed");
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_MODEL, 90, 0) == 1 && loads_of(ASSET_KIND_MODEL, 91, 0) == 1,
                "both asked for");
    later_packet_applies(&fx, 0x7f000002);

    /* The slot is remounted: cancelled before its children go. */
    App_SlotVisualsCancel(&fx.app, owner);
    UITree_ClearChildren(fx.app.tree, owner);
    pump(&fx);
    TEST_ASSERT(queued(fx.app.frame_runner.queue) == 0, "remounting the slot cancels them");
    fixture_free(&fx);
}

/* ---- the root build ------------------------------------------------------- */

static void
test_root_build(void)
{
    struct Fixture fx;
    int32_t root;

    printf("TEST: the root build's models are parallel tasks; the next root cancels them\n");
    fixture_init(&fx);
    root = push_layer(&fx, -1, (161 << 16) | 0);
    (void)push_model(&fx, root, (161 << 16) | 3, 100);
    (void)push_model(&fx, root, (162 << 16) | 4, 101);
    TEST_ASSERT(App_RootVisualsAdd(&fx.app) == 2, "every model node of the tree not here");
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_MODEL, 100, 0) == 1, "asked for");
    later_packet_applies(&fx, 0x7f000003);
    App_MountsTreeCleared(&fx.app);
    pump(&fx);
    TEST_ASSERT(queued(fx.app.frame_runner.queue) == 0, "the next root cancels them");
    fixture_free(&fx);
}

/* ---- IF_SETPLAYERMODEL_BODYTYPE ------------------------------------------- */

static void
test_bodytype_waits_for_configs_only(void)
{
    struct Fixture fx;

    printf("TEST: BODYTYPE waits on the packet queue for idk configs, not kit models\n");
    fixture_init(&fx);
    for( int id = 0; id < 2; id++ )
    {
        struct ToriRS_Idk* idk = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*idk));
        assert(idk);
        idk->body_part_id = id;
        CacheProvider_IdkAdd(&fx.provider, id, idk);
    }
    App_SetInterfacePlayerModelBodyType(&fx.app, (84 << 16) | 4, 1);
    TEST_ASSERT(queued(fx.app.packet_runner.queue) == 1, "the op waits on the packet queue (required)");
    pump_queue(&fx, fx.app.packet_runner.queue);
    TEST_ASSERT(loads_of(ASSET_KIND_CONFIG, 2, ASSET_CONFIG_IDK) == 1, "for the idk table's next config");
    TEST_ASSERT(loads_of_kind(ASSET_KIND_MODEL) == 0, "and for no model");
    Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_CONFIG, 2, ASSET_CONFIG_IDK), false);
    pump_queue(&fx, fx.app.packet_runner.queue);
    TEST_ASSERT(queued(fx.app.packet_runner.queue) == 0, "the end of the table ends the wait");
    TEST_ASSERT(loads_of_kind(ASSET_KIND_MODEL) == 0, "still no kit model");
    fixture_free(&fx);
}

/* ---- a plugin object's spawn ---------------------------------------------- */

static void
test_plugin_object_spawn(void)
{
    struct Fixture fx;
    struct Task_AppSpawn* task;

    printf("TEST: a plugin object's spawn is a parallel task\n");
    fixture_init(&fx);
    task = app_spawn_task_new(&fx.app, APP_SPAWN_PLUGIN_OBJECT, 0, 0, 0);
    task->plugin_object = 7; /* no such object: it was destroyed */
    app_spawn_task_add(&fx.app, task);
    TEST_ASSERT(RS_Pending_Find(fx.app.pending, APP_TASK_SPAWN_PLUGIN_OBJECT, RS_PendingKey_Id(7, 0)) != NULL,
                "listed under its handle");
    TEST_ASSERT(fx.app.packet_runner.queue->head == NULL, "not on the packet queue");
    TEST_ASSERT(queued(fx.app.frame_runner.queue) == 1, "on the parallel queue");
    /* A logout cancels it. */
    (void)RS_Pending_Cancel(fx.app.pending, RS_PendingScope_All());
    pump(&fx);
    TEST_ASSERT(queued(fx.app.frame_runner.queue) == 0, "a logout's cancel ends it");
    fixture_free(&fx);
}

/* ---- a head with no component --------------------------------------------- */

static void
test_loose_head(void)
{
    struct Fixture fx;

    printf("TEST: app_if_head_enqueue: a head with no component is a parallel task\n");
    fixture_init(&fx);
    app_if_head_enqueue(&fx.app, UI_IFHEAD_MODEL, -1, 120);
    TEST_ASSERT(App_IfModelLoosePending(&fx.app, UI_IFHEAD_MODEL, 120), "keyed on what it loads");
    TEST_ASSERT(fx.app.packet_runner.queue->head == NULL, "not on the packet queue");
    app_if_head_enqueue(&fx.app, UI_IFHEAD_MODEL, -1, 120);
    TEST_ASSERT(queued(fx.app.frame_runner.queue) == 1, "asked again while on its way: one task");
    app_if_head_enqueue(&fx.app, UI_IFHEAD_OBJ, -1, 120);
    TEST_ASSERT(queued(fx.app.frame_runner.queue) == 2, "another kind is another head");
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_MODEL, 120, 0) == 1, "it asked for the model");
    TEST_ASSERT(loads_of(ASSET_KIND_OBJ_MODEL, 120, 1) == 1, "and the obj its chain");
    Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_MODEL, 120, 0), false);
    Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_OBJ_MODEL, 120, 1), false);
    pump(&fx);
    TEST_ASSERT(!App_IfModelLoosePending(&fx.app, UI_IFHEAD_MODEL, 120), "it ended");
    TEST_ASSERT(fx.app.if_models.head_count == 0, "and stored no component's head");
    /* Came with nothing to show: the next request asks again. */
    app_if_head_enqueue(&fx.app, UI_IFHEAD_MODEL, -1, 120);
    TEST_ASSERT(App_IfModelLoosePending(&fx.app, UI_IFHEAD_MODEL, 120), "asked again after nothing came");
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_MODEL, 120, 0) == 2, "and loads it again");
    add_model(&fx, 120);
    Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_MODEL, 120, 0), true);
    pump(&fx);
    TEST_ASSERT(!App_IfModelLoosePending(&fx.app, UI_IFHEAD_MODEL, 120), "it landed and ended");
    fixture_free(&fx);
}

int
main(void)
{
    test_group_mount();
    test_replaced_node_binds_nothing();
    test_dat1_slot_mount();
    test_root_build();
    test_bodytype_waits_for_configs_only();
    test_plugin_object_spawn();
    test_loose_head();
    if( g_failures )
    {
        printf("tasks-mounts: %d FAILED\n", g_failures);
        return 1;
    }
    printf("tasks-mounts: all tests passed\n");
    return 0;
}
