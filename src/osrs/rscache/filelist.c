#include "filelist.h"

#include "rsbuf.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLAG_IDENTIFIERS 0x1
#define FLAG_WHIRLPOOL 0x2
#define FLAG_HASH 0x8

struct FileList*
filelist_new_from_cache_dat_archive(struct CacheDatArchive* archive)
{
    // const actualSize = buffer.readMedium();
    // const size = buffer.readMedium();

    // const isCompressed = actualSize !== size;

    // let dataBuffer: ByteBuffer;
    // let metaBuffer: ByteBuffer;
    // if (isCompressed) {
    //     const data = buffer.readUnsignedBytes(size);
    //     const decompressed = Bzip2.decompress(data, actualSize);
    //     dataBuffer = new ByteBuffer(decompressed);
    //     metaBuffer = new ByteBuffer(decompressed);
    // } else {
    //     dataBuffer = new ByteBuffer(data);
    //     metaBuffer = buffer;
    // }

    // fileCount = metaBuffer.readUnsignedShort();
    // dataBuffer.offset = metaBuffer.offset + fileCount * 10;

    // fileIds = new Int32Array(fileCount);
    // fileNameHashes = new Int32Array(fileCount);
    // for (let i = 0; i < fileCount; i++) {
    //     const nameHash = metaBuffer.readInt();
    //     const fileActualSize = metaBuffer.readMedium();
    //     const fileSize = metaBuffer.readMedium();

    //     let decompressedFile: Int8Array;
    //     if (isCompressed) {
    //         decompressedFile = dataBuffer.readBytes(fileSize);
    //     } else {
    //         const data = dataBuffer.readUnsignedBytes(fileSize);
    //         decompressedFile = Bzip2.decompress(data, fileActualSize);
    //     }
    //     files.set(i, new ArchiveFile(i, id, decompressedFile));
    //     fileIds[i] = i;
    //     fileNameHashes[i] = nameHash;
    // }

    struct RSBuffer buffer = { .data = archive->data, .position = 0, .size = archive->data_size };

    int actual_size = g3(&buffer);
    int size = g3(&buffer);

    bool is_compressed = actual_size != size;

    struct RSBuffer data_buffer = { .data = NULL, .position = 0, .size = 0 };
    struct RSBuffer meta_buffer = { .data = NULL, .position = 0, .size = 0 };

    if( is_compressed )
    {
        data_buffer.data = malloc(size);
        if( !data_buffer.data )
            return NULL;
        data_buffer.size = size;

        greadto(&buffer, data_buffer.data, size, size);
    }
    else
    {
        data_buffer.data = archive->data;
        data_buffer.size = archive->data_size;
    }

    meta_buffer.data = archive->data;
    meta_buffer.size = archive->data_size;
}

struct FileList*
filelist_new_from_cache_archive(struct CacheArchive* archive)
{
    return filelist_new_from_decode(archive->data, archive->data_size, archive->file_count);
}

struct FileList*
filelist_new_from_decode(
    char* data,
    int data_size,
    int num_files)
{
    struct FileList* filelist = malloc(sizeof(struct FileList));
    if( !filelist )
        return NULL;

    struct RSBuffer buffer = { .data = data, .position = 0, .size = data_size };

    filelist->files = malloc(num_files * sizeof(char*));
    filelist->file_sizes = malloc(num_files * sizeof(int));
    if( !filelist->files || !filelist->file_sizes )
    {
        free(filelist);
        return NULL;
    }
    memset(filelist->file_sizes, 0, num_files * sizeof(int));
    filelist->file_count = num_files;

    if( num_files == 1 )
    {
        /* if there is only one file, the data is the file. */
        filelist->files[0] = malloc(data_size);
        if( !filelist->files[0] )
            goto error;
        memcpy(filelist->files[0], data, data_size);
        filelist->file_sizes[0] = data_size;

        return filelist;
    }

    /* read the number of chunks at the end of the archive */
    buffer.position = buffer.size - 1;
    int chunks = g1(&buffer);
    buffer.position = 0;

    /* read the sizes of the child entries and individual chunks */
    int** chunk_sizes = malloc(chunks * sizeof(int*));
    if( !chunk_sizes )
        goto error;

    for( int i = 0; i < chunks; i++ )
    {
        chunk_sizes[i] = malloc(num_files * sizeof(int));
        if( !chunk_sizes[i] )
        {
            printf("Failed to allocate chunk sizes\n");
            goto error;
        }
    }

    int* sizes = malloc(num_files * sizeof(int));
    if( !sizes )
        goto error;
    memset(sizes, 0, num_files * sizeof(int));

    buffer.position = buffer.size - 1 - chunks * num_files * 4;
    for( int chunk = 0; chunk < chunks; chunk++ )
    {
        int chunk_size = 0;
        for( int id = 0; id < num_files; id++ )
        {
            /* read the delta-encoded chunk length */
            int delta = g4(&buffer);
            chunk_size += delta;

            /* store the size of this chunk */
            chunk_sizes[chunk][id] = chunk_size;

            /* and add it to the size of the whole file */
            sizes[id] += chunk_size;
        }
    }

    /* allocate the buffers for the child entries */
    int* file_offsets = malloc(num_files * sizeof(int));
    memset(file_offsets, 0, num_files * sizeof(int));

    for( int id = 0; id < num_files; id++ )
    {
        filelist->files[id] = malloc(sizes[id]);
        if( !filelist->files[id] )
            goto error;
    }

    /* read the data into the buffers */
    buffer.position = 0;
    for( int chunk = 0; chunk < chunks; chunk++ )
    {
        for( int id = 0; id < num_files; id++ )
        {
            /* get the length of this chunk */
            int chunk_size = chunk_sizes[chunk][id];

            /* copy this chunk into the file buffer */
            greadto(
                &buffer,
                filelist->files[id] + file_offsets[id],
                sizes[id] - file_offsets[id],
                chunk_size);
            file_offsets[id] += chunk_size;
            filelist->file_sizes[id] += chunk_size;
        }
    }

    /* cleanup */
    for( int i = 0; i < chunks; i++ )
    {
        free(chunk_sizes[i]);
    }
    free(chunk_sizes);
    free(sizes);
    free(file_offsets);

    return filelist;

error:
    if( chunk_sizes )
    {
        for( int i = 0; i < chunks; i++ )
        {
            free(chunk_sizes[i]);
        }
        free(chunk_sizes);
    }

    if( sizes )
        free(sizes);

    if( file_offsets )
        free(file_offsets);

    filelist_free(filelist);
    return NULL;
}

void
filelist_free(struct FileList* filelist)
{
    if( !filelist )
        return;

    for( int i = 0; i < filelist->file_count; i++ )
    {
        if( filelist->files[i] )
            free(filelist->files[i]);
    }

    free(filelist->files);
    free(filelist->file_sizes);
    free(filelist);
}