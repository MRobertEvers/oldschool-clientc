/*
 * NPC_INFO / PLAYER_INFO count-shrinkage despawn (Client-TS getNpcPosOldVis /
 * getPlayerPosOldVis). When the wire 8-bit count is less than the previous
 * tracked length, trailing entities must leave the world pool — plane-change
 * sends count=0 and without this they keep painting on minusedlevel while pick
 * rejects them (visible, Walk-here only).
 */
#include "app.h"
#include "app/app_internal.h"
#include "mem/torirs_mem.h"
#include "asyncio.h"
#include "engine/asset_requests.h"
#include "engine/cache_provider.h"
#include "engine/uitree_scene_bridge.h"
#include "game/rs_entity_sync.h"
#include "game/task_exec_entity_info.h"
#include "game/task_entity_assets.h"
#include "game/rs_hitsplat.h"
#include "net/net.h"
#include "net/rev/gameproto_revisions.h"
#include "streams/info_rs.h"
#include "rsprot_buf.h"
#include "test_harness.h"
#include "toridraw_scene.h"
#include "world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_failures;

struct HeadLoadTrace
{
    int npc_ids[8];
    int npc_count;
    int model_ids[8];
    int model_count;
};

static struct HeadLoadTrace g_head_load_trace;

static struct ToriRS_Task*
trace_npc_load(
    struct CacheProvider* provider,
    int npc_id)
{
    (void)provider;
    if( g_head_load_trace.npc_count < (int)(sizeof(g_head_load_trace.npc_ids) /
                                             sizeof(g_head_load_trace.npc_ids[0])) )
        g_head_load_trace.npc_ids[g_head_load_trace.npc_count++] = npc_id;
    return NULL;
}

static struct ToriRS_Task*
trace_model_load(
    struct CacheProvider* provider,
    int model_id)
{
    (void)provider;
    if( g_head_load_trace.model_count < (int)(sizeof(g_head_load_trace.model_ids) /
                                               sizeof(g_head_load_trace.model_ids[0])) )
        g_head_load_trace.model_ids[g_head_load_trace.model_count++] = model_id;
    return NULL;
}

/*
 * A sequence loader that answers "already registered".
 *
 * That is CreateTask_*Load's documented no-op -- a NULL task means the record
 * is resident and there is nothing to fetch -- and it is the ORDINARY answer
 * for an ANIM: the first one an npc plays loads the seq, and every later ANIM
 * naming it (an npc repeating its attack, a shopkeeper's gesture) gets NULL.
 * The one thing the consumer must not do with that absence is hand it to the
 * queue, which takes a task and dereferences it.
 */
static int g_seq_load_requests;
static int g_seq_load_last_id;

static struct ToriRS_Task*
trace_sequence_load_resident(
    struct CacheProvider* provider,
    struct ToriDraw_Scene* scene,
    int seq_id)
{
    (void)provider;
    (void)scene;
    g_seq_load_requests++;
    g_seq_load_last_id = seq_id;
    return NULL;
}

static int
trace_has_id(
    int const* ids,
    int count,
    int wanted)
{
    for( int i = 0; i < count; i++ )
        if( ids[i] == wanted )
            return 1;
    return 0;
}

static void
run_task_to_done(struct ToriRS_Task* task)
{
    struct ToriRS_IOBatch* io = ToriRS_IOBatch_New();
    int st;
    io->task = task;
    do
        st = task_run(task, io);
    while( st == PT_WAITING || st == PT_YIELDED );
    /* Protothread completion is PT_ENDED (or PT_EXITED); the queue maps those
     * to TORIRS_ASYNCIO_STAT_DONE — call task_run directly here. */
    TEST_ASSERT(st == PT_ENDED || st == PT_EXITED, "entity-info task completes");
    task_free(task);
    ToriRS_IOBatch_Free(io);
}

static void
register_npc(
    struct App* app,
    int element_id,
    int npc_type,
    int server_slot,
    int tile_x,
    int tile_z)
{
    struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();
    int idx = World_NpcSpawn(app->world, element_id, npc_type, 0, tile_x, tile_z, 1, idle);
    struct WorldEntity_NPC* npc = World_EntityPoolGet(&app->world->entities.npc, idx);
    npc->server_slot = server_slot;
    RS_EntitySync_RegisterNpc(&app->esync, server_slot, element_id, idx);
    app->esync.active_npcs[app->esync.active_npc_count++] = server_slot;
}

static void
register_local_player(
    struct App* app,
    int element_id,
    int server_pid,
    int tile_x,
    int tile_z)
{
    struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();
    int idx = World_PlayerSpawn(app->world, element_id, 0, tile_x, tile_z, idle);
    struct WorldEntity_Player* player = World_EntityPoolGet(&app->world->entities.player, idx);
    player->server_pid = server_pid;
    app->esync.local_pid = server_pid;
    app->world->local_pid = server_pid;
    RS_EntitySync_RegisterPlayer(&app->esync, server_pid, element_id, idx);
}

/* The network the exec tasks read their revision's readers from: the lc254
 * table, whose streams these tests write. */
static struct ToriRS_Network*
test_net(void)
{
    static struct ToriRS_Network net;

    net.rev = GameProtoRev_RS254();
    return &net;
}

static void
register_other_player(
    struct App* app,
    int element_id,
    int server_pid,
    int tile_x,
    int tile_z)
{
    struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();
    int idx = World_PlayerSpawn(app->world, element_id, 0, tile_x, tile_z, idle);
    struct WorldEntity_Player* player = World_EntityPoolGet(&app->world->entities.player, idx);
    player->server_pid = server_pid;
    RS_EntitySync_RegisterPlayer(&app->esync, server_pid, element_id, idx);
    app->esync.active_players[app->esync.active_player_count++] = server_pid;
}

static void
test_npc_info_count_zero_despawns_all(void)
{
    printf("TEST: NPC_INFO count=0 despawns all tracked NPCs\n");

    struct App app;
    memset(&app, 0, sizeof(app));
    app.net = test_net();
    app.world = World_TestMakeReady(104);
    RS_EntitySync_Init(&app.esync);
    /* The parallel tasks an npc's removal cancels are found here. */
    app.pending = RS_Pending_New();

    register_npc(&app, 101, 500, 10, 20, 20);
    register_npc(&app, 102, 501, 11, 21, 20);
    register_npc(&app, 103, 502, 12, 22, 20);
    TEST_ASSERT(app.esync.active_npc_count == 3, "three NPCs tracked");
    TEST_ASSERT(
        World_TestPoolIterateCount(&app.world->entities.npc) == 3, "three NPCs in world pool");

    /* 8-bit count=0; new section skipped when packet is too short for a slot. */
    uint8_t packet[] = { 0x00 };
    run_task_to_done(CreateTask_ExecNpcInfo(&app, packet, (int)sizeof(packet), 0));

    TEST_ASSERT(app.esync.active_npc_count == 0, "active list empty after count=0");
    TEST_ASSERT(
        World_TestPoolIterateCount(&app.world->entities.npc) == 0,
        "world pool empty after count=0");
    TEST_ASSERT(!RS_EntitySync_FindNpc(&app.esync, 10, NULL, NULL), "slot 10 gone");
    TEST_ASSERT(!RS_EntitySync_FindNpc(&app.esync, 11, NULL, NULL), "slot 11 gone");
    TEST_ASSERT(!RS_EntitySync_FindNpc(&app.esync, 12, NULL, NULL), "slot 12 gone");

    RS_Pending_Free(app.pending);
    RS_EntitySync_Free(&app.esync);
    World_Free(app.world);
}

static void
test_npc_origin_is_root_for_shore_and_aboard_observers(void)
{
    struct App app;
    memset(&app, 0, sizeof(app));
    app.net = test_net();
    app.world = World_TestMakeReady(104);
    RS_EntitySync_Init(&app.esync);
    /* The parallel tasks an npc's removal cancels are found here. */
    app.pending = RS_Pending_New();
    register_local_player(&app, 100, 7, 4, 4);
    struct WorldEntity_Player* player = World_PlayerGetByServerPid(app.world, 7);
    RS_NpcUpdateOrigin_Set(&app.npc_update_origin, 52, 51);
    int x, z, plane;
    RS_EntityInfo_NpcOrigin(&app, &x, &z, &plane);
    TEST_ASSERT(x == 52 && z == 51 && plane == 0, "shore NPC origin follows its wire packet");
    Wevs_Init(&app.wevs);
    struct WevConfig cfg = {0};
    Wevs_Spawn(&app.wevs, 1, WORLDVIEW_ROOT, &cfg, 2, 3072*128, 3160*128, 0, 0, 0);
    Wevs_SetAboardView(&app.wevs, 1);
    player->grid_position.level = 1;
    player->view_placement.home_view = 1;
    player->pathing.route_x[0] = 4;
    player->pathing.route_z[0] = 6;
    RS_EntityInfo_NpcOrigin(&app, &x, &z, &plane);
    TEST_ASSERT(x == 52 && z == 51 && plane == 0,
                "aboard route4,6/deckplane1 cannot move root NPC origin52,51/plane0");
    Wevs_SetAboardView(&app.wevs, 0);
    RS_NpcUpdateOrigin_Forget(&app.npc_update_origin);
    player->grid_position.level = 2;
    RS_EntityInfo_NpcOrigin(&app, &x, &z, &plane);
    TEST_ASSERT(x == 4 && z == 6 && plane == 2, "older protocols retain route-head origin fallback");
    RS_Pending_Free(app.pending);
    RS_EntitySync_Free(&app.esync);
    World_Free(app.world);
}

