#include "osrs/render.h"

#include "anim.h"
#include "flat.h"
#include "gouraud.h"
#include "gouraud_deob.h"
#include "lighting.h"
#include "palette.h"
#include "projection.h"
#include "scene.h"
#include "scene_tile.h"
#include "tables/maps.h"
#include "tables/model.h"
#include "tables/sprites.h"
#include "tables/texture_pixels.h"
#include "texture.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char*
element_step_str(enum ElementStep step)
{
    switch( step )
    {
    case E_STEP_GROUND:
        return "GROUND";
    case E_STEP_VERIFY_FURTHER_TILES_DONE_UNLESS_SPANNED:
        return "VERIFY_FURTHER_TILES_DONE_UNLESS_SPANNED";
    case E_STEP_WAIT_ADJACENT_GROUND:
        return "WAIT_ADJACENT_GROUND";
    case E_STEP_LOCS:
        return "LOCS";
    case E_STEP_NOTIFY_ADJACENT_TILES:
        return "NOTIFY_ADJACENT_TILES";
    case E_STEP_DONE:
        return "DONE";
    default:
        return "UNKNOWN";
    }
}

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

static void
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
        projected_triangle = project_orthographic(
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
            camera_roll);

        orthographic_vertices_x[i] = projected_triangle.x;
        orthographic_vertices_y[i] = projected_triangle.y;
        orthographic_vertices_z[i] = projected_triangle.z;

        projected_triangle = project_perspective(
            orthographic_vertices_x[i],
            orthographic_vertices_y[i],
            orthographic_vertices_z[i],
            camera_fov,
            near_plane_z);

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

/**
 * Terrain is treated as a single, so the origin test does not apply.
 */
static void
project_vertices_terrain(
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
        projected_triangle = project(
            vertex_x[i],
            vertex_y[i],
            vertex_z[i],
            model_pitch,
            model_yaw,
            model_roll,
            camera_x,
            // Camera z is the y axis in OSRS
            camera_z,
            camera_y,
            camera_pitch,
            camera_yaw,
            camera_roll,
            camera_fov,
            near_plane_z,
            screen_width,
            screen_height);

        if( projected_triangle.clipped )
        {
            // Since terrain vertexes are calculated as a single mesh rather,
            // than around some origin, assume that if any vertex is clipped,
            // then the entire terrain is clipped.
            goto clipped;
        }
        else
        {
            screen_vertices_x[i] = projected_triangle.x;
            screen_vertices_y[i] = projected_triangle.y;
            screen_vertices_z[i] = projected_triangle.z;
        }
    }

    return;
clipped:
    for( int i = 0; i < num_vertices; i++ )
    {
        screen_vertices_x[i] = -5000;
        screen_vertices_y[i] = -5000;
        screen_vertices_z[i] = -5000;
    }

    return;
}

/**
 * Terrain is treated as a single, so the origin test does not apply.
 */
static void
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
    int scene_x,
    int scene_y,
    int scene_z,
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
            scene_x,
            // Camera z is the y axis in OSRS
            scene_z,
            scene_y,
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
            goto clipped;
        }
        else
        {
            screen_vertices_x[i] = projected_triangle.x;
            screen_vertices_y[i] = projected_triangle.y;
            screen_vertices_z[i] = projected_triangle.z;
        }
    }

    return;

clipped:
    for( int i = 0; i < num_vertices; i++ )
    {
        screen_vertices_x[i] = -5000;
        screen_vertices_y[i] = -5000;
        screen_vertices_z[i] = -5000;
    }

    return;
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

enum FaceType
{
    FACE_TYPE_GOURAUD,
    FACE_TYPE_FLAT,
    FACE_TYPE_TEXTURED,
    FACE_TYPE_TEXTURED_FLAT_SHADE,
};

int g_empty_texture_texels[128 * 128] = { 0 };

static void
model_draw_face(
    struct Pixel* pixel_buffer,
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
    int offset_x,
    int offset_y,
    int screen_width,
    int screen_height,
    struct TexturesCache* textures_cache)
{
    struct Texture* texture = NULL;
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
    int y1 = vertex_y[face_a];
    int z1 = vertex_z[face_a];
    int x2 = vertex_x[face_b];
    int y2 = vertex_y[face_b];
    int z2 = vertex_z[face_b];
    int x3 = vertex_x[face_c];
    int y3 = vertex_y[face_c];
    int z3 = vertex_z[face_c];

    // Skip triangle if any vertex was clipped
    if( x1 == -5000 || x3 == -5000 || x3 == -5000 )
        return;

    x1 += offset_x;
    y1 += offset_y;
    x2 += offset_x;
    y2 += offset_y;
    x3 += offset_x;
    y3 += offset_y;

    assert(offset_y == (screen_height >> 1));
    assert(offset_x == (screen_width >> 1));
    int color_a = colors_a[index];
    int color_b = colors_b[index];
    int color_c = colors_c[index];

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

    enum FaceType type = face_infos ? (face_infos[index] & 0x3) : FACE_TYPE_GOURAUD;
    assert(type >= 0 && type <= 3);

