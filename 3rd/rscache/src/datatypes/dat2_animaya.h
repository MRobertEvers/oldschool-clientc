#ifndef RSCACHE_DATATYPES_DAT2_ANIMAYA_H
#define RSCACHE_DATATYPES_DAT2_ANIMAYA_H

#include <stdint.h>

/**
 * A single keyframe control point for a Curve.
 * Maps to CurvePoint in docs/skeletal/Curve.ts.
 */
struct RSCache_Dat2CurvePoint
{
    int16_t x;    /* tick                   */
    float y;      /* value                  */
    float field2; /* tangent in  (prev side) */
    float field3; /* tangent in  (prev side) */
    float field4; /* tangent out (next side) */
    float field5; /* tangent out (next side) */
};

/**
 * A sampled animation curve.  After RSCache_Dat2AnimMayaCurveLoad() is called
 * the raw CurvePoints are consumed and replaced by a dense float array
 * (values[t - startTick]) covering every integer tick from startTick to endTick.
 */
struct RSCache_Dat2Curve
{
    int id;
    int type;
    int start_interp; /* 0-4 */
    int end_interp;   /* 0-4 */
    int bool_flag;

    /* raw points (non-NULL only before Load() is called) */
    struct RSCache_Dat2CurvePoint* points;
    int point_count;

    /* sampled values (non-NULL after Load()) */
    float* values; /* [sampled_count]: ticks start_tick .. start_tick + sampled_count - 1 */
    int sampled_count; /* endTick - startTick + 1, unless a tick limit cut it short */
    int start_tick;
    int end_tick;
    float min_value;
    float max_value;
};

/**
 * BONE transform-type curve set (9 curves: rot xyz, trans xyz, scale xyz).
 * Absent curves are NULL.
 */
struct RSCache_Dat2BoneCurves
{
    struct RSCache_Dat2Curve* curves[9];
};

/**
 * A curve's transform type, the u8 ahead of its group index on the wire. The
 * rev-239 client's class492: each type carries a fixed number of curves, and
 * it applies two of them -- BONE poses the skeleton (class488.method10814 /
 * method10816 / method10826) and TRANSPARENCY adds to the faces of the
 * framemap's alpha groups (class144.method4834). Types 2, 3 and 5 are decoded
 * and stored by that client and never read.
 */
enum RSCache_Dat2AnimMayaTransform
{
    RSCACHE_ANIMAYA_TRANSFORM_NONE = 0,         /* 0 curves */
    RSCACHE_ANIMAYA_TRANSFORM_BONE = 1,         /* 9: rotation, translation, scale xyz */
    RSCACHE_ANIMAYA_TRANSFORM_TYPE2 = 2,        /* 3 */
    RSCACHE_ANIMAYA_TRANSFORM_TYPE3 = 3,        /* 6 */
    RSCACHE_ANIMAYA_TRANSFORM_TRANSPARENCY = 4, /* 1 */
    RSCACHE_ANIMAYA_TRANSFORM_TYPE5 = 5,        /* 3 */
    RSCACHE_ANIMAYA_TRANSFORM_COUNT = 6,
};

#define RSCACHE_ANIMAYA_GROUP_CURVES_MAX 6

/**
 * One framemap transform group's non-bone curves (class488.field5860[group]).
 * A group holds the curves of one transform type, `curves[slot]` by the curve
 * type's slot. These keep their raw points rather than a dense tick range:
 * the client samples them outside their range too (holding or extrapolating
 * by each end's interpolation mode, class488.method10820), which only the raw
 * points can answer -- RSCache_Dat2AnimMayaCurveEvaluate.
 */
struct RSCache_Dat2GroupCurves
{
    int transform_type; /* RSCACHE_ANIMAYA_TRANSFORM_NONE: no curves here */
    struct RSCache_Dat2Curve* curves[RSCACHE_ANIMAYA_GROUP_CURVES_MAX];
};

/**
 * A fully decoded idx22 (Table_Animayas) SkeletalSeq animation.
 * Maps to SkeletalSeq in docs/skeletal/SkeletalSeq.ts.
 *
 * After decode + load, bone_curves[b].curves[0..8] give the per-tick sampled
 * transform channels for bone b (NULL = use bind-pose default).
 * bone_curve_count is the number of entries in the bone_curves array
 * (== skeletal-base bone count).
 */
struct RSCache_Dat2AnimMaya
{
    int id;
    int version;
    int base_id; /* idx1 SeqBase / SkeletalBase archive id */
    int pose_id;

    int bone_curve_count;
    struct RSCache_Dat2BoneCurves* bone_curves; /* [bone_curve_count] */
    /* Every other transform type, by framemap group index (the wire's index
     * field means a group, not a bone, for these). */
    int group_curve_count;
    struct RSCache_Dat2GroupCurves* group_curves; /* [group_curve_count] */
};

/** How many curves a transform type carries (class492's last argument). */
int
RSCache_Dat2AnimMayaTransformCurveCount(int transform_type);

struct RSCache_Dat2Disk;
struct RSCache_Dat2DiskArchive;
struct RSCache_ReferenceTable;

/** Load and decode one idx22 archive, returning NULL on failure. */
struct RSCache_Dat2AnimMaya*
RSCache_Dat2AnimMayaNewFromCache(
    struct RSCache_Dat2Disk* cache,
    int anim_maya_id);

struct RSCache_Dat2AnimMaya*
RSCache_Dat2AnimMayaNewFromArchive(
    struct RSCache_ReferenceTable* table,
    struct RSCache_Dat2DiskArchive* archive,
    int anim_maya_id);

/**
 * Decode a raw idx22 data blob.
 * NOTE: bone_curve_count and bone_curves are populated from the stream;
 * the caller must have already ensured enough context (bone count from base).
 * In practice all curves are indexed by boneIndex which determines the array size.
 */
struct RSCache_Dat2AnimMaya*
RSCache_Dat2AnimMayaNewDecode(
    int id,
    const char* data,
    int data_size);

/**
 * RSCache_Dat2AnimMayaNewDecode, sampling each bone curve only below
 * `tick_limit` (> 0): for a sequence that can show no later tick (its
 * mayarange end), when the curves run far past it -- a ship's sail samples
 * 811 ticks of 121 bones to play 30. Every curve keeps its start/end ticks;
 * RSCache_Dat2AnimMayaCurveGetValue asserts nobody reads a tick that was not
 * sampled.
 */
struct RSCache_Dat2AnimMaya*
RSCache_Dat2AnimMayaNewDecodeTicks(
    int id,
    const char* data,
    int data_size,
    int tick_limit);

/** Sample raw CurvePoints to per-tick float arrays; called after decode. */
void
RSCache_Dat2AnimMayaCurveLoad(struct RSCache_Dat2Curve* curve);

float
RSCache_Dat2AnimMayaCurveGetValue(
    const struct RSCache_Dat2Curve* curve,
    int t);

/**
 * A raw-point curve's value at tick t, anywhere: inside its range the curve
 * itself, before or after it the first/last point when that end's
 * interpolation mode is 0, else the client's extrapolation (class488.method10820
 * / method10813). For curves that kept their points (group curves); a curve
 * with no points is 0, as the client's.
 */
float
RSCache_Dat2AnimMayaCurveEvaluate(
    struct RSCache_Dat2Curve* curve,
    int t);

void
RSCache_Dat2CurveFree(struct RSCache_Dat2Curve* curve);

void
RSCache_Dat2AnimMayaFree(struct RSCache_Dat2AnimMaya* maya);

#endif /* RSCACHE_DATATYPES_DAT2_ANIMAYA_H */
