#include "dat2_framemap.h"

#include "../dat2disk.h"
#include "../rsbuffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct RSCache_Dat2Framemap*
RSCache_Dat2FramemapNewFromCache(
    struct RSCache_Dat2Disk* cache,
    int framemap_id)
{
    struct RSCache_Dat2DiskArchive* archive = RSCache_Dat2DiskArchiveNewLoad(cache, RSCACHE_DAT2_DISK_TABLE_SKELETONS, framemap_id);
    if( !archive )
    {
        printf("Failed to load framemap %d\n", framemap_id);
        return NULL;
    }

    struct RSCache_Dat2Framemap* framemap =
        RSCache_Dat2FramemapNewDecode2(framemap_id, archive->data, archive->data_size);

    RSCache_Dat2DiskArchiveFree(archive);

    return framemap;
}

struct RSCache_Dat2Framemap*
RSCache_Dat2FramemapNewFromArchive(
    struct RSCache_Dat2DiskArchive* archive,
    int framemap_id)
{
    if( !archive )
        return NULL;

    return RSCache_Dat2FramemapNewDecode2(framemap_id, archive->data, archive->data_size);
}

int
RSCache_Dat2FramemapIdFromFrameArchive(
    char* data,
    int data_size)
{
    int framemap_id = (data[0] & 0xFF) << 8 | (data[1] & 0xFF);
    return framemap_id;
}
static struct RSCache_Dat2Framemap*
framemap_new_decode(
    int id,
    struct RSCache_Buffer* buffer);

struct RSCache_Dat2Framemap*
RSCache_Dat2FramemapNewDecode2(
    int id,
    char* data,
    int data_size)
{
    struct RSCache_Buffer buffer = { .data = (uint8_t*)(data), .size = (uint32_t)(data_size), .position = 0 };
    return framemap_new_decode(id, &buffer);
}

static struct RSCache_Dat2Framemap*
framemap_new_decode(
    int id,
    struct RSCache_Buffer* buffer)
{
    struct RSCache_Dat2Framemap* def = malloc(sizeof(struct RSCache_Dat2Framemap));
    memset(def, 0, sizeof(struct RSCache_Dat2Framemap));

    // Initialize the framemap definition
    def->id = id;
    def->length = g1(buffer);
    def->types = malloc(def->length * sizeof(int));
    def->bone_groups = malloc(def->length * sizeof(int*));
    def->bone_groups_lengths = malloc(def->length * sizeof(int));
    memset(def->types, 0, def->length * sizeof(int));
    memset(def->bone_groups, 0, def->length * sizeof(int*));
    memset(def->bone_groups_lengths, 0, def->length * sizeof(int));

    // Read the types array
    for( int i = 0; i < def->length; i++ )
    {
        def->types[i] = g1(buffer);
    }

    // Read the frame maps lengths
    for( int i = 0; i < def->length; i++ )
    {
        int group_length = g1(buffer);
        def->bone_groups_lengths[i] = group_length;
        def->bone_groups[i] = malloc(group_length * sizeof(int));
        memset(def->bone_groups[i], 0, group_length * sizeof(int));
    }

    // Read the frame maps data
    for( int i = 0; i < def->length; i++ )
    {
        for( int j = 0; j < def->bone_groups_lengths[i]; j++ )
        {
            def->bone_groups[i][j] = g1(buffer);
        }
    }

    return def;
}

void
RSCache_Dat2FramemapFree(struct RSCache_Dat2Framemap* def)
{
    if( !def )
        return;

    if( def->types )
        free(def->types);

    if( def->bone_groups )
    {
        for( int i = 0; i < def->length; i++ )
        {
            if( def->bone_groups[i] )
                free(def->bone_groups[i]);
        }
        free(def->bone_groups);
    }

    free(def->bone_groups_lengths);
    free(def);
}