/*
 * Extended-info masks are addressed by POSITION in the list both sides rebuild
 * this tick, so the rebuilt list must have exactly one entry per npc the
 * decoder counted -- including npcs this side cannot resolve.
 *
 * The decoder advances its list index for every kept entry unconditionally. The
 * consumer used to append only when the lookup succeeded, so a wire count
 * longer than the tracked list (entry 1 below resolves to nothing) left the
 * list one short, and every extended-info block after that point applied to the
 * npc AFTER its intended target -- an animation playing on the wrong creature,
 * a TRANSFORMATION retyping the wrong one.
 *
 * Negative control: dropping the unresolvable entry instead of recording it as
 * -1 makes the count assertion below fail at 1.
 */
static void
test_npc_info_unresolvable_entry_keeps_list_positions(void)
{
    printf("TEST: NPC_INFO keeps list positions when an entry cannot be resolved\n");

    struct App app;
    memset(&app, 0, sizeof(app));
    app.net = test_net();
    app.world = World_TestMakeReady(104);
    RS_EntitySync_Init(&app.esync);
    /* The parallel tasks an npc's removal cancels are found here. */
    app.pending = RS_Pending_New();

    /* One tracked npc, but the wire says it is tracking two. */
    register_npc(&app, 301, 500, 20, 30, 30);
    TEST_ASSERT(app.esync.active_npc_count == 1, "one NPC tracked before the packet");

    /* count=2 (0x02), then info=0 for both entries (two clear bits). */
    uint8_t packet[] = { 0x02, 0x00 };
    run_task_to_done(CreateTask_ExecNpcInfo(&app, packet, (int)sizeof(packet), 0));

    TEST_ASSERT(
        app.esync.active_npc_count == 2,
        "the rebuilt list has one entry per npc the decoder counted");
    TEST_ASSERT(app.esync.active_npcs[0] == 20, "position 0 still resolves to slot 20");
    TEST_ASSERT(
        app.esync.active_npcs[1] == -1,
        "position 1 is an explicit no-target, not a shift of position 0");

    RS_Pending_Free(app.pending);
    RS_EntitySync_Free(&app.esync);
    World_Free(app.world);
}

static void
test_npc_info_count_shrink_keeps_prefix(void)
{
    printf("TEST: NPC_INFO count=1 keeps first, despawns trailing\n");

    struct App app;
    memset(&app, 0, sizeof(app));
    app.net = test_net();
    app.world = World_TestMakeReady(104);
    RS_EntitySync_Init(&app.esync);
    /* The parallel tasks an npc's removal cancels are found here. */
    app.pending = RS_Pending_New();

    register_npc(&app, 201, 500, 20, 30, 30);
    register_npc(&app, 202, 501, 21, 31, 30);
    register_npc(&app, 203, 502, 22, 32, 30);

    /* count=1 (0x01), then info=0 for that entry (next bit clear). */
    uint8_t packet[] = { 0x01, 0x00 };
    run_task_to_done(CreateTask_ExecNpcInfo(&app, packet, (int)sizeof(packet), 0));

    TEST_ASSERT(app.esync.active_npc_count == 1, "one NPC kept");
    TEST_ASSERT(app.esync.active_npcs[0] == 20, "kept slot is the first");
    TEST_ASSERT(RS_EntitySync_FindNpc(&app.esync, 20, NULL, NULL), "slot 20 still registered");
    TEST_ASSERT(!RS_EntitySync_FindNpc(&app.esync, 21, NULL, NULL), "slot 21 despawned");
    TEST_ASSERT(!RS_EntitySync_FindNpc(&app.esync, 22, NULL, NULL), "slot 22 despawned");
    TEST_ASSERT(
        World_TestPoolIterateCount(&app.world->entities.npc) == 1, "one NPC left in world pool");

    RS_Pending_Free(app.pending);
    RS_EntitySync_Free(&app.esync);
    World_Free(app.world);
}

/*
 * Same positional invariant as the npc list, on the player stream.
 *
 * The classic decoder advances its tracked-list index for every entry it reads,
 * so a wire count longer than what this side tracks (entry 1 below resolves to
 * nothing) must still occupy a position. Dropping it shifted every later
 * extended-info block -- appearance, chat, hitsplats, animation -- onto the
 * player after its intended target.
 *
 * Negative control: skipping the unresolvable entry makes the count assertion
 * fail at 1.
 */
static void
test_player_info_unresolvable_entry_keeps_list_positions(void)
{
    printf("TEST: PLAYER_INFO keeps list positions when an entry cannot be resolved\n");

    struct App app;
    memset(&app, 0, sizeof(app));
    app.net = test_net();
    app.world = World_TestMakeReady(104);
    RS_EntitySync_Init(&app.esync);
    /* The parallel tasks an npc's removal cancels are found here. */
    app.pending = RS_Pending_New();

    register_local_player(&app, 400, 7, 39, 40);
    register_other_player(&app, 401, 8, 40, 40);
    TEST_ASSERT(app.esync.active_player_count == 1, "one other player tracked");

    /* Local info=0 (1 bit), count=2 (8 bits), then info=0 for both entries. */
    uint8_t packet[] = { 0x01, 0x00 };
    run_task_to_done(CreateTask_ExecPlayerInfo(&app, packet, (int)sizeof(packet)));

    TEST_ASSERT(
        app.esync.active_player_count == 2,
        "the rebuilt list has one entry per player the decoder counted");
    TEST_ASSERT(app.esync.active_players[0] == 8, "position 0 still resolves to pid 8");
    TEST_ASSERT(
        app.esync.active_players[1] == -1,
        "position 1 is an explicit no-target, not a shift of position 0");

    RS_Pending_Free(app.pending);
    RS_EntitySync_Free(&app.esync);
    World_Free(app.world);
}

static void
test_player_info_count_zero_despawns_others(void)
{
    printf("TEST: PLAYER_INFO count=0 despawns tracked other players\n");

    struct App app;
    memset(&app, 0, sizeof(app));
    app.net = test_net();
    app.world = World_TestMakeReady(104);
    RS_EntitySync_Init(&app.esync);
    /* The parallel tasks an npc's removal cancels are found here. */
    app.pending = RS_Pending_New();

    /* Local must already exist so SET_LOCAL does not try to spawn (needs a
     * provider). It is not in active_players — count-shrink only trims that. */
    register_local_player(&app, 300, 7, 39, 40);
    register_other_player(&app, 301, 8, 40, 40);
    register_other_player(&app, 302, 9, 41, 40);
    TEST_ASSERT(app.esync.active_player_count == 2, "two other players tracked");

    /* Local info=0 (1 bit) + count=0 (8 bits) → two zero bytes. */
    uint8_t packet[] = { 0x00, 0x00 };
    run_task_to_done(CreateTask_ExecPlayerInfo(&app, packet, (int)sizeof(packet)));

    TEST_ASSERT(app.esync.active_player_count == 0, "active player list empty");
    TEST_ASSERT(RS_EntitySync_FindPlayer(&app.esync, 7, NULL, NULL), "local player kept");
    TEST_ASSERT(!RS_EntitySync_FindPlayer(&app.esync, 8, NULL, NULL), "pid 8 gone");
    TEST_ASSERT(!RS_EntitySync_FindPlayer(&app.esync, 9, NULL, NULL), "pid 9 gone");
    TEST_ASSERT(
        World_TestPoolIterateCount(&app.world->entities.player) == 1,
        "only local player left in pool");

    RS_Pending_Free(app.pending);
    RS_EntitySync_Free(&app.esync);
    World_Free(app.world);
}

