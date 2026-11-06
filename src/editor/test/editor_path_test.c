/*
 * The path painter's planner (editor/editor_path.c), content-free.
 *
 *   shapes     the coverage of each overlay shape is the client's: shape 0
 *              whole, the diagonal half (1) and its turn the complement, the
 *              half square (6) half; four turns are none; every shape the
 *              fit may choose comes back from its own coverage.
 *   fit        a bare tile, a grazed one, a full one.
 *   pavement   a straight road three wide is three rows of whole tiles; one
 *              two wide sits on tile edges; a diagonal road's edges are half
 *              diagonals, its middle whole; only pavement's shapes.
 *   dirt       a track is continuous (every tile its line passes is painted),
 *              its edges carry edge shapes, the same seed draws the same
 *              edges, another seed others; a narrow track uses half shapes.
 *   joining    a second stroke over a first only adds coverage, and one along
 *              the same line changes nothing.
 *
 * With an output path as its argument it also writes every plan as lines
 * "<name> <x> <z> <shape> <rotation>" for a look (scratchpad render).
 */

#include "editor/editor_path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks;
static int g_failed;
static FILE* g_dump;

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

#define TILES_MAX 8192

static struct Editor_PathTile g_tiles[TILES_MAX];

static int
popcount64(uint64_t v)
{
    int n = 0;
    while( v )
    {
        v &= v - 1;
        n++;
    }
    return n;
}

static void
dump(
    const char* name,
    const struct Editor_PathTile* tiles,
    int count)
{
    if( !g_dump )
        return;
    for( int i = 0; i < count; i++ )
        fprintf(g_dump, "%s %d %d %d %d\n", name, tiles[i].world_x, tiles[i].world_z, tiles[i].shape, tiles[i].rotation);
}

static const struct Editor_PathTile*
tile_at(
    const struct Editor_PathTile* tiles,
    int count,
    int x,
    int z)
{
    for( int i = 0; i < count; i++ )
        if( tiles[i].world_x == x && tiles[i].world_z == z )
            return &tiles[i];
    return NULL;
}

static int
plan(
    enum Editor_PathStyle style,
    double width,
    double roughness,
    uint32_t seed,
    const int* xs,
    const int* zs,
    int count,
    Editor_PathExistingFn existing,
    void* user,
    struct Editor_PathTile* out)
{
    struct Editor_PathParams params;
    memset(&params, 0, sizeof(params));
    params.style = style;
    params.width = width;
    params.roughness = roughness;
    params.seed = seed;
    return Editor_PathPlan(&params, xs, zs, count, existing, user, out, TILES_MAX);
}

/* A drag along a row of tiles, x0..x1 at z. */
static int
row_points(
    int x0,
    int x1,
    int z,
    int* xs,
    int* zs)
{
    int n = 0;
    for( int x = x0; x <= x1; x++ )
    {
        xs[n] = x;
        zs[n] = z;
        n++;
    }
    return n;
}

static void
test_shapes(void)
{
    uint64_t const full = ~(uint64_t)0;

    for( int r = 0; r < 4; r++ )
        CHECK(Editor_PathShapeMask(0, r) == full, "shape 0 covers the tile at every turn");
    CHECK((Editor_PathShapeMask(1, 0) | Editor_PathShapeMask(1, 2)) == full,
        "the half diagonal and its half turn cover the tile together");
    CHECK(popcount64(Editor_PathShapeMask(1, 0) & Editor_PathShapeMask(1, 2)) == 8,
        "...overlapping on the diagonal's own samples only (%d)",
        popcount64(Editor_PathShapeMask(1, 0) & Editor_PathShapeMask(1, 2)));
    /* Shape 1 at rotation 0 is the client's: the west and south edges' half
     * (the minimap's mask: the NW corner sample covered, the NE one not). */
    CHECK((Editor_PathShapeMask(1, 0) >> 56 & 1) && !(Editor_PathShapeMask(1, 0) >> 63 & 1)
              && (Editor_PathShapeMask(1, 0) & 1) && (Editor_PathShapeMask(1, 0) >> 7 & 1),
        "shape 1 rotation 0 is the south-west half");
    CHECK(popcount64(Editor_PathShapeMask(6, 0)) == 32 && (Editor_PathShapeMask(6, 0) & 1)
              && !(Editor_PathShapeMask(6, 0) >> 7 & 1),
        "shape 6 is the west half square");
    for( int s = 0; s < 12; s++ )
        for( int r = 0; r < 4; r++ )
        {
            uint64_t const m = Editor_PathShapeMask(s, r);
            CHECK(m != 0, "shape %d rotation %d covers something", s, r);
            /* Four quarter turns of the sample grid are none. */
            CHECK(popcount64(m) == popcount64(Editor_PathShapeMask(s, (r + 1) & 3)),
                "shape %d keeps its area through a turn", s);
        }
    /* Every shape a style writes comes back from its own coverage. */
    for( int style = 0; style < EDITOR_PATH_STYLE_COUNT; style++ )
        for( int s = 0; s < 11; s++ )
            for( int r = 0; r < 4; r++ )
            {
                uint64_t const m = Editor_PathShapeMask(s, r);
                int shape = -1;
                int rotation = -1;
                int const fitted = Editor_PathFit((enum Editor_PathStyle)style, m, &shape, &rotation);
                int const allowed = style == EDITOR_PATH_DIRT || s == 0 || s == 1 || s == 6 || s == 7 || s == 8;
                if( !allowed )
                    continue;
                CHECK(fitted && Editor_PathShapeMask(shape, rotation) == m,
                    "style %d: shape %d rotation %d fits itself (got %d/%d)", style, s, r, shape, rotation);
            }
}

