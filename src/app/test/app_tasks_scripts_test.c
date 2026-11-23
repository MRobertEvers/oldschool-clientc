/*
 * The scripts family's parallel tasks (app/app_tasks_scripts.c) and the two
 * script-ordering faults of this step.
 *
 *   - A loc model a script set is ONE parallel task, listed in the pending
 *     index while it waits; a newer set on the widget cancels it (the latest
 *     set wins), a second loc set replaces it, and a group closed while it
 *     loaded makes it exit without touching the widget.
 *   - A pack a script baked hands its models to one task, which ends once
 *     they are here.
 *   - A server's IF_SETMODEL on the widget (the if_assets family's head
 *     store) cancels a script's loc model still loading for it: the latest
 *     set wins across the two families.
 *   - U9: TRIGGER_ONDIALOGABORT's listeners run on the PACKET queue, behind
 *     the RUNCLIENTSCRIPTs the tick held before it, not on the parallel queue
 *     where later packets overtook them.
 *   - G1: a burst of keys typed in one frame queues one onKey script per key,
 *     and the first of them waits on a read the line's script calls into.
 *     Each script must see the line the one before it wrote, as the
 *     reference runs them -- one at a time, in order -- so the line holds
 *     every key, not only the last.
 *   - cc_setobject on a MODEL widget whose model is cold poses the widget
 *     when it runs, as the reference's opcode does, and the model landing
 *     later does not pose it again: a cc_setmodelangle the script made after
 *     the set stays (the welcome screen's loading circle, whose thirteen
 *     runes each set their own zoom after their obj, pulsed all at once).
 *   - A client script waiting on a required read through the assets table
 *     that is freed first is forgotten by the table, so the load's end writes
 *     nothing into the freed task. One whose active component is freed while
 *     it waits carries on when the read lands, its writes through that
 *     context going nowhere and its other, live context still written.
 *
 *   make -C src test-tasks-scripts
 */

#include "app/app_gameproto_exec.h"
#include "app.h"
#include "app/app_internal.h"
#include "asyncio.h"
#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_script.h"
#include "mem/torirs_mem.h"
#include "game/task_cs2_run.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "game/rs_gameproto_exec.h"
#include "game/rs_server_state.h"
#include "inv/inv_manager.h"
#include "net/rev/revpacket.h"
#include "ui/uitree.h"
#include "ui/uitree_hook.h"
#include "varc/varc_manager.h"
#include "world/world.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

#define GROUP 300
#define WIDGET ((GROUP << 16) | 5)

/* ---- the fixture: an App with a tree, the queues and a fake loader -------- */

static int g_started;
static struct AssetKey g_last_started;

