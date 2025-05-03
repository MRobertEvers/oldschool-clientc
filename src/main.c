#include "gouraud.c"

#include <SDL.h>
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
    pix3d_set_brightness(g_palette, 1.0);
}

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

/**
 * @param x count of x * (2 * PI / 2048) radians.
 * @return the (sine of x) * (1 << 16)
 */
int
sin_count(int x)
{
    return g_sin_table[x];
}

/**
 * @param x count of x * (2 * PI / 2048) radians.
 * @return the (cosine of x) * (1 << 16)
 */
int
cos_count(int x)
{
    return g_cos_table[x];
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
    struct Point2D p1;
    struct Point2D p2;
    struct Point2D p3;
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

int
edge_interpolate(int x0, int y0, int x1, int y1, int y)
{
    // Integer linear interpolation: avoids floats
    if( y1 == y0 )
        return x0;
    return x0 + (x1 - x0) * (y - y0) / (y1 - y0);
}

void
draw_scanline(struct Pixel* pixel_buffer, int color, int x1, int x2, int y, int screen_width)
{
    // Draw a horizontal line from (x1, y) to (x2, y)
    // If it is the first or pixel, make it black

    for( int x = x1; x <= x2; x++ )
    {
        struct Pixel* pixel = &pixel_buffer[y * screen_width + x];

        if( x >= 0 && x < screen_width && y >= 0 && y < SCREEN_HEIGHT )
        {
            pixel->r = color & 0xFF;
            pixel->g = (color >> 8) & 0xFF;
            pixel->b = (color >> 16) & 0xFF;
            pixel->a = ((color >> 24) & 0xFF) / 2;
        }

        // if it's the last pixel, make it black
        if( x == x1 || x == x2 )
        {
            pixel->r = color & 0xFF;
            pixel->g = (color >> 8) & 0xFF;
            pixel->b = (color >> 16) & 0xFF;
            pixel->a = (color >> 24) & 0xFF;
        }
    }
}

static inline int
interpolate(int a, int b, float t)
{
    return a + (int)((b - a) * t);
}

static void
draw_scanline_gouraud(
    int* pixel_buffer, int y, int x_start, int x_end, int color_start, int color_end)
{
    if( x_start > x_end )
    {
        int tmp;
        tmp = x_start;
        x_start = x_end;
        x_end = tmp;
        tmp = color_start;
        color_start = color_end;
        color_end = tmp;
    }

    int dx = x_end - x_start;
    for( int x = x_start; x <= x_end; ++x )
    {
        float t = dx == 0 ? 0.0f : (float)(x - x_start) / dx;

        int r = interpolate((color_start >> 16) & 0xFF, (color_end >> 16) & 0xFF, t);
        int g = interpolate((color_start >> 8) & 0xFF, (color_end >> 8) & 0xFF, t);
        int b = interpolate(color_start & 0xFF, color_end & 0xFF, t);
        int a = 255; // Alpha value

        int color = (a << 24) | (r << 16) | (g << 8) | b;

        if( x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT )
            pixel_buffer[y * SCREEN_WIDTH + x] = color;
    }
}

void
raster_gouraud(
    int* pixel_buffer,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int color0,
    int color1,
    int color2)
{
    // Sort vertices by y
    if( y0 > y1 )
    {
        int t;
        t = y0;
        y0 = y1;
        y1 = t;
        t = x0;
        x0 = x1;
        x1 = t;
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

        int acolor = interpolate(color0, color2, alpha);
        int bcolor =
            second_half ? interpolate(color1, color2, beta) : interpolate(color0, color1, beta);

        int y = y0 + i;
        draw_scanline_gouraud(pixel_buffer, y, ax, bx, acolor, bcolor);
    }
}

void
swap(struct Point2D* a, struct Point2D* b)
{
    struct Point2D temp = *a;
    *a = *b;
    *b = temp;
}

void
fill_flat_bottom_triangle(
    struct Pixel* pixel_buffer, int color, struct Point2D v0, struct Point2D v1, struct Point2D v2)
{
    // v0.y == v1.y < v2.y
    int yStart = v0.y;
    int yEnd = v2.y;

    for( int y = yStart; y <= yEnd; y++ )
    {
        int xEnd = edge_interpolate(v0.x, v0.y, v2.x, v2.y, y);
        int xStart = edge_interpolate(v1.x, v1.y, v0.x, v0.y, y);
        // Swap endpoints if necessary
        if( xStart > xEnd )
        {
            int temp = xStart;
            xStart = xEnd;
            xEnd = temp;
        }

        draw_scanline(pixel_buffer, color, xStart, xEnd, y, SCREEN_WIDTH);
    }
}

void
fill_flat_top_triangle(
    struct Pixel* pixel_buffer, int color, struct Point2D v0, struct Point2D v1, struct Point2D v2)
{
    // v0.y < v1.y == v2.y
    int yStart = v0.y;
    int yEnd = v2.y;

    for( int y = yStart; y <= yEnd; y++ )
    {
        int xEnd = edge_interpolate(v1.x, v1.y, v2.x, v2.y, y);
        int xStart = edge_interpolate(v0.x, v0.y, v2.x, v2.y, y);
        // Swap endpoints if necessary
        if( xStart > xEnd )
        {
            int temp = xStart;
            xStart = xEnd;
            xEnd = temp;
        }

        draw_scanline(pixel_buffer, color, xStart, xEnd, y, SCREEN_WIDTH);
    }
}

void
fill_triangle(
    struct Pixel* pixel_buffer, int color, struct Point2D p0, struct Point2D p1, struct Point2D p2)
{
    // Sort by y
    if( p0.y > p1.y )
        swap(&p0, &p1);
    if( p1.y > p2.y )
        swap(&p1, &p2);
    if( p0.y > p1.y )
        swap(&p0, &p1);

    // Handle flat-top or flat-bottom triangle
    if( p1.y == p2.y )
        fill_flat_top_triangle(pixel_buffer, color, p0, p1, p2);
    else if( p0.y == p1.y )
        fill_flat_bottom_triangle(pixel_buffer, color, p0, p1, p2);
    else
    {
        // General triangle: split into flat-top and flat-bottom
        // Compute new vertex at p1.y along edge p0-p2
        int x4 = edge_interpolate(p0.x, p0.y, p2.x, p2.y, p1.y);
        struct Point2D p4 = { x4, p1.y };

        fill_flat_bottom_triangle(pixel_buffer, color, p0, p1, p4);
        fill_flat_top_triangle(pixel_buffer, color, p1, p4, p2);
    }
}

void
fill_triangle_gouraud(
    struct Pixel* pixel_buffer,
    struct GouraudColors colors,
    struct Point2D p0,
    struct Point2D p1,
    struct Point2D p2)
{
    raster_gouraud(
        (int*)pixel_buffer,
        p0.x,
        p1.x,
        p2.x,
        p0.y,
        p1.y,
        p2.y,
        colors.color1,
        colors.color2,
        colors.color3);
}

void
raster_triangle(
    struct Pixel* pixel_buffer,
    struct GouraudColors colors,
    struct Triangle2D triangle,
    int screen_width,
    int screen_height)
{
    // Rasterization logic goes here
    // For now, just set the pixels to a color

    // Set the pixels of the triangle to a different color
    // Fill in the pixels of the triangle line by line
    // ScanLine fill algo

    // Get the bounding box of the triangle
    int min_x = triangle.p1.x < triangle.p2.x ? triangle.p1.x : triangle.p2.x;
    min_x = min_x < triangle.p3.x ? min_x : triangle.p3.x;
    int max_x = triangle.p1.x > triangle.p2.x ? triangle.p1.x : triangle.p2.x;
    max_x = max_x > triangle.p3.x ? max_x : triangle.p3.x;

    int min_y = triangle.p1.y < triangle.p2.y ? triangle.p1.y : triangle.p2.y;
    min_y = min_y < triangle.p3.y ? min_y : triangle.p3.y;
    int max_y = triangle.p1.y > triangle.p2.y ? triangle.p1.y : triangle.p2.y;
    max_y = max_y > triangle.p3.y ? max_y : triangle.p3.y;

    // Clamp the bounding box to the screen dimensions
    if( min_x < 0 )
        min_x = 0;
    if( min_y < 0 )
        min_y = 0;
    if( max_x >= screen_width )
        max_x = screen_width - 1;
    if( max_y >= screen_height )
        max_y = screen_height - 1;
    // Loop through each pixel in the bounding box and only do math with ints

    // fill_triangle(pixel_buffer, color, triangle.p1, triangle.p2, triangle.p3);
    fill_triangle_gouraud(pixel_buffer, colors, triangle.p1, triangle.p2, triangle.p3);
}

int
main(int argc, char* argv[])
{
    init_sin_table();
    init_cos_table();
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
    free(vertex_x);
    free(vertex_y);
    free(vertex_z);
    free(face_a);
    free(face_b);
    free(face_c);

    // project the triangle to the screen 2d

    struct Triangle2D* triangles_2d =
        (struct Triangle2D*)malloc(num_faces * sizeof(struct Triangle2D));
    for( int i = 0; i < num_faces; i++ )
    {
        triangles_2d[i].p1.x = (triangles[i].p1.x * 100) / (triangles[i].p1.z + 100);
        triangles_2d[i].p1.y = (triangles[i].p1.y * 100) / (triangles[i].p1.z + 100);

        triangles_2d[i].p2.x = (triangles[i].p2.x * 100) / (triangles[i].p2.z + 100);
        triangles_2d[i].p2.y = (triangles[i].p2.y * 100) / (triangles[i].p2.z + 100);

        triangles_2d[i].p3.x = (triangles[i].p3.x * 100) / (triangles[i].p3.z + 100);
        triangles_2d[i].p3.y = (triangles[i].p3.y * 100) / (triangles[i].p3.z + 100);
    }
    free(triangles);

    struct Pixel* pixel_buffer =
        (struct Pixel*)malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(struct Pixel));
    memset(pixel_buffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(struct Pixel));
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
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,
        SCREEN_WIDTH,
        SCREEN_HEIGHT);
    SDL_Event event;
    // renderer

    // SDL_Renderer* renderer = SDL_GetRenderer(window);
    while( true )
    {
        // SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
        SDL_RenderClear(renderer);

        // raster the triangles
        for( int i = 0; i < num_faces; i++ )
        {
            struct Triangle2D triangle;
            triangle.p1.x = triangles_2d[i].p1.x + SCREEN_WIDTH / 2;
            triangle.p1.y = triangles_2d[i].p1.y + SCREEN_HEIGHT / 2;
            triangle.p2.x = triangles_2d[i].p2.x + SCREEN_WIDTH / 2;
            triangle.p2.y = triangles_2d[i].p2.y + SCREEN_HEIGHT / 2;
            triangle.p3.x = triangles_2d[i].p3.x + SCREEN_WIDTH / 2;
            triangle.p3.y = triangles_2d[i].p3.y + SCREEN_HEIGHT / 2;

            int color_a = colors_a[i];
            int color_b = colors_b[i];
            int color_c = colors_c[i];
            printf("color_a: %d, color_b: %d, color_c: %d\n", color_a, color_b, color_c);

            // Convert the palette color to RGB
            int color = g_palette[color_b];

            struct GouraudColors gouraud_colors;
            gouraud_colors.color1 = g_palette[color_a];
            gouraud_colors.color2 = g_palette[color_b];
            gouraud_colors.color3 = g_palette[color_c];

            raster_triangle(pixel_buffer, gouraud_colors, triangle, SCREEN_WIDTH, SCREEN_HEIGHT);
        }

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