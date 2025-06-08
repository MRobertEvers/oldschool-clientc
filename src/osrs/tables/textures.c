#include "textures.h"

#include "../cache.h"
#include "../rsbuf.h"

#include <stdlib.h>
#include <string.h>

struct TextureDefinition*
texture_definition_new_from_cache(struct Cache* cache, int id)
{
    struct CacheArchive* archive = cache_archive_new_load(cache, CACHE_TEXTURES, id);
    if( !archive )
        return NULL;

    struct TextureDefinition* def =
        texture_definition_new_decode(archive->data, archive->data_size);

    cache_archive_free(archive);

    return def;
}

struct TextureDefinition*
texture_definition_new_decode(const unsigned char* data, int length)
{
    struct TextureDefinition* def = malloc(sizeof(struct TextureDefinition));
    memset(def, 0, sizeof(struct TextureDefinition));
    if( !def )
        return NULL;

    struct RSBuffer buffer;
    rsbuf_init(&buffer, data, length);

    // Read the fields
    def->average_hsl = rsbuf_g2(&buffer);
    def->opaque = rsbuf_g1b(&buffer) != 0;

    // Read count of files
    int count = rsbuf_g1(&buffer);
    def->file_ids_count = count;

    // Allocate and read file IDs
    def->file_ids = malloc(count * sizeof(int));
    if( !def->file_ids )
        goto error;

    for( int i = 0; i < count; i++ )
        def->file_ids[i] = rsbuf_g2(&buffer);

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
    free(def->file_ids);
    free(def->sprite_types);
    free(def->transforms);
    free(def);
    return NULL;
}