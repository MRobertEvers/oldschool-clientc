#ifndef FRAMEMAP_H
#define FRAMEMAP_H

#include "buffer.h"

struct FramemapDefinition
{
    int id;                    // ID of the framemap
    int* types;                // Array of animation types for each vertex group
    int** vertex_groups;       // 2D array of frame maps for each vertex group
    int* vertex_group_lengths; // Array of lengths of frame maps for each vertex group
    int length;                // Number of vertex groups
};

void decode_framemap(struct FramemapDefinition* def, int id, struct Buffer* buffer);
void free_framemap(struct FramemapDefinition* def);

#endif // FRAMEMAP_H
