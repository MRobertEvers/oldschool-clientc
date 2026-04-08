This plan outlines a Linked-Pool Sparse Model. The core philosophy is to move from a "Fat Tile" (where every tile allocates for the worst case) to a "Lean Tile" that uses a side-buffer for complex scenery and implicit calculations for spatial data.

1. Context
The current PaintersTile occupies ~64 bytes. In a 104x104x4 grid (43,264 tiles), this totals ~2.77 MB. However, most tiles in a typical OSRS-style map contain 0–1 scenery elements. By reserving 10 slots per tile, you are effectively "wasting" 90% of your allocated scenery memory. Furthermore, storing sx and sz is redundant as they are encoded in the tile's position within the tiles array.

2. Scope of Changes
A. The "Lean" Struct
We will compress the struct by:

Removing Coordinates: sx and sz are calculated via pointer arithmetic.

Bit-Packing Levels: slevel and terrain_slevel (0–3) only need 2 bits each.

Scenery Pooling: Replacing the scenery[10] array with a single int32_t head index.

B. The Scenery Pool
A global SceneryNode array will handle tiles with multiple objects. This is essentially a custom allocator that lives inside the Painter struct.

C. Logic Shifts
Coordinate Lookup: Functions like painter_tile_at remain the same, but internal logic that needs the tile's X/Z will now derive it from the pointer.

Scenery Iteration: The for(int i=0; i < scenery_count; i++) loops will change to while(node_idx != -1).

3. Diagram of Changes
Memory Layout Shift
Old Model (Static):
[Tile 0: Wall|Decor|Sce0|Sce1|...|Sce9][Tile 1: Wall|Decor|Sce0|...|Sce9]

Result: Huge blocks of empty padding for every tile.

New Model (Pooled):
[Tile 0: Wall|HeadIdx] ----> [Pool Index: Sce0 | NextIdx] ----> [Pool Index: Sce1 | -1]

Result: Tile array is thin and cache-friendly. Extra memory is only consumed when scenery is actually added.

4. TODO List
Phase 1: Data Structure Refactor
[ ] Define the Scenery Pool Node:

C
struct SceneryNode {
    int16_t element_idx;
    uint8_t span;
    int32_t next; // Index of next node, or -1
};
[ ] Redefine PaintersTile:

C
struct PaintersTile {
    int16_t wall_a, wall_b;
    int16_t wall_decor_a, wall_decor_b;
    int16_t ground_decor;
    int16_t ground_obj_bottom, ground_obj_middle, ground_obj_top;

    int32_t bridge_tile;
    int32_t scenery_head; // Initialized to -1

    uint16_t packed_meta; // Bits 0-2: slevel, 3-5: terrain_slevel, 6+: flags
    uint8_t spans;        // Combined spans
};
[ ] Add Pool to Painter: Add struct SceneryNode* scenery_pool and int scenery_pool_count/capacity to the Painter struct.

Phase 2: Implementation of Logic
[ ] Update Coordinate Helpers: Implement a macro or inline function to get X, Z, and grid level from a tile pointer.

C
#define TILE_IDX(p, t) ((int)(t - (p)->tiles))
#define TILE_X(p, t) (TILE_IDX(p, t) % (p)->width)
[ ] Rewrite compute_normal_scenery_spans: Change this to allocate a node from the scenery_pool and link it to the tile's scenery_head.

[ ] Update painter_paint: Refactor the scenery drawing loop to traverse the linked list instead of the fixed array.

Phase 3: Optimization & Cleanup
[ ] Bit-masking: Implement helper functions tile_get_slevel(tile) and tile_set_slevel(tile, val) to handle the packed_meta field.

[ ] Memory Profiling: Verify the new footprint. (The tile array should now be ~24–28 bytes per tile, roughly a 60% reduction).

How many scenery elements do you typically have across the entire 104x104 map—does it usually stay under a few thousand total?