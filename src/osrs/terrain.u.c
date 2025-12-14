#ifndef TERRAIN_U_C
#define TERRAIN_U_C

#include "terrain.h"

#include "tables/maps.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIGHT_DIR_X -50
#define LIGHT_DIR_Y -10
#define LIGHT_DIR_Z -50
#define LIGHT_AMBIENT 96
// Over 256, so 768 / 256 = 3, the lightness is divided by 3.
#define LIGHT_ATTENUATION 768
#define HEIGHT_SCALE 65536

static int*
calculate_lights(struct CacheMapTerrainIter* terrain, int level)
{
    int* lights = (int*)malloc(map_terrain_iter_tiles_size(terrain) * sizeof(int));
    memset(lights, 0, map_terrain_iter_tiles_size(terrain) * sizeof(int));

    int max_x = terrain->chunks_width * MAP_TERRAIN_X;
    int max_z = terrain->chunks_count / terrain->chunks_width;

    int magnitude =
        sqrt(LIGHT_DIR_X * LIGHT_DIR_X + LIGHT_DIR_Y * LIGHT_DIR_Y + LIGHT_DIR_Z * LIGHT_DIR_Z);
    int intensity = (magnitude * LIGHT_ATTENUATION) >> 8;

    for( int z = 1; z < max_z - 1; z++ )
    {
        for( int x = 1; x < max_x - 1; x++ )
        {
            // First we need to calculate the normals for each tile.
            // This is typically by doing a cross product on the tangent vectors which can be
            // derived from the differences in height between adjacent tiles. The code below seems
            // to be calculating the normals directly by skipping the cross product.

            int height_delta_x = map_terrain_iter_at(terrain, x + 1, z, level)->height -
                                 map_terrain_iter_at(terrain, x - 1, z, level)->height;
            int height_delta_z = map_terrain_iter_at(terrain, x, z + 1, level)->height -
                                 map_terrain_iter_at(terrain, x, z - 1, level)->height;
            // const tileNormalLength =
            //                 Math.sqrt(
            //                     heightDeltaY * heightDeltaY + heightDeltaX * heightDeltaX +
            //                     HEIGHT_SCALE,
            //                 ) | 0;

            //             const normalizedTileNormalX = ((heightDeltaX << 8) / tileNormalLength) |
            //             0; const normalizedTileNormalY = (HEIGHT_SCALE / tileNormalLength) | 0;
            //             const normalizedTileNormalZ = ((heightDeltaY << 8) / tileNormalLength) |
            //             0;

            int tile_normal_length = sqrt(
                height_delta_z * height_delta_z + height_delta_x * height_delta_x + HEIGHT_SCALE);

            int normalized_tile_normal_x = (height_delta_x << 8) / tile_normal_length;
            int normalized_tile_normal_y = HEIGHT_SCALE / tile_normal_length;
            int normalized_tile_normal_z = (height_delta_z << 8) / tile_normal_length;

            // Now we calculate the light contribution based on a simplified Phong model,
            // specifically
            // we ignore the material coefficients and there are no specular contributions.

            // For reference, this is the standard Phong model:
            //  I = Ia * Ka + Id * Kd * (N dot L)
            //  I: Total intensity of light at a point on the surface.
            //  Ia: Intensity of ambient light in the scene (constant and uniform).
            //  Ka: Ambient reflection coefficient of the material.
            //  Id: Intensity of the directional (diffuse) light source.
            //  Kd: Diffuse reflection coefficient of the material.
            //  N: Normalized surface normal vector at the point.
            //  L: Normalized direction vector from the point to the light source.
            //  (N dot L): Dot product between the surface normal vector and the light direction
            //  vector.

            // const dot =
            //     normalizedTileNormalX * LIGHT_DIR_X +
            //     normalizedTileNormalY * LIGHT_DIR_Y +
            //     normalizedTileNormalZ * LIGHT_DIR_Z;
            // const sunLight = (dot / lightIntensity + LIGHT_INTENSITY_BASE) | 0;

            int dot = normalized_tile_normal_x * LIGHT_DIR_X +
                      normalized_tile_normal_y * LIGHT_DIR_Y +
                      normalized_tile_normal_z * LIGHT_DIR_Z;
            int sunlight = (dot / intensity + LIGHT_AMBIENT);

            lights[MAP_TILE_COORD(x, z, 0)] = sunlight;
        }
    }

    return lights;
}

static void
apply_shade(
    int* lightmap,
    int* shade_map,
    int level,
    int xboundmin,
    int xboundmax,
    int zboundmin,
    int zboundmax,
    int xmin,
    int xmax,
    int zmin,
    int zmax)
{
    assert(xboundmin <= xmin);
    assert(xboundmax >= xmax);
    assert(zboundmin <= zmin);
    assert(zboundmax >= zmax);

    int shade_west;
    int shade_east;
    int shade_north;
    int shade_south;

    for( int z = zmin; z < zmax; z++ )
    {
        for( int x = xmin; x < xmax; x++ )
        {
            shade_west = 0;
            shade_east = 0;
            shade_north = 0;
            shade_south = 0;

            if( x > xboundmin + 1 )
                shade_west = shade_map[MAP_TILE_COORD(x - 1, z, level)];
            if( x < xboundmax - 1 )
                shade_east = shade_map[MAP_TILE_COORD(x + 1, z, level)];
            if( z < zboundmax - 1 )
                shade_north = shade_map[MAP_TILE_COORD(x, z + 1, level)];
            if( z > zboundmin + 1 )
                shade_south = shade_map[MAP_TILE_COORD(x, z - 1, level)];

            int shade_center = shade_map[MAP_TILE_COORD(x, z, level)];

            int shade = shade_center >> 1;
            shade += shade_west >> 2;
            shade += shade_east >> 3;
            shade += shade_north >> 3;
            shade += shade_south >> 2;

            int light = lightmap[MAP_TILE_COORD(x, z, 0)];

            lightmap[MAP_TILE_COORD(x, z, 0)] = light - shade;
        }
    }
}

#endif