#include "configmap.h"

#include "filelist.h"
#include "tables/config_floortype.h"
#include "tables/config_idk.h"
#include "tables/config_locs.h"
#include "tables/config_npctype.h"
#include "tables/config_object.h"
#include "tables/config_sequence.h"
#include "tables/configs.h"
#include "tables/textures.h"

#include <assert.h>
#include <math.h>
#include <stdalign.h>

static const int METADATA_ID = 0xDEADBEEF;

static inline int
imax(
    int a,
    int b)
{
    return a > b ? a : b;
}

struct MetadataEntry
{
    int id;

    int table_id;
    int archive_id;
};

static int
iterable(
    void* entry,
    void* arg)
{
    struct MetadataEntry* metadata = (struct MetadataEntry*)entry;
    if( metadata->id == METADATA_ID )
        return 0;
    return 1;
}

struct ConfigUnderlayEntry
{
    int id;
    struct CacheConfigUnderlay underlay;
};

struct ConfigOverlayEntry
{
    int id;
    struct CacheConfigOverlay overlay;
};

struct ConfigObjectEntry
{
    int id;
    struct CacheConfigObject object;
};

struct ConfigSequenceEntry
{
    int id;
    struct CacheConfigSequence sequence;
};

struct ConfigNPCEntry
{
    int id;
    struct CacheConfigNPCType npc;
};

struct ConfigLocsEntry
{
    int id;
    struct CacheConfigLocation loc;
};

struct ConfigTexturesEntry
{
    int id;
    struct CacheTexture texture;
};

static size_t
configsize(
    int table_id,
    int archive_id)
{
    if( table_id == CACHE_CONFIGS )
    {
        switch( archive_id )
        {
        case CONFIG_UNDERLAY:
            return sizeof(struct ConfigUnderlayEntry);
        case CONFIG_OVERLAY:
            return sizeof(struct ConfigOverlayEntry);
        case CONFIG_OBJECT:
            return sizeof(struct ConfigObjectEntry);
        case CONFIG_SEQUENCE:
            return sizeof(struct ConfigSequenceEntry);
        case CONFIG_NPC:
            return sizeof(struct ConfigNPCEntry);
        case CONFIG_LOCS:
            return sizeof(struct ConfigLocsEntry);
        default:
            assert(false);
            return 0;
        }
    }
    else if( table_id == CACHE_TEXTURES )
    {
        return sizeof(struct ConfigTexturesEntry);
    }
    else
    {
        assert(false);
        return 0;
    }
}

