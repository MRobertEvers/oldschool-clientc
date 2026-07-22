/*
 * Walkmerge blend unit test (reference Model.maskAnimate): a 3-group rig
 * where group 0 is an ORIGIN, groups 1 and 2 are TRANSLATEs. The primary
 * frame moves groups {1,2}; the secondary frame moves {1}; the mask lists
 * {1}. Expected: group 2 vertices take the primary's offset, group 1
 * vertices take the SECONDARY's offset (mask wins), origins apply in both
 * passes.
 */
#include "toridraw.h"
#include "toridraw_animation.h"
#include "toridraw_model.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    ANIM_OP_ORIGIN = 0,
    ANIM_OP_TRANSLATE = 1,
};

static struct ToriDraw_Model*
make_model(void)
{
    /* Vertex 0 belongs to bone 0 (group 1's label set), vertex 1 to bone 1
     * (group 2's label set). */
    struct ToriDraw_Model* model = calloc(1, sizeof(*model));
    struct ToriDraw_Bones* bones;
    static boneint_t bone0[] = { 0 };
    static boneint_t bone1[] = { 1 };

    assert(model);
    model->vertex_count = 2;
    model->vertices_x = calloc(2, sizeof(*model->vertices_x));
    model->vertices_y = calloc(2, sizeof(*model->vertices_y));
    model->vertices_z = calloc(2, sizeof(*model->vertices_z));

    bones = calloc(1, sizeof(*bones));
    assert(bones);
    bones->bones_count = 2;
    bones->bones = calloc(2, sizeof(*bones->bones));
    bones->bones_sizes = calloc(2, sizeof(*bones->bones_sizes));
    bones->bones[0] = bone0;
    bones->bones[1] = bone1;
    bones->bones_sizes[0] = 1;
    bones->bones_sizes[1] = 1;
    model->vertex_bones = bones;

    ToriDraw_ModelCaptureOriginalVertices(model);
    return model;
}

static struct ToriDraw_AnimBase*
make_base(void)
{
    struct ToriDraw_AnimBase* base = calloc(1, sizeof(*base));
    assert(base);
    base->length = 3;
    base->types = calloc(3, sizeof(*base->types));
    base->types[0] = ANIM_OP_ORIGIN;
    base->types[1] = ANIM_OP_TRANSLATE;
    base->types[2] = ANIM_OP_TRANSLATE;
    base->bone_groups = calloc(3, sizeof(*base->bone_groups));
    base->bone_group_lengths = calloc(3, sizeof(*base->bone_group_lengths));
    /* Group 0 (origin) over bone 0; group 1 drives bone 0; group 2 drives
     * bone 1. */
    static uint8_t g0[] = { 0 };
    static uint8_t g1[] = { 0 };
    static uint8_t g2[] = { 1 };
    base->bone_groups[0] = g0;
    base->bone_groups[1] = g1;
    base->bone_groups[2] = g2;
    base->bone_group_lengths[0] = 1;
    base->bone_group_lengths[1] = 1;
    base->bone_group_lengths[2] = 1;
    return base;
}

/* One frame touching all three groups with the given translate offsets. */
static void
make_frame(
    struct ToriDraw_AnimFrame* frame,
    int dx_group1,
    int dx_group2)
{
    memset(frame, 0, sizeof(*frame));
    frame->length = 3;
    frame->groups = calloc(3, sizeof(*frame->groups));
    frame->x = calloc(3, sizeof(*frame->x));
    frame->y = calloc(3, sizeof(*frame->y));
    frame->z = calloc(3, sizeof(*frame->z));
    frame->groups[0] = 0; /* origin */
    frame->groups[1] = 1;
    frame->x[1] = (int16_t)dx_group1;
    frame->groups[2] = 2;
    frame->x[2] = (int16_t)dx_group2;
}

int
main(void)
{
    struct ToriDraw_Model* model = make_model();
    struct ToriDraw_AnimBase* base = make_base();
    struct ToriDraw_AnimFrame primary;
    struct ToriDraw_AnimFrame secondary;
    static int const walkmerge[] = { 1, 9999999 };

    make_frame(&primary, 100, 200);  /* action: both limbs at +100/+200 */
    make_frame(&secondary, 7, 0);    /* walk: group 1 at +7 */

    /* Plain primary: both groups take the primary offsets. */
    ToriDraw_ModelAnimateReset(model);
    ToriDraw_ModelAnimateFrame(model, base, &primary);
    assert(model->vertices_x[0] == 100);
    assert(model->vertices_x[1] == 200);
    printf("ok - plain primary apply\n");

    /* Masked: group 1 (in walkmerge) takes the SECONDARY offset, group 2
     * keeps the primary's. */
    ToriDraw_ModelAnimateReset(model);
    ToriDraw_ModelAnimateFrameMasked(model, base, &primary, &secondary, walkmerge);
    assert(model->vertices_x[0] == 7);
    assert(model->vertices_x[1] == 200);
    printf("ok - masked blend (secondary drives masked group)\n");

    /* NULL mask degrades to a plain primary apply. */
    ToriDraw_ModelAnimateReset(model);
    ToriDraw_ModelAnimateFrameMasked(model, base, &primary, &secondary, NULL);
    assert(model->vertices_x[0] == 100);
    assert(model->vertices_x[1] == 200);
    printf("ok - NULL mask falls back to primary\n");

    /* Seq-meta copy owns the walkmerge (sentinel-terminated). */
    {
        struct ToriDraw_Animation* anim = calloc(1, sizeof(*anim));
        struct ToriDraw_AnimSeqMeta meta = {
            .walkmerge = walkmerge,
            .priority = 6,
            .max_loops = 2,
            .preanim_move = 2,
            .postanim_move = 2,
            .duplicate_behavior = 1,
        };
        ToriDraw_AnimationSetSeqMeta(anim, &meta);
        assert(anim->walkmerge && anim->walkmerge[0] == 1 && anim->walkmerge[1] == 9999999);
        assert(anim->priority == 6 && anim->max_loops == 2);
        ToriDraw_AnimationFree(anim);
        printf("ok - seq meta copy\n");
    }

    printf("walkmerge: all tests passed\n");
    return 0;
}
