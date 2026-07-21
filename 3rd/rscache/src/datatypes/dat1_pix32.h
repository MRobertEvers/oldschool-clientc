#ifndef RSCACHE_DATATYPES_DAT1_PIX32_H
#define RSCACHE_DATATYPES_DAT1_PIX32_H

// RGB sprite from a dat1 media jagfile ("<name>.dat" + "index.dat").
// Decoded through the shared 8-bit palette stream (see Client-TS Pix32.fromJagfile);
// pixels are 0xRRGGBB ints with 0 = transparent.
struct RSCache_Dat1Pix32
{
    int* pixels;
    int draw_width;
    int draw_height;
    int stride_x;
    int stride_y;
    int crop_x;
    int crop_y;
};

struct RSCache_Dat1Pix32*
RSCache_Dat1Pix32New(
    // "<name>.dat"
    void* data,
    int data_size,
    // "index.dat"
    void* index_data,
    int index_data_size,
    int sprite_idx);

void
RSCache_Dat1Pix32Free(struct RSCache_Dat1Pix32* pix32);

#endif
