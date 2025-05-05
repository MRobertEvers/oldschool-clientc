#include "gouraud.c"

#include <SDL.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720

static int g_sin_table[2048];
static int g_cos_table[2048];

static int g_palette[65536];

static int tmp_depth_face_count[1500] = { 0 };
static int tmp_depth_faces[1500][512] = { 0 };
static int tmp_priority_face_count[12] = { 0 };
static int tmp_priority_depth_sum[12] = { 0 };
static int tmp_priority_faces[12][2000] = { 0 };

void
init_sin_table()
{
    // 0.0030679615 = 2 * PI / 2048
    // (int)(sin((double)i * 0.0030679615) * 65536.0);
    for( int i = 0; i < 2048; i++ )
        g_sin_table[i] = (int)(sin((double)i * 0.0030679615) * (1 << 16));
}

void
init_cos_table()
{
    // 0.0030679615 = 2 * PI / 2048
    // (int)(cos((double)i * 0.0030679615) * 65536.0);
    for( int i = 0; i < 2048; i++ )
        g_cos_table[i] = (int)(cos((double)i * 0.0030679615) * (1 << 16));
}

static inline int
interpolate(int a, int b, float t)
{
    return a + (int)((b - a) * t);
}

int
pix3d_set_gamma(int rgb, double gamma)
{
    double r = (double)(rgb >> 16) / 256.0;
    double g = (double)(rgb >> 8 & 0xff) / 256.0;
    double b = (double)(rgb & 0xff) / 256.0;
    double powR = pow(r, gamma);
    double powG = pow(g, gamma);
    double powB = pow(b, gamma);
    int intR = (int)(powR * 256.0);
    int intG = (int)(powG * 256.0);
    int intB = (int)(powB * 256.0);
    return (intR << 16) + (intG << 8) + intB;
}

void
pix3d_set_brightness(int* palette, double brightness)
{
    double random_brightness = brightness;
    int offset = 0;
    for( int y = 0; y < 512; y++ )
    {
        double hue = (double)(y / 8) / 64.0 + 0.0078125;
        double saturation = (double)(y & 0x7) / 8.0 + 0.0625;
        for( int x = 0; x < 128; x++ )
        {
            double lightness = (double)x / 128.0;
            double r = lightness;
            double g = lightness;
            double b = lightness;
            if( saturation != 0.0 )
            {
                double q;
                if( lightness < 0.5 )
                {
                    q = lightness * (saturation + 1.0);
                }
                else
                {
                    q = lightness + saturation - lightness * saturation;
                }
                double p = lightness * 2.0 - q;
                double t = hue + 0.3333333333333333;
                if( t > 1.0 )
                {
                    t--;
                }
                double d11 = hue - 0.3333333333333333;
                if( d11 < 0.0 )
                {
                    d11++;
                }
                if( t * 6.0 < 1.0 )
                {
                    r = p + (q - p) * 6.0 * t;
                }
                else if( t * 2.0 < 1.0 )
                {
                    r = q;
                }
                else if( t * 3.0 < 2.0 )
                {
                    r = p + (q - p) * (0.6666666666666666 - t) * 6.0;
                }
                else
                {
                    r = p;
                }
                if( hue * 6.0 < 1.0 )
                {
                    g = p + (q - p) * 6.0 * hue;
                }
                else if( hue * 2.0 < 1.0 )
                {
                    g = q;
                }
                else if( hue * 3.0 < 2.0 )
                {
                    g = p + (q - p) * (0.6666666666666666 - hue) * 6.0;
                }
                else
                {
                    g = p;
                }
                if( d11 * 6.0 < 1.0 )
                {
                    b = p + (q - p) * 6.0 * d11;
                }
                else if( d11 * 2.0 < 1.0 )
                {
                    b = q;
                }
                else if( d11 * 3.0 < 2.0 )
                {
                    b = p + (q - p) * (0.6666666666666666 - d11) * 6.0;
                }
                else
                {
                    b = p;
                }
            }
            int intR = (int)(r * 256.0);
            int intG = (int)(g * 256.0);
            int intB = (int)(b * 256.0);
            int rgb = (intR << 16) + (intG << 8) + intB;
            int rgbAdjusted = pix3d_set_gamma(rgb, random_brightness);
            palette[offset++] = rgbAdjusted;
        }
    }
}

void
init_palette()
{
    pix3d_set_brightness(g_palette, 0.8);
}

struct Point3D
{
    int x;
    int y;
    int z;
};

struct Point2D
{
    int x;
    int y;
};

struct Triangle2D
{
    struct Point3D p1;
    struct Point3D p2;
    struct Point3D p3;
};

struct GouraudColors
{
    int color1;
    int color2;
    int color3;
};

struct Triangle3D
{
    struct Point3D p1;
    struct Point3D p2;
    struct Point3D p3;
};

struct Pixel
{
    char r;
    char g;
    char b;
    char a;
};

int g_depth_min = INT_MAX;
int g_depth_max = INT_MIN;

