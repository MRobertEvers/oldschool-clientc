#include "world.h"

#include "cache.h"
#include "filelist.h"
#include "tables/config_locs.h"
#include "tables/configs.h"
#include "tables/maps.h"

#include <assert.h>
#include <stdlib.h>

const int ROTATION_WALL_TYPE[] = { 1, 2, 4, 8 };

// getLocModelData
static void
select_loc_models(
    struct WorldModel* world_model,
    struct CacheConfigLocation* loc,
    int shape_select,
    int orientation,
    int yaw)
{
    memset(world_model, 0x00, sizeof(struct WorldModel));

    world_model->offset_x = loc->offset_x;
    world_model->offset_y = loc->offset_y;
    world_model->offset_z = loc->offset_height;

    world_model->yaw_r2pi2048 = yaw;
    world_model->yaw_orientation = orientation;

    int length = 0;
    if( !loc->shapes )
    {
        int count = loc->lengths[0];
        assert(count < WORLD_MODEL_MAX_COUNT);

        for( int i = 0; i < count; i++ )
        {
            assert(loc->models);
            assert(loc->models[0]);
            int model_id = loc->models[0][i];

            world_model->models[length++] = model_id;
        }
    }
    else
    {
        int count = loc->shapes_and_model_count;

        for( int i = 0; i < count; i++ )
        {
            int count_inner = loc->lengths[i];

            int loc_type = loc->shapes[i];
            if( loc_type == shape_select )
            {
                for( int j = 0; j < count_inner; j++ )
                {
                    int model_id = loc->models[i][j];

                    assert(length < WORLD_MODEL_MAX_COUNT);
                    world_model->models[length++] = model_id;
                }

                goto selected;
            }
        }
    selected:;
    }
}

static struct WorldModel*
world_model_back(struct World* world)
{
    assert(world->world_model_count > 0);
    return &world->world_models[world->world_model_count - 1];
}

static int
world_model_emplace(struct World* world)
{
    if( world->world_model_count >= world->world_model_capacity )
    {
        world->world_model_capacity *= 2;
        world->world_models =
            realloc(world->world_models, sizeof(struct WorldModel) * world->world_model_capacity);
    }

    return world->world_model_count++;
}

