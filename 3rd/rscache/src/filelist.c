#include "filelist.h"

#include "archive.h"
#include "bzip.h"
#include "compression.h"
#include "rsbuffer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Split a group's payload into its member files.
 *
 * IMPORTANT: the returned `files`/`file_sizes` arrays are indexed by POSITION
 * (0..num_files-1), in the order the files appear in the group — NOT by the
 * files' actual cache IDs. Many groups happen to be 0-based and dense (config
 * tables: sequences, objs, npcs, …) so indexing by ID coincidentally works, but
 * that is not guaranteed. In particular, ANIMATION frame archives number their
 * files 1-based (1..N) and may be sparse. To fetch a file by its ID, first map
 * the ID to a position using RSCache_Dat2DiskArchive.file_ids[] (file_ids[pos] is
 * the real ID of position `pos`), then index files[pos]. Using files[id] directly
 * on such a group reads the wrong file and runs off the end for the last frame.
 * See seq_file_pos_for_id() in src/engine/dat2/task_dat2_sequence_load.c.
 */
struct RSCache_FileList*
RSCache_FileListNewFromDecode(
    char* data,
    int data_size,
    int num_files)
{
    struct RSCache_FileList* filelist = malloc(sizeof(struct RSCache_FileList));
    assert(filelist != NULL);

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, data, data_size);

    filelist->files = malloc(num_files * sizeof(char*));
    filelist->file_sizes = malloc(num_files * sizeof(int));
    if( !filelist->files || !filelist->file_sizes )
    {
        free(filelist->files);
        free(filelist->file_sizes);
        free(filelist);
        return NULL;
    }
    memset(filelist->file_sizes, 0, num_files * sizeof(int));
    filelist->file_count = num_files;

    if( num_files == 1 )
    {
        filelist->files[0] = malloc((size_t)data_size);
        if( !filelist->files[0] )
        {
            free(filelist->files);
            free(filelist->file_sizes);
            free(filelist);
            return NULL;
        }
        memcpy(filelist->files[0], data, (size_t)data_size);
        filelist->file_sizes[0] = data_size;
        return filelist;
    }

    buffer.position = buffer.size - 1;
    int chunks = g1(&buffer);
    buffer.position = 0;

    int** chunk_sizes = malloc((size_t)chunks * sizeof(int*));
    if( !chunk_sizes )
        goto error;

    for( int i = 0; i < chunks; i++ )
    {
        chunk_sizes[i] = malloc((size_t)num_files * sizeof(int));
        if( !chunk_sizes[i] )
        {
            printf("Failed to allocate chunk sizes\n");
            goto error;
        }
    }

    int* sizes = malloc((size_t)num_files * sizeof(int));
    if( !sizes )
        goto error;
    memset(sizes, 0, (size_t)num_files * sizeof(int));

    buffer.position = buffer.size - 1 - (uint32_t)chunks * (uint32_t)num_files * 4u;
    for( int chunk = 0; chunk < chunks; chunk++ )
    {
        int chunk_size = 0;
        for( int id = 0; id < num_files; id++ )
        {
            int delta = g4(&buffer);
            chunk_size += delta;
            chunk_sizes[chunk][id] = chunk_size;
            sizes[id] += chunk_size;
        }
    }

    int* file_offsets = calloc((size_t)num_files, sizeof(int));
    if( !file_offsets )
        goto error;

    for( int id = 0; id < num_files; id++ )
    {
        filelist->files[id] = malloc((size_t)sizes[id]);
        if( !filelist->files[id] )
            goto error;
    }

    buffer.position = 0;
    for( int chunk = 0; chunk < chunks; chunk++ )
    {
        for( int id = 0; id < num_files; id++ )
        {
            int chunk_size = chunk_sizes[chunk][id];
            greadto(
                &buffer,
                filelist->files[id] + file_offsets[id],
                sizes[id] - file_offsets[id],
                chunk_size);
            file_offsets[id] += chunk_size;
            filelist->file_sizes[id] += chunk_size;
        }
    }

    for( int i = 0; i < chunks; i++ )
        free(chunk_sizes[i]);
    free(chunk_sizes);
    free(sizes);
    free(file_offsets);

    return filelist;

error:
    if( chunk_sizes )
    {
        for( int i = 0; i < chunks; i++ )
            free(chunk_sizes[i]);
        free(chunk_sizes);
    }

    free(sizes);
    free(file_offsets);
    RSCache_FileListFree(filelist);
    return NULL;
}

uint32_t
RSCache_FileListEncodeBound(const struct RSCache_FileList* filelist)
{
    if( !filelist )
        return 0;

    uint32_t total = 0;
    for( int i = 0; i < filelist->file_count; i++ )
        total += (uint32_t)filelist->file_sizes[i];

    if( filelist->file_count == 1 )
        return total;

    /* One int32 delta per file for the single chunk, plus the chunk-count byte. */
    return total + (uint32_t)filelist->file_count * 4u + 1u;
}