    int* texels = g_empty_texture_texels;
    if( face_textures && face_textures[index] != -1 )
    {
        texture_id = face_textures[index];
        texture = textures_cache_checkout(textures_cache, NULL, texture_id, 128, 1.4);
        assert(texture != NULL);
        texels = texture->texels;

        if( color_c == -1 )
            goto textured_flat;
        else
            goto textured;
    }
    else
    {
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
            break;
        case FACE_TYPE_FLAT:

            raster_flat(pixel_buffer, screen_width, screen_height, x1, x2, x3, y1, y2, y3, color_a);
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

            // struct ProjectedTriangle projected_triangle =
            //     project_perspective(tp_x, tp_y, tp_z, 512, 0);

            // x1 = vertex_x[tp_face];
            // y1 = vertex_y[tp_face];
            // z1 = vertex_z[tp_face];
            // x2 = vertex_x[tm_face];
            // y2 = vertex_y[tm_face];
            // z2 = vertex_z[tm_face];
            // x3 = vertex_x[tn_face];
            // y3 = vertex_y[tn_face];
            // z3 = vertex_z[tn_face];

            // // Skip triangle if any vertex was clipped
            // if( x1 == -5000 || x3 == -5000 || x3 == -5000 )
            //     return;

            // x1 += offset_x;
            // y1 += offset_y;
            // x2 += offset_x;
            // y2 += offset_y;
            // x3 += offset_x;
            // y3 += offset_y;

            // printf("\n");
            // printf("Face %d\n", face_index);

            // printf("P = %d, M = %d, N = %d\n", tp_face, tm_face, tn_face);
            // printf("x1 = %d, y1 = %d, z1 = %d\n", x1, y1, z1);
            // printf("x2 = %d, y2 = %d, z2 = %d\n", x2, y2, z2);
            // printf("x3 = %d, y3 = %d, z3 = %d\n", x3, y3, z3);
            // printf("tP = %d, %d, %d\n", tp_x, tp_y, tp_z);
            // printf("tM = %d, %d, %d\n", tm_x, tm_y, tm_z);
            // printf("tN = %d, %d, %d\n", tn_x, tn_y, tn_z);

            int tex_x1 = vertex_x[tp_face] + offset_x;
            int tex_y1 = vertex_y[tp_face] + offset_y;
            int tex_x2 = vertex_x[tm_face] + offset_x;
            int tex_y2 = vertex_y[tm_face] + offset_y;
            int tex_x3 = vertex_x[tn_face] + offset_x;
            int tex_y3 = vertex_y[tn_face] + offset_y;

            // textureTriangle(
            //     x1,
            //     x2,
            //     x3,
            //     y1,
            //     y2,
            //     y3,
            //     0,
            //     0,
            //     0,
            //     tp_x,
            //     tp_y,
            //     tp_z,
            //     tm_x,
            //     tn_x,
            //     tm_y,
            //     tn_y,
            //     tm_z,
            //     tn_z,
            //     pixel_buffer,
            //     texels,
            //     128);
            raster_texture_step(
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
                texels,
                128,
                false);

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

            raster_texture_step(
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
                tp_y,
                tp_z,
                tm_x,
                tm_y,
                tm_z,
                tn_x,
                tn_y,
                tn_z,
                g_empty_texture_texels,
                128,
                false);
            break;
        }
    }
}

static void
raster_osrs_noprio(
    struct Pixel* pixel_buffer,
    int* faces,
    int* priority_face_counts,
    int* face_infos,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
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
    int* colors_a,
    int* colors_b,
    int* colors_c,
    int offset_x,
    int offset_y,
    int screen_width,
    int screen_height,
    struct TexturesCache* textures_cache)
{}

void
raster_osrs_typed(
    struct Pixel* pixel_buffer,
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
                offset_x,
                offset_y,
                screen_width,
                screen_height,
                textures_cache);
        }
    }
}

void
raster_osrs_single_gouraud(
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

    // drawGouraudTriangle(pixel_buffer, y1, y2, y3, x1, x2, x3, color_a, color_b, color_c);

    raster_gouraud(
        pixel_buffer,
        // z_buffer,
        screen_width,
        screen_height,
        x1,
        x2,
        x3,
        y1,
        y2,
        y3,
        // z1,
        // z2,
        // z3,
        color_a,
        color_b,
        color_c);
}

bool
raster_osrs_single_texture(
    struct Pixel* pixel_buffer,
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
    struct TexturesCache* textures_cache,
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

    texture = textures_cache_checkout(textures_cache, NULL, texture_id, 128, 1.4);
    if( !texture )
        return false;

    raster_texture_step(
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
        texture->texels,
        128,
        texture->opaque);

    return true;
}

static int tmp_depth_face_count[1500] = { 0 };
static int tmp_depth_faces[1500 * 512] = { 0 };
static int tmp_priority_face_count[12] = { 0 };
static int tmp_priority_depth_sum[12] = { 0 };
static int tmp_priority_faces[12 * 2000] = { 0 };

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
    int transform,
    struct CacheModel* model,
    struct CacheModelBones* bones_nullable,
    struct Frame* frame_nullable,
    struct Framemap* framemap_nullable,
    struct TexturesCache* textures_cache)
{
    int* vertices_x = (int*)malloc(model->vertex_count * sizeof(int));
    memcpy(vertices_x, model->vertices_x, model->vertex_count * sizeof(int));
    int* vertices_y = (int*)malloc(model->vertex_count * sizeof(int));
    memcpy(vertices_y, model->vertices_y, model->vertex_count * sizeof(int));
    int* vertices_z = (int*)malloc(model->vertex_count * sizeof(int));
    memcpy(vertices_z, model->vertices_z, model->vertex_count * sizeof(int));

