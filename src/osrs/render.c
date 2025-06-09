#include "osrs/render.h"

#include "anim.h"
#include "gouraud.h"
#include "lighting.h"
#include "projection.h"
#include "scene_tile.h"
#include "tables/maps.h"
#include "tables/model.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int g_sin_table[2048];
extern int g_cos_table[2048];
extern int g_tan_table[2048];

//   This tool renders a color palette using jagex's 16-bit HSL, 6 bits
//             for hue, 3 for saturation and 7 for lightness, bitpacked and
//             represented as a short.
extern int g_hsl16_to_rgb_table[65536];

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
    int screen_height,
    int near_plane_z)
{
    struct ProjectedTriangle projected_triangle;

    assert(camera_pitch >= 0 && camera_pitch < 2048);
    assert(camera_yaw >= 0 && camera_yaw < 2048);
    assert(camera_roll >= 0 && camera_roll < 2048);

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
        model_yaw,
        model_pitch,
        model_roll,
        scene_x,
        scene_y,
        scene_z,
        camera_yaw,
        camera_pitch,
        camera_roll,
        camera_fov,
        near_plane_z,
        screen_width,
        screen_height);

    // int a = (scene_z * cos_camera_yaw - scene_x * sin_camera_yaw) >> 16;
    // // b is the z projection of the models origin (imagine a vertex at x=0,y=0 and z=0).
    // // So the depth is the z projection distance from the origin of the model.
    // int b = (scene_y * sin_camera_pitch + a * cos_camera_pitch) >> 16;

    int model_origin_z_projection = projected_triangle.z1;

    for( int i = 0; i < num_vertices; i++ )
    {
        projected_triangle = project(
            vertex_x[i],
            vertex_y[i],
            vertex_z[i],
            model_yaw,
            model_pitch,
            model_roll,
            scene_x,
            scene_y,
            scene_z,
            camera_yaw,
            camera_pitch,
            camera_roll,
            camera_fov,
            near_plane_z,
            screen_width,
            screen_height);

        // If vertex is too close to camera, set it to a large negative value
        // This will cause it to be clipped in the rasterization step
        if( projected_triangle.z1 < near_plane_z )
        {
            screen_vertices_x[i] = -5000;
            screen_vertices_y[i] = -5000;
            screen_vertices_z[i] = -5000;
        }
        else
        {
            screen_vertices_x[i] = projected_triangle.x1;
            screen_vertices_y[i] = projected_triangle.y1;
            screen_vertices_z[i] = projected_triangle.z1 - model_origin_z_projection;
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
        int dot_product = (xa - xb) * (yc - yb) - (ya - yb) * (xc - xb);
        if( dot_product > 0 )
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
            // The reason is uses the models "upper ys" (max_y) is because OSRS assumes the camera
            // will always be above the model, so the closest vertices to the camera will be towards
            // the top of the model. (i.e. lowest z values)
            // Relative to the model's origin, there may be negative z values, but always |z| <
            // min_depth so the min_depth is used to adjust all z values to be positive, but
            // maintain relative order.
            int depth_average = (za + zb + zc) / 3 + model_min_depth;

            if( depth_average < 1500 && depth_average > 0 )
            {
                int bucket_index = face_depth_bucket_counts[depth_average]++;
                face_depth_buckets[depth_average * 512 + bucket_index] = f;
            }
            else
            {
                printf(
                    "Out of bounds %d, (%d, %d, %d) - %d\n",
                    depth_average,
                    za,
                    zb,
                    zc,
                    model_min_depth);
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

            int x1 = vertex_x[face_indices_a[index]] + offset_x;
            int y1 = vertex_y[face_indices_a[index]] + offset_y;
            int z1 = vertex_z[face_indices_a[index]];
            int x2 = vertex_x[face_indices_b[index]] + offset_x;
            int y2 = vertex_y[face_indices_b[index]] + offset_y;
            int z2 = vertex_z[face_indices_b[index]];
            int x3 = vertex_x[face_indices_c[index]] + offset_x;
            int y3 = vertex_y[face_indices_c[index]] + offset_y;
            int z3 = vertex_z[face_indices_c[index]];

            // Skip triangle if any vertex was clipped
            if( x1 == -5000 || x3 == -5000 || x3 == -5000 )
                continue;

            int color_a = colors_a[index];
            int color_b = colors_b[index];
            int color_c = colors_c[index];

            color_a = g_hsl16_to_rgb_table[color_a];
            color_b = g_hsl16_to_rgb_table[color_b];
            color_c = g_hsl16_to_rgb_table[color_c];

            raster_gouraud(
                pixel_buffer,
                screen_width,
                screen_height,
                x1,
                x2,
                x3,
                y1,
                y2,
                y3,
                color_a,
                color_b,
                color_c);
        }
    }
}

