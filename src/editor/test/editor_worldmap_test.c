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
 *   detail    edpanel_picker.h Editor_PickerLodAt: every band of the
 *             design's table, and the picture each draws through the real
 *             chrome from a synthetic atlas (no content tree): colours and
 *             the page outline under 12, the picture and a lane's inset to
 *             40, outlines / spawn dots / the hover label to 96, icons /
 *             names / loc counts / the zone grid from 96, labels
 *             ellipsised to their cell
 *   budget    EDITOR_PICKER_ITEMS_BUDGET held at every zoom of a crowded
 *             atlas; decoration dropped before essentials, the selection
 *             kept, every drop counted and said
 *   instance  the Instance space page, its bounds, the hatching inside the
 *             grid, its square filled over it
 *   loads     visible first (the middle half of a jump, then the window,
 *             then the rest of the page a slice a tick, nearest first, a slow
 *             slice waiting out its excess), one poll a tick, nothing below
 *             12 px a square, the cold open measured
 *
 * The cache key and the click that opens New square are editor_panels_test's
 * (test_worldmap), against a fake App.
 */

#include "editor/editor_worldmap.h"
#include "engine/torirs_types.h"
#include "game/rs_worldmap_overview.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped_catalog.h"
#include "ui/uitree_debug_overlay.h"

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

/* ---- the picker, content-free: a synthetic atlas, the real chrome ------------------ */

static struct ToriRSChrome g_chrome;
static int g_graph = -1;

/** An atlas of squares [x0, x1] x [z0, z1] in maps/, the surface claiming
 *  them, every one `spawns` spawns and `locs` locs; plus, when `lane`, one
 *  lane square at (50, 49) and one instance-space square at (5, 120).
 *  Static storage: the picker copies it. */
static struct EditorHost_CatalogSquare g_rows[96 * 96 + 4];
static int g_row_areas[1] = { 0 };
static const char* const g_storages[2] = { "maps", "ported/mylane/maps" };
static const struct EditorHost_CatalogArea g_areas[1] = { { "main", "Gielinor Surface", 1 } };

static void
atlas_build(
    struct EditorHost_CatalogSquares* atlas,
    int x0,
    int x1,
    int z0,
    int z1,
    int spawns,
    int locs,
    int lane)
{
    int n = 0;
    memset(atlas, 0, sizeof(*atlas));
    for( int z = z0; z <= z1; z++ )
        for( int x = x0; x <= x1; x++ )
        {
            struct EditorHost_CatalogSquare* row = &g_rows[n++];
            memset(row, 0, sizeof(*row));
            row->map_x = x;
            row->map_z = z;
            row->storage = 0;
            row->flags = TORIRSMAPED_SQUARE_HAS_JM2 | TORIRSMAPED_SQUARE_HAS_JL2;
            row->spawn_count = spawns;
            row->loc_count = locs;
            row->rgb = 0x406020;
            row->primary_area = 0;
            row->area_count = 1;
            row->areas = g_row_areas;
        }
    if( lane )
    {
        struct EditorHost_CatalogSquare* row = &g_rows[n++];
        memset(row, 0, sizeof(*row));
        row->map_x = 50;
        row->map_z = 49;
        row->storage = 1;
        row->rgb = 0x204060;
        row->primary_area = 0;
        row->area_count = 1;
        row->areas = g_row_areas;
        row = &g_rows[n++];
        memset(row, 0, sizeof(*row));
        row->map_x = 5;
        row->map_z = 120;
        row->storage = 0;
        row->flags = TORIRSMAPED_SQUARE_INSTANCE;
        row->rgb = 0x605040;
        row->primary_area = -1;
    }
    atlas->storage_count = 2;
    atlas->storages = g_storages;
    atlas->area_count = 1;
    atlas->areas = g_areas;
    atlas->square_count = n;
    atlas->squares = g_rows;
}

static void
chrome_up(void)
{
    int panel;
    if( g_graph >= 0 )
        return;
    ToriRSChrome_Init(&g_chrome);
    ToriRSChrome_SetSurface(&g_chrome, 4096, 3072);
    panel = ToriRSChrome_PanelAdd(&g_chrome, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 1200, "Squares");
    g_graph = ToriRSChrome_Graph(&g_chrome, panel, NULL, 800);
}

/** Items of `kind` the last draw listed; with `color`, of that colour only
 *  (`any_color` 1 takes every colour). */
static int
items_of(
    int kind,
    int any_color,
    uint32_t color)
{
    const struct ToriRSChromeWidget* w = &g_chrome.widgets[g_graph];
    int n = 0;
    for( int i = 0; i < w->graph_count; i++ )
    {
        const struct ToriRSChromeGraphItem* it = &g_chrome.graph_items[w->graph_first + i];
        if( it->kind == kind && (any_color || it->color == color) )
            n++;
    }
    return n;
}

/** The first TEXT item of the last draw starting with `prefix`, or NULL. */
static const char*
text_starting(const char* prefix)
{
    const struct ToriRSChromeWidget* w = &g_chrome.widgets[g_graph];
    for( int i = 0; i < w->graph_count; i++ )
    {
        const struct ToriRSChromeGraphItem* it = &g_chrome.graph_items[w->graph_first + i];
        if( it->kind == TORIRS_CHROME_GRAPH_TEXT && strncmp(it->text, prefix, strlen(prefix)) == 0 )
            return it->text;
    }
    return NULL;
}