    int* face_indices_a = (int*)malloc(model->face_count * sizeof(int));
    memcpy(face_indices_a, model->face_indices_a, model->face_count * sizeof(int));
    int* face_indices_b = (int*)malloc(model->face_count * sizeof(int));
    memcpy(face_indices_b, model->face_indices_b, model->face_count * sizeof(int));
    int* face_indices_c = (int*)malloc(model->face_count * sizeof(int));
    memcpy(face_indices_c, model->face_indices_c, model->face_count * sizeof(int));

    // if( transform )
    // {
    //     for( int v = 0; v < model->vertex_count; v++ )
    //     {
    //         vertices_z[v] = -vertices_z[v];
    //     }

    //     for( int f = 0; f < model->face_count; f++ )
    //     {
    //         int temp = face_indices_a[f];
    //         face_indices_a[f] = face_indices_c[f];
    //         face_indices_c[f] = temp;
    //     }
    // }

    // int* vertices_x = model->vertices_x;
    // int* vertices_y = model->vertices_y;
    // int* vertices_z = model->vertices_z;

    // int* face_colors_a_hsl16 = (int*)malloc(model->face_count * sizeof(int));
    // memcpy(face_colors_a_hsl16, model->face_colors, model->face_count * sizeof(int));
    // int* face_colors_b_hsl16 = (int*)malloc(model->face_count * sizeof(int));
    // memcpy(face_colors_b_hsl16, model->face_colors, model->face_count * sizeof(int));
    // int* face_colors_c_hsl16 = (int*)malloc(model->face_count * sizeof(int));
    // memcpy(face_colors_c_hsl16, model->face_colors, model->face_count * sizeof(int));

    // int* face_colors_a_hsl16 = model->face_colors;
    // int* face_colors_b_hsl16 = model->face_colors;
    // int* face_colors_c_hsl16 = model->face_colors;

    // TODO: don't allocate this every frame.
    struct VertexNormal* vertex_normals =
        (struct VertexNormal*)malloc(model->vertex_count * sizeof(struct VertexNormal));
    memset(vertex_normals, 0, model->vertex_count * sizeof(struct VertexNormal));

    memset(tmp_depth_face_count, 0, sizeof(tmp_depth_face_count));
    // memset(tmp_depth_faces, 0, sizeof(tmp_depth_faces));
    memset(tmp_priority_face_count, 0, sizeof(tmp_priority_face_count));
    memset(tmp_priority_depth_sum, 0, sizeof(tmp_priority_depth_sum));
    // memset(tmp_priority_faces, 0, sizeof(tmp_priority_faces));

    // int* screen_vertices_x = (int*)malloc(model->vertex_count * sizeof(int));
    // int* screen_vertices_y = (int*)malloc(model->vertex_count * sizeof(int));
    // int* screen_vertices_z = (int*)malloc(model->vertex_count * sizeof(int));
    int* face_colors_a_hsl16 = tmp_face_colors_a_hsl16;
    int* face_colors_b_hsl16 = tmp_face_colors_b_hsl16;
    int* face_colors_c_hsl16 = tmp_face_colors_c_hsl16;

    int* screen_vertices_x = tmp_screen_vertices_x;
    int* screen_vertices_y = tmp_screen_vertices_y;
    int* screen_vertices_z = tmp_screen_vertices_z;

    int* orthographic_vertices_x = tmp_orthographic_vertices_x;
    int* orthographic_vertices_y = tmp_orthographic_vertices_y;
    int* orthographic_vertices_z = tmp_orthographic_vertices_z;

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
        model->vertex_count,
        face_indices_a,
        face_indices_b,
        face_indices_c,
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
        face_indices_a,
        face_indices_b,
        face_indices_c,
        model->face_count,
        vertices_x,
        vertices_y,
        vertices_z,
        model->face_colors,
        model->face_infos,
        light_ambient,
        attenuation,
        lightsrc_x,
        lightsrc_y,
        lightsrc_z);

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

    project_vertices_textured(
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
                model_draw_face(
                    pixel_buffer,
                    face,
                    model->face_infos,
                    face_indices_a,
                    face_indices_b,
                    face_indices_c,
                    model->face_count,
                    screen_vertices_x,
                    screen_vertices_y,
                    screen_vertices_z,
                    orthographic_vertices_x,
                    orthographic_vertices_y,
                    orthographic_vertices_z,
                    model->vertex_count,
                    model->face_textures,
                    model->face_texture_coords,
                    model->textured_face_count,
                    model->textured_p_coordinate,
                    model->textured_m_coordinate,
                    model->textured_n_coordinate,
                    model->textured_face_count,
                    face_colors_a_hsl16,
                    face_colors_b_hsl16,
                    face_colors_c_hsl16,
                    width / 2,
                    height / 2,
                    width,
                    height,
                    textures_cache);
            }
        }
        goto done;
    }

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

    // 637 is willow?

    raster_osrs_typed(
        pixel_buffer,
        tmp_priority_faces,
        tmp_priority_face_count,
        model->face_infos,
        face_indices_a,
        face_indices_b,
        face_indices_c,
        model->face_count,
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        orthographic_vertices_x,
        orthographic_vertices_y,
        orthographic_vertices_z,
        model->vertex_count,
        model->face_textures,
        model->face_texture_coords,
        model->textured_face_count,
        model->textured_p_coordinate,
        model->textured_m_coordinate,
        model->textured_n_coordinate,
        model->textured_face_count,
        face_colors_a_hsl16,
        face_colors_b_hsl16,
        face_colors_c_hsl16,
        width / 2,
        height / 2,
        width,
        height,
        textures_cache);

    free(vertices_x);
    free(vertices_y);
    free(vertices_z);
    free(face_indices_a);
    free(face_indices_b);
    free(face_indices_c);

