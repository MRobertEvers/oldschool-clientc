#ifndef RSCACHE_DATATYPES_DAT2_FRAMEMAP_H
#define RSCACHE_DATATYPES_DAT2_FRAMEMAP_H

#include <stdint.h>

/**
 * A framemap acts as the "rigging" for a model.
 *
 *
 * Conceptually, it essentially defines a virtual machine containing all the operations that can be
 * performed on a model.
 *
 * A conceptual example,
 * [
 *  OP_SET_HIP_ROTATION_ORIGIN(x, y, z),
 *  OP_ROTATE_HIP(pitch, yaw, roll),
 *  OP_SET_JAW_ROTATION_ORIGIN(x, y, z),
 *  OP_ROTATE_JAW(pitch, yaw, roll),
 *  OP_SCALE_JAW(x, y, z),
 *  ...
 * ]
 *
 * Where the frames now specify "programs" to construct the model keyframe.
 *
 * Ex. Frame
 *
 * [
 *  OP_SET_HIP_ROTATION_ORIGIN(0, 0, 0),
 *  OP_ROTATE_HIP(pitch: 512, yaw: 0, roll: 0),
 *  OP_SCALE_JAW(10, 10, 5),
 * ]
 *
 * The function is interpreted by the animation code.
 *
 * The way the effect is achieved is by defining the list of operation types, and then the list of
 * "labels" (or bones) that are affected by each operation.
 *
 * Ex.
 *
 * [
 *  OP_SET_HIP_ROTATION_ORIGIN := (type: SET_ORIGIN, labels: [...<hip_rotation_origin>...]),
 *  OP_ROTATE_HIP := (type: ROTATE, labels: [...<hip_labels>...]),
 * ]
 */
struct RSCache_Dat2Framemap
{
    int id;
    int* types;
    int** bone_groups;        // Set of bone groups
    int* bone_groups_lengths; // Length of each bone group
    int length;
};

struct RSCache_Dat2Disk;
struct RSCache_Dat2DiskArchive;
struct RSCache_ReferenceTable;
struct RSCache_Dat2Framemap*
RSCache_Dat2FramemapNewFromCache(
    struct RSCache_Dat2Disk* cache,
    int framemap_id);
struct RSCache_Dat2Framemap*
RSCache_Dat2FramemapNewFromArchive(
    struct RSCache_Dat2DiskArchive* archive,
    int framemap_id);

/**
 * Frame archives store the framemap id in the first 2 bytes.
 *
 * @param data
 * @param data_size
 * @return int
 */
int
RSCache_Dat2FramemapIdFromFrameArchive(
    char* data,
    int data_size);

struct RSCache_Dat2Framemap*
RSCache_Dat2FramemapNewDecode2(
    int id,
    char* data,
    int data_size);

/**
 * Encode a framemap.
 *
 * A fixed layout rather than an opcode stream, and the decoder discards nothing it
 * reads. The `id` is *not* part of the payload — it comes from the archive/file id —
 * so it is not written.
 *
 * ## Modern archives carry trailing bytes this decoder does not read
 *
 * Measured by comparing each archive's size against what the decode accounts for:
 *
 *   cache.kronos    1887 / 1887 archives  delta 0   (format 5, table version 0)
 *   cache.osrs230   2338 / 2429 archives  delta 2   (format 7, table version 67)
 *   cache.jan2026   2495 / 2613 archives  delta 2
 *
 * So on old caches the decode consumes the archive exactly and this encoder is
 * byte-exact. On modern ones there are two unread trailing bytes — observed as
 * `00 00` — and for ~4% of archives some other amount, so it is *not* simply a
 * two-zero-byte pad.
 *
 * Those bytes are neither decoded nor written back: their meaning has not been
 * established and is not guessed at. Consequence: a modern framemap round-trips
 * semantically but comes out 2 bytes short, which is why byte-exactness reads 0% for
 * modern caches and 100% for old ones.
 *
 * Returns bytes written, or 0 on failure.
 */
uint32_t
RSCache_Dat2FramemapEncode(
    const struct RSCache_Dat2Framemap* def,
    uint8_t* out,
    uint32_t out_capacity);

/** Exact output size for RSCache_Dat2FramemapEncode. */
uint32_t
RSCache_Dat2FramemapEncodeBound(const struct RSCache_Dat2Framemap* def);
void
RSCache_Dat2FramemapFree(struct RSCache_Dat2Framemap* framemap);

#endif // RSCACHE_DATATYPES_DAT2_FRAMEMAP_H
