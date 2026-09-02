/*
 * ToriDraw_SceneElementApplyAnimation skips a pose it has already produced.
 *
 * The renderer asks for an animated element's pose every frame; a sequence
 * advances its frame every two to four cycles, so most requests repeat the
 * previous one and Reset + AnimateFrame + PostTransforms + SetBounds were run
 * for nothing. The skip is only correct if EVERY path that can change what a
 * (track, frame) pair produces forgets the held pose. This test drives each
 * such path and demands a re-pose after it, and demands NO re-pose when
 * nothing changed.
 *
 * "Did it re-pose" is read off the model: the frame translates the test vertex
 * to a known x, the test then scribbles a sentinel over that vertex, and a
 * re-pose restores the frame's value while a skip leaves the sentinel.
 *
 * Run twice by the makefile: with the default (skip on) and with
 * TORIDRAW_ANIM_SKIP_SAME=0, under which the "unchanged frame is skipped"
 * expectation inverts -- the control arm must re-pose every time.
 *
 * Build/run: make -C src test-scene-anim-skip
 */
#include "toridraw_scene.h"
#include <assert.h>
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
        else \
            printf("  ok   %s\n", msg); \
    } while( 0 )

#define SENTINEL 12345
#define FRAME0_X 2000
#define FRAME1_X 3000

/* One vertex on one bone; frame f translates it to FRAME{f}_X. */
static struct ToriDraw_Model*
make_model(void)
{
    struct ToriDraw_Model* model = ToriDraw_ModelNew(1, 0, 0);
    struct ToriDraw_Bones* bones = calloc(1, sizeof(*bones));
    assert(model);
    assert(bones);
    model->vertices_x = calloc(1, sizeof(vertexint_t));
    model->vertices_y = calloc(1, sizeof(vertexint_t));
    model->vertices_z = calloc(1, sizeof(vertexint_t));
    bones->bones_count = 1;
    bones->bones = calloc(1, sizeof(boneint_t*));
    bones->bones[0] = calloc(1, sizeof(boneint_t));
    bones->bones[0][0] = 0;
    bones->bones_sizes = calloc(1, sizeof(boneint_t));
    bones->bones_sizes[0] = 1;
    model->vertex_bones = bones;
    model->has_bounds_cylinder = true;
    return model;
}

static struct ToriDraw_Animation*
make_animation(void)
{
    struct ToriDraw_Animation* anim = calloc(1, sizeof(*anim));
    struct ToriDraw_AnimBase* base = calloc(1, sizeof(*base));
    assert(anim);
    assert(base);
    base->length = 1;
    base->types = calloc(1, 1);
    base->types[0] = 1; /* translate */
    base->bone_groups = calloc(1, sizeof(uint8_t*));
    base->bone_groups[0] = calloc(1, 1);
    base->bone_groups[0][0] = 0;
    base->bone_group_lengths = calloc(1, sizeof(uint16_t));
    base->bone_group_lengths[0] = 1;
    anim->base = base;
    anim->frame_count = 2;
    anim->frames = calloc(2, sizeof(struct ToriDraw_AnimFrame));
    for( int f = 0; f < 2; f++ )
    {
        struct ToriDraw_AnimFrame* frame = &anim->frames[f];
        frame->length = 1;
        frame->groups = calloc(1, sizeof(int16_t));
        frame->x = calloc(1, sizeof(int16_t));
        frame->y = calloc(1, sizeof(int16_t));
        frame->z = calloc(1, sizeof(int16_t));
        frame->x[0] = f == 0 ? FRAME0_X : FRAME1_X;
    }
    return anim;
}

static struct ToriDraw_Model*
element_model(
    struct ToriDraw_Scene* scene,
    int element_id)
{
    struct ToriDraw_SceneElement* element = ToriDraw_SceneElementGet(scene, element_id);
    assert(element);
    assert(element->model.kind == TORIDRAWMK_MODEL);
    return element->model.u.model.model;
}

