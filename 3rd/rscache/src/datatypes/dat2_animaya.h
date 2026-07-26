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
    float* values; /* [endTick - startTick + 1] */
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
};

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

/** Sample raw CurvePoints to per-tick float arrays; called after decode. */
void
RSCache_Dat2AnimMayaCurveLoad(struct RSCache_Dat2Curve* curve);

float
RSCache_Dat2AnimMayaCurveGetValue(
    const struct RSCache_Dat2Curve* curve,
    int t);

void
RSCache_Dat2CurveFree(struct RSCache_Dat2Curve* curve);

void
RSCache_Dat2AnimMayaFree(struct RSCache_Dat2AnimMaya* maya);

#endif /* RSCACHE_DATATYPES_DAT2_ANIMAYA_H */
