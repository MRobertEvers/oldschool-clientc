#ifndef RSCACHE_DATATYPES_DAT2_FRAME_H
#define RSCACHE_DATATYPES_DAT2_FRAME_H

#include "dat2_framemap.h"

#include <stdbool.h>

/**
 * @brief A frame is a set of parameters for how to transform each bone in each skeleton.
 *
 * Nah. Each animation consists of a list of frames. All those frames belong in the same frame map.
You derive the frame map from the stand/walk animation of the monster, and then find all animations
that contain frames which belong to that same frame map. Ultimately gives you the ability to extract
a lot more than what Armar's provided. I personally extract these fields, although they can be
extended even further. https://gist.github.com/555196afa946955d37d4525a162712ea

Oh and this is also a more up to date version of what the OP provided, as of right now it's the
current OSRS's cache dump.
 */
struct RSCache_Dat2Frame
{
    int _id;
    void* _framemap;
    // This is the rigging for the frame.
    int framemap_id;
    int translator_count;
    int* index_frame_ids;
    int* translator_arg_x;
    int* translator_arg_y;
    int* translator_arg_z;
    bool showing;

    /*
     * Provenance, so the frame can be written back.
     *
     * The decode is not a parse — it is a transformation. Three things it does make
     * the output alone insufficient to reconstruct the source:
     *
     *  1. It *inserts* entries that were never in the stream: for a bone whose type
     *     is non-zero it walks backwards and adds a zero-translation entry for the
     *     nearest preceding type-0 bone. Nothing in the output marks these.
     *  2. It skips bones whose flag byte is <= 0, so "no entry" covers both "absent"
     *     and "present but zero-flagged".
     *  3. A translation component that was *not* in the stream is filled in with a
     *     type-derived default (128 for type 3, otherwise 0), which is
     *     indistinguishable from an authored value that happens to equal it.
     *
     * Recording the flag stream verbatim answers (2) and (3) — the flag bits say
     * exactly which components were present — and `synthesized` answers (1).
     */
    /** Number of bone flag bytes, i.e. the `length` byte from the stream. */
    int flag_count;
    /** The flag stream verbatim, `flag_count` bytes. */
    uint8_t* bone_flags;
    /** Per output entry: was it inserted by the decoder rather than read? */
    bool* synthesized;
};

struct RSCache_Dat2Disk;
struct RSCache_Dat2Frame*
RSCache_Dat2FrameNewFromCache(
    struct RSCache_Dat2Disk* cache,
    int frame_id,
    struct RSCache_Dat2Framemap* framemap);

struct RSCache_Dat2Frame*
RSCache_Dat2FrameNewDecode2(
    int frame_id,
    struct RSCache_Dat2Framemap* framemap,
    char* data,
    int data_size);

/**
 * The frame map is required to parse the frame,
 * the framemap id is the first short stored in the frame's buffer.
 *
 * @param data
 * @param data_size
 * @return int
 */
int
RSCache_Dat2FrameFramemapIdFromFile(
    char* data,
    int data_size);

/**
 * Encode an animation frame.
 *
 * Exact, because the decode records what it would otherwise destroy — see the
 * provenance note on the struct. Specifically it replays the flag stream verbatim
 * rather than deriving it from the translation values, and skips the entries the
 * decoder invented.
 *
 * Deriving the flags is not possible: a component absent from the stream is filled
 * in with a type-derived default (128 for bone type 3, else 0), which cannot be told
 * apart from an authored value that happens to equal that default. Reproducing the
 * flag bytes sidesteps the question entirely.
 *
 * Requires a frame produced by this library's decoder. A hand-built frame must set
 * `flag_count`, `bone_flags` and `synthesized` consistently with the entries; the
 * encoder returns 0 rather than guessing if they are missing or a bone index falls
 * outside the flag stream.
 *
 * Returns bytes written, or 0 on failure.
 */
uint32_t
RSCache_Dat2FrameEncode(
    const struct RSCache_Dat2Frame* def,
    uint8_t* out,
    uint32_t out_capacity);

/** Worst-case output size for RSCache_Dat2FrameEncode. */
uint32_t
RSCache_Dat2FrameEncodeBound(const struct RSCache_Dat2Frame* def);

void
RSCache_Dat2FrameFree(struct RSCache_Dat2Frame* frame);

#endif // RSCACHE_DATATYPES_DAT2_FRAME_H
