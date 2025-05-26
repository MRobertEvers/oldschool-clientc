#ifndef FRAME_H
#define FRAME_H

#include "buffer.h"
#include "framemap.h"

#include <stdbool.h>

/**
 * @brief A frame is a set of parameters for how to transform each bone in each skeleton.
 *
 */
struct FrameDefinition
{
    int id;
    // This is the rigging for the frame.
    int framemap_id;
    int translator_count;
    int* index_frame_ids;
    int* translator_arg_x;
    int* translator_arg_y;
    int* translator_arg_z;
    bool showing;
};

struct FrameDefinition*
frame_new_decode(int id, struct FramemapDefinition* framemap, struct Buffer* buffer);
void frame_free(struct FrameDefinition* frame);

#endif // FRAME_H