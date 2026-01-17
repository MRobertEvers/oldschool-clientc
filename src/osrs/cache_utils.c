#include "cache_utils.h"

#include "filepack.h"

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

    return filelist_new_from_decode(
        file_metadata.data_ptr_, file_metadata.data_size, file_metadata.file_count);
}