#ifndef SRC_GAME_RS_WORLDMAP_OVERVIEW_H
#define SRC_GAME_RS_WORLDMAP_OVERVIEW_H

/*
 * The world map at one pixel per tile, for the map editor's square picker:
 * the same regions the CS2 world map (interface 595) draws, baked by the same
 * renderer (rs_worldmap_render.h RS_WorldMapRender_RegionPixels), kept as
 * 64x64 images, and resampled into a window of WORLD squares at any whole
 * number of pixels per square up to 64.
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
struct ToriRS_WorldMapRegionSource;

/** The sharpest the window can be: one pixel per tile. */
#define RS_WORLDMAP_OVERVIEW_PPS_MAX 64
/** Bakes of one region at most: the first, then again while incomplete. */
#define RS_WORLDMAP_OVERVIEW_REBAKES 3

struct RS_WorldMapOverview;

/** A window of world squares and how many pixels a square is drawn as.
 *  Output row 0 is the NORTH edge: square z grows up the picture. */
struct RS_WorldMapOverviewWindow
{
    int square_x0;
    int square_z0;
    int squares_w;
    int squares_h;
    /** 1..RS_WORLDMAP_OVERVIEW_PPS_MAX. */
    int pixels_per_square;
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
    struct RS_WorldMapOverviewWindow const* window);

/** Bumped by every bake that lands: a window drawn at one serial looks the
 *  same until it changes. */
int
RS_WorldMapOverview_Serial(struct RS_WorldMapOverview const* overview);

/**
 * Draw the window from what is baked: (squares_w * pps) x (squares_h * pps)
 * pixels into `out`, 0xFFRRGGBB where the map has a tile, 0 where it has
 * nothing (outside the area, or not baked yet). Loads nothing.
 */
void
RS_WorldMapOverview_Render(
    struct RS_WorldMapOverview* overview,
    struct ToriRS_WorldMapArea const* area,
    struct RS_WorldMapOverviewWindow const* window,
    uint32_t* out);

/* ---- the mapping, on its own (game/test/rs_worldmap_overview_test.c) ---- */

/**
 * Add one compositemap record's tiles to the window's box filter: every
 * source tile inside the window adds its destination bake's pixel to the
 * output pixel it falls in. `bake` is that record's destination region,
 * 64x64, north up; `acc` holds four uint32 per output pixel (r, g, b, count),
 * row-major, (squares_w * pps) wide.
 */
void
RS_WorldMapOverview_Accumulate(
    struct RS_WorldMapOverviewWindow const* window,
    struct ToriRS_WorldMapRegionSource const* source,
    uint32_t const* bake,
    uint32_t* acc);

/** The box filter's answer: each pixel the mean of what fell in it, 0 where
 *  nothing did. */
void
RS_WorldMapOverview_Resolve(
    struct RS_WorldMapOverviewWindow const* window,
    uint32_t const* acc,
    uint32_t* out);

/** Whether a record's source tiles touch the window at all. */
int
RS_WorldMapOverview_SourceInWindow(
    struct RS_WorldMapOverviewWindow const* window,
    struct ToriRS_WorldMapRegionSource const* source);

#endif
