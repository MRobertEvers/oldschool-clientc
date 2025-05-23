#ifndef ANIM_H
#define ANIM_H

#include "frame.h"
#include "model.h"

struct Transformation
{
    int origin_x;
    int origin_y;
    int origin_z;
};

int anim_frame_apply(
    struct FrameDefinition* frame,
    int* vertices_x,
    int* vertices_y,
    int* vertices_z,
    int bone_groups_count,
    int** bone_groups,
    int* bone_groups_sizes);

#endif
