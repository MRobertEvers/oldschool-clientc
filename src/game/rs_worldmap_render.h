#ifndef RS_WORLDMAP_RENDER_H
#define RS_WORLDMAP_RENDER_H

#include <stdbool.h>

struct CacheProvider;
struct ToriDraw_Scene;
struct ToriRS_TaskQueue;
struct ToriRS_WorldMapArea;

/*
 * The world map's map surface: one baked image per 64x64 map-surface region,
 * blitted side by side under the pan/zoom the CS2 world map state holds.
 *
 * Ported from xrsps-typescript's WorldMapArchiveRenderer. Same data path as the
 * reference — table 19's compositemap says which table-18 geography file backs a
 * region, and the tiles in it are already laid out in that region's grid — with
 * two deliberate differences, both noted at their call sites: ground colour
 * comes from the underlay flo rather than table 20's pre-baked image (that is a
 * PNG, and there is no PNG decoder in this build), and map element icons are not
 * plotted yet.
 */

/** Baked-region pool. Regions are baked at one zoom; a zoom change rebakes. */
struct RS_WorldMapRender;

struct RS_WorldMapRender*
RS_WorldMapRender_New(void);

void
RS_WorldMapRender_Free(struct RS_WorldMapRender* render);

/** Call once per frame before any RegionSprite call: resets the per-frame bake
 *  budget that keeps a zoom-out from stalling the client. */
void
RS_WorldMapRender_BeginFrame(struct RS_WorldMapRender* render);

/** Scene id of the "mapscene" sprite pack, from the app's bridge: the landmark
 *  icons (trees, rocks, altars, piers) baked into each region. */
void
RS_WorldMapRender_SetMapScenes(
    struct RS_WorldMapRender* render,
    int mapscene_scene_id);

/** Drop every baked region (area switch, or the tiles behind them changed). */
void
RS_WorldMapRender_Clear(
    struct RS_WorldMapRender* render,
    struct ToriDraw_Scene* scene);

/**
 * Scene sprite id for one map surface region, baking it if needed.
 *
 * Returns -1 when the region's tiles are not resident yet, having queued the
 * load onto `queue` — the caller draws background for it and asks again next
 * frame. `pixels_per_tile` is clamped to 1..8 as in the reference.
 *
 * `out_fallback_scene_id` (optional) receives a bake of the same region at a
 * *different* zoom when this zoom's is not ready. Drawing that one stretched to
 * the destination box is what makes zooming a transition instead of a blink
 * through the background; it is replaced as soon as the right bake lands.
 */
int
RS_WorldMapRender_RegionSprite(
    struct RS_WorldMapRender* render,
    struct CacheProvider* provider,
    struct ToriDraw_Scene* scene,
    struct ToriRS_TaskQueue* queue,
    struct ToriRS_WorldMapArea const* area,
    int region_x,
    int region_y,
    int pixels_per_tile,
    int* out_size,
    int* out_fallback_scene_id);

/** One loc-derived icon inside a baked region (reference buildIcons): the map
 *  element id and where it sits, in tiles from the region's south-west corner. */
struct RS_WorldMapRegionIcon
{
    int element_id;
    int tile_x;
    int tile_y;
};

/**
 * The icons a baked region carries. Most world map icons come from the locs on
 * its tiles (a bank booth becomes the bank icon), not from the compositemap's
 * own list, so they are collected during the bake and read back here.
 *
 * Returns the count and writes a borrowed pointer, valid until the slot is
 * evicted. -1 count when the region is not baked.
 */
int
RS_WorldMapRender_RegionIcons(
    struct RS_WorldMapRender* render,
    int scene_id,
    struct RS_WorldMapRegionIcon const** out_icons);

#endif /* RS_WORLDMAP_RENDER_H */