static void
test_fit(void)
{
    int shape = -1;
    int rotation = -1;
    CHECK(!Editor_PathFit(EDITOR_PATH_DIRT, 0, &shape, &rotation), "fit: nothing covered leaves the tile bare");
    CHECK(!Editor_PathFit(EDITOR_PATH_DIRT, (uint64_t)1, &shape, &rotation), "fit: one sample grazed leaves it bare");
    CHECK(Editor_PathFit(EDITOR_PATH_PAVED, ~(uint64_t)0, &shape, &rotation) && shape == 0,
        "fit: a covered tile is whole (%d)", shape);
}

static int
count_shape(
    const struct Editor_PathTile* tiles,
    int count,
    int shape)
{
    int n = 0;
    for( int i = 0; i < count; i++ )
        n += tiles[i].shape == shape;
    return n;
}

static void
test_paved(void)
{
    int xs[64];
    int zs[64];
    int n;
    int count;

    /* A road three tiles wide along z 100, x 100..119. */
    n = row_points(100, 119, 100, xs, zs);
    count = plan(EDITOR_PATH_PAVED, 3.0, 0.0, 1, xs, zs, n, NULL, NULL, g_tiles);
    dump("paved_straight3", g_tiles, count);
    CHECK(count == 60 && count_shape(g_tiles, count, 0) == 60, "paved: 20 x 3 whole tiles (%d tiles, %d whole)", count,
        count_shape(g_tiles, count, 0));
    CHECK(tile_at(g_tiles, count, 110, 99) && tile_at(g_tiles, count, 110, 101) && !tile_at(g_tiles, count, 110, 102)
              && !tile_at(g_tiles, count, 110, 98),
        "paved: the rows are 99..101, on the drag's middle");

    /* Two wide: its edges on tile edges, two whole rows. */
    count = plan(EDITOR_PATH_PAVED, 2.0, 0.0, 1, xs, zs, n, NULL, NULL, g_tiles);
    dump("paved_straight2", g_tiles, count);
    CHECK(count == 40 && count_shape(g_tiles, count, 0) == 40, "paved: two wide is two whole rows (%d tiles)", count);

    /* One and a half: a whole row and a half-square row. */
    count = plan(EDITOR_PATH_PAVED, 1.5, 0.0, 1, xs, zs, n, NULL, NULL, g_tiles);
    dump("paved_straight15", g_tiles, count);
    CHECK(count == 40 && count_shape(g_tiles, count, 0) == 20 && count_shape(g_tiles, count, 6) == 20,
        "paved: 1.5 wide is a whole row and a half-square row (%d tiles, %d whole, %d half)", count,
        count_shape(g_tiles, count, 0), count_shape(g_tiles, count, 6));

    /* A diagonal: whole in the middle, half diagonals on both edges. */
    n = 0;
    for( int i = 0; i < 20; i++ )
    {
        xs[n] = 200 + i;
        zs[n] = 300 + i;
        n++;
    }
    count = plan(EDITOR_PATH_PAVED, 2.0, 0.0, 1, xs, zs, n, NULL, NULL, g_tiles);
    dump("paved_diagonal", g_tiles, count);
    {
        int edges = count_shape(g_tiles, count, 1);
        int others = 0;
        for( int i = 0; i < count; i++ )
            others += g_tiles[i].shape != 0 && g_tiles[i].shape != 1;
        CHECK(count > 0 && edges >= 2 * 17 && count_shape(g_tiles, count, 0) >= 17,
            "paved: a diagonal road's edges are half diagonals (%d) round whole tiles (%d)", edges,
            count_shape(g_tiles, count, 0));
        /* The square cap across a diagonal takes three tiles at each end. */
        CHECK(others <= 6, "paved: ...with other shapes only at its two ends (%d)", others);
    }

    /* A bend: east then north. Only pavement's shapes, anywhere. */
    n = 0;
    for( int i = 0; i < 12; i++ )
    {
        xs[n] = 400 + i;
        zs[n] = 400;
        n++;
    }
    for( int i = 1; i < 12; i++ )
    {
        xs[n] = 411;
        zs[n] = 400 + i;
        n++;
    }
    count = plan(EDITOR_PATH_PAVED, 3.0, 0.0, 1, xs, zs, n, NULL, NULL, g_tiles);
    dump("paved_bend", g_tiles, count);
    {
        int stray = 0;
        for( int i = 0; i < count; i++ )
            stray += g_tiles[i].shape != 0 && g_tiles[i].shape != 1 && g_tiles[i].shape != 6
                     && g_tiles[i].shape != 7 && g_tiles[i].shape != 8;
        CHECK(stray == 0, "paved: a bend uses only pavement's shapes (%d others)", stray);
        CHECK(tile_at(g_tiles, count, 412, 401) && tile_at(g_tiles, count, 412, 401)->shape == 0,
            "paved: the bend's outer corner is filled (a mitre, not a notch)");
    }
}