/*
 * An ANIM naming a sequence the scene already holds must put NOTHING on the
 * frame queue.
 *
 * The loader answers a resident record with NULL -- that is its contract, and
 * the second and every later ANIM for one seq gets it. Passing that NULL to
 * ToriRS_TaskQueue_Add is a null write: the queue asserts on a debug build and
 * dereferences it on the shipping one, so the client died the moment an npc
 * repeated an animation. Equipping a weapon and attacking with it was the
 * everyday way in -- the npc fights back, and its second swing is the second
 * ANIM.
 *
 * Negative control: handing the queue what the loader returned, unchecked,
 * crashes this test rather than failing it.
 */
static void
test_npc_info_resident_sequence_queues_nothing(void)
{
    printf("TEST: NPC_INFO ANIM for an already-resident sequence queues nothing\n");

    struct App app;
    struct CacheProvider provider;
    struct CacheProviderVTable vtable;
    struct WorldEntity_NPC* npc;

    memset(&app, 0, sizeof(app));
    app.net = test_net();
    memset(&provider, 0, sizeof(provider));
    memset(&vtable, 0, sizeof(vtable));
    vtable.Task_SequenceLoad = trace_sequence_load_resident;
    provider.vtable = &vtable;
    /* The multiNpc resolve walks the npctype cache for every entry carrying
     * extended info; an uninitialised provider has no cache to walk. */
    CacheProvider_InitEngineCaches(&provider);
    g_seq_load_requests = 0;
    g_seq_load_last_id = -1;

    app.world = World_TestMakeReady(104);
    RS_EntitySync_Init(&app.esync);
    /* The parallel tasks an npc's removal cancels are found here. */
    app.pending = RS_Pending_New();
    app.provider = &provider;
    app.frame_runner.queue = ToriRS_TaskQueue_New();
    /* The sequence is wanted through the assets table, which asks the scene
     * whether it is registered before it asks the loader. */
    app.scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    App_AssetsInit(&app);

    register_npc(&app, 601, 500, 30, 30, 30);

    /*
     * count=1 (8 bits), entry 0 info=1 (1 bit) move_op=0 (2 bits), then the
     * 14-bit all-ones terminator that closes the new-npc section -- 25 bits,
     * so the byte-aligned extended block starts at byte 4: mask=ANIM, seq 60
     * (g2), delay 0 (g1).
     */
    uint8_t packet[] = { 0x01, 0x9f, 0xff, 0x80, RS_NPC_MASK_ANIM, 0x00, 0x3c, 0x00 };
    run_task_to_done(CreateTask_ExecNpcInfo(&app, packet, (int)sizeof(packet), 0));

    TEST_ASSERT(g_seq_load_requests == 1, "the ANIM asked the provider for its sequence");
    TEST_ASSERT(g_seq_load_last_id == 60, "it asked for the sequence the wire named");
    TEST_ASSERT(
        app.frame_runner.queue->head == NULL,
        "a loader that answered \"already resident\" put nothing on the queue");

    npc = World_EntityPoolGet(&app.world->entities.npc, 0);
    TEST_ASSERT(npc != NULL, "the npc survived the packet");
    TEST_ASSERT(
        npc && npc->animation.primary.anim_id == 60,
        "the animation is on the npc whether or not anything had to be loaded");

    ToriRS_TaskQueue_Free(app.frame_runner.queue);
    App_AssetsFree(&app);
    ToriDraw_SceneFree(app.scene);
    RS_Pending_Free(app.pending);
    RS_EntitySync_Free(&app.esync);
    World_Free(app.world);
    CacheProvider_FreeEngineCaches(&provider);
}

/* One shared server wrapper must resolve against each client's own varps. This
 * is the multiplayer invariant quest NPCs rely on: advancing Alice's quest may
 * reveal/change the NPC for Alice without changing what Bob sees. */
static void
test_multinpc_resolution_is_per_player(void)
{
    printf("TEST: multiNpc resolution is per player\n");

    struct CacheProvider provider;
    struct App alice;
    struct App bob;
    struct VarPType varp_type = { 0 };
    struct VarBitType varbit_type = { .basevar = 0, .startbit = 14, .endbit = 14 };
    struct ToriRS_Npctype* shell = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*shell));
    struct ToriRS_Npctype* first = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*first));
    struct ToriRS_Npctype* fallback = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*fallback));
    struct ToriRS_Npctype* varbit_shell = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*varbit_shell));
    struct ToriRS_Npctype* varbit_zero = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*varbit_zero));
    struct ToriRS_Npctype* varbit_one = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*varbit_one));

    memset(&provider, 0, sizeof(provider));
    memset(&alice, 0, sizeof(alice));
    memset(&bob, 0, sizeof(bob));
    CacheProvider_InitEngineCaches(&provider);
    VarPManager_Init(&alice.varps);
    VarPManager_Init(&bob.varps);
    TEST_ASSERT(VarPManager_SetVarpTypes(&alice.varps, &varp_type, 1), "Alice varps initialized");
    TEST_ASSERT(VarPManager_SetVarpTypes(&bob.varps, &varp_type, 1), "Bob varps initialized");
    TEST_ASSERT(
        VarPManager_SetVarbitTypes(&alice.varps, &varbit_type, 1),
        "Alice varbits initialized");
    TEST_ASSERT(
        VarPManager_SetVarbitTypes(&bob.varps, &varbit_type, 1),
        "Bob varbits initialized");
    alice.provider = &provider;
    bob.provider = &provider;

    shell->transform_varbit = -1;
    shell->transform_varp = 0;
    shell->transform_count = 3;
    shell->transforms = ToriMem_Alloc(ToriMem_Assets(), 3 * sizeof(*shell->transforms));
    shell->transforms[0] = -1;  /* quest stage 0: absent */
    shell->transforms[1] = 101; /* quest stage 1: first form */
    shell->transforms[2] = 102; /* out-of-range fallback */
    CacheProvider_NpctypeAdd(&provider, 100, shell);
    CacheProvider_NpctypeAdd(&provider, 101, first);
    CacheProvider_NpctypeAdd(&provider, 102, fallback);

    /* Aggie's real shape: a one-bit varbit packed at bit 14 selects her
     * one-op or two-op child independently for each player. This covers the
     * varbit half of the same invariant instead of proving only varp shells. */
    varbit_shell->transform_varbit = 0;
    varbit_shell->transform_varp = -1;
    varbit_shell->transform_count = 2;
    varbit_shell->transforms = ToriMem_Alloc(ToriMem_Assets(), 2 * sizeof(*varbit_shell->transforms));
    varbit_shell->transforms[0] = 111;
    varbit_shell->transforms[1] = 112;
    CacheProvider_NpctypeAdd(&provider, 110, varbit_shell);
    CacheProvider_NpctypeAdd(&provider, 111, varbit_zero);
    CacheProvider_NpctypeAdd(&provider, 112, varbit_one);

    alice.varps.var[0] = 1;
    bob.varps.var[0] = 9;
    TEST_ASSERT(
        App_NpctypeResolveMultiId(&alice, 100) == 101,
        "Alice sees the form selected by her quest stage");
    TEST_ASSERT(
        App_NpctypeResolveMultiId(&bob, 100) == 102,
        "Bob independently sees his fallback form");

    alice.varps.var[0] = 0;
    TEST_ASSERT(
        App_NpctypeResolveMultiId(&alice, 100) == -1,
        "Alice's positional -1 hides only her NPC");
    TEST_ASSERT(
        App_NpctypeResolveMultiId(&bob, 100) == 102,
        "Alice's quest change does not alter Bob's NPC");

    alice.varps.var[0] = 0;
    bob.varps.var[0] = 1 << 14;
    TEST_ASSERT(
        App_NpctypeResolveMultiId(&alice, 110) == 111,
        "Alice sees Aggie's default varbit child");
    TEST_ASSERT(
        App_NpctypeResolveMultiId(&bob, 110) == 112,
        "Bob independently sees Aggie's set-bit child");

    VarPManager_Free(&alice.varps);
    VarPManager_Free(&bob.varps);
    CacheProvider_FreeEngineCaches(&provider);
}

/* IF_SETNPCHEAD names the actor's server type. For a multiNpc actor that is
 * the shell, which intentionally has no heads of its own; the selected child
 * owns the chathead model. This test stops at the provider request seam — a
 * missing fake model makes composition return -1, but the requested id proves
 * the async loader walked through the shell instead of accepting empty heads. */
