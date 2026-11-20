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
#include "engine/dat2/dat2_buildcache.h"
#include "game/rs_gameproto_exec.h"
#include "game/rs_server_state.h"
#include "inv/inv_manager.h"
#include "net/rev/revpacket.h"
#include "ui/uitree.h"
#include "ui/uitree_hook.h"

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

    printf("TEST: a script that goes away while it waits on a required read is forgotten by the table\n");

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
        CHECK(Assets_JoinCount(fx.app->assets, ASSET_KIND_CONFIG) == 1,
            "the script's read is not counted on the table (%d)",
            Assets_JoinCount(fx.app->assets, ASSET_KIND_CONFIG));
        task_free(task);
        CHECK(Assets_JoinCount(fx.app->assets, ASSET_KIND_CONFIG) == 0,
            "the table still counts a load into a freed script");
        /* The load's end: nothing is counted into the freed task. */
        Assets_Finished(fx.app->assets, callee, false);
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
        ToriRS_IOBatch_Reset(fx.io);
        CHECK(task_run(task, fx.io) == PT_ENDED, "the script did not carry on through its callee");
        CHECK(fx.app->tree->components[child].colour == 0,
            "a write through the stale context reached the component that took its id");
        CHECK(fx.app->tree->components[dot].colour == 0x345678,
            "the stale context cancelled the live dot context");
        task_free(task);
        fixture_free(&fx);
    }
}

int
main(void)
{
    test_a_newer_set_cancels_a_loc_model();
    test_a_server_set_cancels_a_loc_model();
    test_a_second_loc_set_replaces_the_first();
    test_a_closed_group_drops_the_loc_model();
    test_a_pack_hands_its_models_to_one_task();
    test_dialog_abort_runs_on_the_packet_queue();
    test_a_script_gone_mid_read_is_forgotten();

    if( g_failures )
    {
        printf("app_tasks_scripts_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("app_tasks_scripts_test: OK\n");
    return 0;
}