void
raster_osrs_single(
    struct Pixel* pixel_buffer,
    int face,
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
    int index = face;

    int x1 = vertex_x[face_indices_a[index]] + offset_x;
    int y1 = vertex_y[face_indices_a[index]] + offset_y;
    int z1 = vertex_z[face_indices_a[index]];
    int x2 = vertex_x[face_indices_b[index]] + offset_x;
    int y2 = vertex_y[face_indices_b[index]] + offset_y;
    int z2 = vertex_z[face_indices_b[index]];
    int x3 = vertex_x[face_indices_c[index]] + offset_x;
    int y3 = vertex_y[face_indices_c[index]] + offset_y;
    int z3 = vertex_z[face_indices_c[index]];

    // Skip triangle if any vertex was clipped
    // TODO: Perhaps use a separate buffer to track this.
    if( x1 == -5000 || x2 == -5000 || x3 == -5000 )
        return;

    int color_a = colors_a[index];
    int color_b = colors_b[index];
    int color_c = colors_c[index];

    assert(color_a >= 0 && color_a < 65536);
    assert(color_b >= 0 && color_b < 65536);
    assert(color_c >= 0 && color_c < 65536);

    color_a = g_hsl16_to_rgb_table[color_a];
    color_b = g_hsl16_to_rgb_table[color_b];
    color_c = g_hsl16_to_rgb_table[color_c];

    raster_gouraud(
        pixel_buffer,
        screen_width,
        screen_height,
        x1,
        x2,
        x3,
        y1,
        y2,
        y3,
        color_a,
        color_b,
        color_c);
}

static int tmp_depth_face_count[1500] = { 0 };
static int tmp_depth_faces[1500 * 512] = { 0 };
static int tmp_priority_face_count[12] = { 0 };
static int tmp_priority_depth_sum[12] = { 0 };
static int tmp_priority_faces[12 * 2000] = { 0 };

void
render_model_frame(
    int* pixel_buffer,
    int width,
    int height,
    int near_plane_z,
    int model_yaw,
    int model_pitch,
    int model_roll,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_pitch,
    int camera_yaw,
    int camera_roll,
    int fov,
    struct Model* model,
    struct ModelBones* bones_nullable,
    struct Frame* frame_nullable,
    struct Framemap* framemap_nullable)
{
    int* vertices_x = (int*)malloc(model->vertex_count * sizeof(int));
    memcpy(vertices_x, model->vertices_x, model->vertex_count * sizeof(int));
    int* vertices_y = (int*)malloc(model->vertex_count * sizeof(int));
    memcpy(vertices_y, model->vertices_y, model->vertex_count * sizeof(int));
    int* vertices_z = (int*)malloc(model->vertex_count * sizeof(int));
    memcpy(vertices_z, model->vertices_z, model->vertex_count * sizeof(int));

    int* face_colors_a_hsl16 = (int*)malloc(model->face_count * sizeof(int));
    memcpy(face_colors_a_hsl16, model->face_colors, model->face_count * sizeof(int));
    int* face_colors_b_hsl16 = (int*)malloc(model->face_count * sizeof(int));
    memcpy(face_colors_b_hsl16, model->face_colors, model->face_count * sizeof(int));
    int* face_colors_c_hsl16 = (int*)malloc(model->face_count * sizeof(int));
    memcpy(face_colors_c_hsl16, model->face_colors, model->face_count * sizeof(int));

