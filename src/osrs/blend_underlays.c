#include "blend_underlays.h"

#include "palette.h"
#include "tables/maps.h"

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
    struct MapTerrain* map_terrain,
    struct Underlay* underlays,
    int* underlay_ids,
    int underlays_count,
    int level)
{
    int size_x = MAP_TERRAIN_X;
    int size_y = MAP_TERRAIN_Y;
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

                    struct Underlay* underlay = &underlays[idx];

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

                    struct Underlay* underlay = &underlays[idx];
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
        for( int yi = blend_start_y; yi < blend_end_y; yi++ )
        {
            // Check north boundary
            int y_north = yi + BLEND_RADIUS;
            if( y_north >= 0 && y_north < size_y )
            {
                running_chroma += chroma[y_north + BLEND_RADIUS];
                running_sat += sats[y_north + BLEND_RADIUS];
                running_light += light[y_north + BLEND_RADIUS];
                running_luminance += luminance[y_north + BLEND_RADIUS];
                running_number += counts[y_north + BLEND_RADIUS];
            }

            // Check south boundary
            int y_south = yi - BLEND_RADIUS;
            if( y_south >= 0 && y_south < size_y )
            {
                running_chroma -= chroma[yi];
                running_sat -= sats[yi];
                running_light -= light[yi];
                running_luminance -= luminance[yi];
                running_number -= counts[yi];
            }

            if( yi < 0 || yi >= size_y )
                continue;

            int avg_hue = (running_chroma * 256 / running_luminance) & 0xFF;
            int avg_sat = (running_sat / running_number) & 0xFF;
            int avg_light = (running_light / running_number) & 0xFF;

            if( avg_light > 255 )
                avg_light = 255;
            if( avg_light < 0 )
                avg_light = 0;

            int hsl = palette_hsl24_to_hsl16(avg_hue, avg_sat, avg_light);

            colors[COLOR_COORD(xi, yi)] = hsl;
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
    struct MapTerrain* map_terrain,
    struct Underlay* underlays,
    int* underlay_ids,
    int underlays_count,
    int level)
{
    int size_x = MAP_TERRAIN_X;
    int size_y = MAP_TERRAIN_Y;
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

                    struct Underlay* underlay = &underlays[idx];

                    struct HSL hsl = palette_rgb_to_hsl24(underlay->rgb_color);

                    chroma[yi + BLEND_RADIUS] += hsl.chroma * 256 / hsl.luminance;
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

                    struct Underlay* underlay = &underlays[idx];
                    struct HSL hsl = palette_rgb_to_hsl24(underlay->rgb_color);

                    chroma[yi + BLEND_RADIUS] -= hsl.chroma * 256 / hsl.luminance;
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
        for( int yi = blend_start_y; yi < blend_end_y; yi++ )
        {
            // Check north boundary
            int y_north = yi + BLEND_RADIUS;
            if( y_north >= 0 && y_north < size_y )
            {
                running_chroma += chroma[y_north + BLEND_RADIUS];
                running_sat += sats[y_north + BLEND_RADIUS];
                running_light += light[y_north + BLEND_RADIUS];
                running_luminance += luminance[y_north + BLEND_RADIUS];
                running_number += counts[y_north + BLEND_RADIUS];
            }

            // Check south boundary
            int y_south = yi - BLEND_RADIUS;
            if( y_south >= 0 && y_south < size_y )
            {
                running_chroma -= chroma[yi];
                running_sat -= sats[yi];
                running_light -= light[yi];
                running_luminance -= luminance[yi];
                running_number -= counts[yi];
            }

            if( yi < 0 || yi >= size_y )
                continue;

            underlay_id = map_terrain->tiles_xyz[MAP_TILE_COORD(xi, yi, level)].underlay_id;
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

                colors[COLOR_COORD(xi, yi)] = hsl;
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

// int*
// blend_underlays(
//     struct MapTerrain* map_terrain,
//     struct Underlay* underlays,
//     int* underlay_ids,
//     int underlays_count,
//     int level)
// {
//     int size = MAP_TERRAIN_X + BLEND_RADIUS * 2;

//     // Allocate arrays for color information
//     int* colors = malloc(MAP_TERRAIN_X * MAP_TERRAIN_Y * sizeof(int));
//     for( int i = 0; i < MAP_TERRAIN_X * MAP_TERRAIN_Y; i++ )
//         colors[i] = -1;

//     // Allocate arrays for HSL calculations
//     int* sats = calloc(size, sizeof(int));
//     int* light = calloc(size, sizeof(int));
//     int* luminance = calloc(size, sizeof(int));
//     int* chroma = calloc(size, sizeof(int));
//     int* counts = calloc(size, sizeof(int));

//     int blend_start_x = -BLEND_RADIUS;
//     int blend_start_y = -BLEND_RADIUS;
//     int blend_end_x = MAP_TERRAIN_X + BLEND_RADIUS;
//     int blend_end_y = MAP_TERRAIN_Y + BLEND_RADIUS;

//     int underlay_id;

//     // Process each column
//     for( int xi = blend_start_x; xi < blend_end_x; xi++ )
//     {
//         // Process each row in the column
//         for( int yi = 0; yi < size_y; yi++ )
//         {
//             // Check east boundary
//             int x_east = xi + BLEND_RADIUS;
//             if( x_east >= 0 && x_east < size_x )
//             {
//                 underlay_id = map_terrain->tiles_xyz[MAP_TILE_COORD(x_east, yi,
//                 level)].underlay_id; if( underlay_id > 0 )
//                 {
//                     int idx = get_index(underlay_ids, underlays_count, underlay_id - 1);
//                     if( idx != -1 )
//                     {
//                         struct Underlay* underlay = &underlays[idx];

//                         struct HSL hsl = palette_rgb_to_hsl24(underlay->rgb_color);
//                         printf(
//                             "underlay(%d): %d, hsl: %d, %d, %d\n",
//                             idx,
//                             underlay->rgb_color,
//                             hsl.hue,
//                             hsl.sat,
//                             hsl.light);

//                         chroma[yi] += hsl.chroma;
//                         sats[yi] += hsl.sat;
//                         light[yi] += hsl.light;
//                         luminance[yi] += hsl.luminance;

//                         counts[yi]++;
//                     }
//                 }
//             }

//             // Check west boundary
//             int x_west = xi - BLEND_RADIUS;
//             if( x_west >= 0 && x_west < size_x )
//             {
//                 underlay_id = map_terrain->tiles_xyz[MAP_TILE_COORD(x_west, yi,
//                 level)].underlay_id; if( underlay_id > 0 && false )
//                 {
//                     int idx = get_index(underlay_ids, underlays_count, underlay_id - 1);
//                     if( idx != -1 )
//                     {
//                         struct Underlay* underlay = &underlays[idx];
//                         struct HSL hsl = palette_rgb_to_hsl24(underlay->rgb_color);

//                         printf(
//                             "underlay(%d): %d, hsl: %d, %d, %d\n",
//                             idx,
//                             underlay->rgb_color,
//                             hsl.hue,
//                             hsl.sat,
//                             hsl.light);

//                         chroma[yi] -= hsl.chroma;
//                         sats[yi] -= hsl.sat;
//                         light[yi] -= hsl.light;
//                         luminance[yi] -= hsl.luminance;

//                         counts[yi]--;
//                     }
//                 }
//             }
//         }

//         if( xi < 1 || xi >= size_x - 1 )
//             continue;

//         int running_hues = 0;
//         int running_sat = 0;
//         int running_light = 0;
//         int running_luminance = 0;
//         int running_number = 0;
//         int running_chroma = 0;

//         // Process each row
//         for( int yi = blend_start_y; yi < blend_end_y; yi++ )
//         {
//             // Check north boundary
//             int y_north = yi + BLEND_RADIUS;
//             if( y_north >= 0 && y_north < size_y )
//             {
//                 running_chroma += chroma[y_north];
//                 running_sat += sats[y_north];
//                 running_light += light[y_north];
//                 running_luminance += luminance[y_north];
//                 running_number += counts[y_north];
//             }

//             // Check south boundary
//             int y_south = yi - BLEND_RADIUS;
//             if( y_south >= 0 && y_south < size_y )
//             {
//                 running_chroma -= chroma[y_south];
//                 running_sat -= sats[y_south];
//                 running_light -= light[y_south];
//                 running_luminance -= luminance[y_south];
//                 running_number -= counts[y_south];
//             }

//             if( yi < 1 || yi >= size_y - 1 )
//                 continue;

//             underlay_id = map_terrain->tiles_xyz[MAP_TILE_COORD(xi, yi, level)].underlay_id;
//             if( underlay_id > 0 )
//             {
//                 int avg_hue = (running_chroma * 256 / running_luminance) & 0xFF;
//                 int avg_sat = (running_sat / running_number) & 0xFF;
//                 int avg_light = (running_light / running_number) & 0xFF;

//                 if( avg_light > 255 )
//                     avg_light = 255;
//                 if( avg_light < 0 )
//                     avg_light = 0;

//                 int hsl = palette_hsl24_to_hsl16(avg_hue, avg_sat, avg_light);

//                 colors[COLOR_COORD(xi, yi)] = hsl;
//             }
//         }
//     }

//     // Free allocated memory

//     free(sats);
//     free(light);
//     free(luminance);
//     free(chroma);
//     free(counts);

//     return colors;
// }