static enum AssetStart
fake_start(
    void* data,
    struct Assets* assets,
    struct AssetKey key)
{
    (void)data;
    (void)assets;
    g_started++;
    g_last_started = key;
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

struct Fixture
{
    struct App* app;
    struct Dat2BuildCache* bc;
    struct ToriRS_IOBatch* io;
    struct InvManager invs;
};

static void
fixture_init(struct Fixture* fx)
{
    struct App* app;
    struct AssetLoader loader = { .start = fake_start, .resident = fake_resident, .data = NULL };
    struct UITreeNodeSpec spec;
    struct RSCache profile;
    int32_t root;

    memset(fx, 0, sizeof(*fx));
    app = calloc(1, sizeof(*app));
    if( !app )
        abort();
    fx->app = app;
    fx->bc = dat2_buildcache_new();
    app->provider = dat2_buildcache_as_provider(fx->bc);
    if( !RSCache_ProfileByName("osrs239", &profile) )
        abort();
    CacheProvider_SetProfile(app->provider, &profile);
    App_AssetsInit(app);
    App_PendingInit(app);
    Assets_SetLoader(app->assets, ASSET_KIND_CONFIG, &loader);
    Assets_SetLoader(app->assets, ASSET_KIND_MODEL, &loader);
    app->frame_runner.queue = ToriRS_TaskQueue_New();
    app->packet_runner.queue = ToriRS_TaskQueue_New();
    /* As App_Init: a loader's siblings, and a script's wait task, go on the
     * parallel queue. */
    CacheProvider_SetAssetQueue(app->provider, app->frame_runner.queue);
    fx->io = ToriRS_IOBatch_New();
    RS_ClientScriptQueue_Init(&app->pending_clientscripts);

    app->tree = UITree_New(16);
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LAYER;
    spec.component_id = GROUP << 16;
    spec.width = 100;
    spec.height = 100;
    root = UITree_Push(app->tree, -1, &spec);
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_MODEL;
    spec.component_id = WIDGET;
    spec.width = 32;
    spec.height = 32;
    spec.u.rs_model.gamecache_model_id = -1;
    if( root < 0 || UITree_Push(app->tree, root, &spec) < 0 )
        abort();
    InvManager_Init(&fx->invs);
    RS_CS2Host_Init(&app->host, app->tree, app->provider, &fx->invs, NULL, NULL, NULL);
    g_started = 0;
}

/* One pass of the parallel queue by hand: every task that is not parked. */
static void
fixture_step_frame_queue(struct Fixture* fx)
{
    struct ToriRS_Task* task = fx->app->frame_runner.queue->head;

    while( task )
    {
        struct ToriRS_Task* next = task->next;
        if( !task->parked )
            (void)ToriRS_TaskQueue_RunTask(fx->app->frame_runner.queue, fx->io, task);
        task = next;
    }
}

static void
fixture_free(struct Fixture* fx)
{
    struct App* app = fx->app;

    Assets_Close(app->assets);
    ToriRS_TaskQueue_Free(app->frame_runner.queue);
    ToriRS_TaskQueue_Free(app->packet_runner.queue);
    ToriRS_IOBatch_Free(fx->io);
    App_PendingFree(app);
    App_AssetsFree(app);
    RS_ClientScriptQueue_Free(&app->pending_clientscripts);
    UIIfModels_Free(&app->if_models);
    UITree_Free(app->tree);
    dat2_buildcache_free(fx->bc);
    free(app);
}

static int
queued(struct ToriRS_TaskQueue const* queue)
{
    int n = 0;
    for( struct ToriRS_Task const* t = queue->head; t; t = t->next )
        n++;
    return n;
}

/* Step every task on the parallel queue once. */
static void
step_parallel(struct Fixture* fx)
{
    struct ToriRS_TaskQueue* queue = fx->app->frame_runner.queue;
    struct ToriRS_Task* task = queue->head;

    while( task )
    {
        struct ToriRS_Task* next = task->next;
        /* A parked task is not run: only what it parked on readies it. */
        if( task->parked )
        {
            task = next;
            continue;
        }
        (void)ToriRS_TaskQueue_RunTask(queue, fx->io, task);
        task = next;
    }
}

/* ---- the loc model -------------------------------------------------------- */

static void
test_a_newer_set_cancels_a_loc_model(void)
{
    struct Fixture fx;

    printf("TEST: a loc model is one listed task; a newer set on the widget cancels it\n");
    fixture_init(&fx);

    app_cs2_widget_loc_model_lazy(fx.app, WIDGET, 77);
    CHECK(App_ScriptVisualPending(fx.app, APP_SCRIPT_TASK_LOC_MODEL, WIDGET),
        "the loc model is not pending");
    CHECK(queued(fx.app->frame_runner.queue) == 1, "not one task on the parallel queue");
    CHECK(queued(fx.app->packet_runner.queue) == 0, "the packet queue was given work");
    step_parallel(&fx);
    CHECK(g_started == 1 && g_last_started.kind == ASSET_KIND_CONFIG &&
              g_last_started.id == 77 && g_last_started.variant == ASSET_CONFIG_LOC,
        "the task did not ask for the loc's config");
    CHECK(queued(fx.app->frame_runner.queue) == 1, "the task ended before its asset came");

    /* A resident cc_setmodel on the same widget: the latest set wins. */
    app_cs2_widget_visual_cancel(fx.app, WIDGET);
    CHECK(!App_ScriptVisualPending(fx.app, APP_SCRIPT_TASK_LOC_MODEL, WIDGET),
        "the cancelled loc model is still pending");
    step_parallel(&fx);
    CHECK(queued(fx.app->frame_runner.queue) == 0, "the cancelled task did not exit");
    fixture_free(&fx);
}

/* The server's set reaches the widget through the head store, not the CS2
 * host, so the host's cancel never runs for it: the store cancels. */
static void
test_a_server_set_cancels_a_loc_model(void)
{
    struct Fixture fx;

    printf("TEST: a server IF_SETMODEL cancels a script's loc model on the widget\n");
    fixture_init(&fx);

    app_cs2_widget_loc_model_lazy(fx.app, WIDGET, 77);
    CHECK(App_ScriptVisualPending(fx.app, APP_SCRIPT_TASK_LOC_MODEL, WIDGET),
        "the loc model is not pending");
    App_SetInterfaceModel(fx.app, WIDGET, 55);
    CHECK(!App_ScriptVisualPending(fx.app, APP_SCRIPT_TASK_LOC_MODEL, WIDGET),
        "the script's loc model is still pending after the server's IF_SETMODEL; it would bind "
        "over the newer model when it lands");
    CHECK(App_IfModelPending(fx.app, WIDGET), "the server's model has no task");
    fixture_free(&fx);
}

/*
 * IF_SETPLAYERMODEL_* is the server's set too, and it reaches the widget
 * through neither the CS2 host nor the head store: the composition is the
 * widget's model from the packet (its build lands later, and the poll binds
 * it). A script's loc or obj model still loading for the widget must not land
 * over it. At 1528fc3a1 the script was held until its model was here, so its
 * set always came first and the player model last.
 */
static void
test_a_server_player_model_cancels_script_models(void)
{
    struct Fixture fx;
    int world_idx;

    printf("TEST: a server IF_SETPLAYERMODEL_SELF cancels a script's loc and obj model on the widget\n");
    fixture_init(&fx);
    fx.app->world = World_New();
    RS_EntitySync_Init(&fx.app->esync);
    fx.app->esync.local_pid = 3;
    world_idx = World_PlayerSpawn(fx.app->world, 7, 0, 20, 20, (struct WorldEntityFacet_IdleAnimations){ 0 });
    RS_EntitySync_RegisterPlayer(&fx.app->esync, 3, 7, world_idx);

    app_cs2_widget_loc_model_lazy(fx.app, WIDGET, 77);
    CHECK(App_ScriptVisualPending(fx.app, APP_SCRIPT_TASK_LOC_MODEL, WIDGET),
        "the loc model is not pending");
    App_SetInterfacePlayerModelSelf(fx.app, WIDGET, 1);
    CHECK(!App_ScriptVisualPending(fx.app, APP_SCRIPT_TASK_LOC_MODEL, WIDGET),
        "the script's loc model is still pending after the server's IF_SETPLAYERMODEL_SELF; it "
        "would bind over the player's model when it lands");

    app_cs2_widget_obj_model_lazy(fx.app, WIDGET, 4151, 1, 0);
    CHECK(App_ScriptVisualPending(fx.app, APP_SCRIPT_TASK_OBJ_MODEL, WIDGET),
        "the obj model is not pending");
    App_SetInterfacePlayerModelBaseColour(fx.app, WIDGET, 0, 3);
    CHECK(!App_ScriptVisualPending(fx.app, APP_SCRIPT_TASK_OBJ_MODEL, WIDGET),
        "the script's obj model is still pending after the server's IF_SETPLAYERMODEL_BASECOLOUR");

    /* The composition's build task is left unrun (this fixture has no
     * bridge to build into); freeing the queue frees it. */
    RS_EntitySync_Free(&fx.app->esync);
    World_Free(fx.app->world);
    fx.app->world = NULL;
    fixture_free(&fx);
}

static void
test_a_second_loc_set_replaces_the_first(void)
{
    struct Fixture fx;

    printf("TEST: a second loc set on one widget replaces the first\n");
    fixture_init(&fx);

    app_cs2_widget_loc_model_lazy(fx.app, WIDGET, 77);
    step_parallel(&fx);
    app_cs2_widget_loc_model_lazy(fx.app, WIDGET, 78);
    CHECK(queued(fx.app->frame_runner.queue) == 2, "the second set did not add its task");
    step_parallel(&fx);
    CHECK(queued(fx.app->frame_runner.queue) == 1, "the replaced task did not exit");
    CHECK(App_ScriptVisualPending(fx.app, APP_SCRIPT_TASK_LOC_MODEL, WIDGET),
        "the newer loc model is not pending");
    CHECK(g_last_started.id == 78, "the newer task did not ask for its own loc");
    fixture_free(&fx);
}

static void
test_a_closed_group_drops_the_loc_model(void)
{
    struct Fixture fx;

    printf("TEST: a loc model whose interface closed while it loaded exits untouched\n");
    fixture_init(&fx);

    app_cs2_widget_loc_model_lazy(fx.app, WIDGET, 79);
    step_parallel(&fx);
    UITree_ReclaimInterfaceGroup(fx.app->tree, GROUP);
    /* The read ends without the loc: the wait still ends. Reaching the bind
     * here would assert on the fixture's absent scene. */
    Assets_Finished(fx.app->assets, AssetKey_Make(ASSET_KIND_CONFIG, 79, ASSET_CONFIG_LOC), false);
    step_parallel(&fx);
    CHECK(queued(fx.app->frame_runner.queue) == 0, "the task outlived its closed interface");
    fixture_free(&fx);
}

/* ---- a baked pack's models ------------------------------------------------ */

static void
test_a_pack_hands_its_models_to_one_task(void)
{
    struct Fixture fx;

    printf("TEST: a baked pack's models are one task that ends when they are here\n");
    fixture_init(&fx);

    app_cs2_pack_models_lazy(fx.app, GROUP);
    CHECK(App_ScriptVisualPending(fx.app, APP_SCRIPT_TASK_PACK_MODELS, GROUP << 16),
        "the pack's models are not pending");
    /* The fixture's provider holds no pack: nothing to wait for. */
    step_parallel(&fx);
    CHECK(queued(fx.app->frame_runner.queue) == 0, "the pack task did not end");
    CHECK(!App_ScriptVisualPending(fx.app, APP_SCRIPT_TASK_PACK_MODELS, GROUP << 16),
        "the ended pack task is still listed");
    fixture_free(&fx);
}

/* ---- U9: TRIGGER_ONDIALOGABORT keeps wire order --------------------------- */

static void
test_dialog_abort_runs_on_the_packet_queue(void)
{
    struct Fixture fx;
    struct UITreeRuntimeScriptHook* hook;
    struct PktRunClientScript held;
    int32_t idx;
    struct VarPManager varps;
    static struct RS_Event events[16];
    struct RS_EventRing ring;
    struct RS_ServerState state;
    struct RevPacket packet;

    printf("TEST: onDialogAbort listeners run on the packet queue, after the held scripts (U9)\n");
    fixture_init(&fx);

    idx = UITree_FindByComponentId(fx.app->tree, WIDGET);
    hook = &UITree_HooksMut(&fx.app->tree->components[idx])->on_dialog_abort;
    UITree_HookSet(hook, 1234, NULL, 0, 0, NULL, 0);

    /* A RUNCLIENTSCRIPT earlier in the tick, held for the fence. */
    memset(&held, 0, sizeof(held));
    held.script_id = 4321;
    RS_ClientScriptQueue_Hold(&fx.app->pending_clientscripts, &held, 1);

    VarPManager_Init(&varps);
    memset(&ring, 0, sizeof(ring));
    /* Every record the model requires (RS_ServerState_Apply asserts each),
     * bound to the fixture's App as the client binds them per packet. */
    App_ServerStateBind(fx.app, &state);
    state.varps = &varps;
    state.invs = &fx.invs;
    state.rev_number = -1;
    state.events = (struct RS_EventSink){ .ring = &ring, .entries = events, .capacity = 16,
                                          .full = RS_EVENT_RING_OVERWRITE };
    {
        memset(&packet, 0, sizeof(packet));
        packet.packet_type = PKT_NAME_TRIGGER_ONDIALOGABORT;
        App_GameProto_Exec(fx.app, &state, &packet);
    }

    CHECK(queued(fx.app->frame_runner.queue) == 0,
        "a listener went to the parallel queue, where later packets overtake it");
    CHECK(queued(fx.app->packet_runner.queue) == 2,
        "the packet queue holds %d tasks, not the held script and the listener",
        queued(fx.app->packet_runner.queue));
    CHECK(RS_ClientScriptQueue_Count(&fx.app->pending_clientscripts) == 0,
        "the script held before the abort was not released ahead of it");
    fixture_free(&fx);
}

/* ---- an obj model a later set made stale --------------------------------- */

#define COLD_OBJ 4151
#define COLD_OBJ_MODEL 99990

/* A resident objtype whose inventory model is not: the set can pose the
 * widget at once, and its model is one parallel task. */
static void
add_cold_model_obj(
    struct Fixture* fx,
    int obj_id,
    int zoom2d)
{
    struct ToriRS_Objtype* obj = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*obj));

    assert(obj);
    obj->inventory_model_id = COLD_OBJ_MODEL;
    obj->resize_x = obj->resize_y = obj->resize_z = 128;
    obj->xan2d = 300;
    obj->yan2d = 600;
    obj->zoom2d = zoom2d;
    CacheProvider_ObjtypeAdd(fx->app->provider, obj_id, obj);
}

