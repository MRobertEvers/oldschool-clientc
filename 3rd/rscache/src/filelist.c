#include "filelist.h"
#include "rscache_alloc.h"

#include "archive.h"
#include "bzip.h"
#include "compression.h"
#include "rsbuffer.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rscache_log.h"
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
/*
 * The per-member decode: every member gets its own allocation and the result
 * is freed with RSCache_FileListFree -- the historical contract, and some
 * callers rely on it to steal or replace individual member pointers. The
 * shared-block decode is RSCache_FileListSplit run to its end.
 */
static struct RSCache_FileList*
filelist_new_from_decode(
    char* data,
    int data_size,
    int num_files)
{
    struct RSCache_FileList* filelist = RSCACHE_MALLOC(sizeof(struct RSCache_FileList));
    assert(filelist != NULL);

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, data, data_size);

    /*
     * Everything the error path frees is declared and nulled here.
     *
     * `files` in particular must be calloc'd: the per-file allocation loop below
     * can fail partway, and RSCache_FileListFree walks the whole array — with
     * malloc it would reach entries that were never assigned and free whatever
     * the heap left there. A malformed group is the normal way in (one produced
     * by a broken encoder, say), so this is a crash on bad input rather than on
     * memory pressure.
     */
    int** chunk_sizes = NULL;
    int* sizes = NULL;
    int* file_offsets = NULL;
    int chunks = 0;

    filelist->files = RSCACHE_CALLOC((size_t)num_files, sizeof(char*));
    filelist->file_sizes = RSCACHE_CALLOC((size_t)num_files, sizeof(int));
    if( !filelist->files || !filelist->file_sizes )
    {
        RSCACHE_FREE(filelist->files);
        RSCACHE_FREE(filelist->file_sizes);
        RSCACHE_FREE(filelist);
        return NULL;
    }
    filelist->file_count = num_files;

    if( num_files == 1 )
    {
        filelist->files[0] = RSCACHE_MALLOC((size_t)data_size > 0 ? (size_t)data_size : 1u);
        if( !filelist->files[0] )
        {
            RSCACHE_FREE(filelist->files);
            RSCACHE_FREE(filelist->file_sizes);
            RSCACHE_FREE(filelist);
            return NULL;
        }
        memcpy(filelist->files[0], data, (size_t)data_size);
        filelist->file_sizes[0] = data_size;
        return filelist;
    }

    buffer.position = buffer.size - 1;
    chunks = g1(&buffer);
    buffer.position = 0;

    /* The size table is `chunks * num_files` int32s plus the count byte, and it
     * has to fit inside the payload. A group whose trailer says otherwise is
     * malformed, and reading it would walk off the front of the buffer. */
    if( chunks <= 0 ||
        (uint64_t)chunks * (uint64_t)num_files * 4u + 1u > (uint64_t)buffer.size )
        goto error;

    chunk_sizes = RSCACHE_MALLOC((size_t)chunks * sizeof(int*));
    if( !chunk_sizes )
        goto error;

    for( int i = 0; i < chunks; i++ )
    {
        chunk_sizes[i] = RSCACHE_MALLOC((size_t)num_files * sizeof(int));
        if( !chunk_sizes[i] )
        {
            RSCACHE_LOG("Failed to allocate chunk sizes\n");
            goto error;
        }
    }

    sizes = RSCACHE_CALLOC((size_t)num_files, sizeof(int));
    if( !sizes )
        goto error;

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

    file_offsets = RSCACHE_CALLOC((size_t)num_files, sizeof(int));
    if( !file_offsets )
        goto error;

    for( int id = 0; id < num_files; id++ )
    {
        /* Deltas are signed and summed, so a malformed table can produce a
         * negative or absurd length. Refuse rather than hand malloc a size_t
         * cast of it. */
        if( sizes[id] < 0 || (uint64_t)sizes[id] > (uint64_t)buffer.size )
            goto error;
    }

    for( int id = 0; id < num_files; id++ )
    {
        filelist->files[id] = RSCACHE_MALLOC((size_t)sizes[id] ? (size_t)sizes[id] : 1u);
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
        RSCACHE_FREE(chunk_sizes[i]);
    RSCACHE_FREE(chunk_sizes);
    RSCACHE_FREE(sizes);
    RSCACHE_FREE(file_offsets);

    return filelist;

error:
    if( chunk_sizes )
    {
        for( int i = 0; i < chunks; i++ )
            RSCACHE_FREE(chunk_sizes[i]);
        RSCACHE_FREE(chunk_sizes);
    }

    RSCACHE_FREE(sizes);
    RSCACHE_FREE(file_offsets);
    RSCache_FileListFree(filelist);
    return NULL;
}

