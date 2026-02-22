#ifndef WORLD_TERRAIN_U_C
#define WORLD_TERRAIN_U_C

#include "palette.h"
#include "world.h"

// clang-format off
#include "terrain_decode_tile.u.c"
// clang-format on

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TILE_SIZE 128
#define LIGHT_DIR_X -50
#define LIGHT_DIR_Y -10
#define LIGHT_DIR_Z -50
#define LIGHT_AMBIENT 96
// Over 256, so 768 / 256 = 3, the lightness is divided by 3.
#define LIGHT_ATTENUATION 768
#define HEIGHT_SCALE 65536

#define BLEND_RADIUS 5

static void
lightmap_build(
    struct Lightmap* lightmap,
    struct Heightmap* heightmap)
{
    int magnitude =
        sqrt(LIGHT_DIR_X * LIGHT_DIR_X + LIGHT_DIR_Y * LIGHT_DIR_Y + LIGHT_DIR_Z * LIGHT_DIR_Z);
    int intensity = (magnitude * LIGHT_ATTENUATION) >> 8;

    for( int level = 0; level < lightmap->levels; level++ )
    {
        for( int z = 1; z < lightmap->height - 1; z++ )
        {
            for( int x = 1; x < lightmap->width - 1; x++ )
            {
                // First we need to calculate the normals for each tile.
                // This is typically by doing a cross product on the tangent vectors which can be
                // derived from the differences in height between adjacent tiles. The code below
                // seems to be calculating the normals directly by skipping the cross product.

                int height_delta_x = heightmap_get(heightmap, x + 1, z, level) -
                                     heightmap_get(heightmap, x - 1, z, level);
                int height_delta_z = heightmap_get(heightmap, x, z + 1, level) -
                                     heightmap_get(heightmap, x, z - 1, level);
                // const tileNormalLength =
                //                 Math.sqrt(
                //                     heightDeltaY * heightDeltaY + heightDeltaX * heightDeltaX +
                //                     HEIGHT_SCALE,
                //                 ) | 0;

                //             const normalizedTileNormalX = ((heightDeltaX << 8) /
                //             tileNormalLength) | 0; const normalizedTileNormalY = (HEIGHT_SCALE /
                //             tileNormalLength) | 0; const normalizedTileNormalZ = ((heightDeltaY
                //             << 8) / tileNormalLength) | 0;

                int tile_normal_length = sqrt(
                    height_delta_z * height_delta_z + height_delta_x * height_delta_x +
                    HEIGHT_SCALE);

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

                lightmap_set(lightmap, x, z, level, sunlight);
            }
        }
    }
}

static void
apply_shade(
    struct Lightmap* lightmap,
    struct Shademap2* shademap2_nullable,
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
    if( shademap2_nullable == NULL )
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
            if( shademap2_nullable )
            {
                if( shademap2_in_bounds(shademap2_nullable, x - 1, z, level) )
                    shade_west = shademap2_get(shademap2_nullable, x - 1, z, level);
                if( shademap2_in_bounds(shademap2_nullable, x + 1, z, level) )
                    shade_east = shademap2_get(shademap2_nullable, x + 1, z, level);
                if( shademap2_in_bounds(shademap2_nullable, x, z + 1, level) )
                    shade_north = shademap2_get(shademap2_nullable, x, z + 1, level);
                if( shademap2_in_bounds(shademap2_nullable, x, z - 1, level) )
                    shade_south = shademap2_get(shademap2_nullable, x, z - 1, level);

                int shade_center = shademap2_get(shademap2_nullable, x, z, level);

                shade = shade_center >> 1;
                shade += shade_west >> 2;
                shade += shade_east >> 3;
                shade += shade_north >> 3;
                shade += shade_south >> 2;
            }
            int light = lightmap_get(lightmap, x, z, level);

            int shaded = light - shade;
            if( shaded < 0 )
                shaded = 0;
            lightmap_set(lightmap, x, z, level, shaded);
        }
    }
}

static int
terrain_element_acquire(
    struct World* world,
    int tile_id)
{
    return scene2_element_acquire(
        world->scene2, entity_unified_id(ENTITY_KIND_MAP_BUILD_TILE, tile_id));
}

static void
init_map_build_tile_entity(
    struct MapBuildTileEntity* map_build_tile_entity,
    int entity_id)
{
    memset(map_build_tile_entity, 0, sizeof(struct MapBuildTileEntity));
    map_build_tile_entity->scene_element.element_id = -1;
    map_build_tile_entity->entity_id = entity_id;
}

static inline int
world_tile_entity_idx(
    struct World* world,
    int x,
    int z,
    int level)
{
    assert(x >= 0 && x < world->_scene_size);
    assert(z >= 0 && z < world->_scene_size);
    assert(level >= 0 && level < MAP_TERRAIN_LEVELS);
    return x + z * world->_scene_size + level * world->_scene_size * world->_scene_size;
}

struct MapBuildTileEntity*
world_tile_entity_at(
    struct World* world,
    int x,
    int z,
    int level)
{
    assert(x >= 0 && x < world->_scene_size);
    assert(z >= 0 && z < world->_scene_size);
    assert(level >= 0 && level < MAP_TERRAIN_LEVELS);
    int idx = world_tile_entity_idx(world, x, z, level);
    assert(idx >= 0 && idx < MAX_MAP_BUILD_TILE_ENTITIES);
    return &world->map_build_tile_entities[idx];
}

