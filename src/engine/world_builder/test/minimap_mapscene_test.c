/*
 * Plotting a loc's mapscene icon into the baked minimap.
 *
 * Three coordinate conventions meet in one expression here, and getting any of
 * them wrong puts the icon on a plausible wrong tile rather than failing --
 * which on a minimap is a bank symbol over the building next door.
 *
 *   - minimap y grows SOUTH while scene z grows north, so the row is
 *     (height - z), not z.
 *   - a loc extends north over its length, so a multi-tile building's icon
 *     belongs at (height - z - (length - 1)) and not at (height - z). This is
 *     the one that is invisible on every 1x1 loc and wrong on every larger one.
 *   - the icon is centred in the footprint and then shifted by the sprite's
 *     own crop origin, because a cache sprite is stored cropped to its ink.
 *
 * What is asserted:
 *
 *   - a 1x1 loc's icon lands on its own tile, with the south-to-north flip.
 *   - a 3x3 loc's icon is centred on the footprint, which is the case the
 *     length term exists for.
 *   - the crop origin shifts the icon, and does so on top of the centring.
 *   - zero-alpha source pixels are skipped rather than painted black. Palette
 *     index 0 in the cache's Pix8 is transparent, so a plot that ignores alpha
 *     paints a black square around every icon.
 *   - every edge clips, including an icon whose origin is off the buffer
 *     entirely, and nothing is written outside the destination.
 *
 * Build and run:
 *   make -C src test-minimap-mapscene
 */

#include "minimap.h"

#include <stdio.h>
#include <string.h>

/* Four pixels per tile, as the bake places them. */
#define TILE 4
#define MAP_W 16
#define MAP_H 16
#define DEST_W (MAP_W * TILE)
#define DEST_H (MAP_H * TILE)

#define OPAQUE 0xFF112233u
#define CLEAR 0x00445566u

static int g_failures;