static void
test_multinpc_interface_head_loads_selected_child(void)
{
    printf("TEST: multiNpc interface head loads selected child\n");

    struct App app;
    struct CacheProvider provider;
    struct CacheProviderVTable vtable;
    struct VarPType varp_type = { 0 };
    struct ToriRS_Npctype* shell = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*shell));
    struct ToriRS_Npctype* child = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*child));
    struct ToriRS_IOBatch* io;

    memset(&app, 0, sizeof(app));
    app.net = test_net();
    memset(&provider, 0, sizeof(provider));
    memset(&vtable, 0, sizeof(vtable));
    memset(&g_head_load_trace, 0, sizeof(g_head_load_trace));
    vtable.Task_NpcLoad = trace_npc_load;
    vtable.Task_ModelLoad = trace_model_load;
    provider.vtable = &vtable;
    CacheProvider_InitEngineCaches(&provider);

    VarPManager_Init(&app.varps);
    TEST_ASSERT(
        VarPManager_SetVarpTypes(&app.varps, &varp_type, 1),
        "chathead fixture varps initialized");
    app.provider = &provider;
    /* The parallel queue: the head's task (Task_IfModel) runs there, listed
     * in the pending index. */
    app.frame_runner.queue = ToriRS_TaskQueue_New();
    App_AssetsInit(&app);
    App_PendingInit(&app);
    io = ToriRS_IOBatch_New();
    app.scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    TEST_ASSERT(app.scene != NULL, "chathead fixture scene initialized");
    UITreeSceneBridge_Init(&app.bridge, app.scene, &provider);

    shell->transform_varbit = -1;
    shell->transform_varp = 0;
    shell->transform_count = 2;
    shell->transforms = ToriMem_Alloc(ToriMem_Assets(), 2 * sizeof(*shell->transforms));
    shell->transforms[0] = 101;
    shell->transforms[1] = 101;
    child->transform_varbit = -1;
    child->transform_varp = -1;
    child->heads_count = 1;
    child->heads = ToriMem_Alloc(ToriMem_Assets(), sizeof(*child->heads));
    child->heads[0] = 777;
    CacheProvider_NpctypeAdd(&provider, 100, shell);
    CacheProvider_NpctypeAdd(&provider, 101, child);

    App_SetInterfaceNpcHead(&app, 0x12340056, 100);
    TEST_ASSERT(
        ToriRS_TaskQueue_Run(app.frame_runner.queue, io) == TORIRS_ASYNCIO_STAT_DONE,
        "multiNpc chathead load task completes");
    /* Both configs are resident, and the resolve reads each rung through
     * the assets table, which loads nothing that is here: the provider is
     * not asked for either. The walk through the shell is proven by the head
     * model below, which only the selected child names. */
    TEST_ASSERT(
        !trace_has_id(g_head_load_trace.npc_ids, g_head_load_trace.npc_count, 100) &&
            !trace_has_id(g_head_load_trace.npc_ids, g_head_load_trace.npc_count, 101),
        "chathead resolve reads the resident shell and child configs without a load");
    TEST_ASSERT(
        g_head_load_trace.model_count == 1 && g_head_load_trace.model_ids[0] == 777,
        "chathead loader requests the selected child's head model");

    UIIfModels_Free(&app.if_models);
    UITreeSceneBridge_Free(&app.bridge);
    ToriDraw_SceneFree(app.scene);
    ToriRS_IOBatch_Free(io);
    ToriRS_TaskQueue_Free(app.frame_runner.queue);
    App_PendingFree(&app);
    VarPManager_Free(&app.varps);
    CacheProvider_FreeEngineCaches(&provider);
}

/* ------------------------------------------------------------------------
 * Entities exist at once; bodies wait on the parallel queue (packet model,
 * step "entities"; FAULTS E1-E11).
 * ------------------------------------------------------------------------ */

struct EntityFixture
{
    struct App app;
    struct CacheProvider provider;
    struct CacheProviderVTable vtable;
    struct VarPType varp_type;
};

static struct ToriRS_Npctype*
fixture_npctype(
    struct EntityFixture* fx,
    int id,
    int model_id)
{
    struct ToriRS_Npctype* type = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*type));

    type->size = 1;
    type->readyanim = -1;
    type->walkanim = -1;
    type->walkanim_b = -1;
    type->walkanim_l = -1;
    type->walkanim_r = -1;
    type->turnanim_l = -1;
    type->turnanim_r = -1;
    type->runanim = -1;
    type->runanim_b = -1;
    type->runanim_l = -1;
    type->runanim_r = -1;
    type->crawlanim = -1;
    type->crawlanim_b = -1;
    type->crawlanim_l = -1;
    type->crawlanim_r = -1;
    type->transform_varbit = -1;
    type->transform_varp = -1;
    snprintf(type->name, sizeof(type->name), "type%d", id);
    if( model_id >= 0 )
    {
        type->models_count = 1;
        type->models = ToriMem_Alloc(ToriMem_Assets(), sizeof(*type->models));
        type->models[0] = model_id;
    }
    CacheProvider_NpctypeAdd(&fx->provider, id, type);
    return type;
}

static void
fixture_init(struct EntityFixture* fx)
{
    memset(fx, 0, sizeof(*fx));
    memset(&g_head_load_trace, 0, sizeof(g_head_load_trace));
    fx->app.net = test_net();
    fx->vtable.Task_NpcLoad = trace_npc_load;
    fx->vtable.Task_ModelLoad = trace_model_load;
    fx->vtable.Task_SequenceLoad = trace_sequence_load_resident;
    fx->provider.vtable = &fx->vtable;
    CacheProvider_InitEngineCaches(&fx->provider);
    VarPManager_Init(&fx->app.varps);
    TEST_ASSERT(VarPManager_SetVarpTypes(&fx->app.varps, &fx->varp_type, 1), "fixture varps");
    /* The App's script host reads the same varps (RS_CS2Host_Init in
     * app.c): a varp change syncs the audio options through it. */
    fx->app.host.varps = &fx->app.varps;
    fx->app.provider = &fx->provider;
    fx->app.world = World_TestMakeReady(104);
    RS_EntitySync_Init(&fx->app.esync);
    fx->app.frame_runner.queue = ToriRS_TaskQueue_New();
    fx->app.scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    App_AssetsInit(&fx->app);
    App_PendingInit(&fx->app);
}

static void
fixture_free(struct EntityFixture* fx)
{
    /* The queue first: every parallel task unlists itself as it is freed. */
    ToriRS_TaskQueue_Free(fx->app.frame_runner.queue);
    App_PendingFree(&fx->app);
    App_AssetsFree(&fx->app);
    ToriDraw_SceneFree(fx->app.scene);
    RS_EntitySync_Free(&fx->app.esync);
    World_Free(fx->app.world);
    VarPManager_Free(&fx->app.varps);
    CacheProvider_FreeEngineCaches(&fx->provider);
}

static void
run_npc_ops(
    struct App* app,
    RsprotInfoOp const* ops,
    int count)
{
    run_task_to_done(CreateTask_ExecNpcInfoOps(app, ops, count));
}

static RsprotInfoOp
op_value(
    int kind,
    uint64_t value)
{
    RsprotInfoOp op;

    memset(&op, 0, sizeof(op));
    op.kind = (uint8_t)kind;
    op.value = value;
    return op;
}

/* How many drive events of `kind` are in the ring; the newest one's a/b. */
static int g_drive_last_c;
static int g_drive_last_d;

static int
drive_count(
    struct App const* app,
    int kind,
    int* out_a,
    int* out_b)
{
    int n = 0;

    for( int i = 0; i < app->drive_events.ring.count && i < APP_DRIVE_RING_CAPACITY; i++ )
    {
        struct RS_Event const* ev = &app->drive_events.entries[i];
        if( ev->kind != kind )
            continue;
        n++;
        if( out_a )
            *out_a = ev->a;
        if( out_b )
            *out_b = ev->b;
        g_drive_last_c = ev->c;
        g_drive_last_d = ev->d;
    }
    return n;
}

static struct WorldEntity_NPC*
npc_in_slot(
    struct App* app,
    int slot)
{
    int world_idx = -1;

    if( !RS_EntitySync_FindNpc(&app->esync, slot, &world_idx, NULL) || world_idx < 0 )
        return NULL;
    return World_EntityPoolGet(&app->world->entities.npc, world_idx);
}

/*
 * (a) An npc whose body is not resident exists at once, draws nothing, and
 * one parallel task waits for its body; it is announced once its add record
 * has placed it (E1, E2), with its server slot (E10). Removing it cancels the
 * task.
 */
