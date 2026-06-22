#ifndef RSCACHE_RSCACHEDAT2A_CONFIGSEQUENCE_H
#define RSCACHE_RSCACHEDAT2A_CONFIGSEQUENCE_H

#include "../shared/shared_file_list.h"

#include <stdbool.h>

struct RSCacheDat2A_ConfigFrameSound
{
    int id;
    int loops;
    int location;
    int retain;
    int weight; // Only used in rev226+
};

struct RSCacheDat2A_ConfigFrameSoundMap
{
    int* frames;                          // Frame indices
    struct RSCacheDat2A_ConfigFrameSound* sounds; // Sound data
    int count;
    int capacity;
};

struct RSCacheDat2A_ConfigSequence
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
    int* interleave_leave;
    int* chat_frame_ids;
    bool* anim_maya_masks;
    char* debug_name;
    struct RSCacheDat2A_ConfigFrameSoundMap frame_sounds; // Map of frame index to sound data
};

struct RSCacheDat2A_ConfigSequence*
RSCacheDat2A_ConfigSequenceNewDecode(
    int revision,
    char* buffer,
    int buffer_size);

struct RSCacheDat1A_ConfigSequence
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

struct RSCacheDat1A_ConfigSequence*
RSCacheDat1A_ConfigSequenceNewDecode(
    char* buffer,
    int buffer_size);

int
RSCacheDat1A_ConfigSequenceDecodeInplace(
    struct RSCacheDat1A_ConfigSequence* sequence,
    char* buffer,
    int buffer_size);

void
RSCacheDat1A_ConfigSequenceFree(struct RSCacheDat1A_ConfigSequence* seq);

void
RSCacheDat2A_ConfigSequenceFree(struct RSCacheDat2A_ConfigSequence* sequence);
void
RSCacheDat2A_ConfigSequenceFreeInplace(struct RSCacheDat2A_ConfigSequence* sequence);

void
RSCacheDat2A_ConfigSequenceDecodeInplace(
    struct RSCacheDat2A_ConfigSequence* sequence,
    int revision,
    char* buffer,
    int buffer_size);

#endif // SEQUENCE_H