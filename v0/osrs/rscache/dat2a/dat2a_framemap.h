#ifndef RSCACHE_RSCACHEDAT2A_FRAMEMAP_H
#define RSCACHE_RSCACHEDAT2A_FRAMEMAP_H

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
struct RSCacheDat2A_Framemap
{
    int id;
    int* types;
    int** bone_groups;        // Set of bone groups
    int* bone_groups_lengths; // Length of each bone group
    int length;
};

struct RSCacheDat2Disk;
struct RSCacheDat2Disk_Archive;
struct RSCacheDat2Disk_ReferenceTable;
struct RSCacheDat2A_Framemap*
RSCacheDat2A_FramemapNewFromCache(
    struct RSCacheDat2Disk* cache,
    int framemap_id);
struct RSCacheDat2A_Framemap*
RSCacheDat2A_FramemapNewFromArchive(
    struct RSCacheDat2Disk_Archive* archive,
    int framemap_id);

/**
 * Frame archives store the framemap id in the first 2 bytes.
 *
 * @param data
 * @param data_size
 * @return int
 */
int
RSCacheDat2A_FramemapIdFromFrameArchive(
    char* data,
    int data_size);

struct RSCacheDat2A_Framemap*
RSCacheDat2A_FramemapNewDecode2(
    int id,
    char* data,
    int data_size);
void
RSCacheDat2A_FramemapFree(struct RSCacheDat2A_Framemap* framemap);

#endif // FRAMEMAP_H
