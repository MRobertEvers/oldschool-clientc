#include "collision_map.h"

#include <rscache.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Collision map logic must match Client-TS:
 *   Client-TS/src/dash3d/CollisionMap.ts (addLoc, addWall, blockGround/unblockGround)
 *   Client-TS/src/dash3d/ClientBuild.ts (addLoc -> shape/blockwalk checks)
 * Reset: border = BOUNDS, interior = OPEN (walkable). We only add FLOOR for floor decor (block),
 * LOC for scenery/roof/centrepiece, WALL_* for walls. BFS uses BLOCK_* composite flags.
 */

/* Direction encoding: match Client.ts DirectionFlag (direction TO parent when backtracking).
 * NORTH=1, EAST=2, SOUTH=4, WEST=8. When we step to (x-1,z), parent is east -> store EAST (2). */
#define DIR_NORTH 1
#define DIR_EAST 2
#define DIR_SOUTH 4
#define DIR_WEST 8
#define DIR_NORTH_EAST (DIR_NORTH | DIR_EAST) /* 3: step to (x-1,z-1), parent NE */
#define DIR_NORTH_WEST (DIR_NORTH | DIR_WEST) /* 9: step to (x+1,z-1), parent NW */
#define DIR_SOUTH_EAST (DIR_SOUTH | DIR_EAST) /* 6: step to (x-1,z+1), parent SE */
#define DIR_SOUTH_WEST (DIR_SOUTH | DIR_WEST) /* 12: step to (x+1,z+1), parent SW */

struct CollisionMap*
collision_map_new(
    int size_x,
    int size_z)
{
    struct CollisionMap* cm = (struct CollisionMap*)malloc(sizeof(struct CollisionMap));
    memset(cm, 0, sizeof(struct CollisionMap));
    cm->size_x = size_x;
    cm->size_z = size_z;
    cm->flags = (int*)malloc((size_t)(size_x * size_z) * sizeof(int));
    collision_map_reset(cm);
    return cm;
}

void
collision_map_free(struct CollisionMap* cm)
{
    if( !cm )
        return;
    free(cm->flags);
    cm->flags = NULL;
    free(cm);
}

void
collision_map_reset(struct CollisionMap* cm)
{
    for( int x = 0; x < cm->size_x; x++ )
    {
        for( int z = 0; z < cm->size_z; z++ )
        {
            int idx = collision_map_index_at(cm, x, z);
            if( x == 0 || z == 0 || x == cm->size_x - 1 || z == cm->size_z - 1 )
                cm->flags[idx] = COLL_FLAG_BOUNDS;
            else
                cm->flags[idx] = COLL_FLAG_OPEN;
        }
    }
}

static void
collision_map_add(
    struct CollisionMap* cm,
    int x,
    int z,
    int flags)
{
    if( x < 0 || x >= cm->size_x || z < 0 || z >= cm->size_z )
        return;
    cm->flags[collision_map_index_at(cm, x, z)] |= flags;
}

void
collision_map_add_floor(
    struct CollisionMap* cm,
    int tile_x,
    int tile_z)
{
    collision_map_add(cm, tile_x, tile_z, COLL_FLAG_FLOOR);
}

void
collision_map_add_loc(
    struct CollisionMap* cm,
    int tile_x,
    int tile_z,
    int size_x,
    int size_z,
    enum CollisionLocAngle angle,
    int blockrange)
{
    int flags = COLL_FLAG_LOC;
    if( blockrange )
        flags |= COLL_FLAG_LOC_PROJ_BLOCKER;

    if( angle == COLL_ANGLE_NORTH || angle == COLL_ANGLE_SOUTH )
    {
        int tmp = size_x;
        size_x = size_z;
        size_z = tmp;
    }

    for( int tx = tile_x; tx < tile_x + size_x; tx++ )
    {
        for( int tz = tile_z; tz < tile_z + size_z; tz++ )
            collision_map_add(cm, tx, tz, flags);
    }
}

