#include "engine/torirs_font_from_rscache.h"

#include "engine/torirs_types.h"

#include <assert.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FONT_ADVANCE_ONLY_GLYPH TORIRS_FONT_GLYPH_COUNT

static uint16_t const TORIRS_FONT_CHARSET[TORIRS_FONT_GLYPH_COUNT] = {
    'A',    'B', 'C',  'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',  'N', 'O', 'P',
    'Q',    'R', 'S',  'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c',  'd', 'e', 'f',
    'g',    'h', 'i',  'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's',  't', 'u', 'v',
    'w',    'x', 'y',  'z', '0', '1', '2', '3', '4', '5', '6', '7', '8',  '9', '!', '"',
    0x00A3, '$', '%',  '^', '&', '*', '(', ')', '-', '_', '=', '+', '[',  '{', ']', '}',
    ';',    ':', '\'', '@', '#', '~', ',', '<', '.', '>', '/', '?', '\\', ' '
};

static int
font_index_of_char(uint8_t c)
{
    int i;

    for( i = 0; i < TORIRS_FONT_GLYPH_COUNT; i++ )
    {
        if( (TORIRS_FONT_CHARSET[i] & 0xFF) == c )
            return i;
    }
    return -1;
}

static void
font_init_charcodeset(struct ToriRS_Font* font)
{
    int i;

    for( i = 0; i < 256; i++ )
    {
        int c = font_index_of_char((uint8_t)i);
        if( c == -1 )
            c = font_index_of_char(' ');
        if( c < 0 || c >= TORIRS_FONT_GLYPH_COUNT )
            c = TORIRS_FONT_GLYPH_COUNT - 1;
        font->charcodeset[i] = (char)c;
    }
    font->charcodeset[(unsigned char)' '] = (char)FONT_ADVANCE_ONLY_GLYPH;
    font->charcodeset[(unsigned char)'|'] = (char)FONT_ADVANCE_ONLY_GLYPH;
}

static void
font_finish_draw_widths(struct ToriRS_Font* font)
{
    int const fallback = font->advance[8] > 0 ? font->advance[8] : 4;
    int i;

    if( font->advance[93] < 4 )
        font->advance[93] = fallback;
    if( font->advance[FONT_ADVANCE_ONLY_GLYPH] <= 0 )
        font->advance[FONT_ADVANCE_ONLY_GLYPH] = fallback;

    for( i = 0; i < 256; i++ )
        font->draw_width[i] = font->advance[(unsigned char)font->charcodeset[i]];

    font->draw_width[(unsigned char)' '] = font->advance[FONT_ADVANCE_ONLY_GLYPH];
    font->draw_width[(unsigned char)'|'] = font->advance[FONT_ADVANCE_ONLY_GLYPH];
}

static int
font_has_any_glyph(struct ToriRS_Font const* font)
{
    int i;

    for( i = 0; i < TORIRS_FONT_GLYPH_COUNT; i++ )
    {
        if( font->glyph_alpha[i] )
            return 1;
    }
    return 0;
}

static struct ToriRS_Font*
font_new_from_dat2_metrics_and_sprite_pack(
    struct RSCache_Dat2FontMetrics const* metrics,
    struct RSCache_Dat2SpritePack const* pack)
{
    struct ToriRS_Font* font;
    int gi;

    assert(metrics);
    if( !pack || pack->count <= 0 )
        return NULL;

    font = calloc(1, sizeof(*font));
    if( !font )
        return NULL;

    font_init_charcodeset(font);
    font->line_height = metrics->ascent;

    for( gi = 0; gi < TORIRS_FONT_GLYPH_COUNT; gi++ )
    {
        int const cp = (int)TORIRS_FONT_CHARSET[gi];
        struct RSCache_Dat2Sprite const* sprite;
        int gw;
        int gh;
        size_t len;
        size_t j;

        if( cp < 0 || cp >= pack->count )
            continue;

        sprite = &pack->sprites[cp];
        gw = sprite->crop_width;
        gh = sprite->crop_height;
        if( gw <= 0 || gh <= 0 || !sprite->pixel_alphas )
            continue;

        len = (size_t)gw * (size_t)gh;
        font->glyph_alpha[gi] = malloc(len);
        if( !font->glyph_alpha[gi] )
        {
            ToriRS_FontFree(font);
            return NULL;
        }

        for( j = 0; j < len; j++ )
            font->glyph_alpha[gi][j] = sprite->pixel_alphas[j] != 0 ? (uint8_t)255 : (uint8_t)0;

        font->glyph_width[gi] = gw;
        font->glyph_height[gi] = gh;
        font->offset_x[gi] = sprite->offset_x;
        font->offset_y[gi] = sprite->offset_y;
        font->advance[gi] = metrics->advance[cp];
        if( font->advance[gi] <= 0 )
            font->advance[gi] = gw > 0 ? gw + 2 : 4;
    }

    if( metrics->advance[(unsigned char)' '] > 0 )
        font->advance[FONT_ADVANCE_ONLY_GLYPH] = metrics->advance[(unsigned char)' '];

    if( font->line_height <= 0 )
    {
        for( gi = 0; gi < TORIRS_FONT_GLYPH_COUNT; gi++ )
        {
            if( font->glyph_height[gi] > font->line_height )
                font->line_height = font->glyph_height[gi];
        }
    }

    font_finish_draw_widths(font);
    if( !font_has_any_glyph(font) )
    {
        ToriRS_FontFree(font);
        return NULL;
    }
    return font;
}

