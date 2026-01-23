#include "pix32.h"

#include "../rsbuf.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

// const dat: Packet = new Packet(archive.read(name + '.dat'));
//         const index: Packet = new Packet(archive.read('index.dat'));

//         // cropW/cropH are shared across all sprites in a single image
//         index.pos = dat.g2();
//         const cropW: number = index.g2();
//         const cropH: number = index.g2();

//         // palette is shared across all images in a single archive
//         const paletteCount: number = index.g1();
//         const palette: number[] = [];
//         const length: number = paletteCount - 1;
//         for (let i: number = 0; i < length; i++) {
//             // the first color (0) is reserved for transparency
//             palette[i + 1] = index.g3();

//             // black (0) will become transparent, make it black (1) so it's visible
//             if (palette[i + 1] === 0) {
//                 palette[i + 1] = 1;
//             }
//         }

//         // advance to sprite
//         for (let i: number = 0; i < sprite; i++) {
//             index.pos += 2;
//             dat.pos += index.g2() * index.g2();
//             index.pos += 1;
//         }

//         if (dat.pos > dat.length || index.pos > index.length) {
//             throw new Error();
//         }

//         // read sprite
//         const cropX: number = index.g1();
//         const cropY: number = index.g1();
//         const width: number = index.g2();
//         const height: number = index.g2();

//         const image: Pix32 = new Pix32(width, height);
//         image.cropLeft = cropX;
//         image.cropTop = cropY;
//         image.width = cropW;
//         image.height = cropH;

//         const pixelOrder: number = index.g1();
//         if (pixelOrder === 0) {
//             const length: number = image.cropRight * image.cropBottom;
//             for (let i: number = 0; i < length; i++) {
//                 image.pixels[i] = palette[dat.g1()];
//             }
//         } else if (pixelOrder === 1) {
//             const width: number = image.cropRight;
//             for (let x: number = 0; x < width; x++) {
//                 const height: number = image.cropBottom;
//                 for (let y: number = 0; y < height; y++) {
//                     image.pixels[x + y * width] = palette[dat.g1()];
//                 }
//             }
//         }

//         return image;

struct CacheDatPix32*
cache_dat_pix32_new(
    void* data,
    int data_size,
    void* index_data,
    int index_data_size,
    int spite_idx)
{
    struct CacheDatPix32* pix32 = (struct CacheDatPix32*)malloc(sizeof(struct CacheDatPix32));
    if( !pix32 )
        return NULL;
    memset(pix32, 0, sizeof(struct CacheDatPix32));

    struct RSBuffer databuf = { .data = data, .size = data_size };
    struct RSBuffer indexbuf = { .data = index_data, .size = index_data_size };

    indexbuf.position = g2(&databuf);
    int draw_width = g2(&indexbuf);
    int draw_height = g2(&indexbuf);

    int palette_count = g1(&indexbuf);
    int* palette = malloc(palette_count * sizeof(int));
    if( !palette )
        return NULL;
    memset(palette, 0, palette_count * sizeof(int));
    for( int i = 0; i < palette_count - 1; i++ )
    {
        int color = g3(&indexbuf);
        if( color == 0 )
            color = 1;
        palette[i + 1] = color;
    }

    for( int i = 0; i < spite_idx; i++ )
    {
        indexbuf.position += 2;
        databuf.position += g2(&indexbuf) * g2(&indexbuf);
        indexbuf.position++;
    }

    if( databuf.position > databuf.size || indexbuf.position > indexbuf.size )
        return NULL;

    int crop_x = g1(&indexbuf);
    int crop_y = g1(&indexbuf);
    int width = g2(&indexbuf);
    int height = g2(&indexbuf);
    int pixel_order = g1(&indexbuf);
    int pixel_count = width * height;

    int* pixels = malloc(pixel_count * sizeof(int));
    if( !pixels )
        return NULL;
    memset(pixels, 0, pixel_count * sizeof(int));

    if( pixel_order == 0 )
    {
        if( databuf.position + pixel_count > databuf.size )
            return NULL;
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
                assert(pixel_index >= 0 && pixel_index < palette_count);
                pixels[x + y * width] = palette[pixel_index];
            }
        }
    }

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
cache_dat_pix32_free(struct CacheDatPix32* pix32)
{
    if( !pix32 )
        return;
    free(pix32->pixels);
    free(pix32);
}