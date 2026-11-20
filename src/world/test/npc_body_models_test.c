/*
 * An npc's body customisation keeps every model the wire sent (FAULTS E12).
 *
 * The block's model count is a byte, and the reference allocates the list from
 * it (`new int[count]`). The npc row used to hold eight, guarded by an assert
 * alone: a build with NDEBUG (OPT=1) wrote a ninth model past the array into
 * the rest of the row. This test sets 0, 8, 9 and 255 models and reads every
 * one back, with the fields after the list untouched, and follows the list
 * through a replacement, the row's own list fed back to it, both despawn paths
 * and World_Free, counting live blocks on the scene group so a leak or a double
 * free shows. It checks with its own macro, never assert(), so the OPT=1 build
 * (asserts compiled out) runs the same checks as OPT=0.
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

/* A model id per position that no other position shares; position 3 is -1,
 * the older form's "no model", which must survive as sent. */
static int
model_at(int i)
{
    return i == 3 ? -1 : 20000 + i * 37;
}

static int
spawn(
    struct World* world,
    int element_id)
{
    return World_NpcSpawn(world, element_id, 500, 0, 10, 10, 1, World_TestDefaultIdle());
}

static void
check_models(
    struct WorldEntity_NPC const* npc,
    int count,
    char const* what)
{
    int wrong = 0;

    TEST_ASSERT(npc->body_model_count == count, what);
    if( count == 0 )
    {
        TEST_ASSERT(npc->body_models == NULL, "no models is no list");
        return;
    }
    TEST_ASSERT(npc->body_models != NULL, "a list for a non-zero count");
    if( !npc->body_models )
        return;
    for( int i = 0; i < count; i++ )
        if( npc->body_models[i] != model_at(i) )
            wrong++;
    TEST_ASSERT(wrong == 0, "every model reads back as sent");
}

static void
test_counts_read_back(void)
{
    static int const counts[] = { 0, 8, 9, 255 };
    int models[255];
    struct World* world = World_New();

    printf("TEST: 0, 8, 9 and 255 body models read back whole\n");
    for( int i = 0; i < 255; i++ )
        models[i] = model_at(i);

    for( size_t c = 0; c < sizeof(counts) / sizeof(counts[0]); c++ )
    {
        int idx = spawn(world, 100 + (int)c);
        struct WorldEntity_NPC* npc = World_EntityPoolAt(&world->entities.npc, idx);
        uint32_t serial = npc->spawn_serial;
        int body_serial = npc->body_serial;
        char what[64];

        npc->seq_sent_id = 4321;
        npc->face_sent_x = 77;
        World_NpcSetBodyModels(world, idx, models, counts[c]);
        npc = World_EntityPoolAt(&world->entities.npc, idx);
        snprintf(what, sizeof(what), "%d models: the count reads back", counts[c]);
        check_models(npc, counts[c], what);
        TEST_ASSERT(npc->body_serial == body_serial + 1, "the body is asked to remount once");
        /* What sat after the old eight-wide array in the row. */
        TEST_ASSERT(npc->body_mounted_serial == 0, "the mounted serial is untouched");
        TEST_ASSERT(npc->seq_sent_id == 4321, "the sequence stamp is untouched");
        TEST_ASSERT(npc->face_sent_x == 77, "the facing stamp is untouched");
        TEST_ASSERT(npc->spawn_serial == serial, "the spawn serial is untouched");
        TEST_ASSERT(npc->server_slot == -1, "the server slot is untouched");
    }
    World_Free(world);
}

static void
test_replace_and_reuse(void)
{
    int models[255];
    intptr_t before;
    struct World* world;
    struct WorldEntity_NPC* npc;
    int idx;

    printf("TEST: a later block replaces the list, and the row's own list can be fed back\n");
    for( int i = 0; i < 255; i++ )
        models[i] = model_at(i);
    world = World_New();
    idx = spawn(world, 7);
    before = scene_live_blocks();

    World_NpcSetBodyModels(world, idx, models, 255);
    TEST_ASSERT(scene_live_blocks() == before + 1, "a list is one block");
    World_NpcSetBodyModels(world, idx, models, 9);
    TEST_ASSERT(scene_live_blocks() == before + 1, "the old list is freed by the new");
    npc = World_EntityPoolAt(&world->entities.npc, idx);
    check_models(npc, 9, "9 models after 255");

    /* The row's own list as the source: copied before the old one goes. */
    World_NpcSetBodyModels(world, idx, npc->body_models, npc->body_model_count);
    npc = World_EntityPoolAt(&world->entities.npc, idx);
    check_models(npc, 9, "its own 9 models fed back");
    TEST_ASSERT(scene_live_blocks() == before + 1, "still one list");

    World_NpcSetBodyModels(world, idx, NULL, 0);
    npc = World_EntityPoolAt(&world->entities.npc, idx);
    check_models(npc, 0, "a reset is no models");
    TEST_ASSERT(scene_live_blocks() == before, "a reset frees the list");
    World_Free(world);
}

static void
test_despawn_and_free(void)
{
    int models[255];
    intptr_t before;
    struct World* world;
    struct World_Event const* ev = NULL;
    int a;
    int b;
    int c;

    printf("TEST: the list goes with the removal's snapshot, and World_Free frees the rest\n");
    for( int i = 0; i < 255; i++ )
        models[i] = model_at(i);
    before = scene_live_blocks();
    world = World_New();
    a = spawn(world, 11);
    b = spawn(world, 12);
    c = spawn(world, 13);
    World_NpcSetBodyModels(world, a, models, 255);
    World_NpcSetBodyModels(world, b, models, 9);
    World_NpcSetBodyModels(world, c, models, 8);

    /* A despawn with an element: the snapshot carries the models. */
    World_NpcDespawn(world, a);
    for( int i = 0; i < World_EventsCount(world); i++ )
        if( World_EventsPeek(world, i)->removed_npc )
            ev = World_EventsPeek(world, i);
    TEST_ASSERT(ev != NULL, "the despawn queued a snapshot");
    if( ev )
        check_models(ev->removed_npc, 255, "the snapshot carries all 255 models");
    /* The slot is reused at once; the new npc must not see the old list. */
    {
        int again = spawn(world, 14);
        struct WorldEntity_NPC* npc = World_EntityPoolAt(&world->entities.npc, again);
        check_models(npc, 0, "a reused slot starts with no models");
    }
    World_EventsClear(world);

    /* A despawn with no element queues no snapshot: the row frees its own. */
    {
        struct WorldEntity_NPC* npc = World_EntityPoolAt(&world->entities.npc, b);
        npc->element_id = -1;
        World_NpcDespawn(world, b);
        TEST_ASSERT(World_EventsCount(world) == 0, "no snapshot without an element");
    }

    /* c still holds its 8, and the world goes with it. */
    World_Free(world);
    TEST_ASSERT(scene_live_blocks() == before, "nothing is left on the scene group");
}

int
main(void)
{
    test_counts_read_back();
    test_replace_and_reuse();
    test_despawn_and_free();
    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("ALL PASS\n");
    return 0;
}