static void
test_npc_cold_body_exists_and_waits_on_the_parallel_queue(void)
{
    printf("TEST: an npc with a cold body exists at once and its body waits on the parallel queue\n");

    struct EntityFixture fx;
    RsprotInfoOp ops[3];
    struct WorldEntity_NPC* npc;
    int a = -1;
    int b = -1;

    fixture_init(&fx);
    fixture_npctype(&fx, 500, 777);

    ops[0] = op_value(RSPROT_OP_ADD_NEW, 7);
    ops[1] = op_value(RSPROT_OP_NPCTYPE, 500);
    memset(&ops[2], 0, sizeof(ops[2]));
    ops[2].kind = RSPROT_OP_DELTA_XZ;
    ops[2].delta_xz.dx = 3;
    ops[2].delta_xz.dz = -2;
    ops[2].delta_xz.jump = 1;
    run_npc_ops(&fx.app, ops, 3);

    npc = npc_in_slot(&fx.app, 7);
    TEST_ASSERT(npc != NULL, "the npc exists although its body is not resident");
    TEST_ASSERT(npc && npc->npc_id == 500, "it is the type the wire named");
    TEST_ASSERT(npc && npc->spawn_serial != 0, "it carries a spawn serial");
    TEST_ASSERT(npc && App_EntityNpcBodyPending(&fx.app, npc), "a body task is pending for it");
    TEST_ASSERT(
        RS_Pending_Count(fx.app.pending, RS_PENDING_KINDS_ENTITIES_FIRST) == 1,
        "one body task, on the parallel queue");
    TEST_ASSERT(fx.app.packet_runner.queue == NULL, "nothing was put on the packet queue");
    TEST_ASSERT(drive_count(&fx.app, RS_EVENT_NPC_SPAWN, &a, &b) == 1, "one npc_spawn");
    TEST_ASSERT(a == 7, "npc_spawn names the server slot, not the pool index (E10)");
    TEST_ASSERT(b == 500, "npc_spawn names the type");
    TEST_ASSERT(
        g_drive_last_c == 52 + 3 && g_drive_last_d == 52 - 2,
        "it was announced (and its NPC_ADD script run) once the add record's DELTA_XZ "
        "placed it, not on the update origin (E1)");

    /* Gone in the next packet: its body task is cancelled with it. */
    {
        RsprotInfoOp clear = op_value(RSPROT_OP_CLEAR_OLD, 0);
        run_npc_ops(&fx.app, &clear, 1);
    }
    TEST_ASSERT(npc_in_slot(&fx.app, 7) == NULL, "the npc was removed");
    TEST_ASSERT(
        RS_Pending_Count(fx.app.pending, RS_PENDING_KINDS_ENTITIES_FIRST) == 0,
        "its body task was cancelled with it");

    fixture_free(&fx);
}

/* (a) + E7: a retype to a type whose body is cold draws nothing (not the old
 * type's body) and replaces the npc's body task; a retype to none draws
 * nothing too. */
static void
test_npc_retype_newest_body_wins(void)
{
    printf("TEST: a retype replaces the npc's pending body task\n");

    struct EntityFixture fx;
    RsprotInfoOp ops[3];
    RsprotInfoOp change[2];
    struct WorldEntity_NPC* npc;
    struct RS_ParallelTask* pending;

    fixture_init(&fx);
    fixture_npctype(&fx, 500, 777);
    fixture_npctype(&fx, 501, 778);

    ops[0] = op_value(RSPROT_OP_ADD_NEW, 9);
    ops[1] = op_value(RSPROT_OP_NPCTYPE, 500);
    memset(&ops[2], 0, sizeof(ops[2]));
    ops[2].kind = RSPROT_OP_DELTA_XZ;
    run_npc_ops(&fx.app, ops, 3);

    /* Kept from the last packet's list, then its block. */
    change[0] = op_value(RSPROT_OP_ADD_OLD, 0);
    memset(&change[1], 0, sizeof(change[1]));
    change[1].kind = RSPROT_OP_CHANGE_TYPE;
    change[1].change_type.npc_type = 501;
    run_npc_ops(&fx.app, change, 2);

    npc = npc_in_slot(&fx.app, 9);
    TEST_ASSERT(npc && npc->npc_id == 501, "the retype applied at once");
    TEST_ASSERT(
        RS_Pending_Count(fx.app.pending, RS_PENDING_KINDS_ENTITIES_FIRST) == 1,
        "one body task: the retype's replaced the spawn's");
    pending = npc ? RS_Pending_Find(fx.app.pending, RS_PENDING_KINDS_ENTITIES_FIRST, App_EntityNpcKey(npc))
                  : NULL;
    TEST_ASSERT(pending != NULL, "and it is listed under the npc's key");
    {
        struct ToriDraw_SceneElement* el =
            npc ? ToriDraw_SceneElementGet(fx.app.scene, npc->element_id) : NULL;
        struct ToriDraw_Model const* model =
            el && ToriDraw_ModelKindIsFull(el->model.kind) ? el->model.u.model.model : NULL;
        TEST_ASSERT(model && model->vertex_count == 0, "it draws nothing until the body lands (E7)");
    }

    fixture_free(&fx);
}

/* E11: an npc whose config the cache does not have still exists, so the ops
 * that follow for its slot apply instead of being dropped. */
static void
test_npc_unknown_type_still_exists(void)
{
    printf("TEST: an npc of a type the cache lacks exists and takes later ops\n");

    struct EntityFixture fx;
    RsprotInfoOp ops[4];
    struct WorldEntity_NPC* npc;

    fixture_init(&fx);

    ops[0] = op_value(RSPROT_OP_ADD_NEW, 4);
    ops[1] = op_value(RSPROT_OP_NPCTYPE, 999);
    memset(&ops[2], 0, sizeof(ops[2]));
    ops[2].kind = RSPROT_OP_DELTA_XZ;
    ops[3] = op_value(RSPROT_OP_LEVEL_CHANGE, 55);
    run_npc_ops(&fx.app, ops, 4);

    npc = npc_in_slot(&fx.app, 4);
    TEST_ASSERT(npc != NULL, "the npc exists although its config is missing");
    TEST_ASSERT(npc && npc->combat_level == 55, "a later op for its slot applied");

    fixture_free(&fx);
}

/*
 * A session that ends under a waiting NPC_INFO.
 *
 * NPC_INFO waits on the packet queue for the npc configs its records name.
 * Its records are positions in the lists the server and this client rebuild
 * in lockstep, so they mean something only against the lists they were sent
 * against. A session end (App_NetSessionReset: logout, a lost link, a world
 * hop) clears those lists (RS_EntitySync_Clear) while the packet waits; the
 * packet, resumed after, must apply nothing -- it used to spawn its npcs into
 * the cleared world, announce them (npc_spawn, the NPC_ADD script) on the
 * title screen, and leave them registered for the next session.
 */
static int g_config_resident;

static enum AssetStart
config_start_loading(
    void* data,
    struct Assets* assets,
    struct AssetKey key)
{
    (void)data;
    (void)assets;
    (void)key;
    return ASSET_START_LOADING;
}

static bool
config_resident_when_told(
    void* data,
    struct AssetKey key)
{
    (void)data;
    (void)key;
    return g_config_resident != 0;
}

static void
test_npc_info_from_an_ended_session_applies_nothing(void)
{
    printf("TEST: an NPC_INFO waiting on a config when the session ends applies nothing after\n");

    struct EntityFixture fx;
    RsprotInfoOp ops[3];
    struct ToriRS_Task* task;
    struct ToriRS_IOBatch* io = ToriRS_IOBatch_New();
    int st;
    int spawns;

    fixture_init(&fx);
    g_config_resident = 0;
    App_AssetsSetLoader(&fx.app, ASSET_KIND_CONFIG, config_start_loading, config_resident_when_told);
    ops[0] = op_value(RSPROT_OP_ADD_NEW, 9);
    ops[1] = op_value(RSPROT_OP_NPCTYPE, 903);
    memset(&ops[2], 0, sizeof(ops[2]));
    ops[2].kind = RSPROT_OP_DELTA_XZ;
    task = CreateTask_ExecNpcInfoOps(&fx.app, ops, 3);
    io->task = task;
    st = task_run(task, io);
    TEST_ASSERT(st == PT_YIELDED && task->park_req.on == TORIRS_PARK_COUNT, "it waits on the config");
    TEST_ASSERT(npc_in_slot(&fx.app, 9) == NULL, "nothing applied yet");
    spawns = drive_count(&fx.app, RS_EVENT_NPC_SPAWN, NULL, NULL);

    /* The session ends while it waits. */
    RS_EntitySync_Clear(&fx.app.esync, fx.app.world);

    /* The config lands; the packet resumes. */
    fixture_npctype(&fx, 903, -1);
    g_config_resident = 1;
    Assets_Finished(fx.app.assets, AssetKey_Make(ASSET_KIND_CONFIG, 903, ASSET_CONFIG_NPC), true);
    do
        st = task_run(task, io);
    while( st == PT_YIELDED && task->park_req.on == TORIRS_PARK_NONE );
    TEST_ASSERT(st == PT_ENDED || st == PT_EXITED, "it ends");
    TEST_ASSERT(npc_in_slot(&fx.app, 9) == NULL, "the ended session's npc is not spawned");
    TEST_ASSERT(fx.app.esync.active_npc_count == 0, "nor listed for the next session");
    TEST_ASSERT(
        World_TestPoolIterateCount(&fx.app.world->entities.npc) == 0, "nor in the world");
    TEST_ASSERT(drive_count(&fx.app, RS_EVENT_NPC_SPAWN, NULL, NULL) == spawns, "nor announced");
    task_free(task);
    ToriRS_IOBatch_Free(io);
    fixture_free(&fx);
}

