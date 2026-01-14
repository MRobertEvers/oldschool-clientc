#include "scenebuilder.h"

#include "graphics/dash.h"

#include <assert.h>

// clang-format off
#include "terrain_grid.u.c"
#include "scenebuilder_terrain.u.c"
#include "scenebuilder.u.c"
// clang-format on

#define MAPXZ(mapx, mapz) ((mapx << 16) | mapz)

// static struct DashModel*
// load_model(
//     struct CacheConfigLocation* loc_config,
//     struct HMap* models_hmap,
//     int shape_select,
//     int orientation,
//     struct TileHeights* tile_heights)
// {
//     struct ModelEntry* model_entry = NULL;
//     int* shapes = loc_config->shapes;
//     int** model_id_sets = loc_config->models;
//     int* lengths = loc_config->lengths;
//     int shapes_and_model_count = loc_config->shapes_and_model_count;

//     struct CacheModel** models = NULL;
//     int model_count = 0;
//     int* model_ids = NULL;

//     struct CacheModel* model = NULL;

//     if( !model_id_sets )
//         return NULL;

//     if( !shapes )
//     {
//         int count = lengths[0];

//         models = malloc(sizeof(struct CacheModel) * count);
//         memset(models, 0, sizeof(struct CacheModel) * count);
//         model_ids = malloc(sizeof(int) * count);
//         memset(model_ids, 0, sizeof(int) * count);

//         for( int i = 0; i < count; i++ )
//         {
//             int model_id = model_id_sets[0][i];
//             assert(model_id);

//             model_entry = (struct ModelEntry*)hmap_search(models_hmap, &model_id, HMAP_FIND);
//             assert(model_entry);
//             model = model_entry->model;

//             assert(model);
//             models[model_count] = model;
//             model_ids[model_count] = model_id;
//             model_count++;
//         }
//     }
//     else
//     {
//         models = malloc(sizeof(struct CacheModel*) * shapes_and_model_count);
//         memset(models, 0, sizeof(struct CacheModel*) * shapes_and_model_count);
//         model_ids = malloc(sizeof(int) * shapes_and_model_count);
//         memset(model_ids, 0, sizeof(int) * shapes_and_model_count);

//         bool found = false;
//         for( int i = 0; i < shapes_and_model_count; i++ )
//         {
//             int count_inner = lengths[i];

//             int loc_type = shapes[i];
//             if( loc_type == shape_select )
//             {
//                 for( int j = 0; j < count_inner; j++ )
//                 {
//                     int model_id = model_id_sets[i][j];
//                     assert(model_id);

//                     model_entry =
//                         (struct ModelEntry*)hmap_search(models_hmap, &model_id, HMAP_FIND);
//                     assert(model_entry);
//                     model = model_entry->model;

//                     assert(model);
//                     models[model_count] = model;
//                     model_ids[model_count] = model_id;
//                     model_count++;
//                     found = true;
//                 }
//             }
//         }
//         assert(found);
//     }

//     assert(model_count > 0);

//     if( model_count > 1 )
//     {
//         model = model_new_merge(models, model_count);
//     }
//     else
//     {
//         model = model_new_copy(models[0]);
//     }

//     // printf("face textures: ");
//     // if( model->face_textures )
//     //     for( int f = 0; f < model->face_count; f++ )
//     //     {
//     //         printf("%d; ", model->face_textures[f]);
//     //     }
//     // printf("\n");

//     apply_transforms(
//         loc_config,
//         model,
//         orientation,
//         tile_heights->sw_height,
//         tile_heights->se_height,
//         tile_heights->ne_height,
//         tile_heights->nw_height);

//     struct DashModel* dash_model = NULL;
//     dash_model = dashmodel_new_from_cache_model(model);
//     model_free(model);

//     //     scene_model->light_ambient = loc_config->ambient;
//     //     scene_model->light_contrast = loc_config->contrast;

//     // light_model_default(dash_model, loc_config->contrast, loc_config->ambient);

//     // Sequences don't account for rotations, so models must be rotated AFTER the animation is
//     // applied.
//     if( loc_config->seq_id != -1 )
//     {
//         // TODO: account for transforms.
//         // Also, I believe this is the only overridden transform.
//         //         const isEntity =
//         // seqId !== -1 ||
//         // locType.transforms !== undefined ||
//         // locLoadType === LocLoadType.NO_MODELS;

//         // scene_model->yaw = 512 * orientation;
//         // scene_model->yaw %= 2048;
//         // orientation = 0;
//     }

//     // scene_model->model = model;
//     // scene_model->model_id = model_ids[0];

//     // scene_model->light_ambient = loc_config->ambient;
//     // scene_model->light_contrast = loc_config->contrast;
//     // scene_model->sharelight = loc_config->sharelight;

//     // scene_model->__loc_id = loc_config->_id;

//     // if( model->vertex_bone_map )
//     //     scene_model->vertex_bones =
//     //         modelbones_new_decode(model->vertex_bone_map, model->vertex_count);
//     // if( model->face_bone_map )
//     //     scene_model->face_bones = modelbones_new_decode(model->face_bone_map,
//     model->face_count);

//     // scene_model->sequence = NULL;
//     // if( loc_config->seq_id != -1 )
//     // {
//     //     scene_model_vertices_create_original(scene_model);

//     //     if( model->face_alphas )
//     //         scene_model_face_alphas_create_original(scene_model);

//     //     sequence = config_sequence_table_get_new(sequence_table, loc_config->seq_id);
//     //     assert(sequence);
//     //     assert(sequence->frame_lengths);
//     //     scene_model->sequence = sequence;

