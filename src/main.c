#include "buffer.h"
#include "gouraud.h"
#include "lighting.h"
#include "osrs/anim.h"
#include "osrs/tables/framemap.h"
#include "osrs/tables/model.h"
#include "osrs_cache.h"
#include "projection.h"
#include "texture.h"
#include <sys/stat.h>

#include <SDL.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720

// Add near plane clipping constant
#define NEAR_PLANE_Z 10 // Minimum z distance before clipping

int g_sin_table[2048];
int g_cos_table[2048];
int g_tan_table[2048];

//   This tool renders a color palette using jagex's 16-bit HSL, 6 bits
//             for hue, 3 for saturation and 7 for lightness, bitpacked and
//             represented as a short.
int g_hsl16_to_rgb_table[65536];

static int tmp_depth_face_count[1500] = { 0 };
static int tmp_depth_faces[1500 * 512] = { 0 };
static int tmp_priority_face_count[12] = { 0 };
static int tmp_priority_depth_sum[12] = { 0 };
static int tmp_priority_faces[12 * 2000] = { 0 };

// Add file change detection variables
static time_t last_vertices_mtime = 0;
static time_t last_faces_mtime = 0;
static time_t last_colors_mtime = 0;
static time_t last_priorities_mtime = 0;

// Function to check if any model file has changed
static bool
check_model_files_changed(const char* model_path)
{
    struct stat st;
    char filename[256];
    bool changed = false;

    // Check vertices file
    snprintf(filename, sizeof(filename), "%s.vertices", model_path);
    if( stat(filename, &st) == 0 )
    {
        if( st.st_mtime > last_vertices_mtime )
        {
            last_vertices_mtime = st.st_mtime;
            changed = true;
        }
    }

    // Check faces file
    snprintf(filename, sizeof(filename), "%s.faces", model_path);
    if( stat(filename, &st) == 0 )
    {
        if( st.st_mtime > last_faces_mtime )
        {
            last_faces_mtime = st.st_mtime;
            changed = true;
        }
    }

    // Check colors file
    snprintf(filename, sizeof(filename), "%s.colors", model_path);
    if( stat(filename, &st) == 0 )
    {
        if( st.st_mtime > last_colors_mtime )
        {
            last_colors_mtime = st.st_mtime;
            changed = true;
        }
    }

    // Check priorities file
    snprintf(filename, sizeof(filename), "%s.priorities", model_path);
    if( stat(filename, &st) == 0 )
    {
        if( st.st_mtime > last_priorities_mtime )
        {
            last_priorities_mtime = st.st_mtime;
            changed = true;
        }
    }

    return changed;
}

// Function to initialize file modification times
static void
init_file_mtimes(const char* model_path)
{
    struct stat st;
    char filename[256];

    snprintf(filename, sizeof(filename), "%s.vertices", model_path);
    if( stat(filename, &st) == 0 )
    {
        last_vertices_mtime = st.st_mtime;
    }

    snprintf(filename, sizeof(filename), "%s.faces", model_path);
    if( stat(filename, &st) == 0 )
    {
        last_faces_mtime = st.st_mtime;
    }

    snprintf(filename, sizeof(filename), "%s.colors", model_path);
    if( stat(filename, &st) == 0 )
    {
        last_colors_mtime = st.st_mtime;
    }

    snprintf(filename, sizeof(filename), "%s.priorities", model_path);
    if( stat(filename, &st) == 0 )
    {
        last_priorities_mtime = st.st_mtime;
    }
}

