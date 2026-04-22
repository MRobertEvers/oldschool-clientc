#ifndef UITREE_GRID_H
#define UITREE_GRID_H

#include <stdbool.h>
#include <stdint.h>

#define UI_GRID_TILE_SIZE 16
#define UI_GRID_W ((4096 + UI_GRID_TILE_SIZE - 1) / UI_GRID_TILE_SIZE)
#define UI_GRID_H ((4096 + UI_GRID_TILE_SIZE - 1) / UI_GRID_TILE_SIZE)

/** One cell of the 16x16-pixel spatial grid; holds indices of overlapping components. */
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

/**
 * Dirty prepass: set is_dirty on every component before tree traversal.
 * Always-live types (WORLD, MINIMAP, COMPASS) and always_dirty nodes are unconditionally marked.
 * When force is false the two-pass grid algorithm is used:
 *   1. For each element in the mouse tile, mark all tiles that element spans as tile-dirty.
 *   2. Mark all elements in any tile-dirty tile as is_dirty.
 */
void
uitree_grid_dirty_prepass(
    struct UITree* tree,
    int mouse_x,
    int mouse_y,
    bool force);

#endif /* UITREE_GRID_H */
