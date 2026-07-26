#ifndef RSCACHE_DATATYPES_DAT2_FRAMEMAP_H
#define RSCACHE_DATATYPES_DAT2_FRAMEMAP_H

#include <stdbool.h>
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

    /*
     * Provenance for era-dependent arrays that sit between the type list and the
     * bone-group lengths (rs-map-viewer Dat2SeqBase.load):
     *
     *   RS >= 481 — transform_actor[i] (u8, 1 = applies to actor)
     *   RS >= 530 — masks[i] (u16)
     *
     * Recorded so encode can reproduce them; the animation apply path does not
     * currently consume them.
     */
    bool has_transform_actor;
    uint8_t* transform_actor;
    bool has_masks;
    uint16_t* masks;

    /** Trailing skeletal-base blob (or modern OSRS unread pad). Captured
     *  verbatim so encode stays byte-exact; not interpreted here. */
    uint8_t* tail;
    int tail_size;
};

/*
 * Framemap wire formats. Pin via cache->codec[RSCACHE_TYPE_FRAMEMAP], or leave
 * AUTO and let RSCache_Dat2FramemapCodecVersion derive from the profile:
 *
 *   V1 — OSRS / RS2 < 481:  [count][types…][group_lens…][bones…]
 *   V2 — RS2 >= 481:        …types…[transform_actor…][group_lens…]…
 *   V3 — RS2 >= 530:        …types…[transform_actor…][masks u16…][group_lens…]…
 *
 * See rs-map-viewer Dat2SeqBase.load.
 */
#define RSCACHE_CODEC_FRAMEMAP_V1 1
#define RSCACHE_CODEC_FRAMEMAP_V2 2
#define RSCACHE_CODEC_FRAMEMAP_V3 3

struct RSCache;

/** Which framemap codec this cache needs. */
int
RSCache_Dat2FramemapCodecVersion(const struct RSCache* cache);

struct RSCache_Dat2Disk;
struct RSCache_Dat2DiskArchive;
struct RSCache_ReferenceTable;

struct RSCache_Dat2Framemap*
RSCache_Dat2FramemapNewFromCache(
    struct RSCache_Dat2Disk* cache,
    int framemap_id);

/** Decode as V1. Prefer NewFromArchiveProfile at call sites that know the cache. */
struct RSCache_Dat2Framemap*
RSCache_Dat2FramemapNewFromArchive(
    struct RSCache_Dat2DiskArchive* archive,
    int framemap_id);

struct RSCache_Dat2Framemap*
RSCache_Dat2FramemapNewFromArchiveProfile(
    const struct RSCache* cache,
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

/** Decode with an explicit codec version (RSCACHE_CODEC_FRAMEMAP_V1..V3). */
struct RSCache_Dat2Framemap*
RSCache_Dat2FramemapNewDecodeCodec(
    int id,
    char* data,
    int data_size,
    int codec_version);

/** Decode using the codec the profile selects. */
struct RSCache_Dat2Framemap*
RSCache_Dat2FramemapNewDecodeProfile(
    const struct RSCache* cache,
    int id,
    char* data,
    int data_size);

/** Decode as V1. Prefer NewDecodeProfile / NewDecodeCodec. */
struct RSCache_Dat2Framemap*
RSCache_Dat2FramemapNewDecode2(
    int id,
    char* data,
    int data_size);

/**
 * Encode a framemap.
 *
 * A fixed layout rather than an opcode stream. The `id` is *not* part of the
 * payload — it comes from the archive/file id — so it is not written.
 *
 * When `has_transform_actor` / `has_masks` are set the matching arrays are
 * emitted between the type list and the group lengths (V2 / V3). Any trailing
 * skeletal-base / unread pad captured in `tail` is appended verbatim, so a
 * decode→encode round-trip is byte-exact for both OSRS and RS2 archives.
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
