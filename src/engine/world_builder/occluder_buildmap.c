#include "occluder_buildmap.h"

#include "heightmap.h"
#include "painters/scene_occluders.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define OCCLUDER_TILE_SIZE 128

static inline int
occluder_buildmap_idx(
    const struct OccluderBuildmap* map,
    int x,
    int z,
    int level)
{
    assert(x >= 0 && x < map->size_x);
    assert(z >= 0 && z < map->size_z);
    assert(level >= 0 && level < map->levels);
    return x + z * map->size_x + level * map->size_x * map->size_z;
}

struct OccluderBuildmap*
occluder_buildmap_new(
    int size_x,
    int size_z,
    int levels)
{
    struct OccluderBuildmap* map;
    int n;

    assert(size_x > 0);
    assert(size_z > 0);
    assert(levels > 0);

    map = calloc(1, sizeof(*map));
    if( !map )
        return NULL;

    n = size_x * size_z * levels;
    map->marks = calloc((size_t)n, sizeof(uint16_t));
    map->floor_opacity = calloc((size_t)n, sizeof(uint8_t));
    if( !map->marks || !map->floor_opacity )
    {
        free(map->marks);
        free(map->floor_opacity);
        free(map);
        return NULL;
    }
    map->size_x = size_x;
    map->size_z = size_z;
    map->levels = levels;
    return map;
}

void
occluder_buildmap_free(struct OccluderBuildmap* map)
{
    if( !map )
        return;
    free(map->marks);
    free(map->floor_opacity);
    free(map);
}

void
occluder_buildmap_or_mark(
    struct OccluderBuildmap* map,
    int x,
    int z,
    int level,
    uint16_t bits)
{
    assert(map);
    if( x < 0 || z < 0 || x >= map->size_x || z >= map->size_z )
        return;
    if( level < 0 || level >= map->levels )
        return;
    map->marks[occluder_buildmap_idx(map, x, z, level)] |= bits;
}

void
occluder_buildmap_set_floor_opacity(
    struct OccluderBuildmap* map,
    int x,
    int z,
    int level,
    int opaque)
{
    assert(map);
    if( x < 0 || z < 0 || x >= map->size_x || z >= map->size_z )
        return;
    if( level < 0 || level >= map->levels )
        return;
    map->floor_opacity[occluder_buildmap_idx(map, x, z, level)] = opaque ? 1 : 0;
}

int
occluder_buildmap_floor_opacity(
    const struct OccluderBuildmap* map,
    int x,
    int z,
    int level)
{
    assert(map);
    if( x < 0 || z < 0 || x >= map->size_x || z >= map->size_z )
        return 0;
    if( level < 0 || level >= map->levels )
        return 0;
    return map->floor_opacity[occluder_buildmap_idx(map, x, z, level)];
}

void
occluder_buildmap_mark_flat_floor(
    struct OccluderBuildmap* map,
    int x,
    int z,
    int level,
    int height_sw,
    int height_se,
    int height_ne,
    int height_nw)
{
    if( !map || level <= 0 )
        return;
    if( !occluder_buildmap_floor_opacity(map, x, z, level) )
        return;
    if( height_sw != height_se || height_sw != height_ne || height_sw != height_nw )
        return;
    occluder_buildmap_or_mark(map, x, z, level, OCCLUDER_MARK_FLOOR_ALL_LEVELS);
}

static int
occluder_ground_y(
    struct Heightmap* heightmap,
    int x,
    int z,
    int level)
{
    if( x < 0 || z < 0 || x >= heightmap->size_x || z >= heightmap->size_z )
        return 0;
    if( level < 0 || level >= heightmap->levels )
        return 0;
    return heightmap_get(heightmap, x, z, level);
}

void
occluder_buildmap_build_occluders(
    struct OccluderBuildmap* map,
    struct Heightmap* heightmap,
    struct SceneOccluders* out)
{
    int wall0;
    int wall1;
    int floor;
    int top_level;
    int level;
    int tile_z;
    int tile_x;
    int max_tile_x;
    int max_tile_z;

    assert(map);
    assert(heightmap);
    assert(out);

    scene_occluders_clear(out);

    /* Copy ground heights so the painters TU can sample corners without
     * depending on world_builder/heightmap. */
    scene_occluders_set_ground_heights(
        out,
        heightmap->heights,
        heightmap->size_x,
        heightmap->size_z,
        heightmap->levels);