void
collision_map_add_wall(
    struct CollisionMap* cm,
    int tile_x,
    int tile_z,
    int shape,
    enum CollisionLocAngle angle,
    int blockrange)
{
    int west = blockrange ? COLL_FLAG_WALL_WEST_PROJ : COLL_FLAG_WALL_WEST;
    int east = blockrange ? COLL_FLAG_WALL_EAST_PROJ : COLL_FLAG_WALL_EAST;
    int north = blockrange ? COLL_FLAG_WALL_NORTH_PROJ : COLL_FLAG_WALL_NORTH;
    int south = blockrange ? COLL_FLAG_WALL_SOUTH_PROJ : COLL_FLAG_WALL_SOUTH;
    int nw = blockrange ? COLL_FLAG_WALL_NORTH_WEST_PROJ : COLL_FLAG_WALL_NORTH_WEST;
    int se = blockrange ? COLL_FLAG_WALL_SOUTH_EAST_PROJ : COLL_FLAG_WALL_SOUTH_EAST;
    int ne = blockrange ? COLL_FLAG_WALL_NORTH_EAST_PROJ : COLL_FLAG_WALL_NORTH_EAST;
    int sw = blockrange ? COLL_FLAG_WALL_SOUTH_WEST_PROJ : COLL_FLAG_WALL_SOUTH_WEST;

    if( shape == RSCACHE_LOC_SHAPE_WALL_SINGLE_SIDE )
    {
        if( angle == COLL_ANGLE_WEST )
        {
            collision_map_add(cm, tile_x, tile_z, west);
            collision_map_add(cm, tile_x - 1, tile_z, east);
        }
        else if( angle == COLL_ANGLE_NORTH )
        {
            collision_map_add(cm, tile_x, tile_z, north);
            collision_map_add(cm, tile_x, tile_z + 1, south);
        }
        else if( angle == COLL_ANGLE_EAST )
        {
            collision_map_add(cm, tile_x, tile_z, east);
            collision_map_add(cm, tile_x + 1, tile_z, west);
        }
        else if( angle == COLL_ANGLE_SOUTH )
        {
            collision_map_add(cm, tile_x, tile_z, south);
            collision_map_add(cm, tile_x, tile_z - 1, north);
        }
    }
    else if( shape == RSCACHE_LOC_SHAPE_WALL_TRI_CORNER || shape == RSCACHE_LOC_SHAPE_WALL_RECT_CORNER )
    {
        if( angle == COLL_ANGLE_WEST )
        {
            collision_map_add(cm, tile_x, tile_z, nw);
            collision_map_add(cm, tile_x - 1, tile_z + 1, se);
        }
        else if( angle == COLL_ANGLE_NORTH )
        {
            collision_map_add(cm, tile_x, tile_z, ne);
            collision_map_add(cm, tile_x + 1, tile_z + 1, sw);
        }
        else if( angle == COLL_ANGLE_EAST )
        {
            collision_map_add(cm, tile_x, tile_z, se);
            collision_map_add(cm, tile_x + 1, tile_z - 1, nw);
        }
        else if( angle == COLL_ANGLE_SOUTH )
        {
            collision_map_add(cm, tile_x, tile_z, sw);
            collision_map_add(cm, tile_x - 1, tile_z - 1, ne);
        }
    }
    else if( shape == RSCACHE_LOC_SHAPE_WALL_TWO_SIDES )
    {
        if( angle == COLL_ANGLE_WEST )
        {
            collision_map_add(cm, tile_x, tile_z, north | west);
            collision_map_add(cm, tile_x - 1, tile_z, east);
            collision_map_add(cm, tile_x, tile_z + 1, south);
        }
        else if( angle == COLL_ANGLE_NORTH )
        {
            collision_map_add(cm, tile_x, tile_z, north | east);
            collision_map_add(cm, tile_x, tile_z + 1, south);
            collision_map_add(cm, tile_x + 1, tile_z, west);
        }
        else if( angle == COLL_ANGLE_EAST )
        {
            collision_map_add(cm, tile_x, tile_z, south | east);
            collision_map_add(cm, tile_x + 1, tile_z, west);
            collision_map_add(cm, tile_x, tile_z - 1, north);
        }
        else if( angle == COLL_ANGLE_SOUTH )
        {
            collision_map_add(cm, tile_x, tile_z, south | west);
            collision_map_add(cm, tile_x, tile_z - 1, north);
            collision_map_add(cm, tile_x - 1, tile_z, east);
        }
    }
    if( blockrange )
        collision_map_add_wall(cm, tile_x, tile_z, shape, angle, 0);
}

