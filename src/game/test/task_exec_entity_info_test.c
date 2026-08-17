/*
 * NPC_INFO / PLAYER_INFO count-shrinkage despawn (Client-TS getNpcPosOldVis /
 * getPlayerPosOldVis). When the wire 8-bit count is less than the previous
 * tracked length, trailing entities must leave the world pool — plane-change
 * sends count=0 and without this they keep painting on minusedlevel while pick
 * rejects them (visible, Walk-here only).
 */
#include "app.h"
#include "asyncio.h"
#include "engine/cache_provider.h"
#include "engine/uitree_scene_bridge.h"
#include "game/rs_entity_sync.h"
#include "game/task_exec_entity_info.h"
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
    struct ToriRS_IO* io = ToriRS_IO_New();
    int st;
    do
        st = task_run(task, io);
    while( st == PT_WAITING || st == PT_YIELDED );
    /* Protothread completion is PT_ENDED (or PT_EXITED); the queue maps those
     * to TORIRS_ASYNCIO_STAT_DONE — call task_run directly here. */
    TEST_ASSERT(st == PT_ENDED || st == PT_EXITED, "entity-info task completes");
    task_free(task);
    ToriRS_IO_Free(io);
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
    app.world = World_TestMakeReady(104);
    RS_EntitySync_Init(&app.esync);

    register_npc(&app, 101, 500, 10, 20, 20);
    register_npc(&app, 102, 501, 11, 21, 20);
    register_npc(&app, 103, 502, 12, 22, 20);
    TEST_ASSERT(app.esync.active_npc_count == 3, "three NPCs tracked");
    TEST_ASSERT(
        World_TestPoolIterateCount(&app.world->entities.npc) == 3, "three NPCs in world pool");

    /* 8-bit count=0; new section skipped when packet is too short for a slot. */
    uint8_t packet[] = { 0x00 };
    run_task_to_done(CreateTask_ExecNpcInfo(&app, packet, (int)sizeof(packet)));

    TEST_ASSERT(app.esync.active_npc_count == 0, "active list empty after count=0");
    TEST_ASSERT(
        World_TestPoolIterateCount(&app.world->entities.npc) == 0,
        "world pool empty after count=0");
    TEST_ASSERT(!RS_EntitySync_FindNpc(&app.esync, 10, NULL, NULL), "slot 10 gone");
    TEST_ASSERT(!RS_EntitySync_FindNpc(&app.esync, 11, NULL, NULL), "slot 11 gone");
    TEST_ASSERT(!RS_EntitySync_FindNpc(&app.esync, 12, NULL, NULL), "slot 12 gone");

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
    app.world = World_TestMakeReady(104);
    RS_EntitySync_Init(&app.esync);

    /* One tracked npc, but the wire says it is tracking two. */
    register_npc(&app, 301, 500, 20, 30, 30);
    TEST_ASSERT(app.esync.active_npc_count == 1, "one NPC tracked before the packet");

    /* count=2 (0x02), then info=0 for both entries (two clear bits). */
    uint8_t packet[] = { 0x02, 0x00 };
    run_task_to_done(CreateTask_ExecNpcInfo(&app, packet, (int)sizeof(packet)));

    TEST_ASSERT(
        app.esync.active_npc_count == 2,
        "the rebuilt list has one entry per npc the decoder counted");
    TEST_ASSERT(app.esync.active_npcs[0] == 20, "position 0 still resolves to slot 20");
    TEST_ASSERT(
        app.esync.active_npcs[1] == -1,
        "position 1 is an explicit no-target, not a shift of position 0");

    RS_EntitySync_Free(&app.esync);
    World_Free(app.world);
}

static void
test_npc_info_count_shrink_keeps_prefix(void)
{
    printf("TEST: NPC_INFO count=1 keeps first, despawns trailing\n");

    struct App app;
    memset(&app, 0, sizeof(app));
    app.world = World_TestMakeReady(104);
    RS_EntitySync_Init(&app.esync);

    register_npc(&app, 201, 500, 20, 30, 30);
    register_npc(&app, 202, 501, 21, 31, 30);
    register_npc(&app, 203, 502, 22, 32, 30);

    /* count=1 (0x01), then info=0 for that entry (next bit clear). */
    uint8_t packet[] = { 0x01, 0x00 };
    run_task_to_done(CreateTask_ExecNpcInfo(&app, packet, (int)sizeof(packet)));

    TEST_ASSERT(app.esync.active_npc_count == 1, "one NPC kept");
    TEST_ASSERT(app.esync.active_npcs[0] == 20, "kept slot is the first");
    TEST_ASSERT(RS_EntitySync_FindNpc(&app.esync, 20, NULL, NULL), "slot 20 still registered");
    TEST_ASSERT(!RS_EntitySync_FindNpc(&app.esync, 21, NULL, NULL), "slot 21 despawned");
    TEST_ASSERT(!RS_EntitySync_FindNpc(&app.esync, 22, NULL, NULL), "slot 22 despawned");
    TEST_ASSERT(
        World_TestPoolIterateCount(&app.world->entities.npc) == 1, "one NPC left in world pool");

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
    app.world = World_TestMakeReady(104);
    RS_EntitySync_Init(&app.esync);

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

    RS_EntitySync_Free(&app.esync);
    World_Free(app.world);
}

