#include "blend_underlays.h"

#include "configmap.h"
#include "palette.h"
#include "tables/config_floortype.h"
#include "tables/maps.h"

// clang-format off
#include "terrain.u.c"
// clang-format on

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLEND_RADIUS 5

static int
get_index(int* ids, int count, int id)
{
    for( int i = 0; i < count; i++ )
    {
        if( ids[i] == id )
            return i;
    }
    return -1;
}

/**
 * @brief This matches the summing of the hues and chroma that RuneLite does.
 * I do not believe it is accurate to the deob.
 *
 * @param map_terrain
 * @param underlays
 * @param underlay_ids
 * @param underlays_count
 * @param level
 * @return int*
 */
int*
blend_underlays_runelite(
    struct CacheMapTerrain* map_terrain,
    struct CacheConfigUnderlay* underlays,
    int* underlay_ids,
    int underlays_count,
    int level)
{
    int size_x = MAP_TERRAIN_X;
    int size_y = MAP_TERRAIN_Z;
    int max_size = size_x > size_y ? size_x : size_y;

    // Allocate arrays for color information
    int* colors = malloc(size_x * size_y * sizeof(int));
    for( int i = 0; i < size_x * size_y; i++ )
        colors[i] = -1;

    // Allocate arrays for HSL calculations
    int* sats = calloc(max_size, sizeof(int));
    int* light = calloc(max_size, sizeof(int));
    int* luminance = calloc(max_size, sizeof(int));
    int* chroma = calloc(max_size, sizeof(int));
    int* counts = calloc(max_size, sizeof(int));

    memset(sats, 0, max_size * sizeof(int));
    memset(light, 0, max_size * sizeof(int));
    memset(luminance, 0, max_size * sizeof(int));
    memset(chroma, 0, max_size * sizeof(int));
    memset(counts, 0, max_size * sizeof(int));

    int blend_start_x = -BLEND_RADIUS;
    int blend_start_y = -BLEND_RADIUS;
    int blend_end_x = size_x + BLEND_RADIUS;
    int blend_end_y = size_y + BLEND_RADIUS;

    int underlay_id;

    // Process each column
    for( int xi = blend_start_x; xi < blend_end_x; xi++ )
    {
        // Process each row in the column
        for( int yi = 0; yi < size_y; yi++ )
        {
            // Check east boundary
            int x_east = xi + BLEND_RADIUS;
            if( x_east >= 0 && x_east < size_x )
            {
                underlay_id = map_terrain->tiles_xyz[MAP_TILE_COORD(x_east, yi, level)].underlay_id;
                if( underlay_id > 0 )
                {
                    int idx = get_index(underlay_ids, underlays_count, underlay_id - 1);
                    assert(idx != -1);

                    struct CacheConfigUnderlay* underlay = &underlays[idx];

                    struct HSL hsl = palette_rgb_to_hsl24(underlay->rgb_color);

                    chroma[yi + BLEND_RADIUS] += hsl.chroma;
                    sats[yi + BLEND_RADIUS] += hsl.sat;
                    light[yi + BLEND_RADIUS] += hsl.light;
                    luminance[yi + BLEND_RADIUS] += hsl.luminance;

                    counts[yi]++;
                }
            }

            // Check west boundary
            int x_west = xi - BLEND_RADIUS;
            if( x_west >= 0 && x_west < size_x )
            {
                underlay_id = map_terrain->tiles_xyz[MAP_TILE_COORD(x_west, yi, level)].underlay_id;
                if( underlay_id > 0 )
                {
                    int idx = get_index(underlay_ids, underlays_count, underlay_id - 1);
                    assert(idx != -1);

                    struct CacheConfigUnderlay* underlay = &underlays[idx];
                    struct HSL hsl = palette_rgb_to_hsl24(underlay->rgb_color);

                    chroma[yi + BLEND_RADIUS] -= hsl.chroma;
                    sats[yi + BLEND_RADIUS] -= hsl.sat;
                    light[yi + BLEND_RADIUS] -= hsl.light;
                    luminance[yi + BLEND_RADIUS] -= hsl.luminance;

                    counts[yi]--;
                }
            }
        }

        if( xi < 0 || xi >= size_x )
            continue;

        int running_hues = 0;
        int running_sat = 0;
        int running_light = 0;
        int running_luminance = 0;
        int running_number = 0;
        int running_chroma = 0;

        // Process each row
        for( int zi = blend_start_y; zi < blend_end_y; zi++ )
        {
            // Check north boundary
            int z_north = zi + BLEND_RADIUS;
            if( z_north >= 0 && z_north < size_y )
            {
                running_chroma += chroma[z_north + BLEND_RADIUS];
                running_sat += sats[z_north + BLEND_RADIUS];
                running_light += light[z_north + BLEND_RADIUS];
                running_luminance += luminance[z_north + BLEND_RADIUS];
                running_number += counts[z_north + BLEND_RADIUS];
            }

            // Check south boundary
            int z_south = zi - BLEND_RADIUS;
            if( z_south >= 0 && z_south < size_y )
            {
                running_chroma -= chroma[z_south];
                running_sat -= sats[z_south];
                running_light -= light[z_south];
                running_luminance -= luminance[z_south];
                running_number -= counts[z_south];
            }

            if( zi < 0 || zi >= size_y )
                continue;

            int avg_hue = (running_chroma * 256 / running_luminance) & 0xFF;
            int avg_sat = (running_sat / running_number) & 0xFF;
            int avg_light = (running_light / running_number) & 0xFF;

            if( avg_light > 255 )
                avg_light = 255;
            if( avg_light < 0 )
                avg_light = 0;

            int hsl = palette_hsl24_to_hsl16(avg_hue, avg_sat, avg_light);

            colors[COLOR_COORD(xi, zi)] = hsl;
        }
    }

    // Free allocated memory

    free(sats);
    free(light);
    free(luminance);
    free(chroma);
    free(counts);

    return colors;
}

