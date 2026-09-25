/*
 * Dat1 bitmap fonts: the two glyph layouts, and where the SPACE width comes
 * from in each.
 *
 * Space is the one character no font file describes. Neither layout ships a
 * usable record for it, so both clients synthesise its advance from a letter --
 * and they pick a DIFFERENT letter:
 *
 *   94-record ("b12")      space = 'I'          (PixFont.java: charAdvance[94] = charAdvance[8])
 *   256-record ("b12_full") space = 'i', or 'I' for the quill font
 *                                        (PixFont.ts: charAdvance[32] = charAdvance[105 or 73])
 *
 * Two pixels of difference in b12, which is a word-boundary each time: a line
 * of rev-289 heading text came out ~20px long and ran over the box it sat in.
 * That is what these checks are for -- everything else about the two decoders
 * is shared, so the space rule is the part that can silently drift.
 *
 * The fonts here are synthesised, not read from a cache, so this never skips.
 */

#include "rscache_test.h"

#include <rscache.h>
#include <stdlib.h>
#include <string.h>

/* The charset the 94-record layout stores its glyphs in, in file order. The
 * last entry is the space, which has NO record -- 94 records, 95 characters. */
static const char* const CHARSET =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!\"\xA3$%^&*()-_=+[{]};:'@#~,<.>/"
    "?\\| ";

/* Every glyph is a solid block, so neither blank-column trim fires and the
 * advance is exactly width + 2. 'i' and 'I' are given the widths they have in
 * a real b12, because those two are what the space rule chooses between. */
static int
glyph_width_for(unsigned char c)
{
    if( c == 'i' )
        return 2; /* advance 4 */
    if( c == 'I' )
        return 4; /* advance 6 */
    if( c == '|' )
        return 1; /* advance 3 -- and narrow enough that a "space is short, widen
                   * it" patch aimed at the wrong slot would show up here */
    return 3;     /* advance 5 */
}

#define GLYPH_HEIGHT 8
#define INDEX_LEAD 6 /* the .dat's stated offset, plus the 4 the reader skips */

/*
 * Build one font: `index.dat` (per-record geometry) and `<name>.dat` (one
 * sequential stream of pixel bytes). `codes` lists the characters to emit
 * records for, in file order -- CHARSET order for the 94-record layout, byte
 * value order for the 256-record one.
 */
static void
build_font(
    const unsigned char* codes,
    int code_count,
    unsigned char** index_out,
    int* index_size_out,
    unsigned char** data_out,
    int* data_size_out)
{
    int index_size = INDEX_LEAD + 1 + code_count * 7;
    unsigned char* index = calloc(1, (size_t)index_size);
    int data_size = 2;
    int i;

    for( i = 0; i < code_count; i++ )
        data_size += glyph_width_for(codes[i]) * GLYPH_HEIGHT;

    unsigned char* data = calloc(1, (size_t)data_size);

    /* The .dat opens with the index offset; the reader adds 4 to it. */
    data[0] = (unsigned char)((INDEX_LEAD - 4) >> 8);
    data[1] = (unsigned char)((INDEX_LEAD - 4) & 0xFF);

    int ip = INDEX_LEAD;
    index[ip++] = 1; /* palette count: 1 means nothing to skip */

    int dp = 2;
    for( i = 0; i < code_count; i++ )
    {
        int w = glyph_width_for(codes[i]);
        int h = GLYPH_HEIGHT;
        int j;

        index[ip++] = 0; /* offset_x, overwritten by the decoder */
        index[ip++] = 2; /* offset_y */
        index[ip++] = (unsigned char)(w >> 8);
        index[ip++] = (unsigned char)(w & 0xFF);
        index[ip++] = (unsigned char)(h >> 8);
        index[ip++] = (unsigned char)(h & 0xFF);
        index[ip++] = 0; /* pixel order: row major */

        for( j = 0; j < w * h; j++ )
            data[dp++] = 1;
    }

    *index_out = index;
    *index_size_out = index_size;
    *data_out = data;
    *data_size_out = data_size;
}