/* E3: a body that lands after a BODY_CUSTOM leaves the customisation on: the
 * landing asks for the custom body to be mounted over the type's. */
static void
test_npc_body_landing_keeps_body_custom(void)
{
    printf("TEST: a landing body keeps the server's body customisation\n");

    struct EntityFixture fx;
    RsprotInfoOp ops[3];
    struct WorldEntity_NPC* npc;
    int world_idx = -1;
    int serial;

    fixture_init(&fx);
    fixture_npctype(&fx, 502, -1);
    ops[0] = op_value(RSPROT_OP_ADD_NEW, 3);
    ops[1] = op_value(RSPROT_OP_NPCTYPE, 502);
    memset(&ops[2], 0, sizeof(ops[2]));
    ops[2].kind = RSPROT_OP_DELTA_XZ;
    run_npc_ops(&fx.app, ops, 3);
    TEST_ASSERT(RS_EntitySync_FindNpc(&fx.app.esync, 3, &world_idx, NULL), "spawned");
    {
        int const custom[1] = { 4242 };
        struct WorldNpcOverrideIn in = { .models_sent = true, .model_count = 1, .models = custom };
        World_NpcSetBodyCustom(fx.app.world, world_idx, &in);
    }
    npc = World_EntityPoolGet(&fx.app.world->entities.npc, world_idx);
    serial = npc->body_serial;

    App_WorldNpcBodyLanded(&fx.app, world_idx, npc->element_id, 502, 502);
    npc = World_EntityPoolGet(&fx.app.world->entities.npc, world_idx);
    TEST_ASSERT(npc->body_serial == serial + 1, "the custom body is mounted again over the type's");

    fixture_free(&fx);
}

/* E12: a BODY_CUSTOM block's model count is a byte and the reference sizes
 * the list from it. Nine models (one past the npc row's old fixed eight) and
 * 255 (the most a byte says) go through osrs242's real NPC_INFO reader and
 * the npc exec into the world, and every one reads back. */
static int
e12_model(int i)
{
    /* Above 0xFFFF, so g4_3412's high half is exercised too. */
    return 0x12345 + i * 0x101;
}

static int
e12_npc_info_body_custom(
    uint8_t* out,
    int cap,
    int model_count)
{
    int n = 0;

    TEST_ASSERT(cap >= 4 + 4 + 2 + model_count * 4, "the packet fits its buffer");
    /* The bit section: 8 bits of count (1), the npc updated (1), stood still
     * with extended info (type 00), then no adds: the 0xFFFF terminator,
     * needed because the extended tail leaves 28 or more readable bits. */
    out[n++] = 0x01;
    out[n++] = 0x9F; /* 1 00 11111 */
    out[n++] = 0xFF;
    out[n++] = 0xE0; /* 111, then the pad to a byte */
    /* The flag word: 0x4 -> a second byte, 0x400 -> a third, 0x200000 -> a
     * fourth carrying BIT_BODY_CUSTOMISATION_V3 (0x2000000). */
    out[n++] = 0x04;
    out[n++] = 0x04;
    out[n++] = 0x20;
    out[n++] = 0x02;
    /* BodyCustomisationV3: flag 0x2 (counted models), the count (g1), each
     * model g4_3412. */
    out[n++] = 0x02;
    out[n++] = (uint8_t)model_count;
    for( int i = 0; i < model_count; i++ )
    {
        uint32_t v = (uint32_t)e12_model(i);
        out[n++] = (uint8_t)(v >> 8);
        out[n++] = (uint8_t)v;
        out[n++] = (uint8_t)(v >> 24);
        out[n++] = (uint8_t)(v >> 16);
    }
    return n;
}

static void
test_npc_body_custom_keeps_every_wire_model(void)
{
    static int const counts[] = { 9, 255 };
    static uint8_t packet[4 + 4 + 2 + 255 * 4];
    static struct ToriRS_Network net242;

    printf("TEST: a BODY_CUSTOM of 9 and of 255 models reaches the world whole\n");
    net242.rev = GameProtoRev_OSRS242();
    for( size_t c = 0; c < sizeof(counts) / sizeof(counts[0]); c++ )
    {
        struct EntityFixture fx;
        struct WorldEntity_NPC* npc;
        int len;
        int serial;
        int wrong = 0;

        fixture_init(&fx);
        fx.app.net = &net242;
        register_npc(&fx.app, 301, 500, 6, 30, 30);
        npc = npc_in_slot(&fx.app, 6);
        TEST_ASSERT(npc != NULL, "the npc is tracked before the packet");
        if( !npc )
        {
            fixture_free(&fx);
            continue;
        }
        serial = npc->body_serial;
        npc->seq_sent_id = 4321;

        len = e12_npc_info_body_custom(packet, (int)sizeof(packet), counts[c]);
        run_task_to_done(CreateTask_ExecNpcInfo(&fx.app, packet, len, 0));

        npc = npc_in_slot(&fx.app, 6);
        TEST_ASSERT(npc != NULL, "the npc survived the packet");
        if( !npc )
        {
            fixture_free(&fx);
            continue;
        }
        TEST_ASSERT(npc->body_custom.model_count == counts[c], "the count is the wire's");
        TEST_ASSERT(npc->body_custom.models != NULL, "the list is there");
        for( int i = 0; npc->body_custom.models && i < npc->body_custom.model_count; i++ )
            if( npc->body_custom.models[i] != e12_model(i) )
                wrong++;
        TEST_ASSERT(wrong == 0, "every model is the wire's, in order");
        TEST_ASSERT(npc->body_serial == serial + 1, "the body is asked to remount");
        TEST_ASSERT(npc->seq_sent_id == 4321, "the row past the list is untouched");
        TEST_ASSERT(npc->server_slot == 6, "the server slot is untouched");
        fixture_free(&fx);
    }
}

/* E12's other half, decoded: osrs242's BodyCustomisationV3 with 0x4 | 0x8
 * and no 0x2 (recolours and retextures over the type's own models) reaches
 * the world whole, where the reader's `reset` used to read "no model list" as
 * the reset and the recolours were dropped; then 0x1 alone clears it. */
static int
e12_npc_info_v3_flag(
    uint8_t* out,
    int cap,
    int flag,
    int recolours,
    int retextures)
{
    RsprotBuf b;

    rsprot_buf_wrap(&b, out, cap);
    /* The bit section and the flag word, as e12_npc_info_body_custom. */
    rsprot_p1(&b, 0x01);
    rsprot_p1(&b, 0x9F);
    rsprot_p1(&b, 0xFF);
    rsprot_p1(&b, 0xE0);
    rsprot_p1(&b, 0x04);
    rsprot_p1(&b, 0x04);
    rsprot_p1(&b, 0x20);
    rsprot_p1(&b, 0x02);
    rsprot_p1(&b, flag);
    if( flag & 0x4 )
    {
        rsprot_p1_add128(&b, recolours);
        for( int i = 0; i < recolours; i++ )
            rsprot_p2_21_add128(&b, 1000 + i);
    }
    if( flag & 0x8 )
    {
        rsprot_p1_add128(&b, retextures);
        for( int i = 0; i < retextures; i++ )
            rsprot_p2_12(&b, 2000 + i);
    }
    TEST_ASSERT(b.err == 0, "the packet fits its buffer");
    return b.wpos;
}

