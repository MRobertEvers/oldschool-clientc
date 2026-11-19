/*
 * The world map surface's session state (game/rs_worldmap_surface.h).
 *
 * The initial state, the renderer handed back, when the heat image is drawn
 * again, the box a press belongs to, and the screen point turned back into a
 * map position.
 *
 * Run: make -C src test-worldmap-surface
 */

#include "game/rs_worldmap_surface.h"

#include "game/rs_worldmap.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define TEST_ASSERT(cond, what)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/* Large: the blit and visit arrays. */
static struct RS_WorldMapSurface g_surface;

static void
test_init_holds_the_renderer_and_nothing_else(void)
{
    struct RS_WorldMapRender* render = (struct RS_WorldMapRender*)&g_failures;

    memset(&g_surface, 0x5a, sizeof(g_surface));
    RS_WorldMapSurface_Init(&g_surface, render);
    TEST_ASSERT(g_surface.render == render, "the renderer is held");
    TEST_ASSERT(g_surface.overview_scene_id == 0, "the overview has not been needed yet");
    TEST_ASSERT(g_surface.overview_area_id == -1, "and names no area");
    TEST_ASSERT(g_surface.flash_scene_id == 0, "the flash marker has not been built");
    TEST_ASSERT(g_surface.tiles.count == 0 && g_surface.visits.count == 0, "no blits, no regions");
    TEST_ASSERT(!RS_WorldMapSurface_HeatmapOn(&g_surface), "the heat image starts off");
    TEST_ASSERT(!RS_WorldMapSurface_BoxContains(&g_surface, 0, 0), "a box with no area holds nothing");

    TEST_ASSERT(RS_WorldMapSurface_TakeRender(&g_surface) == render, "taking hands it back");
    TEST_ASSERT(g_surface.render == NULL, "and the surface holds none after");
}

static void
test_the_heat_image_is_redrawn_only_when_due(void)
{
    struct RS_WorldMapHeatmapKey const key = { 400, 300, 200, 150, 64 };
    struct RS_WorldMapHeatmapKey moved = key;

    RS_WorldMapSurface_Init(&g_surface, NULL);
    RS_WorldMapSurface_SetHeatmap(&g_surface, true);
    TEST_ASSERT(RS_WorldMapSurface_HeatmapOn(&g_surface), "the packet switches it on");
    TEST_ASSERT(RS_WorldMapSurface_HeatmapStale(&g_surface, &key, 1000), "never drawn is due");

    RS_WorldMapSurface_HeatmapBuilt(&g_surface, &key, 1000);
    TEST_ASSERT(!RS_WorldMapSurface_HeatmapStale(&g_surface, &key, 1000), "just drawn is not due");
    TEST_ASSERT(!RS_WorldMapSurface_HeatmapStale(&g_surface, &key, 1049), "49 cycles later is not");
    TEST_ASSERT(RS_WorldMapSurface_HeatmapStale(&g_surface, &key, 1050), "50 cycles later is");

    moved.centre_x++;
    TEST_ASSERT(RS_WorldMapSurface_HeatmapStale(&g_surface, &moved, 1001), "a moved centre is due");
    moved = key;
    moved.region_px = 32;
    TEST_ASSERT(RS_WorldMapSurface_HeatmapStale(&g_surface, &moved, 1001), "a new zoom is due");
    moved = key;
    moved.box_h = 301;
    TEST_ASSERT(RS_WorldMapSurface_HeatmapStale(&g_surface, &moved, 1001), "a resized box is due");

    /* The packet again: whatever was drawn before is not trusted. */
    RS_WorldMapSurface_SetHeatmap(&g_surface, true);
    TEST_ASSERT(RS_WorldMapSurface_HeatmapStale(&g_surface, &key, 1001), "a resend draws afresh");

    RS_WorldMapSurface_HeatmapBuilt(&g_surface, &key, 1001);
    RS_WorldMapSurface_HeatmapOff(&g_surface);
    TEST_ASSERT(!RS_WorldMapSurface_HeatmapOn(&g_surface), "the logout switches it off");
}

static void
test_the_box_is_half_open(void)
{
    RS_WorldMapSurface_Init(&g_surface, NULL);
    RS_WorldMapSurface_SetBox(&g_surface, 10, 20, 100, 50);
    TEST_ASSERT(g_surface.drag.box_x == 10 && g_surface.drag.box_h == 50, "the drag reads the same box");
    TEST_ASSERT(RS_WorldMapSurface_BoxContains(&g_surface, 10, 20), "the top-left pixel is inside");
    TEST_ASSERT(RS_WorldMapSurface_BoxContains(&g_surface, 109, 69), "the bottom-right pixel is inside");
    TEST_ASSERT(!RS_WorldMapSurface_BoxContains(&g_surface, 110, 30), "one past the right edge is not");
    TEST_ASSERT(!RS_WorldMapSurface_BoxContains(&g_surface, 50, 70), "one past the bottom is not");
    TEST_ASSERT(!RS_WorldMapSurface_BoxContains(&g_surface, 9, 30), "one before the left is not");

    RS_WorldMapSurface_SetBox(&g_surface, 10, 20, 0, 50);
    TEST_ASSERT(!RS_WorldMapSurface_BoxContains(&g_surface, 10, 20), "a box with no width holds nothing");
}

static void
test_a_click_is_the_inverse_of_the_placement(void)
{
    int map_x = 0;
    int map_y = 0;

    RS_WorldMapSurface_Init(&g_surface, NULL);
    RS_WorldMapSurface_SetBox(&g_surface, 100, 100, 200, 100);
    /* Centre (200, 150) shows display (3200, 3200). */
    RS_WorldMapSurface_ScreenToDisplay(
        &g_surface, 3200, 3200, RS_WORLDMAP_ZOOM_SCALE_ONE, 200, 150, &map_x, &map_y);
    TEST_ASSERT(map_x == 3200 && map_y == 3200, "the centre is the display position");

    RS_WorldMapSurface_ScreenToDisplay(
        &g_surface, 3200, 3200, RS_WORLDMAP_ZOOM_SCALE_ONE, 210, 140, &map_x, &map_y);
    TEST_ASSERT(map_x == 3210, "right on screen is east on the map");
    TEST_ASSERT(map_y == 3210, "up on screen is north on the map");

    /* Four pixels a tile: ten pixels is two whole tiles. */
    RS_WorldMapSurface_ScreenToDisplay(
        &g_surface, 3200, 3200, RS_WORLDMAP_ZOOM_SCALE_ONE * 4, 210, 160, &map_x, &map_y);
    TEST_ASSERT(map_x == 3202, "the zoom divides the offset");
    TEST_ASSERT(map_y == 3198, "on both axes");
}

int
main(void)
{
    test_init_holds_the_renderer_and_nothing_else();
    test_the_heat_image_is_redrawn_only_when_due();
    test_the_box_is_half_open();
    test_a_click_is_the_inverse_of_the_placement();
    if( g_failures )
    {
        printf("worldmap_surface: %d check(s) failed\n", g_failures);
        return 1;
    }
    printf("worldmap_surface: all checks passed\n");
    return 0;
}