/* Reference CollisionMap.testWall (CollisionMap.ts:236): can the mover, standing
 * on (src) and facing a wall loc at (dst) of the given reference shape/angle,
 * interact from here? The C collision map is scene-local (startX/startZ = 0), so
 * sx/sz are the raw src coords and the flag lookup is the src tile. */
static bool
collision_test_wall(
    struct CollisionMap* cm,
    int src_x,
    int src_z,
    int dst_x,
    int dst_z,
    int shape,
    int angle)
{
    if( src_x == dst_x && src_z == dst_z )
        return true;

    int f = cm->flags[collision_map_index_at(cm, src_x, src_z)];
    int sx = src_x, sz = src_z, dx = dst_x, dz = dst_z;

    if( shape == RSCACHE_LOC_SHAPE_WALL_SINGLE_SIDE ) /* LocShape.WALL_STRAIGHT (0) */
    {
        if( angle == COLL_ANGLE_WEST )
        {
            if( sx == dx - 1 && sz == dz )
                return true;
            if( sx == dx && sz == dz + 1 && (f & COLL_FLAG_BLOCK_NORTH) == COLL_FLAG_OPEN )
                return true;
            if( sx == dx && sz == dz - 1 && (f & COLL_FLAG_BLOCK_SOUTH) == COLL_FLAG_OPEN )
                return true;
        }
        else if( angle == COLL_ANGLE_NORTH )
        {
            if( sx == dx && sz == dz + 1 )
                return true;
            if( sx == dx - 1 && sz == dz && (f & COLL_FLAG_BLOCK_WEST) == COLL_FLAG_OPEN )
                return true;
            if( sx == dx + 1 && sz == dz && (f & COLL_FLAG_BLOCK_EAST) == COLL_FLAG_OPEN )
                return true;
        }
        else if( angle == COLL_ANGLE_EAST )
        {
            if( sx == dx + 1 && sz == dz )
                return true;
            if( sx == dx && sz == dz + 1 && (f & COLL_FLAG_BLOCK_NORTH) == COLL_FLAG_OPEN )
                return true;
            if( sx == dx && sz == dz - 1 && (f & COLL_FLAG_BLOCK_SOUTH) == COLL_FLAG_OPEN )
                return true;
        }
        else if( angle == COLL_ANGLE_SOUTH )
        {
            if( sx == dx && sz == dz - 1 )
                return true;
            if( sx == dx - 1 && sz == dz && (f & COLL_FLAG_BLOCK_WEST) == COLL_FLAG_OPEN )
                return true;
            if( sx == dx + 1 && sz == dz && (f & COLL_FLAG_BLOCK_EAST) == COLL_FLAG_OPEN )
                return true;
        }
    }
    else if( shape == RSCACHE_LOC_SHAPE_WALL_TWO_SIDES ) /* LocShape.WALL_L (2) */
    {
        if( angle == COLL_ANGLE_WEST )
        {
            if( sx == dx - 1 && sz == dz )
                return true;
            if( sx == dx && sz == dz + 1 )
                return true;
            if( sx == dx + 1 && sz == dz && (f & COLL_FLAG_BLOCK_EAST) == COLL_FLAG_OPEN )
                return true;
            if( sx == dx && sz == dz - 1 && (f & COLL_FLAG_BLOCK_SOUTH) == COLL_FLAG_OPEN )
                return true;
        }
        else if( angle == COLL_ANGLE_NORTH )
        {
            if( sx == dx - 1 && sz == dz && (f & COLL_FLAG_BLOCK_WEST) == COLL_FLAG_OPEN )
                return true;
            if( sx == dx && sz == dz + 1 )
                return true;
            if( sx == dx + 1 && sz == dz )
                return true;
            if( sx == dx && sz == dz - 1 && (f & COLL_FLAG_BLOCK_SOUTH) == COLL_FLAG_OPEN )
                return true;
        }
        else if( angle == COLL_ANGLE_EAST )
        {
            if( sx == dx - 1 && sz == dz && (f & COLL_FLAG_BLOCK_WEST) == COLL_FLAG_OPEN )
                return true;
            if( sx == dx && sz == dz + 1 && (f & COLL_FLAG_BLOCK_NORTH) == COLL_FLAG_OPEN )
                return true;
            if( sx == dx + 1 && sz == dz )
                return true;
            if( sx == dx && sz == dz - 1 )
                return true;
        }
        else if( angle == COLL_ANGLE_SOUTH )
        {
            if( sx == dx - 1 && sz == dz )
                return true;
            if( sx == dx && sz == dz + 1 && (f & COLL_FLAG_BLOCK_NORTH) == COLL_FLAG_OPEN )
                return true;
            if( sx == dx + 1 && sz == dz && (f & COLL_FLAG_BLOCK_EAST) == COLL_FLAG_OPEN )
                return true;
            if( sx == dx && sz == dz - 1 )
                return true;
        }
    }
    else if( shape == RSCACHE_LOC_SHAPE_WALL_DIAGONAL ) /* LocShape.WALL_DIAGONAL (9) */
    {
        if( sx == dx && sz == dz + 1 && (f & COLL_FLAG_WALL_SOUTH) == COLL_FLAG_OPEN )
            return true;
        if( sx == dx && sz == dz - 1 && (f & COLL_FLAG_WALL_NORTH) == COLL_FLAG_OPEN )
            return true;
        if( sx == dx - 1 && sz == dz && (f & COLL_FLAG_WALL_EAST) == COLL_FLAG_OPEN )
            return true;
        if( sx == dx + 1 && sz == dz && (f & COLL_FLAG_WALL_WEST) == COLL_FLAG_OPEN )
            return true;
    }
    return false;
}