struct RSCache_FileList*
RSCache_FileListNewFromDecode(
    char* data,
    int data_size,
    int num_files)
{
    return filelist_new_from_decode(data, data_size, num_files);
}

/* The resumable split's phases, in order. */
enum
{
    SPLIT_SIZES = 1, /* the size table: chunks x members int32 deltas */
    SPLIT_CHECK,     /* every member's length, and the block they need */
    SPLIT_LAYOUT,    /* every member's place in the block */
    SPLIT_COPY,      /* the payload: chunks x members */
    SPLIT_DONE,
    SPLIT_TAKEN,
    SPLIT_FAILED,
};

static void
split_free_tables(struct RSCache_FileListSplit* split)
{
    if( split->chunk_sizes )
    {
        for( int i = 0; i < split->chunks; i++ )
            RSCACHE_FREE(split->chunk_sizes[i]);
        RSCACHE_FREE(split->chunk_sizes);
    }
    split->chunk_sizes = NULL;
    RSCACHE_FREE(split->sizes);
    split->sizes = NULL;
    RSCACHE_FREE(split->file_offsets);
    split->file_offsets = NULL;
}

static void
split_fail(struct RSCache_FileListSplit* split)
{
    split_free_tables(split);
    RSCACHE_FREE(split->blob);
    split->blob = NULL;
    if( split->filelist )
    {
        RSCACHE_FREE(split->filelist->files);
        RSCACHE_FREE(split->filelist->file_sizes);
        RSCACHE_FREE(split->filelist);
    }
    split->filelist = NULL;
    split->phase = SPLIT_FAILED;
}

int
RSCache_FileListSplitBegin(
    struct RSCache_FileListSplit* split,
    char* data,
    int data_size,
    int num_files)
{
    struct RSCache_Buffer buffer;

    assert(split);
    assert(data);
    memset(split, 0, sizeof(*split));
    split->phase = SPLIT_FAILED;
    /* A group's member count is the archive's: input, not a contract. */
    if( num_files <= 0 )
        return -1;
    split->data = data;
    split->data_size = data_size;
    split->num_files = num_files;
    split->filelist = RSCACHE_MALLOC(sizeof(struct RSCache_FileList));
    assert(split->filelist);
    split->filelist->files = RSCACHE_CALLOC((size_t)num_files, sizeof(char*));
    assert(split->filelist->files);
    split->filelist->file_sizes = RSCACHE_CALLOC((size_t)num_files, sizeof(int));
    assert(split->filelist->file_sizes);
    split->filelist->file_count = num_files;

    if( num_files == 1 )
    {
        split->blob = RSCACHE_MALLOC((size_t)data_size > 0 ? (size_t)data_size : 1u);
        assert(split->blob);
        memcpy(split->blob, data, (size_t)data_size);
        split->filelist->files[0] = split->blob;
        split->filelist->file_sizes[0] = data_size;
        split->blob_bytes = data_size > 0 ? (uint64_t)data_size : 0;
        split->phase = SPLIT_DONE;
        return 0;
    }

    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);
    buffer.position = buffer.size - 1;
    split->chunks = g1(&buffer);
    /* As filelist_new_from_decode: the size table has to fit the payload. */
    if( split->chunks <= 0 ||
        (uint64_t)split->chunks * (uint64_t)num_files * 4u + 1u > (uint64_t)buffer.size )
    {
        split->chunks = 0;
        split_fail(split);
        return -1;
    }
    split->chunk_sizes = RSCACHE_MALLOC((size_t)split->chunks * sizeof(int*));
    assert(split->chunk_sizes);
    for( int i = 0; i < split->chunks; i++ )
    {
        split->chunk_sizes[i] = RSCACHE_MALLOC((size_t)num_files * sizeof(int));
        assert(split->chunk_sizes[i]);
    }
    split->sizes = RSCACHE_CALLOC((size_t)num_files, sizeof(int));
    assert(split->sizes);
    split->position = buffer.size - 1 - (uint32_t)split->chunks * (uint32_t)num_files * 4u;
    split->phase = SPLIT_SIZES;
    return 0;
}

