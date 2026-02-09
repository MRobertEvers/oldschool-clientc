#ifndef COLLISION_MAP_H
#define COLLISION_MAP_H

#include "osrs/rscache/tables/config_locs.h"
#include "osrs/rscache/tables/maps.h"

/* Match clientts/src/dash3d/CollisionConstants and CollisionFlag */
#define COLLISION_SIZE 104
#define COLLISION_LEVELS 4

/* CollisionFlag equivalents (CollisionFlag.ts) */
#define COLL_FLAG_OPEN                0x0
#define COLL_FLAG_WALL_NORTH_WEST     0x1
#define COLL_FLAG_WALL_NORTH          0x2
#define COLL_FLAG_WALL_NORTH_EAST     0x4
#define COLL_FLAG_WALL_EAST           0x8
#define COLL_FLAG_WALL_SOUTH_EAST     0x10
#define COLL_FLAG_WALL_SOUTH          0x20
#define COLL_FLAG_WALL_SOUTH_WEST     0x40
#define COLL_FLAG_WALL_WEST           0x80

#define COLL_FLAG_LOC                 0x100
#define COLL_FLAG_WALL_NORTH_WEST_PROJ 0x200
#define COLL_FLAG_WALL_NORTH_PROJ     0x400
#define COLL_FLAG_WALL_NORTH_EAST_PROJ 0x800
#define COLL_FLAG_WALL_EAST_PROJ      0x1000
#define COLL_FLAG_WALL_SOUTH_EAST_PROJ 0x2000
#define COLL_FLAG_WALL_SOUTH_PROJ     0x4000
#define COLL_FLAG_WALL_SOUTH_WEST_PROJ 0x8000
#define COLL_FLAG_WALL_WEST_PROJ      0x10000
#define COLL_FLAG_LOC_PROJ_BLOCKER    0x20000

#define COLL_FLAG_ANTIMACRO           0x80000
#define COLL_FLAG_FLOOR              0x200000

#define COLL_FLAG_BOUNDS              0xffffff

/* Blocked walk flags (combination flags used by BFS) */
#define COLL_FLAG_BLOCK_SOUTH         0x280102
#define COLL_FLAG_BLOCK_WEST         0x280108
#define COLL_FLAG_BLOCK_SOUTH_WEST    0x28010E
#define COLL_FLAG_BLOCK_NORTH         0x280120
#define COLL_FLAG_BLOCK_NORTH_WEST    0x280138
#define COLL_FLAG_BLOCK_EAST          0x280180
#define COLL_FLAG_BLOCK_SOUTH_EAST    0x280183
#define COLL_FLAG_BLOCK_NORTH_EAST    0x2801E0

/* LocAngle: 0=WEST, 1=NORTH, 2=EAST, 3=SOUTH (clientts LocAngle) */
enum CollisionLocAngle
{
    COLL_ANGLE_WEST = 0,
    COLL_ANGLE_NORTH = 1,
    COLL_ANGLE_EAST = 2,
    COLL_ANGLE_SOUTH = 3
};

struct CollisionMap
{
    int* flags;
    int size_x;
    int size_z;
};

struct CollisionMap*
collision_map_new(int size_x, int size_z);

void
collision_map_free(struct CollisionMap* cm);

void
collision_map_reset(struct CollisionMap* cm);

void
collision_map_add_floor(struct CollisionMap* cm, int tile_x, int tile_z);

void
collision_map_add_loc(
    struct CollisionMap* cm,
    int tile_x,
    int tile_z,
    int size_x,
    int size_z,
    enum CollisionLocAngle angle,
    int blockrange);

void
collision_map_add_wall(
    struct CollisionMap* cm,
    int tile_x,
    int tile_z,
    int shape,
    enum CollisionLocAngle angle,
    int blockrange);

/* BFS pathfinding: fill path_x, path_z with up to max_path steps from (src_x,src_z) to (dst_x,dst_z).
 * Returns number of steps (excluding start); -1 if no path. path[0] = first step toward dest. */
int
collision_map_bfs_path(
    struct CollisionMap* cm,
    int src_x,
    int src_z,
    int dst_x,
    int dst_z,
    int* path_x,
    int* path_z,
    int max_path);

static inline int
collision_map_index(int x, int z)
{
    return x * COLLISION_SIZE + z;
}

#endif
