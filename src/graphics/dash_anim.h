#ifndef DASH_ANIM_H
#define DASH_ANIM_H

#include <stdbool.h>
#include <stdint.h>

struct DashFrame
{
    int _id;
    int framemap_id;
    int translator_count;
    int16_t* index_frame_ids;
    int16_t* translator_arg_x;
    int16_t* translator_arg_y;
    int16_t* translator_arg_z;
    bool showing;
};

struct DashFramemap
{
    int id;
    int* types;
    int** bone_groups;
    int* bone_groups_lengths;
    int length;
};

void
dashframe_free(struct DashFrame* frame);

void
dashframemap_free(struct DashFramemap* framemap);

#endif