int
RSCache_FileListSplitStep(
    struct RSCache_FileListSplit* split,
    int entries)
{
    struct RSCache_Buffer buffer;
    int const n = split->num_files;

    assert(split);
    assert(entries > 0);
    if( split->phase == SPLIT_DONE )
        return 1;
    assert(split->phase >= SPLIT_SIZES);
    assert(split->phase <= SPLIT_COPY);
    RSCache_BufferInit(&buffer, (uint8_t*)split->data, (uint32_t)split->data_size);
    buffer.position = split->position;
    while( entries > 0 && split->phase != SPLIT_DONE )
    {
        if( split->phase == SPLIT_SIZES )
        {
            int delta = g4(&buffer);

            split->chunk_size += delta;
            split->chunk_sizes[split->chunk][split->id] = split->chunk_size;
            split->sizes[split->id] += split->chunk_size;
            if( ++split->id == n )
            {
                split->id = 0;
                split->chunk_size = 0;
                if( ++split->chunk == split->chunks )
                {
                    split->chunk = 0;
                    split->total = 0;
                    split->phase = SPLIT_CHECK;
                }
            }
        }
        else if( split->phase == SPLIT_CHECK )
        {
            /* Signed deltas summed can make any length; the members partition
             * the payload, so no length and no sum may pass it. */
            int const size = split->sizes[split->id];

            if( size < 0 || (uint64_t)size > (uint64_t)buffer.size )
            {
                split_fail(split);
                return -1;
            }
            split->total += (uint64_t)size;
            if( ++split->id == n )
            {
                if( split->total > (uint64_t)buffer.size )
                {
                    split_fail(split);
                    return -1;
                }
                split->blob = RSCACHE_MALLOC(split->total > 0 ? (size_t)split->total : 1u);
                assert(split->blob);
                split->blob_bytes = split->total;
                split->file_offsets = RSCACHE_CALLOC((size_t)n, sizeof(int));
                assert(split->file_offsets);
                split->id = 0;
                split->total = 0;
                split->phase = SPLIT_LAYOUT;
            }
        }
        else if( split->phase == SPLIT_LAYOUT )
        {
            split->filelist->files[split->id] = split->blob + split->total;
            split->total += (uint64_t)split->sizes[split->id];
            if( ++split->id == n )
            {
                split->id = 0;
                buffer.position = 0;
                split->phase = SPLIT_COPY;
            }
        }
        else
        {
            int const id = split->id;
            int const chunk_size = split->chunk_sizes[split->chunk][id];

            greadto(
                &buffer,
                split->filelist->files[id] + split->file_offsets[id],
                split->sizes[id] - split->file_offsets[id],
                chunk_size);
            split->file_offsets[id] += chunk_size;
            split->filelist->file_sizes[id] += chunk_size;
            if( ++split->id == n )
            {
                split->id = 0;
                if( ++split->chunk == split->chunks )
                {
                    split_free_tables(split);
                    split->phase = SPLIT_DONE;
                }
            }
        }
        entries--;
    }
    split->position = buffer.position;
    return split->phase == SPLIT_DONE;
}

