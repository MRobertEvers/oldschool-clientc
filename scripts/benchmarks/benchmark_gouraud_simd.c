#include <sys/time.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Bring in the rasterizers and helpers
#include "../../src/graphics/alpha.h"
#include "../../src/graphics/gouraud.u.c"

#define W 800
#define H 600
#define NUM_TRIS 20000
#define MIN_AREA_ABS 64

// Provide the color table definition expected by the raster code
int g_hsl16_to_rgb_table[65536];

static inline void
draw_scanline_gouraud_alpha_s4_nosimd(
    int* pixel_buffer,
    int stride_width,
    int y,
    int x_start,
    int x_end,
    int color_start_hsl16_ish8,
    int color_end_hsl16_ish8,
    int alpha)
{
    if( x_start == x_end )
        return;
    if( x_start > x_end )
    {
        int tmp;
        tmp = x_start;
        x_start = x_end;
        x_end = tmp;
        tmp = color_start_hsl16_ish8;
        color_start_hsl16_ish8 = color_end_hsl16_ish8;
        color_end_hsl16_ish8 = tmp;
    }

    int dx_stride = x_end - x_start;
    assert(dx_stride > 0);

    int dcolor_hsl16_ish8 = color_end_hsl16_ish8 - color_start_hsl16_ish8;

    int step_color_hsl16_ish8 = 0;
    if( dx_stride > 3 )
    {
        step_color_hsl16_ish8 = dcolor_hsl16_ish8 / dx_stride;
    }

    if( x_end >= stride_width )
    {
        x_end = stride_width - 1;
    }
    if( x_start < 0 )
    {
        color_start_hsl16_ish8 -= step_color_hsl16_ish8 * x_start;
        x_start = 0;
    }

    if( x_start >= x_end )
        return;

    dx_stride = x_end - x_start;

    // Steps by 4.
    int offset = x_start + y * stride_width;
    int steps = (dx_stride) >> 2;
    step_color_hsl16_ish8 <<= 2;

    int color_hsl16_ish8 = color_start_hsl16_ish8;
    while( --steps >= 0 )
    {
        int color_hsl16 = color_hsl16_ish8 >> 8;
        int rgb_color = g_hsl16_to_rgb_table[color_hsl16];

        // Tested in lumbridge church window, this is faster than the scalar version.
        // raster_linear_alpha_s4((uint32_t*)pixel_buffer, offset, rgb_color, alpha);
        // offset += 4;

        // Checked on 09/16/2025, clang does NOT vectorize this loop.
        // even with -O3
        for( int i = 0; i < 4; i++ )
        {
            int rgb_blend = pixel_buffer[offset];
            rgb_blend = alpha_blend(alpha, rgb_blend, rgb_color);
            pixel_buffer[offset] = rgb_blend;
            offset += 1;
        }

        color_hsl16_ish8 += step_color_hsl16_ish8;
    }

    int rgb_color = g_hsl16_to_rgb_table[color_hsl16_ish8 >> 8];
    int rgb_blend;
    steps = (dx_stride) & 0x3;
    switch( steps )
    {
    case 3:
        rgb_blend = pixel_buffer[offset];
        rgb_blend = alpha_blend(alpha, rgb_blend, rgb_color);
        pixel_buffer[offset] = rgb_blend;
        offset += 1;
    case 2:
        rgb_blend = pixel_buffer[offset];
        rgb_blend = alpha_blend(alpha, rgb_blend, rgb_color);
        pixel_buffer[offset] = rgb_blend;
        offset += 1;
    case 1:
        rgb_blend = pixel_buffer[offset];
        rgb_blend = alpha_blend(alpha, rgb_blend, rgb_color);
        pixel_buffer[offset] = rgb_blend;
    }
}

