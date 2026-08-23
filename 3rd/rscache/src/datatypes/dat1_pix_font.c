#include "dat1_pix_font.h"

#include "../rsbuffer.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

// UTF16 because '£' compiles to 0x00A3, which is 2 bytes wide even in a char array.
// RuneScape only compares the low byte of each code point ('£' -> 0xA3, non-ascii,
// so there is no collision with the ascii entries).
static const uint16_t CHARSET[] = {
    'A', 'B',  'C', 'D', 'E',  'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O',  'P', 'Q', 'R',  'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    'a', 'b',  'c', 'd', 'e',  'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
    'n', 'o',  'p', 'q', 'r',  's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '0', '1',  '2', '3', '4',  '5', '6', '7', '8', '9', '!', '"', 0x00A3 /*'£'*/,
    '$', '%',  '^', '&', '*',  '(', ')', '-', '_', '=', '+', '[', '{',
    ']', '}',  ';', ':', '\'', '@', '#', '~', ',', '<', '.', '>', '/',
    '?', '\\', '|', ' '
};

/* 95 entries, not 94: the last one is the SPACE, which has no glyph record in
 * either layout. That is why char_advance is one longer than every other
 * per-glyph array -- CHARSET[94] addresses char_advance[94] and nothing else.
 * A search that stops at CHAR_COUNT cannot find ' ', and the two callers below
 * both ask for it. */
_Static_assert(
    sizeof(CHARSET) / sizeof(CHARSET[0]) == RSCACHE_DAT1_PIXFONT_SPACE_SLOT + 1,
    "CHARSET is the 94 glyph records plus the advance-only space");

static int
index_of_char(uint8_t c)
{
    for( int i = 0; i <= RSCACHE_DAT1_PIXFONT_SPACE_SLOT; i++ )
    {
        if( (CHARSET[i] & 0xFF) == c )
            return i;
    }
    return -1;
}

static void
pixfont_init_charcodeset(struct RSCache_Dat1PixFont* pixfont)
{
    for( int i = 0; i < 256; i++ )
    {
        int c = index_of_char((uint8_t)i);
        if( c == -1 )
            c = RSCACHE_DAT1_PIXFONT_SPACE_SLOT;
        pixfont->charcodeset[i] = (char)c;
    }
}

struct RSCache_Dat1PixFont*
RSCache_Dat1PixFontNewDecode(
    void* data,
    int data_size,
    void* index_data,
    int index_data_size)
{
    struct RSCache_Dat1PixFont* pixfont = malloc(sizeof(struct RSCache_Dat1PixFont));
    if( !pixfont )
        return NULL;
    memset(pixfont, 0, sizeof(struct RSCache_Dat1PixFont));
    pixfont_init_charcodeset(pixfont);

    struct RSCache_Buffer databuf;
    struct RSCache_Buffer indexbuf;
    RSCache_BufferInit(&databuf, (uint8_t*)data, (uint32_t)data_size);
    RSCache_BufferInit(&indexbuf, (uint8_t*)index_data, (uint32_t)index_data_size);

    // skip cropW and cropH
    indexbuf.position = g2(&databuf) + 4;
    int off = g1(&indexbuf);
    if( off > 0 )
        // skip palette
        indexbuf.position += (off - 1) * 3;

    for( int i = 0; i < RSCACHE_DAT1_PIXFONT_CHAR_COUNT; i++ )
    {
        pixfont->char_offset_x[i] = g1(&indexbuf);
        pixfont->char_offset_y[i] = g1(&indexbuf);

        int w = g2(&indexbuf);
        int h = g2(&indexbuf);
        pixfont->char_mask_width[i] = w;
        pixfont->char_mask_height[i] = h;

        int type = g1(&indexbuf);
        int len = w * h;

        pixfont->char_mask[i] = malloc(len * sizeof(int));
        if( !pixfont->char_mask[i] )
        {
            RSCache_Dat1PixFontFree(pixfont);
            return NULL;
        }
        memset(pixfont->char_mask[i], 0, len * sizeof(int));
        if( type == 0 )
        {
            for( int j = 0; j < len; j++ )
                pixfont->char_mask[i][j] = g1b(&databuf);
        }
        else if( type == 1 )
        {
            for( int x = 0; x < w; x++ )
            {
                for( int y = 0; y < h; y++ )
                    pixfont->char_mask[i][x + y * w] = g1b(&databuf);
            }
        }

        pixfont->char_offset_x[i] = 1;
        pixfont->char_advance[i] = w + 2;

        int space = 0;
        for( int y = h / 7; y < h; y++ )
        {
            space += pixfont->char_mask[i][y * w];
        }

        if( space <= h / 7 )
        {
            pixfont->char_advance[i]--;
            pixfont->char_offset_x[i] = 0;
        }

        space = 0;
        for( int y = h / 7; y < h; y++ )
        {
            space += pixfont->char_mask[i][w + y * w - 1];
        }

        if( space <= h / 7 )
        {
            pixfont->char_advance[i]--;
        }
    }

    // Space has no record here -- the file stops at '|' -- so it takes 'I''s
    // advance, CHARSET index 8. The reference does exactly this one assignment
    // and nothing to '|' (index 93), which is a glyph like any other.
    pixfont->char_advance[RSCACHE_DAT1_PIXFONT_SPACE_SLOT] = pixfont->char_advance[8];
    for( int i = 0; i < 256; i++ )
    {
        pixfont->draw_width[i] =
            pixfont->char_advance[(unsigned char)pixfont->charcodeset[i]];
    }

    return pixfont;
}