// Helper function to create a simple test texture
static void
create_test_texture(int* texels, int width, int height)
{
    for( int y = 0; y < height; y++ )
    {
        for( int x = 0; x < width; x++ )
        {
            // Create a simple checkerboard pattern
            int color = ((x / 8) + (y / 8)) % 2 ? 0xFFFFFF : 0x000000;
            texels[y * width + x] = color;
        }
    }
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

void
init_tan_table()
{
    for( int i = 0; i < 2048; i++ )
        g_tan_table[i] = (int)(tan((double)i * 0.0030679615) * (1 << 16));
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
init_hsl16_to_rgb_table()
{
    pix3d_set_brightness(g_hsl16_to_rgb_table, 0.8);
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

    int color1;
    int color2;
    int color3;
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
raster_triangle_zbuf(
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

void
raster_triangle(
    struct Pixel* pixel_buffer,
    struct GouraudColors colors,
    struct Triangle2D triangle,
    int screen_width,
    int screen_height)
{
    raster_gouraud(
        (int*)pixel_buffer,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        triangle.p1.x,
        triangle.p2.x,
        triangle.p3.x,
        triangle.p1.y,
        triangle.p2.y,
        triangle.p3.y,
        colors.color1,
        colors.color2,
        colors.color3,
        0xFF);
}

static struct Triangle3D*
create_triangles_from_model(struct CacheModel* model)
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

/**
 * A top and bottom cylinder that bounds the model.
 */
struct BoundingCylinder
{
    int center_to_top_edge;
    int center_to_bottom_edge;
    int radius;

    // TODO: Name?
    // - Max extent from model origin.
    // - Distance to farthest vertex?
    int min_z_depth_any_rotation;
};

static struct BoundingCylinder
calculate_bounding_cylinder(int num_vertices, int* vertex_x, int* vertex_y, int* vertex_z)
{
    struct BoundingCylinder bounding_cylinder = { 0 };

    int min_y = INT_MAX;
    int max_y = INT_MIN;
    int radius_squared = 0;

    for( int i = 0; i < num_vertices; i++ )
    {
        int x = vertex_x[i];
        int y = vertex_y[i];
        int z = vertex_z[i];
        if( y < min_y )
            min_y = y;
        if( y > max_y )
            max_y = y;
        int radius_squared_vertex = x * x + z * z;
        if( radius_squared_vertex > radius_squared )
            radius_squared = radius_squared_vertex;
    }

    // Reminder, +y is down on the screen.
    bounding_cylinder.center_to_bottom_edge = (int)sqrt(radius_squared + min_y * min_y) + 1;
    bounding_cylinder.center_to_top_edge = (int)sqrt(radius_squared + max_y * max_y) + 1;

    // Use max of the two here because OSRS assumes the camera is always above the model,
    // which may not be the case for us.
    bounding_cylinder.min_z_depth_any_rotation =
        bounding_cylinder.center_to_top_edge > bounding_cylinder.center_to_bottom_edge
            ? bounding_cylinder.center_to_top_edge
            : bounding_cylinder.center_to_bottom_edge;

    return bounding_cylinder;
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
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_yaw,
    int camera_pitch,
    int camera_roll,
    int camera_fov,
    int screen_width,
    int screen_height)
{
    struct ProjectedTriangle projected_triangle;
    int cos_camera_pitch = g_cos_table[camera_pitch];
    int sin_camera_pitch = g_sin_table[camera_pitch];
    int cos_camera_yaw = g_cos_table[camera_yaw];
    int sin_camera_yaw = g_sin_table[camera_yaw];
    int cos_camera_roll = g_cos_table[camera_roll];
    int sin_camera_roll = g_sin_table[camera_roll];

    projected_triangle = project(
        0,
        0,
        0,
        model_pitch,
        model_yaw,
        model_roll,
        scene_x,
        scene_y,
        scene_z,
        camera_pitch,
        camera_yaw,
        camera_roll,
        camera_fov,
        NEAR_PLANE_Z,
        screen_width,
        screen_height);

    if( projected_triangle.clipped )
    {
        for( int i = 0; i < num_vertices; i++ )
        {
            screen_vertices_x[i] = -5000;
            screen_vertices_y[i] = -5000;
            screen_vertices_z[i] = -5000;

            return;
        }
    }

    // int a = (scene_z * cos_camera_yaw - scene_x * sin_camera_yaw) >> 16;
    // // b is the z projection of the models origin (imagine a vertex at x=0,y=0 and z=0).
    // // So the depth is the z projection distance from the origin of the model.
    // int b = (scene_y * sin_camera_pitch + a * cos_camera_pitch) >> 16;
    int model_origin_z_projection = projected_triangle.z;

    for( int i = 0; i < num_vertices; i++ )
    {
        projected_triangle = project(
            vertex_x[i],
            vertex_y[i],
            vertex_z[i],
            model_pitch,
            model_yaw,
            model_roll,
            scene_x,
            scene_y,
            scene_z,
            camera_pitch,
            camera_yaw,
            camera_roll,
            camera_fov,
            NEAR_PLANE_Z,
            screen_width,
            screen_height);

        // If vertex is too close to camera, set it to a large negative value
        // This will cause it to be clipped in the rasterization step
        if( projected_triangle.clipped )
        {
            screen_vertices_x[i] = -5000;
            screen_vertices_y[i] = -5000;
            screen_vertices_z[i] = -5000;
        }
        else
        {
            screen_vertices_x[i] = projected_triangle.x;
            screen_vertices_y[i] = projected_triangle.y;
            screen_vertices_z[i] = projected_triangle.z - model_origin_z_projection;
        }
    }
}

static void
bucket_sort_by_average_depth(
    int* face_depth_buckets,
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

        // dot product of the vectors (AB, BC)
        // If the dot product is 0, then AB and BC are on the same line.
        // if the dot product is negative, then the triangle is facing away from the camera.
        // Note: This assumes that face vertices are ordered in some way. TODO: Determine that
        // order.
        if( (xa - xb) * (yc - yb) - (ya - yb) * (xc - xb) > 0 )
        {
            // the z's are the depth of the vertex relative to the origin of the model.
            // This means some of the z's will be negative.
            // model_min_depth is calculate from as the radius sphere around the origin of the
            // model, that encloses all vertices on the model. This adjusts all the z's to be
            // positive, but maintain relative order.
            //
            //
            // Note: In osrs, the min_depth is actually calculated from a cylinder that encloses
            // the model
            //
            //                   / |
            //  min_depth ->   /    |
            //               /      |
            //              /       |
            //              --------
            //              ^ model xz radius
            //    Note: There is a lower cylinder as well, but it is not used in depth sorting.
            // The reason is uses the models "upper ys" (max_y) is because OSRS assumes the
            // camera will always be above the model, so the closest vertices to the camera will
            // be towards the top of the model. (i.e. lowest z values) Relative to the model's
            // origin, there may be negative z values, but always |z| < min_depth so the
            // min_depth is used to adjust all z values to be positive, but maintain relative
            // order.
            int depth_average = (za + zb + zc) / 3 + model_min_depth;

            if( depth_average < 1500 && depth_average > 0 )
            {
                int bucket_index = face_depth_bucket_counts[depth_average]++;
                face_depth_buckets[depth_average * 512 + bucket_index] = f;
            }
            else
            {
                // printf(
                //     "Out of bounds %d, (%d, %d, %d) - %d\n",
                //     depth_average,
                //     za,
                //     zb,
                //     zc,
                //     model_min_depth);
            }
        }
    }
}

static void
parition_faces_by_priority(
    int* face_priority_buckets,
    int* face_priority_bucket_counts,
    int* face_depth_buckets,
    int* face_depth_bucket_counts,
    int num_faces,
    int* face_priorities,
    int depth_upper_bound)
{
    for( int depth = depth_upper_bound; depth >= 0 && depth < 1500; depth-- )
    {
        int face_count = face_depth_bucket_counts[depth];
        if( face_count == 0 )
            continue;

        int* faces = &face_depth_buckets[depth * 512];
        for( int i = 0; i < face_count; i++ )
        {
            int face_idx = faces[i];
            int prio = face_priorities[face_idx];
            int priority_face_count = face_priority_bucket_counts[prio]++;
            face_priority_buckets[prio * 2000 + priority_face_count] = face_idx;
        }
    }
}

void
raster_osrs(
    struct Pixel* pixel_buffer,
    int* priority_faces,
    int* priority_face_counts,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int* colors_a,
    int* colors_b,
    int* colors_c,
    int offset_x,
    int offset_y,
    int screen_width,
    int screen_height)
{
    for( int prio = 0; prio < 12; prio++ )
    {
        int* triangle_indexes = &priority_faces[prio * 2000];
        int triangle_count = priority_face_counts[prio];

        for( int i = 0; i < triangle_count; i++ )
        {
            int index = triangle_indexes[i];

            struct Triangle2D triangle;
            triangle.p1.x = vertex_x[face_indices_a[index]] + offset_x;
            triangle.p1.y = vertex_y[face_indices_a[index]] + offset_y;
            triangle.p1.z = vertex_z[face_indices_a[index]];

            triangle.p2.x = vertex_x[face_indices_b[index]] + offset_x;
            triangle.p2.y = vertex_y[face_indices_b[index]] + offset_y;
            triangle.p2.z = vertex_z[face_indices_b[index]];

            triangle.p3.x = vertex_x[face_indices_c[index]] + offset_x;
            triangle.p3.y = vertex_y[face_indices_c[index]] + offset_y;
            triangle.p3.z = vertex_z[face_indices_c[index]];

            // Skip triangle if any vertex was clipped
            if( triangle.p1.x < 0 || triangle.p2.x < 0 || triangle.p3.x < 0 )
            {
                continue;
            }

            int color_a = colors_a[index];
            int color_b = colors_b[index];
            int color_c = colors_c[index];

            struct GouraudColors gouraud_colors;
            gouraud_colors.color1 = g_hsl16_to_rgb_table[color_a];
            gouraud_colors.color2 = g_hsl16_to_rgb_table[color_b];
            gouraud_colors.color3 = g_hsl16_to_rgb_table[color_c];

            raster_triangle(pixel_buffer, gouraud_colors, triangle, SCREEN_WIDTH, SCREEN_HEIGHT);
        }
    }
}

void
raster_zbuf(
    struct Pixel* pixel_buffer,
    int* z_buffer,
    int num_faces,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int* colors_a,
    int* colors_b,
    int* colors_c,
    int offset_x,
    int offset_y,
    int screen_width,
    int screen_height)
{
    for( int index = 0; index < num_faces; index++ )
    {
        // Get vertex z coordinates for near plane check
        int z1 = vertex_z[face_indices_a[index]];
        int z2 = vertex_z[face_indices_b[index]];
        int z3 = vertex_z[face_indices_c[index]];

        struct Triangle2D triangle;
        triangle.p1.x = vertex_x[face_indices_a[index]] + offset_x;
        triangle.p1.y = vertex_y[face_indices_a[index]] + offset_y;
        triangle.p1.z = z1;

        triangle.p2.x = vertex_x[face_indices_b[index]] + offset_x;
        triangle.p2.y = vertex_y[face_indices_b[index]] + offset_y;
        triangle.p2.z = z2;

        triangle.p3.x = vertex_x[face_indices_c[index]] + offset_x;
        triangle.p3.y = vertex_y[face_indices_c[index]] + offset_y;
        triangle.p3.z = z3;

        int color_a = colors_a[index];
        int color_b = colors_b[index];
        int color_c = colors_c[index];

        struct GouraudColors gouraud_colors;
        gouraud_colors.color1 = g_hsl16_to_rgb_table[color_a];
        gouraud_colors.color2 = g_hsl16_to_rgb_table[color_b];
        gouraud_colors.color3 = g_hsl16_to_rgb_table[color_c];

        raster_triangle_zbuf(
            pixel_buffer, z_buffer, gouraud_colors, triangle, SCREEN_WIDTH, SCREEN_HEIGHT);
    }
}

static struct CacheModel*
load_from_cache(int model_id)
{
    return cache_load_model(model_id);
}

int
main(int argc, char* argv[])
{
    init_cos_table();
    init_sin_table();
    init_tan_table();
    init_hsl16_to_rgb_table();

    const int TEXTURE_WIDTH = 32;
    const int TEXTURE_HEIGHT = 32;

    // Create test buffers
    int* texels = malloc(TEXTURE_WIDTH * TEXTURE_HEIGHT * sizeof(int));
    create_test_texture(texels, TEXTURE_WIDTH, TEXTURE_HEIGHT);

    // Camera variables
    int camera_x = 0;
    int camera_y = 0;
    int camera_z = 420; // Start at a reasonable distance
    int camera_yaw = 0;
    int camera_pitch = 0;
    int camera_roll = 0;
    int camera_fov = 512; // Default FOV (approximately 90 degrees)

    static const int TZTOK_JAD_MODEL_ID = 9319;
    static const int TZTOK_JAD_NPCTYPE_ID = 3127;
    struct CacheModel* model = load_from_cache(TZTOK_JAD_MODEL_ID);
    struct CacheConfigNPCType* npc_type = cache_load_config_npctype(TZTOK_JAD_NPCTYPE_ID);
    if( model == NULL || model->vertex_count == 0 || npc_type == NULL )
    {
        printf("Failed to load model\n");
        return -1;
    }

    if( npc_type->name && strcmp(npc_type->name, "TzTok-Jad") == 0 )
    {
        printf("TzTok-Jad\n");
    }

    struct CacheModelBones* bones =
        modelbones_new_decode(model->vertex_bone_map, model->vertex_count);

    int* screen_vertices_x = (int*)malloc(model->vertex_count * sizeof(int));
    int* screen_vertices_y = (int*)malloc(model->vertex_count * sizeof(int));
    int* screen_vertices_z = (int*)malloc(model->vertex_count * sizeof(int));

    struct VertexNormal* vertex_normals =
        (struct VertexNormal*)malloc(model->vertex_count * sizeof(struct VertexNormal));

    int* face_colors_a_hsl16 = (int*)malloc(model->face_count * sizeof(int));
    int* face_colors_b_hsl16 = (int*)malloc(model->face_count * sizeof(int));
    int* face_colors_c_hsl16 = (int*)malloc(model->face_count * sizeof(int));

    int model_pitch = 0;
    int model_yaw = 0;
    int model_roll = 0;

    // Add frame timing variables
    int current_frame = -1;
    int32_t last_frame_time = -200000;
    // Pretty sure the sequence contains the number of 30fps frames that pass before the next
    // frame is played.
    const int32_t FRAME_DURATION = 150;

    struct BoundingCylinder bounding_cylinder = calculate_bounding_cylinder(
        model->vertex_count, model->vertices_x, model->vertices_y, model->vertices_z);

    int model_min_depth = bounding_cylinder.min_z_depth_any_rotation;

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

    // Make a copy of the model vertices for animation
    int* animated_vertices_x = malloc(sizeof(int) * model->vertex_count);
    int* animated_vertices_y = malloc(sizeof(int) * model->vertex_count);
    int* animated_vertices_z = malloc(sizeof(int) * model->vertex_count);

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

    // Add mouse state tracking
    int last_mouse_x = 0;
    int last_mouse_y = 0;
    bool mouse_down = false;

    while( true )
    {
        memset(tmp_depth_face_count, 0, sizeof(tmp_depth_face_count));
        memset(tmp_depth_faces, 0, sizeof(tmp_depth_faces));
        memset(tmp_priority_face_count, 0, sizeof(tmp_priority_face_count));
        memset(tmp_priority_faces, 0, sizeof(tmp_priority_faces));

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
            case SDL_MOUSEBUTTONDOWN:
                if( event.button.button == SDL_BUTTON_LEFT )
                {
                    mouse_down = true;
                    last_mouse_x = event.button.x;
                    last_mouse_y = event.button.y;
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if( event.button.button == SDL_BUTTON_LEFT )
                {
                    mouse_down = false;
                }
                break;
            case SDL_MOUSEMOTION:
                if( mouse_down )
                {
                    int dx = -(event.motion.x - last_mouse_x);
                    int dy = -(event.motion.y - last_mouse_y);

                    // Update model rotation based on mouse movement
                    model_yaw = (model_yaw - dx * 2 + 2048) % 2048;
                    model_pitch = (model_pitch - dy * 2 + 2048) % 2048;

                    last_mouse_x = event.motion.x;
                    last_mouse_y = event.motion.y;
                }
                break;
            case SDL_MOUSEWHEEL:
                // Adjust camera distance based on scroll direction
                // Positive y is scroll up (zoom in), negative y is scroll down (zoom out)
                camera_z -= event.wheel.y * 10; // Move camera closer/further
                if( camera_z < 50 )
                    camera_z = 50; // Prevent getting too close
                break;
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

        // Update frame timing
        Uint32 current_time = SDL_GetTicks();
        bool update_frame = false;
        if( current_time - last_frame_time >= FRAME_DURATION )
        {
            current_frame = (current_frame + 1) % 16; // Wrap at 15
            last_frame_time = current_time;
            update_frame = true;
        }

        if( update_frame )
        {
            // Load and apply frame transformation for current frame
            char framemap_filename[256];
            char frame_filename[256];
            snprintf(
                framemap_filename, sizeof(framemap_filename), "framemap_%d.bin", current_frame);
            snprintf(frame_filename, sizeof(frame_filename), "frame_%d.bin", current_frame);

            // Load framemap
            FILE* framemap_file = fopen(framemap_filename, "rb");
            if( !framemap_file )
            {
                printf("Failed to open framemap file %s\n", framemap_filename);
            }
            else
            {
                fseek(framemap_file, 0, SEEK_END);
                int framemap_size = ftell(framemap_file);
                fseek(framemap_file, 0, SEEK_SET);
                char* framemap_data = malloc(framemap_size);
                fread(framemap_data, 1, framemap_size, framemap_file);
                fclose(framemap_file);

                // Load frame
                FILE* frame_file = fopen(frame_filename, "rb");
                if( !frame_file )
                {
                    printf("Failed to open frame file %s\n", frame_filename);
                    free(framemap_data);
                }
                else
                {
                    fseek(frame_file, 0, SEEK_END);
                    int frame_size = ftell(frame_file);
                    fseek(frame_file, 0, SEEK_SET);
                    char* frame_data = malloc(frame_size);
                    fread(frame_data, 1, frame_size, frame_file);
                    fclose(frame_file);

                    // Decode framemap and frame
                    struct Buffer framemap_buffer = { .data = framemap_data,
                                                      .data_size = framemap_size,
                                                      .position = 0 };
                    struct Buffer frame_buffer = { .data = frame_data,
                                                   .data_size = frame_size,
                                                   .position = 0 };

                    struct CacheFramemap* framemap =
                        framemap_new_decode(current_frame, &framemap_buffer);

                    struct CacheFrame* frame =
                        frame_new_decode(current_frame, framemap, &frame_buffer);

                    memcpy(
                        animated_vertices_x, model->vertices_x, sizeof(int) * model->vertex_count);
                    memcpy(
                        animated_vertices_y, model->vertices_y, sizeof(int) * model->vertex_count);
                    memcpy(
                        animated_vertices_z, model->vertices_z, sizeof(int) * model->vertex_count);

                    // Apply frame transformation
                    anim_frame_apply(
                        frame,
                        framemap,
                        animated_vertices_x,
                        animated_vertices_y,
                        animated_vertices_z,
                        bones->bones_count,
                        bones->bones,
                        bones->bones_sizes);

                    // Cleanup
                    frame_free(frame);
                    framemap_free(framemap);
                    free(framemap_data);
                    free(frame_data);
                }
            }
        }

        project_vertices(
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            model->vertex_count,
            animated_vertices_x,
            animated_vertices_y,
            animated_vertices_z,
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
        clock_t start = clock();
        bucket_sort_by_average_depth(
            tmp_depth_faces,
            tmp_depth_face_count,
            model_min_depth,
            model->face_count,
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            model->face_indices_a,
            model->face_indices_b,
            model->face_indices_c);

        //
        // partition the sorted faces by priority.
        // So each partition are sorted by depth.

        // since the depth of the faces is shifted up by model_min_depth,
        // that means the depth upper bound is model_min_depth*2
        parition_faces_by_priority(
            tmp_priority_faces,
            tmp_priority_face_count,
            tmp_depth_faces,
            tmp_depth_face_count,
            model->face_count,
            model->face_priorities,
            // 1499);
            model_min_depth * 2);

        calculate_vertex_normals(
            vertex_normals,
            model->vertex_count,
            model->face_indices_a,
            model->face_indices_b,
            model->face_indices_c,
            animated_vertices_x,
            animated_vertices_y,
            animated_vertices_z,
            model->face_count);

        int light_ambient = 64;
        int light_attenuation = 850;
        int lightsrc_x = -30;
        int lightsrc_y = -50;
        int lightsrc_z = -30;
        int light_magnitude =
            (int)sqrt(lightsrc_x * lightsrc_x + lightsrc_y * lightsrc_y + lightsrc_z * lightsrc_z);
        int attenuation = light_attenuation * light_magnitude >> 8;

        apply_lighting(
            face_colors_a_hsl16,
            face_colors_b_hsl16,
            face_colors_c_hsl16,
            vertex_normals,
            model->face_indices_a,
            model->face_indices_b,
            model->face_indices_c,
            model->face_count,
            animated_vertices_x,
            animated_vertices_y,
            animated_vertices_z,
            model->face_colors,
            model->face_infos,
            light_ambient,
            attenuation,
            lightsrc_x,
            lightsrc_y,
            lightsrc_z);

        raster_osrs(
            pixel_buffer,
            tmp_priority_faces,
            tmp_priority_face_count,
            model->face_indices_a,
            model->face_indices_b,
            model->face_indices_c,
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            face_colors_a_hsl16,
            face_colors_b_hsl16,
            face_colors_c_hsl16,
            0,
            0,
            SCREEN_WIDTH,
            SCREEN_HEIGHT);
        clock_t end = clock();
        printf("raster_osrs: %f\n", (double)(end - start) / CLOCKS_PER_SEC);

        // start = clock();
        // raster_zbuf(
        //     pixel_buffer,
        //     z_buffer,
        //     model.face_count,
        //     model.face_indices_a,
        //     model.face_indices_b,
        //     model.face_indices_c,
        //     screen_vertices_x,
        //     screen_vertices_y,
        //     screen_vertices_z,
        //     model.face_color_a,
        //     model.face_color_b,
        //     model.face_color_c,
        //     SCREEN_WIDTH / 4,
        //     0,
        //     SCREEN_WIDTH,
        //     SCREEN_HEIGHT);
        // end = clock();
        // printf("raster_zbuf: %f\n", (double)(end - start) / CLOCKS_PER_SEC);

        // Create test texture in upper left corner

        // Draw textured triangle in upper left
        // raster_texture(
        //     (int*)pixel_buffer,
        //     SCREEN_WIDTH,
        //     SCREEN_HEIGHT,
        //     10,
        //     100,
        //     100, // x coords
        //     10,
        //     10,
        //     100, // y coords
        //     0,
        //     31,
        //     31, // z coords
        //     0,
        //     31,
        //     31, // u coords
        //     0,
        //     0,
        //     31, // v coords
        //     texels,
        //     TEXTURE_WIDTH);

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
        SDL_RenderPresent(renderer);
    }

done:

    SDL_DestroyWindow(window);
    SDL_Quit();

    return EXIT_SUCCESS;
}