static void
test_94_record_space_is_capital_i(void)
{
    RSCACHE_TEST_GROUP("94-record layout: space takes 'I'");

    unsigned char codes[94];
    int i;
    for( i = 0; i < 94; i++ )
        codes[i] = (unsigned char)CHARSET[i];

    unsigned char* index;
    unsigned char* data;
    int index_size;
    int data_size;
    build_font(codes, 94, &index, &index_size, &data, &data_size);

    struct RSCache_Dat1PixFont* font =
        RSCache_Dat1PixFontNewDecode(data, data_size, index, index_size);
    RSCACHE_CHECK(font != NULL);
    if( font )
    {
        RSCACHE_CHECK_EQ(font->draw_width['i'], 4);
        RSCACHE_CHECK_EQ(font->draw_width['I'], 6);
        RSCACHE_CHECK_EQ(font->draw_width[' '], font->draw_width['I']);
        /* '|' is a glyph like any other -- it is not the space, and nothing
         * may widen it on the space's behalf. */
        RSCACHE_CHECK_EQ(font->draw_width['|'], 3);
        RSCache_Dat1PixFontFree(font);
    }
    free(index);
    free(data);
}

static struct RSCache_Dat1PixFont*
decode_full(int quill)
{
    unsigned char codes[256];
    int i;
    for( i = 0; i < 256; i++ )
        codes[i] = (unsigned char)i;

    unsigned char* index;
    unsigned char* data;
    int index_size;
    int data_size;
    build_font(codes, 256, &index, &index_size, &data, &data_size);

    struct RSCache_Dat1PixFont* font =
        RSCache_Dat1PixFontFullNewDecode(data, data_size, index, index_size, quill);
    free(index);
    free(data);
    return font;
}

static void
test_full_space_is_lowercase_i(void)
{
    RSCACHE_TEST_GROUP("256-record layout: space takes 'i'");

    struct RSCache_Dat1PixFont* font = decode_full(0);
    RSCACHE_CHECK(font != NULL);
    if( font )
    {
        RSCACHE_CHECK_EQ(font->draw_width['i'], 4);
        RSCACHE_CHECK_EQ(font->draw_width['I'], 6);
        RSCACHE_CHECK_EQ(font->draw_width[' '], font->draw_width['i']);

        /* The width this decoder was getting wrong, spelled out at the level a
         * layout sees it: eleven words of heading. */
        const char* line = "Welcome to RuneScape - Use the buttons below to design your player";
        int width = 0;
        const char* c;
        for( c = line; *c; c++ )
            width += font->draw_width[(unsigned char)*c];
        /* 66 characters: 54 at 5, one 'i' at 4, eleven spaces at 4. Taking the
         * space from 'I' instead adds 22 -- the exact overrun that was seen. */
        RSCACHE_CHECK_EQ(width, 54 * 5 + 4 + 11 * 4);

        RSCache_Dat1PixFontFree(font);
    }
}

static void
test_full_quill_space_is_capital_i(void)
{
    RSCACHE_TEST_GROUP("256-record layout: the quill font takes 'I'");

    struct RSCache_Dat1PixFont* font = decode_full(1);
    RSCACHE_CHECK(font != NULL);
    if( font )
    {
        RSCACHE_CHECK_EQ(font->draw_width[' '], font->draw_width['I']);
        RSCACHE_CHECK_EQ(font->draw_width[' '], 6);
        RSCache_Dat1PixFontFree(font);
    }
}

static void
test_every_byte_has_a_width(void)
{
    RSCACHE_TEST_GROUP("no byte addresses past the advance table");

    struct RSCache_Dat1PixFont* font = decode_full(0);
    RSCACHE_CHECK(font != NULL);
    if( font )
    {
        /* charcodeset is an index into char_advance, which is 95 long. A
         * character outside the charset must land on the space slot, not on a
         * negative index that reads whatever follows the array. */
        int i;
        int in_range = 1;
        for( i = 0; i < 256; i++ )
        {
            int slot = (unsigned char)font->charcodeset[i];
            if( slot < 0 || slot > RSCACHE_DAT1_PIXFONT_SPACE_SLOT )
                in_range = 0;
        }
        RSCACHE_CHECK(in_range);
        RSCACHE_CHECK_EQ(font->draw_width[0x01], font->draw_width[' ']);
        RSCache_Dat1PixFontFree(font);
    }
}

int
main(void)
{
    test_94_record_space_is_capital_i();
    test_full_space_is_lowercase_i();
    test_full_quill_space_is_capital_i();
    test_every_byte_has_a_width();
    return rscache_test_report("test_dat1_pix_font");
}
