/*
 * The seq source World reads animation timing through.
 *
 * Two things here are worth pinning. The defaults, because an unloaded seq is
 * not an error -- the loader is asynchronous -- and the defaults are what
 * decide whether a track freezes and waits or races ahead with nonsense. And
 * the total-duration sum, because it is off by one in a way that is easy to
 * get wrong and whose symptom is a spotanim that plays, freezes in its last
 * pose, and only then disappears.
 *
 * What is asserted:
 *
 *   - with no scene bound, every getter answers its default and nothing
 *     dereferences anything. This is the state between App_Init and the first
 *     world load, and it is reached on every boot.
 *   - a negative id answers the same defaults. World asks about -1 routinely:
 *     that is how "this entity has no animation" is spelled.
 *   - the defaults are the specific values the world's advance treats as
 *     "nothing to do": duration 1 cycle, no frame step, 99 loops, priority 5,
 *     duplicate behaviour -1.
 *   - total duration is sum(delay) + 1, NOT sum(delay + 1). The rev239 advance
 *     is `while (cycle > delay) cycle -= delay`, so from a zero counter the
 *     first frame needs delay+1 cycles to trip a strict `>` and leaves 1
 *     behind; every later frame then costs exactly its own delay. Summing
 *     (delay + 1) overstates the loop by frame_count - 1, and the element
 *     drops its animation and sits in the un-posed base model for that long.
 *   - a frame carrying delay <= 0 counts as one cycle, because that is what
 *     the advance does with it.
 *   - a skeletal seq carries no per-frame delays at all, and every frame of
 *     one is a single cycle.
 *
 * Build and run:
 *   make -C src test-world-seq-source
 */

#include "engine/world_seq_source_toridraw.h"

#include "toridraw_animation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;

