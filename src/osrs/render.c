#include "osrs/render.h"

#include "anim.h"
#include "flat.h"
#include "frustrum_cullmap.h"
#include "gouraud.h"
#include "gouraud_deob.h"
#include "lighting.h"
#include "palette.h"
#include "projection.h"
#include "scene.h"
#include "scene_tile.h"
#include "shared_tables.h"
#include "tables/maps.h"
#include "tables/model.h"
#include "tables/sprites.h"
#include "texture.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tmp_depth_face_count[1500] = { 0 };
static int tmp_depth_faces[1500 * 512] = { 0 };
static int tmp_priority_face_count[12] = { 0 };
static int tmp_priority_depth_sum[12] = { 0 };
static int tmp_priority_faces[12 * 2000] = { 0 };
static int tmp_flex_prio11_face_to_depth[1024] = { 0 };
static int tmp_flex_prio12_face_to_depth[512] = { 0 };
static int tmp_face_order[1024] = { 0 };

static int tmp_screen_vertices_x[4096] = { 0 };
static int tmp_screen_vertices_y[4096] = { 0 };
static int tmp_screen_vertices_z[4096] = { 0 };

static int tmp_orthographic_vertices_x[4096] = { 0 };
static int tmp_orthographic_vertices_y[4096] = { 0 };
static int tmp_orthographic_vertices_z[4096] = { 0 };

static int tmp_face_colors_a_hsl16[4096] = { 0 };
static int tmp_face_colors_b_hsl16[4096] = { 0 };
static int tmp_face_colors_c_hsl16[4096] = { 0 };

static int tmp_vertex_normals[4096] = { 0 };

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
    int model_pitch,
    int model_yaw,
    int model_roll,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_pitch,
    int camera_yaw,
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
        near_plane_z,
        screen_width,
        screen_height);

    // int a = (scene_z * cos_camera_yaw - scene_x * sin_camera_yaw) >> 16;
    // // b is the z projection of the models origin (imagine a vertex at x=0,y=0 and z=0).
    // // So the depth is the z projection distance from the origin of the model.
    // int b = (scene_y * sin_camera_pitch + a * cos_camera_pitch) >> 16;

    int model_origin_z_projection = projected_triangle.z;

    if( projected_triangle.clipped )
    {
        for( int i = 0; i < num_vertices; i++ )
        {
            screen_vertices_x[i] = -5000;
            screen_vertices_y[i] = -5000;
            screen_vertices_z[i] = -5000;
        }
        return;
    }

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
            near_plane_z,
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

static int
project_vertices_textured(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int num_vertices,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_pitch,
    int camera_yaw,
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

    project_fast(
        &projected_triangle,
        0,
        0,
        0,
        model_yaw,
        scene_x,
        scene_y,
        scene_z,
        camera_pitch,
        camera_yaw,
        camera_fov,
        near_plane_z,
        screen_width,
        screen_height);

    // int a = (scene_z * cos_camera_yaw - scene_x * sin_camera_yaw) >> 16;
    // // b is the z projection of the models origin (imagine a vertex at x=0,y=0 and z=0).
    // // So the depth is the z projection distance from the origin of the model.
    // int b = (scene_y * sin_camera_pitch + a * cos_camera_pitch) >> 16;

    int model_origin_z_projection = projected_triangle.z;

    if( projected_triangle.clipped )
    {
        return 0;
    }

    /**
     * These checks are a significant performance improvement.
     */
    // if( projected_triangle.z > 3500 )
    // {
    //     return 0;
    // }

    int width = (screen_width >> 1) + (screen_width >> 2);
    int height = (screen_height >> 1) + (screen_height >> 2);

    if( projected_triangle.x < -width || projected_triangle.x > width )
    {
        return 0;
    }

    if( projected_triangle.y < -height || projected_triangle.y > height )
    {
        return 0;
    }

    for( int i = 0; i < num_vertices; i++ )
    {
        project_orthographic_fast(
            &projected_triangle,
            vertex_x[i],
            vertex_y[i],
            vertex_z[i],
            model_yaw,
            scene_x,
            scene_y,
            scene_z,
            camera_pitch,
            camera_yaw);

        orthographic_vertices_x[i] = projected_triangle.x;
        orthographic_vertices_y[i] = projected_triangle.y;
        orthographic_vertices_z[i] = projected_triangle.z;

        project_perspective_fast(
            &projected_triangle,
            orthographic_vertices_x[i],
            orthographic_vertices_y[i],
            orthographic_vertices_z[i],
            // camera_fov,
            near_plane_z);

        // If vertex is too close to camera, set it to a large negative value
        // This will cause it to be clipped in the rasterization step
        if( projected_triangle.clipped )
        {
            screen_vertices_x[i] = -5000;
            // screen_vertices_y[i] = -5000;
            // screen_vertices_z[i] = -5000;
        }
        else
        {
            screen_vertices_x[i] = projected_triangle.x;
            screen_vertices_y[i] = projected_triangle.y;
            screen_vertices_z[i] = projected_triangle.z - model_origin_z_projection;
        }
    }

    return 1;
}

/**
 * Terrain is treated as a single, so the origin test does not apply.
 */
static bool
project_vertices_terrain_textured(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int num_vertices,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int camera_x,
    int camera_y,
    int camera_z,
    int camera_pitch,
    int camera_yaw,
    int camera_roll,
    int camera_fov,
    int near_plane_z,
    int screen_width,
    int screen_height)
{
    struct ProjectedTriangle projected_triangle;

    assert(camera_pitch >= 0 && camera_pitch < 2048);
    assert(camera_yaw >= 0 && camera_yaw < 2048);
    assert(camera_roll >= 0 && camera_roll < 2048);

    for( int i = 0; i < num_vertices; i++ )
    {
        projected_triangle = project_orthographic(
            vertex_x[i],
            vertex_y[i],
            vertex_z[i],
            model_pitch,
            model_yaw,
            model_roll,
            -camera_x,
            // Camera z is the y axis in OSRS
            -camera_z,
            -camera_y,
            camera_pitch,
            camera_yaw,
            camera_roll);

        assert(projected_triangle.clipped == 0);

        orthographic_vertices_x[i] = projected_triangle.x;
        orthographic_vertices_y[i] = projected_triangle.y;
        orthographic_vertices_z[i] = projected_triangle.z;

        projected_triangle = project_perspective(
            orthographic_vertices_x[i],
            orthographic_vertices_y[i],
            orthographic_vertices_z[i],
            camera_fov,
            near_plane_z);

        if( projected_triangle.clipped )
        {
            // Since terrain vertexes are calculated as a single mesh rather,
            // than around some origin, assume that if any vertex is clipped,
            // then the entire terrain is clipped.
            screen_vertices_x[i] = -5000;
            screen_vertices_y[i] = -9999;
            screen_vertices_z[i] = projected_triangle.z;
        }
        else
        {
            screen_vertices_x[i] = projected_triangle.x;
            if( screen_vertices_x[i] == -5000 )
                screen_vertices_x[i] = -5001;
            screen_vertices_y[i] = projected_triangle.y;
            screen_vertices_z[i] = projected_triangle.z;
        }
    }

    return true;

clipped:
    return false;
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
    /**
     * TODO: Priority 11 and 12 are flexible priorities.
     *
     * Some examples are "arms", "capes", etc. For example, we want arms
     * to be drawn "above" the body, but if the arm is on the far side,
     * then it should be drawn "below" the body.
     *
     */
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

/**
 * Priorities 0-10 faces are always drawn in their relative order.
 *
 * Priorities 11, 12 are also always drawn in their relative order,
 * however, the faces are inserted in the 0-10 ordering based on
 * average depth of the nearby priorities. The faces of 11 and 12
 * are distributed among these priorities, for example, some of the
 * prio 11 faces may be inserted before prio 0, and some may be inserted
 * before prio 3. I.e. Priority 11 and 12 faces are not always inserted
 * at the same prio.
 *
 * For priorities 11 and twelve, we need a reverse mapping of face => depth,
 * face_depth_buckets maps depth[depth_i][n] => face_y.
 *
 * Possible insertion points before: 0, 3, 5, or after all other prios.
 */
static int
sort_face_draw_order(
    int* face_draw_order,
    int* face_depth_buckets,
    int* face_depth_bucket_counts,
    int* face_priority_buckets,
    int* face_priority_bucket_counts,
    int num_faces,
    int* face_priorities,
    int depth_upper_bound)
{
    int* priority_depths = tmp_priority_depth_sum;
    int* flex_prio11_face_to_depth = tmp_flex_prio11_face_to_depth;
    int* flex_prio12_face_to_depth = tmp_flex_prio12_face_to_depth;

    int counts[12] = { 0 };
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

            int face_count = counts[prio];

            if( prio < 10 )
            {
                priority_depths[prio] += depth;
            }
            else if( prio == 10 )
            {
                // Hack so we don't have to st
                flex_prio11_face_to_depth[face_count] = depth | (face_idx << 16);
            }
            else if( prio == 11 )
            {
                flex_prio12_face_to_depth[face_count] = depth | (face_idx << 16);
            }

            counts[prio]++;
        }
    }

    int average_depth1_2 = 0;
    int count1_2 = counts[1] + counts[2];
    if( count1_2 > 0 )
        average_depth1_2 = (priority_depths[1] + priority_depths[2]) / count1_2;
    int average_depth3_4 = 0;
    int count3_4 = counts[3] + counts[4];
    if( count3_4 > 0 )
        average_depth3_4 = (priority_depths[3] + priority_depths[4]) / count3_4;
    int average_depth6_8 = 0;
    int count6_8 = counts[6] + counts[8];
    if( count6_8 > 0 )
        average_depth6_8 = (priority_depths[6] + priority_depths[8]) / count6_8;

    // Concat the flexible faces
    for( int i = 0; i < counts[11]; i++ )
    {
        flex_prio11_face_to_depth[counts[10] + i] = flex_prio12_face_to_depth[i];
    }
    counts[10] += counts[11];

    int flexible_face_index = 0;
    int order_index = 0;

    // Insert flexible faces before 0 if the flex faces are farther away than the average prio 1-2
    // faces.
    while( flexible_face_index < counts[10] &&
           (flex_prio11_face_to_depth[flexible_face_index] & 0xFFFF) > average_depth1_2 )
    {
        face_draw_order[order_index++] = flex_prio11_face_to_depth[flexible_face_index] >> 16;
        flexible_face_index++;
    }

    for( int prio = 0; prio < 3; prio++ )
    {
        for( int i = 0; i < counts[prio]; i++ )
        {
            face_draw_order[order_index++] = face_priority_buckets[prio * 2000 + i];
        }
    }

    // Insert flexible faces before 3 if the flex faces are farther away than the average prio 3-4
    // faces.
    while( flexible_face_index < counts[10] &&
           (flex_prio11_face_to_depth[flexible_face_index] & 0xFFFF) > average_depth3_4 )
    {
        face_draw_order[order_index++] = flex_prio11_face_to_depth[flexible_face_index] >> 16;
        flexible_face_index++;
    }

    for( int prio = 3; prio < 5; prio++ )
    {
        for( int i = 0; i < counts[prio]; i++ )
        {
            face_draw_order[order_index++] = face_priority_buckets[prio * 2000 + i];
        }
    }

    // Insert flexible faces before 6 if the flex faces are farther away than the average prio 6 and
    // 8 faces.
    while( flexible_face_index < counts[10] &&
           (flex_prio11_face_to_depth[flexible_face_index] & 0xFFFF) > average_depth6_8 )
    {
        face_draw_order[order_index++] = flex_prio11_face_to_depth[flexible_face_index] >> 16;
        flexible_face_index++;
    }

    for( int prio = 5; prio < 10; prio++ )
    {
        for( int i = 0; i < counts[prio]; i++ )
        {
            face_draw_order[order_index++] = face_priority_buckets[prio * 2000 + i];
        }
    }

    // Draw any remaining flexible faces.
    while( flexible_face_index < counts[10] )
    {
        face_draw_order[order_index++] = flex_prio11_face_to_depth[flexible_face_index] >> 16;
        flexible_face_index++;
    }

    return order_index;
}

void
raster_osrs(
    int* pixel_buffer,
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
    int* face_alphas,
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
            if( x1 == -5000 || x2 == -5000 || x3 == -5000 )
                continue;

            int color_a = colors_a[index];
            int color_b = colors_b[index];
            int color_c = colors_c[index];

            if( color_a > 65535 )
            {
                color_a = 0xF123;
            }
            if( color_b > 65535 )
            {
                color_b = 0xF123;
            }
            if( color_c > 65535 )
            {
                color_c = 0xF123;
            }

            int alpha = face_alphas[index];

            if( alpha == 0xFF )
            {
                raster_gouraud_s4(
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
            else
            {
                raster_gouraud_blend_s4(
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
                    color_c,
                    alpha);
            }
        }
    }
}

enum FaceType
{
    FACE_TYPE_GOURAUD,
    FACE_TYPE_FLAT,
    FACE_TYPE_TEXTURED,
    FACE_TYPE_TEXTURED_FLAT_SHADE,
};

/**
 * Communicates how to take the face color and create colors_a, colors_b, colors_c.
 *
 * If a FACE is textured, the colors_a, colors_b, colors_c are lightness values 0-127
 */
// Face infos contain this
enum FaceBlend
{
    FACE_BLEND_GRADIENT = 0,
    FACE_BLEND_FLAT = 1,

    // Alpha -1 or face_info == 2
    FACE_BLEND_HIDDEN = 2,

    // Alpha -2 - as far as I can tell face_info is never 3
    FACE_BLEND_BLACK = 3,
};

int g_empty_texture_texels[128 * 128] = { 0 };

void
model_draw_face(
    int* pixel_buffer,
    int face_index,
    int* face_infos,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int num_faces,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int* orthographic_vertex_x_nullable,
    int* orthographic_vertex_y_nullable,
    int* orthographic_vertex_z_nullable,
    int num_vertices,
    int* face_textures,
    int* face_texture_coords,
    int face_texture_coords_length,
    int* face_p_coordinate_nullable,
    int* face_m_coordinate_nullable,
    int* face_n_coordinate_nullable,
    int num_textured_faces,
    int* colors_a,
    int* colors_b,
    int* colors_c,
    int* face_alphas_nullable,
    int offset_x,
    int offset_y,
    int screen_width,
    int screen_height,
    struct TexturesCache* textures_cache)
{
    struct Texture* texture = NULL;

    // TODO: FaceTYpe is wrong, type 2 is hidden, 3 is black, 0 is gouraud, 1 is flat.
    enum FaceType type = face_infos ? (face_infos[face_index] & 0x3) : FACE_TYPE_GOURAUD;
    if( type == 2 )
    {
        return;
    }
    assert(type >= 0 && type <= 3);

