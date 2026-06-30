#include "dat2_buildcache_ui.h"

#include "osrs/rscache/dat2a/dat2a_sprites.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "osrs/rscache/shared/shared_rs_buffer.h"
#include "toridraw/toridraw_sprite.h"

#include <stdlib.h>
#include <string.h>

void
dat2_buildcache_ui_set_disk(
    struct Dat2BuildCache* buildcache,
    struct RSCacheDat2Disk* disk)
{
    if( !buildcache )
        return;
    buildcache->ui_disk = disk;
}

struct RSCacheDat2Disk*
dat2_buildcache_ui_disk(struct Dat2BuildCache* buildcache)
{
    return buildcache ? buildcache->ui_disk : NULL;
}

static struct ToriDraw_Sprite*
sprite_decode_from_pack_frame(
    struct RSCacheDat2A_SpritePack const* pack,
    int frame_index)
{
    if( !pack || frame_index < 0 || frame_index >= pack->count )
        return NULL;

    struct RSCacheDat2A_Sprite* def = &pack->sprites[frame_index];
    int* px = RSCacheDat2A_SpriteGetPixels(def, pack->palette, 0);
    if( !px )
        return NULL;

    struct ToriDraw_Sprite* sprite = ToriDraw_SpriteNewFromArgbOwned(
        (uint32_t*)px,
        def->crop_width > 0 ? def->crop_width : def->width,
        def->crop_height > 0 ? def->crop_height : def->height);
    if( !sprite )
        return NULL;

    sprite->crop_x = def->offset_x;
    sprite->crop_y = def->offset_y;
    sprite->crop_width = sprite->width;
    sprite->crop_height = sprite->height;
    return sprite;
}

static void
sprite_apply_transforms(
    struct ToriDraw_Sprite* sprite,
    struct RevConfigCacheItem const* item)
{
    if( !sprite || !item )
        return;
    for( int i = 0; i < item->transform_count; i++ )
    {
        if( strcmp(item->transform[i], "flip_h") == 0 )
            ToriDraw_SpriteFlipHorizontal(sprite);
        else if( strcmp(item->transform[i], "flip_v") == 0 )
            ToriDraw_SpriteFlipVertical(sprite);
    }
}

static void
sprite_apply_crop(
    struct ToriDraw_Sprite* sprite,
    struct RevConfigCacheItem const* item)
{
    if( !sprite || !item )
        return;
    if( item->crop_width > 0 && item->crop_height > 0 )
    {
        sprite->crop_x = item->crop_x;
        sprite->crop_y = item->crop_y;
        sprite->crop_width = item->crop_width;
        sprite->crop_height = item->crop_height;
    }
}

static struct ToriDraw_Sprite**
sprite_decode_pack_frames(
    struct RSCacheDat2A_SpritePack* pack,
    struct RevConfigCacheItem const* item,
    int start,
    int count,
    int* out_count)
{
    if( out_count )
        *out_count = 0;
    if( !pack || count <= 0 )
        return NULL;

    struct ToriDraw_Sprite** sprites = calloc((size_t)count, sizeof(struct ToriDraw_Sprite*));
    if( !sprites )
        return NULL;

    int loaded = 0;
    for( int j = 0; j < count; j++ )
    {
        int frame_index = start + j;
        if( frame_index < 0 || frame_index >= pack->count )
            continue;

        sprites[j] = sprite_decode_from_pack_frame(pack, frame_index);
        if( sprites[j] )
        {
            sprite_apply_transforms(sprites[j], item);
            sprite_apply_crop(sprites[j], item);
            loaded++;
        }
    }

    if( loaded <= 0 )
    {
        free(sprites);
        return NULL;
    }

    if( out_count )
        *out_count = count;
    return sprites;
}

struct ToriDraw_Sprite**
dat2_buildcache_sprite_decode(
    struct Dat2BuildCache* buildcache,
    struct RevConfigCacheItem const* item,
    int* out_count)
{
    if( out_count )
        *out_count = 0;
    struct RSCacheDat2Disk* disk = dat2_buildcache_ui_disk(buildcache);
    if( !disk || !item || item->archive_id < 0 )
        return NULL;

    struct RSCacheDat2A_SpritePack* pack =
        RSCacheDat2A_SpritePackNewFromCache(disk, item->archive_id);
    if( !pack || pack->count <= 0 )
    {
        if( pack )
            RSCacheDat2A_SpritePackFree(pack);
        return NULL;
    }

    int start = item->atlas_count > 0 ? 0 : item->atlas_index;
    int count = item->atlas_count > 0 ? item->atlas_count : 1;

    struct ToriDraw_Sprite** sprites =
        sprite_decode_pack_frames(pack, item, start, count, out_count);
    RSCacheDat2A_SpritePackFree(pack);
    return sprites;
}

