#include "collision_map.h"

#include <rscache.h>

#include <assert.h>
#include <limits.h>
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
    cm->route_window = 0; /* whole map, until the owner states a window */
    cm->flags = (int*)malloc((size_t)(size_x * size_z) * sizeof(int));
    collision_map_reset(cm);
    return cm;
}

void
collision_map_set_route_window(
    struct CollisionMap* cm,
    int window)
{
    assert(cm);
    assert(window >= 0);
    cm->route_window = window;
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

static void
collision_map_remove(
    struct CollisionMap* cm,
    int x,
    int z,
    int flags)
{
    if( x < 0 || x >= cm->size_x || z < 0 || z >= cm->size_z )
        return;
    cm->flags[collision_map_index_at(cm, x, z)] &= ~flags;
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
collision_map_change_roof(
    struct CollisionMap* cm,
    int tile_x,
    int tile_z,
    int add)
{
    if( add )
        collision_map_add(cm, tile_x, tile_z, COLL_FLAG_ROOF);
    else
        collision_map_remove(cm, tile_x, tile_z, COLL_FLAG_ROOF);
}

/*
 * rsmod CollisionStrategy.canMove, verbatim per branch.
 *
 * NORMAL is the mask test every other reader of this file already does; the
 * other four are the moverestrict cases. LINE_OF_SIGHT is the one worth
 * reading twice: it does not consult a separate flag tier, it *reinterprets*
 * the mask, shifting the wall and loc bits up into their projectile twins
 * (0x1 << 9 == WALL_NORTH_WEST_PROJ, 0x100 << 9 == LOC_PROJ_BLOCKER), so a
 * mover walks exactly where a projectile could fly.
 */
int
collision_can_move(
    int coll_type,
    int tile_flag,
    int block_flag)
{
    static const int k_los_movement = COLL_FLAG_WALL_NORTH_WEST | COLL_FLAG_WALL_NORTH |
                                      COLL_FLAG_WALL_NORTH_EAST | COLL_FLAG_WALL_EAST |
                                      COLL_FLAG_WALL_SOUTH_EAST | COLL_FLAG_WALL_SOUTH |
                                      COLL_FLAG_WALL_SOUTH_WEST | COLL_FLAG_WALL_WEST |
                                      COLL_FLAG_LOC;

    switch( coll_type )
    {
    case COLL_TYPE_NORMAL:
        return (tile_flag & block_flag) == COLL_FLAG_OPEN;
    case COLL_TYPE_BLOCKED:
    {
        int mask = block_flag & ~COLL_FLAG_FLOOR;
        return (tile_flag & mask) == COLL_FLAG_OPEN &&
               (tile_flag & COLL_FLAG_FLOOR) != COLL_FLAG_OPEN;
    }
    case COLL_TYPE_INDOORS:
        return (tile_flag & block_flag) == COLL_FLAG_OPEN &&
               (tile_flag & COLL_FLAG_ROOF) != COLL_FLAG_OPEN;
    case COLL_TYPE_OUTDOORS:
        return (tile_flag & (block_flag | COLL_FLAG_ROOF)) == COLL_FLAG_OPEN;
    case COLL_TYPE_LINE_OF_SIGHT:
    {
        /* The proj twins are exactly nine bits above the walk bits in this
         * layout too, which is what makes the reference's shift portable here.
         */
        _Static_assert(
            (COLL_FLAG_WALL_NORTH_WEST << 9) == COLL_FLAG_WALL_NORTH_WEST_PROJ,
            "wall bits must sit nine below their proj twins");
        _Static_assert(
            (COLL_FLAG_LOC << 9) == COLL_FLAG_LOC_PROJ_BLOCKER,
            "LOC must sit nine below LOC_PROJ_BLOCKER");
        return (tile_flag & ((block_flag & k_los_movement) << 9)) == COLL_FLAG_OPEN;
    }
    default:
        return 0;
    }
}

/* Shared core for add_loc / del_loc: `add` selects OR (add) vs AND-NOT (del) so
 * the two are guaranteed exact inverses. */
static void
collision_map_loc_apply(
    struct CollisionMap* cm,
    int tile_x,
    int tile_z,
    int size_x,
    int size_z,
    enum CollisionLocAngle angle,
    int blockrange,
    int add)
{
    void (*op)(struct CollisionMap*, int, int, int) =
        add ? collision_map_add : collision_map_remove;
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
            op(cm, tx, tz, flags);
    }
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
    collision_map_loc_apply(cm, tile_x, tile_z, size_x, size_z, angle, blockrange, 1);
}

void
collision_map_del_loc(
    struct CollisionMap* cm,
    int tile_x,
    int tile_z,
    int size_x,
    int size_z,
    enum CollisionLocAngle angle,
    int blockrange)
{
    collision_map_loc_apply(cm, tile_x, tile_z, size_x, size_z, angle, blockrange, 0);
}

void
collision_map_del_floor(
    struct CollisionMap* cm,
    int tile_x,
    int tile_z)
{
    collision_map_remove(cm, tile_x, tile_z, COLL_FLAG_FLOOR);
}

/* Shared core for add_wall / del_wall: `add` selects OR vs AND-NOT so the two
 * are guaranteed exact inverses. */
static void
collision_map_wall_apply(
    struct CollisionMap* cm,
    int tile_x,
    int tile_z,
    int shape,
    enum CollisionLocAngle angle,
    int blockrange,
    int add)
{
    void (*op)(struct CollisionMap*, int, int, int) =
        add ? collision_map_add : collision_map_remove;
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
            op(cm, tile_x, tile_z, west);
            op(cm, tile_x - 1, tile_z, east);
        }
        else if( angle == COLL_ANGLE_NORTH )
        {
            op(cm, tile_x, tile_z, north);
            op(cm, tile_x, tile_z + 1, south);
        }
        else if( angle == COLL_ANGLE_EAST )
        {
            op(cm, tile_x, tile_z, east);
            op(cm, tile_x + 1, tile_z, west);
        }
        else if( angle == COLL_ANGLE_SOUTH )
        {
            op(cm, tile_x, tile_z, south);
            op(cm, tile_x, tile_z - 1, north);
        }
    }
    else if( shape == RSCACHE_LOC_SHAPE_WALL_TRI_CORNER || shape == RSCACHE_LOC_SHAPE_WALL_RECT_CORNER )
    {
        if( angle == COLL_ANGLE_WEST )
        {
            op(cm, tile_x, tile_z, nw);
            op(cm, tile_x - 1, tile_z + 1, se);
        }
        else if( angle == COLL_ANGLE_NORTH )
        {
            op(cm, tile_x, tile_z, ne);
            op(cm, tile_x + 1, tile_z + 1, sw);
        }
        else if( angle == COLL_ANGLE_EAST )
        {
            op(cm, tile_x, tile_z, se);
            op(cm, tile_x + 1, tile_z - 1, nw);
        }
        else if( angle == COLL_ANGLE_SOUTH )
        {
            op(cm, tile_x, tile_z, sw);
            op(cm, tile_x - 1, tile_z - 1, ne);
        }
    }
    else if( shape == RSCACHE_LOC_SHAPE_WALL_TWO_SIDES )
    {
        if( angle == COLL_ANGLE_WEST )
        {
            op(cm, tile_x, tile_z, north | west);
            op(cm, tile_x - 1, tile_z, east);
            op(cm, tile_x, tile_z + 1, south);
        }
        else if( angle == COLL_ANGLE_NORTH )
        {
            op(cm, tile_x, tile_z, north | east);
            op(cm, tile_x, tile_z + 1, south);
            op(cm, tile_x + 1, tile_z, west);
        }
        else if( angle == COLL_ANGLE_EAST )
        {
            op(cm, tile_x, tile_z, south | east);
            op(cm, tile_x + 1, tile_z, west);
            op(cm, tile_x, tile_z - 1, north);
        }
        else if( angle == COLL_ANGLE_SOUTH )
        {
            op(cm, tile_x, tile_z, south | west);
            op(cm, tile_x, tile_z - 1, north);
            op(cm, tile_x - 1, tile_z, east);
        }
    }
    if( blockrange )
        collision_map_wall_apply(cm, tile_x, tile_z, shape, angle, 0, add);
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
    collision_map_wall_apply(cm, tile_x, tile_z, shape, angle, blockrange, 1);
}

