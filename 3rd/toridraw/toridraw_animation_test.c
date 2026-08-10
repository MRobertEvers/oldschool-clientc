#include "toridraw_scene.h"
#include "toridraw_animation.h"

#include <stdio.h>

static int failures;

#define CHECK(cond, msg) \
    do \
    { \
        if( !(cond) ) \
        { \
            fprintf(stderr, "FAIL: %s\n", msg); \
            failures++; \
        } \
    } while( 0 )

int
main(void)
{
    struct ToriDraw_Animation anim = { .frame_count = 90, .frame_step = 1 };
    struct ToriDraw_AnimFrame timed_frames[] = {
        { .delay = 2 },
        { .delay = 3 },
        { .delay = 1 },
    };
    struct ToriDraw_Scene* scene;
    struct ToriDraw_SceneElement* element;
    int frame;

    frame = 12;
    CHECK(ToriDraw_AnimationAdvanceObjectFrame(&anim, &frame), "interior frame advances");
    CHECK(frame == 13, "interior frame value");

    frame = 89;
    CHECK(ToriDraw_AnimationAdvanceObjectFrame(&anim, &frame), "one-frame step stays valid");
    CHECK(frame == 89, "one-frame step holds terminal frame");

    anim.frame_step = 3;
    frame = 89;
    CHECK(ToriDraw_AnimationAdvanceObjectFrame(&anim, &frame), "multi-frame step loops tail");
    CHECK(frame == 87, "multi-frame step value");

    anim.frame_step = -1;
    frame = 89;
    CHECK(!ToriDraw_AnimationAdvanceObjectFrame(&anim, &frame), "no frame step ends object sequence");

    /* rev239 DynamicObject timing: a length-2 frame is visible for both
     * cycle 1 and cycle 2, and advances only when the accumulator reaches 3. */
    anim.frames = timed_frames;
    anim.frame_count = 3;
    anim.frame_step = 1;
    frame = 0;
    {
        int cycle = 0;
        CHECK(ToriDraw_AnimationAdvanceObjectCycles(&anim, &frame, &cycle, 1),
              "first held-frame cycle remains live");
        CHECK(frame == 0 && cycle == 1, "length-2 frame holds cycle one");
        CHECK(ToriDraw_AnimationAdvanceObjectCycles(&anim, &frame, &cycle, 1),
              "second held-frame cycle remains live");
        CHECK(frame == 0 && cycle == 2, "length-2 frame holds cycle two");
        CHECK(ToriDraw_AnimationAdvanceObjectCycles(&anim, &frame, &cycle, 1),
              "crossing held-frame boundary remains live");
        CHECK(frame == 1 && cycle == 1, "strict boundary preserves one-cycle remainder");

        CHECK(ToriDraw_AnimationAdvanceObjectCycles(&anim, &frame, &cycle, 2),
              "length-3 frame accepts catch-up cycles");
        CHECK(frame == 1 && cycle == 3, "length-3 frame holds through its declared length");
        CHECK(ToriDraw_AnimationAdvanceObjectCycles(&anim, &frame, &cycle, 1),
              "length-3 frame advances after its declared length");
        CHECK(frame == 2 && cycle == 1, "variable-length boundary preserves remainder");
    }

    scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_HIGH_8K);
    CHECK(scene != NULL, "scene allocation");
    if( scene )
    {
        int element_id = ToriDraw_SceneElementAdd(scene);
        element = ToriDraw_SceneElementGet(scene, element_id);
        CHECK(element != NULL, "scene element allocation");
        if( element )
        {
            ToriDraw_SceneElementSetAnimationSeq(scene, element_id, 1);
            element->is_skeletal = true;
            element->skeletal_animation = (struct ToriDraw_SkeletalAnim*)1;
            element->skeletal_play_frames = 3;
            ToriDraw_SceneElementSetAnimation(scene, element_id, NULL, true);
            CHECK(element->anim_seq_id == -1, "finished sequence clears its id");
            CHECK(!element->is_skeletal, "finished sequence clears skeletal mode");
            CHECK(element->skeletal_animation == NULL, "finished sequence clears skeletal pose");
        }
        ToriDraw_SceneFree(scene);
    }

    if( failures )
        return 1;
    puts("toridraw animation object-step tests: PASS");
    return 0;
}
