#include "toridraw_animation.h"
#include "toridraw_alloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void
ToriDraw_AnimbaseFree(struct ToriDraw_AnimBase* base)
{
    if( !base )
        return;

    if( base->bone_groups )
    {
        for( int i = 0; i < base->length; i++ )
            TORIDRAW_FREE(base->bone_groups[i]);
        TORIDRAW_FREE(base->bone_groups);
    }
    TORIDRAW_FREE(base->bone_group_lengths);
    TORIDRAW_FREE(base->types);
    TORIDRAW_FREE(base);
}

void
ToriDraw_AnimationSetSeqMeta(
    struct ToriDraw_Animation* anim,
    struct ToriDraw_AnimSeqMeta const* meta)
{
    assert(anim);
    assert(meta);

    TORIDRAW_FREE(anim->walkmerge);
    anim->walkmerge = NULL;
    if( meta->walkmerge )
    {
        int count = 0;
        while( meta->walkmerge[count] != 9999999 )
            count++;
        anim->walkmerge = TORIDRAW_MALLOC(((size_t)count + 1) * sizeof(int));
        assert(anim->walkmerge);
        memcpy(anim->walkmerge, meta->walkmerge, ((size_t)count + 1) * sizeof(int));
    }
    TORIDRAW_FREE(anim->maya_masks);
    anim->maya_masks = NULL;
    if( meta->maya_masks )
    {
        anim->maya_masks = TORIDRAW_MALLOC(256 * sizeof(bool));
        assert(anim->maya_masks);
        memcpy(anim->maya_masks, meta->maya_masks, 256 * sizeof(bool));
    }
    anim->priority = meta->priority;
    anim->max_loops = meta->max_loops;
    anim->preanim_move = meta->preanim_move;
    anim->postanim_move = meta->postanim_move;
    anim->duplicate_behavior = meta->duplicate_behavior;
    anim->replaceheldleft = meta->replaceheldleft;
    anim->replaceheldright = meta->replaceheldright;
    anim->stretches = meta->stretches;
    anim->vertical_offset = meta->vertical_offset;
}

bool
ToriDraw_AnimationAdvanceObjectFrame(
    struct ToriDraw_Animation const* anim,
    int* frame)
{
    int next;

    assert(anim);
    if( anim->frame_count <= 0 )
        return false;
    assert(frame);

    next = *frame + 1;
    if( next < anim->frame_count )
    {
        *frame = next;
        return true;
    }

    /* DynamicObject.getModel: `frame -= sequenceDefinition.frameCount`.
     * A frameStep of one therefore holds the terminal frame, whereas no
     * frameStep drops the sequence instead of replaying from frame zero. */
    next -= anim->frame_step;
    if( next < 0 || next >= anim->frame_count )
        return false;

    *frame = next;
    return true;
}

bool
ToriDraw_AnimationAdvanceObjectCycles(
    struct ToriDraw_Animation const* anim,
    int* frame,
    int* frame_cycle,
    int cycles)
{
    int cycle;

    if( !anim || !frame || !frame_cycle || !anim->frames || anim->frame_count <= 0 || *frame < 0 ||
        *frame >= anim->frame_count )
        return false;

    cycle = *frame_cycle;
    if( cycle < 0 )
        cycle = 0;
    if( cycles > 0 )
        cycle += cycles;

    /* rev239 class405.method9325/class454: advance only while accumulated
     * time is strictly greater than the current frame length, then subtract
     * that length rather than resetting the counter. Sequence data should not
     * contain non-positive lengths; treating one as a one-cycle frame keeps a
     * malformed record from making this loop non-terminating. */
    while( cycle > (anim->frames[*frame].delay > 0 ? anim->frames[*frame].delay : 1) )
    {
        int delay = anim->frames[*frame].delay > 0 ? anim->frames[*frame].delay : 1;

        cycle -= delay;
        if( !ToriDraw_AnimationAdvanceObjectFrame(anim, frame) )
        {
            *frame_cycle = cycle;
            return false;
        }
    }

    *frame_cycle = cycle;
    return true;
}

void
ToriDraw_AnimationAdvanceLoopCycles(
    struct ToriDraw_Animation const* anim,
    int* frame,
    int* frame_cycle,
    int cycles)
{
    int cycle;

    assert(anim);
    if( !anim->frames || anim->frame_count <= 0 )
        return;
    assert(frame);
    assert(frame_cycle);
    if( *frame < 0 || *frame >= anim->frame_count )
        *frame = 0;

    cycle = *frame_cycle;
    if( cycle < 0 )
        cycle = 0;
    if( cycles > 0 )
        cycle += cycles;

    /* Same boundary rule as the DynamicObject advance above (a frame stays
     * current while the accumulated time is not strictly greater than its
     * length), differing only in what happens past the last frame: wrap to 0
     * rather than consult frame_step. */
    while( cycle > (anim->frames[*frame].delay > 0 ? anim->frames[*frame].delay : 1) )
    {
        int delay = anim->frames[*frame].delay > 0 ? anim->frames[*frame].delay : 1;

        cycle -= delay;
        (*frame)++;
        if( *frame >= anim->frame_count )
            *frame = 0;
    }

    *frame_cycle = cycle;
}

void
ToriDraw_AnimationFree(struct ToriDraw_Animation* anim)
{
    if( !anim )
        return;

    ToriDraw_AnimbaseFree(anim->base);

    if( anim->frames )
    {
        for( int i = 0; i < anim->frame_count; i++ )
        {
            TORIDRAW_FREE(anim->frames[i].groups);
            TORIDRAW_FREE(anim->frames[i].x);
            TORIDRAW_FREE(anim->frames[i].y);
            TORIDRAW_FREE(anim->frames[i].z);
        }
        TORIDRAW_FREE(anim->frames);
    }

    TORIDRAW_FREE(anim->walkmerge);
    TORIDRAW_FREE(anim->maya_masks);
    ToriDraw_SkeletalAnimFree(anim->skeletal);
    TORIDRAW_FREE(anim->frame_sounds.frame_indices);
    TORIDRAW_FREE(anim->frame_sounds.sounds);
    TORIDRAW_FREE(anim);
}

void
ToriDraw_SkeletalAnimFree(struct ToriDraw_SkeletalAnim* skeletal)
{
    if( !skeletal )
        return;
    TORIDRAW_FREE(skeletal->matrices);
    for( int g = 0; g < skeletal->alpha_group_count; g++ )
        TORIDRAW_FREE(skeletal->alpha_labels[g]);
    TORIDRAW_FREE(skeletal->alpha_labels);
    TORIDRAW_FREE(skeletal->alpha_label_counts);
    TORIDRAW_FREE(skeletal->alpha_values);
    TORIDRAW_FREE(skeletal->parents);
    TORIDRAW_FREE(skeletal->locals);
    TORIDRAW_FREE(skeletal->inverse_binds);
    TORIDRAW_FREE(skeletal);
}
