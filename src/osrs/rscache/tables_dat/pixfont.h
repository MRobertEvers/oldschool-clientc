#ifndef PIXFONT_H
#define PIXFONT_H

#include "../cache_dat.h"

#include <stdbool.h>
#include <stdint.h>

// We have to use UTF16 here because '£' is gets compiled to 0x00A3, which is 2 bytes wide, even in
// a char array.
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

// #define CHAR_COUNT (sizeof(CHARSET) - 1)
#define CHAR_COUNT 94

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
    uint8_t* text,
    int x,
    int y,
    int* pixels,
    int stride);
#endif