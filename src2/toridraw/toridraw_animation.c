#include "toridraw_animation.h"

#include "osrs/rscache/tables_dat/animframe.h"

#include <stdlib.h>
#include <string.h>

static struct ToriDraw_AnimBase*
toridraw_animbase_move_from_cache(struct CacheAnimBase* cache_base)
{
    if( !cache_base )
        return NULL;

    struct ToriDraw_AnimBase* base = malloc(sizeof(struct ToriDraw_AnimBase));
    if( !base )
        return NULL;

    memset(base, 0, sizeof(struct ToriDraw_AnimBase));
    base->length = cache_base->length;
    base->types = cache_base->types;
    base->bone_groups = cache_base->labels;
    base->bone_group_lengths = cache_base->label_counts;

    cache_base->types = NULL;
    cache_base->labels = NULL;
    cache_base->label_counts = NULL;
    cache_base->length = 0;

    free(cache_base);
    return base;
}

static void
toridraw_animbase_free(struct ToriDraw_AnimBase* base)
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

struct ToriDraw_Animation*
toridraw_animation_new_from_cache_dat_animbaseframes(struct CacheDatAnimBaseFrames* abf)
{
    if( !abf )
        return NULL;

    struct ToriDraw_Animation* anim = malloc(sizeof(struct ToriDraw_Animation));
    if( !anim )
        return NULL;

    memset(anim, 0, sizeof(struct ToriDraw_Animation));
    anim->base = toridraw_animbase_move_from_cache(abf->base);
    abf->base = NULL;

    anim->frame_count = abf->frame_count;
    if( abf->frame_count > 0 && abf->frames )
    {
        anim->frames = malloc((size_t)abf->frame_count * sizeof(struct ToriDraw_AnimFrame));
        if( !anim->frames )
        {
            toridraw_animation_free(anim);
            free(abf->frames);
            free(abf);
            return NULL;
        }

        for( int i = 0; i < abf->frame_count; i++ )
        {
            struct CacheAnimframe* cf = &abf->frames[i];
            struct ToriDraw_AnimFrame* tf = &anim->frames[i];
            memset(tf, 0, sizeof(struct ToriDraw_AnimFrame));

            tf->id = cf->id;
            tf->length = cf->length;
            tf->groups = cf->groups;
            tf->x = cf->x;
            tf->y = cf->y;
            tf->z = cf->z;
            tf->delay = cf->delay;

            cf->groups = NULL;
            cf->x = NULL;
            cf->y = NULL;
            cf->z = NULL;
            cf->length = 0;
        }
        free(abf->frames);
        abf->frames = NULL;
        abf->frame_count = 0;
    }

    free(abf);
    return anim;
}

void
toridraw_animation_free(struct ToriDraw_Animation* anim)
{
    if( !anim )
        return;

    toridraw_animbase_free(anim->base);

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
