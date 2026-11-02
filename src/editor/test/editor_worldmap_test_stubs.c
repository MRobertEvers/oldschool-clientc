/*
 * What editor_worldmap_test links instead of the client: the pure halves it
 * tests (the overview's mapping, the picture's composition) never reach
 * these, so each aborts if one is ever called.
 */

#include "editor/panels/edpanel_picker.h"
#include "game/rs_worldmap_render.h"

#include <stdio.h>
#include <stdlib.h>

#define STUB(name)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        fprintf(stderr, "editor_worldmap_test: %s is a stub\n", name);                            \
        abort();                                                                                   \
    } while( 0 )

struct RS_WorldMapRender*
RS_WorldMapRender_New(void)
{
    STUB("RS_WorldMapRender_New");
}

void
RS_WorldMapRender_Free(struct RS_WorldMapRender* render)
{
    (void)render;
    STUB("RS_WorldMapRender_Free");
}

void
RS_WorldMapRender_BeginFrame(struct RS_WorldMapRender* render)
{
    (void)render;
    STUB("RS_WorldMapRender_BeginFrame");
}

void
RS_WorldMapRender_SetFrameBudget(
    struct RS_WorldMapRender* render,
    int bakes_per_frame,
    int loads_per_frame)
{
    (void)render;
    (void)bakes_per_frame;
    (void)loads_per_frame;
    STUB("RS_WorldMapRender_SetFrameBudget");
}

void
RS_WorldMapRender_SetMapScenes(
    struct RS_WorldMapRender* render,
    int mapscene_scene_id)
{
    (void)render;
    (void)mapscene_scene_id;
    STUB("RS_WorldMapRender_SetMapScenes");
}

int
RS_WorldMapRender_RegionPixels(
    struct RS_WorldMapRender* render,
    struct CacheProvider* provider,
    struct ToriDraw_Scene* scene,
    struct ToriRS_TaskQueue* queue,
    struct ToriRS_WorldMapArea const* area,
    int region_x,
    int region_y,
    int pixels_per_tile,
    uint32_t* out_pixels,
    bool* out_complete,
    struct RS_WorldMapRegionIcon* out_icons,
    int icon_capacity,
    int* out_icon_count)
{
    (void)out_icons;
    (void)icon_capacity;
    (void)out_icon_count;
    (void)render;
    (void)provider;
    (void)scene;
    (void)queue;
    (void)area;
    (void)region_x;
    (void)region_y;
    (void)pixels_per_tile;
    (void)out_pixels;
    (void)out_complete;
    STUB("RS_WorldMapRender_RegionPixels");
}

int
Editor_PickerSquareVisible(
    const struct Editor_Picker* picker,
    int index)
{
    (void)picker;
    (void)index;
    STUB("Editor_PickerSquareVisible");
}

void
Editor_PickerCellOrigin(
    const struct Editor_Picker* picker,
    int map_x,
    int map_z,
    int* out_x,
    int* out_y)
{
    (void)picker;
    (void)map_x;
    (void)map_z;
    (void)out_x;
    (void)out_y;
    STUB("Editor_PickerCellOrigin");
}

void
Editor_PickerGlobalOrigin(
    const struct Editor_Picker* picker,
    int* out_x,
    int* out_y)
{
    (void)picker;
    (void)out_x;
    (void)out_y;
    STUB("Editor_PickerGlobalOrigin");
}

int
Editor_PickerVisibleSquares(
    const struct Editor_Picker* picker,
    int* out_x0,
    int* out_z0,
    int* out_w,
    int* out_h)
{
    (void)picker;
    (void)out_x0;
    (void)out_z0;
    (void)out_w;
    (void)out_h;
    STUB("Editor_PickerVisibleSquares");
}
