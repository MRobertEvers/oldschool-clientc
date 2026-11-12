/*
 * Region painting's planner (editor/editor_region.c), content-free. A small
 * map of tiles stands in for the document: each holds nothing, the overlay
 * being painted at a shape and rotation, or another overlay.
 *
 *   round trip  every shape fits its own coverage; a region read back off
 *               the map and replanned (Tidy at smooth 0) writes what is there.
 *   disc        a brush disc off the grid: edge shapes on its rim, whole
 *               tiles inside, no poor fit, nothing outside its reach.
 *   subtract    a stroke subtracted along the add it retraces leaves the
 *               ground bare.
 *   join        a second stroke over the first never takes coverage away.
 *   other       a tile holding another overlay is replaced only where the
 *               region covers it whole.
 *   lasso       a loop on tile edges fills whole tiles; a diagonal one cuts
 *               the tiles it crosses into halves.
 *   smooth      a stepped run read back and smoothed gains edge shapes and
 *               keeps its middle.
 *   hardness    harder plans cover no more than softer ones.
 *   dither      a rim tile is painted in about the share it is covered.
 *   flood       the run a tile belongs to, and a run too big is clipped.
 *
 * With an output path as its argument it also writes every plan as lines
 * "<name> <x> <z> <shape> <rotation>" for a look.
 */

#include "editor/editor_region.h"

#include <math.h>
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

#define MAP 64
#define TILES_MAX (MAP * MAP)

/* -1 bare, 0..11 the painted overlay at that shape, 99 another overlay. */
#define MAP_OTHER 99

struct Map
{
    signed char shape[MAP][MAP];
    signed char rotation[MAP][MAP];
};

static struct Map g_map;
static struct Editor_RegionTile g_plan[TILES_MAX];

static void
map_clear(struct Map* map)
{
    memset(map->shape, -1, sizeof(map->shape));
    memset(map->rotation, 0, sizeof(map->rotation));
}

static int
map_existing(
    void* user,
    int world_x,
    int world_z,
    int* out_shape,
    int* out_rotation)
{
    struct Map const* map = user;
    if( world_x < 0 || world_z < 0 || world_x >= MAP || world_z >= MAP )
        return EDITOR_REGION_HAS_NONE;
    if( map->shape[world_z][world_x] == MAP_OTHER )
        return EDITOR_REGION_HAS_OTHER;
    if( map->shape[world_z][world_x] < 0 )
        return EDITOR_REGION_HAS_NONE;
    *out_shape = map->shape[world_z][world_x];
    *out_rotation = map->rotation[world_z][world_x];
    return EDITOR_REGION_HAS_SAME;
}

static void
map_apply(
    struct Map* map,
    const struct Editor_RegionTile* plan,
    int count)
{
    for( int i = 0; i < count; i++ )
    {
        map->shape[plan[i].world_z][plan[i].world_x] = (signed char)plan[i].shape;
        map->rotation[plan[i].world_z][plan[i].world_x] = (signed char)plan[i].rotation;
    }
}

static uint64_t
map_mask(
    const struct Map* map,
    int x,
    int z)
{
    int const s = map->shape[z][x];
    if( s < 0 || s == MAP_OTHER )
        return 0;
    return Editor_RegionShapeMask(s, map->rotation[z][x]);
}

static void
dump(
    const char* name,
    const struct Editor_RegionTile* plan,
    int count)
{
    if( !g_dump )
        return;
    for( int i = 0; i < count; i++ )
        fprintf(g_dump, "%s %d %d %d %d\n", name, plan[i].world_x, plan[i].world_z, plan[i].shape, plan[i].rotation);
}

