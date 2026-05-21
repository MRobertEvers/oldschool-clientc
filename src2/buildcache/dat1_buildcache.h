#ifndef DAT1_BUILDCACHE_H
#define DAT1_BUILDCACHE_H

#include "graphics/dash.h"
#include "osrs/rscache/filelist.h"
#include "osrs/rscache/tables/model.h"

#include <stdint.h>

struct Dat1BuildCache
{
    struct FileListDat* fromconfigtable_config_jagfile;
    struct DashMap* models_hmap;
};

struct Dat1BuildCache*
dat1_buildcache_new(void);

void
dat1_buildcache_free(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_set_fromconfigtable_config_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    struct FileListDat* fromconfigtable_config_jagfile);

void
dat1_buildcache_model_add(
    struct Dat1BuildCache* dat1_buildcache,
    int model_id,
    struct CacheModel* model);

struct CacheModel*
dat1_buildcache_model_get(
    struct Dat1BuildCache* dat1_buildcache,
    int model_id);

#endif