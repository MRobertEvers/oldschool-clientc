#ifndef GOURAUD_H
#define GOURAUD_H

void draw_scanline_gouraud_zbuf(
    int* pixel_buffer,
    int* z_buffer,
    int stride_width,
    int y,
    int x_start,
    int x_end,
    int depth_start,
    int depth_end,
    int color_start,
    int color_end);

void draw_scanline_gouraud(
    int* pixel_buffer,
    int stride_width,
    int y,
    int x_start,
    int x_end,
    int color_start,
    int color_end,
    int alpha);

void raster_gouraud_zbuf(
    int* pixel_buffer,
    int* z_buffer,
    int screen_width,
    int screen_height,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int z0,
    int z1,
    int z2,
    int color0,
    int color1,
    int color2);

/**
 * Attention! The colors must all be the same hue and saturation.
 *
 * The colors must ONLY be lightness delta.
 *
 * Use the RGB interpolation for more complex color vertices.
 */
void raster_gouraud(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int color0_hsl16,
    int color1_hsl16,
    int color2_hsl16,
    int alpha);

void raster_gouraud_lerp(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int color0,
    int color1,
    int color2);

#endif
