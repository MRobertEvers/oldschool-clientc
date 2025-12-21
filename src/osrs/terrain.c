#include "terrain.h"

#include "blend_underlays.h"
#include "palette.h"
#include "tables/config_floortype.h"
#include "tables/maps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// clang-format off
#include "terrain.u.c"
#include "terrain_decode_tile.u.c"
// clang-format on

struct TileHeights
terrain_tile_heights_at(struct CacheMapTerrainIter* terrain, int sx, int sz, int level)
{
    struct TileHeights tile_heights;

    int height_sw = map_terrain_iter_at(terrain, sx, sz, level)->height;
    int height_se = height_sw;
    if( sx + 1 < map_terrain_iter_bound_x(terrain) )
        height_se = map_terrain_iter_at(terrain, sx + 1, sz, level)->height;

    int height_ne = height_sw;
    if( sz + 1 < map_terrain_iter_bound_z(terrain) && sx + 1 < map_terrain_iter_bound_x(terrain) )
        height_ne = map_terrain_iter_at(terrain, sx + 1, sz + 1, level)->height;

    int height_nw = height_sw;
    if( sz + 1 < map_terrain_iter_bound_z(terrain) )
        height_nw = map_terrain_iter_at(terrain, sx, sz + 1, level)->height;

    int height_center = (height_sw + height_se + height_ne + height_nw) >> 2;

    tile_heights.sw_height = height_sw;
    tile_heights.se_height = height_se;
    tile_heights.ne_height = height_ne;
    tile_heights.nw_height = height_nw;
    tile_heights.height_center = height_center;

    return tile_heights;
}

// /Users/matthewevers/Documents/git_repos/meteor-client/osrs/src/main/java/Scene.java
// /Users/matthewevers/Documents/git_repos/meteor-client/osrs/src/main/java/class481.java
struct Terrain*
terrain_new_from_map_terrain(
    struct CacheMapTerrainIter* terrain,
    int* shade_map_nullable,
    struct ConfigMap* config_underlay_map,
    struct ConfigMap* config_overlay_map)
{
    struct CacheConfigUnderlay* underlay = NULL;
    struct CacheConfigOverlay* overlay = NULL;
    struct TerrainTileModel* chunk_tiles = NULL;
    int max_z = MAP_TERRAIN_Z;
    int max_x = MAP_TERRAIN_X;
    struct TerrainTileModel* tiles = (struct TerrainTileModel*)malloc(
        CHUNK_TILE_COUNT * terrain->chunks_count * sizeof(struct TerrainTileModel));

    if( !tiles )
    {
        fprintf(stderr, "Failed to allocate memory for tiles\n");
        return NULL;
    }

    for( int chunk_index = 0; chunk_index < terrain->chunks_count; chunk_index++ )
    {
        chunk_tiles = &tiles[chunk_index * CHUNK_TILE_COUNT];

        // Blend underlays and calculate lights
        for( int i = 0; i < CHUNK_TILE_COUNT; i++ )
            memset(&chunk_tiles[i], 0, sizeof(struct TerrainTileModel));
    }

    for( int level = 0; level < MAP_TERRAIN_LEVELS; level++ )
    {
        int* blended_underlays = blend_underlays(terrain, config_underlay_map, level);
        int* lights = calculate_lights(terrain, level);

        if( shade_map_nullable )
            apply_shade(
                lights,
                shade_map_nullable,
                level,
                0,
                MAP_TERRAIN_X,
                0,
                MAP_TERRAIN_Z,
                0,
                MAP_TERRAIN_X,
                0,
                MAP_TERRAIN_Z);

        for( int z = 1; z < max_z - 1; z++ )
        {
            for( int x = 1; x < max_x - 1; x++ )
            {
                struct CacheMapFloor* map = map_terrain_iter_at(terrain, x, z, level);

                struct TerrainTileModel* tile = &tiles[MAP_TILE_COORD(x, z, level)];
                int underlay_id = map->underlay_id - 1;

                int overlay_id = map->overlay_id - 1;

                if( underlay_id == -1 && overlay_id == -1 )
                    continue;

                int height_sw = map_terrain_iter_at(terrain, x, z, level)->height;
                int height_se = map_terrain_iter_at(terrain, x + 1, z, level)->height;
                int height_ne = map_terrain_iter_at(terrain, x + 1, z + 1, level)->height;
                int height_nw = map_terrain_iter_at(terrain, x, z + 1, level)->height;

                int light_sw = lights[MAP_TILE_COORD(x, z, 0)];
                int light_se = lights[MAP_TILE_COORD(x + 1, z, 0)];
                int light_ne = lights[MAP_TILE_COORD(x + 1, z + 1, 0)];
                int light_nw = lights[MAP_TILE_COORD(x, z + 1, 0)];

                int underlay_hsl = -1;
                int overlay_hsl = 0;

                if( underlay_id != -1 )
                {
                    underlay = (struct CacheConfigUnderlay*)configmap_get(
                        config_underlay_map, underlay_id);
                    assert(underlay != NULL);

                    underlay_hsl = blended_underlays[COLOR_COORD(x, z)];

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

                if( overlay_id != -1 )
                {
                    overlay =
                        (struct CacheConfigOverlay*)configmap_get(config_overlay_map, overlay_id);
                    assert(overlay != NULL);

                    if( overlay->texture != -1 )
                        overlay_hsl = -1;
                    else
                        overlay_hsl =
                            adjust_lightness(palette_rgb_to_hsl16(overlay->rgb_color), 96);
                }

                int shape = overlay_id == -1 ? 0 : (map->shape + 1);
                int rotation = overlay_id == -1 ? 0 : map->rotation;
                int texture_id = overlay_id == -1 ? -1 : overlay->texture;

                tile->chunk_pos_x = x;
                tile->chunk_pos_z = z;
                tile->chunk_pos_level = level;

                bool success = decode_tile(
                    tile,
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

                assert(success);
            }
        }

        free(blended_underlays);
        free(lights);
    }

    return tiles;
}