/* A wandering drag: a curve through tiles, staircased as a mouse makes it. */
static int
curve_points(
    int* xs,
    int* zs)
{
    int n = 0;
    int x = 1000;
    int z = 1000;
    for( int i = 0; i < 60; i++ )
    {
        xs[n] = x;
        zs[n] = z;
        n++;
        /* Mostly east, drifting north then south. */
        if( i % 3 != 2 )
            x++;
        else
            z += i < 30 ? 1 : -1;
    }
    return n;
}

static int g_existing_count;
static struct Editor_PathTile* g_existing;

static int
fake_existing(
    void* user,
    int world_x,
    int world_z,
    int* out_shape,
    int* out_rotation)
{
    const struct Editor_PathTile* t = tile_at(g_existing, g_existing_count, world_x, world_z);
    (void)user;
    if( !t )
        return 0;
    *out_shape = t->shape;
    *out_rotation = t->rotation;
    return 1;
}

static void
test_dirt(void)
{
    static struct Editor_PathTile other[TILES_MAX];
    int xs[256];
    int zs[256];
    int const n = curve_points(xs, zs);
    int count;
    int count2;
    int edges = 0;
    int same = 1;

    count = plan(EDITOR_PATH_DIRT, 2.0, 0.6, 7, xs, zs, n, NULL, NULL, g_tiles);
    dump("dirt_curve", g_tiles, count);
    for( int i = 0; i < n; i++ )
        CHECK(tile_at(g_tiles, count, xs[i], zs[i]), "dirt: the drag's tile %d,%d is on the track", xs[i], zs[i]);
    for( int i = 0; i < count; i++ )
        edges += g_tiles[i].shape != 0;
    CHECK(count > 0 && edges * 100 / count >= 20 && edges * 100 / count <= 70,
        "dirt: a share of the track's tiles are edge shapes, as the content's tracks (%d of %d)", edges, count);

    count2 = plan(EDITOR_PATH_DIRT, 2.0, 0.6, 7, xs, zs, n, NULL, NULL, other);
    if( count2 != count )
        same = 0;
    for( int i = 0; same && i < count; i++ )
        same = memcmp(&g_tiles[i], &other[i], sizeof(g_tiles[i])) == 0;
    CHECK(same, "dirt: the same seed draws the same track");
    count2 = plan(EDITOR_PATH_DIRT, 2.0, 0.6, 8, xs, zs, n, NULL, NULL, other);
    same = count2 == count;
    for( int i = 0; same && i < count; i++ )
        same = memcmp(&g_tiles[i], &other[i], sizeof(g_tiles[i])) == 0;
    CHECK(!same, "dirt: another seed draws other edges");

    /* A smooth line (roughness 0) is still a clean track. */
    count2 = plan(EDITOR_PATH_DIRT, 3.0, 0.0, 7, xs, zs, n, NULL, NULL, other);
    dump("dirt_smooth3", other, count2);
    CHECK(count2 > count, "dirt: three wide covers more than two (%d > %d)", count2, count);

    /* A narrow trail: half a tile and more across, edge shapes on most of it. */
    count2 = plan(EDITOR_PATH_DIRT, 1.0, 0.3, 9, xs, zs, n, NULL, NULL, other);
    dump("dirt_trail1", other, count2);
    {
        int halves = 0;
        for( int i = 0; i < count2; i++ )
            halves += other[i].shape != 0;
        CHECK(count2 > 0 && halves * 100 / count2 >= 40, "dirt: a one-wide trail is mostly edge shapes (%d of %d)",
            halves, count2);
    }

    /* Joining: the same stroke again changes nothing; a fork only adds. */
    g_existing = g_tiles;
    g_existing_count = count;
    count2 = plan(EDITOR_PATH_DIRT, 2.0, 0.6, 7, xs, zs, n, fake_existing, NULL, other);
    {
        int changed = 0;
        for( int i = 0; i < count2; i++ )
        {
            const struct Editor_PathTile* was = tile_at(g_tiles, count, other[i].world_x, other[i].world_z);
            if( !was || was->shape != other[i].shape || was->rotation != other[i].rotation )
                changed++;
        }
        CHECK(changed == 0, "joining: the same stroke over itself changes no tile (%d)", changed);
    }
    {
        int fx[64];
        int fz[64];
        int lost = 0;
        int m = 0;
        for( int i = 0; i < 25; i++ )
        {
            fx[m] = 1015;
            fz[m] = 1005 + 12 - i;
            m++;
        }
        count2 = plan(EDITOR_PATH_DIRT, 2.0, 0.6, 11, fx, fz, m, fake_existing, NULL, other);
        dump("dirt_fork", other, count2);
        for( int i = 0; i < count2; i++ )
        {
            const struct Editor_PathTile* was = tile_at(g_tiles, count, other[i].world_x, other[i].world_z);
            if( was
                && (Editor_PathShapeMask(was->shape, was->rotation) & ~Editor_PathShapeMask(other[i].shape, other[i].rotation))
                       != 0
                && popcount64(Editor_PathShapeMask(was->shape, was->rotation)
                              & ~Editor_PathShapeMask(other[i].shape, other[i].rotation))
                       > 8 )
                lost++;
        }
        CHECK(lost == 0, "joining: a fork across the track takes no coverage from it (%d tiles lost)", lost);
    }
}

