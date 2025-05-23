#ifndef FRAMEMAP_H
#define FRAMEMAP_H

#include "buffer.h"

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
