#ifndef SRC_GAME_RS_WORLDMAP_SURFACE_H
#define SRC_GAME_RS_WORLDMAP_SURFACE_H

/*
 * Everything the world map surface keeps for the length of a session: this
 * frame's blits and the regions behind them, the overview pane's blit, the
 * box the emit walk last drew the surface into, and when the heat image was
 * last drawn and for what.
 *
 * The box is where every screen question about the map starts. A click is
 * turned into a map position from the box's centre, which shows the view's
 * display position, at the live zoom scale with screen y running opposite to
 * map y -- the inverse of how the regions were placed. A press inside the box
 * belongs to the map and never reaches the world behind it.
 *
 * The heat image (SET_HEATMAP_ENABLED) is redrawn at most every 50 cycles, and
 * sooner only when what it was drawn for moves: the box's size, the centre,
 * or the size a region is drawn at. A heat image held past any of those is
 * drawn in the wrong place.
 *
 * Nothing here reads a cache, the CS2 world map state, a scene or a clock.
 * The caller passes the cycle, the display position and the zoom scale, builds
 * and uploads every sprite, owns the baked-region renderer it hands in, and
 * says whether the surface is mounted and shown at all -- the box outlives a
 * map that has closed.
 */

#include "game/rs_worldmap_drag.h"
#include "game/rs_worldmap_view.h"
#include "ui/uitree_worldmap_tile.h"

#include <stdbool.h>

struct RS_WorldMapRender;

/** What a heat image was drawn for. */
struct RS_WorldMapHeatmapKey
{
    int box_w;
    int box_h;
    int centre_x;
    int centre_y;
    int region_px;
};

struct RS_WorldMapSurface
{
    /* Per-frame blits, filled by the GET_WORLDMAP_TILES host request and
     * consumed by the same frame's draw: the visible regions first, then every
     * map element icon over them. */
    struct RS_WorldMapTiles tiles;
    /** SET_HEATMAP_ENABLED (deob Statics.field3246), the cycle the last heat
     *  image was drawn on (-1 = never since it was switched) and what it was
     *  drawn for. */
    bool heatmap_on;
    int heatmap_built_cycle;
    struct RS_WorldMapHeatmapKey heatmap_built;
    /* Visible regions for this frame, ordered nearest-the-view-centre first —
     * the order decides who gets the frame's bake and load allowance. */
    struct RS_WorldMapVisits visits;
    /** Overview pane blit (clientCode 1401): one scaled compositetexture. */
    struct UITreeWorldMapTile overview_tile;
    /** Scene id of the uploaded overview texture; 0 until first needed, -1 if
     *  the last upload failed. Replaced when the current area changes. */
    int overview_scene_id;
    /** Area id whose compositetexture is currently in the overview scene slot;
     *  -1 when none. */
    int overview_area_id;
    /** Baked map-surface regions (game/rs_worldmap_render.h). Borrowed from
     *  the caller, which made it and frees it. */
    struct RS_WorldMapRender* render;
    /* Scene id of the synthesised flash marker drawn behind a flashing icon;
     * 0 until first needed, -1 if it could not be built. */
    int flash_scene_id;
    /** Surface box and drag-to-pan state. */
    struct UIWorldMapDrag drag;
    /** Frames the surface has built; the debug overrides key off it. */
    int debug_frame;
};

/** Initial state: no blits, no box, no overview, heat image off, `render`
 *  held for the caller. */
void
RS_WorldMapSurface_Init(
    struct RS_WorldMapSurface* surface,
    struct RS_WorldMapRender* render);

/** Hand the renderer back for the caller to free; the surface holds none. */
struct RS_WorldMapRender*
RS_WorldMapSurface_TakeRender(struct RS_WorldMapSurface* surface);

/** SET_HEATMAP_ENABLED: switch the heat image and draw it afresh when next on. */
void
RS_WorldMapSurface_SetHeatmap(
    struct RS_WorldMapSurface* surface,
    bool on);

/** The logout reset: the heat image is off. */
void
RS_WorldMapSurface_HeatmapOff(struct RS_WorldMapSurface* surface);

bool
RS_WorldMapSurface_HeatmapOn(struct RS_WorldMapSurface const* surface);

/**
 * True when the heat image must be drawn again this cycle: never drawn since
 * it was switched, 50 cycles or more old, or drawn for another key.
 */
bool
RS_WorldMapSurface_HeatmapStale(
    struct RS_WorldMapSurface const* surface,
    struct RS_WorldMapHeatmapKey const* key,
    int cycle);

/** Record that the heat image was drawn on `cycle` for `key`. */
void
RS_WorldMapSurface_HeatmapBuilt(
    struct RS_WorldMapSurface* surface,
    struct RS_WorldMapHeatmapKey const* key,
    int cycle);

/** Record the box this frame's emit walk drew the surface into. */
void
RS_WorldMapSurface_SetBox(
    struct RS_WorldMapSurface* surface,
    int x,
    int y,
    int w,
    int h);

/** True when the point is inside a box with area. */
bool
RS_WorldMapSurface_BoxContains(
    struct RS_WorldMapSurface const* surface,
    int x,
    int y);

/**
 * The map display position under a screen point.
 *
 * `display_x`/`display_y` are what the box centre shows and `scale_fp` the
 * live zoom in RS_WORLDMAP_ZOOM_SCALE_ONE-ths of a pixel per tile, above 0.
 */
void
RS_WorldMapSurface_ScreenToDisplay(
    struct RS_WorldMapSurface const* surface,
    int display_x,
    int display_y,
    int scale_fp,
    int screen_x,
    int screen_y,
    int* out_map_x,
    int* out_map_y);

#endif
