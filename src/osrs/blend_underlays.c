#include "blend_underlays.h"

#include "palette.h"
#include "tables/maps.h"

#include <stdlib.h>
#include <string.h>

#define BLEND_RADIUS 5

// Helper function to pack HSL values into a single integer
static int
pack_hsl(int hue, int sat, int light)
{
    int h = hue;
    int s = sat;
    int l = light;
    return (h << 10) | (s << 7) | l;
}

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

int*
blend_underlays(
    struct MapTerrain* map_terrain,
    struct Underlay* underlays,
    int* underlay_ids,
    int underlays_count)
{
    int size_x = MAP_TERRAIN_X;
    int size_y = MAP_TERRAIN_Y;
    int max_size = size_x > size_y ? size_x : size_y;

    // Allocate arrays for color information
    int* colors = malloc(size_x * size_y * sizeof(int));
    for( int i = 0; i < size_x * size_y; i++ )
        colors[i] = -1;

    // Allocate arrays for HSL calculations
    int* hues = calloc(max_size, sizeof(int));
    int* sats = calloc(max_size, sizeof(int));
    int* light = calloc(max_size, sizeof(int));
    int* mul = calloc(max_size, sizeof(int));
    int* num = calloc(max_size, sizeof(int));

    int blend_start_x = -BLEND_RADIUS;
    int blend_start_y = -BLEND_RADIUS;
    int blend_end_x = size_x + BLEND_RADIUS;
    int blend_end_y = size_y + BLEND_RADIUS;

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
                int underlay_id = map_terrain->tiles_xyz[MAP_TILE_COORD(x_east, yi, 0)].underlay_id;
                if( underlay_id > 0 )
                {
                    int idx = get_index(underlay_ids, underlays_count, underlay_id - 1);
                    if( idx != -1 )
                    {
                        struct Underlay* underlay = &underlays[idx];

                        struct HSL hsl = palette_rgb_to_hsl(underlay->rgb_color);
                        int hue = hsl.hue;
                        int sat = hsl.sat;
                        int lightness = hsl.light;

                        hues[yi] += hue;
                        sats[yi] += sat;
                        light[yi] += lightness;
                        mul[yi] += hsl.mul;
                        num[yi]++;
                    }
                }
            }

            // Check west boundary
            int x_west = xi - BLEND_RADIUS;
            if( x_west >= 0 && x_west < size_x )
            {
                int underlay_id = map_terrain->tiles_xyz[MAP_TILE_COORD(x_west, yi, 0)].underlay_id;
                if( underlay_id > 0 )
                {
                    int idx = get_index(underlay_ids, underlays_count, underlay_id - 1);
                    if( idx != -1 )
                    {
                        struct Underlay* underlay = &underlays[idx];
                        struct HSL hsl = palette_rgb_to_hsl(underlay->rgb_color);

                        hues[yi] -= hsl.hue;
                        sats[yi] -= hsl.sat;
                        light[yi] -= hsl.light;
                        mul[yi] -= hsl.mul;
                        num[yi]--;
                    }
                }
            }
        }

        if( xi < 0 || xi >= size_x )
        {
            continue;
        }

        int running_hues = 0;
        int running_sat = 0;
        int running_light = 0;
        int running_multiplier = 0;
        int running_number = 0;

        // Process each row
        for( int yi = blend_start_y; yi < blend_end_y; yi++ )
        {
            // Check north boundary
            int y_north = yi + BLEND_RADIUS;
            if( y_north >= 0 && y_north < size_y )
            {
                running_hues += hues[y_north];
                running_sat += sats[y_north];
                running_light += light[y_north];
                running_multiplier += mul[y_north];
                running_number += num[y_north];
            }

            // Check south boundary
            int y_south = yi - BLEND_RADIUS;
            if( y_south >= 0 && y_south < size_y )
            {
                running_hues -= hues[y_south];
                running_sat -= sats[y_south];
                running_light -= light[y_south];
                running_multiplier -= mul[y_south];
                running_number -= num[y_south];
            }

            if( yi < 0 || yi >= size_y )
            {
                continue;
            }

            int underlay_id = map_terrain->tiles_xyz[MAP_TILE_COORD(xi, yi, 0)].underlay_id;
            if( underlay_id > 0 )
            {
                int avg_hue = running_hues / running_number;
                int avg_sat = running_sat / running_number;
                int avg_light = running_light / running_number;

                int hsl = pack_hsl(avg_hue, avg_sat, avg_light);
                colors[COLOR_COORD(xi, yi)] = hsl;
            }
        }
    }

    // Free allocated memory

    free(hues);
    free(sats);
    free(light);
    free(mul);
    free(num);

    return colors;
}