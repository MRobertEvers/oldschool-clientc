#ifndef RSCACHE_RSCACHEDAT2A_ANIMAYA_H
#define RSCACHE_RSCACHEDAT2A_ANIMAYA_H

#include <stdint.h>

/**
 * A single keyframe control point for a Curve.
 * Maps to CurvePoint in docs/skeletal/Curve.ts.
 */
struct RSCacheDat2A_CurvePoint
{
    int16_t x;       /* tick                   */
    float   y;       /* value                  */
    float   field2;  /* tangent in  (prev side) */
    float   field3;  /* tangent in  (prev side) */
    float   field4;  /* tangent out (next side) */
    float   field5;  /* tangent out (next side) */
};

/**
 * A sampled animation curve.  After RSCacheDat2A_AnimMayaCurveLoad() is called
 * the raw CurvePoints are consumed and replaced by a dense float array
 * (values[t - startTick]) covering every integer tick from startTick to endTick.
 */
struct RSCacheDat2A_Curve
{
    int     id;
    int     type;
    int     start_interp; /* 0-4 */
    int     end_interp;   /* 0-4 */
    int     bool_flag;

    /* raw points (non-NULL only before Load() is called) */
    struct RSCacheDat2A_CurvePoint* points;
    int point_count;

    /* sampled values (non-NULL after Load()) */
    float*  values;     /* [endTick - startTick + 1] */
    int     start_tick;
    int     end_tick;
    float   min_value;
    float   max_value;
};

/**
 * BONE transform-type curve set (9 curves: rot xyz, trans xyz, scale xyz).
 * Absent curves are NULL.
 */
struct RSCacheDat2A_BoneCurves
{
    struct RSCacheDat2A_Curve* curves[9];
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
struct RSCacheDat2A_AnimMaya
{
    int id;
    int version;
    int base_id;  /* idx1 SeqBase / SkeletalBase archive id */
    int pose_id;

    int bone_curve_count;
    struct RSCacheDat2A_BoneCurves* bone_curves; /* [bone_curve_count] */
};

struct RSCacheDat2Disk;

/** Load and decode one idx22 archive, returning NULL on failure. */
struct RSCacheDat2A_AnimMaya*
RSCacheDat2A_AnimMayaNewFromCache(
    struct RSCacheDat2Disk* cache,
    int anim_maya_id);

struct RSCacheDat2A_AnimMaya*
RSCacheDat2A_AnimMayaNewFromArchive(
    struct RSCacheDat2Disk_ReferenceTable* table,
    struct RSCacheDat2Disk_Archive* archive,
    int anim_maya_id);

/**
 * Decode a raw idx22 data blob.
 * NOTE: bone_curve_count and bone_curves are populated from the stream;
 * the caller must have already ensured enough context (bone count from base).
 * In practice all curves are indexed by boneIndex which determines the array size.
 */
struct RSCacheDat2A_AnimMaya*
RSCacheDat2A_AnimMayaNewDecode(
    int id,
    const char* data,
    int data_size);

/** Sample raw CurvePoints to per-tick float arrays; called after decode. */
void
RSCacheDat2A_AnimMayaCurveLoad(struct RSCacheDat2A_Curve* curve);

float
RSCacheDat2A_AnimMayaCurveGetValue(const struct RSCacheDat2A_Curve* curve, int t);

void
RSCacheDat2A_CurveFree(struct RSCacheDat2A_Curve* curve);

void
RSCacheDat2A_AnimMayaFree(struct RSCacheDat2A_AnimMaya* maya);

#endif /* RSCACHE_RSCACHEDAT2A_ANIMAYA_H */