/* Scribble the sentinel, ask for `frame` again, report whether it re-posed.
 * Under the walkmerge blend the test bone is driven by the SECONDARY track,
 * so `want` is that track's frame rather than the primary's. */
static int
reposed_blend(
    struct ToriDraw_Scene* scene,
    int element_id,
    int frame,
    int want)
{
    struct ToriDraw_Model* model = element_model(scene, element_id);
    model->vertices_x[0] = SENTINEL;
    ToriDraw_SceneElementApplyAnimation(scene, element_id, true, frame);
    if( model->vertices_x[0] == want )
        return 1;
    if( model->vertices_x[0] == SENTINEL )
        return 0;
    fprintf(stderr, "vertex is neither the frame's %d nor the sentinel: %d\n", want,
            model->vertices_x[0]);
    failures++;
    return -1;
}

static int
reposed(
    struct ToriDraw_Scene* scene,
    int element_id,
    int frame)
{
    return reposed_blend(scene, element_id, frame, frame == 0 ? FRAME0_X : FRAME1_X);
}

int
main(void)
{
    char const* env = getenv("TORIDRAW_ANIM_SKIP_SAME");
    int const skip_on = env && env[0] == '1'; /* the knob's own default is off */
    struct ToriDraw_Scene* scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_HIGH_8K);
    struct ToriDraw_Animation* anim = make_animation();
    struct ToriDraw_Animation* other = make_animation();
    struct ToriDraw_ModelHandle hnd;
    struct ToriDraw_SceneElement* element;
    int element_id;
    uint32_t revision;

    assert(scene);
    printf("TORIDRAW_ANIM_SKIP_SAME arm: %s\n", skip_on ? "skip (default)" : "re-pose every call");

    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = make_model();
    element_id = ToriDraw_SceneElementAdd(scene);
    ToriDraw_SceneElementSetModel(scene, element_id, hnd);
    ToriDraw_SceneElementSetAnimation(scene, element_id, anim, true);
    element = ToriDraw_SceneElementGet(scene, element_id);
    assert(element);

    CHECK(reposed(scene, element_id, 0) == 1, "the first request poses");
    CHECK(reposed(scene, element_id, 0) == !skip_on, "the same frame again is skipped (re-posed on the control arm)");
    CHECK(reposed(scene, element_id, 1) == 1, "a different frame re-poses");
    CHECK(reposed(scene, element_id, 1) == !skip_on, "and repeats of it are skipped");
    CHECK(reposed(scene, element_id, 0) == 1, "going back to the first frame re-poses");

    revision = element->model_revision;
    ToriDraw_SceneElementSetAnimation(scene, element_id, anim, true);
    CHECK(element->model_revision == revision + 1, "SetAnimation bumps the model revision");
    CHECK(reposed(scene, element_id, 0) == 1, "re-binding the same sequence re-poses");

    ToriDraw_SceneElementSetAnimation(scene, element_id, NULL, true);
    CHECK(element_model(scene, element_id)->vertices_x[0] == 0, "dropping the sequence resets the model to bind");
    ToriDraw_SceneElementSetAnimation(scene, element_id, anim, true);
    CHECK(reposed(scene, element_id, 0) == 1, "and re-binding after the drop re-poses");

    revision = element->model_revision;
    ToriDraw_SceneElementSetAnimationSeq(scene, element_id, 77);
    element->animation = anim; /* the app's direct bind, as app_element_set_anim does */
    CHECK(element->model_revision > revision, "SetAnimationSeq bumps the model revision");
    CHECK(reposed(scene, element_id, 0) == 1, "SetAnimationSeq forces a re-pose");

    revision = element->model_revision;
    ToriDraw_SceneElementSetSecondaryAnimationSeq(scene, element_id, 78);
    CHECK(element->model_revision > revision, "SetSecondaryAnimationSeq bumps the model revision");
    CHECK(reposed(scene, element_id, 0) == 1, "SetSecondaryAnimationSeq forces a re-pose");

    revision = element->model_revision;
    CHECK(ToriDraw_SceneElementModelForWrite(scene, element_id) == element_model(scene, element_id),
          "ModelForWrite on an owned model hands back the same model");
    CHECK(element->model_revision > revision, "ModelForWrite bumps the model revision");
    CHECK(reposed(scene, element_id, 0) == 1, "ModelForWrite forces a re-pose");

    revision = element->model_revision;
    ToriDraw_SceneElementPoseInvalidate(scene, element_id);
    CHECK(element->model_revision > revision, "PoseInvalidate bumps the model revision");
    CHECK(reposed(scene, element_id, 0) == 1, "PoseInvalidate forces a re-pose");

    CHECK(reposed(scene, element_id, 0) == !skip_on, "with nothing changed the frame is skipped again");
    element->animation = other; /* a direct write of the track pointer, no scene call */
    CHECK(reposed(scene, element_id, 0) == 1, "a track swapped by direct write is caught by identity");
    element->animation = anim;
    CHECK(reposed(scene, element_id, 0) == 1, "and swapped back likewise");

    /* A model swapped under the running animation. */
    {
        struct ToriDraw_ModelHandle fresh;
        memset(&fresh, 0, sizeof(fresh));
        fresh.kind = TORIDRAWMK_MODEL;
        fresh.u.model.model = make_model();
        revision = element->model_revision;
        ToriDraw_SceneElementSetModel(scene, element_id, fresh);
        CHECK(element->model_revision > revision, "SetModel bumps the model revision");
        CHECK(reposed(scene, element_id, 0) == 1, "a model mounted under the animation is posed");
        CHECK(reposed(scene, element_id, 0) == !skip_on, "and then skipped like any other");
    }

    /* The walkmerge blend: the secondary frame is part of the tuple. */
    {
        int* walkmerge = calloc(2, sizeof(int));
        walkmerge[0] = 0;
        walkmerge[1] = 9999999;
        anim->walkmerge = walkmerge;
        ToriDraw_SceneElementSetAnimation(scene, element_id, other, false);
        ToriDraw_SceneElementSetAnimFrames(scene, element_id, 0, 0);
        CHECK(reposed_blend(scene, element_id, 0, FRAME0_X) == 1, "binding a secondary track re-poses");
        CHECK(reposed_blend(scene, element_id, 0, FRAME0_X) == !skip_on, "same primary and secondary frames: skipped");
        ToriDraw_SceneElementSetAnimFrames(scene, element_id, 0, 1);
        CHECK(reposed_blend(scene, element_id, 0, FRAME1_X) == 1,
              "a secondary frame change alone re-poses (the masked bone follows the secondary)");
        CHECK(reposed_blend(scene, element_id, 0, FRAME1_X) == !skip_on, "and the blended pose is skipped when repeated");
        anim->walkmerge = NULL;
        free(walkmerge);
    }

    /* Freed elements come back with no remembered pose. */
    ToriDraw_SceneElementRemove(scene, element_id);
    {
        struct ToriDraw_ModelHandle again;
        int reused = ToriDraw_SceneElementAdd(scene);
        struct ToriDraw_SceneElement* e2 = ToriDraw_SceneElementGet(scene, reused);
        CHECK(e2 && e2->posed_primary < 0, "a fresh element holds no pose");
        memset(&again, 0, sizeof(again));
        again.kind = TORIDRAWMK_MODEL;
        again.u.model.model = make_model();
        ToriDraw_SceneElementSetModel(scene, reused, again);
        ToriDraw_SceneElementSetAnimation(scene, reused, anim, true);
        CHECK(reposed(scene, reused, 0) == 1, "and its first request poses");
    }

    ToriDraw_SceneFree(scene);
    ToriDraw_AnimationFree(anim);
    ToriDraw_AnimationFree(other);

    if( failures )
    {
        fprintf(stderr, "FAILED: toridraw_scene_anim_skip_test (%d)\n", failures);
        return 1;
    }
    printf("OK: toridraw_scene_anim_skip_test\n");
    return 0;
}
