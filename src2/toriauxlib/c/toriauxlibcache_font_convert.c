#include "toriauxlibcache_font_convert.h"

#include "osrs/rscache/dat2a/dat2a_sprites.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "toridraw/toridraw_font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DAT2_FONT_METRICS_SIZE 257

static struct ToriDraw_Font*
font_new_from_dat2_metrics_and_sprite_pack(
    uint8_t const* metrics,
    struct RSCacheDat2A_SpritePack const* pack)
{
    if( !metrics || !pack || pack->count <= 0 )
        return NULL;

    struct ToriDraw_Font* font = calloc(1, sizeof(struct ToriDraw_Font));
    if( !font )
        return NULL;

    ToriDraw_FontInitCharcodeset(font);

    font->line_height = metrics[256] & 0xFF;

    for( int gi = 0; gi < TORIDRAW_FONT_GLYPH_COUNT; gi++ )
    {
        uint16_t const codepoint = ToriDraw_FontCharsetTable()[gi];
        int const cp = (int)codepoint;
        if( cp < 0 || cp >= pack->count )
            continue;

        struct RSCacheDat2A_Sprite const* sprite = &pack->sprites[cp];
        int const gw = sprite->crop_width;
        int const gh = sprite->crop_height;
        if( gw <= 0 || gh <= 0 || !sprite->pixel_alphas )
            continue;

        size_t const len = (size_t)gw * (size_t)gh;
        font->glyph_alpha[gi] = malloc(len);
        if( !font->glyph_alpha[gi] )
            goto fail;

        for( size_t j = 0; j < len; j++ )
            font->glyph_alpha[gi][j] = sprite->pixel_alphas[j] != 0 ? (uint8_t)255 : (uint8_t)0;

        font->glyph_width[gi] = gw;
        font->glyph_height[gi] = gh;
        font->offset_x[gi] = sprite->offset_x;
        font->offset_y[gi] = sprite->offset_y;
        font->advance[gi] = metrics[cp] & 0xFF;
        if( font->advance[gi] <= 0 )
            font->advance[gi] = gw > 0 ? gw + 2 : 4;
    }

    if( font->line_height <= 0 )
    {
        for( int i = 0; i < TORIDRAW_FONT_GLYPH_COUNT; i++ )
        {
            if( font->glyph_height[i] > font->line_height )
                font->line_height = font->glyph_height[i];
        }
    }

    ToriDraw_FontFinishDrawWidths(font);

    if( !ToriDraw_FontValidate(font) )
        goto fail;
    return font;

fail:
    ToriDraw_FontFree(font);
    return NULL;
}

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
    if( !fl || fl->file_count <= 0 || fl->file_sizes[0] < DAT2_FONT_METRICS_SIZE )
    {
        RSCacheShared_FileListFree(fl);
        return NULL;
    }

    uint8_t metrics[DAT2_FONT_METRICS_SIZE];
    memcpy(metrics, fl->files[0], DAT2_FONT_METRICS_SIZE);
    RSCacheShared_FileListFree(fl);

    struct RSCacheDat2Disk_Archive* sprite_archive =
        RSCacheDat2Disk_ArchiveNewLoad(cache, RSCacheDat2Disk_Table_Sprites, font_id);
    if( !sprite_archive )
        return NULL;

    struct RSCacheDat2A_SpritePack* pack = RSCacheDat2A_SpritePackNewDecode(
        (unsigned char const*)sprite_archive->data,
        sprite_archive->data_size,
        SPRITELOAD_FLAG_NONE);
    RSCacheDat2Disk_ArchiveFree(sprite_archive);
    if( !pack )
        return NULL;

    struct ToriDraw_Font* draw_font = font_new_from_dat2_metrics_and_sprite_pack(metrics, pack);
    RSCacheDat2A_SpritePackFree(pack);
    struct ToriAuxLibCore_Font* font = ToriAuxLibCache_FontNewFromToriDrawByMove(draw_font);
    if( font )
        snprintf(font->name, sizeof(font->name), "fnt:%d", font_id);
    return font;
}
