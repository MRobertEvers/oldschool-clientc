#include "dat1_pix8.h"

#include "../rsbuffer.h"

#include <stdlib.h>
#include <string.h>

// Client-TS Pix8.depack:
// index.pos = dat.g2();
// owi = index.g2(); ohi = index.g2();          // crop_width/crop_height
// bpalCount = index.g1();
// for (i = 0; i < bpalCount - 1; i++) bpal[i + 1] = index.g3();
// for (i = 0; i < sprite; i++) { index.pos += 2; dat.pos += index.g2() * index.g2(); index.pos +=
// 1; }
// xof = index.g1(); yof = index.g1(); wi = index.g2(); hi = index.g2();
// encoding = index.g1();
// encoding 0: row-major g1b stream; encoding 1: column-major.
struct RSCache_Dat1Pix8*
RSCache_Dat1Pix8New(
    void* data,
    int data_size,
    void* index_data,
    int index_data_size,
    int sprite_idx)
{
    struct RSCache_Dat1Pix8* pix8 = malloc(sizeof(struct RSCache_Dat1Pix8));
    if( !pix8 )
        return NULL;
    memset(pix8, 0, sizeof(struct RSCache_Dat1Pix8));

    struct RSCache_Buffer databuf;
    struct RSCache_Buffer indexbuf;
    RSCache_BufferInit(&databuf, (uint8_t*)data, (uint32_t)data_size);
    RSCache_BufferInit(&indexbuf, (uint8_t*)index_data, (uint32_t)index_data_size);

    indexbuf.position = g2(&databuf);
    int crop_width = g2(&indexbuf);
    int crop_height = g2(&indexbuf);
    int palette_count = g1(&indexbuf);
    int* palette = malloc(palette_count * sizeof(int));
    if( !palette )
    {
        free(pix8);
        return NULL;
    }
    memset(palette, 0, palette_count * sizeof(int));
    // The first color (0) is reserved for transparency
    for( int i = 0; i < palette_count - 1; i++ )
    {
        int color = g3(&indexbuf);
        // black (0) would become transparent, make it black (1) so it's visible
        if( color == 0 )
            color = 1;
        palette[i + 1] = color;
    }

    // Advance to sprite
    for( int i = 0; i < sprite_idx; i++ )
    {
        indexbuf.position += 2;
        databuf.position += g2(&indexbuf) * g2(&indexbuf);
        indexbuf.position++;
    }

    if( databuf.position > databuf.size || indexbuf.position > indexbuf.size )
    {
        free(palette);
        free(pix8);
        return NULL;
    }

    int crop_x = g1(&indexbuf);
    int crop_y = g1(&indexbuf);
    int width = g2(&indexbuf);
    int height = g2(&indexbuf);
    int pixel_order = g1(&indexbuf);
    int pixel_count = width * height;

    uint8_t* pixels = malloc(pixel_count * sizeof(uint8_t));
    if( !pixels )
    {
        free(palette);
        free(pix8);
        return NULL;
    }
    memset(pixels, 0, pixel_count * sizeof(uint8_t));

    if( pixel_order == 0 )
    {
        for( int i = 0; i < pixel_count; i++ )
            pixels[i] = g1b(&databuf);
    }
    else if( pixel_order == 1 )
    {
        for( int x = 0; x < width; x++ )
            for( int y = 0; y < height; y++ )
                pixels[x + y * width] = g1b(&databuf);
    }

    pix8->width = width;
    pix8->height = height;
    pix8->crop_width = crop_width;
    pix8->crop_height = crop_height;
    pix8->crop_x = crop_x;
    pix8->crop_y = crop_y;
    pix8->pixels = pixels;
    pix8->palette = palette;
    pix8->palette_count = palette_count;

    return pix8;
}

void
RSCache_Dat1Pix8Free(struct RSCache_Dat1Pix8* pix8)
{
    if( !pix8 )
        return;
    free(pix8->pixels);
    free(pix8->palette);
    free(pix8);
}
