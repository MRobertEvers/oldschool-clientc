#ifndef SEQUENCE_H
#define SEQUENCE_H

#include "buffer.h"

#include <stdbool.h>

struct CacheConfigFrameSound
{
    int id;
    int loops;
    int location;
    int retain;
    int weight; // Only used in rev226+
};

struct CacheConfigFrameSoundMap
{
    int* frames;                          // Frame indices
    struct CacheConfigFrameSound* sounds; // Sound data
    int count;
    int capacity;
};

struct CacheConfigSequence
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
    struct CacheConfigFrameSoundMap frame_sounds; // Map of frame index to sound data
};

struct CacheConfigSequence* config_sequence_new_decode(int revision, char* buffer, int buffer_size);
void config_sequence_free(struct CacheConfigSequence* sequence);

/**
 * @deprecated
 */
void decode_sequence(struct CacheConfigSequence* def, int revision, struct Buffer* buffer);

/**
 * @deprecated
 */
void print_sequence(struct CacheConfigSequence* def);

/**
 * @deprecated
 */
void free_sequence(struct CacheConfigSequence* def);

#endif // SEQUENCE_H