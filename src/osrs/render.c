#include "osrs/render.h"

#include "anim.h"
#include "gouraud.h"
#include "lighting.h"
#include "projection.h"
#include "scene_tile.h"
#include "tables/maps.h"
#include "tables/model.h"

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
            screen_width,
            screen_height);

        // If vertex is too close to camera, set it to a large negative value
        // This will cause it to be clipped in the rasterization step
        if( projected_triangle.z1 < near_plane_z )
        {
            screen_vertices_x[i] = -1;
            screen_vertices_y[i] = -1;
            screen_vertices_z[i] = -1;
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
            if( x1 < 0 || x2 < 0 || x3 < 0 )
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
    if( x1 < 0 || x2 < 0 || x3 < 0 )
        return;

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

static int tile_shape_vertex_indices[15][4] = {
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
render_map_terrain(
    int* pixel_buffer,
    int width,
    int height,
    int near_plane_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_yaw,
    int camera_pitch,
    int camera_roll,
    int fov,
    struct MapTerrain* map_terrain,
    struct MapLocs* map_locs)
{
    for( int z = 0; z < 4; z++ )
    {
        for( int x = 0; x < 64; x++ )
        {
            for( int y = 0; y < 64; y++ )
            {
                struct MapTile* tile = &map_terrain->tiles_xyz[MAP_TILE_COORD(x, y, z)];

                int shape = tile->shape;
                int rotation = tile->rotation;

                int vertex_count = tile_shape_vertex_indices_lengths[shape];
                int* vertex_indices = tile_shape_vertex_indices[shape];
                int* faces = tile_shape_faces[shape];

                int* vertex_x = malloc(vertex_count * sizeof(int));
                int* vertex_y = malloc(vertex_count * sizeof(int));
                int* vertex_z = malloc(vertex_count * sizeof(int));

                int tile_x = (x + 1) * TILE_SIZE;
                int tile_y = (y + 1) * TILE_SIZE;

                int height_sw = map_terrain->tiles_xyz[MAP_TILE_COORD(tile_x, tile_y, 0)].height;
                int height_se =
                    map_terrain->tiles_xyz[MAP_TILE_COORD(tile_x + 1, tile_y, 0)].height;
                int height_ne =
                    map_terrain->tiles_xyz[MAP_TILE_COORD(tile_x + 1, tile_y + 1, 0)].height;
                int height_nw =
                    map_terrain->tiles_xyz[MAP_TILE_COORD(tile_x, tile_y + 1, 0)].height;

                // for (let i = 0; i < vertexCount; i++) {
                //     let vertexIndex = vertexIndices[i];
                //     if ((vertexIndex & 1) === 0 && vertexIndex <= 8) {
                //         vertexIndex = ((vertexIndex - rotation - rotation - 1) & 7) + 1;
                //     }

                //     if (vertexIndex > 8 && vertexIndex <= 12) {
                //         vertexIndex = ((vertexIndex - 9 - rotation) & 3) + 9;
                //     }

                //     if (vertexIndex > 12 && vertexIndex <= 16) {
                //         vertexIndex = ((vertexIndex - 13 - rotation) & 3) + 13;
                //     }

                //     let vertX = 0;
                //     let vertZ = 0;
                //     let vertY = 0;
                //     let vertUnderlayHsl = 0;
                //     let vertUnderlayMinimapHsl = 0;
                //     let vertOverlayHsl = 0;
                //     let vertOverlayMinimapHsl = 0;

                //     if (vertexIndex === 1) {
                //         vertX = tileX;
                //         vertZ = tileY;
                //         vertY = heightSw;
                //         vertUnderlayHsl = underlayHslSw;
                //         vertUnderlayMinimapHsl = underlayMinimapHslSw;
                //         vertOverlayHsl = overlayHslSw;
                //         vertOverlayMinimapHsl = overlayMinimapHslSw;
                //     } else if (vertexIndex === 2) {
                //         vertX = tileX + HALF_TILE_SIZE;
                //         vertZ = tileY;
                //         vertY = (heightSe + heightSw) >> 1;
                //         vertUnderlayHsl = mixHsl(underlayHslSe, underlayHslSw);
                //         vertUnderlayMinimapHsl = (underlayMinimapHslSe + underlayMinimapHslSw) >>
                //         1; vertOverlayHsl = (overlayHslSe + overlayHslSw) >> 1;
                //         vertOverlayMinimapHsl = (overlayMinimapHslSe + overlayMinimapHslSw) >> 1;
                //     } else if (vertexIndex === 3) {
                //         vertX = tileX + TILE_SIZE;
                //         vertZ = tileY;
                //         vertY = heightSe;
                //         vertUnderlayHsl = underlayHslSe;
                //         vertUnderlayMinimapHsl = underlayMinimapHslSe;
                //         vertOverlayHsl = overlayHslSe;
                //         vertOverlayMinimapHsl = overlayMinimapHslSe;
                //     } else if (vertexIndex === 4) {
                //         vertX = tileX + TILE_SIZE;
                //         vertZ = tileY + HALF_TILE_SIZE;
                //         vertY = (heightNe + heightSe) >> 1;
                //         vertUnderlayHsl = mixHsl(underlayHslSe, underlayHslNe);
                //         vertUnderlayMinimapHsl = (underlayMinimapHslSe + underlayMinimapHslNe) >>
                //         1; vertOverlayHsl = (overlayHslSe + overlayHslNe) >> 1;
                //         vertOverlayMinimapHsl = (overlayMinimapHslSe + overlayMinimapHslNe) >> 1;
                //     } else if (vertexIndex === 5) {
                //         vertX = tileX + TILE_SIZE;
                //         vertZ = tileY + TILE_SIZE;
                //         vertY = heightNe;
                //         vertUnderlayHsl = underlayHslNe;
                //         vertUnderlayMinimapHsl = underlayMinimapHslNe;
                //         vertOverlayHsl = overlayHslNe;
                //         vertOverlayMinimapHsl = overlayMinimapHslNe;
                //     } else if (vertexIndex === 6) {
                //         vertX = tileX + HALF_TILE_SIZE;
                //         vertZ = tileY + TILE_SIZE;
                //         vertY = (heightNe + heightNw) >> 1;
                //         vertUnderlayHsl = mixHsl(underlayHslNw, underlayHslNe);
                //         vertUnderlayMinimapHsl = (underlayMinimapHslNw + underlayMinimapHslNe) >>
                //         1; vertOverlayHsl = (overlayHslNw + overlayHslNe) >> 1;
                //         vertOverlayMinimapHsl = (overlayMinimapHslNw + overlayMinimapHslNe) >> 1;
                //     } else if (vertexIndex === 7) {
                //         vertX = tileX;
                //         vertZ = tileY + TILE_SIZE;
                //         vertY = heightNw;
                //         vertUnderlayHsl = underlayHslNw;
                //         vertUnderlayMinimapHsl = underlayMinimapHslNw;
                //         vertOverlayHsl = overlayHslNw;
                //         vertOverlayMinimapHsl = overlayMinimapHslNw;
                //     } else if (vertexIndex === 8) {
                //         vertX = tileX;
                //         vertZ = tileY + HALF_TILE_SIZE;
                //         vertY = (heightNw + heightSw) >> 1;
                //         vertUnderlayHsl = mixHsl(underlayHslNw, underlayHslSw);
                //         vertUnderlayMinimapHsl = (underlayMinimapHslNw + underlayMinimapHslSw) >>
                //         1; vertOverlayHsl = (overlayHslNw + overlayHslSw) >> 1;
                //         vertOverlayMinimapHsl = (overlayMinimapHslNw + overlayMinimapHslSw) >> 1;
                //     } else if (vertexIndex === 9) {
                //         vertX = tileX + HALF_TILE_SIZE;
                //         vertZ = tileY + QUARTER_TILE_SIZE;
                //         vertY = (heightSe + heightSw) >> 1;
                //         vertUnderlayHsl = mixHsl(underlayHslSe, underlayHslSw);
                //         vertUnderlayMinimapHsl = (underlayMinimapHslSe + underlayMinimapHslSw) >>
                //         1; vertOverlayHsl = (overlayHslSe + overlayHslSw) >> 1;
                //         vertOverlayMinimapHsl = (overlayMinimapHslSe + overlayMinimapHslSw) >> 1;
                //     } else if (vertexIndex === 10) {
                //         vertX = tileX + THREE_QTR_TILE_SIZE;
                //         vertZ = tileY + HALF_TILE_SIZE;
                //         vertY = (heightNe + heightSe) >> 1;
                //         vertUnderlayHsl = mixHsl(underlayHslSe, underlayHslNe);
                //         vertUnderlayMinimapHsl = (underlayMinimapHslSe + underlayMinimapHslNe) >>
                //         1; vertOverlayHsl = (overlayHslSe + overlayHslNe) >> 1;
                //         vertOverlayMinimapHsl = (overlayMinimapHslSe + overlayMinimapHslNe) >> 1;
                //     } else if (vertexIndex === 11) {
                //         vertX = tileX + HALF_TILE_SIZE;
                //         vertZ = tileY + THREE_QTR_TILE_SIZE;
                //         vertY = (heightNe + heightNw) >> 1;
                //         vertUnderlayHsl = mixHsl(underlayHslNw, underlayHslNe);
                //         vertUnderlayMinimapHsl = (underlayMinimapHslNw + underlayMinimapHslNe) >>
                //         1; vertOverlayHsl = (overlayHslNw + overlayHslNe) >> 1;
                //         vertOverlayMinimapHsl = (overlayMinimapHslNw + overlayMinimapHslNe) >> 1;
                //     } else if (vertexIndex === 12) {
                //         vertX = tileX + QUARTER_TILE_SIZE;
                //         vertZ = tileY + HALF_TILE_SIZE;
                //         vertY = (heightNw + heightSw) >> 1;
                //         vertUnderlayHsl = mixHsl(underlayHslNw, underlayHslSw);
                //         vertUnderlayMinimapHsl = (underlayMinimapHslNw + underlayMinimapHslSw) >>
                //         1; vertOverlayHsl = (overlayHslNw + overlayHslSw) >> 1;
                //         vertOverlayMinimapHsl = (overlayMinimapHslNw + overlayMinimapHslSw) >> 1;
                //     } else if (vertexIndex === 13) {
                //         vertX = tileX + QUARTER_TILE_SIZE;
                //         vertZ = tileY + QUARTER_TILE_SIZE;
                //         vertY = heightSw;
                //         vertUnderlayHsl = underlayHslSw;
                //         vertUnderlayMinimapHsl = underlayMinimapHslSw;
                //         vertOverlayHsl = overlayHslSw;
                //         vertOverlayMinimapHsl = overlayMinimapHslSw;
                //     } else if (vertexIndex === 14) {
                //         vertX = tileX + THREE_QTR_TILE_SIZE;
                //         vertZ = tileY + QUARTER_TILE_SIZE;
                //         vertY = heightSe;
                //         vertUnderlayHsl = underlayHslSe;
                //         vertUnderlayMinimapHsl = underlayMinimapHslSe;
                //         vertOverlayHsl = overlayHslSe;
                //         vertOverlayMinimapHsl = overlayMinimapHslSe;
                //     } else if (vertexIndex === 15) {
                //         vertX = tileX + THREE_QTR_TILE_SIZE;
                //         vertZ = tileY + THREE_QTR_TILE_SIZE;
                //         vertY = heightNe;
                //         vertUnderlayHsl = underlayHslNe;
                //         vertUnderlayMinimapHsl = underlayMinimapHslNe;
                //         vertOverlayHsl = overlayHslNe;
                //         vertOverlayMinimapHsl = overlayMinimapHslNe;
                //     } else {
                //         vertX = tileX + QUARTER_TILE_SIZE;
                //         vertZ = tileY + THREE_QTR_TILE_SIZE;
                //         vertY = heightNw;
                //         vertUnderlayHsl = underlayHslNw;
                //         vertUnderlayMinimapHsl = underlayMinimapHslNw;
                //         vertOverlayHsl = overlayHslNw;
                //         vertOverlayMinimapHsl = overlayMinimapHslNw;
                //     }

                //     this.vertexX[i] = vertX;
                //     this.vertexY[i] = vertY;
                //     this.vertexZ[i] = vertZ;
                //     underlayHsls[i] = vertUnderlayHsl;
                //     underlayMinimapHsls[i] = vertUnderlayMinimapHsl;
                //     overlayHsls[i] = vertOverlayHsl;
                //     overlayMinimapHsls[i] = vertOverlayMinimapHsl;
                // }
                for( int i = 0; i < vertex_count; i++ )
                {
                    int vertex_index = vertex_indices[i];
                    if( (vertex_index & 1) == 0 && vertex_index <= 8 )
                    {
                        vertex_index = ((vertex_index - rotation - rotation - 1) & 7) + 1;
                    }

                    if( vertex_index > 8 && vertex_index <= 12 )
                    {
                        vertex_index = ((vertex_index - 9 - rotation) & 3) + 9;
                    }

                    if( vertex_index > 12 && vertex_index <= 16 )
                    {
                        vertex_index = ((vertex_index - 13 - rotation) & 3) + 13;
                    }

                    int vert_x = 0;
                    int vert_z = 0;
                    int vert_y = 0;

                    if( vertex_index == 1 )
                    {
                        vert_x = tile_x;
                        vert_z = tile_y;
                        vert_y = height_sw;
                    }
                    else if( vertex_index == 2 )
                    {
                        vert_x = tile_x + TILE_SIZE / 2;
                        vert_z = tile_y;
                        vert_y = (height_se + height_sw) >> 1;
                    }
                    else if( vertex_index == 3 )
                    {
                        vert_x = tile_x + TILE_SIZE;
                        vert_z = tile_y;
                        vert_y = height_se;
                    }
                    else if( vertex_index == 4 )
                    {
                        vert_x = tile_x + TILE_SIZE;
                        vert_z = tile_y + TILE_SIZE / 2;
                        vert_y = (height_ne + height_se) >> 1;
                    }
                    else if( vertex_index == 5 )
                    {
                        vert_x = tile_x + TILE_SIZE;
                        vert_z = tile_y + TILE_SIZE;
                        vert_y = height_ne;
                    }
                    else if( vertex_index == 6 )
                    {
                        vert_x = tile_x + TILE_SIZE / 2;
                        vert_z = tile_y + TILE_SIZE;
                        vert_y = (height_ne + height_nw) >> 1;
                    }
                    else if( vertex_index == 7 )
                    {
                        vert_x = tile_x;
                        vert_z = tile_y + TILE_SIZE;
                        vert_y = height_nw;
                    }
                    else if( vertex_index == 8 )
                    {
                        vert_x = tile_x;
                        vert_z = tile_y + TILE_SIZE / 2;
                        vert_y = (height_nw + height_sw) >> 1;
                    }
                    else if( vertex_index == 9 )
                    {
                        vert_x = tile_x + TILE_SIZE / 2;
                        vert_z = tile_y + TILE_SIZE / 4;
                        vert_y = (height_sw + height_se) >> 1;
                    }
                    else if( vertex_index == 10 )
                    {
                        vert_x = tile_x + TILE_SIZE * 3 / 4;
                        vert_z = tile_y + TILE_SIZE / 2;
                        vert_y = (height_se + height_ne) >> 1;
                    }
                    else if( vertex_index == 11 )
                    {
                        vert_x = tile_x + TILE_SIZE / 2;
                        vert_z = tile_y + TILE_SIZE * 3 / 4;
                        vert_y = (height_ne + height_nw) >> 1;
                    }
                    else if( vertex_index == 12 )
                    {
                        vert_x = tile_x + TILE_SIZE / 4;
                        vert_z = tile_y + TILE_SIZE / 2;
                        vert_y = (height_nw + height_sw) >> 1;
                    }
                    else if( vertex_index == 13 )
                    {
                        vert_x = tile_x + TILE_SIZE / 4;
                        vert_z = tile_y + TILE_SIZE / 4;
                        vert_y = height_sw;
                    }
                    else if( vertex_index == 14 )
                    {
                        vert_x = tile_x + TILE_SIZE * 3 / 4;
                        vert_z = tile_y + TILE_SIZE / 4;
                        vert_y = height_se;
                    }
                    else if( vertex_index == 15 )
                    {
                        vert_x = tile_x + TILE_SIZE * 3 / 4;
                        vert_z = tile_y + TILE_SIZE * 3 / 4;
                        vert_y = height_ne;
                    }
                    else
                    {
                        vert_x = tile_x + TILE_SIZE / 4;
                        vert_z = tile_y + TILE_SIZE * 3 / 4;
                        vert_y = height_nw;
                    }

                    vertex_x[i] = vert_x;
                    vertex_y[i] = vert_y;
                    vertex_z[i] = vert_z;
                }

                //     this.facesA = new Int32Array(faceCount);
                // this.facesB = new Int32Array(faceCount);
                // this.facesC = new Int32Array(faceCount);

                // this.faceColorsA = new Int32Array(faceCount);
                // this.faceColorsB = new Int32Array(faceCount);
                // this.faceColorsC = new Int32Array(faceCount);

                // for (let i = 0; i < faceCount; i++) {
                //     const isOverlay = tileFaces[tileFaceIndex++] === 1;
                //     let a = tileFaces[tileFaceIndex++];
                //     let b = tileFaces[tileFaceIndex++];
                //     let c = tileFaces[tileFaceIndex++];

                //     if (a < 4) {
                //         a = (a - rotation) & 3;
                //     }

                //     if (b < 4) {
                //         b = (b - rotation) & 3;
                //     }

                //     if (c < 4) {
                //         c = (c - rotation) & 3;
                //     }

                //     this.facesA[i] = a;
                //     this.facesB[i] = b;
                //     this.facesC[i] = c;
                //     let faceTextureId = -1;
                //     let hslA = 0;
                //     let hslB = 0;
                //     let hslC = 0;
                //     let minimapHslA = 0;
                //     let minimapHslB = 0;
                //     let minimapHslC = 0;
                //     if (isOverlay) {
                //         hslA = overlayHsls[a];
                //         hslB = overlayHsls[b];
                //         hslC = overlayHsls[c];
                //         minimapHslA = overlayMinimapHsls[a];
                //         minimapHslB = overlayMinimapHsls[b];
                //         minimapHslC = overlayMinimapHsls[c];
                //         faceTextureId = textureId;
                //         if (this.faceTextures) {
                //             this.faceTextures[i] = textureId;
                //         }
                //     } else {
                //         hslA = underlayHsls[a];
                //         hslB = underlayHsls[b];
                //         hslC = underlayHsls[c];
                //         minimapHslA = underlayMinimapHsls[a];
                //         minimapHslB = underlayMinimapHsls[b];
                //         minimapHslC = underlayMinimapHsls[c];
                //         if (this.faceTextures) {
                //             this.faceTextures[i] = -1;
                //         }
                //     }

                //     this.faceColorsA[i] = hslA;
                //     this.faceColorsB[i] = hslB;
                //     this.faceColorsC[i] = hslC;

                //     this.minimapFaceColorsA[i] = minimapHslA;
                //     this.minimapFaceColorsB[i] = minimapHslB;
                //     this.minimapFaceColorsC[i] = minimapHslC;

                //     if (hslA === INVALID_HSL_COLOR && faceTextureId === -1) {
                //         continue;
                //     }

                //     const u0 = (this.vertexX[a] - tileX) / TILE_SIZE;
                //     const v0 = (this.vertexZ[a] - tileY) / TILE_SIZE;

                //     const u1 = (this.vertexX[b] - tileX) / TILE_SIZE;
                //     const v1 = (this.vertexZ[b] - tileY) / TILE_SIZE;

                //     const u2 = (this.vertexX[c] - tileX) / TILE_SIZE;
                //     const v2 = (this.vertexZ[c] - tileY) / TILE_SIZE;

                //     this.faces.push({
                //         vertices: [
                //             {
                //                 x: this.vertexX[a],
                //                 y: this.vertexY[a],
                //                 z: this.vertexZ[a],
                //                 hsl: hslA,
                //                 u: u0,
                //                 v: v0,
                //                 textureId: faceTextureId,
                //             },
                //             {
                //                 x: this.vertexX[b],
                //                 y: this.vertexY[b],
                //                 z: this.vertexZ[b],
                //                 hsl: hslB,
                //                 u: u1,
                //                 v: v1,
                //                 textureId: faceTextureId,
                //             },
                //             {
                //                 x: this.vertexX[c],
                //                 y: this.vertexY[c],
                //                 z: this.vertexZ[c],
                //                 hsl: hslC,
                //                 u: u2,
                //                 v: v2,
                //                 textureId: faceTextureId,
                //             },
                //         ],
                //     });
                // }

                int face_count = tile_shape_face_counts[shape] / 4;
                int* faces_a = malloc(face_count * sizeof(int));
                int* faces_b = malloc(face_count * sizeof(int));
                int* faces_c = malloc(face_count * sizeof(int));
                int* face_colors_a = malloc(face_count * sizeof(int));
                int* face_colors_b = malloc(face_count * sizeof(int));
                int* face_colors_c = malloc(face_count * sizeof(int));

                for( int i = 0; i < face_count; i++ )
                {
                    int a = faces[i * 3];
                    int b = faces[i * 3 + 1];
                    int c = faces[i * 3 + 2];

                    faces_a[i] = a;
                    faces_b[i] = b;
                    faces_c[i] = c;

                    // Skip invalid faces
                    if( face_colors_a[i] == -1 )
                    {
                        continue;
                    }

                    face_colors_a[i] = 0x0020;
                    face_colors_b[i] = 0x0020;
                    face_colors_c[i] = 0x0020;

                    // Calculate texture coordinates
                    // float u0 = (float)(vertex_x[a] - tile_x) / TILE_SIZE;
                    // float v0 = (float)(vertex_z[a] - tile_y) / TILE_SIZE;

                    // float u1 = (float)(vertex_x[b] - tile_x) / TILE_SIZE;
                    // float v1 = (float)(vertex_z[b] - tile_y) / TILE_SIZE;

                    // float u2 = (float)(vertex_x[c] - tile_x) / TILE_SIZE;
                    // float v2 = (float)(vertex_z[c] - tile_y) / TILE_SIZE;
                }

                int* screen_vertices_x = (int*)malloc(vertex_count * sizeof(int));
                int* screen_vertices_y = (int*)malloc(vertex_count * sizeof(int));
                int* screen_vertices_z = (int*)malloc(vertex_count * sizeof(int));

                project_vertices(
                    screen_vertices_x,
                    screen_vertices_y,
                    screen_vertices_z,
                    vertex_count,
                    vertex_x,
                    vertex_y,
                    vertex_z,
                    0,
                    0,
                    0,
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

                for( int face = 0; face < face_count; face++ )
                {
                    raster_osrs_single(
                        pixel_buffer,
                        face,
                        faces_a,
                        faces_b,
                        faces_c,
                        screen_vertices_x,
                        screen_vertices_y,
                        screen_vertices_z,
                        face_colors_a,
                        face_colors_b,
                        face_colors_c,
                        0,
                        0,
                        width,
                        height);
                }

                free(vertex_x);
                free(vertex_y);
                free(vertex_z);
                free(faces_a);
                free(faces_b);
                free(faces_c);
                free(face_colors_a);
                free(face_colors_b);
                free(face_colors_c);
                free(screen_vertices_x);
                free(screen_vertices_y);
                free(screen_vertices_z);
            }
        }
    }
}

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
    for( int i = 0; i < tile_count; i++ )
    {
        struct SceneTile* tile = &tiles[i];

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
            scene_y,
            scene_z,
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
                tile->face_color_hsl,
                tile->face_color_hsl,
                tile->face_color_hsl,
                0,
                0,
                width,
                height);
        }
    }

    free(screen_vertices_x);
    free(screen_vertices_y);
    free(screen_vertices_z);
}