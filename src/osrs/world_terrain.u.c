#ifndef WORLD_TERRAIN_U_C
#define WORLD_TERRAIN_U_C

#include "palette.h"
#include "world.h"

// clang-format off
#include "terrain_decode_tile.u.c"
// clang-format on

#include <assert.h>
#include <math.h>
#include <stdint.h>
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
    return scene2_element_acquire_fast(
        world->scene2, (int)entity_unified_id(ENTITY_KIND_MAP_BUILD_TILE, tile_id));
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
    return world_map_build_tile_entity(world, idx);
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

static struct MapBuildTileEntity*
world_tile_entity_at_new_by_id(
    struct World* world,
    int entity_id)
{
    assert(entity_id >= 0 && entity_id < MAX_MAP_BUILD_TILE_ENTITIES);
    struct MapBuildTileEntity* tile_entity = world_map_build_tile_entity(world, entity_id);
    world_cleanup_map_build_tile_entity(world, tile_entity->entity_id);
    tile_entity->entity_id = entity_id;
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

                scene2_element_set_dash_position_ptr(scene_element, dashposition_new());
                scene2_element_set_dash_model(world->scene2, scene_element, model);
                struct DashPosition* dp = scene2_element_dash_position(scene_element);
                dp->x = x * TILE_SIZE;
                dp->z = z * TILE_SIZE;
                // The height is built into the model.
                dp->y = 0;

                /* Explicit overlay minimap color from flo config; UINT32_MAX = unset (init_tile).
                 */
                if( overlay_tile->minimap_rgb_color != UINT32_MAX )
                {
                    minimap_foreground_rgb = (int)(overlay_tile->minimap_rgb_color & 0x00FFFFFFu);
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
                    world->minimap, x, z, minimap_foreground_rgb, MINIMAP_FOREGROUND);
                minimap_set_tile_color(
                    world->minimap, x, z, minimap_background_rgb, MINIMAP_BACKGROUND);
                minimap_set_tile_shape(world->minimap, x, z, shape, rotation);
            }
        }
    }
}

