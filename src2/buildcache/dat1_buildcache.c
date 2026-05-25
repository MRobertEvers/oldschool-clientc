#include "dat1_buildcache.h"

#include "graphics/dash.h"
#include "toridraw/toridraw_model.h"

#include <stdlib.h>
#include <string.h>

struct MapEntry_CacheModel
{
    int id;
    struct CacheModel* model;
};

struct Dat1BuildCache*
dat1_buildcache_new(void)
{
    struct Dat1BuildCache* dat1_buildcache = malloc(sizeof(struct Dat1BuildCache));
    memset(dat1_buildcache, 0, sizeof(struct Dat1BuildCache));

    struct DashMapConfig config = { 0 };
    int buffer_size = dashmap_buffer_size_for(sizeof(struct MapEntry_CacheModel), 1024);
    config = (struct DashMapConfig){
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct MapEntry_CacheModel),
    };
    dat1_buildcache->models_hmap = dashmap_new(&config, 0);

    return dat1_buildcache;
}

void
dat1_buildcache_free(struct Dat1BuildCache* dat1_buildcache)
{
    if( !dat1_buildcache )
        return;

    if( dat1_buildcache->models_hmap )
    {
        struct DashMapIter* iter = dashmap_iter_new(dat1_buildcache->models_hmap);
        struct MapEntry_CacheModel* entry = NULL;
        while( (entry = (struct MapEntry_CacheModel*)dashmap_iter_next(iter)) )
        {
            if( entry->model )
                model_free(entry->model);
        }
        dashmap_iter_free(iter);

        free(dashmap_buffer_ptr(dat1_buildcache->models_hmap));
        dashmap_free(dat1_buildcache->models_hmap);
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
    struct MapEntry_CacheModel* entry = (struct MapEntry_CacheModel*)dashmap_search(
        dat1_buildcache->models_hmap, &model_id, DASHMAP_INSERT);
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
    struct MapEntry_CacheModel* entry = (struct MapEntry_CacheModel*)dashmap_search(
        dat1_buildcache->models_hmap, &model_id, DASHMAP_FIND);
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

    struct MapEntry_CacheModel* entry = (struct MapEntry_CacheModel*)dashmap_search(
        dat1_buildcache->models_hmap, &model_id, DASHMAP_REMOVE);
    if( !entry || !entry->model )
        return;

    model_free(entry->model);
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