uint32_t
RSCache_FileListEncode(
    const struct RSCache_FileList* filelist,
    uint8_t* out,
    uint32_t out_capacity)
{
    if( !filelist || !out || !filelist->files || !filelist->file_sizes )
        return 0;
    if( filelist->file_count <= 0 )
        return 0;
    if( out_capacity < RSCache_FileListEncodeBound(filelist) )
        return 0;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    /* A single-file group carries no size table: the decoder takes the whole
     * payload as file 0 rather than reading a trailer. Emitting one here would
     * append the table to the file's own bytes. */
    if( filelist->file_count == 1 )
    {
        pbuf(&buffer, (const uint8_t*)filelist->files[0], filelist->file_sizes[0]);
        return buffer.position;
    }

    for( int i = 0; i < filelist->file_count; i++ )
        pbuf(&buffer, (const uint8_t*)filelist->files[i], filelist->file_sizes[i]);

    /* Single chunk: the deltas are the successive differences of the file sizes,
     * which the decoder accumulates back into per-file chunk sizes. */
    int previous = 0;
    for( int i = 0; i < filelist->file_count; i++ )
    {
        p4(&buffer, filelist->file_sizes[i] - previous);
        previous = filelist->file_sizes[i];
    }

    p1(&buffer, 1); /* chunk count */

    return buffer.position;
}

void
RSCache_FileListFree(struct RSCache_FileList* filelist)
{
    if( !filelist )
        return;

    for( int i = 0; i < filelist->file_count; i++ )
        free(filelist->files[i]);

    free(filelist->files);
    free(filelist->file_sizes);
    free(filelist);
}

struct RSCache_FileListDat*
RSCache_FileListDatNewFromDecode(
    char* data,
    int data_size)
{
    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, data, data_size);
    struct RSCache_FileListDat* filelist = NULL;
    int actual_size = g3(&buffer);
    int size = g3(&buffer);

    bool is_compressed = actual_size != size;

    char* decompressed_archive = NULL;
    struct RSCache_Buffer data_buffer;
    struct RSCache_Buffer meta_buffer;

    if( is_compressed )
    {
        char* compressed_data = malloc(size);
        if( !compressed_data )
            return NULL;

        int bytes_read = greadto(&buffer, compressed_data, size, size);
        if( bytes_read < size )
        {
            free(compressed_data);
            return NULL;
        }

        decompressed_archive = malloc(actual_size);
        if( !decompressed_archive )
        {
            free(compressed_data);
            return NULL;
        }

        int decompressed_size = RSCache_CompressionBzipDecompress(
            (uint8_t*)decompressed_archive, actual_size, (uint8_t*)compressed_data, size);
        assert(decompressed_size == actual_size);
        free(compressed_data);

        data_buffer.data = (uint8_t*)decompressed_archive;
        data_buffer.size = (uint32_t)actual_size;
        data_buffer.position = 0;

        meta_buffer = data_buffer;
    }
    else
    {
        data_buffer.data = (uint8_t*)data;
        data_buffer.size = (uint32_t)data_size;
        data_buffer.position = buffer.position;

        meta_buffer = data_buffer;
    }

    int file_count = g2(&meta_buffer);
    data_buffer.position = meta_buffer.position + (uint32_t)file_count * 10u;

    filelist = malloc(sizeof(struct RSCache_FileListDat));
    if( !filelist )
    {
        free(decompressed_archive);
        return NULL;
    }

    filelist->files = malloc(file_count * sizeof(char*));
    filelist->file_sizes = malloc(file_count * sizeof(int));
    filelist->file_name_hashes = malloc(file_count * sizeof(int));
    if( !filelist->files || !filelist->file_sizes || !filelist->file_name_hashes )
        goto error;

    memset(filelist->files, 0, file_count * sizeof(char*));
    memset(filelist->file_sizes, 0, file_count * sizeof(int));
    memset(filelist->file_name_hashes, 0, file_count * sizeof(int));
    filelist->file_count = file_count;

    for( int i = 0; i < file_count; i++ )
    {
        int name_hash = g4(&meta_buffer);
        int file_actual_size = g3(&meta_buffer);
        int file_size = g3(&meta_buffer);

        char* file_data = NULL;

        if( is_compressed )
        {
            file_data = malloc(file_size);
            if( !file_data )
                goto error;

            int bytes_read = greadto(&data_buffer, file_data, file_size, file_size);
            if( bytes_read < file_size )
            {
                free(file_data);
                goto error;
            }
        }
        else
        {
            char* compressed_file_data = malloc(file_size);
            if( !compressed_file_data )
                goto error;

            int bytes_read = greadto(&data_buffer, compressed_file_data, file_size, file_size);
            if( bytes_read < file_size )
            {
                free(compressed_file_data);
                goto error;
            }

            file_data = malloc(file_actual_size);
            if( !file_data )
            {
                free(compressed_file_data);
                goto error;
            }

            RSCache_CompressionBzipDecompress(
                (uint8_t*)file_data, file_actual_size, (uint8_t*)compressed_file_data, file_size);
            free(compressed_file_data);
        }

        filelist->files[i] = file_data;
        filelist->file_sizes[i] = is_compressed ? file_size : file_actual_size;
        filelist->file_name_hashes[i] = name_hash;
    }

    free(decompressed_archive);
    return filelist;

