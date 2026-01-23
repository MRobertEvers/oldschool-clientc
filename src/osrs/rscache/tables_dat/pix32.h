#ifndef PIX32_H
#define PIX32_H

#include "../cache_dat.h"

#include <stdbool.h>
#include <stdint.h>

// cropRight: number;
// cropBottom: number;
// cropLeft: number;
// cropTop: number;
// width: number;
// height: number;
struct CacheDatPix32
{
    int* pixels;
    int draw_width;
    int draw_height;
    int stride_x;
    int stride_y;
    int crop_x;
    int crop_y;
};

struct CacheDatPix32*
cache_dat_pix32_new(
    void* data,
    int data_size,
    void* index_data,
    int index_data_size,
    int spite_idx);

void
cache_dat_pix32_free(struct CacheDatPix32* pix32);
#endif