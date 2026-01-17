#ifndef SCENE_BUILDER_TERRAIN_U_C
#define SCENE_BUILDER_TERRAIN_U_C

#include "configmap.h"
#include "palette.h"
#include "rscache/tables/config_floortype.h"
#include "rscache/tables/maps.h"
#include "scene.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// clang-format off
#include "terrain_grid.u.c"
#include "terrain_decode_tile.u.c"
#include "scenebuilder_shademap.u.c"
#include "scenebuilder.u.c"
// clang-format on

#define LIGHT_DIR_X -50
#define LIGHT_DIR_Y -10
#define LIGHT_DIR_Z -50
#define LIGHT_AMBIENT 96
// Over 256, so 768 / 256 = 3, the lightness is divided by 3.
#define LIGHT_ATTENUATION 768
#define HEIGHT_SCALE 65536

#define BLEND_RADIUS 5

static inline int
grid_coord(
    int width,
    int sx,
    int sz,
    int slevel)
{
    int tile_coord = sx + sz * width + slevel * width * width;
    return tile_coord;
}

static int*
blend_underlays(
    struct TerrainGrid* terrain_grid,
    struct DashMap* config_underlay_map,
    int level)
{
    struct CacheConfigUnderlay* entry = NULL;
    struct CacheMapFloor* tile = NULL;

    struct HSL hsl;
    int size_x = terrain_grid_x_width(terrain_grid);
    int size_z = terrain_grid_z_height(terrain_grid);
    int max_size = size_x > size_z ? size_x : size_z;

    // Allocate arrays for color information
    int* colors = malloc(size_x * size_z * sizeof(int));
    for( int i = 0; i < size_x * size_z; i++ )
        colors[i] = -1;

    // // Allocate arrays for HSL calculations
    // int* sats = calloc(max_size, sizeof(int));
    // int* light = calloc(max_size, sizeof(int));
    // int* luminance = calloc(max_size, sizeof(int));
    // int* chroma = calloc(max_size, sizeof(int));
    // int* counts = calloc(max_size, sizeof(int));

    int sats[512] = { 0 };
    int light[512] = { 0 };
    int luminance[512] = { 0 };
    int chroma[512] = { 0 };
    int counts[512] = { 0 };

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
                tile = tile_from_sw_origin(terrain_grid, x_east, zi, level);
                assert(tile != NULL);
                underlay_id = tile->underlay_id;

                if( underlay_id > 0 )
                {
                    entry = (struct CacheConfigUnderlay*)configmap_get(
                        config_underlay_map, underlay_id - 1);
                    assert(entry != NULL);

                    hsl = palette_rgb_to_hsl24(entry->rgb_color);

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
                tile = tile_from_sw_origin(terrain_grid, x_west, zi, level);
                assert(tile != NULL);

                underlay_id = tile->underlay_id;
                if( underlay_id > 0 )
                {
                    entry = (struct CacheConfigUnderlay*)configmap_get(
                        config_underlay_map, underlay_id - 1);
                    assert(entry != NULL);

                    hsl = palette_rgb_to_hsl24(entry->rgb_color);

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

            tile = tile_from_sw_origin(terrain_grid, xi, zi, level);
            assert(tile != NULL);
            underlay_id = tile->underlay_id;
            if( underlay_id > 0 )
            {
                int avg_hue = ((running_chroma * 256) / running_luminance) & 0xFF;
                int avg_sat = (running_sat / running_number) & 0xFF;
                int avg_light = (running_light / running_number) & 0xFF;

                if( avg_light > 255 )
                    avg_light = 255;
                if( avg_light < 0 )
                    avg_light = 0;

                int hsl16 = palette_hsl24_to_hsl16(avg_hue, avg_sat, avg_light);

                int idx = grid_coord(terrain_grid_x_width(terrain_grid), xi, zi, 0);
                colors[idx] = hsl16;
            }
        }
    }

    // Free allocated memory

    // free(sats);
    // free(light);
    // free(luminance);
    // free(chroma);
    // free(counts);

    return colors;
}

