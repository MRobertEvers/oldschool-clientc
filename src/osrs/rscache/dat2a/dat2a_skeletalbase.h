#ifndef RSCACHE_RSCACHEDAT2A_SKELETALBASE_H
#define RSCACHE_RSCACHEDAT2A_SKELETALBASE_H

/**
 * Skeletal bind-pose data decoded from the tail of an idx1 SeqBase file.
 * Maps to SkeletalBase / SkeletalBone in docs/skeletal/.
 *
 * The idx1 Dat2SeqBase layout (Dat2SeqBase.load from docs/seq/SeqBase.ts):
 *   u8  count
 *   count x u8  type
 *   count x u8  transformActor  (rev >= 481, always present in modern caches)
 *   count x u16 mask            (rev >= 530, always present in modern caches)
 *   count x u8  labelLen
 *   all labels packed (labelLen[i] x u8 each)
 *   -- if bytes remain:
 *   u16 boneCount
 *   if boneCount > 0:
 *     u8 poseCount
 *     boneCount x SkeletalBone:
 *       s16 parentId  (-1 = root)
 *       poseCount x (16 f32 localMatrix + 3 f32 unused)
 *
 * After construction each bone has:
 *   - local_matrices[poseId]        (column-major 4x4 float)
 *   - model_matrices[poseId]        (local * parent.model, computed on demand)
 *   - inverted_model_matrices[poseId]
 *   - rotations[poseId]  (euler xyz from inverted local)
 *   - translations[poseId]
 *   - scalings[poseId]
 *   - parent pointer
 */

#define SKELETALBASE_MAX_POSES 8

typedef float SkeletalMat4[16]; /* column-major */
typedef float SkeletalVec3[3];

struct RSCacheDat2A_SkeletalBone
{
    int parent_id; /* -1 = root */

    int pose_count;
    SkeletalMat4* local_matrices;            /* [pose_count] */
    SkeletalMat4* model_matrices;            /* [pose_count], lazy-computed */
    SkeletalMat4* inverted_model_matrices;   /* [pose_count], lazy-computed */

    SkeletalVec3* rotations;    /* [pose_count] euler xyz (radians) */
    SkeletalVec3* translations; /* [pose_count] */
    SkeletalVec3* scalings;     /* [pose_count] */

    struct RSCacheDat2A_SkeletalBone* parent; /* NULL for roots */
};

struct RSCacheDat2A_SkeletalBase
{
    int bone_count;
    int pose_count;
    struct RSCacheDat2A_SkeletalBone* bones; /* [bone_count] */
};

struct RSCacheDat2Disk;

/**
 * Load and decode the skeletal base appended to the idx1 file for base_id.
 * Returns NULL if the file has no skeletal tail or on error.
 */
struct RSCacheDat2A_SkeletalBase*
RSCacheDat2A_SkeletalBaseNewFromCache(
    struct RSCacheDat2Disk* cache,
    int base_id);

/**
 * Compute (lazily) and return the model-space matrix for bone b, pose poseId.
 * Walks up the parent chain as needed.
 */
const SkeletalMat4*
RSCacheDat2A_SkeletalBoneGetModelMatrix(
    struct RSCacheDat2A_SkeletalBase* base,
    int bone_index,
    int pose_id);

/**
 * Return the inverse of the model-space matrix for bone b, pose poseId.
 */
const SkeletalMat4*
RSCacheDat2A_SkeletalBoneGetInvertedModelMatrix(
    struct RSCacheDat2A_SkeletalBase* base,
    int bone_index,
    int pose_id);

void
RSCacheDat2A_SkeletalBaseFree(struct RSCacheDat2A_SkeletalBase* base);

/* ---- Baked palette ---- */

struct RSCacheDat2A_AnimMaya;

/**
 * Bake all frames from an AnimMaya + SkeletalBase into a flat matrix palette.
 *
 * Returns a heap-allocated array of  frame_count * bone_count  column-major
 * 4x4 float matrices (16 floats each):
 *
 *   palette[(frame * bone_count + bone) * 16 .. +15]
 *
 * This is the skinning matrix to multiply each original vertex position by
 * for a given frame and bone influence.
 *
 * Returns NULL on failure.  frame_count_out and bone_count_out are set on success.
 */
float*
RSCacheDat2A_SkeletalBaseBakePalette(
    const struct RSCacheDat2A_AnimMaya* maya,
    struct RSCacheDat2A_SkeletalBase*   base,
    int*                                frame_count_out,
    int*                                bone_count_out);

#endif /* RSCACHE_RSCACHEDAT2A_SKELETALBASE_H */
