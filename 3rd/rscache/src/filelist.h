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
// Refered to as "JagFile" in many rsps codebases.
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

/** ".dat" + ".idx" files inside the config table config archive. */
struct RSCache_FileListDatIndexed
{
    char* data;
    int data_size;

    int* offsets;
    int offset_count;
};

struct RSCache_FileListDatIndexed*
RSCache_FileListDatIndexedNewFromDecode(
    char* index_data,
    int index_data_size,
    char* data,
    int data_size);

void
RSCache_FileListDatIndexedFree(struct RSCache_FileListDatIndexed* filelist);

#endif
