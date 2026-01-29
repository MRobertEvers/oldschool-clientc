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

    int mapx_sw = scene_builder->wx_sw / 64;
    int mapz_sw = scene_builder->wz_sw / 64;
    int mapx_ne = (scene_builder->wx_ne) / 64;
    int mapz_ne = (scene_builder->wz_ne) / 64;

    int offset_x = scene_builder->wx_sw % 64;
    int offset_z = scene_builder->wz_sw % 64;

    terrain_grid->mapx_ne = mapx_ne;
    terrain_grid->mapz_ne = mapz_ne;
    terrain_grid->mapx_sw = mapx_sw;
    terrain_grid->mapz_sw = mapz_sw;
    terrain_grid->offset_x = offset_x;
    terrain_grid->offset_z = offset_z;
    terrain_grid->size_x = scene_builder->size_x;
    terrain_grid->size_z = scene_builder->size_z;

    int map_index = 0;
    struct CacheMapTerrain* map_terrain = NULL;
    for( int mapz = mapz_sw; mapz <= mapz_ne; mapz++ )
    {
        for( int mapx = mapx_sw; mapx <= mapx_ne; mapx++ )
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
                tile_heights_at_sized(terrain_grid, sx, sz, slevel, 1, 1, &tile_heights);

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
    int wx_sw,
    int wz_sw,
    int wx_ne,
    int wz_ne,
    int size_x,
    int size_z)
{
    scene_builder->wx_sw = wx_sw;
    scene_builder->wz_sw = wz_sw;
    scene_builder->wx_ne = wx_ne;
    scene_builder->wz_ne = wz_ne;
    scene_builder->size_x = size_x;
    scene_builder->size_z = size_z;

    int height = size_z; // ((mapz_ne - mapz_sw + 1) * MAP_TERRAIN_Z) - base_tile_x;
    int width = size_x;  // (mapx_ne - mapx_sw + 1) * MAP_TERRAIN_X - base_tile_z;

    scene_builder->build_grid = build_grid_new(width, height);
    scene_builder->shademap = shademap_new(width, height, MAP_TERRAIN_LEVELS);

    struct TerrainGrid terrain_grid;

    struct Scene* scene = NULL;

    init_terrain_grid(scene_builder, &terrain_grid);

    init_build_grid(&terrain_grid, scene_builder->build_grid);

    scene = scene_new(width, height, 1024);

    build_scene_scenery(scene_builder, &terrain_grid, scene);
    build_scene_terrain(scene_builder, &terrain_grid, scene->terrain);

    painter_mark_static_count(scene_builder->painter);

    return scene;
}

struct Scene*
scenebuilder_load_from_buildcachedat(
    struct SceneBuilder* scene_builder,
    int wx_sw,
    int wz_sw,
    int wx_ne,
    int wz_ne,
    int size_x,
    int size_z,
    struct BuildCacheDat* buildcachedat)
{
    scene_builder->buildcachedat = buildcachedat;
    return scenebuiler_build(scene_builder, wx_sw, wz_sw, wx_ne, wz_ne, size_x, size_z);
}

struct Scene*
scenebuilder_load_from_buildcache(
    struct SceneBuilder* scene_builder,
    int wx_sw,
    int wz_sw,
    int wx_ne,
    int wz_ne,
    int size_x,
    int size_z,
    struct BuildCache* buildcache)
{
    scene_builder->buildcache = buildcache;
    return scenebuiler_build(scene_builder, wx_sw, wz_sw, wx_ne, wz_ne, size_x, size_z);
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

static void
dash_position_from_offset_wxh_elem(
    struct SceneElement* element,
    int sx,
    int sz,
    int height_center,
    int size_x,
    int size_z)
{
    element->dash_position->x = sx * TILE_SIZE + size_x * 64;
    element->dash_position->z = sz * TILE_SIZE + size_z * 64;
    element->dash_position->y = height_center;
}

static struct DashPosition*
dash_position_new_from_offset_1x1_elem(
    int sx,
    int sz,
    int height_center)
{
    struct DashPosition* dash_position = malloc(sizeof(struct DashPosition));
    memset(dash_position, 0, sizeof(struct DashPosition));

    dash_position_from_offset_wxh_elem(dash_position, sx, sz, height_center, 1, 1);

    return dash_position;
}

void
scenebuilder_push_dynamic_element(
    struct SceneBuilder* scene_builder,
    struct Scene* scene,
    int sx,
    int sz,
    int slevel,
    int size_x,
    int size_z,
    struct SceneElement* element)
{
    struct SceneElement scene_element = { 0 };

    int height_center = scene_terrain_height_center(scene, sx, sz, slevel);

    scene_element.dash_model = element->dash_model;
    scene_element.dash_position = element->dash_position;
    dash_position_from_offset_wxh_elem(&scene_element, sx, sz, height_center, size_x, size_z);

    int element_id = scene_scenery_push_dynamic_element_move(scene->scenery, &scene_element);

    painter_add_normal_scenery(scene_builder->painter, sx, sz, slevel, element_id, 1, 1);
}

void
scenebuilder_reset_dynamic_elements(
    struct SceneBuilder* scene_builder,
    struct Scene* scene)
{
    painter_reset_to_static(scene_builder->painter);
    scene_scenery_reset_dynamic_elements(scene->scenery);
}