/* One triangle: enough of a model for the bridge to build the obj's. */
static struct ToriRS_Model*
one_triangle(void)
{
    struct ToriRS_Model* m = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*m));

    assert(m);
    m->vertex_count = 3;
    m->face_count = 1;
    m->vertices_x = ToriMem_Calloc(ToriMem_Assets(), 3, sizeof(*m->vertices_x));
    m->vertices_y = ToriMem_Calloc(ToriMem_Assets(), 3, sizeof(*m->vertices_y));
    m->vertices_z = ToriMem_Calloc(ToriMem_Assets(), 3, sizeof(*m->vertices_z));
    m->face_indices_a = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*m->face_indices_a));
    m->face_indices_b = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*m->face_indices_b));
    m->face_indices_c = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*m->face_indices_c));
    m->face_colors = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*m->face_colors));
    m->face_colors_a = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*m->face_colors_a));
    m->face_colors_b = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*m->face_colors_b));
    m->face_colors_c = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*m->face_colors_c));
    assert(m->vertices_x);
    assert(m->vertices_y);
    assert(m->vertices_z);
    assert(m->face_indices_a);
    assert(m->face_indices_b);
    assert(m->face_indices_c);
    assert(m->face_colors);
    assert(m->face_colors_a);
    assert(m->face_colors_b);
    assert(m->face_colors_c);
    m->vertices_x[1] = 64;
    m->vertices_z[2] = 64;
    m->face_indices_b[0] = 1;
    m->face_indices_c[0] = 2;
    return m;
}