/* Reference CollisionMap.testWDecor (CollisionMap.ts:337): approach test for a
 * diagonal wall-decoration loc. shape is the reference locShape (locShape - 1 as
 * the caller passes). Uses the raw WALL_* bits (not BLOCK_*). */
static bool
collision_test_wdecor(
    struct CollisionMap* cm,
    int src_x,
    int src_z,
    int dst_x,
    int dst_z,
    int shape,
    int angle)
{
    if( src_x == dst_x && src_z == dst_z )
        return true;

    int f = cm->flags[collision_map_index_at(cm, src_x, src_z)];
    int sx = src_x, sz = src_z, dx = dst_x, dz = dst_z;

    if( shape == RSCACHE_LOC_SHAPE_WALL_DECOR_DIAGONAL_OUTSIDE || /* 6 */
        shape == RSCACHE_LOC_SHAPE_WALL_DECOR_DIAGONAL_INSIDE )   /* 7 */
    {
        if( shape == RSCACHE_LOC_SHAPE_WALL_DECOR_DIAGONAL_INSIDE )
            angle = (angle + 2) & 0x3;

        if( angle == COLL_ANGLE_WEST )
        {
            if( sx == dx + 1 && sz == dz && (f & COLL_FLAG_WALL_WEST) == COLL_FLAG_OPEN )
                return true;
            if( sx == dx && sz == dz - 1 && (f & COLL_FLAG_WALL_NORTH) == COLL_FLAG_OPEN )
                return true;
        }
        else if( angle == COLL_ANGLE_NORTH )
        {
            if( sx == dx - 1 && sz == dz && (f & COLL_FLAG_WALL_EAST) == COLL_FLAG_OPEN )
                return true;
            if( sx == dx && sz == dz - 1 && (f & COLL_FLAG_WALL_NORTH) == COLL_FLAG_OPEN )
                return true;
        }
        else if( angle == COLL_ANGLE_EAST )
        {
            if( sx == dx - 1 && sz == dz && (f & COLL_FLAG_WALL_EAST) == COLL_FLAG_OPEN )
                return true;
            if( sx == dx && sz == dz + 1 && (f & COLL_FLAG_WALL_SOUTH) == COLL_FLAG_OPEN )
                return true;
        }
        else if( angle == COLL_ANGLE_SOUTH )
        {
            if( sx == dx + 1 && sz == dz && (f & COLL_FLAG_WALL_WEST) == COLL_FLAG_OPEN )
                return true;
            if( sx == dx && sz == dz + 1 && (f & COLL_FLAG_WALL_SOUTH) == COLL_FLAG_OPEN )
                return true;
        }
    }
    else if( shape == RSCACHE_LOC_SHAPE_WALL_DECOR_DIAGONAL_DOUBLE ) /* 8 */
    {
        if( sx == dx && sz == dz + 1 && (f & COLL_FLAG_WALL_SOUTH) == COLL_FLAG_OPEN )
            return true;
        if( sx == dx && sz == dz - 1 && (f & COLL_FLAG_WALL_NORTH) == COLL_FLAG_OPEN )
            return true;
        if( sx == dx - 1 && sz == dz && (f & COLL_FLAG_WALL_EAST) == COLL_FLAG_OPEN )
            return true;
        if( sx == dx + 1 && sz == dz && (f & COLL_FLAG_WALL_WEST) == COLL_FLAG_OPEN )
            return true;
    }
    return false;
}

