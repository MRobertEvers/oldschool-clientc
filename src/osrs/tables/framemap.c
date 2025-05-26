#include "framemap.h"

#include "osrs/cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct FramemapDefinition*
framemap_new_from_cache(struct Cache* cache, int framemap_id)
{
    struct CacheArchive* archive = cache_archive_new_load(cache, CACHE_SKELETONS, framemap_id);
    if( !archive )
    {
        printf("Failed to load framemap %d\n", framemap_id);
        return NULL;
    }

    struct FramemapDefinition* framemap =
        framemap_new_decode2(framemap_id, archive->data, archive->data_size);

    cache_archive_free(archive);

    return framemap;
}

int
framemap_id_from_frame_archive(char* data, int data_size)
{
    int framemap_id = (data[0] & 0xFF) << 8 | (data[1] & 0xFF);
    return framemap_id;
}

struct FramemapDefinition*
framemap_new_decode2(int id, char* data, int data_size)
{
    struct Buffer buffer = { .data = data, .data_size = data_size, .position = 0 };
    return framemap_new_decode(id, &buffer);
}

struct FramemapDefinition*
framemap_new_decode(int id, struct Buffer* buffer)
{
    struct FramemapDefinition* def = malloc(sizeof(struct FramemapDefinition));
    memset(def, 0, sizeof(struct FramemapDefinition));

    // Initialize the framemap definition
    def->id = id;
    def->length = read_8(buffer);
    def->types = malloc(def->length * sizeof(int));
    def->bone_groups = malloc(def->length * sizeof(int*));
    def->bone_groups_lengths = malloc(def->length * sizeof(int));
    memset(def->types, 0, def->length * sizeof(int));
    memset(def->bone_groups, 0, def->length * sizeof(int*));
    memset(def->bone_groups_lengths, 0, def->length * sizeof(int));

    // Read the types array
    for( int i = 0; i < def->length; i++ )
    {
        def->types[i] = read_8(buffer);
    }

    // Read the frame maps lengths
    for( int i = 0; i < def->length; i++ )
    {
        def->bone_groups_lengths[i] = read_8(buffer);
        def->bone_groups[i] = malloc(def->bone_groups_lengths[i] * sizeof(int));
        memset(def->bone_groups[i], 0, def->bone_groups_lengths[i] * sizeof(int));
    }

    // Read the frame maps data
    for( int i = 0; i < def->length; i++ )
    {
        for( int j = 0; j < def->bone_groups_lengths[i]; j++ )
        {
            def->bone_groups[i][j] = read_8(buffer);
        }
    }

    return def;
}

void
framemap_free(struct FramemapDefinition* def)
{
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