static int
plan_disc(
    struct Map* map,
    double cx,
    double cz,
    double radius,
    enum Editor_RegionOp op,
    int hardness)
{
    struct Editor_RegionGrid grid;
    struct Editor_RegionParams params = { EDITOR_REGION_NATURAL, op, hardness, EDITOR_REGION_POOR_FIT };
    int count;
    int const ok = Editor_RegionGridInit(&grid, (int)floor(cx - radius) - 1, (int)floor(cz - radius) - 1,
        (int)ceil(2 * radius) + 3, (int)ceil(2 * radius) + 3);
    CHECK(ok, "a disc's grid fits");
    Editor_RegionCapsule(&grid, cx, cz, cx, cz, radius);
    count = Editor_RegionPlan(&params, &grid, map_existing, map, g_plan, TILES_MAX);
    Editor_RegionGridFree(&grid);
    return count;
}

static void
test_round_trip(void)
{
    struct Editor_RegionGrid grid;
    struct Editor_RegionParams params = { EDITOR_REGION_NATURAL, EDITOR_REGION_REPLACE, 50, EDITOR_REGION_POOR_FIT };
    int count;
    int same = 1;

    for( int s = 0; s < EDITOR_REGION_SHAPES; s++ )
        for( int r = 0; r < 4; r++ )
        {
            uint64_t const m = Editor_RegionShapeMask(s, r);
            int shape = -1;
            int rotation = -1;
            int const found = Editor_RegionFit(EDITOR_REGION_ALL, 50, m, &shape, &rotation);
            CHECK(found && Editor_RegionShapeMask(shape, rotation) == m, "shape %d rotation %d fits itself", s, r);
        }
    CHECK(Editor_RegionShapeMask(0, 0) == ~(uint64_t)0, "shape 0 is the whole tile");
    /* A sample on the diagonal counts for both halves: 36 each, the two
     * turns together the whole tile. */
    CHECK(Editor_RegionPopcount(Editor_RegionShapeMask(1, 0)) == 36, "the diagonal half is 36 samples");
    CHECK((Editor_RegionShapeMask(1, 0) | Editor_RegionShapeMask(1, 2)) == ~(uint64_t)0, "its turn is the rest");

    /* Tidy at smooth 0: a disc laid, read back, replanned as it is. */
    map_clear(&g_map);
    count = plan_disc(&g_map, 20.3, 20.6, 3.1, EDITOR_REGION_ADD, 50);
    map_apply(&g_map, g_plan, count);
    CHECK(Editor_RegionGridInit(&grid, 14, 14, 14, 14), "grid");
    Editor_RegionReadBack(&grid, map_existing, &g_map);
    Editor_RegionSmooth(&grid, 0, 50);
    count = Editor_RegionPlan(&params, &grid, map_existing, &g_map, g_plan, TILES_MAX);
    for( int i = 0; i < count; i++ )
        if( g_plan[i].shape < 0
            || Editor_RegionShapeMask(g_plan[i].shape, g_plan[i].rotation)
                   != map_mask(&g_map, g_plan[i].world_x, g_plan[i].world_z) )
            same = 0;
    CHECK(same, "a laid region replanned at smooth 0 is what was laid");
    Editor_RegionGridFree(&grid);
}

static void
test_disc(void)
{
    int count;
    int edges = 0;
    int whole = 0;
    int poor = 0;
    int far = 0;
    int shapes_seen = 0;
    int seen[EDITOR_REGION_SHAPES] = { 0 };

    map_clear(&g_map);
    count = plan_disc(&g_map, 30.4, 30.7, 3.0, EDITOR_REGION_ADD, 50);
    dump("disc", g_plan, count);
    for( int i = 0; i < count; i++ )
    {
        double const dx = g_plan[i].world_x + 0.5 - 30.4;
        double const dz = g_plan[i].world_z + 0.5 - 30.7;
        CHECK(g_plan[i].shape >= 0, "an add writes shapes only");
        if( g_plan[i].shape == 0 )
            whole++;
        else
            edges++;
        if( g_plan[i].shape >= 0 && !seen[g_plan[i].shape]++ )
            shapes_seen++;
        if( g_plan[i].misfit > EDITOR_REGION_POOR_FIT )
            poor++;
        if( sqrt(dx * dx + dz * dz) > 3.0 + 0.75 )
            far++;
    }
    CHECK(whole >= 16, "a radius-3 disc has a whole middle (%d)", whole);
    CHECK(edges >= 12, "its rim is edge shapes (%d)", edges);
    CHECK(shapes_seen >= 4, "the rim uses several shapes (%d)", shapes_seen);
    CHECK(poor == 0, "no poor fits on a radius-3 disc (%d)", poor);
    CHECK(far == 0, "nothing beyond the disc's reach (%d)", far);
}

