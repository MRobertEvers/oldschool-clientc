#include "dash.h"

#include "dashmap.h"
#include "render.h"
#include "shared_tables.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

// clang-format off
#include "render_gouraud.u.c"
#include "render_flat.u.c"
#include "render_texture.u.c"
#include "projection.u.c"
#include "projection_simd.u.c"
// clang-format on

struct DashTextureEntry
{
    int id;
    struct DashTexture* texture;
};

struct DashGraphics
{
    int screen_vertices_x[4096];
    int screen_vertices_y[4096];
    int screen_vertices_z[4096];
    int orthographic_vertices_x[4096];
    int orthographic_vertices_y[4096];
    int orthographic_vertices_z[4096];

    short tmp_depth_face_count[1500];
    short tmp_depth_faces[1500 * 512];
    short tmp_priority_face_count[12];
    short tmp_priority_depth_sum[12];
    short tmp_priority_faces[12 * 2000];
    int tmp_flex_prio11_face_to_depth[1024];
    int tmp_flex_prio12_face_to_depth[512];
    int tmp_face_order[1024];

    struct DashMap* textures_hmap;
};

struct DashGraphics*
dash_new()
{
    init_hsl16_to_rgb_table();
    init_sin_table();
    init_cos_table();
    init_tan_table();
    init_reciprocal16();

    struct DashGraphics* dash = (struct DashGraphics*)malloc(sizeof(struct DashGraphics));
    if( dash == NULL )
        return NULL;
    memset(dash, 0, sizeof(struct DashGraphics));

    struct DashMapConfig config = {
        .buffer = malloc(4096),
        .buffer_size = 4096,
        .key_size = sizeof(int),
        .entry_size = sizeof(struct DashTextureEntry),
    };
    dash->textures_hmap = dashmap_new(&config, 0);

    return dash;
}

void //
dash_free(struct DashGraphics* dash)
{
    free(dash);
}

#define DASHCULL_VISIBLE 1
#define DASHCULL_NOT_VISIBLE 0

static int
dash3d_fast_cull(
    struct DashViewPort* view_port,
    struct DashModel* model,
    struct DashPosition* position,
    struct DashCamera* camera,
    struct ProjectedVertex* projected_vertex)
{
    int model_yaw = position->yaw;
    int scene_x = position->x;
    int scene_y = position->y;
    int scene_z = position->z;

    int camera_pitch = camera->pitch;
    int camera_yaw = camera->yaw;
    int near_plane_z = camera->near_plane_z;

    project_orthographic_fast(
        projected_vertex, 0, 0, 0, model_yaw, scene_x, scene_y, scene_z, camera_pitch, camera_yaw);

    int model_edge_radius = model->bounds_cylinder->radius;

    // int a = (scene_z * cos_camera_yaw - scene_x * sin_camera_yaw) >> 16;
    // // b is the z projection of the models origin (imagine a vertex at x=0,y=0 and z=0).
    // // So the depth is the z projection distance from the origin of the model.
    // int b = (scene_y * sin_camera_pitch + a * cos_camera_pitch) >> 16;

    /**
     * These checks are a significant performance improvement.
     */
    int mid_z = projected_vertex->z;
    int max_z = model_edge_radius + mid_z;
    if( max_z < near_plane_z )
    {
        // The edge of the model that is farthest from the camera is too close to the near plane.
        return DASHCULL_NOT_VISIBLE;
    }

    if( mid_z > 3500 )
    {
        // Model too far away.
        return DASHCULL_NOT_VISIBLE;
    }

    int mid_x = projected_vertex->x;
    int mid_y = projected_vertex->y;

    if( mid_z < near_plane_z )
        mid_z = near_plane_z;

    int ortho_screen_x_min = mid_x - model_edge_radius;
    int ortho_screen_x_max = mid_x + model_edge_radius;

