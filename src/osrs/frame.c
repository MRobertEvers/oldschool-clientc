#include "frame.h"

#include "buffer.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static int
read_short_smart(struct Buffer* buffer)
{
    int value = buffer->data[buffer->position] & 0xFF;
    if( value < 128 )
    {
        return read_8(buffer) - 64;
    }
    else
    {
        unsigned short ushort_value = read_16(buffer);
        return (int)(ushort_value - 0xC000);
    }
}

void
decode_frame(
    struct FrameDefinition* def, struct FramemapDefinition* framemap, int id, struct Buffer* buffer)
{
    // Initialize the frame definition
    def->id = id;
    def->framemap = framemap;
    def->showing = false;

    // Read the framemap archive index and length
    int framemap_archive_index = read_16(buffer);
    int length = read_8(buffer);

    // Skip the framemap archive index and length in the data buffer
    struct Buffer data = *buffer;
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
        int var9 = read_8(buffer);

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
            scratch_translator_x[index] = read_short_smart(&data);
        }
        else
        {
            scratch_translator_x[index] = var11;
        }

        if( (var9 & 2) != 0 )
        {
            scratch_translator_y[index] = read_short_smart(&data);
        }
        else
        {
            scratch_translator_y[index] = var11;
        }

        if( (var9 & 4) != 0 )
        {
            scratch_translator_z[index] = read_short_smart(&data);
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
    if( data.position != data.data_size )
    {
        // Handle error - data size mismatch
        free(index_frame_ids);
        free(scratch_translator_x);
        free(scratch_translator_y);
        free(scratch_translator_z);
        return;
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
}

void
free_frame(struct FrameDefinition* def)
{
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
}