#include "dat2a_textures.h"

#include "../dat2disk/dat2disk.h"
#include "../shared/shared_file_list.h"
#include "../shared/shared_rs_buffer.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct RSCacheDat2A_Texture*
RSCacheDat2A_TextureDefinitionNewFromCache(
    struct RSCacheDat2Disk* cache,
    int id)
{
    struct RSCacheShared_FileList* filelist = NULL;
    struct RSCacheDat2Disk_ArchiveReference* reference = NULL;
    struct RSCacheDat2A_Texture* def = NULL;
    struct RSCacheDat2Disk_Archive* archive = RSCacheDat2Disk_ArchiveNewLoad(cache, RSCacheDat2Disk_Table_Textures, 0);
    if( !archive )
        return NULL;

    filelist = RSCacheShared_FileListNewFromCacheArchive(archive);
    if( !filelist )
        return NULL;

    reference = &cache->tables[RSCacheDat2Disk_Table_Textures]->archives[0];
    if( !reference )
        return NULL;

    if( id == 34 )
    {
        printf("id: %d\n", id);
    }

    /**
     * Texture definition ids are mostly contiguous but id=54 is missing.
     * So files[54].id = 55.
     *
     * This is a quick workaround
     */
    int file_index = id;
    if( file_index >= 54 )
        file_index--;

    def = RSCacheDat2A_TextureDefinitionNewDecode(
        (const unsigned char*)filelist->files[file_index], filelist->file_sizes[file_index]);

    assert(def->sprite_ids);
    assert(reference->children.files[file_index].id == id);

    RSCacheShared_FileListFree(filelist);

    RSCacheDat2Disk_ArchiveFree(archive);

    assert(def);

    return def;
}

void
RSCacheDat2A_TextureDefinitionFree(struct RSCacheDat2A_Texture* texture_definition)
{
    RSCacheDat2A_TextureDefinitionFreeInplace(texture_definition);
    free(texture_definition);
}

void
RSCacheDat2A_TextureDefinitionFreeInplace(struct RSCacheDat2A_Texture* texture_definition)
{
    if( !texture_definition )
        return;

    free(texture_definition->sprite_ids);
    free(texture_definition->sprite_types);
    free(texture_definition->transforms);
}

struct RSCacheDat2A_Texture*
RSCacheDat2A_TextureDefinitionNewDecode(
    const unsigned char* data,
    int length)
{
    struct RSCacheDat2A_Texture* def = malloc(sizeof(struct RSCacheDat2A_Texture));
    memset(def, 0, sizeof(struct RSCacheDat2A_Texture));
    if( !def )
        return NULL;

    return RSCacheDat2A_TextureDefinitionDecodeInplace(def, data, length);
}

struct RSCacheDat2A_Texture*
RSCacheDat2A_TextureDefinitionDecodeInplace(
    struct RSCacheDat2A_Texture* def,
    const unsigned char* data,
    int length)
{
    struct RSCacheShared_RSBuffer buffer;
    RSCacheShared_RSBufferInit(&buffer, (uint8_t*)data, (uint32_t)length);

    // Read the fields
    def->average_hsl = RSCacheShared_RSBufferG2(&buffer);
    def->opaque = RSCacheShared_RSBufferG1b(&buffer) != 0;

    // Read count of files
    int count = RSCacheShared_RSBufferG1(&buffer);
    def->sprite_ids_count = count;

    // Allocate and read file IDs
    def->sprite_ids = malloc(count * sizeof(int));
    if( !def->sprite_ids )
        goto error;

    for( int i = 0; i < count; i++ )
        def->sprite_ids[i] = RSCacheShared_RSBufferG2(&buffer);

    // Handle field1780 (count > 1 case)
    if( count > 1 )
    {
        def->sprite_types = malloc((count - 1) * sizeof(int));
        if( !def->sprite_types )
            goto error;

        for( int i = 0; i < count - 1; i++ )
            def->sprite_types[i] = RSCacheShared_RSBufferG1(&buffer);
    }

    if( count > 1 )
    {
        // unused?
        for( int i = 0; i < count - 1; i++ )
            RSCacheShared_RSBufferG1(&buffer);
    }

    // Handle field1786
    def->transforms = malloc(count * sizeof(int));
    if( !def->transforms )
        goto error;

    for( int i = 0; i < count; i++ )
        def->transforms[i] = RSCacheShared_RSBufferG4(&buffer);

    // Read animation properties
    def->animation_direction = RSCacheShared_RSBufferG1(&buffer);
    def->animation_speed = RSCacheShared_RSBufferG1(&buffer);

    return def;

error:
    free(def->sprite_ids);
    free(def->sprite_types);
    free(def->transforms);
    free(def);
    return NULL;
}
