#include "engine/toridraw_animation_from_rscache.h"
#include <assert.h>

#include "datatypes/dat1_anim_frame.h"
#include "datatypes/dat2_frame.h"
#include "datatypes/dat2_framemap.h"
#include "datatypes/dat2_skeletalbase.h"
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
    assert(base);
    base->length = len;
    base->types = len > 0 ? calloc((size_t)len, sizeof(uint8_t)) : NULL;
    base->bone_groups = len > 0 ? calloc((size_t)len, sizeof(uint8_t*)) : NULL;
    base->bone_group_lengths = len > 0 ? calloc((size_t)len, sizeof(uint16_t)) : NULL;

    for( i = 0; i < len; i++ )
    {
        int glen = fm->bone_groups_lengths ? fm->bone_groups_lengths[i] : 0;
        int j;
        if( base->types )
        {
            /* Dat2SeqBase maps TYPE_6 to ROTATE for apply; keep the wire type
             * raw on the framemap so encode stays exact. */
            int type = fm->types ? fm->types[i] : 0;
            if( type == 6 )
                type = 2;
            base->types[i] = (uint8_t)type;
        }
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
    assert(anim);
    anim->base = anim_base_from_framemap(framemap);
    anim->frame_count = frame_count;
    anim->frame_step = frame_step;
    /* Reference SeqType default: no held-item override (opcodes 6/7 absent).
     * A calloc'd 0 would mean "hide the item", so seed -1 up front for callers
     * that never attach seq meta. */
    anim->replaceheldleft = -1;
    anim->replaceheldright = -1;
    anim->frames = calloc((size_t)frame_count, sizeof(struct ToriDraw_AnimFrame));
    assert(anim->base);
    assert(anim->frames);
    for( i = 0; i < frame_count; i++ )
        anim_frame_from_rscache(&anim->frames[i], frames[i], delays ? delays[i] : 0);

    return anim;
}

static struct ToriDraw_AnimBase*
anim_base_from_dat1(struct RSCache_Dat1AnimBase const* src)
{
    struct ToriDraw_AnimBase* base;
    int len = src->length;
    int i;

    base = calloc(1, sizeof(*base));
    assert(base);
    base->length = len;
    base->types = len > 0 ? calloc((size_t)len, sizeof(uint8_t)) : NULL;
    base->bone_groups = len > 0 ? calloc((size_t)len, sizeof(uint8_t*)) : NULL;
    base->bone_group_lengths = len > 0 ? calloc((size_t)len, sizeof(uint16_t)) : NULL;

    for( i = 0; i < len; i++ )
    {
        /* dat1 calls these "labels"; they are the same per-transform vertex
         * group lists dat2 stores in the framemap. */
        int glen = src->label_counts ? src->label_counts[i] : 0;
        int j;
        if( base->types )
            base->types[i] = src->types ? src->types[i] : 0;
        if( base->bone_group_lengths )
            base->bone_group_lengths[i] = (uint16_t)glen;
        if( base->bone_groups )
        {
            base->bone_groups[i] = glen > 0 ? calloc((size_t)glen, sizeof(uint8_t)) : NULL;
            for( j = 0; j < glen && base->bone_groups[i] && src->labels && src->labels[i]; j++ )
                base->bone_groups[i][j] = src->labels[i][j];
        }
    }
    return base;
}

static void
anim_frame_from_dat1(
    struct ToriDraw_AnimFrame* out,
    struct RSCache_Dat1AnimFrame const* src,
    int delay)
{
    int len = src ? src->length : 0;
    int i;

    memset(out, 0, sizeof(*out));
    out->id = src ? src->id : 0;
    out->length = len;
    /* The frame's own delay is the animation's cadence; the sequence's per-frame
     * delay only overrides it when non-zero (reference SeqType.delay). */
    out->delay = delay > 0 ? delay : (src ? src->delay : 0);
    if( len <= 0 )
        return;

    out->groups = calloc((size_t)len, sizeof(int16_t));
    out->x = calloc((size_t)len, sizeof(int16_t));
    out->y = calloc((size_t)len, sizeof(int16_t));
    out->z = calloc((size_t)len, sizeof(int16_t));
    for( i = 0; i < len; i++ )
    {
        if( out->groups )
            out->groups[i] = src->groups ? src->groups[i] : 0;
        if( out->x )
            out->x[i] = src->x ? src->x[i] : 0;
        if( out->y )
            out->y[i] = src->y ? src->y[i] : 0;
        if( out->z )
            out->z[i] = src->z ? src->z[i] : 0;
    }
}

struct ToriDraw_Animation*
ToriDraw_AnimationFromRSCacheDat1(
    struct RSCache_Dat1AnimBase const* base,
    struct RSCache_Dat1AnimFrame const* const* frames,
    int const* delays,
    int frame_count,
    int frame_step)
{
    struct ToriDraw_Animation* anim;
    int i;

    if( !base || !frames || frame_count <= 0 )
        return NULL;

    anim = calloc(1, sizeof(*anim));
    assert(anim);
    anim->base = anim_base_from_dat1(base);
    anim->frame_count = frame_count;
    anim->frame_step = frame_step;
    /* Reference SeqType default: no held-item override (see dat2 constructor). */
    anim->replaceheldleft = -1;
    anim->replaceheldright = -1;
    anim->frames = calloc((size_t)frame_count, sizeof(struct ToriDraw_AnimFrame));
    assert(anim->base);
    assert(anim->frames);
    for( i = 0; i < frame_count; i++ )
        anim_frame_from_dat1(&anim->frames[i], frames[i], delays ? delays[i] : 0);

    return anim;
}

struct ToriDraw_SkeletalAnim*
ToriDraw_SkeletalAnimFromRSCache(
    int seq_id,
    struct RSCache_Dat2AnimMaya const* maya,
    struct RSCache_Dat2SkeletalBase* base)
{
    struct ToriDraw_SkeletalAnim* skeletal;
    float* palette;
    int frame_count = 0;
    int bone_count = 0;

    assert(maya);
    assert(base);

    palette = RSCache_Dat2SkeletalBaseBakePalette(maya, base, &frame_count, &bone_count);
    if( !palette )
        return NULL;
    if( frame_count <= 0 || bone_count <= 0 )
    {
        free(palette);
        return NULL;
    }

    skeletal = calloc(1, sizeof(*skeletal));
    assert(skeletal);
    skeletal->id = seq_id;
    skeletal->bone_count = bone_count;
    skeletal->frame_count = frame_count;
    skeletal->matrices = palette;
    return skeletal;
}
