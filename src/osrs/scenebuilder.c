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

    build_scene_terrain(scene_builder, &terrain_grid, NULL, scene->terrain);

    return scene;
}