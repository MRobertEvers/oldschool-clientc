#include "dat2_buildcache_ui.h"

#include "toriauxlib/c/toriauxlibcache_font_convert.h"
#include "toriauxlib/c/toriauxlibcache_sprite_convert.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "osrs/rscache/shared/shared_rs_buffer.h"

#include <stdlib.h>
#include <string.h>

struct ToriAuxLibCore_Sprite*
dat2_buildcache_sprite_decode_from_archive(
    struct RSCacheDat2Disk_Archive* archive,
    struct RevConfigCacheItem const* item)
{
    return ToriAuxLibCache_SpriteNewFromDat2Archive(archive, item);
}

struct ToriAuxLibCore_Sprite*
dat2_buildcache_sprite_decode_id_from_archive(
    struct RSCacheDat2Disk_Archive* archive,
    int sprite_id)
{
    return ToriAuxLibCache_SpriteNewFromDat2ArchiveId(archive, sprite_id);
}

struct ToriAuxLibCore_Font*
dat2_buildcache_font_decode_from_archive(
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive,
    int font_id)
{
    return ToriAuxLibCache_FontNewFromDat2Archive(cache, archive, font_id);
}

static Component*
component_decode_from_bytes(
    int packed_id,
    char* data,
    int size)
{
    if( !data || size <= 0 )
        return NULL;

    Component* comp = calloc(1, sizeof(Component));
    if( !comp )
        return NULL;

    struct RSCacheShared_RSBuffer buf;
    RSCacheShared_RSBufferInit(&buf, (uint8_t*)data, size);
    Component_init(comp);
    comp->id = packed_id;
    if( (unsigned char)data[0] == (unsigned char)255 )
        Component_decodeIf3(comp, &buf);
    else
        Component_decodeIf1(comp, &buf);
    return comp;
}

Component*
dat2_buildcache_component_decode_iface_file_from_archive(
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive,
    int iface_id,
    int file_index,
    int* out_size)
{
    if( !cache || !archive )
    {
        RSCacheDat2Disk_ArchiveFree(archive);
        return NULL;
    }

    RSCacheDat2Disk_ArchiveInitMetadata(cache, archive);
    struct RSCacheShared_FileList* fl = RSCacheShared_FileListNewFromCacheArchive(archive);
    RSCacheDat2Disk_ArchiveFree(archive);
    if( !fl || file_index < 0 || file_index >= fl->file_count )
    {
        RSCacheShared_FileListFree(fl);
        return NULL;
    }

    char* data = fl->files[file_index];
    int size = fl->file_sizes[file_index];
    int packed_id = (iface_id << 16) | (file_index & 0xFFFF);
    Component* comp = component_decode_from_bytes(packed_id, data, size);

    if( out_size )
        *out_size = size;

    RSCacheShared_FileListFree(fl);
    return comp;
}

struct Dat2BuildCache_InterfaceArchive*
dat2_buildcache_component_decode_iface_archive_from_archive(
    struct RSCacheDat2Disk_ReferenceTable* reference_table,
    struct RSCacheDat2Disk_Archive* archive,
    int iface_id)
{
    if( !reference_table || !archive )
    {
        RSCacheDat2Disk_ArchiveFree(archive);
        return NULL;
    }

    RSCacheDat2Disk_ArchiveInitMetadataFromTable(reference_table, archive);
    struct RSCacheShared_FileList* fl = RSCacheShared_FileListNewFromCacheArchive(archive);
    RSCacheDat2Disk_ArchiveFree(archive);
    if( !fl || fl->file_count <= 0 )
    {
        RSCacheShared_FileListFree(fl);
        return NULL;
    }

    struct Dat2BuildCache_InterfaceArchive* iface_archive =
        calloc(1, sizeof(struct Dat2BuildCache_InterfaceArchive));
    if( !iface_archive )
    {
        RSCacheShared_FileListFree(fl);
        return NULL;
    }

    iface_archive->component_count = fl->file_count;
    iface_archive->components = calloc((size_t)fl->file_count, sizeof(Component*));
    if( !iface_archive->components )
    {
        free(iface_archive);
        RSCacheShared_FileListFree(fl);
        return NULL;
    }

    for( int i = 0; i < fl->file_count; i++ )
    {
        int packed_id = (iface_id << 16) | (i & 0xFFFF);
        iface_archive->components[i] =
            component_decode_from_bytes(packed_id, fl->files[i], fl->file_sizes[i]);
    }

    RSCacheShared_FileListFree(fl);
    return iface_archive;
}