#define CHECK(condition, ...)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if( !(condition) )                                                                         \
        {                                                                                          \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/*
 * The destination, plus a GUARD BAND after it.
 *
 * The clip tests below drive the plot at coordinates exactly one past each
 * edge. A clip that is off by one then writes into the guard instead of
 * crashing, and the test can say so -- without it, relaxing `>=` to `>` passes
 * every assertion while corrupting whatever follows the buffer.
 */
#define GUARD_ROWS 4
static uint32_t g_destination[DEST_W * (DEST_H + GUARD_ROWS)];

static void
clear_destination(void)
{
    memset(g_destination, 0, sizeof(g_destination));
}

/* Nothing may be written at or past DEST_H, nor past column DEST_W - 1 of any
 * row -- which in a flat buffer is the first column of the row after. */
static int
guard_is_clean(void)
{
    for( int i = DEST_W * DEST_H; i < DEST_W * (DEST_H + GUARD_ROWS); i++ )
        if( g_destination[i] != 0 )
            return 0;
    return 1;
}

static uint32_t
pixel_at(
    int x,
    int y)
{
    if( x < 0 || y < 0 || x >= DEST_W || y >= DEST_H )
        return 0;
    return g_destination[(size_t)y * DEST_W + x];
}

static int
painted_count(void)
{
    int count = 0;

    for( int i = 0; i < DEST_W * DEST_H; i++ )
        if( g_destination[i] != 0 )
            count++;
    return count;
}

/* A 4x4 fully opaque icon: exactly one tile. */
static uint32_t g_icon_4x4[16];

static void
fill_icon(void)
{
    for( int i = 0; i < 16; i++ )
        g_icon_4x4[i] = OPAQUE;
}

static void
test_a_single_tile_icon_lands_on_its_tile(void)
{
    clear_destination();
    fill_icon();

    /* Tile (2, 3) of a 16-tile-high map. Minimap y grows south, so the row is
     * (16 - 3) * 4 = 52 and the column is 2 * 4 = 8. */
    minimap_plot_mapscene(
        g_destination, DEST_W, DEST_H, g_icon_4x4, 4, 4, 0, 0, 2, 3, MAP_H, 1, 1);

    CHECK(pixel_at(8, 52) == OPAQUE, "the icon's top-left is not at 8,52");
    CHECK(pixel_at(11, 55) == OPAQUE, "the icon's bottom-right is not at 11,55");
    CHECK(pixel_at(7, 52) == 0, "the icon spilled one column west");
    CHECK(pixel_at(8, 51) == 0, "the icon spilled one row north");
    CHECK(painted_count() == 16, "painted %d pixels, want exactly 16", painted_count());
}

static void
test_a_multi_tile_loc_centres_its_icon(void)
{
    clear_destination();
    fill_icon();

    /*
     * A 3x3 building whose south-west tile is (5, 5). Its footprint is 12x12
     * pixels and the icon is 4x4, so it is centred with a 4-pixel margin.
     *
     * The row is (16 - 5 - 2) * 4 = 36 -- NOT (16 - 5) * 4 = 44. Dropping the
     * length term puts the icon two tiles south of the building, which is the
     * bug this term exists for and which never shows on a 1x1 loc.
     */
    minimap_plot_mapscene(
        g_destination, DEST_W, DEST_H, g_icon_4x4, 4, 4, 0, 0, 5, 5, MAP_H, 3, 3);

    CHECK(pixel_at(5 * TILE + 4, 36 + 4) == OPAQUE, "the 3x3 icon is not centred at 24,40");
    CHECK(painted_count() == 16, "painted %d pixels, want 16", painted_count());
    CHECK(pixel_at(5 * TILE + 4, 44 + 4) == 0, "the icon landed where the length term is ignored");
}

static void
test_the_crop_origin_shifts_it(void)
{
    clear_destination();
    fill_icon();

    /* Same 1x1 placement as the first test, plus a crop origin. A cache sprite
     * is stored cropped to its ink with the offset recorded, so the offset has
     * to be added on top of the centring rather than instead of it. */
    minimap_plot_mapscene(
        g_destination, DEST_W, DEST_H, g_icon_4x4, 4, 4, 2, -1, 2, 3, MAP_H, 1, 1);

    CHECK(pixel_at(8 + 2, 52 - 1) == OPAQUE, "the crop origin did not shift the icon");
    CHECK(pixel_at(8, 52) == 0, "the icon is still at the unshifted position");
}

static void
test_transparent_pixels_are_skipped(void)
{
    uint32_t icon[16];

    clear_destination();
    for( int i = 0; i < 16; i++ )
        icon[i] = (i % 2) ? OPAQUE : CLEAR;

    minimap_plot_mapscene(g_destination, DEST_W, DEST_H, icon, 4, 4, 0, 0, 2, 3, MAP_H, 1, 1);

    CHECK(
        painted_count() == 8,
        "painted %d pixels, want 8; zero-alpha source pixels must be skipped, "
        "or every icon gets a black square around it",
        painted_count());
    CHECK(pixel_at(8, 52) == 0, "a transparent source pixel was painted");
    CHECK(pixel_at(9, 52) == OPAQUE, "an opaque source pixel was skipped");
}

static void
test_every_edge_clips(void)
{
    /*
     * The west edge. Tile (0, 15) with a crop origin of (-2, -2) puts the
     * icon's origin at x = -2, y = 2 -- so two of its four columns are off the
     * buffer and all four rows are on it. Eight pixels survive, and they start
     * at column 0.
     *
     * The numbers are worked out rather than guessed: the first draft said
     * four, on the assumption that both axes clipped, and the test said
     * otherwise. Worth keeping the reasoning next to the answer.
     */
    clear_destination();
    fill_icon();
    minimap_plot_mapscene(
        g_destination, DEST_W, DEST_H, g_icon_4x4, 4, 4, -2, -2, 0, MAP_H - 1, MAP_H, 1, 1);
    CHECK(painted_count() == 8, "clipped at the west edge painted %d, want 8", painted_count());
    CHECK(pixel_at(0, 2) == OPAQUE, "the surviving half does not start at column 0");
    CHECK(pixel_at(0, 1) == 0, "the icon painted above its own top row");

    /* South-east: past the last tile in both axes. */
    clear_destination();
    fill_icon();
    minimap_plot_mapscene(
        g_destination, DEST_W, DEST_H, g_icon_4x4, 4, 4, 2, 2, MAP_W - 1, 0, MAP_H, 1, 1);
    CHECK(painted_count() < 16, "the south-east icon was not clipped at all");

    /* Entirely off the buffer: nothing is written and nothing is read. */
    clear_destination();
    fill_icon();
    minimap_plot_mapscene(
        g_destination, DEST_W, DEST_H, g_icon_4x4, 4, 4, 0, 0, -50, -50, MAP_H, 1, 1);
    CHECK(painted_count() == 0, "an off-buffer icon painted %d pixels", painted_count());

    clear_destination();
    fill_icon();
    minimap_plot_mapscene(
        g_destination, DEST_W, DEST_H, g_icon_4x4, 4, 4, 0, 0, 500, 500, MAP_H, 1, 1);
    CHECK(painted_count() == 0, "an icon far past the buffer painted %d pixels", painted_count());

    /*
     * Exactly one row past the bottom. Tile z = 0 puts the icon's first row at
     * (16 - 0) * 4 = 64, which IS the height -- the first row that does not
     * exist. A clip testing `>` instead of `>=` writes it, and lands in the
     * guard band rather than failing anywhere visible.
     */
    clear_destination();
    fill_icon();
    minimap_plot_mapscene(
        g_destination, DEST_W, DEST_H, g_icon_4x4, 4, 4, 0, 0, 2, 0, MAP_H, 1, 1);
    CHECK(painted_count() == 0, "the row at exactly DEST_H was painted");
    CHECK(guard_is_clean(), "the bottom clip is off by one: it wrote past the buffer");

    /*
     * And exactly one column past the right. Tile x = 16 puts the icon's first
     * column at 64, which is the width. In a flat buffer that is column 0 of
     * the NEXT row, so an off-by-one here paints a stray pixel on the row
     * below rather than crashing.
     */
    clear_destination();
    fill_icon();
    minimap_plot_mapscene(
        g_destination, DEST_W, DEST_H, g_icon_4x4, 4, 4, 0, 0, MAP_W, 8, MAP_H, 1, 1);
    CHECK(painted_count() == 0, "the column at exactly DEST_W was painted");
    CHECK(guard_is_clean(), "the right clip wrote past the buffer");
}

int
main(void)
{
    test_a_single_tile_icon_lands_on_its_tile();
    test_a_multi_tile_loc_centres_its_icon();
    test_the_crop_origin_shifts_it();
    test_transparent_pixels_are_skipped();
    test_every_edge_clips();

    if( g_failures )
    {
        printf("minimap_mapscene_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("minimap_mapscene_test: OK\n");
    return 0;
}
