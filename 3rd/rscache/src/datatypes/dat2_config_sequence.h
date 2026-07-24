#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_SEQUENCE_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_SEQUENCE_H

#include "../filelist.h"
#include "../rscache_profile.h"

#include <stdbool.h>

struct RSCache_Dat2ConfigFrameSound
{
    int id;
    int loops;
    int location;
    int retain;
    int weight; // Only used in rev226+
};

struct RSCache_Dat2ConfigFrameSoundMap
{
    int* frames;                          // Frame indices
    struct RSCache_Dat2ConfigFrameSound* sounds; // Sound data
    int count;
    int capacity;
};

struct RSCache_Dat2ConfigSequence
{
    int id;
    int* frame_ids;
    int* frame_lengths;
    int frame_count;
    int frame_step;
    bool stretches;
    int forced_priority;
    int left_hand_item;
    int right_hand_item;
    int max_loops;
    int precedence_animating;
    int priority;
    int reply_mode;
    int anim_maya_id;
    int anim_maya_start;
    int anim_maya_end;
    int vertical_offset; /* rev226+ opcode 16 */
    int* interleave_leave;
    int* chat_frame_ids;
    bool* anim_maya_masks;
    char* debug_name;
    struct RSCache_Dat2ConfigFrameSoundMap frame_sounds; // Map of frame index to sound data
};

/*
 * Codec versions.
 *
 * Sequences are the clearest case of a difference too large for a flag: the
 * frame-sound record changes shape outright between eras, so each gets its own
 * codec rather than a branch inside one.
 *
 *  v1  pre-220  — frame sound is a single g3 packing id/loops/location.
 *  v2  220..226 — id/loops/location split into separate fields.
 *  v3  226+     — adds a per-sound weight byte, plus opcode 16 vertical offset.
 *
 * The reference client (RuneLite's SequenceLoader.configureForRevision) gates
 * these on the sequence group's own JS5 archive revision, which is why the
 * thresholds below are archive revisions rather than game revisions.
 */
#define RSCACHE_CODEC_SEQUENCE_V1 1
#define RSCACHE_CODEC_SEQUENCE_V2 2
#define RSCACHE_CODEC_SEQUENCE_V3 3

/** Archive revision at which the frame-sound record split (game rev 220). */
#define RSCACHE_SEQUENCE_ARCHIVE_REV_220 1141
/** Archive revision at which the weight byte appeared (game rev 226). */
#define RSCACHE_SEQUENCE_ARCHIVE_REV_226 1268

/** Which sequence codec this cache needs. */
int
RSCache_Dat2ConfigSequenceCodecVersion(const struct RSCache* cache);

/** Decode using the codec version the profile selects. */
void
RSCache_Dat2ConfigSequenceDecodeProfile(
    struct RSCache_Dat2ConfigSequence* sequence,
    const struct RSCache* cache,
    char* data,
    int buffer_size);

struct RSCache_Dat2ConfigSequence*
RSCache_Dat2ConfigSequenceNewDecode(
    int revision,
    char* buffer,
    int buffer_size);

struct RSCache_Dat1ConfigSequence
{
    // frameCount: number = 0;
    // frames: Int16Array | null = null;
    // iframes: Int16Array | null = null;
    // delay: Int16Array | null = null;
    // loops: number = -1;
    // walkmerge: Int32Array | null = null;
    // stretches: boolean = false;
    // priority: number = 5;
    // replaceheldleft: number = -1;
    // replaceheldright: number = -1;
    // maxloops: number = 99;
    // preanim_move: number = -1;
    // postanim_move: number = -1;
    // duplicatebehavior: number = -1;

    int frame_count;
    int* frames;
    int* iframes;
    int* delay;
    int loops;
    int* walkmerge;
    bool stretches;
    int priority;
    int replaceheldleft;
    int replaceheldright;
    int maxloops;
    int preanim_move;
    int postanim_move;
    int duplicate_behavior;
};

struct RSCache_Dat1ConfigSequence*
RSCache_Dat1ConfigSequenceNewDecode(
    char* buffer,
    int buffer_size);

int
RSCache_Dat1ConfigSequenceDecodeInplace(
    struct RSCache_Dat1ConfigSequence* sequence,
    char* buffer,
    int buffer_size);

void
RSCache_Dat1ConfigSequenceFree(struct RSCache_Dat1ConfigSequence* seq);

void
RSCache_Dat2ConfigSequenceFree(struct RSCache_Dat2ConfigSequence* sequence);
void
RSCache_Dat2ConfigSequenceFreeInplace(struct RSCache_Dat2ConfigSequence* sequence);

void
RSCache_Dat2ConfigSequenceDecodeInplace(
    struct RSCache_Dat2ConfigSequence* sequence,
    int revision,
    char* buffer,
    int buffer_size);

#endif // RSCACHE_DATATYPES_DAT2_CONFIG_SEQUENCE_H