struct World*
world_new_from_cache(struct Cache* cache, int chunk_x, int chunk_y)
{
    struct World* world = NULL;
    world = malloc(sizeof(struct World));
    memset(world, 0x00, sizeof(struct World));

    world->tiles = malloc(sizeof(struct WorldTile) * MAP_TILE_COUNT);
    memset(world->tiles, 0x00, sizeof(struct WorldTile) * MAP_TILE_COUNT);

    world->world_models = malloc(sizeof(struct WorldModel) * 128);
    memset(world->world_models, 0x00, sizeof(struct WorldModel) * 128);

    struct ArchiveReference* archive_reference = NULL;
    struct CacheArchive* archive = NULL;
    struct FileList* filelist = NULL;
    struct WorldTile* tile = NULL;
    struct CacheMapLocs* map_locs = NULL;
    struct CacheMapLoc* map = NULL;
    struct WorldModel* world_model = NULL;
    struct CacheMapTerrain* map_terrain = NULL;
    struct CacheMapFloor* map_floor = NULL;

    struct Wall* wall = NULL;
    struct NormalScenery* scenery = NULL;
    struct Floor* floor = NULL;

    struct CacheConfigLocation loc = { 0 };

    world = calloc(1, sizeof(struct World));
    if( !world )
    {
        printf("Failed to allocate world\n");
        goto error;
    }

    world->x_width = MAP_TERRAIN_X;
    world->z_width = MAP_TERRAIN_Y;

    for( int level = 0; level < MAP_TERRAIN_Z; level++ )
    {
        for( int x = 0; x < MAP_TERRAIN_X; x++ )
        {
            for( int y = 0; y < MAP_TERRAIN_Y; y++ )
            {
                tile = &world->tiles[MAP_TILE_COORD(x, y, level)];
                tile->x = x;
                tile->z = y;
                tile->level = level;
            }
        }
    }

    /**
     * Load Map Floors
     *
     */
    map_terrain = map_terrain_new_from_cache(cache, chunk_x, chunk_y);
    if( !map_terrain )
    {
        printf("Failed to load map terrain\n");
        goto error;
    }

    for( int i = 0; i < MAP_TILE_COUNT; i++ )
    {
        tile = &world->tiles[i];

        map_floor = &map_terrain->tiles_xyz[i];

        floor = (struct Floor*)malloc(sizeof(struct Floor));
        memset(floor, 0x00, sizeof(struct Floor));

        floor->height = map_floor->height;
        floor->attr_opcode = map_floor->attr_opcode;
        floor->settings = map_floor->settings;
        floor->overlay_id = map_floor->overlay_id;
        floor->shape = map_floor->shape;
        floor->rotation = map_floor->rotation;
        floor->underlay_id = map_floor->underlay_id;

        tile->floor = floor;
    }

    map_terrain_free(map_terrain);

    /**
     * Load Map Scenery
     *
     */

    map_locs = map_locs_new_from_cache(cache, chunk_x, chunk_y);
    if( !map_locs )
    {
        printf("Failed to load map locs\n");
        goto error;
    }

    archive = cache_archive_new_load(cache, CACHE_CONFIGS, CONFIG_LOCS);
    if( !archive )
    {
        printf("Failed to load locs archive\n");
        goto error;
    }

    filelist = filelist_new_from_cache_archive(archive);
    if( !filelist )
    {
        printf("Failed to load locs files\n");
        goto error;
    }

    for( int i = 0; i < map_locs->locs_count; i++ )
    {
        map = &map_locs->locs[i];

        tile =
            &world->tiles[MAP_TILE_COORD(map->chunk_pos_x, map->chunk_pos_y, map->chunk_pos_level)];

        tile->x = map->chunk_pos_x;
        tile->z = map->chunk_pos_y;
        tile->level = map->chunk_pos_level;

        int loc_id = map->loc_id;
        decode_loc(&loc, filelist->files[loc_id], filelist->file_sizes[loc_id]);

        switch( map->shape_select )
        {
        case LOC_SHAPE_WALL_SINGLE_SIDE:
        {
            wall = (struct Wall*)malloc(sizeof(struct Wall));
            memset(wall, 0x00, sizeof(struct Wall));

            int world_model_id = world_model_emplace(world);
            select_loc_models(
                world_model_back(world), &loc, LOC_SHAPE_WALL_SINGLE_SIDE, map->orientation, 0);

            wall->wmodel = world_model_id;
            wall->wall_type = ROTATION_WALL_TYPE[map->orientation];

            tile->wall = wall;
        }
        break;
        case LOC_SHAPE_WALL_TRI_CORNER:
        {
        }
        break;
        case LOC_SHAPE_WALL_TWO_SIDES:
        {
        }
        break;
        case LOC_SHAPE_WALL_RECT_CORNER:
        {
        }
        break;
        case LOC_SHAPE_WALL_DECORATION_NOOFFSET:
        {
        }
        break;
        case LOC_SHAPE_WALL_DECORATION_OFFSET:
        {
        }
        break;
        case LOC_SHAPE_WALL_DECORATION_DIAGONAL_OUTSIDE:
        {
        }
        break;
        case LOC_SHAPE_WALL_DECORATION_DIAGONAL_INSIDE:
        {
        }
        break;
        case LOC_SHAPE_WALL_DECORATION_DIAGONAL_DOUBLE:
        {
        }
        break;
        case LOC_SHAPE_WALL_DIAGONAL:
        {
        }
        break;
        case LOC_SHAPE_NORMAL:
        case LOC_SHAPE_NORMAL_DIAGIONAL:
        {
            scenery = (struct NormalScenery*)malloc(sizeof(struct NormalScenery));
            memset(scenery, 0x00, sizeof(struct NormalScenery));

            int yaw = 0;
            if( map->shape_select == LOC_SHAPE_NORMAL_DIAGIONAL )
                yaw = 256;

            int world_model_id = world_model_emplace(world);
            select_loc_models(
                world_model_back(world), &loc, LOC_SHAPE_NORMAL, map->orientation, yaw);

            scenery->wmodel = world_model_id;
            scenery->span_x = loc.size_x;
            scenery->span_z = loc.size_y;
            if( map->orientation == 1 || map->orientation == 3 )
            {
                scenery->span_x = loc.size_y;
                scenery->span_z = loc.size_x;
            }

            assert(tile->normals_length < WORLD_NORMAL_MAX_COUNT);
            tile->normals[tile->normals_length++] = scenery;
        }
        break;
        case LOC_SHAPE_ROOF_SLOPED:
        {
        }
        break;
        case LOC_SHAPE_ROOF_SLOPED_OUTER_CORNER:
        {
        }
        break;
        case LOC_SHAPE_ROOF_SLOPED_INNER_CORNER:
        {
        }
        break;
        case LOC_SHAPE_ROOF_SLOPED_HARD_INNER_CORNER:
        {
        }
        break;
        case LOC_SHAPE_ROOF_SLOPED_HARD_OUTER_CORNER:
        {
        }
        break;
        case LOC_SHAPE_ROOF_FLAT:
        {
        }
        break;
        case LOC_SHAPE_ROOF_SLOPED_OVERHANG:
        {
        }
        break;
        case LOC_SHAPE_ROOF_SLOPED_OVERHANG_OUTER_CORNER:
        {
        }
        break;
        case LOC_SHAPE_ROOF_SLOPED_OVERHANG_INNER_CORNER:
        {
        }
        break;
        case LOC_SHAPE_ROOF_SLOPED_OVERHANG_HARD_OUTER_CORNER:
        {
        }
        break;
        case LOC_SHAPE_FLOOR_DECORATION:
        {
        }
        break;
        }
    }

    map_locs_free(map_locs);
    cache_archive_free(archive);
    filelist_free(filelist);

error:
    return world;
}

void
world_free(struct World* world)
{
    free(world);
}