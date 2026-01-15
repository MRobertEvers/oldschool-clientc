#include "scenebuilder.h"

#include "graphics/dash.h"

#include <assert.h>

// clang-format off
#include "terrain_grid.u.c"
#include "scenebuilder_terrain.u.c"
#include "scenebuilder_scenery.u.c"
#include "scenebuilder.u.c"
// clang-format on

#define MAPXZ(mapx, mapz) ((mapx << 16) | mapz)

struct SceneBuilder*
scenebuilder_new_painter(
    struct Painter* painter,
    int mapx_sw,
    int mapz_sw,
    int mapx_ne,
    int mapz_ne)
{
    struct DashMapConfig config;
    struct SceneBuilder* scene_builder = malloc(sizeof(struct SceneBuilder));
    memset(scene_builder, 0, sizeof(struct SceneBuilder));

    scene_builder->painter = painter;

    scene_builder->mapx_sw = mapx_sw;
    scene_builder->mapz_sw = mapz_sw;
    scene_builder->mapx_ne = mapx_ne;
    scene_builder->mapz_ne = mapz_ne;

    int buffer_size = 1024 * sizeof(struct ModelEntry);
    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct ModelEntry),
    };
    scene_builder->models_hmap = dashmap_new(&config, 0);

    buffer_size = 1024 * sizeof(struct MapLocsEntry);
    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct MapLocsEntry),
    };
    scene_builder->map_locs_hmap = dashmap_new(&config, 0);

    buffer_size = 1024 * sizeof(struct MapTerrainEntry);
    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct MapTerrainEntry),
    };
    scene_builder->map_terrains_hmap = dashmap_new(&config, 0);

    return scene_builder;
}

void
scenebuilder_free(struct SceneBuilder* scene_builder)
{
    free(dashmap_buffer_ptr(scene_builder->models_hmap));
    dashmap_free(scene_builder->models_hmap);

    free(dashmap_buffer_ptr(scene_builder->map_locs_hmap));
    dashmap_free(scene_builder->map_locs_hmap);

    free(dashmap_buffer_ptr(scene_builder->map_terrains_hmap));
    dashmap_free(scene_builder->map_terrains_hmap);

    free(scene_builder);
}

void
scenebuilder_cache_configmap_underlay(
    struct SceneBuilder* scene_builder,
    struct DashMap* config_underlay_map)
{
    assert(configmap_valid(config_underlay_map) && "Config underlay map must be valid");
    scene_builder->config_underlay_configmap = config_underlay_map;
}

void
scenebuilder_cache_configmap_overlay(
    struct SceneBuilder* scene_builder,
    struct DashMap* config_overlay_map)
{
    assert(configmap_valid(config_overlay_map) && "Config overlay map must be valid");
    scene_builder->config_overlay_configmap = config_overlay_map;
}

void
scenebuilder_cache_configmap_locs(
    struct SceneBuilder* scene_builder,
    struct DashMap* config_locs_configmap)
{
    assert(configmap_valid(config_locs_configmap) && "Config locs map must be valid");
    scene_builder->config_locs_configmap = config_locs_configmap;
}

void
scenebuilder_cache_model(
    struct SceneBuilder* scene_builder,
    int model_id,
    struct CacheModel* model)
{
    struct ModelEntry* model_entry = NULL;

    model_entry =
        (struct ModelEntry*)dashmap_search(scene_builder->models_hmap, &model_id, DASHMAP_INSERT);
    assert(model_entry && "Model must be inserted into hmap");
    model_entry->id = model_id;
    model_entry->model = model;
}

void
scenebuilder_cache_map_locs(
    struct SceneBuilder* scene_builder,
    int mapx,
    int mapz,
    struct CacheMapLocs* map_locs)
{
    struct MapLocsEntry* loc_entry = NULL;

    int mapxz = MAPXZ(mapx, mapz);

    loc_entry =
        (struct MapLocsEntry*)dashmap_search(scene_builder->map_locs_hmap, &mapxz, DASHMAP_INSERT);
    assert(loc_entry && "Loc must be inserted into hmap");
    loc_entry->id = mapxz;
    loc_entry->mapx = mapx;
    loc_entry->mapz = mapz;
    loc_entry->locs = map_locs;
}

void
scenebuilder_cache_map_terrain(
    struct SceneBuilder* scene_builder,
    int mapx,
    int mapz,
    struct CacheMapTerrain* map_terrain)
{
    struct MapTerrainEntry* map_entry = NULL;

    int mapxz = MAPXZ(mapx, mapz);

