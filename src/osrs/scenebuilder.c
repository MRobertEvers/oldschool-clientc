#include "scenebuilder.h"

#include "graphics/dash.h"

#include <assert.h>

// clang-format off
#include "terrain_grid.u.c"
#include "scenebuilder_terrain.u.c"
#include "scenebuilder_scenery.u.c"
#include "scenebuilder.u.c"
// clang-format on

#define ENTRYS 4096

struct SceneBuilder*
scenebuilder_new_painter(
    struct Painter* painter,
    struct Minimap* minimap,
    int mapx_sw,
    int mapz_sw,
    int mapx_ne,
    int mapz_ne)
{
    struct DashMapConfig config;
    struct SceneBuilder* scene_builder = malloc(sizeof(struct SceneBuilder));
    memset(scene_builder, 0, sizeof(struct SceneBuilder));

    scene_builder->painter = painter;
    scene_builder->minimap = minimap;
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

    buffer_size = ENTRYS * 8 * sizeof(struct FrameAnimEntry);
    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct FrameAnimEntry),
    };
    scene_builder->frames_hmap = dashmap_new(&config, 0);

    buffer_size = ENTRYS * sizeof(struct FlotypeEntry);
    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct FlotypeEntry),
    };
    scene_builder->flotypes_hmap = dashmap_new(&config, 0);

    buffer_size = ENTRYS * sizeof(struct DatSequenceEntry);
    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct DatSequenceEntry),
    };
    scene_builder->sequences_configmap = dashmap_new(&config, 0);

    buffer_size = 1024 * 16 * sizeof(struct CacheConfigLocationEntry);
    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct CacheConfigLocationEntry),
    };
    memset(config.buffer, 0, buffer_size);
    scene_builder->config_locs_hmap = dashmap_new(&config, 0);

    buffer_size = ENTRYS * sizeof(struct TextureEntry);
    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct TextureEntry),
    };
    scene_builder->textures_hmap = dashmap_new(&config, 0);

    int height = (mapz_ne - mapz_sw + 1) * MAP_TERRAIN_Z;
    int width = (mapx_ne - mapx_sw + 1) * MAP_TERRAIN_X;

    scene_builder->build_grid = build_grid_new(width, height);
    scene_builder->shademap = shademap_new(width, height, MAP_TERRAIN_LEVELS);

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

    build_grid_free(scene_builder->build_grid);
    shademap_free(scene_builder->shademap);

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
scenebuilder_cache_configmap_sequences(
    struct SceneBuilder* scene_builder,
    struct DashMap* config_sequences_configmap)
{
    assert(configmap_valid(config_sequences_configmap) && "Config sequences map must be valid");
    scene_builder->sequences_configmap = config_sequences_configmap;
}

void
scenebuilder_cache_frame(
    struct SceneBuilder* scene_builder,
    // frame_file
    int frame_anim_id,
    struct CacheFrame* frame)
{
    struct FrameAnimEntry* frame_anim_entry = (struct FrameAnimEntry*)dashmap_search(
        scene_builder->frames_hmap, &frame_anim_id, DASHMAP_INSERT);
    assert(frame_anim_entry && "Frame anim must be inserted into hmap");

    frame_anim_entry->id = frame_anim_id;
    frame_anim_entry->frame = frame;
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
    model_entry->model->_id = model_id;
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

void
scenebuilder_cache_flotype(
    struct SceneBuilder* scene_builder,
    int flotype_id,
    struct CacheConfigOverlay* flotype)
{
    struct FlotypeEntry* flotype_entry = NULL;

    flotype_entry = (struct FlotypeEntry*)dashmap_search(
        scene_builder->flotypes_hmap, &flotype_id, DASHMAP_INSERT);
    assert(flotype_entry && "Flotype must be inserted into hmap");
    flotype_entry->id = flotype_id;
    flotype_entry->flotype = flotype;
}

void
scenebuilder_cache_config_loc(
    struct SceneBuilder* scene_builder,
    int loc_id,
    struct CacheConfigLocation* config_loc)
{
    struct CacheConfigLocationEntry* config_loc_entry = NULL;
    config_loc_entry = (struct CacheConfigLocationEntry*)dashmap_search(
        scene_builder->config_locs_hmap, &loc_id, DASHMAP_INSERT);
    assert(config_loc_entry && "Config loc must be inserted into hmap");
    assert(config_loc_entry->id == loc_id);
    assert(!config_loc_entry->config_loc || config_loc_entry->config_loc->_id == loc_id);
    config_loc_entry->id = loc_id;

    config_loc_entry->config_loc = config_loc;
}

void
scenebuilder_cache_animframe(
    struct SceneBuilder* scene_builder,
    int animframe_id,
    struct CacheAnimframe* animframe)
{
    struct AnimframeEntry* animframe_entry = NULL;
    animframe_entry = (struct AnimframeEntry*)dashmap_search(
        scene_builder->frames_hmap, &animframe_id, DASHMAP_INSERT);
    assert(animframe_entry && "Animframe must be inserted into hmap");
    animframe_entry->id = animframe_id;
    animframe_entry->animframe = animframe;
}

void
scenebuilder_cache_dat_sequence(
    struct SceneBuilder* scene_builder,
    int sequence_id,
    struct CacheDatSequence* sequence)
{
    struct DatSequenceEntry* sequence_entry = NULL;
    sequence_entry = (struct DatSequenceEntry*)dashmap_search(
        scene_builder->sequences_configmap, &sequence_id, DASHMAP_INSERT);
    assert(sequence_entry && "Dat sequence must be inserted into hmap");
    sequence_entry->id = sequence_id;
    sequence_entry->dat_sequence = sequence;
}

void
scenebuilder_cache_texture(
    struct SceneBuilder* scene_builder,
    int texture_id,
    struct DashTexture* texture)
{
    struct TextureEntry* texture_entry = NULL;
    texture_entry = (struct TextureEntry*)dashmap_search(
        scene_builder->textures_hmap, &texture_id, DASHMAP_INSERT);
    assert(texture_entry && "Texture must be inserted into hmap");
    texture_entry->id = texture_id;
    texture_entry->texture = texture;
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

            map_terrain->map_x = mapx;
            map_terrain->map_z = mapz;
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
    build_scene_terrain(scene_builder, &terrain_grid, scene->terrain);

    return scene;
}