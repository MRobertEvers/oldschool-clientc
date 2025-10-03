#ifndef ASSETS_H
#define ASSETS_H

#include "cache.h"

struct Assets;

struct Assets* assets_new_inet(char const* ip, int port);
void assets_free(struct Assets* assets);

struct CacheArchive* assets_cache_archive_new_load(
    struct Assets* assets, struct Cache* cache, int table_id, int archive_id);

#endif