/*
 * Region painting's commands against a document (editor/panels/
 * edpanel_region.c): an in-memory document of two empty squares side by side,
 * each command's after-tile written back as the authority would.
 *
 *   overlay    a disc across the squares' border lays the overlay in both,
 *              edge shapes on its rim, nothing outside it; laid twice, the
 *              second changes nothing.
 *   take off   the same disc subtracted leaves the ground bare.
 *   closed     a tile in a square that is not open is left out and counted.
 *   other      another overlay's tile is cut by nobody: only a tile the
 *              region covers whole is replaced.
 *   underlay   the region's middle is painted whole; nothing outside it.
 *   tidy       a stepped run is refitted with edge shapes; a second run of
 *              the same overlay beside it is left as it was.
 *   room       an edit larger than the commands' room says how much it left.
 */

#include "editor/editor_doc.h"
#include "editor/editor_region.h"
#include "editor/panels/edpanel_region.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks;
static int g_failed;

#define CHECK(cond, ...)                                                                                     \
    do                                                                                                       \
    {                                                                                                        \
        g_checks++;                                                                                          \
        if( !(cond) )                                                                                        \
        {                                                                                                    \
            g_failed++;                                                                                      \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                      \
            printf(__VA_ARGS__);                                                                             \
            printf("\n");                                                                                    \
        }                                                                                                    \
    } while( 0 )

#define CMDS_MAX 8192
#define OVERLAY 7
#define OTHER_OVERLAY 9
#define UNDERLAY 33

static struct Editor_Doc g_doc;
static struct Editor_Cmd g_cmds[CMDS_MAX];

/* Squares (50, 50) and (51, 50): world tiles x 3200..3327, z 3200..3263. */
#define X0 3200
#define Z0 3200

static struct Editor_Tile*
tile_at(
    int world_x,
    int world_z)
{
    struct Editor_Square* square =
        Editor_DocFindSquare(&g_doc, world_x / EDITOR_SQUARE_X, world_z / EDITOR_SQUARE_Z);
    if( !square || !square->loaded )
        return NULL;
    return &square->tiles[Editor_TileIndex(world_x % EDITOR_SQUARE_X, world_z % EDITOR_SQUARE_Z, 0)];
}

static void
apply(
    const struct Editor_Cmd* cmds,
    int count)
{
    for( int i = 0; i < count; i++ )
    {
        struct Editor_Square* square = NULL;
        int map_x;
        int map_z;
        if( sscanf(cmds[i].doc_key, "m%d_%d", &map_x, &map_z) == 2 )
            square = Editor_DocFindSquare(&g_doc, map_x, map_z);
        CHECK(square != NULL, "a command lands in an open square (%s)", cmds[i].doc_key);
        CHECK(cmds[i].kind == EDITOR_CMD_TILE, "region edits are TILE commands");
        if( !square )
            continue;
        CHECK(memcmp(&square->tiles[Editor_TileIndex(cmds[i].u.tile.x, cmds[i].u.tile.z, cmds[i].u.tile.level)],
                  &cmds[i].u.tile.before, sizeof(struct Editor_Tile))
                  == 0,
            "a command's before is the tile as it is");
        square->tiles[Editor_TileIndex(cmds[i].u.tile.x, cmds[i].u.tile.z, cmds[i].u.tile.level)] =
            cmds[i].u.tile.after;
    }
}

static void
clear_squares(void)
{
    for( int mx = 50; mx <= 51; mx++ )
    {
        struct Editor_Square* square = Editor_DocFindSquare(&g_doc, mx, 50);
        memset(square->tiles, 0, sizeof(square->tiles));
    }
}

static void
disc_grid(
    struct Editor_RegionGrid* grid,
    double cx,
    double cz,
    double r)
{
    int const ok = Editor_RegionGridInit(grid, (int)floor(cx - r) - 1, (int)floor(cz - r) - 1, (int)ceil(2 * r) + 3,
        (int)ceil(2 * r) + 3);
    CHECK(ok, "the disc's grid fits");
    Editor_RegionCapsule(grid, cx, cz, cx, cz, r);
}