    struct VertexNormal* vertex_normals =
        (struct VertexNormal*)malloc(model->vertex_count * sizeof(struct VertexNormal));
    memset(vertex_normals, 0, model->vertex_count * sizeof(struct VertexNormal));

    memset(tmp_depth_face_count, 0, sizeof(tmp_depth_face_count));
    memset(tmp_depth_faces, 0, sizeof(tmp_depth_faces));
    memset(tmp_priority_face_count, 0, sizeof(tmp_priority_face_count));
    memset(tmp_priority_depth_sum, 0, sizeof(tmp_priority_depth_sum));
    memset(tmp_priority_faces, 0, sizeof(tmp_priority_faces));

    int* screen_vertices_x = (int*)malloc(model->vertex_count * sizeof(int));
    int* screen_vertices_y = (int*)malloc(model->vertex_count * sizeof(int));
    int* screen_vertices_z = (int*)malloc(model->vertex_count * sizeof(int));

    struct BoundingCylinder bounding_cylinder = calculate_bounding_cylinder(
        model->vertex_count, model->vertices_x, model->vertices_y, model->vertices_z);

    int model_min_depth = bounding_cylinder.min_z_depth_any_rotation;

    if( frame_nullable && framemap_nullable && bones_nullable )
    {
        anim_frame_apply(
            frame_nullable,
            framemap_nullable,
            vertices_x,
            vertices_y,
            vertices_z,
            bones_nullable->bones_count,
            bones_nullable->bones,
            bones_nullable->bones_sizes);
    }

    calculate_vertex_normals(
        vertex_normals,
        model->face_indices_a,
        model->face_indices_b,
        model->face_indices_c,
        vertices_x,
        vertices_y,
        vertices_z,
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
        vertices_x,
        vertices_y,
        vertices_z,
        model->face_colors,
        light_ambient,
        attenuation,
        lightsrc_x,
        lightsrc_y,
        lightsrc_z);

    project_vertices(
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        model->vertex_count,
        vertices_x,
        vertices_y,
        vertices_z,
        model_yaw,
        model_pitch,
        model_roll,
        scene_x,
        scene_y,
        scene_z,
        camera_yaw,
        camera_pitch,
        camera_roll,
        fov,
        width,
        height,
        near_plane_z);

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

    parition_faces_by_priority(
        tmp_priority_faces,
        tmp_priority_face_count,
        tmp_depth_faces,
        tmp_depth_face_count,
        model->face_count,
        model->face_priorities,
        model_min_depth * 2);

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
        width,
        height);

    free(vertices_x);
    free(vertices_y);
    free(vertices_z);
    free(face_colors_a_hsl16);
    free(face_colors_b_hsl16);
    free(face_colors_c_hsl16);
    free(vertex_normals);
    free(screen_vertices_x);
    free(screen_vertices_y);
    free(screen_vertices_z);
}

static int tile_shape_vertex_indices[15][6] = {
    { 1, 3, 5, 7 },
    { 1, 3, 5, 7 },
    { 1, 3, 5, 7 },
    { 1, 3, 5, 7, 6 },
    { 1, 3, 5, 7, 6 },
    { 1, 3, 5, 7, 6 },
    { 1, 3, 5, 7, 6 },
    { 1, 3, 5, 7, 2, 6 },
    { 1, 3, 5, 7, 2, 8 },
    { 1, 3, 5, 7, 2, 8 },
    { 1, 3, 5, 7, 11, 12 },
    { 1, 3, 5, 7, 11, 12 },
    { 1, 3, 5, 7, 13, 14 },
};

static int tile_shape_vertex_indices_lengths[15] = {
    4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6,
};

