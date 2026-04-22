#ifndef UITREE_GRID_H
#define UITREE_GRID_H

#include <stdint.h>

#define UI_GRID_TILE_SIZE 50
#define UI_GRID_W ((4096 + UI_GRID_TILE_SIZE - 1) / UI_GRID_TILE_SIZE)
#define UI_GRID_H ((4096 + UI_GRID_TILE_SIZE - 1) / UI_GRID_TILE_SIZE)

/** One cell of the 50x50-pixel spatial grid; holds indices of overlapping components. */
struct UIGridTile
{
    int32_t* indices;
    int count;
    int capacity;
    uint8_t dirty; /* set during the dirty prepass; cleared each frame */
};

struct UITree;

/** Free all tile index arrays in the spatial grid (does not free the tree itself). */
void
uitree_grid_clear(struct UITree* tree);

/** Rebuild the spatial grid from current component bounds (call after tree build). */
void
uitree_rebuild_grid(struct UITree* tree);

#endif /* UITREE_GRID_H */