done:
    free(vertex_normals);
    // free(screen_vertices_x);
    // free(screen_vertices_y);
    // free(screen_vertices_z);
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

static void
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
    int grid_x,
    int grid_y,
    int grid_z,
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

    // TODO: A faster culling
    // if the tile is far enough away, skip it.
    // Calculate distance from camera to tile center
    // int tile_center_x = (grid_x - 1) * TILE_SIZE;
    // int tile_center_z = (grid_y - 1) * TILE_SIZE;
    // int tile_center_y = -(grid_z - 1) * 240;

    // // int dx = tile_center_x + scene_x;
    // // int dy = tile_center_y + scene_y;
    // // int dz = tile_center_z + scene_z;

    // for( int i = 0; i < tile->vertex_count; i++ )
    // {
    //     tile->vertex_x[i] += tile_center_x;
    //     tile->vertex_y[i] += tile_center_y;
    //     tile->vertex_z[i] += tile_center_z;
    // }

    // Simple squared distance - avoid sqrt for performance
    // int dist_sq = dx * dx;
    // if( dist_sq > TILE_SIZE * TILE_SIZE * 10000 )
    //     return;

    // dist_sq = dy * dy;
    // if( dist_sq > TILE_SIZE * TILE_SIZE * 10000 )
    //     return;

    for( int face = 0; face < tile->face_count; face++ )
    {
        if( tile->valid_faces[face] == 0 )
            continue;

        int texture_id = tile->face_texture_ids ? tile->face_texture_ids[face] : -1;

        if( texture_id == -1 || textures_cache_nullable == NULL )
        {
            project_vertices_terrain(
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
                camera_x,
                camera_y,
                camera_z,
                camera_pitch,
                camera_yaw,
                camera_roll,
                fov,
                near_plane_z,
                width,
                height);

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
                // TODO: Remove legacy face_color_hsl.
                colors_a,
                colors_b,
                colors_c,
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
                camera_y,
                camera_z,
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
                textures_cache_nullable,
                width / 2,
                height / 2);
        }
    }

    // for( int i = 0; i < tile->vertex_count; i++ )
    // {
    //     tile->vertex_x[i] -= tile_center_x;
    //     tile->vertex_y[i] -= tile_center_y;
    //     tile->vertex_z[i] -= tile_center_z;
    // }
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
                    x,
                    y,
                    z,
                    scene_x,
                    scene_y,
                    scene_z,
                    camera_pitch,
                    camera_yaw,
                    camera_roll,
                    fov,
                    tile,
                    textures,
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

