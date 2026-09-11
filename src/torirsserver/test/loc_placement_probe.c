/*
 * "Where on the map is this loc, and which way does it face?"
 *
 * The companion question to walkable_probe's. Authoring a handler for an
 * obstacle the cache ships means knowing the placement — a rope swing's start
 * tile, its rotation, and therefore which way across it carries you — and no
 * reference in this tree publishes those. What the cache DOES hold is the map
 * squares, so the placement can be read instead of guessed.
 *
 *   loc_placement_probe <cache_dir> <loc_id> <x> <z> [radius]
 *
 * Builds the server's own scene around (x,z) — the same `ToriRSServer_SceneBuild`
 * the world uses — and prints every placement of `loc_id` in it, on all four
 * levels. `radius` limits the report to tiles within that box of (x,z).
 *
 * The scene is a 13x13 zone window, so a wrong (x,z) reports nothing rather
 * than searching the world: give it a tile the wiki names and widen from there.
 */

#include "torirs_server_scene.h"

#include <stdio.h>
#include <stdlib.h>

/* Multiloc resolution wants a player's varbits. There is no player here, and a
 * placement's own record is what the map wrote, so the untransformed record is
 * the right answer — the same stub, for the same reason, as walkable_probe's. */
int
ToriRSServer_VarbitGet(
    struct ToriRSServerPlayer* player,
    int varbit_id)
{
    (void)player;
    (void)varbit_id;
    return 0;
}

int
main(int argc, char** argv)
{
    const char* cache_dir;
    int loc_id, x, z, radius = -1, slot, found = 0;

    if( argc < 5 )
    {
        fprintf(stderr,
                "usage: loc_placement_probe <cache_dir> <loc_id> <x> <z> [radius]\n");
        return 2;
    }
    cache_dir = argv[1];
    loc_id = atoi(argv[2]);
    x = atoi(argv[3]);
    z = atoi(argv[4]);
    if( argc > 5 )
        radius = atoi(argv[5]);

    if( !ToriRSServer_SceneBuild(cache_dir, x >> 3, z >> 3) )
    {
        fprintf(stderr, "loc_placement_probe: could not build a scene at %d,%d\n", x, z);
        return 1;
    }

    for( slot = 0;; slot++ )
    {
        struct ToriRSServerSceneLoc* loc = ToriRSServer_SceneLoc(slot);

        if( !loc )
            break;
        if( !loc->active || loc->loc_id != loc_id )
            continue;
        if( radius >= 0 && (loc->x < x - radius || loc->x > x + radius ||
                            loc->z < z - radius || loc->z > z + radius) )
            continue;
        printf("loc %d at %d,%d level %d  shape=%d angle=%d size=%dx%d\n",
               loc->loc_id, loc->x, loc->z, loc->level, loc->shape, loc->angle,
               loc->size_x, loc->size_z);
        found++;
    }
    if( !found )
        printf("no placement of loc %d in the scene around %d,%d\n", loc_id, x, z);
    return found ? 0 : 1;
}
