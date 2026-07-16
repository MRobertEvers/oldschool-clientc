#include "filelist.h"

#include "rsbuffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct RSCache_FileList*
RSCache_FileListNewFromDecode(
    char* data,
    int data_size,
    int num_files)
{
    struct RSCache_FileList* filelist = malloc(sizeof(struct RSCache_FileList));
    if( !filelist )
        return NULL;

    struct RSCache_Buffer buffer = { .data = (uint8_t*)data, .position = 0, .size = (uint32_t)data_size };

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