struct SceneTextures*
scene_textures_new_from_tiles(
    struct SceneTile* tiles,
    int tile_count,
    struct CacheSpritePack* sprite_packs,
    int* sprite_ids,
    int sprite_count,
    struct CacheTexture* textures,
    int* texture_ids,
    int texture_count)
{
    struct SceneTextures* scene_textures =
        (struct SceneTextures*)malloc(sizeof(struct SceneTextures));
    memset(scene_textures, 0, sizeof(struct SceneTextures));

    scene_textures->texel_id_to_offset_lut = (int*)malloc(tile_count * sizeof(int));
    memset(scene_textures->texel_id_to_offset_lut, 0, tile_count * sizeof(int));

    // TODO: Use a hashmap.
    int max_texture_id = 0;
    for( int i = 0; i < tile_count; i++ )
    {
        struct SceneTile* tile = &tiles[i];

        for( int face = 0; face < tile->face_count; face++ )
        {
            if( tile->valid_faces[face] == 0 || tile->face_texture_ids == NULL )
                continue;

            int texture_id = tile->face_texture_ids[face];
            if( texture_id > max_texture_id )
                max_texture_id = texture_id;
        }
    }

    scene_textures->texel_id_to_offset_lut = (int*)malloc((max_texture_id + 1) * sizeof(int));
    memset(scene_textures->texel_id_to_offset_lut, 0, (max_texture_id + 1) * sizeof(int));
    scene_textures->texels = (int**)malloc((max_texture_id + 1) * sizeof(int*));
    memset(scene_textures->texels, 0, (max_texture_id + 1) * sizeof(int*));

    for( int i = 0; i < max_texture_id + 1; i++ )
        scene_textures->texel_id_to_offset_lut[i] = -1;

    for( int i = 0; i < tile_count; i++ )
    {
        struct SceneTile* tile = &tiles[i];

        for( int face = 0; face < tile->face_count; face++ )
        {
            if( tile->valid_faces[face] == 0 || tile->face_texture_ids == NULL )
                continue;

            int texture_id = tile->face_texture_ids[face];
            if( texture_id == -1 )
                continue;

            struct CacheTexture* texture_definition = NULL;
            for( int i = 0; i < texture_count; i++ )
            {
                if( texture_ids[i] == texture_id )
                {
                    texture_definition = &textures[i];
                    break;
                }
            }

            if( !texture_definition )
                continue;

            if( scene_textures->texel_id_to_offset_lut[texture_id] == -1 )
            {
                int* texels = texture_pixels_new_from_definition(
                    texture_definition, 128, sprite_packs, sprite_ids, sprite_count, 1);
                if( !texels )
                    continue;

                scene_textures->texel_id_to_offset_lut[texture_id] = scene_textures->texel_count;
                scene_textures->texels[scene_textures->texel_count++] = texels;
            }
        }
    }

    return scene_textures;
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

void
model_rotate_y180(
    int* vertices_z, int* face_indices_a, int* face_indices_c, int vertex_count, int face_count)
{
    for( int v = 0; v < vertex_count; v++ )
    {
        vertices_z[v] = -vertices_z[v];
    }

    for( int f = 0; f < face_count; f++ )
    {
        int temp = face_indices_a[f];
        face_indices_a[f] = face_indices_c[f];
        face_indices_c[f] = temp;
    }
}

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
    int y = camera_y + model->region_y;
    int z = camera_z + model->region_z;

    // if( model->mirrored )
    // {
    //     yaw += 1024;
    // }

    // int rotation = model->orientation;
    // while( rotation-- )
    // {
    //     yaw += 1536;
    // }
    yaw %= 2048;

    x += model->offset_x;
    y += model->offset_y;
    z += model->offset_height;

    for( int j = 0; j < 1 && model->model_count; j++ )
    {
        struct CacheModel* drawable = model->models[j];
        if( !drawable )
            continue;

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
            false,
            drawable,
            NULL,
            NULL,
            NULL,
            textures_cache);
    }
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
near_wall_flags(int camera_tile_x, int camera_tile_y, int loc_x, int loc_y)
{
    int flags = 0;

    int camera_is_north = loc_y < camera_tile_y;
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

static int
reverse_wall_side(int side)
{
    switch( side )
    {
    case WALL_SIDE_WEST:
        return WALL_SIDE_EAST;
    case WALL_SIDE_EAST:
        return WALL_SIDE_WEST;
    case WALL_SIDE_NORTH:
        return WALL_SIDE_SOUTH;
    case WALL_SIDE_SOUTH:
        return WALL_SIDE_NORTH;
    case WALL_CORNER_NORTHWEST:
        return WALL_CORNER_SOUTHEAST;
    case WALL_CORNER_NORTHEAST:
        return WALL_CORNER_SOUTHWEST;
    case WALL_CORNER_SOUTHEAST:
        return WALL_CORNER_NORTHWEST;
    case WALL_CORNER_SOUTHWEST:
        return WALL_CORNER_NORTHEAST;
    default:
        assert(false);
        return 0;
    }
}

static int
wallside_rotation_quadrant(enum WallSide side)
{
    // Assumes model is facing west.
    // Rotating counter-clockwise.
    switch( side )
    {
    case WALL_CORNER_NORTHEAST:
        return 3;
    case WALL_CORNER_NORTHWEST:
        return 2;
    case WALL_CORNER_SOUTHWEST:
        return 1;
    case WALL_CORNER_SOUTHEAST:
        return 0;
    default:
        printf("side %d\n", side);
        assert(false);
        return 0;
    }
}

/**
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
    int coord_list_y[4];

    int coord_list_length = 0;

    int camera_tile_x = -camera_x / TILE_SIZE;
    int camera_tile_y = -camera_y / TILE_SIZE;
    int camera_tile_z = camera_z / 240;

    int max_draw_x = camera_tile_x + radius;
    int max_draw_y = camera_tile_y + radius;
    if( max_draw_x >= MAP_TERRAIN_X )
        max_draw_x = MAP_TERRAIN_X;
    if( max_draw_y >= MAP_TERRAIN_Y )
        max_draw_y = MAP_TERRAIN_Y;
    if( max_draw_x < 0 )
        max_draw_x = 0;
    if( max_draw_y < 0 )
        max_draw_y = 0;

    int min_draw_x = camera_tile_x - radius;
    int min_draw_y = camera_tile_y - radius;
    if( min_draw_x < 0 )
        min_draw_x = 0;
    if( min_draw_y < 0 )
        min_draw_y = 0;
    if( min_draw_x > MAP_TERRAIN_X )
        min_draw_x = MAP_TERRAIN_X;
    if( min_draw_y > MAP_TERRAIN_Y )
        min_draw_y = MAP_TERRAIN_Y;

    if( min_draw_x >= max_draw_x )
        return ops;
    if( min_draw_y >= max_draw_y )
        return ops;

    struct IntQueue queue = { 0 };
    int_queue_init(&queue, scene->grid_tiles_length);
    struct IntQueue catchup_queue = { 0 };
    int_queue_init(&catchup_queue, scene->grid_tiles_length);

    struct SceneElement* element = NULL;
    struct SceneElement* other = NULL;

    coord_list_length = 4;
    coord_list_x[0] = min_draw_x;
    coord_list_y[0] = min_draw_y;

    coord_list_x[1] = min_draw_x;
    coord_list_y[1] = max_draw_y - 1;

    coord_list_x[2] = max_draw_x - 1;
    coord_list_y[2] = min_draw_y;

    coord_list_x[3] = max_draw_x - 1;
    coord_list_y[3] = max_draw_y - 1;

    // Render tiles in painter's algorithm order (farthest to nearest)
    for( int i = 0; i < coord_list_length; i++ )
    {
        int _coord_x = coord_list_x[i];
        int _coord_y = coord_list_y[i];

        assert(_coord_x >= min_draw_x);
        assert(_coord_x < max_draw_x);
        assert(_coord_y >= min_draw_y);
        assert(_coord_y < max_draw_y);

        int _coord_idx = MAP_TILE_COORD(_coord_x, _coord_y, 0);
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
            int tile_y = grid_tile->z;
            int tile_level = grid_tile->level;

            element = &elements[tile_coord];
            element->generation = gen++;
            element->q_count--;

            if( element->q_count > 0 )
            {
                continue;
            }

            if( (grid_tile->flags & GRID_TILE_FLAG_BRIDGE) != 0 )
            {
                element = &elements[tile_coord];
                element->step = E_STEP_DONE;
                continue;
            }

            // printf("Tile %d, %d Coord %d\n", tile_x, tile_y, tile_coord);
            // printf("Min %d, %d\n", min_draw_x, min_draw_y);
            // printf("Max %d, %d\n", max_draw_x, max_draw_y);

            assert(tile_x >= min_draw_x);
            assert(tile_x < max_draw_x);
            assert(tile_y >= min_draw_y);
            assert(tile_y < max_draw_y);

            element = &elements[tile_coord];

            /**
             * If this is a loc revisit, then we need to verify adjacent tiles are done.
             *
             */

            if( element->step == E_STEP_VERIFY_FURTHER_TILES_DONE_UNLESS_SPANNED )
            {
                if( tile_level > 0 )
                {
                    other = &elements[MAP_TILE_COORD(tile_x, tile_y, tile_level - 1)];

                    if( other->step != E_STEP_DONE )
                    {
                        goto done;
                    }
                }

                if( tile_x >= camera_tile_x && tile_x < max_draw_x )
                {
                    if( tile_x + 1 < max_draw_x )
                    {
                        other = &elements[MAP_TILE_COORD(tile_x + 1, tile_y, tile_level)];

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
                        other = &elements[MAP_TILE_COORD(tile_x - 1, tile_y, tile_level)];
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

                if( tile_y >= camera_tile_y && tile_y < max_draw_y )
                {
                    if( tile_y + 1 < max_draw_y )
                    {
                        other = &elements[MAP_TILE_COORD(tile_x, tile_y + 1, tile_level)];
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
                if( tile_y <= camera_tile_y && tile_y > min_draw_y )
                {
                    if( tile_y - 1 >= min_draw_y )
                    {
                        other = &elements[MAP_TILE_COORD(tile_x, tile_y - 1, tile_level)];
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

                int near_walls = near_wall_flags(camera_tile_x, camera_tile_y, tile_x, tile_y);
                int far_walls = ~near_walls;
                element->near_wall_flags |= near_walls;

                if( grid_tile->bridge_tile != -1 )
                {
                    bridge_underpass_tile = &scene->grid_tiles[grid_tile->bridge_tile];
                    ops[op_count++] = (struct SceneOp){
                        .op = SCENE_OP_TYPE_DRAW_GROUND,
                        .x = tile_x,
                        .z = tile_y,
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
                            .op = SCENE_OP_TYPE_DRAW_GROUND_DECOR,
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
                    .z = tile_y,
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

                if( grid_tile->wall_decor != -1 )
                {
                    loc = &scene->locs[grid_tile->wall_decor];
                    if( loc->_wall_decor.through_wall_flags != 0 )
                    {
                        int x_diff = loc->chunk_pos_x - camera_tile_x;
                        int y_diff = loc->chunk_pos_y - camera_tile_y;

                        // TODO: Document what this is doing.
                        int x_near = x_diff;
                        if( loc->_wall_decor.side == WALL_CORNER_NORTHEAST ||
                            loc->_wall_decor.side == WALL_CORNER_SOUTHEAST )
                            x_near = -x_diff;

                        int y_near = y_diff;
                        if( loc->_wall_decor.side == WALL_CORNER_SOUTHEAST ||
                            loc->_wall_decor.side == WALL_CORNER_SOUTHWEST )
                            y_near = -y_diff;

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
                                                .angle = 768 },
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
                                                .angle = 768 },
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

                ops[op_count++] = (struct SceneOp){
                    .op = SCENE_OP_TYPE_DBG_TILE,
                    .x = tile_x,
                    .z = tile_y,
                    .level = tile_level,
                    ._dbg = { .step = E_STEP_GROUND, .q_count = element->q_count },
                };

                if( tile_x < camera_tile_x )
                {
                    if( tile_x + 1 < max_draw_x )
                    {
                        int idx = MAP_TILE_COORD(tile_x + 1, tile_y, tile_level);
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
                        int idx = MAP_TILE_COORD(tile_x - 1, tile_y, tile_level);
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

                if( tile_y < camera_tile_y )
                {
                    if( tile_y + 1 < max_draw_y )
                    {
                        int idx = MAP_TILE_COORD(tile_x, tile_y + 1, tile_level);
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

                if( tile_y > camera_tile_y )
                {
                    if( tile_y - 1 >= min_draw_y )
                    {
                        int idx = MAP_TILE_COORD(tile_x, tile_y - 1, tile_level);
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
                    int min_tile_y = loc->chunk_pos_y;
                    int max_tile_x = min_tile_x + loc->size_x - 1;
                    int max_tile_y = min_tile_y + loc->size_y - 1;

                    if( max_tile_x > max_draw_x - 1 )
                        max_tile_x = max_draw_x - 1;
                    if( max_tile_y > max_draw_y - 1 )
                        max_tile_y = max_draw_y - 1;
                    if( min_tile_x < min_draw_x )
                        min_tile_x = min_draw_x;
                    if( min_tile_y < min_draw_y )
                        min_tile_y = min_draw_y;

                    for( int other_tile_x = min_tile_x; other_tile_x <= max_tile_x; other_tile_x++ )
                    {
                        for( int other_tile_y = min_tile_y; other_tile_y <= max_tile_y;
                             other_tile_y++ )
                        {
                            other =
                                &elements[MAP_TILE_COORD(other_tile_x, other_tile_y, tile_level)];
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
                    int min_tile_y = loc->chunk_pos_y;
                    int max_tile_x = min_tile_x + loc->size_x - 1;
                    int max_tile_y = min_tile_y + loc->size_y - 1;

                    int next_prio = 0;
                    if( loc->size_x > 1 || loc->size_y > 1 )
                    {
                        next_prio = loc->size_x > loc->size_y ? loc->size_x : loc->size_y;
                    }

                    if( max_tile_x > max_draw_x - 1 )
                        max_tile_x = max_draw_x - 1;
                    if( max_tile_y > max_draw_y - 1 )
                        max_tile_y = max_draw_y - 1;
                    if( min_tile_x < min_draw_x )
                        min_tile_x = min_draw_x;
                    if( min_tile_y < min_draw_y )
                        min_tile_y = min_draw_y;

                    for( int other_tile_x = min_tile_x; other_tile_x <= max_tile_x; other_tile_x++ )
                    {
                        for( int other_tile_y = min_tile_y; other_tile_y <= max_tile_y;
                             other_tile_y++ )
                        {
                            int idx = MAP_TILE_COORD(other_tile_x, other_tile_y, tile_level);
                            other = &elements[idx];
                            if( other_tile_x != tile_x || other_tile_y != tile_y )
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
                if( tile_x >= camera_tile_x && tile_x < max_draw_x )
                {
                    if( tile_x + 1 < max_draw_x )
                    {
                        other = &elements[MAP_TILE_COORD(tile_x + 1, tile_y, tile_level)];

                        // If we are not spanned by the tile, then we need to verify it is done.
                        if( other->step != E_STEP_DONE )
                        {
                            goto done;
                        }
                    }
                }

                if( tile_x <= camera_tile_x && tile_x > min_draw_x )
                {
                    if( tile_x - 1 >= min_draw_x )
                    {
                        other = &elements[MAP_TILE_COORD(tile_x - 1, tile_y, tile_level)];
                        if( other->step != E_STEP_DONE )
                        {
                            goto done;
                        }
                    }
                }

                if( tile_y >= camera_tile_y && tile_y < max_draw_y )
                {
                    if( tile_y + 1 < max_draw_y )
                    {
                        other = &elements[MAP_TILE_COORD(tile_x, tile_y + 1, tile_level)];
                        if( other->step != E_STEP_DONE )
                        {
                            goto done;
                        }
                    }
                }
                if( tile_y <= camera_tile_y && tile_y > min_draw_y )
                {
                    if( tile_y - 1 >= min_draw_y )
                    {
                        other = &elements[MAP_TILE_COORD(tile_x, tile_y - 1, tile_level)];
                        if( other->step != E_STEP_DONE )
                        {
                            goto done;
                        }
                    }
                }

                if( tile_level < MAP_TERRAIN_Z - 1 )
                {
                    int idx = MAP_TILE_COORD(tile_x, tile_y, tile_level + 1);
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
                        int idx = MAP_TILE_COORD(tile_x + 1, tile_y, tile_level);
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
                        int idx = MAP_TILE_COORD(tile_x - 1, tile_y, tile_level);
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

                if( tile_y < camera_tile_y )
                {
                    if( tile_y + 1 < max_draw_y )
                    {
                        int idx = MAP_TILE_COORD(tile_x, tile_y + 1, tile_level);
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

                if( tile_y > camera_tile_y )
                {
                    if( tile_y - 1 >= min_draw_y )
                    {
                        int idx = MAP_TILE_COORD(tile_x, tile_y - 1, tile_level);
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
                        int y_diff = loc->chunk_pos_y - camera_tile_y;

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
                                                .angle = 768 },
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
                                                .angle = 768 },
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

                ops[op_count++] = (struct SceneOp){
                    .op = SCENE_OP_TYPE_DBG_TILE,
                    .x = tile_x,
                    .z = tile_y,
                    .level = tile_level,
                    ._dbg = { .step = E_STEP_DONE, .q_count = element->q_count },
                };
            }

            if( element->step == E_STEP_DONE )
            {
            }

        done:;
        }
    }

    // for( int z = 0; z < MAP_TERRAIN_Z; z++ )
    // {
    //     for( int x = 0; x < MAP_TERRAIN_X; x++ )
    //     {
    //         for( int y = 0; y < MAP_TERRAIN_Y; y++ )
    //         {
    //             if( x < min_draw_x || x >= max_draw_x || y < min_draw_y || y >= max_draw_y )
    //                 continue;

    //             int idx = MAP_TILE_COORD(x, y, z);
    //             struct GridTile* grid_tile = &scene->grid_tiles[idx];
    //             struct SceneElement* element = &elements[idx];
    //             if( element->step != E_STEP_DONE )
    //             {
    //                 printf("Element %d, %d, %d not done\n", x, y, z);
    //             }
    //             if( element->remaining_locs != 0 )
    //             {
    //                 printf(
    //                     "Element %d, %d, %d has %d remaining locs\n",
    //                     x,
    //                     y,
    //                     z,
    //                     element->remaining_locs);
    //             }
    //         }
    //     }
    // }

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
        0,
        0,
        0,
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
    // int* screen_vertices_x = (int*)malloc(20 * sizeof(int));
    // int* screen_vertices_y = (int*)malloc(20 * sizeof(int));
    // int* screen_vertices_z = (int*)malloc(20 * sizeof(int));

    // // These are the vertices prior to perspective correction.
    // int* ortho_vertices_x = (int*)malloc(20 * sizeof(int));
    // int* ortho_vertices_y = (int*)malloc(20 * sizeof(int));
    // int* ortho_vertices_z = (int*)malloc(20 * sizeof(int));

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
        if( k == number_to_render - 1 )
        {
            int iii = 0;
        }

        int i = offset + k;
        if( i >= op_count )
            break;

        struct SceneOp* op = &ops[i];
        grid_tile = &scene->grid_tiles[MAP_TILE_COORD(op->x, op->z, op->level)];

        // if( op->level != 1 && op->level != 2 )
        //     continue;
        // if( op->x != 29 || op->z != 3 || op->level != 0 )
        //     continue;

        // Oak tree on 19, 5
        // int target_x = 19;
        // int target_z = 5;

        // int radius = 1;
        // if( (op->x - target_x) * (op->x - target_x) + (op->z - target_z) * (op->z - target_z)
        // >
        //     radius * radius )
        //     continue;

        // if( op->x >= 22 && op->x <= 25 && op->z >= 3 && op->z <= 10 )
        // {
        // }
        // else
        // {
        //     continue;
        // };

        // || op->level != 2
        // if( op->x != 6 || op->z != 11 )
        //     continue;

        switch( op->op )
        {
        case SCENE_OP_TYPE_DRAW_GROUND:
        {
            tile = grid_tile->tile;
            if( !tile || !tile->valid_faces )
                break;
            // if( !tile || !tile->valid_faces )
            // {
            //     tile = scene->grid_tiles[MAP_TILE_COORD(1, 1, 0)].tile;

            //     render_scene_tile(
            //         screen_vertices_x,
            //         screen_vertices_y,
            //         screen_vertices_z,
            //         ortho_vertices_x,
            //         ortho_vertices_y,
            //         ortho_vertices_z,
            //         pixel_buffer,
            //         width,
            //         height,
            //         near_plane_z,
            //         op->x,
            //         op->z,
            //         op->level,
            //         camera_x,
            //         camera_y,
            //         camera_z,
            //         camera_pitch,
            //         camera_yaw,
            //         camera_roll,
            //         fov,
            //         tile,
            //         textures_cache,
            //         NULL);
            //     break;
            // }

            int tile_x = tile->chunk_pos_x;
            int tile_y = tile->chunk_pos_y;
            int tile_z = tile->chunk_pos_level;

            int* color_override_hsl16_nullable = NULL;
            if( op->_ground.override_color )
            {
                color_override_hsl16_nullable = (int*)malloc(sizeof(int) * tile->face_count);
                for( int j = 0; j < tile->face_count; j++ )
                {
                    color_override_hsl16_nullable[j] = op->_ground.color_hsl16;
                }
            }

            // dbg_tile(
            //     pixel_buffer,
            //     width,
            //     height,
            //     near_plane_z,
            //     tile_x,
            //     tile_y,
            //     tile_z,
            //     0x00FFFFFF,
            //     camera_x,
            //     camera_y,
            //     camera_z,
            //     camera_pitch,
            //     camera_yaw,
            //     camera_roll,
            //     fov);
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
                tile_x,
                tile_y,
                tile_z,
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

            // if( loc->size_x > 1 || loc->size_y > 1 )
            // {
            //     printf("Sized %d\n", loc->size_x * loc->size_y);
            // }
            // else
            // {
            //     break;
            // }

            assert(model_index >= 0);
            assert(model_index < scene->models_length);

            model = &scene->models[model_index];

            // 1571 is an oak tree
            // if( model->model_ids && model->model_ids[0] == 1571 )
            // {
            //     printf("Drawing oak tree at %d, %d, %d\n", op->x, op->z, op->level);

            //     model->region_x = 0;
            //     model->region_y = 768;
            //     model->region_z = 200;
            //     render_scene_model(
            //         pixel_buffer,
            //         width,
            //         height,
            //         near_plane_z,
            //         0,
            //         0,
            //         0,
            //         0,
            //         0,
            //         camera_yaw,
            //         0,
            //         fov,
            //         model,
            //         textures_cache);
            //     continue;
            // }
            // else
            // {
            //     continue;
            // }

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
                op->_wall_decor.angle,
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
            // break;
            int tile_x = op->x;
            int tile_y = op->z;
            int tile_z = op->level;

            int red_adjust = (op->_dbg.q_count + 1) * 0x00220000;

            int color_rgb = 0x99FFFFFF;
            switch( op->_dbg.step )
            {
            case E_STEP_GROUND:
                color_rgb = 0xFF00DDDD + red_adjust;
                break;
            case E_STEP_NOTIFY_ADJACENT_TILES:
                color_rgb = 0xFF111111;
                break;
            case E_STEP_LOCS:
                color_rgb = 0xFF00FF00 + red_adjust;
                break;
            case E_STEP_DONE:
                color_rgb = 0xFFFFFFFF - (tile_z * 0x00111111);
                break;
            }

            dbg_tile(
                pixel_buffer,
                width,
                height,
                near_plane_z,
                tile_x,
                tile_y,
                tile_z,
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

        // free(screen_vertices_x);
        // free(screen_vertices_y);
        // free(screen_vertices_z);
        // free(ortho_vertices_x);
        // free(ortho_vertices_y);
        // free(ortho_vertices_z);
    }
}
