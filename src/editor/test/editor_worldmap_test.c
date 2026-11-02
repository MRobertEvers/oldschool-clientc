/*
 * The square picker's world map, its pure halves:
 *
 *   mapping   game/rs_worldmap_overview.h: a compositemap record's tiles,
 *             read from its destination bake, land on the right pixels of a
 *             picture of a zoom's global pixel space -- whole regions and
 *             8x8 chunks, north up, the picture's edges, the box filter
 *             below one pixel a tile, sharp blocks above it, real zooms, the
 *             48 px a square the owner saw blank, the grid agreeing with the
 *             map
 *   picture   editor/editor_worldmap.h Editor_WorldMapCompose: a square the
 *             page does not show is dimmed, nothing on the map is black, the
 *             grid when asked, the south row at the bottom
 *
 * The polling, the cache key and the click that opens New square are
 * editor_panels_test's (test_worldmap), against a fake App.
 */

#include "editor/editor_worldmap.h"
#include "engine/torirs_types.h"
#include "game/rs_worldmap_overview.h"
#include "mem/torirs_mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks;
static int g_failed;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if( !(cond) )                                                                              \
        {                                                                                          \
            g_failed++;                                                                            \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while( 0 )

static uint32_t g_bake[64 * 64];
static uint32_t g_out[128 * 128];
static uint32_t g_acc[128 * 128 * 4];

/** A bake whose every pixel names its own row and column. */
static void
bake_rows_cols(int scale)
{
    for( int r = 0; r < 64; r++ )
        for( int c = 0; c < 64; c++ )
            g_bake[r * 64 + c] = 0xFF000000u | ((uint32_t)(r * scale) << 8) | (uint32_t)(c * scale);
}

/** The picture of `zoom`'s global pixel space whose top-left is square
 *  (map_x, map_z)'s north-west corner. */
static void
tile_at(
    struct RS_WorldMapOverviewTile* tile,
    double zoom,
    int map_x,
    int map_z,
    int size)
{
    memset(tile, 0, sizeof(*tile));
    tile->pixels_per_square = zoom;
    tile->pixel_x0 = Editor_PickerEdge(zoom, map_x);
    tile->pixel_y0 = Editor_PickerEdge(zoom, EDITOR_PICKER_WORLD_SQUARES - 1 - map_z);
    tile->width = size;
    tile->height = size;
}

static void
draw(
    const struct RS_WorldMapOverviewTile* tile,
    const struct ToriRS_WorldMapRegionSource* source)
{
    size_t const n = (size_t)tile->width * (size_t)tile->height;
    memset(g_acc, 0, n * 4 * sizeof(uint32_t));
    RS_WorldMapOverview_AccumulateTile(tile, source, g_bake, g_acc);
    RS_WorldMapOverview_ResolveTile(tile, g_acc, g_out);
}

static int
count_drawn(
    const struct RS_WorldMapOverviewTile* tile)
{
    int drawn = 0;
    for( int i = 0; i < tile->width * tile->height; i++ )
        drawn += g_out[i] != 0;
    return drawn;
}

static void
test_mapping(void)
{
    struct RS_WorldMapOverviewTile tile;
    struct RS_WorldMapOverviewWindow window;
    struct ToriRS_WorldMapRegionSource source;

    /* A whole region: square (50, 50) shown from region (10, 20). */
    memset(&source, 0, sizeof(source));
    source.kind = 0;
    source.src_region_x = 50;
    source.src_region_y = 50;
    source.dst_region_x = 10;
    source.dst_region_y = 20;
    memset(&window, 0, sizeof(window));
    window.square_x0 = 50;
    window.square_z0 = 50;
    window.squares_w = 1;
    window.squares_h = 1;
    CHECK(RS_WorldMapOverview_SourceInWindow(&window, &source), "the record's square is in the window");
    window.square_x0 = 51;
    CHECK(!RS_WorldMapOverview_SourceInWindow(&window, &source), "a window east of it misses it");

    /* 64 px a square: one pixel a tile, north up. */
    bake_rows_cols(1);
    tile_at(&tile, 64.0, 50, 50, 64);
    draw(&tile, &source);
    CHECK(g_out[0] == g_bake[0], "64 px a square: the north-west tile is the bake's (0x%08x)", g_out[0]);
    CHECK(g_out[63 * 64] == g_bake[63 * 64], "...the south-west tile too (0x%08x)", g_out[63 * 64]);
    CHECK(g_out[17 * 64 + 40] == g_bake[17 * 64 + 40], "...and any tile");

    /* The box filter: 32 px a square averages 2x2 tiles. */
    bake_rows_cols(2);
    tile_at(&tile, 32.0, 50, 50, 32);
    draw(&tile, &source);
    CHECK(g_out[0] == 0xFF000101u, "32 px a square: pixel (0, 0) is the mean of rows 0-1, cols 0-1 (0x%08x)",
        g_out[0]);
    CHECK(g_out[31 * 32 + 31] == (0xFF000000u | (125u << 8) | 125u),
        "...the last is the mean of rows 62-63, cols 62-63 (0x%08x)", g_out[31 * 32 + 31]);

    /* 48 px a square, the zoom the owner saw blank: a 128 px picture from
     * the square's corner holds the square (48 x 48 drawn) and nothing of
     * the empty squares east and south of it. */
    bake_rows_cols(1);
    tile_at(&tile, 48.0, 50, 50, 128);
    draw(&tile, &source);
    CHECK(count_drawn(&tile) == 48 * 48, "48 px a square: the region is drawn, 48 x 48 (%d pixels)",
        count_drawn(&tile));
    CHECK(g_out[0] != 0 && g_out[47 * 128 + 47] != 0 && g_out[48] == 0 && g_out[48 * 128] == 0,
        "...exactly over its square");

    /* A picture around it: 3x3 squares at 4 px; the record is the middle. */
    tile_at(&tile, 4.0, 49, 51, 12);
    draw(&tile, &source);
    CHECK(g_out[0] == 0, "the north-west square has no record: nothing (0x%08x)", g_out[0]);
    CHECK(g_out[4 * 12 + 4] != 0 && g_out[7 * 12 + 7] != 0, "the middle square is drawn");
    CHECK(g_out[8 * 12 + 4] == 0, "the square south of it is not: z grows up the picture");
    CHECK(g_out[4 * 12 + 8] == 0, "nor the one east of it");

    /* Past one pixel a tile, sharp: 512 px a square is 8x8 px a tile. */
    tile_at(&tile, 512.0, 50, 50, 128);
    draw(&tile, &source);
    CHECK(g_out[0] == g_bake[0] && g_out[7] == g_bake[0] && g_out[8] == g_bake[1] && g_out[127] == g_bake[15],
        "512 px a square: each tile a sharp 8 px block (0x%08x 0x%08x)", g_out[8], g_bake[1]);
    CHECK(g_out[8 * 128] == g_bake[64] && g_out[127 * 128 + 127] == g_bake[15 * 64 + 15],
        "...down the rows too");

    /* Close up from a close-up bake: 128 px a square from a 2 px a tile
     * bake is that bake pixel for pixel (a wall a 2 px line, not 2 tiles). */
    {
        static uint32_t big[128 * 128];
        for( int r = 0; r < 128; r++ )
            for( int c = 0; c < 128; c++ )
                big[r * 128 + c] = 0xFF000000u | ((uint32_t)r << 8) | (uint32_t)c;
        tile_at(&tile, 128.0, 50, 50, 128);
        memset(g_acc, 0, sizeof(g_acc));
        RS_WorldMapOverview_AccumulateTileScaled(&tile, &source, big, 2, g_acc);
        RS_WorldMapOverview_ResolveTile(&tile, g_acc, g_out);
        CHECK(g_out[0] == big[0] && g_out[1] == big[1] && g_out[2] == big[2] && g_out[127 * 128 + 127] == big[127 * 128 + 127],
            "128 px a square from a 2 px a tile bake: pixel for pixel (0x%08x 0x%08x)", g_out[1], big[1]);
        CHECK(RS_WorldMapOverview_BakeScale(48.0) == 1 && RS_WorldMapOverview_BakeScale(64.0) == 1
                  && RS_WorldMapOverview_BakeScale(65.0) == 2 && RS_WorldMapOverview_BakeScale(256.0) == 4
                  && RS_WorldMapOverview_BakeScale(3000.0) == 8,
            "the close-up bake is ceil(px / 64) px a tile, 1..8");
    }

    /* A real zoom: every pixel of the square drawn, the grid's edges are the
     * map's. */
    {
        double const zooms[4] = { 7.3, 48.0, 113.7, 331.25 };
        int agree = 1;
        for( int z = 0; z < 4; z++ )
            for( int m = 0; m < 256; m++ )
                if( RS_WorldMapOverview_TilePixel(zooms[z], m * 64) != Editor_PickerEdge(zooms[z], m) )
                    agree = 0;
        CHECK(agree, "the map's square edges are the picker's at every zoom");
        tile_at(&tile, 113.7, 50, 50, 128);
        draw(&tile, &source);
        CHECK(count_drawn(&tile)
                  == (Editor_PickerEdge(113.7, 51) - Editor_PickerEdge(113.7, 50))
                         * (Editor_PickerEdge(113.7, 206) - Editor_PickerEdge(113.7, 205)),
            "113.7 px a square: exactly the square's cell is drawn (%d)", count_drawn(&tile));
    }

    /* A chunk: chunk (1, 2) of square (50, 50) shown from chunk (3, 4) of
     * region (5, 5). */
    memset(&source, 0, sizeof(source));
    source.kind = 1;
    source.src_region_x = 50;
    source.src_region_y = 50;
    source.src_chunk_x = 1;
    source.src_chunk_y = 2;
    source.dst_region_x = 5;
    source.dst_region_y = 5;
    source.dst_chunk_x = 3;
    source.dst_chunk_y = 4;
    bake_rows_cols(1);
    tile_at(&tile, 64.0, 50, 50, 64);
    draw(&tile, &source);
    /* Tile x 8..15, z 16..23 of the square: picture columns 8..15, rows
     * 63-23 .. 63-16 = 40..47. Picture (row 40, col 8) is tile z 23 = chunk
     * row 7, destination tile z 4*8+7 = 39, bake row 63-39 = 24, bake column
     * 3*8 = 24. */
    CHECK(g_out[40 * 64 + 8] == g_bake[24 * 64 + 24], "a chunk lands on its own 8x8 (0x%08x, want 0x%08x)",
        g_out[40 * 64 + 8], g_bake[24 * 64 + 24]);
    CHECK(g_out[47 * 64 + 15] == g_bake[31 * 64 + 31], "...to its south-east tile");
    CHECK(g_out[39 * 64 + 8] == 0 && g_out[40 * 64 + 7] == 0 && g_out[48 * 64 + 8] == 0
              && g_out[40 * 64 + 16] == 0,
        "...and nowhere else");
}

static void
test_compose(void)
{
    uint32_t pixels[16 * 16];
    uint32_t shown[4];

    /* 2x2 squares at 8 px from global (0, 0): squares x 0..1, north rows
     * 0..1; the north-west one shown, the rest not; one pixel of the
     * north-east square has no map. */
    for( int i = 0; i < 16 * 16; i++ )
        pixels[i] = 0xFF808080u;
    pixels[3 * 16 + 11] = 0;
    pixels[11 * 16 + 4] = 0;
    shown[0] = 0xFF123456u;
    shown[1] = 0;
    shown[2] = 0;
    shown[3] = 0;
    Editor_WorldMapComposeTile(pixels, 16, 8.0, 0, 0, 1, 0, 0, 2, 2, shown);
    CHECK(pixels[3 * 16 + 3] == 0xFF808080u, "a shown square keeps the map (0x%08x)", pixels[3 * 16 + 3]);
    CHECK(pixels[0 * 16 + 3] == 0xFF5F5F5Fu, "...its top row is the grid (0x%08x)", pixels[3]);
    CHECK(pixels[3 * 16 + 8] == 0xFF252525u, "the square east of it is dimmed, its grid too (0x%08x)",
        pixels[3 * 16 + 8]);
    CHECK(pixels[3 * 16 + 9] == 0xFF323232u, "...dimmed to 100/256 (0x%08x)", pixels[3 * 16 + 9]);
    CHECK(pixels[3 * 16 + 11] == 0xFF000000u, "nothing on the map is black (0x%08x)", pixels[3 * 16 + 11]);
    CHECK(pixels[11 * 16 + 3] == 0xFF323232u, "the square south of it is not shown: dimmed");
    pixels[3 * 16 + 4] = 0;
    for( int i = 0; i < 16 * 16; i++ )
        if( i != 3 * 16 + 4 )
            pixels[i] = 0xFF808080u;
    Editor_WorldMapComposeTile(pixels, 16, 8.0, 0, 0, 1, 0, 0, 2, 2, shown);
    CHECK(pixels[3 * 16 + 4] == 0xFF123456u, "a shown square no map covers keeps its own colour (0x%08x)",
        pixels[3 * 16 + 4]);

    for( int i = 0; i < 16 * 16; i++ )
        pixels[i] = 0xFF808080u;
    Editor_WorldMapComposeTile(pixels, 16, 8.0, 0, 0, 0, 0, 0, 0, 0, NULL);
    CHECK(pixels[0] == 0xFF808080u && pixels[8] == 0xFF808080u,
        "no grid, no page: the square edges keep the map");

    CHECK(Editor_WorldMapFade(40.0, 40.0, 64.0) == 255 && Editor_WorldMapFade(64.0, 40.0, 64.0) == 0
              && Editor_WorldMapFade(52.0, 40.0, 64.0) == 127,
        "a fade from 40 to 64 px a square (%d at 52)", Editor_WorldMapFade(52.0, 40.0, 64.0));
}

int
main(void)
{
    ToriMem_Init();
    test_mapping();
    test_compose();
    printf("editor_worldmap_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
