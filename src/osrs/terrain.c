#include "terrain.h"

#include "blend_underlays.h"
#include "graphics/dash.h"
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

struct TerrainTileModel*
terrain_tile_model_at(struct Terrain* terrain, int sx, int sz, int slevel)
{
    int tile_coord = terrain_tile_coord(terrain->side, sx, sz, slevel);
    assert(tile_coord < terrain->tile_count);
    return &terrain->tiles[tile_coord];
}

// /Users/matthewevers/Documents/git_repos/meteor-client/osrs/src/main/java/Scene.java
// /Users/matthewevers/Documents/git_repos/meteor-client/osrs/src/main/java/class481.java
struct Terrain*
terrain_new_from_map_terrain(
    struct CacheMapTerrainIter* terrain_iter,
    int* shade_map_nullable,
    struct ConfigMap* config_underlay_map,
    struct ConfigMap* config_overlay_map)
{
    struct Terrain* terrain = (struct Terrain*)malloc(sizeof(struct Terrain));
    memset(terrain, 0, sizeof(struct Terrain));

    struct CacheConfigUnderlay* underlay = NULL;
    struct CacheConfigOverlay* overlay = NULL;
    struct TerrainTileModel* chunk_tiles = NULL;
    int max_z = terrain_iter->chunks_count / terrain_iter->chunks_width * MAP_TERRAIN_Z;
    int max_x = terrain_iter->chunks_width * MAP_TERRAIN_X;
    int tile_count = CHUNK_TILE_COUNT * terrain_iter->chunks_count;
    struct TerrainTileModel* tiles =
        (struct TerrainTileModel*)malloc(tile_count * sizeof(struct TerrainTileModel));
    memset(tiles, 0, tile_count * sizeof(struct TerrainTileModel));

    terrain->tiles = tiles;
    terrain->tile_count = tile_count;
    terrain->side = terrain_iter->chunks_width;

    if( !tiles )
    {
        fprintf(stderr, "Failed to allocate memory for tiles\n");
        return NULL;
    }

    for( int chunk_index = 0; chunk_index < terrain_iter->chunks_count; chunk_index++ )
    {
        chunk_tiles = &tiles[chunk_index * CHUNK_TILE_COUNT];

        // Blend underlays and calculate lights
        for( int i = 0; i < CHUNK_TILE_COUNT; i++ )
            memset(&chunk_tiles[i], 0, sizeof(struct TerrainTileModel));
    }

    for( int level = 0; level < MAP_TERRAIN_LEVELS; level++ )
    {
        int* blended_underlays = blend_underlays(terrain_iter, config_underlay_map, level);
        int* lights = calculate_lights(terrain_iter, level);

        apply_shade(lights, shade_map_nullable, level, 0, max_x, 0, max_z, 0, max_x, 0, max_z);

        for( int z = 1; z < max_z - 1; z++ )
        {
            for( int x = 1; x < max_x - 1; x++ )
            {
                struct CacheMapFloor* map = map_terrain_iter_at(terrain_iter, x, z, level);

                struct TerrainTileModel* tile = terrain_tile_model_at(terrain, x, z, level);
                int underlay_id = map->underlay_id - 1;

                int overlay_id = map->overlay_id - 1;

                if( underlay_id == -1 && overlay_id == -1 )
                    continue;

                int height_sw = map_terrain_iter_at(terrain_iter, x, z, level)->height;
                int height_se = map_terrain_iter_at(terrain_iter, x + 1, z, level)->height;
                int height_ne = map_terrain_iter_at(terrain_iter, x + 1, z + 1, level)->height;
                int height_nw = map_terrain_iter_at(terrain_iter, x, z + 1, level)->height;

                int light_sw = lights[terrain_tile_coord(terrain_iter->chunks_width, x, z, 0)];
                int light_se = lights[terrain_tile_coord(terrain_iter->chunks_width, x + 1, z, 0)];
                int light_ne =
                    lights[terrain_tile_coord(terrain_iter->chunks_width, x + 1, z + 1, 0)];
                int light_nw = lights[terrain_tile_coord(terrain_iter->chunks_width, x, z + 1, 0)];

                int underlay_hsl = -1;
                int overlay_hsl = 0;

                if( underlay_id != -1 )
                {
                    underlay = (struct CacheConfigUnderlay*)configmap_get(
                        config_underlay_map, underlay_id);
                    assert(underlay != NULL);

                    underlay_hsl =
                        blended_underlays[terrain_tile_coord(terrain_iter->chunks_width, x, z, 0)];

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

                tile->cx = x;
                tile->cz = z;
                tile->clevel = level;

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

    return terrain;
}