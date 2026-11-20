/*
 * An actor's attached graphics are kept per wire slot, every one of the 256
 * slot bytes its own, as the rev-239 actor keeps them (class105.field1512, a
 * table keyed by the slot: method3677 replaces the slot's graphic, or removes
 * it for -1 / 65535).
 *
 * The rows held eight and clamped slots 8..255 into slot 7, where they shared
 * one graphic: a graphic in slot 12 replaced the one in slot 9, and clearing
 * slot 12 cleared slot 9's. This test sets graphics in slots 0, 7, 8, 9, 12
 * and 255 on an npc and on a player and reads each back, replaces and clears
 * single slots, checks a clear of a slot never sent stores nothing, and
 * follows the lists through both despawns and World_Free, counting live
 * blocks on the scene group so a leak or a double free shows. Its own check
 * macro, never assert(), so OPT=1 runs the same checks.
 */
#include "world.h"
#include "test_harness.h"

#include "mem/torirs_mem.h"

#include <stdio.h>
#include <string.h>

int g_failures;

static intptr_t
scene_live_blocks(void)
{
    return __atomic_load_n(&ToriMem_Scene()->live_blocks, __ATOMIC_ACQUIRE);
}

static int
spot_id(
    struct WorldEntityFacet_EntitySpotanims const* spots,
    int slot)
{
    struct WorldEntityFacet_EntitySpotanim const* spot = World_EntitySpotanimInSlot(spots, slot);

    return spot ? spot->id : -2; /* -2: no entry for the slot */
}

static int
sorted(struct WorldEntityFacet_EntitySpotanims const* spots)
{
    for( int i = 1; i < spots->count; i++ )
        if( spots->items[i - 1].slot >= spots->items[i].slot )
            return 0;
    return 1;
}

static void
test_every_slot_is_its_own(void)
{
    static int const slots[] = { 12, 0, 255, 8, 7, 9 };
    intptr_t const start = scene_live_blocks();
    struct World* world = World_New();
    intptr_t before;
    int npc;
    int player;

    printf("TEST: graphics in slots 0, 7, 8, 9, 12 and 255 are kept apart\n");
    npc = World_NpcSpawn(world, 1, 500, 0, 10, 10, 1, World_TestDefaultIdle());
    player = World_PlayerSpawn(world, 2, 0, 12, 12, World_TestDefaultIdle());
    TEST_ASSERT(npc >= 0 && player >= 0, "spawned");
    before = scene_live_blocks();
    {
        struct WorldEntity_NPC const* n = World_EntityPoolGet(&world->entities.npc, npc);
        struct WorldEntity_Player const* p = World_EntityPoolGet(&world->entities.player, player);

        TEST_ASSERT(n->spotanims.count == 0 && !n->spotanims.items, "a new npc wears nothing");
        TEST_ASSERT(p->spotanims.count == 0 && !p->spotanims.items, "a new player wears nothing");
        for( size_t i = 0; i < sizeof(slots) / sizeof(slots[0]); i++ )
        {
            World_NpcSetSpotanim(world, npc, slots[i], 1000 + slots[i], 0, 0, false);
            World_PlayerSetSpotanim(world, player, slots[i], 2000 + slots[i], 0, 0, false);
        }
        n = World_EntityPoolGet(&world->entities.npc, npc);
        p = World_EntityPoolGet(&world->entities.player, player);
        for( size_t i = 0; i < sizeof(slots) / sizeof(slots[0]); i++ )
        {
            char what[96];

            snprintf(what, sizeof(what), "the npc's slot %d holds its own graphic", slots[i]);
            TEST_ASSERT(spot_id(&n->spotanims, slots[i]) == 1000 + slots[i], what);
            snprintf(what, sizeof(what), "the player's slot %d holds its own graphic", slots[i]);
            TEST_ASSERT(spot_id(&p->spotanims, slots[i]) == 2000 + slots[i], what);
        }
        printf("   npc: %d entries, player: %d (6 slots sent to each)\n", n->spotanims.count,
            p->spotanims.count);
        TEST_ASSERT(n->spotanims.count == 6 && p->spotanims.count == 6, "one entry per slot sent");
        TEST_ASSERT(sorted(&n->spotanims) && sorted(&p->spotanims), "kept in slot order");

        /* A graphic replaces only its own slot's; a clear clears only its. */
        World_NpcSetSpotanim(world, npc, 12, 3012, 0, 0, false);
        World_NpcSetSpotanim(world, npc, 9, -1, 0, 0, false);
        n = World_EntityPoolGet(&world->entities.npc, npc);
        TEST_ASSERT(spot_id(&n->spotanims, 12) == 3012, "slot 12 replaced");
        TEST_ASSERT(spot_id(&n->spotanims, 9) == -1, "slot 9 cleared");
        TEST_ASSERT(spot_id(&n->spotanims, 8) == 1008, "slot 8 untouched by either");
        TEST_ASSERT(spot_id(&n->spotanims, 255) == 1255, "slot 255 untouched by either");
        TEST_ASSERT(spot_id(&n->spotanims, 7) == 1007, "slot 7 untouched by either");

        /* Clearing a slot never sent stores nothing. */
        World_NpcSetSpotanim(world, npc, 100, -1, 0, 0, false);
        n = World_EntityPoolGet(&world->entities.npc, npc);
        TEST_ASSERT(spot_id(&n->spotanims, 100) == -2 && n->spotanims.count == 6,
            "a clear of a slot never sent adds no entry");
    }
    TEST_ASSERT(scene_live_blocks() > before, "the lists live on the scene group");
    World_NpcDespawn(world, npc);
    World_PlayerDespawn(world, player);
    World_EventsClear(world);
    TEST_ASSERT(scene_live_blocks() == before, "both despawns free their lists");

    npc = World_NpcSpawn(world, 3, 500, 0, 10, 10, 1, World_TestDefaultIdle());
    World_NpcSetSpotanim(world, npc, 200, 77, 0, 0, false);
    World_Free(world);
    TEST_ASSERT(scene_live_blocks() == start, "World_Free frees a live row's list, and the rest");
}

int
main(void)
{
    ToriMem_Init();
    test_every_slot_is_its_own();
    if( g_failures )
    {
        printf("spotanim_slots_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("spotanim_slots_test: OK\n");
    return 0;
}