static inline void
raster_gouraud_alpha_s4_nosimd(
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
    int alpha)
{
    if( y0 > y1 )
    {
        int temp = y0;
        y0 = y1;
        y1 = temp;
        temp = x0;
        x0 = x1;
        x1 = temp;
        temp = color0_hsl16;
        color0_hsl16 = color1_hsl16;
        color1_hsl16 = temp;
    }

    if( y1 > y2 )
    {
        int temp = y1;
        y1 = y2;
        y2 = temp;
        temp = x1;
        x1 = x2;
        x2 = temp;
        temp = color1_hsl16;
        color1_hsl16 = color2_hsl16;
        color2_hsl16 = temp;
    }

    if( y0 > y1 )
    {
        int temp = y0;
        y0 = y1;
        y1 = temp;
        temp = x0;
        x0 = x1;
        x1 = temp;
        temp = color0_hsl16;
        color0_hsl16 = color1_hsl16;
        color1_hsl16 = temp;
    }

    int total_height = y2 - y0;
    if( total_height == 0 )
        return;

    if( x0 == x1 && x1 == x2 )
        return;

    int dx_AC = x2 - x0;
    int dy_AC = y2 - y0;
    int dx_AB = x1 - x0;
    int dy_AB = y1 - y0;
    int dx_BC = x2 - x1;
    int dy_BC = y2 - y1;

    int step_edge_x_AC_ish16 = (dy_AC > 0) ? ((dx_AC << 16) / dy_AC) : 0;
    int step_edge_x_AB_ish16 = (dy_AB > 0) ? ((dx_AB << 16) / dy_AB) : 0;
    int step_edge_x_BC_ish16 = (dy_BC > 0) ? ((dx_BC << 16) / dy_BC) : 0;

    int edge_x_AC_ish16 = x0 << 16;
    int edge_x_AB_ish16 = x0 << 16;
    int edge_x_BC_ish16 = x1 << 16;

    int dcolor_AC = color2_hsl16 - color0_hsl16;
    int dcolor_AB = color1_hsl16 - color0_hsl16;
    int dcolor_BC = color2_hsl16 - color1_hsl16;

    int step_edge_color_AC_ish15 = (dy_AC > 0) ? ((dcolor_AC << 15) / dy_AC) : 0;
    int step_edge_color_AB_ish15 = (dy_AB > 0) ? ((dcolor_AB << 15) / dy_AB) : 0;
    int step_edge_color_BC_ish15 = (dy_BC > 0) ? ((dcolor_BC << 15) / dy_BC) : 0;

    int edge_color_AC_ish15 = color0_hsl16 << 15;
    int edge_color_AB_ish15 = color0_hsl16 << 15;
    int edge_color_BC_ish15 = color1_hsl16 << 15;

    if( y0 < 0 )
    {
        edge_x_AC_ish16 -= step_edge_x_AC_ish16 * y0;
        edge_x_AB_ish16 -= step_edge_x_AB_ish16 * y0;
        edge_color_AC_ish15 -= step_edge_color_AC_ish15 * y0;
        edge_color_AB_ish15 -= step_edge_color_AB_ish15 * y0;
        y0 = 0;
    }

    if( y1 < 0 )
    {
        edge_x_BC_ish16 -= step_edge_x_BC_ish16 * y1;
        edge_color_BC_ish15 -= step_edge_color_BC_ish15 * y1;
        y1 = 0;
    }

    if( y1 >= screen_height )
        y1 = screen_height - 1;
    if( y2 >= screen_height )
        y2 = screen_height - 1;

    int i = y0;
    for( ; i < y1 && i < screen_height; ++i )
    {
        int x_start_current = edge_x_AC_ish16 >> 16;
        int x_end_current = edge_x_AB_ish16 >> 16;
        int color_start_current = edge_color_AC_ish15 >> 7;
        int color_end_current = edge_color_AB_ish15 >> 7;

        draw_scanline_gouraud_alpha_s4_nosimd(
            pixel_buffer,
            screen_width,
            i,
            x_start_current,
            x_end_current,
            color_start_current,
            color_end_current,
            alpha);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_AB_ish16 += step_edge_x_AB_ish16;
        edge_color_AC_ish15 += step_edge_color_AC_ish15;
        edge_color_AB_ish15 += step_edge_color_AB_ish15;
    }

    i = y1;
    for( ; i < y2 && i < screen_height; ++i )
    {
        int x_start_current = edge_x_AC_ish16 >> 16;
        int x_end_current = edge_x_BC_ish16 >> 16;
        int color_start_current = edge_color_AC_ish15 >> 7;
        int color_end_current = edge_color_BC_ish15 >> 7;

        draw_scanline_gouraud_alpha_s4_nosimd(
            pixel_buffer,
            screen_width,
            i,
            x_start_current,
            x_end_current,
            color_start_current,
            color_end_current,
            alpha);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_BC_ish16 += step_edge_x_BC_ish16;
        edge_color_AC_ish15 += step_edge_color_AC_ish15;
        edge_color_BC_ish15 += step_edge_color_BC_ish15;
    }
}

typedef struct
{
    int x0, y0, x1, y1, x2, y2;
    int c0, c1, c2;
} Tri;

static Tri tris[NUM_TRIS];