struct ToriRS_Font*
ToriRS_FontFromDat1Jagfile(
    struct RSCache_FileListDat* title_jagfile,
    char const* font_name)
{
    struct RSCache_Dat1PixFont* pixfont;
    struct ToriRS_Font* font;
    char data_filename[80];
    int data_file_idx;
    int index_file_idx;
    int gi;

    assert(title_jagfile);
    assert(font_name);
    if( font_name[0] == '\0' )
        return NULL;

    snprintf(data_filename, sizeof(data_filename), "%s.dat", font_name);
    data_file_idx = RSCache_FileListDatFindFileByName(title_jagfile, data_filename);
    index_file_idx = RSCache_FileListDatFindFileByName(title_jagfile, "index.dat");
    if( data_file_idx == -1 || index_file_idx == -1 )
    {
        fprintf(
            stderr, "ToriRS_FontFromDat1Jagfile: missing %s/index.dat in title jagfile\n",
            data_filename);
        return NULL;
    }

    pixfont = RSCache_Dat1PixFontNewDecode(
        title_jagfile->files[data_file_idx],
        title_jagfile->file_sizes[data_file_idx],
        title_jagfile->files[index_file_idx],
        title_jagfile->file_sizes[index_file_idx]);
    if( !pixfont )
        return NULL;

    font = calloc(1, sizeof(*font));
    if( !font )
    {
        RSCache_Dat1PixFontFree(pixfont);
        return NULL;
    }

    font_init_charcodeset(font);

    /* Dat1 pixfont glyphs share the 94-char CHARSET order used here. */
    for( gi = 0; gi < TORIRS_FONT_GLYPH_COUNT && gi < RSCACHE_DAT1_PIXFONT_CHAR_COUNT; gi++ )
    {
        int gw = pixfont->char_mask_width[gi];
        int gh = pixfont->char_mask_height[gi];
        size_t len;
        size_t j;

        font->advance[gi] = pixfont->char_advance[gi];

        if( gw <= 0 || gh <= 0 || !pixfont->char_mask[gi] )
            continue;

        len = (size_t)gw * (size_t)gh;
        font->glyph_alpha[gi] = malloc(len);
        if( !font->glyph_alpha[gi] )
        {
            RSCache_Dat1PixFontFree(pixfont);
            ToriRS_FontFree(font);
            return NULL;
        }
        for( j = 0; j < len; j++ )
            font->glyph_alpha[gi][j] = pixfont->char_mask[gi][j] != 0 ? (uint8_t)255 : (uint8_t)0;

        font->glyph_width[gi] = gw;
        font->glyph_height[gi] = gh;
        font->offset_x[gi] = pixfont->char_offset_x[gi];
        font->offset_y[gi] = pixfont->char_offset_y[gi];
    }
    font->advance[FONT_ADVANCE_ONLY_GLYPH] =
        pixfont->char_advance[RSCACHE_DAT1_PIXFONT_CHAR_COUNT];

    for( gi = 0; gi < TORIRS_FONT_GLYPH_COUNT; gi++ )
    {
        if( font->glyph_height[gi] > font->line_height )
            font->line_height = font->glyph_height[gi];
    }

    font_finish_draw_widths(font);
    RSCache_Dat1PixFontFree(pixfont);

    if( !font_has_any_glyph(font) )
    {
        ToriRS_FontFree(font);
        return NULL;
    }
    snprintf(font->name, sizeof(font->name), "%s", font_name);
    return font;
}

struct ToriRS_Font*
ToriRS_FontFromDat2Archives(
    const struct RSCache* profile,
    struct RSCache_Dat2DiskArchive* font_archive,
    struct RSCache_Dat2DiskArchive* sprite_archive,
    int font_id)
{
    struct RSCache_FileList* fl;
    struct RSCache_Dat2SpritePack* pack;
    struct ToriRS_Font* font;
    struct RSCache_Dat2FontMetrics metrics;

    assert(profile);

    if( !font_archive || !sprite_archive || font_id < 0 )
    {
        if( font_archive )
            RSCache_Dat2DiskArchiveFree(font_archive);
        if( sprite_archive )
            RSCache_Dat2DiskArchiveFree(sprite_archive);
        return NULL;
    }

    fl = RSCache_FileListNewFromDecode(
        font_archive->data, font_archive->data_size, font_archive->file_count);
    RSCache_Dat2DiskArchiveFree(font_archive);
    if( !fl || fl->file_count <= 0 || !fl->files[0] )
    {
        RSCache_Dat2DiskArchiveFree(sprite_archive);
        RSCache_FileListFree(fl);
        return NULL;
    }

    if( !RSCache_Dat2FontMetricsDecodeProfile(
            profile, (uint8_t const*)fl->files[0], fl->file_sizes[0], &metrics) )
    {
        RSCache_Dat2DiskArchiveFree(sprite_archive);
        RSCache_FileListFree(fl);
        return NULL;
    }
    RSCache_FileListFree(fl);

    pack = RSCache_Dat2SpritePackNewDecode(
        (unsigned char const*)sprite_archive->data,
        sprite_archive->data_size,
        RSCACHE_SPRITELOAD_FLAG_NONE);
    RSCache_Dat2DiskArchiveFree(sprite_archive);
    if( !pack )
        return NULL;

    font = font_new_from_dat2_metrics_and_sprite_pack(&metrics, pack);
    RSCache_Dat2SpritePackFree(pack);
    if( font )
        snprintf(font->name, sizeof(font->name), "fnt:%d", font_id);
    return font;
}