static void
test_overlay(void)
{
    struct Editor_RegionGrid grid;
    struct Editor_RegionParams params = { EDITOR_REGION_NATURAL, EDITOR_REGION_ADD, 50, EDITOR_REGION_POOR_FIT };
    struct Editor_RegionStats stats;
    int count;
    int west = 0;
    int east = 0;
    int edges = 0;
    int far = 0;
    int left = 0;

    clear_squares();
    /* Across the border at x = 3264. */
    disc_grid(&grid, 3264.3, 3230.6, 3.0);
    count = Editor_RegionOverlayCommands(&g_doc, 0, &params, OVERLAY, &grid, g_cmds, CMDS_MAX, &stats);
    apply(g_cmds, count);
    CHECK(count == stats.planned, "every planned tile is a change on bare ground (%d of %d)", count, stats.planned);
    CHECK(stats.closed == 0 && stats.dropped == 0, "nothing left out");
    for( int z = 3220; z < 3242; z++ )
        for( int x = 3250; x < 3280; x++ )
        {
            struct Editor_Tile const* t = tile_at(x, z);
            double const dx = x + 0.5 - 3264.3;
            double const dz = z + 0.5 - 3230.6;
            if( !t->has_overlay )
                continue;
            CHECK(t->overlay_id == OVERLAY, "the palette's overlay");
            west += x < 3264;
            east += x >= 3264;
            edges += t->shape != 0;
            far += sqrt(dx * dx + dz * dz) > 3.75;
        }
    CHECK(west > 8 && east > 8, "both squares take it (%d west, %d east)", west, east);
    CHECK(edges >= 12, "its rim is edge shapes (%d)", edges);
    CHECK(far == 0, "nothing beyond the disc (%d)", far);

    count = Editor_RegionOverlayCommands(&g_doc, 0, &params, OVERLAY, &grid, g_cmds, CMDS_MAX, &stats);
    CHECK(count == 0, "the same stroke again changes nothing (%d)", count);

    params.op = EDITOR_REGION_SUBTRACT;
    count = Editor_RegionOverlayCommands(&g_doc, 0, &params, OVERLAY, &grid, g_cmds, CMDS_MAX, &stats);
    apply(g_cmds, count);
    CHECK(stats.cleared > 0, "the take-off clears tiles (%d)", stats.cleared);
    for( int z = 3220; z < 3242; z++ )
        for( int x = 3250; x < 3280; x++ )
            left += tile_at(x, z)->has_overlay;
    CHECK(left == 0, "taken off along the same disc, nothing is left (%d)", left);
    Editor_RegionGridFree(&grid);
}

static void
test_closed(void)
{
    struct Editor_RegionGrid grid;
    struct Editor_RegionParams params = { EDITOR_REGION_NATURAL, EDITOR_REGION_ADD, 50, EDITOR_REGION_POOR_FIT };
    struct Editor_RegionStats stats;
    int count;

    clear_squares();
    /* Over the north edge, z 3264+ is square (50, 51): not open. */
    disc_grid(&grid, 3230.5, 3263.5, 2.5);
    count = Editor_RegionOverlayCommands(&g_doc, 0, &params, OVERLAY, &grid, g_cmds, CMDS_MAX, &stats);
    CHECK(stats.closed > 0, "tiles in a square not open are counted (%d)", stats.closed);
    CHECK(count == stats.planned - stats.closed, "and left out (%d of %d)", count, stats.planned);
    Editor_RegionGridFree(&grid);
}

static void
test_other(void)
{
    struct Editor_RegionGrid grid;
    struct Editor_RegionParams params = { EDITOR_REGION_NATURAL, EDITOR_REGION_ADD, 50, EDITOR_REGION_POOR_FIT };
    struct Editor_RegionStats stats;
    int count;
    int cut = 0;
    int replaced = 0;

    clear_squares();
    for( int z = 3200; z < 3264; z++ )
        for( int x = 3232; x < 3264; x++ )
        {
            struct Editor_Tile* t = tile_at(x, z);
            t->has_overlay = 1;
            t->overlay_id = OTHER_OVERLAY;
        }
    disc_grid(&grid, 3231.5, 3230.5, 3.0);
    count = Editor_RegionOverlayCommands(&g_doc, 0, &params, OVERLAY, &grid, g_cmds, CMDS_MAX, &stats);
    apply(g_cmds, count);
    for( int z = 3220; z < 3242; z++ )
        for( int x = 3232; x < 3240; x++ )
        {
            struct Editor_Tile const* t = tile_at(x, z);
            if( t->overlay_id == OVERLAY )
                replaced += t->shape == 0 ? 1 : 0, cut += t->shape != 0;
            else
                CHECK(t->has_overlay && t->overlay_id == OTHER_OVERLAY && t->shape == 0,
                    "the other overlay's tile is kept whole");
        }
    CHECK(cut == 0, "no tile of another overlay is cut to a shape (%d)", cut);
    CHECK(replaced > 0, "a tile the disc covers whole is replaced (%d)", replaced);
    Editor_RegionGridFree(&grid);
}