/* Reference CollisionMap.testLoc (CollisionMap.ts:392): a tile inside the loc's
 * (size_x x size_z) footprint, or beside an edge whose wall is open and whose
 * approach direction isn't blocked by forceapproach (DirectionFlag bits). */
static bool
collision_test_loc(
    struct CollisionMap* cm,
    int src_x,
    int src_z,
    int dst_x,
    int dst_z,
    int size_x,
    int size_z,
    int forceapproach)
{
    int max_x = dst_x + size_x - 1;
    int max_z = dst_z + size_z - 1;
    int f = cm->flags[collision_map_index_at(cm, src_x, src_z)];

    if( src_x >= dst_x && src_x <= max_x && src_z >= dst_z && src_z <= max_z )
        return true;
    if( src_x == dst_x - 1 && src_z >= dst_z && src_z <= max_z &&
        (f & COLL_FLAG_WALL_EAST) == COLL_FLAG_OPEN && (forceapproach & DIR_WEST) == 0 )
        return true;
    if( src_x == max_x + 1 && src_z >= dst_z && src_z <= max_z &&
        (f & COLL_FLAG_WALL_WEST) == COLL_FLAG_OPEN && (forceapproach & DIR_EAST) == 0 )
        return true;
    if( src_z == dst_z - 1 && src_x >= dst_x && src_x <= max_x &&
        (f & COLL_FLAG_WALL_NORTH) == COLL_FLAG_OPEN && (forceapproach & DIR_SOUTH) == 0 )
        return true;
    if( src_z == max_z + 1 && src_x >= dst_x && src_x <= max_x &&
        (f & COLL_FLAG_WALL_SOUTH) == COLL_FLAG_OPEN && (forceapproach & DIR_NORTH) == 0 )
        return true;
    return false;
}

/* Arrival predicate for the flood: exact destination, or (when `approach` is
 * set) any loc/obj approach test the reference tryMove checks per popped tile
 * (Client.ts:5885-5906). */
static bool
collision_flood_arrived(
    struct CollisionMap* cm,
    int x,
    int z,
    int dst_x,
    int dst_z,
    struct CollisionApproach const* approach)
{
    if( x == dst_x && z == dst_z )
        return true;
    if( !approach )
        return false;

    int shape = approach->loc_shape;
    int angle = approach->loc_angle;

    if( shape != RSCACHE_LOC_SHAPE_WALL_SINGLE_SIDE ) /* locShape !== WALL_STRAIGHT */
    {
        if( (shape < RSCACHE_LOC_SHAPE_WALL_DECOR_OUTSIDE /* WALLDECOR_STRAIGHT_OFFSET (5) */ ||
             shape == RSCACHE_LOC_SHAPE_SCENERY /* CENTREPIECE_STRAIGHT (10) */) &&
            collision_test_wall(cm, x, z, dst_x, dst_z, shape - 1, angle) )
            return true;
        if( shape < RSCACHE_LOC_SHAPE_SCENERY /* CENTREPIECE_STRAIGHT (10) */ &&
            collision_test_wdecor(cm, x, z, dst_x, dst_z, shape - 1, angle) )
            return true;
    }

    if( approach->loc_width != 0 && approach->loc_length != 0 &&
        collision_test_loc(
            cm, x, z, dst_x, dst_z, approach->loc_width, approach->loc_length,
            approach->forceapproach) )
        return true;

    return false;
}

/* Reference tryMove flood (Client.ts:5860-6008 ground-click arrival): fills
 * dir_map with the DirectionFlag toward each tile's parent and dist_map with
 * step counts, stopping early when a tile satisfies `approach` (or is the exact
 * destination when approach is NULL). The arrival tile is written to
 * *out_arrive_x/z. All four scratch arrays are size_x*size_z ints. Returns 1
 * when arrival was reached; on 0 the maps are fully flooded (needed for the
 * try-nearest fallback). */
