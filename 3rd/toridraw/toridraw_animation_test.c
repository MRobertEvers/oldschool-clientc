#include "toridraw_scene.h"
#include "toridraw_animation.h"
#include "toridraw_model.h"
#include "toridraw_model_transform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/*
 * A projectile's sequence loops; it does not terminate.
 *
 * Reference ClientProj.move / MapSpotAnim.update wrap animFrame to 0 at the end
 * of the frame list without consulting SeqType.frameStep. The DynamicObject
 * advance beside it does the opposite, and sharing it for projectiles is a real
 * visual bug: flight time routinely outlasts the sequence (the steel titan's
 * shot is 4 frames x 3 cycles = 12 against a 30+ cycle flight), so the element
 * loses its animation mid-air and snaps back to the model's un-posed bind pose.
 * Spotanim models routinely hide geometry by scaling it to zero every frame, so
 * that shows up as extra parts of the model appearing partway to the target.
 */
static void
check_projectile_sequence_loops(void)
{
    struct ToriDraw_Animation anim = { .frame_count = 4, .frame_step = -1 };
    struct ToriDraw_AnimFrame frames[4] = {
        { .delay = 3 }, { .delay = 3 }, { .delay = 3 }, { .delay = 3 },
    };
    int hist_frame[400];
    int hist_cycle[400];
    int occupancy[4] = { 0, 0, 0, 0 };
    int frame;
    int cycle;

    anim.frames = frames;

    /* Baseline: the DynamicObject advance gives up once frame_step is invalid,
     * and does so after exactly one pass of the frame list. */
    frame = 0;
    cycle = 0;
    {
        int died = -1;
        for( int c = 0; c < 40; c++ )
            if( !ToriDraw_AnimationAdvanceObjectCycles(&anim, &frame, &cycle, 1) )
            {
                died = c;
                break;
            }
        CHECK(died == 12, "object advance terminates at the end of a frame_step -1 seq");
    }

    /* The loop advance keeps cycling well past that. Assert the exact period
     * rather than mere liveness — holding on the terminal frame would also
     * never terminate, and that is the wrong behaviour too. */
    frame = 0;
    cycle = 0;
    for( int c = 0; c < 400; c++ )
    {
        ToriDraw_AnimationAdvanceLoopCycles(&anim, &frame, &cycle, 1);
        CHECK(frame >= 0 && frame < anim.frame_count, "looped frame stays in range");
        hist_frame[c] = frame;
        hist_cycle[c] = cycle;
        if( frame >= 0 && frame < anim.frame_count )
            occupancy[frame]++;
    }
    for( int c = 0; c + 12 < 400; c++ )
        CHECK(
            hist_frame[c] == hist_frame[c + 12] && hist_cycle[c] == hist_cycle[c + 12],
            "loop advance repeats with the sequence's 12-cycle period");
    /* Each frame holds for 3 of every 12 cycles, so ~100 of 400. */
    for( int i = 0; i < 4; i++ )
        CHECK(occupancy[i] >= 90 && occupancy[i] <= 110, "each frame gets its share of cycles");

    /* Inside the frame list the two advances must agree exactly; they differ
     * only in what happens past the last frame. */
    {
        int loop_frame = 0, loop_cycle = 0, obj_frame = 0, obj_cycle = 0;
        for( int c = 0; c < 9; c++ )
        {
            ToriDraw_AnimationAdvanceLoopCycles(&anim, &loop_frame, &loop_cycle, 1);
            ToriDraw_AnimationAdvanceObjectCycles(&anim, &obj_frame, &obj_cycle, 1);
            CHECK(
                loop_frame == obj_frame && loop_cycle == obj_cycle,
                "loop and object advance agree inside the frame list");
        }
    }

    /* An out-of-range frame is snapped back rather than indexed past the array. */
    frame = 99;
    cycle = 0;
    ToriDraw_AnimationAdvanceLoopCycles(&anim, &frame, &cycle, 1);
    CHECK(frame >= 0 && frame < anim.frame_count, "out-of-range frame is clamped");
}

/*
 * A second registration of one sequence id must not free the animation the
 * first one installed.
 *
 * Scene elements cache the resolved `struct ToriDraw_Animation*` (element->
 * animation), and so does every render command already built this frame; the
 * registry cannot reach any of them. So freeing the registered animation to
 * install a duplicate leaves those pointers dangling, and the very next read is
 * `animation->frames[frame].length` in ToriDraw_SceneElementApplyAnimation --
 * freed memory reading <= 0 takes the hole-frame branch and resets the model to
 * its bind pose. Duplicates are reachable because the load tasks dedupe against
 * what is registered, not against what is in flight.
 *
 * Asserted through the registry's own observable behaviour (the pointer a
 * later Get returns is the pointer the element was handed) rather than by
 * dereferencing freed memory, which would be undefined either way. The
 * negative control is the old code: replacing instead of keeping makes the
 * first CHECK fail, because Get then returns the duplicate.
 */
