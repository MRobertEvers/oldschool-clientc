#ifndef RSCACHE_DATATYPES_DAT1_PIX_FONT_H
#define RSCACHE_DATATYPES_DAT1_PIX_FONT_H

#include <stdint.h>

// 94 drawable glyph RECORDS (A-Z a-z 0-9 punctuation, '|' last). See Client-TS PixFont.
#define RSCACHE_DAT1_PIXFONT_CHAR_COUNT 94

// The space, which is one past the last glyph record. It has no bitmap in
// either layout -- neither the 94-record file nor the 256-record one supplies
// its width -- so it is an ADVANCE ONLY, taken from a letter after the fact
// ('I' in the 94-record layout, 'i' or 'I' in the 256-record one). Hence
// char_advance is CHAR_COUNT + 1 long while every other array is CHAR_COUNT.
#define RSCACHE_DAT1_PIXFONT_SPACE_SLOT RSCACHE_DAT1_PIXFONT_CHAR_COUNT

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

// Number of glyph records in a "full" font's index stream: one per byte value,
// addressed directly by character code.
#define RSCACHE_DAT1_PIXFONT_FULL_RECORD_COUNT 256

/*
 * The other dat1 font layout: 256 glyph records indexed BY CHARACTER CODE,
 * rather than 94 records in CHARSET order.
 *
 * LostCity's later builds (274, 289) ship these as "<name>_full" in the title
 * jagfile -- p11_full, p12_full, b12_full, q8_full. The record encoding is
 * identical; what changes is how many records there are and what a record's
 * position means, which is exactly the kind of difference that cannot be a
 * parameter to one loop without every read in it having to ask which font it
 * is decoding. Hence a second decoder, not a flag.
 *
 * The result is the SAME struct: all 256 records are consumed (the pixel bytes
 * are one sequential stream, so skipping a record would misalign every record
 * after it), and the 94 whose character code is in CHARSET are kept at their
 * CHARSET position. Characters outside that set are not addressable by this
 * struct -- which is no loss against the 94-glyph layout, where they do not
 * exist at all.
 *
 * `quill`: the reference derives the space advance from 'I' for the quill font
 * (q8) and from 'i' for every other. Pass 1 only for q8.
 */
struct RSCache_Dat1PixFont*
RSCache_Dat1PixFontFullNewDecode(
    // "<name>_full.dat"
    void* data,
    int data_size,
    // "index.dat"
    void* index_data,
    int index_data_size,
    int quill);

void
RSCache_Dat1PixFontFree(struct RSCache_Dat1PixFont* pixfont);

#endif
