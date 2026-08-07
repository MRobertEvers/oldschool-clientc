#include "toridraw_animation.h"

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
            free(base->bone_groups[i]);
        free(base->bone_groups);
    }
    free(base->bone_group_lengths);
    free(base->types);
    free(base);
}

void
ToriDraw_AnimationSetSeqMeta(
    struct ToriDraw_Animation* anim,
    struct ToriDraw_AnimSeqMeta const* meta)
{
    if( !anim || !meta )
        return;

    free(anim->walkmerge);
    anim->walkmerge = NULL;
    if( meta->walkmerge )
    {
        int count = 0;
        while( meta->walkmerge[count] != 9999999 )
            count++;
        anim->walkmerge = malloc(((size_t)count + 1) * sizeof(int));
        if( anim->walkmerge )
            memcpy(anim->walkmerge, meta->walkmerge, ((size_t)count + 1) * sizeof(int));
    }
    anim->priority = meta->priority;
    anim->max_loops = meta->max_loops;
    anim->preanim_move = meta->preanim_move;
    anim->postanim_move = meta->postanim_move;
    anim->duplicate_behavior = meta->duplicate_behavior;
    anim->replaceheldleft = meta->replaceheldleft;
    anim->replaceheldright = meta->replaceheldright;
    anim->stretches = meta->stretches;
}

bool
ToriDraw_AnimationAdvanceObjectFrame(struct ToriDraw_Animation const* anim, int* frame)
{
    int next;

    if( !anim || !frame || anim->frame_count <= 0 )
        return false;

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
            free(anim->frames[i].groups);
            free(anim->frames[i].x);
            free(anim->frames[i].y);
            free(anim->frames[i].z);
        }
        free(anim->frames);
    }

    free(anim->walkmerge);
    ToriDraw_SkeletalAnimFree(anim->skeletal);
    free(anim->frame_sounds.frame_indices);
    free(anim->frame_sounds.sounds);
    free(anim);
}

void
ToriDraw_SkeletalAnimFree(struct ToriDraw_SkeletalAnim* skeletal)
{
    if( !skeletal )
        return;
    free(skeletal->matrices);
    free(skeletal);
}