/** Whether the last draw holds a TEXT item reading `text`. */
static int
text_drawn(const char* text)
{
    const struct ToriRSChromeWidget* w = &g_chrome.widgets[g_graph];
    for( int i = 0; i < w->graph_count; i++ )
    {
        const struct ToriRSChromeGraphItem* it = &g_chrome.graph_items[w->graph_first + i];
        if( it->kind == TORIRS_CHROME_GRAPH_TEXT && strcmp(it->text, text) == 0 )
            return 1;
    }
    return 0;
}

static int
draw_at(
    struct Editor_Picker* picker,
    const struct Editor_PickerColors* colors,
    double zoom,
    int well_w,
    int well_h)
{
    picker->well_w = well_w;
    picker->well_h = well_h;
    picker->zoom = zoom;
    Editor_PickerCentre(picker, 50, 50);
    return Editor_PickerDraw(picker, &g_chrome, g_graph, well_w, well_h, colors, NULL, 0, 0);
}

/** An underlay as the world map leaves it: one picture, one icon, one name. */
static void
underlay_fake(struct Editor_Picker* picker)
{
    memset(&picker->underlay, 0, sizeof(picker->underlay));
    picker->underlay.active = 1;
    picker->underlay.count = 1;
    picker->underlay.items[0].scene_id = 4001;
    picker->underlay.items[0].x = EDITOR_PICKER_LEFT;
    picker->underlay.items[0].y = EDITOR_PICKER_TOP;
    picker->underlay.items[0].w = 128;
    picker->underlay.items[0].h = 128;
    picker->underlay.icon_count = 1;
    picker->underlay.icons[0].scene_id = 4100;
    picker->underlay.icons[0].x = 100;
    picker->underlay.icons[0].y = 100;
    picker->underlay.icons[0].w = 15;
    picker->underlay.icons[0].h = 15;
    picker->underlay.label_count = 1;
    picker->underlay.labels[0].x = 120;
    picker->underlay.labels[0].y = 120;
    snprintf(picker->underlay.labels[0].text, sizeof(picker->underlay.labels[0].text), "Lumbridge");
}

static void
test_lod_table(void)
{
    struct Editor_PickerLod lod;
    static const struct
    {
        double zoom;
        int band;
    } k_edges[] = {
        { 0.25, EDITOR_PICKER_LOD_COLOUR },  { 11.99, EDITOR_PICKER_LOD_COLOUR },
        { 12.0, EDITOR_PICKER_LOD_PICTURE }, { 39.99, EDITOR_PICKER_LOD_PICTURE },
        { 40.0, EDITOR_PICKER_LOD_DETAIL },  { 60.0, EDITOR_PICKER_LOD_DETAIL },
        { 95.99, EDITOR_PICKER_LOD_DETAIL }, { 96.0, EDITOR_PICKER_LOD_CLOSE },
        { 256.0, EDITOR_PICKER_LOD_CLOSE },
    };

    for( int i = 0; i < (int)(sizeof(k_edges) / sizeof(k_edges[0])); i++ )
    {
        Editor_PickerLodAt(k_edges[i].zoom, &lod);
        CHECK((int)lod.band == k_edges[i].band, "LOD: %.2f px a square is band %d (%d)", k_edges[i].zoom,
            k_edges[i].band, (int)lod.band);
    }

    Editor_PickerLodAt(8.0, &lod);
    CHECK(lod.colour_cells && lod.page_outline && !lod.pictures && !lod.icons && !lod.names && !lod.hover_label
              && !lod.state_outlines && !lod.spawn_dots && !lod.loc_count && !lod.zone_grid && !lod.lane_inset,
        "under 12: the average colour and the page's outline, nothing else");
    Editor_PickerLodAt(24.0, &lod);
    CHECK(!lod.colour_cells && !lod.page_outline && lod.pictures && lod.lane_inset && !lod.icons && !lod.names
              && !lod.hover_label && !lod.state_outlines && !lod.spawn_dots && !lod.loc_count && !lod.zone_grid,
        "12 to 40: the picture (and a lane's inset), no icons, no labels");
    Editor_PickerLodAt(60.0, &lod);
    CHECK(lod.pictures && lod.state_outlines && lod.spawn_dots && lod.hover_label && !lod.icons && !lod.names
              && !lod.loc_count && !lod.zone_grid,
        "40 to 96: picture, open / dirty outlines, spawn dots, the hover label; no icons");
    Editor_PickerLodAt(96.0, &lod);
    CHECK(lod.pictures && lod.state_outlines && lod.spawn_dots && lod.icons && lod.names && lod.loc_count
              && lod.zone_grid,
        "96 and up: icons and names, the loc count, the zone grid, and the rest");
}