static int
host_set_model_zoom(
    struct CS2VM2_Thread* t,
    int component_id,
    int zoom)
{
    struct CS2VM_HostRequest req;

    memset(&req, 0, sizeof(req));
    req.kind = CS2VM_HOST_REQUEST_CC_SETMODELANGLE;
    req.u.CC_SETMODELANGLE.component_id = component_id;
    req.u.CC_SETMODELANGLE.angle_x = 512;
    req.u.CC_SETMODELANGLE.angle_y = 1024;
    req.u.CC_SETMODELANGLE.zoom = zoom;
    return RS_CS2Host_Exec(t, &req);
}

static int
host_set_object(
    struct CS2VM2_Thread* t,
    int component_id,
    int obj_id,
    int count)
{
    struct CS2VM_HostRequest req;

    memset(&req, 0, sizeof(req));
    req.kind = CS2VM_HOST_REQUEST_IF_SETOBJECT;
    req.u.IF_SETOBJECT.component_id = component_id;
    req.u.IF_SETOBJECT.obj_id = obj_id;
    req.u.IF_SETOBJECT.count = count;
    req.u.IF_SETOBJECT.num_mode = 0;
    return RS_CS2Host_Exec(t, &req);
}

/*
 * A script sets a cold obj on a MODEL widget (one parallel task waits for its
 * model) and then clears the widget, `if_setobject(-1)`: a set that needs no
 * task, so it cancels none. The older task must not put its obj back when its
 * model lands -- the latest set wins. At 1528fc3a1 the first set held the
 * script until the model was here, so the clear always came last.
 */
