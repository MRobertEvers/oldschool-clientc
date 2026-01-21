#include "cache_utils.h"

#include "filepack.h"
#include "rscache/rsbuf.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct FileList*
cu_filelist_new_from_filepack(
    void* data,
    int data_size)
{
    struct FileMetadata file_metadata = { 0 };
    struct FilePack filepack = { .data = data, .data_size = data_size };
    filepack_metadata(&filepack, &file_metadata);

    assert(file_metadata.flags == 0);

    return filelist_new_from_decode(
        file_metadata.data_ptr_, file_metadata.data_size, file_metadata.file_count);
}

struct FileListDatIndexed*
cu_filelist_dat_indexed_new_from_filepack(
    void* data,
    int data_size)
{
    struct RSBuffer buffer = { .data = data, .position = 0, .size = data_size };

    int flags = g4(&buffer);
    assert(flags == 1);

    int payload_size = g4(&buffer);
    int offset_count = g4(&buffer);
    char* payload = malloc(payload_size);
    memcpy(payload, buffer.data + buffer.position, payload_size);
    buffer.position += payload_size;

    int* offsets = malloc(offset_count * sizeof(int));
    memcpy(offsets, buffer.data + buffer.position, offset_count * sizeof(int));

    struct FileListDatIndexed* filelist_indexed = malloc(sizeof(struct FileListDatIndexed));
    filelist_indexed->data = payload;
    filelist_indexed->data_size = data_size;
    filelist_indexed->offsets = offsets;
    filelist_indexed->offset_count = offset_count;
    return filelist_indexed;
}