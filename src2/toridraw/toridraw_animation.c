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

    free(anim);
}
