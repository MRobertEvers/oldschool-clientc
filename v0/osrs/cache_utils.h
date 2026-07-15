#ifndef CACHE_UTILS_H
#define CACHE_UTILS_H

#include "osrs/rscache/dat2disk/dat2disk.h"
#include "osrs/rscache/shared/shared_file_list.h"

struct RSCacheShared_FileList*
cu_filelist_new_from_filepack(
    void* data,
    int data_size);

struct RSCacheShared_FileListDatIndexed*
cu_filelist_dat_indexed_new_from_filepack(
    void* data,
    int data_size);

#endif