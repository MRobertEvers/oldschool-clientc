#include "scenebuilder.h"

#include "graphics/dash.h"

#include <assert.h>

// clang-format off
#include "terrain_grid.u.c"
#include "scenebuilder_terrain.u.c"
#include "scenebuilder_scenery.u.c"
#include "scenebuild_compat.u.c"
#include "scenebuilder.u.c"
// clang-format on

#define ENTRYS 4096

struct SceneBuilder*
scenebuilder_new_painter(
    struct Painter* painter,
    struct Minimap* minimap)
{
    struct DashMapConfig config;
    struct SceneBuilder* scene_builder = malloc(sizeof(struct SceneBuilder));
    memset(scene_builder, 0, sizeof(struct SceneBuilder));

    scene_builder->painter = painter;
    scene_builder->minimap = minimap;

    return scene_builder;
}

void
scenebuilder_free(struct SceneBuilder* scene_builder)
{
    build_grid_free(scene_builder->build_grid);
    shademap_free(scene_builder->shademap);

    free(scene_builder);
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
    struct CacheMapTerrain* map_terrain = NULL;
    for( int mapz = scene_builder->mapz_sw; mapz <= scene_builder->mapz_ne; mapz++ )
    {
        for( int mapx = scene_builder->mapx_sw; mapx <= scene_builder->mapx_ne; mapx++ )
        {
            map_terrain = scenebuilder_compat_get_map_terrain(scene_builder, mapx, mapz);
            assert(map_terrain && "Map terrain must be found");

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

static struct Scene*
scenebuiler_build(
    struct SceneBuilder* scene_builder,
    int mapx_sw,
    int mapz_sw,
    int mapx_ne,
    int mapz_ne)
{
    scene_builder->mapx_sw = mapx_sw;
    scene_builder->mapz_sw = mapz_sw;
    scene_builder->mapx_ne = mapx_ne;
    scene_builder->mapz_ne = mapz_ne;

    int height = (mapz_ne - mapz_sw + 1) * MAP_TERRAIN_Z;
    int width = (mapx_ne - mapx_sw + 1) * MAP_TERRAIN_X;

    scene_builder->build_grid = build_grid_new(width, height);
    scene_builder->shademap = shademap_new(width, height, MAP_TERRAIN_LEVELS);

    struct TerrainGrid terrain_grid;

    struct Scene* scene = NULL;

    init_terrain_grid(scene_builder, &terrain_grid);

    init_build_grid(&terrain_grid, scene_builder->build_grid);

    scene =
        scene_new(terrain_grid_x_width(&terrain_grid), terrain_grid_z_height(&terrain_grid), 1024);

    build_scene_scenery(scene_builder, &terrain_grid, scene);
    build_scene_terrain(scene_builder, &terrain_grid, scene->terrain);

    return scene;
}

struct Scene*
scenebuilder_load_from_buildcachedat(
    struct SceneBuilder* scene_builder,
    int mapx_sw,
    int mapz_sw,
    int mapx_ne,
    int mapz_ne,
    struct BuildCacheDat* buildcachedat)
{
    scene_builder->buildcachedat = buildcachedat;
    return scenebuiler_build(scene_builder, mapx_sw, mapz_sw, mapx_ne, mapz_ne);
}

struct Scene*
scenebuilder_load_from_buildcache(
    struct SceneBuilder* scene_builder,
    int mapx_sw,
    int mapz_sw,
    int mapx_ne,
    int mapz_ne,
    struct BuildCache* buildcache)
{
    scene_builder->buildcache = buildcache;
    return scenebuiler_build(scene_builder, mapx_sw, mapz_sw, mapx_ne, mapz_ne);
}

// struct SceneAnimation*
// scenebuilder_new_animation(
//     struct SceneBuilder* scene_builder,
//     int sequence_id)
// {
//     struct DatSequenceEntry* sequence_entry = NULL;
//     sequence_entry = (struct DatSequenceEntry*)dashmap_search(
//         scene_builder->sequences_configmap, &sequence_id, DASHMAP_FIND);
//     assert(sequence_entry && "Sequence must be found in hmap");

//     struct SceneAnimation* animation = NULL;
//     animation = load_model_animations_dati(
//         sequence_id, scene_builder->sequences_configmap, scene_builder->frames_hmap);

//     return animation;
// }

static struct DashPosition*
dash_position_from_offset_1x1_elem(
    int sx,
    int sz,
    int height_center)
{
    struct DashPosition* dash_position = malloc(sizeof(struct DashPosition));
    memset(dash_position, 0, sizeof(struct DashPosition));

    dash_position->x = sx * TILE_SIZE + 64;
    dash_position->z = sz * TILE_SIZE + 64;
    dash_position->y = height_center;

    return dash_position;
}

void
scenebuilder_push_element(
    struct SceneBuilder* scene_builder,
    struct Scene* scene,
    int sx,
    int sz,
    int slevel,
    int size_x,
    int size_z,
    struct DashModel* dash_model)
{
    struct SceneElement scene_element = { 0 };

    int height_center = scene_terrain_height_center(scene, sx, sz, slevel);

    scene_element.dash_model = dash_model;
    scene_element.dash_position = dash_position_from_offset_1x1_elem(sx, sz, height_center);

    int element_id = scene_push_element_move(scene, &scene_element);

    painter_add_normal_scenery(scene_builder->painter, sx, sz, slevel, element_id, size_x, size_z);
}