// Returns positive if points are counterclockwise, negative if clockwise
static int
check_winding(int x0, int y0, int x1, int y1, int x2, int y2)
{
    return (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
}

static void
gen_tris()
{
    printf("Generating %d triangles...\n", NUM_TRIS);
    srand(42);
    int i = 0;
    int attempts = 0;
    const int MAX_ATTEMPTS = NUM_TRIS * 10; // Prevent infinite loop

    while( i < NUM_TRIS && attempts < MAX_ATTEMPTS )
    {
        attempts++;
        int x0 = rand() % W;
        int y0 = rand() % H;
        int x1 = rand() % W;
        int y1 = rand() % H;
        int x2 = rand() % W;
        int y2 = rand() % H;

        // Make sure vertices are in counterclockwise order
        int winding = check_winding(x0, y0, x1, y1, x2, y2);
        if( winding < 0 )
        {
            // Swap vertices 1 and 2 to make counterclockwise
            int temp_x = x1;
            int temp_y = y1;
            x1 = x2;
            y1 = y2;
            x2 = temp_x;
            y2 = temp_y;
        }

        int dx01 = x1 - x0;
        int dy01 = y1 - y0;
        int dx02 = x2 - x0;
        int dy02 = y2 - y0;
        int area = dx01 * dy02 - dx02 * dy01;
        if( area < 0 )
            area = -area;
        if( area < MIN_AREA_ABS )
        {
            printf("Rejected triangle %d due to small area %d\n", i, area);
            continue;
        }

        // Also ensure reasonable span to exercise scanlines
        int minx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
        int maxx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
        if( maxx - minx < 8 )
        {
            printf("Rejected triangle %d due to small x-span %d\n", i, maxx - minx);
            continue;
        }

        int c0 = rand() & 0xFFFF;
        int c1 = rand() & 0xFFFF;
        int c2 = rand() & 0xFFFF;

        // If we swapped vertices for CCW order, swap colors too
        if( winding < 0 )
        {
            int temp_c = c1;
            c1 = c2;
            c2 = temp_c;
        }

        if( i >= sizeof(tris) / sizeof(tris[0]) )
        {
            printf("Error: Trying to write beyond tris array bounds at index %d\n", i);
            break;
        }

        tris[i].x0 = x0;
        tris[i].y0 = y0;
        tris[i].x1 = x1;
        tris[i].y1 = y1;
        tris[i].x2 = x2;
        tris[i].y2 = y2;
        tris[i].c0 = c0;
        tris[i].c1 = c1;
        tris[i].c2 = c2;
        i++;
    }

    printf("Generated %d triangles after %d attempts\n", i, attempts);
    if( i < NUM_TRIS )
    {
        printf("Warning: Only generated %d triangles out of requested %d\n", i, NUM_TRIS);
    }
}

static double
time_func(void (*func)(int*, int))
{
    struct timeval start, end;
    gettimeofday(&start, NULL);
    static int buf[W * H];
    memset(buf, 0, sizeof(buf));
    func(buf, 128);
    gettimeofday(&end, NULL);
    double us = (end.tv_sec - start.tv_sec) * 1000000.0;
    us += (end.tv_usec - start.tv_usec);
    return us;
}

static void
bench_old(int* buffer, int alpha)
{
    for( int i = 0; i < NUM_TRIS; i++ )
    {
        Tri t = tris[i];
        raster_gouraud_alpha_s4(
            buffer, W, H, t.x0, t.y0, t.x1, t.y1, t.x2, t.y2, t.c0, t.c1, t.c2, alpha);
    }
}

static void
bench_new(int* buffer, int alpha)
{
    for( int i = 0; i < NUM_TRIS; i++ )
    {
        Tri t = tris[i];
        raster_gouraud_alpha_s4_nosimd(
            buffer, W, H, t.x0, t.x1, t.x2, t.y0, t.y1, t.y2, t.c0, t.c1, t.c2, alpha);
    }
}

int
main()
{
    // Initialize trivial HSL16 -> RGB lookup as grayscale
    for( int i = 0; i < 65536; i++ )
    {
        int v = i & 0xFF;
        g_hsl16_to_rgb_table[i] = (v << 16) | (v << 8) | v;
    }

    gen_tris();

    // Warm-up
    {
        static int tmp[W * H];
        bench_old(tmp, 128);
        bench_new(tmp, 128);
    }

    double t_old = time_func(bench_old);
    double t_new = time_func(bench_new);

    printf("Alpha Gouraud OLD (edge color): %.1f us\n", t_old);
    printf("Alpha Gouraud NEW (simd): %.1f us\n", t_new);
    printf("Speedup: %.2fx\n", t_old / t_new);

    return 0;
}

// clang -O3 benchmark_gouraud_simd.c -o benchmark_gouraud_simd
// ./benchmark_gouraud_simd