    int screen_x_min_unoffset =
        project_divide(ortho_screen_x_min, mid_z + model_edge_radius, camera->fov_rpi2048);
    int screen_x_max_unoffset =
        project_divide(ortho_screen_x_max, mid_z + model_edge_radius, camera->fov_rpi2048);
    int screen_edge_width = view_port->width >> 1;

    if( screen_x_min_unoffset > screen_edge_width || screen_x_max_unoffset < -screen_edge_width )
    {
        // All parts of the model left or right edges are projected off screen.
        return DASHCULL_NOT_VISIBLE;
    }

    // compute_screen_y_aabb(
    //     aabb,
    //     mid_y,
    //     mid_z,
    //     model_edge_radius,
    //     model_cylinder_center_to_top_edge,
    //     model_cylinder_center_to_bottom_edge,
    //     camera_fov,
    //     camera_pitch,
    //     screen_height);

    int model_center_to_top_edge = model->bounds_cylinder->center_to_top_edge;

    int model_center_to_bottom_edge =
        (model->bounds_cylinder->center_to_bottom_edge * g_cos_table[camera_pitch] >> 16) +
        (model_edge_radius * g_sin_table[camera_pitch] >> 16);

    int screen_y_min_unoffset =
        project_divide(mid_y - abs(model_center_to_bottom_edge), mid_z, camera->fov_rpi2048);
    int screen_y_max_unoffset =
        project_divide(mid_y + abs(model_center_to_top_edge), mid_z, camera->fov_rpi2048);
    int screen_edge_height = view_port->height >> 1;
    if( screen_y_min_unoffset > screen_edge_height || screen_y_max_unoffset < -screen_edge_height )
    {
        // All parts of the model top or bottom edges are projected off screen.
        return DASHCULL_NOT_VISIBLE;
    }

    return DASHCULL_VISIBLE;
}

static int
dash3d_aabb_cull(struct DashAABB* aabb, struct DashViewPort* view_port, struct DashCamera* camera)
{
    int screen_width = view_port->width;
    int screen_height = view_port->height;

    if( aabb->min_screen_x >= screen_width )
        return DASHCULL_NOT_VISIBLE;
    if( aabb->min_screen_y >= screen_height )
        return DASHCULL_NOT_VISIBLE;
    if( aabb->max_screen_x < 0 )
        return DASHCULL_NOT_VISIBLE;
    if( aabb->max_screen_y < 0 )
        return DASHCULL_NOT_VISIBLE;

    return DASHCULL_VISIBLE;
}

static void
dash3d_calculate_aabb(
    struct DashAABB* aabb,
    struct DashModel* model,
    struct DashPosition* position,
    struct DashViewPort* view_port,
    struct DashCamera* camera)
{
    int model_yaw = position->yaw;
    int model_edge_radius = model->bounds_cylinder->radius;
    int model_center_to_top_edge = model->bounds_cylinder->center_to_top_edge;
    int model_center_to_bottom_edge = model->bounds_cylinder->center_to_bottom_edge;
    int model_min_y = model->bounds_cylinder->min_y;
    int model_max_y = model->bounds_cylinder->max_y;
    int screen_edge_width = view_port->width >> 1;
    int screen_edge_height = view_port->height >> 1;
    int scene_x = position->x;
    int scene_y = position->y;
    int scene_z = position->z;
    int near_plane_z = camera->near_plane_z;
    int camera_pitch = camera->pitch;
    int camera_yaw = camera->yaw;
    int camera_fov = camera->fov_rpi2048;

    int bb_x[8];
    int bb_y[8];
    int bb_z[8];

    int mz = 0;
    int my = 0;
    int mx = 0;
    bb_x[0] = mx + model_edge_radius;
    bb_x[1] = mx + model_edge_radius;
    bb_x[2] = mx + model_edge_radius;
    bb_x[3] = mx + model_edge_radius;
    bb_x[4] = mx - model_edge_radius;
    bb_x[5] = mx - model_edge_radius;
    bb_x[6] = mx - model_edge_radius;
    bb_x[7] = mx - model_edge_radius;

