#ifndef FRAME_H
#define FRAME_H

#include "buffer.h"
#include "framemap.h"

#include <stdbool.h>

/**
 * @brief A frame is a set of parameters for how to transform each bone in each skeleton.
 *
 * Nah. Each animation consists of a list of frames. All those frames belong in the same frame map.
You derive the frame map from the stand/walk animation of the monster, and then find all animations
that contain frames which belong to that same frame map. Ultimately gives you the ability to extract
a lot more than what Armar's provided. I personally extract these fields, although they can be
extended even further. https://gist.github.com/555196afa946955d37d4525a162712ea

Oh and this is also a more up to date version of what the OP provided, as of right now it's the
current OSRS's cache dump.
 */
struct CacheFrame
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

struct Cache;
struct CacheFrame*
frame_new_from_cache(struct Cache* cache, int frame_id, struct CacheFramemap* framemap);

struct CacheFrame*
frame_new_decode2(int frame_id, struct CacheFramemap* framemap, char* data, int data_size);

struct CacheFrame* frame_new_decode(int id, struct CacheFramemap* framemap, struct Buffer* buffer);
void frame_free(struct CacheFrame* frame);

#endif // FRAME_H