#include "collision_map.h"

#include "osrs/rscache/dat2a/rscache_dat2a_config_locs.h"

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

    if( shape == LOC_SHAPE_WALL_SINGLE_SIDE )
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
    else if( shape == LOC_SHAPE_WALL_TRI_CORNER || shape == LOC_SHAPE_WALL_RECT_CORNER )
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
    else if( shape == LOC_SHAPE_WALL_TWO_SIDES )
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
    const int scene_width = cm->size_x;
    const int scene_length = cm->size_z;
    const int buf_size = scene_width * scene_length;

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

    memset(bfs_direction, 0, (size_t)buf_size * sizeof(int));
    for( int i = 0; i < buf_size; i++ )
        bfs_cost[i] = 99999999;

    int src_idx = collision_map_index_at(cm, src_x, src_z);
    bfs_direction[src_idx] = 99;
    bfs_cost[src_idx] = 0;

    int steps = 0;
    int length = 0;
    bfs_step_x[steps] = src_x;
    bfs_step_z[steps++] = src_z;

    int arrived = 0;
    int end_x = src_x, end_z = src_z;

    while( length != steps )
    {
        int x = bfs_step_x[length];
        int z = bfs_step_z[length];
        length = (length + 1) % buf_size;

        if( x == dst_x && z == dst_z )
        {
            arrived = 1;
            end_x = x;
            end_z = z;
            break;
        }

        int next_cost = bfs_cost[collision_map_index_at(cm, x, z)] + 1;
        int idx = 0;

        if( collision_map_can_step_west(cm, x, z) )
        {
            idx = collision_map_index_at(cm, x - 1, z);
            if( bfs_direction[idx] == 0 )
            {
                bfs_step_x[steps] = x - 1;
                bfs_step_z[steps] = z;
                steps = (steps + 1) % buf_size;
                bfs_direction[idx] = DIR_EAST;
                bfs_cost[idx] = next_cost;
            }
        }

        if( collision_map_can_step_east(cm, x, z) )
        {
            idx = collision_map_index_at(cm, x + 1, z);
            if( bfs_direction[idx] == 0 )
            {
                bfs_step_x[steps] = x + 1;
                bfs_step_z[steps] = z;
                steps = (steps + 1) % buf_size;
                bfs_direction[idx] = DIR_WEST;
                bfs_cost[idx] = next_cost;
            }
        }

        if( collision_map_can_step_south(cm, x, z) )
        {
            idx = collision_map_index_at(cm, x, z - 1);
            if( bfs_direction[idx] == 0 )
            {
                bfs_step_x[steps] = x;
                bfs_step_z[steps] = z - 1;
                steps = (steps + 1) % buf_size;
                bfs_direction[idx] = DIR_NORTH;
                bfs_cost[idx] = next_cost;
            }
        }

        if( collision_map_can_step_north(cm, x, z) )
        {
            idx = collision_map_index_at(cm, x, z + 1);
            if( bfs_direction[idx] == 0 )
            {
                bfs_step_x[steps] = x;
                bfs_step_z[steps] = z + 1;
                steps = (steps + 1) % buf_size;
                bfs_direction[idx] = DIR_SOUTH;
                bfs_cost[idx] = next_cost;
            }
        }

        if( collision_map_can_step_diagonal_south_west(cm, x, z) )
        {
            idx = collision_map_index_at(cm, x - 1, z - 1);
            if( bfs_direction[idx] == 0 )
            {
                bfs_step_x[steps] = x - 1;
                bfs_step_z[steps] = z - 1;
                steps = (steps + 1) % buf_size;
                bfs_direction[idx] = DIR_NORTH_EAST;
                bfs_cost[idx] = next_cost;
            }
        }
        if( collision_map_can_step_diagonal_south_east(cm, x, z) )
        {
            idx = collision_map_index_at(cm, x + 1, z - 1);
            if( bfs_direction[idx] == 0 )
            {
                bfs_step_x[steps] = x + 1;
                bfs_step_z[steps] = z - 1;
                steps = (steps + 1) % buf_size;
                bfs_direction[idx] = DIR_NORTH_WEST;
                bfs_cost[idx] = next_cost;
            }
        }
        if( collision_map_can_step_diagonal_north_west(cm, x, z) )
        {
            idx = collision_map_index_at(cm, x - 1, z + 1);
            if( bfs_direction[idx] == 0 )
            {
                bfs_step_x[steps] = x - 1;
                bfs_step_z[steps] = z + 1;
                steps = (steps + 1) % buf_size;
                bfs_direction[idx] = DIR_SOUTH_EAST;
                bfs_cost[idx] = next_cost;
            }
        }
        if( collision_map_can_step_diagonal_north_east(cm, x, z) )
        {
            idx = collision_map_index_at(cm, x + 1, z + 1);
            if( bfs_direction[idx] == 0 )
            {
                bfs_step_x[steps] = x + 1;
                bfs_step_z[steps] = z + 1;
                steps = (steps + 1) % buf_size;
                bfs_direction[idx] = DIR_SOUTH_WEST;
                bfs_cost[idx] = next_cost;
            }
        }
    }

    if( !arrived )
    {
        free(bfs_direction);
        free(bfs_cost);
        free(bfs_step_x);
        free(bfs_step_z);
        return -1;
    }

    int trace_x = end_x, trace_z = end_z;
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
