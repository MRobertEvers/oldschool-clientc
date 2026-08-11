#include "toridraw_scene.h"
#include "toridraw_animation.h"
#include "toridraw_model.h"
#include "toridraw_model_transform.h"

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

static void
check_classic_animation_refreshes_bounds(void)
{
    vertexint_t vertices_x[1] = { 0 };
    vertexint_t vertices_y[1] = { 0 };
    vertexint_t vertices_z[1] = { 0 };
    boneint_t bone_vertices[1] = { 0 };
    boneint_t* bones[1] = { bone_vertices };
    boneint_t bone_sizes[1] = { 1 };
    struct ToriDraw_Bones vertex_bones = {
        .bones_count = 1,
        .bones = bones,
        .bones_sizes = bone_sizes,
    };
    uint8_t transform_types[1] = { 1 };
    uint8_t transform_bones[1] = { 0 };
    uint8_t* bone_groups[1] = { transform_bones };
    uint16_t bone_group_lengths[1] = { 1 };
    struct ToriDraw_AnimBase base = {
        .length = 1,
        .types = transform_types,
        .bone_groups = bone_groups,
        .bone_group_lengths = bone_group_lengths,
    };
    int16_t frame_groups[1] = { 0 };
    int16_t frame_x[1] = { 2000 };
    int16_t frame_y[1] = { 0 };
    int16_t frame_z[1] = { 0 };
    struct ToriDraw_AnimFrame frame = {
        .length = 1,
        .groups = frame_groups,
        .x = frame_x,
        .y = frame_y,
        .z = frame_z,
    };
    struct ToriDraw_BoundsCylinder bounds = { 0 };
    struct ToriDraw_Model model = {
        .vertex_count = 1,
        .vertices_x = vertices_x,
        .vertices_y = vertices_y,
        .vertices_z = vertices_z,
        .vertex_bones = &vertex_bones,
        .bounds_cylinder = &bounds,
    };

    ToriDraw_ModelSetBoundsCylinder(&model);
    CHECK(bounds.radius == 0, "bind-pose radius seeded");
    CHECK(bounds.min_z_depth_any_rotation == 1, "bind-pose depth bias seeded");

    ToriDraw_ModelAnimateFrame(&model, &base, &frame);
    CHECK(vertices_x[0] == 2000, "classic frame translated test vertex");
    CHECK(bounds.radius == 2000, "classic frame refreshed horizontal radius");
    CHECK(bounds.min_z_depth_any_rotation == 2001, "classic frame refreshed depth bias");
}

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

    check_classic_animation_refreshes_bounds();

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