#define CHECK(condition, ...)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if( !(condition) )                                                                         \
        {                                                                                          \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static void
test_unbound_source_answers_defaults(void)
{
    struct WorldSeqSourceToriDraw source;
    struct World_SeqSource vtable;

    /* No scene, no provider: the state every boot passes through. */
    WorldSeqSourceToriDraw_Bind(&source, NULL, NULL);
    WorldSeqSourceToriDraw_Fill(&source, &vtable);

    CHECK(vtable.userdata == &source, "the vtable does not point at its own binding");
    CHECK(
        WorldSeqSourceToriDraw_Animation(&source, 42) == NULL,
        "an unbound source produced an animation");

    for( int seq_id = -1; seq_id <= 1; seq_id++ )
    {
        CHECK(vtable.frame_count(&source, seq_id) == 0, "seq %d frame_count is not 0", seq_id);
        CHECK(
            vtable.frame_duration(&source, seq_id, 0) == 1,
            "seq %d frame_duration is not 1 cycle",
            seq_id);
        CHECK(vtable.frame_step(&source, seq_id) == 0, "seq %d frame_step is not 0", seq_id);
        CHECK(vtable.max_loops(&source, seq_id) == 99, "seq %d max_loops is not 99", seq_id);
        CHECK(vtable.priority(&source, seq_id) == 5, "seq %d priority is not 5", seq_id);
        CHECK(
            vtable.duplicate_behavior(&source, seq_id) == -1,
            "seq %d duplicate_behavior is not -1",
            seq_id);
        CHECK(vtable.preanim_move(&source, seq_id) == 0, "seq %d preanim_move is not 0", seq_id);
        CHECK(vtable.postanim_move(&source, seq_id) == 0, "seq %d postanim_move is not 0", seq_id);
        CHECK(vtable.stretches(&source, seq_id) == 0, "seq %d stretches is not 0", seq_id);
    }

    CHECK(
        WorldSeqSourceToriDraw_TotalDuration(&source, 7) == 1,
        "an unloaded seq's loop is not one cycle");
    CHECK(
        WorldSeqSourceToriDraw_TotalDuration(&source, -1) == 1,
        "a negative seq id's loop is not one cycle");
}

/* A scene with one animation in it, built by hand: the point is the timing
 * arithmetic, and a real seq load would need a cache. */
static struct ToriDraw_Scene*
scene_with_animation(
    int seq_id,
    int const* delays,
    int frame_count,
    int skeletal)
{
    struct ToriDraw_Scene* scene =
        ToriDraw_SceneNew(TORIDRAW_SCENE_SMALL, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    struct ToriDraw_Animation* anim;

    anim = calloc(1, sizeof(*anim));
    anim->frame_count = frame_count;
    anim->max_loops = 0;
    anim->priority = 5;
    anim->duplicate_behavior = -1;
    if( !skeletal )
    {
        /* One slot more than frame_count, holding a delay nothing should ever
         * read. It is what makes the out-of-range guard testable: a guard that
         * is off by one reads this instead of answering its default. */
        anim->frames = calloc((size_t)frame_count + 1, sizeof(*anim->frames));
        for( int i = 0; i < frame_count; i++ )
            anim->frames[i].delay = delays[i];
        anim->frames[frame_count].delay = 777;
    }
    ToriDraw_SceneAnimationAdd(scene, seq_id, anim);
    return scene;
}

static void
test_total_duration_is_sum_plus_one(void)
{
    struct WorldSeqSourceToriDraw source;
    int const delays[] = {3, 5, 2, 4};
    int const frame_count = (int)(sizeof(delays) / sizeof(delays[0]));
    int sum = 0;
    struct ToriDraw_Scene* scene = scene_with_animation(11, delays, frame_count, 0);

    for( int i = 0; i < frame_count; i++ )
        sum += delays[i];

    WorldSeqSourceToriDraw_Bind(&source, scene, NULL);

    CHECK(
        WorldSeqSourceToriDraw_Animation(&source, 11) != NULL,
        "the bound scene did not answer for the seq it holds");
    CHECK(
        WorldSeqSourceToriDraw_Animation(&source, 12) == NULL,
        "the bound scene answered for a seq it does not hold");

    CHECK(
        WorldSeqSourceToriDraw_TotalDuration(&source, 11) == sum + 1,
        "loop is %d cycles, want sum(delay) + 1 = %d",
        WorldSeqSourceToriDraw_TotalDuration(&source, 11),
        sum + 1);
    CHECK(
        WorldSeqSourceToriDraw_TotalDuration(&source, 11) != sum + frame_count,
        "loop equals sum(delay + 1); that overstates it by frame_count - 1 and "
        "leaves the element frozen in its base pose for the difference");

    ToriDraw_SceneFree(scene);
}

static void
test_a_nonpositive_delay_is_one_cycle(void)
{
    struct WorldSeqSourceToriDraw source;
    struct World_SeqSource vtable;
    int const delays[] = {0, -4, 6};
    struct ToriDraw_Scene* scene = scene_with_animation(3, delays, 3, 0);

    WorldSeqSourceToriDraw_Bind(&source, scene, NULL);
    WorldSeqSourceToriDraw_Fill(&source, &vtable);

    CHECK(vtable.frame_duration(&source, 3, 0) == 1, "a zero delay is not one cycle");
    CHECK(vtable.frame_duration(&source, 3, 1) == 1, "a negative delay is not one cycle");
    CHECK(vtable.frame_duration(&source, 3, 2) == 6, "a real delay is not itself");

    /* Out-of-range frame indices are ordinary: the world asks about the frame
     * it is on, and a seq can be replaced under it.
     *
     * Frame == frame_count is the boundary and the one that matters, so the
     * fixture puts a real delay in the slot just past the end. Ask only about
     * frame 99 and the guard can be relaxed by one without the test noticing,
     * while the code reads off the end of the array. */
    CHECK(vtable.frame_duration(&source, 3, -1) == 1, "frame -1 is not one cycle");
    CHECK(
        vtable.frame_duration(&source, 3, 3) == 1,
        "the frame at frame_count was sampled instead of answering the default");
    CHECK(vtable.frame_duration(&source, 3, 99) == 1, "a frame past the end is not one cycle");

    /* 1 + 1 + 6, then the loop's own +1. */
    CHECK(
        WorldSeqSourceToriDraw_TotalDuration(&source, 3) == 9,
        "loop with non-positive delays is %d cycles, want 9",
        WorldSeqSourceToriDraw_TotalDuration(&source, 3));

    ToriDraw_SceneFree(scene);
}

static void
test_a_skeletal_seq_has_no_per_frame_delays(void)
{
    /* Skeletal curves are sampled one tick per client cycle, so the animation
     * carries no frames array at all and every frame is a single cycle. */
    struct WorldSeqSourceToriDraw source;
    struct World_SeqSource vtable;
    struct ToriDraw_Scene* scene = scene_with_animation(5, NULL, 10, 1);

    WorldSeqSourceToriDraw_Bind(&source, scene, NULL);
    WorldSeqSourceToriDraw_Fill(&source, &vtable);

    CHECK(vtable.frame_count(&source, 5) == 10, "skeletal frame_count is not 10");
    CHECK(vtable.frame_duration(&source, 5, 4) == 1, "a skeletal frame is not one cycle");
    CHECK(
        WorldSeqSourceToriDraw_TotalDuration(&source, 5) == 11,
        "a 10-frame skeletal loop is %d cycles, want 11",
        WorldSeqSourceToriDraw_TotalDuration(&source, 5));

    ToriDraw_SceneFree(scene);
}

int
main(void)
{
    test_unbound_source_answers_defaults();
    test_total_duration_is_sum_plus_one();
    test_a_nonpositive_delay_is_one_cycle();
    test_a_skeletal_seq_has_no_per_frame_delays();

    if( g_failures )
    {
        printf("world_seq_source_toridraw_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("world_seq_source_toridraw_test: OK\n");
    return 0;
}