static void
check_duplicate_animation_registration_keeps_the_first(void)
{
    struct ToriDraw_Scene* scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_HIGH_8K);
    struct ToriDraw_Animation* first;
    struct ToriDraw_Animation* duplicate;
    int element_id;
    struct ToriDraw_SceneElement* element;

    CHECK(scene != NULL, "scene allocation for duplicate-registration test");
    if( !scene )
        return;

    first = calloc(1, sizeof(*first));
    duplicate = calloc(1, sizeof(*duplicate));
    CHECK(first && duplicate, "animation allocations");
    if( !first || !duplicate )
    {
        free(first);
        free(duplicate);
        ToriDraw_SceneFree(scene);
        return;
    }
    first->frame_count = 4;
    duplicate->frame_count = 4;

    ToriDraw_SceneAnimationAdd(scene, 4242, first);
    element_id = ToriDraw_SceneElementAdd(scene);
    element = ToriDraw_SceneElementGet(scene, element_id);
    CHECK(element != NULL, "scene element allocation");
    if( element )
        element->animation = ToriDraw_SceneAnimationGet(scene, 4242);
    CHECK(element && element->animation == first, "the element holds the registered animation");

    /* The second load of the same seq lands. */
    ToriDraw_SceneAnimationAdd(scene, 4242, duplicate);

    CHECK(
        ToriDraw_SceneAnimationGet(scene, 4242) == first,
        "a duplicate registration keeps the animation elements already hold");
    CHECK(
        element && element->animation == ToriDraw_SceneAnimationGet(scene, 4242),
        "so the element's cached pointer is still the registered one");

    /* The duplicate was taken over by the registry (freed), so the scene owns
     * exactly one animation for this id and SceneFree must not double-free. */
    ToriDraw_SceneFree(scene);
}

/*
 * A copied model keeps its bind pose, and mounting one that has none captures
 * it. Both halves of the "Queen Black Dragon inflates into shards" fix.
 *
 * ToriDraw_ModelAnimateReset is gated on original_vertices_x: with no originals
 * it RETURNS instead of restoring, so the model does not animate, it
 * ACCUMULATES -- every keyframe composes with the previous frame's output, and
 * a few renders later the geometry is many times its own size. Nothing errors
 * on the way; the only symptom is the size.
 *
 * ToriDraw_ModelCopy used to drop the originals, and npc models come from
 * TorirsModelInstCache_CopyGet, so every cache hit handed the scene a model in
 * that state. It hid because ToriDraw_SceneElementSetAnimationSeq captures on
 * the way past, covering the ordinary spawn-then-animate order. The QBD's
 * artefact restore does npc_changetype + npc_anim on ONE tick and re-binds the
 * sequence she is already playing, so the model was swapped under a live
 * animation with nothing behind it to capture.
 *
 * Negative controls, both verified: drop the original_vertices copy out of
 * ToriDraw_ModelCopy and the first three CHECKs fail; drop the capture out of
 * ToriDraw_SceneElementSetModel and the last one fails.
 */
static void
check_a_copied_model_keeps_its_bind_pose(void)
{
    struct ToriDraw_Model* src = ToriDraw_ModelNew(2, 0, 0);
    struct ToriDraw_Model* copy;
    struct ToriDraw_Model* uncaptured;
    struct ToriDraw_Scene* scene;
    struct ToriDraw_ModelHandle hnd;
    int element_id;

    CHECK(src != NULL, "source model allocation");
    if( !src )
        return;
    src->vertices_x = calloc(2, sizeof(vertexint_t));
    src->vertices_y = calloc(2, sizeof(vertexint_t));
    src->vertices_z = calloc(2, sizeof(vertexint_t));
    CHECK(src->vertices_x && src->vertices_y && src->vertices_z, "source vertex arrays");
    if( !src->vertices_x || !src->vertices_y || !src->vertices_z )
    {
        ToriDraw_ModelFree(src);
        return;
    }
    src->vertices_y[0] = 20;
    src->vertices_y[1] = -20;

    ToriDraw_ModelCaptureOriginalVertices(src);
    /* Pose it, the way the renderer leaves a model between frames. */
    src->vertices_y[0] = 900;

    copy = ToriDraw_ModelCopy(src);
    CHECK(copy != NULL, "model copy allocation");
    if( copy )
    {
        CHECK(copy->original_vertices_x != NULL, "a copy carries a bind pose at all");
        CHECK(
            copy->original_vertices_y && copy->original_vertices_y[0] == 20,
            "the copy's bind pose is the source's BIND, not the pose it was copied in");
        ToriDraw_ModelAnimateReset(copy);
        CHECK(
            copy->vertices_y[0] == 20,
            "so AnimateReset restores it instead of silently doing nothing");
        ToriDraw_ModelFree(copy);
    }
    ToriDraw_ModelFree(src);

    /* Mount time is the last moment a model is guaranteed to be at bind and the
     * first at which the renderer may animate it, so a model that arrives
     * without originals must acquire them here. */
    scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_HIGH_8K);
    uncaptured = ToriDraw_ModelNew(2, 0, 0);
    CHECK(scene && uncaptured, "scene and un-captured model allocation");
    if( !scene || !uncaptured )
    {
        ToriDraw_ModelFree(uncaptured);
        if( scene )
            ToriDraw_SceneFree(scene);
        return;
    }
    uncaptured->vertices_x = calloc(2, sizeof(vertexint_t));
    uncaptured->vertices_y = calloc(2, sizeof(vertexint_t));
    uncaptured->vertices_z = calloc(2, sizeof(vertexint_t));
    if( uncaptured->vertices_y )
        uncaptured->vertices_y[0] = 77;

    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = uncaptured;
    element_id = ToriDraw_SceneElementAdd(scene);
    ToriDraw_SceneElementSetModel(scene, element_id, hnd);
    CHECK(
        uncaptured->original_vertices_x && uncaptured->original_vertices_y &&
            uncaptured->original_vertices_y[0] == 77,
        "mounting a model with no bind pose captures one, so no element can hold "
        "a model whose AnimateReset is a no-op");

    ToriDraw_SceneFree(scene);
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
    check_projectile_sequence_loops();
    check_duplicate_animation_registration_keeps_the_first();
    check_a_copied_model_keeps_its_bind_pose();

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