static void
test_lod_draw(void)
{
    struct Editor_Picker picker;
    struct Editor_PickerColors colors;
    struct EditorHost_CatalogSquares atlas;
    int dirty;

    chrome_up();
    Editor_PickerDefaultColors(&colors);
    Editor_PickerInit(&picker);
    atlas_build(&atlas, 40, 60, 40, 60, 25, 61, 1);
    Editor_PickerSetAtlas(&picker, &atlas);
    Editor_PickerSetTab(&picker, 0);
    dirty = Editor_PickerFind(&picker, 51, 50, 0);
    picker.squares[dirty].flags |= TORIRSMAPED_SQUARE_DIRTY;
    underlay_fake(&picker);

    /* Under 12: colours, even with a map under the grid; the page's outline. */
    draw_at(&picker, &colors, 8.0, 600, 400);
    CHECK(items_of(TORIRS_CHROME_GRAPH_IMAGE, 1, 0) == 0, "8 px a square: no picture, no icon (%d images)",
        items_of(TORIRS_CHROME_GRAPH_IMAGE, 1, 0));
    CHECK(items_of(TORIRS_CHROME_GRAPH_RECT, 0, 0x406020) > 100, "...the cells are the atlas colour (%d)",
        items_of(TORIRS_CHROME_GRAPH_RECT, 0, 0x406020));
    CHECK(items_of(TORIRS_CHROME_GRAPH_RECT_OUTLINE, 0, colors.text) == 1, "...the page's outline, once (%d)",
        items_of(TORIRS_CHROME_GRAPH_RECT_OUTLINE, 0, colors.text));
    CHECK(items_of(TORIRS_CHROME_GRAPH_RECT_OUTLINE, 0, colors.dirty) == 0, "...no unsaved outline yet");
    CHECK(!text_drawn("Lumbridge"), "...no names");

    /* 12 to 40: the picture is the fill; no icons, no labels. */
    draw_at(&picker, &colors, 24.0, 600, 400);
    CHECK(items_of(TORIRS_CHROME_GRAPH_IMAGE, 1, 0) == 1, "24 px a square: the picture, no icon (%d images)",
        items_of(TORIRS_CHROME_GRAPH_IMAGE, 1, 0));
    CHECK(items_of(TORIRS_CHROME_GRAPH_RECT, 0, 0x406020) == 0, "...no square filled over it");
    CHECK(items_of(TORIRS_CHROME_GRAPH_RECT_OUTLINE, 0, colors.lane) == 1, "...the lane's square: the blue inset");
    CHECK(items_of(TORIRS_CHROME_GRAPH_RECT_OUTLINE, 0, colors.dirty) == 0, "...no unsaved outline below 40");
    CHECK(items_of(TORIRS_CHROME_GRAPH_RECT, 0, colors.spawn) == 0, "...no spawn dots below 40");
    CHECK(!text_drawn("Lumbridge"), "...no names below 96");

    /* 40 to 96: outlines, a dot per ten spawns, the hover label only. */
    picker.hover_valid = 1;
    picker.hover_map_x = 50;
    picker.hover_map_z = 50;
    draw_at(&picker, &colors, 60.0, 600, 400);
    CHECK(items_of(TORIRS_CHROME_GRAPH_IMAGE, 1, 0) == 1, "60 px a square: no icon (the 6.4 photograph check)");
    CHECK(items_of(TORIRS_CHROME_GRAPH_RECT_OUTLINE, 0, colors.dirty) == 1, "...the unsaved square outlined");
    {
        int vx0;
        int vz0;
        int vw;
        int vh;
        int dots = items_of(TORIRS_CHROME_GRAPH_RECT, 0, colors.spawn);
        Editor_PickerVisibleSquares(&picker, &vx0, &vz0, &vw, &vh);
        CHECK(dots > 0 && dots % 2 == 0 && dots <= 2 * vw * vh,
            "...25 spawns: two dots a square (%d dots, %d squares in view)", dots, vw * vh);
    }
    CHECK(text_drawn("m50_50"), "...the hovered cell's coordinates");
    CHECK(!text_drawn("m51_50"), "...and no other cell's");
    CHECK(!text_drawn("61 locs"), "...no loc count below 96");
    CHECK(!picker.truncated && picker.drop_text[0] == '\0', "...everything in the budget (%d items)",
        picker.items_drawn);

    /* 96 and up: icons and names, loc counts, the zone grid. */
    draw_at(&picker, &colors, 128.0, 600, 400);
    CHECK(items_of(TORIRS_CHROME_GRAPH_IMAGE, 1, 0) == 2, "128 px a square: the picture and its icon (%d)",
        items_of(TORIRS_CHROME_GRAPH_IMAGE, 1, 0));
    CHECK(text_drawn("Lumbridge"), "...the map's names");
    CHECK(text_drawn("61 locs"), "...the loc count");
    CHECK(items_of(TORIRS_CHROME_GRAPH_LINE, 0, colors.zone) >= 7 * 2, "...the zone grid (%d lines)",
        items_of(TORIRS_CHROME_GRAPH_LINE, 0, colors.zone));

    /* A label that does not fit its cell is ellipsised, never clipped: at
     * 40 px a square "m150_250" has 32 px. */
    picker.hover_map_x = 150;
    picker.hover_map_z = 250;
    draw_at(&picker, &colors, 40.0, 600, 400);
    picker.zoom = 40.0;
    Editor_PickerCentre(&picker, 150, 250);
    Editor_PickerDraw(&picker, &g_chrome, g_graph, 600, 400, &colors, NULL, 0, 0);
    {
        int const face = g_chrome.theme.font_row;
        const char* label = text_starting("m1");
        int const full = ToriRSChrome_MeasureTextQ(face, 4, "m150_250");
        CHECK(label != NULL, "40 px: the hover label is drawn");
        if( label )
        {
            int const width = ToriRSChrome_MeasureTextQ(face, 4, label);
            size_t const n = strlen(label);
            CHECK(width <= 32, "...and fits its 32 px (%d px, '%s')", width, label);
            CHECK(full <= 32 ? strcmp(label, "m150_250") == 0 : (n > 3 && strcmp(label + n - 3, "...") == 0),
                "...whole when it fits, else ellipsised (%d px whole, '%s')", full, label);
        }
    }
    Editor_PickerFree(&picker);
}