static void
test_subtract(void)
{
    int count;
    int left = 0;

    map_clear(&g_map);
    count = plan_disc(&g_map, 20.5, 20.5, 2.6, EDITOR_REGION_ADD, 50);
    map_apply(&g_map, g_plan, count);
    count = plan_disc(&g_map, 20.5, 20.5, 2.6, EDITOR_REGION_SUBTRACT, 50);
    dump("subtract", g_plan, count);
    map_apply(&g_map, g_plan, count);
    for( int z = 0; z < MAP; z++ )
        for( int x = 0; x < MAP; x++ )
            left += g_map.shape[z][x] >= 0;
    CHECK(left == 0, "a subtract along the add leaves nothing (%d tiles)", left);

    /* Half of it subtracted: the west half stays, the east goes. */
    map_clear(&g_map);
    count = plan_disc(&g_map, 20.5, 20.5, 3.0, EDITOR_REGION_ADD, 50);
    map_apply(&g_map, g_plan, count);
    count = plan_disc(&g_map, 24.0, 20.5, 2.0, EDITOR_REGION_SUBTRACT, 50);
    map_apply(&g_map, g_plan, count);
    CHECK(g_map.shape[20][18] == 0, "the far side is untouched");
    CHECK(g_map.shape[20][23] < 0, "the bite is bare");
}

static void
test_join(void)
{
    struct Map before;
    int count;
    int lost = 0;

    map_clear(&g_map);
    count = plan_disc(&g_map, 20.3, 20.4, 2.5, EDITOR_REGION_ADD, 50);
    map_apply(&g_map, g_plan, count);
    before = g_map;
    count = plan_disc(&g_map, 23.1, 21.2, 2.5, EDITOR_REGION_ADD, 50);
    dump("join", g_plan, count);
    map_apply(&g_map, g_plan, count);
    for( int z = 0; z < MAP; z++ )
        for( int x = 0; x < MAP; x++ )
        {
            uint64_t const was = map_mask(&before, x, z);
            uint64_t const now = map_mask(&g_map, x, z);
            /* The fit may trade a few samples at a seam; it never drops
             * a tile or most of one. */
            if( was && Editor_RegionPopcount(now & was) < Editor_RegionPopcount(was) - EDITOR_REGION_POOR_FIT )
                lost++;
        }
    CHECK(lost == 0, "a second stroke keeps the first's coverage (%d tiles lost)", lost);
}

static void
test_other(void)
{
    int count;
    int partial = 0;
    int whole = 0;

    map_clear(&g_map);
    for( int z = 0; z < MAP; z++ )
        for( int x = 22; x < MAP; x++ )
            g_map.shape[z][x] = MAP_OTHER;
    count = plan_disc(&g_map, 21.5, 20.5, 3.0, EDITOR_REGION_ADD, 50);
    dump("other", g_plan, count);
    for( int i = 0; i < count; i++ )
    {
        if( g_map.shape[g_plan[i].world_z][g_plan[i].world_x] != MAP_OTHER )
            continue;
        if( g_plan[i].shape == 0 && g_plan[i].coverage == 64 )
            whole++;
        else
            partial++;
    }
    CHECK(partial == 0, "another overlay's tile is never cut (%d)", partial);
    CHECK(whole > 0, "another overlay's tile the region covers whole is replaced (%d)", whole);

    /* Subtract never touches another overlay. */
    count = plan_disc(&g_map, 23.5, 20.5, 2.0, EDITOR_REGION_SUBTRACT, 50);
    CHECK(count == 0, "a subtract over another overlay changes nothing (%d)", count);
}

