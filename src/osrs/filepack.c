#include "filepack.h"

#include <stdlib.h>
#include <string.h>

struct FilePack*
filepack_new(
    struct Cache* cache,
    struct CacheArchive* archive)
{
    int data_size = 0;
    int offset = 0;
    int filelist_reference_size = 0;
    struct ArchiveReference* archive_reference = NULL;
    struct FilePack* packed = malloc(sizeof(struct FilePack));
    memset(packed, 0, sizeof(struct FilePack));

    archive_reference = &cache->tables[archive->table_id]->archives[archive->archive_id];
    filelist_reference_size =
        archive_reference->children.count * sizeof(archive_reference->children.files[0]);

    data_size += sizeof(int); // filelist reference size
    data_size += sizeof(int); // revision
    data_size += sizeof(int); // file count
    data_size += sizeof(int); // archive id
    data_size += sizeof(int); // table id
    data_size += filelist_reference_size;
    data_size += archive->data_size;

    packed->data = malloc(data_size);
    packed->data_size = data_size;
    memset(packed->data, 0, packed->data_size);

    memcpy((uint8_t*)packed->data + offset, &filelist_reference_size, sizeof(int));
    offset += sizeof(int);
    memcpy((uint8_t*)packed->data + offset, &archive_reference->version, sizeof(int));
    offset += sizeof(int);
    memcpy((uint8_t*)packed->data + offset, &archive_reference->children.count, sizeof(int));
    offset += sizeof(int);
    memcpy((uint8_t*)packed->data + offset, &archive->archive_id, sizeof(int));
    offset += sizeof(int);
    memcpy((uint8_t*)packed->data + offset, &archive->table_id, sizeof(int));
    offset += sizeof(int);
    memcpy(
        (uint8_t*)packed->data + offset,
        archive_reference->children.files,
        filelist_reference_size);
    offset += filelist_reference_size;
    memcpy((uint8_t*)packed->data + offset, archive->data, archive->data_size);
    offset += archive->data_size;

    return packed;
}

void
filepack_free(struct FilePack* filepack)
{
    free(filepack->data);
    free(filepack);
}