static void
test_budget(void)
{
    struct Editor_Picker picker;
    struct Editor_PickerColors colors;
    struct EditorHost_CatalogSquares atlas;
    static const double k_zooms[] = { 0.25, 2.0, 4.0, 8.0, 11.9, 12.0, 24.0, 39.9, 40.0, 60.0, 95.0, 96.0, 128.0,
        256.0 };
    int selected;

    chrome_up();
    Editor_PickerDefaultColors(&colors);
    Editor_PickerInit(&picker);
    /* 81 x 81 squares, every one crowded: 200 spawns, a thousand locs. */
    atlas_build(&atlas, 10, 90, 10, 90, 200, 1000, 0);
    Editor_PickerSetAtlas(&picker, &atlas);
    Editor_PickerSetTab(&picker, 0);
    selected = Editor_PickerFind(&picker, 50, 50, 0);
    Editor_PickerSelect(&picker, selected, 0);
    picker.hover_valid = 1;
    picker.hover_map_x = 50;
    picker.hover_map_z = 50;

    /* Every zoom, a large well: never past the budget. */
    for( int i = 0; i < (int)(sizeof(k_zooms) / sizeof(k_zooms[0])); i++ )
    {
        int const items = draw_at(&picker, &colors, k_zooms[i], 1800, 1200);
        int const listed = g_chrome.widgets[g_graph].graph_count;
        CHECK(items <= EDITOR_PICKER_ITEMS_BUDGET && listed == items,
            "%.2f px a square: %d items listed, budget %d", k_zooms[i], listed, EDITOR_PICKER_ITEMS_BUDGET);
        CHECK(k_zooms[i] < 2.0 || items_of(TORIRS_CHROME_GRAPH_RECT_OUTLINE, 0, colors.selected) == 1,
            "%.2f px a square: the selection is drawn whatever was dropped", k_zooms[i]);
        CHECK(!picker.truncated || picker.drop_text[0], "%.2f px a square: a drop is never silent", k_zooms[i]);
    }

    /* 40 px a square in a 1800 x 1200 well: 44 x 30 cells of 12 dots each
     * cannot all fit; the dots go, the cells and the outlines stay, and the
     * line says so. */
    draw_at(&picker, &colors, 40.0, 1800, 1200);
    CHECK(picker.truncated && picker.dropped[EDITOR_PICKER_DROP_DOTS] > 0, "40 px: spawn dots dropped (%d)",
        picker.dropped[EDITOR_PICKER_DROP_DOTS]);
    CHECK(picker.dropped[EDITOR_PICKER_DROP_CELLS] == 0 && picker.dropped[EDITOR_PICKER_DROP_OUTLINES] == 0,
        "...and no cell or outline");
    CHECK(strstr(picker.drop_text, "spawn dots") != NULL, "...the log line names them: '%s'", picker.drop_text);
    CHECK(text_starting("m5") != NULL, "...the hover label kept");

    /* 12 px a square in a huge well: more cells than the budget holds. The
     * budget still holds; what went is counted. */
    draw_at(&picker, &colors, 12.0, 3000, 2400);
    CHECK(picker.items_drawn <= EDITOR_PICKER_ITEMS_BUDGET && picker.dropped[EDITOR_PICKER_DROP_CELLS] > 0,
        "12 px, 3000 x 2400: %d items, %d cells dropped", picker.items_drawn,
        picker.dropped[EDITOR_PICKER_DROP_CELLS]);
    CHECK(strstr(picker.drop_text, "cells") != NULL, "...and said: '%s'", picker.drop_text);
    CHECK(items_of(TORIRS_CHROME_GRAPH_RECT_OUTLINE, 0, colors.selected) == 1
              && items_of(TORIRS_CHROME_GRAPH_RECT_OUTLINE, 0, colors.text) >= 1,
        "...the selection and the hover kept their room");

    /* Back to a well that holds it all: the drop is cleared. */
    picker.hover_valid = 0;
    draw_at(&picker, &colors, 24.0, 300, 200);
    CHECK(!picker.truncated && picker.drop_text[0] == '\0', "a small well: nothing dropped ('%s')", picker.drop_text);
    Editor_PickerFree(&picker);
}