static void
test_lasso(void)
{
    struct Editor_RegionGrid grid;
    struct Editor_RegionParams params = { EDITOR_REGION_NATURAL, EDITOR_REGION_ADD, 50, EDITOR_REGION_POOR_FIT };
    double xs[64] = { 10, 14, 14, 10 };
    double zs[64] = { 10, 10, 13, 13 };
    int n = 4;
    int count;
    int whole = 0;
    int halves = 0;

    map_clear(&g_map);
    CHECK(Editor_RegionGridInit(&grid, 8, 8, 10, 10), "grid");
    Editor_RegionPolygon(&grid, xs, zs, 4);
    count = Editor_RegionPlan(&params, &grid, map_existing, &g_map, g_plan, TILES_MAX);
    dump("lasso_square", g_plan, count);
    for( int i = 0; i < count; i++ )
        whole += g_plan[i].shape == 0;
    CHECK(count == 12 && whole == 12, "a 4 x 3 loop on tile edges is 12 whole tiles (%d, %d)", count, whole);
    Editor_RegionGridFree(&grid);

    /* A right triangle whose hypotenuse runs corner to corner. */
    xs[0] = 10;
    zs[0] = 10;
    xs[1] = 16;
    zs[1] = 10;
    xs[2] = 10;
    zs[2] = 16;
    CHECK(Editor_RegionGridInit(&grid, 8, 8, 10, 10), "grid");
    Editor_RegionPolygon(&grid, xs, zs, 3);
    count = Editor_RegionPlan(&params, &grid, map_existing, &g_map, g_plan, TILES_MAX);
    dump("lasso_triangle", g_plan, count);
    for( int i = 0; i < count; i++ )
        halves += g_plan[i].shape == 1;
    CHECK(halves == 6, "the hypotenuse is six diagonal halves (%d)", halves);
    CHECK(count == 21, "the triangle is 15 whole and 6 halves (%d)", count);
    Editor_RegionGridFree(&grid);

    /* Smoothing a loop doubles its points per pass, up to the room given. */
    xs[0] = 0;
    zs[0] = 0;
    xs[1] = 4;
    zs[1] = 0;
    xs[2] = 4;
    zs[2] = 4;
    xs[3] = 0;
    zs[3] = 4;
    n = 4;
    Editor_RegionLoopSmooth(xs, zs, &n, 64, 2);
    CHECK(n == 16, "two passes on four points make sixteen (%d)", n);
    Editor_RegionLoopSmooth(xs, zs, &n, 20, 1);
    CHECK(n == 16, "a pass that would not fit is not made (%d)", n);
}

static void
test_smooth(void)
{
    struct Editor_RegionGrid grid;
    struct Editor_RegionParams params = { EDITOR_REGION_NATURAL, EDITOR_REGION_REPLACE, 50, EDITOR_REGION_POOR_FIT };
    int count;
    int edges = 0;
    int middle_kept = 1;
    int removed = 0;

    /* The stepped disc the plain brush paints: whole tiles by centre. */
    map_clear(&g_map);
    for( int z = 0; z < MAP; z++ )
        for( int x = 0; x < MAP; x++ )
        {
            double const dx = x + 0.5 - 30.6;
            double const dz = z + 0.5 - 30.3;
            if( dx * dx + dz * dz <= 2.7 * 2.7 )
                g_map.shape[z][x] = 0;
        }
    CHECK(Editor_RegionGridInit(&grid, 24, 24, 14, 14), "grid");
    Editor_RegionReadBack(&grid, map_existing, &g_map);
    Editor_RegionSmooth(&grid, 4, 50);
    count = Editor_RegionPlan(&params, &grid, map_existing, &g_map, g_plan, TILES_MAX);
    dump("tidy", g_plan, count);
    for( int i = 0; i < count; i++ )
    {
        int const x = g_plan[i].world_x;
        int const z = g_plan[i].world_z;
        double const dx = x + 0.5 - 30.6;
        double const dz = z + 0.5 - 30.3;
        if( g_plan[i].shape > 0 )
            edges++;
        if( g_plan[i].shape < 0 )
            removed++;
        if( dx * dx + dz * dz <= 1.5 * 1.5 && g_plan[i].shape != 0 )
            middle_kept = 0;
    }
    CHECK(edges >= 8, "Tidy cuts the steps into edge shapes (%d)", edges);
    CHECK(middle_kept, "Tidy keeps the middle whole");
    CHECK(removed <= 4, "Tidy at half a tile takes off at most the corner steps (%d)", removed);
    Editor_RegionGridFree(&grid);
}

