#include "gouraud.h"
#include "load_separate.h"
#include "projection.h"

#include <SDL.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720

int g_sin_table[2048];
int g_cos_table[2048];

int g_palette[65536];

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

void
raster_triangle(
    struct Pixel* pixel_buffer,
    int* z_buffer,
    struct GouraudColors colors,
    struct Triangle2D triangle,
    int screen_width,
    int screen_height)
{
    raster_gouraud_zbuf(
        (int*)pixel_buffer,
        z_buffer,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
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

static struct Triangle3D*
create_triangles_from_model(struct Model* model)
{
    struct Triangle3D* triangles =
        (struct Triangle3D*)malloc(model->face_count * sizeof(struct Triangle3D));

    for( int i = 0; i < model->face_count; i++ )
    {
        triangles[i].p1.x = model->vertices_x[model->face_indices_a[i]];
        triangles[i].p1.y = model->vertices_y[model->face_indices_a[i]];
        triangles[i].p1.z = model->vertices_z[model->face_indices_a[i]];

        triangles[i].p2.x = model->vertices_x[model->face_indices_b[i]];
        triangles[i].p2.y = model->vertices_y[model->face_indices_b[i]];
        triangles[i].p2.z = model->vertices_z[model->face_indices_b[i]];

        triangles[i].p3.x = model->vertices_x[model->face_indices_c[i]];
        triangles[i].p3.y = model->vertices_y[model->face_indices_c[i]];
        triangles[i].p3.z = model->vertices_z[model->face_indices_c[i]];
    }

    return triangles;
}

static void
project_vertices(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int num_vertices,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int model_yaw,
    int model_pitch,
    int model_roll,
    int camera_x,
    int camera_y,
    int camera_z,
    int camera_yaw,
    int camera_pitch,
    int camera_roll,
    int camera_fov,
    int screen_width,
    int screen_height)
{
    for( int i = 0; i < num_vertices; i++ )
    {
        struct ProjectedTriangle projected_triangle = project(
            vertex_x[i],
            vertex_y[i],
            vertex_z[i],
            model_yaw,
            model_pitch,
            model_roll,
            camera_x,
            camera_y,
            camera_z,
            camera_yaw,
            camera_pitch,
            camera_roll,
            camera_fov,
            SCREEN_WIDTH,
            SCREEN_HEIGHT);

        screen_vertices_x[i] = projected_triangle.x1;
        screen_vertices_y[i] = projected_triangle.y1;
        screen_vertices_z[i] = projected_triangle.depth1;
    }
}

static void
bucket_sort_by_average_depth(
    int face_depth_buckets[1500][512],
    int* face_depth_bucket_counts,
    int model_min_depth,
    int num_faces,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int* face_a,
    int* face_b,
    int* face_c)
{
    for( int f = 0; f < num_faces; f++ )
    {
        int a = face_a[f];
        int b = face_b[f];
        int c = face_c[f];

        int xa = vertex_x[a];
        int xb = vertex_x[b];
        int xc = vertex_x[c];

        int ya = vertex_y[a];
        int yb = vertex_y[b];
        int yc = vertex_y[c];

        int za = vertex_z[a];
        int zb = vertex_z[b];
        int zc = vertex_z[c];

        if( (xa - xb) * (yc - yb) - (ya - yb) * (xc - xb) > 0 )
        {
            int depth_average = (za + zb + zc) / 3 + model_min_depth;
            if( depth_average < 1500 && depth_average > 0 )
            {
                int bucket_index = face_depth_bucket_counts[depth_average]++;
                face_depth_buckets[depth_average][bucket_index] = f;
            }
        }
    }
}

static void
parition_faces_by_priority(
    int face_priority_buckets[12][2000],
    int* face_priority_bucket_counts,
    int face_depth_buckets[1500][512],
    int* face_depth_bucket_counts,
    int num_faces,
    int* face_priorities,
    int depth_lower_bound,
    int depth_upper_bound)
{
    for( int depth = depth_upper_bound; depth >= depth_lower_bound && depth < 1500; depth-- )
    {
        int face_count = face_depth_bucket_counts[depth];
        if( face_count == 0 )
            continue;

        int* faces = face_depth_buckets[depth];
        for( int i = 0; i < face_count; i++ )
        {
            int face_idx = faces[i];
            int prio = face_priorities[face_idx];
            int priority_face_count = face_priority_bucket_counts[prio]++;
            face_priority_buckets[prio][priority_face_count] = face_idx;
            // if( prio < 10 )
            // {
            //     tmp_priority_depth_sum[prio] += depth;
            // }
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

int
main(int argc, char* argv[])
{
    init_cos_table();
    init_sin_table();
    init_palette();

    // Camera variables
    int camera_x = 0;
    int camera_y = 0;
    int camera_z = 420; // Start at a reasonable distance
    int camera_yaw = 0;
    int camera_pitch = 0;
    int camera_roll = 0;
    int camera_fov = 512; // Default FOV (approximately 90 degrees)

    struct Model model = load_separate("../model");

    int max_model_depth = 1499;

    struct Triangle3D* triangles = create_triangles_from_model(&model);

    struct Triangle2D* triangles_2d =
        (struct Triangle2D*)malloc(model.face_count * sizeof(struct Triangle2D));

    int* screen_vertices_x = (int*)malloc(model.vertex_count * sizeof(int));
    int* screen_vertices_y = (int*)malloc(model.vertex_count * sizeof(int));
    int* screen_vertices_z = (int*)malloc(model.vertex_count * sizeof(int));

    int model_pitch = 0;
    int model_yaw = 0;
    int model_roll = 0;

    project_vertices(
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        model.vertex_count,
        model.vertices_x,
        model.vertices_y,
        model.vertices_z,
        model_yaw,
        model_pitch,
        model_roll,
        camera_x,
        camera_y,
        camera_z,
        camera_yaw,
        camera_pitch,
        camera_roll,
        camera_fov,
        SCREEN_WIDTH,
        SCREEN_HEIGHT);

    int model_min_depth = 96;

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
    int step = 200;
    // SDL_Renderer* renderer = SDL_GetRenderer(window);

    while( true )
    {
        memset(tmp_depth_face_count, 0, sizeof(tmp_depth_face_count));
        for( int i = 0; i < 1500; i++ )
            memset(tmp_depth_faces[i], 0, sizeof(tmp_depth_faces[i]));

        memset(tmp_priority_face_count, 0, sizeof(tmp_priority_face_count));

        for( int i = 0; i < 12; i++ )
            memset(tmp_priority_faces[i], 0, sizeof(tmp_priority_faces[i]));

        for( int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++ )
            z_buffer[i] = INT32_MAX;

        // Clear the pixel buffer
        memset(pixel_buffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(struct Pixel));

        // Get keyboard input
        bool keydown = false;
        while( SDL_PollEvent(&event) )
        {
            switch( event.type )
            {
            case SDL_QUIT:
                goto done;
            case SDL_KEYDOWN:
            {
                keydown = true;
                switch( event.key.keysym.sym )
                {
                case SDLK_ESCAPE:
                    goto done;
                case SDLK_UP:
                    camera_pitch = (camera_pitch + step) % 2048;
                    break;
                case SDLK_DOWN:
                    camera_pitch = (camera_pitch - step + 2048) % 2048;
                    break;
                case SDLK_LEFT:
                    camera_yaw = (camera_yaw - step + 2048) % 2048;
                    break;
                case SDLK_RIGHT:
                    camera_yaw = (camera_yaw + step) % 2048;
                    break;
                case SDLK_GREATER:
                    break;
                case SDLK_LESS:
                    camera_z += 10;
                    break;
                case SDLK_w:
                    camera_z -= 10;
                    break;
                case SDLK_s:
                    camera_z += 10;
                    break;
                case SDLK_a:
                    camera_x -= 10;
                    break;
                case SDLK_d:
                    camera_x += 10;
                    break;
                case SDLK_q:
                    camera_roll = (camera_yaw - step + 2048) % 2048;
                    break;
                case SDLK_e:
                    camera_roll = (camera_yaw + step) % 2048;
                    break;
                case SDLK_i:
                    model_pitch = (model_pitch - step + 2048) % 2048;
                    break;
                case SDLK_k:
                    model_pitch = (model_pitch + step) % 2048;
                    break;
                case SDLK_j:
                    model_yaw = (model_yaw - step + 2048) % 2048;
                    break;
                case SDLK_l:
                    model_yaw = (model_yaw + step) % 2048;
                    break;
                case SDLK_u:
                    model_roll = (model_roll - step + 2048) % 2048;
                    break;
                case SDLK_o:
                    model_roll = (model_roll + step) % 2048;
                    break;
                case SDLK_f:
                    camera_fov = (camera_fov - 50 + 2048) % 2048; // Decrease FOV (zoom in)
                    break;
                case SDLK_g:
                    camera_fov = (camera_fov + 50) % 2048; // Increase FOV (zoom out)
                    break;
                }
            }
            }
        }

        if( camera_fov < 171 ) // 20 degrees in units of 2048ths of 2π (20/360 * 2048)
            camera_fov = 171;
        if( camera_fov > 1280 ) // 150 degrees in units of 2048ths of 2π (150/360 * 2048)
            camera_fov = 1280;

        project_vertices(
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            model.vertex_count,
            model.vertices_x,
            model.vertices_y,
            model.vertices_z,
            model_yaw,
            model_pitch,
            model_roll,
            camera_x,
            camera_y,
            camera_z,
            camera_yaw,
            camera_pitch,
            camera_roll,
            camera_fov,
            SCREEN_WIDTH,
            SCREEN_HEIGHT);

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
        bucket_sort_by_average_depth(
            tmp_depth_faces,
            tmp_depth_face_count,
            model_min_depth,
            model.face_count,
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            model.face_indices_a,
            model.face_indices_b,
            model.face_indices_c);

        //
        // partition the sorted faces by priority.
        // So each partition are sorted by depth.
        parition_faces_by_priority(
            tmp_priority_faces,
            tmp_priority_face_count,
            tmp_depth_faces,
            tmp_depth_face_count,
            model.face_count,
            model.face_priorities,
            0,
            1499);

        // SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
        SDL_RenderClear(renderer);

        // raster the triangles
        // triangle_count = triangle_count

        for( int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++ )
            z_buffer[i] = INT32_MAX;
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
                struct Triangle2D triangle = { 0 };
                triangle.p1.x = screen_vertices_x[model.face_indices_a[index]] + offset_x;
                triangle.p1.y = screen_vertices_y[model.face_indices_a[index]] + offset_y;
                triangle.p1.z = screen_vertices_z[model.face_indices_a[index]];

                triangle.p2.x = screen_vertices_x[model.face_indices_b[index]] + offset_x;
                triangle.p2.y = screen_vertices_y[model.face_indices_b[index]] + offset_y;
                triangle.p2.z = screen_vertices_z[model.face_indices_b[index]];

                triangle.p3.x = screen_vertices_x[model.face_indices_c[index]] + offset_x;
                triangle.p3.y = screen_vertices_y[model.face_indices_c[index]] + offset_y;
                triangle.p3.z = screen_vertices_z[model.face_indices_c[index]];

                int color_a = model.face_color_a[index];
                int color_b = model.face_color_b[index];
                int color_c = model.face_color_c[index];

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
    }

done:

    SDL_DestroyWindow(window);
    SDL_Quit();

    return EXIT_SUCCESS;
}