#include "configmap.h"

#include "filepack.h"
#include "osrs/rscache/shared/rscache_shared_file_list.h"
#include "osrs/rscache/dat2a/rscache_dat2a_config_floortype.h"
#include "osrs/rscache/dat2a/rscache_dat2a_config_idk.h"
#include "osrs/rscache/dat2a/rscache_dat2a_config_locs.h"
#include "osrs/rscache/dat2a/rscache_dat2a_config_npctype.h"
#include "osrs/rscache/dat2a/rscache_dat2a_config_object.h"
#include "osrs/rscache/dat2a/rscache_dat2a_config_sequence.h"
#include "osrs/rscache/dat2a/rscache_dat2a_configs.h"
#include "osrs/rscache/dat2a/rscache_dat2a_frame.h"
#include "osrs/rscache/dat2a/rscache_dat2a_textures.h"

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
    struct RSCacheDat2A_ConfigUnderlay underlay;
};

struct ConfigOverlayEntry
{
    int id;
    struct RSCacheDat2A_ConfigOverlay overlay;
};

struct ConfigObjectEntry
{
    int id;
    struct RSCacheDat2A_ConfigObject object;
};

struct ConfigSequenceEntry
{
    int id;
    struct RSCacheDat2A_ConfigSequence sequence;
};

struct ConfigNPCEntry
{
    int id;
    struct RSCacheDat2A_ConfigNpctype npc;
};

struct ConfigLocsEntry
{
    int id;
    struct RSCacheDat2A_ConfigLocation loc;
};

struct ConfigTexturesEntry
{
    int id;
    struct RSCacheDat2A_Texture texture;
};

struct FrameEntry
{
    int id;
    struct RSCacheDat2A_Frame frame;
};