static void
test_npc_body_custom_recolours_without_models(void)
{
    static uint8_t packet[64 + 255 * 4];
    static struct ToriRS_Network net242;
    struct EntityFixture fx;
    struct WorldEntity_NPC* npc;
    int len;
    int wrong = 0;

    printf("TEST: a BODY_CUSTOM of recolours and retextures and no models reaches the world\n");
    net242.rev = GameProtoRev_OSRS242();
    fixture_init(&fx);
    fx.app.net = &net242;
    register_npc(&fx.app, 301, 500, 6, 30, 30);
    npc = npc_in_slot(&fx.app, 6);
    TEST_ASSERT(npc != NULL, "the npc is tracked before the packet");

    len = e12_npc_info_v3_flag(packet, (int)sizeof(packet), 0x4 | 0x8, 255, 2);
    run_task_to_done(CreateTask_ExecNpcInfo(&fx.app, packet, len, 0));
    npc = npc_in_slot(&fx.app, 6);
    TEST_ASSERT(npc && npc->body_custom.active, "the block is an override, not the reset");
    if( npc && npc->body_custom.active )
    {
        TEST_ASSERT(!npc->body_custom.models_sent, "over the type's own models");
        TEST_ASSERT(npc->body_custom.recolour_count == 255, "every recolour the wire counted");
        for( int i = 0; npc->body_custom.recolours && i < npc->body_custom.recolour_count; i++ )
            if( npc->body_custom.recolours[i] != 1000 + i )
                wrong++;
        TEST_ASSERT(npc->body_custom.recolours && wrong == 0, "each as sent, in order");
        TEST_ASSERT(npc->body_custom.retexture_count == 2 && npc->body_custom.retextures[1] == 2001,
            "and the retextures");
    }

    len = e12_npc_info_v3_flag(packet, (int)sizeof(packet), 0x1, 0, 0);
    run_task_to_done(CreateTask_ExecNpcInfo(&fx.app, packet, len, 0));
    npc = npc_in_slot(&fx.app, 6);
    TEST_ASSERT(npc && !npc->body_custom.active && npc->body_custom.recolours == NULL, "0x1 alone resets it");
    fixture_free(&fx);
}

/* E12, flags 0x10 and 0x20: a body customisation that mirrors the local
 * player is remounted when the local player's body changes, and one that is
 * a player composition draws nothing of the npc's type -- it wants the
 * composition's kit configs, and mounts nothing until they are here. */
static void
test_npc_body_custom_mirror_and_player(void)
{
    struct EntityFixture fx;
    RsprotInfoOp ops[3];
    struct WorldEntity_NPC* npc;
    int world_idx = -1;
    int serial;
    int const kits[2] = { 256 + 7, 0 };
    int const colours[5] = { 1, 2, 3, 4, 5 };

    printf("TEST: a mirror body follows the local player; a composition wants its kits\n");
    fixture_init(&fx);
    fixture_npctype(&fx, 502, -1);
    ops[0] = op_value(RSPROT_OP_ADD_NEW, 3);
    ops[1] = op_value(RSPROT_OP_NPCTYPE, 502);
    memset(&ops[2], 0, sizeof(ops[2]));
    ops[2].kind = RSPROT_OP_DELTA_XZ;
    run_npc_ops(&fx.app, ops, 3);
    TEST_ASSERT(RS_EntitySync_FindNpc(&fx.app.esync, 3, &world_idx, NULL), "spawned");

    {
        struct WorldNpcOverrideIn in = { .mirror = true };
        World_NpcSetBodyCustom(fx.app.world, world_idx, &in);
    }
    npc = World_EntityPoolGet(&fx.app.world->entities.npc, world_idx);
    serial = npc->body_serial;
    App_WorldNpcMirrorsRemount(&fx.app);
    npc = World_EntityPoolGet(&fx.app.world->entities.npc, world_idx);
    TEST_ASSERT(npc->body_serial == serial + 1, "the local player's new body remounts a mirror");

    {
        struct WorldNpcOverrideIn in = {
            .player = true, .body_type = 0, .identkit_count = 2, .identkits = kits,
            .recolour_count = 5, .recolours = colours,
        };
        World_NpcSetBodyCustom(fx.app.world, world_idx, &in);
    }
    npc = World_EntityPoolGet(&fx.app.world->entities.npc, world_idx);
    serial = npc->body_serial;
    App_WorldNpcMirrorsRemount(&fx.app);
    npc = World_EntityPoolGet(&fx.app.world->entities.npc, world_idx);
    TEST_ASSERT(npc->body_serial == serial, "a composition does not follow the local player");
    TEST_ASSERT(Assets_LoadsStarted(fx.app.assets, ASSET_KIND_CONFIG) == 0, "no config is asked for before the sync");
    app_world_sync_npc_bodies(&fx.app);
    npc = World_EntityPoolGet(&fx.app.world->entities.npc, world_idx);
    TEST_ASSERT(Assets_LoadsStarted(fx.app.assets, ASSET_KIND_CONFIG) == 1,
        "the composition's one kit config is asked of the assets table");
    TEST_ASSERT(npc->body_mounted_serial != npc->body_serial, "and nothing is mounted while it loads");
    fixture_free(&fx);
}

/* E12 FREEZE, decoded: osrs242's npc freeze block (flag 0x8: g2_21_add128
 * delay, g2_21 duration, g1 reset) opens a window in the world; 65535 is the
 * cancel. */
static int
e12_npc_info_freeze(
    uint8_t* out,
    int cap,
    int delay,
    int duration,
    int reset)
{
    RsprotBuf b;

    rsprot_buf_wrap(&b, out, cap);
    rsprot_p1(&b, 0x01);
    rsprot_p1(&b, 0x9F);
    rsprot_p1(&b, 0xFF);
    rsprot_p1(&b, 0xE0);
    rsprot_p1(&b, 0x08);
    rsprot_p2_21_add128(&b, delay);
    rsprot_p2_21(&b, duration);
    rsprot_p1(&b, reset);
    TEST_ASSERT(b.err == 0, "the packet fits its buffer");
    return b.wpos;
}

static void
test_npc_freeze_reaches_the_world(void)
{
    static uint8_t packet[32];
    static struct ToriRS_Network net242;
    struct EntityFixture fx;
    struct WorldEntity_NPC* npc;
    int len;
    int now;

    printf("TEST: an NPC_INFO freeze opens a window; 65535 cancels\n");
    net242.rev = GameProtoRev_OSRS242();
    fixture_init(&fx);
    fx.app.net = &net242;
    register_npc(&fx.app, 301, 500, 6, 30, 30);
    now = (int)fx.app.world->cycle;
    len = e12_npc_info_freeze(packet, (int)sizeof(packet), 30, 90, 1);
    run_task_to_done(CreateTask_ExecNpcInfo(&fx.app, packet, len, 0));
    npc = npc_in_slot(&fx.app, 6);
    TEST_ASSERT(npc && npc->freeze.count == 1, "one window");
    TEST_ASSERT(npc && npc->freeze.count == 1 && npc->freeze.windows[0].start_cycle == now + 30 &&
                    npc->freeze.windows[0].end_cycle == now + 120 && npc->freeze.windows[0].reset_seq,
        "from the cycle plus the delay, for the duration, with its reset");
    len = e12_npc_info_freeze(packet, (int)sizeof(packet), 65535, 65535, 0);
    run_task_to_done(CreateTask_ExecNpcInfo(&fx.app, packet, len, 0));
    npc = npc_in_slot(&fx.app, 6);
    TEST_ASSERT(npc && npc->freeze.count == 0, "65535 cancels every window");
    fixture_free(&fx);
}

/* E5: removing a player (or anything else not an npc) is not an npc despawn;
 * an npc's removal is, with its server slot (E10). */
static void
test_only_npc_removals_are_npc_despawns(void)
{
    printf("TEST: only an npc's removal is reported as an npc despawn\n");

    struct EntityFixture fx;
    struct ToriDraw_Model* model;
    int element;
    int player;
    RsprotInfoOp ops[3];
    int a = -1;

    fixture_init(&fx);
    model = ToriDraw_ModelNew(0, 0, 0);
    element = app_world_scene_element_create(&fx.app, TORIDRAW_ELEMENT_KIND_PLAYER, model, 64, 0, 64);
    player = World_PlayerSpawn(fx.app.world, element, 0, 10, 10, World_TestDefaultIdle());
    World_PlayerDespawn(fx.app.world, player);
    App_WorldDrainEntityRemovedFor(&fx.app, fx.app.world);
    TEST_ASSERT(drive_count(&fx.app, RS_EVENT_NPC_DESPAWN, NULL, NULL) == 0, "a player's removal is no npc despawn");

    fixture_npctype(&fx, 503, -1);
    ops[0] = op_value(RSPROT_OP_ADD_NEW, 12);
    ops[1] = op_value(RSPROT_OP_NPCTYPE, 503);
    memset(&ops[2], 0, sizeof(ops[2]));
    ops[2].kind = RSPROT_OP_DELTA_XZ;
    run_npc_ops(&fx.app, ops, 3);
    {
        RsprotInfoOp clear = op_value(RSPROT_OP_CLEAR_OLD, 0);
        run_npc_ops(&fx.app, &clear, 1);
    }
    App_WorldDrainEntityRemovedFor(&fx.app, fx.app.world);
    TEST_ASSERT(drive_count(&fx.app, RS_EVENT_NPC_DESPAWN, &a, NULL) == 1, "an npc's removal is one");
    TEST_ASSERT(a == 12, "naming its server slot");

    fixture_free(&fx);
}