int
edge_interpolate(int x0, int y0, int x1, int y1, int y)
{
    // Integer linear interpolation: avoids floats
    if( y1 == y0 )
        return x0;
    return x0 + (x1 - x0) * (y - y0) / (y1 - y0);
}

static void
draw_scanline_gouraud(
    int* pixel_buffer,
    int* z_buffer,
    int y,
    int x_start,
    int x_end,
    int depth_start,
    int depth_end,
    int color_start,
    int color_end)
{
    if( x_start > x_end )
    {
        int tmp;
        tmp = x_start;
        x_start = x_end;
        x_end = tmp;
        tmp = depth_start;
        depth_start = depth_end;
        depth_end = tmp;
        tmp = color_start;
        color_start = color_end;
        color_end = tmp;
    }

    int dx = x_end - x_start;
    for( int x = x_start; x <= x_end; ++x )
    {
        float t = dx == 0 ? 0.0f : (float)(x - x_start) / dx;

        // int depth = interpolate(depth_start, depth_end, t);
        // if( z_buffer[y * SCREEN_WIDTH + x] >= depth )
        //     z_buffer[y * SCREEN_WIDTH + x] = depth;
        // else
        //     continue;

        int r = interpolate((color_start >> 16) & 0xFF, (color_end >> 16) & 0xFF, t);
        int g = interpolate((color_start >> 8) & 0xFF, (color_end >> 8) & 0xFF, t);
        int b = interpolate(color_start & 0xFF, color_end & 0xFF, t);
        int a = 0xFF; // Alpha value

        // g = 0;
        // b = 0;
        // int range = (g_depth_max - g_depth_min);
        // float numer = ((float)(depth + (g_depth_min)));
        // r = (numer / range) * 255;

        int color = (a << 24) | (r << 16) | (g << 8) | b;

        if( x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT )
            pixel_buffer[y * SCREEN_WIDTH + x] = color;
    }
}

int
interpolate_color(int color1, int color2, float t)
{
    int r1 = (color1 >> 16) & 0xFF;
    int g1 = (color1 >> 8) & 0xFF;
    int b1 = color1 & 0xFF;

    int r2 = (color2 >> 16) & 0xFF;
    int g2 = (color2 >> 8) & 0xFF;
    int b2 = color2 & 0xFF;

    int r = (int)(r1 + (r2 - r1) * t);
    int g = (int)(g1 + (g2 - g1) * t);
    int b = (int)(b1 + (b2 - b1) * t);

    return (r << 16) | (g << 8) | b;
}

void
raster_gouraud(
    int* pixel_buffer,
    int* z_buffer,
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
    int color2)
{
    // Sort vertices by y
    // where y0 is the bottom vertex and y2 is the top vertex
    if( y0 > y1 )
    {
        int t;
        t = y0;
        y0 = y1;
        y1 = t;
        t = x0;
        x0 = x1;
        x1 = t;
        t = z0;
        z0 = z1;
        z1 = t;
        t = color0;
        color0 = color1;
        color1 = t;
    }
    if( y0 > y2 )
    {
        int t;
        t = y0;
        y0 = y2;
        y2 = t;
        t = x0;
        x0 = x2;
        x2 = t;
        t = z0;
        z0 = z2;
        z2 = t;
        t = color0;
        color0 = color2;
        color2 = t;
    }
    if( y1 > y2 )
    {
        int t;
        t = y1;
        y1 = y2;
        y2 = t;
        t = x1;
        x1 = x2;
        x2 = t;
        t = z1;
        z1 = z2;
        z2 = t;
        t = color1;
        color1 = color2;
        color2 = t;
    }

    int total_height = y2 - y0;
    if( total_height == 0 )
        return;

    for( int i = 0; i < total_height; ++i )
    {
        bool second_half = i > (y1 - y0) || y1 == y0;
        int segment_height = second_half ? y2 - y1 : y1 - y0;
        if( segment_height == 0 )
            continue;

        float alpha = (float)i / total_height;
        float beta = (float)(i - (second_half ? y1 - y0 : 0)) / segment_height;

        int ax = interpolate(x0, x2, alpha);
        int bx = second_half ? interpolate(x1, x2, beta) : interpolate(x0, x1, beta);

        // interpolate colors

        int acolor = interpolate_color(color0, color2, alpha);
        int bcolor = second_half ? interpolate_color(color1, color2, beta)
                                 : interpolate_color(color0, color1, beta);

        int adepth = interpolate(z0, z2, alpha);
        int bdepth = second_half ? interpolate(z1, z2, beta) : interpolate(z0, z1, beta);

        /**
         * The decompiled renderer code uses the average of the three z values
         * to calculate the depth of the scanline.
         */
        int depth_average = (z0 + z1 + z2) / 3;

        adepth = depth_average;
        bdepth = depth_average;

        int y = y0 + i;
        draw_scanline_gouraud(pixel_buffer, z_buffer, y, ax, bx, adepth, bdepth, acolor, bcolor);
    }
}