static int*
calculate_lights(
    struct TerrainGrid* terrain_grid,
    int level)
{
    int* lights = (int*)malloc(terrain_grid_size(terrain_grid) * sizeof(int));
    memset(lights, 0, terrain_grid_size(terrain_grid) * sizeof(int));

    int max_x = terrain_grid_x_width(terrain_grid);
    int max_z = terrain_grid_z_height(terrain_grid);

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

            int height_delta_x = tile_from_sw_origin(terrain_grid, x + 1, z, level)->height -
                                 tile_from_sw_origin(terrain_grid, x - 1, z, level)->height;
            int height_delta_z = tile_from_sw_origin(terrain_grid, x, z + 1, level)->height -
                                 tile_from_sw_origin(terrain_grid, x, z - 1, level)->height;
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

            lights[grid_coord(terrain_grid_x_width(terrain_grid), x, z, 0)] = sunlight;
        }
    }

    return lights;
}

static void
apply_shade(
    int* lightmap,
    int map_width,
    struct Shademap* shademap_nullable,
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
    if( shademap_nullable == NULL )
        return;

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

            int shade = 0;
            if( shademap_nullable )
            {
                if( shademap_in_bounds(shademap_nullable, x - 1, z, level) )
                    shade_west = shademap_get(shademap_nullable, x - 1, z, level);
                if( shademap_in_bounds(shademap_nullable, x + 1, z, level) )
                    shade_east = shademap_get(shademap_nullable, x + 1, z, level);
                if( shademap_in_bounds(shademap_nullable, x, z + 1, level) )
                    shade_north = shademap_get(shademap_nullable, x, z + 1, level);
                if( shademap_in_bounds(shademap_nullable, x, z - 1, level) )
                    shade_south = shademap_get(shademap_nullable, x, z - 1, level);

                int shade_center = shademap_get(shademap_nullable, x, z, level);

                shade = shade_center >> 1;
                shade += shade_west >> 2;
                shade += shade_east >> 3;
                shade += shade_north >> 3;
                shade += shade_south >> 2;
            }
            int light = lightmap[grid_coord(map_width, x, z, 0)];

            lightmap[grid_coord(map_width, x, z, 0)] = light - shade;
        }
    }
}

static void
build_scene_terrain(
    struct SceneBuilder* scene_builder,
    struct TerrainGrid* terrain_grid,
    struct SceneTerrain* terrain)
{
    struct DashMap* config_underlay_map = scene_builder->config_underlay_configmap;
    struct DashMap* config_overlay_map = scene_builder->config_overlay_configmap;
    assert(configmap_valid(config_underlay_map) && "Config underlay map must be valid");
    assert(configmap_valid(config_overlay_map) && "Config overlay map must be valid");

    struct DashModel* model = NULL;
    struct CacheConfigUnderlay* underlay = NULL;
    struct CacheConfigOverlay* overlay = NULL;
    struct TerrainTileModel* chunk_tiles = NULL;
    struct CacheMapFloor* map = NULL;
    struct SceneTerrainTile* tile = NULL;

    int max_z = terrain_grid_z_height(terrain_grid);
    int max_x = terrain_grid_x_width(terrain_grid);
    int tile_count = terrain_grid_size(terrain_grid);

