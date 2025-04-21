#include <SDL.h>
#include <stdbool.h>
#include <stdio.h>

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720

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
draw_scanline(struct Pixel* pixel_buffer, int x1, int x2, int y, int screen_width)
{
    // Draw a horizontal line from (x1, y) to (x2, y)
    for( int x = x1; x <= x2; x++ )
    {
        if( x >= 0 && x < screen_width && y >= 0 && y < SCREEN_HEIGHT )
        {
            struct Pixel* pixel = &pixel_buffer[y * screen_width + x];
            pixel->r = 255;
            pixel->g = 0;
            pixel->b = 0;
            pixel->a = 255;
        }
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
    struct Pixel* pixel_buffer, struct Point2D v0, struct Point2D v1, struct Point2D v2)
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

        draw_scanline(pixel_buffer, xStart, xEnd, y, SCREEN_WIDTH);
    }
}

void
fill_flat_top_triangle(
    struct Pixel* pixel_buffer, struct Point2D v0, struct Point2D v1, struct Point2D v2)
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

        draw_scanline(pixel_buffer, xStart, xEnd, y, SCREEN_WIDTH);
    }
}

void
fill_triangle(struct Pixel* pixel_buffer, struct Point2D p0, struct Point2D p1, struct Point2D p2)
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
        fill_flat_top_triangle(pixel_buffer, p0, p1, p2);
    else if( p0.y == p1.y )
        fill_flat_bottom_triangle(pixel_buffer, p0, p1, p2);
    else
    {
        // General triangle: split into flat-top and flat-bottom
        // Compute new vertex at p1.y along edge p0-p2
        int x4 = edge_interpolate(p0.x, p0.y, p2.x, p2.y, p1.y);
        struct Point2D p4 = { x4, p1.y };

        fill_flat_bottom_triangle(pixel_buffer, p0, p1, p4);
        fill_flat_top_triangle(pixel_buffer, p1, p4, p2);
    }
}

void
raster_triangle(
    struct Pixel* pixel_buffer, struct Triangle2D triangle, int screen_width, int screen_height)
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
    {
        min_x = 0;
    }
    if( min_y < 0 )
    {
        min_y = 0;
    }
    if( max_x >= screen_width )
    {
        max_x = screen_width - 1;
    }
    if( max_y >= screen_height )
    {
        max_y = screen_height - 1;
    }
    // Loop through each pixel in the bounding box and only do math with ints

    fill_triangle(pixel_buffer, triangle.p1, triangle.p2, triangle.p3);

    for( int y = min_y; y <= max_y; y++ )
    {
        // int xStart = edge_interpolate(v0.x, v0.y, v1.x, v1.y, y);
        // int xEnd = edge_interpolate(v0.x, v0.y, v2.x, v2.y, y);
        // drawScanline(xStart, xEnd, y);

        // for( int x = min_x; x <= max_x; x++ )
        // {
        //     // Check if the pixel is inside the triangle using barycentric coordinates
        //     float alpha =
        //         ((float)(triangle.p2.y - triangle.p3.y) * (x - triangle.p3.x) +
        //          (float)(triangle.p3.x - triangle.p2.x) * (y - triangle.p3.y)) /
        //         ((float)(triangle.p2.y - triangle.p3.y) * (triangle.p1.x - triangle.p3.x) +
        //          (float)(triangle.p3.x - triangle.p2.x) * (triangle.p1.y - triangle.p3.y));
        //     float beta = ((float)(triangle.p3.y - triangle.p1.y) * (x - triangle.p3.x) +
        //                   (float)(triangle.p1.x - triangle.p3.x) * (y - triangle.p3.y)) /
        //                  ((float)(triangle.p2.y - triangle.p3.y) * (triangle.p1.x -
        //                  triangle.p3.x) +
        //                   (float)(triangle.p3.x - triangle.p2.x) * (triangle.p1.y -
        //                   triangle.p3.y));
        //     float gamma = 1.0f - alpha - beta;

        //     if( alpha >= 0 && beta >= 0 && gamma >= 0 )
        //     {
        //         // Set the pixel color
        //         struct Pixel* pixel = &pixel_buffer[y * screen_width + x];
        //         pixel->r = 255;
        //         pixel->g = 0;
        //         pixel->b = 0;
        //         pixel->a = 255;
        //     }
        // }
    }
}

int
main(int argc, char* argv[])
{
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
        SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
        SDL_RenderClear(renderer);

        // Draw something
        // SDL_Rect rect = { 200, 150, 100, 100 };
        // SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
        // SDL_RenderFillRect(renderer, &rect);

        // SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

        // Draw a triangle on the SDL Window
        struct Triangle2D triangle;
        triangle.p1.x = SCREEN_WIDTH / 2;
        triangle.p1.y = SCREEN_HEIGHT / 2;
        triangle.p2.x = SCREEN_WIDTH / 2 + 200;
        triangle.p2.y = SCREEN_HEIGHT / 2 + 200;
        triangle.p3.x = SCREEN_WIDTH / 2 - 100;
        triangle.p3.y = SCREEN_HEIGHT / 2 + 100;
        // SDL_RenderDrawLine(renderer, triangle.p1.x, triangle.p1.y, triangle.p2.x, triangle.p2.y);
        // SDL_RenderDrawLine(renderer, triangle.p2.x, triangle.p2.y, triangle.p3.x, triangle.p3.y);
        // SDL_RenderDrawLine(renderer, triangle.p3.x, triangle.p3.y, triangle.p1.x, triangle.p1.y);
        // SDL_RenderPresent(renderer);

        raster_triangle(pixel_buffer, triangle, SCREEN_WIDTH, SCREEN_HEIGHT);

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

        // Now show it.

        // Copy the pixels into the texture
        int* pix_write = NULL;
        int _pitch_unused = 0;
        if( SDL_LockTexture(texture, NULL, (void**)&pix_write, &_pitch_unused) < 0 )
        {
            return;
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