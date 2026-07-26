#include "dat1_version_list.h"

#include "../archive.h"
#include "../filelist.h"
#include "../rsbuffer.h"
#include "mapsquares.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static void*
copy_bytes(
    const void* src,
    size_t size)
{
    if( size == 0 )
        return NULL;
    assert(src != NULL);
    void* copy = malloc(size);
    if( !copy )
        return NULL;
    memcpy(copy, src, size);
    return copy;
}

static char*
jag_find(
    struct RSCache_FileListDat* jag,
    const char* name,
    int* out_size)
{
    assert(jag != NULL);
    assert(name != NULL);
    assert(out_size != NULL);
    *out_size = 0;
    int idx = RSCache_FileListDatFindFileByName(jag, name);
    if( idx < 0 )
        return NULL;
    *out_size = jag->file_sizes[idx];
    return jag->files[idx];
}

static uint16_t*
decode_u16_array(
    const char* data,
    int data_size,
    int* out_count)
{
    assert(out_count != NULL);
    *out_count = 0;
    if( !data || data_size < 0 )
        return NULL;
    int count = data_size / 2;
    if( count <= 0 )
        return NULL;
    uint16_t* out = malloc((size_t)count * sizeof(uint16_t));
    if( !out )
        return NULL;
    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);
    for( int i = 0; i < count; i++ )
        out[i] = (uint16_t)g2(&buffer);
    *out_count = count;
    return out;
}

static int32_t*
decode_i32_array(
    const char* data,
    int data_size,
    int* out_count)
{
    assert(out_count != NULL);
    *out_count = 0;
    if( !data || data_size < 0 )
        return NULL;
    int count = data_size / 4;
    if( count <= 0 )
        return NULL;
    int32_t* out = malloc((size_t)count * sizeof(int32_t));
    if( !out )
        return NULL;
    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);
    for( int i = 0; i < count; i++ )
        out[i] = (int32_t)g4(&buffer);
    *out_count = count;
    return out;
}

static uint8_t*
decode_u8_array(
    const char* data,
    int data_size,
    int* out_count)
{
    assert(out_count != NULL);
    *out_count = 0;
    if( !data || data_size <= 0 )
        return NULL;
    uint8_t* out = copy_bytes(data, (size_t)data_size);
    if( !out )
        return NULL;
    *out_count = data_size;
    return out;
}

static uint8_t*
encode_u16_array(
    const uint16_t* values,
    int count,
    int* out_size)
{
    assert(out_size != NULL);
    *out_size = 0;
    if( count <= 0 )
        return NULL;
    assert(values != NULL);
    uint8_t* out = malloc((size_t)count * 2u);
    if( !out )
        return NULL;
    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, (uint32_t)count * 2u);
    for( int i = 0; i < count; i++ )
        p2(&buffer, values[i]);
    *out_size = (int)buffer.position;
    return out;
}

static uint8_t*
encode_i32_array(
    const int32_t* values,
    int count,
    int* out_size)
{
    assert(out_size != NULL);
    *out_size = 0;
    if( count <= 0 )
        return NULL;
    assert(values != NULL);
    uint8_t* out = malloc((size_t)count * 4u);
    if( !out )
        return NULL;
    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, (uint32_t)count * 4u);
    for( int i = 0; i < count; i++ )
        p4(&buffer, values[i]);
    *out_size = (int)buffer.position;
    return out;
}

static uint8_t*
encode_map_index(
    const struct RSCache_MapSquares* squares,
    int* out_size)
{
    assert(out_size != NULL);
    *out_size = 0;
    if( !squares || squares->squares_count <= 0 )
        return NULL;
    assert(squares->squares != NULL);
    int size = squares->squares_count * 7;
    uint8_t* out = malloc((size_t)size);
    if( !out )
        return NULL;
    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, (uint32_t)size);
    for( int i = 0; i < squares->squares_count; i++ )
    {
        p2(&buffer, squares->squares[i].map_id);
        p2(&buffer, squares->squares[i].terrain_archive_id);
        p2(&buffer, squares->squares[i].loc_archive_id);
        p1(&buffer, squares->squares[i].members ? 1 : 0);
    }
    *out_size = (int)buffer.position;
    return out;
}

static bool
jag_upsert(
    struct RSCache_FileListDat* jag,
    const char* name,
    uint8_t* data,
    int size)
{
    assert(jag != NULL);
    assert(name != NULL);
    assert(size == 0 || data != NULL);

    int hash = RSCache_ArchiveNameHashDat(name);
    int idx = RSCache_FileListDatFindFileByName(jag, name);
    if( idx >= 0 )
    {
        free(jag->files[idx]);
        jag->files[idx] = (char*)data;
        jag->file_sizes[idx] = size;
        jag->file_name_hashes[idx] = hash;
        return true;
    }

    int new_count = jag->file_count + 1;
    char** files = realloc(jag->files, (size_t)new_count * sizeof(char*));
    int* sizes = realloc(jag->file_sizes, (size_t)new_count * sizeof(int));
    int* hashes = realloc(jag->file_name_hashes, (size_t)new_count * sizeof(int));
    if( !files || !sizes || !hashes )
    {
        /* Leave jag pointers alone if realloc failed mid-way — caller owns data. */
        if( files )
            jag->files = files;
        if( sizes )
            jag->file_sizes = sizes;
        if( hashes )
            jag->file_name_hashes = hashes;
        return false;
    }
    jag->files = files;
    jag->file_sizes = sizes;
    jag->file_name_hashes = hashes;
    jag->files[jag->file_count] = (char*)data;
    jag->file_sizes[jag->file_count] = size;
    jag->file_name_hashes[jag->file_count] = hash;
    jag->file_count = new_count;
    return true;
}