static void
test_a_clear_outlives_an_obj_model(void)
{
    struct Fixture fx;
    struct AssetLoader loader = { .start = fake_start, .resident = fake_resident, .data = NULL };
    struct CS2VM2 vm;
    struct CS2VM2_Thread* t;
    int32_t idx;

    printf("TEST: a clear after a cold obj model on a MODEL widget is not undone when the model lands\n");
    fixture_init(&fx);
    Assets_SetLoader(fx.app->assets, ASSET_KIND_OBJ_MODEL, &loader);
    fx.app->scene = ToriDraw_SceneNew(
        TORIDRAW_SCENE_SMALL | TORIDRAW_SCENE_DEPTH_16K | TORIDRAW_SCENE_MODEL_ZBUFFER,
        TORIDRAW_SCRATCH_BUFFER_VERYHIGH_16K);
    if( !fx.app->scene )
        abort();
    UITreeSceneBridge_Init(&fx.app->bridge, fx.app->scene, fx.app->provider);
    RS_CS2Host_SetBridge(&fx.app->host, &fx.app->bridge);
    fx.app->host.world_user = fx.app;
    app_tasks_scripts_bind_host(fx.app);
    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, &fx.app->host, RS_CS2Host_Exec);
    t = CS2VM2_ThreadMain(&vm);
    idx = UITree_FindByComponentId(fx.app->tree, WIDGET);
    add_cold_model_obj(&fx, COLD_OBJ, 1500);

    CHECK(host_set_object(t, WIDGET, COLD_OBJ, 1) == CS2VM_EXECNO_OK, "the cold set stopped the script");
    CHECK(App_ScriptVisualPending(fx.app, APP_SCRIPT_TASK_OBJ_MODEL, WIDGET),
        "the cold obj's model has no task");
    step_parallel(&fx);
    CHECK(host_set_object(t, WIDGET, -1, 0) == CS2VM_EXECNO_OK, "the clear stopped the script");
    CHECK(fx.app->tree->components[idx].item_id == UITREE_NO_OBJ, "the clear did not clear the node");

    /* The older set's model comes. */
    Assets_Finished(fx.app->assets, AssetKey_Make(ASSET_KIND_OBJ_MODEL, COLD_OBJ, 1), true);
    step_parallel(&fx);
    CHECK(queued(fx.app->frame_runner.queue) == 0, "the obj model task did not end");
    CHECK(fx.app->tree->components[idx].item_id == UITREE_NO_OBJ,
        "the widget holds obj %d again: the older set's task undid the later clear",
        fx.app->tree->components[idx].item_id);

    CS2VM2_Free(&vm);
    UITreeSceneBridge_Free(&fx.app->bridge);
    ToriDraw_SceneFree(fx.app->scene);
    fx.app->scene = NULL;
    fixture_free(&fx);
}

/*
 * loading_icon_orbit_build (the welcome screen's circle of runes) does, per
 * rune: cc_create(MODEL), cc_setobject(rune), cc_setmodelangle(.., zoom) with
 * a zoom stepped around the circle, and an onTimer that grows each zoom from
 * there. The reference poses the widget from the objtype inside CC_SETOBJECT,
 * so the script's own zoom lands on top of it and the runes stay staggered:
 * the circle turns. Here the set wrote the pose only when its cold model
 * landed, frames later, over the script's zoom -- every rune back on the
 * same zoom2d, and the circle pulsed all at once (4697d7696).
 */
