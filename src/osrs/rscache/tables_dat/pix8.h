#ifndef PIX8_H
#define PIX8_H

#include "../cache_dat.h"

struct CacheDatPix8Palette
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

struct CacheDatPix8Palette*
cache_dat_pix8_palette_new(
    // "name.dat"
    void* data,
    int data_size,
    // "index.dat"
    void* index_data,
    int index_data_size,
    int spite_idx);

void
cache_dat_pix8_palette_free(struct CacheDatPix8Palette* pix8_palette);
#endif