static int
collision_flood(
    struct CollisionMap* cm,
    int src_x,
    int src_z,
    int dst_x,
    int dst_z,
    struct CollisionApproach const* approach,
    int* dir_map,
    int* dist_map,
    int* queue_x,
    int* queue_z,
    int* out_arrive_x,
    int* out_arrive_z)
{
    const int buf_size = cm->size_x * cm->size_z;

    memset(dir_map, 0, (size_t)buf_size * sizeof(int));
    for( int i = 0; i < buf_size; i++ )
        dist_map[i] = 99999999;

    int src_idx = collision_map_index_at(cm, src_x, src_z);
    dir_map[src_idx] = 99;
    dist_map[src_idx] = 0;

    int steps = 0;
    int length = 0;
    queue_x[steps] = src_x;
    queue_z[steps++] = src_z;

    while( length != steps )
    {
        int x = queue_x[length];
        int z = queue_z[length];
        length = (length + 1) % buf_size;

        if( collision_flood_arrived(cm, x, z, dst_x, dst_z, approach) )
        {
            if( out_arrive_x )
                *out_arrive_x = x;
            if( out_arrive_z )
                *out_arrive_z = z;
            return 1;
        }

        int next_cost = dist_map[collision_map_index_at(cm, x, z)] + 1;
        int idx = 0;

        if( collision_map_can_step_west(cm, x, z) )
        {
            idx = collision_map_index_at(cm, x - 1, z);
            if( dir_map[idx] == 0 )
            {
                queue_x[steps] = x - 1;
                queue_z[steps] = z;
                steps = (steps + 1) % buf_size;
                dir_map[idx] = DIR_EAST;
                dist_map[idx] = next_cost;
            }
        }

        if( collision_map_can_step_east(cm, x, z) )
        {
            idx = collision_map_index_at(cm, x + 1, z);
            if( dir_map[idx] == 0 )
            {
                queue_x[steps] = x + 1;
                queue_z[steps] = z;
                steps = (steps + 1) % buf_size;
                dir_map[idx] = DIR_WEST;
                dist_map[idx] = next_cost;
            }
        }

        if( collision_map_can_step_south(cm, x, z) )
        {
            idx = collision_map_index_at(cm, x, z - 1);
            if( dir_map[idx] == 0 )
            {
                queue_x[steps] = x;
                queue_z[steps] = z - 1;
                steps = (steps + 1) % buf_size;
                dir_map[idx] = DIR_NORTH;
                dist_map[idx] = next_cost;
            }
        }

        if( collision_map_can_step_north(cm, x, z) )
        {
            idx = collision_map_index_at(cm, x, z + 1);
            if( dir_map[idx] == 0 )
            {
                queue_x[steps] = x;
                queue_z[steps] = z + 1;
                steps = (steps + 1) % buf_size;
                dir_map[idx] = DIR_SOUTH;
                dist_map[idx] = next_cost;
            }
        }

        if( collision_map_can_step_diagonal_south_west(cm, x, z) )
        {
            idx = collision_map_index_at(cm, x - 1, z - 1);
            if( dir_map[idx] == 0 )
            {
                queue_x[steps] = x - 1;
                queue_z[steps] = z - 1;
                steps = (steps + 1) % buf_size;
                dir_map[idx] = DIR_NORTH_EAST;
                dist_map[idx] = next_cost;
            }
        }
        if( collision_map_can_step_diagonal_south_east(cm, x, z) )
        {
            idx = collision_map_index_at(cm, x + 1, z - 1);
            if( dir_map[idx] == 0 )
            {
                queue_x[steps] = x + 1;
                queue_z[steps] = z - 1;
                steps = (steps + 1) % buf_size;
                dir_map[idx] = DIR_NORTH_WEST;
                dist_map[idx] = next_cost;
            }
        }
        if( collision_map_can_step_diagonal_north_west(cm, x, z) )
        {
            idx = collision_map_index_at(cm, x - 1, z + 1);
            if( dir_map[idx] == 0 )
            {
                queue_x[steps] = x - 1;
                queue_z[steps] = z + 1;
                steps = (steps + 1) % buf_size;
                dir_map[idx] = DIR_SOUTH_EAST;
                dist_map[idx] = next_cost;
            }
        }
        if( collision_map_can_step_diagonal_north_east(cm, x, z) )
        {
            idx = collision_map_index_at(cm, x + 1, z + 1);
            if( dir_map[idx] == 0 )
            {
                queue_x[steps] = x + 1;
                queue_z[steps] = z + 1;
                steps = (steps + 1) % buf_size;
                dir_map[idx] = DIR_SOUTH_WEST;
                dist_map[idx] = next_cost;
            }
        }
    }
    return 0;
}

