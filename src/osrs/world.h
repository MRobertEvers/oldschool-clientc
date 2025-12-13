#ifndef WORLD_H
#define WORLD_H

struct World;

struct WorldTile
{
    // These are only used for normal locs
    int locs[20];
    int locs_count;

    int temporary_locs[20];
    int temporary_locs_count;

    int wall;
    int wall_decor;
    int ground_decor;

    int bridge_tile;

    int ground_object_bottom;
    int ground_object_middle;
    int ground_object_top;

    // Contains directions for which tiles are waiting for us to draw.
    // This is determined by locs that are larger than 1x1.
    // E.g. If a table is 3x1, then the spans for each tile will be:
    // (Assuming the table is going west-east direction)
    // WEST SIDE
    //     SPAN_FLAG_EAST,
    //     SPAN_FLAG_EAST | SPAN_FLAG_WEST,
    //     SPAN_FLAG_WEST
    // EAST SIDE
    //
    // For a 2x2 table, the spans will be:
    // WEST SIDE  <->  EAST SIDE
    //     [SPAN_FLAG_EAST | SPAN_FLAG_SOUTH,    SPAN_FLAG_WEST | SPAN_FLAG_SOUTH]
    //     [SPAN_FLAG_EAST | SPAN_FLAG_NORTH,    SPAN_FLAG_WEST | SPAN_FLAG_NORTH]
    //
    //
    // As the underlays are drawn diagonally inwards from the corner, once each of the
    // underlays is drawn, the loc on top is drawn.
    // The spans are used to determine which tiles are waiting for us to draw.
    int spans;

    int ground;

    int x;
    int z;
    int level;

    int flags;
};

struct WorldElement
{};

#endif