    map_entry = (struct MapTerrainEntry*)dashmap_search(
        scene_builder->map_terrains_hmap, &mapxz, DASHMAP_INSERT);
    assert(map_entry && "Map must be inserted into hmap");
    map_entry->id = mapxz;
    map_entry->mapx = mapx;
    map_entry->mapz = mapz;
    map_entry->map_terrain = map_terrain;
}

static void
init_terrain_grid(
    struct SceneBuilder* scene_builder,
    struct TerrainGrid* terrain_grid)
{
    int mapxz = 0;
    memset(terrain_grid, 0, sizeof(struct TerrainGrid));

    terrain_grid->mapx_ne = scene_builder->mapx_ne;
    terrain_grid->mapz_ne = scene_builder->mapz_ne;
    terrain_grid->mapx_sw = scene_builder->mapx_sw;
    terrain_grid->mapz_sw = scene_builder->mapz_sw;

    int map_index = 0;
    for( int mapz = scene_builder->mapz_sw; mapz <= scene_builder->mapz_ne; mapz++ )
    {
        for( int mapx = scene_builder->mapx_sw; mapx <= scene_builder->mapx_ne; mapx++ )
        {
            struct MapTerrainEntry* map_terrain_entry = NULL;
            struct CacheMapTerrain* map_terrain = NULL;

            mapxz = MAPXZ(mapx, mapz);
            map_terrain_entry = (struct MapTerrainEntry*)dashmap_search(
                scene_builder->map_terrains_hmap, &mapxz, DASHMAP_FIND);
            assert(map_terrain_entry && "Map terrain must be found in hmap");
            map_terrain = map_terrain_entry->map_terrain;

            terrain_grid->map_terrain[map_index] = map_terrain;
            map_index++;
        }
    }
}

// static int
// gather_sharelight_models(
//     struct SceneScenery* scenery,
//     struct DashModel** out,
//     int out_size,
//     struct SceneSceneryTile* scenery_tile,
//     struct Loc* loc_pool,
//     int loc_pool_size,
//     struct SceneModel* models,
//     int models_size)
// {
//     int count = 0;
//     struct Loc* loc = NULL;
//     struct SceneModel* model = NULL;

//     if( tile->wall != -1 )
//     {
//         assert(tile->wall >= 0);
//         assert(tile->wall < loc_pool_size);
//         loc = &loc_pool[tile->wall];

//         model = tile_wall_model_nullable(models, loc, 1);
//         if( model && model->sharelight )
//             out[count++] = model;

//         model = tile_wall_model_nullable(models, loc, 0);
//         if( model && model->sharelight )
//             out[count++] = model;
//     }

//     // if( tile->ground_decor != -1 )
//     // {
//     //     assert(tile->ground_decor < loc_pool_size);
//     //     loc = &loc_pool[tile->ground_decor];

//     //     model = tile_ground_decor_model_nullable(models, loc);
//     //     if( model && model->sharelight )
//     //         out[count++] = model;
//     // }

//     for( int i = 0; i < tile->locs_length; i++ )
//     {
//         loc = &loc_pool[tile->locs[i]];
//         model = tile_scenery_model_nullable(models, loc);
//         if( model && model->sharelight )
//             out[count++] = model;

//         model = tile_wall_model_nullable(models, loc, 1);
//         if( model && model->sharelight )
//             out[count++] = model;

//         model = tile_wall_model_nullable(models, loc, 0);
//         if( model && model->sharelight )
//             out[count++] = model;

//         model = tile_ground_decor_model_nullable(models, loc);
//         if( model && model->sharelight )
//             out[count++] = model;
//     }

//     assert(count <= out_size);
//     return count;
// }

struct TileCoord
{
    int x;
    int z;
    int level;
};

static int
gather_adjacent_tiles(
    struct TileCoord* out,
    int out_size,
    struct SceneScenery* scenery,
    int tile_x,
    int tile_z,
    int tile_level,
    int tile_size_x,
    int tile_size_z)
{
    int min_tile_x = tile_x;
    int max_tile_x = tile_x + tile_size_x;
    int min_tile_z = tile_z - 1;
    int max_tile_z = tile_z + tile_size_z;

    int count = 0;
    for( int level = 0; level <= tile_level + 1; level++ )
    {
        for( int x = min_tile_x; x <= max_tile_x; x++ )
        {
            for( int z = min_tile_z; z <= max_tile_z; z++ )
            {
                if( (x == tile_x && z == tile_z && level == tile_level) )
                    continue;
                if( x < 0 || z < 0 || x >= scenery->tile_width_x || z >= scenery->tile_width_z ||
                    level < 0 || level >= MAP_TERRAIN_LEVELS )
                    continue;

                assert(count < out_size);
                out[count++] = (struct TileCoord){ .x = x, .z = z, .level = level };
            }
        }
    }

    return count;
}

