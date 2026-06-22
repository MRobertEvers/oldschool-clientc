#ifndef RSCACHE_RSCACHEDAT1A_PIX8_H
#define RSCACHE_RSCACHEDAT1A_PIX8_H

#include "../dat1disk/dat1disk.h"

struct RSCacheDat1A_Pix8Palette
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

struct RSCacheDat1A_Pix8Palette*
RSCacheDat1A_Pix8PaletteNew(
    // "name.dat"
    void* data,
    int data_size,
    // "index.dat"
    void* index_data,
    int index_data_size,
    int spite_idx);

void
RSCacheDat1A_Pix8PaletteFree(struct RSCacheDat1A_Pix8Palette* pix8_palette);
#endif