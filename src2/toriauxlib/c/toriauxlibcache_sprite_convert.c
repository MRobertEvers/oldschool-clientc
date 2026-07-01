#include "toriauxlibcache_sprite_convert.h"

#include "osrs/rscache/dat1a/dat1a_pix32.h"
#include "osrs/rscache/dat1a/dat1a_pix8.h"
#include "osrs/rscache/dat2a/dat2a_sprites.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "toriauxlib/td/toridraw_cachesprite.h"
#include "toridraw/toridraw_sprite.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct ToriAuxLibCore_SpriteFrame*
sprite_frame_from_tori_draw(struct ToriDraw_Sprite* sprite)
{
    if( !sprite )
        return NULL;

    struct ToriAuxLibCore_SpriteFrame* frame = calloc(1, sizeof(struct ToriAuxLibCore_SpriteFrame));
    if( !frame )
        return NULL;

    frame->pixels_argb = sprite->pixels_argb;
    frame->width = sprite->width;
    frame->height = sprite->height;
    frame->crop_x = sprite->crop_x;
    frame->crop_y = sprite->crop_y;
    frame->crop_width = sprite->crop_width > 0 ? sprite->crop_width : sprite->width;
    frame->crop_height = sprite->crop_height > 0 ? sprite->crop_height : sprite->height;

    sprite->pixels_argb = NULL;
    ToriDraw_SpriteFree(sprite);
    return frame;
}

struct ToriAuxLibCore_SpriteFrame*
ToriAuxLibCache_SpriteFrameNewFromToriDrawByMove(struct ToriDraw_Sprite* sprite)
{
    return sprite_frame_from_tori_draw(sprite);
}

struct ToriAuxLibCore_SpriteFrame*
ToriAuxLibCache_SpriteFrameNewFromArgbByMove(
    uint32_t* pixels_argb,
    int width,
    int height)
{
    struct ToriDraw_Sprite* sprite = ToriDraw_SpriteNewFromArgbOwned(pixels_argb, width, height);
    return sprite_frame_from_tori_draw(sprite);
}

static void
sprite_frame_apply_transform(
    struct ToriAuxLibCore_SpriteFrame* frame,
    char const* transform)
{
    if( !frame || !frame->pixels_argb || !transform )
        return;

    struct ToriDraw_Sprite tmp = {
        .pixels_argb = frame->pixels_argb,
        .width = frame->width,
        .height = frame->height,
        .crop_x = frame->crop_x,
        .crop_y = frame->crop_y,
        .crop_width = frame->crop_width,
        .crop_height = frame->crop_height,
    };

    if( strcmp(transform, "flip_h") == 0 )
        ToriDraw_SpriteFlipHorizontal(&tmp);
    else if( strcmp(transform, "flip_v") == 0 )
        ToriDraw_SpriteFlipVertical(&tmp);
}

void
ToriAuxLibCache_SpriteApplyRevConfigItem(
    struct ToriAuxLibCore_Sprite* sprite,
    struct RevConfigCacheItem const* item)
{
    if( !sprite || !item )
        return;

    for( int i = 0; i < sprite->frame_count; i++ )
    {
        struct ToriAuxLibCore_SpriteFrame* frame = &sprite->frames[i];
        if( !frame->pixels_argb )
            continue;

        for( int t = 0; t < item->transform_count; t++ )
            sprite_frame_apply_transform(frame, item->transform[t]);

        if( item->crop_width > 0 && item->crop_height > 0 )
        {
            frame->crop_x = item->crop_x;
            frame->crop_y = item->crop_y;
            frame->crop_width = item->crop_width;
            frame->crop_height = item->crop_height;
        }
        else if( frame->crop_width <= 0 || frame->crop_height <= 0 )
        {
            frame->crop_width = frame->width;
            frame->crop_height = frame->height;
        }
    }
}

