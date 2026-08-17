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
 * `mock230_scene_build` the world uses — and reports standability, so a course
 * author can check a tile rather than hope.
 *
 *   walkable_probe <cache_dir> <x> <z> <level> [radius]
 *
 * With a radius it prints the standable tiles in that box as a picture, which
 * is what actually answers "where does this obstacle put me".
 */

#include "mock230_scene.h"

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
    {
        int flags = collision_map_tile(cm, local_x, local_z);
        /*
         * A flag word of zero is not an empty tile, it is a tile this scene
         * has NO DATA for — nothing stamped a floor there, so it is sky or
         * unloaded map. Reporting it standable is how a course lands a player
         * inside a building or off the edge of the world, so it is refused.
         */
        if( flags == 0 )
            return 0;
        return (flags & (COLL_FLAG_LOC | COLL_FLAG_FLOOR_BLOCKED)) == 0;
    }
}

/*
 * Multiloc resolution wants a player's varbits. There is no player here, and a
 * loc's *collision* is its placed record's footprint either way, so the
 * untransformed record is the right answer — the same stub, for the same
 * reason, as collision_doors_test.c's.
 */
int
mock230_varbit_get(
    struct Mock230Player* player,
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

    if( !mock230_scene_build(cache_dir, x >> 3, z >> 3) )
    {
        fprintf(stderr, "walkable_probe: could not build a scene at %d,%d\n", x, z);
        return 1;
    }
    /* Scene-local (0,0) in absolute tiles. Inlined rather than including
     * mock230.h, which drags the whole server in for one line of arithmetic:
     * see mock230_scene_origin. */
    origin_x = ((x >> 3) - 6) * 8;
    origin_z = ((z >> 3) - 6) * 8;
    cm = mock230_scene_collision(level);
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
               (flags & COLL_FLAG_FLOOR) ? " NOFLOOR" : "",
               (flags == 0) ? " (no scene data)" : "");
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