static void
test_limits(void)
{
    int xs[2] = { 10, 600 };
    int zs[2] = { 10, 10 };
    int one_x = 50;
    int one_z = 50;
    CHECK(plan(EDITOR_PATH_DIRT, 2.0, 0.5, 1, xs, zs, 0, NULL, NULL, g_tiles) == 0, "limits: no points, no tiles");
    CHECK(plan(EDITOR_PATH_PAVED, 2.0, 0.0, 1, xs, zs, 2, NULL, NULL, g_tiles) == 0,
        "limits: a path wider than %d tiles is refused", EDITOR_PATH_SPAN_MAX);
    CHECK(plan(EDITOR_PATH_PAVED, 1.0, 0.0, 1, &one_x, &one_z, 1, NULL, NULL, g_tiles) == 1 && g_tiles[0].shape == 0,
        "limits: a click is one whole tile");
}

int
main(
    int argc,
    char** argv)
{
    if( argc > 1 )
    {
        g_dump = fopen(argv[1], "w");
        if( !g_dump )
        {
            fprintf(stderr, "cannot write %s\n", argv[1]);
            return 2;
        }
    }
    test_shapes();
    test_fit();
    test_paved();
    test_dirt();
    test_limits();
    if( g_dump )
        fclose(g_dump);
    printf("editor_path_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
