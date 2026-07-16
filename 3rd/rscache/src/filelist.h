#ifndef RSCACHE_FILELIST_H
#define RSCACHE_FILELIST_H

struct RSCache_FileList
{
    char** files;
    int* file_sizes;
    int file_count;
};

struct RSCache_FileList*
RSCache_FileListNewFromDecode(
    char* data,
    int data_size,
    int num_files);

void
RSCache_FileListFree(struct RSCache_FileList* filelist);

#endif