static void
test_hardness(void)
{
    int area[3];
    static const int k_hard[3] = { 25, 50, 75 };

    for( int h = 0; h < 3; h++ )
    {
        int const count = (map_clear(&g_map), plan_disc(&g_map, 30.3, 30.2, 2.4, EDITOR_REGION_ADD, k_hard[h]));
        area[h] = 0;
        for( int i = 0; i < count; i++ )
            area[h] += Editor_RegionPopcount(Editor_RegionShapeMask(g_plan[i].shape, g_plan[i].rotation));
    }
    CHECK(area[0] >= area[1] && area[1] >= area[2], "harder covers no more (%d %d %d)", area[0], area[1], area[2]);
    CHECK(area[0] > area[2], "hardness changes the cover (%d vs %d)", area[0], area[2]);
}

static void
test_dither(void)
{
    struct Editor_RegionGrid grid;
    static int xs[TILES_MAX];
    static int zs[TILES_MAX];
    int hits = 0;
    int trials = 0;
    int a;
    int b;

    CHECK(Editor_RegionGridInit(&grid, 0, 0, 4, 1), "grid");
    grid.tiles[0] = ~(uint64_t)0;
    grid.tiles[1] = Editor_RegionShapeMask(6, 0); /* 32 of 64 */
    grid.tiles[2] = Editor_RegionShapeMask(7, 0);
    for( uint32_t seed = 1; seed <= 1000; seed++ )
    {
        int const n = Editor_RegionDither(&grid, 0, seed * 2654435761u, xs, zs, TILES_MAX);
        int whole = 0;
        for( int i = 0; i < n; i++ )
        {
            whole += xs[i] == 0;
            hits += xs[i] == 1;
            CHECK(xs[i] != 3, "a bare tile is never painted");
        }
        CHECK(whole == 1, "a whole tile is always painted");
        trials++;
    }
    CHECK(fabs((double)hits / trials - 0.5) < 0.05, "a half-covered tile is painted about half the time (%d/%d)", hits,
        trials);
    a = Editor_RegionDither(&grid, 0, 77, xs, zs, TILES_MAX);
    b = Editor_RegionDither(&grid, 0, 77, xs, zs, TILES_MAX);
    CHECK(a == b, "a seed replays");
    Editor_RegionGridFree(&grid);
}

static void
test_flood(void)
{
    static int xs[TILES_MAX];
    static int zs[TILES_MAX];
    int clipped = -1;
    int count;

    map_clear(&g_map);
    for( int z = 5; z < 10; z++ )
        for( int x = 5; x < 12; x++ )
            g_map.shape[z][x] = 0;
    g_map.shape[20][20] = 1; /* not joined to the run */
    count = Editor_RegionFlood(map_existing, &g_map, 7, 7, xs, zs, TILES_MAX, &clipped);
    CHECK(count == 35 && !clipped, "the run is its 35 tiles (%d, clipped %d)", count, clipped);
    count = Editor_RegionFlood(map_existing, &g_map, 7, 7, xs, zs, 10, &clipped);
    CHECK(count == 10 && clipped, "a run bigger than the room is clipped (%d, %d)", count, clipped);
    count = Editor_RegionFlood(map_existing, &g_map, 30, 30, xs, zs, TILES_MAX, &clipped);
    CHECK(count == 0, "a bare tile has no run");
}

