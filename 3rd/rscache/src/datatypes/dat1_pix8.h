#ifndef RSCACHE_DATATYPES_DAT1_PIX8_H
#define RSCACHE_DATATYPES_DAT1_PIX8_H

#include <stdint.h>

// Palette-indexed sprite from a dat1 media jagfile ("<name>.dat" + "index.dat").
// See Client-TS Pix8.depack: crop_width/crop_height (owi/ohi) are shared across
// all sprites in one image; palette index 0 is transparent.
struct RSCache_Dat1Pix8
{
    uint8_t* pixels;
    int width;
    int height;

    int crop_width;
    int crop_height;
    int crop_x;
    int crop_y;

    int* palette;
    int palette_count;
};

struct RSCache_Dat1Pix8*
RSCache_Dat1Pix8New(
    // "<name>.dat"
    void* data,
    int data_size,
    // "index.dat"
    void* index_data,
    int index_data_size,
    int sprite_idx);

void
RSCache_Dat1Pix8Free(struct RSCache_Dat1Pix8* pix8);

#endif
