#ifndef RSCACHE_DATATYPES_DAT1_PIX_FONT_H
#define RSCACHE_DATATYPES_DAT1_PIX_FONT_H

#include <stdint.h>

// 94 drawable glyphs (A-Z a-z 0-9 punctuation, space last). See Client-TS PixFont.
#define RSCACHE_DAT1_PIXFONT_CHAR_COUNT 94

// Bitmap font from the dat1 title jagfile ("<name>.dat" + "index.dat", e.g. b12.dat).
struct RSCache_Dat1PixFont
{
    // Per-glyph coverage mask (width*height ints, nonzero = opaque).
    int* char_mask[RSCACHE_DAT1_PIXFONT_CHAR_COUNT];
    int char_mask_width[RSCACHE_DAT1_PIXFONT_CHAR_COUNT];
    int char_mask_height[RSCACHE_DAT1_PIXFONT_CHAR_COUNT];
    int char_offset_x[RSCACHE_DAT1_PIXFONT_CHAR_COUNT];
    int char_offset_y[RSCACHE_DAT1_PIXFONT_CHAR_COUNT];
    int char_advance[RSCACHE_DAT1_PIXFONT_CHAR_COUNT + 1];
    int draw_width[256];
    // Byte value -> glyph index (unmapped bytes fall back to space).
    char charcodeset[256];
};

struct RSCache_Dat1PixFont*
RSCache_Dat1PixFontNewDecode(
    // "<name>.dat"
    void* data,
    int data_size,
    // "index.dat"
    void* index_data,
    int index_data_size);

void
RSCache_Dat1PixFontFree(struct RSCache_Dat1PixFont* pixfont);

#endif