static void
test_a_cold_obj_model_keeps_the_scripts_pose(void)
{
    struct Fixture fx;
    struct AssetLoader loader = { .start = fake_start, .resident = fake_resident, .data = NULL };
    struct CS2VM2 vm;
    struct CS2VM2_Thread* t;
    int32_t idx;

    printf("TEST: a cold obj model on a MODEL widget is posed by the set; its landing keeps the "
           "script's later zoom\n");
    fixture_init(&fx);
    Assets_SetLoader(fx.app->assets, ASSET_KIND_OBJ_MODEL, &loader);
    fx.app->scene = ToriDraw_SceneNew(
        TORIDRAW_SCENE_SMALL | TORIDRAW_SCENE_DEPTH_16K | TORIDRAW_SCENE_MODEL_ZBUFFER,
        TORIDRAW_SCRATCH_BUFFER_VERYHIGH_16K);
    if( !fx.app->scene )
        abort();
    UITreeSceneBridge_Init(&fx.app->bridge, fx.app->scene, fx.app->provider);
    RS_CS2Host_SetBridge(&fx.app->host, &fx.app->bridge);
    fx.app->host.world_user = fx.app;
    app_tasks_scripts_bind_host(fx.app);
    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, &fx.app->host, RS_CS2Host_Exec);
    t = CS2VM2_ThreadMain(&vm);
    idx = UITree_FindByComponentId(fx.app->tree, WIDGET);
    add_cold_model_obj(&fx, COLD_OBJ, 1500);

    CHECK(host_set_object(t, WIDGET, COLD_OBJ, 1) == CS2VM_EXECNO_OK, "the cold set stopped the script");
    CHECK(App_ScriptVisualPending(fx.app, APP_SCRIPT_TASK_OBJ_MODEL, WIDGET),
        "the cold obj's model has no task");
    CHECK(fx.app->tree->components[idx].u.rs_model.zoom == 1500 &&
              fx.app->tree->components[idx].u.rs_model.xan == 300 &&
              fx.app->tree->components[idx].u.rs_model.yan == 600,
        "the set did not pose the widget from its objtype (zoom %d, xan %d, yan %d): "
        "cc_getmodelzoom after cc_setobject reads what the reference's opcode wrote",
        fx.app->tree->components[idx].u.rs_model.zoom,
        fx.app->tree->components[idx].u.rs_model.xan,
        fx.app->tree->components[idx].u.rs_model.yan);
    step_parallel(&fx);

    /* The script's own pose, after the set (loading_icon_orbit_build). */
    CHECK(host_set_model_zoom(t, WIDGET, 2700) == CS2VM_EXECNO_OK, "the angle set stopped the script");
    CHECK(fx.app->tree->components[idx].u.rs_model.zoom == 2700, "cc_setmodelangle did not write the zoom");

    /* The model lands. */
    CacheProvider_ModelAdd(fx.app->provider, COLD_OBJ_MODEL, one_triangle());
    Assets_Finished(fx.app->assets, AssetKey_Make(ASSET_KIND_OBJ_MODEL, COLD_OBJ, 1), true);
    step_parallel(&fx);
    CHECK(queued(fx.app->frame_runner.queue) == 0, "the obj model task did not end");
    CHECK(fx.app->tree->components[idx].item_id == COLD_OBJ, "the widget lost its obj");
    CHECK(fx.app->tree->components[idx].u.rs_model.gamecache_model_id > 0,
        "the landed model was not bound to the widget");
    CHECK(fx.app->tree->components[idx].u.rs_model.zoom == 2700 &&
              fx.app->tree->components[idx].u.rs_model.xan == 512 &&
              fx.app->tree->components[idx].u.rs_model.yan == 1024,
        "the model's landing re-posed the widget over the script's later cc_setmodelangle "
        "(zoom %d, xan %d, yan %d; the script wrote 2700, 512, 1024)",
        fx.app->tree->components[idx].u.rs_model.zoom,
        fx.app->tree->components[idx].u.rs_model.xan,
        fx.app->tree->components[idx].u.rs_model.yan);

    CS2VM2_Free(&vm);
    UITreeSceneBridge_Free(&fx.app->bridge);
    ToriDraw_SceneFree(fx.app->scene);
    fx.app->scene = NULL;
    fixture_free(&fx);
}

/* ---- a script that goes away mid-read is forgotten by the table ---------- */

#define MISSING_CALLEE 99996