void
collision_map_del_wall(
    struct CollisionMap* cm,
    int tile_x,
    int tile_z,
    int shape,
    enum CollisionLocAngle angle,
    int blockrange)
{
    collision_map_wall_apply(cm, tile_x, tile_z, shape, angle, blockrange, 0);
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

/* Does a size x size mover footprint anchored at (src) overlap the rect
 * [min_x..max_x] x [min_z..max_z]? */
static bool
collision_footprint_overlaps(
    int src_x,
    int src_z,
    int size,
    int min_x,
    int min_z,
    int max_x,
    int max_z)
{
    return src_x <= max_x && src_x + size - 1 >= min_x && src_z <= max_z &&
           src_z + size - 1 >= min_z;
}

/*
 * rsmod reachRectangle1 / reachRectangleN — source-tile wall bits for size 1,
 * dest-edge wall bits for size N. blocked_sides is BlockAccessFlag /
 * forceapproach (1 N, 2 E, 4 S, 8 W), naming the side the mover stands on.
 *
 * The earlier both-tiles check was an XRSPS invention; do not reinstate it.
 */
static bool
collision_test_rect_adjacent(
    struct CollisionMap* cm,
    int src_x,
    int src_z,
    int dst_x,
    int dst_z,
    struct CollisionApproach const* approach)
{
    int size = approach->mover_size > 0 ? approach->mover_size : 1;
    int width = approach->loc_width > 0 ? approach->loc_width : 1;
    int length = approach->loc_length > 0 ? approach->loc_length : 1;
    int min_x = dst_x;
    int min_z = dst_z;
    int max_x = dst_x + width - 1;
    int max_z = dst_z + length - 1;
    int blocked = approach->blocked_sides;

    if( collision_footprint_overlaps(src_x, src_z, size, min_x, min_z, max_x, max_z) )
        return approach->allow_overlap != 0;

    if( size == 1 )
    {
        int f = cm->flags[collision_map_index_at(cm, src_x, src_z)];

        if( src_x == dst_x - 1 && src_z >= dst_z && src_z <= max_z &&
            (f & COLL_FLAG_WALL_EAST) == COLL_FLAG_OPEN && (blocked & DIR_WEST) == 0 )
            return true;
        if( src_x == max_x + 1 && src_z >= dst_z && src_z <= max_z &&
            (f & COLL_FLAG_WALL_WEST) == COLL_FLAG_OPEN && (blocked & DIR_EAST) == 0 )
            return true;
        if( src_z + 1 == dst_z && src_x >= dst_x && src_x <= max_x &&
            (f & COLL_FLAG_WALL_NORTH) == COLL_FLAG_OPEN && (blocked & DIR_SOUTH) == 0 )
            return true;
        if( src_z == max_z + 1 && src_x >= dst_x && src_x <= max_x &&
            (f & COLL_FLAG_WALL_SOUTH) == COLL_FLAG_OPEN && (blocked & DIR_NORTH) == 0 )
            return true;
        return false;
    }

    /* size-N: rsmod reachRectangleN — scan the shared edge reading the
     * destination's facing wall bit. */
    {
        int src_east = src_x + size;
        int src_north = src_z + size;
        int dest_east = dst_x + width;
        int dest_north = dst_z + length;

        if( dest_east == src_x && (blocked & DIR_EAST) == 0 )
        {
            int from_z = src_z > dst_z ? src_z : dst_z;
            int to_z = src_north < dest_north ? src_north : dest_north;
            for( int side_z = from_z; side_z < to_z; side_z++ )
            {
                if( (cm->flags[collision_map_index_at(cm, dest_east - 1, side_z)] &
                     COLL_FLAG_WALL_EAST) == COLL_FLAG_OPEN )
                    return true;
            }
        }
        else if( src_east == dst_x && (blocked & DIR_WEST) == 0 )
        {
            int from_z = src_z > dst_z ? src_z : dst_z;
            int to_z = src_north < dest_north ? src_north : dest_north;
            for( int side_z = from_z; side_z < to_z; side_z++ )
            {
                if( (cm->flags[collision_map_index_at(cm, dst_x, side_z)] &
                     COLL_FLAG_WALL_WEST) == COLL_FLAG_OPEN )
                    return true;
            }
        }
        else if( src_z == dest_north && (blocked & DIR_NORTH) == 0 )
        {
            int from_x = src_x > dst_x ? src_x : dst_x;
            int to_x = src_east < dest_east ? src_east : dest_east;
            for( int side_x = from_x; side_x < to_x; side_x++ )
            {
                if( (cm->flags[collision_map_index_at(cm, side_x, dest_north - 1)] &
                     COLL_FLAG_WALL_NORTH) == COLL_FLAG_OPEN )
                    return true;
            }
        }
        else if( dst_z == src_north && (blocked & DIR_SOUTH) == 0 )
        {
            int from_x = src_x > dst_x ? src_x : dst_x;
            int to_x = src_east < dest_east ? src_east : dest_east;
            for( int side_x = from_x; side_x < to_x; side_x++ )
            {
                if( (cm->flags[collision_map_index_at(cm, side_x, dst_z)] &
                     COLL_FLAG_WALL_SOUTH) == COLL_FLAG_OPEN )
                    return true;
            }
        }
    }
    return false;
}

/* rsmod reachExclusiveRectangle: adjacent and must NOT overlap. */
static bool
collision_test_rect_exclusive(
    struct CollisionMap* cm,
    int src_x,
    int src_z,
    int dst_x,
    int dst_z,
    struct CollisionApproach const* approach)
{
    int size = approach->mover_size > 0 ? approach->mover_size : 1;
    int width = approach->loc_width > 0 ? approach->loc_width : 1;
    int length = approach->loc_length > 0 ? approach->loc_length : 1;
    struct CollisionApproach adj = *approach;

    if( collision_footprint_overlaps(
            src_x, src_z, size, dst_x, dst_z, dst_x + width - 1, dst_z + length - 1) )
        return false;
    adj.allow_overlap = 0;
    return collision_test_rect_adjacent(cm, src_x, src_z, dst_x, dst_z, &adj);
}

/* rsmod RectRouteStrategy: stand anywhere on the rect. */
static bool
collision_test_rect_inside(
    int src_x,
    int src_z,
    int dst_x,
    int dst_z,
    struct CollisionApproach const* approach)
{
    int size = approach->mover_size > 0 ? approach->mover_size : 1;
    return collision_footprint_overlaps(
        src_x, src_z, size, dst_x, dst_z, dst_x + (approach->loc_width > 0 ? approach->loc_width : 1) - 1,
        dst_z + (approach->loc_length > 0 ? approach->loc_length : 1) - 1);
}

/* rsmod RectWithinRangeRouteStrategy: never on the rect, else rect-to-rect
 * Chebyshev distance within range. */
static bool
collision_test_rect_within_range(
    int src_x,
    int src_z,
    int dst_x,
    int dst_z,
    struct CollisionApproach const* approach)
{
    int size = approach->mover_size > 0 ? approach->mover_size : 1;
    int min_x = dst_x;
    int min_z = dst_z;
    int max_x = dst_x + (approach->loc_width > 0 ? approach->loc_width : 1) - 1;
    int max_z = dst_z + (approach->loc_length > 0 ? approach->loc_length : 1) - 1;
    int src_max_x = src_x + size - 1;
    int src_max_z = src_z + size - 1;

    if( collision_footprint_overlaps(src_x, src_z, size, min_x, min_z, max_x, max_z) )
        return false;

    int dx = src_x > max_x ? src_x - max_x : (min_x > src_max_x ? min_x - src_max_x : 0);
    int dz = src_z > max_z ? src_z - max_z : (min_z > src_max_z ? min_z - src_max_z : 0);
    int chebyshev = dx > dz ? dx : dz;
    return chebyshev <= (approach->range > 0 ? approach->range : 1);
}

/* Client-TS tryMove's per-popped-tile arrival tests (Client.ts:6034-6058). */
static bool
collision_test_legacy_shape(
    struct CollisionMap* cm,
    int x,
    int z,
    int dst_x,
    int dst_z,
    struct CollisionApproach const* approach)
{
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

/*
 * Arrival predicate for the flood.
 *
 * Exact-tile shortcut: rsmod / LostCity ReachStrategy.reached opens with
 * `if (exitStrategy != RECTANGLE_EXCLUSIVE && srcX == destX && srcZ == destZ)
 * return true`. Exclusive-rectangle is the only strategy that refuses it —
 * that is what keeps a player from "reaching" an NPC by standing on it.
 * The earlier claim that "rsmod has no such test" described XRSPS, not rsmod.
 */
static bool
collision_flood_arrived(
    struct CollisionMap* cm,
    int x,
    int z,
    int dst_x,
    int dst_z,
    struct CollisionApproach const* approach)
{
    if( !approach )
        return x == dst_x && z == dst_z;

    switch( approach->kind )
    {
    case COLL_APPROACH_LEGACY_SHAPE:
        return (x == dst_x && z == dst_z) ||
               collision_test_legacy_shape(cm, x, z, dst_x, dst_z, approach);
    case COLL_APPROACH_WALL:
        return (x == dst_x && z == dst_z) ||
               collision_test_wall(
                   cm, x, z, dst_x, dst_z, approach->loc_shape, approach->loc_angle);
    case COLL_APPROACH_WALL_DECOR:
        return (x == dst_x && z == dst_z) ||
               collision_test_wdecor(
                   cm, x, z, dst_x, dst_z, approach->loc_shape, approach->loc_angle);
    case COLL_APPROACH_RECT_ADJACENT:
        /* reachRectangle = intersects OR adjacent. allow_overlap carries the
         * intersects half; the exact-tile shortcut would bypass allow_overlap=0
         * so it is not applied here. Under exitStrategy RECTANGLE the filler
         * always sets allow_overlap. */
        return collision_test_rect_adjacent(cm, x, z, dst_x, dst_z, approach);
    case COLL_APPROACH_RECT_INSIDE:
        return (x == dst_x && z == dst_z) ||
               collision_test_rect_inside(x, z, dst_x, dst_z, approach);
    case COLL_APPROACH_RECT_WITHIN_RANGE:
        return collision_test_rect_within_range(x, z, dst_x, dst_z, approach);
    case COLL_APPROACH_RECT_EXCLUSIVE:
        /* No exact-tile shortcut — exclusive means not overlapping. */
        return collision_test_rect_exclusive(cm, x, z, dst_x, dst_z, approach);
    case COLL_APPROACH_EXACT:
    default:
        return x == dst_x && z == dst_z;
    }
}

int
collision_exit_strategy(int loc_shape)
{
    if( loc_shape == -2 )
        return COLL_EXIT_RECTANGLE_EXCLUSIVE;
    if( loc_shape == -1 )
        return COLL_EXIT_NONE;
    if( (loc_shape >= 0 && loc_shape <= 3) || loc_shape == 9 )
        return COLL_EXIT_WALL;
    if( loc_shape < 9 )
        return COLL_EXIT_WALL_DECOR;
    if( (loc_shape >= 10 && loc_shape <= 11) || loc_shape == 22 )
        return COLL_EXIT_RECTANGLE;
    return COLL_EXIT_NONE;
}

void
collision_approach_from_shape(
    int loc_shape,
    int loc_angle,
    int width,
    int length,
    int forceapproach_rotated,
    int mover_size,
    struct CollisionApproach* out)
{
    int exit;

    assert(out);
    memset(out, 0, sizeof(*out));
    out->mover_size = mover_size > 0 ? mover_size : 1;
    out->loc_width = width > 0 ? width : 1;
    out->loc_length = length > 0 ? length : 1;
    out->loc_angle = loc_angle;
    out->loc_shape = loc_shape;
    out->blocked_sides = forceapproach_rotated & 0xf;
    out->forceapproach = forceapproach_rotated & 0xf;

    exit = collision_exit_strategy(loc_shape);
    switch( exit )
    {
    case COLL_EXIT_WALL:
        out->kind = COLL_APPROACH_WALL;
        out->loc_width = 1;
        out->loc_length = 1;
        break;
    case COLL_EXIT_WALL_DECOR:
        out->kind = COLL_APPROACH_WALL_DECOR;
        out->loc_width = 1;
        out->loc_length = 1;
        break;
    case COLL_EXIT_RECTANGLE:
        /* reachRectangle = intersects OR adjacent. */
        out->kind = COLL_APPROACH_RECT_ADJACENT;
        out->allow_overlap = 1;
        break;
    case COLL_EXIT_RECTANGLE_EXCLUSIVE:
        out->kind = COLL_APPROACH_RECT_EXCLUSIVE;
        out->allow_overlap = 0;
        break;
    case COLL_EXIT_NONE:
    default:
        out->kind = COLL_APPROACH_EXACT;
        break;
    }
}

/*
 * The unreachable fallback, shared by ground clicks and (under the modern era)
 * interaction clicks. Scans a (2*range+1)-square box around the destination for
 * the best flooded tile and writes it through out_x / out_z. Returns 1 when one
 * was found.
 *
 * Two rankings, because the two references rank differently and the choice only
 * makes sense alongside the box size — see struct CollisionNearestOpts.
 */
static int
collision_nearest_fallback(
    struct CollisionMap* cm,
    int dst_x,
    int dst_z,
    struct CollisionApproach const* approach,
    struct CollisionNearestOpts const* opts,
    int const* dist_map,
    int* out_x,
    int* out_z)
{
    if( !opts || opts->range <= 0 )
        return 0;

    int max_dist = opts->max_dist > 0 ? opts->max_dist : 100;
    int rect_w = approach && approach->loc_width > 0 ? approach->loc_width : 1;
    int rect_l = approach && approach->loc_length > 0 ? approach->loc_length : 1;
    int best_cost = INT_MAX;
    int best_dist = INT_MAX;
    int found = 0;

    for( int px = dst_x - opts->range; px <= dst_x + opts->range; px++ )
    {
        for( int pz = dst_z - opts->range; pz <= dst_z + opts->range; pz++ )
        {
            if( px < 0 || pz < 0 || px >= cm->size_x || pz >= cm->size_z )
                continue;
            int dist = dist_map[collision_map_index_at(cm, px, pz)];
            if( dist >= max_dist )
                continue;

            if( !opts->rank_by_rect_distance )
            {
                /* Reference 3x3 ring: strictly-lower step count wins, so the
                 * first tile at the minimum is kept (scan order is the
                 * reference's px-then-pz). */
                if( dist >= best_dist )
                    continue;
                best_dist = dist;
                *out_x = px;
                *out_z = pz;
                found = 1;
                continue;
            }

            /* XRSPS alternative route: squared distance to the target
             * rectangle first, step count only as a tie-break. */
            int dx = 0;
            if( px < dst_x )
                dx = dst_x - px;
            else if( px > dst_x + rect_w - 1 )
                dx = px - (dst_x + rect_w - 1);
            int dz = 0;
            if( pz < dst_z )
                dz = dst_z - pz;
            else if( pz > dst_z + rect_l - 1 )
                dz = pz - (dst_z + rect_l - 1);
            int cost = dx * dx + dz * dz;

            if( cost < best_cost || (cost == best_cost && dist < best_dist) )
            {
                best_cost = cost;
                best_dist = dist;
                *out_x = px;
                *out_z = pz;
                found = 1;
            }
        }
    }
    return found;
}

/* The tile range the flood may expand into: cm->route_window tiles centred on
 * the source (rsmod `baseX = srcX - searchHalfMapSize`), clipped to the map. A
 * window of 0, or one at least as wide as the map, is the whole map — which is
 * what the 2004 client does and why a 104-tile scene sees no difference. */
struct CollisionFloodWindow
{
    int min_x;
    int min_z;
    int max_x; /* inclusive */
    int max_z; /* inclusive */
};

static void
collision_flood_window(
    struct CollisionMap const* cm,
    int src_x,
    int src_z,
    struct CollisionFloodWindow* out)
{
    int half;

    assert(cm && out);
    out->min_x = 0;
    out->min_z = 0;
    out->max_x = cm->size_x - 1;
    out->max_z = cm->size_z - 1;
    if( cm->route_window <= 0 )
        return;

    half = cm->route_window / 2;
    if( src_x - half > out->min_x )
        out->min_x = src_x - half;
    if( src_z - half > out->min_z )
        out->min_z = src_z - half;
    if( src_x - half + cm->route_window - 1 < out->max_x )
        out->max_x = src_x - half + cm->route_window - 1;
    if( src_z - half + cm->route_window - 1 < out->max_z )
        out->max_z = src_z - half + cm->route_window - 1;
}

/* Reference tryMove flood (Client.ts:5860-6008 ground-click arrival): fills
 * dir_map with the DirectionFlag toward each tile's parent and dist_map with
 * step counts, stopping early when a tile satisfies `approach` (or is the exact
 * destination when approach is NULL). The arrival tile is written to
 * *out_arrive_x/z. All four scratch arrays are size_x*size_z ints. Returns 1
 * when arrival was reached; on 0 the maps are fully flooded (needed for the
 * try-nearest fallback).
 *
 * Expansion is confined to the router's window (above), so the reachable set is
 * the mover's neighbourhood rather than "however much scene is loaded". */
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
    struct CollisionFloodWindow win;

    collision_flood_window(cm, src_x, src_z, &win);

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

        if( x - 1 >= win.min_x && collision_map_can_step_west(cm, x, z) )
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

        if( x + 1 <= win.max_x && collision_map_can_step_east(cm, x, z) )
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

        if( z - 1 >= win.min_z && collision_map_can_step_south(cm, x, z) )
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

        if( z + 1 <= win.max_z && collision_map_can_step_north(cm, x, z) )
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

        if( x - 1 >= win.min_x && z - 1 >= win.min_z &&
            collision_map_can_step_diagonal_south_west(cm, x, z) )
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
        if( x + 1 <= win.max_x && z - 1 >= win.min_z &&
            collision_map_can_step_diagonal_south_east(cm, x, z) )
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
        if( x - 1 >= win.min_x && z + 1 <= win.max_z &&
            collision_map_can_step_diagonal_north_west(cm, x, z) )
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
        if( x + 1 <= win.max_x && z + 1 <= win.max_z &&
            collision_map_can_step_diagonal_north_east(cm, x, z) )
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
    return collision_map_route_tiles(
        cm, src_x, src_z, dst_x, dst_z, NULL, NULL, path_x, path_z, max_path, NULL, NULL, NULL);
}

/*
 * Backtrace every tile from arrival toward the source into a heap buffer, then
 * emit the first min(len, max_path) tiles in walk order (source end first).
 *
 * Truncating at the destination end is the load-bearing property: an over-long
 * path must still start on a tile adjacent to the mover, or the next step is
 * not a legal direction and the walk aborts. The previous fixed 256-entry
 * scratch truncated at the source end and produced exactly that failure on any
 * route longer than the caller's budget.
 */
static int
collision_tiles_backtrace(
    struct CollisionMap* cm,
    int src_x,
    int src_z,
    int end_x,
    int end_z,
    int const* dir_map,
    int* path_x,
    int* path_z,
    int max_path)
{
    const int buf_size = cm->size_x * cm->size_z;
    int* tmp_x;
    int* tmp_z;
    int path_len = 0;
    int n;
    int trace_x = end_x;
    int trace_z = end_z;

    if( max_path <= 0 )
        return 0;
    if( end_x == src_x && end_z == src_z )
        return 0;

    tmp_x = (int*)malloc((size_t)buf_size * sizeof(int));
    tmp_z = (int*)malloc((size_t)buf_size * sizeof(int));
    if( !tmp_x || !tmp_z )
    {
        free(tmp_x);
        free(tmp_z);
        return -1;
    }

    while( path_len < buf_size && (trace_x != src_x || trace_z != src_z) )
    {
        int dir = dir_map[collision_map_index_at(cm, trace_x, trace_z)];
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

    /* path_len tiles, dest-first in tmp. Walk order is the reverse; keep the
     * source end when truncating. */
    n = path_len < max_path ? path_len : max_path;
    for( int i = 0; i < n; i++ )
    {
        path_x[i] = tmp_x[path_len - 1 - i];
        path_z[i] = tmp_z[path_len - 1 - i];
    }

    free(tmp_x);
    free(tmp_z);
    return n;
}

int
collision_map_reached(
    struct CollisionMap* cm,
    int x,
    int z,
    int dst_x,
    int dst_z,
    struct CollisionApproach const* approach)
{
    return collision_flood_arrived(cm, x, z, dst_x, dst_z, approach) ? 1 : 0;
}

int
collision_map_route_tiles(
    struct CollisionMap* cm,
    int src_x,
    int src_z,
    int dst_x,
    int dst_z,
    struct CollisionApproach const* approach,
    struct CollisionNearestOpts const* nearest,
    int* path_x,
    int* path_z,
    int max_path,
    int* out_used_nearest,
    int* out_arrive_x,
    int* out_arrive_z)
{
    const int buf_size = cm->size_x * cm->size_z;
    int end_x = dst_x;
    int end_z = dst_z;
    int arrived;
    int n;
    int* dir_map;
    int* dist_map;
    int* queue_x;
    int* queue_z;

    if( out_used_nearest )
        *out_used_nearest = 0;
    if( !cm || !path_x || !path_z || max_path < 0 )
        return -1;
    if( src_x < 0 || src_z < 0 || src_x >= cm->size_x || src_z >= cm->size_z || dst_x < 0 ||
        dst_z < 0 || dst_x >= cm->size_x || dst_z >= cm->size_z )
        return -1;

    if( collision_flood_arrived(cm, src_x, src_z, dst_x, dst_z, approach) )
    {
        if( out_arrive_x )
            *out_arrive_x = src_x;
        if( out_arrive_z )
            *out_arrive_z = src_z;
        return 0;
    }

    dir_map = (int*)malloc((size_t)buf_size * sizeof(int));
    dist_map = (int*)malloc((size_t)buf_size * sizeof(int));
    queue_x = (int*)malloc((size_t)buf_size * sizeof(int));
    queue_z = (int*)malloc((size_t)buf_size * sizeof(int));
    if( !dir_map || !dist_map || !queue_x || !queue_z )
    {
        free(dir_map);
        free(dist_map);
        free(queue_x);
        free(queue_z);
        return -1;
    }

    arrived = collision_flood(
        cm, src_x, src_z, dst_x, dst_z, approach, dir_map, dist_map, queue_x, queue_z, &end_x,
        &end_z);

    if( !arrived &&
        collision_nearest_fallback(cm, dst_x, dst_z, approach, nearest, dist_map, &end_x, &end_z) )
    {
        arrived = 1;
        if( out_used_nearest )
            *out_used_nearest = 1;
    }

    if( !arrived )
    {
        free(dir_map);
        free(dist_map);
        free(queue_x);
        free(queue_z);
        return -1;
    }

    if( out_arrive_x )
        *out_arrive_x = end_x;
    if( out_arrive_z )
        *out_arrive_z = end_z;

    n = collision_tiles_backtrace(cm, src_x, src_z, end_x, end_z, dir_map, path_x, path_z, max_path);

    free(dir_map);
    free(dist_map);
    free(queue_x);
    free(queue_z);
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
        /* Reference ground/minimap nearest fallback: the lowest-distance
         * flooded tile in the 3x3 ring around the destination (dist < 100).
         * Identical in both references, so it is not era-conditional. */
        struct CollisionNearestOpts const ring = {
            .range = 1,
            .max_dist = 100,
            .rank_by_rect_distance = 0,
        };
        if( collision_nearest_fallback(cm, dst_x, dst_z, NULL, &ring, dist_map, &end_x, &end_z) )
        {
            arrived = 1;
            if( out_used_nearest )
                *out_used_nearest = 1;
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
    struct CollisionNearestOpts const* nearest,
    int* route_x,
    int* route_z,
    int max_route,
    int* out_used_nearest)
{
    const int buf_size = cm->size_x * cm->size_z;

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

    /* Arrival is whichever approach tile the flood reaches first (or the exact
     * tile). Client-TS passes tryNearest = false for every type-2 tryMove, so
     * with `nearest` NULL/range 0 an unreachable target yields no route at all
     * — the reference behaviour. The modern era supplies the XRSPS
     * alternative-route box instead. */
    int end_x = dst_x;
    int end_z = dst_z;
    int arrived = collision_flood(
        cm, src_x, src_z, dst_x, dst_z, approach, dir_map, dist_map, queue_x, queue_z, &end_x,
        &end_z);

    if( !arrived &&
        collision_nearest_fallback(cm, dst_x, dst_z, approach, nearest, dist_map, &end_x, &end_z) )
    {
        arrived = 1;
        if( out_used_nearest )
            *out_used_nearest = 1;
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

/* =========================================================================
 * Line of sight / line of walk — rsmod LineValidator.rayCastLine
 * ========================================================================= */

/* Fixed-point helpers: tiles << 16, HALF_TILE = 0x8000. */
#define COLL_HALF_TILE 0x8000

static int
collision_scale_up(int tiles)
{
    return tiles << 16;
}

static int
collision_scale_down(int tiles)
{
    return tiles >> 16;
}

int
collision_line_coordinate(
    int a,
    int b,
    int size)
{
    if( a >= b )
        return a;
    if( a + size - 1 <= b )
        return a + size - 1;
    return b;
}

static int
collision_is_flagged(
    struct CollisionMap* cm,
    int x,
    int z,
    int masks)
{
    if( x < 0 || z < 0 || x >= cm->size_x || z >= cm->size_z )
        return 1;
    return (cm->flags[collision_map_index_at(cm, x, z)] & masks) != COLL_FLAG_OPEN;
}

static int
collision_ray_cast(
    struct CollisionMap* cm,
    int src_x,
    int src_z,
    int dest_x,
    int dest_z,
    int src_width,
    int src_height,
    int dest_width,
    int dest_height,
    int flag_west,
    int flag_east,
    int flag_south,
    int flag_north,
    int flag_loc,
    int flag_proj,
    int los)
{
    int start_x = collision_line_coordinate(src_x, dest_x, src_width);
    int start_z = collision_line_coordinate(src_z, dest_z, src_height);
    int end_x = collision_line_coordinate(dest_x, src_x, dest_width);
    int end_z = collision_line_coordinate(dest_z, src_z, dest_height);
    int delta_x;
    int delta_z;
    int abs_dx;
    int abs_dz;
    int travel_east;
    int travel_north;
    int x_flags;
    int z_flags;

    assert(cm);
    assert(src_width >= 1 && src_height >= 1);
    assert(dest_width >= 1 && dest_height >= 1);

    if( start_x == end_x && start_z == end_z )
        return 1;

    if( los && collision_is_flagged(cm, start_x, start_z, flag_loc) )
        return 0;

    delta_x = end_x - start_x;
    delta_z = end_z - start_z;
    abs_dx = delta_x < 0 ? -delta_x : delta_x;
    abs_dz = delta_z < 0 ? -delta_z : delta_z;
    travel_east = delta_x >= 0;
    travel_north = delta_z >= 0;
    x_flags = travel_east ? flag_west : flag_east;
    z_flags = travel_north ? flag_south : flag_north;

    if( abs_dx > abs_dz )
    {
        int offset_x = travel_east ? 1 : -1;
        int offset_z = travel_north ? 0 : -1;
        int scaled_z = collision_scale_up(start_z) + COLL_HALF_TILE + offset_z;
        int tangent = abs_dx == 0 ? 0 : (collision_scale_up(delta_z) / abs_dx);
        int curr_x = start_x;

        while( curr_x != end_x )
        {
            int curr_z;
            int next_z;

            curr_x += offset_x;
            curr_z = collision_scale_down(scaled_z);
            if( los && curr_x == end_x && curr_z == end_z )
                x_flags &= ~flag_proj;
            if( collision_is_flagged(cm, curr_x, curr_z, x_flags) )
                return 0;

            scaled_z += tangent;
            next_z = collision_scale_down(scaled_z);
            if( los && curr_x == end_x && next_z == end_z )
                z_flags &= ~flag_proj;
            if( next_z != curr_z && collision_is_flagged(cm, curr_x, next_z, z_flags) )
                return 0;
        }
    }
    else
    {
        int offset_x = travel_east ? 0 : -1;
        int offset_z = travel_north ? 1 : -1;
        int scaled_x = collision_scale_up(start_x) + COLL_HALF_TILE + offset_x;
        int tangent = abs_dz == 0 ? 0 : (collision_scale_up(delta_x) / abs_dz);
        int curr_z = start_z;

        while( curr_z != end_z )
        {
            int curr_x;
            int next_x;

            curr_z += offset_z;
            curr_x = collision_scale_down(scaled_x);
            if( los && curr_x == end_x && curr_z == end_z )
                z_flags &= ~flag_proj;
            if( collision_is_flagged(cm, curr_x, curr_z, z_flags) )
                return 0;

            scaled_x += tangent;
            next_x = collision_scale_down(scaled_x);
            if( los && next_x == end_x && curr_z == end_z )
                x_flags &= ~flag_proj;
            if( next_x != curr_x && collision_is_flagged(cm, next_x, curr_z, x_flags) )
                return 0;
        }
    }

    return 1;
}

int
collision_map_line_of_sight(
    struct CollisionMap* cm,
    int src_x,
    int src_z,
    int dest_x,
    int dest_z,
    int src_width,
    int src_height,
    int dest_width,
    int dest_height,
    int extra_flag)
{
    return collision_ray_cast(
        cm, src_x, src_z, dest_x, dest_z, src_width, src_height, dest_width, dest_height,
        COLL_FLAG_SIGHT_BLOCKED_WEST | extra_flag, COLL_FLAG_SIGHT_BLOCKED_EAST | extra_flag,
        COLL_FLAG_SIGHT_BLOCKED_SOUTH | extra_flag, COLL_FLAG_SIGHT_BLOCKED_NORTH | extra_flag,
        COLL_FLAG_LOC | extra_flag, COLL_FLAG_LOC_PROJ_BLOCKER | extra_flag, 1);
}

int
collision_map_line_of_walk(
    struct CollisionMap* cm,
    int src_x,
    int src_z,
    int dest_x,
    int dest_z,
    int src_width,
    int src_height,
    int dest_width,
    int dest_height,
    int extra_flag)
{
    /*
     * Walk masks: WALK_BLOCKED_WEST == BLOCK_EAST, WALK_BLOCKED_EAST ==
     * BLOCK_WEST, WALK_BLOCKED_SOUTH == BLOCK_NORTH, WALK_BLOCKED_NORTH ==
     * BLOCK_SOUTH. Compile-time pinned so a flag-layout change cannot silently
     * break LoW.
     */
    _Static_assert(
        COLL_FLAG_BLOCK_EAST == (COLL_FLAG_WALL_WEST | COLL_FLAG_WALK_BLOCKED),
        "WALK_BLOCKED_WEST must equal BLOCK_EAST");
    _Static_assert(
        COLL_FLAG_BLOCK_WEST == (COLL_FLAG_WALL_EAST | COLL_FLAG_WALK_BLOCKED),
        "WALK_BLOCKED_EAST must equal BLOCK_WEST");
    _Static_assert(
        COLL_FLAG_BLOCK_NORTH == (COLL_FLAG_WALL_SOUTH | COLL_FLAG_WALK_BLOCKED),
        "WALK_BLOCKED_SOUTH must equal BLOCK_NORTH");
    _Static_assert(
        COLL_FLAG_BLOCK_SOUTH == (COLL_FLAG_WALL_NORTH | COLL_FLAG_WALK_BLOCKED),
        "WALK_BLOCKED_NORTH must equal BLOCK_SOUTH");

    return collision_ray_cast(
        cm, src_x, src_z, dest_x, dest_z, src_width, src_height, dest_width, dest_height,
        COLL_FLAG_BLOCK_EAST | extra_flag, COLL_FLAG_BLOCK_WEST | extra_flag,
        COLL_FLAG_BLOCK_NORTH | extra_flag, COLL_FLAG_BLOCK_SOUTH | extra_flag,
        COLL_FLAG_LOC | extra_flag, COLL_FLAG_LOC_PROJ_BLOCKER | extra_flag, 0);
}

static int
collision_footprints_intersect(
    int ax,
    int az,
    int aw,
    int ah,
    int bx,
    int bz,
    int bw,
    int bh)
{
    return !(bx >= ax + aw || bx + bw <= ax || bz >= az + ah || bz + bh <= az);
}

int
collision_map_approached(
    struct CollisionMap* cm,
    int src_x,
    int src_z,
    int dest_x,
    int dest_z,
    int src_width,
    int src_height,
    int dest_width,
    int dest_height)
{
    assert(cm);
    if( collision_footprints_intersect(
            src_x, src_z, src_width, src_height, dest_x, dest_z, dest_width, dest_height) )
        return 0;
    return collision_map_line_of_sight(
        cm, src_x, src_z, dest_x, dest_z, src_width, src_height, dest_width, dest_height,
        COLL_FLAG_BLOCK_NPC_AND_PLAYERS);
}

int
collision_map_approached_symmetric(
    struct CollisionMap* cm,
    int src_x,
    int src_z,
    int dest_x,
    int dest_z,
    int src_width,
    int src_height,
    int dest_width,
    int dest_height)
{
    assert(cm);
    if( !collision_map_approached(
            cm, src_x, src_z, dest_x, dest_z, src_width, src_height, dest_width, dest_height) )
        return 0;
    return collision_map_approached(
        cm, dest_x, dest_z, src_x, src_z, dest_width, dest_height, src_width, src_height);
}

/* =========================================================================
 * Entity occupancy — rsmod CollisionEngine.changeSquare
 * ========================================================================= */

void
collision_map_change_square(
    struct CollisionMap* cm,
    int tile_x,
    int tile_z,
    int size,
    int mask,
    int add)
{
    int area;
    int i;

    assert(cm);
    assert(size >= 1);
    assert(mask != 0);

    area = size * size;
    for( i = 0; i < area; i++ )
    {
        int dx = tile_x + (i % size);
        int dz = tile_z + (i / size);
        if( add )
            collision_map_add(cm, dx, dz, mask);
        else
            collision_map_remove(cm, dx, dz, mask);
    }
}

/* =========================================================================
 * Step validator — rsmod StepValidator.canTravel (size 1 primary path)
 * ========================================================================= */

static int
collision_tile_open(
    struct CollisionMap* cm,
    int coll_type,
    int x,
    int z,
    int masks)
{
    if( x < 0 || z < 0 || x >= cm->size_x || z >= cm->size_z )
        return 0;
    return collision_can_move(coll_type, cm->flags[collision_map_index_at(cm, x, z)], masks);
}

int
collision_map_can_travel(
    struct CollisionMap* cm,
    int x,
    int z,
    int offset_x,
    int offset_z,
    int size,
    int extra_flag)
{
    return collision_map_can_travel_typed(
        cm, x, z, offset_x, offset_z, size, extra_flag, COLL_TYPE_NORMAL);
}

int
collision_map_can_travel_typed(
    struct CollisionMap* cm,
    int x,
    int z,
    int offset_x,
    int offset_z,
    int size,
    int extra_flag,
    int coll_type)
{
    assert(cm);
    assert(size >= 1);

    /* Size > 1 is supported for the composites the plan requires, but NPCs /
     * players today are size 1 through the naive pathfinder. */
    if( size == 1 )
    {
        if( offset_x == 0 && offset_z == -1 )
            return collision_tile_open(cm, coll_type, x, z - 1, COLL_FLAG_BLOCK_SOUTH | extra_flag);
        if( offset_x == 0 && offset_z == 1 )
            return collision_tile_open(cm, coll_type, x, z + 1, COLL_FLAG_BLOCK_NORTH | extra_flag);
        if( offset_x == -1 && offset_z == 0 )
            return collision_tile_open(cm, coll_type, x - 1, z, COLL_FLAG_BLOCK_WEST | extra_flag);
        if( offset_x == 1 && offset_z == 0 )
            return collision_tile_open(cm, coll_type, x + 1, z, COLL_FLAG_BLOCK_EAST | extra_flag);
        if( offset_x == -1 && offset_z == -1 )
            return collision_tile_open(cm, coll_type, x - 1, z - 1, COLL_FLAG_BLOCK_SOUTH_WEST | extra_flag) &&
                   collision_tile_open(cm, coll_type, x - 1, z, COLL_FLAG_BLOCK_WEST | extra_flag) &&
                   collision_tile_open(cm, coll_type, x, z - 1, COLL_FLAG_BLOCK_SOUTH | extra_flag);
        if( offset_x == -1 && offset_z == 1 )
            return collision_tile_open(cm, coll_type, x - 1, z + 1, COLL_FLAG_BLOCK_NORTH_WEST | extra_flag) &&
                   collision_tile_open(cm, coll_type, x - 1, z, COLL_FLAG_BLOCK_WEST | extra_flag) &&
                   collision_tile_open(cm, coll_type, x, z + 1, COLL_FLAG_BLOCK_NORTH | extra_flag);
        if( offset_x == 1 && offset_z == -1 )
            return collision_tile_open(cm, coll_type, x + 1, z - 1, COLL_FLAG_BLOCK_SOUTH_EAST | extra_flag) &&
                   collision_tile_open(cm, coll_type, x + 1, z, COLL_FLAG_BLOCK_EAST | extra_flag) &&
                   collision_tile_open(cm, coll_type, x, z - 1, COLL_FLAG_BLOCK_SOUTH | extra_flag);
        if( offset_x == 1 && offset_z == 1 )
            return collision_tile_open(cm, coll_type, x + 1, z + 1, COLL_FLAG_BLOCK_NORTH_EAST | extra_flag) &&
                   collision_tile_open(cm, coll_type, x + 1, z, COLL_FLAG_BLOCK_EAST | extra_flag) &&
                   collision_tile_open(cm, coll_type, x, z + 1, COLL_FLAG_BLOCK_NORTH | extra_flag);
        return 0;
    }

    if( size == 2 )
    {
        if( offset_x == 0 && offset_z == -1 )
            return collision_tile_open(cm, coll_type, x, z - 1, COLL_FLAG_BLOCK_SOUTH_WEST | extra_flag) &&
                   collision_tile_open(cm, coll_type, x + 1, z - 1, COLL_FLAG_BLOCK_SOUTH_EAST | extra_flag);
        if( offset_x == 0 && offset_z == 1 )
            return collision_tile_open(cm, coll_type, x, z + 2, COLL_FLAG_BLOCK_NORTH_WEST | extra_flag) &&
                   collision_tile_open(cm, coll_type, x + 1, z + 2, COLL_FLAG_BLOCK_NORTH_EAST | extra_flag);
        if( offset_x == -1 && offset_z == 0 )
            return collision_tile_open(cm, coll_type, x - 1, z, COLL_FLAG_BLOCK_SOUTH_WEST | extra_flag) &&
                   collision_tile_open(cm, coll_type, x - 1, z + 1, COLL_FLAG_BLOCK_NORTH_WEST | extra_flag);
        if( offset_x == 1 && offset_z == 0 )
            return collision_tile_open(cm, coll_type, x + 2, z, COLL_FLAG_BLOCK_SOUTH_EAST | extra_flag) &&
                   collision_tile_open(cm, coll_type, x + 2, z + 1, COLL_FLAG_BLOCK_NORTH_EAST | extra_flag);
        /* Diagonals for size 2: three-tile check via the size-1 style on the
         * leading corner plus the two cardinals — matches StepValidator. */
        if( offset_x == -1 && offset_z == -1 )
            return collision_tile_open(cm, coll_type, x - 1, z - 1, COLL_FLAG_BLOCK_SOUTH_WEST | extra_flag) &&
                   collision_tile_open(cm, coll_type, x - 1, z, COLL_FLAG_BLOCK_NORTH_AND_SOUTH_EAST | extra_flag) &&
                   collision_tile_open(cm, coll_type, x, z - 1, COLL_FLAG_BLOCK_NORTH_EAST_AND_WEST | extra_flag);
        if( offset_x == -1 && offset_z == 1 )
            return collision_tile_open(cm, coll_type, x - 1, z + 2, COLL_FLAG_BLOCK_NORTH_WEST | extra_flag) &&
                   collision_tile_open(cm, coll_type, x - 1, z + 1, COLL_FLAG_BLOCK_NORTH_AND_SOUTH_EAST | extra_flag) &&
                   collision_tile_open(cm, coll_type, x, z + 2, COLL_FLAG_BLOCK_SOUTH_EAST_AND_WEST | extra_flag);
        if( offset_x == 1 && offset_z == -1 )
            return collision_tile_open(cm, coll_type, x + 2, z - 1, COLL_FLAG_BLOCK_SOUTH_EAST | extra_flag) &&
                   collision_tile_open(cm, coll_type, x + 2, z, COLL_FLAG_BLOCK_NORTH_AND_SOUTH_WEST | extra_flag) &&
                   collision_tile_open(cm, coll_type, x + 1, z - 1, COLL_FLAG_BLOCK_NORTH_EAST_AND_WEST | extra_flag);
        if( offset_x == 1 && offset_z == 1 )
            return collision_tile_open(cm, coll_type, x + 2, z + 2, COLL_FLAG_BLOCK_NORTH_EAST | extra_flag) &&
                   collision_tile_open(cm, coll_type, x + 2, z + 1, COLL_FLAG_BLOCK_NORTH_AND_SOUTH_WEST | extra_flag) &&
                   collision_tile_open(cm, coll_type, x + 1, z + 2, COLL_FLAG_BLOCK_SOUTH_EAST_AND_WEST | extra_flag);
        return 0;
    }

    /* size >= 3: corners + mid-edge loop (StepValidator default branch). */
    if( offset_x == 0 && offset_z == -1 )
    {
        int mid;
        if( !collision_tile_open(cm, coll_type, x, z - 1, COLL_FLAG_BLOCK_SOUTH_WEST | extra_flag) )
            return 0;
        if( !collision_tile_open(cm, coll_type, x + size - 1, z - 1, COLL_FLAG_BLOCK_SOUTH_EAST | extra_flag) )
            return 0;
        for( mid = x + 1; mid < x + size - 1; mid++ )
        {
            if( !collision_tile_open(cm, coll_type, mid, z - 1, COLL_FLAG_BLOCK_NORTH_EAST_AND_WEST | extra_flag) )
                return 0;
        }
        return 1;
    }
    if( offset_x == 0 && offset_z == 1 )
    {
        int mid;
        if( !collision_tile_open(cm, coll_type, x, z + size, COLL_FLAG_BLOCK_NORTH_WEST | extra_flag) )
            return 0;
        if( !collision_tile_open(cm, coll_type, x + size - 1, z + size, COLL_FLAG_BLOCK_NORTH_EAST | extra_flag) )
            return 0;
        for( mid = x + 1; mid < x + size - 1; mid++ )
        {
            if( !collision_tile_open(cm, coll_type, mid, z + size, COLL_FLAG_BLOCK_SOUTH_EAST_AND_WEST | extra_flag) )
                return 0;
        }
        return 1;
    }
    if( offset_x == -1 && offset_z == 0 )
    {
        int mid;
        if( !collision_tile_open(cm, coll_type, x - 1, z, COLL_FLAG_BLOCK_SOUTH_WEST | extra_flag) )
            return 0;
        if( !collision_tile_open(cm, coll_type, x - 1, z + size - 1, COLL_FLAG_BLOCK_NORTH_WEST | extra_flag) )
            return 0;
        for( mid = z + 1; mid < z + size - 1; mid++ )
        {
            if( !collision_tile_open(cm, coll_type, x - 1, mid, COLL_FLAG_BLOCK_NORTH_AND_SOUTH_EAST | extra_flag) )
                return 0;
        }
        return 1;
    }
    if( offset_x == 1 && offset_z == 0 )
    {
        int mid;
        if( !collision_tile_open(cm, coll_type, x + size, z, COLL_FLAG_BLOCK_SOUTH_EAST | extra_flag) )
            return 0;
        if( !collision_tile_open(cm, coll_type, x + size, z + size - 1, COLL_FLAG_BLOCK_NORTH_EAST | extra_flag) )
            return 0;
        for( mid = z + 1; mid < z + size - 1; mid++ )
        {
            if( !collision_tile_open(cm, coll_type, x + size, mid, COLL_FLAG_BLOCK_NORTH_AND_SOUTH_WEST | extra_flag) )
                return 0;
        }
        return 1;
    }
    /* Diagonals for size >= 3 — StepValidator isBlocked* default branches. */
    if( offset_x == -1 && offset_z == -1 )
    {
        int mid;
        if( !collision_tile_open(cm, coll_type, x - 1, z - 1, COLL_FLAG_BLOCK_SOUTH_WEST | extra_flag) )
            return 0;
        for( mid = 1; mid < size; mid++ )
        {
            if( !collision_tile_open(
                    cm, coll_type, x - 1, z + mid - 1, COLL_FLAG_BLOCK_NORTH_AND_SOUTH_EAST | extra_flag) )
                return 0;
            if( !collision_tile_open(
                    cm, coll_type, x + mid - 1, z - 1, COLL_FLAG_BLOCK_NORTH_EAST_AND_WEST | extra_flag) )
                return 0;
        }
        return 1;
    }
    if( offset_x == -1 && offset_z == 1 )
    {
        int mid;
        if( !collision_tile_open(cm, coll_type, x - 1, z + size, COLL_FLAG_BLOCK_NORTH_WEST | extra_flag) )
            return 0;
        for( mid = 1; mid < size; mid++ )
        {
            if( !collision_tile_open(
                    cm, coll_type, x - 1, z + mid, COLL_FLAG_BLOCK_NORTH_AND_SOUTH_EAST | extra_flag) )
                return 0;
            if( !collision_tile_open(
                    cm, coll_type, x + mid - 1, z + size, COLL_FLAG_BLOCK_SOUTH_EAST_AND_WEST | extra_flag) )
                return 0;
        }
        return 1;
    }
    if( offset_x == 1 && offset_z == -1 )
    {
        int mid;
        if( !collision_tile_open(cm, coll_type, x + size, z - 1, COLL_FLAG_BLOCK_SOUTH_EAST | extra_flag) )
            return 0;
        for( mid = 1; mid < size; mid++ )
        {
            if( !collision_tile_open(
                    cm, coll_type, x + size, z + mid - 1, COLL_FLAG_BLOCK_NORTH_AND_SOUTH_WEST | extra_flag) )
                return 0;
            if( !collision_tile_open(
                    cm, coll_type, x + mid, z - 1, COLL_FLAG_BLOCK_NORTH_EAST_AND_WEST | extra_flag) )
                return 0;
        }
        return 1;
    }
    if( offset_x == 1 && offset_z == 1 )
    {
        int mid;
        if( !collision_tile_open(cm, coll_type, x + size, z + size, COLL_FLAG_BLOCK_NORTH_EAST | extra_flag) )
            return 0;
        for( mid = 1; mid < size; mid++ )
        {
            if( !collision_tile_open(
                    cm, coll_type, x + mid, z + size, COLL_FLAG_BLOCK_SOUTH_EAST_AND_WEST | extra_flag) )
                return 0;
            if( !collision_tile_open(
                    cm, coll_type, x + size, z + mid, COLL_FLAG_BLOCK_NORTH_AND_SOUTH_WEST | extra_flag) )
                return 0;
        }
        return 1;
    }
    return 0;
}

/* =========================================================================
 * Naive pathfinder — rsmod NaivePathFinder.findNaivePath
 * ========================================================================= */

static int
collision_coerce_at_most(int value, int max)
{
    return value > max ? max : value;
}

static int
collision_coerce_at_least(int value, int min)
{
    return value < min ? min : value;
}

static int
collision_is_diagonal_adj(
    int src_x,
    int src_z,
    int src_w,
    int src_h,
    int dest_x,
    int dest_z,
    int dest_w,
    int dest_h)
{
    if( src_x + src_w == dest_x && src_z + src_h == dest_z )
        return 1;
    if( src_x - 1 == dest_x + dest_w - 1 && src_z - 1 == dest_z + dest_h - 1 )
        return 1;
    if( src_x + src_w == dest_x && src_z - 1 == dest_z + dest_h - 1 )
        return 1;
    return src_x - 1 == dest_x + dest_w - 1 && src_z + src_h == dest_z;
}

static unsigned
collision_rng_next(unsigned* rng)
{
    /* xorshift32 — deterministic, matches the project's "fixed seed" rule. */
    unsigned x = *rng ? *rng : 0x5eed1234u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *rng = x;
    return x;
}

static int
collision_sign(int v)
{
    return (v > 0) - (v < 0);
}

/*
 * Returns 1 and writes out_x/out_z on success; 0 when the source is exactly
 * on a corner (authentic empty path).
 */
static int
collision_naive_destination(
    int src_x,
    int src_z,
    int src_w,
    int src_h,
    int dest_x,
    int dest_z,
    int dest_w,
    int dest_h,
    int* out_x,
    int* out_z)
{
    int diagonal = src_x - dest_x + (src_z - dest_z);
    int anti = src_x - dest_x - (src_z - dest_z);
    int south_west_cw = anti < 0;
    int north_west_cw = diagonal >= dest_h - 1 - (src_w - 1);
    int north_east_cw = anti > src_w - src_h;
    int south_east_cw = diagonal <= dest_w - 1 - (src_h - 1);

    if( south_west_cw && !north_west_cw )
    {
        int off_z = 0;
        if( diagonal >= -src_w )
            off_z = collision_coerce_at_most(diagonal + src_w, dest_h - 1);
        else if( anti > -src_w )
            off_z = -(src_w + anti);
        *out_x = -src_w + dest_x;
        *out_z = off_z + dest_z;
        return 1;
    }
    if( north_west_cw && !north_east_cw )
    {
        int off_x = 0;
        if( anti >= -dest_h )
            off_x = collision_coerce_at_most(anti + dest_h, dest_w - 1);
        else if( diagonal < dest_h )
            off_x = collision_coerce_at_least(diagonal - dest_h, -(src_w - 1));
        *out_x = off_x + dest_x;
        *out_z = dest_h + dest_z;
        return 1;
    }
    if( north_east_cw && !south_east_cw )
    {
        int off_z = 0;
        if( anti <= dest_w )
            off_z = dest_h - anti;
        else if( diagonal < dest_w )
            off_z = collision_coerce_at_least(diagonal - dest_w, -(src_h - 1));
        *out_x = dest_w + dest_x;
        *out_z = off_z + dest_z;
        return 1;
    }
    if( !(south_east_cw && !south_west_cw) )
        return 0;

    {
        int off_x = 0;
        if( diagonal > -src_h )
            off_x = collision_coerce_at_most(diagonal + src_h, dest_w - 1);
        else if( anti < src_h )
            off_x = collision_coerce_at_least(anti - src_h, -(src_h - 1));
        *out_x = off_x + dest_x;
        *out_z = -src_h + dest_z;
        return 1;
    }
}

int
collision_map_naive_path(
    struct CollisionMap* cm,
    int src_x,
    int src_z,
    int dest_x,
    int dest_z,
    int src_width,
    int src_height,
    int dest_width,
    int dest_height,
    int extra_flag,
    unsigned* rng,
    int* out_x,
    int* out_z)
{
    static const int k_cardinals[4][2] = { { -1, 0 }, { 1, 0 }, { 0, 1 }, { 0, -1 } };
    int dx;
    int dz;
    int curr_x;
    int curr_z;
    unsigned local_rng;

    assert(cm);
    assert(out_x && out_z);
    assert(src_width >= 1 && src_height >= 1);
    assert(dest_width >= 1 && dest_height >= 1);

    if( !rng )
    {
        local_rng = 0x5eed1234u;
        rng = &local_rng;
    }

    if( collision_footprints_intersect(
            src_x, src_z, src_width, src_height, dest_x, dest_z, dest_width, dest_height) )
    {
        int pick = (int)(collision_rng_next(rng) % 4u);
        *out_x = src_x + k_cardinals[pick][0];
        *out_z = src_z + k_cardinals[pick][1];
        return 1;
    }

    /* naiveDestination is called with dest size 1x1 in LostCity — the SW of
     * the target footprint is the anchor the perimeter walk uses. */
    if( !collision_naive_destination(
            src_x, src_z, src_width, src_height, dest_x, dest_z, 1, 1, &dx, &dz) )
        return 0;

    if( collision_is_diagonal_adj(
            dx, dz, src_width, src_height, dest_x, dest_z, dest_width, dest_height) )
    {
        *out_x = dx;
        *out_z = dz;
        return 1;
    }

    if( collision_footprints_intersect(
            dx, dz, src_width, src_height, dest_x, dest_z, dest_width, dest_height) )
    {
        *out_x = dx;
        *out_z = dz;
        return 1;
    }

    curr_x = dx;
    curr_z = dz;
    while( curr_x != dest_x && curr_z != dest_z )
    {
        int step_x = collision_sign(dest_x - curr_x);
        int step_z = collision_sign(dest_z - curr_z);
        if( collision_map_can_travel(
                cm, curr_x, curr_z, step_x, step_z, src_width, extra_flag) )
        {
            curr_x += step_x;
            curr_z += step_z;
        }
        else if(
            step_x != 0 &&
            collision_map_can_travel(cm, curr_x, curr_z, step_x, 0, src_width, extra_flag) )
        {
            curr_x += step_x;
        }
        else if(
            step_z != 0 &&
            collision_map_can_travel(cm, curr_x, curr_z, 0, step_z, src_width, extra_flag) )
        {
            curr_z += step_z;
        }
        else
        {
            break;
        }
    }

    *out_x = curr_x;
    *out_z = curr_z;
    return 1;
}
