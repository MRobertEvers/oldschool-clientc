/*
 * Work that kept a tracker of its own, moved onto the assets table and the
 * pending index (engine/asset_requests.h, game/rs_pending.h), on a stack App
 * whose assets table has fake loaders the test answers by hand.
 *
 * Pins:
 *   - A player's body fetch (game/task_entity_assets.c, EntityAssets_
 *     WantPlayerBody) is ONE parallel task per distinct body in flight,
 *     listed in the pending index: a second appearance naming the same body
 *     adds nothing (each block queued its own fetch before), a different body
 *     is its own task, a resident body none; each part loads once however
 *     many fetches name it; a logout's cancel ends it.
 *   - A client trigger's subject is the script's own data
 *     (Task_CS2Run_SetSubject): each queued script writes ITS subject to the
 *     client-op register at its first step, in place of the one-shot
 *     Task_ClientTriggerSubject queued in front of it.
 *   - The npc multi-resolve reads each rung's config through the assets table
 *     (ASSET_CONFIG_NPC): two resolves of one type load it once.
 *
 * Run: make -C src test-app-trackers
 */
#include "app.h"
#include "app/app_internal.h"
#include "asyncio.h"
#include "engine/asset_requests.h"
#include "engine/cache_provider.h"
#include "engine/uitree_scene_bridge.h"
#include "game/rs_clientop.h"
#include "game/task_cs2_run.h"
#include "game/task_entity_assets.h"
#include "mem/torirs_mem.h"
#include "net/rev/packets/pkt_player_appearance.h"
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
        ASSET_KIND_MODEL, ASSET_KIND_SEQUENCE, ASSET_KIND_CONFIG
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
add_kit(
    struct Fixture* fx,
    int kit_id,
    int model_id)
{
    struct ToriRS_Idk* idk = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*idk));
    assert(idk);
    idk->body_part_id = 0;
    idk->model_ids_count = 1;
    idk->model_ids = ToriMem_Alloc(ToriMem_Assets(), sizeof(*idk->model_ids));
    assert(idk->model_ids);
    idk->model_ids[0] = model_id;
    CacheProvider_IdkAdd(&fx->provider, kit_id, idk);
}

/* ---- the player's body fetch -------------------------------------------- */

static void
body(
    int slots[12],
    int kit)
{
    for( int i = 0; i < 12; i++ )
        slots[i] = -1;
    slots[4] = APPEARANCE_PACK_KIT + kit;
}

static void
test_player_body_is_one_listed_task(void)
{
    struct Fixture fx;
    int a[12];
    int b[12];
    int const seqs[7] = { 808, -1, -1, -1, -1, -1, -1 };

    printf("TEST: a player body fetch is one listed task per distinct body\n");
    fixture_init(&fx);
    body(a, 7);
    body(b, 8);

    TEST_ASSERT(EntityAssets_WantPlayerBody(&fx.app, a, 0, seqs, 7) == 1, "the first block fetches");
    TEST_ASSERT(EntityAssets_WantPlayerBody(&fx.app, a, 0, seqs, 7) == 0,
        "a second block naming the same body adds nothing");
    TEST_ASSERT(EntityAssets_PlayerBodyPending(&fx.app) == 1 && queued(&fx) == 1, "one task");
    TEST_ASSERT(EntityAssets_WantPlayerBody(&fx.app, a, 1, seqs, 7) == 1, "another gender is another body");
    TEST_ASSERT(EntityAssets_WantPlayerBody(&fx.app, b, 0, seqs, 7) == 1, "another kit is another body");
    TEST_ASSERT(EntityAssets_PlayerBodyPending(&fx.app) == 3, "three bodies, three tasks");
    TEST_ASSERT(!RS_PendingKey_Equal(
                    EntityAssets_PlayerBodyKey(a, 0, seqs, 7), EntityAssets_PlayerBodyKey(b, 0, seqs, 7)),
        "two bodies are two keys");

    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_CONFIG, 7, ASSET_CONFIG_IDK) == 1, "kit 7 loads once for two fetches");
    TEST_ASSERT(loads_of(ASSET_KIND_SEQUENCE, 808, 0) == 1, "the stance loads once for three");

    /* The kit lands, naming its model: one model load, the fetches wait on. */
    add_kit(&fx, 7, 70);
    Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_CONFIG, 7, ASSET_CONFIG_IDK), true);
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_MODEL, 70, 0) == 0, "not before the stance is here too");
    Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_SEQUENCE, 808, 0), false);
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_MODEL, 70, 0) == 1, "then the kit's model, once");
    TEST_ASSERT(EntityAssets_PlayerBodyPending(&fx.app) == 3, "every fetch still waits");
    TEST_ASSERT(EntityAssets_WantPlayerBody(&fx.app, a, 0, seqs, 7) == 0,
        "a block arriving meanwhile still finds its fetch");

    /* A logout cancels every fetch. */
    TEST_ASSERT(RS_Pending_Cancel(fx.app.pending, RS_PendingScope_All()) == 3, "cancelled");
    pump(&fx);
    TEST_ASSERT(EntityAssets_PlayerBodyPending(&fx.app) == 0 && queued(&fx) == 0, "and ended");
    fixture_free(&fx);
}

