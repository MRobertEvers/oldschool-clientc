#include "dat1_buildcache.h"

#include "osrs/rscache/tables/maps.h"
#include "toridraw/toridraw_map.h"
#include "toridraw/toridraw_model.h"

#include <stdlib.h>
#include <string.h>

struct MapEntry_CacheModel
{
    int id;
    struct CacheModel* model;
};

struct MapEntry_Terrain
{
    int id;
    struct CacheMapTerrain* terrain;
};

struct MapEntry_Scenery
{
    int id;
    struct CacheMapLocs* locs;
};

static struct ToriDraw_Map*
dat1_buildcache_map_new(
    int entry_size,
    int capacity)
{
    int buffer_size = toridraw_map_buffer_size_for(entry_size, capacity);
    struct ToriDraw_MapConfig config = {
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = entry_size,
    };
    return toridraw_map_new(&config, 0);
}

struct Dat1BuildCache*
dat1_buildcache_new(void)
{
    struct Dat1BuildCache* dat1_buildcache = malloc(sizeof(struct Dat1BuildCache));
    memset(dat1_buildcache, 0, sizeof(struct Dat1BuildCache));

    dat1_buildcache->models_hmap =
        dat1_buildcache_map_new(sizeof(struct MapEntry_CacheModel), 1024);
    dat1_buildcache->map_terrain_hmap =
        dat1_buildcache_map_new(sizeof(struct MapEntry_Terrain), 256);
    dat1_buildcache->map_scenery_hmap =
        dat1_buildcache_map_new(sizeof(struct MapEntry_Scenery), 256);

    return dat1_buildcache;
}

void
dat1_buildcache_free(struct Dat1BuildCache* dat1_buildcache)
{
    if( !dat1_buildcache )
        return;

    if( dat1_buildcache->models_hmap )
    {
        struct ToriDraw_MapIter* iter = toridraw_map_iter_new(dat1_buildcache->models_hmap);
        struct MapEntry_CacheModel* entry = NULL;
        while( (entry = (struct MapEntry_CacheModel*)toridraw_map_iter_next(iter)) )
        {
            if( entry->model )
                model_free(entry->model);
        }
        toridraw_map_iter_free(iter);

        free(toridraw_map_buffer_ptr(dat1_buildcache->models_hmap));
        toridraw_map_free(dat1_buildcache->models_hmap);
    }

    if( dat1_buildcache->map_terrain_hmap )
    {
        struct ToriDraw_MapIter* iter = toridraw_map_iter_new(dat1_buildcache->map_terrain_hmap);
        struct MapEntry_Terrain* entry = NULL;
        while( (entry = (struct MapEntry_Terrain*)toridraw_map_iter_next(iter)) )
        {
            if( entry->terrain )
                map_terrain_free(entry->terrain);
        }
        toridraw_map_iter_free(iter);

        free(toridraw_map_buffer_ptr(dat1_buildcache->map_terrain_hmap));
        toridraw_map_free(dat1_buildcache->map_terrain_hmap);
    }

    if( dat1_buildcache->map_scenery_hmap )
    {
        struct ToriDraw_MapIter* iter = toridraw_map_iter_new(dat1_buildcache->map_scenery_hmap);
        struct MapEntry_Scenery* entry = NULL;
        while( (entry = (struct MapEntry_Scenery*)toridraw_map_iter_next(iter)) )
        {
            if( entry->locs )
                map_locs_free(entry->locs);
        }
        toridraw_map_iter_free(iter);

        free(toridraw_map_buffer_ptr(dat1_buildcache->map_scenery_hmap));
        toridraw_map_free(dat1_buildcache->map_scenery_hmap);
    }

    dat1_buildcache_texture_clear(dat1_buildcache);

    free(dat1_buildcache);
}

void
dat1_buildcache_set_fromconfigtable_config_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    struct FileListDat* fromconfigtable_config_jagfile)
{
    if( dat1_buildcache->fromconfigtable_config_jagfile )
        filelist_dat_free(dat1_buildcache->fromconfigtable_config_jagfile);

    dat1_buildcache->fromconfigtable_config_jagfile = fromconfigtable_config_jagfile;
}

void
dat1_buildcache_model_add(
    struct Dat1BuildCache* dat1_buildcache,
    int model_id,
    struct CacheModel* model)
{
    struct MapEntry_CacheModel* entry = (struct MapEntry_CacheModel*)toridraw_map_search(
        dat1_buildcache->models_hmap, &model_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    entry->id = model_id;
    entry->model = model;
}

struct CacheModel*
dat1_buildcache_model_get(
    struct Dat1BuildCache* dat1_buildcache,
    int model_id)
{
    struct MapEntry_CacheModel* entry = (struct MapEntry_CacheModel*)toridraw_map_search(
        dat1_buildcache->models_hmap, &model_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->model;
}

void
dat1_buildcache_model_remove(
    struct Dat1BuildCache* dat1_buildcache,
    int model_id)
{
    if( !dat1_buildcache )
        return;

    struct MapEntry_CacheModel* entry = (struct MapEntry_CacheModel*)toridraw_map_search(
        dat1_buildcache->models_hmap, &model_id, TORIDRAW_MAP_REMOVE);
    if( !entry || !entry->model )
        return;

    model_free(entry->model);
}

void
dat1_buildcache_map_terrain_add(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id,
    struct CacheMapTerrain* terrain)
{
    struct MapEntry_Terrain* entry = (struct MapEntry_Terrain*)toridraw_map_search(
        dat1_buildcache->map_terrain_hmap, &map_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    entry->id = map_id;
    entry->terrain = terrain;
}

void
dat1_buildcache_map_scenery_add(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id,
    struct CacheMapLocs* locs)
{
    struct MapEntry_Scenery* entry = (struct MapEntry_Scenery*)toridraw_map_search(
        dat1_buildcache->map_scenery_hmap, &map_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;

    entry->id = map_id;
    entry->locs = locs;
}

void
dat1_buildcache_texture_set(
    struct Dat1BuildCache* dat1_buildcache,
    int index,
    struct ToriDraw_Texture* texture)
{
    if( !dat1_buildcache || index < 0 || index >= DAT1_TEXTURE_COUNT )
        return;

    if( dat1_buildcache->textures[index] )
    {
        toridraw_texture_free(dat1_buildcache->textures[index]);
        dat1_buildcache->textures[index] = NULL;
    }

    dat1_buildcache->textures[index] = texture;
    if( index >= dat1_buildcache->texture_count )
        dat1_buildcache->texture_count = index + 1;
}

struct ToriDraw_Texture*
dat1_buildcache_texture_get(
    struct Dat1BuildCache* dat1_buildcache,
    int index)
{
    if( !dat1_buildcache || index < 0 || index >= DAT1_TEXTURE_COUNT )
        return NULL;
    return dat1_buildcache->textures[index];
}

void
dat1_buildcache_texture_clear(struct Dat1BuildCache* dat1_buildcache)
{
    if( !dat1_buildcache )
        return;

    for( int i = 0; i < DAT1_TEXTURE_COUNT; i++ )
    {
        if( dat1_buildcache->textures[i] )
            toridraw_texture_free(dat1_buildcache->textures[i]);
        dat1_buildcache->textures[i] = NULL;
    }
    dat1_buildcache->texture_count = 0;
}
