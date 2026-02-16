#include "blendmap.h"

#include "palette.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLEND_RADIUS 5

static inline int
blendmap_coord_idx(
    struct Blendmap* blendmap,
    int x,
    int z,
    int level)
{
    assert(x >= 0 && x < blendmap->width);
    assert(z >= 0 && z < blendmap->height);
    assert(level >= 0 && level < blendmap->levels);
    return x + z * blendmap->width + level * blendmap->width * blendmap->height;
}

static inline void
blendmap_set_blended_hsl16(
    struct Blendmap* blendmap,
    int x,
    int z,
    int level,
    int32_t hsl16)
{
    blendmap->blendmap[blendmap_coord_idx(blendmap, x, z, level)] = hsl16;
}

struct Blendmap*
blendmap_new(
    int width,
    int height,
    int levels)
{
    struct Blendmap* blendmap = malloc(sizeof(struct Blendmap));
    memset(blendmap, 0, sizeof(struct Blendmap));
    blendmap->underlaymap = malloc(width * height * levels * sizeof(uint32_t));
    memset(blendmap->underlaymap, 0, width * height * levels * sizeof(uint32_t));
    blendmap->blendmap = malloc(width * height * levels * sizeof(int32_t));
    memset(blendmap->blendmap, 0, width * height * levels * sizeof(int32_t));
    blendmap->width = width;
    blendmap->height = height;
    blendmap->levels = levels;

    for( int i = 0; i < width * height * levels; i++ )
    {
        blendmap->blendmap[i] = BLENDMAP_HSL16_NONE;
    }

    return blendmap;
}

void
blendmap_free(struct Blendmap* blendmap)
{
    free(blendmap->underlaymap);
    free(blendmap->blendmap);
    free(blendmap);
}

void
blendmap_set_underlay_rgb(
    struct Blendmap* blendmap,
    int x,
    int z,
    int level,
    uint32_t rgb)
{
    blendmap->underlaymap[blendmap_coord_idx(blendmap, x, z, level)] = rgb;
}

uint32_t
blendmap_get_underlay_rgb(
    struct Blendmap* blendmap,
    int x,
    int z,
    int level)
{
    return blendmap->underlaymap[blendmap_coord_idx(blendmap, x, z, level)];
}

int32_t
blendmap_get_blended_hsl16(
    struct Blendmap* blendmap,
    int x,
    int z,
    int level)
{
    assert(blendmap->status == BLENDMAP_STATUS_BLENDED);
    return blendmap->blendmap[blendmap_coord_idx(blendmap, x, z, level)];
}

void
blendmap_build(struct Blendmap* blendmap)
{
    struct CacheMapFloor* tile = NULL;

    struct HSL hsl;
    int size_x = blendmap->width;
    int size_z = blendmap->height;

    int sats[512] = { 0 };
    int light[512] = { 0 };
    int luminance[512] = { 0 };
    int chroma[512] = { 0 };
    int counts[512] = { 0 };

    int blend_start_x = -BLEND_RADIUS;
    int blend_start_y = -BLEND_RADIUS;
    int blend_end_x = size_x + BLEND_RADIUS;
    int blend_end_y = size_z + BLEND_RADIUS;
    uint32_t underlay_rgb = 0;

    for( int level = 0; level < blendmap->levels; level++ )
    {
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
                    underlay_rgb = blendmap_get_underlay_rgb(blendmap, x_east, zi, level);

                    if( underlay_rgb > 0 )
                    {
                        // struct FlotypeEntry* flotype_entry = NULL;
                        // int search_id = underlay_id - 1;
                        // flotype_entry = (struct FlotypeEntry*)dashmap_search(
                        //     config_underlay_map, &search_id, DASHMAP_FIND);
                        // assert(flotype_entry != NULL);

                        // hsl = palette_rgb_to_hsl24(flotype_entry->flotype->rgb_color);

                        // struct CacheConfigUnderlay* entry = (struct
                        // CacheConfigUnderlay*)configmap_get(
                        //     config_underlay_map, underlay_id - 1);
                        // assert(entry != NULL);

                        hsl = palette_rgb_to_hsl24(underlay_rgb);

                        chroma[zi] += hsl.chroma;
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
                    underlay_rgb = blendmap_get_underlay_rgb(blendmap, x_west, zi, level);

                    if( underlay_rgb > 0 )
                    {
                        // struct FlotypeEntry* flotype_entry = NULL;
                        // int search_id = underlay_id - 1;
                        // flotype_entry = (struct FlotypeEntry*)dashmap_search(
                        //     config_underlay_map, &search_id, DASHMAP_FIND);
                        // assert(flotype_entry != NULL);

                        // hsl = palette_rgb_to_hsl24(flotype_entry->flotype->rgb_color);

                        // struct CacheConfigUnderlay* entry = (struct
                        // CacheConfigUnderlay*)configmap_get(
                        //     config_underlay_map, underlay_id - 1);
                        // assert(entry != NULL);

                        hsl = palette_rgb_to_hsl24(underlay_rgb);

                        chroma[zi] -= hsl.chroma;
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

                underlay_rgb = blendmap_get_underlay_rgb(blendmap, xi, zi, level);
                if( underlay_rgb > 0 )
                {
                    int avg_hue = ((running_chroma * 256) / running_luminance) & 0xFF;
                    int avg_sat = (running_sat / running_number) & 0xFF;
                    int avg_light = (running_light / running_number) & 0xFF;

                    if( avg_light > 255 )
                        avg_light = 255;
                    if( avg_light < 0 )
                        avg_light = 0;

                    int hsl16 = palette_hsl24_to_hsl16(avg_hue, avg_sat, avg_light);

                    blendmap_set_blended_hsl16(blendmap, xi, zi, level, hsl16);
                }
                else
                {
                    blendmap_set_blended_hsl16(blendmap, xi, zi, level, -1);
                }
            }
        }
    }

    blendmap->status = BLENDMAP_STATUS_BLENDED;
}