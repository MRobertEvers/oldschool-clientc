#include "dat1_buildcache.h"

#include "graphics/dash.h"

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

    free(dashmap_buffer_ptr(dat1_buildcache->models_hmap));
    dashmap_free(dat1_buildcache->models_hmap);

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
dat1_buildcache_models_add(
    struct Dat1BuildCache* dat1_buildcache,
    int model_id,
    struct CacheModel* model)
{
    struct MapEntry_CacheModel* entry = (struct MapEntry_CacheModel*)dashmap_search(
        dat1_buildcache->models_hmap, model_id, DASHMAP_INSERT);
    if( !entry )
        return;

    entry->id = model_id;
    entry->model = model;
}