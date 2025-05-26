#include "osrs/filelist.h"

#include "buffer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLAG_IDENTIFIERS 0x1
#define FLAG_WHIRLPOOL 0x2
#define FLAG_HASH 0x8

struct FileList*
filelist_new_from_cache_archive(struct CacheArchive* archive)
{
    struct FileList* filelist = malloc(sizeof(struct FileList));
    if( !filelist )
        return NULL;

    struct Buffer buffer = { .data = archive->data,
                             .position = 0,
                             .data_size = archive->data_size };

    filelist->files = malloc(archive->file_count * sizeof(char*));
    filelist->file_sizes = malloc(archive->file_count * sizeof(int));
    if( !filelist->files || !filelist->file_sizes )
    {
        free(filelist);
        return NULL;
    }
    memset(filelist->file_sizes, 0, archive->file_count * sizeof(int));
    filelist->file_count = archive->file_count;

    /* read the number of chunks at the end of the archive */
    buffer.position = buffer.data_size - 1;
    int chunks = read_8(&buffer);
    buffer.position = 0;

    /* read the sizes of the child entries and individual chunks */
    int** chunk_sizes = malloc(chunks * sizeof(int*));
    if( !chunk_sizes )
        goto error;

    for( int i = 0; i < chunks; i++ )
    {
        chunk_sizes[i] = malloc(archive->file_count * sizeof(int));
        if( !chunk_sizes[i] )
        {
            printf("Failed to allocate chunk sizes\n");
            goto error;
        }
    }

    int* sizes = malloc(archive->file_count * sizeof(int));
    if( !sizes )
        goto error;
    memset(sizes, 0, archive->file_count * sizeof(int));

    buffer.position = buffer.data_size - 1 - chunks * archive->file_count * 4;
    for( int chunk = 0; chunk < chunks; chunk++ )
    {
        int chunk_size = 0;
        for( int id = 0; id < archive->file_count; id++ )
        {
            /* read the delta-encoded chunk length */
            int delta = read_32(&buffer);
            chunk_size += delta;

            /* store the size of this chunk */
            chunk_sizes[chunk][id] = chunk_size;

            /* and add it to the size of the whole file */
            sizes[id] += chunk_size;
        }
    }

    /* allocate the buffers for the child entries */
    int* file_offsets = malloc(archive->file_count * sizeof(int));
    memset(file_offsets, 0, archive->file_count * sizeof(int));

    for( int id = 0; id < archive->file_count; id++ )
    {
        filelist->files[id] = malloc(sizes[id]);
        if( !filelist->files[id] )
            goto error;
    }

    /* read the data into the buffers */
    buffer.position = 0;
    for( int chunk = 0; chunk < chunks; chunk++ )
    {
        for( int id = 0; id < archive->file_count; id++ )
        {
            /* get the length of this chunk */
            int chunk_size = chunk_sizes[chunk][id];

            /* copy this chunk into the file buffer */
            readto(
                filelist->files[id] + file_offsets[id],
                sizes[id] - file_offsets[id],
                chunk_size,
                &buffer);
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