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
    struct RSCache_Dat2DiskArchive* archive = RSCache_Dat2DiskArchiveNewLoad(
        cache, RSCache_Dat2DiskTableId(cache, RSCACHE_DAT2_TABLE_ANIMATIONS), frame_id);
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
    /* Entry count is bounded by `length`: a real entry consumes bone index i and a
     * synthesized one a distinct index below it, with last_i advancing past both, so
     * together they never exceed one entry per bone. */
    bool* scratch_synthesized = calloc(500, sizeof(bool));

    /* Capture the flag stream verbatim before the cursor walks it — see the
     * provenance note in the header. */
    def->flag_count = length;
    def->bone_flags = length > 0 ? malloc((size_t)length) : NULL;
    if( length > 0 && def->bone_flags )
        memcpy(def->bone_flags, buffer->data + buffer->position, (size_t)length);

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
                    scratch_synthesized[index] = true;
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
        free(scratch_synthesized);
        free(def->bone_flags);
        free(def);
        return NULL;
    }

    // Allocate final arrays
    def->translator_count = index;
    def->index_frame_ids = malloc(index * sizeof(int));
    def->translator_arg_x = malloc(index * sizeof(int));
    def->translator_arg_y = malloc(index * sizeof(int));
    def->translator_arg_z = malloc(index * sizeof(int));
    def->synthesized = calloc((size_t)(index > 0 ? index : 1), sizeof(bool));

    // Copy data to final arrays
    for( int i = 0; i < index; ++i )
    {
        def->index_frame_ids[i] = index_frame_ids[i];
        def->translator_arg_x[i] = scratch_translator_x[i];
        def->translator_arg_y[i] = scratch_translator_y[i];
        def->translator_arg_z[i] = scratch_translator_z[i];
        def->synthesized[i] = scratch_synthesized[i];
    }

    // Free temporary arrays
    free(index_frame_ids);
    free(scratch_translator_x);
    free(scratch_translator_y);
    free(scratch_translator_z);
    free(scratch_synthesized);

    return def;
}

uint32_t
RSCache_Dat2FrameEncodeBound(const struct RSCache_Dat2Frame* def)
{
    if( !def )
        return 0;

    /* Header, the flag stream, then up to three shortsmarts (2 bytes each) per
     * entry. */
    return 3u + (uint32_t)def->flag_count + (uint32_t)def->translator_count * 6u + 16u;
}

uint32_t
RSCache_Dat2FrameEncode(
    const struct RSCache_Dat2Frame* def,
    uint8_t* out,
    uint32_t out_capacity)
{
    if( !def || !out )
        return 0;
    if( def->flag_count > 0 && !def->bone_flags )
        return 0;
    if( def->translator_count > 0 && !def->synthesized )
        return 0;
    if( out_capacity < RSCache_Dat2FrameEncodeBound(def) )
        return 0;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    p2(&buffer, def->framemap_id);
    p1(&buffer, def->flag_count);

    /* The flag stream verbatim. Reproducing it rather than deriving it is what makes
     * this exact: the flag bits say which translation components were present, and a
     * component that was absent got a type-derived default on decode, which cannot be
     * told apart from an authored value equal to that default. */
    for( int i = 0; i < def->flag_count; i++ )
        p1(&buffer, def->bone_flags[i]);

    /*
     * Then the value stream, in entry order, skipping the entries the decoder
     * invented. Real entries appear in ascending bone order and a synthesized entry
     * always immediately precedes the real one that caused it, so walking the array
     * and skipping synthesized entries reproduces the source order exactly.
     */
    for( int i = 0; i < def->translator_count; i++ )
    {
        if( def->synthesized[i] )
            continue;

        int bone = def->index_frame_ids[i];
        if( bone < 0 || bone >= def->flag_count )
            return 0;

        int flags = def->bone_flags[bone];
        if( flags & 1 )
            pshortsmart(&buffer, def->translator_arg_x[i]);
        if( flags & 2 )
            pshortsmart(&buffer, def->translator_arg_y[i]);
        if( flags & 4 )
            pshortsmart(&buffer, def->translator_arg_z[i]);
    }

    return buffer.position;
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
    free(def->bone_flags);
    free(def->synthesized);

    free(def);
}
