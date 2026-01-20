#ifndef CACHE_DAT_H
#define CACHE_DAT_H

#include "cache.h"

struct CacheDat;

enum CacheDatTable
{
    CACHE_DAT_CONFIGS = 0,
    CACHE_DAT_MODELS = 1,
    CACHE_DAT_ANIMATIONS = 2,
    CACHE_DAT_SOUNDS = 3,
    CACHE_DAT_MAPS = 4,
};

struct CacheDat*
cache_dat_new(char const* path);

void
cache_dat_free(struct CacheDat* cache_dat);

#endif