static void
test_a_script_gone_mid_read_is_forgotten(void)
{
    struct Fixture fx;
    struct AssetKey callee = AssetKey_Make(ASSET_KIND_CONFIG, MISSING_CALLEE, ASSET_CONFIG_SCRIPT);
    struct ToriRS_Task* task;
    int32_t root;
    int32_t child;
    int child_id;

    printf("TEST: a script that goes away while it waits on a required read leaves its wait counting into nothing\n");

    /* Freed mid-wait: a gosub to a script the cache has not got. */
    {
        uint16_t ops[] = { CS2_OP_GOSUB_WITH_PARAMS, CS2_OP_RETURN };
        int operands[] = { MISSING_CALLEE, 0 };
        char* strings[] = { NULL, NULL };
        struct CS2VM2_Script script = { .script_id = 99995, .op_count = 2, .opcodes = ops,
                                        .int_operands = operands, .string_operands = strings };

        fixture_init(&fx);
        fx.app->host.assets = fx.app->assets;
        task = CreateTask_CS2RunScript(&fx.app->host, &script, -1, -1, NULL, 0);
        fx.io->task = task;
        CHECK(task_run(task, fx.io) == PT_YIELDED, "the script did not stop for its callee");
        /* Its wait task is queued beside it; that task asks the table. */
        CHECK(fx.app->frame_runner.queue->head && !fx.app->frame_runner.queue->head->next,
            "the script did not queue one wait task");
        fixture_step_frame_queue(&fx);
        CHECK(Assets_JoinCount(fx.app->assets, ASSET_KIND_CONFIG) == 1,
            "the wait task's read is not counted on the table (%d)",
            Assets_JoinCount(fx.app->assets, ASSET_KIND_CONFIG));
        task_free(task);
        CHECK(fx.app->frame_runner.queue->head && !fx.app->frame_runner.queue->head->join,
            "the wait task still counts into the freed script");
        /* The load's end: the wait task ends on its own, counting into
         * nothing. */
        Assets_Finished(fx.app->assets, callee, false);
        CHECK(Assets_JoinCount(fx.app->assets, ASSET_KIND_CONFIG) == 0,
            "the table still counts the ended load");
        fixture_step_frame_queue(&fx);
        CHECK(!fx.app->frame_runner.queue->head, "the wait task did not end with its load");
        fixture_free(&fx);
    }

    /* Its active component deleted (and its id taken by a new one) while it
     * waits on its callee through the table: when the callee lands the script
     * carries on, its writes through the stale context go nowhere, its live
     * dot context is still written, and the table counts nothing. */
    {
        uint16_t ops[] = { CS2_OP_GOSUB_WITH_PARAMS, CS2_OP_PUSH_CONSTANT_INT, CS2_OP_CC_SETCOLOUR,
                           CS2_OP_PUSH_CONSTANT_INT, CS2_OP_CC_SETCOLOUR, CS2_OP_RETURN };
        int operands[] = { MISSING_CALLEE, 0x234567, 0, 0x345678, 1, 0 };
        char* strings[] = { NULL, NULL, NULL, NULL, NULL, NULL };
        struct CS2VM2_Script script = { .script_id = 99997, .op_count = 6, .opcodes = ops,
                                        .int_operands = operands, .string_operands = strings,
                                        .int_stack_depth = 1 };
        uint16_t callee_ops[] = { CS2_OP_RETURN };
        int callee_operands[] = { 0 };
        char* callee_strings[] = { NULL };
        struct CS2VM2_Script callee_src = { .script_id = MISSING_CALLEE, .op_count = 1,
                                            .opcodes = callee_ops, .int_operands = callee_operands,
                                            .string_operands = callee_strings };
        struct CS2VM2_Script* cached;
        int32_t dot;
        int dot_id;

        fixture_init(&fx);
        fx.app->host.assets = fx.app->assets;
        root = UITree_FindByComponentId(fx.app->tree, GROUP << 16);
        child = UITree_CcCreate(fx.app->tree, root, GROUP << 16, 3, 0);
        dot = UITree_CcCreate(fx.app->tree, root, GROUP << 16, 3, 1);
        CHECK(child >= 0 && dot >= 0, "no dynamic children");
        child_id = fx.app->tree->components[child].component_id;
        dot_id = fx.app->tree->components[dot].component_id;
        task = CreateTask_CS2RunScript(&fx.app->host, &script, child_id, dot_id, NULL, 0);
        fx.io->task = task;
        CHECK(task_run(task, fx.io) == PT_YIELDED, "the script did not stop for its callee");
        fixture_step_frame_queue(&fx);
        CHECK(Assets_JoinCount(fx.app->assets, ASSET_KIND_CONFIG) == 1,
            "the callee read is not counted on the table (%d)",
            Assets_JoinCount(fx.app->assets, ASSET_KIND_CONFIG));
        UITree_CcDelete(fx.app->tree, child);
        fx.app->tree->next_dynamic_uid = (uint16_t)(child_id & 0xffff);
        child = UITree_CcCreate(fx.app->tree, root, GROUP << 16, 3, 0);
        CHECK(fx.app->tree->components[child].component_id == child_id, "the id was not reused");
        cached = ToriMem_Alloc(ToriMem_Script(), sizeof(*cached));
        CS2VM2_ScriptInit(cached);
        CHECK(CS2VM2_ScriptCopy(&callee_src, cached), "copy the callee");
        CacheProvider_ClientScriptAdd(fx.app->provider, MISSING_CALLEE, cached);
        Assets_Finished(fx.app->assets, callee, true);
        CHECK(Assets_JoinCount(fx.app->assets, ASSET_KIND_CONFIG) == 0,
            "the landed callee is still counted");
        /* The wait task ends, which is what the script is joined on. */
        fixture_step_frame_queue(&fx);
        CHECK(!fx.app->frame_runner.queue->head, "the wait task did not end with its load");
        ToriRS_IOBatch_Reset(fx.io);
        fx.io->task = task;
        CHECK(task_run(task, fx.io) == PT_ENDED, "the script did not carry on through its callee");
        CHECK(fx.app->tree->components[child].colour == 0,
            "a write through the stale context reached the component that took its id");
        CHECK(fx.app->tree->components[dot].colour == 0x345678,
            "the stale context cancelled the live dot context");
        task_free(task);
        fixture_free(&fx);
    }
}


/* ---- G1: a typed burst's scripts run one at a time, in order ------------- */

#define LINE_VARC 335
#define LINE_CALLEE 99980

/* One pass of the parallel queue as TaskRunner_Step walks it: every task that
 * is not waiting on a read or parked gets one step. */
static int
parallel_pass(
    struct ToriRS_TaskQueue* queue,
    struct ToriRS_IOBatch* io)
{
    struct ToriRS_Task* task;
    struct ToriRS_Task* next;
    int ran = 0;

    for( task = queue->head; task; task = next )
    {
        next = task->next;
        if( task->io.pending || task->parked )
            continue;
        ToriRS_IOBatch_Reset(io);
        (void)ToriRS_TaskQueue_RunTask(queue, io, task);
        ran++;
    }
    return ran;
}

