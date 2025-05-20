#ifndef SEQUENCE_H
#define SEQUENCE_H

#include "buffer.h"

#include <stdbool.h>

struct SequenceDefinition
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
};

void decode_sequence(struct SequenceDefinition* def, struct Buffer* buffer);
void print_sequence(struct SequenceDefinition* def);
void free_sequence(struct SequenceDefinition* def);

#endif // SEQUENCE_H