/*
 * The collision store's map squares, at their edges.
 *
 * The store keeps collision per 64x64 map square, each square built on its own
 * from the cache (torirs_server_scene.c). The answer it has to give is the one
 * a single build spanning the edge gives: that is what every window used to
 * be, one build stamping both sides of each edge inside it. So the reference
 * here is exactly that -- a fresh 104x104 build centred on a square corner --
 * and the store is compared with it tile by tile, step by step, line by line
 * and route by route, at corners across the map.
 *
 * Then the edge under runtime change: a map loc whose collision crosses an
 * edge is removed, and another is turned; each time the store must equal the
 * same change made on a fresh build, and must still equal it after the
 * neighbouring squares are dropped and loaded again (a square loading next to
 * a changed loc takes that change across the edge). Then occupancy of a large
 * npc standing across an edge, tiles in squares nobody has loaded, and squares
 * lingering out and coming back.
 *
 * No world: the scene module runs against the server pack alone, as
 * loc_test.c does.
 */

#include "servpack_test_group.h"
#include "torirs_server_boot.h"
#include "torirs_server.h"
#include "torirs_server_content.h"
#include "torirs_server_scene.h"

#include "engine/world_builder/collision_map.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int g_fail = 0;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( cond )                                                                                 \
            printf("  ok   ");                                                                     \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL ");                                                                     \
            g_fail++;                                                                              \
        }                                                                                          \
        printf(__VA_ARGS__);                                                                       \
        printf("\n");                                                                              \
    } while( 0 )

static const char*
resolve_dir(
    const char* relative,
    char* scratch,
    size_t scratch_size)
{
    struct stat info;

    snprintf(scratch, scratch_size, "%s", relative);
    if( stat(scratch, &info) == 0 )
        return scratch;
    snprintf(scratch, scratch_size, "../%s", relative);
    return scratch;
}

/* Square corners in built-up places: walls, doors and fences run across these
 * edges. The window is centred on the corner, so all four squares meeting
 * there sit in its interior. */
static const struct
{
    const char* name;
    int x;
    int z;
} k_corners[] = {
    { "Lumbridge", 3200, 3200 },     { "Lumbridge castle", 3200, 3264 },
    { "Varrock square", 3200, 3456 }, { "Varrock east", 3264, 3392 },
    { "Falador", 2944, 3328 },        { "Draynor", 3072, 3264 },
    { "Al Kharid", 3328, 3200 },      { "Edgeville", 3072, 3520 },
    { "East Ardougne", 2624, 3328 },  { "Catherby", 2816, 3456 },
    { "Seers' Village", 2688, 3456 }, { "Camelot", 2752, 3520 },
};

static void
test_static_corners(const char* cache_dir)
{
    printf("store vs one build across each edge\n");
    for( size_t i = 0; i < sizeof(k_corners) / sizeof(k_corners[0]); i++ )
    {
        struct ToriRSServerSceneCompare compare;
        int zone_x = k_corners[i].x >> 3;
        int zone_z = k_corners[i].z >> 3;
        int ok;

        if( !ToriRSServer_SceneBuild(cache_dir, zone_x, zone_z) )
        {
            CHECK(0, "%s: the window builds", k_corners[i].name);
            continue;
        }
        ok = ToriRSServer_SceneDebugCompareFresh(cache_dir, zone_x, zone_z, &compare);
        /* The routes are searched in the bound window's region, which the
         * fresh comparison just left exactly where the window is. */
        ok = ok && ToriRSServer_SceneDebugCompareRoutes(cache_dir, zone_x, zone_z, &compare);
        CHECK(ok && compare.tiles > 0 && compare.tile_mismatches == 0,
              "%s: %d tile flags (4 levels, walk and boat) match, %d differ", k_corners[i].name,
              compare.tiles, compare.tile_mismatches);
        CHECK(compare.steps > 0 && compare.step_mismatches == 0,
              "%s: %d steps on and across the edges (sizes 1-3) match, %d differ",
              k_corners[i].name, compare.steps, compare.step_mismatches);
        CHECK(compare.lines > 0 && compare.line_mismatches == 0,
              "%s: %d lines of sight and walk across the edges match, %d differ",
              k_corners[i].name, compare.lines, compare.line_mismatches);
        CHECK(compare.routes > 0 && compare.route_mismatches == 0,
              "%s: %d routes across the edges match, %d differ", k_corners[i].name,
              compare.routes, compare.route_mismatches);
    }
}

/* The sweep's clock: rising, as the world's tick does. */
static int g_tick = 1000;

/* Every square around (x, z), the one holding it too, back to the cache --
 * without a world nothing replays, so each change below starts clean of the
 * ones before it. */
