#ifndef FRAME_H
#define FRAME_H

#include "buffer.h"
#include "framemap.h"

#include <stdbool.h>

struct FrameDefinition
{
    int id;
    struct FramemapDefinition* framemap;
    int translator_count;
    int* index_frame_ids;
    int* translator_arg_x;
    int* translator_arg_y;
    int* translator_arg_z;
    bool showing;
};

void decode_frame(
    struct FrameDefinition* def,
    struct FramemapDefinition* framemap,
    int id,
    struct Buffer* buffer);
void free_frame(struct FrameDefinition* def);

#endif // FRAME_H