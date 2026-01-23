#ifndef PIXFONT_H
#define PIXFONT_H

#include "../cache_dat.h"

#include <stdbool.h>
#include <stdint.h>

// static readonly CHARSET: string =
// 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!"£$%^&*()-_=+[{]};:\'@#~,<.>/?\\|
// '; static readonly CHARCODESET: number[] = [];

// private readonly charMask: Int8Array[] = [];
// readonly charMaskWidth: Int32Array = new Int32Array(94);
// readonly charMaskHeight: Int32Array = new Int32Array(94);
// readonly charOffsetX: Int32Array = new Int32Array(94);
// readonly charOffsetY: Int32Array = new Int32Array(94);
// readonly charAdvance: Int32Array = new Int32Array(95);
// readonly drawWidth: Int32Array = new Int32Array(256);

static const char CHARSET[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!\",£$%"
    "^&*()-_=+[{]};:\'@#~,<.>/?\\| ";

#define CHAR_COUNT (sizeof(CHARSET) - 1)

struct CacheDatPixfont
{
    int* charcode_set;
    int* char_mask[CHAR_COUNT];
    int char_mask_count;

    int char_mask_width[CHAR_COUNT];
    int char_mask_height[CHAR_COUNT];
    int char_offset_x[CHAR_COUNT];
    int char_offset_y[CHAR_COUNT];
    int char_advance[CHAR_COUNT + 1];
    int draw_width[256];
};

static void
cache_dat_pixfont_init();

struct CacheDatPixfont*
cache_dat_pixfont_new_decode(
    void* data,
    int data_size,
    void* index_data,
    int index_data_size);

void
cache_dat_pixfont_free(struct CacheDatPixfont* pixfont);

void
pixfont_draw_text(
    struct CacheDatPixfont* pixfont,
    char* text,
    int x,
    int y,
    int* pixels,
    int stride);
#endif