    int color_a = colors_a[face_index];
    int color_b = colors_b[face_index];
    int color_c = colors_c[face_index];

    if( color_c == -2 )
    {
        return;
    }

    int tp_face = -1;
    int tm_face = -1;
    int tn_face = -1;

    int tp_x = -1;
    int tp_y = -1;
    int tp_z = -1;
    int tm_x = -1;
    int tm_y = -1;
    int tm_z = -1;
    int tn_x = -1;
    int tn_y = -1;
    int tn_z = -1;

    int texture_id = -1;
    int texture_face = -1;

    int index = face_index;

    assert(index < num_faces);

    int face_a = face_indices_a[index];
    int face_b = face_indices_b[index];
    int face_c = face_indices_c[index];

    int x1 = vertex_x[face_a];
    int x2 = vertex_x[face_b];
    int x3 = vertex_x[face_c];

    // Skip triangle if any vertex was clipped
    if( x1 == -5000 || x2 == -5000 || x3 == -5000 )
        return;

    int y1 = vertex_y[face_a];
    int y2 = vertex_y[face_b];
    int y3 = vertex_y[face_c];
    int z1 = vertex_z[face_a];
    int z2 = vertex_z[face_b];
    int z3 = vertex_z[face_c];

    x1 += offset_x;
    y1 += offset_y;
    x2 += offset_x;
    y2 += offset_y;
    x3 += offset_x;
    y3 += offset_y;

    assert(offset_y == (screen_height >> 1));
    assert(offset_x == (screen_width >> 1));

    int alpha = face_alphas_nullable ? (face_alphas_nullable[index]) : 0xFF;

    // Faces with color_c == -2 are not drawn. As far as I can tell, these faces are used for
    // texture PNM coordinates that do not coincide with a visible face.
    // /Users/matthewevers/Documents/git_repos/OS1/src/main/java/jagex3/dash3d/ModelUnlit.java
    // OS1 skips faces with -2.
    if( color_c == -2 )
    {
        // TODO: How to organize this.
        // See here
        // /Users/matthewevers/Documents/git_repos/rs-map-viewer/src/rs/model/ModelData.ts
        // .light

        // and
        // /Users/matthewevers/Documents/git_repos/rs-map-viewer/src/mapviewer/webgl/buffer/SceneBuffer.ts
        // getModelFaces
        return;
        // color_c = 0;
    }

    // TODO: See above comments. alpha overrides colors.
    // if( face_alphas_nullable && face_alphas_nullable[index] < 0 )
    // {
    //     return;
    // }

