#ifndef WORLD_TERRAIN_U_C
#define WORLD_TERRAIN_U_C

#include "blendmap.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "heightmap.h"
#include "lightmap.h"
#include "minimap.h"
#include "osrs/palette.h"
#include "overlaymap.h"
#include "shademap.h"
#include "terrain_shapemap.h"
#include "toridraw/toridraw_hsl16.h"
#include "toridraw/toridraw_model.h"
#include "world_builder.h"
#include "world_decode_tile.h"
#include "toridraw/toridraw_scene.h"

// clang-format off
#include "flag_map.u.c"
// clang-format on

#include <assert.h>
#include <stdint.h>

#define WORLD_TILE_SIZE 128

static void
world_apply_shade(
    struct WorldBuilder* builder,
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
    if( !builder->shademap )
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
            if( shademap2_in_bounds(builder->shademap, x - 1, z, level) )
                shade_west = shademap2_get(builder->shademap, x - 1, z, level);
            if( shademap2_in_bounds(builder->shademap, x + 1, z, level) )
                shade_east = shademap2_get(builder->shademap, x + 1, z, level);
            if( shademap2_in_bounds(builder->shademap, x, z + 1, level) )
                shade_north = shademap2_get(builder->shademap, x, z + 1, level);
            if( shademap2_in_bounds(builder->shademap, x, z - 1, level) )
                shade_south = shademap2_get(builder->shademap, x, z - 1, level);

            int shade_center = shademap2_get(builder->shademap, x, z, level);

            shade = shade_center >> 1;
            shade += shade_west >> 2;
            shade += shade_east >> 3;
            shade += shade_north >> 3;
            shade += shade_south >> 2;

            int light = lightmap_get(builder->lightmap, x, z, level);

            int shaded = light - shade;
            if( shaded < 0 )
                shaded = 0;
            lightmap_set(builder->lightmap, x, z, level, (uint8_t)shaded);
        }
    }
}

void
world_builder_rebuild_centerzone_chunk_terrain(
    struct WorldBuilder* builder,
    int mapx,
    int mapz)
{
    struct World* world = builder->world;
    int map_id = (mapx << 16) | (mapz & 0xFFFF);
    struct ToriAuxLibCore_MapTerrain* map_terrain = ToriAuxLibCore_MapTerrainGet(builder->core, map_id);
    assert(map_terrain && "Map terrain must be found");

    int scene_size = world->_scene_size;

    /* ---- Heightmap + flag_map ---- */

    for( int tile_x = 0; tile_x < WORLD_MAP_TERRAIN_X; tile_x++ )
    {
        for( int tile_z = 0; tile_z < WORLD_MAP_TERRAIN_Z; tile_z++ )
        {
            int offset_x = world_to_scene_x(world, mapx, tile_x);
            int offset_z = world_to_scene_z(world, mapz, tile_z);

            for( int level = 0; level < WORLD_MAP_TERRAIN_LEVELS; level++ )
            {
                int chunk_index = WORLD_MAP_TILE_COORD(tile_x, tile_z, level);
                struct ToriAuxLibCore_MapFloor* tile = &map_terrain->tiles_xyz[chunk_index];

                if( offset_x >= 0 && offset_z >= 0 && offset_x <= scene_size &&
                    offset_z <= scene_size )
                {
                    heightmap_set(world->heightmap, offset_x, offset_z, level, tile->height);
                }

                if( offset_x >= 0 && offset_z >= 0 && offset_x < scene_size &&
                    offset_z < scene_size )
                {
                    flag_map_set(builder->flag_map, offset_x, offset_z, level, tile->settings);
                }

                if( !(offset_x >= 0 && offset_z >= 0 && offset_x < scene_size &&
                      offset_z < scene_size) )
                {
                    continue;
                }

                if( tile->underlay_id > 0 )
                {
                    struct ToriAuxLibCore_Flotype* flotype =
                        ToriAuxLibCore_FlotypeGet(builder->core, tile->underlay_id - 1);
                    if( flotype )
                    {
                        blendmap_set_underlay_rgb(
                            builder->blendmap,
                            offset_x,
                            offset_z,
                            level,
                            (uint32_t)flotype->rgb_color);
                    }
                }

                int overlay_id = tile->overlay_id - 1;
                int underlay_id = tile->underlay_id - 1;

                if( overlay_id != -1 )
                {
                    struct ToriAuxLibCore_Flotype* flotype =
                        ToriAuxLibCore_FlotypeGet(builder->core, overlay_id);
                    if( flotype )
                    {
                        overlaymap_set_tile_rgb(
                            builder->overlaymap,
                            offset_x,
                            offset_z,
                            level,
                            (uint32_t)flotype->rgb_color);

                        if( flotype->texture != -1 )
                        {
                            int texture_avg_hsl16 = palette_rgb_to_hsl16(flotype->rgb_color);
                            overlaymap_set_tile_texture(
                                builder->overlaymap,
                                offset_x,
                                offset_z,
                                level,
                                (uint8_t)flotype->texture,
                                (uint16_t)texture_avg_hsl16);
                        }

                        if( flotype->secondary_rgb_color > 0 )
                        {
                            overlaymap_set_tile_minimap(
                                builder->overlaymap,
                                offset_x,
                                offset_z,
                                level,
                                (uint32_t)flotype->secondary_rgb_color);
                        }
                    }
                }

                if( underlay_id != -1 || overlay_id != -1 )
                {
                    int shape = 0;
                    int rotation = 0;
                    if( overlay_id != -1 )
                    {
                        shape = tile->shape + 1;
                        rotation = tile->rotation;
                    }

                    terrain_shape_map_set_tile(
                        builder->terrain_shapemap, offset_x, offset_z, level, shape, rotation);
                }
            }
        }
    }
}

