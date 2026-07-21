#include "engine/toridraw_animation_from_rscache.h"

#include "datatypes/dat2_frame.h"
#include "datatypes/dat2_framemap.h"
#include "toridraw_animation.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static struct ToriDraw_AnimBase*
anim_base_from_framemap(struct RSCache_Dat2Framemap const* fm)
{
    struct ToriDraw_AnimBase* base;
    int i;
    int len = fm->length;

    base = calloc(1, sizeof(*base));
    if( !base )
        return NULL;
    base->length = len;
    base->types = len > 0 ? calloc((size_t)len, sizeof(uint8_t)) : NULL;
    base->bone_groups = len > 0 ? calloc((size_t)len, sizeof(uint8_t*)) : NULL;
    base->bone_group_lengths = len > 0 ? calloc((size_t)len, sizeof(uint16_t)) : NULL;

    for( i = 0; i < len; i++ )
    {
        int glen = fm->bone_groups_lengths ? fm->bone_groups_lengths[i] : 0;
        int j;
        if( base->types )
            base->types[i] = (uint8_t)(fm->types ? fm->types[i] : 0);
        if( base->bone_group_lengths )
            base->bone_group_lengths[i] = (uint16_t)glen;
        if( base->bone_groups )
        {
            base->bone_groups[i] = glen > 0 ? calloc((size_t)glen, sizeof(uint8_t)) : NULL;
            for( j = 0; j < glen && base->bone_groups[i]; j++ )
                base->bone_groups[i][j] = (uint8_t)fm->bone_groups[i][j];
        }
    }
    return base;
}

static void
anim_frame_from_rscache(
    struct ToriDraw_AnimFrame* out,
    struct RSCache_Dat2Frame const* src,
    int delay)
{
    int n = src ? src->translator_count : 0;
    int i;
    memset(out, 0, sizeof(*out));
    out->id = src ? src->_id : 0;
    out->length = n;
    out->delay = delay;
    if( n <= 0 )
        return;
    out->groups = calloc((size_t)n, sizeof(int16_t));
    out->x = calloc((size_t)n, sizeof(int16_t));
    out->y = calloc((size_t)n, sizeof(int16_t));
    out->z = calloc((size_t)n, sizeof(int16_t));
    for( i = 0; i < n; i++ )
    {
        if( out->groups )
            out->groups[i] = (int16_t)(src->index_frame_ids ? src->index_frame_ids[i] : 0);
        if( out->x )
            out->x[i] = (int16_t)(src->translator_arg_x ? src->translator_arg_x[i] : 0);
        if( out->y )
            out->y[i] = (int16_t)(src->translator_arg_y ? src->translator_arg_y[i] : 0);
        if( out->z )
            out->z[i] = (int16_t)(src->translator_arg_z ? src->translator_arg_z[i] : 0);
    }
}

struct ToriDraw_Animation*
ToriDraw_AnimationFromRSCache(
    struct RSCache_Dat2Framemap const* framemap,
    struct RSCache_Dat2Frame const* const* frames,
    int const* delays,
    int frame_count,
    int frame_step)
{
    struct ToriDraw_Animation* anim;
    int i;

    if( !framemap || !frames || frame_count <= 0 )
        return NULL;

    anim = calloc(1, sizeof(*anim));
    if( !anim )
        return NULL;
    anim->base = anim_base_from_framemap(framemap);
    anim->frame_count = frame_count;
    anim->frame_step = frame_step;
    anim->frames = calloc((size_t)frame_count, sizeof(struct ToriDraw_AnimFrame));
    if( !anim->base || !anim->frames )
    {
        ToriDraw_AnimationFree(anim);
        return NULL;
    }
    for( i = 0; i < frame_count; i++ )
        anim_frame_from_rscache(&anim->frames[i], frames[i], delays ? delays[i] : 0);

    return anim;
}
