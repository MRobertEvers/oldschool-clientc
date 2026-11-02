/*
 * The square picker's world map, its pure halves:
 *
 *   mapping   game/rs_worldmap_overview.h: a compositemap record's tiles,
 *             read from its destination bake, land on the right pixels of a
 *             window of WORLD squares -- whole regions and 8x8 chunks, north
 *             up, the window's edges, the box filter below one pixel a tile
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
static uint32_t g_out[16 * 64 * 64];
static uint32_t g_acc[16 * 64 * 64 * 4];

/** A bake whose every pixel names its own row and column. */
static void
bake_rows_cols(int scale)
{
    for( int r = 0; r < 64; r++ )
        for( int c = 0; c < 64; c++ )
            g_bake[r * 64 + c] = 0xFF000000u | ((uint32_t)(r * scale) << 8) | (uint32_t)(c * scale);
}

static void
draw(
    const struct RS_WorldMapOverviewWindow* window,
    const struct ToriRS_WorldMapRegionSource* source)
{
    size_t const n = (size_t)window->squares_w * window->pixels_per_square * window->squares_h
                     * window->pixels_per_square;
    memset(g_acc, 0, n * 4 * sizeof(uint32_t));
    RS_WorldMapOverview_Accumulate(window, source, g_bake, g_acc);
    RS_WorldMapOverview_Resolve(window, g_acc, g_out);
}

static void
test_mapping(void)
{
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
    window.pixels_per_square = 64;
    bake_rows_cols(1);
    CHECK(RS_WorldMapOverview_SourceInWindow(&window, &source), "the record's square is in the window");
    draw(&window, &source);
    CHECK(g_out[0] == g_bake[0], "64 px a square: the north-west tile is the bake's (0x%08x)", g_out[0]);
    CHECK(g_out[63 * 64] == g_bake[63 * 64], "...the south-west tile too (0x%08x)", g_out[63 * 64]);
    CHECK(g_out[17 * 64 + 40] == g_bake[17 * 64 + 40], "...and any tile: one pixel a tile, north up");

    /* The box filter: 32 px a square averages 2x2 tiles. */
    window.pixels_per_square = 32;
    bake_rows_cols(2);
    draw(&window, &source);
    CHECK(g_out[0] == 0xFF000101u, "32 px a square: pixel (0, 0) is the mean of rows 0-1, cols 0-1 (0x%08x)",
        g_out[0]);
    CHECK(g_out[31 * 32 + 31] == (0xFF000000u | (125u << 8) | 125u),
        "...the last is the mean of rows 62-63, cols 62-63 (0x%08x)", g_out[31 * 32 + 31]);

    /* A window around it: 3x3 squares at 4 px; the record is the middle. */
    window.square_x0 = 49;
    window.square_z0 = 49;
    window.squares_w = 3;
    window.squares_h = 3;
    window.pixels_per_square = 4;
    draw(&window, &source);
    CHECK(g_out[0] == 0, "the north-west square has no record: nothing (0x%08x)", g_out[0]);
    CHECK(g_out[4 * 12 + 4] != 0 && g_out[7 * 12 + 7] != 0, "the middle square is drawn");
    CHECK(g_out[8 * 12 + 4] == 0, "the square south of it is not: z grows up the picture");
    CHECK(g_out[4 * 12 + 8] == 0, "nor the one east of it");

    /* Outside the window: nothing touched. */
    window.square_x0 = 51;
    CHECK(!RS_WorldMapOverview_SourceInWindow(&window, &source), "a window east of the record misses it");
    draw(&window, &source);
    CHECK(g_out[4 * 12 + 4] == 0, "...and draws nothing of it");

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
    window.square_x0 = 50;
    window.square_z0 = 50;
    window.squares_w = 1;
    window.squares_h = 1;
    window.pixels_per_square = 64;
    bake_rows_cols(1);
    draw(&window, &source);
    /* Tile x 8..15, z 16..23 of the square: picture columns 8..15, rows
     * 63-23 .. 63-16 = 40..47. Picture (row 40, col 8) is tile z 23 = chunk
     * row 7, which is destination tile z 4*8+7 = 39, bake row 63-39 = 24,
     * bake column 3*8 = 24. */
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
    uint32_t pixels[2 * 8 * 2 * 8];
    unsigned char shown[4];

    /* 2x2 squares at 8 px: the south-west square shown, the rest not; one
     * pixel of the north-east square has no map. Picture rows 0..7 are the
     * NORTH row (shown[2], shown[3]), rows 8..15 the south (shown[0], [1]). */
    for( int i = 0; i < 2 * 8 * 2 * 8; i++ )
        pixels[i] = 0xFF808080u;
    pixels[3 * 16 + 11] = 0;
    shown[0] = 1;
    shown[1] = 0;
    shown[2] = 0;
    shown[3] = 0;
    Editor_WorldMapCompose(pixels, 2, 2, 8, 1, shown);
    CHECK(pixels[11 * 16 + 3] == 0xFF808080u, "a shown square keeps the map (0x%08x)", pixels[11 * 16 + 3]);
    CHECK(pixels[8 * 16 + 3] == 0xFF5F5F5Fu, "...its top row is the grid (0x%08x)", pixels[8 * 16 + 3]);
    CHECK(pixels[11 * 16 + 8] == 0xFF252525u, "...the square east of it is dimmed, its grid too (0x%08x)",
        pixels[11 * 16 + 8]);
    CHECK(pixels[11 * 16 + 9] == 0xFF323232u, "...dimmed to 100/256 (0x%08x)", pixels[11 * 16 + 9]);
    CHECK(pixels[3 * 16 + 11] == 0xFF000000u, "nothing on the map is black (0x%08x)", pixels[3 * 16 + 11]);
    CHECK(pixels[3 * 16 + 3] == 0xFF323232u, "the north-west square is not shown: dimmed");

    /* No grid asked for: none drawn. */
    for( int i = 0; i < 2 * 4 * 1 * 4; i++ )
        pixels[i] = 0xFF808080u;
    shown[0] = 1;
    shown[1] = 1;
    Editor_WorldMapCompose(pixels, 2, 1, 4, 0, shown);
    CHECK(pixels[0] == 0xFF808080u && pixels[4] == 0xFF808080u, "no grid: the square edges keep the map");
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
