#include "textures.h"

#include "../cache.h"
#include "../datastruct/hmap.h"
#include "../filelist.h"
#include "../rsbuf.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct CacheTexture*
texture_definition_new_from_cache(struct Cache* cache, int id)
{
    struct FileList* filelist = NULL;
    struct ArchiveReference* reference = NULL;
    struct CacheTexture* def = NULL;
    struct CacheArchive* archive = cache_archive_new_load(cache, CACHE_TEXTURES, 0);
    if( !archive )
        return NULL;

    filelist = filelist_new_from_cache_archive(archive);
    if( !filelist )
        return NULL;

    reference = &cache->tables[CACHE_TEXTURES]->archives[0];
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

struct CacheTextureMap
{
    struct HMap* map;
};

struct CacheTextureMapLoader
{
    struct CacheArchive* archive;
    struct FileList* filelist;

    struct CacheTextureMap* map;
};

struct CacheTextureMapEntry
{
    int id;
    struct CacheTexture* texture;
};

struct CacheTextureMapLoader*
texture_definition_map_loader_new_from_archive(struct CacheArchive* archive)
{
    struct CacheTextureMap* map = NULL;
    struct HashConfig config;
    struct CacheTextureMapLoader* loader = malloc(sizeof(struct CacheTextureMapLoader));
    if( !loader )
        return NULL;

    memset(loader, 0, sizeof(struct CacheTextureMapEntry));

    loader->archive = archive;
    loader->filelist = filelist_new_from_cache_archive(archive);
    if( !loader->filelist )
        return NULL;

    config = (struct HashConfig){
        .buffer = NULL,
        .buffer_size = 0,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct CacheTextureMapEntry),
    };

    map = malloc(sizeof(struct CacheTextureMap));
    memset(map, 0, sizeof(struct CacheTextureMap));
    if( !map )
        return NULL;

    map->map = hmap_new(&config, 0);
    if( !map->map )
        return NULL;

    loader->map = map;

    return loader;
}

void
texture_definition_map_loader_load(struct CacheTextureMapLoader* loader, int id)
{
    struct FileList* filelist = loader->filelist;

    struct CacheTexture* def = NULL;
    struct CacheTextureMapEntry* item = NULL;

    /**
     * Texture definition ids are mostly contiguous but id=54 is missing.
     * So files[54].id = 55.
     *
     * This is a quick workaround
     */
    int file_index = id;
    if( file_index >= 54 )
        file_index--;

    def = texture_definition_new_decode(
        filelist->files[file_index], filelist->file_sizes[file_index]);

    assert(def->sprite_ids);

    item = hmap_search(loader->map, &id, HMAP_INSERT);
    if( !item )
        return;

    item->id = id;
    item->texture = def;
}

void
texture_definition_map_loader_free(struct CacheTextureMapLoader* loader)
{
    if( loader )
    {
        if( loader->archive )
            cache_archive_free(loader->archive);
        filelist_free(loader->filelist);
        free(loader);
    }
}

struct CacheTextureMap*
texture_definition_map_new_from_loader(struct CacheTextureMapLoader* loader)
{
    assert(loader->map);

    struct CacheTextureMap* map = loader->map;

    loader->map = NULL;

    return map;
}

struct CacheTexture*
texture_definition_map_get(struct CacheTextureMap* map, int id)
{
    struct CacheTextureMapEntry* item = hmap_search(map->map, &id, HMAP_FIND);
    if( !item )
        return NULL;

    return item->texture;
}

void
texture_definition_map_free(struct CacheTextureMap* map)
{
    if( map )
    {
        struct HMapIter* iter = hmap_iter_new(map->map);
        struct CacheTextureMapEntry* item = NULL;
        while( (item = (struct CacheTextureMapEntry*)hmap_iter_next(iter)) != NULL )
        {
            texture_definition_free(item->texture);
        }
        hmap_iter_free(iter);

        hmap_free(map->map);
        free(map);
    }
}