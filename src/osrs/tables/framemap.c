#include "framemap.h"

#include "osrs/cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct CacheFramemap*
framemap_new_from_cache(struct Cache* cache, int framemap_id)
{
    struct CacheArchive* archive = cache_archive_new_load(cache, CACHE_SKELETONS, framemap_id);
    if( !archive )
    {
        printf("Failed to load framemap %d\n", framemap_id);
        return NULL;
    }

    struct CacheFramemap* framemap =
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

struct CacheFramemap*
framemap_new_decode2(int id, char* data, int data_size)
{
    struct Buffer buffer = { .data = data, .data_size = data_size, .position = 0 };
    return framemap_new_decode(id, &buffer);
}

struct CacheFramemap*
framemap_new_decode(int id, struct Buffer* buffer)
{
    struct CacheFramemap* def = malloc(sizeof(struct CacheFramemap));
    memset(def, 0, sizeof(struct CacheFramemap));

    // Initialize the framemap definition
    def->id = id;
    def->length = read_u8(buffer);
    def->types = malloc(def->length * sizeof(int));
    def->bone_groups = malloc(def->length * sizeof(int*));
    def->bone_groups_lengths = malloc(def->length * sizeof(int));
    memset(def->types, 0, def->length * sizeof(int));
    memset(def->bone_groups, 0, def->length * sizeof(int*));
    memset(def->bone_groups_lengths, 0, def->length * sizeof(int));

    // Read the types array
    for( int i = 0; i < def->length; i++ )
    {
        def->types[i] = read_u8(buffer);
    }

    // Read the frame maps lengths
    for( int i = 0; i < def->length; i++ )
    {
        int group_length = read_u8(buffer);
        def->bone_groups_lengths[i] = group_length;
        def->bone_groups[i] = malloc(group_length * sizeof(int));
        memset(def->bone_groups[i], 0, group_length * sizeof(int));
    }

    // Read the frame maps data
    for( int i = 0; i < def->length; i++ )
    {
        for( int j = 0; j < def->bone_groups_lengths[i]; j++ )
        {
            def->bone_groups[i][j] = read_u8(buffer);
        }
    }

    return def;
}

void
framemap_free(struct CacheFramemap* def)
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
