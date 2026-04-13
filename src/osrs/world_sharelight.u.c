#ifndef WORLD_SHARELIGHT_U_C
#define WORLD_SHARELIGHT_U_C

#include "world.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>

struct TileCoord
{
    int x;
    int z;
    int level;
};

/**
 * @brief TODO: IDK how this works
 *
 * @param out
 * @param out_size
 * @param scenery
 * @param tile_x
 * @param tile_z
 * @param tile_level
 * @param tile_size_x
 * @param tile_size_z
 * @return int
 */
static int
gather_adjacent_tiles(
    int world_width,
    int world_height,
    struct TileCoord* out,
    int out_size,
    int tile_x,
    int tile_z,
    int tile_level,
    int element_size_x,
    int element_size_z)
{
    int min_tile_x = tile_x;
    int max_tile_x = tile_x + (element_size_x);
    int min_tile_z = tile_z - 1;
    int max_tile_z = tile_z + (element_size_z);

    bool huh = false;

    int count = 0;

    for( int level = tile_level; level <= tile_level + 1; level++ )
    {
        for( int x = min_tile_x; x <= max_tile_x; x++ )
        {
            for( int z = min_tile_z; z <= max_tile_z; z++ )
            {
                // if( (x == tile_x && z == tile_z && level == tile_level) )
                //     continue;
                if( x < 0 || z < 0 || x >= world_width || z >= world_height || level < 0 ||
                    level >= MAP_TERRAIN_LEVELS )
                    continue;
                if( (!huh && x < max_tile_x && z < max_tile_z && (z >= tile_z || x >= tile_x)) )
                    continue;

                assert(count < out_size);
                out[count++] = (struct TileCoord){ .x = x, .z = z, .level = level };
            }
        }

        min_tile_x -= 1;
        huh = true;
    }

    return count;
}

static int g_merge_index = 0;
static int g_vertex_a_merge_index[10000] = { 0 };
static int g_vertex_b_merge_index[10000] = { 0 };

