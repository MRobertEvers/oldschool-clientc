#ifndef FLAT_H
#define FLAT_H

void draw_scanline_flat(
    int* pixel_buffer, int stride_width, int y, int x_start, int x_end, int color_hsl16);

void raster_flat(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int color_hsl16);

void raster_flat_alpha_step4(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int color_hsl16,
    int alpha);

#endif