static struct ToriAuxLibCore_SpriteFrame*
sprite_frame_from_dat1_filelist(
    struct RSCacheShared_FileListDat* filelist,
    struct RevConfigCacheItem const* item,
    int atlas_index)
{
    if( !filelist || !item )
        return NULL;

    int index_file_idx = RSCacheShared_FileListDatFindFileByName(filelist, item->index_filename);
    int data_file_idx = RSCacheShared_FileListDatFindFileByName(filelist, item->data_filename);
    if( index_file_idx < 0 || data_file_idx < 0 )
        return NULL;

    if( strcmp(item->format, "pix8") == 0 )
    {
        struct RSCacheDat1A_Pix8Palette* pix8 = RSCacheDat1A_Pix8PaletteNew(
            filelist->files[data_file_idx],
            filelist->file_sizes[data_file_idx],
            filelist->files[index_file_idx],
            filelist->file_sizes[index_file_idx],
            atlas_index);
        if( !pix8 )
            return NULL;
        struct ToriDraw_Sprite* sprite = ToriDraw_SpriteNewFromCachePix8Palette(pix8);
        RSCacheDat1A_Pix8PaletteFree(pix8);
        return sprite_frame_from_tori_draw(sprite);
    }

    if( strcmp(item->format, "pix32") == 0 )
    {
        struct RSCacheDat1A_Pix32* pix32 = RSCacheDat1A_Pix32New(
            filelist->files[data_file_idx],
            filelist->file_sizes[data_file_idx],
            filelist->files[index_file_idx],
            filelist->file_sizes[index_file_idx],
            atlas_index);
        if( !pix32 )
            return NULL;
        struct ToriDraw_Sprite* sprite = ToriDraw_SpriteNewFromCachePix32(pix32);
        RSCacheDat1A_Pix32Free(pix32);
        return sprite_frame_from_tori_draw(sprite);
    }

    return NULL;
}

static struct ToriAuxLibCore_Sprite*
sprite_new_with_frames(int frame_count)
{
    if( frame_count <= 0 )
        return NULL;

    struct ToriAuxLibCore_Sprite* sprite = calloc(1, sizeof(struct ToriAuxLibCore_Sprite));
    if( !sprite )
        return NULL;

    sprite->frames = calloc((size_t)frame_count, sizeof(struct ToriAuxLibCore_SpriteFrame));
    if( !sprite->frames )
    {
        free(sprite);
        return NULL;
    }
    sprite->frame_count = frame_count;
    return sprite;
}

static bool
sprite_finalize_contiguous_frames(struct ToriAuxLibCore_Sprite* sprite)
{
    if( !sprite || !sprite->frames || sprite->frame_count <= 0 )
        return false;

    int contiguous = 0;
    for( int j = 0; j < sprite->frame_count; j++ )
    {
        if( !sprite->frames[j].pixels_argb )
            break;
        contiguous++;
    }

    for( int j = contiguous; j < sprite->frame_count; j++ )
    {
        if( sprite->frames[j].pixels_argb )
            return false;
    }

    if( contiguous <= 0 )
        return false;

    sprite->frame_count = contiguous;
    return true;
}

struct ToriAuxLibCore_Sprite*
ToriAuxLibCache_SpriteNewFromDat1RevConfigItem(
    struct RSCacheShared_FileListDat* filelist,
    struct RevConfigCacheItem const* item)
{
    assert(filelist);
    assert(item);

    int start = item->atlas_count > 0 ? 0 : item->atlas_index;
    int count = item->atlas_count > 0 ? item->atlas_count : 1;

    struct ToriAuxLibCore_Sprite* sprite = sprite_new_with_frames(count);
    if( !sprite )
        return NULL;

    int loaded = 0;
    for( int j = 0; j < count; j++ )
    {
        struct ToriAuxLibCore_SpriteFrame* frame =
            sprite_frame_from_dat1_filelist(filelist, item, start + j);
        if( frame )
        {
            sprite->frames[j] = *frame;
            free(frame);
            loaded++;
        }
    }