static void
reload_around(
    int x,
    int z)
{
    for( int dx = -1; dx <= 1; dx++ )
        for( int dz = -1; dz <= 1; dz++ )
            ToriRSServer_SceneDebugReloadSquare((x >> 6) + dx, (z >> 6) + dz);
}

/* Drop and reload every square around (x, z) except the one holding it. */
static void
reload_neighbours(
    int x,
    int z)
{
    for( int dx = -1; dx <= 1; dx++ )
        for( int dz = -1; dz <= 1; dz++ )
            if( dx || dz )
                ToriRSServer_SceneDebugReloadSquare((x >> 6) + dx, (z >> 6) + dz);
}

static void
test_runtime_edge(const char* cache_dir)
{
    int removed = 0;
    int turned = 0;

    printf("a change to a loc that crosses an edge\n");
    for( size_t i = 0; i < sizeof(k_corners) / sizeof(k_corners[0]); i++ )
    {
        ToriRSServer_SceneBuild(cache_dir, k_corners[i].x >> 3, k_corners[i].z >> 3);
        /* The four squares meeting at the corner, two locs from each. */
        for( int q = 0; q < 4; q++ )
        {
            int qx = k_corners[i].x - 1 + (q & 1) * 2;
            int qz = k_corners[i].z - 1 + (q >> 1) * 2;

            for( int skip = 0; skip < 4; skip += 2 )
            {
                int handle = ToriRSServer_SceneDebugFindEdgeLoc(qx, qz, skip);
                struct ToriRSServerSceneLoc* loc = ToriRSServer_SceneLoc(handle);
                struct ToriRSServerSceneLoc was;
                int differ;

                if( !loc )
                    continue;
                was = *loc;
                reload_around(was.x, was.z);
                handle = ToriRSServer_SceneFindLocExact(was.x, was.z, was.level, was.shape);
                CHECK(ToriRSServer_SceneLoc(handle) != NULL, "%s: loc %d at %d,%d is there again",
                      k_corners[i].name, was.loc_id, was.x, was.z);
                if( skip == 0 )
                {
                    /* Removed: both sides of the edge lose its stamp, and
                     * whatever shared those bits keeps them. */
                    CHECK(ToriRSServer_SceneRemoveLoc(handle), "%s: removed loc %d at %d,%d",
                          k_corners[i].name, was.loc_id, was.x, was.z);
                    differ = ToriRSServer_SceneDebugCheckChange(cache_dir, was.x, was.z, was.level,
                                                                was.shape, -1, 0);
                    CHECK(differ == 0, "  the store equals the same removal on one build (%d differ)",
                          differ);
                    reload_neighbours(was.x, was.z);
                    differ = ToriRSServer_SceneDebugCheckChange(cache_dir, was.x, was.z, was.level,
                                                                was.shape, -1, 0);
                    CHECK(differ == 0,
                          "  and still does after its neighbours were dropped and loaded (%d differ)",
                          differ);
                    removed++;
                }
                else
                {
                    /* Turned a quarter: the cache's stamp comes off across
                     * the edge, the new one goes on. */
                    int angle = (was.angle + 1) & 3;

                    CHECK(ToriRSServer_SceneReplaceLoc(handle, was.loc_id, angle),
                          "%s: turned loc %d at %d,%d to angle %d", k_corners[i].name, was.loc_id,
                          was.x, was.z, angle);
                    differ = ToriRSServer_SceneDebugCheckChange(cache_dir, was.x, was.z, was.level,
                                                                was.shape, was.loc_id, angle);
                    CHECK(differ == 0, "  the store equals the same turn on one build (%d differ)",
                          differ);
                    reload_neighbours(was.x, was.z);
                    differ = ToriRSServer_SceneDebugCheckChange(cache_dir, was.x, was.z, was.level,
                                                                was.shape, was.loc_id, angle);
                    CHECK(differ == 0,
                          "  and still does after its neighbours were dropped and loaded (%d differ)",
                          differ);
                    turned++;
                }
            }
        }
    }
    CHECK(removed >= 8 && turned >= 4,
          "the map gave edge-crossing locs to change: %d removed, %d turned", removed, turned);
}