static void
test_player_info_count_zero_despawns_others(void)
{
    printf("TEST: PLAYER_INFO count=0 despawns tracked other players\n");

    struct App app;
    memset(&app, 0, sizeof(app));
    app.world = World_TestMakeReady(104);
    RS_EntitySync_Init(&app.esync);

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

    RS_EntitySync_Free(&app.esync);
    World_Free(app.world);
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
    struct ToriRS_Npctype* shell = calloc(1, sizeof(*shell));
    struct ToriRS_Npctype* first = calloc(1, sizeof(*first));
    struct ToriRS_Npctype* fallback = calloc(1, sizeof(*fallback));
    struct ToriRS_Npctype* varbit_shell = calloc(1, sizeof(*varbit_shell));
    struct ToriRS_Npctype* varbit_zero = calloc(1, sizeof(*varbit_zero));
    struct ToriRS_Npctype* varbit_one = calloc(1, sizeof(*varbit_one));

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
    shell->transforms = malloc(3 * sizeof(*shell->transforms));
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
    varbit_shell->transforms = malloc(2 * sizeof(*varbit_shell->transforms));
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
    struct ToriRS_Npctype* shell = calloc(1, sizeof(*shell));
    struct ToriRS_Npctype* child = calloc(1, sizeof(*child));
    struct ToriRS_IO* io;

    memset(&app, 0, sizeof(app));
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
    app.exec_runner.queue = ToriRS_TaskQueue_New();
    io = ToriRS_IO_New();
    app.scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    TEST_ASSERT(app.scene != NULL, "chathead fixture scene initialized");
    UITreeSceneBridge_Init(&app.bridge, app.scene, &provider);

    shell->transform_varbit = -1;
    shell->transform_varp = 0;
    shell->transform_count = 2;
    shell->transforms = malloc(2 * sizeof(*shell->transforms));
    shell->transforms[0] = 101;
    shell->transforms[1] = 101;
    child->transform_varbit = -1;
    child->transform_varp = -1;
    child->heads_count = 1;
    child->heads = malloc(sizeof(*child->heads));
    child->heads[0] = 777;
    CacheProvider_NpctypeAdd(&provider, 100, shell);
    CacheProvider_NpctypeAdd(&provider, 101, child);

    App_SetInterfaceNpcHead(&app, 0x12340056, 100);
    TEST_ASSERT(
        ToriRS_TaskQueue_Run(app.exec_runner.queue, io) == TORIRS_ASYNCIO_STAT_DONE,
        "multiNpc chathead load task completes");
    TEST_ASSERT(
        trace_has_id(
            g_head_load_trace.npc_ids, g_head_load_trace.npc_count, 100) &&
            trace_has_id(g_head_load_trace.npc_ids, g_head_load_trace.npc_count, 101),
        "chathead loader requests both the shell and selected child configs");
    TEST_ASSERT(
        g_head_load_trace.model_count == 1 && g_head_load_trace.model_ids[0] == 777,
        "chathead loader requests the selected child's head model");

    free(app.if_heads);
    UITreeSceneBridge_Free(&app.bridge);
    ToriDraw_SceneFree(app.scene);
    ToriRS_IO_Free(io);
    ToriRS_TaskQueue_Free(app.exec_runner.queue);
    VarPManager_Free(&app.varps);
    CacheProvider_FreeEngineCaches(&provider);
}

int
main(void)
{
    test_npc_info_count_zero_despawns_all();
    test_npc_info_count_shrink_keeps_prefix();
    test_npc_info_unresolvable_entry_keeps_list_positions();
    test_player_info_count_zero_despawns_others();
    test_player_info_unresolvable_entry_keeps_list_positions();
    test_multinpc_resolution_is_per_player();
    test_multinpc_interface_head_loads_selected_child();
    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("ALL PASS\n");
    return 0;
}