    if( loaded <= 0 )
    {
        ToriAuxLibCore_SpriteFree(sprite);
        return NULL;
    }

    if( !sprite_finalize_contiguous_frames(sprite) )
    {
        ToriAuxLibCore_SpriteFree(sprite);
        return NULL;
    }

    strncpy(sprite->name, item->name, sizeof(sprite->name) - 1);
    ToriAuxLibCache_SpriteApplyRevConfigItem(sprite, item);
    return sprite;
}

struct ToriAuxLibCore_Sprite*
ToriAuxLibCache_SpriteNewFromDat1Ref(
    struct RSCacheShared_FileListDat* filelist,
    char const* sprite_ref)
{
    if( !filelist || !sprite_ref || !sprite_ref[0] )
        return NULL;

    char filename_buf[256];
    int sprite_idx = 0;

    if( sscanf(sprite_ref, "%255[^,],%d", filename_buf, &sprite_idx) != 2 )
    {
        char name_buf[256];
        char index_buf[32];
        if( sscanf(sprite_ref, "%255[^[][%31[^]]", name_buf, index_buf) == 2 )
        {
            strncpy(filename_buf, name_buf, sizeof(filename_buf) - 1);
            filename_buf[sizeof(filename_buf) - 1] = '\0';
            sprite_idx = atoi(index_buf);
        }
        else
        {
            strncpy(filename_buf, sprite_ref, sizeof(filename_buf) - 1);
            filename_buf[sizeof(filename_buf) - 1] = '\0';
            sprite_idx = 0;
        }
    }

    size_t len = strlen(filename_buf);
    if( len + 5 <= sizeof(filename_buf) &&
        (len < 4 || strcmp(filename_buf + len - 4, ".dat") != 0) )
    {
        memcpy(filename_buf + len, ".dat", 4);
        filename_buf[len + 4] = '\0';
    }

    struct RevConfigCacheItem item = { 0 };
    strncpy(item.index_filename, "index.dat", sizeof(item.index_filename) - 1);
    strncpy(item.data_filename, filename_buf, sizeof(item.data_filename) - 1);
    strncpy(item.format, "pix32", sizeof(item.format) - 1);
    item.atlas_index = sprite_idx;

    struct ToriAuxLibCore_SpriteFrame* frame =
        sprite_frame_from_dat1_filelist(filelist, &item, sprite_idx);
    if( !frame )
    {
        strncpy(item.format, "pix8", sizeof(item.format) - 1);
        frame = sprite_frame_from_dat1_filelist(filelist, &item, sprite_idx);
    }
    if( !frame )
        return NULL;

    struct ToriAuxLibCore_Sprite* sprite = sprite_new_with_frames(1);
    if( !sprite )
    {
        ToriAuxLibCore_SpriteFrameFree(frame);
        return NULL;
    }

    sprite->frames[0] = *frame;
    free(frame);
    strncpy(sprite->name, sprite_ref, sizeof(sprite->name) - 1);
    return sprite;
}

static struct ToriAuxLibCore_SpriteFrame*
sprite_frame_from_dat2_pack(
    struct RSCacheDat2A_SpritePack const* pack,
    int frame_index)
{
    if( !pack || frame_index < 0 || frame_index >= pack->count )
        return NULL;

    struct RSCacheDat2A_Sprite* def = &pack->sprites[frame_index];
    int* px = RSCacheDat2A_SpriteGetPixels(def, pack->palette, 0);
    if( !px )
        return NULL;

    struct ToriAuxLibCore_SpriteFrame* frame = calloc(1, sizeof(struct ToriAuxLibCore_SpriteFrame));
    if( !frame )
    {
        free(px);
        return NULL;
    }

    frame->pixels_argb = (uint32_t*)px;
    frame->width = def->crop_width > 0 ? def->crop_width : def->width;
    frame->height = def->crop_height > 0 ? def->crop_height : def->height;
    frame->crop_x = def->offset_x;
    frame->crop_y = def->offset_y;
    frame->crop_width = frame->width;
    frame->crop_height = frame->height;
    return frame;
}