    /* The reference iterates tileX/tileZ up to and including maxTileX/Z, and
     * mapo is sized maxTileX+1. Our marks array is the scene tile grid; the
     * heightmap is scene_size+1. Cap the scan to the smaller of the two so
     * wall marks at [x+1] stay addressable while merge never reads past
     * marks. */
    max_tile_x = map->size_x - 1;
    max_tile_z = map->size_z - 1;
    if( max_tile_x > heightmap->size_x - 1 )
        max_tile_x = heightmap->size_x - 1;
    if( max_tile_z > heightmap->size_z - 1 )
        max_tile_z = heightmap->size_z - 1;

    wall0 = 0x1;
    wall1 = 0x2;
    floor = 0x4;

    for( top_level = 0; top_level < map->levels; top_level++ )
    {
        if( top_level > 0 )
        {
            wall0 <<= 3;
            wall1 <<= 3;
            floor <<= 3;
        }

        for( level = 0; level <= top_level; level++ )
        {
            for( tile_z = 0; tile_z <= max_tile_z; tile_z++ )
            {
                for( tile_x = 0; tile_x <= max_tile_x; tile_x++ )
                {
                    int idx = occluder_buildmap_idx(map, tile_x, tile_z, level);

                    /* --- wall along X (type 1, constant-X plane) --- */
                    if( (map->marks[idx] & wall0) != 0 )
                    {
                        int min_tile_z = tile_z;
                        int max_tz = tile_z;
                        int min_level = level;
                        int max_level = level;
                        int area;
                        int z;
                        int l;

                        while( min_tile_z > 0 &&
                               (map->marks[occluder_buildmap_idx(
                                    map, tile_x, min_tile_z - 1, level)] &
                                wall0) != 0 )
                            min_tile_z--;

                        while( max_tz < max_tile_z &&
                               (map->marks[occluder_buildmap_idx(
                                    map, tile_x, max_tz + 1, level)] &
                                wall0) != 0 )
                            max_tz++;

                        while( min_level > 0 )
                        {
                            int ok = 1;
                            for( z = min_tile_z; z <= max_tz; z++ )
                            {
                                if( (map->marks[occluder_buildmap_idx(
                                         map, tile_x, z, min_level - 1)] &
                                     wall0) == 0 )
                                {
                                    ok = 0;
                                    break;
                                }
                            }
                            if( !ok )
                                break;
                            min_level--;
                        }

                        while( max_level < top_level )
                        {
                            int ok = 1;
                            for( z = min_tile_z; z <= max_tz; z++ )
                            {
                                if( (map->marks[occluder_buildmap_idx(
                                         map, tile_x, z, max_level + 1)] &
                                     wall0) == 0 )
                                {
                                    ok = 0;
                                    break;
                                }
                            }
                            if( !ok )
                                break;
                            max_level++;
                        }

                        area = (max_level + 1 - min_level) * (max_tz + 1 - min_tile_z);
                        if( area >= OCCLUDER_WALL_MIN_TILE_AREA )
                        {
                            int min_y =
                                occluder_ground_y(
                                    heightmap, tile_x, min_tile_z, max_level) -
                                OCCLUDER_WALL_HEIGHT;
                            int max_y = occluder_ground_y(
                                heightmap, tile_x, min_tile_z, min_level);

                            scene_occluders_add(
                                out,
                                top_level,
                                OCCLUDER_PLANE_CONSTANT_X,
                                tile_x * OCCLUDER_TILE_SIZE,
                                min_y,
                                min_tile_z * OCCLUDER_TILE_SIZE,
                                tile_x * OCCLUDER_TILE_SIZE,
                                max_y,
                                max_tz * OCCLUDER_TILE_SIZE + OCCLUDER_TILE_SIZE);

                            for( l = min_level; l <= max_level; l++ )
                                for( z = min_tile_z; z <= max_tz; z++ )
                                    map->marks[occluder_buildmap_idx(
                                        map, tile_x, z, l)] &= (uint16_t)~wall0;
                        }
                    }

                    /* --- wall along Z (type 2, constant-Z plane) --- */
                    if( (map->marks[idx] & wall1) != 0 )
                    {
                        int min_tile_x = tile_x;
                        int max_tx = tile_x;
                        int min_level = level;
                        int max_level = level;
                        int area;
                        int x;
                        int l;

                        while( min_tile_x > 0 &&
                               (map->marks[occluder_buildmap_idx(
                                    map, min_tile_x - 1, tile_z, level)] &
                                wall1) != 0 )
                            min_tile_x--;

                        while( max_tx < max_tile_x &&
                               (map->marks[occluder_buildmap_idx(
                                    map, max_tx + 1, tile_z, level)] &
                                wall1) != 0 )
                            max_tx++;

                        while( min_level > 0 )
                        {
                            int ok = 1;
                            for( x = min_tile_x; x <= max_tx; x++ )
                            {
                                if( (map->marks[occluder_buildmap_idx(
                                         map, x, tile_z, min_level - 1)] &
                                     wall1) == 0 )
                                {
                                    ok = 0;
                                    break;
                                }
                            }
                            if( !ok )
                                break;
                            min_level--;
                        }

                        while( max_level < top_level )
                        {
                            int ok = 1;
                            for( x = min_tile_x; x <= max_tx; x++ )
                            {
                                if( (map->marks[occluder_buildmap_idx(
                                         map, x, tile_z, max_level + 1)] &
                                     wall1) == 0 )
                                {
                                    ok = 0;
                                    break;
                                }
                            }
                            if( !ok )
                                break;
                            max_level++;
                        }

                        area = (max_level + 1 - min_level) * (max_tx + 1 - min_tile_x);
                        if( area >= OCCLUDER_WALL_MIN_TILE_AREA )
                        {
                            int min_y =
                                occluder_ground_y(
                                    heightmap, min_tile_x, tile_z, max_level) -
                                OCCLUDER_WALL_HEIGHT;
                            int max_y = occluder_ground_y(
                                heightmap, min_tile_x, tile_z, min_level);

                            scene_occluders_add(
                                out,
                                top_level,
                                OCCLUDER_PLANE_CONSTANT_Z,
                                min_tile_x * OCCLUDER_TILE_SIZE,
                                min_y,
                                tile_z * OCCLUDER_TILE_SIZE,
                                max_tx * OCCLUDER_TILE_SIZE + OCCLUDER_TILE_SIZE,
                                max_y,
                                tile_z * OCCLUDER_TILE_SIZE);

                            for( l = min_level; l <= max_level; l++ )
                                for( x = min_tile_x; x <= max_tx; x++ )
                                    map->marks[occluder_buildmap_idx(
                                        map, x, tile_z, l)] &= (uint16_t)~wall1;
                        }
                    }

                    /* --- flat floor (type 4, constant-Y plane) --- */
                    if( (map->marks[idx] & floor) != 0 )
                    {
                        int min_tile_x = tile_x;
                        int max_tx = tile_x;
                        int min_tile_z = tile_z;
                        int max_tz = tile_z;
                        int x;
                        int z;

                        while( min_tile_z > 0 &&
                               (map->marks[occluder_buildmap_idx(
                                    map, tile_x, min_tile_z - 1, level)] &
                                floor) != 0 )
                            min_tile_z--;

                        while( max_tz < max_tile_z &&
                               (map->marks[occluder_buildmap_idx(
                                    map, tile_x, max_tz + 1, level)] &
                                floor) != 0 )
                            max_tz++;

                        while( min_tile_x > 0 )
                        {
                            int ok = 1;
                            for( z = min_tile_z; z <= max_tz; z++ )
                            {
                                if( (map->marks[occluder_buildmap_idx(
                                         map, min_tile_x - 1, z, level)] &
                                     floor) == 0 )
                                {
                                    ok = 0;
                                    break;
                                }
                            }
                            if( !ok )
                                break;
                            min_tile_x--;
                        }

                        while( max_tx < max_tile_x )
                        {
                            int ok = 1;
                            for( z = min_tile_z; z <= max_tz; z++ )
                            {
                                if( (map->marks[occluder_buildmap_idx(
                                         map, max_tx + 1, z, level)] &
                                     floor) == 0 )
                                {
                                    ok = 0;
                                    break;
                                }
                            }
                            if( !ok )
                                break;
                            max_tx++;
                        }

                        if( (max_tx + 1 - min_tile_x) * (max_tz + 1 - min_tile_z) >=
                            OCCLUDER_FLOOR_MIN_TILE_AREA )
                        {
                            int y = occluder_ground_y(
                                heightmap, min_tile_x, min_tile_z, level);

                            scene_occluders_add(
                                out,
                                top_level,
                                OCCLUDER_PLANE_CONSTANT_Y,
                                min_tile_x * OCCLUDER_TILE_SIZE,
                                y,
                                min_tile_z * OCCLUDER_TILE_SIZE,
                                max_tx * OCCLUDER_TILE_SIZE + OCCLUDER_TILE_SIZE,
                                y,
                                max_tz * OCCLUDER_TILE_SIZE + OCCLUDER_TILE_SIZE);

                            for( x = min_tile_x; x <= max_tx; x++ )
                                for( z = min_tile_z; z <= max_tz; z++ )
                                    map->marks[occluder_buildmap_idx(
                                        map, x, z, level)] &= (uint16_t)~floor;
                        }
                    }
                }
            }
        }
    }
}