static void
test_underlay(void)
{
    struct Editor_RegionGrid grid;
    struct Editor_RegionStats stats;
    int count;
    int outside = 0;

    clear_squares();
    if( !Editor_RegionGridInit(&grid, 3220, 3220, 14, 14) )
        CHECK(0, "grid");
    Editor_RegionCapsule(&grid, 3227.0, 3227.0, 3227.0, 3227.0, 3.0);
    count = Editor_RegionUnderlayCommands(&g_doc, 0, UNDERLAY, &grid, 1, 99u, g_cmds, CMDS_MAX, &stats);
    apply(g_cmds, count);
    CHECK(tile_at(3226, 3226)->underlay_id == UNDERLAY, "the middle is painted");
    CHECK(tile_at(3227, 3227)->underlay_id == UNDERLAY, "the middle is painted");
    for( int z = 3220; z < 3234; z++ )
        for( int x = 3220; x < 3234; x++ )
        {
            double const dx = x + 0.5 - 3227.0;
            double const dz = z + 0.5 - 3227.0;
            if( tile_at(x, z)->underlay_id == UNDERLAY && sqrt(dx * dx + dz * dz) > 3.0 + 1.0 + 0.75 )
                outside++;
            CHECK(!tile_at(x, z)->has_overlay, "an underlay stroke writes no overlay");
        }
    CHECK(outside == 0, "nothing beyond the disc and its feather (%d)", outside);
    Editor_RegionGridFree(&grid);
}

static void
test_tidy(void)
{
    struct Editor_RegionTidy tidy;
    struct Editor_RegionStats stats;
    int count;
    int begun;
    int edges = 0;

    clear_squares();
    /* A stepped disc of whole tiles, and a separate run two tiles east. */
    for( int z = 3200; z < 3264; z++ )
        for( int x = 3200; x < 3264; x++ )
        {
            double const dx = x + 0.5 - 3220.6;
            double const dz = z + 0.5 - 3220.3;
            struct Editor_Tile* t = tile_at(x, z);
            if( dx * dx + dz * dz <= 2.7 * 2.7 || (x >= 3226 && x < 3229 && z >= 3218 && z < 3222) )
            {
                t->has_overlay = 1;
                t->overlay_id = OVERLAY;
            }
        }
    begun = Editor_RegionTidyBegin(&g_doc, 0, 3220, 3220, 1, 50, &tidy);
    CHECK(begun == 1, "Tidy begins on the run (%d)", begun);
    CHECK(tidy.run > 16 && tidy.run < 30, "the run is the disc only (%d tiles)", tidy.run);
    count = Editor_RegionTidyCommands(&g_doc, &tidy, EDITOR_REGION_NATURAL, 50, g_cmds, CMDS_MAX, &stats);
    apply(g_cmds, count);
    for( int z = 3214; z < 3227; z++ )
        for( int x = 3214; x < 3226; x++ )
            edges += tile_at(x, z)->has_overlay && tile_at(x, z)->shape != 0;
    CHECK(edges >= 8, "the steps are cut into edge shapes (%d)", edges);
    CHECK(tile_at(3220, 3220)->has_overlay && tile_at(3220, 3220)->shape == 0, "the middle is whole");
    for( int z = 3218; z < 3222; z++ )
        for( int x = 3226; x < 3229; x++ )
            CHECK(tile_at(x, z)->has_overlay && tile_at(x, z)->shape == 0,
                "the other run is left as it was (%d,%d)", x, z);
    Editor_RegionTidyFree(&tidy);

    begun = Editor_RegionTidyBegin(&g_doc, 0, 3250, 3250, 1, 50, &tidy);
    CHECK(begun == 0, "Tidy on bare ground begins nothing");
    Editor_RegionTidyFree(&tidy);
}

static void
test_room(void)
{
    struct Editor_RegionGrid grid;
    struct Editor_RegionParams params = { EDITOR_REGION_NATURAL, EDITOR_REGION_ADD, 50, EDITOR_REGION_POOR_FIT };
    struct Editor_RegionStats stats;
    int count;

    clear_squares();
    disc_grid(&grid, 3230.0, 3230.0, 6.0);
    count = Editor_RegionOverlayCommands(&g_doc, 0, &params, OVERLAY, &grid, g_cmds, 10, &stats);
    CHECK(count == 10, "the room is filled (%d)", count);
    CHECK(stats.dropped == stats.planned - 10, "the rest is counted as left out (%d of %d)", stats.dropped,
        stats.planned);
    Editor_RegionGridFree(&grid);
}

int
main(void)
{
    Editor_DocInit(&g_doc, NULL);
    for( int mx = 50; mx <= 51; mx++ )
    {
        struct Editor_Square* square = Editor_DocOpenSquare(&g_doc, mx, 50);
        CHECK(square != NULL, "square opens");
        square->loaded = 1;
    }
    test_overlay();
    test_closed();
    test_other();
    test_underlay();
    test_tidy();
    test_room();
    Editor_DocFree(&g_doc);
    printf("editor_region_ops_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