    bb_y[0] = my + model_min_y;
    bb_y[1] = my + model_min_y;
    bb_y[2] = my + model_max_y;
    bb_y[3] = my + model_max_y;
    bb_y[4] = my + model_min_y;
    bb_y[5] = my + model_min_y;
    bb_y[6] = my + model_max_y;
    bb_y[7] = my + model_max_y;

    bb_z[0] = mz + model_edge_radius;
    bb_z[1] = mz - model_edge_radius;
    bb_z[2] = mz + model_edge_radius;
    bb_z[3] = mz - model_edge_radius;
    bb_z[4] = mz + model_edge_radius;
    bb_z[5] = mz - model_edge_radius;
    bb_z[6] = mz + model_edge_radius;
    bb_z[7] = mz - model_edge_radius;

    int sc_x[8] = { 0 };
    int sc_y[8] = { 0 };
    int sc_z[8] = { 0 };
    int o_x[8] = { 0 };
    int o_y[8] = { 0 };
    int o_z[8] = { 0 };
    project_vertices_array(
        o_x,
        o_y,
        o_z,
        sc_x,
        sc_y,
        sc_z,
        bb_x,
        bb_y,
        bb_z,
        8,
        model_yaw,
        0,
        scene_x,
        scene_y,
        scene_z,
        near_plane_z,
        camera_fov,
        camera_pitch,
        camera_yaw);

    aabb->min_screen_x = sc_x[0];
    aabb->min_screen_y = sc_y[0];
    aabb->max_screen_x = sc_x[0];
    aabb->max_screen_y = sc_y[0];
    for( int i = 0; i < 8; i++ )
    {
        if( sc_x[i] < aabb->min_screen_x )
            aabb->min_screen_x = sc_x[i];
        if( sc_x[i] > aabb->max_screen_x )
            aabb->max_screen_x = sc_x[i];
        if( sc_y[i] < aabb->min_screen_y )
            aabb->min_screen_y = sc_y[i];
        if( sc_y[i] > aabb->max_screen_y )
            aabb->max_screen_y = sc_y[i];
    }

    aabb->min_screen_x += screen_edge_width;
    aabb->min_screen_y += screen_edge_height;
    aabb->max_screen_x += screen_edge_width;
    aabb->max_screen_y += screen_edge_height;
}

static const int g_empty_texture_texels[128 * 128] = { 0 };

enum FaceType
{
    FACE_TYPE_GOURAUD,
    FACE_TYPE_FLAT,
    FACE_TYPE_TEXTURED,
    FACE_TYPE_TEXTURED_FLAT_SHADE,
};

void
dash3d_raster_model_face(
    int* pixel_buffer,
    int face,
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
    int near_plane_z,
    int screen_width,
    int screen_height,
    int camera_fov,
    struct DashMap* textures_hmap)
{
    struct DashTextureEntry* texture_entry = NULL;
    struct Texture* texture = NULL;

    // TODO: FaceTYpe is wrong, type 2 is hidden, 3 is black, 0 is gouraud, 1 is flat.
    enum FaceType type = face_infos ? (face_infos[face] & 0x3) : FACE_TYPE_GOURAUD;
    if( type == 2 )
    {
        return;
    }
    assert(type >= 0 && type <= 3);

    int color_a = colors_a[face];
    int color_b = colors_b[face];
    int color_c = colors_c[face];

    if( color_c == -2 )
    {
        return;
    }

    int tp_vertex = -1;
    int tm_vertex = -1;
    int tn_vertex = -1;

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

    assert(face < num_faces);