struct RSCache_FileList*
RSCache_FileListSplitTake(
    struct RSCache_FileListSplit* split,
    char** out_blob)
{
    struct RSCache_FileList* filelist;

    assert(split);
    assert(out_blob);
    assert(split->phase == SPLIT_DONE);
    filelist = split->filelist;
    *out_blob = split->blob;
    split->filelist = NULL;
    split->blob = NULL;
    split->phase = SPLIT_TAKEN;
    return filelist;
}

void
RSCache_FileListSplitAbandon(struct RSCache_FileListSplit* split)
{
    if( !split )
        return;
    if( split->phase == SPLIT_TAKEN || split->phase == SPLIT_FAILED )
        return;
    split_fail(split);
}

struct RSCache_FileList*
RSCache_FileListNewFromDecodeShared(
    char* data,
    int data_size,
    int num_files,
    char** out_blob)
{
    struct RSCache_FileListSplit split;
    int done;

    assert(out_blob);
    *out_blob = NULL;
    /* One implementation: the split run to its end. */
    if( RSCache_FileListSplitBegin(&split, data, data_size, num_files) != 0 )
        return NULL;
    done = RSCache_FileListSplitStep(&split, INT_MAX);
    if( done < 0 )
        return NULL;
    assert(done == 1);
    return RSCache_FileListSplitTake(&split, out_blob);
}

void
RSCache_FileListFreeShared(
    struct RSCache_FileList* filelist,
    char* blob)
{
    RSCACHE_FREE(blob);
    if( !filelist )
        return;
    RSCACHE_FREE(filelist->files);
    RSCACHE_FREE(filelist->file_sizes);
    RSCACHE_FREE(filelist);
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
        RSCACHE_FREE(filelist->files[i]);

    RSCACHE_FREE(filelist->files);
    RSCACHE_FREE(filelist->file_sizes);
    RSCACHE_FREE(filelist);
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
        char* compressed_data = RSCACHE_MALLOC(size);
        if( !compressed_data )
            return NULL;

        int bytes_read = greadto(&buffer, compressed_data, size, size);
        if( bytes_read < size )
        {
            RSCACHE_FREE(compressed_data);
            return NULL;
        }

        decompressed_archive = RSCACHE_MALLOC(actual_size);
        if( !decompressed_archive )
        {
            RSCACHE_FREE(compressed_data);
            return NULL;
        }

        /* Not an assert: a corrupt container is data, and under NDEBUG the
         * assert vanished and published an uninitialised buffer. */
        int decompressed_size = RSCache_CompressionBzipDecompress(
            (uint8_t*)decompressed_archive, actual_size, (uint8_t*)compressed_data, size);
        RSCACHE_FREE(compressed_data);
        if( decompressed_size != actual_size )
        {
            RSCACHE_FREE(decompressed_archive);
            return NULL;
        }

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

    filelist = RSCACHE_MALLOC(sizeof(struct RSCache_FileListDat));
    if( !filelist )
    {
        RSCACHE_FREE(decompressed_archive);
        return NULL;
    }

    filelist->files = RSCACHE_MALLOC(file_count * sizeof(char*));
    filelist->file_sizes = RSCACHE_MALLOC(file_count * sizeof(int));
    filelist->file_name_hashes = RSCACHE_MALLOC(file_count * sizeof(int));
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
            file_data = RSCACHE_MALLOC(file_size);
            if( !file_data )
                goto error;

            int bytes_read = greadto(&data_buffer, file_data, file_size, file_size);
            if( bytes_read < file_size )
            {
                RSCACHE_FREE(file_data);
                goto error;
            }
        }
        else
        {
            char* compressed_file_data = RSCACHE_MALLOC(file_size);
            if( !compressed_file_data )
                goto error;

            int bytes_read = greadto(&data_buffer, compressed_file_data, file_size, file_size);
            if( bytes_read < file_size )
            {
                RSCACHE_FREE(compressed_file_data);
                goto error;
            }

            file_data = RSCACHE_MALLOC(file_actual_size);
            if( !file_data )
            {
                RSCACHE_FREE(compressed_file_data);
                goto error;
            }

            int file_decompressed_size = RSCache_CompressionBzipDecompress(
                (uint8_t*)file_data, file_actual_size, (uint8_t*)compressed_file_data, file_size);
            RSCACHE_FREE(compressed_file_data);
            if( file_decompressed_size != file_actual_size )
            {
                RSCACHE_FREE(file_data);
                goto error;
            }
        }

        filelist->files[i] = file_data;
        filelist->file_sizes[i] = is_compressed ? file_size : file_actual_size;
        filelist->file_name_hashes[i] = name_hash;
    }

    RSCACHE_FREE(decompressed_archive);
    return filelist;

