/*
 * NPC_INFO / PLAYER_INFO count-shrinkage despawn (Client-TS getNpcPosOldVis /
 * getPlayerPosOldVis). When the wire 8-bit count is less than the previous
 * tracked length, trailing entities must leave the world pool — plane-change
 * sends count=0 and without this they keep painting on minusedlevel while pick
 * rejects them (visible, Walk-here only).
 */
#include "app.h"
#include "asyncio.h"
#include "game/rs_entity_sync.h"
#include "game/task_exec_entity_info.h"
#include "test_harness.h"
#include "world.h"

#include <stdio.h>
#include <string.h>

int g_failures;

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

int
main(void)
{
    test_npc_info_count_zero_despawns_all();
    test_npc_info_count_shrink_keeps_prefix();
    test_player_info_count_zero_despawns_others();
    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("ALL PASS\n");
    return 0;
}