struct ToriDraw_Sprite**
dat2_buildcache_sprite_decode_id(
    struct Dat2BuildCache* buildcache,
    int sprite_id,
    int* out_count)
{
    if( out_count )
        *out_count = 0;
    struct RSCacheDat2Disk* disk = dat2_buildcache_ui_disk(buildcache);
    if( !disk || sprite_id < 0 )
        return NULL;

    struct RSCacheDat2A_SpritePack* pack = RSCacheDat2A_SpritePackNewFromCache(disk, sprite_id);
    if( !pack || pack->count <= 0 )
    {
        if( pack )
            RSCacheDat2A_SpritePackFree(pack);
        return NULL;
    }

    struct RevConfigCacheItem item = { 0 };
    item.archive_id = sprite_id;
    item.atlas_count = pack->count;

    struct ToriDraw_Sprite** sprites =
        sprite_decode_pack_frames(pack, &item, 0, pack->count, out_count);
    RSCacheDat2A_SpritePackFree(pack);
    return sprites;
}

struct ToriDraw_Font*
dat2_buildcache_font_decode_id(
    struct Dat2BuildCache* buildcache,
    int font_id)
{
    struct RSCacheDat2Disk* disk = dat2_buildcache_ui_disk(buildcache);
    if( !disk || font_id < 0 )
        return NULL;

    struct RSCacheDat2Disk_Archive* arch =
        RSCacheDat2Disk_ArchiveNewLoad(disk, RSCacheDat2Disk_Table_Fonts, font_id);
    if( !arch )
        return NULL;

    RSCacheDat2Disk_ArchiveInitMetadata(disk, arch);
    struct RSCacheShared_FileList* fl = RSCacheShared_FileListNewFromCacheArchive(arch);
    if( !fl || fl->file_count <= 0 )
    {
        RSCacheShared_FileListFree(fl);
        RSCacheDat2Disk_ArchiveFree(arch);
        return NULL;
    }

    void* data = fl->files[0];
    int data_size = fl->file_sizes[0];
    void* index_data = fl->file_count > 1 ? fl->files[1] : NULL;
    int index_size = fl->file_count > 1 ? fl->file_sizes[1] : 0;

    struct ToriDraw_Font* font =
        ToriDraw_FontNewFromRSBytes(data, data_size, index_data, index_size);

    RSCacheShared_FileListFree(fl);
    RSCacheDat2Disk_ArchiveFree(arch);
    return font;
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
dat2_buildcache_component_decode_iface_file(
    struct Dat2BuildCache* buildcache,
    int iface_id,
    int file_index,
    int* out_size)
{
    struct RSCacheDat2Disk* disk = dat2_buildcache_ui_disk(buildcache);
    if( !disk )
        return NULL;

    struct RSCacheDat2Disk_Archive* arch =
        RSCacheDat2Disk_ArchiveNewLoad(disk, RSCacheDat2Disk_Table_Interfaces, iface_id);
    if( !arch )
        return NULL;

    RSCacheDat2Disk_ArchiveInitMetadata(disk, arch);
    struct RSCacheShared_FileList* fl = RSCacheShared_FileListNewFromCacheArchive(arch);
    if( !fl || file_index < 0 || file_index >= fl->file_count )
    {
        RSCacheShared_FileListFree(fl);
        RSCacheDat2Disk_ArchiveFree(arch);
        return NULL;
    }

    char* data = fl->files[file_index];
    int size = fl->file_sizes[file_index];
    int packed_id = (iface_id << 16) | (file_index & 0xFFFF);
    Component* comp = component_decode_from_bytes(packed_id, data, size);

    if( out_size )
        *out_size = size;

    RSCacheShared_FileListFree(fl);
    RSCacheDat2Disk_ArchiveFree(arch);
    return comp;
}

struct Dat2BuildCache_InterfaceArchive*
dat2_buildcache_component_decode_iface_archive(
    struct Dat2BuildCache* buildcache,
    int iface_id)
{
    struct RSCacheDat2Disk* disk = dat2_buildcache_ui_disk(buildcache);
    if( !disk )
        return NULL;

    struct RSCacheDat2Disk_Archive* arch =
        RSCacheDat2Disk_ArchiveNewLoad(disk, RSCacheDat2Disk_Table_Interfaces, iface_id);
    if( !arch )
        return NULL;

    RSCacheDat2Disk_ArchiveInitMetadata(disk, arch);
    struct RSCacheShared_FileList* fl = RSCacheShared_FileListNewFromCacheArchive(arch);
    if( !fl || fl->file_count <= 0 )
    {
        RSCacheShared_FileListFree(fl);
        RSCacheDat2Disk_ArchiveFree(arch);
        return NULL;
    }

    struct Dat2BuildCache_InterfaceArchive* iface_archive =
        calloc(1, sizeof(struct Dat2BuildCache_InterfaceArchive));
    if( !iface_archive )
    {
        RSCacheShared_FileListFree(fl);
        RSCacheDat2Disk_ArchiveFree(arch);
        return NULL;
    }

    iface_archive->component_count = fl->file_count;
    iface_archive->components = calloc((size_t)fl->file_count, sizeof(Component*));
    if( !iface_archive->components )
    {
        free(iface_archive);
        RSCacheShared_FileListFree(fl);
        RSCacheDat2Disk_ArchiveFree(arch);
        return NULL;
    }

    for( int i = 0; i < fl->file_count; i++ )
    {
        int packed_id = (iface_id << 16) | (i & 0xFFFF);
        iface_archive->components[i] =
            component_decode_from_bytes(packed_id, fl->files[i], fl->file_sizes[i]);
    }

    RSCacheShared_FileListFree(fl);
    RSCacheDat2Disk_ArchiveFree(arch);
    return iface_archive;
}