static void
merge_normals(
    struct DashModel* model,
    struct LightingNormal* vertex_normals,
    struct LightingNormal* lighting_vertex_normals,
    struct DashModel* other_model,
    struct LightingNormal* other_vertex_normals,
    struct LightingNormal* other_lighting_vertex_normals,
    int check_offset_x,
    int check_offset_y,
    int check_offset_z)
{
    g_merge_index++;

    struct LightingNormal* model_a_normal = NULL;
    struct LightingNormal* model_b_normal = NULL;
    struct LightingNormal* model_a_lighting_normal = NULL;
    struct LightingNormal* model_b_lighting_normal = NULL;
    int x, y, z;
    int other_x, other_y, other_z;

    int merged_vertex_count = 0;

    int model_vc = dashmodel_vertex_count(model);
    vertexint_t* model_vx = dashmodel_vertices_x(model);
    vertexint_t* model_vy = dashmodel_vertices_y(model);
    vertexint_t* model_vz = dashmodel_vertices_z(model);

    int other_vc = dashmodel_vertex_count(other_model);
    vertexint_t* other_vx = dashmodel_vertices_x(other_model);
    vertexint_t* other_vy = dashmodel_vertices_y(other_model);
    vertexint_t* other_vz = dashmodel_vertices_z(other_model);

    for( int vertex = 0; vertex < model_vc; vertex++ )
    {
        x = model_vx[vertex] - check_offset_x;
        y = model_vy[vertex] - check_offset_y;
        z = model_vz[vertex] - check_offset_z;

        model_a_normal = &vertex_normals[vertex];
        model_a_lighting_normal = &lighting_vertex_normals[vertex];

        for( int other_vertex = 0; other_vertex < other_vc; other_vertex++ )
        {
            other_x = other_vx[other_vertex];
            other_y = other_vy[other_vertex];
            other_z = other_vz[other_vertex];

            model_b_normal = &other_vertex_normals[other_vertex];
            model_b_lighting_normal = &other_lighting_vertex_normals[other_vertex];

            if( x == other_x && y == other_y && z == other_z && model_b_normal->face_count > 0 &&
                model_a_normal->face_count > 0 )
            {
                model_a_lighting_normal->x += model_b_normal->x;
                model_a_lighting_normal->y += model_b_normal->y;
                model_a_lighting_normal->z += model_b_normal->z;
                model_a_lighting_normal->face_count += model_b_normal->face_count;
                model_a_lighting_normal->merged++;

                model_b_lighting_normal->x += model_a_normal->x;
                model_b_lighting_normal->y += model_a_normal->y;
                model_b_lighting_normal->z += model_a_normal->z;
                model_b_lighting_normal->face_count += model_a_normal->face_count;
                model_b_lighting_normal->merged++;

                merged_vertex_count++;

                g_vertex_a_merge_index[vertex] = g_merge_index;
                g_vertex_b_merge_index[other_vertex] = g_merge_index;
            }
        }
    }

    /**
     * Normals that are merged are assumed to be abutting each other and their faces not visible.
     * Hide them.
     */

    // TODO: This isn't allow for locs. Only ground decor.
    if( merged_vertex_count < 3 )
        return;

    // Can't have two faces with the same 3 points, so only need to check two.
    int m_fc = dashmodel_face_count(model);
    faceint_t* m_fa = dashmodel_face_indices_a(model);
    faceint_t* m_fb = dashmodel_face_indices_b(model);
    faceint_t* m_fci = dashmodel_face_indices_c(model);
    for( int face = 0; face < m_fc; face++ )
    {
        // If all three vertices of the face are merged, hide the face.
        if( g_vertex_a_merge_index[m_fa[face]] == g_merge_index &&
            g_vertex_a_merge_index[m_fb[face]] == g_merge_index &&
            g_vertex_a_merge_index[m_fci[face]] == g_merge_index )
        {
            int* infos = dashmodel_face_infos_ensure_zero(model);
            if( infos )
                infos[face] = 2;
        }
    }
    int o_fc = dashmodel_face_count(other_model);
    faceint_t* o_fa = dashmodel_face_indices_a(other_model);
    faceint_t* o_fb = dashmodel_face_indices_b(other_model);
    faceint_t* o_fci = dashmodel_face_indices_c(other_model);
    for( int face = 0; face < o_fc; face++ )
    {
        // If all three vertices of the face are merged, hide the face.
        if( g_vertex_b_merge_index[o_fa[face]] == g_merge_index &&
            g_vertex_b_merge_index[o_fb[face]] == g_merge_index &&
            g_vertex_b_merge_index[o_fci[face]] == g_merge_index )
        {
            int* infos = dashmodel_face_infos_ensure_zero(other_model);
            if( infos )
                infos[face] = 2;
        }
    }
}

#define ADJACENT_TILES_COUNT 48

