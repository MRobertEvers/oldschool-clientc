#include "toriauxlibcache_font_convert.h"

#include "osrs/rscache/dat2disk/dat2disk.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "toridraw/toridraw_font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ToriAuxLibCore_Font*
ToriAuxLibCache_FontNewFromToriDrawByMove(struct ToriDraw_Font* font)
{
    if( !font )
        return NULL;

    struct ToriAuxLibCore_Font* core = calloc(1, sizeof(struct ToriAuxLibCore_Font));
    if( !core )
        return NULL;

    for( int i = 0; i < TORIAUXLIBCORE_FONT_GLYPH_COUNT; i++ )
    {
        core->glyph_alpha[i] = font->glyph_alpha[i];
        core->glyph_width[i] = font->glyph_width[i];
        core->glyph_height[i] = font->glyph_height[i];
        core->offset_x[i] = font->offset_x[i];
        core->offset_y[i] = font->offset_y[i];
        core->advance[i] = font->advance[i];
        font->glyph_alpha[i] = NULL;
    }
    core->advance[TORIAUXLIBCORE_FONT_GLYPH_COUNT] = font->advance[TORIDRAW_FONT_GLYPH_COUNT];
    memcpy(core->draw_width, font->draw_width, sizeof(core->draw_width));
    core->line_height = font->line_height;
    memcpy(core->charcodeset, font->charcodeset, sizeof(core->charcodeset));

    free(font);
    return core;
}

struct ToriAuxLibCore_Font*
ToriAuxLibCache_FontNewFromRSBytesByMove(
    void* data,
    int data_size,
    void* index_data,
    int index_data_size)
{
    struct ToriDraw_Font* font = ToriDraw_FontNewFromRSBytes(data, data_size, index_data, index_data_size);
    return ToriAuxLibCache_FontNewFromToriDrawByMove(font);
}

struct ToriAuxLibCore_Font*
ToriAuxLibCache_FontNewFromDat1Jagfile(
    struct RSCacheShared_FileListDat* filelist,
    char const* font_name)
{
    if( !filelist || !font_name || !font_name[0] )
        return NULL;

    char data_filename[32];
    snprintf(data_filename, sizeof(data_filename), "%s.dat", font_name);

    int index_file_idx = RSCacheShared_FileListDatFindFileByName(filelist, "index.dat");
    int data_file_idx = RSCacheShared_FileListDatFindFileByName(filelist, data_filename);
    if( index_file_idx < 0 || data_file_idx < 0 )
        return NULL;

    struct ToriAuxLibCore_Font* font = ToriAuxLibCache_FontNewFromRSBytesByMove(
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx],
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx]);
    if( font )
        strncpy(font->name, font_name, sizeof(font->name) - 1);
    return font;
}

struct ToriAuxLibCore_Font*
ToriAuxLibCache_FontNewFromDat2Archive(
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive,
    int font_id)
{
    if( !cache || !archive || font_id < 0 )
    {
        RSCacheDat2Disk_ArchiveFree(archive);
        return NULL;
    }

    RSCacheDat2Disk_ArchiveInitMetadata(cache, archive);
    struct RSCacheShared_FileList* fl = RSCacheShared_FileListNewFromCacheArchive(archive);
    RSCacheDat2Disk_ArchiveFree(archive);
    if( !fl || fl->file_count <= 0 )
    {
        RSCacheShared_FileListFree(fl);
        return NULL;
    }

    void* data = fl->files[0];
    int data_size = fl->file_sizes[0];
    void* index_data = fl->file_count > 1 ? fl->files[1] : NULL;
    int index_size = fl->file_count > 1 ? fl->file_sizes[1] : 0;

    struct ToriAuxLibCore_Font* font =
        ToriAuxLibCache_FontNewFromRSBytesByMove(data, data_size, index_data, index_size);

    RSCacheShared_FileListFree(fl);
    if( font )
        snprintf(font->name, sizeof(font->name), "fnt:%d", font_id);
    return font;
}