/* (c) + E6: several varps that one npc's multinpc depends on make one retype,
 * applied at once without the packet queue, keeping the server's overrides. */
static void
test_varp_retype_coalesces_and_keeps_overrides(void)
{
    printf("TEST: a varp retype is applied at once, once, keeping server overrides\n");

    struct EntityFixture fx;
    struct ToriRS_Npctype* shell;
    RsprotInfoOp ops[4];
    struct WorldEntity_NPC* npc;

    fixture_init(&fx);
    shell = fixture_npctype(&fx, 600, -1);
    shell->transform_varp = 0;
    shell->transform_count = 3;
    shell->transforms = ToriMem_Alloc(ToriMem_Assets(), 3 * sizeof(*shell->transforms));
    shell->transforms[0] = 601;
    shell->transforms[1] = 602;
    shell->transforms[2] = 603;
    fixture_npctype(&fx, 601, -1);
    fixture_npctype(&fx, 602, -1);
    fixture_npctype(&fx, 603, -1);

    ops[0] = op_value(RSPROT_OP_ADD_NEW, 20);
    ops[1] = op_value(RSPROT_OP_NPCTYPE, 600);
    memset(&ops[2], 0, sizeof(ops[2]));
    ops[2].kind = RSPROT_OP_DELTA_XZ;
    memset(&ops[3], 0, sizeof(ops[3]));
    ops[3].kind = RSPROT_OP_NAME_CHANGE;
    ops[3].name_change.name = "Renamed";
    run_npc_ops(&fx.app, ops, 4);
    npc = npc_in_slot(&fx.app, 20);
    TEST_ASSERT(npc && npc->npc_id == 601, "spawned as the rung varp 0 selects");

    fx.app.varps.var[0] = 1;
    app_varp_change(&fx.app, 0);
    fx.app.varps.var[0] = 2;
    app_varp_change(&fx.app, 0);
    TEST_ASSERT(npc->npc_id == 601, "not applied between two var writes");
    App_EntityNpcRetypeFlush(&fx.app);

    TEST_ASSERT(npc->npc_id == 603, "applied as the rung both writes select");
    TEST_ASSERT(drive_count(&fx.app, RS_EVENT_NPC_RETYPE, NULL, NULL) == 1, "one retype for two varps");
    TEST_ASSERT(strcmp(npc->name, "Renamed") == 0, "the server's NAME_CHANGE survived the retype");
    TEST_ASSERT(fx.app.packet_runner.queue == NULL, "nothing waited on the packet queue");
    TEST_ASSERT(fx.app.world->npc_retype_wanted == 0, "nothing left wanted");

    fixture_free(&fx);
}

/* (a): PLAYER_INFO's add makes the player at once -- nothing awaited, no
 * default kit, no ready stance -- and registers it. */
static void
test_player_add_exists_at_once(void)
{
    printf("TEST: a new player exists at once and nothing is awaited\n");

    struct EntityFixture fx;
    RsprotInfoOp add = op_value(RSPROT_OP_ADD_NEW, 5);
    int world_idx = -1;

    fixture_init(&fx);
    run_task_to_done(CreateTask_ExecPlayerInfoOps(&fx.app, &add, 1));
    TEST_ASSERT(
        RS_EntitySync_FindPlayer(&fx.app.esync, 5, &world_idx, NULL) && world_idx >= 0,
        "the player exists and is registered");
    TEST_ASSERT(g_head_load_trace.model_count == 0, "no kit model was asked for");

    fixture_free(&fx);
}

/* E4: an npc's body is resident when the stances it PLAYS are -- the shell's,
 * not the drawn rung's. */
static void
test_npc_body_resident_reads_the_shells_stances(void)
{
    printf("TEST: an npc body's stances are the shell's\n");

    struct EntityFixture fx;
    struct ToriRS_Npctype* shell;
    struct ToriRS_Npctype* drawn;

    fixture_init(&fx);
    shell = fixture_npctype(&fx, 610, -1);
    drawn = fixture_npctype(&fx, 611, -1);
    shell->readyanim = 50;
    drawn->readyanim = 51;
    ToriDraw_SceneAnimationAdd(fx.app.scene, 50, calloc(1, sizeof(struct ToriDraw_Animation)));

    TEST_ASSERT(
        EntityAssets_NpcBodyResident(&fx.app, 611, 610) == 1,
        "the shell's stance is resident: so is the body");
    TEST_ASSERT(
        EntityAssets_NpcBodyResident(&fx.app, 611, -1) == 0,
        "with no shell, the drawn type's stance is the one wanted");

    fixture_free(&fx);
}

/* E8: a splat's duration comes off the type its selector resolves to when it
 * arrives (deob class105), not off the wire's selector record. */
static void
test_hitsplat_duration_is_the_resolved_types(void)
{
    printf("TEST: a hitsplat's duration is the resolved type's\n");

    struct EntityFixture fx;
    RsprotInfoOp ops[4];
    struct WorldEntity_NPC* npc;
    int* sprites = ToriMem_Calloc(ToriMem_Assets(), 2, sizeof(int));
    int* durations = ToriMem_Calloc(ToriMem_Assets(), 2, sizeof(int));
    int* policies = ToriMem_Calloc(ToriMem_Assets(), 2, sizeof(int));
    struct RS_HitsplatVariants* variants = ToriMem_Calloc(ToriMem_Assets(), 2, sizeof(*variants));

    fixture_init(&fx);
    RS_Hitsplats_Init(&fx.app.hitsplats);
    sprites[0] = sprites[1] = -1;
    durations[0] = 70;  /* the selector record's own */
    durations[1] = 123; /* the leaf it selects */
    policies[0] = policies[1] = -1;
    variants[0].varbit = -1;
    variants[0].varp = 0;
    variants[0].count = 2;
    variants[0].ids = ToriMem_Calloc(ToriMem_Assets(), 2, sizeof(int));
    variants[0].ids[0] = 1;
    variants[0].ids[1] = 1;
    variants[1].varbit = -1;
    variants[1].varp = -1;
    TEST_ASSERT(
        RS_Hitsplats_SetTypes(&fx.app.hitsplats, sprites, durations, policies, NULL, variants, 2),
        "hitsplat types installed");
    fixture_npctype(&fx, 504, -1);

    ops[0] = op_value(RSPROT_OP_ADD_NEW, 2);
    ops[1] = op_value(RSPROT_OP_NPCTYPE, 504);
    memset(&ops[2], 0, sizeof(ops[2]));
    ops[2].kind = RSPROT_OP_DELTA_XZ;
    memset(&ops[3], 0, sizeof(ops[3]));
    ops[3].kind = RSPROT_OP_DAMAGE;
    ops[3].damage.damage_type = 0;
    ops[3].damage.damage = 5;
    ops[3].damage.health = 10;
    ops[3].damage.total_health = 20;
    run_npc_ops(&fx.app, ops, 4);

    npc = npc_in_slot(&fx.app, 2);
    TEST_ASSERT(npc != NULL, "spawned");
    TEST_ASSERT(npc && npc->combat.damage_types[0] == 0, "the wire's type is kept for the draw");
    TEST_ASSERT(
        npc && npc->combat.damage_cycles[0] - npc->combat.damage_start_cycles[0] == 123,
        "the duration is the resolved leaf's");

    RS_Hitsplats_Free(&fx.app.hitsplats);
    fixture_free(&fx);
}

int
main(void)
{
    test_npc_origin_is_root_for_shore_and_aboard_observers();
    test_npc_info_count_zero_despawns_all();
    test_npc_info_count_shrink_keeps_prefix();
    test_npc_info_unresolvable_entry_keeps_list_positions();
    test_npc_info_resident_sequence_queues_nothing();
    test_player_info_count_zero_despawns_others();
    test_player_info_unresolvable_entry_keeps_list_positions();
    test_multinpc_resolution_is_per_player();
    test_multinpc_interface_head_loads_selected_child();
    test_npc_cold_body_exists_and_waits_on_the_parallel_queue();
    test_npc_retype_newest_body_wins();
    test_npc_unknown_type_still_exists();
    test_npc_info_from_an_ended_session_applies_nothing();
    test_npc_body_landing_keeps_body_custom();
    test_npc_body_custom_keeps_every_wire_model();
    test_npc_body_custom_recolours_without_models();
    test_npc_body_custom_mirror_and_player();
    test_npc_freeze_reaches_the_world();
    test_only_npc_removals_are_npc_despawns();
    test_varp_retype_coalesces_and_keeps_overrides();
    test_player_add_exists_at_once();
    test_npc_body_resident_reads_the_shells_stances();
    test_hitsplat_duration_is_the_resolved_types();
    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("ALL PASS\n");
    return 0;
}