    int alpha = face_alphas_nullable ? (face_alphas_nullable[face]) : 0xFF;

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
    int texture_size = 0;
    int texture_opaque = true;
    if( face_textures && face_textures[face] != -1 )
    {
        texture_id = face_textures[face];
        // gamma 0.8 is the default in os1
        texture_entry =
            (struct DashTextureEntry*)dashmap_search(textures_hmap, &texture_id, DASHMAP_FIND);
        assert(texture_entry != NULL);
        texture = texture_entry->texture;

        texels = texture->texels;
        texture_size = texture->width;
        texture_opaque = texture->opaque;

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

            raster_face_gouraud(
                pixel_buffer,
                face,
                face_indices_a,
                face_indices_b,
                face_indices_c,
                vertex_x,
                vertex_y,
                vertex_z,
                orthographic_vertex_x_nullable,
                orthographic_vertex_y_nullable,
                orthographic_vertex_z_nullable,
                colors_a,
                colors_b,
                colors_c,
                face_alphas_nullable,
                near_plane_z,
                offset_x,
                offset_y,
                screen_width,
                screen_height);

            break;
        case FACE_TYPE_FLAT:
            // Skip triangle if any vertex was clipped

            raster_face_flat(
                pixel_buffer,
                face,
                face_indices_a,
                face_indices_b,
                face_indices_c,
                vertex_x,
                vertex_y,
                vertex_z,
                orthographic_vertex_x_nullable,
                orthographic_vertex_y_nullable,
                orthographic_vertex_z_nullable,
                colors_a,
                face_alphas_nullable,
                near_plane_z,
                offset_x,
                offset_y,
                screen_width,
                screen_height);

            break;
        case FACE_TYPE_TEXTURED:
        textured:;
            assert(face_p_coordinate_nullable != NULL);
            assert(face_m_coordinate_nullable != NULL);
            assert(face_n_coordinate_nullable != NULL);
            assert(orthographic_vertex_x_nullable != NULL);
            assert(orthographic_vertex_y_nullable != NULL);
            assert(orthographic_vertex_z_nullable != NULL);

            if( face_texture_coords && face_texture_coords[face] != -1 )
            {
                texture_face = face_texture_coords[face];

                tp_vertex = face_p_coordinate_nullable[texture_face];
                tm_vertex = face_m_coordinate_nullable[texture_face];
                tn_vertex = face_n_coordinate_nullable[texture_face];
            }
            else
            {
                texture_face = face;
                tp_vertex = face_indices_a[texture_face];
                tm_vertex = face_indices_b[texture_face];
                tn_vertex = face_indices_c[texture_face];
            }
            // texture_id = face_textures[index];
            // texture_face = face_infos[index] >> 2;
            // texture_face = face_texture_coords[index];

            assert(tp_vertex > -1);
            assert(tm_vertex > -1);
            assert(tn_vertex > -1);

            assert(tp_vertex < num_vertices);
            assert(tm_vertex < num_vertices);
            assert(tn_vertex < num_vertices);

            raster_face_texture_blend(
                pixel_buffer,
                screen_width,
                screen_height,
                camera_fov,
                face,
                tp_vertex,
                tm_vertex,
                tn_vertex,
                face_indices_a,
                face_indices_b,
                face_indices_c,
                vertex_x,
                vertex_y,
                vertex_z,
                orthographic_vertex_x_nullable,
                orthographic_vertex_y_nullable,
                orthographic_vertex_z_nullable,
                colors_a,
                colors_b,
                colors_c,
                texels,
                texture_size,
                texture_opaque,
                near_plane_z,
                offset_x,
                offset_y);

            // tp_x = orthographic_vertex_x_nullable[tp_face];
            // tp_y = orthographic_vertex_y_nullable[tp_face];
            // tp_z = orthographic_vertex_z_nullable[tp_face];

            // tm_x = orthographic_vertex_x_nullable[tm_face];
            // tm_y = orthographic_vertex_y_nullable[tm_face];
            // tm_z = orthographic_vertex_z_nullable[tm_face];

            // tn_x = orthographic_vertex_x_nullable[tn_face];
            // tn_y = orthographic_vertex_y_nullable[tn_face];
            // tn_z = orthographic_vertex_z_nullable[tn_face];

            // if( texture->opaque )
            // {
            //     raster_texture_opaque_blend_lerp8(
            //         pixel_buffer,
            //         screen_width,
            //         screen_height,
            //         x1,
            //         x2,
            //         x3,
            //         y1,
            //         y2,
            //         y3,
            //         tp_x,
            //         tm_x,
            //         tn_x,
            //         tp_y,
            //         tm_y,
            //         tn_y,
            //         tp_z,
            //         tm_z,
            //         tn_z,
            //         color_a,
            //         color_b,
            //         color_c,
            //         texels,
            //         128);
            // }
            // else
            // {
            //     raster_texture_transparent_blend_lerp8(
            //         pixel_buffer,
            //         screen_width,
            //         screen_height,
            //         x1,
            //         x2,
            //         x3,
            //         y1,
            //         y2,
            //         y3,
            //         tp_x,
            //         tm_x,
            //         tn_x,
            //         tp_y,
            //         tm_y,
            //         tn_y,
            //         tp_z,
            //         tm_z,
            //         tn_z,
            //         color_a,
            //         color_b,
            //         color_c,
            //         texels,
            //         128);
            // }

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

            if( face_texture_coords && face_texture_coords[face] != -1 )
            {
                texture_face = face_texture_coords[face];

                tp_vertex = face_p_coordinate_nullable[texture_face];
                tm_vertex = face_m_coordinate_nullable[texture_face];
                tn_vertex = face_n_coordinate_nullable[texture_face];
            }
            else
            {
                texture_face = face;
                tp_vertex = face_indices_a[texture_face];
                tm_vertex = face_indices_b[texture_face];
                tn_vertex = face_indices_c[texture_face];
            }
            // texture_id = face_textures[index];
            // texture_face = face_infos[index] >> 2;
            // texture_face = face_texture_coords[index];

            assert(tp_vertex > -1);
            assert(tm_vertex > -1);
            assert(tn_vertex > -1);

            assert(tp_vertex < num_vertices);
            assert(tm_vertex < num_vertices);
            assert(tn_vertex < num_vertices);

            raster_face_texture_flat(
                pixel_buffer,
                screen_width,
                screen_height,
                camera_fov,
                face,
                tp_vertex,
                tm_vertex,
                tn_vertex,
                face_indices_a,
                face_indices_b,
                face_indices_c,
                vertex_x,
                vertex_y,
                vertex_z,
                orthographic_vertex_x_nullable,
                orthographic_vertex_y_nullable,
                orthographic_vertex_z_nullable,
                colors_a,
                texels,
                texture_size,
                texture_opaque,
                near_plane_z,
                offset_x,
                offset_y);

            // tp_x = orthographic_vertex_x_nullable[tp_face];
            // tp_y = orthographic_vertex_y_nullable[tp_face];
            // tp_z = orthographic_vertex_z_nullable[tp_face];

            // tm_x = orthographic_vertex_x_nullable[tm_face];
            // tm_y = orthographic_vertex_y_nullable[tm_face];
            // tm_z = orthographic_vertex_z_nullable[tm_face];

            // tn_x = orthographic_vertex_x_nullable[tn_face];
            // tn_y = orthographic_vertex_y_nullable[tn_face];
            // tn_z = orthographic_vertex_z_nullable[tn_face];

            // if( texture->opaque )
            // {
            //     raster_texture_opaque_lerp8(
            //         pixel_buffer,
            //         screen_width,
            //         screen_height,
            //         x1,
            //         x2,
            //         x3,
            //         y1,
            //         y2,
            //         y3,
            //         tp_x,
            //         tm_x,
            //         tn_x,
            //         tp_y,
            //         tm_y,
            //         tn_y,
            //         tp_z,
            //         tm_z,
            //         tn_z,
            //         color_a,
            //         texels,
            //         128);
            // }
            // else
            // {
            //     raster_texture_transparent_lerp8(
            //         pixel_buffer,
            //         screen_width,
            //         screen_height,
            //         x1,
            //         x2,
            //         x3,
            //         y1,
            //         y2,
            //         y3,
            //         tp_x,
            //         tm_x,
            //         tn_x,
            //         tp_y,
            //         tm_y,
            //         tn_y,
            //         tp_z,
            //         tm_z,
            //         tn_z,
            //         color_a,
            //         texels,
            //         128);
            // }

            break;
        }
    }
}

