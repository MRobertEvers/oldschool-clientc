#ifndef FRAMEMAP_H
#define FRAMEMAP_H

#include "buffer.h"

/**
 * A framemap is a rigging/skeletons for a set of frames.
 * "bone_groups" is a list of skeletons (i.e. a list of a list of bones)
 *
 * A frame contains a list of parameters for how to transform each bone in each skeleton.
 */
struct FramemapDefinition
{
    int id;
    int* types;
    int** bone_groups;        // Set of bone groups
    int* bone_groups_lengths; // Length of each bone group
    int length;
};

struct Cache;
struct FramemapDefinition* framemap_new_from_cache(struct Cache* cache, int framemap_id);

/**
 * Frame archives store the framemap id in the first 2 bytes.
 *
 * @param data
 * @param data_size
 * @return int
 */
int framemap_id_from_frame_archive(char* data, int data_size);

struct FramemapDefinition* framemap_new_decode(int id, struct Buffer* buffer);
struct FramemapDefinition* framemap_new_decode2(int id, char* data, int data_size);
void framemap_free(struct FramemapDefinition* framemap);

#endif // FRAMEMAP_H