static int
configalign(
    int table_id,
    int archive_id)
{
    if( table_id == CACHE_CONFIGS )
    {
        switch( archive_id )
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
    else if( table_id == CACHE_TEXTURES )
    {
        return alignof(struct CacheTexture);
    }
    else
    {
        assert(false);
        return 0;
    }
}

static void
configfree(
    int table_id,
    int archive_id,
    void* ptr)
{
    if( table_id == CACHE_CONFIGS )
    {
        switch( archive_id )
        {
        case CONFIG_UNDERLAY:
            config_floortype_underlay_free_inplace(&((struct ConfigUnderlayEntry*)ptr)->underlay);
            break;
        case CONFIG_OVERLAY:
            config_floortype_overlay_free_inplace(&((struct ConfigOverlayEntry*)ptr)->overlay);
            break;
        case CONFIG_OBJECT:
            // config_object_free((struct CacheConfigObject*)ptr);
            break;
        case CONFIG_SEQUENCE:
            config_sequence_free_inplace(&((struct ConfigSequenceEntry*)ptr)->sequence);
            break;
        case CONFIG_NPC:
            // config_npctype_free((struct CacheConfigNPCType*)ptr);
            break;
        case CONFIG_LOCS:
            config_locs_free_inplace(&((struct ConfigLocsEntry*)ptr)->loc);
            break;
        default:
            assert(false);
            break;
        }
    }
    else if( table_id == CACHE_TEXTURES )
    {
        texture_definition_free_inplace(&((struct ConfigTexturesEntry*)ptr)->texture);
    }
    else
    {
        assert(false);
    }
}

static void
configdecode(
    int table_id,
    int archive_id,
    void* ptr,
    int id,
    int revision,
    char* data,
    int data_size)
{
    if( table_id == CACHE_CONFIGS )
    {
        switch( archive_id )
        {
        case CONFIG_UNDERLAY:
        {
            struct ConfigUnderlayEntry* entry = (struct ConfigUnderlayEntry*)ptr;
            entry->id = id;
            config_floortype_underlay_decode_inplace(&entry->underlay, data, data_size);
        }
        break;
        case CONFIG_OVERLAY:
        {
            struct ConfigOverlayEntry* entry = (struct ConfigOverlayEntry*)ptr;
            entry->id = id;
            config_floortype_overlay_decode_inplace(&entry->overlay, data, data_size);
        }
        break;
        case CONFIG_OBJECT:
        {
            struct ConfigObjectEntry* entry = (struct ConfigObjectEntry*)ptr;
            entry->id = id;
            config_object_decode_inplace(&entry->object, data, data_size);
        }
        break;
        case CONFIG_SEQUENCE:
        {
            struct ConfigSequenceEntry* entry = (struct ConfigSequenceEntry*)ptr;
            entry->id = id;
            config_sequence_decode_inplace(&entry->sequence, revision, data, data_size);
        }
        break;
        case CONFIG_LOCS:
        {
            struct ConfigLocsEntry* entry = (struct ConfigLocsEntry*)ptr;
            entry->id = id;
            if( id == 3125 )
            {
                printf("config_locs_decode_inplace: %d\n", id);
            }
            config_locs_decode_inplace(&entry->loc, data, data_size);
            entry->loc._id = id;
        }
        break;

        case CONFIG_NPC:
        {
            assert(false);
        }
        break;
        }
    }
    else if( table_id == CACHE_TEXTURES )
    {
        struct ConfigTexturesEntry* entry = (struct ConfigTexturesEntry*)ptr;
        entry->id = id;
        texture_definition_decode_inplace(&entry->texture, data, data_size);
        entry->texture._id = id;
    }
    else
    {
        assert(false);
    }
}

void*
configvalue(
    int table_id,
    int archive_id,
    void* ptr)
{
    if( table_id == CACHE_CONFIGS )
    {
        switch( archive_id )
        {
        case CONFIG_UNDERLAY:
            return &((struct ConfigUnderlayEntry*)ptr)->underlay;
        case CONFIG_OVERLAY:
            return &((struct ConfigOverlayEntry*)ptr)->overlay;
        case CONFIG_OBJECT:
            return &((struct ConfigObjectEntry*)ptr)->object;
        case CONFIG_SEQUENCE:
            return &((struct ConfigSequenceEntry*)ptr)->sequence;
        case CONFIG_NPC:
            return &((struct ConfigNPCEntry*)ptr)->npc;
        case CONFIG_LOCS:
        {
            struct ConfigLocsEntry* entry = (struct ConfigLocsEntry*)ptr;
            return &entry->loc;
        }
        default:
            assert(false);
            return NULL;
        }
    }
    else if( table_id == CACHE_TEXTURES )
    {
        return &((struct ConfigTexturesEntry*)ptr)->texture;
    }
    else
    {
        assert(false);
    }
    return ptr;
}

static int
alignup(
    int size,
    int alignment)
{
    return (size + alignment - 1) & ~(alignment - 1);
}

struct ConfigMapPacked*
configmap_packed_new(
    struct Cache* cache,
    struct CacheArchive* archive)
{
    int data_size = 0;
    int offset = 0;
    int filelist_reference_size = 0;
    struct ArchiveReference* archive_reference = NULL;
    struct ConfigMapPacked* packed = malloc(sizeof(struct ConfigMapPacked));
    memset(packed, 0, sizeof(struct ConfigMapPacked));

    archive_reference = &cache->tables[archive->table_id]->archives[archive->archive_id];
    filelist_reference_size =
        archive_reference->children.count * sizeof(archive_reference->children.files[0]);

    data_size += sizeof(int); // filelist reference size
    data_size += sizeof(int); // revision
    data_size += sizeof(int); // file count
    data_size += sizeof(int); // archive id
    data_size += sizeof(int); // table id
    data_size += filelist_reference_size;
    data_size += archive->data_size;

    packed->data = malloc(data_size);
    packed->data_size = data_size;
    memset(packed->data, 0, packed->data_size);

    memcpy((uint8_t*)packed->data + offset, &filelist_reference_size, sizeof(int));
    offset += sizeof(int);
    memcpy((uint8_t*)packed->data + offset, &archive_reference->version, sizeof(int));
    offset += sizeof(int);
    memcpy((uint8_t*)packed->data + offset, &archive_reference->children.count, sizeof(int));
    offset += sizeof(int);
    memcpy((uint8_t*)packed->data + offset, &archive->archive_id, sizeof(int));
    offset += sizeof(int);
    memcpy((uint8_t*)packed->data + offset, &archive->table_id, sizeof(int));
    offset += sizeof(int);
    memcpy(
        (uint8_t*)packed->data + offset,
        archive_reference->children.files,
        filelist_reference_size);
    offset += filelist_reference_size;
    memcpy((uint8_t*)packed->data + offset, archive->data, archive->data_size);
    offset += archive->data_size;

    return packed;
}

void
configmap_packed_free(struct ConfigMapPacked* packed)
{
    free(packed->data);
    free(packed);
}

static bool
ids_contains(
    int* ids_nullable,
    int ids_size,
    int id)
{
    for( int i = 0; i < ids_size; i++ )
    {
        if( ids_nullable[i] == id )
            return true;
    }
    return false;
}

struct DashMap*
configmap_new_from_packed(
    void* data,
    int data_size,
    int* ids_nullable,
    int ids_size)
{
    struct ArchiveReference* archive_reference = NULL;
    struct FileList* file_list = NULL;

    struct DashMapConfig config = { 0 };
    struct DashMap* dashmap = NULL;
    struct MetadataEntry* metadata = NULL;
    struct ArchiveFileReference* archive_file_references = NULL;

    int offset = 0;
    int filelist_reference_size = 0;
    int revision = 0;
    int file_count = 0;
    int archive_id = 0;
    int table_id = 0;

    memcpy(&filelist_reference_size, (uint8_t*)data + offset, sizeof(int));
    offset += sizeof(int);
    memcpy(&revision, (uint8_t*)data + offset, sizeof(int));
    offset += sizeof(int);
    memcpy(&file_count, (uint8_t*)data + offset, sizeof(int));
    offset += sizeof(int);
    memcpy(&archive_id, (uint8_t*)data + offset, sizeof(int));
    offset += sizeof(int);
    memcpy(&table_id, (uint8_t*)data + offset, sizeof(int));
    offset += sizeof(int);

    archive_file_references = (struct ArchiveFileReference*)((uint8_t*)data + offset);
    offset += filelist_reference_size;

    file_list = filelist_new_from_decode((uint8_t*)data + offset, data_size - offset, file_count);

    int count = ids_nullable ? ids_size : file_list->file_count;
    int target_capacity = count < 512 ? 1024 : count * 2;

    int element_size = imax(sizeof(struct MetadataEntry), configsize(table_id, archive_id));
    int buffer_size = target_capacity * (element_size + 32);
    config.buffer = malloc(buffer_size);
    config.buffer_size = buffer_size;
    config.key_size = sizeof(int);
    config.entry_size = element_size;
    config.iterable_fn_nullable = iterable;

    memset(config.buffer, 0, buffer_size);
    dashmap = dashmap_new(&config, 0);

    void* ptr = NULL;
    for( int i = 0; i < file_list->file_count; i++ )
    {
        int id = archive_file_references[i].id;

        if( ids_nullable && !ids_contains(ids_nullable, ids_size, id) )
            continue;

        ptr = dashmap_search(dashmap, &id, DASHMAP_INSERT);

        assert(ptr);

        configdecode(
            table_id, archive_id, ptr, id, revision, file_list->files[i], file_list->file_sizes[i]);
    }

    filelist_free(file_list);

    // Insert metadata

    metadata = (struct MetadataEntry*)dashmap_search(dashmap, &METADATA_ID, DASHMAP_INSERT);
    assert(metadata);

    metadata->id = METADATA_ID;
    metadata->table_id = table_id;
    metadata->archive_id = archive_id;

    return dashmap;
}

static inline struct MetadataEntry*
configmap_metadata_get(struct DashMap* dashmap)
{
    return (struct MetadataEntry*)dashmap_search(dashmap, &METADATA_ID, DASHMAP_FIND);
}

void
configmap_free(struct DashMap* dashmap)
{
    if( !dashmap )
        return;

    struct MetadataEntry* metadata = configmap_metadata_get(dashmap);

    struct DashMapIter* iter = dashmap_iter_new(dashmap);
    void* ptr = NULL;
    while( (ptr = dashmap_iter_next(iter)) )
        configfree(metadata->table_id, metadata->archive_id, ptr);

    dashmap_iter_free(iter);

    free(dashmap_buffer_ptr(dashmap));
    dashmap_free(dashmap);
}

void*
configmap_get(
    struct DashMap* configmap,
    int id)
{
    struct MetadataEntry* metadata = configmap_metadata_get(configmap);
    void* ptr = dashmap_search(configmap, &id, DASHMAP_FIND);
    assert(metadata);
    assert(ptr);

    return configvalue(metadata->table_id, metadata->archive_id, ptr);
}

void*
configmap_iter_next(struct DashMapIter* iter)
{
    void* ptr = dashmap_iter_next(iter);
    if( !ptr )
        return NULL;

    struct MetadataEntry* metadata = configmap_metadata_get(dashmap_iter_get_map(iter));
    return configvalue(metadata->table_id, metadata->archive_id, ptr);
}

bool
configmap_valid(struct DashMap* configmap)
{
    struct MetadataEntry* metadata = configmap_metadata_get(configmap);
    return metadata != NULL;
}