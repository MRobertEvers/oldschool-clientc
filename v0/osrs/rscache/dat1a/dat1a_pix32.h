#ifndef RSCACHE_RSCACHEDAT1A_PIX32_H
#define RSCACHE_RSCACHEDAT1A_PIX32_H

#include "../dat1disk/dat1disk.h"

#include <stdbool.h>
#include <stdint.h>

// cropRight: number;
// cropBottom: number;
// cropLeft: number;
// cropTop: number;
// width: number;
// height: number;
struct RSCacheDat1A_Pix32
{
    int* pixels;
    int draw_width;
    int draw_height;
    int stride_x;
    int stride_y;
    int crop_x;
    int crop_y;
};

struct RSCacheDat1A_Pix32*
RSCacheDat1A_Pix32New(
    void* data,
    int data_size,
    void* index_data,
    int index_data_size,
    int spite_idx);

void
RSCacheDat1A_Pix32Free(struct RSCacheDat1A_Pix32* pix32);
#endif