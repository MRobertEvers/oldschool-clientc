#ifndef SRC_GAME_RS_WORLDMAP_VIEW_H
#define SRC_GAME_RS_WORLDMAP_VIEW_H

/*
 * Which regions of the world map are on screen, and in what order.
 *
 * The SET is geometry and the ORDER is a budget, and they fail differently.
 *
 * The set has to reach one region past the edge of the box in every direction,
 * because a region is only ever partly visible at the boundary: draw exactly
 * what the box covers and the map ends in a hard line one region short of the
 * frame, with the background showing through the strip between. It also has to
 * stop at the area's own bounds, because a region outside them has no terrain
 * to bake and asking for one spends the frame's allowance on nothing.
 *
 * The order is the part nobody would guess. A frame bakes and loads only so
 * much, and whichever region is visited first gets it. Scan order -- top-left
 * onwards -- spends that allowance on whatever happens to be scanned first, so
 * the region the view is centred on can wait behind a whole screenful of edge
 * regions. What that looks like is a map that fills in from the corner while
 * the place you are actually looking at stays blank, and only loads once you
 * have panned past it. Nearest-the-centre first is the fix, and it is the
 * reference's own order.
 *
 * Ties are broken by region rather than left to the sort, because the order
 * decides what gets baked: an unstable order re-picks a different region every
 * frame at the same distance, and at a steady zoom the same two regions take
 * turns being the one that never finishes.
 *
 * Nothing here reads a cache, a provider or a scene. The caller says where the
 * view is and how big a region is drawn; this says which ones to ask for.
 */

#include "ui/uitree_worldmap_tile.h"

#include <stdbool.h>

enum
{
    /**
     * Tiles along each side of one map region.
     *
     * Stated here rather than reached for, so this file can be built and
     * tested against nothing but itself. app_worldmap.u.c sees both this and
     * the world's own WORLD_MAP_TERRAIN_X/Z and checks at compile time that
     * they still agree -- a drift would put every region on the map at the
     * wrong offset, which reads as a map subtly out of register with its own
     * icons.
     */
    RS_WORLDMAP_REGION_TILES_X = 64,
    RS_WORLDMAP_REGION_TILES_Z = 64,
    /** Room for one frame's regions. The lowest zoom over the whole map
     *  surface stays well inside this. */
    RS_WORLDMAP_VISITS_MAX = 512,
    /** Room for one frame's blits: the visible regions, then every map
     *  element icon over them. A full-screen surface at the densest zoom is
     *  about thirty regions and a few hundred icons. */
    RS_WORLDMAP_TILES_MAX = 512,
};

/** One region to draw this frame, with how far from the view centre it is. */
struct RS_WorldMapVisit
{
    int region_x;
    int region_y;
    /** Squared tile distance from the view centre. Squared because it is only
     *  ever compared, and a square root would cost a rounding for nothing. */
    int distance;
};

/** The area's own extent, in regions, inclusive at both ends. */
struct RS_WorldMapRegionBounds
{
    int low_x;
    int low_y;
    int high_x;
    int high_y;
};

/** Where the view is, in the area's tile coordinates and screen pixels. */
struct RS_WorldMapViewport
{
    /** Tile the centre of the box is looking at. */
    int display_tile_x;
    int display_tile_z;
    /** The widget box, in screen pixels. */
    int box_w;
    int box_h;
    /** How many screen pixels one whole region is drawn as. */
    int region_px;
};

struct RS_WorldMapVisits
{
    struct RS_WorldMapVisit items[RS_WORLDMAP_VISITS_MAX];
    int count;
};

/** One frame's blits. Refilled from scratch every frame. */
struct RS_WorldMapTiles
{
    struct UITreeWorldMapTile items[RS_WORLDMAP_TILES_MAX];
    int count;
};

/**
 * Which of two regions is visited first: negative for `lhs`, positive for `rhs`.
 *
 * Public because it IS the rule, and because the sort cannot be made to
 * exercise all of it. Regions are generated in scan order, which already
 * agrees with the tie-break, so no input can make a stable sort disagree with
 * an unstable one -- and the standard library's sort is not required to be
 * either. The tie-break is what makes the answer the same both ways, so it is
 * checked here rather than through a sort that happens to be stable today.
 */
int
RS_WorldMapVisit_Compare(struct RS_WorldMapVisit const* lhs, struct RS_WorldMapVisit const* rhs);

/**
 * Fill `out` with every region the view can see, nearest the centre first.
 *
 * Returns the number of regions. Zero when the viewport is degenerate -- a box
 * with no area, or a zoom that draws a region as no pixels at all -- which is
 * a real state during a layout that has not been sized yet, not a caller bug.
 */
int
RS_WorldMapVisits_Build(
    struct RS_WorldMapVisits* out,
    struct RS_WorldMapRegionBounds const* bounds,
    struct RS_WorldMapViewport const* viewport);

/** Begin a frame's blits. */
void
RS_WorldMapTiles_Reset(struct RS_WorldMapTiles* tiles);

/**
 * Claim the next blit slot, already blank.
 *
 * NULL when the frame is full. Blank for the same reason the minimap's dots
 * are: the array outlives the frame, and a slot handed back carrying its last
 * occupant's `scaled` flag stretches a sprite nobody asked to stretch.
 */
struct UITreeWorldMapTile*
RS_WorldMapTiles_Push(struct RS_WorldMapTiles* tiles);

#endif