static void
test_instance_space(void)
{
    struct Editor_Picker picker;
    struct Editor_PickerColors colors;
    struct EditorHost_CatalogSquares atlas;
    int tab;
    int square;

    chrome_up();
    Editor_PickerDefaultColors(&colors);
    Editor_PickerInit(&picker);
    atlas_build(&atlas, 48, 52, 48, 52, 0, 0, 1);
    Editor_PickerSetAtlas(&picker, &atlas);
    tab = Editor_PickerInstanceTab(&picker);
    CHECK(tab >= 0 && strcmp(picker.tabs[tab].title, "Instance space") == 0,
        "the atlas has an Instance space page (the Area dropdown's entry)");
    CHECK(tab >= 0 && picker.tabs[tab].min_x == TORIRSMAPED_INSTANCE_X0
              && picker.tabs[tab].max_x == TORIRSMAPED_INSTANCE_X1 && picker.tabs[tab].min_z == TORIRSMAPED_INSTANCE_Z0
              && picker.tabs[tab].max_z == TORIRSMAPED_INSTANCE_Z1,
        "...framing the whole space");
    CHECK(tab >= 0 && picker.tabs[tab].count == 1, "...holding its one square (%d)", tab >= 0 ? picker.tabs[tab].count : -1);
    CHECK(strcmp(picker.tabs[picker.tab_count - 1].title, "Unmapped") == 0 && tab == picker.tab_count - 2,
        "...just before Unmapped");
    CHECK(Editor_PickerInInstanceSpace(0, 100) && Editor_PickerInInstanceSpace(19, 255)
              && !Editor_PickerInInstanceSpace(20, 100) && !Editor_PickerInInstanceSpace(0, 99),
        "instance space is 0..19 x 100..255");
    if( tab < 0 )
        return;
    square = Editor_PickerFind(&picker, 5, 120, 0);
    CHECK(square >= 0 && Editor_PickerSquareOnTab(&picker, square, tab), "the instance square is on the page");
    CHECK(!Editor_PickerSquareOnTab(&picker, Editor_PickerFind(&picker, 50, 50, 0), tab),
        "...a surface square is not");

    Editor_PickerSetTab(&picker, tab);
    underlay_fake(&picker);
    picker.zoom = 24.0;
    picker.well_w = 600;
    picker.well_h = 400;
    Editor_PickerCentre(&picker, 8, 120);
    Editor_PickerDraw(&picker, &g_chrome, g_graph, 600, 400, &colors, NULL, 0, 0);
    CHECK(items_of(TORIRS_CHROME_GRAPH_LINE, 0, colors.hatch) > 20, "instance space is hatched (%d lines)",
        items_of(TORIRS_CHROME_GRAPH_LINE, 0, colors.hatch));
    CHECK(items_of(TORIRS_CHROME_GRAPH_RECT, 0, 0x605040) == 1, "...its square filled over the hatching");
    {
        /* Every hatch line stays inside the grid area: nothing drawn over
         * the axis labels. */
        const struct ToriRSChromeWidget* w = &g_chrome.widgets[g_graph];
        int inside = 1;
        for( int i = 0; i < w->graph_count; i++ )
        {
            const struct ToriRSChromeGraphItem* it = &g_chrome.graph_items[w->graph_first + i];
            if( it->kind != TORIRS_CHROME_GRAPH_LINE || it->color != colors.hatch )
                continue;
            if( it->x < EDITOR_PICKER_LEFT || it->y < EDITOR_PICKER_TOP || it->x + it->w < EDITOR_PICKER_LEFT
                || it->y + it->h < EDITOR_PICKER_TOP || it->x >= 600 || it->y >= 400 )
                inside = 0;
        }
        CHECK(inside, "...every hatch line inside the grid");
    }

    /* The surface page shows no hatching. */
    Editor_PickerSetTab(&picker, 0);
    picker.zoom = 24.0;
    Editor_PickerCentre(&picker, 50, 50);
    Editor_PickerDraw(&picker, &g_chrome, g_graph, 600, 400, &colors, NULL, 0, 0);
    CHECK(items_of(TORIRS_CHROME_GRAPH_LINE, 0, colors.hatch) == 0, "the surface page: no hatching");
    Editor_PickerFree(&picker);
}

/* What each band of the map is for: the instance pool, the templates,
 * underground, outlined and named; the surface is not. */
static void
test_uses(void)
{
    struct Editor_Picker picker;
    struct Editor_PickerColors colors;
    struct EditorHost_CatalogSquares atlas;
    const struct ToriRSChromeWidget* w;
    int inside = 1;

    CHECK(Editor_PickerUseOf(50, 50) == EDITOR_PICKER_USE_SURFACE && Editor_PickerUseOf(19, 120) == EDITOR_PICKER_USE_TEMPLATE
              && Editor_PickerUseOf(20, 120) == EDITOR_PICKER_USE_UNDERGROUND
              && Editor_PickerUseOf(99, 255) == EDITOR_PICKER_USE_UNDERGROUND
              && Editor_PickerUseOf(100, 1) == EDITOR_PICKER_USE_POOL && Editor_PickerUseOf(100, 0) == EDITOR_PICKER_USE_SURFACE
              && Editor_PickerUseOf(50, 99) == EDITOR_PICKER_USE_SURFACE,
        "the bands: templates 0..19 x 100..255, underground 20..99 x 100..255, the pool from x 100");
    CHECK(strcmp(Editor_PickerUseName(EDITOR_PICKER_USE_POOL), "instance pool (runtime copies)") == 0
              && Editor_PickerUseName(EDITOR_PICKER_USE_SURFACE)[0] == '\0',
        "the bands' names; the surface has none");

    chrome_up();
    Editor_PickerDefaultColors(&colors);
    Editor_PickerInit(&picker);
    atlas_build(&atlas, 48, 52, 48, 52, 0, 0, 1);
    Editor_PickerSetAtlas(&picker, &atlas);
    underlay_fake(&picker);
    picker.zoom = 12.0;
    picker.well_w = 600;
    picker.well_h = 400;

    /* Where the templates meet underground: both outlines, both names. */
    Editor_PickerCentre(&picker, 20, 120);
    Editor_PickerDraw(&picker, &g_chrome, g_graph, 600, 400, &colors, NULL, 0, 0);
    CHECK(items_of(TORIRS_CHROME_GRAPH_LINE, 0, colors.use_template) > 0, "the templates are outlined (%d lines)",
        items_of(TORIRS_CHROME_GRAPH_LINE, 0, colors.use_template));
    CHECK(items_of(TORIRS_CHROME_GRAPH_LINE, 0, colors.use_underground) > 0, "underground is outlined (%d lines)",
        items_of(TORIRS_CHROME_GRAPH_LINE, 0, colors.use_underground));
    CHECK(text_starting("instance templates") && text_starting("underground"), "and both are named");
    w = &g_chrome.widgets[g_graph];
    for( int i = 0; i < w->graph_count; i++ )
    {
        const struct ToriRSChromeGraphItem* it = &g_chrome.graph_items[w->graph_first + i];
        if( it->kind != TORIRS_CHROME_GRAPH_LINE
            || (it->color != colors.use_template && it->color != colors.use_underground && it->color != colors.use_pool) )
            continue;
        if( it->x < EDITOR_PICKER_LEFT || it->y < EDITOR_PICKER_TOP || it->x >= 600 || it->y >= 400 )
            inside = 0;
    }
    CHECK(inside, "...every outline inside the grid, none along the view's edge");

    /* Zoomed out, the templates (20 squares) are narrower than their name:
     * the short one, so it does not run into underground's. */
    picker.zoom = 4.0;
    Editor_PickerCentre(&picker, 50, 150);
    Editor_PickerDraw(&picker, &g_chrome, g_graph, 600, 400, &colors, NULL, 0, 0);
    CHECK(text_starting("templates") && !text_starting("instance templates") && text_starting("underground"),
        "zoomed out, the templates take their short name");
    picker.zoom = 12.0;

    /* The pool's west edge at x 100. */
    Editor_PickerCentre(&picker, 100, 60);
    Editor_PickerDraw(&picker, &g_chrome, g_graph, 600, 400, &colors, NULL, 0, 0);
    CHECK(items_of(TORIRS_CHROME_GRAPH_LINE, 0, colors.use_pool) > 0 && text_starting("instance pool"),
        "the instance pool is outlined and named");

    /* The surface: none. */
    Editor_PickerCentre(&picker, 50, 50);
    Editor_PickerDraw(&picker, &g_chrome, g_graph, 600, 400, &colors, NULL, 0, 0);
    CHECK(items_of(TORIRS_CHROME_GRAPH_LINE, 0, colors.use_template) == 0
              && items_of(TORIRS_CHROME_GRAPH_LINE, 0, colors.use_underground) == 0
              && items_of(TORIRS_CHROME_GRAPH_LINE, 0, colors.use_pool) == 0,
        "the surface: no band outlines");
    Editor_PickerFree(&picker);
}