struct MapBuildTileEntity*
world_tile_entity_at_new(
    struct World* world,
    int x,
    int z,
    int level)
{
    struct MapBuildTileEntity* tile_entity = world_tile_entity_at(world, x, z, level);
    world_cleanup_map_build_tile_entity(world, tile_entity->entity_id);
    tile_entity->entity_id = world_tile_entity_idx(world, x, z, level);
    return tile_entity;
}

static void
build_scene_terrain(struct World* world)
{
    struct DashModel* model = NULL;
    struct Scene2Element* scene_element = NULL;

    int scene_size = world->_scene_size;
    struct TerrainShapeMapTile* shape_tile = NULL;
    struct OverlaymapTile* overlay_tile = NULL;
    struct MapBuildTileEntity* tile_entity = NULL;

    /**
     * Scene Tiles
     *
     */
    for( int level = 0; level < MAP_TERRAIN_LEVELS; level++ )
    {
        apply_shade(
            world->lightmap,
            world->shademap,
            level,
            0,
            scene_size,
            0,
            scene_size,
            0,
            scene_size,
            0,
            scene_size);

        for( int z = 1; z < scene_size - 1; z++ )
        {
            for( int x = 1; x < scene_size - 1; x++ )
            {
                tile_entity = world_tile_entity_at_new(world, x, z, level);

                shape_tile = terrain_shape_map_get_tile(world->terrain_shapemap, x, z, level);
                if( !shape_tile->active )
                    continue;

                overlay_tile = overlaymap_get_tile(world->overlaymap, x, z, level);

                int height_sw = heightmap_get(world->heightmap, x, z, level);
                int height_se = heightmap_get(world->heightmap, x + 1, z, level);
                int height_ne = heightmap_get(world->heightmap, x + 1, z + 1, level);
                int height_nw = heightmap_get(world->heightmap, x, z + 1, level);

                int light_sw = lightmap_get(world->lightmap, x, z, level);
                int light_se = lightmap_get(world->lightmap, x + 1, z, level);
                int light_ne = lightmap_get(world->lightmap, x + 1, z + 1, level);
                int light_nw = lightmap_get(world->lightmap, x, z + 1, level);

                int underlay_hsl = -1;
                int overlay_hsl = 0;
                int minimap_foreground_rgb = 0;
                int minimap_background_rgb = 0;

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
                // struct FlotypeEntry* flotype_entry = NULL;
                // flotype_entry = (struct FlotypeEntry*)dashmap_search(
                //     config_underlay_map, &underlay_id, DASHMAP_FIND);
                // assert(flotype_entry != NULL);
                // underlay = flotype_entry->flotype;
                // assert(underlay != NULL);

                // underlay = (struct CacheConfigUnderlay*)configmap_get(
                //     config_underlay_map, underlay_id - 1);
                // assert(underlay != NULL);

                underlay_hsl = blendmap_get_blended_hsl16(world->blendmap, x, z, level);
                if( underlay_hsl == BLENDMAP_HSL16_NONE )
                    underlay_hsl = UNDERLAY_HSL_NONE;

                int shape = shape_tile->shape;
                int rotation = shape_tile->rotation;
                int texture_id = -1;

                if( overlay_tile->texture_id != -1 )
                {
                    texture_id = overlay_tile->texture_id;
                    overlay_hsl = OVERLAY_HSL_LIGHTNESS_ONLY;
                }
                else if( overlay_tile->rgb_color == 0xFF00FF )
                {
                    overlay_hsl = OVERLAY_HSL_TRANSPARENT;
                    texture_id = -1;
                }
                else
                {
                    int hsl = palette_rgb_to_hsl16(overlay_tile->rgb_color);
                    overlay_hsl = adjust_lightness(hsl, 96);
                    texture_id = -1;
                }

                tile_entity->scene_element.element_id =
                    terrain_element_acquire(world, tile_entity->entity_id);
                assert(tile_entity->scene_element.element_id != 0);
                scene_element =
                    scene2_element_at(world->scene2, tile_entity->scene_element.element_id);

                model = decode_tile(
                    shape,
                    rotation,
                    texture_id,
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

                scene_element->dash_model = model;
                scene_element->dash_position = dashposition_new();
                scene_element->dash_position->x = x * TILE_SIZE;
                scene_element->dash_position->z = z * TILE_SIZE;
                // The height is built into the model.
                scene_element->dash_position->y = 0;

                // Minimap
                if( overlay_tile->minimap_rgb_color > 0 )
                {
                    minimap_foreground_rgb = overlay_tile->minimap_rgb_color;
                }
                else if( overlay_hsl > 0 )
                {
                    minimap_foreground_rgb = dash_hsl16_to_rgb(overlay_hsl);
                }
                else if( overlay_hsl == OVERLAY_HSL_LIGHTNESS_ONLY )
                {
                    assert(overlay_tile->texture_id != -1);
                    minimap_foreground_rgb = dash_hsl16_to_rgb(overlay_tile->texture_avg_hsl16);
                }
                else if( overlay_hsl == OVERLAY_HSL_TRANSPARENT )
                {
                    minimap_foreground_rgb = 0;
                }
                else
                {
                    assert(false && "Unexpected overlay hsl");
                }

                if( underlay_hsl != -1 )
                    minimap_background_rgb = dash_hsl16_to_rgb(underlay_hsl);

                minimap_set_tile_color(
                    world->minimap, x, z, level, minimap_foreground_rgb, MINIMAP_FOREGROUND);
                minimap_set_tile_color(
                    world->minimap, x, z, level, minimap_background_rgb, MINIMAP_BACKGROUND);
                minimap_set_tile_shape(world->minimap, x, z, level, shape, rotation);
            }
        }
    }
}

#endif