static inline int
div3_fast(int x)
{
    // See benchmark_div3_trick.c for more details.
    return (x * 21845) >> 16;
}

static inline int
bucket_sort_by_average_depth(
    short* face_depth_buckets,
    short* face_depth_bucket_counts,
    int model_min_depth,
    int num_faces,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int* face_a,
    int* face_b,
    int* face_c)
{
    int min_depth = INT_MAX;
    int max_depth = INT_MIN;

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
            int depth_average = div3_fast(za + zb + zc) + model_min_depth;

            if( depth_average < 1500 && depth_average > 0 )
            {
                int bucket_index = face_depth_bucket_counts[depth_average]++;
                face_depth_buckets[(depth_average << 9) + bucket_index] = f;

                if( depth_average < min_depth )
                    min_depth = depth_average;
                if( depth_average > max_depth )
                    max_depth = depth_average;
            }
        }
    }

    return (min_depth) | (max_depth << 16);
}

static inline void
parition_faces_by_priority(
    short* face_priority_buckets,
    short* face_priority_bucket_counts,
    short* face_depth_buckets,
    short* face_depth_bucket_counts,
    int num_faces,
    int* face_priorities,
    int depth_lower_bound,
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
    for( int depth = depth_upper_bound; depth >= depth_lower_bound && depth < 1500; depth-- )
    {
        int face_count = face_depth_bucket_counts[depth];
        if( face_count == 0 )
            continue;

        short* faces = &face_depth_buckets[depth << 9];
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
static inline int
sort_face_draw_order(
    short* priority_depths,
    int* flex_prio11_face_to_depth,
    int* flex_prio12_face_to_depth,
    int* face_draw_order,
    short* face_depth_buckets,
    short* face_depth_bucket_counts,
    short* face_priority_buckets,
    short* face_priority_bucket_counts,
    int num_faces,
    int* face_priorities,
    int depth_lower_bound,
    int depth_upper_bound)
{
    int counts[12] = { 0 };
    for( int depth = depth_upper_bound; depth >= depth_lower_bound && depth < 1500; depth-- )
    {
        int face_count = face_depth_bucket_counts[depth];
        if( face_count == 0 )
            continue;

        short* faces = &face_depth_buckets[depth << 9];
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

void //
dash3d_render_model( //
    struct DashGraphics* dash, 
    struct DashModel* model,
    struct DashPosition* position,
    struct DashViewPort* view_port,
    struct DashCamera* camera,
    int* pixel_buffer
)
{
    struct DashAABB aabb;
    struct ProjectedVertex center_projection;
    int cull = DASHCULL_VISIBLE;

    if( model == NULL || model->vertex_count == 0 || model->face_count == 0 )
        return;

    cull = dash3d_fast_cull(view_port, model, position, camera, &center_projection);
    if( cull == DASHCULL_NOT_VISIBLE )
        return;

    dash3d_calculate_aabb(&aabb, model, position, view_port, camera);

    cull = dash3d_aabb_cull(&aabb, view_port, camera);
    if( cull == DASHCULL_NOT_VISIBLE )
        return;

    project_vertices_array(
        dash->orthographic_vertices_x,
        dash->orthographic_vertices_y,
        dash->orthographic_vertices_z,
        dash->screen_vertices_x,
        dash->screen_vertices_y,
        dash->screen_vertices_z,
        model->vertices_x,
        model->vertices_y,
        model->vertices_z,
        model->vertex_count,
        position->yaw,
        center_projection.z,
        position->x,
        position->y,
        position->z,
        camera->near_plane_z,
        camera->fov_rpi2048,
        camera->pitch,
        camera->yaw);

    int model_min_depth = model->bounds_cylinder->min_z_depth_any_rotation;
    memset(
        dash->tmp_depth_face_count,
        0,
        (model_min_depth * 2 + 1) * sizeof(dash->tmp_depth_face_count[0]));

    int bounds = bucket_sort_by_average_depth(
        dash->tmp_depth_faces,
        dash->tmp_depth_face_count,
        model_min_depth,
        model->face_count,
        dash->screen_vertices_x,
        dash->screen_vertices_y,
        dash->screen_vertices_z,
        model->face_indices_a,
        model->face_indices_b,
        model->face_indices_c);

    model_min_depth = bounds & 0xFFFF;
    int model_max_depth = bounds >> 16;

    if( !model->face_priorities )
    {
        for( int depth = model_max_depth; depth < 1500 && depth >= model_min_depth; depth-- )
        {
            int bucket_count = dash->tmp_depth_face_count[depth];
            if( bucket_count == 0 )
                continue;

            short* faces = &dash->tmp_depth_faces[depth << 9];
            for( int j = 0; j < bucket_count; j++ )
            {
                int face = faces[j];
                dash3d_raster_model_face(
                    pixel_buffer,
                    face,
                    model->face_infos,
                    model->face_indices_a,
                    model->face_indices_b,
                    model->face_indices_c,
                    model->face_count,
                    dash->screen_vertices_x,
                    dash->screen_vertices_y,
                    dash->screen_vertices_z,
                    dash->orthographic_vertices_x,
                    dash->orthographic_vertices_y,
                    dash->orthographic_vertices_z,
                    model->vertex_count,
                    model->face_textures,
                    model->face_texture_coords,
                    model->textured_face_count,
                    model->textured_p_coordinate,
                    model->textured_m_coordinate,
                    model->textured_n_coordinate,
                    model->textured_face_count,
                    model->lighting->face_colors_hsl_a,
                    model->lighting->face_colors_hsl_b,
                    model->lighting->face_colors_hsl_c,
                    model->face_alphas,
                    view_port->width / 2,
                    view_port->height / 2,
                    camera->near_plane_z,
                    view_port->width,
                    view_port->height,
                    camera->fov_rpi2048,
                    dash->textures_hmap);
            }
        }
    }
    else
    {
        memset(dash->tmp_priority_depth_sum, 0, sizeof(dash->tmp_priority_depth_sum));
        memset(dash->tmp_priority_face_count, 0, sizeof(dash->tmp_priority_face_count));

        parition_faces_by_priority(
            dash->tmp_priority_faces,
            dash->tmp_priority_face_count,
            dash->tmp_depth_faces,
            dash->tmp_depth_face_count,
            model->face_count,
            model->face_priorities,
            model_min_depth,
            model_max_depth);

        int valid_faces = sort_face_draw_order(
            dash->tmp_priority_depth_sum,
            dash->tmp_flex_prio11_face_to_depth,
            dash->tmp_flex_prio12_face_to_depth,
            dash->tmp_face_order,
            dash->tmp_depth_faces,
            dash->tmp_depth_face_count,
            dash->tmp_priority_faces,
            dash->tmp_priority_face_count,
            model->face_count,
            model->face_priorities,
            model_min_depth,
            model_max_depth);

        for( int i = 0; i < valid_faces; i++ )
        {
            int face = dash->tmp_face_order[i];
            dash3d_raster_model_face(
                pixel_buffer,
                face,
                model->face_infos,
                model->face_indices_a,
                model->face_indices_b,
                model->face_indices_c,
                model->face_count,
                dash->screen_vertices_x,
                dash->screen_vertices_y,
                dash->screen_vertices_z,
                dash->orthographic_vertices_x,
                dash->orthographic_vertices_y,
                dash->orthographic_vertices_z,
                model->vertex_count,
                model->face_textures,
                model->face_texture_coords,
                model->textured_face_count,
                model->textured_p_coordinate,
                model->textured_m_coordinate,
                model->textured_n_coordinate,
                model->textured_face_count,
                model->lighting->face_colors_hsl_a,
                model->lighting->face_colors_hsl_b,
                model->lighting->face_colors_hsl_c,
                model->face_alphas,
                view_port->width / 2,
                view_port->height / 2,
                camera->near_plane_z,
                view_port->width,
                view_port->height,
                camera->fov_rpi2048,
                dash->textures_hmap);
        }
    }
}

struct DashBoundsCylinder
dash3d_calculate_bounds_cylinder(int num_vertices, int* vertex_x, int* vertex_y, int* vertex_z)
{
    struct DashBoundsCylinder bounds_cylinder = { 0 };

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
    int center_to_bottom_edge = (int)sqrt(radius_squared + min_y * min_y) + 1;
    int center_to_top_edge = (int)sqrt(radius_squared + max_y * max_y) + 1;
    bounds_cylinder.center_to_bottom_edge = center_to_bottom_edge;
    bounds_cylinder.center_to_top_edge = center_to_top_edge;
    bounds_cylinder.min_y = min_y;
    bounds_cylinder.max_y = max_y;

    bounds_cylinder.radius = (int)sqrt(radius_squared);

    // Use max of the two here because OSRS assumes the camera is always above the model,
    // which may not be the case for us.
    bounds_cylinder.min_z_depth_any_rotation =
        center_to_top_edge > center_to_bottom_edge ? center_to_top_edge : center_to_bottom_edge;

    return bounds_cylinder;
}

void //
dash3d_add_texture(
    struct DashGraphics* dash,
    int texture_id, //
    struct DashTexture* texture)
{
    struct DashTextureEntry* entry =
        (struct DashTextureEntry*)dashmap_search(dash->textures_hmap, &texture_id, DASHMAP_INSERT);

    printf("Adding Texture %d\n", texture_id);

    if( !entry )
    {
        // TODO: Resize;
        assert(false && "Handle resize textures hashmap");
    }

    entry->id = texture_id;
    entry->texture = texture;
}

struct DashModelNormals*
dashmodel_normals_new(int vertex_count, int face_count)
{
    struct DashModelNormals* normals =
        (struct DashModelNormals*)malloc(sizeof(struct DashModelNormals));
    memset(normals, 0, sizeof(struct DashModelNormals));
    normals->lighting_vertex_normals = malloc(sizeof(struct LightingNormal) * vertex_count);
    memset(normals->lighting_vertex_normals, 0, sizeof(struct LightingNormal) * vertex_count);
    normals->lighting_face_normals = malloc(sizeof(struct LightingNormal) * face_count);
    memset(normals->lighting_face_normals, 0, sizeof(struct LightingNormal) * face_count);
    normals->lighting_vertex_normals_count = vertex_count;
    normals->lighting_face_normals_count = face_count;
    return normals;
}

struct DashModelLighting*
dashmodel_lighting_new(int face_count)
{
    struct DashModelLighting* lighting =
        (struct DashModelLighting*)malloc(sizeof(struct DashModelLighting));
    memset(lighting, 0, sizeof(struct DashModelLighting));
    lighting->face_colors_hsl_a = malloc(sizeof(int) * face_count);
    memset(lighting->face_colors_hsl_a, 0, sizeof(int) * face_count);

    lighting->face_colors_hsl_b = malloc(sizeof(int) * face_count);
    memset(lighting->face_colors_hsl_b, 0, sizeof(int) * face_count);

    lighting->face_colors_hsl_c = malloc(sizeof(int) * face_count);
    memset(lighting->face_colors_hsl_c, 0, sizeof(int) * face_count);

    return lighting;
}
