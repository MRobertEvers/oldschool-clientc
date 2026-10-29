#include "mem/torirs_mem.h"
#include "mem/torimem_rscache.h"
#include "mem/torimem_toridraw.h"
#include "engine/toridraw_animation_from_rscache.h"
#include <assert.h>

#include "datatypes/dat1_anim_frame.h"
#include "datatypes/dat2_frame.h"
#include "datatypes/dat2_framemap.h"
#include "datatypes/dat2_animaya.h"
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

    base = TORIDRAW_CALLOC(1, sizeof(*base));
    assert(base);
    base->length = len;
    base->types = len > 0 ? TORIDRAW_CALLOC((size_t)len, sizeof(uint8_t)) : NULL;
    base->bone_groups = len > 0 ? TORIDRAW_CALLOC((size_t)len, sizeof(uint8_t*)) : NULL;
    base->bone_group_lengths = len > 0 ? TORIDRAW_CALLOC((size_t)len, sizeof(uint16_t)) : NULL;

    for( i = 0; i < len; i++ )
    {
        int glen = fm->bone_groups_lengths ? fm->bone_groups_lengths[i] : 0;
        int j;
        if( base->types )
        {
            /*
             * Transform-group type, as the reference decodes it.
             *
             * The complete wire table, read off the rev-727 client
             * (open727Client, AnimationFrameBase / AnimationFrame /
             * MeshRasterizer_Sub1.method11268 -- the last-published client that
             * still implements every one of them):
             *
             *   0  ORIGIN     pivot = mean of the group's vertices, + arg<<4
             *   1  TRANSLATE  verts += arg<<4
             *   2  ROTATE     verts rotated about the pivot, 14-bit angles
             *   3  SCALE      verts = v * arg / 128 about the pivot (id 128)
             *   4  --         unused, never emitted
             *   5  ALPHA      face alpha += x*8, clamped 0..255
             *   6  = 2        aliased to ROTATE at framemap load; see below
             *   7  LIGHT      face HSL: h += x (&63), s += y/4, l += z, clamped
             *   8  BILLBOARD_TRANSLATE
             *   9  BILLBOARD_ROTATE
             *   10 BILLBOARD_SCALE    (identity 128, like SCALE)
             *
             * TYPE_6 is not a distinct transform. The reference rewrites it to
             * ROTATE the moment the framemap is decoded, before anything can
             * observe the difference -- same 14-bit angle scaling, same sin/cos
             * pass about the current pivot. Whatever the authoring tool meant
             * by it (most likely a different Euler order) the client discards;
             * folding it here rather than at apply is what the reference does,
             * and it keeps the apply switch from needing a second rotate arm.
             *
             * 7 through 10 we do not implement, and the model layer's apply
             * switch drops them (toridraw_model.c ToriDraw_AnimApplyTransform).
             * 7 recolours faces in HSL; 8/9/10 do not touch geometry at all --
             * they drive rev-727 BILLBOARDS, a per-model list of camera-facing
             * quads anchored to a face centroid, each carrying its own 2D
             * offset, 2D scale (128 = 1.0), roll (14-bit, in the screen plane)
             * and ARGB. Their label groups index that billboard list, NOT the
             * vertex or face bone groups every other type uses, so there is
             * nothing sane to apply them to until models carry billboards.
             *
             * Keep the wire type raw on the framemap so encode stays exact --
             * only the render-ready base collapses 6.
             */
            int type = fm->types ? fm->types[i] : 0;
            if( type == 6 )
                type = 2;
            base->types[i] = (uint8_t)type;
        }
        if( base->bone_group_lengths )
            base->bone_group_lengths[i] = (uint16_t)glen;
        if( base->bone_groups )
        {
            base->bone_groups[i] = glen > 0 ? TORIDRAW_CALLOC((size_t)glen, sizeof(uint8_t)) : NULL;
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
    out->groups = TORIDRAW_CALLOC((size_t)n, sizeof(int16_t));
    out->x = TORIDRAW_CALLOC((size_t)n, sizeof(int16_t));
    out->y = TORIDRAW_CALLOC((size_t)n, sizeof(int16_t));
    out->z = TORIDRAW_CALLOC((size_t)n, sizeof(int16_t));
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

    anim = TORIDRAW_CALLOC(1, sizeof(*anim));
    assert(anim);
    anim->base = anim_base_from_framemap(framemap);
    anim->frame_count = frame_count;
    anim->frame_step = frame_step;
    /* Reference SeqType default: no held-item override (opcodes 6/7 absent).
     * A calloc'd 0 would mean "hide the item", so seed -1 up front for callers
     * that never attach seq meta. */
    anim->replaceheldleft = -1;
    anim->replaceheldright = -1;
    anim->frames = TORIDRAW_CALLOC((size_t)frame_count, sizeof(struct ToriDraw_AnimFrame));
    assert(anim->frames);
    if( !anim->base )
    {
        ToriDraw_AnimationFree(anim);
        return NULL;
    }
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

    base = TORIDRAW_CALLOC(1, sizeof(*base));
    assert(base);
    base->length = len;
    base->types = len > 0 ? TORIDRAW_CALLOC((size_t)len, sizeof(uint8_t)) : NULL;
    base->bone_groups = len > 0 ? TORIDRAW_CALLOC((size_t)len, sizeof(uint8_t*)) : NULL;
    base->bone_group_lengths = len > 0 ? TORIDRAW_CALLOC((size_t)len, sizeof(uint16_t)) : NULL;

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
            base->bone_groups[i] = glen > 0 ? TORIDRAW_CALLOC((size_t)glen, sizeof(uint8_t)) : NULL;
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

    out->groups = TORIDRAW_CALLOC((size_t)len, sizeof(int16_t));
    out->x = TORIDRAW_CALLOC((size_t)len, sizeof(int16_t));
    out->y = TORIDRAW_CALLOC((size_t)len, sizeof(int16_t));
    out->z = TORIDRAW_CALLOC((size_t)len, sizeof(int16_t));
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

    anim = TORIDRAW_CALLOC(1, sizeof(*anim));
    assert(anim);
    anim->base = anim_base_from_dat1(base);
    anim->frame_count = frame_count;
    anim->frame_step = frame_step;
    /* Reference SeqType default: no held-item override (see dat2 constructor). */
    anim->replaceheldleft = -1;
    anim->replaceheldright = -1;
    anim->frames = TORIDRAW_CALLOC((size_t)frame_count, sizeof(struct ToriDraw_AnimFrame));
    assert(anim->frames);
    if( !anim->base )
    {
        ToriDraw_AnimationFree(anim);
        return NULL;
    }
    for( i = 0; i < frame_count; i++ )
        anim_frame_from_dat1(&anim->frames[i], frames[i], delays ? delays[i] : 0);

    return anim;
}

/*
 * The alpha table (class488.field5864 and class144.method4834): every framemap
 * group of type 5 that the animation drives with a TRANSPARENCY curve, its face
 * labels, and the curve's value at each baked frame. Sampled from the raw
 * points, so a frame past the curve's last point holds or extrapolates by its
 * end mode as the client's sampler does (a finished fade stays faded).
 */
#define SKELETAL_FRAMEMAP_TYPE_ALPHA 5

static void
skeletal_alpha_from_rscache(
    struct ToriDraw_SkeletalAnim* skeletal,
    struct RSCache_Dat2AnimMaya const* maya,
    struct RSCache_Dat2Framemap const* framemap)
{
    int count = 0;
    for( int g = 0; g < maya->group_curve_count && g < framemap->length; g++ )
    {
        struct RSCache_Dat2GroupCurves const* gc = &maya->group_curves[g];
        if( gc->transform_type == RSCACHE_ANIMAYA_TRANSFORM_TRANSPARENCY && gc->curves[0] &&
            framemap->types[g] == SKELETAL_FRAMEMAP_TYPE_ALPHA )
            count++;
    }
    if( count == 0 )
        return;

    skeletal->alpha_label_counts = TORIDRAW_CALLOC((size_t)count, sizeof(int));
    skeletal->alpha_labels = TORIDRAW_CALLOC((size_t)count, sizeof(int*));
    skeletal->alpha_values =
        TORIDRAW_MALLOC((size_t)skeletal->frame_count * (size_t)count * sizeof(float));
    assert(skeletal->alpha_label_counts);
    assert(skeletal->alpha_labels);
    assert(skeletal->alpha_values);
    skeletal->alpha_group_count = count;

    int k = 0;
    for( int g = 0; g < maya->group_curve_count && g < framemap->length; g++ )
    {
        struct RSCache_Dat2GroupCurves const* gc = &maya->group_curves[g];
        if( gc->transform_type != RSCACHE_ANIMAYA_TRANSFORM_TRANSPARENCY || !gc->curves[0] ||
            framemap->types[g] != SKELETAL_FRAMEMAP_TYPE_ALPHA )
            continue;
        int const n = framemap->bone_groups_lengths[g];
        skeletal->alpha_label_counts[k] = n;
        if( n > 0 )
        {
            skeletal->alpha_labels[k] = TORIDRAW_MALLOC((size_t)n * sizeof(int));
            assert(skeletal->alpha_labels[k]);
            memcpy(skeletal->alpha_labels[k], framemap->bone_groups[g], (size_t)n * sizeof(int));
        }
        for( int f = 0; f < skeletal->frame_count; f++ )
            skeletal->alpha_values[(size_t)f * (size_t)count + (size_t)k] =
                RSCache_Dat2AnimMayaCurveEvaluate(gc->curves[0], f);
        k++;
    }
}

/*
 * What a walkmerge needs that the palette has multiplied away: each bone's own
 * matrix per frame, the parent it composes through, and this animation's
 * inverse bind (the pose the walk pass skins with, class174.method5754 ->
 * method5708 with the walk anim's pose id).
 */
static void
skeletal_rig_from_rscache(
    struct ToriDraw_SkeletalAnim* skeletal,
    struct RSCache_Dat2AnimMaya const* maya,
    struct RSCache_Dat2SkeletalBase* base,
    float* locals)
{
    int const bones = skeletal->bone_count;

    assert(locals);
    skeletal->base_id = base->id;
    if( ToriMem_ToriDrawInstalled() )
        locals = ToriMem_Adopt(locals, ToriMem_LibToriDraw());
    skeletal->locals = locals;
    skeletal->parents = TORIDRAW_MALLOC((size_t)bones * sizeof(int));
    skeletal->inverse_binds = TORIDRAW_MALLOC((size_t)bones * 16 * sizeof(float));
    assert(skeletal->parents);
    assert(skeletal->inverse_binds);
    for( int b = 0; b < bones; b++ )
    {
        /* The resolved link, as the palette composed through: an id out of
         * range or naming the bone itself is a root there too. */
        skeletal->parents[b] =
            base->bones[b].parent ? (int)(base->bones[b].parent - base->bones) : -1;
        memcpy(
            &skeletal->inverse_binds[(size_t)b * 16],
            *RSCache_Dat2SkeletalBoneInvertedModelMatrix(base, b, maya->pose_id),
            16 * sizeof(float));
    }
}

struct ToriDraw_SkeletalAnim*
ToriDraw_SkeletalAnimFromRSCache(
    int seq_id,
    struct RSCache_Dat2AnimMaya const* maya,
    struct RSCache_Dat2SkeletalBase* base,
    struct RSCache_Dat2Framemap const* framemap,
    int frame_limit)
{
    struct ToriDraw_SkeletalAnim* skeletal;
    float* palette = NULL;
    float* locals = NULL;
    int frame_count = 0;
    int bone_count = 0;

    assert(maya);
    assert(base);
    assert(framemap);

    /* A rig with no bones has nothing to bake (the palette path's NULL). */
    if( base->bone_count <= 0 )
        return NULL;
    /* The palette and the walkmerge's locals in one pass: each bone's
     * animMatrix is built once for both. */
    RSCache_Dat2SkeletalBaseBake(
        maya, base, frame_limit, &frame_count, &bone_count, &palette, &locals);

    skeletal = TORIDRAW_CALLOC(1, sizeof(*skeletal));
    assert(skeletal);
    skeletal->id = seq_id;
    skeletal->bone_count = bone_count;
    skeletal->frame_count = frame_count;
    /* rscache's palette becomes toridraw's; ToriDraw_SkeletalAnimFree releases
     * it. Both libraries allocate through ToriMem or both through libc
     * (install every library or none), so the handoff moves nothing -- with
     * ToriMem it only re-labels the block as toridraw's. */
    assert(ToriMem_RSCacheInstalled() == ToriMem_ToriDrawInstalled());
    if( ToriMem_ToriDrawInstalled() )
        palette = ToriMem_Adopt(palette, ToriMem_LibToriDraw());
    skeletal->matrices = palette;
    skeletal_alpha_from_rscache(skeletal, maya, framemap);
    skeletal_rig_from_rscache(skeletal, maya, base, locals);
    return skeletal;
}
