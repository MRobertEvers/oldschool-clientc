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

struct RSCache_FileListDat
{
    char** files;
    int* file_sizes;
    int* file_name_hashes;
    int file_count;
};

struct RSCache_FileListDat*
RSCache_FileListDatNewFromDecode(
    char* data,
    int data_size);

void
RSCache_FileListDatFree(struct RSCache_FileListDat* filelist);

int
RSCache_FileListDatFindFileByName(
    struct RSCache_FileListDat* filelist,
    const char* name);

#endif
