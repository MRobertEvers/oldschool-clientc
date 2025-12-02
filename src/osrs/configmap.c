#include "configmap.h"

#include "datastruct/hmap.h"
#include "filelist.h"
#include "tables/config_floortype.h"
#include "tables/config_idk.h"
#include "tables/config_locs.h"
#include "tables/config_npctype.h"
#include "tables/config_object.h"
#include "tables/config_sequence.h"
#include "tables/configs.h"

#include <assert.h>
#include <stdalign.h>

struct ConfigMap
{
    struct HMap* hmap;

    enum ConfigKind kind;
};

size_t
configsize(enum ConfigKind kind)
{
    switch( kind )
    {
    case CONFIG_UNDERLAY:
        return sizeof(struct CacheConfigUnderlay);
    case CONFIG_OVERLAY:
        return sizeof(struct CacheConfigOverlay);
    case CONFIG_OBJECT:
        return sizeof(struct CacheConfigObject);
    case CONFIG_SEQUENCE:
        return sizeof(struct CacheConfigSequence);
    case CONFIG_NPC:
        return sizeof(struct CacheConfigNPCType);
    case CONFIG_LOCS:
        return sizeof(struct CacheConfigLocation);
    default:
        assert(false);
        return 0;
    }
}

int
configalign(enum ConfigKind kind)
{
    switch( kind )
    {
    case CONFIG_UNDERLAY:
        return alignof(struct CacheConfigUnderlay);
    case CONFIG_OVERLAY:
        return alignof(struct CacheConfigOverlay);
    case CONFIG_OBJECT:
        return alignof(struct CacheConfigObject);
    case CONFIG_SEQUENCE:
        return alignof(struct CacheConfigSequence);
    case CONFIG_NPC:
        return alignof(struct CacheConfigNPCType);
    case CONFIG_LOCS:
        return alignof(struct CacheConfigLocation);
    default:
        assert(false);
        return 0;
    }
}

void
configfree(enum ConfigKind kind, void* ptr)
{
    switch( kind )
    {
    case CONFIG_UNDERLAY:
        config_floortype_underlay_free_inplace((struct CacheConfigUnderlay*)ptr);
        break;
    case CONFIG_OVERLAY:
        config_floortype_overlay_free_inplace((struct CacheConfigOverlay*)ptr);
        break;
    case CONFIG_OBJECT:
        // config_object_free((struct CacheConfigObject*)ptr);
        break;
    case CONFIG_SEQUENCE:
        config_sequence_free_inplace((struct CacheConfigSequence*)ptr);
        break;
    case CONFIG_NPC:
        // config_npctype_free((struct CacheConfigNPCType*)ptr);
        break;
    case CONFIG_LOCS:
        config_locs_free_inplace((struct CacheConfigLocation*)ptr);
        break;
    default:
        assert(false);
        break;
    }
}

void
configdecode(enum ConfigKind kind, void* ptr, int id, int revision, char* data, int data_size)
{
    switch( kind )
    {
    case CONFIG_UNDERLAY:
        config_floortype_underlay_decode_inplace((struct CacheConfigUnderlay*)ptr, data, data_size);
        break;
    case CONFIG_OVERLAY:
        config_floortype_overlay_decode_inplace((struct CacheConfigOverlay*)ptr, data, data_size);
        break;
    case CONFIG_OBJECT:
        config_object_decode_inplace((struct CacheConfigObject*)ptr, data, data_size);
        break;
    case CONFIG_SEQUENCE:
        config_sequence_decode_inplace((struct CacheConfigSequence*)ptr, revision, data, data_size);
        break;
    case CONFIG_LOCS:
        config_locs_decode_inplace((struct CacheConfigLocation*)ptr, data, data_size);
        break;

    case CONFIG_NPC:
        assert(false);
        break;
    }
}

static int
alignup(int size, int alignment)
{
    return (size + alignment - 1) & ~(alignment - 1);
}

struct ConfigMap*
configmap_new_from_archive(struct Cache* cache, struct CacheArchive* archive)
{
    assert(archive->table_id == CACHE_CONFIGS);

    struct ArchiveReference* archive_reference = NULL;
    struct FileList* file_list = NULL;

    struct HashConfig config = {
        .buffer = NULL,
        .buffer_size = 0,
        .key_size = sizeof(int),
        /**
         * Add 16 bytes to the entry size for the key and ensure it's aligned to 16 bytes.
         */
        .entry_size = alignup(configsize(archive->archive_id) + 4, 16),
    };
    struct ConfigMap* configmap = NULL;

    file_list = filelist_new_from_cache_archive(archive);
    configmap = malloc(sizeof(struct ConfigMap));
    memset(configmap, 0, sizeof(struct ConfigMap));

    int target_capacity = file_list->file_count < 1024 ? 1024 : file_list->file_count * 2;

    int buffer_size = target_capacity * (configsize(archive->archive_id) + 32);
    config.buffer = malloc(buffer_size);
    memset(config.buffer, 0, buffer_size);

    config.buffer_size = buffer_size;

    configmap->hmap = hmap_new(&config, 0);

    archive_reference = &cache->tables[CACHE_CONFIGS]->archives[archive->archive_id];

    void* ptr = NULL;
    for( int i = 0; i < file_list->file_count; i++ )
    {
        int id = archive_reference->children.files[i].id;

        ptr = hmap_search(configmap->hmap, &id, HMAP_INSERT);

        assert(ptr);

        if( id == 21 )
        {
            printf("Found id 179\n");
            printf("ptr: %p\n", ptr);
        }

        ptr = ((int8_t*)ptr) + 16;

        configdecode(
            archive->archive_id,
            ptr,
            id,
            archive_reference->version,
            file_list->files[i],
            file_list->file_sizes[i]);
    }

    configmap->kind = archive->archive_id;

    filelist_free(file_list);

    return configmap;
}

void
configmap_free(struct ConfigMap* configmap)
{
    struct HMapIter* iter = hmap_iter_new(configmap->hmap);
    void* ptr = NULL;
    while( (ptr = hmap_iter_next(iter)) )
        configfree(configmap->kind, ptr);
    hmap_iter_free(iter);

    free(hmap_buffer_ptr(configmap->hmap));
    hmap_free(configmap->hmap);
    free(configmap);
}

enum ConfigKind
configmap_kind(struct ConfigMap* configmap)
{
    return configmap->kind;
}

void*
configmap_get(struct ConfigMap* configmap, int id)
{
    void* ptr = hmap_search(configmap->hmap, &id, HMAP_FIND);

    if( !ptr )
        return NULL;

    // Only return the user data.
    return ((int8_t*)ptr) + 16;
}