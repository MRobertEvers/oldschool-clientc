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
    int* face_alphas,
    // These are the vertex bones of the model. They are defined with the model.
    int vertex_bones_count,
    int** vertex_bones,
    int* vertex_bones_sizes,
    int face_bones_count,
    int** face_bones,
    int* face_bones_sizes);

#endif
