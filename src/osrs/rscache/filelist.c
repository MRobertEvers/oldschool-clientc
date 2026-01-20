#include "filelist.h"

#include "archive.h"
#include "compression.h"
#include "rsbuf.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLAG_IDENTIFIERS 0x1
#define FLAG_WHIRLPOOL 0x2
#define FLAG_HASH 0x8

struct FileListDat*
filelist_dat_new_from_cache_dat_archive(struct CacheDatArchive* archive)
{
    return filelist_dat_new_from_decode(archive->data, archive->data_size);
}

struct FileListDat*
filelist_dat_new_from_decode(
    char* data,
    int data_size)
{
    struct RSBuffer buffer = { .data = (int8_t*)data, .position = 0, .size = data_size };
    struct FileListDat* filelist = NULL;
    int actual_size = g3(&buffer);
    int size = g3(&buffer);

    bool is_compressed = actual_size != size;

    char* decompressed_archive = NULL;
    struct RSBuffer data_buffer;
    struct RSBuffer meta_buffer;

    if( is_compressed )
    {
        // Read compressed data
        char* compressed_data = malloc(size);
        if( !compressed_data )
            return NULL;

        int bytes_read = greadto(&buffer, compressed_data, size, size);
        if( bytes_read < size )
        {
            free(compressed_data);
            return NULL;
        }

        // Decompress the archive
        decompressed_archive = malloc(actual_size);
        if( !decompressed_archive )
        {
            free(compressed_data);
            return NULL;
        }

        int decompressed_size = cache_bzip_decompress(
            (uint8_t*)decompressed_archive, actual_size, (uint8_t*)compressed_data, size);
        assert(decompressed_size == actual_size);
        free(compressed_data);

        // Both buffers point to the decompressed data
        data_buffer.data = (int8_t*)decompressed_archive;
        data_buffer.size = actual_size;
        data_buffer.position = 0;

        meta_buffer.data = (int8_t*)decompressed_archive;
        meta_buffer.size = actual_size;
        meta_buffer.position = 0;
    }
    else
    {
        // Not compressed, use original data
        data_buffer.data = (int8_t*)data;
        data_buffer.size = data_size;
        data_buffer.position = buffer.position;

        meta_buffer.data = (int8_t*)data;
        meta_buffer.size = data_size;
        meta_buffer.position = buffer.position;
    }

    // Read file count
    int file_count = g2(&meta_buffer);

    // Skip ahead in data buffer by fileCount * 10 bytes
    data_buffer.position = meta_buffer.position + file_count * 10;

    // Allocate FileList
    filelist = malloc(sizeof(struct FileListDat));
    if( !filelist )
    {
        if( decompressed_archive )
            free(decompressed_archive);
        return NULL;
    }

    filelist->files = malloc(file_count * sizeof(char*));
    filelist->file_sizes = malloc(file_count * sizeof(int));
    filelist->file_name_hashes = malloc(file_count * sizeof(int));
    if( !filelist->files || !filelist->file_sizes || !filelist->file_name_hashes )
    {
        if( filelist->files )
            free(filelist->files);
        if( filelist->file_sizes )
            free(filelist->file_sizes);
        if( filelist->file_name_hashes )
            free(filelist->file_name_hashes);
        free(filelist);
        if( decompressed_archive )
            free(decompressed_archive);
        return NULL;
    }

    memset(filelist->files, 0, file_count * sizeof(char*));
    memset(filelist->file_sizes, 0, file_count * sizeof(int));
    memset(filelist->file_name_hashes, 0, file_count * sizeof(int));
    filelist->file_count = file_count;

    // Read each file
    for( int i = 0; i < file_count; i++ )
    {
        int name_hash = g4(&meta_buffer);
        int file_actual_size = g3(&meta_buffer);
        int file_size = g3(&meta_buffer);

        char* file_data = NULL;

        if( is_compressed )
        {
            // Archive is compressed, files are already decompressed
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
            // Archive is not compressed, individual files are compressed
            char* compressed_file_data = malloc(file_size);
            if( !compressed_file_data )
                goto error;

            int bytes_read = greadto(&data_buffer, compressed_file_data, file_size, file_size);
            if( bytes_read < file_size )
            {
                free(compressed_file_data);
                goto error;
            }

            // Decompress the file
            file_data = malloc(file_actual_size);
            if( !file_data )
            {
                free(compressed_file_data);
                goto error;
            }

            cache_bzip_decompress(
                (uint8_t*)file_data, file_actual_size, (uint8_t*)compressed_file_data, file_size);
            free(compressed_file_data);
        }

        filelist->files[i] = file_data;
        filelist->file_sizes[i] = is_compressed ? file_size : file_actual_size;
        filelist->file_name_hashes[i] = name_hash;
    }

    // Cleanup decompressed archive if we allocated it
    if( decompressed_archive )
        free(decompressed_archive);

    return filelist;

error:
    // Cleanup on error
    if( filelist )
    {
        for( int i = 0; i < file_count; i++ )
        {
            if( filelist->files && filelist->files[i] )
                free(filelist->files[i]);
        }
        if( filelist->files )
            free(filelist->files);
        if( filelist->file_sizes )
            free(filelist->file_sizes);
        free(filelist);
    }
    if( decompressed_archive )
        free(decompressed_archive);
    return NULL;
}

void
filelist_dat_free(struct FileListDat* filelist)
{
    if( !filelist )
        return;
    for( int i = 0; i < filelist->file_count; i++ )
    {
        free(filelist->files[i]);
    }
    free(filelist->file_sizes);
    free(filelist->file_name_hashes);
    free(filelist);
}

int
filelist_dat_find_file_by_name(
    struct FileListDat* filelist,
    const char* name)
{
    int name_hash = archive_name_hash_dat(name);
    for( int i = 0; i < filelist->file_count; i++ )
    {
        if( filelist->file_name_hashes[i] == name_hash )
            return i;
    }
    return -1;
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