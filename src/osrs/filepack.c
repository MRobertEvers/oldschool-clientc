#include "filepack.h"

#include "rscache/rsbuf.h"

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

    data_size += sizeof(int); // flags
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

    int flags = 0;
    memcpy((uint8_t*)packed->data + offset, &flags, sizeof(int));
    offset += sizeof(int);

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

struct FilePack*
filepack_new_from_filelist_dat_indexed(struct FileListDatIndexed* filelist_indexed)
{
    int data_size = 0;
    int offset = 0;
    int zero = 0;
    struct FilePack* packed = malloc(sizeof(struct FilePack));
    memset(packed, 0, sizeof(struct FilePack));

    data_size += sizeof(int); // flags
    data_size += sizeof(int); // data size
    data_size += sizeof(int); // offset sizes
    data_size += filelist_indexed->data_size;
    data_size += filelist_indexed->offset_count * sizeof(int);

    packed->data = malloc(data_size);
    packed->data_size = data_size;
    memset(packed->data, 0, packed->data_size);

    struct RSBuffer buffer = { .data = packed->data, .position = 0, .size = data_size };

    int flags = 1;
    p4(&buffer, flags);

    p4(&buffer, filelist_indexed->data_size);
    p4(&buffer, filelist_indexed->offset_count);

    memcpy(buffer.data + buffer.position, filelist_indexed->data, filelist_indexed->data_size);
    buffer.position += filelist_indexed->data_size;

    memcpy(
        buffer.data + buffer.position,
        filelist_indexed->offsets,
        filelist_indexed->offset_count * sizeof(int));
    buffer.position += filelist_indexed->offset_count * sizeof(int);

    return packed;
}

void
filepack_free(struct FilePack* filepack)
{
    free(filepack->data);
    free(filepack);
}

void
filepack_metadata(
    struct FilePack* filepack,
    struct FileMetadata* out)
{
    memset(out, 0, sizeof(struct FileMetadata));

    void* data = filepack->data;
    int data_size = filepack->data_size;

    int offset = 0;
    int filelist_reference_size = 0;
    int flags = 0;
    int revision = 0;
    int file_count = 0;
    int archive_id = 0;
    int table_id = 0;

    memcpy(&flags, (uint8_t*)data + offset, sizeof(int));
    offset += sizeof(int);

    out->flags = flags;
    if( flags == 1 )
        return;
    memcpy(&filelist_reference_size, (uint8_t*)data + offset, sizeof(int));
    offset += sizeof(int);
    memcpy(&revision, (uint8_t*)data + offset, sizeof(int));
    offset += sizeof(int);
    memcpy(&file_count, (uint8_t*)data + offset, sizeof(int));
    offset += sizeof(int);
    memcpy(&archive_id, (uint8_t*)data + offset, sizeof(int));
    offset += sizeof(int);
    memcpy(&table_id, (uint8_t*)data + offset, sizeof(int));
    offset += sizeof(int);

    out->filelist_reference_ptr_ = (uint8_t*)filepack->data + offset;
    out->filelist_reference_size = filelist_reference_size;
    offset += filelist_reference_size;

    out->data_ptr_ = (uint8_t*)filepack->data + offset;
    out->data_size = filepack->data_size - offset;

    out->revision = revision;
    out->file_count = file_count;
    out->archive_id = archive_id;
    out->table_id = table_id;
}