    for( int level = 0; level < MAP_TERRAIN_LEVELS; level++ )
    {
        int* blended_underlays = blend_underlays(terrain_grid, config_underlay_map, level);
        int* lights = calculate_lights(terrain_grid, level);

        apply_shade(
            lights, max_x, scene_builder->shademap, level, 0, max_x, 0, max_z, 0, max_x, 0, max_z);

        for( int z = 1; z < max_z - 1; z++ )
        {
            for( int x = 1; x < max_x - 1; x++ )
            {
                map = tile_from_sw_origin(terrain_grid, x, z, level);

                tile = scene_terrain_tile_at(terrain, x, z, level);
                int underlay_id = map->underlay_id - 1;

                int overlay_id = map->overlay_id - 1;

                if( underlay_id == -1 && overlay_id == -1 )
                    continue;

                int height_sw = tile_from_sw_origin(terrain_grid, x, z, level)->height;
                int height_se = tile_from_sw_origin(terrain_grid, x + 1, z, level)->height;
                int height_ne = tile_from_sw_origin(terrain_grid, x + 1, z + 1, level)->height;
                int height_nw = tile_from_sw_origin(terrain_grid, x, z + 1, level)->height;

                int light_sw = lights[grid_coord(terrain_grid_x_width(terrain_grid), x, z, 0)];
                int light_se = lights[grid_coord(terrain_grid_x_width(terrain_grid), x + 1, z, 0)];
                int light_ne =
                    lights[grid_coord(terrain_grid_x_width(terrain_grid), x + 1, z + 1, 0)];
                int light_nw = lights[grid_coord(terrain_grid_x_width(terrain_grid), x, z + 1, 0)];

                int underlay_hsl = -1;
                int overlay_hsl = 0;

                if( underlay_id != -1 )
                {
                    underlay = (struct CacheConfigUnderlay*)configmap_get(
                        config_underlay_map, underlay_id);
                    assert(underlay != NULL);

                    if( x == 183 && z == 103 )
                    {
                        printf("underlay_hsl: %d\n", underlay_hsl);
                    }

                    // underlay_hsl = palette_rgb_to_hsl16(underlay->rgb_color);
                    underlay_hsl =
                        blended_underlays[grid_coord(terrain_grid_x_width(terrain_grid), x, z, 0)];

                    /**
                     * This is confusing.
                     *
                     * When this is false, the underlays are rendered correctly.
                     * When this is true, they are not.
                     *
                     * I checked the underlay rendering with the actual osrs client,
                     * and the underlays render correct when SMOOTH_UNDERLAYS is false.
                     *
                     * See
                     * ![my_renderer](res/underlay_blending/underlay_my_renderer_no_smooth_blending.png)
                     * and ![osrs_client](res/underlay_blending/underlay_osrs.png)
                     *
                     * This works for rsmap viewer because rsmap viewer blends rgb in opengl, not
                     * just lightness.
                     *
                     * The real DeOb actually just uses lightness
                     */
                    // underlay_hsl_se = blended_underlays[COLOR_COORD(x + 1, y)];
                    // underlay_hsl_ne = blended_underlays[COLOR_COORD(x + 1, y + 1)];
                    // underlay_hsl_nw = blended_underlays[COLOR_COORD(x, y + 1)];
                    // if( underlay_hsl_se == -1 || !SMOOTH_UNDERLAYS )
                    //     underlay_hsl_se = underlay_hsl_sw;
                    // if( underlay_hsl_ne == -1 || !SMOOTH_UNDERLAYS )
                    //     underlay_hsl_ne = underlay_hsl_sw;
                    // if( underlay_hsl_nw == -1 || !SMOOTH_UNDERLAYS )
                    //     underlay_hsl_nw = underlay_hsl_sw;
                }

                int shape = overlay_id == -1 ? 0 : (map->shape + 1);
                int rotation = overlay_id == -1 ? 0 : map->rotation;
                int texture_id = -1;

                if( overlay_id != -1 )
                {
                    overlay =
                        (struct CacheConfigOverlay*)configmap_get(config_overlay_map, overlay_id);
                    assert(overlay != NULL);

                    if( overlay->texture != -1 )
                    {
                        overlay_hsl = -1;
                        texture_id = overlay->texture;
                    }
                    // Magenta is transparent 16711935
                    else if( overlay->rgb_color == 0xFF00FF )
                    {
                        overlay_hsl = -2;
                        texture_id = -1;
                    }
                    else
                    {
                        int from_rgb = overlay->secondary_rgb_color != -1
                                           ? overlay->secondary_rgb_color
                                           : overlay->rgb_color;
                        // overlay_hsl = 0xFF00FF;
                        int hsl = palette_rgb_to_hsl16(from_rgb);
                        overlay_hsl = adjust_lightness(hsl, 96);
                    }
                }

                tile->sx = x;
                tile->sz = z;
                tile->slevel = level;

                assert(tile->dash_model == NULL);

                model = decode_tile(
                    shape,
                    rotation,
                    texture_id,
                    x,
                    z,
                    level,
                    height_sw,
                    height_se,
                    height_ne,
                    height_nw,
                    light_sw,
                    light_se,
                    light_ne,
                    light_nw,
                    underlay_hsl,
                    overlay_hsl);

                tile->dash_model = model;

                assert(model);
            }
        }

        free(blended_underlays);
        free(lights);
    }
}

#endif