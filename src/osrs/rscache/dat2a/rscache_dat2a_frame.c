#include "rscache_dat2a_frame.h"

#include "../dat2disk/rscache_dat2disk.h"
#include "../shared/rscache_shared_rs_buffer.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct RSCacheDat2A_Frame*
RSCacheDat2A_FrameNewFromCache(
    struct RSCacheDat2Disk* cache,
    int frame_id,
    struct RSCacheDat2A_Framemap* framemap)
{
    struct RSCacheDat2Disk_Archive* archive = RSCacheDat2Disk_ArchiveNewLoad(cache, RSCacheDat2Disk_Table_Animations, frame_id);
    if( !archive )
    {
        printf("Failed to load frame %d\n", frame_id);
        return NULL;
    }

    struct RSCacheDat2A_Frame* frame =
        RSCacheDat2A_FrameNewDecode2(frame_id, framemap, archive->data, archive->data_size);

    RSCacheDat2Disk_ArchiveFree(archive);

    return frame;
}

static struct RSCacheDat2A_Frame*
frame_new_decode(
    int id,
    struct RSCacheDat2A_Framemap* framemap,
    struct RSCacheShared_RSBuffer* buffer);

struct RSCacheDat2A_Frame*
RSCacheDat2A_FrameNewDecode2(
    int id,
    struct RSCacheDat2A_Framemap* framemap,
    char* data,
    int data_size)
{
    struct RSCacheShared_RSBuffer buffer = { .data = (uint8_t*)(data), .size = (uint32_t)(data_size), .position = 0 };
    return frame_new_decode(id, framemap, &buffer);
}

static struct RSCacheDat2A_Frame*
frame_new_decode(
    int id,
    struct RSCacheDat2A_Framemap* framemap,
    struct RSCacheShared_RSBuffer* buffer)
{
    // Initialize the frame definition
    struct RSCacheDat2A_Frame* def = malloc(sizeof(struct RSCacheDat2A_Frame));
    memset(def, 0, sizeof(struct RSCacheDat2A_Frame));

    def->_id = id;
    def->framemap_id = framemap->id;
    def->_framemap = framemap;
    def->showing = false;

    // Read the framemap archive index and length
    int framemap_archive_index = g2(buffer);
    assert(framemap_archive_index == framemap->id);
    int length = g1(buffer);

    // Skip the framemap archive index and length in the data buffer
    struct RSCacheShared_RSBuffer data = *buffer;
    data.position = 3 + length;

    // Allocate temporary arrays for processing
    int* index_frame_ids = malloc(500 * sizeof(int));
    int* scratch_translator_x = malloc(500 * sizeof(int));
    int* scratch_translator_y = malloc(500 * sizeof(int));
    int* scratch_translator_z = malloc(500 * sizeof(int));

    int last_i = -1;
    int index = 0;

    // Process each frame
    for( int i = 0; i < length; ++i )
    {
        int var9 = g1(buffer);

        if( var9 <= 0 )
        {
            continue;
        }

        // Handle type 0 frames
        if( framemap->types[i] != 0 )
        {
            for( int var10 = i - 1; var10 > last_i; --var10 )
            {
                if( framemap->types[var10] == 0 )
                {
                    index_frame_ids[index] = var10;
                    scratch_translator_x[index] = 0;
                    scratch_translator_y[index] = 0;
                    scratch_translator_z[index] = 0;
                    ++index;
                    break;
                }
            }
        }

        // Set the frame ID
        index_frame_ids[index] = i;
        short var11 = 0;
        if( framemap->types[i] == 3 )
        {
            var11 = 128;
        }

        // Read translation values based on flags
        if( (var9 & 1) != 0 )
        {
            scratch_translator_x[index] = gshortsmart(&data);
        }
        else
        {
            scratch_translator_x[index] = var11;
        }

        if( (var9 & 2) != 0 )
        {
            scratch_translator_y[index] = gshortsmart(&data);
        }
        else
        {
            scratch_translator_y[index] = var11;
        }

        if( (var9 & 4) != 0 )
        {
            scratch_translator_z[index] = gshortsmart(&data);
        }
        else
        {
            scratch_translator_z[index] = var11;
        }

        last_i = i;
        ++index;

        // Set showing flag for type 5 frames
        if( framemap->types[i] == 5 )
        {
            def->showing = true;
        }
    }

    // Verify we read all the data
    if( data.position != data.size )
    {
        // Handle error - data size mismatch
        free(index_frame_ids);
        free(scratch_translator_x);
        free(scratch_translator_y);
        free(scratch_translator_z);
        return NULL;
    }

    // Allocate final arrays
    def->translator_count = index;
    def->index_frame_ids = malloc(index * sizeof(int));
    def->translator_arg_x = malloc(index * sizeof(int));
    def->translator_arg_y = malloc(index * sizeof(int));
    def->translator_arg_z = malloc(index * sizeof(int));

    // Copy data to final arrays
    for( int i = 0; i < index; ++i )
    {
        def->index_frame_ids[i] = index_frame_ids[i];
        def->translator_arg_x[i] = scratch_translator_x[i];
        def->translator_arg_y[i] = scratch_translator_y[i];
        def->translator_arg_z[i] = scratch_translator_z[i];
    }

    // Free temporary arrays
    free(index_frame_ids);
    free(scratch_translator_x);
    free(scratch_translator_y);
    free(scratch_translator_z);

    return def;
}

int
RSCacheDat2A_FrameFramemapIdFromFile(
    char* data,
    int data_size)
{
    struct RSCacheShared_RSBuffer buffer = { .data = (uint8_t*)(data), .size = (uint32_t)(data_size), .position = 0 };
    return g2(&buffer);
}

void
RSCacheDat2A_FrameFree(struct RSCacheDat2A_Frame* def)
{
    if( !def )
        return;

    if( def->index_frame_ids )
    {
        free(def->index_frame_ids);
    }
    if( def->translator_arg_x )
    {
        free(def->translator_arg_x);
    }
    if( def->translator_arg_y )
    {
        free(def->translator_arg_y);
    }
    if( def->translator_arg_z )
    {
        free(def->translator_arg_z);
    }

    free(def);
}