static void
sharelight_build(struct World* world)
{
    struct TileCoord adjacent_tiles[ADJACENT_TILES_COUNT];
    struct SharelightMapTile* map_tile = NULL;
    struct SharelightMapTile* adjacent_map_tile = NULL;
    struct SharelightMapElement* map_element = NULL;
    struct SharelightMapElement* adjacent_map_element = NULL;

    struct Scene2Element* scene_element = NULL;
    struct Scene2Element* adjacent_scene_element = NULL;

    for( int sx = 0; sx < world->sharelight_map->width; sx++ )
    {
        for( int sz = 0; sz < world->sharelight_map->height; sz++ )
        {
            for( int slevel = 0; slevel < MAP_TERRAIN_LEVELS; slevel++ )
            {
                map_tile = sharelight_map_tile_at(world->sharelight_map, sx, sz, slevel);
                assert(map_tile && "Sharelight map tile must be valid");

                for( int32_t pi = map_tile->sharelight_head; pi != -1;
                     pi = world->sharelight_map->pool[pi].next )
                {
                    map_element = &world->sharelight_map->pool[pi].element;

                    scene_element = scene2_element_at(world->scene2, map_element->element_idx);
                    if( !scene_element || !scene2_element_dash_model(scene_element) )
                        continue;

                    int adjacent_tiles_count = gather_adjacent_tiles(
                        world->sharelight_map->width,
                        world->sharelight_map->height,
                        adjacent_tiles,
                        ADJACENT_TILES_COUNT,
                        sx,
                        sz,
                        slevel,
                        map_element->size_x,
                        map_element->size_z);

                    for( int j = 0; j < adjacent_tiles_count; j++ )
                    {
                        struct TileCoord adjacent_tile_coord = adjacent_tiles[j];
                        adjacent_map_tile = sharelight_map_tile_at(
                            world->sharelight_map,
                            adjacent_tile_coord.x,
                            adjacent_tile_coord.z,
                            adjacent_tile_coord.level);
                        for( int32_t kidx = adjacent_map_tile->sharelight_head; kidx != -1;
                             kidx = world->sharelight_map->pool[kidx].next )
                        {
                            adjacent_map_element = &world->sharelight_map->pool[kidx].element;
                            if( adjacent_map_element->element_idx == map_element->element_idx )
                                continue;

                            adjacent_scene_element =
                                scene2_element_at(world->scene2, adjacent_map_element->element_idx);
                            if( !adjacent_scene_element ||
                                !scene2_element_dash_model(adjacent_scene_element) )
                                continue;

                            int check_offset_x =
                                (adjacent_tile_coord.x - sx) * 128 +
                                (adjacent_map_element->size_x - map_element->size_x) * 64;

                            int check_offset_z =
                                (adjacent_tile_coord.z - sz) * 128 +
                                (adjacent_map_element->size_z - map_element->size_z) * 64;

                            int height_center =
                                heightmap_get_center(world->heightmap, sx, sz, slevel);
                            int adjacent_height_center = heightmap_get_center(
                                world->heightmap,
                                adjacent_tile_coord.x,
                                adjacent_tile_coord.z,
                                adjacent_tile_coord.level);
                            int check_offset_height = adjacent_height_center - height_center;

                            if( !dashmodel_is_lightable(scene2_element_dash_model(adjacent_scene_element)) ||
                                !dashmodel_is_lightable(scene2_element_dash_model(scene_element)) )
                                continue;

                            merge_normals(
                                scene2_element_dash_model(scene_element),
                                dashmodel_normals(scene2_element_dash_model(scene_element))
                                    ->lighting_vertex_normals,
                                dashmodel_merged_normals(scene2_element_dash_model(scene_element))
                                    ->lighting_vertex_normals,
                                scene2_element_dash_model(adjacent_scene_element),
                                dashmodel_normals(scene2_element_dash_model(adjacent_scene_element))
                                    ->lighting_vertex_normals,
                                dashmodel_merged_normals(scene2_element_dash_model(adjacent_scene_element))
                                    ->lighting_vertex_normals,
                                check_offset_x,
                                check_offset_height,
                                check_offset_z);
                        }
                    }
                }
            }
        }
    }

    for( int sx = 0; sx < world->sharelight_map->width; sx++ )
    {
        for( int sz = 0; sz < world->sharelight_map->height; sz++ )
        {
            for( int slevel = 0; slevel < MAP_TERRAIN_LEVELS; slevel++ )
            {
                map_tile = sharelight_map_tile_at(world->sharelight_map, sx, sz, slevel);

                for( int32_t pi = map_tile->sharelight_head; pi != -1;
                     pi = world->sharelight_map->pool[pi].next )
                {
                    map_element = &world->sharelight_map->pool[pi].element;

                    scene_element = scene2_element_at(world->scene2, map_element->element_idx);
                    if( !scene_element || !scene2_element_dash_model(scene_element) )
                        continue;

                    int light_ambient = 64;
                    int light_attenuation = 768;
                    int lightsrc_x = -50;
                    int lightsrc_y = -10;
                    int lightsrc_z = -50;

                    light_ambient += map_element->light_ambient;
                    light_attenuation += map_element->light_attenuation;

                    int light_magnitude = (int)sqrt(
                        lightsrc_x * lightsrc_x + lightsrc_y * lightsrc_y +
                        lightsrc_z * lightsrc_z);
                    int attenuation = (light_attenuation * light_magnitude) >> 8;

                    struct DashModel* dm = scene2_element_dash_model(scene_element);
                    if( !dashmodel_is_lightable(dm) )
                        continue;
                    apply_lighting(
                        dashmodel_face_colors_a(dm),
                        dashmodel_face_colors_b(dm),
                        dashmodel_face_colors_c(dm),
                        dashmodel_merged_normals(dm)->lighting_vertex_normals,
                        dashmodel_merged_normals(dm)->lighting_face_normals,
                        dashmodel_face_indices_a(dm),
                        dashmodel_face_indices_b(dm),
                        dashmodel_face_indices_c(dm),
                        dashmodel_face_count(dm),
                        dashmodel_face_colors_flat(dm),
                        dashmodel_face_alphas(dm),
                        dashmodel_face_textures(dm),
                        dashmodel_face_infos(dm),
                        light_ambient,
                        attenuation,
                        lightsrc_x,
                        lightsrc_y,
                        lightsrc_z);
                }
            }
        }
    }
}

