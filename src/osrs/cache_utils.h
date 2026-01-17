#ifndef CACHE_UTILS_H
#define CACHE_UTILS_H

#include "rscache/cache.h"
#include "rscache/filelist.h"

struct FileList*
cu_filelist_new_from_filepack(
    void* data,
    int data_size);

#endif