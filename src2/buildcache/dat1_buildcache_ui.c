#include "dat1_buildcache_ui.h"

#include "toriauxlib/td/toridraw_cachesprite.h"
#include "osrs/rscache/dat1a/dat1a_pix32.h"
#include "osrs/rscache/dat1a/dat1a_pix8.h"
#include "osrs/rscache/shared/shared_file_list.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct ToriDraw_Sprite*
sprite_decode_from_filelist(
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
        if( sprite )
        {
            if( item->crop_width > 0 && item->crop_height > 0 )
            {
                sprite->crop_x = item->crop_x;
                sprite->crop_y = item->crop_y;
                sprite->crop_width = item->crop_width;
                sprite->crop_height = item->crop_height;
            }
            else
            {
                sprite->crop_width = sprite->width;
                sprite->crop_height = sprite->height;
            }
        }
        RSCacheDat1A_Pix8PaletteFree(pix8);
        return sprite;
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
        return sprite;
    }

    return NULL;
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

struct ToriDraw_Sprite**
dat1_buildcache_sprite_decode(
    struct Dat1BuildCache* buildcache,
    struct RevConfigCacheItem const* item,
    int* out_count)
{
    if( out_count )
        *out_count = 0;
    if( !buildcache || !item )
        return NULL;

    struct RSCacheShared_FileListDat* filelist =
        dat1_buildcache_get_media_2d_graphics_jagfile(buildcache);
    if( !filelist )
        return NULL;

    int start = item->atlas_count > 0 ? 0 : item->atlas_index;
    int count = item->atlas_count > 0 ? item->atlas_count : 1;
    struct ToriDraw_Sprite** sprites = calloc((size_t)count, sizeof(struct ToriDraw_Sprite*));
    if( !sprites )
        return NULL;

    int loaded = 0;
    for( int j = 0; j < count; j++ )
    {
        sprites[j] = sprite_decode_from_filelist(filelist, item, start + j);
        if( sprites[j] )
        {
            sprite_apply_transforms(sprites[j], item);
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

struct ToriDraw_Sprite*
dat1_buildcache_sprite_decode_ref(
    struct Dat1BuildCache* buildcache,
    char const* sprite_ref)
{
    struct RSCacheShared_FileListDat* filelist =
        dat1_buildcache_get_media_2d_graphics_jagfile(buildcache);
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

    int index_file_idx = RSCacheShared_FileListDatFindFileByName(filelist, "index.dat");
    int data_file_idx = RSCacheShared_FileListDatFindFileByName(filelist, filename_buf);
    if( index_file_idx < 0 || data_file_idx < 0 )
        return NULL;

    struct RSCacheDat1A_Pix32* pix32 = RSCacheDat1A_Pix32New(
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx],
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx],
        sprite_idx);
    if( pix32 )
    {
        struct ToriDraw_Sprite* sprite = ToriDraw_SpriteNewFromCachePix32(pix32);
        RSCacheDat1A_Pix32Free(pix32);
        if( sprite )
            return sprite;
    }

    struct RSCacheDat1A_Pix8Palette* pix8 = RSCacheDat1A_Pix8PaletteNew(
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx],
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx],
        sprite_idx);
    if( !pix8 )
        return NULL;
    struct ToriDraw_Sprite* sprite = ToriDraw_SpriteNewFromCachePix8Palette(pix8);
    RSCacheDat1A_Pix8PaletteFree(pix8);
    return sprite;
}

struct ToriDraw_Font*
dat1_buildcache_font_decode(
    struct Dat1BuildCache* buildcache,
    char const* font_name)
{
    if( !buildcache || !font_name || !font_name[0] )
        return NULL;

    struct RSCacheShared_FileListDat* filelist = buildcache->title_fonts_jagfile;
    if( !filelist )
        return NULL;

    char data_filename[32];
    snprintf(data_filename, sizeof(data_filename), "%s.dat", font_name);

    int index_file_idx = RSCacheShared_FileListDatFindFileByName(filelist, "index.dat");
    int data_file_idx = RSCacheShared_FileListDatFindFileByName(filelist, data_filename);
    if( index_file_idx < 0 || data_file_idx < 0 )
        return NULL;

    return ToriDraw_FontNewFromRSBytes(
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx],
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx]);
}
