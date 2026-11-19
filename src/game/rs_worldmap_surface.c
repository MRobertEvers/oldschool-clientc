#include "game/rs_worldmap_surface.h"

#include "game/rs_worldmap.h"

#include <assert.h>
#include <string.h>

void
RS_WorldMapSurface_Init(
    struct RS_WorldMapSurface* surface,
    struct RS_WorldMapRender* render)
{
    assert(surface);
    memset(surface, 0, sizeof(*surface));
    surface->render = render;
    surface->overview_scene_id = 0;
    surface->overview_area_id = -1;
}

struct RS_WorldMapRender*
RS_WorldMapSurface_TakeRender(struct RS_WorldMapSurface* surface)
{
    struct RS_WorldMapRender* render;

    assert(surface);
    render = surface->render;
    surface->render = NULL;
    return render;
}

void
RS_WorldMapSurface_SetHeatmap(
    struct RS_WorldMapSurface* surface,
    bool on)
{
    assert(surface);
    surface->heatmap_on = on;
    surface->heatmap_built_cycle = -1;
}

void
RS_WorldMapSurface_HeatmapOff(struct RS_WorldMapSurface* surface)
{
    assert(surface);
    surface->heatmap_on = false;
}

bool
RS_WorldMapSurface_HeatmapOn(struct RS_WorldMapSurface const* surface)
{
    assert(surface);
    return surface->heatmap_on;
}

bool
RS_WorldMapSurface_HeatmapStale(
    struct RS_WorldMapSurface const* surface,
    struct RS_WorldMapHeatmapKey const* key,
    int cycle)
{
    struct RS_WorldMapHeatmapKey const* built;

    assert(surface);
    assert(key);
    built = &surface->heatmap_built;
    return surface->heatmap_built_cycle < 0 || cycle - surface->heatmap_built_cycle >= 50 ||
           built->box_w != key->box_w || built->box_h != key->box_h ||
           built->centre_x != key->centre_x || built->centre_y != key->centre_y ||
           built->region_px != key->region_px;
}

void
RS_WorldMapSurface_HeatmapBuilt(
    struct RS_WorldMapSurface* surface,
    struct RS_WorldMapHeatmapKey const* key,
    int cycle)
{
    assert(surface);
    assert(key);
    surface->heatmap_built_cycle = cycle;
    surface->heatmap_built = *key;
}

void
RS_WorldMapSurface_SetBox(
    struct RS_WorldMapSurface* surface,
    int x,
    int y,
    int w,
    int h)
{
    assert(surface);
    surface->drag.box_x = x;
    surface->drag.box_y = y;
    surface->drag.box_w = w;
    surface->drag.box_h = h;
}

bool
RS_WorldMapSurface_BoxContains(
    struct RS_WorldMapSurface const* surface,
    int x,
    int y)
{
    struct UIWorldMapDrag const* box;

    assert(surface);
    box = &surface->drag;
    return box->box_w > 0 && box->box_h > 0 && x >= box->box_x && x < box->box_x + box->box_w &&
           y >= box->box_y && y < box->box_y + box->box_h;
}

void
RS_WorldMapSurface_ScreenToDisplay(
    struct RS_WorldMapSurface const* surface,
    int display_x,
    int display_y,
    int scale_fp,
    int screen_x,
    int screen_y,
    int* out_map_x,
    int* out_map_y)
{
    struct UIWorldMapDrag const* box;

    assert(surface);
    assert(scale_fp > 0);
    assert(out_map_x);
    assert(out_map_y);
    box = &surface->drag;
    *out_map_x =
        display_x + (screen_x - (box->box_x + box->box_w / 2)) * RS_WORLDMAP_ZOOM_SCALE_ONE / scale_fp;
    *out_map_y =
        display_y - (screen_y - (box->box_y + box->box_h / 2)) * RS_WORLDMAP_ZOOM_SCALE_ONE / scale_fp;
}