static int
plan_run(
    struct Map* map,
    enum Editor_RegionSet set,
    double ax,
    double az,
    double bx,
    double bz,
    double radius)
{
    struct Editor_RegionGrid grid;
    struct Editor_RegionParams params = { set, EDITOR_REGION_ADD, 50, EDITOR_REGION_POOR_FIT };
    int count;
    int const ok = Editor_RegionGridInit(&grid, 0, 0, MAP, MAP);
    CHECK(ok, "a run's grid fits");
    Editor_RegionCapsule(&grid, ax, az, bx, bz, radius);
    count = Editor_RegionPlan(&params, &grid, map_existing, map, g_plan, TILES_MAX);
    Editor_RegionGridFree(&grid);
    return count;
}

static void
test_straight(void)
{
    double sx;
    double sz;
    double ex;
    double ez;
    int count;
    int square = 1;
    int edges = 0;
    int poor = 0;

    /* The start goes to its tile's centre; the end along the nearest of the
     * eight directions, a whole number of tiles on. */
    Editor_RegionStraight(10.3, 20.8, 25.9, 21.4, &sx, &sz, &ex, &ez);
    CHECK(sx == 10.5 && sz == 20.5 && ex == 25.5 && ez == 20.5, "east: %g,%g -> %g,%g", sx, sz, ex, ez);
    Editor_RegionStraight(10.3, 20.8, 10.9, 30.1, &sx, &sz, &ex, &ez);
    CHECK(ex == 10.5 && ez == 30.5, "north: -> %g,%g", ex, ez);
    Editor_RegionStraight(10.3, 20.8, 16.0, 26.9, &sx, &sz, &ex, &ez);
    CHECK(ex == 16.5 && ez == 26.5, "north-east: -> %g,%g", ex, ez);
    Editor_RegionStraight(10.3, 20.8, 4.0, 15.0, &sx, &sz, &ex, &ez);
    CHECK(ex == 4.5 && ez == 14.5, "south-west: -> %g,%g", ex, ez);
    Editor_RegionStraight(10.3, 20.8, 10.4, 20.6, &sx, &sz, &ex, &ez);
    CHECK(ex == sx && ez == sz, "a press without a drag is a disc at the tile's centre");

    /* What it is for: a three-wide run along a row is whole tiles from end
     * cap to end cap, its edges the grid's -- no tooth anywhere. */
    map_clear(&g_map);
    count = plan_run(&g_map, EDITOR_REGION_PAVED, 10.5, 20.5, 40.5, 20.5, 1.5);
    dump("straight_row", g_plan, count);
    map_apply(&g_map, g_plan, count);
    for( int x = 13; x <= 38; x++ )
        for( int z = 17; z <= 23; z++ )
        {
            int const want = z >= 19 && z <= 21 ? 0 : -1;
            if( g_map.shape[z][x] != want )
                square = 0;
        }
    CHECK(square, "a straight row is three whole tiles wide along its length, nothing beside it");

    /* Along the diagonal both rims are edge shapes, every one a good fit. */
    map_clear(&g_map);
    count = plan_run(&g_map, EDITOR_REGION_PAVED, 10.5, 10.5, 40.5, 40.5, 1.5);
    dump("straight_diagonal", g_plan, count);
    for( int i = 0; i < count; i++ )
    {
        int const along = g_plan[i].world_x + g_plan[i].world_z;
        if( along < 30 || along > 70 )
            continue;
        if( g_plan[i].shape > 0 )
            edges++;
        if( g_plan[i].misfit > EDITOR_REGION_POOR_FIT )
            poor++;
    }
    CHECK(edges >= 30, "a diagonal run's two rims are edge shapes (%d)", edges);
    CHECK(poor == 0, "and none of them a poor fit (%d)", poor);
}

int
main(
    int argc,
    char** argv)
{
    if( argc > 1 )
        g_dump = fopen(argv[1], "w");
    test_round_trip();
    test_disc();
    test_subtract();
    test_join();
    test_other();
    test_lasso();
    test_smooth();
    test_hardness();
    test_dither();
    test_flood();
    test_straight();
    if( g_dump )
        fclose(g_dump);
    printf("editor_region_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
