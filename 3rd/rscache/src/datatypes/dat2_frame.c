#include "dat2_frame.h"

#include "../dat2disk.h"
#include "../rsbuffer.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct RSCache_Dat2Frame*
RSCache_Dat2FrameNewFromCache(
    struct RSCache_Dat2Disk* cache,
    int frame_id,
    struct RSCache_Dat2Framemap* framemap)
{
    struct RSCache_Dat2DiskArchive* archive = RSCache_Dat2DiskArchiveNewLoad(cache, RSCACHE_DAT2_DISK_TABLE_ANIMATIONS, frame_id);
    if( !archive )
    {
        printf("Failed to load frame %d\n", frame_id);
        return NULL;
    }

    struct RSCache_Dat2Frame* frame =
        RSCache_Dat2FrameNewDecode2(frame_id, framemap, archive->data, archive->data_size);

    RSCache_Dat2DiskArchiveFree(archive);

    return frame;
}

static struct RSCache_Dat2Frame*
frame_new_decode(
    int id,
    struct RSCache_Dat2Framemap* framemap,
    struct RSCache_Buffer* buffer);

struct RSCache_Dat2Frame*
RSCache_Dat2FrameNewDecode2(
    int id,
    struct RSCache_Dat2Framemap* framemap,
    char* data,
    int data_size)
{
    struct RSCache_Buffer buffer = { .data = (uint8_t*)(data), .size = (uint32_t)(data_size), .position = 0 };
    return frame_new_decode(id, framemap, &buffer);
}

static struct RSCache_Dat2Frame*
frame_new_decode(
    int id,
    struct RSCache_Dat2Framemap* framemap,
    struct RSCache_Buffer* buffer)
{
    // Initialize the frame definition
    struct RSCache_Dat2Frame* def = malloc(sizeof(struct RSCache_Dat2Frame));
    memset(def, 0, sizeof(struct RSCache_Dat2Frame));

    def->_id = id;
    def->framemap_id = framemap->id;
    def->_framemap = framemap;
    def->showing = false;

    // Read the framemap archive index and length
    int framemap_archive_index = g2(buffer);
    assert(framemap_archive_index == framemap->id);
    int length = g1(buffer);

    // Skip the framemap archive index and length in the data buffer
    struct RSCache_Buffer data = *buffer;
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
RSCache_Dat2FrameFramemapIdFromFile(
    char* data,
    int data_size)
{
    struct RSCache_Buffer buffer = { .data = (uint8_t*)(data), .size = (uint32_t)(data_size), .position = 0 };
    return g2(&buffer);
}

void
RSCache_Dat2FrameFree(struct RSCache_Dat2Frame* def)
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