//     //     assert(scene_model->frames == NULL);
//     //     scene_model->frames = malloc(sizeof(struct CacheFrame*) * sequence->frame_count);
//     //     memset(scene_model->frames, 0, sizeof(struct CacheFrame*) * sequence->frame_count);

//     //     int frame_id = sequence->frame_ids[0];
//     //     int frame_archive_id = (frame_id >> 16) & 0xFFFF;
//     //     // Get the frame definition ID from the second 2 bytes of the sequence frame ID The
//     //     //     first 2 bytes are the sequence ID,
//     //     //     the second 2 bytes are the frame archive ID

//     //     frame_archive = cache_archive_new_load(cache, CACHE_ANIMATIONS, frame_archive_id);
//     //     frame_filelist = filelist_new_from_cache_archive(frame_archive);
//     //     for( int i = 0; i < sequence->frame_count; i++ )
//     //     {
//     //         assert(((sequence->frame_ids[i] >> 16) & 0xFFFF) == frame_archive_id);
//     //         // assert(i < frame_filelist->file_count);

//     //         int frame_id = sequence->frame_ids[i];
//     //         int frame_archive_id = (frame_id >> 16) & 0xFFFF;
//     //         int frame_file_id = frame_id & 0xFFFF;

//     //         assert(frame_file_id > 0);
//     //         assert(frame_file_id - 1 < frame_filelist->file_count);

//     //         char* frame_data = frame_filelist->files[frame_file_id - 1];
//     //         int frame_data_size = frame_filelist->file_sizes[frame_file_id - 1];
//     //         int framemap_id = framemap_id_from_frame_archive(frame_data, frame_data_size);

//     //         if( !scene_model->framemap )
//     //         {
//     //             framemap = framemap_new_from_cache(cache, framemap_id);
//     //             scene_model->framemap = framemap;
//     //         }

//     //         frame = frame_new_decode2(frame_id, scene_model->framemap, frame_data,
//     //         frame_data_size);

//     //         scene_model->frames[scene_model->frame_count++] = frame;
//     //     }

//     //     cache_archive_free(frame_archive);
//     //     frame_archive = NULL;
//     //     filelist_free(frame_filelist);
//     //     frame_filelist = NULL;
//     // }

//     free(models);
//     free(model_ids);

//     return dash_model;
// }

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

    buffer_size = 1024 * sizeof(struct ConfigLocEntry);
    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct ConfigLocEntry),
    };
    scene_builder->config_locs_hmap = dashmap_new(&config, 0);

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

    free(dashmap_buffer_ptr(scene_builder->config_locs_hmap));
    dashmap_free(scene_builder->config_locs_hmap);

    free(scene_builder);
}

void
scenebuilder_cache_configmap_underlay(
    struct SceneBuilder* scene_builder,
    struct DashMap* config_underlay_map)
{
    scene_builder->config_underlay_hmap = config_underlay_map;
}

void
scenebuilder_cache_configmap_overlay(
    struct SceneBuilder* scene_builder,
    struct DashMap* config_overlay_map)
{
    scene_builder->config_overlay_hmap = config_overlay_map;
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

void
scenebuilder_cache_config_loc(
    struct SceneBuilder* scene_builder,
    int config_loc_id,
    struct CacheConfigLocation* config_loc)
{
    struct ConfigLocEntry* config_loc_entry = NULL;

    config_loc_entry = (struct ConfigLocEntry*)dashmap_search(
        scene_builder->config_locs_hmap, &config_loc_id, DASHMAP_INSERT);
    assert(config_loc_entry && "Config loc must be inserted into hmap");
    config_loc_entry->id = config_loc_id;
    config_loc_entry->config_loc = config_loc;
}

static void
load_loc_model(
    struct SceneBuilder* scene_builder,
    struct TerrainGrid* terrain_grid,
    int mapx,
    int mapz,
    struct CacheMapLoc* map_loc)
{
    struct ConfigLocEntry* config_loc_entry = NULL;
}

static void
add_loc(
    struct SceneBuilder* scene_builder,
    struct TerrainGrid* terrain_grid,
    int mapx,
    int mapz,
    struct CacheMapLoc* map_loc)
{
    struct ConfigLocEntry* config_loc_entry = NULL;
    struct CacheConfigLocation* config_loc = NULL;
    struct TileHeights tile_heights;

    config_loc_entry = (struct ConfigLocEntry*)dashmap_search(
        scene_builder->config_locs_hmap, &map_loc->loc_id, DASHMAP_FIND);
    assert(config_loc_entry && "Config loc must be found in hmap");

    config_loc = config_loc_entry->config_loc;

    tile_heights_at(
        terrain_grid,
        mapx,
        mapz,
        map_loc->chunk_pos_x,
        map_loc->chunk_pos_z,
        map_loc->chunk_pos_level,
        &tile_heights);

    switch( map_loc->shape_select )
    {
    case LOC_SHAPE_WALL_SINGLE_SIDE:
    }
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
    struct DashMapIter* iter = dashmap_iter_new(scene_builder->map_terrains_hmap);
    struct Scene* scene = scene_new();

    init_terrain_grid(scene_builder, &terrain_grid);

    while( (map_locs_entry = (struct MapLocsEntry*)dashmap_iter_next(iter)) )
    {
        map_locs = map_locs_entry->locs;

        for( int i = 0; i < map_locs->locs_count; i++ )
        {
            map_loc = &map_locs->locs[i];
            // add_loc(
            //     scene_builder, &terrain_grid, map_locs_entry->mapx, map_locs_entry->mapz,
            //     map_loc);
        }
    }

    scene->terrain = build_scene_terrain(scene_builder, &terrain_grid, NULL);

    return scene;
}