error:
    if( filelist )
    {
        for( int i = 0; i < file_count; i++ )
        {
            if( filelist->files && filelist->files[i] )
                free(filelist->files[i]);
        }
        free(filelist->files);
        free(filelist->file_sizes);
        free(filelist->file_name_hashes);
        free(filelist);
    }
    free(decompressed_archive);
    return NULL;
}

void
RSCache_FileListDatFree(struct RSCache_FileListDat* filelist)
{
    if( !filelist )
        return;

    for( int i = 0; i < filelist->file_count; i++ )
        free(filelist->files[i]);

    free(filelist->files);
    free(filelist->file_sizes);
    free(filelist->file_name_hashes);
    free(filelist);
}

int
RSCache_FileListDatFindFileByName(
    struct RSCache_FileListDat* filelist,
    const char* name)
{
    int name_hash = RSCache_ArchiveNameHashDat(name);
    for( int i = 0; i < filelist->file_count; i++ )
    {
        if( filelist->file_name_hashes[i] == name_hash )
            return i;
    }
    return -1;
}

/* Patch a big-endian u24 already written into `data` at `offset`. The jagfile
 * file table records each member's compressed size, which is only known after
 * compressing it, so the column is reserved and filled in afterwards. */
static void
patch_u24(
    uint8_t* data,
    uint32_t offset,
    int value)
{
    data[offset] = (uint8_t)((value >> 16) & 0xff);
    data[offset + 1] = (uint8_t)((value >> 8) & 0xff);
    data[offset + 2] = (uint8_t)(value & 0xff);
}

uint32_t
RSCache_FileListDatEncodeBound(const struct RSCache_FileListDat* filelist)
{
    if( !filelist )
        return 0;

    uint32_t payload = 0;
    for( int i = 0; i < filelist->file_count; i++ )
        payload += (uint32_t)filelist->file_sizes[i];

    /* 6-byte archive header + 2-byte file count + 10 bytes per file entry, and
     * bzip2 can expand slightly on incompressible input, so route the payload
     * estimate through its own bound. */
    uint32_t framed = 6u + 2u + (uint32_t)filelist->file_count * 10u + payload;
    return bzip_compress_bound(framed) + framed + 64u;
}

