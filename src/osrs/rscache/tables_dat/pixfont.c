#include "pixfont.h"

#include "../rsbuf.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static char CHARCODESET[256] = { 0 };

/**
 * Runescape only looks at the first byte of a UTF16 code point.
 * For example, str.charCodeAt(str.indexOf('£')) returns 163 or a3, which is not ascii anyway and
 * all the other characters are ascii, so there is no collision.
 * @param c
 * @return int
 */
static inline int
index_of_char(uint8_t c)
{
    for( int i = 0; i < CHAR_COUNT; i++ )
    {
        if( (CHARSET[i] & 0xFF) == c )
            return i;
    }
    return -1;
}

static void
cache_dat_pixfont_init()
{
    // Initialize CHARCODESET for single-byte values (0-255)
    for( int i = 0; i < 256; i++ )
    {
        int c = index_of_char((uint8_t)i);
        if( c == -1 )
            c = index_of_char(' '); // space

        assert(c < CHAR_COUNT);
        CHARCODESET[i] = c;
    }
}

struct CacheDatPixfont*
cache_dat_pixfont_new_decode(
    void* data,
    int data_size,
    void* index_data,
    int index_data_size)
{
    cache_dat_pixfont_init();
    struct CacheDatPixfont* pixfont =
        (struct CacheDatPixfont*)malloc(sizeof(struct CacheDatPixfont));
    if( !pixfont )
        return NULL;
    memset(pixfont, 0, sizeof(struct CacheDatPixfont));

    struct RSBuffer databuf = { .data = data, .size = data_size };
    struct RSBuffer indexbuf = { .data = index_data, .size = index_data_size };

    // skip cropW and cropH
    indexbuf.position = g2(&databuf) + 4;
    int off = g1(&indexbuf);
    if( off > 0 )
        // skip palette
        indexbuf.position += (off - 1) * 3;

    for( int i = 0; i < 94; i++ )
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
            return NULL;
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

        // if (h > font.height2d) {
        //     font.height2d = h;
        // }

        pixfont->char_offset_x[i] = 1;
        pixfont->char_advance[i] = w + 2;

        int space = 0;
        for( int y = (h / 7) | 0; y < h; y++ )
        {
            space += pixfont->char_mask[i][y * w];
        }

        if( space <= ((h / 7) | 0) )
        {
            pixfont->char_advance[i]--;
            pixfont->char_offset_x[i] = 0;
        }

        space = 0;
        for( int y = (h / 7) | 0; y < h; y++ )
        {
            space += pixfont->char_mask[i][w + y * w - 1];
        }

        if( space <= ((h / 7) | 0) )
        {
            pixfont->char_advance[i]--;
        }
    }

    // space is the 94th char. It has the same offset as the 8th char.
    pixfont->char_advance[94] = pixfont->char_advance[8];
    for( int i = 0; i < 256; i++ )
    {
        pixfont->draw_width[i] = pixfont->char_advance[CHARCODESET[i]];
    }

    return pixfont;
}

void
cache_dat_pixfont_free(struct CacheDatPixfont* pixfont)
{
    if( !pixfont )
        return;
    for( int i = 0; i < 94; i++ )
    {
        free(pixfont->char_mask[i]);
    }
    free(pixfont);
}

static void
drawMask(
    int w,
    int h,
    int* src,
    int srcOff,
    int srcStep,
    int* dst,
    int dstOff,
    int dstStep,
    int rgb)
{
    int hw = -(w >> 2);
    w = -(w & 0x3);
    for( int y = -h; y < 0; y++ )
    {
        for( int x = hw; x < 0; x++ )
        {
            if( src[srcOff++] == 0 )
            {
                dstOff++;
            }
            else
            {
                dst[dstOff++] = rgb;
            }

            if( src[srcOff++] == 0 )
            {
                dstOff++;
            }
            else
            {
                dst[dstOff++] = rgb;
            }

            if( src[srcOff++] == 0 )
            {
                dstOff++;
            }
            else
            {
                dst[dstOff++] = rgb;
            }

            if( src[srcOff++] == 0 )
            {
                dstOff++;
            }
            else
            {
                dst[dstOff++] = rgb;
            }
        }

        for( int x = w; x < 0; x++ )
        {
            if( src[srcOff++] == 0 )
            {
                dstOff++;
            }
            else
            {
                dst[dstOff++] = rgb;
            }
        }

        dstOff += dstStep;
        srcOff += srcStep;
    }
}

void
pixfont_draw_text(
    struct CacheDatPixfont* pixfont,
    uint8_t* text,
    int x,
    int y,
    int* pixels,
    int stride)
{
    // Calculate length of UTF16 string (null-terminated)
    int length = strlen(text);

    for( int i = 0; i < length; i++ )
    {
        uint8_t code_point = text[i];
        int c = 0;
        c = CHARCODESET[code_point];

        if( c < CHAR_COUNT )
        {
            int w = pixfont->char_mask_width[c];
            int h = pixfont->char_mask_height[c];
            int* mask = pixfont->char_mask[c];
            drawMask(
                w,
                h,
                mask,
                0,
                0,
                pixels,
                x + pixfont->char_offset_x[c] + pixfont->char_offset_y[c] * stride,
                stride - w,
                0xFF00FF);
        }
        x += pixfont->char_advance[c];
    }
}