static void
build_scene_terrain_va(struct World* world)
{
    int scene_size = world->_scene_size;
    int grid_side = scene_size + 1;

    /* Maximum vertex and face counts per level (pessimistic upper bounds). */
    int max_verts = grid_side * grid_side                      /* all corners */
                    + (scene_size - 2) * (scene_size - 2) * 2; /* non-corner verts per tile */
    int max_faces = (scene_size - 2) * (scene_size - 2) * 6;   /* 6 faces per tile */

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

        /* Buildtime corner dedup grid: stores global vertex index for each (gx, gz) corner,
         * or -1 if not yet added.  Only corner vertex types (1/3/5/7) hit the 128 boundary. */
        int* corner_grid = (int*)malloc((size_t)grid_side * grid_side * sizeof(int));
        for( int i = 0; i < grid_side * grid_side; i++ )
            corner_grid[i] = -1;

        /* Vertex position arrays (VA owns them). */
        vertexint_t* va_x = (vertexint_t*)malloc((size_t)max_verts * sizeof(vertexint_t));
        vertexint_t* va_y = (vertexint_t*)malloc((size_t)max_verts * sizeof(vertexint_t));
        vertexint_t* va_z = (vertexint_t*)malloc((size_t)max_verts * sizeof(vertexint_t));

        /* Face arrays (model owns them). */
        hsl16_t* fc_a = (hsl16_t*)malloc((size_t)max_faces * sizeof(hsl16_t));
        hsl16_t* fc_b = (hsl16_t*)malloc((size_t)max_faces * sizeof(hsl16_t));
        hsl16_t* fc_c = (hsl16_t*)malloc((size_t)max_faces * sizeof(hsl16_t));
        faceint_t* fi_a = (faceint_t*)malloc((size_t)max_faces * sizeof(faceint_t));
        faceint_t* fi_b = (faceint_t*)malloc((size_t)max_faces * sizeof(faceint_t));
        faceint_t* fi_c = (faceint_t*)malloc((size_t)max_faces * sizeof(faceint_t));
        /* face_textures allocated on demand if any textured tile appears */
        faceint_t* face_textures = NULL;
        bool has_textures = false;

        int vertex_count = 0;
        int face_count = 0;

        for( int z = 1; z < scene_size - 1; z++ )
        {
            for( int x = 1; x < scene_size - 1; x++ )
            {
                struct TerrainShapeMapTile* shape_tile =
                    terrain_shape_map_get_tile(world->terrain_shapemap, x, z, level);
                if( !shape_tile->active )
                    continue;

                struct OverlaymapTile* overlay_tile =
                    overlaymap_get_tile(world->overlaymap, x, z, level);

                int height_sw = heightmap_get(world->heightmap, x, z, level);
                int height_se = heightmap_get(world->heightmap, x + 1, z, level);
                int height_ne = heightmap_get(world->heightmap, x + 1, z + 1, level);
                int height_nw = heightmap_get(world->heightmap, x, z + 1, level);

                int light_sw = lightmap_get(world->lightmap, x, z, level);
                int light_se = lightmap_get(world->lightmap, x + 1, z, level);
                int light_ne = lightmap_get(world->lightmap, x + 1, z + 1, level);
                int light_nw = lightmap_get(world->lightmap, x, z + 1, level);

                int underlay_hsl = blendmap_get_blended_hsl16(world->blendmap, x, z, level);
                if( underlay_hsl == BLENDMAP_HSL16_NONE )
                    underlay_hsl = UNDERLAY_HSL_NONE;

                int shape = shape_tile->shape;
                int rotation = shape_tile->rotation;
                int texture_id = -1;
                int overlay_hsl = 0;

                if( overlay_tile->texture_id != -1 )
                {
                    texture_id = overlay_tile->texture_id;
                    overlay_hsl = OVERLAY_HSL_LIGHTNESS_ONLY;
                }
                else if( overlay_tile->rgb_color == 0xFF00FF )
                {
                    overlay_hsl = OVERLAY_HSL_TRANSPARENT;
                }
                else
                {
                    int hsl = palette_rgb_to_hsl16(overlay_tile->rgb_color);
                    overlay_hsl = adjust_lightness(hsl, 96);
                }

                /* Pre-compute per-corner lit colors (same logic as decode_tile). */
                int underlay_hsl_sw = multiply_lightness(underlay_hsl, light_sw);
                int underlay_hsl_se = multiply_lightness(underlay_hsl, light_se);
                int underlay_hsl_ne = multiply_lightness(underlay_hsl, light_ne);
                int underlay_hsl_nw = multiply_lightness(underlay_hsl, light_nw);

                int overlay_hsl_sw = adjust_lightness(overlay_hsl, light_sw);
                int overlay_hsl_se = adjust_lightness(overlay_hsl, light_se);
                int overlay_hsl_ne = adjust_lightness(overlay_hsl, light_ne);
                int overlay_hsl_nw = adjust_lightness(overlay_hsl, light_nw);

                /* -- Vertex pass -- */
                int* vertex_indices = g_tile_shape_vertex_types[shape];
                int vcount = g_tile_shape_vertex_types_lengths[shape];

                int tile_x = x * TILE_SIZE;
                int tile_z = z * TILE_SIZE;

                /* Per-tile local arrays: lit colors and global index mapping. */
                int underlay_colors[6];
                int overlay_colors[6];
                int local_to_global[6];

                for( int vi = 0; vi < vcount; vi++ )
                {
                    int vtype = vertex_indices[vi];

                    /* Apply rotation (same as decode_tile). */
                    if( (vtype & 1) == 0 && vtype <= 8 )
                        vtype = ((vtype - rotation - rotation - 1) & 7) + 1;
                    if( vtype > 8 && vtype <= 12 )
                        vtype = ((vtype - 9 - rotation) & 3) + 9;
                    if( vtype > 12 && vtype <= 16 )
                        vtype = ((vtype - 13 - rotation) & 3) + 13;

                    int vx, vy, vz;
                    int uc, oc;

                    switch( vtype )
                    {
                    case 1: /* SW */
                        vx = tile_x;
                        vy = height_sw;
                        vz = tile_z;
                        uc = underlay_hsl_sw;
                        oc = overlay_hsl_sw;
                        break;
                    case 2: /* S-edge mid */
                        vx = tile_x + TILE_SIZE / 2;
                        vy = (height_sw + height_se) >> 1;
                        vz = tile_z;
                        uc = mix_hsl(underlay_hsl_se, underlay_hsl_sw);
                        oc = (overlay_hsl_se + overlay_hsl_sw) >> 1;
                        break;
                    case 3: /* SE */
                        vx = tile_x + TILE_SIZE;
                        vy = height_se;
                        vz = tile_z;
                        uc = underlay_hsl_se;
                        oc = overlay_hsl_se;
                        break;
                    case 4: /* E-edge mid */
                        vx = tile_x + TILE_SIZE;
                        vy = (height_se + height_ne) >> 1;
                        vz = tile_z + TILE_SIZE / 2;
                        uc = mix_hsl(underlay_hsl_se, underlay_hsl_ne);
                        oc = (overlay_hsl_ne + overlay_hsl_se) >> 1;
                        break;
                    case 5: /* NE */
                        vx = tile_x + TILE_SIZE;
                        vy = height_ne;
                        vz = tile_z + TILE_SIZE;
                        uc = underlay_hsl_ne;
                        oc = overlay_hsl_ne;
                        break;
                    case 6: /* N-edge mid */
                        vx = tile_x + TILE_SIZE / 2;
                        vy = (height_ne + height_nw) >> 1;
                        vz = tile_z + TILE_SIZE;
                        uc = mix_hsl(underlay_hsl_ne, underlay_hsl_nw);
                        oc = (overlay_hsl_ne + overlay_hsl_nw) >> 1;
                        break;
                    case 7: /* NW */
                        vx = tile_x;
                        vy = height_nw;
                        vz = tile_z + TILE_SIZE;
                        uc = underlay_hsl_nw;
                        oc = overlay_hsl_nw;
                        break;
                    case 8: /* W-edge mid */
                        vx = tile_x;
                        vy = (height_nw + height_sw) >> 1;
                        vz = tile_z + TILE_SIZE / 2;
                        uc = mix_hsl(underlay_hsl_nw, underlay_hsl_sw);
                        oc = (overlay_hsl_nw + overlay_hsl_sw) >> 1;
                        break;
                    case 9:
                        vx = tile_x + TILE_SIZE / 2;
                        vy = (height_sw + height_se) >> 1;
                        vz = tile_z + TILE_SIZE / 4;
                        uc = mix_hsl(underlay_hsl_sw, underlay_hsl_se);
                        oc = (overlay_hsl_sw + overlay_hsl_se) >> 1;
                        break;
                    case 10:
                        vx = tile_x + TILE_SIZE * 3 / 4;
                        vy = (height_se + height_ne) >> 1;
                        vz = tile_z + TILE_SIZE / 2;
                        uc = mix_hsl(underlay_hsl_se, underlay_hsl_ne);
                        oc = (overlay_hsl_se + overlay_hsl_ne) >> 1;
                        break;
                    case 11:
                        vx = tile_x + TILE_SIZE / 2;
                        vy = (height_ne + height_nw) >> 1;
                        vz = tile_z + TILE_SIZE * 3 / 4;
                        uc = mix_hsl(underlay_hsl_ne, underlay_hsl_nw);
                        oc = (overlay_hsl_ne + overlay_hsl_nw) >> 1;
                        break;
                    case 12:
                        vx = tile_x + TILE_SIZE / 4;
                        vy = (height_nw + height_sw) >> 1;
                        vz = tile_z + TILE_SIZE / 2;
                        uc = mix_hsl(underlay_hsl_nw, underlay_hsl_sw);
                        oc = (overlay_hsl_nw + overlay_hsl_sw) >> 1;
                        break;
                    case 13:
                        vx = tile_x + TILE_SIZE / 4;
                        vy = height_sw;
                        vz = tile_z + TILE_SIZE / 4;
                        uc = underlay_hsl_sw;
                        oc = overlay_hsl_sw;
                        break;
                    case 14:
                        vx = tile_x + TILE_SIZE * 3 / 4;
                        vy = height_se;
                        vz = tile_z + TILE_SIZE / 4;
                        uc = underlay_hsl_se;
                        oc = overlay_hsl_se;
                        break;
                    case 15:
                        vx = tile_x + TILE_SIZE * 3 / 4;
                        vy = height_ne;
                        vz = tile_z + TILE_SIZE * 3 / 4;
                        uc = underlay_hsl_ne;
                        oc = overlay_hsl_ne;
                        break;
                    default: /* 16: NW quarter center */
                        vx = tile_x + TILE_SIZE / 4;
                        vy = height_nw;
                        vz = tile_z + TILE_SIZE * 3 / 4;
                        uc = underlay_hsl_nw;
                        oc = overlay_hsl_nw;
                        break;
                    }

                    underlay_colors[vi] = uc;
                    overlay_colors[vi] = oc;

                    /* Corner types 1/3/5/7: deduplicate via the corner grid. */
                    int global_idx;
                    if( vtype == 1 || vtype == 3 || vtype == 5 || vtype == 7 )
                    {
                        /* Grid coords: vtype 1=SW(x,z) 3=SE(x+1,z) 5=NE(x+1,z+1) 7=NW(x,z+1) */
                        int gx = (vtype == 1 || vtype == 7) ? x : x + 1;
                        int gz = (vtype == 5 || vtype == 7) ? z + 1 : z;
                        int grid_idx = gz * grid_side + gx;
                        if( corner_grid[grid_idx] != -1 )
                        {
                            global_idx = corner_grid[grid_idx];
                        }
                        else
                        {
                            assert(vertex_count < max_verts);
                            global_idx = vertex_count;
                            va_x[vertex_count] = (vertexint_t)vx;
                            va_y[vertex_count] = (vertexint_t)vy;
                            va_z[vertex_count] = (vertexint_t)vz;
                            vertex_count++;
                            corner_grid[grid_idx] = global_idx;
                        }
                    }
                    else
                    {
                        assert(vertex_count < max_verts);
                        global_idx = vertex_count;
                        va_x[vertex_count] = (vertexint_t)vx;
                        va_y[vertex_count] = (vertexint_t)vy;
                        va_z[vertex_count] = (vertexint_t)vz;
                        vertex_count++;
                    }

                    local_to_global[vi] = global_idx;
                }

                /* -- Face pass -- */
                int* face_indices_raw = g_tile_shape_faces[shape];
                int fcount = g_tile_shape_face_counts[shape] / 4;

                for( int fi = 0; fi < fcount; fi++ )
                {
                    bool is_overlay = face_indices_raw[fi * 4] == 1;
                    int la = face_indices_raw[fi * 4 + 1];
                    int lb = face_indices_raw[fi * 4 + 2];
                    int lc = face_indices_raw[fi * 4 + 3];

                    if( la < 4 )
                        la = (la - rotation) & 3;
                    if( lb < 4 )
                        lb = (lb - rotation) & 3;
                    if( lc < 4 )
                        lc = (lc - rotation) & 3;

                    int color_a, color_b, color_c;
                    int face_tex = -1;

                    if( is_overlay )
                    {
                        color_a = overlay_colors[la];
                        color_b = overlay_colors[lb];
                        color_c = overlay_colors[lc];
                        face_tex = texture_id;
                    }
                    else
                    {
                        color_a = underlay_colors[la];
                        color_b = underlay_colors[lb];
                        color_c = underlay_colors[lc];
                    }

                    /* Hide faces whose colors are invalid (transparent / no underlay). */
                    hsl16_t hsl_c;
                    if( color_a == INVALID_HSL_COLOR && face_tex == -1 )
                        hsl_c = DASHHSL16_HIDDEN;
                    else if(
                        color_a == INVALID_HSL_COLOR || color_b == INVALID_HSL_COLOR ||
                        color_c == INVALID_HSL_COLOR )
                        hsl_c = DASHHSL16_HIDDEN;
                    else
                        hsl_c = (hsl16_t)(unsigned)(color_c & 0xffff);

                    assert(face_count < max_faces);
                    fc_a[face_count] = (hsl16_t)(unsigned)(color_a & 0xffff);
                    fc_b[face_count] = (hsl16_t)(unsigned)(color_b & 0xffff);
                    fc_c[face_count] = hsl_c;
                    fi_a[face_count] = (faceint_t)local_to_global[la];
                    fi_b[face_count] = (faceint_t)local_to_global[lb];
                    fi_c[face_count] = (faceint_t)local_to_global[lc];

                    if( face_tex != -1 )
                    {
                        if( !face_textures )
                        {
                            face_textures =
                                (faceint_t*)malloc((size_t)max_faces * sizeof(faceint_t));
                            for( int k = 0; k < face_count; k++ )
                                face_textures[k] = -1;
                            has_textures = true;
                        }
                        face_textures[face_count] = (faceint_t)face_tex;
                    }
                    else if( face_textures )
                    {
                        face_textures[face_count] = -1;
                    }

                    face_count++;
                }

                /* Minimap (same as build_scene_terrain). */
                int minimap_foreground_rgb = 0;
                int minimap_background_rgb = 0;

                if( overlay_tile->minimap_rgb_color != UINT32_MAX )
                    minimap_foreground_rgb = (int)(overlay_tile->minimap_rgb_color & 0x00FFFFFFu);
                else if( overlay_hsl > 0 )
                    minimap_foreground_rgb = dash_hsl16_to_rgb(overlay_hsl);
                else if( overlay_hsl == OVERLAY_HSL_LIGHTNESS_ONLY )
                    minimap_foreground_rgb = dash_hsl16_to_rgb(overlay_tile->texture_avg_hsl16);
                else if( overlay_hsl == OVERLAY_HSL_TRANSPARENT )
                    minimap_foreground_rgb = 0;

                if( underlay_hsl != UNDERLAY_HSL_NONE )
                    minimap_background_rgb = dash_hsl16_to_rgb(underlay_hsl);

                minimap_set_tile_color(
                    world->minimap, x, z, minimap_foreground_rgb, MINIMAP_FOREGROUND);
                minimap_set_tile_color(
                    world->minimap, x, z, minimap_background_rgb, MINIMAP_BACKGROUND);
                minimap_set_tile_shape(world->minimap, x, z, shape, rotation);
            }
        }

        free(corner_grid);

        /* Trim to actual sizes. */
        va_x = (vertexint_t*)realloc(va_x, (size_t)vertex_count * sizeof(vertexint_t));
        va_y = (vertexint_t*)realloc(va_y, (size_t)vertex_count * sizeof(vertexint_t));
        va_z = (vertexint_t*)realloc(va_z, (size_t)vertex_count * sizeof(vertexint_t));

        fc_a = (hsl16_t*)realloc(fc_a, (size_t)face_count * sizeof(hsl16_t));
        fc_b = (hsl16_t*)realloc(fc_b, (size_t)face_count * sizeof(hsl16_t));
        fc_c = (hsl16_t*)realloc(fc_c, (size_t)face_count * sizeof(hsl16_t));
        fi_a = (faceint_t*)realloc(fi_a, (size_t)face_count * sizeof(faceint_t));
        fi_b = (faceint_t*)realloc(fi_b, (size_t)face_count * sizeof(faceint_t));
        fi_c = (faceint_t*)realloc(fi_c, (size_t)face_count * sizeof(faceint_t));
        if( face_textures )
            face_textures =
                (faceint_t*)realloc(face_textures, (size_t)face_count * sizeof(faceint_t));

        /* Build the vertex array (vertices only; face data goes on the model). */
        struct DashVertexArray* va =
            (struct DashVertexArray*)malloc(sizeof(struct DashVertexArray));
        va->vertex_count = vertex_count;
        va->vertices_x = va_x;
        va->vertices_y = va_y;
        va->vertices_z = va_z;

        /* Free previous VA for this level (rebuild path). */
        if( world->terrain_va[level] )
            dashvertexarray_free(world->terrain_va[level]);
        world->terrain_va[level] = va;

        /* Build and register the VA model on scene2. */
        struct DashModel* model = dashmodel_va_new(va);
        dashmodel_va_set_face_data(
            model,
            face_count,
            fc_a,
            fc_b,
            fc_c,
            fi_a,
            fi_b,
            fi_c,
            has_textures ? face_textures : NULL);
        if( !has_textures )
            free(face_textures);
        face_textures = NULL;

        dashmodel_set_has_textures(model, has_textures);
        has_textures = false;
        dashmodel_set_bounds_cylinder(model);
        dashmodel_set_loaded(model, true);

        int entity_id = world_terrain_va_tile_entity_id(level);
        struct MapBuildTileEntity* tile_entity = world_tile_entity_at_new_by_id(world, entity_id);

        tile_entity->scene_element.element_id = terrain_element_acquire(world, entity_id);
        struct Scene2Element* scene_element =
            scene2_element_at(world->scene2, tile_entity->scene_element.element_id);
        scene2_element_set_dash_position_ptr(scene_element, dashposition_new());
        scene2_element_set_dash_model(world->scene2, scene_element, model);
        struct DashPosition* dp = scene2_element_dash_position(scene_element);
        dp->x = 0;
        dp->y = 0;
        dp->z = 0;
    }
}

#endif