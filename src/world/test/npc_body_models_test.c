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

/* The models-only block the E12 cases send: 0x2 with `count` models, and a
 * count of 0 the reset (0x1), as the old models-only setter took it. */
static void
set_models(
    struct World* world,
    int idx,
    int const* models,
    int count)
{
    struct WorldNpcOverrideIn in = { .models_sent = true, .model_count = count, .models = models };

    World_NpcSetBodyCustom(world, idx, count > 0 ? &in : NULL);
}

static void
check_models(
    struct WorldEntity_NPC const* npc,
    int count,
    char const* what)
{
    int wrong = 0;

    TEST_ASSERT(npc->body_custom.model_count == count, what);
    if( count == 0 )
    {
        TEST_ASSERT(npc->body_custom.models == NULL, "no models is no list");
        return;
    }
    TEST_ASSERT(npc->body_custom.models != NULL, "a list for a non-zero count");
    if( !npc->body_custom.models )
        return;
    for( int i = 0; i < count; i++ )
        if( npc->body_custom.models[i] != model_at(i) )
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
        set_models(world, idx, models, counts[c]);
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

    set_models(world, idx, models, 255);
    TEST_ASSERT(scene_live_blocks() == before + 1, "a list is one block");
    set_models(world, idx, models, 9);
    TEST_ASSERT(scene_live_blocks() == before + 1, "the old list is freed by the new");
    npc = World_EntityPoolAt(&world->entities.npc, idx);
    check_models(npc, 9, "9 models after 255");

    /* The row's own list as the source: copied before the old one goes. */
    set_models(world, idx, npc->body_custom.models, npc->body_custom.model_count);
    npc = World_EntityPoolAt(&world->entities.npc, idx);
    check_models(npc, 9, "its own 9 models fed back");
    TEST_ASSERT(scene_live_blocks() == before + 1, "still one list");

    set_models(world, idx, NULL, 0);
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
    set_models(world, a, models, 255);
    set_models(world, b, models, 9);
    set_models(world, c, models, 8);

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

/* E12's other half: a block's recolours and retextures are kept, sized from
 * the wire, with or without models (deob Statics.method9224 takes the type's
 * models when the override sent none, and still applies its colours); they
 * replace the type's destination colours index for index; each override is
 * its own cache key; the chathead's override is the body's sibling, not part
 * of it; and every array goes with the row's snapshot and World_Free. */
static void
test_recolours_and_retextures(void)
{
    int colours[255];
    int textures[2] = { 41, 42 };
    int kits[7] = { 1, 2, 3, 4, 5, 6, 7 };
    intptr_t before;
    struct World* world;
    struct WorldEntity_NPC* npc;
    struct World_Event const* ev = NULL;
    int idx;
    int body_serial;
    int wrong = 0;
    int64_t key_a;
    int64_t key_b;

    printf("TEST: an override's recolours and retextures are kept, with or without models\n");
    for( int i = 0; i < 255; i++ )
        colours[i] = 900 + i;
    before = scene_live_blocks();
    world = World_New();
    idx = spawn(world, 21);
    npc = World_EntityPoolAt(&world->entities.npc, idx);
    body_serial = npc->body_serial;
    TEST_ASSERT(World_NpcBodyModelKey(500, &npc->body_custom) == 500, "no override: the type's key");
    TEST_ASSERT(World_NpcOverrideRecolour(&npc->body_custom, 0, 77) == 77, "no override: the type's colour");

    /* Flag 0x4 alone: no models, 255 recolours. */
    {
        struct WorldNpcOverrideIn in = { .recolour_count = 255, .recolours = colours };
        World_NpcSetBodyCustom(world, idx, &in);
    }
    npc = World_EntityPoolAt(&world->entities.npc, idx);
    TEST_ASSERT(npc->body_custom.active, "a recolour-only block is an override");
    TEST_ASSERT(!npc->body_custom.models_sent && npc->body_custom.model_count == 0,
        "it keeps the type's models");
    TEST_ASSERT(npc->body_custom.recolour_count == 255, "every recolour the wire sent");
    for( int i = 0; npc->body_custom.recolours && i < 255; i++ )
        if( npc->body_custom.recolours[i] != colours[i] )
            wrong++;
    TEST_ASSERT(npc->body_custom.recolours != NULL && wrong == 0, "each reads back as sent");
    TEST_ASSERT(npc->body_serial == body_serial + 1, "the body is asked to remount");
    TEST_ASSERT(World_NpcOverrideRecolour(&npc->body_custom, 4, 77) == 904, "it replaces the type's colour 4");
    TEST_ASSERT(World_NpcOverrideRetexture(&npc->body_custom, 0, 66) == 66, "and no texture");
    key_a = World_NpcBodyModelKey(500, &npc->body_custom);
    TEST_ASSERT(key_a != 500, "a customised body has its own cache key");

    /* A shorter list (the counted form's own count) over a longer type:
     * past it, the type's. Retextures and a composition beside it. */
    {
        struct WorldNpcOverrideIn in = {
            .recolour_count = 2,
            .recolours = colours,
            .retexture_count = 2,
            .retextures = textures,
            .player = true,
            .body_type = 1,
            .identkit_count = 7,
            .identkits = kits,
        };
        World_NpcSetBodyCustom(world, idx, &in);
    }
    npc = World_EntityPoolAt(&world->entities.npc, idx);
    TEST_ASSERT(World_NpcOverrideRecolour(&npc->body_custom, 1, 77) == 901, "index 1 is the override's");
    TEST_ASSERT(World_NpcOverrideRecolour(&npc->body_custom, 2, 77) == 77, "index 2, past its list, the type's");
    TEST_ASSERT(World_NpcOverrideRetexture(&npc->body_custom, 1, 66) == 42, "its textures replace the type's");
    TEST_ASSERT(npc->body_custom.player && npc->body_custom.body_type == 1 &&
                    npc->body_custom.identkit_count == 7 && npc->body_custom.identkits[6] == 7,
        "the player composition is kept");
    key_b = World_NpcBodyModelKey(500, &npc->body_custom);
    TEST_ASSERT(key_b != key_a && key_b != 500, "a second override is a second key");

    /* The chathead's override is separate. */
    body_serial = npc->body_serial;
    {
        int head_models[3] = { 5, 6, 7 };
        struct WorldNpcOverrideIn in = { .models_sent = true, .model_count = 3, .models = head_models, .mirror = true };
        World_NpcSetHeadCustom(world, idx, &in);
    }
    npc = World_EntityPoolAt(&world->entities.npc, idx);
    TEST_ASSERT(npc->head_custom.active && npc->head_custom.model_count == 3 && npc->head_custom.models[2] == 7 &&
                    npc->head_custom.mirror,
        "the chathead override is kept");
    TEST_ASSERT(npc->body_serial == body_serial, "a chathead block does not remount the body");
    World_NpcSetBodyCustom(world, idx, NULL);
    npc = World_EntityPoolAt(&world->entities.npc, idx);
    TEST_ASSERT(!npc->body_custom.active && npc->body_custom.recolours == NULL, "0x1 clears the body's");
    TEST_ASSERT(npc->head_custom.active, "and leaves the chathead's");
    TEST_ASSERT(World_NpcBodyModelKey(500, &npc->body_custom) == 500, "the type's key again");

    /* Everything goes with the snapshot. */
    {
        struct WorldNpcOverrideIn in = { .recolour_count = 9, .recolours = colours, .retexture_count = 2,
                                         .retextures = textures };
        World_NpcSetBodyCustom(world, idx, &in);
    }
    World_NpcDespawn(world, idx);
    for( int i = 0; i < World_EventsCount(world); i++ )
        if( World_EventsPeek(world, i)->removed_npc )
            ev = World_EventsPeek(world, i);
    TEST_ASSERT(ev && ev->removed_npc->body_custom.recolour_count == 9 && ev->removed_npc->head_custom.active,
        "the snapshot carries both overrides");
    World_EventsClear(world);
    World_Free(world);
    TEST_ASSERT(scene_live_blocks() == before, "nothing is left on the scene group");
}

/* FREEZE (entity_freeze.h): a window stops the pose and the sequence from
 * stepping and draws the tracks as it began; its reset byte stops the live
 * sequence while the drawing keeps it; a window that is over is reused; a
 * cancel drops every window and draws the live tracks again; the windows go
 * with the row. */
static int
freeze_frames(void* userdata, int seq_id)
{
    (void)userdata;
    (void)seq_id;
    return 50;
}

static int
freeze_duration(void* userdata, int seq_id, int frame)
{
    (void)userdata;
    (void)seq_id;
    (void)frame;
    return 1;
}

static int
freeze_loops(void* userdata, int seq_id)
{
    (void)userdata;
    (void)seq_id;
    return 99;
}

static void
test_freeze_windows(void)
{
    intptr_t before = scene_live_blocks();
    struct World* world = World_TestMakeReady(104);
    struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();
    int ni = World_NpcSpawn(world, 7, 1234, 0, 20, 20, 1, idle);
    struct WorldEntity_NPC* npc = World_EntityPoolGet(&world->entities.npc, ni);
    struct WorldEntityFacet_Animation const* drawn;
    int primary_frame;
    int secondary_frame;

    printf("TEST: a freeze window holds the tracks and draws them as it began\n");
    world->seq_source.frame_count = freeze_frames;
    world->seq_source.frame_duration = freeze_duration;
    world->seq_source.max_loops = freeze_loops;
    npc->animation.secondary.anim_id = (uint16_t)idle.readyanim;
    World_NpcSetPrimaryAnimation(world, ni, 500, 0);
    World_Cycle(world, 3);
    TEST_ASSERT(npc->animation.primary.anim_id == 500 && npc->animation.primary.frame > 0, "the sequence plays");
    TEST_ASSERT(World_EntityDrawnAnimation(&npc->animation, &npc->freeze) == &npc->animation,
        "unfrozen, the live tracks are drawn");

    World_NpcFreeze(world, ni, 1, 4, false);
    World_Cycle(world, 1);
    TEST_ASSERT(npc->freeze.frozen, "the window covers the cycle it starts on");
    primary_frame = npc->animation.primary.frame;
    secondary_frame = npc->animation.secondary.frame;
    World_Cycle(world, 2);
    TEST_ASSERT(npc->animation.primary.frame == primary_frame, "the sequence holds");
    TEST_ASSERT(npc->animation.secondary.frame == secondary_frame, "and so does the pose");
    drawn = World_EntityDrawnAnimation(&npc->animation, &npc->freeze);
    TEST_ASSERT(drawn != &npc->animation && drawn->primary.anim_id == 500, "the snapshot is drawn");
    World_Cycle(world, 3);
    TEST_ASSERT(!npc->freeze.frozen, "past its end the freeze is over");
    TEST_ASSERT(npc->animation.primary.frame > primary_frame, "and the sequence steps again");

    /* The reset byte: the live sequence stops as the window starts, and the
     * drawing keeps the frame it froze on. The window that was over is
     * reused. */
    World_NpcFreeze(world, ni, 1, 5, true);
    TEST_ASSERT(npc->freeze.count == 1, "a window that is over is reused");
    World_Cycle(world, 1);
    TEST_ASSERT(npc->animation.primary.anim_id == (uint16_t)-1, "the reset stops the live sequence");
    drawn = World_EntityDrawnAnimation(&npc->animation, &npc->freeze);
    TEST_ASSERT(drawn->primary.anim_id == 500, "while the frozen drawing keeps it");

    /* Two open windows, then the cancel (65535): all go and its reset stops
     * a sequence set meanwhile. The frozen flag waits for the next step
     * (class105.method3627 clears only the windows and the snapshots), so
     * until then the actor is drawn with no sequence and its held pose, and
     * the step clears the flag and draws the live tracks again. */
    World_NpcFreeze(world, ni, 2, 40, false);
    TEST_ASSERT(npc->freeze.count == 2, "an open window is not reused");
    World_NpcSetPrimaryAnimation(world, ni, 501, 0);
    secondary_frame = npc->animation.secondary.frame;
    World_NpcFreeze(world, ni, -1, -1, true);
    TEST_ASSERT(npc->freeze.count == 0, "a cancel drops every window");
    TEST_ASSERT(npc->animation.primary.anim_id == (uint16_t)-1, "and its reset stops the sequence");
    TEST_ASSERT(npc->freeze.frozen, "the frozen flag is left to the next step");
    drawn = World_EntityDrawnAnimation(&npc->animation, &npc->freeze);
    TEST_ASSERT(drawn != &npc->animation && drawn->primary.anim_id == (uint16_t)-1,
        "until then no sequence is drawn, not the snapshot");
    TEST_ASSERT(drawn->secondary.frame == secondary_frame, "and the pose as it is held");
    World_Cycle(world, 1);
    TEST_ASSERT(!npc->freeze.frozen, "the next step clears the flag");
    TEST_ASSERT(World_EntityDrawnAnimation(&npc->animation, &npc->freeze) == &npc->animation,
        "and the live tracks are drawn again");

    /* A frozen actor does not turn, and after a cancel it waits for the
     * step: the reference turns first (Statics.method1, `if
     * (!field1479)`) and clears the flag last (method7852), so the cycle
     * after a cancel turns nothing and the next one does. */
    {
        uint16_t yaw;

        World_NpcFreeze(world, ni, 0, 40, false);
        World_Cycle(world, 1);
        TEST_ASSERT(npc->freeze.frozen, "frozen again");
        yaw = npc->orientation.yaw;
        npc->orientation.dst_yaw = (uint16_t)((yaw + 512) & 0x7ff);
        npc->facing.turn_speed = 32;
        World_Cycle(world, 2);
        TEST_ASSERT(npc->orientation.yaw == yaw, "a frozen actor does not turn");
        World_NpcFreeze(world, ni, -1, -1, false);
        World_Cycle(world, 1);
        TEST_ASSERT(npc->orientation.yaw == yaw, "nor in the cycle after a cancel");
        World_Cycle(world, 1);
        TEST_ASSERT(npc->orientation.yaw != yaw, "and turns in the one after that");
    }

    World_NpcFreeze(world, ni, 0, 9, false);
    World_NpcDespawn(world, ni);
    World_EventsClear(world);
    World_Free(world);
    TEST_ASSERT(scene_live_blocks() == before, "the windows go with the row");
}

int
main(void)
{
    test_counts_read_back();
    test_replace_and_reuse();
    test_despawn_and_free();
    test_recolours_and_retextures();
    test_freeze_windows();
    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("ALL PASS\n");
    return 0;
}
