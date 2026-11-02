#ifndef SRC_GAME_RS_WORLDMAP_OVERVIEW_H
#define SRC_GAME_RS_WORLDMAP_OVERVIEW_H

/*
 * The world map at one pixel per tile, for the map editor's square picker:
 * the same regions the CS2 world map (interface 595) draws, baked by the same
 * renderer (rs_worldmap_render.h RS_WorldMapRender_RegionPixels), kept as
 * 64x64 images with the map element icons their locs carry, and resampled
 * into pictures of WORLD squares at any zoom: box-filtered below one pixel a
 * tile, each tile a sharp block above it.
 *
 * WORLD squares, not map-surface regions: the picker's cells are the content
 * tree's squares (map_x, map_z), and an area's compositemap says which real
 * square (or 8x8 chunk of one) each map-surface region shows -- a dungeon is
 * drawn on the world map far from where it is in the world. So a window here
 * is in the source coordinates, and each record's destination bake is read
 * back through it.
 *
 * Bakes are cached for the process (16 KB a region; the whole surface is
 * about 25 MB) and never evicted: the editor flips between a handful of areas
 * and re-baking one costs its loads again. A bake made before every floor
 * and loc it needed had loaded is baked again, at most
 * RS_WORLDMAP_OVERVIEW_REBAKES times.
 */

#include <stdint.h>

struct CacheProvider;
struct ToriDraw_Scene;
struct ToriRS_TaskQueue;
struct ToriRS_WorldMapArea;
struct ToriRS_WorldMapAreas;
struct ToriRS_WorldMapRegionSource;

/** Bakes of one region at most: the first, then again while incomplete. */
#define RS_WORLDMAP_OVERVIEW_REBAKES 3

struct RS_WorldMapOverview;

/** A window of world squares (what a poll and an icon query cover). */
struct RS_WorldMapOverviewWindow
{
    int square_x0;
    int square_z0;
    int squares_w;
    int squares_h;
};

/**
 * One picture of the map at `pixels_per_square` (any positive real, up past
 * one pixel per tile): the rectangle [pixel_x0, pixel_x0 + width) x
 * [pixel_y0, pixel_y0 + height) of the GLOBAL pixel space of that zoom, where
 * world tile column t covers [floor(t * k), floor((t + 1) * k)) with
 * k = pixels_per_square / 64 (at least one pixel: below one pixel a tile, the
 * tiles sharing a pixel are averaged), and rows count from the north edge of
 * the world (tile z RS_WORLDMAP_OVERVIEW_WORLD_TILES_Z - 1 is row 0). Every
 * picture of one zoom lines up with every other, so a map is drawn as fixed
 * size tiles of this space and a pan renders only the new ones.
 */
struct RS_WorldMapOverviewTile
{
    double pixels_per_square;
    int pixel_x0;
    int pixel_y0;
    int width;
    int height;
};

/** The world's north edge in tiles: row 0 of the global pixel space. */
#define RS_WORLDMAP_OVERVIEW_WORLD_TILES_Z (256 * 64)

/** A map element on the map: its config id and world tile. */
struct RS_WorldMapOverviewIcon
{
    int element_id;
    int tile_x;
    int tile_z;
};

struct RS_WorldMapOverview*
RS_WorldMapOverview_New(void);

void
RS_WorldMapOverview_Free(struct RS_WorldMapOverview* overview);

/**
 * Move the window's regions along: queue what is not loaded onto `queue`,
 * bake what is ready (many per call: a bake at one pixel per tile is cheap).
 * Call once per frame while it reports regions missing. `mapscene_scene_id`
 * is the "mapscene" sprite pack's scene id (-1 none: no landmark icons).
 * Past one pixel a tile (`pixels_per_square` > 64) the window's regions are
 * also baked at RS_WorldMapOverview_BakeScale pixels a tile, as 595 bakes
 * them at its zooms (walls stay lines, shaped floors stay diagonal), in a
 * small pool of close-up bakes (least recently used out, 64 MB at most).
 *
 * Returns the regions in the window not yet baked (or baked before all their
 * assets landed and due another try); 0 when the window is final.
 */
int
RS_WorldMapOverview_Poll(
    struct RS_WorldMapOverview* overview,
    struct CacheProvider* provider,
    struct ToriDraw_Scene* scene,
    struct ToriRS_TaskQueue* queue,
    int mapscene_scene_id,
    struct ToriRS_WorldMapArea const* area,
    struct RS_WorldMapOverviewWindow const* window,
    double pixels_per_square);

/** Bumped by every bake that lands: a picture drawn at one serial looks the
 *  same until it changes. */
