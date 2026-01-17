#ifndef FILEPACK_H
#define FILEPACK_H

#include "rscache/cache.h"

struct FilePack
{
    void* data;
    int data_size;
};

struct FilePack*
filepack_new(
    struct Cache* cache,
    struct CacheArchive* archive);

void
filepack_free(struct FilePack* filepack);

#endif