static bool
upsert_u16(
    struct RSCache_FileListDat* jag,
    const char* name,
    const uint16_t* values,
    int count)
{
    int size = 0;
    uint8_t* encoded = encode_u16_array(values, count, &size);
    if( count > 0 && !encoded )
        return false;
    if( !jag_upsert(jag, name, encoded, size) )
    {
        free(encoded);
        return false;
    }
    return true;
}

static bool
upsert_i32(
    struct RSCache_FileListDat* jag,
    const char* name,
    const int32_t* values,
    int count)
{
    int size = 0;
    uint8_t* encoded = encode_i32_array(values, count, &size);
    if( count > 0 && !encoded )
        return false;
    if( !jag_upsert(jag, name, encoded, size) )
    {
        free(encoded);
        return false;
    }
    return true;
}

static bool
upsert_bytes(
    struct RSCache_FileListDat* jag,
    const char* name,
    const uint8_t* values,
    int count)
{
    uint8_t* encoded = NULL;
    if( count > 0 )
    {
        encoded = copy_bytes(values, (size_t)count);
        if( !encoded )
            return false;
    }
    if( !jag_upsert(jag, name, encoded, count) )
    {
        free(encoded);
        return false;
    }
    return true;
}

struct RSCache_Dat1VersionList*
RSCache_Dat1VersionListNewFromJagfile(struct RSCache_FileListDat* jag)
{
    assert(jag != NULL);

    struct RSCache_Dat1VersionList* vl = calloc(1, sizeof(*vl));
    if( !vl )
        return NULL;

    int size = 0;
    char* data = NULL;

    data = jag_find(jag, "model_version", &size);
    vl->model_version = decode_u16_array(data, size, &vl->model_version_count);
    data = jag_find(jag, "anim_version", &size);
    vl->anim_version = decode_u16_array(data, size, &vl->anim_version_count);
    data = jag_find(jag, "midi_version", &size);
    vl->midi_version = decode_u16_array(data, size, &vl->midi_version_count);
    data = jag_find(jag, "map_version", &size);
    vl->map_version = decode_u16_array(data, size, &vl->map_version_count);

    data = jag_find(jag, "model_crc", &size);
    vl->model_crc = decode_i32_array(data, size, &vl->model_crc_count);
    data = jag_find(jag, "anim_crc", &size);
    vl->anim_crc = decode_i32_array(data, size, &vl->anim_crc_count);
    data = jag_find(jag, "midi_crc", &size);
    vl->midi_crc = decode_i32_array(data, size, &vl->midi_crc_count);
    data = jag_find(jag, "map_crc", &size);
    vl->map_crc = decode_i32_array(data, size, &vl->map_crc_count);

    data = jag_find(jag, "model_index", &size);
    vl->model_index = decode_u8_array(data, size, &vl->model_index_count);
    data = jag_find(jag, "anim_index", &size);
    vl->anim_index = decode_u16_array(data, size, &vl->anim_index_count);
    data = jag_find(jag, "midi_index", &size);
    vl->midi_index = decode_u8_array(data, size, &vl->midi_index_count);

    data = jag_find(jag, "map_index", &size);
    if( data && size > 0 )
        vl->map_squares = RSCache_MapSquaresNewDecode(data, size);

    return vl;
}

uint32_t
RSCache_Dat1VersionListEncodeIntoJagfile(
    const struct RSCache_Dat1VersionList* vl,
    struct RSCache_FileListDat* jag)
{
    assert(vl != NULL);
    assert(jag != NULL);

    if( !upsert_u16(jag, "model_version", vl->model_version, vl->model_version_count) )
        return 0;
    if( !upsert_u16(jag, "anim_version", vl->anim_version, vl->anim_version_count) )
        return 0;
    if( !upsert_u16(jag, "midi_version", vl->midi_version, vl->midi_version_count) )
        return 0;
    if( !upsert_u16(jag, "map_version", vl->map_version, vl->map_version_count) )
        return 0;

    if( !upsert_i32(jag, "model_crc", vl->model_crc, vl->model_crc_count) )
        return 0;
    if( !upsert_i32(jag, "anim_crc", vl->anim_crc, vl->anim_crc_count) )
        return 0;
    if( !upsert_i32(jag, "midi_crc", vl->midi_crc, vl->midi_crc_count) )
        return 0;
    if( !upsert_i32(jag, "map_crc", vl->map_crc, vl->map_crc_count) )
        return 0;

    if( !upsert_bytes(jag, "model_index", vl->model_index, vl->model_index_count) )
        return 0;
    if( !upsert_u16(jag, "anim_index", vl->anim_index, vl->anim_index_count) )
        return 0;
    if( !upsert_bytes(jag, "midi_index", vl->midi_index, vl->midi_index_count) )
        return 0;

    if( vl->map_squares && vl->map_squares->squares_count > 0 )
    {
        int map_size = 0;
        uint8_t* map_data = encode_map_index(vl->map_squares, &map_size);
        if( !map_data )
            return 0;
        if( !jag_upsert(jag, "map_index", map_data, map_size) )
        {
            free(map_data);
            return 0;
        }
    }

    return RSCache_FileListDatEncodeBound(jag);
}

void
RSCache_Dat1VersionListFree(struct RSCache_Dat1VersionList* vl)
{
    if( !vl )
        return;
    free(vl->model_version);
    free(vl->anim_version);
    free(vl->midi_version);
    free(vl->map_version);
    free(vl->model_crc);
    free(vl->anim_crc);
    free(vl->midi_crc);
    free(vl->map_crc);
    free(vl->model_index);
    free(vl->anim_index);
    free(vl->midi_index);
    RSCache_MapSquaresFree(vl->map_squares);
    free(vl);
}
