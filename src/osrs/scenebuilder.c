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

#define ENTRYS 4096

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

    int buffer_size = ENTRYS * sizeof(struct ModelEntry);
    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct ModelEntry),
    };
    scene_builder->models_hmap = dashmap_new(&config, 0);

    buffer_size = ENTRYS * sizeof(struct MapLocsEntry);
    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct MapLocsEntry),
    };
    scene_builder->map_locs_hmap = dashmap_new(&config, 0);

    buffer_size = ENTRYS * sizeof(struct MapTerrainEntry);
    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct MapTerrainEntry),
    };
    scene_builder->map_terrains_hmap = dashmap_new(&config, 0);

    int height = (mapz_ne - mapz_sw + 1) * MAP_TERRAIN_Z;
    int width = (mapx_ne - mapx_sw + 1) * MAP_TERRAIN_X;

    scene_builder->build_grid = build_grid_new(width, height);

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

static void
init_build_grid(
    struct TerrainGrid* terrain_grid,
    struct BuildGrid* build_grid)
{
    struct TileHeights tile_heights = { 0 };
    struct BuildTile* build_tile = NULL;

    build_grid->tile_width_x = terrain_grid_x_width(terrain_grid);
    build_grid->tile_width_z = terrain_grid_z_height(terrain_grid);

    for( int sx = 0; sx < build_grid->tile_width_x; sx++ )
    {
        for( int sz = 0; sz < build_grid->tile_width_z; sz++ )
        {
            for( int slevel = 0; slevel < MAP_TERRAIN_LEVELS; slevel++ )
            {
                tile_heights_at(
                    terrain_grid,
                    terrain_grid->mapx_sw + (sx / MAP_TERRAIN_X),
                    terrain_grid->mapz_sw + (sz / MAP_TERRAIN_Z),
                    sx % MAP_TERRAIN_X,
                    sz % MAP_TERRAIN_Z,
                    slevel,
                    &tile_heights);

                int index = build_grid_index(
                    build_grid->tile_width_x, build_grid->tile_width_z, sx, sz, slevel);
                init_build_tile(
                    &build_grid->tiles[index], sx, sz, slevel, tile_heights.height_center);
            }
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

struct Scene*
scenebuilder_load(struct SceneBuilder* scene_builder)
{
    struct TerrainGrid terrain_grid;

    struct Scene* scene = NULL;

    init_terrain_grid(scene_builder, &terrain_grid);

    init_build_grid(&terrain_grid, scene_builder->build_grid);

    scene =
        scene_new(terrain_grid_x_width(&terrain_grid), terrain_grid_z_height(&terrain_grid), 1024);

    build_scene_scenery(scene_builder, &terrain_grid, scene);
    build_scene_terrain(scene_builder, &terrain_grid, NULL, scene->terrain);

    return scene;
}