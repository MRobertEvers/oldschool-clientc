/*
 * "Is this tile standable, and what is standable near it?"
 *
 * Authoring an agility course means naming a landing tile for every obstacle,
 * and no reference in this tree publishes those tiles for the courses that are
 * still missing (Pollnivneach, Shayzien, Ape Atoll, Werewolf, Penguin,
 * Dorgesh-Kaan, Prifddinas, Colossal Wyrm). What the cache DOES hold is the
 * geometry itself, so a landing tile can be derived from it instead of guessed:
 * the roof of a rooftop course is a small island of standable tiles at its
 * plane, and there is usually exactly one candidate next to an obstacle.
 *
 * This builds the server's own collision map out of real map squares — the same
 * `ToriRSServer_SceneBuild` the world uses — and reports standability, so a course
 * author can check a tile rather than hope.
 *
 *   walkable_probe <cache_dir> <x> <z> <level> [radius]
 *
 * With a radius it prints the standable tiles in that box as a picture, which
 * is what actually answers "where does this obstacle put me".
 */

#include "torirs_server_scene.h"

#include "engine/world_builder/collision_map.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * Standable = nothing on the tile itself stops a walking player occupying it.
 *
 * That is the tile's own flags, not a step test: `collision_map_can_travel`
 * answers "may I move from here to there", and with a zero offset it answers
 * about a step nobody takes. The tile bits that matter are the loc occupancy
 * and the floor pair (FLOOR is "there is no floor here", which is why an empty
 * roof tile and the open sky read differently).
 */
static int
standable(struct CollisionMap* cm, int local_x, int local_z)
{
    if( !cm )
        return 0;
    if( local_x < 0 || local_x >= cm->size_x || local_z < 0 || local_z >= cm->size_z )
        return 0;
    /*
     * A flag word of zero is an ORDINARY OPEN TILE, and this is the trap.
     *
     * The map's BLOCK setting is what stamps COLL_FLAG_FLOOR (apply_terrain_column
     * in torirs_server_scene.c), so FLOOR means "this tile is blocked floor" — the
     * absence of every flag means nothing blocks it. The middle of Lumbridge's
     * field reads 0x0. An earlier version of this probe read zero as "no scene
     * data" and called it blocked, which turned three quarters of every course's
     * landings into false alarms and nearly had eight working rooftops
     * "corrected" onto tiles they did not need.
     */
    return (collision_map_tile(cm, local_x, local_z)
            & (COLL_FLAG_LOC | COLL_FLAG_FLOOR_BLOCKED)) == 0;
}

/*
 * Multiloc resolution wants a player's varbits. There is no player here, and a
 * loc's *collision* is its placed record's footprint either way, so the
 * untransformed record is the right answer — the same stub, for the same
 * reason, as collision_doors_test.c's.
 */
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
    int x, z, level, radius = 0;
    int origin_x, origin_z;
    struct CollisionMap* cm;

    if( argc < 5 )
    {
        fprintf(stderr, "usage: walkable_probe <cache_dir> <x> <z> <level> [radius]\n");
        return 2;
    }
    cache_dir = argv[1];
    x = atoi(argv[2]);
    z = atoi(argv[3]);
    level = atoi(argv[4]);
    if( argc > 5 )
        radius = atoi(argv[5]);

    if( !ToriRSServer_SceneBuild(cache_dir, x >> 3, z >> 3) )
    {
        fprintf(stderr, "walkable_probe: could not build a scene at %d,%d\n", x, z);
        return 1;
    }
    /* Scene-local (0,0) in absolute tiles. Inlined rather than including
     * torirs_server.h, which drags the whole server in for one line of arithmetic:
     * see ToriRSServer_SceneOrigin. */
    origin_x = ((x >> 3) - 6) * 8;
    origin_z = ((z >> 3) - 6) * 8;
    cm = ToriRSServer_SceneCollision(level);
    if( !cm )
    {
        fprintf(stderr, "walkable_probe: no collision for level %d\n", level);
        return 1;
    }

    {
        int lx = x - origin_x, lz = z - origin_z;
        int flags = (lx >= 0 && lx < cm->size_x && lz >= 0 && lz < cm->size_z)
                        ? collision_map_tile(cm, lx, lz)
                        : -1;
        /* The flag word matters as much as the verdict: a tile with no floor
         * flag at all is a tile the scene has no data for, which reads
         * "standable" and is not. FLOOR is "there is no floor here". */
        printf("%d %d %d %s flags=0x%x%s%s%s\n", x, z, level,
               standable(cm, lx, lz) ? "standable" : "blocked", flags,
               (flags & COLL_FLAG_LOC) ? " LOC" : "",
               (flags & COLL_FLAG_FLOOR) ? " BLOCKEDFLOOR" : "",
               (flags == 0) ? " (open)" : "");
    }

    if( radius > 0 )
    {
        /* North at the top, the way a map is read. */
        for( int zz = z + radius; zz >= z - radius; zz-- )
        {
            printf("%5d ", zz);
            for( int xx = x - radius; xx <= x + radius; xx++ )
            {
                int ok = standable(cm, xx - origin_x, zz - origin_z);
                printf("%c", (xx == x && zz == z) ? (ok ? '@' : 'X') : (ok ? '.' : '#'));
            }
            printf("\n");
        }
        printf("      ");
        for( int xx = x - radius; xx <= x + radius; xx++ )
            printf("%c", (xx % 10 == 0) ? '|' : ' ');
        printf("   (x from %d to %d; . standable, # blocked)\n", x - radius, x + radius);
    }
    return 0;
}