static struct ToriAuxLibCore_Sprite*
sprite_new_from_dat2_pack(
    struct RSCacheDat2A_SpritePack* pack,
    struct RevConfigCacheItem const* item,
    int start,
    int count)
{
    if( !pack || count <= 0 )
        return NULL;

    struct ToriAuxLibCore_Sprite* sprite = sprite_new_with_frames(count);
    if( !sprite )
        return NULL;

    int loaded = 0;
    for( int j = 0; j < count; j++ )
    {
        int frame_index = start + j;
        if( frame_index < 0 || frame_index >= pack->count )
            continue;

        struct ToriAuxLibCore_SpriteFrame* frame = sprite_frame_from_dat2_pack(pack, frame_index);
        if( frame )
        {
            sprite->frames[j] = *frame;
            free(frame);
            loaded++;
        }
    }

    if( loaded <= 0 )
    {
        ToriAuxLibCore_SpriteFree(sprite);
        return NULL;
    }

    if( !sprite_finalize_contiguous_frames(sprite) )
    {
        ToriAuxLibCore_SpriteFree(sprite);
        return NULL;
    }

    if( item )
    {
        if( item->name[0] != '\0' )
            strncpy(sprite->name, item->name, sizeof(sprite->name) - 1);
        ToriAuxLibCache_SpriteApplyRevConfigItem(sprite, item);
    }

    return sprite;
}

struct ToriAuxLibCore_Sprite*
ToriAuxLibCache_SpriteNewFromDat2Archive(
    struct RSCacheDat2Disk_Archive* archive,
    struct RevConfigCacheItem const* item)
{
    if( !archive || !item || item->archive_id < 0 )
    {
        RSCacheDat2Disk_ArchiveFree(archive);
        return NULL;
    }

    struct RSCacheDat2A_SpritePack* pack = RSCacheDat2A_SpritePackNewDecode(
        (const unsigned char*)archive->data, archive->data_size, SPRITELOAD_FLAG_NORMALIZE);
    RSCacheDat2Disk_ArchiveFree(archive);
    if( !pack || pack->count <= 0 )
    {
        if( pack )
            RSCacheDat2A_SpritePackFree(pack);
        return NULL;
    }

    int start = item->atlas_count > 0 ? 0 : item->atlas_index;
    int count = item->atlas_count > 0 ? item->atlas_count : 1;

    struct ToriAuxLibCore_Sprite* sprite = sprite_new_from_dat2_pack(pack, item, start, count);
    RSCacheDat2A_SpritePackFree(pack);
    return sprite;
}

struct ToriAuxLibCore_Sprite*
ToriAuxLibCache_SpriteNewFromDat2ArchiveId(
    struct RSCacheDat2Disk_Archive* archive,
    int sprite_id)
{
    if( !archive || sprite_id < 0 )
    {
        RSCacheDat2Disk_ArchiveFree(archive);
        return NULL;
    }

    struct RSCacheDat2A_SpritePack* pack = RSCacheDat2A_SpritePackNewDecode(
        (const unsigned char*)archive->data, archive->data_size, SPRITELOAD_FLAG_NORMALIZE);
    RSCacheDat2Disk_ArchiveFree(archive);
    if( !pack || pack->count <= 0 )
    {
        if( pack )
            RSCacheDat2A_SpritePackFree(pack);
        return NULL;
    }

    struct RevConfigCacheItem item = { 0 };
    item.archive_id = sprite_id;
    item.atlas_count = pack->count;

    struct ToriAuxLibCore_Sprite* sprite = sprite_new_from_dat2_pack(pack, &item, 0, pack->count);
    if( sprite )
        snprintf(sprite->name, sizeof(sprite->name), "spr:%d", sprite_id);
    RSCacheDat2A_SpritePackFree(pack);
    return sprite;
}