static void
defaultlight_build(struct World* world)
{
    struct Scene2Element* scene_element = NULL;
    struct SharelightMapTile* map_tile = NULL;
    struct SharelightMapElement* map_element = NULL;

    for( int sx = 0; sx < world->sharelight_map->width; sx++ )
    {
        for( int sz = 0; sz < world->sharelight_map->height; sz++ )
        {
            for( int slevel = 0; slevel < MAP_TERRAIN_LEVELS; slevel++ )
            {
                map_tile = sharelight_map_tile_at(world->sharelight_map, sx, sz, slevel);
                assert(map_tile && "Sharelight map tile must be valid");

                for( int32_t pi = map_tile->defaultlight_head; pi != -1;
                     pi = world->sharelight_map->pool[pi].next )
                {
                    map_element = &world->sharelight_map->pool[pi].element;
                    if( map_element->element_idx == -1 )
                        continue;

                    scene_element = scene2_element_at(world->scene2, map_element->element_idx);

                    if( !scene_element || !scene2_element_dash_model(scene_element) )
                        continue;

                    if( !dashmodel_is_lightable(scene2_element_dash_model(scene_element)) )
                        continue;

                    _light_model_default(
                        scene2_element_dash_model(scene_element),
                        map_element->light_attenuation,
                        map_element->light_ambient);
                    dashmodel_free_normals(scene2_element_dash_model(scene_element));
                }
            }
        }
    }
}

static void
world_build_lighting(struct World* world)
{
    struct SharelightMapTile* map_tile = NULL;
    struct SharelightMapElement* map_element = NULL;
    struct Scene2Element* scene_element = NULL;

    for( int sx = 0; sx < world->sharelight_map->width; sx++ )
    {
        for( int sz = 0; sz < world->sharelight_map->height; sz++ )
        {
            for( int slevel = 0; slevel < MAP_TERRAIN_LEVELS; slevel++ )
            {
                map_tile = sharelight_map_tile_at(world->sharelight_map, sx, sz, slevel);
                assert(map_tile && "Sharelight map tile must be valid");

                for( int32_t pi = map_tile->sharelight_head; pi != -1;
                     pi = world->sharelight_map->pool[pi].next )
                {
                    map_element = &world->sharelight_map->pool[pi].element;
                    scene_element = scene2_element_at(world->scene2, map_element->element_idx);
                    if( !scene_element || !scene2_element_dash_model(scene_element) )
                        continue;

                    dashmodel_alloc_normals(scene2_element_dash_model(scene_element));
                    dashmodel_calculate_vertex_normals(scene2_element_dash_model(scene_element));
                }
            }
        }
    }

    sharelight_build(world);

    for( int sx = 0; sx < world->sharelight_map->width; sx++ )
    {
        for( int sz = 0; sz < world->sharelight_map->height; sz++ )
        {
            for( int slevel = 0; slevel < MAP_TERRAIN_LEVELS; slevel++ )
            {
                map_tile = sharelight_map_tile_at(world->sharelight_map, sx, sz, slevel);
                assert(map_tile && "Sharelight map tile must be valid");

                for( int32_t pi = map_tile->sharelight_head; pi != -1;
                     pi = world->sharelight_map->pool[pi].next )
                {
                    map_element = &world->sharelight_map->pool[pi].element;
                    scene_element = scene2_element_at(world->scene2, map_element->element_idx);
                    if( !scene_element || !scene2_element_dash_model(scene_element) )
                        continue;

                    dashmodel_free_normals(scene2_element_dash_model(scene_element));
                }
            }
        }
    }

    defaultlight_build(world);
}

#endif