static void
test_occupancy_and_unloaded(const char* cache_dir)
{
    /* Lumbridge's corner: squares 49/50 meet at 3200,3200. */
    int x = 3199;
    int z = 3199;
    int all = 1;

    printf("occupancy across an edge, and the edge of the loaded world\n");
    ToriRSServer_SceneBuild(cache_dir, 3200 >> 3, 3200 >> 3);
    /* Only what this window holds: the earlier corners' squares linger. */
    g_tick += 1000;
    ToriRSServer_SceneSquaresSweep(g_tick);
    /* A size-3 npc whose south-west tile is the last tile of one square:
     * its other tiles are in three others. */
    ToriRSServer_SceneChangeOccupancy(0, x, z, 3, COLL_FLAG_NPC_OCC, 1);
    for( int dx = 0; dx < 3; dx++ )
        for( int dz = 0; dz < 3; dz++ )
            all &= (ToriRSServer_SceneCollisionFlagsAt(0, x + dx, z + dz) & COLL_FLAG_NPC_OCC) != 0;
    CHECK(all, "a size-3 npc on 3199,3199 marks all nine tiles across four squares");
    CHECK(!ToriRSServer_SceneCanTravel(0, x - 1, z, 1, 0, 1, COLL_FLAG_NPC_OCC),
          "and a mover stepping east onto it from the other side of the edge is blocked");
    ToriRSServer_SceneChangeOccupancy(0, x, z, 3, COLL_FLAG_NPC_OCC, 0);
    all = 1;
    for( int dx = 0; dx < 3; dx++ )
        for( int dz = 0; dz < 3; dz++ )
            all &= (ToriRSServer_SceneCollisionFlagsAt(0, x + dx, z + dz) & COLL_FLAG_NPC_OCC) == 0;
    CHECK(all, "and leaving clears all nine");

    /* The window holds its view and one ring of squares: 3200 +- 52 is in
     * squares 48..51, so square 53 is nobody's. */
    CHECK(ToriRSServer_SceneCollisionFlagsAt(0, 53 * 64 + 10, 3210) == -1,
          "a tile in a square nobody holds reads as not loaded");
    CHECK(!ToriRSServer_SceneContains(53 * 64 + 10, 3210), "and is outside the scene");
    {
        /* The last held square's east edge: a step onto the unloaded square
         * is refused, the same as a window's edge refused it. */
        int edge_x = 52 * 64 - 1;
        int open_z = -1;

        for( int tz = 3190; tz < 3220 && open_z < 0; tz++ )
            if( ToriRSServer_SceneCanTravel(0, edge_x - 1, tz, 1, 0, 1, 0) )
                open_z = tz;
        CHECK(open_z >= 0, "the last held square has an open tile one in from its east edge");
        if( open_z >= 0 )
            CHECK(!ToriRSServer_SceneCanTravel(0, edge_x, open_z, 1, 0, 1, 0),
                  "and no step leaves it eastward into the unloaded square");
    }
}

static void
test_linger(const char* cache_dir)
{
    int held;
    int after;
    struct ToriRSServerSceneCompare compare;

    printf("squares linger, drop and come back\n");
    ToriRSServer_SceneBuild(cache_dir, 3200 >> 3, 3200 >> 3);
    g_tick += 1000;
    ToriRSServer_SceneSquaresSweep(g_tick);
    held = ToriRSServer_SceneSquareCount();
    /* Far away: the window lets go of Lumbridge's squares, which linger. */
    ToriRSServer_SceneBuild(cache_dir, 2944 >> 3, 3328 >> 3);
    ToriRSServer_SceneSquaresSweep(++g_tick);
    CHECK(ToriRSServer_SceneSquareCount() > held,
          "squares let go of stay loaded through their linger (%d loaded)",
          ToriRSServer_SceneSquareCount());
    g_tick += 1000;
    ToriRSServer_SceneSquaresSweep(g_tick);
    after = ToriRSServer_SceneSquareCount();
    CHECK(after <= held, "and are dropped after it (%d loaded, the window holds %d)", after, held);
    ToriRSServer_SceneBuild(cache_dir, 3200 >> 3, 3200 >> 3);
    CHECK(ToriRSServer_SceneDebugCompareFresh(cache_dir, 3200 >> 3, 3200 >> 3, &compare) &&
              compare.tile_mismatches == 0 && compare.step_mismatches == 0,
          "Lumbridge loaded again matches one build (%d tiles, %d steps differ)",
          compare.tile_mismatches, compare.step_mismatches);
}

int
main(void)
{
    char cache_scratch[512];
    char content_scratch[512];
    const char* cache_dir;
    const char* content_dir;

    cache_dir = resolve_dir(TORIRSSERVER_CACHE_DIR_DEFAULT, cache_scratch, sizeof(cache_scratch));
    content_dir = resolve_dir("OSRS-Content/osrs-content", content_scratch,
                              sizeof(content_scratch));
    ToriRSServer_ServPackDir(content_dir, cache_dir, g_test_pack_dir, sizeof(g_test_pack_dir));
    if( ToriRSServer_BootLoadContent(content_dir, cache_dir) != 0 )
    {
        CHECK(0, "the server pack loaded");
        return 1;
    }

    test_static_corners(cache_dir);
    test_runtime_edge(cache_dir);
    test_occupancy_and_unloaded(cache_dir);
    test_linger(cache_dir);

    ToriRSServer_SceneWindowsShutdown();
    ToriRSServer_ContentFree();
    if( g_fail )
    {
        printf("square_test: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("square_test: all checks passed\n");
    return 0;
}
