#ifndef SCENE_GROUND_H
#define SCENE_GROUND_H

#include "scene_tile.h"

/**
 * Tells the renderer to defer drawing locs until
 * the underlay is drawn for tiles in the direction
 * of the span.
 */
enum SpanFlag
{
    SPAN_FLAG_WEST = 1 << 0,
    SPAN_FLAG_NORTH = 1 << 1,
    SPAN_FLAG_EAST = 1 << 2,
    SPAN_FLAG_SOUTH = 1 << 3,
};

/**
 *  underlay: TileUnderlay | null = null;
 *  overlay: TileOverlay | null = null;
 *  wall: Wall | null = null;
 *  wallDecoration: Decor | null = null;
 *  groundDecoration: GroundDecor | null = null;
 *  objStack: ObjStack | null = null;
 *  bridge: Ground | null = null;
 *  locCount: number = 0;
 *  locSpans: number = 0;
 *  drawLevel: number = 0;
 *  groundVisible: boolean = false;
 *  update: boolean = false;
 *  containsLocs: boolean = false;
 *  checkLocSpans: number = 0;
 *  blockLocSpans: number = 0;
 *  inverseBlockLocSpans: number = 0;
 *  backWallTypes: number = 0;
 *
 */
struct SceneGround
{
    // The index of the loc in the scene's locs array.
    int locs[20];
    int locs_length;

    // The index of the loc in the scene's locs array.
    int wall;
    int wall_decor;
    int ground_decor;

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

    struct SceneTile tile;

    struct SceneTextures* textures;
    int textures_length;

    int x;
    int z;
    int level;
};

#endif