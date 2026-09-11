#include "toridraw_rscache.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/*
 * A framemap is the RIG and a frame is one POSE on it. ToriDraw's two types are
 * the same pair narrowed: the rig's transform types to a byte, its bone groups
 * to bytes, a pose's arguments to int16. Nothing here is a reinterpretation --
 * except one collapse, on transform type 6, described below.
 */

struct ToriDraw_AnimBase*
ToriDraw_RSCacheAnimBaseNew(const struct RSCache_Dat2Framemap* framemap)
{
    struct ToriDraw_AnimBase* base;
    int len;
    int i;

    assert(framemap);

    len = framemap->length;
    base = calloc(1, sizeof(*base));
    assert(base);
    base->length = len;
    if( len <= 0 )
        return base;

    base->types = calloc((size_t)len, sizeof(uint8_t));
    base->bone_groups = calloc((size_t)len, sizeof(uint8_t*));
    base->bone_group_lengths = calloc((size_t)len, sizeof(uint16_t));
    assert(base->types);
    assert(base->bone_groups);
    assert(base->bone_group_lengths);

    for( i = 0; i < len; i++ )
    {
        int glen = framemap->bone_groups_lengths ? framemap->bone_groups_lengths[i] : 0;
        int type = framemap->types ? framemap->types[i] : 0;
        int j;

        /*
         * TYPE_6 is not a distinct transform. The reference rewrites it to
         * ROTATE the moment the framemap is decoded, before anything can
         * observe the difference -- same 14-bit angle scaling, same sin/cos
         * pass about the current pivot. Whatever the authoring tool meant by it
         * the client discards.
         *
         * Folded HERE, in the render-ready base, and not on the framemap: the
         * framemap keeps its wire type so a re-encode stays byte-exact, and the
         * apply switch never needs a second rotate arm.
         */
        if( type == 6 )
            type = 2;
        base->types[i] = (uint8_t)type;
        base->bone_group_lengths[i] = (uint16_t)glen;

        if( glen <= 0 || !framemap->bone_groups || !framemap->bone_groups[i] )
            continue;
        base->bone_groups[i] = calloc((size_t)glen, sizeof(uint8_t));
        assert(base->bone_groups[i]);
        for( j = 0; j < glen; j++ )
            base->bone_groups[i][j] = (uint8_t)framemap->bone_groups[i][j];
    }

    return base;
}

void
ToriDraw_RSCacheAnimBaseFree(struct ToriDraw_AnimBase* base)
{
    int i;

    if( !base )
        return;
    if( base->bone_groups )
        for( i = 0; i < base->length; i++ )
            free(base->bone_groups[i]);
    free(base->bone_groups);
    free(base->bone_group_lengths);
    free(base->types);
    free(base);
}

void
ToriDraw_RSCacheAnimFrameInit(
    struct ToriDraw_AnimFrame* out,
    const struct RSCache_Dat2Frame* frame)
{
    int n;
    int i;

    assert(out);
    assert(frame);

    memset(out, 0, sizeof(*out));
    out->id = frame->_id;
    n = frame->translator_count;
    out->length = n;
    /* A frame with no transforms is a real pose -- the bind pose -- not an
     * error. It has nothing to allocate. */
    if( n <= 0 )
        return;

    out->groups = calloc((size_t)n, sizeof(int16_t));
    out->x = calloc((size_t)n, sizeof(int16_t));
    out->y = calloc((size_t)n, sizeof(int16_t));
    out->z = calloc((size_t)n, sizeof(int16_t));
    assert(out->groups);
    assert(out->x);
    assert(out->y);
    assert(out->z);

    for( i = 0; i < n; i++ )
    {
        out->groups[i] = (int16_t)(frame->index_frame_ids ? frame->index_frame_ids[i] : 0);
        out->x[i] = (int16_t)(frame->translator_arg_x ? frame->translator_arg_x[i] : 0);
        out->y[i] = (int16_t)(frame->translator_arg_y ? frame->translator_arg_y[i] : 0);
        out->z[i] = (int16_t)(frame->translator_arg_z ? frame->translator_arg_z[i] : 0);
    }
}

void
ToriDraw_RSCacheAnimFrameCleanup(struct ToriDraw_AnimFrame* frame)
{
    if( !frame )
        return;
    free(frame->groups);
    free(frame->x);
    free(frame->y);
    free(frame->z);
    memset(frame, 0, sizeof(*frame));
}

struct ToriDraw_Animation*
ToriDraw_RSCacheAnimationNew(
    const struct RSCache_Dat2Framemap* framemap,
    const struct RSCache_Dat2Frame* const* frames,
    int frame_count)
{
    struct ToriDraw_Animation* anim;
    int i;

    assert(framemap);
    assert(frames);
    assert(frame_count > 0);

    anim = calloc(1, sizeof(*anim));
    assert(anim);
    anim->base = ToriDraw_RSCacheAnimBaseNew(framemap);
    anim->frame_count = frame_count;
    anim->frames = calloc((size_t)frame_count, sizeof(struct ToriDraw_AnimFrame));
    assert(anim->frames);

    /* Reference SeqType default is "no held-item override" (opcodes 6/7
     * absent). A calloc'd 0 means "hide the item", so seed -1 for a caller that
     * never attaches seq meta. */
    anim->replaceheldleft = -1;
    anim->replaceheldright = -1;

    for( i = 0; i < frame_count; i++ )
        ToriDraw_RSCacheAnimFrameInit(&anim->frames[i], frames[i]);

    return anim;
}
