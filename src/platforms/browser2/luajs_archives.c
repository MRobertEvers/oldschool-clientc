#include "luajs_archives.h"
#include "luajs_emscripten_compat.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define LUAJS_ARCHIVE_HEADER_SIZE (7 * sizeof(int))

/* Explicit little-endian encoding for 32-bit integers. */
static void
write_int(
    uint8_t* p,
    int value)
{
    uint32_t v = (uint32_t)value;
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static int
read_int(const uint8_t* p)
{
    uint32_t v = 0;
    v |= (uint32_t)p[0];
    v |= (uint32_t)p[1] << 8;
    v |= (uint32_t)p[2] << 16;
    v |= (uint32_t)p[3] << 24;
    return (int)v;
}

EMSCRIPTEN_KEEPALIVE
int
luajs_CacheDatArchive_serialized_size(const struct CacheDatArchive* archive)
{
    if( !archive )
        return -1;
    return LUAJS_ARCHIVE_HEADER_SIZE + (archive->data_size >= 0 ? (size_t)archive->data_size : 0);
}

EMSCRIPTEN_KEEPALIVE
int
luajs_CacheDatArchive_serialize_to_buffer(
    const struct CacheDatArchive* archive,
    void* buffer,
    int size)
{
    if( !archive || !buffer || size < 0 )
        return -1;

    int need = luajs_CacheDatArchive_serialized_size(archive);
    if( need < 0 || size < need )
        return -1;

    uint8_t* p = (uint8_t*)buffer;
    int data_size = archive->data_size >= 0 ? archive->data_size : 0;

    write_int(p + 0, need);                  /* total_size */
    write_int(p + 4, data_size);             /* data_size */
    write_int(p + 8, archive->archive_id);   /* archive_id */
    write_int(p + 12, archive->table_id);    /* table_id */
    write_int(p + 16, archive->revision);    /* revision */
    write_int(p + 20, archive->file_count);  /* file_count */
    write_int(p + 24, (int)archive->format); /* format */

    if( data_size > 0 && archive->data )
        memcpy(p + LUAJS_ARCHIVE_HEADER_SIZE, archive->data, (size_t)data_size);

    return need;
}

EMSCRIPTEN_KEEPALIVE
struct CacheDatArchive*
luajs_CacheDatArchive_deserialize(
    const void* buffer,
    int size)
{
    if( !buffer || size < LUAJS_ARCHIVE_HEADER_SIZE )
        return NULL;

    const uint8_t* p = (const uint8_t*)buffer;
    int total_size = read_int(p + 0);
    int data_size = read_int(p + 4);

    if( total_size < LUAJS_ARCHIVE_HEADER_SIZE ||
        total_size != LUAJS_ARCHIVE_HEADER_SIZE + data_size )
        return NULL;
    if( size < total_size )
        return NULL;
    if( data_size < 0 )
        return NULL;

    struct CacheDatArchive* archive =
        (struct CacheDatArchive*)malloc(sizeof(struct CacheDatArchive));
    if( !archive )
        return NULL;
    memset(archive, 0, sizeof(struct CacheDatArchive));

    archive->data_size = data_size;
    archive->archive_id = read_int(p + 8);
    archive->table_id = read_int(p + 12);
    archive->revision = read_int(p + 16);
    archive->file_count = read_int(p + 20);
    archive->format = (enum ArchiveFormat)read_int(p + 24);

    if( data_size > 0 )
    {
        archive->data = (char*)malloc((size_t)data_size);
        if( !archive->data )
        {
            free(archive);
            return NULL;
        }
        memcpy(archive->data, p + LUAJS_ARCHIVE_HEADER_SIZE, (size_t)data_size);
    }
    else
        archive->data = NULL;

    return archive;
}

EMSCRIPTEN_KEEPALIVE
void
luajs_CacheDatArchive_free(struct CacheDatArchive* archive)
{
    if( !archive )
        return;
    if( archive->data )
        free(archive->data);
    free(archive);
}