int*
blend_underlays(
    struct CacheMapTerrainIter* terrain, struct ConfigMap* config_underlay_map, int level)
{
    struct CacheConfigUnderlay* entry = NULL;
    struct CacheMapFloor* tile = NULL;

    struct HSL hsl;
    int size_x = terrain->chunks_width * MAP_TERRAIN_X;
    int size_z = (terrain->chunks_count / terrain->chunks_width) * MAP_TERRAIN_Z;
    int max_size = size_x > size_z ? size_x : size_z;

    // Allocate arrays for color information
    int* colors = malloc(size_x * size_z * sizeof(int));
    for( int i = 0; i < size_x * size_z; i++ )
        colors[i] = -1;

    // Allocate arrays for HSL calculations
    int* sats = calloc(max_size, sizeof(int));
    int* light = calloc(max_size, sizeof(int));
    int* luminance = calloc(max_size, sizeof(int));
    int* chroma = calloc(max_size, sizeof(int));
    int* counts = calloc(max_size, sizeof(int));

    memset(sats, 0, max_size * sizeof(int));
    memset(light, 0, max_size * sizeof(int));
    memset(luminance, 0, max_size * sizeof(int));
    memset(chroma, 0, max_size * sizeof(int));
    memset(counts, 0, max_size * sizeof(int));

    int blend_start_x = -BLEND_RADIUS;
    int blend_start_y = -BLEND_RADIUS;
    int blend_end_x = size_x + BLEND_RADIUS;
    int blend_end_y = size_z + BLEND_RADIUS;

    int underlay_id;

    // Process each column
    for( int xi = blend_start_x; xi < blend_end_x; xi++ )
    {
        // Process each row in the column
        for( int zi = 0; zi < size_z; zi++ )
        {
            // Check east boundary
            int x_east = xi + BLEND_RADIUS;
            if( x_east >= 0 && x_east < size_x )
            {
                tile = map_terrain_iter_at(terrain, x_east, zi, level);
                assert(tile != NULL);
                underlay_id = tile->underlay_id;
                if( underlay_id > 0 )
                {
                    entry = (struct CacheConfigUnderlay*)configmap_get(
                        config_underlay_map, underlay_id - 1);
                    assert(entry != NULL);

                    hsl = palette_rgb_to_hsl24(entry->rgb_color);

                    chroma[zi] += hsl.chroma * 256 / hsl.luminance;
                    sats[zi] += hsl.sat;
                    light[zi] += hsl.light;
                    luminance[zi] += hsl.luminance;

                    counts[zi]++;
                }
            }

            // Check west boundary
            int x_west = xi - BLEND_RADIUS;
            if( x_west >= 0 && x_west < size_x )
            {
                tile = map_terrain_iter_at(terrain, x_west, zi, level);
                assert(tile != NULL);

                underlay_id = tile->underlay_id;
                if( underlay_id > 0 )
                {
                    entry = (struct CacheConfigUnderlay*)configmap_get(
                        config_underlay_map, underlay_id - 1);
                    assert(entry != NULL);

                    hsl = palette_rgb_to_hsl24(entry->rgb_color);

                    chroma[zi] -= hsl.chroma * 256 / hsl.luminance;
                    sats[zi] -= hsl.sat;
                    light[zi] -= hsl.light;
                    luminance[zi] -= hsl.luminance;

                    counts[zi]--;
                }
            }
        }

        if( xi < 0 || xi >= size_x )
            continue;

        int running_hues = 0;
        int running_sat = 0;
        int running_light = 0;
        int running_luminance = 0;
        int running_number = 0;
        int running_chroma = 0;

        // Process each row
        for( int zi = blend_start_y; zi < blend_end_y; zi++ )
        {
            // Check north boundary
            int z_north = zi + BLEND_RADIUS;
            if( z_north >= 0 && z_north < size_z )
            {
                running_chroma += chroma[z_north];
                running_sat += sats[z_north];
                running_light += light[z_north];
                running_luminance += luminance[z_north];
                running_number += counts[z_north];
            }

            // Check south boundary
            int z_south = zi - BLEND_RADIUS;
            if( z_south >= 0 && z_south < size_z )
            {
                running_chroma -= chroma[z_south];
                running_sat -= sats[z_south];
                running_light -= light[z_south];
                running_luminance -= luminance[z_south];
                running_number -= counts[z_south];
            }

            if( zi < 0 || zi >= size_z )
                continue;

            tile = map_terrain_iter_at(terrain, xi, zi, level);
            assert(tile != NULL);
            underlay_id = tile->underlay_id;
            if( underlay_id > 0 )
            {
                int avg_hue = (running_chroma / running_number) & 0xFF;
                int avg_sat = (running_sat / running_number) & 0xFF;
                int avg_light = (running_light / running_number) & 0xFF;

                if( avg_light > 255 )
                    avg_light = 255;
                if( avg_light < 0 )
                    avg_light = 0;

                int hsl = palette_hsl24_to_hsl16(avg_hue, avg_sat, avg_light);

                colors[terrain_tile_coord(terrain->chunks_width, xi, zi, 0)] = hsl;
            }
        }
    }

    // Free allocated memory

    free(sats);
    free(light);
    free(luminance);
    free(chroma);
    free(counts);

    return colors;
}