uint32_t
RSCache_FileListDatEncode(
    const struct RSCache_FileListDat* filelist,
    bool whole_archive,
    uint8_t* out,
    uint32_t out_capacity)
{
    if( !filelist || !out || !filelist->files || !filelist->file_sizes ||
        !filelist->file_name_hashes )
        return 0;
    if( filelist->file_count < 0 )
        return 0;

    /* Build the body — file table plus payloads — then frame it. In the
     * whole-archive arrangement the body is compressed as one unit; in the
     * per-file arrangement the table is plain and each payload is compressed
     * individually, so the table's "compressed size" column is only known after
     * compressing. Hence two passes rather than one streaming write. */

    uint32_t body_capacity = 2u + (uint32_t)filelist->file_count * 10u;
    for( int i = 0; i < filelist->file_count; i++ )
        body_capacity += (uint32_t)bzip_compress_bound((uint32_t)filelist->file_sizes[i]) + 64u;

    uint8_t* body = malloc(body_capacity);
    if( !body )
        return 0;

    struct RSCache_Buffer body_buffer;
    RSCache_BufferInit(&body_buffer, body, body_capacity);

    p2(&body_buffer, filelist->file_count);

    /* Reserve the table; the per-file compressed sizes are patched in below once
     * known. Position is recorded so the patch does not have to re-derive it. */
    uint32_t table_position = body_buffer.position;
    for( int i = 0; i < filelist->file_count; i++ )
    {
        p4(&body_buffer, filelist->file_name_hashes[i]);
        p3(&body_buffer, filelist->file_sizes[i]); /* uncompressed */
        p3(&body_buffer, 0);                        /* compressed, patched below */
    }

    for( int i = 0; i < filelist->file_count; i++ )
    {
        uint32_t stored;
        if( whole_archive )
        {
            /* The outer stream compresses everything, so members go in plain. */
            pbuf(&body_buffer, (const uint8_t*)filelist->files[i], filelist->file_sizes[i]);
            stored = (uint32_t)filelist->file_sizes[i];
        }
        else
        {
            uint32_t before = body_buffer.position;
            uint32_t written = RSCache_CompressionBzipCompress(
                body + before,
                (int)(body_capacity - before),
                (const uint8_t*)filelist->files[i],
                filelist->file_sizes[i]);
            if( written == 0 && filelist->file_sizes[i] != 0 )
            {
                free(body);
                return 0;
            }
            body_buffer.position += written;
            stored = written;
        }

        /* Patch the compressed-size column for this entry. The two size columns
         * are what tell a reader which arrangement is in use, so they have to
         * agree with what was actually written. */
        patch_u24(
            body,
            table_position + (uint32_t)i * 10u + 7u,
            whole_archive ? filelist->file_sizes[i] : (int)stored);
    }

    uint32_t body_size = body_buffer.position;

    struct RSCache_Buffer out_buffer;
    RSCache_BufferInit(&out_buffer, out, out_capacity);

    if( whole_archive )
    {
        uint32_t bound = bzip_compress_bound(body_size);
        uint8_t* packed = malloc(bound);
        if( !packed )
        {
            free(body);
            return 0;
        }

        uint32_t packed_size =
            RSCache_CompressionBzipCompress(packed, (int)bound, body, (int)body_size);
        if( packed_size == 0 )
        {
            free(packed);
            free(body);
            return 0;
        }

        /* Unequal sizes are the signal for "whole archive compressed". */
        p3(&out_buffer, (int)body_size);
        p3(&out_buffer, (int)packed_size);
        pbuf(&out_buffer, packed, (int)packed_size);
        free(packed);
    }
    else
    {
        /* Equal sizes are the signal for "each file compressed separately". */
        p3(&out_buffer, (int)body_size);
        p3(&out_buffer, (int)body_size);
        pbuf(&out_buffer, body, (int)body_size);
    }

    free(body);
    return out_buffer.position;
}

struct RSCache_FileListDatIndexed*
RSCache_FileListDatIndexedNewFromDecode(
    char* index_data,
    int index_data_size,
    char* data,
    int data_size)
{
    struct RSCache_FileListDatIndexed* filelist = malloc(sizeof(struct RSCache_FileListDatIndexed));
    if( !filelist )
        return NULL;

    struct RSCache_Buffer index_buffer;
    RSCache_BufferInit(&index_buffer, (uint8_t*)index_data, (uint32_t)index_data_size);
    int index_count = g2(&index_buffer);

    filelist->offsets = malloc(index_count * sizeof(int));
    filelist->offset_count = index_count;
    if( !filelist->offsets )
    {
        free(filelist);
        return NULL;
    }

    int offset = 2;
    for( int i = 0; i < index_count; i++ )
    {
        filelist->offsets[i] = offset;

        int delta = g2(&index_buffer);
        offset += delta;
    }

    filelist->data = malloc(data_size);
    if( !filelist->data )
    {
        free(filelist->offsets);
        free(filelist);
        return NULL;
    }
    memcpy(filelist->data, data, (size_t)data_size);
    filelist->data_size = data_size;

    return filelist;
}

uint32_t
RSCache_FileListDatIndexedEncodeIndexBound(const struct RSCache_FileListDatIndexed* filelist)
{
    if( !filelist )
        return 0;
    return 2u + (uint32_t)filelist->offset_count * 2u;
}

uint32_t
RSCache_FileListDatIndexedEncodeIndex(
    const struct RSCache_FileListDatIndexed* filelist,
    uint8_t* out,
    uint32_t out_capacity)
{
    if( !filelist || !out || !filelist->offsets )
        return 0;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    if( out_capacity < RSCache_FileListDatIndexedEncodeIndexBound(filelist) )
        return 0;

    p2(&buffer, filelist->offset_count);

    for( int i = 0; i < filelist->offset_count; i++ )
    {
        /* Each entry is the record's length; the decoder accumulates them from a
         * base of 2. The final record's length is not recoverable from `offsets`
         * — nothing stores an offset past the last record — so it is taken as the
         * remainder of the .dat. That reproduces a correct index, but will differ
         * from the original entry if the source .dat carried trailing slack. */
        int length;
        if( i + 1 < filelist->offset_count )
            length = filelist->offsets[i + 1] - filelist->offsets[i];
        else
            length = filelist->data_size - filelist->offsets[i];

        if( length < 0 || length > 0xFFFF )
            return 0;
        p2(&buffer, length);
    }

    return buffer.position;
}

void
RSCache_FileListDatIndexedFree(struct RSCache_FileListDatIndexed* filelist)
{
    if( !filelist )
        return;
    free(filelist->offsets);
    free(filelist->data);
    free(filelist);
}