error:
    if( filelist )
    {
        for( int i = 0; i < file_count; i++ )
        {
            if( filelist->files && filelist->files[i] )
                RSCACHE_FREE(filelist->files[i]);
        }
        RSCACHE_FREE(filelist->files);
        RSCACHE_FREE(filelist->file_sizes);
        RSCACHE_FREE(filelist->file_name_hashes);
        RSCACHE_FREE(filelist);
    }
    RSCACHE_FREE(decompressed_archive);
    return NULL;
}

void
RSCache_FileListDatFree(struct RSCache_FileListDat* filelist)
{
    if( !filelist )
        return;

    for( int i = 0; i < filelist->file_count; i++ )
        RSCACHE_FREE(filelist->files[i]);

    RSCACHE_FREE(filelist->files);
    RSCACHE_FREE(filelist->file_sizes);
    RSCACHE_FREE(filelist->file_name_hashes);
    RSCACHE_FREE(filelist);
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

    uint8_t* body = RSCACHE_MALLOC(body_capacity);
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
                RSCACHE_FREE(body);
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
        uint8_t* packed = RSCACHE_MALLOC(bound);
        if( !packed )
        {
            RSCACHE_FREE(body);
            return 0;
        }

        uint32_t packed_size =
            RSCache_CompressionBzipCompress(packed, (int)bound, body, (int)body_size);
        if( packed_size == 0 )
        {
            RSCACHE_FREE(packed);
            RSCACHE_FREE(body);
            return 0;
        }

        /* Unequal sizes are the signal for "whole archive compressed". */
        p3(&out_buffer, (int)body_size);
        p3(&out_buffer, (int)packed_size);
        pbuf(&out_buffer, packed, (int)packed_size);
        RSCACHE_FREE(packed);
    }
    else
    {
        /* Equal sizes are the signal for "each file compressed separately". */
        p3(&out_buffer, (int)body_size);
        p3(&out_buffer, (int)body_size);
        pbuf(&out_buffer, body, (int)body_size);
    }

    RSCACHE_FREE(body);
    return out_buffer.position;
}

struct RSCache_FileListDatIndexed*
RSCache_FileListDatIndexedNewFromDecode(
    char* index_data,
    int index_data_size,
    char* data,
    int data_size)
{
    struct RSCache_FileListDatIndexed* filelist = RSCACHE_MALLOC(sizeof(struct RSCache_FileListDatIndexed));
    if( !filelist )
        return NULL;

    struct RSCache_Buffer index_buffer;
    RSCache_BufferInit(&index_buffer, (uint8_t*)index_data, (uint32_t)index_data_size);
    int index_count = g2(&index_buffer);

    filelist->offsets = RSCACHE_MALLOC(index_count * sizeof(int));
    filelist->offset_count = index_count;
    if( !filelist->offsets )
    {
        RSCACHE_FREE(filelist);
        return NULL;
    }

    int offset = 2;
    for( int i = 0; i < index_count; i++ )
    {
        filelist->offsets[i] = offset;

        int delta = g2(&index_buffer);
        offset += delta;
    }

    filelist->data = RSCACHE_MALLOC(data_size);
    if( !filelist->data )
    {
        RSCACHE_FREE(filelist->offsets);
        RSCACHE_FREE(filelist);
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
    RSCACHE_FREE(filelist->offsets);
    RSCACHE_FREE(filelist->data);
    RSCACHE_FREE(filelist);
}