/* ---- visible first, against fake services ------------------------------------------ */

#define FAKE_POLLS_MAX 4096

static struct
{
    int count;
    unsigned long tick[FAKE_POLLS_MAX];
    int x0[FAKE_POLLS_MAX];
    int z0[FAKE_POLLS_MAX];
    int w[FAKE_POLLS_MAX];
    int h[FAKE_POLLS_MAX];
    double zoom[FAKE_POLLS_MAX];
    int missing;
    int renders;
    int publishes;
    uint64_t clock_us;
    uint64_t poll_cost_us;
    uint64_t frame_us;
    unsigned long now_tick;
} g_fake;

static int
fake_area(
    void* user,
    const char* name)
{
    (void)user;
    (void)name;
    return 0;
}

static int
fake_poll(
    void* user,
    int area,
    int x0,
    int z0,
    int w,
    int h,
    double zoom,
    int* out_missing)
{
    int const i = g_fake.count;
    (void)user;
    (void)area;
    if( i < FAKE_POLLS_MAX )
    {
        g_fake.tick[i] = g_fake.now_tick;
        g_fake.x0[i] = x0;
        g_fake.z0[i] = z0;
        g_fake.w[i] = w;
        g_fake.h[i] = h;
        g_fake.zoom[i] = zoom;
        g_fake.count++;
    }
    g_fake.clock_us += g_fake.poll_cost_us;
    *out_missing = g_fake.missing;
    return 1;
}

static int
fake_render(
    void* user,
    int area,
    double zoom,
    int gx0,
    int gy0,
    int size,
    uint32_t* out)
{
    (void)user;
    (void)area;
    (void)zoom;
    (void)gx0;
    (void)gy0;
    for( int i = 0; i < size * size; i++ )
        out[i] = 0xFF305020u;
    g_fake.renders++;
    return 1;
}

static int
fake_publish(
    void* user,
    int slot,
    const uint32_t* argb,
    int w,
    int h)
{
    (void)user;
    (void)argb;
    (void)w;
    (void)h;
    g_fake.publishes++;
    return 5000 + slot;
}

static uint64_t
fake_now(void* user)
{
    (void)user;
    return g_fake.clock_us;
}

static uint64_t
fake_frame(void* user)
{
    (void)user;
    return g_fake.frame_us;
}

static void
tick(
    struct Editor_WorldMap* map,
    const struct EditorUiAppServices* services,
    struct Editor_Picker* picker)
{
    g_fake.now_tick++;
    Editor_WorldMapUpdate(map, services, picker);
}

