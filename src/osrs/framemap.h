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

void decode_framemap(struct FramemapDefinition* def, int id, struct Buffer* buffer);
void free_framemap(struct FramemapDefinition* def);

#endif // FRAMEMAP_H