static void
test_a_typed_burst_keeps_every_key(void)
{
    /* chatdefault_onkey's shape (clientscript 73): read the line, call a
     * proc, write the line plus the key. The proc is not resident the first
     * time anything is typed, so the first key's script waits for it with
     * the line it read already on its stack. */
    static char const keys[] = "abcd";
    uint16_t ops[] = { CS2_OP_PUSH_VARC_STRING, CS2_OP_GOSUB_WITH_PARAMS, CS2_OP_PUSH_CONSTANT_STRING,
                       CS2_OP_JOIN_STRING, CS2_OP_POP_VARC_STRING, CS2_OP_RETURN };
    struct AssetKey callee = AssetKey_Make(ASSET_KIND_CONFIG, LINE_CALLEE, ASSET_CONFIG_SCRIPT);
    uint16_t callee_ops[] = { CS2_OP_RETURN };
    int callee_operands[] = { 0 };
    char* callee_strings[] = { NULL };
    struct CS2VM2_Script callee_src = { .script_id = LINE_CALLEE, .op_count = 1,
                                        .opcodes = callee_ops, .int_operands = callee_operands,
                                        .string_operands = callee_strings };
    struct CS2VM2_Script* cached;
    struct VarCManager varcs;
    struct Fixture fx;
    char key_text[sizeof(keys)][2];
    int operands[sizeof(keys)][6];
    char* strings[sizeof(keys)][6];
    struct CS2VM2_Script scripts[sizeof(keys)];
    int passes = 0;

    printf("TEST: a burst of keys typed in one frame keeps every key (G1)\n");
    fixture_init(&fx);
    fx.app->host.assets = fx.app->assets;
    fx.app->frame_runner.parallel = 1;
    VarCManager_Init(&varcs);
    fx.app->host.varcs = &varcs;

    for( int k = 0; k < (int)sizeof(keys) - 1; k++ )
    {
        struct ToriRS_Task* task;

        key_text[k][0] = keys[k];
        key_text[k][1] = '\0';
        operands[k][0] = LINE_VARC;
        operands[k][1] = LINE_CALLEE;
        operands[k][2] = 0;
        operands[k][3] = 2;
        operands[k][4] = LINE_VARC;
        operands[k][5] = 0;
        for( int i = 0; i < 6; i++ )
            strings[k][i] = NULL;
        strings[k][2] = key_text[k];
        memset(&scripts[k], 0, sizeof(scripts[k]));
        scripts[k].script_id = 99981 + k;
        scripts[k].op_count = 6;
        scripts[k].opcodes = ops;
        scripts[k].int_operands = operands[k];
        scripts[k].string_operands = strings[k];
        scripts[k].str_stack_depth = 2;
        task = CreateTask_CS2RunScript(&fx.app->host, &scripts[k], -1, -1, NULL, 0);
        /* How the key broadcast queues each one (RS_CS2_DispatchHook). */
        TaskRunner_AddRenderBlockingSerialTask(&fx.app->frame_runner, task);
    }

    /* The frame's pump: the first script stops for the proc. */
    (void)parallel_pass(fx.app->frame_runner.queue, fx.io);
    CHECK(Assets_JoinCount(fx.app->assets, ASSET_KIND_CONFIG) >= 1, "nothing waits on the proc");

    /* The proc lands. */
    cached = ToriMem_Alloc(ToriMem_Script(), sizeof(*cached));
    CS2VM2_ScriptInit(cached);
    CHECK(CS2VM2_ScriptCopy(&callee_src, cached), "copy the proc");
    CacheProvider_ClientScriptAdd(fx.app->provider, LINE_CALLEE, cached);
    Assets_Finished(fx.app->assets, callee, true);

    while( fx.app->frame_runner.queue->head && passes++ < 32 )
        (void)parallel_pass(fx.app->frame_runner.queue, fx.io);

    CHECK(!fx.app->frame_runner.queue->head, "scripts left after %d passes", passes);
    CHECK(strcmp(VarCManager_GetString(&varcs, LINE_VARC), "abcd") == 0,
        "the typed line is \"%s\", not \"abcd\": a later key's script ran while an earlier one "
        "waited and wrote over it",
        VarCManager_GetString(&varcs, LINE_VARC));
    fx.app->host.varcs = NULL;
    VarCManager_Free(&varcs);
    fixture_free(&fx);
}

int
main(void)
{
    test_a_newer_set_cancels_a_loc_model();
    test_a_server_set_cancels_a_loc_model();
    test_a_server_player_model_cancels_script_models();
    test_a_second_loc_set_replaces_the_first();
    test_a_closed_group_drops_the_loc_model();
    test_a_pack_hands_its_models_to_one_task();
    test_dialog_abort_runs_on_the_packet_queue();
    test_a_script_gone_mid_read_is_forgotten();
    test_a_clear_outlives_an_obj_model();
    test_a_cold_obj_model_keeps_the_scripts_pose();
    test_a_typed_burst_keeps_every_key();

    if( g_failures )
    {
        printf("app_tasks_scripts_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("app_tasks_scripts_test: OK\n");
    return 0;
}