static void
test_pure_slices(void)
{
    static uint32_t slices[EDITOR_WORLDMAP_SLICES * EDITOR_WORLDMAP_SLICES];
    int x0;
    int z0;
    int w;
    int h;
    int first;
    int second;

    Editor_WorldMapCore(40, 30, 16, 12, &x0, &z0, &w, &h);
    CHECK(x0 == 44 && z0 == 33 && w == 8 && h == 6, "the middle half of 40,30 16x12 is 44,33 8x6 (%d,%d %dx%d)",
        x0, z0, w, h);
    Editor_WorldMapCore(10, 10, 1, 3, &x0, &z0, &w, &h);
    CHECK(w == 1 && h == 1 && x0 == 10 && z0 == 11, "...at least a square across (%d,%d %dx%d)", x0, z0, w, h);

    memset(slices, 0, sizeof(slices));
    /* A page 0..63 x 0..63, the view 24..39 x 24..39: the slices wholly in
     * it are skipped, the nearest of the rest first. */
    first = Editor_WorldMapNextSlice(slices, 100, 0, 63, 0, 63, 24, 24, 16, 16);
    CHECK(first >= 0, "a slice is left");
    {
        int const sx = first % EDITOR_WORLDMAP_SLICES;
        int const sz = first / EDITOR_WORLDMAP_SLICES;
        CHECK(sx >= 2 && sx <= 5 && sz >= 2 && sz <= 5 && !(sx >= 3 && sx <= 4 && sz >= 3 && sz <= 4),
            "...the first is a ring slice next to the view (%d, %d)", sx, sz);
    }
    slices[first] = EDITOR_WORLDMAP_SLICE_DONE;
    second = Editor_WorldMapNextSlice(slices, 100, 0, 63, 0, 63, 24, 24, 16, 16);
    CHECK(second >= 0 && second != first, "a done slice is not asked again");
    slices[second] = 95;
    CHECK(Editor_WorldMapNextSlice(slices, 100, 0, 63, 0, 63, 24, 24, 16, 16) != second,
        "a slice asked 5 ticks ago waits");
    CHECK(Editor_WorldMapNextSlice(slices, 95 + EDITOR_WORLDMAP_PREFETCH_RETRY_TICKS, 0, 63, 0, 63, 24, 24, 16, 16)
              == second,
        "...and is asked again after the retry");
    for( int i = 0; i < EDITOR_WORLDMAP_SLICES * EDITOR_WORLDMAP_SLICES; i++ )
        slices[i] = EDITOR_WORLDMAP_SLICE_DONE;
    CHECK(Editor_WorldMapNextSlice(slices, 100, 0, 63, 0, 63, 24, 24, 16, 16) < 0, "all done: none left");
}

