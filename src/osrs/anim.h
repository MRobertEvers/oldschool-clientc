#ifndef ANIM_H
#define ANIM_H

#include "osrs/tables/frame.h"
#include "osrs/tables/model.h"

struct Transformation
{
    int origin_x;
    int origin_y;
    int origin_z;
};

int anim_frame_apply(
    struct CacheFrame* frame,
    struct CacheFramemap* framemap,
    int* vertices_x,
    int* vertices_y,
    int* vertices_z,
    int bones_count,
    int** bones,
    int* bones_sizes);

#endif
