#ifndef WORLD_SHARELIGHT_U_C
#define WORLD_SHARELIGHT_U_C

#include "world.h"

#include <assert.h>
#include <math.h>

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

    for( int vertex = 0; vertex < model->vertex_count; vertex++ )
    {
        x = model->vertices_x[vertex] - check_offset_x;
        y = model->vertices_y[vertex] - check_offset_y;
        z = model->vertices_z[vertex] - check_offset_z;

        model_a_normal = &vertex_normals[vertex];
        model_a_lighting_normal = &lighting_vertex_normals[vertex];

        for( int other_vertex = 0; other_vertex < other_model->vertex_count; other_vertex++ )
        {
            other_x = other_model->vertices_x[other_vertex];
            other_y = other_model->vertices_y[other_vertex];
            other_z = other_model->vertices_z[other_vertex];

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
    if( merged_vertex_count < 3 || true )
        return;

    // Can't have two faces with the same 3 points, so only need to check two.
    for( int face = 0; face < model->face_count; face++ )
    {
        if( g_vertex_a_merge_index[model->face_indices_a[face]] == g_merge_index &&
            g_vertex_a_merge_index[model->face_indices_b[face]] == g_merge_index &&
            g_vertex_a_merge_index[model->face_indices_c[face]] == g_merge_index )
        {
            // OS1 initializes face infos to 0 here.
            if( !model->face_infos )
            {
                model->face_infos = malloc(sizeof(int) * model->face_count);
                memset(model->face_infos, 0, sizeof(int) * model->face_count);
            }
            // Hidden face (facetype 2 is hidden)
            model->face_infos[face] = 2;
            break;
        }
    }
    for( int face = 0; face < other_model->face_count; face++ )
    {
        if( g_vertex_b_merge_index[other_model->face_indices_a[face]] == g_merge_index &&
            g_vertex_b_merge_index[other_model->face_indices_b[face]] == g_merge_index &&
            g_vertex_b_merge_index[other_model->face_indices_c[face]] == g_merge_index )
        {
            // OS1 initializes face infos to 0 here.
            if( !other_model->face_infos )
            {
                other_model->face_infos = malloc(sizeof(int) * other_model->face_count);
                memset(other_model->face_infos, 0, sizeof(int) * other_model->face_count);
            }
            // Hidden face (facetype 2 is hidden)
            other_model->face_infos[face] = 2;
        }
    }
}

static void
alloc_merged_normals(struct Scene2Element* scene_element)
{
    if( !scene_element || !scene_element->dash_model || !scene_element->dash_model->normals ||
        scene_element->dash_model->merged_normals )
        return;

    scene_element->dash_model->merged_normals =
        dashmodel_normals_new_copy(scene_element->dash_model->normals);
}

#define ADJACENT_TILES_COUNT 24

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

    struct MapBuildLocEntity* mapbuild_entity = NULL;
    struct MapBuildLocEntity* adjacent_mapbuild_entity = NULL;

    for( int sx = 0; sx < world->sharelight_map->width; sx++ )
    {
        for( int sz = 0; sz < world->sharelight_map->height; sz++ )
        {
            for( int slevel = 0; slevel < MAP_TERRAIN_LEVELS; slevel++ )
            {
                map_tile = sharelight_map_tile_at(world->sharelight_map, sx, sz, slevel);
                assert(map_tile && "Sharelight map tile must be valid");

                for( int i = 0; i < map_tile->elements_count; i++ )
                {
                    map_element = &map_tile->elements[i];

                    mapbuild_entity = &world->map_build_loc_entities[map_element->element_idx];
                    if( mapbuild_entity->scene_element.element_id == -1 )
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

                    for( int i = 0; i < adjacent_tiles_count; i++ )
                    {
                        struct TileCoord adjacent_tile_coord = adjacent_tiles[i];
                        adjacent_map_tile = sharelight_map_tile_at(
                            world->sharelight_map,
                            adjacent_tile_coord.x,
                            adjacent_tile_coord.z,
                            adjacent_tile_coord.level);
                        for( int j = 0; j < adjacent_map_tile->elements_count; j++ )
                        {
                            adjacent_map_element = &adjacent_map_tile->elements[j];
                            if( adjacent_map_element->element_idx == map_element->element_idx )
                                continue;

                            adjacent_mapbuild_entity =
                                &world->map_build_loc_entities[adjacent_map_element->element_idx];
                            if( adjacent_mapbuild_entity->scene_element.element_id == -1 )
                                continue;

                            scene_element =
                                &world->scene2->elements[mapbuild_entity->scene_element.element_id];
                            adjacent_scene_element =
                                &world->scene2
                                     ->elements[adjacent_mapbuild_entity->scene_element.element_id];

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

                            assert(
                                adjacent_scene_element->dash_model->normals &&
                                "Adjacent scene element must have normals");
                            assert(
                                scene_element->dash_model->normals &&
                                "Scene element must have normals");
                            alloc_merged_normals(scene_element);
                            alloc_merged_normals(adjacent_scene_element);

                            merge_normals(
                                scene_element->dash_model,
                                scene_element->dash_model->normals->lighting_vertex_normals,
                                scene_element->dash_model->merged_normals->lighting_vertex_normals,
                                adjacent_scene_element->dash_model,
                                adjacent_scene_element->dash_model->normals
                                    ->lighting_vertex_normals,
                                adjacent_scene_element->dash_model->merged_normals
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

                for( int i = 0; i < map_tile->elements_count; i++ )
                {
                    map_element = &map_tile->elements[i];

                    mapbuild_entity = &world->map_build_loc_entities[map_element->element_idx];
                    if( mapbuild_entity->scene_element.element_id == -1 )
                        continue;

                    scene_element =
                        scene2_element_at(world->scene2, mapbuild_entity->scene_element.element_id);

                    if( !scene_element->dash_model )
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

                    alloc_merged_normals(scene_element);

                    apply_lighting(
                        scene_element->dash_model->lighting->face_colors_hsl_a,
                        scene_element->dash_model->lighting->face_colors_hsl_b,
                        scene_element->dash_model->lighting->face_colors_hsl_c,
                        scene_element->dash_model->merged_normals->lighting_vertex_normals,
                        scene_element->dash_model->merged_normals->lighting_face_normals,
                        scene_element->dash_model->face_indices_a,
                        scene_element->dash_model->face_indices_b,
                        scene_element->dash_model->face_indices_c,
                        scene_element->dash_model->face_count,
                        scene_element->dash_model->face_colors,
                        scene_element->dash_model->face_alphas,
                        scene_element->dash_model->face_textures,
                        scene_element->dash_model->face_infos,
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

#endif