    int* texels = g_empty_texture_texels;
    if( face_textures && face_textures[index] != -1 )
    {
        texture_id = face_textures[index];
        // gamma 0.8 is the default in os1
        texture = textures_cache_checkout(textures_cache, NULL, texture_id, 128, 0.8);
        assert(texture != NULL);
        texels = texture->texels;

        if( color_c == -1 )
            goto textured_flat;
        else
            goto textured;
    }
    else
    {
        // Alpha is a signed byte, but for non-textured
        // faces, we treat it as unsigned.
        // -1 and -2 are reserved. See lighting code.
        if( face_alphas_nullable )
            alpha = 0xFF - (alpha & 0xff);

        if( color_c == -1 )
        {
            type = FACE_TYPE_FLAT;
        }
        else
        {
            type = FACE_TYPE_GOURAUD;
        }
        switch( type )
        {
        case FACE_TYPE_GOURAUD:
            assert(face_a < num_vertices);
            assert(face_b < num_vertices);
            assert(face_c < num_vertices);
            if( alpha == 0xFF )
            {
                raster_gouraud_s4(
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
            else
            {
                raster_gouraud_blend_s4(
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
                    color_c,
                    alpha);
            }
            break;
        case FACE_TYPE_FLAT:
            if( alpha == 0xFF )
            {
                raster_flat(
                    pixel_buffer, screen_width, screen_height, x1, x2, x3, y1, y2, y3, color_a);
            }
            else
            {
                raster_flat_alpha_step4(
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
                    alpha);
            }

            break;
        case FACE_TYPE_TEXTURED:
        textured:;
            assert(face_p_coordinate_nullable != NULL);
            assert(face_m_coordinate_nullable != NULL);
            assert(face_n_coordinate_nullable != NULL);
            assert(orthographic_vertex_x_nullable != NULL);
            assert(orthographic_vertex_y_nullable != NULL);
            assert(orthographic_vertex_z_nullable != NULL);

            if( face_texture_coords && face_texture_coords[index] != -1 )
            {
                texture_face = face_texture_coords[index];

                tp_face = face_p_coordinate_nullable[texture_face];
                tm_face = face_m_coordinate_nullable[texture_face];
                tn_face = face_n_coordinate_nullable[texture_face];
            }
            else
            {
                texture_face = index;
                tp_face = face_indices_a[texture_face];
                tm_face = face_indices_b[texture_face];
                tn_face = face_indices_c[texture_face];
            }
            // texture_id = face_textures[index];
            // texture_face = face_infos[index] >> 2;
            // texture_face = face_texture_coords[index];

            assert(tp_face > -1);
            assert(tm_face > -1);
            assert(tn_face > -1);

            assert(tp_face < num_vertices);
            assert(tm_face < num_vertices);
            assert(tn_face < num_vertices);

            tp_x = orthographic_vertex_x_nullable[tp_face];
            tp_y = orthographic_vertex_y_nullable[tp_face];
            tp_z = orthographic_vertex_z_nullable[tp_face];

            tm_x = orthographic_vertex_x_nullable[tm_face];
            tm_y = orthographic_vertex_y_nullable[tm_face];
            tm_z = orthographic_vertex_z_nullable[tm_face];

            tn_x = orthographic_vertex_x_nullable[tn_face];
            tn_y = orthographic_vertex_y_nullable[tn_face];
            tn_z = orthographic_vertex_z_nullable[tn_face];

            if( texture->opaque )
            {
                raster_texture_opaque_blend_lerp8(
                    pixel_buffer,
                    screen_width,
                    screen_height,
                    x1,
                    x2,
                    x3,
                    y1,
                    y2,
                    y3,
                    z1,
                    z2,
                    z3,
                    tp_x,
                    tm_x,
                    tn_x,
                    tp_y,
                    tm_y,
                    tn_y,
                    tp_z,
                    tm_z,
                    tn_z,
                    color_a,
                    color_b,
                    color_c,
                    texels,
                    128);
            }
            else
            {
                raster_texture_transparent_blend_lerp8(
                    pixel_buffer,
                    screen_width,
                    screen_height,
                    x1,
                    x2,
                    x3,
                    y1,
                    y2,
                    y3,
                    z1,
                    z2,
                    z3,
                    tp_x,
                    tm_x,
                    tn_x,
                    tp_y,
                    tm_y,
                    tn_y,
                    tp_z,
                    tm_z,
                    tn_z,
                    color_a,
                    color_b,
                    color_c,
                    texels,
                    128);
            }

            // This will draw a white triangle over the projected texture pnm coords.

            // raster_flat(
            //     pixel_buffer,
            //     screen_width,
            //     screen_height,
            //     tex_x1,
            //     tex_x2,
            //     tex_x3,
            //     tex_y1,
            //     tex_y2,
            //     tex_y3,
            //     color_a);
            break;
        case FACE_TYPE_TEXTURED_FLAT_SHADE:
        textured_flat:;
            assert(face_p_coordinate_nullable != NULL);
            assert(face_m_coordinate_nullable != NULL);
            assert(face_n_coordinate_nullable != NULL);
            assert(orthographic_vertex_x_nullable != NULL);
            assert(orthographic_vertex_y_nullable != NULL);
            assert(orthographic_vertex_z_nullable != NULL);

            if( face_texture_coords && face_texture_coords[index] != -1 )
            {
                texture_face = face_texture_coords[index];

                tp_face = face_p_coordinate_nullable[texture_face];
                tm_face = face_m_coordinate_nullable[texture_face];
                tn_face = face_n_coordinate_nullable[texture_face];
            }
            else
            {
                texture_face = index;
                tp_face = face_indices_a[texture_face];
                tm_face = face_indices_b[texture_face];
                tn_face = face_indices_c[texture_face];
            }
            // texture_id = face_textures[index];
            // texture_face = face_infos[index] >> 2;
            // texture_face = face_texture_coords[index];

            assert(tp_face > -1);
            assert(tm_face > -1);
            assert(tn_face > -1);

            assert(tp_face < num_vertices);
            assert(tm_face < num_vertices);
            assert(tn_face < num_vertices);

            tp_x = orthographic_vertex_x_nullable[tp_face];
            tp_y = orthographic_vertex_y_nullable[tp_face];
            tp_z = orthographic_vertex_z_nullable[tp_face];

            tm_x = orthographic_vertex_x_nullable[tm_face];
            tm_y = orthographic_vertex_y_nullable[tm_face];
            tm_z = orthographic_vertex_z_nullable[tm_face];

            tn_x = orthographic_vertex_x_nullable[tn_face];
            tn_y = orthographic_vertex_y_nullable[tn_face];
            tn_z = orthographic_vertex_z_nullable[tn_face];

            if( texture->opaque )
            {
                raster_texture_opaque_lerp8(
                    pixel_buffer,
                    screen_width,
                    screen_height,
                    x1,
                    x2,
                    x3,
                    y1,
                    y2,
                    y3,
                    z1,
                    z2,
                    z3,
                    tp_x,
                    tm_x,
                    tn_x,
                    tp_y,
                    tm_y,
                    tn_y,
                    tp_z,
                    tm_z,
                    tn_z,
                    color_a,
                    texels,
                    128);
            }
            else
            {
                raster_texture_transparent_lerp8(
                    pixel_buffer,
                    screen_width,
                    screen_height,
                    x1,
                    x2,
                    x3,
                    y1,
                    y2,
                    y3,
                    z1,
                    z2,
                    z3,
                    tp_x,
                    tm_x,
                    tn_x,
                    tp_y,
                    tm_y,
                    tn_y,
                    tp_z,
                    tm_z,
                    tn_z,
                    color_a,
                    texels,
                    128);
            }

            break;
        }
    }
}

void
raster_osrs_typed(
    int* pixel_buffer,
    int* priority_faces,
    int* priority_face_counts,
    int* face_infos,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int num_faces,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int* orthographic_vertex_x_nullable,
    int* orthographic_vertex_y_nullable,
    int* orthographic_vertex_z_nullable,
    int num_vertices,
    int* face_textures,
    int* face_texture_coords,
    int face_texture_coords_length,
    int* face_p_coordinate_nullable,
    int* face_m_coordinate_nullable,
    int* face_n_coordinate_nullable,
    int num_textured_faces,
    int* colors_a,
    int* colors_b,
    int* colors_c,
    int* face_alphas_nullable,
    int offset_x,
    int offset_y,
    int screen_width,
    int screen_height,
    struct TexturesCache* textures_cache)
{
    int face_index = 0;
    for( int prio = 0; prio < 12; prio++ )
    {
        int* triangle_indexes = &priority_faces[prio * 2000];
        int triangle_count = priority_face_counts[prio];

        for( int i = 0; i < triangle_count; i++ )
        {
            face_index = triangle_indexes[i];
            // Face 12

            model_draw_face(
                pixel_buffer,
                face_index,
                face_infos,
                face_indices_a,
                face_indices_b,
                face_indices_c,
                num_faces,
                vertex_x,
                vertex_y,
                vertex_z,
                orthographic_vertex_x_nullable,
                orthographic_vertex_y_nullable,
                orthographic_vertex_z_nullable,
                num_vertices,
                face_textures,
                face_texture_coords,
                face_texture_coords_length,
                face_p_coordinate_nullable,
                face_m_coordinate_nullable,
                face_n_coordinate_nullable,
                num_textured_faces,
                colors_a,
                colors_b,
                colors_c,
                face_alphas_nullable,
                offset_x,
                offset_y,
                screen_width,
                screen_height,
                textures_cache);
        }
    }
}

static long long g_clip_x[10] = { 0 };
static long long g_clip_y[10] = { 0 };
static long long g_clip_color[10] = { 0 };

static const int reciprocol_shift = 16;

static inline int
slopei(int near_plane_z, int za, int zb)
{
    assert(za - zb >= 0);
    return (za - near_plane_z) * g_reciprocal16[za - zb];
}

static inline int
lerp_planei(int near_plane_z, int lerp_slope, int pa, int pb)
{
    int lerp_p = pa + (((pb - pa) * lerp_slope) >> reciprocol_shift);

    return lerp_p;
}

static inline int
lerp_plane_projecti(int near_plane_z, int lerp_slope, int pa, int pb)
{
    int lerp_p = lerp_planei(near_plane_z, lerp_slope, pa, pb);

    return SCALE_UNIT(lerp_p) / near_plane_z;
}

static inline void
raster_osrs_single_gouraud_near_clip(
    int* pixel_buffer,
    int face,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* colors_a,
    int* colors_b,
    int* colors_c,
    int* face_alphas_nullable,
    int near_plane_z,
    int offset_x,
    int offset_y,
    int screen_width,
    int screen_height)
{
    int clipped_count = 0;
    int a = face_indices_a[face];
    int b = face_indices_b[face];
    int c = face_indices_c[face];

    int za = orthographic_vertices_z[a];
    int zb = orthographic_vertices_z[b];
    int zc = orthographic_vertices_z[c];

    int xa;
    int xb;
    int xc;
    int ya;
    int yb;
    int yc;
    int color_a;
    int color_b;
    int color_c;
    int lerp_slope;

    if( za >= near_plane_z )
    {
        g_clip_x[clipped_count] = screen_vertices_x[a];
        g_clip_y[clipped_count] = screen_vertices_y[a];
        assert(g_clip_x[clipped_count] != -5000);
        g_clip_color[clipped_count] = colors_a[face];
        clipped_count++;
    }
    else
    {
        xa = orthographic_vertices_x[a];
        ya = orthographic_vertices_y[a];
        color_a = colors_a[face];

        if( zc >= near_plane_z )
        {
            assert(zc - za >= 0);
            lerp_slope = slopei(near_plane_z, zc, za);

            g_clip_x[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_x[c], xa);
            g_clip_y[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_y[c], ya);
            g_clip_color[clipped_count] =
                lerp_planei(near_plane_z, lerp_slope, colors_c[face], color_a);
            clipped_count++;
        }

        if( zb >= near_plane_z )
        {
            assert(zb - za >= 0);
            lerp_slope = slopei(near_plane_z, zb, za);

            g_clip_x[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_x[b], xa);
            g_clip_y[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_y[b], ya);
            g_clip_color[clipped_count] =
                lerp_planei(near_plane_z, lerp_slope, colors_b[face], color_a);
            clipped_count++;
        }
    }
    if( zb >= near_plane_z )
    {
        g_clip_x[clipped_count] = screen_vertices_x[b];
        g_clip_y[clipped_count] = screen_vertices_y[b];
        assert(g_clip_x[clipped_count] != -5000);
        g_clip_color[clipped_count] = colors_b[face];
        clipped_count++;
    }
    else
    {
        xb = orthographic_vertices_x[b];
        yb = orthographic_vertices_y[b];
        color_b = colors_b[face];

        if( za >= near_plane_z )
        {
            lerp_slope = slopei(near_plane_z, za, zb);

            g_clip_x[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_x[a], xb);
            g_clip_y[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_y[a], yb);
            g_clip_color[clipped_count] =
                lerp_planei(near_plane_z, lerp_slope, colors_a[face], color_b);
            clipped_count++;
        }

        if( zc >= near_plane_z )
        {
            assert(zc - zb >= 0);
            lerp_slope = slopei(near_plane_z, zc, zb);

            // assert(0xFFFF0000 & ((zb - near_plane_z) * (orthographic_vertices_x[c] - xb)) == 0);

            g_clip_x[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_x[c], xb);
            g_clip_y[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_y[c], yb);
            g_clip_color[clipped_count] =
                lerp_planei(near_plane_z, lerp_slope, colors_c[face], color_b);
            clipped_count++;
        }
    }
    if( zc >= near_plane_z )
    {
        g_clip_x[clipped_count] = screen_vertices_x[c];
        g_clip_y[clipped_count] = screen_vertices_y[c];
        g_clip_color[clipped_count] = colors_c[face];
        assert(g_clip_x[clipped_count] != -5000);
        clipped_count++;
    }
    else
    {
        xc = orthographic_vertices_x[c];
        yc = orthographic_vertices_y[c];
        color_c = colors_c[face];

        if( zb >= near_plane_z )
        {
            assert(zb - zc >= 0);
            lerp_slope = slopei(near_plane_z, zb, zc);

            g_clip_x[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_x[b], xc);
            g_clip_y[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_y[b], yc);
            g_clip_color[clipped_count] =
                lerp_planei(near_plane_z, lerp_slope, colors_b[face], color_c);
            clipped_count++;
        }

        if( za >= near_plane_z )
        {
            assert(za - zc >= 0);
            lerp_slope = slopei(near_plane_z, za, zc);

            g_clip_x[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_x[a], xc);
            g_clip_y[clipped_count] =
                lerp_plane_projecti(near_plane_z, lerp_slope, orthographic_vertices_y[a], yc);
            g_clip_color[clipped_count] =
                lerp_planei(near_plane_z, lerp_slope, colors_a[face], color_c);
            clipped_count++;
        }
    }
    if( clipped_count < 3 )
        return;

    xa = g_clip_x[0];
    ya = g_clip_y[0];
    color_a = g_clip_color[0];
    xb = g_clip_x[1];
    yb = g_clip_y[1];
    color_b = g_clip_color[1];
    xc = g_clip_x[2];
    yc = g_clip_y[2];
    color_c = g_clip_color[2];

    assert(color_a >= 0 && color_a < 65536);
    assert(color_b >= 0 && color_b < 65536);
    assert(color_c >= 0 && color_c < 65536);

    xa += offset_x;
    ya += offset_y;
    xb += offset_x;
    yb += offset_y;
    xc += offset_x;
    yc += offset_y;

    raster_gouraud_s4(
        pixel_buffer,
        screen_width,
        screen_height,
        xa,
        xb,
        xc,
        ya,
        yb,
        yc,
        color_a,
        color_b,
        color_c);

    if( clipped_count != 4 )
        return;

    xb = g_clip_x[3];
    yb = g_clip_y[3];
    color_b = g_clip_color[3];

    xb += offset_x;
    yb += offset_y;

    raster_gouraud_s4(
        pixel_buffer,
        screen_width,
        screen_height,
        xa,
        xc,
        xb,
        ya,
        yc,
        yb,
        color_a,
        color_c,
        color_b);
}

static inline float
slopef(float near_plane_z, float za, float zb)
{
    return (za - near_plane_z) / (za - zb);
}

static inline float
lerp_planef(float near_plane_z, float lerp_slope, float pa, float pb)
{
    return pa + (((pb - pa) * lerp_slope));
}

static inline float
lerp_plane_projectf(float near_plane_z, float lerp_slope, float pa, float pb)
{
    float lerp_p = lerp_planef(near_plane_z, lerp_slope, pa, pb);

    return SCALE_UNIT(lerp_p) / near_plane_z;
}

static inline void
raster_osrs_single_gouraud_near_clipf(
    int* pixel_buffer,
    int face,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* colors_a,
    int* colors_b,
    int* colors_c,
    int* face_alphas_nullable,
    int near_plane_z,
    int offset_x,
    int offset_y,
    int screen_width,
    int screen_height)
{
    int clipped_count = 0;
    int a = face_indices_a[face];
    int b = face_indices_b[face];
    int c = face_indices_c[face];

    float za = orthographic_vertices_z[a];
    float zb = orthographic_vertices_z[b];
    float zc = orthographic_vertices_z[c];

    float xa;
    float xb;
    float xc;
    float ya;
    float yb;
    float yc;
    float color_a;
    float color_b;
    float color_c;
    float lerp_slope;

    if( za >= near_plane_z )
    {
        g_clip_x[clipped_count] = screen_vertices_x[a];
        g_clip_y[clipped_count] = screen_vertices_y[a];
        assert(g_clip_x[clipped_count] != -5000);
        g_clip_color[clipped_count] = colors_a[face];

        clipped_count++;
    }
    else
    {
        xa = orthographic_vertices_x[a];
        ya = orthographic_vertices_y[a];
        color_a = colors_a[face];

        if( zc >= near_plane_z )
        {
            assert(zc - za >= 0);
            lerp_slope = slopef(near_plane_z, zc, za);

            g_clip_x[clipped_count] =
                lerp_plane_projectf(near_plane_z, lerp_slope, orthographic_vertices_x[c], xa);

            g_clip_y[clipped_count] =
                lerp_plane_projectf(near_plane_z, lerp_slope, orthographic_vertices_y[c], ya);

            g_clip_color[clipped_count] =
                lerp_planef(near_plane_z, lerp_slope, colors_c[face], color_a);

            clipped_count++;
        }

        if( zb >= near_plane_z )
        {
            assert(zb - za >= 0);
            lerp_slope = slopef(near_plane_z, zb, za);

            g_clip_x[clipped_count] =
                lerp_plane_projectf(near_plane_z, lerp_slope, orthographic_vertices_x[b], xa);

            g_clip_y[clipped_count] =
                lerp_plane_projectf(near_plane_z, lerp_slope, orthographic_vertices_y[b], ya);

            g_clip_color[clipped_count] =
                lerp_planef(near_plane_z, lerp_slope, colors_b[face], color_a);

            clipped_count++;
        }
    }
    if( zb >= near_plane_z )
    {
        g_clip_x[clipped_count] = screen_vertices_x[b];
        g_clip_y[clipped_count] = screen_vertices_y[b];
        assert(g_clip_x[clipped_count] != -5000);
        g_clip_color[clipped_count] = colors_b[face];

        clipped_count++;
    }
    else
    {
        xb = orthographic_vertices_x[b];
        yb = orthographic_vertices_y[b];
        color_b = colors_b[face];

        if( za >= near_plane_z )
        {
            lerp_slope = slopef(near_plane_z, za, zb);

            g_clip_x[clipped_count] =
                lerp_plane_projectf(near_plane_z, lerp_slope, orthographic_vertices_x[a], xb);

            g_clip_y[clipped_count] =
                lerp_plane_projectf(near_plane_z, lerp_slope, orthographic_vertices_y[a], yb);

            g_clip_color[clipped_count] =
                lerp_planef(near_plane_z, lerp_slope, colors_a[face], color_b);

            clipped_count++;
        }

        if( zc >= near_plane_z )
        {
            assert(zc - zb >= 0);
            lerp_slope = slopef(near_plane_z, zc, zb);

            g_clip_x[clipped_count] =
                lerp_plane_projectf(near_plane_z, lerp_slope, orthographic_vertices_x[c], xb);

            g_clip_y[clipped_count] =
                lerp_plane_projectf(near_plane_z, lerp_slope, orthographic_vertices_y[c], yb);

            g_clip_color[clipped_count] =
                lerp_planef(near_plane_z, lerp_slope, colors_c[face], color_b);

            clipped_count++;
        }
    }
    if( zc >= near_plane_z )
    {
        g_clip_x[clipped_count] = screen_vertices_x[c];
        g_clip_y[clipped_count] = screen_vertices_y[c];
        g_clip_color[clipped_count] = colors_c[face];
        assert(g_clip_x[clipped_count] != -5000);

        clipped_count++;
    }
    else
    {
        xc = orthographic_vertices_x[c];
        yc = orthographic_vertices_y[c];
        color_c = colors_c[face];

        if( zb >= near_plane_z )
        {
            assert(zb - zc >= 0);
            lerp_slope = slopef(near_plane_z, zb, zc);

            g_clip_x[clipped_count] =
                lerp_plane_projectf(near_plane_z, lerp_slope, orthographic_vertices_x[b], xc);
            g_clip_y[clipped_count] =
                lerp_plane_projectf(near_plane_z, lerp_slope, orthographic_vertices_y[b], yc);

            g_clip_color[clipped_count] =
                lerp_planef(near_plane_z, lerp_slope, colors_b[face], color_c);

            clipped_count++;
        }

        if( za >= near_plane_z )
        {
            assert(za - zc >= 0);
            lerp_slope = slopef(near_plane_z, za, zc);

            g_clip_x[clipped_count] =
                lerp_plane_projectf(near_plane_z, lerp_slope, orthographic_vertices_x[a], xc);
            g_clip_y[clipped_count] =
                lerp_plane_projectf(near_plane_z, lerp_slope, orthographic_vertices_y[a], yc);
            g_clip_color[clipped_count] =
                lerp_planef(near_plane_z, lerp_slope, colors_a[face], color_c);

            clipped_count++;
        }
    }
    if( clipped_count < 3 )
        return;

    xa = g_clip_x[0];
    ya = g_clip_y[0];
    color_a = g_clip_color[0];
    xb = g_clip_x[1];
    yb = g_clip_y[1];
    color_b = g_clip_color[1];
    xc = g_clip_x[2];
    yc = g_clip_y[2];
    color_c = g_clip_color[2];

    assert(color_a >= 0 && color_a < 65536);
    assert(color_b >= 0 && color_b < 65536);
    assert(color_c >= 0 && color_c < 65536);

    xa += offset_x;
    ya += offset_y;
    xb += offset_x;
    yb += offset_y;
    xc += offset_x;
    yc += offset_y;

    raster_gouraud_s4(
        pixel_buffer,
        screen_width,
        screen_height,
        xa,
        xb,
        xc,
        ya,
        yb,
        yc,
        color_a,
        color_b,
        color_c);

    assert(clipped_count <= 4);
    if( clipped_count != 4 )
        return;

    // static int colors[4] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00 };

    // for( int i = 0; i < screen_height; i++ )
    // {
    //     for( int j = 0; j < 4; j++ )
    //     {
    //         int x = g_clip_x[j];

    //         x += offset_x;

    //         if( x > 0 && x < screen_width )
    //         {
    //             pixel_buffer[i * screen_width + ((int)x)] = colors[j];
    //         }
    //     }
    // }

    xb = g_clip_x[3];
    yb = g_clip_y[3];
    color_b = g_clip_color[3];

    xb += offset_x;
    yb += offset_y;

    raster_gouraud_s4(
        pixel_buffer,
        screen_width,
        screen_height,
        xa,
        xc,
        xb,
        ya,
        yc,
        yb,
        color_a,
        color_c,
        color_b);
}

static inline void
raster_osrs_single_gouraud(
    int* pixel_buffer,
    int face,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* colors_a,
    int* colors_b,
    int* colors_c,
    int* face_alphas_nullable,
    int near_plane_z,
    int offset_x,
    int offset_y,
    int screen_width,
    int screen_height)
{
    int index = face;

    int x1 = screen_vertices_x[face_indices_a[index]];
    int x2 = screen_vertices_x[face_indices_b[index]];
    int x3 = screen_vertices_x[face_indices_c[index]];

    // Skip triangle if any vertex was clipped
    // TODO: Perhaps use a separate buffer to track this.
    if( x1 == -5000 || x2 == -5000 || x3 == -5000 )
    {
        raster_osrs_single_gouraud_near_clip(
            pixel_buffer,
            face,
            face_indices_a,
            face_indices_b,
            face_indices_c,
            screen_vertices_x,
            screen_vertices_y,
            screen_vertices_z,
            orthographic_vertices_x,
            orthographic_vertices_y,
            orthographic_vertices_z,
            colors_a,
            colors_b,
            colors_c,
            face_alphas_nullable,
            near_plane_z,
            offset_x,
            offset_y,
            screen_width,
            screen_height);
        return;
    }

    int y1 = screen_vertices_y[face_indices_a[index]];
    int z1 = screen_vertices_z[face_indices_a[index]];

    int y2 = screen_vertices_y[face_indices_b[index]];
    int z2 = screen_vertices_z[face_indices_b[index]];

    int y3 = screen_vertices_y[face_indices_c[index]];
    int z3 = screen_vertices_z[face_indices_c[index]];

    x1 += offset_x;
    y1 += offset_y;
    x2 += offset_x;
    y2 += offset_y;
    x3 += offset_x;
    y3 += offset_y;

    int color_a = colors_a[index];
    int color_b = colors_b[index];
    int color_c = colors_c[index];
    int alpha = face_alphas_nullable ? face_alphas_nullable[index] : 0xFF;
    assert(color_a >= 0 && color_a < 65536);
    assert(color_b >= 0 && color_b < 65536);
    assert(color_c >= 0 && color_c < 65536);

    // drawGouraudTriangle(pixel_buffer, y1, y2, y3, x1, x2, x3, color_a, color_b, color_c);

    if( alpha == 0xFF )
    {
        raster_gouraud_s4(
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
    else
    {
        raster_gouraud_blend_s4(
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
            color_c,
            alpha);
    }
}

static inline bool
raster_osrs_single_texture(
    int* pixel_buffer,
    int width,
    int height,
    int face,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int* screen_vertex_x,
    int* screen_vertex_y,
    int* screen_vertex_z,
    int* orthographic_vertex_x,
    int* orthographic_vertex_y,
    int* orthographic_vertex_z,
    int* face_texture_ids,
    int* face_texture_u_a,
    int* face_texture_v_a,
    int* face_texture_u_b,
    int* face_texture_v_b,
    int* face_texture_u_c,
    int* face_texture_v_c,
    int* colors_a,
    int* colors_b,
    int* colors_c,
    struct TexturesCache* textures_cache,
    int near_plane_z,
    int offset_x,
    int offset_y)
{
    struct Texture* texture = NULL;
    int index = face;

    int x1 = screen_vertex_x[face_indices_a[index]];
    int y1 = screen_vertex_y[face_indices_a[index]];
    int z1 = screen_vertex_z[face_indices_a[index]];
    int x2 = screen_vertex_x[face_indices_b[index]];
    int y2 = screen_vertex_y[face_indices_b[index]];
    int z2 = screen_vertex_z[face_indices_b[index]];
    int x3 = screen_vertex_x[face_indices_c[index]];
    int y3 = screen_vertex_y[face_indices_c[index]];
    int z3 = screen_vertex_z[face_indices_c[index]];
    // // sw
    int ortho_x0 = orthographic_vertex_x[0];
    int ortho_y0 = orthographic_vertex_y[0];
    int ortho_z0 = orthographic_vertex_z[0];
    // se
    int ortho_x1 = orthographic_vertex_x[1];
    int ortho_y1 = orthographic_vertex_y[1];
    int ortho_z1 = orthographic_vertex_z[1];
    // ne
    int ortho_x2 = orthographic_vertex_x[2];
    int ortho_y2 = orthographic_vertex_y[2];
    int ortho_z2 = orthographic_vertex_z[2];
    // nw
    int ortho_x3 = orthographic_vertex_x[3];
    int ortho_y3 = orthographic_vertex_y[3];
    int ortho_z3 = orthographic_vertex_z[3];

    int shade_a = colors_a[index];
    int shade_b = colors_b[index];
    int shade_c = colors_c[index];

    assert(shade_a >= 0 && shade_a < 0xFF);
    assert(shade_b >= 0 && shade_b < 0xFF);
    assert(shade_c >= 0 && shade_c < 0xFF);

    // Skip triangle if any vertex was clipped
    // TODO: Perhaps use a separate buffer to track this.
    if( x1 == -5000 || x2 == -5000 || x3 == -5000 )
        return false;

    x1 += offset_x;
    y1 += offset_y;
    x2 += offset_x;
    y2 += offset_y;
    x3 += offset_x;
    y3 += offset_y;

    int texture_id = face_texture_ids[index];
    if( texture_id == -1 )
        return false;

    texture = textures_cache_checkout(textures_cache, NULL, texture_id, 128, 0.8);
    if( !texture )
        return false;

    if( texture->opaque )
    {
        raster_texture_opaque_blend_lerp8(
            pixel_buffer,
            width,
            height,
            x1,
            x2,
            x3,
            y1,
            y2,
            y3,
            z1,
            z2,
            z3,
            ortho_x0,
            ortho_x1,
            ortho_x3,
            ortho_y0,
            ortho_y1,
            ortho_y3,
            ortho_z0,
            ortho_z1,
            ortho_z3,
            shade_a,
            shade_b,
            shade_c,
            texture->texels,
            128);
    }
    else
    {
        raster_texture_transparent_blend_lerp8(
            pixel_buffer,
            width,
            height,
            x1,
            x2,
            x3,
            y1,
            y2,
            y3,
            z1,
            z2,
            z3,
            ortho_x0,
            ortho_x1,
            ortho_x3,
            ortho_y0,
            ortho_y1,
            ortho_y3,
            ortho_z0,
            ortho_z1,
            ortho_z3,
            shade_a,
            shade_b,
            shade_c,
            texture->texels,
            128);
    }
    // raster_texture_step_blend(
    //     pixel_buffer,
    //     width,
    //     height,
    //     x1,
    //     x2,
    //     x3,
    //     y1,
    //     y2,
    //     y3,
    //     z1,
    //     z2,
    //     z3,
    //     ortho_x0,
    //     ortho_x1,
    //     ortho_x3,
    //     ortho_y0,
    //     ortho_y1,
    //     ortho_y3,
    //     ortho_z0,
    //     ortho_z1,
    //     ortho_z3,
    //     shade_a,
    //     shade_b,
    //     shade_c,
    //     texture->texels,
    //     128,
    //     texture->opaque);

    return true;
}

void
render_model_frame(
    int* pixel_buffer,
    int width,
    int height,
    int near_plane_z,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int camera_x,
    int camera_y,
    int camera_z,
    int camera_pitch,
    int camera_yaw,
    int camera_roll,
    int fov,
    struct CacheModel* model,
    struct ModelLighting* lighting,
    struct BoundsCylinder* bounds_cylinder,
    struct CacheModelBones* bones_nullable,
    struct Frame* frame_nullable,
    struct Framemap* framemap_nullable,
    struct TexturesCache* textures_cache)
{
    //     int* vertices_x = model->vertices_x;
    //     int* vertices_y = model->vertices_y;
    //     int* vertices_z = model->vertices_z;

    //     int* face_indices_a = model->face_indices_a;
    //     int* face_indices_b = model->face_indices_b;
    //     int* face_indices_c = model->face_indices_c;

    // memset(tmp_depth_faces, 0, sizeof(tmp_depth_faces));
    // memset(tmp_priority_faces, 0, sizeof(tmp_priority_faces));

    // int* screen_vertices_x = tmp_screen_vertices_x;
    // int* screen_vertices_y = tmp_screen_vertices_y;
    // int* screen_vertices_z = tmp_screen_vertices_z;

    // int* orthographic_vertices_x = tmp_orthographic_vertices_x;
    // int* orthographic_vertices_y = tmp_orthographic_vertices_y;
    // int* orthographic_vertices_z = tmp_orthographic_vertices_z;

    int model_min_depth = bounds_cylinder->min_z_depth_any_rotation;

    memset(tmp_depth_face_count, 0, (model_min_depth * 2 + 1) * sizeof(tmp_depth_face_count[0]));

    // TODO: Move this before lighting.

    // project_vertices(
    //     screen_vertices_x,
    //     screen_vertices_y,
    //     screen_vertices_z,
    //     model->vertex_count,
    //     vertices_x,
    //     vertices_y,
    //     vertices_z,
    //     model_pitch,
    //     model_yaw,
    //     model_roll,
    //     camera_x,
    //     camera_z,
    //     camera_y,
    //     camera_pitch,
    //     camera_yaw,
    //     camera_roll,
    //     fov,
    //     width,
    //     height,
    //     near_plane_z);

    int success = project_vertices_textured(
        tmp_screen_vertices_x,
        tmp_screen_vertices_y,
        tmp_screen_vertices_z,
        tmp_orthographic_vertices_x,
        tmp_orthographic_vertices_y,
        tmp_orthographic_vertices_z,
        model->vertex_count,
        model->vertices_x,
        model->vertices_y,
        model->vertices_z,
        model_pitch,
        model_yaw,
        model_roll,
        camera_x,
        camera_z,
        camera_y,
        camera_pitch,
        camera_yaw,
        camera_roll,
        fov,
        width,
        height,
        near_plane_z);
    if( !success )
        return;

    bucket_sort_by_average_depth(
        tmp_depth_faces,
        tmp_depth_face_count,
        model_min_depth,
        model->face_count,
        tmp_screen_vertices_x,
        tmp_screen_vertices_y,
        tmp_screen_vertices_z,
        model->face_indices_a,
        model->face_indices_b,
        model->face_indices_c);

    if( !model->face_priorities )
    {
        for( int depth = model_min_depth * 2; depth < 1500 && depth >= 0; depth-- )
        {
            int bucket_count = tmp_depth_face_count[depth];
            if( bucket_count == 0 )
                continue;

            int* faces = &tmp_depth_faces[depth << 9];
            for( int j = 0; j < bucket_count; j++ )
            {
                int face = faces[j];
                model_draw_face(
                    pixel_buffer,
                    face,
                    model->face_infos,
                    model->face_indices_a,
                    model->face_indices_b,
                    model->face_indices_c,
                    model->face_count,
                    tmp_screen_vertices_x,
                    tmp_screen_vertices_y,
                    tmp_screen_vertices_z,
                    tmp_orthographic_vertices_x,
                    tmp_orthographic_vertices_y,
                    tmp_orthographic_vertices_z,
                    model->vertex_count,
                    model->face_textures,
                    model->face_texture_coords,
                    model->textured_face_count,
                    model->textured_p_coordinate,
                    model->textured_m_coordinate,
                    model->textured_n_coordinate,
                    model->textured_face_count,
                    lighting->face_colors_hsl_a,
                    lighting->face_colors_hsl_b,
                    lighting->face_colors_hsl_c,
                    model->face_alphas,
                    width / 2,
                    height / 2,
                    width,
                    height,
                    textures_cache);
            }
        }
        goto done;
    }
    memset(tmp_priority_depth_sum, 0, sizeof(tmp_priority_depth_sum));
    memset(tmp_priority_face_count, 0, sizeof(tmp_priority_face_count));

    // project_vertices_terrain_textured(
    //     screen_vertices_x,
    //     screen_vertices_y,
    //     screen_vertices_z,
    //     orthographic_vertices_x,
    //     orthographic_vertices_y,
    //     orthographic_vertices_z,
    //     model->vertex_count,
    //     vertices_x,
    //     vertices_y,
    //     vertices_z,
    //     model_pitch,
    //     model_yaw,
    //     model_roll,
    //     camera_x,
    //     camera_y,
    //     camera_z,
    //     camera_pitch,
    //     camera_yaw,
    //     camera_roll,
    //     fov,
    //     near_plane_z,
    //     width,
    //     height);

    parition_faces_by_priority(
        tmp_priority_faces,
        tmp_priority_face_count,
        tmp_depth_faces,
        tmp_depth_face_count,
        model->face_count,
        model->face_priorities,
        model_min_depth * 2);

    int valid_faces = sort_face_draw_order(
        tmp_face_order,
        tmp_depth_faces,
        tmp_depth_face_count,
        tmp_priority_faces,
        tmp_priority_face_count,
        model->face_count,
        model->face_priorities,
        model_min_depth * 2);

    for( int i = 0; i < valid_faces; i++ )
    {
        int face_index = tmp_face_order[i];

        if( model->_id == 2255 )
        {
            int iiii = 0;
        }

        model_draw_face(
            pixel_buffer,
            face_index,
            model->face_infos,
            model->face_indices_a,
            model->face_indices_b,
            model->face_indices_c,
            model->face_count,
            tmp_screen_vertices_x,
            tmp_screen_vertices_y,
            tmp_screen_vertices_z,
            tmp_orthographic_vertices_x,
            tmp_orthographic_vertices_y,
            tmp_orthographic_vertices_z,
            model->vertex_count,
            model->face_textures,
            model->face_texture_coords,
            model->textured_face_count,
            model->textured_p_coordinate,
            model->textured_m_coordinate,
            model->textured_n_coordinate,
            model->textured_face_count,
            lighting->face_colors_hsl_a,
            lighting->face_colors_hsl_b,
            lighting->face_colors_hsl_c,
            model->face_alphas,
            width / 2,
            height / 2,
            width,
            height,
            textures_cache);
    }

    // raster_osrs_typed(
    //     pixel_buffer,
    //     tmp_priority_faces,
    //     tmp_priority_face_count,
    //     model->face_infos,
    //     face_indices_a,
    //     face_indices_b,
    //     face_indices_c,
    //     model->face_count,
    //     screen_vertices_x,
    //     screen_vertices_y,
    //     screen_vertices_z,
    //     orthographic_vertices_x,
    //     orthographic_vertices_y,
    //     orthographic_vertices_z,
    //     model->vertex_count,
    //     model->face_textures,
    //     model->face_texture_coords,
    //     model->textured_face_count,
    //     model->textured_p_coordinate,
    //     model->textured_m_coordinate,
    //     model->textured_n_coordinate,
    //     model->textured_face_count,
    //     lighting->face_colors_hsl_a,
    //     lighting->face_colors_hsl_b,
    //     lighting->face_colors_hsl_c,
    //     model->face_alphas,
    //     width / 2,
    //     height / 2,
    //     width,
    //     height,
    //     textures_cache);

    // free(vertices_x);
    // free(vertices_y);
    // free(vertices_z);
    // free(face_indices_a);
    // free(face_indices_b);
    // free(face_indices_c);

    // Fall through to cleanup
done:;
    // free(screen_vertices_x);
    // free(screen_vertices_y);
    // free(screen_vertices_z);
    return;
}

#define TILE_SIZE 128

void
render_scene_tile(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* ortho_vertices_x,
    int* ortho_vertices_y,
    int* ortho_vertices_z,
    int* pixel_buffer,
    int width,
    int height,
    int near_plane_z,
    int camera_x,
    int camera_y,
    int camera_z,
    int camera_pitch,
    int camera_yaw,
    int camera_roll,
    int fov,
    struct SceneTile* tile,
    struct TexturesCache* textures_cache_nullable,
    int* color_override_hsl16_nullable)
{
    if( tile->vertex_count == 0 || tile->face_color_hsl_a == NULL )
        return;

    for( int face = 0; face < tile->face_count; face++ )
    {
        if( tile->valid_faces[face] == 0 )
            continue;

        // if( face > 0 )
        //     continue;

        int texture_id = tile->face_texture_ids ? tile->face_texture_ids[face] : -1;

        if( texture_id == -1 || textures_cache_nullable == NULL )
        {
            bool success = project_vertices_terrain_textured(
                screen_vertices_x,
                screen_vertices_y,
                screen_vertices_z,
                ortho_vertices_x,
                ortho_vertices_y,
                ortho_vertices_z,
                tile->vertex_count,
                tile->vertex_x,
                tile->vertex_y,
                tile->vertex_z,
                0,
                0,
                0,
                camera_x,
                camera_z,
                camera_y,
                camera_pitch,
                camera_yaw,
                camera_roll,
                fov,
                near_plane_z,
                width,
                height);

            // if( !success )
            //     continue;

            int* colors_a = color_override_hsl16_nullable ? color_override_hsl16_nullable
                                                          : tile->face_color_hsl_a;
            int* colors_b = color_override_hsl16_nullable ? color_override_hsl16_nullable
                                                          : tile->face_color_hsl_b;
            int* colors_c = color_override_hsl16_nullable ? color_override_hsl16_nullable
                                                          : tile->face_color_hsl_c;

            raster_osrs_single_gouraud(
                pixel_buffer,
                face,
                tile->faces_a,
                tile->faces_b,
                tile->faces_c,
                screen_vertices_x,
                screen_vertices_y,
                screen_vertices_z,
                ortho_vertices_x,
                ortho_vertices_y,
                ortho_vertices_z,
                colors_a,
                colors_b,
                colors_c,
                NULL,
                near_plane_z,
                width / 2,
                height / 2,
                width,
                height);
        }
        else
        {
            // Tile vertexes are wrapped ccw.
            project_vertices_terrain_textured(
                screen_vertices_x,
                screen_vertices_y,
                screen_vertices_z,
                ortho_vertices_x,
                ortho_vertices_y,
                ortho_vertices_z,
                tile->vertex_count,
                tile->vertex_x,
                tile->vertex_y,
                tile->vertex_z,
                0,
                0,
                0,
                camera_x,
                camera_z,
                camera_y,
                camera_pitch,
                camera_yaw,
                camera_roll,
                fov,
                near_plane_z,
                width,
                height);

            bool success = raster_osrs_single_texture(
                pixel_buffer,
                width,
                height,
                face,
                tile->faces_a,
                tile->faces_b,
                tile->faces_c,
                screen_vertices_x,
                screen_vertices_y,
                screen_vertices_z,
                ortho_vertices_x,
                ortho_vertices_y,
                ortho_vertices_z,
                tile->face_texture_ids,
                tile->face_texture_u_a,
                tile->face_texture_v_a,
                tile->face_texture_u_b,
                tile->face_texture_v_b,
                tile->face_texture_u_c,
                tile->face_texture_v_c,
                tile->face_color_hsl_a,
                tile->face_color_hsl_b,
                tile->face_color_hsl_c,
                textures_cache_nullable,
                near_plane_z,
                width / 2,
                height / 2);
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
    int tile_count,
    struct SceneTextures* textures)
{
    int* screen_vertices_x = (int*)malloc(20 * sizeof(int));
    int* screen_vertices_y = (int*)malloc(20 * sizeof(int));
    int* screen_vertices_z = (int*)malloc(20 * sizeof(int));

    // These are the vertices prior to perspective correction.
    int* ortho_vertices_x = (int*)malloc(20 * sizeof(int));
    int* ortho_vertices_y = (int*)malloc(20 * sizeof(int));
    int* ortho_vertices_z = (int*)malloc(20 * sizeof(int));

    int* z_buffer = (int*)malloc(width * height * sizeof(int));
    for( int i = 0; i < width * height; i++ )
        z_buffer[i] = INT_MAX;

    for( int z = 0; z < MAP_TERRAIN_Z; z++ )
    {
        // y = 5, and x = 40 is the left corner of the church.
        for( int y = 0; y < MAP_TERRAIN_Y; y++ )
        {
            for( int x = 0; x < MAP_TERRAIN_X; x++ )
            {
                int i = MAP_TILE_COORD(x, y, z);
                struct SceneTile* tile = &tiles[i];
                render_scene_tile(
                    screen_vertices_x,
                    screen_vertices_y,
                    screen_vertices_z,
                    ortho_vertices_x,
                    ortho_vertices_y,
                    ortho_vertices_z,
                    pixel_buffer,
                    width,
                    height,
                    near_plane_z,
                    scene_x,
                    scene_y,
                    scene_z,
                    camera_pitch,
                    camera_yaw,
                    camera_roll,
                    fov,
                    tile,
                    NULL,
                    NULL);
            }
        }
    }

done:
    free(screen_vertices_x);
    free(screen_vertices_y);
    free(screen_vertices_z);

    free(ortho_vertices_x);
    free(ortho_vertices_y);
    free(ortho_vertices_z);
}

void
scene_textures_free(struct SceneTextures* textures)
{
    for( int i = 0; i < textures->texel_count; i++ )
    {
        free(textures->texels[i]);
    }
    free(textures->texels);
    free(textures->texel_id_to_offset_lut);
    free(textures);
}

#include "tables/maps.h"

static void
render_scene_model(
    int* pixel_buffer,
    int width,
    int height,
    int near_plane_z,
    int yaw,
    int camera_x,
    int camera_y,
    int camera_z,
    int camera_pitch,
    int camera_yaw,
    int camera_roll,
    int fov,
    struct SceneModel* model,
    struct TexturesCache* textures_cache)
{
    int x = camera_x + model->region_x;
    int y = camera_y + model->region_z;
    int z = camera_z + model->region_height;

    // if( model->mirrored )
    // {
    //     yaw += 1024;
    // }

    yaw += model->yaw;

    // int rotation = model->orientation;
    // while( rotation-- )
    // {
    //     yaw += 1536;
    // }
    // 2048 in hex

    yaw &= 0x7FF;

    x += model->offset_x;
    y += model->offset_y;
    z += model->offset_height;

    if( model->model == NULL )
        return;

    render_model_frame(
        pixel_buffer,
        width,
        height,
        near_plane_z,
        0,
        yaw,
        0,
        x,
        y,
        z,
        camera_pitch,
        camera_yaw,
        camera_roll,
        fov,
        model->model,
        model->lighting,
        model->bounds_cylinder,
        NULL,
        NULL,
        NULL,
        textures_cache);
}

struct IntQueue
{
    int* data;
    int length;
    int capacity;

    int head;
    int tail;
};

void
int_queue_init(struct IntQueue* queue, int capacity)
{
    queue->data = (int*)malloc(capacity * sizeof(int));
    queue->length = 0;
    queue->capacity = capacity;
}

void
int_queue_push_wrap(struct IntQueue* queue, int value)
{
    int next_tail = (queue->tail + 1) % queue->capacity;
    assert(next_tail != queue->head);

    queue->data[queue->tail] = value << 8;
    queue->tail = next_tail;
    queue->length++;
}

void
int_queue_push_wrap_prio(struct IntQueue* queue, int value, int prio)
{
    int next_tail = (queue->tail + 1) % queue->capacity;
    assert(next_tail != queue->head);

    queue->data[queue->tail] = (value << 8) | prio;
    queue->tail = next_tail;
    queue->length++;
}

int
int_queue_pop(struct IntQueue* queue)
{
    assert((queue->head) != queue->tail);

    int value = queue->data[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->length--;

    return value;
}

void
int_queue_free(struct IntQueue* queue)
{
    free(queue->data);
}

static int g_loc_buffer[100];
static int g_loc_buffer_length = 0;

static inline int
near_wall_flags(int camera_tile_x, int camera_tile_z, int loc_x, int loc_y)
{
    int flags = 0;

    int camera_is_north = loc_y < camera_tile_z;
    int camera_is_east = loc_x < camera_tile_x;

    if( camera_is_north )
        flags |= WALL_SIDE_NORTH | WALL_CORNER_NORTHWEST | WALL_CORNER_NORTHEAST;

    if( !camera_is_north )
        flags |= WALL_SIDE_SOUTH | WALL_CORNER_SOUTHEAST | WALL_CORNER_SOUTHWEST;

    if( camera_is_east )
        flags |= WALL_SIDE_EAST | WALL_CORNER_NORTHEAST | WALL_CORNER_SOUTHEAST;

    if( !camera_is_east )
        flags |= WALL_SIDE_WEST | WALL_CORNER_NORTHWEST | WALL_CORNER_SOUTHWEST;

    return flags;
}

/**
 * Painters algorithm
 *
 * 1. Draw Bridge Underlay (the water, not the surface)
 * 2. Draw Bridge Wall (the arches holding the bridge up)
 * 3. Draw bridge locs
 * 4. Draw tile underlay
 * 5. Draw far wall
 * 6. Draw far wall decor (i.e. facing the camera)
 * 7. Draw ground decor
 * 8. Draw ground items on ground
 * 9. Draw locs
 * 10. Draw ground items on table
 * 11. Draw near decor (i.e. facing away from the camera on the near wall)
 * 12. Draw the near wall.
 */
struct SceneOp*
render_scene_compute_ops(int camera_x, int camera_y, int camera_z, struct Scene* scene, int* len)
{
    struct GridTile* grid_tile = NULL;
    struct GridTile* bridge_underpass_tile = NULL;

    struct Loc* loc = NULL;

    for( int i = 0; i < scene->locs_length; i++ )
    {
        scene->locs[i].__drawn = false;
    }

    int op_count = 0;

    int op_capacity = scene->grid_tiles_length * 11;
    struct SceneOp* ops = (struct SceneOp*)malloc(op_capacity * sizeof(struct SceneOp));
    memset(ops, 0, op_capacity * sizeof(struct SceneOp));
    *len = 0;
    struct SceneElement* elements =
        (struct SceneElement*)malloc(scene->grid_tiles_length * sizeof(struct SceneElement));

    for( int i = 0; i < scene->grid_tiles_length; i++ )
    {
        struct SceneElement* element = &elements[i];
        element->step = E_STEP_VERIFY_FURTHER_TILES_DONE_UNLESS_SPANNED;
        element->remaining_locs = scene->grid_tiles[i].locs_length;
        element->near_wall_flags = 0;
        element->generation = 0;
        element->q_count = 0;
    }

    // Generate painter's algorithm coordinate list - farthest to nearest
    int radius = 30;
    int coord_list_x[4];
    int coord_list_z[4];
    int max_level = 0;

    int coord_list_length = 0;

    int camera_tile_x = camera_x / TILE_SIZE;
    int camera_tile_z = camera_z / TILE_SIZE;
    int camera_tile_y = camera_y / 240;

    int max_draw_x = camera_tile_x + radius;
    int max_draw_z = camera_tile_z + radius;
    if( max_draw_x >= MAP_TERRAIN_X )
        max_draw_x = MAP_TERRAIN_X;
    if( max_draw_z >= MAP_TERRAIN_Y )
        max_draw_z = MAP_TERRAIN_Y;
    if( max_draw_x < 0 )
        max_draw_x = 0;
    if( max_draw_z < 0 )
        max_draw_z = 0;

    int min_draw_x = camera_tile_x - radius;
    int min_draw_z = camera_tile_z - radius;
    if( min_draw_x < 0 )
        min_draw_x = 0;
    if( min_draw_z < 0 )
        min_draw_z = 0;
    if( min_draw_x > MAP_TERRAIN_X )
        min_draw_x = MAP_TERRAIN_X;
    if( min_draw_z > MAP_TERRAIN_Y )
        min_draw_z = MAP_TERRAIN_Y;

    if( min_draw_x >= max_draw_x )
        return ops;
    if( min_draw_z >= max_draw_z )
        return ops;

    struct IntQueue queue = { 0 };
    int_queue_init(&queue, scene->grid_tiles_length);
    struct IntQueue catchup_queue = { 0 };
    int_queue_init(&catchup_queue, scene->grid_tiles_length);

    struct SceneElement* element = NULL;
    struct SceneElement* other = NULL;

    coord_list_length = 4;
    coord_list_x[0] = min_draw_x;
    coord_list_z[0] = min_draw_z;

    coord_list_x[1] = min_draw_x;
    coord_list_z[1] = max_draw_z - 1;

    coord_list_x[2] = max_draw_x - 1;
    coord_list_z[2] = min_draw_z;

    coord_list_x[3] = max_draw_x - 1;
    coord_list_z[3] = max_draw_z - 1;

    // Render tiles in painter's algorithm order (farthest to nearest)
    for( int i = 0; i < coord_list_length; i++ )
    {
        int _coord_x = coord_list_x[i];
        int _coord_z = coord_list_z[i];

        assert(_coord_x >= min_draw_x);
        assert(_coord_x < max_draw_x);
        assert(_coord_z >= min_draw_z);
        assert(_coord_z < max_draw_z);

        int _coord_idx = MAP_TILE_COORD(_coord_x, _coord_z, 0);
        int_queue_push_wrap(&queue, _coord_idx);

        int gen = 0;

        while( queue.length > 0 )
        {
            int val;

            if( catchup_queue.length > 0 )
            {
                val = int_queue_pop(&catchup_queue);
            }
            else
            {
                val = int_queue_pop(&queue);
            }

            int tile_coord = val >> 8;
            int prio = val & 0xFF;

            grid_tile = &scene->grid_tiles[tile_coord];

            int tile_x = grid_tile->x;
            int tile_z = grid_tile->z;
            int tile_level = grid_tile->level;

            element = &elements[tile_coord];
            element->generation = gen++;
            element->q_count--;

            // https://discord.com/channels/788652898904309761/1069689552052166657/1172452179160870922
            // Dane discovered this also.
            // The issue turned out to be...a nuance of the DoublyLinkedList and Node
            // implementations. In 317 anything that extends Node will be automatically unlinked
            // from a list when inserted into a list, even if it's already in that same list. I had
            // overlooked this and used a standard FIFO list in my scene tile queue. This meant that
            // tiles which were supposed to move in the render queue were actually just occupying >1
            // spot in the list.
            // It's also not a very standard way to implement doubly linked lists.. but it does save
            // allocations since now the next/prev is built into the tile. luckily the solution is
            // super simple. Add the next and prev fields to the Tile struct and use this queue:
            if( element->q_count > 0 )
                continue;

            if( (grid_tile->flags & GRID_TILE_FLAG_BRIDGE) != 0 || tile_level > max_level )
            {
                element = &elements[tile_coord];
                element->step = E_STEP_DONE;
                continue;
            }

            // printf("Tile %d, %d Coord %d\n", tile_x, tile_z, tile_coord);
            // printf("Min %d, %d\n", min_draw_x, min_draw_z);
            // printf("Max %d, %d\n", max_draw_x, max_draw_z);

            assert(tile_x >= min_draw_x);
            assert(tile_x < max_draw_x);
            assert(tile_z >= min_draw_z);
            assert(tile_z < max_draw_z);

            element = &elements[tile_coord];

            /**
             * If this is a loc revisit, then we need to verify adjacent tiles are done.
             *
             */

            if( element->step == E_STEP_VERIFY_FURTHER_TILES_DONE_UNLESS_SPANNED )
            {
                if( tile_level > 0 )
                {
                    other = &elements[MAP_TILE_COORD(tile_x, tile_z, tile_level - 1)];

                    if( other->step != E_STEP_DONE )
                    {
                        goto done;
                    }
                }

                if( tile_x >= camera_tile_x && tile_x < max_draw_x )
                {
                    if( tile_x + 1 < max_draw_x )
                    {
                        other = &elements[MAP_TILE_COORD(tile_x + 1, tile_z, tile_level)];

                        // If we are not spanned by the tile, then we need to verify it is done.
                        if( other->step != E_STEP_DONE )
                        {
                            if( (grid_tile->spans & SPAN_FLAG_EAST) == 0 ||
                                other->step != E_STEP_WAIT_ADJACENT_GROUND )
                            {
                                goto done;
                            }
                        }
                    }
                }

                if( tile_x <= camera_tile_x && tile_x > min_draw_x )
                {
                    if( tile_x - 1 >= min_draw_x )
                    {
                        other = &elements[MAP_TILE_COORD(tile_x - 1, tile_z, tile_level)];
                        if( other->step != E_STEP_DONE )
                        {
                            if( (grid_tile->spans & SPAN_FLAG_WEST) == 0 ||
                                other->step != E_STEP_WAIT_ADJACENT_GROUND )
                            {
                                goto done;
                            }
                        }
                    }
                }

                if( tile_z >= camera_tile_z && tile_z < max_draw_z )
                {
                    if( tile_z + 1 < max_draw_z )
                    {
                        other = &elements[MAP_TILE_COORD(tile_x, tile_z + 1, tile_level)];
                        if( other->step != E_STEP_DONE )
                        {
                            if( (grid_tile->spans & SPAN_FLAG_NORTH) == 0 ||
                                other->step != E_STEP_WAIT_ADJACENT_GROUND )
                            {
                                goto done;
                            }
                        }
                    }
                }
                if( tile_z <= camera_tile_z && tile_z > min_draw_z )
                {
                    if( tile_z - 1 >= min_draw_z )
                    {
                        other = &elements[MAP_TILE_COORD(tile_x, tile_z - 1, tile_level)];
                        if( other->step != E_STEP_DONE )
                        {
                            if( (grid_tile->spans & SPAN_FLAG_SOUTH) == 0 ||
                                other->step != E_STEP_WAIT_ADJACENT_GROUND )
                            {
                                goto done;
                            }
                        }
                    }
                }

                element->step = E_STEP_GROUND;
            }

            if( element->step == E_STEP_GROUND )
            {
                element->step = E_STEP_WAIT_ADJACENT_GROUND;

                int near_walls = near_wall_flags(camera_tile_x, camera_tile_z, tile_x, tile_z);
                int far_walls = ~near_walls;
                element->near_wall_flags |= near_walls;

                if( grid_tile->bridge_tile != -1 )
                {
                    bridge_underpass_tile = &scene->grid_tiles[grid_tile->bridge_tile];
                    ops[op_count++] = (struct SceneOp){
                        .op = SCENE_OP_TYPE_DRAW_GROUND,
                        .x = tile_x,
                        .z = tile_z,

                        .level = bridge_underpass_tile->level,
                    };

                    if( bridge_underpass_tile->wall != -1 )
                    {
                        loc = &scene->locs[bridge_underpass_tile->wall];
                        ops[op_count++] = (struct SceneOp){
                            .op = SCENE_OP_TYPE_DRAW_WALL,
                            .x = loc->chunk_pos_x,
                            .z = loc->chunk_pos_y,
                            .level = loc->chunk_pos_level,
                            ._wall = { .loc_index = bridge_underpass_tile->wall,
                                      .is_wall_a = true },
                        };
                    }

                    for( int i = 0; i < bridge_underpass_tile->locs_length; i++ )
                    {
                        int loc_index = bridge_underpass_tile->locs[i];
                        loc = &scene->locs[loc_index];
                        ops[op_count++] = (struct SceneOp){
                            .op = SCENE_OP_TYPE_DRAW_LOC,
                            .x = loc->chunk_pos_x,
                            .z = loc->chunk_pos_y,
                            .level = loc->chunk_pos_level,
                            ._ground_decor = { .loc_index = loc_index },
                        };
                    }
                }

                ops[op_count++] = (struct SceneOp){
                    .op = SCENE_OP_TYPE_DRAW_GROUND,
                    .x = tile_x,
                    .z = tile_z,
                    .level = tile_level,
                };

                if( grid_tile->wall != -1 )
                {
                    loc = &scene->locs[grid_tile->wall];

                    if( (loc->_wall.side_a & far_walls) != 0 )
                        ops[op_count++] = (struct SceneOp){
                            .op = SCENE_OP_TYPE_DRAW_WALL,
                            .x = loc->chunk_pos_x,
                            .z = loc->chunk_pos_y,
                            .level = loc->chunk_pos_level,
                            ._wall = { .loc_index = grid_tile->wall, .is_wall_a = true },
                        };
                    if( (loc->_wall.side_b & far_walls) != 0 )
                        ops[op_count++] = (struct SceneOp){
                            .op = SCENE_OP_TYPE_DRAW_WALL,
                            .x = loc->chunk_pos_x,
                            .z = loc->chunk_pos_y,
                            .level = loc->chunk_pos_level,
                            ._wall = { .loc_index = grid_tile->wall, .is_wall_a = false },
                        };
                }

                if( grid_tile->ground_decor != -1 )
                {
                    loc = &scene->locs[grid_tile->ground_decor];

                    ops[op_count++] = (struct SceneOp){
                        .op = SCENE_OP_TYPE_DRAW_GROUND_DECOR,
                        .x = loc->chunk_pos_x,
                        .z = loc->chunk_pos_y,
                        .level = loc->chunk_pos_level,
                        ._ground_decor = { .loc_index = grid_tile->ground_decor },
                    };
                }

                if( grid_tile->ground_object_bottom != -1 )
                {
                    loc = &scene->locs[grid_tile->ground_object_bottom];
                    ops[op_count++] = (struct SceneOp){
                        .op = SCENE_OP_TYPE_DRAW_GROUND_OBJECT,
                        .x = loc->chunk_pos_x,
                        .z = loc->chunk_pos_y,
                        .level = loc->chunk_pos_level,
                        ._ground_object = { .num = 0 },
                    };
                }

                if( grid_tile->wall_decor != -1 )
                {
                    loc = &scene->locs[grid_tile->wall_decor];
                    if( loc->_wall_decor.through_wall_flags != 0 )
                    {
                        int x_diff = loc->chunk_pos_x - camera_tile_x;
                        int y_diff = loc->chunk_pos_y - camera_tile_z;

                        // TODO: Document what this is doing.
                        int x_near = x_diff;
                        if( loc->_wall_decor.side == WALL_CORNER_NORTHEAST ||
                            loc->_wall_decor.side == WALL_CORNER_SOUTHEAST )
                            x_near = -x_diff;

                        int y_near = y_diff;
                        if( loc->_wall_decor.side == WALL_CORNER_SOUTHEAST ||
                            loc->_wall_decor.side == WALL_CORNER_SOUTHWEST )
                            y_near = -y_diff;

                        if( y_near < x_near )
                        {
                            // Draw model a
                            ops[op_count++] = (struct SceneOp){
                                .op = SCENE_OP_TYPE_DRAW_WALL_DECOR,
                                .x = loc->chunk_pos_x,
                                .z = loc->chunk_pos_y,
                                .level = loc->chunk_pos_level,
                                ._wall_decor = { .loc_index = grid_tile->wall_decor,
                                                .is_wall_a = true,
                                                .__rotation = loc->_wall_decor.side,
                                                .wall_yaw_adjust = 256,
                                               },
                            };
                        }

                        else if( loc->_wall_decor.model_b != -1 )
                        {
                            // Draw model b
                            ops[op_count++] = (struct SceneOp){
                                .op = SCENE_OP_TYPE_DRAW_WALL_DECOR,
                                .x = loc->chunk_pos_x,
                                .z = loc->chunk_pos_y,
                                .level = loc->chunk_pos_level,
                                ._wall_decor = { .loc_index = grid_tile->wall_decor,
                                                .is_wall_a = false,
                                                .__rotation = loc->_wall_decor.side,
                                                .wall_yaw_adjust = 1280,
                                           },
                            };
                        }
                    }
                    else if( (loc->_wall_decor.side & far_walls) != 0 )
                    {
                        ops[op_count++] = (struct SceneOp){
                            .op = SCENE_OP_TYPE_DRAW_WALL_DECOR,
                            .x = loc->chunk_pos_x,
                            .z = loc->chunk_pos_y,
                            .level = loc->chunk_pos_level,
                            ._wall_decor = { .loc_index = grid_tile->wall_decor },
                        };
                    }
                }

                if( tile_x < camera_tile_x )
                {
                    if( tile_x + 1 < max_draw_x )
                    {
                        int idx = MAP_TILE_COORD(tile_x + 1, tile_z, tile_level);
                        other = &elements[idx];

                        if( other->step != E_STEP_DONE && (grid_tile->spans & SPAN_FLAG_EAST) != 0 )
                        {
                            other->q_count++;

                            if( prio == 0 )
                                int_queue_push_wrap(&queue, idx);
                            else
                                int_queue_push_wrap_prio(&catchup_queue, idx, prio - 1);
                        }
                    }
                }
                if( tile_x > camera_tile_x )
                {
                    if( tile_x - 1 >= min_draw_x )
                    {
                        int idx = MAP_TILE_COORD(tile_x - 1, tile_z, tile_level);
                        other = &elements[idx];

                        if( other->step != E_STEP_DONE && (grid_tile->spans & SPAN_FLAG_WEST) != 0 )
                        {
                            other->q_count++;

                            if( prio == 0 )
                                int_queue_push_wrap(&queue, idx);
                            else
                                int_queue_push_wrap_prio(&catchup_queue, idx, prio - 1);
                        }
                    }
                }

                if( tile_z < camera_tile_z )
                {
                    if( tile_z + 1 < max_draw_z )
                    {
                        int idx = MAP_TILE_COORD(tile_x, tile_z + 1, tile_level);
                        other = &elements[idx];

                        if( other->step != E_STEP_DONE &&
                            (grid_tile->spans & SPAN_FLAG_NORTH) != 0 )
                        {
                            other->q_count++;

                            if( prio == 0 )
                                int_queue_push_wrap(&queue, idx);
                            else
                                int_queue_push_wrap_prio(&catchup_queue, idx, prio - 1);
                        }
                    }
                }

                if( tile_z > camera_tile_z )
                {
                    if( tile_z - 1 >= min_draw_z )
                    {
                        int idx = MAP_TILE_COORD(tile_x, tile_z - 1, tile_level);
                        other = &elements[idx];

                        if( other->step != E_STEP_DONE &&
                            (grid_tile->spans & SPAN_FLAG_SOUTH) != 0 )
                        {
                            other->q_count++;

                            if( prio == 0 )
                                int_queue_push_wrap(&queue, idx);
                            else
                                int_queue_push_wrap_prio(&catchup_queue, idx, prio - 1);
                        }
                    }
                }
            }

            g_loc_buffer_length = 0;
            bool contains_locs = false;
            memset(g_loc_buffer, 0, sizeof(g_loc_buffer));
            if( element->step == E_STEP_WAIT_ADJACENT_GROUND )
            {
                element->step = E_STEP_LOCS;

                // Check if all locs are drawable.
                for( int j = 0; j < grid_tile->locs_length; j++ )
                {
                    int loc_index = grid_tile->locs[j];

                    loc = &scene->locs[loc_index];
                    if( scene->locs[loc_index].__drawn )
                        continue;

                    int min_tile_x = loc->chunk_pos_x;
                    int min_tile_z = loc->chunk_pos_y;
                    int max_tile_x = min_tile_x + loc->size_x - 1;
                    int max_tile_z = min_tile_z + loc->size_y - 1;

                    if( max_tile_x > max_draw_x - 1 )
                        max_tile_x = max_draw_x - 1;
                    if( max_tile_z > max_draw_z - 1 )
                        max_tile_z = max_draw_z - 1;
                    if( min_tile_x < min_draw_x )
                        min_tile_x = min_draw_x;
                    if( min_tile_z < min_draw_z )
                        min_tile_z = min_draw_z;

                    for( int other_tile_x = min_tile_x; other_tile_x <= max_tile_x; other_tile_x++ )
                    {
                        for( int other_tile_z = min_tile_z; other_tile_z <= max_tile_z;
                             other_tile_z++ )
                        {
                            other =
                                &elements[MAP_TILE_COORD(other_tile_x, other_tile_z, tile_level)];
                            if( other->step <= E_STEP_GROUND )
                            {
                                contains_locs = true;
                                goto step_locs;
                            }
                        }
                    }

                    g_loc_buffer[g_loc_buffer_length++] = loc_index;

                step_locs:;
                }
            }

            if( element->step == E_STEP_LOCS )
            {
                // TODO: Draw farthest first.
                for( int j = 0; j < g_loc_buffer_length; j++ )
                {
                    int loc_index = g_loc_buffer[j];
                    if( scene->locs[loc_index].__drawn )
                        continue;
                    scene->locs[loc_index].__drawn = true;
                    loc = &scene->locs[loc_index];

                    ops[op_count++] = (struct SceneOp){
                        .op = SCENE_OP_TYPE_DRAW_LOC,
                        .x = loc->chunk_pos_x,
                        .z = loc->chunk_pos_y,
                        .level = loc->chunk_pos_level,
                        ._loc = { .loc_index = loc_index },
                    };

                    int min_tile_x = loc->chunk_pos_x;
                    int min_tile_z = loc->chunk_pos_y;
                    int max_tile_x = min_tile_x + loc->size_x - 1;
                    int max_tile_z = min_tile_z + loc->size_y - 1;

                    int next_prio = 0;
                    if( loc->size_x > 1 || loc->size_y > 1 )
                    {
                        next_prio = loc->size_x > loc->size_y ? loc->size_x : loc->size_y;
                    }

                    if( max_tile_x > max_draw_x - 1 )
                        max_tile_x = max_draw_x - 1;
                    if( max_tile_z > max_draw_z - 1 )
                        max_tile_z = max_draw_z - 1;
                    if( min_tile_x < min_draw_x )
                        min_tile_x = min_draw_x;
                    if( min_tile_z < min_draw_z )
                        min_tile_z = min_draw_z;

                    int step_x = tile_x <= camera_tile_x ? 1 : -1;
                    int step_y = tile_z <= camera_tile_z ? 1 : -1;

                    int start_x = min_tile_x;
                    int start_y = min_tile_z;
                    int end_x = max_tile_x;
                    int end_y = max_tile_z;

                    if( step_x < 0 )
                    {
                        int tmp = start_x;
                        start_x = end_x;
                        end_x = tmp;
                    }

                    if( step_y < 0 )
                    {
                        int tmp = start_y;
                        start_y = end_y;
                        end_y = tmp;
                    }

                    for( int other_tile_x = start_x; other_tile_x != end_x + step_x;
                         other_tile_x += step_x )
                    {
                        for( int other_tile_z = start_y; other_tile_z != end_y + step_y;
                             other_tile_z += step_y )
                        {
                            int idx = MAP_TILE_COORD(other_tile_x, other_tile_z, tile_level);
                            other = &elements[idx];
                            if( other_tile_x != tile_x || other_tile_z != tile_z )
                            {
                                other->q_count++;
                                if( next_prio == 0 )
                                    int_queue_push_wrap(&queue, idx);
                                else
                                    int_queue_push_wrap_prio(&catchup_queue, idx, next_prio - 1);
                            }
                        }
                    }
                }

                if( !contains_locs )
                    element->step = E_STEP_NOTIFY_ADJACENT_TILES;
                else
                {
                    element->step = E_STEP_WAIT_ADJACENT_GROUND;
                }
            }

            // Move towards camera if farther away tiles are done.
            if( element->step == E_STEP_NOTIFY_ADJACENT_TILES )
            {
                if( tile_level < MAP_TERRAIN_Z - 1 )
                {
                    int idx = MAP_TILE_COORD(tile_x, tile_z, tile_level + 1);
                    other = &elements[idx];

                    if( other->step != E_STEP_DONE )
                    {
                        other->q_count++;
                        if( prio == 0 )
                            int_queue_push_wrap(&queue, idx);
                        else
                            int_queue_push_wrap_prio(&catchup_queue, idx, prio - 1);
                    }
                }

                if( tile_x < camera_tile_x )
                {
                    if( tile_x + 1 < max_draw_x )
                    {
                        int idx = MAP_TILE_COORD(tile_x + 1, tile_z, tile_level);
                        other = &elements[idx];

                        if( other->step != E_STEP_DONE )
                        {
                            other->q_count++;
                            if( prio == 0 )
                                int_queue_push_wrap(&queue, idx);
                            else
                                int_queue_push_wrap_prio(&catchup_queue, idx, prio - 1);
                        }
                    }
                }
                if( tile_x > camera_tile_x )
                {
                    if( tile_x - 1 >= min_draw_x )
                    {
                        int idx = MAP_TILE_COORD(tile_x - 1, tile_z, tile_level);
                        other = &elements[idx];
                        if( other->step != E_STEP_DONE )
                        {
                            other->q_count++;
                            if( prio == 0 )
                                int_queue_push_wrap(&queue, idx);
                            else
                                int_queue_push_wrap_prio(&catchup_queue, idx, prio - 1);
                        }
                    }
                }

                if( tile_z < camera_tile_z )
                {
                    if( tile_z + 1 < max_draw_z )
                    {
                        int idx = MAP_TILE_COORD(tile_x, tile_z + 1, tile_level);
                        other = &elements[idx];
                        if( other->step != E_STEP_DONE )
                        {
                            other->q_count++;
                            if( prio == 0 )
                                int_queue_push_wrap(&queue, idx);
                            else
                                int_queue_push_wrap_prio(&catchup_queue, idx, prio - 1);
                        }
                    }
                }

                if( tile_z > camera_tile_z )
                {
                    if( tile_z - 1 >= min_draw_z )
                    {
                        int idx = MAP_TILE_COORD(tile_x, tile_z - 1, tile_level);
                        other = &elements[idx];
                        if( other->step != E_STEP_DONE )
                        {
                            other->q_count++;
                            if( prio == 0 )
                                int_queue_push_wrap(&queue, idx);
                            else
                                int_queue_push_wrap_prio(&catchup_queue, idx, prio - 1);
                        }
                    }
                }

                element->step = E_STEP_NEAR_WALL;
            }

            if( element->step == E_STEP_NEAR_WALL )
            {
                if( grid_tile->wall_decor != -1 )
                {
                    loc = &scene->locs[grid_tile->wall_decor];

                    if( loc->_wall_decor.through_wall_flags != 0 )
                    {
                        int x_diff = loc->chunk_pos_x - camera_tile_x;
                        int y_diff = loc->chunk_pos_y - camera_tile_z;

                        // TODO: Document what this is doing.
                        int x_near = x_diff;
                        if( loc->_wall_decor.side == WALL_CORNER_NORTHEAST ||
                            loc->_wall_decor.side == WALL_CORNER_SOUTHEAST )
                            x_near = -x_diff;

                        int y_near = y_diff;
                        if( loc->_wall_decor.side == WALL_CORNER_SOUTHEAST ||
                            loc->_wall_decor.side == WALL_CORNER_SOUTHWEST )
                            y_near = -y_diff;

                        // The deobs and official clients calculate the nearest quadrant.
                        // Notice a line goes from SW to NE with y = x.
                        if( y_near >= x_near )
                        {
                            // Draw model a
                            ops[op_count++] = (struct SceneOp){
                                .op = SCENE_OP_TYPE_DRAW_WALL_DECOR,
                                .x = loc->chunk_pos_x,
                                .z = loc->chunk_pos_y,
                                .level = loc->chunk_pos_level,
                                ._wall_decor = { .loc_index = grid_tile->wall_decor,
                                                .is_wall_a = true,
                                                .__rotation = loc->_wall_decor.side,
                                                .wall_yaw_adjust = 256,
                                          },
                            };
                        }
                        else if( loc->_wall_decor.model_b != -1 )
                        {
                            // Draw model b
                            ops[op_count++] = (struct SceneOp){
                                .op = SCENE_OP_TYPE_DRAW_WALL_DECOR,
                                .x = loc->chunk_pos_x,
                                .z = loc->chunk_pos_y,
                                .level = loc->chunk_pos_level,
                                ._wall_decor = { .loc_index = grid_tile->wall_decor,
                                                .is_wall_a = false,
                                                .__rotation = loc->_wall_decor.side,
                                                .wall_yaw_adjust = 1280,
                                          },
                            };
                        }
                    }
                    else if( (loc->_wall_decor.side & element->near_wall_flags) != 0 )
                    {
                        ops[op_count++] = (struct SceneOp){
                            .op = SCENE_OP_TYPE_DRAW_WALL_DECOR,
                            .x = loc->chunk_pos_x,
                            .z = loc->chunk_pos_y,
                            .level = loc->chunk_pos_level,
                            ._wall_decor = { .loc_index = grid_tile->wall_decor },
                        };
                    }
                }

                if( grid_tile->wall != -1 )
                {
                    int near_walls = element->near_wall_flags;

                    loc = &scene->locs[grid_tile->wall];

                    if( (loc->_wall.side_a & near_walls) != 0 )
                        ops[op_count++] = (struct SceneOp){
                            .op = SCENE_OP_TYPE_DRAW_WALL,
                            .x = loc->chunk_pos_x,
                            .z = loc->chunk_pos_y,
                            .level = loc->chunk_pos_level,
                            ._wall = { .loc_index = grid_tile->wall, .is_wall_a = true },
                        };
                    if( (loc->_wall.side_b & near_walls) != 0 )
                        ops[op_count++] = (struct SceneOp){
                            .op = SCENE_OP_TYPE_DRAW_WALL,
                            .x = loc->chunk_pos_x,
                            .z = loc->chunk_pos_y,
                            .level = loc->chunk_pos_level,
                            ._wall = { .loc_index = grid_tile->wall, .is_wall_a = false },
                        };
                }

                element->step = E_STEP_DONE;
            }

            if( element->step == E_STEP_DONE )
            {
            }

        done:;
        }
    }

    free(elements);
    int_queue_free(&queue);
    int_queue_free(&catchup_queue);

    *len = op_count;
    return ops;
}

static int g_white_hsl16 = 0x00FFFFFF;

static struct SceneTile g_dbg_tile = { 0 };
static int g_dbg_tile_vertex_count = 4;
static int g_dbg_tile_face_count = 2;
static int g_dbg_tile_valid_faces[4] = { 1, 1 };
static int g_dbg_tile_faces_a[4] = { 0, 3 };
static int g_dbg_tile_faces_b[4] = { 1, 1 };
static int g_dbg_tile_faces_c[4] = { 3, 2 };
static int g_dbg_tile_vertex_x_init[20] = { 0, TILE_SIZE, TILE_SIZE, 0 };
static int g_dbg_tile_vertex_y_init[20] = { 0, 0, TILE_SIZE, TILE_SIZE };
static int g_dbg_tile_vertex_z_init[20] = { 0, 0, 0, 0 };
static int g_dbg_tile_colors[4] = { 0x00FFFFFF, 0x00FFFFFF, 0x00FFFFFF, 0x00FFFFFF };

static int g_dbg_tile_vertex_x[20] = { 0, TILE_SIZE, TILE_SIZE, 0 };
static int g_dbg_tile_vertex_y[20] = { 0, 0, TILE_SIZE, TILE_SIZE };
static int g_dbg_tile_vertex_z[20] = { 0, 0, 0, 0 };

static int g_screen_vertices_x[20];
static int g_screen_vertices_y[20];
static int g_screen_vertices_z[20];
static int g_ortho_vertices_x[20];
static int g_ortho_vertices_y[20];
static int g_ortho_vertices_z[20];

static void
dbg_tile(
    int* pixel_buffer,
    int width,
    int height,
    int near_plane_z,
    int grid_x,
    int grid_y,
    int grid_z,
    int color_rgb,
    int camera_x,
    int camera_y,
    int camera_z,
    int camera_pitch,
    int camera_yaw,
    int camera_roll,
    int fov)
{
    memcpy(g_dbg_tile_vertex_x, g_dbg_tile_vertex_x_init, sizeof(g_dbg_tile_vertex_x));
    memcpy(g_dbg_tile_vertex_y, g_dbg_tile_vertex_y_init, sizeof(g_dbg_tile_vertex_y));
    memcpy(g_dbg_tile_vertex_z, g_dbg_tile_vertex_z_init, sizeof(g_dbg_tile_vertex_z));

    int tile_center_x = (grid_x)*TILE_SIZE;
    int tile_center_y = (grid_y)*TILE_SIZE;
    int tile_center_z = -(grid_z + 1) * 240;

    g_dbg_tile = (struct SceneTile){
        .vertex_count = g_dbg_tile_vertex_count,
        .vertex_x = g_dbg_tile_vertex_x,
        .vertex_y = g_dbg_tile_vertex_z,
        .vertex_z = g_dbg_tile_vertex_y,
        .face_count = g_dbg_tile_face_count,
        .faces_a = g_dbg_tile_faces_a,
        .faces_b = g_dbg_tile_faces_b,
        .faces_c = g_dbg_tile_faces_c,
        .valid_faces = g_dbg_tile_valid_faces,
        .face_texture_ids = NULL,
        .face_color_hsl_a = g_dbg_tile_colors,
        .face_color_hsl_b = g_dbg_tile_colors,
        .face_color_hsl_c = g_dbg_tile_colors,
    };

    for( int i = 0; i < 4; i++ )
    {
        g_dbg_tile_colors[i] = palette_rgb_to_hsl16(color_rgb);
    }

    // int dx = tile_center_x + scene_x;
    // int dy = tile_center_y + scene_y;
    // int dz = tile_center_z + scene_z;

    struct SceneTile* tile = &g_dbg_tile;
    int* screen_vertices_x = g_screen_vertices_x;
    int* screen_vertices_y = g_screen_vertices_y;
    int* screen_vertices_z = g_screen_vertices_z;
    int* ortho_vertices_x = g_ortho_vertices_x;
    int* ortho_vertices_y = g_ortho_vertices_y;
    int* ortho_vertices_z = g_ortho_vertices_z;

    for( int i = 0; i < 4; i++ )
    {
        tile->vertex_x[i] += tile_center_x;
        tile->vertex_z[i] += tile_center_y;
        tile->vertex_y[i] += tile_center_z;
    }

    render_scene_tile(
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        ortho_vertices_x,
        ortho_vertices_y,
        ortho_vertices_z,
        pixel_buffer,
        width,
        height,
        near_plane_z,
        camera_x,
        camera_y,
        camera_z,
        camera_pitch,
        camera_yaw,
        camera_roll,
        fov,
        tile,
        NULL,
        g_dbg_tile_colors);
}

static bool
within_rect(int x, int y, int rect_x, int rect_y, int rect_width, int rect_height)
{
    return x >= rect_x && x < rect_x + rect_width && y >= rect_y && y < rect_y + rect_height;
}

void
render_scene_ops(
    struct SceneOp* ops,
    int op_count,
    int offset,
    int number_to_render,
    int* pixel_buffer,
    int width,
    int height,
    int near_plane_z,
    int camera_x,
    int camera_y,
    int camera_z,
    int camera_pitch,
    int camera_yaw,
    int camera_roll,
    int fov,
    struct Scene* scene,
    struct TexturesCache* textures_cache)
{
    int* screen_vertices_x = g_screen_vertices_x;
    int* screen_vertices_y = g_screen_vertices_y;
    int* screen_vertices_z = g_screen_vertices_z;
    int* ortho_vertices_x = g_ortho_vertices_x;
    int* ortho_vertices_y = g_ortho_vertices_y;
    int* ortho_vertices_z = g_ortho_vertices_z;

    struct GridTile* grid_tile = NULL;
    struct Loc* loc = NULL;
    struct SceneModel* model = NULL;
    struct SceneTile* tile = NULL;

    for( int k = 0; k < number_to_render; k++ )
    {
        int i = offset + k;
        if( i >= op_count )
            break;

        struct SceneOp* op = &ops[i];
        grid_tile = &scene->grid_tiles[MAP_TILE_COORD(op->x, op->z, op->level)];

        // if( !within_rect(op->x, op->z, 30, 0, 20, 20) )
        //     continue;

        switch( op->op )
        {
        case SCENE_OP_TYPE_DRAW_GROUND:
        {
            if( grid_tile->ground == -1 )
                break;

            tile = &scene->scene_tiles[grid_tile->ground];
            if( !tile->valid_faces )
                break;

            int* color_override_hsl16_nullable = NULL;
            if( op->_ground.override_color )
            {
                color_override_hsl16_nullable = (int*)malloc(sizeof(int) * tile->face_count);
                for( int j = 0; j < tile->face_count; j++ )
                {
                    color_override_hsl16_nullable[j] = op->_ground.color_hsl16;
                }
            }

            render_scene_tile(
                screen_vertices_x,
                screen_vertices_y,
                screen_vertices_z,
                ortho_vertices_x,
                ortho_vertices_y,
                ortho_vertices_z,
                pixel_buffer,
                width,
                height,
                near_plane_z,
                camera_x,
                camera_y,
                camera_z,
                camera_pitch,
                camera_yaw,
                camera_roll,
                fov,
                tile,
                textures_cache,
                color_override_hsl16_nullable);
        }
        break;
        case SCENE_OP_TYPE_DRAW_LOC:
        {
            int model_index = -1;
            int loc_index = op->_loc.loc_index;

            loc = &scene->locs[loc_index];

            model_index = loc->_scenery.model;

            assert(model_index >= 0);
            assert(model_index < scene->models_length);

            model = &scene->models[model_index];

            assert(model != NULL);

            render_scene_model(
                pixel_buffer,
                width,
                height,
                near_plane_z,
                0,
                camera_x,
                camera_y,
                camera_z,
                camera_pitch,
                camera_yaw,
                camera_roll,
                fov,
                model,
                textures_cache);
        }
        break;
        case SCENE_OP_TYPE_DRAW_WALL:
        {
            int model_index = -1;
            int loc_index = op->_wall.loc_index;
            loc = &scene->locs[loc_index];

            assert(loc->type == LOC_TYPE_WALL);

            if( op->_wall.is_wall_a )
                model_index = loc->_wall.model_a;
            else
                model_index = loc->_wall.model_b;

            assert(model_index >= 0);
            assert(model_index < scene->models_length);

            model = &scene->models[model_index];

            assert(model != NULL);

            render_scene_model(
                pixel_buffer,
                width,
                height,
                near_plane_z,
                0,
                camera_x,
                camera_y,
                camera_z,
                camera_pitch,
                camera_yaw,
                camera_roll,
                fov,
                model,
                textures_cache);
        }
        break;
        case SCENE_OP_TYPE_DRAW_GROUND_DECOR:
        {
            int model_index = -1;
            int loc_index = op->_ground_decor.loc_index;
            loc = &scene->locs[loc_index];

            model_index = loc->_ground_decor.model;

            assert(model_index >= 0);
            assert(model_index < scene->models_length);

            model = &scene->models[model_index];

            render_scene_model(
                pixel_buffer,
                width,
                height,
                near_plane_z,
                0,
                camera_x,
                camera_y,
                camera_z,
                camera_pitch,
                camera_yaw,
                camera_roll,
                fov,
                model,
                textures_cache);
        }
        break;
        case SCENE_OP_TYPE_DRAW_GROUND_OBJECT:
        {
            int model_index = -1;
            int num = op->_ground_object.num;
            int loc_index = 0;
            switch( num )
            {
            case 0:
                loc_index = grid_tile->ground_object_bottom;
                break;
            case 1:
                loc_index = grid_tile->ground_object_middle;
                break;
            case 2:
                loc_index = grid_tile->ground_object_top;
                break;
            default:
                assert(false);
                break;
            }
            loc = &scene->locs[loc_index];

            model_index = loc->_ground_object.model;

            assert(model_index >= 0);
            assert(model_index < scene->models_length);

            model = &scene->models[model_index];

            render_scene_model(
                pixel_buffer,
                width,
                height,
                near_plane_z,
                0,
                camera_x,
                camera_y,
                camera_z,
                camera_pitch,
                camera_yaw,
                camera_roll,
                fov,
                model,
                textures_cache);
        }
        break;
        case SCENE_OP_TYPE_DRAW_WALL_DECOR:
        {
            int model_index = -1;
            int loc_index = op->_wall_decor.loc_index;
            loc = &scene->locs[loc_index];

            if( loc->_wall_decor.through_wall_flags == 0 || op->_wall_decor.is_wall_a )
            {
                model_index = loc->_wall_decor.model_a;
            }
            else
            {
                assert(loc->_wall_decor.model_b != -1);
                model_index = loc->_wall_decor.model_b;
            }

            assert(model_index >= 0);
            assert(model_index < scene->models_length);

            model = &scene->models[model_index];

            render_scene_model(
                pixel_buffer,
                width,
                height,
                near_plane_z,
                0,
                camera_x,
                camera_y,
                camera_z,
                camera_pitch,
                camera_yaw,
                camera_roll,
                fov,
                model,
                textures_cache);
        }
        break;
        case SCENE_OP_TYPE_DBG_TILE:
        {
            int tile_x = op->x;
            int tile_z = op->z;
            int tile_level = op->level;

            int color_rgb = op->_dbg.color;

            dbg_tile(
                pixel_buffer,
                width,
                height,
                near_plane_z,
                tile_x,
                tile_z,
                tile_level,
                color_rgb,
                camera_x,
                camera_y,
                camera_z,
                camera_pitch,
                camera_yaw,
                camera_roll,
                fov);
        }
        break;
        }
    }
}

void
iter_render_scene_ops_init(
    struct IterRenderSceneOps* iter,
    struct FrustrumCullmap* map,
    struct Scene* scene,
    struct SceneOp* ops,
    int op_count,
    int op_max,
    int camera_pitch,
    int camera_yaw,
    int camera_x,
    int camera_z)
{
    memset(iter, 0, sizeof(struct IterRenderSceneOps));
    iter->has_value = false;
    iter->map = map;
    iter->scene = scene;
    iter->_ops = ops;
    iter->_op_count = op_count;
    iter->_current_op = 0;
    iter->_op_max = op_max;
    iter->camera_pitch = camera_pitch; // 2048 / 16 PITCH_STEPS
    iter->camera_yaw = camera_yaw;     // 2048 / 16 YAW_STEPS
    iter->camera_x = camera_x;
    iter->camera_z = camera_z;
}

bool
iter_render_scene_ops_next(struct IterRenderSceneOps* iter)
{
    struct GridTile* grid_tile = NULL;
    struct Loc* loc = NULL;
    struct SceneModel* model = NULL;
    struct SceneTile* tile = NULL;

next:
    memset(&iter->value, 0, sizeof(iter->value));

    int i = iter->_current_op;
    if( i >= iter->_op_count || i >= iter->_op_max )
        return false;

    iter->_current_op++;

    struct SceneOp* op = &iter->_ops[i];
    grid_tile = &iter->scene->grid_tiles[MAP_TILE_COORD(op->x, op->z, op->level)];

    int to_tile_x = op->x - iter->camera_x;
    int to_tile_z = op->z - iter->camera_z;
    if( frustrum_cullmap_get(
            iter->map, to_tile_x, to_tile_z, iter->camera_pitch, iter->camera_yaw) == 0 )
        goto next;

    // if( !within_rect(op->x, op->z, 30, 0, 20, 20) )
    //     continue;

    switch( op->op )
    {
    case SCENE_OP_TYPE_DRAW_GROUND:
    {
        if( grid_tile->ground == -1 )
            goto next;

        tile = &iter->scene->scene_tiles[grid_tile->ground];
        if( !tile->valid_faces )
            goto next;

        int tile_x = tile->chunk_pos_x;
        int tile_z = tile->chunk_pos_y;
        int tile_level = tile->chunk_pos_level;

        int* color_override_hsl16_nullable = NULL;
        if( op->_ground.override_color )
        {
            color_override_hsl16_nullable = (int*)malloc(sizeof(int) * tile->face_count);
            for( int j = 0; j < tile->face_count; j++ )
            {
                color_override_hsl16_nullable[j] = op->_ground.color_hsl16;
            }
        }

        iter->value.tile_nullable_ = tile;
        iter->value.x = op->x;
        iter->value.z = op->z;
        iter->value.level = op->level;
        iter->has_value = true;
    }
    break;
    case SCENE_OP_TYPE_DRAW_LOC:
    {
        int model_index = -1;
        int loc_index = op->_loc.loc_index;
        loc = &iter->scene->locs[loc_index];

        model_index = loc->_scenery.model;

        assert(model_index >= 0);
        assert(model_index < iter->scene->models_length);

        model = &iter->scene->models[model_index];

        assert(model != NULL);

        iter->value.model_nullable_ = model;
        iter->value.x = op->x;
        iter->value.z = op->z;
        iter->value.level = op->level;
        iter->has_value = true;
    }
    break;
    case SCENE_OP_TYPE_DRAW_WALL:
    {
        int model_index = -1;
        int loc_index = op->_wall.loc_index;
        loc = &iter->scene->locs[loc_index];

        assert(loc->type == LOC_TYPE_WALL);

        if( op->_wall.is_wall_a )
            model_index = loc->_wall.model_a;
        else
            model_index = loc->_wall.model_b;

        assert(model_index >= 0);
        assert(model_index < iter->scene->models_length);

        model = &iter->scene->models[model_index];

        assert(model != NULL);

        iter->value.model_nullable_ = model;
        iter->value.x = op->x;
        iter->value.z = op->z;
        iter->value.level = op->level;
        iter->has_value = true;
    }
    break;
    case SCENE_OP_TYPE_DRAW_GROUND_DECOR:
    {
        int model_index = -1;
        int loc_index = op->_ground_decor.loc_index;
        loc = &iter->scene->locs[loc_index];

        model_index = loc->_ground_decor.model;

        assert(model_index >= 0);
        assert(model_index < iter->scene->models_length);

        model = &iter->scene->models[model_index];

        iter->value.model_nullable_ = model;
        iter->value.x = op->x;
        iter->value.z = op->z;
        iter->value.level = op->level;
        iter->has_value = true;
    }
    break;
    case SCENE_OP_TYPE_DRAW_WALL_DECOR:
    {
        int model_index = -1;
        int loc_index = op->_wall_decor.loc_index;
        loc = &iter->scene->locs[loc_index];

        if( loc->_wall_decor.through_wall_flags == 0 || op->_wall_decor.is_wall_a )
        {
            model_index = loc->_wall_decor.model_a;
        }
        else
        {
            assert(loc->_wall_decor.model_b != -1);
            model_index = loc->_wall_decor.model_b;
        }

        assert(model_index >= 0);
        assert(model_index < iter->scene->models_length);

        model = &iter->scene->models[model_index];

        iter->value.model_nullable_ = model;
        iter->value.x = op->x;
        iter->value.z = op->z;
        iter->value.level = op->level;
        iter->value.yaw = op->_wall_decor.wall_yaw_adjust;
        iter->has_value = true;
    }
    break;
    case SCENE_OP_TYPE_DRAW_GROUND_OBJECT:
    {
        int model_index = -1;
        int num = op->_ground_object.num;
        int loc_index = 0;
        switch( num )
        {
        case 0:
            loc_index = grid_tile->ground_object_bottom;
            break;
        case 1:
            loc_index = grid_tile->ground_object_middle;
            break;
        case 2:
            loc_index = grid_tile->ground_object_top;
            break;
        default:
            assert(false);
            break;
        }
        loc = &iter->scene->locs[loc_index];

        model_index = loc->_ground_object.model;

        assert(model_index >= 0);
        assert(model_index < iter->scene->models_length);

        model = &iter->scene->models[model_index];

        iter->value.model_nullable_ = model;
        iter->value.x = op->x;
        iter->value.z = op->z;
        iter->value.level = op->level;
        iter->has_value = true;
    }
    break;
    case SCENE_OP_TYPE_DBG_TILE:
    {
        int tile_x = op->x;
        int tile_z = op->z;

        int color_rgb = op->_dbg.color;
    }
    break;
    }

    return iter->has_value;
}

void
iter_render_scene_ops_free(struct IterRenderSceneOps* iter)
{
    free(iter);
}

void
iter_render_model_init(
    struct IterRenderModel* iter,
    struct SceneModel* scene_model,
    int yaw,
    int camera_x,
    int camera_y,
    int camera_z,
    int camera_pitch,
    int camera_yaw,
    int camera_roll,
    int fov,
    int width,
    int height,
    int near_plane_z)
{
    memset(iter, 0, sizeof(struct IterRenderModel));
    iter->model = scene_model;
    struct BoundingCylinder bounding_cylinder;

    iter->screen_vertices_x = tmp_screen_vertices_x;
    iter->screen_vertices_y = tmp_screen_vertices_y;
    iter->screen_vertices_z = tmp_screen_vertices_z;
    iter->ortho_vertices_x = tmp_orthographic_vertices_x;
    iter->ortho_vertices_y = tmp_orthographic_vertices_y;
    iter->ortho_vertices_z = tmp_orthographic_vertices_z;

    struct CacheModel* model = scene_model->model;
    iter->index = 0;

    int* vertices_x = model->vertices_x;
    int* vertices_y = model->vertices_y;
    int* vertices_z = model->vertices_z;

    int* face_indices_a = model->face_indices_a;
    int* face_indices_b = model->face_indices_b;
    int* face_indices_c = model->face_indices_c;

    // memset(tmp_depth_faces, 0, sizeof(tmp_depth_faces));
    memset(tmp_priority_face_count, 0, sizeof(tmp_priority_face_count));
    memset(tmp_priority_depth_sum, 0, sizeof(tmp_priority_depth_sum));
    // memset(tmp_priority_faces, 0, sizeof(tmp_priority_faces));

    int* screen_vertices_x = tmp_screen_vertices_x;
    int* screen_vertices_y = tmp_screen_vertices_y;
    int* screen_vertices_z = tmp_screen_vertices_z;

    int* orthographic_vertices_x = tmp_orthographic_vertices_x;
    int* orthographic_vertices_y = tmp_orthographic_vertices_y;
    int* orthographic_vertices_z = tmp_orthographic_vertices_z;

    int scene_x = scene_model->region_x - camera_x;
    int scene_y = scene_model->region_height - camera_y;
    int scene_z = scene_model->region_z - camera_z;
    yaw += scene_model->yaw;
    yaw %= 2048;

    scene_x += scene_model->offset_x;
    scene_y += scene_model->offset_y;
    scene_z += scene_model->offset_height;

    int success = project_vertices_textured(
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        orthographic_vertices_x,
        orthographic_vertices_y,
        orthographic_vertices_z,
        model->vertex_count,
        vertices_x,
        vertices_y,
        vertices_z,
        0,
        yaw,
        0,
        scene_x,
        scene_y,
        scene_z,
        camera_pitch,
        camera_yaw,
        camera_roll,
        fov,
        width,
        height,
        near_plane_z);

    if( !success )
        return;

    bounding_cylinder = calculate_bounding_cylinder(
        model->vertex_count, model->vertices_x, model->vertices_y, model->vertices_z);

    int model_min_depth = bounding_cylinder.min_z_depth_any_rotation;

    memset(tmp_depth_face_count, 0, (model_min_depth * 2 + 1) * sizeof(tmp_depth_face_count[0]));

    bucket_sort_by_average_depth(
        tmp_depth_faces,
        tmp_depth_face_count,
        model_min_depth,
        model->face_count,
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        face_indices_a,
        face_indices_b,
        face_indices_c);

    if( !model->face_priorities )
    {
        for( int depth = model_min_depth * 2; depth < 1500 && depth >= 0; depth-- )
        {
            int bucket_count = tmp_depth_face_count[depth];
            if( bucket_count == 0 )
                continue;

            int* faces = &tmp_depth_faces[depth * 512];
            for( int j = 0; j < bucket_count; j++ )
            {
                int face = faces[j];
                tmp_face_order[iter->valid_faces++] = face;
            }
        }
    }
    else
    {
        parition_faces_by_priority(
            tmp_priority_faces,
            tmp_priority_face_count,
            tmp_depth_faces,
            tmp_depth_face_count,
            model->face_count,
            model->face_priorities,
            model_min_depth * 2);

        int valid_faces = sort_face_draw_order(
            tmp_face_order,
            tmp_depth_faces,
            tmp_depth_face_count,
            tmp_priority_faces,
            tmp_priority_face_count,
            model->face_count,
            model->face_priorities,
            model_min_depth * 2);

        iter->valid_faces = valid_faces;
    }
}

bool
iter_render_model_next(struct IterRenderModel* iter)
{
    if( iter->index >= iter->valid_faces )
        return false;

    int face = tmp_face_order[iter->index];
    iter->index++;
    iter->value_face = face;

    return true;
}
