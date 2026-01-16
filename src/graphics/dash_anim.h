#ifndef DASH_ANIM_H
#define DASH_ANIM_H

#include <stdbool.h>

struct DashFrame
{
    int _id;
    // This is the rigging for the frame.
    int framemap_id;
    int translator_count;
    int* index_frame_ids;
    int* translator_arg_x;
    int* translator_arg_y;
    int* translator_arg_z;
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

#endif