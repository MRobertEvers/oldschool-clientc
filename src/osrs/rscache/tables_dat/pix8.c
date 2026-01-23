#include "pix8.h"

#include "../rsbuf.h"

#include <stdio.h>
#include <string.h>

// // cropW/cropH are shared across all sprites in a single image
// index.pos = dat.g2();
// const cropW: number = index.g2();
// const cropH: number = index.g2();

// // palette is shared across all images in a single archive
// const paletteCount: number = index.g1();
// const palette: Int32Array = new Int32Array(paletteCount);
// // the first color (0) is reserved for transparency
// for (let i: number = 1; i < paletteCount; i++) {
//     palette[i] = index.g3();
//     // black (0) will become transparent, make it black (1) so it's visible
//     if (palette[i] === 0) {
//         palette[i] = 1;
//     }
// }

// // advance to sprite
// for (let i: number = 0; i < sprite; i++) {
//     index.pos += 2;
//     dat.pos += index.g2() * index.g2();
//     index.pos += 1;
// }

// if (dat.pos > dat.length || index.pos > index.length) {
//     throw new Error();
// }

// // read sprite
// const cropX: number = index.g1();
// const cropY: number = index.g1();
// const width: number = index.g2();
// const height: number = index.g2();

// const image: Pix8 = new Pix8(width, height, palette);
// image.cropX = cropX;
// image.cropY = cropY;
// image.cropW = cropW;
// image.cropH = cropH;

// const pixels: Int8Array = image.pixels;
// const pixelOrder: number = index.g1();
// if (pixelOrder === 0) {
//     const length: number = image.width2d * image.height2d;
//     for (let i: number = 0; i < length; i++) {
//         pixels[i] = dat.g1b();
//     }
// } else if (pixelOrder === 1) {
//     const width: number = image.width2d;
//     const height: number = image.height2d;
//     for (let x: number = 0; x < width; x++) {
//         for (let y: number = 0; y < height; y++) {
//             pixels[x + y * width] = dat.g1b();
//         }
//     }
// }

// return image;
struct CacheDatPix8Palette*
cache_dat_pix8_palette_new(
    void* data,
    int data_size,
    void* index_data,
    int index_data_size,
    int spite_idx)
{
    struct CacheDatPix8Palette* pix8 =
        (struct CacheDatPix8Palette*)malloc(sizeof(struct CacheDatPix8Palette));
    if( !pix8 )
        return NULL;
    memset(pix8, 0, sizeof(struct CacheDatPix8Palette));

    struct RSBuffer databuf = { .data = data, .size = data_size };
    struct RSBuffer indexbuf = { .data = index_data, .size = index_data_size };

    indexbuf.position = g2(&databuf);
    int crop_width = g2(&indexbuf);
    int crop_height = g2(&indexbuf);
    int palette_count = g1(&indexbuf);
    int* palette = malloc(palette_count * sizeof(int));
    if( !palette )
        return NULL;
    memset(palette, 0, palette_count * sizeof(int));
    // The first color (0) is reserved for transparency
    for( int i = 0; i < palette_count - 1; i++ )
    {
        int color = g3(&indexbuf);
        // black (0) will become transparent, make it black (1) so it's visible
        if( color == 0 )
            color = 1;
        palette[i + 1] = color;
    }

    // Advance to sprite
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

    uint8_t* pixels = malloc(pixel_count * sizeof(uint8_t));
    if( !pixels )
        return NULL;
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
cache_dat_pix8_palette_free(struct CacheDatPix8Palette* pix8_palette)
{
    if( !pix8_palette )
        return;
    free(pix8_palette->pixels);
    free(pix8_palette->palette);
    free(pix8_palette);
}