static int tile_shape_faces[15][30] = {
    { 0, 1, 2, 3, 0, 0, 1, 3 },
    { 1, 1, 2, 3, 1, 0, 1, 3 },
    { 0, 1, 2, 3, 1, 0, 1, 3 },
    { 0, 0, 1, 2, 0, 0, 2, 4, 1, 0, 4, 3 },
    { 0, 0, 1, 4, 0, 0, 4, 3, 1, 1, 2, 4 },
    { 0, 0, 4, 3, 1, 0, 1, 2, 1, 0, 2, 4 },
    { 0, 1, 2, 4, 1, 0, 1, 4, 1, 0, 4, 3 },
    { 0, 4, 1, 2, 0, 4, 2, 5, 1, 0, 4, 5, 1, 0, 5, 3 },
    { 0, 4, 1, 2, 0, 4, 2, 3, 0, 4, 3, 5, 1, 0, 4, 5 },
    { 0, 0, 4, 5, 1, 4, 1, 2, 1, 4, 2, 3, 1, 4, 3, 5 },
    { 0, 0, 1, 5, 0, 1, 4, 5, 0, 1, 2, 4, 1, 0, 5, 3, 1, 5, 4, 3, 1, 4, 2, 3 },
    { 1, 0, 1, 5, 1, 1, 4, 5, 1, 1, 2, 4, 0, 0, 5, 3, 0, 5, 4, 3, 0, 4, 2, 3 },
    { 1, 0, 5, 4, 1, 0, 1, 5, 0, 0, 4, 3, 0, 4, 5, 3, 0, 5, 2, 3, 0, 1, 2, 5 },
};

static int tile_shape_face_counts[15] = {
    8, 8, 8, 12, 12, 12, 12, 16, 16, 16, 24, 24, 24, 24, 24,
};

#define TILE_SIZE 128

void
render_scene_tiles(
    int* pixel_buffer,
    int width,
    int height,
    int near_plane_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_pitch,
    int camera_yaw,
    int camera_roll,
    int fov,
    struct SceneTile* tiles,
    int tile_count)
{
    int* screen_vertices_x = (int*)malloc(20 * sizeof(int));
    int* screen_vertices_y = (int*)malloc(20 * sizeof(int));
    int* screen_vertices_z = (int*)malloc(20 * sizeof(int));

    for( int z = 0; z < 1; z++ )
    {
        // y = 5, and x = 40 is the left corner of the church.
        for( int y = 0; y < MAP_TERRAIN_Y; y++ )
        {
            for( int x = 0; x < MAP_TERRAIN_X; x++ )
            {
                int i = MAP_TILE_COORD(x, y, z);
                struct SceneTile* tile = &tiles[i];

                if( tile->vertex_count == 0 || tile->face_color_hsl_a == NULL )
                    continue;

                project_vertices(
                    screen_vertices_x,
                    screen_vertices_y,
                    screen_vertices_z,
                    tile->vertex_count,
                    tile->vertex_x,
                    tile->vertex_y,
                    tile->vertex_z,
                    0,
                    0,
                    0,
                    scene_x,
                    // world_y is scene_z
                    scene_z,
                    // world_z is scene_y
                    scene_y,
                    camera_yaw,
                    camera_pitch,
                    camera_roll,
                    fov,
                    width,
                    height,
                    near_plane_z);

                for( int face = 0; face < tile->face_count; face++ )
                {
                    raster_osrs_single(
                        pixel_buffer,
                        face,
                        tile->faces_a,
                        tile->faces_b,
                        tile->faces_c,
                        screen_vertices_x,
                        screen_vertices_y,
                        screen_vertices_z,
                        // TODO: Remove legacy face_color_hsl.
                        tile->face_color_hsl_a,
                        tile->face_color_hsl_b,
                        tile->face_color_hsl_c,
                        0,
                        0,
                        width,
                        height);
                }
            }
        }
    }
    free(screen_vertices_x);
    free(screen_vertices_y);
    free(screen_vertices_z);
}