static size_t
configsize(
    int table_id,
    int archive_id)
{
    if( table_id == RSCacheDat2Disk_Table_Configs )
    {
        switch( archive_id )
        {
        case RSCacheDat2A_ConfigKind_Underlay:
            return sizeof(struct ConfigUnderlayEntry);
        case RSCacheDat2A_ConfigKind_Overlay:
            return sizeof(struct ConfigOverlayEntry);
        case RSCacheDat2A_ConfigKind_Object:
            return sizeof(struct ConfigObjectEntry);
        case RSCacheDat2A_ConfigKind_Sequence:
            return sizeof(struct ConfigSequenceEntry);
        case RSCacheDat2A_ConfigKind_Npc:
            return sizeof(struct ConfigNPCEntry);
        case RSCacheDat2A_ConfigKind_Locs:
            return sizeof(struct ConfigLocsEntry);
        default:
            assert(false);
            return 0;
        }
    }
    else if( table_id == RSCacheDat2Disk_Table_Textures )
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
    if( table_id == RSCacheDat2Disk_Table_Configs )
    {
        switch( archive_id )
        {
        case RSCacheDat2A_ConfigKind_Underlay:
            return alignof(struct RSCacheDat2A_ConfigUnderlay);
        case RSCacheDat2A_ConfigKind_Overlay:
            return alignof(struct RSCacheDat2A_ConfigOverlay);
        case RSCacheDat2A_ConfigKind_Object:
            return alignof(struct RSCacheDat2A_ConfigObject);
        case RSCacheDat2A_ConfigKind_Sequence:
            return alignof(struct RSCacheDat2A_ConfigSequence);
        case RSCacheDat2A_ConfigKind_Npc:
            return alignof(struct RSCacheDat2A_ConfigNpctype);
        case RSCacheDat2A_ConfigKind_Locs:
            return alignof(struct RSCacheDat2A_ConfigLocation);
        default:
            assert(false);
            return 0;
        }
    }
    else if( table_id == RSCacheDat2Disk_Table_Textures )
    {
        return alignof(struct RSCacheDat2A_Texture);
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
    if( table_id == RSCacheDat2Disk_Table_Configs )
    {
        switch( archive_id )
        {
        case RSCacheDat2A_ConfigKind_Underlay:
            config_floortype_underlay_free_inplace(&((struct ConfigUnderlayEntry*)ptr)->underlay);
            break;
        case RSCacheDat2A_ConfigKind_Overlay:
            config_floortype_overlay_free_inplace(&((struct ConfigOverlayEntry*)ptr)->overlay);
            break;
        case RSCacheDat2A_ConfigKind_Object:
            // RSCacheDat2A_ConfigObjectFree((struct RSCacheDat2A_ConfigObject*)ptr);
            break;
        case RSCacheDat2A_ConfigKind_Sequence:
            RSCacheDat2A_ConfigSequenceFreeInplace(&((struct ConfigSequenceEntry*)ptr)->sequence);
            break;
        case RSCacheDat2A_ConfigKind_Npc:
            // RSCacheDat2A_ConfigNpctypeFree((struct RSCacheDat2A_ConfigNpctype*)ptr);
            break;
        case RSCacheDat2A_ConfigKind_Locs:
            config_locs_free_inplace(&((struct ConfigLocsEntry*)ptr)->loc);
            break;
        default:
            assert(false);
            break;
        }
    }
    else if( table_id == RSCacheDat2Disk_Table_Textures )
    {
        RSCacheDat2A_TextureDefinitionFreeInplace(&((struct ConfigTexturesEntry*)ptr)->texture);
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
    if( table_id == RSCacheDat2Disk_Table_Configs )
    {
        switch( archive_id )
        {
        case RSCacheDat2A_ConfigKind_Underlay:
        {
            struct ConfigUnderlayEntry* entry = (struct ConfigUnderlayEntry*)ptr;
            entry->id = id;
            config_floortype_underlay_decode_inplace(&entry->underlay, data, data_size);
        }
        break;
        case RSCacheDat2A_ConfigKind_Overlay:
        {
            struct ConfigOverlayEntry* entry = (struct ConfigOverlayEntry*)ptr;
            entry->id = id;
            config_floortype_overlay_decode_inplace(&entry->overlay, data, data_size);
        }
        break;
        case RSCacheDat2A_ConfigKind_Object:
        {
            struct ConfigObjectEntry* entry = (struct ConfigObjectEntry*)ptr;
            entry->id = id;
            RSCacheDat2A_ConfigObjectDecodeInplace(&entry->object, data, data_size);
        }
        break;
        case RSCacheDat2A_ConfigKind_Sequence:
        {
            struct ConfigSequenceEntry* entry = (struct ConfigSequenceEntry*)ptr;
            entry->id = id;
            RSCacheDat2A_ConfigSequenceDecodeInplace(&entry->sequence, revision, data, data_size);
            entry->sequence.id = id;
        }
        break;
        case RSCacheDat2A_ConfigKind_Locs:
        {
            struct ConfigLocsEntry* entry = (struct ConfigLocsEntry*)ptr;
            entry->id = id;
            if( id == 3125 )
            {
                printf("config_locs_decode_inplace: %d\n", id);
            }
            config_locs_decode_inplace(&entry->loc, data, data_size, CONFIG_LOC_DECODE_DAT2);
            entry->loc._id = id;
        }
        break;

        case RSCacheDat2A_ConfigKind_Npc:
        {
            assert(false);
        }
        break;
        }
    }
    else if( table_id == RSCacheDat2Disk_Table_Textures )
    {
        struct ConfigTexturesEntry* entry = (struct ConfigTexturesEntry*)ptr;
        entry->id = id;
        RSCacheDat2A_TextureDefinitionDecodeInplace(&entry->texture, data, data_size);
        entry->texture._id = id;
    }
    else if( table_id == RSCacheDat2Disk_Table_Animations )
    {
        struct FrameEntry* entry = (struct FrameEntry*)ptr;
        entry->id = id;
        // frame_decode_inplace(&entry->frame, revision, data, data_size);
        entry->frame._id = id;
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
    void* ptr,
    int* out_id)
{
    int dummy = 0;
    if( !out_id )
    {
        out_id = &dummy;
    }

    if( table_id == RSCacheDat2Disk_Table_Configs )
    {
        switch( archive_id )
        {
        case RSCacheDat2A_ConfigKind_Underlay:
        {
            struct ConfigUnderlayEntry* entry = (struct ConfigUnderlayEntry*)ptr;
            *out_id = entry->id;
            return &entry->underlay;
        }
        case RSCacheDat2A_ConfigKind_Overlay:
        {
            struct ConfigOverlayEntry* entry = (struct ConfigOverlayEntry*)ptr;
            *out_id = entry->id;
            entry->overlay._id = entry->id;
            return &entry->overlay;
        }
        case RSCacheDat2A_ConfigKind_Object:
        {
            struct ConfigObjectEntry* entry = (struct ConfigObjectEntry*)ptr;
            *out_id = entry->id;
            entry->object._id = entry->id;
            return &entry->object;
        }
        case RSCacheDat2A_ConfigKind_Sequence:
        {
            struct ConfigSequenceEntry* entry = (struct ConfigSequenceEntry*)ptr;
            *out_id = entry->id;
            return &entry->sequence;
        }
        case RSCacheDat2A_ConfigKind_Npc:
        {
            struct ConfigNPCEntry* entry = (struct ConfigNPCEntry*)ptr;
            *out_id = entry->id;
            return &entry->npc;
        }
        case RSCacheDat2A_ConfigKind_Locs:
        {
            struct ConfigLocsEntry* entry = (struct ConfigLocsEntry*)ptr;
            *out_id = entry->id;
            entry->loc._id = entry->id;
            return &entry->loc;
        }
        default:
            assert(false);
            return NULL;
        }
    }
    else if( table_id == RSCacheDat2Disk_Table_Textures )
    {
        struct ConfigTexturesEntry* entry = (struct ConfigTexturesEntry*)ptr;
        entry->texture._id = entry->id;
        *out_id = entry->id;
        return &entry->texture;
    }
    else if( table_id == RSCacheDat2Disk_Table_Animations )
    {
        struct FrameEntry* entry = (struct FrameEntry*)ptr;
        entry->frame._id = entry->id;
        *out_id = entry->id;
        return &entry->frame;
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
configmap_new_from_filepack(
    void* data,
    int data_size,
    int* ids_nullable,
    int ids_size)
{
    struct RSCacheDat2Disk_ArchiveReference* archive_reference = NULL;
    struct RSCacheShared_FileList* file_list = NULL;

    struct DashMapConfig config = { 0 };
    struct DashMap* dashmap = NULL;
    struct MetadataEntry* metadata = NULL;
    struct RSCacheDat2Disk_ArchiveFileReference* archive_file_references = NULL;

    struct FileMetadata file_metadata = { 0 };
    struct FilePack filepack = { .data = data, .data_size = data_size };
    filepack_metadata(&filepack, &file_metadata);

    int table_id = file_metadata.table_id;
    int archive_id = file_metadata.archive_id;
    int revision = file_metadata.revision;

    archive_file_references = (struct RSCacheDat2Disk_ArchiveFileReference*)(file_metadata.filelist_reference_ptr_);

    file_list = RSCacheShared_FileListNewFromDecode(
        file_metadata.data_ptr_, file_metadata.data_size, file_metadata.file_count);

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

    RSCacheShared_FileListFree(file_list);

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
    assert(ptr && "Config must be found");

    return configvalue(metadata->table_id, metadata->archive_id, ptr, NULL);
}

void*
configmap_iter_next(
    struct DashMapIter* iter,
    int* out_id)
{
    void* ptr = dashmap_iter_next(iter);
    if( !ptr )
        return NULL;

    struct MetadataEntry* metadata = configmap_metadata_get(dashmap_iter_get_map(iter));
    return configvalue(metadata->table_id, metadata->archive_id, ptr, out_id);
}

bool
configmap_valid(struct DashMap* configmap)
{
    struct MetadataEntry* metadata = configmap_metadata_get(configmap);
    return metadata != NULL;
}