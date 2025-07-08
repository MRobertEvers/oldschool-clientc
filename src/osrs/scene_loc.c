#include "scene_loc.h"

#include "filelist.h"
#include "tables/config_locs.h"
#include "tables/configs.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

void
load_loc_model(struct SceneLoc* scene_loc, struct Loc* loc, struct Cache* cache)
{
    struct Model* model = NULL;
    if( !loc->models )
        return;

    if( !loc->types )
    {
        int count = loc->lengths[0];

        scene_loc->models = malloc(sizeof(struct Model) * count);
        memset(scene_loc->models, 0, sizeof(struct Model) * count);
        scene_loc->model_count = count;

        for( int i = 0; i < count; i++ )
        {
            int model_id = loc->models[0][i];

            model = model_new_from_cache(cache, model_id);
            scene_loc->models[i] = model;
        }
    }
    else
    {
        // TODO:
        return;
    }
}

#define TILE_SIZE 128

struct SceneLocs*
scene_locs_new_from_map_locs(
    struct MapTerrain* terrain, struct MapLocs* map_locs, struct Cache* cache)
{
    struct ArchiveReference* archive_reference = NULL;
    struct CacheArchive* archive = NULL;
    struct FileList* filelist = NULL;
    struct Loc loc = { 0 };

    struct SceneLocs* scene_locs = malloc(sizeof(struct SceneLocs));

    scene_locs->locs = malloc(sizeof(struct SceneLoc) * map_locs->locs_count);
    memset(scene_locs->locs, 0, sizeof(struct SceneLoc) * map_locs->locs_count);
    scene_locs->locs_count = map_locs->locs_count;

    archive = cache_archive_new_load(cache, CACHE_CONFIGS, CONFIG_LOCS);
    if( !archive )
    {
        printf("Failed to load locs archive\n");
        goto error;
    }

    filelist = filelist_new_from_cache_archive(archive);

    for( int i = 0; i < map_locs->locs_count; i++ )
    {
        struct MapLoc* map_loc = &map_locs->locs[i];
        int x = map_loc->chunk_pos_x;
        int y = map_loc->chunk_pos_y;
        int z = map_loc->chunk_pos_level;

        int loc_id = map_loc->id;

        archive_reference = cache->tables[CACHE_CONFIGS]->archives;

        decode_loc(&loc, filelist->files[loc_id], filelist->file_sizes[loc_id]);

        load_loc_model(&scene_locs->locs[i], &loc, cache);

        int height_sw = terrain->tiles_xyz[MAP_TILE_COORD(x, y, z)].height;
        int height_se = terrain->tiles_xyz[MAP_TILE_COORD(x + 1, y, z)].height;
        int height_ne = terrain->tiles_xyz[MAP_TILE_COORD(x + 1, y + 1, z)].height;
        int height_nw = terrain->tiles_xyz[MAP_TILE_COORD(x, y + 1, z)].height;

        int height_center = (height_sw + height_se + height_ne + height_nw) >> 2;

        scene_locs->locs[i].region_x = x * TILE_SIZE;
        scene_locs->locs[i].region_y = y * TILE_SIZE;
        scene_locs->locs[i].region_z = height_center;

        scene_locs->locs[i].chunk_pos_x = x;
        scene_locs->locs[i].chunk_pos_y = y;
        scene_locs->locs[i].chunk_pos_level = z;

        scene_locs->locs[i].size_x = loc.size_x;
        scene_locs->locs[i].size_y = loc.size_y;
    }

done:
    filelist_free(filelist);
    cache_archive_free(archive);

    return scene_locs;

error:
    free(scene_locs->locs);
    free(scene_locs);
    return NULL;
}