#include "textures.h"

#include "../cache.h"
#include "../filelist.h"
#include "../rsbuf.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct CacheTexture*
texture_definition_new_from_cache(struct Cache* cache, int id)
{
    struct CacheArchive* archive = cache_archive_new_load(cache, CACHE_TEXTURES, 0);
    if( !archive )
        return NULL;

    struct FileList* filelist = filelist_new_from_cache_archive(archive);
    if( !filelist )
        return NULL;

    struct ArchiveReference* reference = &cache->tables[CACHE_TEXTURES]->archives[0];
    if( !reference )
        return NULL;

    /**
     * Texture definition ids are mostly contiguous but id=54 is missing.
     * So files[54].id = 55.
     *
     * This is a quick workaround
     */
    int file_index = id;
    if( file_index >= 54 )
        file_index--;

    struct CacheTexture* def = NULL;
    def = texture_definition_new_decode(
        filelist->files[file_index], filelist->file_sizes[file_index]);

    assert(def->sprite_ids);
    assert(reference->children.files[file_index].id == id);

    filelist_free(filelist);

    cache_archive_free(archive);

    assert(def);

    return def;
}

void
texture_definition_free(struct CacheTexture* texture_definition)
{
    free(texture_definition->sprite_ids);
    free(texture_definition->sprite_types);
    free(texture_definition->transforms);
    free(texture_definition);
}

struct CacheTexture*
texture_definition_new_decode(const unsigned char* data, int length)
{
    struct CacheTexture* def = malloc(sizeof(struct CacheTexture));
    memset(def, 0, sizeof(struct CacheTexture));
    if( !def )
        return NULL;

    return texture_definition_decode_inplace(def, data, length);
}

struct CacheTexture*
texture_definition_decode_inplace(struct CacheTexture* def, const unsigned char* data, int length)
{
    struct RSBuffer buffer;
    rsbuf_init(&buffer, data, length);

    // Read the fields
    def->average_hsl = rsbuf_g2(&buffer);
    def->opaque = rsbuf_g1b(&buffer) != 0;

    // Read count of files
    int count = rsbuf_g1(&buffer);
    def->sprite_ids_count = count;

    // Allocate and read file IDs
    def->sprite_ids = malloc(count * sizeof(int));
    if( !def->sprite_ids )
        goto error;

    for( int i = 0; i < count; i++ )
        def->sprite_ids[i] = rsbuf_g2(&buffer);

    // Handle field1780 (count > 1 case)
    if( count > 1 )
    {
        def->sprite_types = malloc((count - 1) * sizeof(int));
        if( !def->sprite_types )
            goto error;

        for( int i = 0; i < count - 1; i++ )
            def->sprite_types[i] = rsbuf_g1(&buffer);
    }

    if( count > 1 )
    {
        // unused?
        for( int i = 0; i < count - 1; i++ )
            rsbuf_g1(&buffer);
    }

    // Handle field1786
    def->transforms = malloc(count * sizeof(int));
    if( !def->transforms )
        goto error;

    for( int i = 0; i < count; i++ )
        def->transforms[i] = rsbuf_g4(&buffer);

    // Read animation properties
    def->animation_direction = rsbuf_g1(&buffer);
    def->animation_speed = rsbuf_g1(&buffer);

    return def;

error:
    free(def->sprite_ids);
    free(def->sprite_types);
    free(def->transforms);
    free(def);
    return NULL;
}