struct RSCache_Dat1PixFont*
RSCache_Dat1PixFontFullNewDecode(
    void* data,
    int data_size,
    void* index_data,
    int index_data_size,
    int quill)
{
    struct RSCache_Dat1PixFont* pixfont = malloc(sizeof(struct RSCache_Dat1PixFont));
    if( !pixfont )
        return NULL;
    memset(pixfont, 0, sizeof(struct RSCache_Dat1PixFont));
    pixfont_init_charcodeset(pixfont);

    struct RSCache_Buffer databuf;
    struct RSCache_Buffer indexbuf;
    RSCache_BufferInit(&databuf, (uint8_t*)data, (uint32_t)data_size);
    RSCache_BufferInit(&indexbuf, (uint8_t*)index_data, (uint32_t)index_data_size);

    // skip cropW and cropH
    indexbuf.position = g2(&databuf) + 4;
    int off = g1(&indexbuf);
    if( off > 0 )
        // skip palette
        indexbuf.position += (off - 1) * 3;

    for( int code = 0; code < RSCACHE_DAT1_PIXFONT_FULL_RECORD_COUNT; code++ )
    {
        int offset_x = g1(&indexbuf);
        int offset_y = g1(&indexbuf);
        int w = g2(&indexbuf);
        int h = g2(&indexbuf);
        int type = g1(&indexbuf);
        int len = w * h;

        // The pixel bytes are one sequential stream shared by every record, so
        // a record this struct cannot address still has to be READ -- skipping
        // it would shift every glyph after it. Decode into scratch and drop it.
        int* mask = malloc((len > 0 ? len : 1) * sizeof(int));
        if( !mask )
        {
            RSCache_Dat1PixFontFree(pixfont);
            return NULL;
        }
        memset(mask, 0, (len > 0 ? len : 1) * sizeof(int));
        if( type == 0 )
        {
            for( int j = 0; j < len; j++ )
                mask[j] = g1b(&databuf);
        }
        else if( type == 1 )
        {
            for( int x = 0; x < w; x++ )
            {
                for( int y = 0; y < h; y++ )
                    mask[x + y * w] = g1b(&databuf);
            }
        }

        // Codes outside CHARSET are dropped rather than folded onto space:
        // charcodeset already routes an unmapped byte to the space GLYPH at
        // draw time, and storing byte 0x01's bitmap there would replace it.
        //
        // Space itself is dropped for a different reason: it is an advance and
        // not a glyph, so it has no bitmap slot to be stored in -- and the
        // advance this record would give it is overwritten by the donor below.
        int slot = index_of_char((uint8_t)code);
        if( slot < 0 || slot >= RSCACHE_DAT1_PIXFONT_CHAR_COUNT )
        {
            free(mask);
            continue;
        }

        free(pixfont->char_mask[slot]);
        pixfont->char_mask[slot] = mask;
        pixfont->char_mask_width[slot] = w;
        pixfont->char_mask_height[slot] = h;
        pixfont->char_offset_x[slot] = offset_x;
        pixfont->char_offset_y[slot] = offset_y;

        pixfont->char_offset_x[slot] = 1;
        pixfont->char_advance[slot] = w + 2;

        int space = 0;
        for( int y = h / 7; y < h; y++ )
            space += pixfont->char_mask[slot][y * w];
        if( space <= h / 7 )
        {
            pixfont->char_advance[slot]--;
            pixfont->char_offset_x[slot] = 0;
        }

        space = 0;
        for( int y = h / 7; y < h; y++ )
            space += pixfont->char_mask[slot][w + y * w - 1];
        if( space <= h / 7 )
            pixfont->char_advance[slot]--;
    }

    // Space takes its width from a letter rather than from its own (empty)
    // glyph: 'I' for the quill font, 'i' for the rest. Getting this wrong is
    // not subtle at draw time -- 'I' is two pixels wider than 'i' in b12, so
    // every b12 heading came out two pixels per word too long and ran over the
    // box it was centred in.
    {
        int donor = index_of_char(quill ? (uint8_t)'I' : (uint8_t)'i');
        assert(donor >= 0);
        pixfont->char_advance[RSCACHE_DAT1_PIXFONT_SPACE_SLOT] = pixfont->char_advance[donor];
    }

    for( int i = 0; i < 256; i++ )
    {
        pixfont->draw_width[i] =
            pixfont->char_advance[(unsigned char)pixfont->charcodeset[i]];
    }

    return pixfont;
}

void
RSCache_Dat1PixFontFree(struct RSCache_Dat1PixFont* pixfont)
{
    if( !pixfont )
        return;
    for( int i = 0; i < RSCACHE_DAT1_PIXFONT_CHAR_COUNT; i++ )
    {
        free(pixfont->char_mask[i]);
    }
    free(pixfont);
}