static void
test_player_body_resident_wants_nothing(void)
{
    struct Fixture fx;
    int slots[12];
    int const seqs[1] = { -1 };

    printf("TEST: a resident body wants nothing\n");
    fixture_init(&fx);
    for( int i = 0; i < 12; i++ )
        slots[i] = -1;
    TEST_ASSERT(EntityAssets_WantPlayerBody(&fx.app, slots, 0, seqs, 1) == 0, "nothing to fetch");
    TEST_ASSERT(queued(&fx) == 0, "no task");
    fixture_free(&fx);
}

/* ---- the multinpc resolve through the table ---------------------------- */

static void
test_npc_resolve_reads_through_the_table(void)
{
    struct Fixture fx;
    int out_a = -2;
    int out_b = -2;
    struct ToriRS_Task* a;
    struct ToriRS_Task* b;

    printf("TEST: two resolves of one npc type read its config once\n");
    fixture_init(&fx);
    a = CreateTask_NpcMultiResolve(&fx.app, 300, &out_a);
    b = CreateTask_NpcMultiResolve(&fx.app, 300, &out_b);
    ToriRS_TaskQueue_Add(fx.app.frame_runner.queue, a);
    ToriRS_TaskQueue_Add(fx.app.frame_runner.queue, b);
    pump(&fx);
    TEST_ASSERT(loads_of(ASSET_KIND_CONFIG, 300, ASSET_CONFIG_NPC) == 1, "one config load for both");
    TEST_ASSERT(queued(&fx) == 2, "both wait for it");
    /* It does not come: a type with no config resolves to itself. */
    Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_CONFIG, 300, ASSET_CONFIG_NPC), false);
    pump(&fx);
    TEST_ASSERT(queued(&fx) == 0, "both ended");
    TEST_ASSERT(out_a == 300 && out_b == 300, "each resolved to the type itself");
    fixture_free(&fx);
}

/* ---- the client trigger's subject --------------------------------------- */

static void
test_script_carries_its_subject(void)
{
    struct RS_CS2Host host;
    struct CacheProvider provider;
    struct CacheProviderVTable vtable;
    struct ToriRS_TaskQueue* queue = ToriRS_TaskQueue_New();
    struct ToriRS_IOBatch* io = ToriRS_IOBatch_New();
    struct RS_ClientOpContext first;
    struct RS_ClientOpContext second;
    struct ToriRS_Task* one;
    struct ToriRS_Task* two;

    printf("TEST: each queued trigger script writes its own subject at its first step\n");
    memset(&host, 0, sizeof(host));
    memset(&provider, 0, sizeof(provider));
    memset(&vtable, 0, sizeof(vtable));
    provider.vtable = &vtable;
    CacheProvider_InitEngineCaches(&provider);
    host.provider = &provider;
    RS_ClientOpReset(&host.clientop);
    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    first.kind = RS_CLIENTOP_NPC;
    first.uid = 11;
    second.kind = RS_CLIENTOP_NPC;
    second.uid = 26;

    /* Script id 0: the script itself ends at once, after the subject. */
    one = CreateTask_CS2RunMixed(&host, 0, -1, -1, NULL, 0, 0, NULL, 0);
    two = CreateTask_CS2RunMixed(&host, 0, -1, -1, NULL, 0, 0, NULL, 0);
    Task_CS2Run_SetSubject(one, &first);
    Task_CS2Run_SetSubject(two, &second);
    ToriRS_TaskQueue_Add(queue, one);
    ToriRS_TaskQueue_Add(queue, two);
    TEST_ASSERT(host.clientop.active[RS_CLIENTOP_NPC].uid == -1, "queueing writes nothing");
    (void)ToriRS_TaskQueue_RunTask(queue, io, one);
    TEST_ASSERT(host.clientop.active[RS_CLIENTOP_NPC].uid == 11, "the first script sees npc 11");
    (void)ToriRS_TaskQueue_RunTask(queue, io, two);
    TEST_ASSERT(host.clientop.active[RS_CLIENTOP_NPC].uid == 26, "the second sees npc 26");

    ToriRS_TaskQueue_Free(queue);
    ToriRS_IOBatch_Free(io);
    CacheProvider_FreeEngineCaches(&provider);
}

int
main(void)
{
    test_player_body_is_one_listed_task();
    test_player_body_resident_wants_nothing();
    test_npc_resolve_reads_through_the_table();
    test_script_carries_its_subject();
    if( g_failures )
    {
        printf("app_trackers: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("app_trackers: all passed\n");
    return 0;
}