/* Backtrace a flooded dir_map from the arrival tile toward the source, writing
 * route[0] = arrival tile and one entry per direction change, ascending toward
 * the source (the source tile itself is never stored — reference routeX/routeZ
 * layout). Returns route length (>= 1), or -1 on overflow of max_route. */
static int
collision_route_backtrace(
    struct CollisionMap* cm,
    int src_x,
    int src_z,
    int end_x,
    int end_z,
    int const* dir_map,
    int* route_x,
    int* route_z,
    int max_route)
{
    int x = end_x;
    int z = end_z;
    int length = 0;
    route_x[length] = x;
    route_z[length++] = z;

    int dir = dir_map[collision_map_index_at(cm, x, z)];
    int next = dir;
    while( x != src_x || z != src_z )
    {
        if( next != dir )
        {
            dir = next;
            if( length >= max_route )
                return -1;
            route_x[length] = x;
            route_z[length++] = z;
        }

        if( (next & DIR_EAST) != 0 )
            x++;
        else if( (next & DIR_WEST) != 0 )
            x--;
        if( (next & DIR_NORTH) != 0 )
            z++;
        else if( (next & DIR_SOUTH) != 0 )
            z--;

        next = dir_map[collision_map_index_at(cm, x, z)];
    }
    return length;
}

int
collision_map_bfs_path(
    struct CollisionMap* cm,
    int src_x,
    int src_z,
    int dst_x,
    int dst_z,
    int* path_x,
    int* path_z,
    int max_path)
{
    const int buf_size = cm->size_x * cm->size_z;

    int* bfs_direction = (int*)malloc((size_t)buf_size * sizeof(int));
    int* bfs_cost = (int*)malloc((size_t)buf_size * sizeof(int));
    int* bfs_step_x = (int*)malloc((size_t)buf_size * sizeof(int));
    int* bfs_step_z = (int*)malloc((size_t)buf_size * sizeof(int));
    if( !bfs_direction || !bfs_cost || !bfs_step_x || !bfs_step_z )
    {
        free(bfs_direction);
        free(bfs_cost);
        free(bfs_step_x);
        free(bfs_step_z);
        return -1;
    }

    int arrived = collision_flood(
        cm, src_x, src_z, dst_x, dst_z, NULL, bfs_direction, bfs_cost, bfs_step_x, bfs_step_z, NULL,
        NULL);

    if( !arrived )
    {
        free(bfs_direction);
        free(bfs_cost);
        free(bfs_step_x);
        free(bfs_step_z);
        return -1;
    }

    int trace_x = dst_x, trace_z = dst_z;
    int path_len = 0;
    int tmp_x[256], tmp_z[256];
    assert(max_path <= 256);

    while( path_len < max_path && (trace_x != src_x || trace_z != src_z) )
    {
        int dir = bfs_direction[collision_map_index_at(cm, trace_x, trace_z)];
        tmp_x[path_len] = trace_x;
        tmp_z[path_len] = trace_z;
        path_len++;

        if( (dir & DIR_EAST) != 0 )
            trace_x++;
        else if( (dir & DIR_WEST) != 0 )
            trace_x--;
        if( (dir & DIR_NORTH) != 0 )
            trace_z++;
        else if( (dir & DIR_SOUTH) != 0 )
            trace_z--;
    }

    int n = path_len < max_path ? path_len : max_path;
    for( int i = 0; i < n; i++ )
    {
        path_x[i] = tmp_x[n - 1 - i];
        path_z[i] = tmp_z[n - 1 - i];
    }

    free(bfs_direction);
    free(bfs_cost);
    free(bfs_step_x);
    free(bfs_step_z);
    return n;
}