static void
world_build_scene_terrain(struct WorldBuilder* builder)
{
    struct World* world = builder->world;
    int scene_size = world->_scene_size;

    blendmap_build(builder->blendmap);
    lightmap_build(builder->lightmap, world->heightmap);

    for( int level = 0; level < WORLD_MAP_TERRAIN_LEVELS; level++ )
    {
        world_apply_shade(builder, level, 0, scene_size, 0, scene_size, 0, scene_size, 0, scene_size);

        for( int z = 1; z < scene_size - 1; z++ )
        {
            for( int x = 1; x < scene_size - 1; x++ )
            {
                struct TerrainShapeMapTile* shape_tile =
                    terrain_shape_map_get_tile(builder->terrain_shapemap, x, z, level);
                if( !shape_tile || !shape_tile->active )
                    continue;

                struct OverlaymapTile* overlay_tile =
                    overlaymap_get_tile(builder->overlaymap, x, z, level);

                int height_sw = heightmap_get(world->heightmap, x, z, level);
                int height_se = heightmap_get(world->heightmap, x + 1, z, level);
                int height_ne = heightmap_get(world->heightmap, x + 1, z + 1, level);
                int height_nw = heightmap_get(world->heightmap, x, z + 1, level);

                int light_sw = lightmap_get(builder->lightmap, x, z, level);
                int light_se = lightmap_get(builder->lightmap, x + 1, z, level);
                int light_ne = lightmap_get(builder->lightmap, x + 1, z + 1, level);
                int light_nw = lightmap_get(builder->lightmap, x, z + 1, level);

                int32_t underlay_hsl = blendmap_get_blended_hsl16(builder->blendmap, x, z, level);
                if( underlay_hsl == BLENDMAP_HSL16_NONE )
                    underlay_hsl = TERRAIN_UNDERLAY_HSL_NONE;

                int overlay_hsl = 0;
                int texture_id = -1;
                if( overlay_tile->texture_id != -1 )
                {
                    texture_id = overlay_tile->texture_id;
                    overlay_hsl = TERRAIN_OVERLAY_HSL_LIGHTNESS_ONLY;
                }
                else if( overlay_tile->rgb_color == 0xFF00FF )
                {
                    overlay_hsl = TERRAIN_OVERLAY_HSL_TRANSPARENT;
                    texture_id = -1;
                }
                else
                {
                    int hsl = palette_rgb_to_hsl16(overlay_tile->rgb_color);
                    overlay_hsl = terrain_adjust_lightness(hsl, 96);
                    texture_id = -1;
                }

                int minimap_foreground_rgb = 0;
                int minimap_background_rgb = 0;

                if( overlay_tile->minimap_rgb_color != UINT32_MAX )
                {
                    minimap_foreground_rgb = (int)(overlay_tile->minimap_rgb_color & 0x00FFFFFFu);
                }
                else if( overlay_hsl > 0 )
                {
                    minimap_foreground_rgb = ToriDraw_Hsl16ToRgb((uint16_t)overlay_hsl);
                }
                else if( overlay_hsl == TERRAIN_OVERLAY_HSL_LIGHTNESS_ONLY )
                {
                    minimap_foreground_rgb = ToriDraw_Hsl16ToRgb(overlay_tile->texture_avg_hsl16);
                }
                else if( overlay_hsl == TERRAIN_OVERLAY_HSL_TRANSPARENT )
                {
                    minimap_foreground_rgb = 0;
                }

                if( underlay_hsl != TERRAIN_UNDERLAY_HSL_NONE )
                    minimap_background_rgb = ToriDraw_Hsl16ToRgb((uint16_t)underlay_hsl);

                if( level == 0 && world->minimap )
                {
                    minimap_set_tile_color(
                        world->minimap, x, z, minimap_foreground_rgb, MINIMAP_FOREGROUND);
                    minimap_set_tile_color(
                        world->minimap, x, z, minimap_background_rgb, MINIMAP_BACKGROUND);
                    minimap_set_tile_shape(
                        world->minimap, x, z, shape_tile->shape, shape_tile->rotation);
                }

                struct ToriDraw_Model* td = world_decode_tile(
                    shape_tile->shape,
                    shape_tile->rotation,
                    texture_id,
                    height_sw,
                    height_se,
                    height_ne,
                    height_nw,
                    light_sw,
                    light_se,
                    light_ne,
                    light_nw,
                    (int)underlay_hsl,
                    overlay_hsl);
                if( !td )
                    continue;

                struct ToriDraw_ModelHandle hnd = {
                    .kind = TORIDRAWMK_MODEL,
                    .u.model.model = td,
                };

                int element_id = ToriDraw_SceneElementAdd(builder->scene);
                if( element_id < 0 )
                {
                    ToriDraw_ModelFree(td);
                    continue;
                }

                ToriDraw_SceneElementSetModel(builder->scene, element_id, hnd);
                ToriDraw_SceneElementSetPosition(
                    builder->scene, element_id, x * WORLD_TILE_SIZE, 0, z * WORLD_TILE_SIZE, 0);

                if( world->terrain_element_ids )
                {
                    int idx = x + z * scene_size + level * scene_size * scene_size;
                    world->terrain_element_ids[idx] = element_id;
                }
            }
        }
    }
}

#endif
