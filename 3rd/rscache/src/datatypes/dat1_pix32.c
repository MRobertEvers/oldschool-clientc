#include "dat1_pix32.h"

#include "../rsbuffer.h"

#include <stdlib.h>
#include <string.h>

struct RSCache_Dat1Pix32*
RSCache_Dat1Pix32New(
    void* data,
    int data_size,
    void* index_data,
    int index_data_size,
    int sprite_idx)
{
    struct RSCache_Dat1Pix32* pix32 = malloc(sizeof(struct RSCache_Dat1Pix32));
    if( !pix32 )
        return NULL;
    memset(pix32, 0, sizeof(struct RSCache_Dat1Pix32));

    struct RSCache_Buffer databuf;
    struct RSCache_Buffer indexbuf;
    RSCache_BufferInit(&databuf, (uint8_t*)data, (uint32_t)data_size);
    RSCache_BufferInit(&indexbuf, (uint8_t*)index_data, (uint32_t)index_data_size);

    indexbuf.position = g2(&databuf);
    int draw_width = g2(&indexbuf);
    int draw_height = g2(&indexbuf);

    int palette_count = g1(&indexbuf);
    int* palette = malloc(palette_count * sizeof(int));
    if( !palette )
    {
        free(pix32);
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
        free(pix32);
        return NULL;
    }

    int crop_x = g1(&indexbuf);
    int crop_y = g1(&indexbuf);
    int width = g2(&indexbuf);
    int height = g2(&indexbuf);
    int pixel_order = g1(&indexbuf);
    int pixel_count = width * height;

    int* pixels = malloc(pixel_count * sizeof(int));
    if( !pixels )
    {
        free(palette);
        free(pix32);
        return NULL;
    }
    memset(pixels, 0, pixel_count * sizeof(int));

    if( pixel_order == 0 )
    {
        if( databuf.position + pixel_count > databuf.size )
        {
            free(palette);
            free(pixels);
            free(pix32);
            return NULL;
        }
        for( int i = 0; i < pixel_count; i++ )
        {
            int pixel_index = g1(&databuf);
            pixels[i] = palette[pixel_index];
        }
    }
    else if( pixel_order == 1 )
    {
        for( int x = 0; x < width; x++ )
        {
            for( int y = 0; y < height; y++ )
            {
                int pixel_index = g1(&databuf);
                if( pixel_index < 0 || pixel_index >= palette_count )
                {
                    free(palette);
                    free(pixels);
                    free(pix32);
                    return NULL;
                }
                pixels[x + y * width] = palette[pixel_index];
            }
        }
    }

    free(palette);

    pix32->pixels = pixels;
    pix32->draw_width = draw_width;
    pix32->draw_height = draw_height;
    pix32->crop_x = crop_x;
    pix32->crop_y = crop_y;
    pix32->stride_x = width;
    pix32->stride_y = height;

    return pix32;
}

void
RSCache_Dat1Pix32Free(struct RSCache_Dat1Pix32* pix32)
{
    if( !pix32 )
        return;
    free(pix32->pixels);
    free(pix32);
}