int
collision_map_try_route(
    struct CollisionMap* cm,
    int src_x,
    int src_z,
    int dst_x,
    int dst_z,
    bool try_nearest,
    int* route_x,
    int* route_z,
    int max_route,
    int* out_used_nearest)
{
    const int buf_size = cm->size_x * cm->size_z;
    int end_x = dst_x;
    int end_z = dst_z;

    if( out_used_nearest )
        *out_used_nearest = 0;
    if( src_x < 0 || src_z < 0 || src_x >= cm->size_x || src_z >= cm->size_z || dst_x < 0 ||
        dst_z < 0 || dst_x >= cm->size_x || dst_z >= cm->size_z )
        return -1;

    int* dir_map = (int*)malloc((size_t)buf_size * sizeof(int));
    int* dist_map = (int*)malloc((size_t)buf_size * sizeof(int));
    int* queue_x = (int*)malloc((size_t)buf_size * sizeof(int));
    int* queue_z = (int*)malloc((size_t)buf_size * sizeof(int));
    if( !dir_map || !dist_map || !queue_x || !queue_z )
    {
        free(dir_map);
        free(dist_map);
        free(queue_x);
        free(queue_z);
        return -1;
    }

    int arrived = collision_flood(
        cm, src_x, src_z, dst_x, dst_z, NULL, dir_map, dist_map, queue_x, queue_z, NULL, NULL);

    if( !arrived && try_nearest )
    {
        /* Reference nearest fallback: lowest-distance flooded tile in the 3x3
         * ring around the destination (dist must be < 100). */
        int min = 100;
        for( int px = dst_x - 1; px <= dst_x + 1; px++ )
        {
            for( int pz = dst_z - 1; pz <= dst_z + 1; pz++ )
            {
                if( px < 0 || pz < 0 || px >= cm->size_x || pz >= cm->size_z )
                    continue;
                int dist = dist_map[collision_map_index_at(cm, px, pz)];
                if( dist < min )
                {
                    min = dist;
                    end_x = px;
                    end_z = pz;
                    arrived = 1;
                    if( out_used_nearest )
                        *out_used_nearest = 1;
                }
            }
        }
    }

    int length = arrived
        ? collision_route_backtrace(cm, src_x, src_z, end_x, end_z, dir_map, route_x, route_z, max_route)
        : -1;

    free(dir_map);
    free(dist_map);
    free(queue_x);
    free(queue_z);
    return length;
}

int
collision_map_try_route_op(
    struct CollisionMap* cm,
    int src_x,
    int src_z,
    int dst_x,
    int dst_z,
    struct CollisionApproach const* approach,
    int* route_x,
    int* route_z,
    int max_route)
{
    const int buf_size = cm->size_x * cm->size_z;

    if( src_x < 0 || src_z < 0 || src_x >= cm->size_x || src_z >= cm->size_z || dst_x < 0 ||
        dst_z < 0 || dst_x >= cm->size_x || dst_z >= cm->size_z )
        return -1;

    int* dir_map = (int*)malloc((size_t)buf_size * sizeof(int));
    int* dist_map = (int*)malloc((size_t)buf_size * sizeof(int));
    int* queue_x = (int*)malloc((size_t)buf_size * sizeof(int));
    int* queue_z = (int*)malloc((size_t)buf_size * sizeof(int));
    if( !dir_map || !dist_map || !queue_x || !queue_z )
    {
        free(dir_map);
        free(dist_map);
        free(queue_x);
        free(queue_z);
        return -1;
    }

    /* Reference tryMove type 2: tryNearest = false, so no 3x3 fallback — arrival
     * is whichever approach tile the flood reaches first (or the exact tile). */
    int end_x = dst_x;
    int end_z = dst_z;
    int arrived = collision_flood(
        cm, src_x, src_z, dst_x, dst_z, approach, dir_map, dist_map, queue_x, queue_z, &end_x,
        &end_z);

    int length = arrived
        ? collision_route_backtrace(cm, src_x, src_z, end_x, end_z, dir_map, route_x, route_z, max_route)
        : -1;

    free(dir_map);
    free(dist_map);
    free(queue_x);
    free(queue_z);
    return length;
}