// void
// fill_triangle_gouraud(
//     struct Pixel* pixel_buffer,
//     struct GouraudColors colors,
//     struct Point2D p0,
//     struct Point2D p1,
//     struct Point2D p2)
// {
//     raster_gouraud(
//         (int*)pixel_buffer,
//         p0.x,
//         p1.x,
//         p2.x,
//         p0.y,
//         p1.y,
//         p2.y,
//         p0.z,
//         p1.z,
//         p2.z,
//         colors.color1,
//         colors.color2,
//         colors.color3);
// }

void
raster_triangle(
    struct Pixel* pixel_buffer,
    int* z_buffer,
    struct GouraudColors colors,
    struct Triangle2D triangle,
    int screen_width,
    int screen_height)
{
    // Rasterization logic goes here

    // fill_triangle(pixel_buffer, color, triangle.p1, triangle.p2, triangle.p3);
    // fill_triangle_gouraud(pixel_buffer, colors, triangle.p1, triangle.p2, triangle.p3);

    raster_gouraud(
        (int*)pixel_buffer,
        z_buffer,
        triangle.p1.x,
        triangle.p2.x,
        triangle.p3.x,
        triangle.p1.y,
        triangle.p2.y,
        triangle.p3.y,
        triangle.p1.z,
        triangle.p2.z,
        triangle.p3.z,
        colors.color1,
        colors.color2,
        colors.color3);
}

struct Triangle2D
project(
    int x1,
    int x2,
    int x3,
    int y1,
    int y2,
    int y3,
    int z1,
    int z2,
    int z3,
    int yaw,
    int pitch,
    int roll,
    int scene_x,
    int scene_y,
    int scene_z,
    int screen_width,
    int screen_height)
{
    struct Triangle2D projected_triangle;
    int cos_camera_yaw = g_cos_table[0];
    int sin_camera_yaw = g_sin_table[0];
    int cos_camera_pitch = g_cos_table[0];
    int sin_camera_pitch = g_sin_table[0];
    int a = (scene_z * cos_camera_yaw - scene_x * sin_camera_yaw) >> 16;
    int b = (scene_y * sin_camera_pitch + a * cos_camera_pitch) >> 16;
    // b = 0;

    // Apply rotation
    int sin_yaw = g_sin_table[yaw];
    int cos_yaw = g_cos_table[yaw];
    int sin_pitch = g_sin_table[pitch];
    int cos_pitch = g_cos_table[pitch];
    int sin_roll = g_sin_table[roll];
    int cos_roll = g_cos_table[roll];

    // Rotate around Y-axis
    int x1_rotated = x1 * cos_yaw - z1 * sin_yaw;
    x1_rotated >>= 16;
    int z1_rotated = x1 * sin_yaw + z1 * cos_yaw;
    z1_rotated >>= 16;
    int x2_rotated = x2 * cos_yaw - z2 * sin_yaw;
    x2_rotated >>= 16;
    int z2_rotated = x2 * sin_yaw + z2 * cos_yaw;
    z2_rotated >>= 16;
    int x3_rotated = x3 * cos_yaw - z3 * sin_yaw;
    x3_rotated >>= 16;
    int z3_rotated = x3 * sin_yaw + z3 * cos_yaw;
    z3_rotated >>= 16;
    // Rotate around X-axis
    int y1_rotated = y1 * cos_pitch - z1_rotated * sin_pitch;
    y1_rotated >>= 16;
    int z1_rotated2 = y1 * sin_pitch + z1_rotated * cos_pitch;
    z1_rotated2 >>= 16;
    int y2_rotated = y2 * cos_pitch - z2_rotated * sin_pitch;
    y2_rotated >>= 16;
    int z2_rotated2 = y2 * sin_pitch + z2_rotated * cos_pitch;
    z2_rotated2 >>= 16;
    int y3_rotated = y3 * cos_pitch - z3_rotated * sin_pitch;
    y3_rotated >>= 16;
    int z3_rotated2 = y3 * sin_pitch + z3_rotated * cos_pitch;
    z3_rotated2 >>= 16;
    // Rotate around Z-axis
    int x1_final = x1_rotated * cos_roll - y1_rotated * sin_roll;
    x1_final >>= 16;
    int y1_final = x1_rotated * sin_roll + y1_rotated * cos_roll;
    y1_final >>= 16;
    int x2_final = x2_rotated * cos_roll - y2_rotated * sin_roll;
    x2_final >>= 16;
    int y2_final = x2_rotated * sin_roll + y2_rotated * cos_roll;
    y2_final >>= 16;
    int x3_final = x3_rotated * cos_roll - y3_rotated * sin_roll;
    x3_final >>= 16;
    int y3_final = x3_rotated * sin_roll + y3_rotated * cos_roll;
    y3_final >>= 16;

    // Perspective projection
    int z1_final = z1_rotated2 + scene_z;
    int z2_final = z2_rotated2 + scene_z;
    int z3_final = z3_rotated2 + scene_z;
    int screen_x1 = (x1_final * 256) / z1_final + scene_x;
    int screen_y1 = (y1_final * 256) / z1_final + scene_y;
    int screen_x2 = (x2_final * 256) / z2_final + scene_x;
    int screen_y2 = (y2_final * 256) / z2_final + scene_y;
    int screen_x3 = (x3_final * 256) / z3_final + scene_x;
    int screen_y3 = (y3_final * 256) / z3_final + scene_y;

    // Clip to screen bounds
    if( screen_x1 < 0 )
        screen_x1 = 0;
    if( screen_x1 >= screen_width )

        screen_x1 = screen_width - 1;
    if( screen_y1 < 0 )
        screen_y1 = 0;
    if( screen_y1 >= screen_height )
        screen_y1 = screen_height - 1;
    if( screen_x2 < 0 )
        screen_x2 = 0;
    if( screen_x2 >= screen_width )
        screen_x2 = screen_width - 1;
    if( screen_y2 < 0 )
        screen_y2 = 0;
    if( screen_y2 >= screen_height )
        screen_y2 = screen_height - 1;
    if( screen_x3 < 0 )
        screen_x3 = 0;
    if( screen_x3 >= screen_width )
        screen_x3 = screen_width - 1;
    if( screen_y3 < 0 )
        screen_y3 = 0;
    if( screen_y3 >= screen_height )
        screen_y3 = screen_height - 1;

    // Set the projected triangle
    projected_triangle.p1.x = screen_x1;
    projected_triangle.p1.y = screen_y1;
    projected_triangle.p1.z = z1_final - b;
    projected_triangle.p2.x = screen_x2;
    projected_triangle.p2.y = screen_y2;
    projected_triangle.p2.z = z2_final - b;
    projected_triangle.p3.x = screen_x3;
    projected_triangle.p3.y = screen_y3;
    projected_triangle.p3.z = z3_final - b;

    return projected_triangle;
}

