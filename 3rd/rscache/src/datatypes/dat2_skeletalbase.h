#ifndef RSCACHE_DATATYPES_DAT2_SKELETALBASE_H
#define RSCACHE_DATATYPES_DAT2_SKELETALBASE_H

#include <stdint.h>

/**
 * Skeletal (Animaya) bind pose — the rig an idx22 SkeletalSeq animates.
 *
 * It lives in the *tail* of an idx1 framemap (SeqBase) file, after the classic
 * transform-type / bone-group lists the frame animator uses:
 *
 *   u16 boneCount
 *   if boneCount > 0:
 *     u8 poseCount
 *     boneCount x:
 *       s16 parentId               (-1 = root)
 *       poseCount x (16 f32 localMatrix + 3 f32 unused)
 *
 * `RSCache_Dat2Framemap` already captures those trailing bytes verbatim
 * (`tail` / `tail_size`), so decode reads them rather than re-walking the
 * classic section, which keeps the era-dependent transform_actor / masks
 * handling in exactly one place.
 *
 * Per bone, per pose the decode derives:
 *   - local_matrices[pose]           (column-major 4x4, straight off the wire)
 *   - model_matrices[pose]           local * parent.model (lazy)
 *   - inverted_model_matrices[pose]  (lazy)
 *   - rotations/translations/scalings — the bind-pose defaults an animation
 *     curve set falls back to for any channel it does not drive.
 *
 * Maps to SkeletalBase / SkeletalBone in docs/skeletal/.
 */

typedef float RSCache_SkeletalMat4[16]; /* column-major */
typedef float RSCache_SkeletalVec3[3];

struct RSCache_Dat2SkeletalBone
{
    int parent_id; /* -1 = root */

    int pose_count;
    RSCache_SkeletalMat4* local_matrices;          /* [pose_count] */
    RSCache_SkeletalMat4* model_matrices;          /* [pose_count], lazy */
    RSCache_SkeletalMat4* inverted_model_matrices; /* [pose_count], lazy */

    RSCache_SkeletalVec3* rotations;    /* [pose_count] euler xyz (radians) */
    RSCache_SkeletalVec3* translations; /* [pose_count] */
    RSCache_SkeletalVec3* scalings;     /* [pose_count] */

    struct RSCache_Dat2SkeletalBone* parent; /* NULL for roots */
};

struct RSCache_Dat2SkeletalBase
{
    int id; /* idx1 framemap (SeqBase) archive id */
    int bone_count;
    int pose_count;
    struct RSCache_Dat2SkeletalBone* bones; /* [bone_count] */
};

struct RSCache_Dat2Framemap;
struct RSCache_Dat2AnimMaya;

/** Decode the skeletal tail of an already-decoded framemap. NULL when the
 *  framemap carries no skeletal rig (every pre-Animaya cache). */
struct RSCache_Dat2SkeletalBase*
RSCache_Dat2SkeletalBaseNewFromFramemap(const struct RSCache_Dat2Framemap* framemap);

/** Decode a raw skeletal tail blob (`[u16 boneCount][u8 poseCount][bones…]`). */
struct RSCache_Dat2SkeletalBase*
RSCache_Dat2SkeletalBaseNewDecodeTail(
    int base_id,
    const uint8_t* tail,
    int tail_size);

/** Model-space matrix for (bone, pose); computed on first use. */
const RSCache_SkeletalMat4*
RSCache_Dat2SkeletalBoneModelMatrix(
    struct RSCache_Dat2SkeletalBase* base,
    int bone_index,
    int pose_id);

/** Inverse of RSCache_Dat2SkeletalBoneModelMatrix. */
const RSCache_SkeletalMat4*
RSCache_Dat2SkeletalBoneInvertedModelMatrix(
    struct RSCache_Dat2SkeletalBase* base,
    int bone_index,
    int pose_id);

/**
 * Bake every tick of an AnimMaya against a bind pose into a flat skinning
 * palette of column-major 4x4 matrices:
 *
 *   palette[(frame * bone_count + bone) * 16 .. +15]
 *
 * Each entry is animModelMatrix(frame, bone) * invertedModelMatrix(bone, poseId)
 * — multiply an original vertex position by the weighted sum of its influences
 * to skin it. Caller owns the returned array. NULL on failure.
 */
float*
RSCache_Dat2SkeletalBaseBakePalette(
    const struct RSCache_Dat2AnimMaya* maya,
    struct RSCache_Dat2SkeletalBase* base,
    int* frame_count_out,
    int* bone_count_out);

/**
 * RSCache_Dat2SkeletalBaseBakePalette and RSCache_Dat2SkeletalBaseBakeLocals in
 * one pass, each animMatrix built once for both. The caller owns both arrays,
 * each [*frame_count_out * *bone_count_out * 16].
 *
 * `frame_limit` > 0 bakes only the first frame_limit ticks: the frames a
 * sequence can show (its mayarange end) when its curves run longer -- a ship's
 * sail plays 30 or 90 of 811. 0 bakes the whole curve set.
 */
void
RSCache_Dat2SkeletalBaseBake(
    const struct RSCache_Dat2AnimMaya* maya,
    struct RSCache_Dat2SkeletalBase* base,
    int frame_limit,
    int* frame_count_out,
    int* bone_count_out,
    float** palette_out,
    float** locals_out);

/**
 * The same ticks as RSCache_Dat2SkeletalBaseBakePalette, before the hierarchy:
 * each bone's own animMatrix (its curves over this animation's bind pose),
 *
 *   locals[(frame * bone_count + bone) * 16 .. +15]
 *
 * A walkmerge poses some bones from one animation and the rest from another,
 * then composes them through the parents (rev-241 class128.method4376, then
 * class174.method5804), so it needs the matrices the palette has already
 * multiplied together. `frame_count` is the palette's. Caller owns the array.
 */
float*
RSCache_Dat2SkeletalBaseBakeLocals(
    const struct RSCache_Dat2AnimMaya* maya,
    struct RSCache_Dat2SkeletalBase* base,
    int frame_count);

void
RSCache_Dat2SkeletalBaseFree(struct RSCache_Dat2SkeletalBase* base);

#endif /* RSCACHE_DATATYPES_DAT2_SKELETALBASE_H */