int
RS_WorldMapOverview_Serial(struct RS_WorldMapOverview const* overview);

/**
 * Draw one picture from what is baked: width x height pixels into `out`,
 * 0xFFRRGGBB where the map has a tile, 0 where it has nothing (outside the
 * area, or not baked yet). Loads nothing. Its cost is the tiles of the
 * records it overlaps: a picture at a few pixels a square covers many.
 */
void
RS_WorldMapOverview_RenderTile(
    struct RS_WorldMapOverview* overview,
    struct ToriRS_WorldMapArea const* area,
    struct RS_WorldMapOverviewTile const* tile,
    uint32_t* out);

/**
 * The map elements in the window: the icons the baked regions' locs carry
 * (banks, altars, dungeon entrances; what 595 draws over a region) and the
 * area's own compositemap list (labels and loc-less icons), at their WORLD
 * tiles. Returns how many, at most `capacity`.
 */
int
RS_WorldMapOverview_Icons(
    struct RS_WorldMapOverview* overview,
    struct ToriRS_WorldMapArea const* area,
    struct RS_WorldMapOverviewWindow const* window,
    struct RS_WorldMapOverviewIcon* out,
    int capacity);

/* ---- every area at once, on the true square grid (the picker's Full map) --
 *
 * Each area's records name the real squares their regions show, so every
 * area composed onto one grid of WORLD squares is the whole map where it
 * really is: the surface, and each dungeon, cave and instance region at its
 * own coordinates. Areas are drawn the main one first; where two place a
 * region on one square, the first drawn keeps it. */

int
RS_WorldMapOverview_PollAll(
    struct RS_WorldMapOverview* overview,
    struct CacheProvider* provider,
    struct ToriDraw_Scene* scene,
    struct ToriRS_TaskQueue* queue,
    int mapscene_scene_id,
    struct ToriRS_WorldMapAreas const* areas,
    struct RS_WorldMapOverviewWindow const* window,
    double pixels_per_square);

void
RS_WorldMapOverview_RenderTileAll(
    struct RS_WorldMapOverview* overview,
    struct ToriRS_WorldMapAreas const* areas,
    struct RS_WorldMapOverviewTile const* tile,
    uint32_t* out);

int
RS_WorldMapOverview_IconsAll(
    struct RS_WorldMapOverview* overview,
    struct ToriRS_WorldMapAreas const* areas,
    struct RS_WorldMapOverviewWindow const* window,
    struct RS_WorldMapOverviewIcon* out,
    int capacity);

/* ---- the mapping, on its own (editor/test/editor_worldmap_test.c) ---- */

/**
 * Add one compositemap record's tiles to a picture's box filter: every
 * source tile inside the picture adds its destination bake's pixel to the
 * output pixels it covers. `bake` is that record's destination region,
 * 64x64, north up; `acc` holds four uint32 per output pixel (r, g, b, count),
 * row-major, `width` wide.
 */
void
RS_WorldMapOverview_AccumulateTile(
    struct RS_WorldMapOverviewTile const* tile,
    struct ToriRS_WorldMapRegionSource const* source,
    uint32_t const* bake,
    uint32_t* acc);

/** The box filter's answer: each pixel the mean of what fell in it, 0 where
 *  nothing did. */
void
RS_WorldMapOverview_ResolveTile(
    struct RS_WorldMapOverviewTile const* tile,
    uint32_t const* acc,
    uint32_t* out);

/** The close-up bake a zoom draws from: ceil(pixels_per_square / 64)
 *  pixels a tile, 1..8 (595's own range). */
int
RS_WorldMapOverview_BakeScale(double pixels_per_square);

/** RS_WorldMapOverview_AccumulateTile from a close-up bake ((64 * scale)
 *  square, north up): each source tile's pixel span is filled from its
 *  scale x scale block, nearest. */
void
RS_WorldMapOverview_AccumulateTileScaled(
    struct RS_WorldMapOverviewTile const* tile,
    struct ToriRS_WorldMapRegionSource const* source,
    uint32_t const* bake,
    int scale,
    uint32_t* acc);

/** Whether a record's source square is in the window. */
int
RS_WorldMapOverview_SourceInWindow(
    struct RS_WorldMapOverviewWindow const* window,
    struct ToriRS_WorldMapRegionSource const* source);

/** World tile column `t`'s first pixel at `pixels_per_square` (the global
 *  pixel space above): floor(t * pixels_per_square / 64). */
int
RS_WorldMapOverview_TilePixel(
    double pixels_per_square,
    int t);

#endif
