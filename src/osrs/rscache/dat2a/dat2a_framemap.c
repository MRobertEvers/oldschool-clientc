#include "dat2a_framemap.h"

#include "../dat2disk/dat2disk.h"
#include "../shared/shared_rs_buffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct RSCacheDat2A_Framemap*
RSCacheDat2A_FramemapNewFromCache(
    struct RSCacheDat2Disk* cache,
    int framemap_id)
{
    struct RSCacheDat2Disk_Archive* archive = RSCacheDat2Disk_ArchiveNewLoad(cache, RSCacheDat2Disk_Table_Skeletons, framemap_id);
    if( !archive )
    {
        printf("Failed to load framemap %d\n", framemap_id);
        return NULL;
    }

    struct RSCacheDat2A_Framemap* framemap =
        RSCacheDat2A_FramemapNewDecode2(framemap_id, archive->data, archive->data_size);

    RSCacheDat2Disk_ArchiveFree(archive);

    return framemap;
}

struct RSCacheDat2A_Framemap*
RSCacheDat2A_FramemapNewFromArchive(
    struct RSCacheDat2Disk_Archive* archive,
    int framemap_id)
{
    if( !archive )
        return NULL;

    return RSCacheDat2A_FramemapNewDecode2(framemap_id, archive->data, archive->data_size);
}

int
RSCacheDat2A_FramemapIdFromFrameArchive(
    char* data,
    int data_size)
{
    int framemap_id = (data[0] & 0xFF) << 8 | (data[1] & 0xFF);
    return framemap_id;
}
static struct RSCacheDat2A_Framemap*
framemap_new_decode(
    int id,
    struct RSCacheShared_RSBuffer* buffer);

struct RSCacheDat2A_Framemap*
RSCacheDat2A_FramemapNewDecode2(
    int id,
    char* data,
    int data_size)
{
    struct RSCacheShared_RSBuffer buffer = { .data = (uint8_t*)(data), .size = (uint32_t)(data_size), .position = 0 };
    return framemap_new_decode(id, &buffer);
}

static struct RSCacheDat2A_Framemap*
framemap_new_decode(
    int id,
    struct RSCacheShared_RSBuffer* buffer)
{
    struct RSCacheDat2A_Framemap* def = malloc(sizeof(struct RSCacheDat2A_Framemap));
    memset(def, 0, sizeof(struct RSCacheDat2A_Framemap));

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
RSCacheDat2A_FramemapFree(struct RSCacheDat2A_Framemap* def)
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
