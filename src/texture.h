#ifndef TEXTURE_H
#define TEXTURE_H

#define SWAP(a, b)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        typeof(a) temp = a;                                                                        \
        a = b;                                                                                     \
        b = temp;                                                                                  \
    } while( 0 )

void draw_scanline_texture_zbuf(
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

// void draw_scanline_texture(
//     int* pixel_buffer,
//     int screen_width,
//     int screen_height,
//     int y,
//     int screen_x_start,
//     int screen_x_end,
//     int z_start,
//     int z_end,
//     int u_start,
//     int u_end,
//     int v_start,
//     int v_end,
//     int* texels,
//     int texture_width,
//     int texture_opaque);

void raster_texture_zbuf(
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

void raster_texture(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int screen_x0,
    int screen_x1,
    int screen_x2,
    int screen_y0,
    int screen_y1,
    int screen_y2,
    int screen_z0,
    int screen_z1,
    int screen_z2,
    int orthographic_x0,
    int orthographic_x1,
    int orthographic_x2,
    int orthographic_y0,
    int orthographic_y1,
    int orthographic_y2,
    int orthographic_z0,
    int orthographic_z1,
    int orthographic_z2,
    int* texels,
    int texture_width,
    int texture_opaque);

void raster_texture_step(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int screen_x0,
    int screen_x1,
    int screen_x2,
    int screen_y0,
    int screen_y1,
    int screen_y2,
    int screen_z0,
    int screen_z1,
    int screen_z2,
    int orthographic_uvorigin_x0,
    int orthographic_uend_x1,
    int orthographic_vend_x2,
    int orthographic_uvorigin_y0,
    int orthographic_uend_y1,
    int orthographic_vend_y2,
    int orthographic_uvorigin_z0,
    int orthographic_uend_z1,
    int orthographic_vend_z2,
    int* texels,
    int texture_width,
    int texture_opaque);

void raster_texture_step_blend(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int screen_x0,
    int screen_x1,
    int screen_x2,
    int screen_y0,
    int screen_y1,
    int screen_y2,
    int screen_z0,
    int screen_z1,
    int screen_z2,
    int orthographic_uvorigin_x0,
    int orthographic_uend_x1,
    int orthographic_vend_x2,
    int orthographic_uvorigin_y0,
    int orthographic_uend_y1,
    int orthographic_vend_y2,
    int orthographic_uvorigin_z0,
    int orthographic_uend_z1,
    int orthographic_vend_z2,
    int color_a,
    int color_b,
    int color_c,
    int* texels,
    int texture_width,
    int texture_opaque);

void textureRaster(
    int xA,
    int xB,
    int* dst,
    int offset,
    int* texels,
    int curU,
    int curV,
    int u,
    int v,
    int w,
    int uStride,
    int vStride,
    int wStride,
    int shadeA,
    int shadeB);

void textureTriangle(
    int xA,
    int xB,
    int xC,
    int yA,
    int yB,
    int yC,
    int shadeA,
    int shadeB,
    int shadeC,
    int originX,
    int originY,
    int originZ,
    int txB,
    int txC,
    int tyB,
    int tyC,
    int tzB,
    int tzC,
    int* pixel_buffer,
    int* texels,
    int texture_width);

#endif