static void
test_visible_first(void)
{
    struct Editor_Picker picker;
    struct EditorHost_CatalogSquares atlas;
    struct Editor_WorldMap map;
    struct EditorUiAppServices services;
    int vx0;
    int vz0;
    int vw;
    int vh;
    int before;

    memset(&g_fake, 0, sizeof(g_fake));
    memset(&services, 0, sizeof(services));
    services.worldmap_area = fake_area;
    services.worldmap_poll = fake_poll;
    services.worldmap_render_tile = fake_render;
    services.image_publish = fake_publish;
    services.now_us = fake_now;
    services.last_frame_us = fake_frame;

    Editor_PickerInit(&picker);
    atlas_build(&atlas, 30, 70, 30, 70, 0, 0, 0);
    Editor_PickerSetAtlas(&picker, &atlas);
    Editor_PickerSetTab(&picker, 0);
    picker.well_w = 400;
    picker.well_h = 300;
    picker.zoom = 24.0;
    Editor_PickerCentre(&picker, 50, 50);
    Editor_PickerVisibleSquares(&picker, &vx0, &vz0, &vw, &vh);
    Editor_WorldMapInit(&map);

    /* A cold open: the middle half first, then the whole window. */
    g_fake.missing = 30;
    g_fake.frame_us = 12000;
    tick(&map, &services, &picker);
    CHECK(g_fake.count == 1 && map.core_polls == 1, "a cold open polls once (%d)", g_fake.count);
    CHECK(g_fake.w[0] < vw && g_fake.h[0] < vh && g_fake.x0[0] > vx0 && g_fake.z0[0] > vz0
              && g_fake.x0[0] + g_fake.w[0] < vx0 + vw && g_fake.z0[0] + g_fake.h[0] < vz0 + vh,
        "...the view's middle first ([%d,%d %dx%d] in [%d,%d %dx%d])", g_fake.x0[0], g_fake.z0[0], g_fake.w[0],
        g_fake.h[0], vx0, vz0, vw, vh);
    tick(&map, &services, &picker);
    CHECK(g_fake.count == 2 && g_fake.x0[1] == vx0 && g_fake.z0[1] == vz0 && g_fake.w[1] == vw && g_fake.h[1] == vh,
        "...then the whole window ([%d,%d %dx%d])", g_fake.x0[1], g_fake.z0[1], g_fake.w[1], g_fake.h[1]);
    for( int i = 0; i < 5; i++ )
        tick(&map, &services, &picker);
    CHECK(g_fake.count == 7, "while regions arrive: the view polled every tick, once (%d)", g_fake.count);
    for( int i = 2; i < g_fake.count; i++ )
        CHECK(g_fake.w[i] == vw && g_fake.h[i] == vh, "...the view, poll %d", i);
    CHECK(map.prefetch_polls == 0, "...nothing else is asked for while the view loads");

    /* One slow frame during it, then the view lands. */
    g_fake.frame_us = 41000;
    tick(&map, &services, &picker);
    g_fake.frame_us = 9000;
    g_fake.missing = 0;
    tick(&map, &services, &picker);
    tick(&map, &services, &picker);
    CHECK(!map.cold_active && map.cold_over == 1 && map.cold_slowest_us == 41000,
        "the cold open measured: %d frames, slowest %.1f ms, %d over 33 ms", map.cold_frames,
        (double)map.cold_slowest_us / 1000.0, map.cold_over);

    /* The view is in: the rest of the page, a slice a tick, nearest first,
     * at no more than one pixel a tile. */
    before = g_fake.count;
    g_fake.poll_cost_us = 500;
    for( int i = 0; i < 4; i++ )
        tick(&map, &services, &picker);
    CHECK(map.prefetch_polls >= 3, "the rest of the page follows (%d slices)", map.prefetch_polls);
    {
        int one_a_tick = 1;
        for( int i = before + 1; i < g_fake.count; i++ )
            if( g_fake.tick[i] == g_fake.tick[i - 1] )
                one_a_tick = 0;
        CHECK(one_a_tick, "...one poll a tick, never two");
    }
    CHECK(g_fake.w[before] == EDITOR_WORLDMAP_SLICE && g_fake.h[before] == EDITOR_WORLDMAP_SLICE
              && g_fake.zoom[before] == 24.0,
        "...a %d-square slice at the view's zoom (%dx%d at %.1f)", EDITOR_WORLDMAP_SLICE, g_fake.w[before],
        g_fake.h[before], g_fake.zoom[before]);
    {
        /* The first slice touches the view or its ring. */
        int const x = g_fake.x0[before];
        int const z = g_fake.z0[before];
        CHECK(x + EDITOR_WORLDMAP_SLICE >= vx0 - EDITOR_WORLDMAP_SLICE && x <= vx0 + vw + EDITOR_WORLDMAP_SLICE
                  && z + EDITOR_WORLDMAP_SLICE >= vz0 - EDITOR_WORLDMAP_SLICE
                  && z <= vz0 + vh + EDITOR_WORLDMAP_SLICE,
            "...the first next to the view (%d,%d)", x, z);
    }

    /* A slice over the frame's 2 ms waits its excess out. */
    g_fake.poll_cost_us = 5000;
    tick(&map, &services, &picker);
    before = g_fake.count;
    tick(&map, &services, &picker);
    tick(&map, &services, &picker);
    CHECK(g_fake.count == before, "a 5 ms slice: the next two ticks ask nothing (%d)", g_fake.count - before);
    tick(&map, &services, &picker);
    CHECK(g_fake.count == before + 1, "...then the next slice");
    g_fake.poll_cost_us = 100;

    /* Every slice once, then quiet. */
    for( int i = 0; i < 200; i++ )
        tick(&map, &services, &picker);
    before = g_fake.count;
    for( int i = 0; i < 30; i++ )
        tick(&map, &services, &picker);
    CHECK(g_fake.count == before, "the page in: no more polls (%d)", g_fake.count - before);
    {
        int left = 0;
        for( int sz = 30 / EDITOR_WORLDMAP_SLICE; sz <= 70 / EDITOR_WORLDMAP_SLICE; sz++ )
            for( int sx = 30 / EDITOR_WORLDMAP_SLICE; sx <= 70 / EDITOR_WORLDMAP_SLICE; sx++ )
            {
                int const x = sx * EDITOR_WORLDMAP_SLICE;
                int const z = sz * EDITOR_WORLDMAP_SLICE;
                int const in_view = x >= vx0 && x + EDITOR_WORLDMAP_SLICE <= vx0 + vw && z >= vz0
                                    && z + EDITOR_WORLDMAP_SLICE <= vz0 + vh;
                left += !in_view && map.slices[sx + sz * EDITOR_WORLDMAP_SLICES] != EDITOR_WORLDMAP_SLICE_DONE;
            }
        CHECK(left == 0, "...every slice of the page asked and done (%d left)", left);
    }

    /* A pan: the whole window at once (its edges are all that is new). */
    before = g_fake.count;
    Editor_PickerPan(&picker, -24, 0);
    tick(&map, &services, &picker);
    Editor_PickerVisibleSquares(&picker, &vx0, &vz0, &vw, &vh);
    CHECK(g_fake.count == before + 1 && g_fake.x0[before] == vx0 && g_fake.w[before] == vw,
        "a pan polls the whole new window at once ([%d %d] vs [%d %d])", g_fake.x0[before], g_fake.w[before], vx0, vw);

    /* A jump far away: the middle first again. */
    before = g_fake.count;
    Editor_PickerCentre(&picker, 33, 66);
    tick(&map, &services, &picker);
    Editor_PickerVisibleSquares(&picker, &vx0, &vz0, &vw, &vh);
    CHECK(g_fake.count == before + 1 && g_fake.w[before] < vw && map.core_polls == 2,
        "a jump polls its middle first (%dx%d of %dx%d)", g_fake.w[before], g_fake.h[before], vw, vh);

    /* Below 12 px a square nothing is asked for and nothing is drawn. */
    before = g_fake.count;
    {
        int const renders = g_fake.renders;
        picker.zoom = 8.0;
        for( int i = 0; i < 10; i++ )
            tick(&map, &services, &picker);
        CHECK(g_fake.count == before && g_fake.renders == renders && !picker.underlay.active,
            "8 px a square: no poll, no picture (%d polls, %d renders)", g_fake.count - before,
            g_fake.renders - renders);
    }

    /* Instance space has no area: nothing asked either. */
    picker.zoom = 24.0;
    Editor_PickerSetTab(&picker, Editor_PickerInstanceTab(&picker));
    before = g_fake.count;
    for( int i = 0; i < 5; i++ )
        tick(&map, &services, &picker);
    CHECK(g_fake.count == before && !picker.underlay.active, "Instance space: no map");

    Editor_WorldMapFree(&map);
    Editor_PickerFree(&picker);
}

int
main(void)
{
    ToriMem_Init();
    test_mapping();
    test_compose();
    test_lod_table();
    test_lod_draw();
    test_budget();
    test_instance_space();
    test_uses();
    test_pure_slices();
    test_visible_first();
    printf("editor_worldmap_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
