#ifndef SEQUENCE_H
#define SEQUENCE_H

#include "../filelist.h"

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

struct CacheConfigSequence*
config_sequence_new_decode(
    int revision,
    char* buffer,
    int buffer_size);

struct CacheDatSequence
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

struct CacheDatSequence*
config_dat_sequence_new_decode(
    char* buffer,
    int buffer_size);

int
config_dat_sequence_decode_inplace(
    struct CacheDatSequence* sequence,
    char* buffer,
    int buffer_size);

void
config_sequence_free(struct CacheConfigSequence* sequence);
void
config_sequence_free_inplace(struct CacheConfigSequence* sequence);

void
config_sequence_decode_inplace(
    struct CacheConfigSequence* sequence,
    int revision,
    char* buffer,
    int buffer_size);

#endif // SEQUENCE_H