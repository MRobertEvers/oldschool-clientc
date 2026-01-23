#include "pixfont.h"

#include "../rsbuf.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static char CHARCODESET[256] = { 0 };

// idx.pos = dat.g2() + 4; // skip cropW and cropH

// const off: number = idx.g1();
// if (off > 0) {
//     // skip palette
//     idx.pos += (off - 1) * 3;
// }

// const font: PixFont = new PixFont();

// for (let i: number = 0; i < 94; i++) {
//     font.charOffsetX[i] = idx.g1();
//     font.charOffsetY[i] = idx.g1();

//     const w: number = (font.charMaskWidth[i] = idx.g2());
//     const h: number = (font.charMaskHeight[i] = idx.g2());

//     const type: number = idx.g1();
//     const len: number = w * h;

//     font.charMask[i] = new Int8Array(len);

//     if (type === 0) {
//         for (let j: number = 0; j < w * h; j++) {
//             font.charMask[i][j] = dat.g1b();
//         }
//     } else if (type === 1) {
//         for (let x: number = 0; x < w; x++) {
//             for (let y: number = 0; y < h; y++) {
//                 font.charMask[i][x + y * w] = dat.g1b();
//             }
//         }
//     }

//     if (h > font.height2d) {
//         font.height2d = h;
//     }

//     font.charOffsetX[i] = 1;
//     font.charAdvance[i] = w + 2;

//     {
//         let space: number = 0;
//         for (let y: number = (h / 7) | 0; y < h; y++) {
//             space += font.charMask[i][y * w];
//         }

//         if (space <= ((h / 7) | 0)) {
//             font.charAdvance[i]--;
//             font.charOffsetX[i] = 0;
//         }
//     }

//     {
//         let space: number = 0;
//         for (let y: number = (h / 7) | 0; y < h; y++) {
//             space += font.charMask[i][w + y * w - 1];
//         }

//         if (space <= ((h / 7) | 0)) {
//             font.charAdvance[i]--;
//         }
//     }
// }

// font.charAdvance[94] = font.charAdvance[8];
// for (let i: number = 0; i < 256; i++) {
//     font.drawWidth[i] = font.charAdvance[PixFont.CHARCODESET[i]];
// }

// return font;

static inline int
index_of_char(char c)
{
    for( int i = 0; i < sizeof(CHARSET); i++ )
    {
        if( CHARSET[i] == c )
            return i;
    }
    return -1;
}

static void
cache_dat_pixfont_init()
{
    for( int i = 0; i < 256; i++ )
    {
        int c = index_of_char(i);
        if( c == -1 )
            c = 74; // space

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
    char* text,
    int x,
    int y,
    int* pixels,
    int stride)
{
    int length = strlen(text);
    for( int i = 0; i < length; i++ )
    {
        int c = CHARCODESET[text[i]];
        if( c != CHAR_COUNT )
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