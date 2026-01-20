#include "cache_dat.h"

struct CacheDat
{
    char const* path;
};

struct CacheDat*
cache_dat_new(char const* path)
{
    struct CacheDat* cache_dat = malloc(sizeof(struct CacheDat));
    if( cache_dat == NULL )
        return NULL;
    cache_dat->path = strdup(path);
    return cache_dat;
}

void
cache_dat_free(struct CacheDat* cache_dat)
{
    if( cache_dat == NULL )
        return;
    free(cache_dat->path);
    free(cache_dat);
}