int
main(int argc, char* argv[])
{
    init_cos_table();
    init_sin_table();
    init_palette();

    // load model.vt
    FILE* file = fopen("../model.vertices", "rb");
    if( file == NULL )
    {
        printf("Failed to open model.vt\n");
        return -1;
    }

    // Read the file into a buffer
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char* buffer = (char*)malloc(file_size);
    if( buffer == NULL )
    {
        printf("Failed to allocate memory for model.vt\n");
        fclose(file);
        return -1;
    }
    size_t bytes_read = fread(buffer, 1, file_size, file);
    if( bytes_read != file_size )
    {
        printf("Failed to read model.vt\n");
        free(buffer);
        fclose(file);
        return -1;
    }
    fclose(file);

    // read int from the first buffer
    int num_vertices = *(int*)buffer;
    printf("num_vertices: %d\n", num_vertices);

    int* vertex_x = (int*)malloc(num_vertices * sizeof(int));
    int* vertex_y = (int*)malloc(num_vertices * sizeof(int));
    int* vertex_z = (int*)malloc(num_vertices * sizeof(int));

    memcpy(vertex_x, buffer + 4, num_vertices * sizeof(int));
    memcpy(vertex_y, buffer + 4 + num_vertices * sizeof(int), num_vertices * sizeof(int));
    memcpy(vertex_z, buffer + 4 + num_vertices * sizeof(int) * 2, num_vertices * sizeof(int));
    free(buffer);

    // load the faces
    file = fopen("../model.faces", "rb");
    if( file == NULL )
    {
        printf("Failed to open model.faces\n");
        return -1;
    }

    // Read the file into a buffer
    fseek(file, 0, SEEK_END);
    file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    buffer = (char*)malloc(file_size);
    if( buffer == NULL )
    {
        printf("Failed to allocate memory for model.faces\n");
        fclose(file);
        return -1;
    }

    bytes_read = fread(buffer, 1, file_size, file);
    if( bytes_read != file_size )
    {
        printf("Failed to read model.faces\n");
        free(buffer);
        fclose(file);
        return -1;
    }

    fclose(file);

    // read int from the first buffer
    int num_faces = *(int*)buffer;
    printf("num_faces: %d\n", num_faces);

    int* face_a = (int*)malloc(num_faces * sizeof(int));
    int* face_b = (int*)malloc(num_faces * sizeof(int));
    int* face_c = (int*)malloc(num_faces * sizeof(int));

    memcpy(face_a, buffer + 4, num_faces * sizeof(int));
    memcpy(face_b, buffer + 4 + num_faces * sizeof(int), num_faces * sizeof(int));
    memcpy(face_c, buffer + 4 + num_faces * sizeof(int) * 2, num_faces * sizeof(int));
    free(buffer);

    // load the colors
    file = fopen("../model.colors", "rb");
    if( file == NULL )
    {
        printf("Failed to open model.colors\n");
        return -1;
    }

    // Read the file into a buffer
    fseek(file, 0, SEEK_END);
    file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    buffer = (char*)malloc(file_size);
    if( buffer == NULL )
    {
        printf("Failed to allocate memory for model.faces\n");
        fclose(file);
        return -1;
    }

    bytes_read = fread(buffer, 1, file_size, file);
    if( bytes_read != file_size )
    {
        printf("Failed to read model.faces\n");
        free(buffer);
        fclose(file);
        return -1;
    }

    fclose(file);

    // read int from the first buffer
    num_faces = *(int*)buffer;
    printf("num_faces: %d\n", num_faces);

    int* colors_a = (int*)malloc(num_faces * sizeof(int));
    int* colors_b = (int*)malloc(num_faces * sizeof(int));
    int* colors_c = (int*)malloc(num_faces * sizeof(int));

    memcpy(colors_a, buffer + 4, num_faces * sizeof(int));
    memcpy(colors_b, buffer + 4 + num_faces * sizeof(int), num_faces * sizeof(int));
    memcpy(colors_c, buffer + 4 + num_faces * sizeof(int) * 2, num_faces * sizeof(int));
    free(buffer);

    // Load the face priorities
    file = fopen("../model.priorities", "rb");
    if( file == NULL )
    {
        printf("Failed to open model.priorities\n");
        return -1;
    }

    // Read the file into a buffer
    fseek(file, 0, SEEK_END);
    file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    buffer = (char*)malloc(file_size);
    if( buffer == NULL )
    {
        printf("Failed to allocate memory for model.priorities\n");
        fclose(file);
        return -1;
    }

    bytes_read = fread(buffer, 1, file_size, file);
    if( bytes_read != file_size )
    {
        printf("Failed to read model.priorities\n");
        free(buffer);
        fclose(file);
        return -1;
    }

    fclose(file);

    // read int from the first buffer
    num_faces = *(int*)buffer;
    printf("num_faces: %d\n", num_faces);
    int* face_priority = (int*)malloc(num_faces * sizeof(int));
    memcpy(face_priority, buffer + 4, num_faces * sizeof(int));
    free(buffer);

    // print the face priorities
    // for( int i = 0; i < num_faces; i++ )
    // {
    //     printf("face_priority[%d]: %d\n", i, face_priority[i]);
    // }

    int max_model_depth = 211;

    struct Triangle3D* triangles =
        (struct Triangle3D*)malloc(num_faces * sizeof(struct Triangle3D));
    for( int i = 0; i < num_faces; i++ )
    {
        triangles[i].p1.x = vertex_x[face_a[i]];
        triangles[i].p1.y = vertex_y[face_a[i]];
        triangles[i].p1.z = vertex_z[face_a[i]];

        triangles[i].p2.x = vertex_x[face_b[i]];
        triangles[i].p2.y = vertex_y[face_b[i]];
        triangles[i].p2.z = vertex_z[face_b[i]];

        triangles[i].p3.x = vertex_x[face_c[i]];
        triangles[i].p3.y = vertex_y[face_c[i]];
        triangles[i].p3.z = vertex_z[face_c[i]];
    }

    /**
     * Sort faces by "priority"
     */

    int sorted_triangle_indexes[12][500] = { 0 };
    int sorted_triangle_count[12] = { 0 };
    for( int i = 0; i < num_faces; i++ )
    {
        int face_prio = face_priority[i];
        sorted_triangle_indexes[face_prio][sorted_triangle_count[face_prio]++] = i;
    }

    /**
     * Sort by depth
     */
    // _Model.tmp_depth_face_count = calloc(MODEL_MAX_DEPTH, sizeof(int));
    // _Model.tmp_depth_faces = calloc(MODEL_MAX_DEPTH, sizeof(int *));
    // for (int i = 0; i < MODEL_MAX_DEPTH; i++) {
    //     _Model.tmp_depth_faces[i] = calloc(MODEL_DEPTH_FACE_COUNT, sizeof(int));
    // }
    // _Model.tmp_priority_face_count = calloc(12, sizeof(int));
    // _Model.tmp_priority_faces = calloc(12, sizeof(int *));

    struct Triangle2D* triangles_2d =
        (struct Triangle2D*)malloc(num_faces * sizeof(struct Triangle2D));
    int* triangles_2d_map = (int*)malloc(num_faces * sizeof(int));

    int model_pitch = 0;
    int model_yaw = 500;
    int model_roll = 0;

    // int model_yawcos = g_cos_table[model_yaw];
    // int model_yawsin = g_sin_table[model_yaw];

    int triangle_count = 0;
    for( int i = 0; i < num_faces; i++ )
    {
        // calculate normals and skip if facing away from the camera

        // calculate the normal of the triangle
        // int dx1 = triangles[i].p2.x - triangles[i].p1.x;
        // int dy1 = triangles[i].p2.y - triangles[i].p1.y;
        // int dz1 = triangles[i].p2.z - triangles[i].p1.z;
        // int dx2 = triangles[i].p3.x - triangles[i].p1.x;
        // int dy2 = triangles[i].p3.y - triangles[i].p1.y;
        // int dz2 = triangles[i].p3.z - triangles[i].p1.z;
        // int normal_x = dy1 * dz2 - dy2 * dz1;
        // int normal_y = dz1 * dx2 - dz2 * dx1;
        // int normal_z = dx1 * dy2 - dx2 * dy1;
        // check if the triangle is facing away from the camera
        // if( normal_z < 0 )
        // {
        //     // skip the triangle
        //     continue;
        // }

        // int x = m->vertices_x[v];
        // int y = m->vertices_y[v];
        // int z = m->vertices_z[v];
        // int temp;
        // if (yaw != 0) {
        //     temp = (z * yawsin + x * yawcos) >> 16;
        //     z = (z * yawcos - x * yawsin) >> 16;
        //     x = temp;
        // }
        // x += sceneX;
        // y += sceneY;
        // z += sceneZ;
        // temp = (z * sinCameraYaw + x * cosCameraYaw) >> 16;
        // z = (z * cosCameraYaw - x * sinCameraYaw) >> 16;
        // x = temp;
        // temp = (y * cosCameraPitch - z * sinCameraPitch) >> 16;
        // z = (y * sinCameraPitch + z * cosCameraPitch) >> 16;

        // int scene_z = 420;
        // int scene_x = 0;
        // int scene_y = 0;
        // int cos_camera_yaw = g_cos_table[0];
        // int sin_camera_yaw = g_sin_table[0];
        // int cos_camera_pitch = g_cos_table[0];
        // int sin_camera_pitch = g_sin_table[0];
        // int a = (scene_z * cos_camera_yaw - scene_x * sin_camera_yaw) >> 16;
        // int b = (scene_y * sin_camera_pitch + a * cos_camera_pitch) >> 16;

        // int x = triangles[i].p1.x;
        // int y = triangles[i].p1.y;
        // int z = triangles[i].p1.z;

        // z = scene_z + triangles[i].p1.z;
        // if( z < g_depth_min )
        //     g_depth_min = z;
        // if( z > g_depth_max )
        //     g_depth_max = z;

        // // Project the 3D point to 2D with model_pitch, yaw, roll
        // triangles_2d[triangle_count].p1.x <<= 9;
        // triangles_2d[triangle_count].p1.y <<= 9;
        // triangles_2d[triangle_count].p1.z = z;
        // int x = (triangles[i].p1.x * cos_camera_yaw - triangles[i].p1.z * sin_camera_yaw) >> 16;
        // int y = (triangles[i].p1.y * cos_camera_pitch - triangles[i].p1.z * sin_camera_pitch) >>
        // 16; int z = (triangles[i].p1.z * cos_camera_pitch + triangles[i].p1.y * sin_camera_pitch)
        // >> 16;

        // int temp;
        // if( model_yaw != 0 )
        // {
        //     temp = (z * model_yawsin + x * model_yawcos) >> 16;
        //     z = (z * model_yawcos - x * model_yawsin) >> 16;
        //     x = temp;
        // }

        // // triangles_2d[triangle_count].p1.x = (triangles[i].p1.x << 9) / (z);
        // // triangles_2d[triangle_count].p1.y = (triangles[i].p1.y << 9) / (z);
        // // triangles_2d[triangle_count].p1.z = z;

        // z = scene_z + triangles[i].p2.z;
        // if( z < g_depth_min )
        //     g_depth_min = z;
        // if( z > g_depth_max )
        //     g_depth_max = z;

        // triangles_2d[triangle_count].p2.x = (triangles[i].p2.x << 9) / (z);
        // triangles_2d[triangle_count].p2.y = (triangles[i].p2.y << 9) / (z);
        // triangles_2d[triangle_count].p2.z = z;

        // z = scene_z + triangles[i].p3.z;
        // if( z < g_depth_min )
        //     g_depth_min = z;
        // if( z > g_depth_max )
        //     g_depth_max = z;

        // triangles_2d[triangle_count].p3.x = (triangles[i].p3.x << 9) / (z);
        // triangles_2d[triangle_count].p3.y = (triangles[i].p3.y << 9) / (z);
        // triangles_2d[triangle_count].p3.z = z;

        triangles_2d[triangle_count] = project(
            vertex_x[face_a[i]],
            vertex_x[face_b[i]],
            vertex_x[face_c[i]],
            vertex_y[face_a[i]],
            vertex_y[face_b[i]],
            vertex_y[face_c[i]],
            vertex_z[face_a[i]],
            vertex_z[face_b[i]],
            vertex_z[face_c[i]],
            model_yaw,
            model_pitch,
            model_roll,
            SCREEN_WIDTH / 2,
            SCREEN_HEIGHT / 2,
            420,
            SCREEN_WIDTH,
            SCREEN_HEIGHT);

        triangles_2d_map[i] = triangle_count;

        triangle_count += 1;
    }

    int model_min_depth = 96;

    // bucket sort the faces by depth
    // ex.
    //
    // depth = 0: 0, 1, 2, 3
    // depth = 1: 4, 5, 6
    // depth = 2: 7, 8, 9
    //
    // len = 0: 4
    // len = 1: 3
    // len = 2: 3
    //
    for( int f = 0; f < num_faces; f++ )
    {
        int a = face_a[f];
        int b = face_b[f];
        int c = face_c[f];

        // int xa = vertex_x[a];
        // int xb = vertex_x[b];
        // int xc = vertex_x[c];

        int xa = triangles_2d[f].p1.x;
        int xb = triangles_2d[f].p2.x;
        int xc = triangles_2d[f].p3.x;

        // int ya = vertex_y[a];
        // int yb = vertex_y[b];
        // int yc = vertex_y[c];

        int ya = triangles_2d[f].p1.y;
        int yb = triangles_2d[f].p2.y;
        int yc = triangles_2d[f].p3.y;

        // int za = vertex_z[a];
        // int zb = vertex_z[b];
        // int zc = vertex_z[c];

        int za = triangles_2d[f].p1.z;
        int zb = triangles_2d[f].p2.z;
        int zc = triangles_2d[f].p3.z;

        if( (xa - xb) * (yc - yb) - (ya - yb) * (xc - xb) > 0 )
        {
            int depth_average = (za + zb + zc) / 3 + model_min_depth;
            if( depth_average < 1500 )
            {
                tmp_depth_faces[depth_average][tmp_depth_face_count[depth_average]++] = f;
            }
        }
    }

    //
    // partition the sorted faces by priority.
    // So each partition are sorted by depth.
    for( int depth = max_model_depth; depth >= 0 && depth < 1500; depth-- )
    {
        const int face_count = tmp_depth_face_count[depth];
        if( face_count > 0 )
        {
            int* faces = tmp_depth_faces[depth];
            for( int i = 0; i < face_count; i++ )
            {
                int face_idx = faces[i];
                int prio = face_priority[face_idx];
                int priority_face_count = tmp_priority_face_count[prio]++;
                tmp_priority_faces[prio][priority_face_count] = face_idx;
                if( prio < 10 )
                {
                    tmp_priority_depth_sum[prio] += depth;
                }
                // else if( depth_average == 10 )
                // {
                //     _Model.tmp_priority10_face_depth[priority_face_count] = depth;
                // }
                // else
                // {
                //     _Model.tmp_priority11_face_depth[priority_face_count] = depth;
                // }
            }
        }
    }

    struct Pixel* pixel_buffer =
        (struct Pixel*)malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(struct Pixel));
    memset(pixel_buffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(struct Pixel));

    int* z_buffer = (int*)malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(int));
    memset(z_buffer, INT32_MAX, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(int));

    int init = SDL_INIT_VIDEO;

    int res = SDL_Init(init);
    if( res < 0 )
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Oldschool Runescape",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        SDL_WINDOW_ALLOW_HIGHDPI);

    if( NULL == window )
    {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        return -1;
    }

    SDL_Renderer* renderer =
        SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(renderer, SCREEN_WIDTH, SCREEN_HEIGHT);

    // Create texture
    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        // SDL_PIXELFORMAT_RGBA8888,
        SDL_PIXELFORMAT_XRGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        SCREEN_WIDTH,
        SCREEN_HEIGHT);
    SDL_Event event;
    // renderer
    int step = 20;
    // SDL_Renderer* renderer = SDL_GetRenderer(window);
    while( true )
    {
        for( int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++ )
            z_buffer[i] = INT32_MAX;

        // Clear the pixel buffer
        // memset(pixel_buffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(struct Pixel));

        // Get keyboard input
        SDL_PollEvent(&event);
        if( event.type == SDL_QUIT )
            break;

        model_yaw = (model_yaw + 1) % 2048;
        // Check if a key is pressed
        if( event.type == SDL_KEYDOWN )
        {
            printf("key: %d\n", event.key.keysym.sym);
            if( event.key.keysym.sym == SDLK_ESCAPE )
                break;
            if( event.key.keysym.sym == SDLK_UP )
                model_pitch += step;
            if( event.key.keysym.sym == SDLK_DOWN )
                model_pitch -= step;
            if( event.key.keysym.sym == SDLK_LEFT )
                model_yaw -= step;
            if( event.key.keysym.sym == SDLK_RIGHT )
                model_yaw += step;
            if( event.key.keysym.sym == SDLK_q )
                model_roll -= step;
            if( event.key.keysym.sym == SDLK_e )
                model_roll += step;
        }

        // for( int i = 0; i < num_faces; i++ )
        // {
        //     triangles_2d[i] = project(
        //         triangles[i].p1.x,
        //         triangles[i].p2.x,
        //         triangles[i].p3.x,
        //         triangles[i].p1.y,
        //         triangles[i].p2.y,
        //         triangles[i].p3.y,
        //         triangles[i].p1.z,
        //         triangles[i].p2.z,
        //         triangles[i].p3.z,
        //         model_yaw,
        //         model_pitch,
        //         model_roll,
        //         SCREEN_WIDTH / 2,
        //         SCREEN_HEIGHT / 2,
        //         420,
        //         SCREEN_WIDTH,
        //         SCREEN_HEIGHT);
        // }
        // SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
        SDL_RenderClear(renderer);

        // raster the triangles
        // triangle_count = triangle_count
        for( int prio = 0; prio < 12; prio++ )
        {
            int triangle_count = tmp_priority_face_count[prio];
            int* triangle_indexes = tmp_priority_faces[prio];

            int offset_x = SCREEN_WIDTH / 2;
            int offset_y = SCREEN_HEIGHT / 2;
            offset_x = 0;
            offset_y = 0;

            // offset_x = offset_x + (prio - 5) * 120;
            // offset_x += 240;
            for( int i = 0; i < triangle_count; i++ )
            {
                int index = triangle_indexes[i];
                struct Triangle2D triangle = triangles_2d[triangles_2d_map[index]];

                int color_a = colors_a[index];
                int color_b = colors_b[index];
                int color_c = colors_c[index];

                struct GouraudColors gouraud_colors;
                gouraud_colors.color1 = g_palette[color_a];
                gouraud_colors.color2 = g_palette[color_b];
                gouraud_colors.color3 = g_palette[color_c];

                raster_triangle(
                    pixel_buffer, z_buffer, gouraud_colors, triangle, SCREEN_WIDTH, SCREEN_HEIGHT);
            }
        }
        // for( int prio = 0; prio < 12; prio++ )
        // {
        //     // reset the z buffer for each priority.
        //     for( int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++ )
        //         z_buffer[i] = INT32_MAX;

        //     int* triangle_indexes = sorted_triangle_indexes[prio];
        //     int triangle_count = sorted_triangle_count[prio];

        //     for( int i = 0; i < triangle_count; i++ )
        //     {
        //         int index_original = triangle_indexes[i];
        //         int index = triangles_2d_map[index_original];

        //         struct Triangle2D triangle;
        //         triangle.p1.x = triangles_2d[index].p1.x + SCREEN_WIDTH / 2;
        //         triangle.p1.y = triangles_2d[index].p1.y + SCREEN_HEIGHT / 2;
        //         triangle.p1.z = triangles_2d[index].p1.z;

        //         triangle.p2.x = triangles_2d[index].p2.x + SCREEN_WIDTH / 2;
        //         triangle.p2.y = triangles_2d[index].p2.y + SCREEN_HEIGHT / 2;
        //         triangle.p2.z = triangles_2d[index].p2.z;

        //         triangle.p3.x = triangles_2d[index].p3.x + SCREEN_WIDTH / 2;
        //         triangle.p3.y = triangles_2d[index].p3.y + SCREEN_HEIGHT / 2;
        //         triangle.p3.z = triangles_2d[index].p3.z;

        //         int color_a = colors_a[index];
        //         int color_b = colors_b[index];
        //         int color_c = colors_c[index];

        //         struct GouraudColors gouraud_colors;
        //         gouraud_colors.color1 = g_palette[color_a];
        //         gouraud_colors.color2 = g_palette[color_b];
        //         gouraud_colors.color3 = g_palette[color_c];

        //         raster_triangle(
        //             pixel_buffer, z_buffer, gouraud_colors, triangle, SCREEN_WIDTH,
        //             SCREEN_HEIGHT);
        //     }
        // }

        // render pixel buffer to SDL_Surface
        SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(
            pixel_buffer,
            SCREEN_WIDTH,
            SCREEN_HEIGHT,
            32,
            SCREEN_WIDTH * sizeof(struct Pixel),
            0x00FF0000,
            0x0000FF00,
            0x000000FF,
            0xFF000000);

        // Copy the pixels into the texture
        int* pix_write = NULL;
        int _pitch_unused = 0;
        if( SDL_LockTexture(texture, NULL, (void**)&pix_write, &_pitch_unused) < 0 )
        {
            return 1;
        }
        int row_size = SCREEN_WIDTH * sizeof(int);
        int* src_pixels = (int*)surface->pixels;
        for( int src_y = 0; src_y < (SCREEN_HEIGHT); src_y++ )
        {
            // Calculate offset in texture to write a single row of pixels
            int* row = &pix_write[(src_y * SCREEN_WIDTH)];
            // Copy a single row of pixels
            memcpy(row, &src_pixels[(src_y - 0) * SCREEN_WIDTH], row_size);
        }

        // Unlock the texture so that it may be used elsewhere
        SDL_UnlockTexture(texture);
        SDL_RenderCopy(renderer, texture, NULL, NULL);

        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);

        //
        if( SDL_PollEvent(&event) )
        {
            if( SDL_QUIT == event.type )
            {
                break;
            }
        }
    }

    SDL_DestroyWindow(window);
    SDL_Quit();

    return EXIT_SUCCESS;
}