static int
gather_sharelight_models(
    struct SceneScenery* scenery,
    struct SceneSceneryTile* scenery_tile,
    struct SceneElement** out,
    int out_size)
{
    int count = 0;
    struct SceneElement* element = NULL;
    if( scenery_tile->wall_a_element_idx != -1 )
    {
        element = scene_element_at(scenery, scenery_tile->wall_a_element_idx);
        assert(element && "Element must be valid");

        if( element->sharelight )
            out[count++] = element;
    }

    if( scenery_tile->wall_b_element_idx != -1 )
    {
        element = scene_element_at(scenery, scenery_tile->wall_b_element_idx);
        assert(element && "Element must be valid");

        if( element->sharelight )
            out[count++] = element;
    }

    // if( tile->ground_decor != -1 )
    // {
    //     assert(tile->ground_decor < loc_pool_size);
    //     loc = &loc_pool[tile->ground_decor];

    //     model = tile_ground_decor_model_nullable(models, loc);
    //     if( model && model->sharelight )
    //         out[count++] = model;
    // }

    // for( int i = 0; i < tile->locs_length; i++ )
    // {
    //     loc = &loc_pool[tile->locs[i]];
    //     model = tile_scenery_model_nullable(models, loc);
    //     if( model && model->sharelight )
    //         out[count++] = model;

    //     model = tile_wall_model_nullable(models, loc, 1);
    //     if( model && model->sharelight )
    //         out[count++] = model;

    //     model = tile_wall_model_nullable(models, loc, 0);
    //     if( model && model->sharelight )
    //         out[count++] = model;

    //     model = tile_ground_decor_model_nullable(models, loc);
    //     if( model && model->sharelight )
    //         out[count++] = model;
    // }

    assert(count <= out_size);
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
dashmodel_alias_normals(struct SceneElement* element)
{
    if( element->aliased_lighting_normals )
        return;

    element->aliased_lighting_normals = dashmodel_normals_new_copy(element->dash_model->normals);
}

struct Scene*
scenebuilder_load(struct SceneBuilder* scene_builder)
{
    struct MapLocsEntry* map_locs_entry = NULL;
    struct CacheMapLocs* map_locs = NULL;
    struct CacheMapLoc* map_loc = NULL;
    int mapxz = 0;
    struct TerrainGrid terrain_grid;
    struct DashMapIter* iter = dashmap_iter_new(scene_builder->map_locs_hmap);
    struct Scene* scene = NULL;

    init_terrain_grid(scene_builder, &terrain_grid);

    scene =
        scene_new(terrain_grid_x_width(&terrain_grid), terrain_grid_z_height(&terrain_grid), 1024);

    while( (map_locs_entry = (struct MapLocsEntry*)dashmap_iter_next(iter)) )
    {
        map_locs = map_locs_entry->locs;
        assert(map_locs && "Map locs must be valid");

        for( int i = 0; i < map_locs->locs_count; i++ )
        {
            map_loc = &map_locs->locs[i];
            assert(map_loc && "Map loc must be valid");

            scenery_add(
                scene_builder,
                &terrain_grid,
                map_locs_entry->mapx,
                map_locs_entry->mapz,
                map_loc,
                scene->scenery);
        }
    }

#define SHARELIGHT_MODELS_COUNT 40

    struct SceneSceneryTile* scenery_tile = NULL;
    struct SceneSceneryTile* adjacent_scenery_tile = NULL;

    struct TileCoord adjacent_tiles[40] = { 0 };
    struct SceneElement* sharelight_element = NULL;
    struct SceneElement* adjacent_element = NULL;
    //     struct SceneModel* sharelight_models[SHARELIGHT_MODELS_COUNT] = { 0 };
    //     struct SceneModel* adjacent_sharelight_models[SHARELIGHT_MODELS_COUNT] = { 0 };
    int sharelight_models_count = 0;
    int adjacent_sharelight_elements_count = 0;
    struct SceneElement* sharelight_elements[SHARELIGHT_MODELS_COUNT] = { 0 };
    struct SceneElement* adjacent_sharelight_elements[SHARELIGHT_MODELS_COUNT] = { 0 };
    for( int sx = 0; sx < scene->tile_width_x; sx++ )
    {
        for( int sz = 0; sz < scene->tile_width_z; sz++ )
        {
            for( int slevel = 0; slevel < MAP_TERRAIN_LEVELS; slevel++ )
            {
                scenery_tile = scene_scenery_tile_at(scene->scenery, sx, sz, slevel);
                assert(scenery_tile && "Scenery tile must be valid");

                sharelight_models_count = gather_sharelight_models(
                    scene->scenery, scenery_tile, sharelight_elements, SHARELIGHT_MODELS_COUNT);

                for( int i = 0; i < sharelight_models_count; i++ )
                {
                    sharelight_element = sharelight_elements[i];

                    int adjacent_tiles_count = gather_adjacent_tiles(
                        adjacent_tiles,
                        SHARELIGHT_MODELS_COUNT,
                        scene->scenery,
                        sx,
                        sz,
                        slevel,
                        sharelight_element->size_x,
                        sharelight_element->size_z);

                    for( int j = 0; j < adjacent_tiles_count; j++ )
                    {
                        struct TileCoord adjacent_tile_coord = adjacent_tiles[j];

                        adjacent_scenery_tile = scene_scenery_tile_at(
                            scene->scenery,
                            adjacent_tile_coord.x,
                            adjacent_tile_coord.z,
                            adjacent_tile_coord.level);

                        adjacent_sharelight_elements_count = gather_sharelight_models(
                            scene->scenery,
                            adjacent_scenery_tile,
                            adjacent_sharelight_elements,
                            SHARELIGHT_MODELS_COUNT);

                        for( int k = 0; k < adjacent_sharelight_elements_count; k++ )
                        {
                            adjacent_element = adjacent_sharelight_elements[k];
                            assert(adjacent_element && "Adjacent element must be valid");

                            int check_offset_x =
                                (adjacent_element->sx - sharelight_element->sx) * 128 +
                                (adjacent_element->size_x - sharelight_element->size_x) * 64;
                            int check_offset_y =
                                (adjacent_element->sz - sharelight_element->sz) * 128 +
                                (adjacent_element->size_z - sharelight_element->size_z) * 64;
                            int check_offset_level =
                                adjacent_element->height_center - sharelight_element->height_center;

                            dashmodel_alias_normals(sharelight_element);
                            dashmodel_alias_normals(adjacent_element);

                            merge_normals(
                                sharelight_element->dash_model,
                                sharelight_element->dash_model->normals->lighting_vertex_normals,
                                sharelight_element->aliased_lighting_normals
                                    ->lighting_vertex_normals,
                                adjacent_element->dash_model,
                                adjacent_element->dash_model->normals->lighting_vertex_normals,
                                adjacent_element->aliased_lighting_normals->lighting_vertex_normals,
                                check_offset_x,
                                check_offset_level,
                                check_offset_y);
                        }
                    }
                }
            }
        }
    }

    for( int i = 0; i < scene->scenery->elements_length; i++ )
    {
        struct SceneElement* element = &scene->scenery->elements[i];
        assert(element && "Element must be valid");

        if( !element->dash_model )
            continue;

        int light_ambient = 64;
        int light_attenuation = 768;
        int lightsrc_x = -50;
        int lightsrc_y = -10;
        int lightsrc_z = -50;

        light_ambient += element->light_ambient;
        light_attenuation += element->light_attenuation;

        int light_magnitude =
            (int)sqrt(lightsrc_x * lightsrc_x + lightsrc_y * lightsrc_y + lightsrc_z * lightsrc_z);
        int attenuation = (light_attenuation * light_magnitude) >> 8;

        if( element->sharelight )
        {
            dashmodel_alias_normals(element);
        }

        apply_lighting(
            element->dash_model->lighting->face_colors_hsl_a,
            element->dash_model->lighting->face_colors_hsl_b,
            element->dash_model->lighting->face_colors_hsl_c,
            element->sharelight ? element->aliased_lighting_normals->lighting_vertex_normals
                                : element->dash_model->normals->lighting_vertex_normals,
            element->dash_model->normals->lighting_face_normals,
            element->dash_model->face_indices_a,
            element->dash_model->face_indices_b,
            element->dash_model->face_indices_c,
            element->dash_model->face_count,
            element->dash_model->face_colors,
            element->dash_model->face_alphas,
            element->dash_model->face_textures,
            element->dash_model->face_infos,
            light_ambient,
            attenuation,
            lightsrc_x,
            lightsrc_y,
            lightsrc_z);
    }

    build_scene_terrain(scene_builder, &terrain_grid, NULL, scene->terrain);

    return scene;
}