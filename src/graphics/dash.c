#include "dash.h"

#include "dashmap.h"
#include "osrs/colors.h"
#include "osrs/minimap.h"
#include "osrs/palette.h"
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
#include "anim.u.c"
// clang-format on

struct DashTextureEntry
{
    int id;
    struct DashTexture* texture;
};

struct DashGraphics
{
    struct DashAABB aabb;
    struct DashAABB cylinder_fast_aabb;

    int screen_vertices_x[4096];
    int screen_vertices_y[4096];
    int screen_vertices_z[4096];
    int orthographic_vertices_x[4096];
    int orthographic_vertices_y[4096];
    int orthographic_vertices_z[4096];

    int16_t tmp_depth_face_count[1500];
    int16_t tmp_depth_faces[1500 * 512];
    int16_t tmp_priority_face_count[12];
    int16_t tmp_priority_depth_sum[12];
    int16_t tmp_priority_faces[12 * 2000];
    int tmp_flex_prio11_face_to_depth[1024];
    int tmp_flex_prio12_face_to_depth[512];
    // Used to be 1024, but now we need to support larger models.
    int tmp_face_order[4096];
    int tmp_face_order_count;

    struct DashMap* textures_hmap;
};

/**
 * Runescape only looks at the first byte of a UTF16 code point.
 * For example, str.charCodeAt(str.indexOf('£')) returns 163 or a3, which is not ascii anyway and
 * all the other characters are ascii, so there is no collision.
 * @param c
 * @return int
 */
static inline int
index_of_char(uint8_t c)
{
    for( int i = 0; i < DASH_FONT_CHAR_COUNT; i++ )
    {
        if( (DASH_FONT_CHARSET[i] & 0xFF) == c )
            return i;
    }
    return -1;
}

static uint8_t DASH_FONT_CHARCODESET[256] = { 0 };

static inline void
init_font_charset()
{
    // Initialize CHARCODESET for single-byte values (0-255)
    for( int i = 0; i < 256; i++ )
    {
        int c = index_of_char((uint8_t)i);
        if( c == -1 )
            c = index_of_char(' '); // space

        assert(c < DASH_FONT_CHAR_COUNT);
        DASH_FONT_CHARCODESET[i] = c;
    }
}

void
dash_init(void)
{
    init_font_charset();
    init_hsl16_to_rgb_table();
    init_sin_table();
    init_cos_table();
    init_tan_table();
    init_reciprocal16();
}

struct DashGraphics*
dash_new()
{
    struct DashGraphics* dash = (struct DashGraphics*)malloc(sizeof(struct DashGraphics));
    if( dash == NULL )
        return NULL;
    memset(dash, 0, sizeof(struct DashGraphics));

    printf("Sizeof(struct DashGraphics): %zu\n", sizeof(struct DashGraphics));

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
    if( !dash )
        return;
    if( dash->textures_hmap )
    {
        free(dashmap_buffer_ptr(dash->textures_hmap));
        dashmap_free(dash->textures_hmap);
    }
    free(dash);
}

static inline int
dash3d_aabb_cull(
    struct DashAABB* aabb,
    struct DashViewPort* view_port,
    struct DashCamera* camera)
{
    int screen_width = view_port->width;
    int screen_height = view_port->height;

    if( aabb->min_screen_x >= screen_width )
        return DASHCULL_CULLED_AABB;
    if( aabb->min_screen_y >= screen_height )
        return DASHCULL_CULLED_AABB;
    if( aabb->max_screen_x < 0 )
        return DASHCULL_CULLED_AABB;
    if( aabb->max_screen_y < 0 )
        return DASHCULL_CULLED_AABB;

    return DASHCULL_VISIBLE;
}

/** Far plane for bounding-cylinder frustum cull. */
#define DASH3D_CYLINDER_FAR_PLANE_Z 3500

static int
dash3d_fast_cull(
    struct DashAABB* aabb,
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
        return DASHCULL_CULLED_FAST;
    }

    if( mid_z > 3500 )
    {
        // Model too far away.
        return DASHCULL_CULLED_FAST;
    }

    int mid_x = projected_vertex->x;
    int mid_y = projected_vertex->y;

    if( mid_z < near_plane_z )
        mid_z = near_plane_z;

    int ortho_screen_x_min = mid_x - model_edge_radius;
    int ortho_screen_x_max = mid_x + model_edge_radius;

    int screen_x_min_unoffset = project_divide(ortho_screen_x_min, mid_z, camera->fov_rpi2048);
    int screen_x_max_unoffset = project_divide(ortho_screen_x_max, mid_z, camera->fov_rpi2048);
    int screen_edge_width = view_port->width >> 1;

    if( screen_x_min_unoffset > screen_edge_width || screen_x_max_unoffset < -screen_edge_width )
    {
        // All parts of the model left or right edges are projected off screen.
        return DASHCULL_CULLED_FAST;
    }

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
        return DASHCULL_CULLED_FAST;
    }

    aabb->min_screen_x = screen_x_min_unoffset + view_port->x_center;
    aabb->min_screen_y = screen_y_min_unoffset + view_port->y_center;
    aabb->max_screen_x = screen_x_max_unoffset + view_port->x_center;
    aabb->max_screen_y = screen_y_max_unoffset + view_port->y_center;
    aabb->kind = DASHAABB_KIND_CYLINDER_4POINT;

    return DASHCULL_VISIBLE;
}

// The Fast Approximation (The 8-Point Method)
// A very tight
// approximation is achieved by calculating the AABB of the 8 "extreme" points of the cylinder in
// world space:Transform the cylinder center to view space.Calculate the vectors in view space that
// represent the cylinder's "sides" relative to the camera.Project these 8 points:Top & Bottom
// center points: $(C_x, C_y \pm h, C_z)$The 4 "corners" of the cylinder aligned to the view
// plane:Find the vector from the camera to the cylinder center.The "horizontal" extent of the
// cylinder in view space is always perpendicular to the view vector and the cylinder's up-axis.
static void
dash3d_calculate_cylinder_aabb_8point(
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
    int screen_edge_width = view_port->x_center;
    int screen_edge_height = view_port->y_center;
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
    project_vertices_array_notex(
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

    aabb->kind = DASHAABB_KIND_CYLINDER_8POINT;
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
    hsl16_t* colors_a,
    hsl16_t* colors_b,
    hsl16_t* colors_c,
    int* face_alphas_nullable,
    int offset_x,
    int offset_y,
    int near_plane_z,
    int screen_width,
    int screen_height,
    int stride,
    int camera_fov,
    struct DashMap* textures_hmap,
    bool smooth)
{
    struct DashTextureEntry* texture_entry = NULL;
    struct DashTexture* texture = NULL;

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

    if( color_c == DASHHSL16_HIDDEN )
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
    if( color_c == DASHHSL16_HIDDEN )
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
        assert(texture != NULL);

        texels = texture->texels;
        texture_size = texture->width;
        texture_opaque = texture->opaque;

        if( color_c == DASHHSL16_FLAT )
            goto textured_flat;
        else
            goto textured;
    }
    else
    {
        // Alpha is a signed byte, but for non-textured
        // faces, we treat it as unsigned.
        // DASHHSL16_FLAT / DASHHSL16_HIDDEN are reserved. See lighting code.
        if( face_alphas_nullable )
            alpha = 0xFF - (alpha & 0xff);

        if( color_c == DASHHSL16_FLAT )
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

            if( smooth )
            {
                raster_face_gouraud_s1(
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
                    stride,
                    screen_width,
                    screen_height);
            }
            else
            {
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
                    stride,
                    screen_width,
                    screen_height);
            }

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
                stride,
                screen_width,
                screen_height);

            break;
        case FACE_TYPE_TEXTURED:
        textured:;
            assert(orthographic_vertex_x_nullable != NULL);
            assert(orthographic_vertex_y_nullable != NULL);
            assert(orthographic_vertex_z_nullable != NULL);

            if( face_texture_coords && face_texture_coords[face] != -1 )
            {
                assert(face_p_coordinate_nullable != NULL);
                assert(face_m_coordinate_nullable != NULL);
                assert(face_n_coordinate_nullable != NULL);

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
                stride,
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
                stride,
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

static inline void
dash3d_sort_face_draw_order(
    struct DashGraphics* dash,
    struct DashModel* model,
    struct DashViewPort* view_port,
    struct DashCamera* camera,
    int* pixel_buffer,
    bool smooth)
{
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
        int order_index = 0;
        for( int depth = model_max_depth; depth < 1500 && depth >= model_min_depth; depth-- )
        {
            int bucket_count = dash->tmp_depth_face_count[depth];
            if( bucket_count == 0 )
                continue;

            short* faces = &dash->tmp_depth_faces[depth << 9];
            for( int j = 0; j < bucket_count; j++ )
            {
                int face = faces[j];
                dash->tmp_face_order[order_index++] = face;
            }
        }

        dash->tmp_face_order_count = order_index;
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

        dash->tmp_face_order_count = valid_faces;
    }
}

static inline void
dash3d_raster(
    struct DashGraphics* dash,
    struct DashModel* model,
    struct DashViewPort* view_port,
    struct DashCamera* camera,
    int* pixel_buffer,
    bool smooth)
{
    dash3d_sort_face_draw_order(dash, model, view_port, camera, pixel_buffer, smooth);

    for( int i = 0; i < dash->tmp_face_order_count; i++ )
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
            model->has_textures ? dash->orthographic_vertices_x : NULL,
            model->has_textures ? dash->orthographic_vertices_y : NULL,
            model->has_textures ? dash->orthographic_vertices_z : NULL,
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
            view_port->width >> 1,
            view_port->height >> 1,
            camera->near_plane_z,
            view_port->width,
            view_port->height,
            view_port->stride,
            camera->fov_rpi2048,
            dash->textures_hmap,
            smooth);
    }
}

static inline void
dash3d_compute_projected_face_order(
    struct DashGraphics* dash,
    struct DashModel* model)
{
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
        int order_index = 0;
        for( int depth = model_max_depth; depth < 1500 && depth >= model_min_depth; depth-- )
        {
            int bucket_count = dash->tmp_depth_face_count[depth];
            if( bucket_count == 0 )
                continue;

            short* faces = &dash->tmp_depth_faces[depth << 9];
            for( int j = 0; j < bucket_count; j++ )
            {
                dash->tmp_face_order[order_index++] = faces[j];
            }
        }
        dash->tmp_face_order_count = order_index;
        return;
    }

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

    dash->tmp_face_order_count = sort_face_draw_order(
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
}

static inline int
dash3d_project(
    struct DashGraphics* dash,
    struct DashModel* model,
    struct DashPosition* position,
    struct DashViewPort* view_port,
    struct DashCamera* camera)
{
    struct ProjectedVertex center_projection;
    int cull = DASHCULL_VISIBLE;

    if( model == NULL || model->vertex_count == 0 || model->face_count == 0 )
        return DASHCULL_ERROR;

    cull = dash3d_fast_cull(
        &dash->cylinder_fast_aabb, view_port, model, position, camera, &center_projection);
    if( cull != DASHCULL_VISIBLE )
    {
        return cull;
    }

    dash3d_calculate_cylinder_aabb_8point(&dash->aabb, model, position, view_port, camera);

    cull = dash3d_aabb_cull(&dash->aabb, view_port, camera);
    if( cull != DASHCULL_VISIBLE )
    {
        return cull;
    }

    if( model->has_textures )
    {
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
    }
    else
    {
        project_vertices_array_notex(
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
    }

    return DASHCULL_VISIBLE;
}

struct DashAABB*
dash3d_projected_model_aabb(struct DashGraphics* dash)
{
    return &dash->aabb;
}

struct DashAABB*
dash3d_projected_model_cylinder_fast_aabb(struct DashGraphics* dash)
{
    return &dash->cylinder_fast_aabb;
}

int
dash3d_project_model(
    struct DashGraphics* dash,
    struct DashModel* model,
    struct DashPosition* position,
    struct DashViewPort* view_port,
    struct DashCamera* camera)
{
    int cull = dash3d_project(dash, model, position, view_port, camera);
    return cull;
}

int
dash3d_prepare_projected_face_order(
    struct DashGraphics* dash,
    struct DashModel* model,
    struct DashPosition* position,
    struct DashViewPort* view_port,
    struct DashCamera* camera)
{
    dash3d_compute_projected_face_order(dash, model);
    return dash->tmp_face_order_count;
}

const int*
dash3d_projected_face_order(
    struct DashGraphics* dash,
    int* out_face_count)
{
    if( out_face_count )
        *out_face_count = dash ? dash->tmp_face_order_count : 0;
    if( !dash || dash->tmp_face_order_count <= 0 )
        return NULL;
    return dash->tmp_face_order;
}

int
dash3d_cull_fast(
    struct DashGraphics* dash,
    struct DashModel* model,
    struct DashPosition* position,
    struct DashViewPort* view_port,
    struct DashCamera* camera)
{
    struct ProjectedVertex center_projection;
    if( model == NULL || model->vertex_count == 0 || model->face_count == 0 )
        return DASHCULL_ERROR;

    return dash3d_fast_cull(
        &dash->cylinder_fast_aabb, view_port, model, position, camera, &center_projection);
}

int
dash3d_cull_aabb(
    struct DashGraphics* dash,
    struct DashModel* model,
    struct DashPosition* position,
    struct DashViewPort* view_port,
    struct DashCamera* camera)
{
    if( model == NULL || model->vertex_count == 0 || model->face_count == 0 )
        return DASHCULL_ERROR;

    dash3d_calculate_cylinder_aabb_8point(&dash->aabb, model, position, view_port, camera);
    return dash3d_aabb_cull(&dash->aabb, view_port, camera);
}

int
dash3d_project_raw(
    struct DashGraphics* dash,
    struct DashModel* model,
    struct DashPosition* position,
    struct DashViewPort* view_port,
    struct DashCamera* camera)
{
    struct ProjectedVertex center_projection;
    if( model == NULL || model->vertex_count == 0 || model->face_count == 0 )
        return DASHCULL_ERROR;

    project_orthographic_fast(
        &center_projection,
        0,
        0,
        0,
        position->yaw,
        position->x,
        position->y,
        position->z,
        camera->pitch,
        camera->yaw);

    if( model->has_textures )
    {
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
    }
    else
    {
        project_vertices_array_notex(
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
    }

    return DASHCULL_VISIBLE;
}

int
dash3d_cull(
    struct DashGraphics* dash,
    struct DashModel* model,
    struct DashPosition* position,
    struct DashViewPort* view_port,
    struct DashCamera* camera)
{
    struct ProjectedVertex center_projection;
    int cull = DASHCULL_VISIBLE;

    if( model == NULL || model->vertex_count == 0 || model->face_count == 0 )
        return DASHCULL_ERROR;

    cull = dash3d_fast_cull(
        &dash->cylinder_fast_aabb, view_port, model, position, camera, &center_projection);
    if( cull != DASHCULL_VISIBLE )
        return cull;

    dash3d_calculate_cylinder_aabb_8point(&dash->aabb, model, position, view_port, camera);

    cull = dash3d_aabb_cull(&dash->aabb, view_port, camera);
    if( cull != DASHCULL_VISIBLE )
        return cull;

    return cull;
}

void
dash3d_copy_screen_vertices_float(
    struct DashGraphics* dash,
    float* out_x,
    float* out_y,
    int count)
{
    for( int i = 0; i < count; i++ )
    {
        out_x[i] = (float)dash->screen_vertices_x[i];
        out_y[i] = (float)dash->screen_vertices_y[i];
    }
}

int
dash3d_project_point(
    struct DashGraphics* dash,
    int scene_x,
    int scene_y,
    int scene_z,
    struct DashViewPort* view_port,
    struct DashCamera* camera,
    int* out_screen_x,
    int* out_screen_y)
{
    struct ProjectedVertex pv;
    project_orthographic_fast(
        &pv, 0, 0, 0, 0, scene_x, scene_y, scene_z, camera->pitch, camera->yaw);
    project_perspective_fast(&pv, pv.x, pv.y, pv.z, camera->fov_rpi2048, camera->near_plane_z);
    if( pv.clipped )
        return 0;
    *out_screen_x = pv.x + view_port->x_center;
    *out_screen_y = pv.y + view_port->y_center;
    (void)dash;
    return 1;
}

static inline int
dash3d_project6(
    struct DashGraphics* dash,
    struct DashModel* model,
    struct DashPosition* position,
    struct DashViewPort* view_port,
    struct DashCamera* camera)
{
    struct ProjectedVertex center_projection;
    int cull = DASHCULL_VISIBLE;

    if( model == NULL || model->vertex_count == 0 || model->face_count == 0 )
        return DASHCULL_ERROR;

    cull = dash3d_fast_cull(
        &dash->cylinder_fast_aabb, view_port, model, position, camera, &center_projection);
    if( cull != DASHCULL_VISIBLE )
        return cull;

    dash3d_calculate_cylinder_aabb_8point(&dash->aabb, model, position, view_port, camera);

    cull = dash3d_aabb_cull(&dash->aabb, view_port, camera);
    if( cull != DASHCULL_VISIBLE )
        return cull;

    if( model->has_textures )
    {
        project_vertices_array6(
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
            position->pitch,
            position->yaw,
            position->roll,
            center_projection.z,
            position->x,
            position->y,
            position->z,
            camera->near_plane_z,
            camera->fov_rpi2048,
            camera->pitch,
            camera->yaw,
            camera->roll);
    }
    else
    {
        project_vertices_array6_notex(
            dash->screen_vertices_x,
            dash->screen_vertices_y,
            dash->screen_vertices_z,
            model->vertices_x,
            model->vertices_y,
            model->vertices_z,
            model->vertex_count,
            position->pitch,
            position->yaw,
            position->roll,
            center_projection.z,
            position->x,
            position->y,
            position->z,
            camera->near_plane_z,
            camera->fov_rpi2048,
            camera->pitch,
            camera->yaw,
            camera->roll);
    }

    return DASHCULL_VISIBLE;
}

int
dash3d_project_model6(
    struct DashGraphics* dash,
    struct DashModel* model,
    struct DashPosition* position,
    struct DashViewPort* view_port,
    struct DashCamera* camera)
{
    int cull = dash3d_project6(dash, model, position, view_port, camera);
    return cull;
}

void
dash3d_raster_projected_model(
    struct DashGraphics* dash,
    struct DashModel* model,
    struct DashPosition* position,
    struct DashViewPort* view_port,
    struct DashCamera* camera,
    int* pixel_buffer,
    bool smooth)
{
    dash3d_raster(dash, model, view_port, camera, pixel_buffer, smooth);
}

static inline bool
dash3d_projected_model_contains_aabb(
    struct DashGraphics* dash,
    int screen_x,
    int screen_y)
{
    return screen_x >= dash->aabb.min_screen_x && screen_x <= dash->aabb.max_screen_x &&
           screen_y >= dash->aabb.min_screen_y && screen_y <= dash->aabb.max_screen_y;
}

static inline bool
triangle_contains_point(
    int x1,
    int y1,
    int x2,
    int y2,
    int x3,
    int y3,
    int x,
    int y)
{
    int denominator = (y2 - y3) * (x1 - x3) + (x3 - x2) * (y1 - y3);
    if( denominator != 0 )
    {
        float a = ((y2 - y3) * (x - x3) + (x3 - x2) * (y - y3)) / (float)denominator;
        float b = ((y3 - y1) * (x - x3) + (x1 - x3) * (y - y3)) / (float)denominator;
        float c = 1 - a - b;
        return (a >= 0 && b >= 0 && c >= 0);
    }
    return false;
}

static inline bool
projected_model_contains(
    struct DashGraphics* dash,
    struct DashModel* model,
    struct DashViewPort* view_port,
    int screen_x,
    int screen_y)
{
    bool contains = false;

    int adjusted_screen_x = screen_x - view_port->x_center;
    int adjusted_screen_y = screen_y - view_port->y_center;

    for( int i = 0; i < model->face_count; i++ )
    {
        int face_a = model->face_indices_a[i];
        int face_b = model->face_indices_b[i];
        int face_c = model->face_indices_c[i];

        int x1 = dash->screen_vertices_x[face_a];
        int y1 = dash->screen_vertices_y[face_a];
        int x2 = dash->screen_vertices_x[face_b];
        int y2 = dash->screen_vertices_y[face_b];
        int x3 = dash->screen_vertices_x[face_c];
        int y3 = dash->screen_vertices_y[face_c];

        bool contains_face =
            triangle_contains_point(x1, y1, x2, y2, x3, y3, adjusted_screen_x, adjusted_screen_y);
        if( contains_face )
        {
            contains = true;
            break;
        }
    }

    return contains;
}

bool
dash3d_projected_model_contains(
    struct DashGraphics* dash,
    struct DashModel* model,
    struct DashViewPort* view_port,
    int screen_x,
    int screen_y)
{
    assert(view_port->x_center != 0);
    assert(view_port->y_center != 0);
    if( !dash3d_projected_model_contains_aabb(dash, screen_x, screen_y) )
        return false;

    return projected_model_contains(dash, model, view_port, screen_x, screen_y);
}

int
dash_hsl16_to_rgb(int hsl16)
{
    assert(hsl16 >= 0 && hsl16 < 65536);
    return g_hsl16_to_rgb_table[hsl16];
}

int //
dash3d_render_model( //
    struct DashGraphics* dash, 
    struct DashModel* model,
    struct DashPosition* position,
    struct DashViewPort* view_port,
    struct DashCamera* camera,
    int* pixel_buffer
)
{
    int cull = dash3d_project_model(dash, model, position, view_port, camera);
    if( cull != DASHCULL_VISIBLE )
        return cull;

    dash3d_raster(dash, model, view_port, camera, pixel_buffer, false);
    return DASHCULL_VISIBLE;
}

void
dash3d_calculate_bounds_cylinder(
    struct DashBoundsCylinder* bounds_cylinder,
    int num_vertices,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z)
{
    memset(bounds_cylinder, 0, sizeof(struct DashBoundsCylinder));

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
    bounds_cylinder->center_to_bottom_edge = center_to_bottom_edge;
    bounds_cylinder->center_to_top_edge = center_to_top_edge;
    bounds_cylinder->min_y = min_y;
    bounds_cylinder->max_y = max_y;

    bounds_cylinder->radius = (int)sqrt(radius_squared);

    // Use max of the two here because OSRS assumes the camera is always above the model,
    // which may not be the case for us.
    bounds_cylinder->min_z_depth_any_rotation =
        center_to_top_edge > center_to_bottom_edge ? center_to_top_edge : center_to_bottom_edge;
}

static void
dash3d_calculate_vertex_normals(
    struct DashModelNormals* normals,
    int face_count,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int vertex_count,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z)
{
    calculate_vertex_normals(
        normals->lighting_vertex_normals,
        normals->lighting_face_normals,
        vertex_count,
        face_indices_a,
        face_indices_b,
        face_indices_c,
        vertex_x,
        vertex_y,
        vertex_z,
        face_count);

    normals->lighting_vertex_normals_count = vertex_count;
    normals->lighting_face_normals_count = face_count;
}

void //
dashmodel_calculate_vertex_normals(struct DashModel* model)
{
    dash3d_calculate_vertex_normals(
        model->normals,
        model->face_count,
        model->face_indices_a,
        model->face_indices_b,
        model->face_indices_c,
        model->vertex_count,
        model->vertices_x,
        model->vertices_y,
        model->vertices_z);

    memcpy(
        model->merged_normals->lighting_vertex_normals,
        model->normals->lighting_vertex_normals,
        sizeof(struct LightingNormal) * model->vertex_count);
    memcpy(
        model->merged_normals->lighting_face_normals,
        model->normals->lighting_face_normals,
        sizeof(struct LightingNormal) * model->face_count);
}

void //
dash3d_add_texture(
    struct DashGraphics* dash,
    int texture_id, //
    struct DashTexture* texture)
{
    struct DashTextureEntry* entry =
        (struct DashTextureEntry*)dashmap_search(dash->textures_hmap, &texture_id, DASHMAP_INSERT);

    if( !entry )
    {
        // TODO: Resize;
        assert(false && "Handle resize textures hashmap");
    }

    entry->id = texture_id;
    entry->texture = texture;
}

/* Texture animation - matches Java animate_texture (res/animate_texture.java) and Client.ts.
 * RuneScape uses: direction 1,3 = V (vertical), 2,4 = U (horizontal);
 * direction 1,2 = DOWN (negate offset), 3,4 = UP.
 * In-place: process by cycles (one temp int per cycle) to avoid overwrite. */
static int
gcd(int a,
    int b)
{
    a = a < 0 ? -a : a;
    b = b < 0 ? -b : b;
    while( b )
    {
        int t = b;
        b = a % b;
        a = t;
    }
    return a;
}

static void
animate_texture(
    struct DashTexture* texture,
    int time_delta)
{
    if( texture->animation_direction == TEXANIM_DIRECTION_NONE )
        return;

    int animation_speed = texture->animation_speed;
    int animation_direction = texture->animation_direction;

    int width = texture->width;
    int length = width * texture->height;

    int* pixels = texture->texels;

    /* V direction (1 or 3): shift by whole rows. Java: var4 = arg0 * width * animationSpeed */
    if( animation_direction == TEXANIM_DIRECTION_V_DOWN ||
        animation_direction == TEXANIM_DIRECTION_V_UP )
    {
        int v_offset = width * time_delta * animation_speed;
        if( animation_direction == TEXANIM_DIRECTION_V_DOWN )
            v_offset = -v_offset;

        int mask = length - 1;
        int g = gcd(v_offset, length);
        for( int start = 0; start < g; start++ )
        {
            int saved = pixels[start];
            int pos = start;
            int next = (pos + v_offset) & mask;
            while( next != start )
            {
                pixels[pos] = pixels[next];
                pos = next;
                next = (pos + v_offset) & mask;
            }
            pixels[pos] = saved;
        }
    }
    /* U direction (2 or 4): shift by columns within each row. Java: var11 = animationSpeed * arg0
     */
    else if(
        animation_direction == TEXANIM_DIRECTION_U_DOWN ||
        animation_direction == TEXANIM_DIRECTION_U_UP )
    {
        int u_offset = animation_speed * time_delta;
        if( animation_direction == TEXANIM_DIRECTION_U_DOWN )
            u_offset = -u_offset;

        int col_mask = width - 1;
        int g = gcd(u_offset, width);
        for( int row_start = 0; row_start < length; row_start += width )
        {
            for( int start = 0; start < g; start++ )
            {
                int saved = pixels[row_start + start];
                int pos = start;
                int next = (pos + u_offset) & col_mask;
                while( next != start )
                {
                    pixels[row_start + pos] = pixels[row_start + next];
                    pos = next;
                    next = (pos + u_offset) & col_mask;
                }
                pixels[row_start + pos] = saved;
            }
        }
    }
}

void
dash_animate_textures(
    struct DashGraphics* dash,
    int time_delta)
{
    struct DashTextureEntry* entry = NULL;

    struct DashMapIter* iter = dashmap_iter_new(dash->textures_hmap);
    while( (entry = (struct DashTextureEntry*)dashmap_iter_next(iter)) )
        animate_texture(entry->texture, time_delta);
    dashmap_iter_free(iter);
}

struct DashPosition*
dashposition_new(void)
{
    struct DashPosition* position = (struct DashPosition*)malloc(sizeof(struct DashPosition));
    memset(position, 0, sizeof(struct DashPosition));
    return position;
}

void
dashposition_free(struct DashPosition* position)
{
    free(position);
    position = NULL;
}

struct DashModel*
dashmodel_new(void)
{
    struct DashModel* model = (struct DashModel*)malloc(sizeof(struct DashModel));
    memset(model, 0, sizeof(struct DashModel));

    return model;
}

void
dashmodel_free(struct DashModel* model)
{
    if( !model )
        return;
    free(model->vertices_x);
    free(model->vertices_y);
    free(model->vertices_z);
    free(model->original_vertices_x);
    free(model->original_vertices_y);
    free(model->original_vertices_z);
    free(model->face_indices_a);
    free(model->face_indices_b);
    free(model->face_indices_c);
    free(model->face_alphas);
    free(model->original_face_alphas);
    free(model->face_infos);
    free(model->face_priorities);
    free(model->face_colors);
    free(model->textured_p_coordinate);
    free(model->textured_m_coordinate);
    free(model->textured_n_coordinate);
    free(model->face_textures);
    free(model->face_texture_coords);
    if( model->normals )
    {
        free(model->normals->lighting_vertex_normals);
        free(model->normals->lighting_face_normals);
        free(model->normals);
        model->normals = NULL;
    }
    if( model->merged_normals )
    {
        free(model->merged_normals->lighting_vertex_normals);
        free(model->merged_normals->lighting_face_normals);
        free(model->merged_normals);
        model->merged_normals = NULL;
    }

    if( model->lighting )
    {
        free(model->lighting->face_colors_hsl_a);
        free(model->lighting->face_colors_hsl_b);
        free(model->lighting->face_colors_hsl_c);
        free(model->lighting);
        model->lighting = NULL;
    }
    if( model->vertex_bones )
    {
        for( int i = 0; i < model->vertex_bones->bones_count; i++ )
            free(model->vertex_bones->bones[i]);
        free(model->vertex_bones->bones);
        free(model->vertex_bones->bones_sizes);
        free(model->vertex_bones);
    }
    if( model->face_bones )
    {
        for( int i = 0; i < model->face_bones->bones_count; i++ )
            free(model->face_bones->bones[i]);
        free(model->face_bones->bones);
        free(model->face_bones->bones_sizes);
        free(model->face_bones);
    }
    free(model->bounds_cylinder);
    free(model);
    model = NULL;
}

static size_t
dashmodel_normals_heap_bytes(const struct DashModelNormals* nm)
{
    if( !nm )
        return 0;
    size_t n = sizeof(struct DashModelNormals);
    if( nm->lighting_vertex_normals && nm->lighting_vertex_normals_count > 0 )
        n += (size_t)nm->lighting_vertex_normals_count * sizeof(struct LightingNormal);
    if( nm->lighting_face_normals && nm->lighting_face_normals_count > 0 )
        n += (size_t)nm->lighting_face_normals_count * sizeof(struct LightingNormal);
    return n;
}

static size_t
dashmodel_bones_heap_bytes(const struct DashModelBones* bones)
{
    if( !bones )
        return 0;
    size_t n = sizeof(struct DashModelBones);
    if( bones->bones_count > 0 && bones->bones )
        n += (size_t)bones->bones_count * sizeof(int*);
    if( bones->bones_count > 0 && bones->bones_sizes )
        n += (size_t)bones->bones_count * sizeof(int);
    if( bones->bones && bones->bones_sizes )
    {
        for( int i = 0; i < bones->bones_count; i++ )
        {
            if( bones->bones[i] && bones->bones_sizes[i] > 0 )
                n += (size_t)bones->bones_sizes[i] * sizeof(int);
        }
    }
    return n;
}

size_t
dashmodel_heap_bytes(const struct DashModel* model)
{
    if( !model )
        return 0;

    size_t total = sizeof(struct DashModel);
    int vc = model->vertex_count;
    int fc = model->face_count;
    int tfc = model->textured_face_count;

    if( vc > 0 )
    {
        if( model->vertices_x )
            total += (size_t)vc * sizeof(int);
        if( model->vertices_y )
            total += (size_t)vc * sizeof(int);
        if( model->vertices_z )
            total += (size_t)vc * sizeof(int);
        if( model->original_vertices_x )
            total += (size_t)vc * sizeof(int);
        if( model->original_vertices_y )
            total += (size_t)vc * sizeof(int);
        if( model->original_vertices_z )
            total += (size_t)vc * sizeof(int);
    }

    if( fc > 0 )
    {
        if( model->face_indices_a )
            total += (size_t)fc * sizeof(int);
        if( model->face_indices_b )
            total += (size_t)fc * sizeof(int);
        if( model->face_indices_c )
            total += (size_t)fc * sizeof(int);
        if( model->face_alphas )
            total += (size_t)fc * sizeof(int);
        if( model->original_face_alphas )
            total += (size_t)fc * sizeof(int);
        if( model->face_infos )
            total += (size_t)fc * sizeof(int);
        if( model->face_priorities )
            total += (size_t)fc * sizeof(int);
        if( model->face_colors )
            total += (size_t)fc * sizeof(hsl16_t);
        if( model->face_textures )
            total += (size_t)fc * sizeof(int);
        if( model->face_texture_coords )
            total += (size_t)fc * sizeof(int);
    }

    if( tfc > 0 )
    {
        if( model->textured_p_coordinate )
            total += (size_t)tfc * sizeof(int);
        if( model->textured_m_coordinate )
            total += (size_t)tfc * sizeof(int);
        if( model->textured_n_coordinate )
            total += (size_t)tfc * sizeof(int);
    }

    total += dashmodel_normals_heap_bytes(model->normals);
    total += dashmodel_normals_heap_bytes(model->merged_normals);

    if( model->lighting )
    {
        total += sizeof(struct DashModelLighting);
        if( model->lighting->face_colors_hsl_a && fc > 0 )
            total += (size_t)fc * sizeof(hsl16_t);
        if( model->lighting->face_colors_hsl_b && fc > 0 )
            total += (size_t)fc * sizeof(hsl16_t);
        if( model->lighting->face_colors_hsl_c && fc > 0 )
            total += (size_t)fc * sizeof(hsl16_t);
    }

    total += dashmodel_bones_heap_bytes(model->vertex_bones);
    total += dashmodel_bones_heap_bytes(model->face_bones);

    if( model->bounds_cylinder )
        total += sizeof(struct DashBoundsCylinder);

    return total;
}

void
dashmodel_alloc_normals(struct DashModel* model)
{
    if( model->normals )
        return;
    model->normals = dashmodel_normals_new(model->vertex_count, model->face_count);
    model->merged_normals = dashmodel_normals_new(model->vertex_count, model->face_count);
}

void
dashmodel_free_normals(struct DashModel* model)
{
    if( !model->normals )
        return;
    dashmodel_normals_free(model->normals);
    dashmodel_normals_free(model->merged_normals);
    model->normals = NULL;
    model->merged_normals = NULL;
    model->normals = NULL;
    model->merged_normals = NULL;
}

struct DashModelNormals*
dashmodel_normals_new(
    int vertex_count,
    int face_count)
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

void
dashmodel_normals_free(struct DashModelNormals* normals)
{
    if( !normals )
        return;
    free(normals->lighting_vertex_normals);
    free(normals->lighting_face_normals);
    free(normals);
    normals = NULL;
}

struct DashModelNormals* //
dashmodel_normals_new_copy(struct DashModelNormals* normals)
{
    struct DashModelNormals* aliased_normals = dashmodel_normals_new(
        normals->lighting_vertex_normals_count, normals->lighting_face_normals_count);
    memcpy(
        aliased_normals->lighting_vertex_normals,
        normals->lighting_vertex_normals,
        sizeof(struct LightingNormal) * normals->lighting_vertex_normals_count);
    memcpy(
        aliased_normals->lighting_face_normals,
        normals->lighting_face_normals,
        sizeof(struct LightingNormal) * normals->lighting_face_normals_count);
    return aliased_normals;
}

struct DashModelLighting*
dashmodel_lighting_new(int face_count)
{
    struct DashModelLighting* lighting =
        (struct DashModelLighting*)malloc(sizeof(struct DashModelLighting));
    memset(lighting, 0, sizeof(struct DashModelLighting));
    lighting->face_colors_hsl_a = malloc(sizeof(hsl16_t) * face_count);
    memset(lighting->face_colors_hsl_a, 0, sizeof(hsl16_t) * face_count);

    lighting->face_colors_hsl_b = malloc(sizeof(hsl16_t) * face_count);
    memset(lighting->face_colors_hsl_b, 0, sizeof(hsl16_t) * face_count);

    lighting->face_colors_hsl_c = malloc(sizeof(hsl16_t) * face_count);
    memset(lighting->face_colors_hsl_c, 0, sizeof(hsl16_t) * face_count);

    return lighting;
}

struct DashBoundsCylinder* //
dashmodel_bounds_cylinder_new(void)
{
    struct DashBoundsCylinder* bounds_cylinder =
        (struct DashBoundsCylinder*)malloc(sizeof(struct DashBoundsCylinder));
    memset(bounds_cylinder, 0, sizeof(struct DashBoundsCylinder));
    return bounds_cylinder;
}

bool //
dashmodel_valid(struct DashModel* model)
{
    if( model->lighting == NULL )
        return false;
    // if( model->normals == NULL )
    //     return false;
    if( model->bounds_cylinder == NULL )
        return false;

    return true;
}

static void
reset_original_values(struct DashModel* model)
{
    if( model->original_vertices_x == NULL )
    {
        model->original_vertices_x = malloc(sizeof(int) * model->vertex_count);
        model->original_vertices_y = malloc(sizeof(int) * model->vertex_count);
        model->original_vertices_z = malloc(sizeof(int) * model->vertex_count);
        memcpy(model->original_vertices_x, model->vertices_x, sizeof(int) * model->vertex_count);
        memcpy(model->original_vertices_y, model->vertices_y, sizeof(int) * model->vertex_count);
        memcpy(model->original_vertices_z, model->vertices_z, sizeof(int) * model->vertex_count);
    }

    if( model->face_alphas && model->original_face_alphas == NULL )
    {
        model->original_face_alphas = malloc(sizeof(int) * model->face_count);
        memcpy(model->original_face_alphas, model->face_alphas, sizeof(int) * model->face_count);
    }

    memcpy(model->vertices_x, model->original_vertices_x, sizeof(int) * model->vertex_count);
    memcpy(model->vertices_y, model->original_vertices_y, sizeof(int) * model->vertex_count);
    memcpy(model->vertices_z, model->original_vertices_z, sizeof(int) * model->vertex_count);
    if( model->face_alphas && model->original_face_alphas )
    {
        memcpy(model->face_alphas, model->original_face_alphas, sizeof(int) * model->face_count);
    }
}

void
dashmodel_animate(
    struct DashModel* model,
    struct DashFrame* frame,
    struct DashFramemap* framemap)
{
    reset_original_values(model);
    assert(model->original_vertices_x != NULL);
    if( frame == NULL )
        return;
    anim_frame_apply(
        frame,
        framemap,
        model->vertices_x,
        model->vertices_y,
        model->vertices_z,
        model->face_alphas,
        model->vertex_bones ? model->vertex_bones->bones_count : 0,
        model->vertex_bones ? model->vertex_bones->bones : NULL,
        model->vertex_bones ? model->vertex_bones->bones_sizes : NULL,
        model->face_bones ? model->face_bones->bones_count : 0,
        model->face_bones ? model->face_bones->bones : NULL,
        model->face_bones ? model->face_bones->bones_sizes : NULL);
}

void
dashmodel_animate_mask(
    struct DashModel* model,
    struct DashFrame* primary_frame,
    struct DashFrame* secondary_frame,
    struct DashFramemap* framemap,
    int* walkmerge)
{
    reset_original_values(model);
    assert(model->original_vertices_x != NULL);
    if( primary_frame == NULL || secondary_frame == NULL || walkmerge == NULL )
    {
        if( primary_frame )
            dashmodel_animate(model, primary_frame, framemap);
        return;
    }
    anim_frame_apply_mask(
        primary_frame,
        secondary_frame,
        framemap,
        walkmerge,
        model->vertices_x,
        model->vertices_y,
        model->vertices_z,
        model->face_alphas,
        model->vertex_bones ? model->vertex_bones->bones_count : 0,
        model->vertex_bones ? model->vertex_bones->bones : NULL,
        model->vertex_bones ? model->vertex_bones->bones_sizes : NULL,
        model->face_bones ? model->face_bones->bones_count : 0,
        model->face_bones ? model->face_bones->bones : NULL,
        model->face_bones ? model->face_bones->bones_sizes : NULL);
}

static int*
dashpix8_to_argb(
    struct DashPix8* pix8,
    struct DashPixPalette* palette)
{
    int* pixels_argb = NULL;
    pixels_argb = malloc(pix8->width * pix8->height * sizeof(int));
    memset(pixels_argb, 0, pix8->width * pix8->height * sizeof(int));

    for( int i = 0; i < pix8->width * pix8->height; i++ )
    {
        int palette_index = pix8->pixels[i];
        assert(palette_index >= 0 && palette_index < palette->palette_count);
        pixels_argb[i] = palette->palette[palette_index];
    }

    return pixels_argb;
}

struct DashSprite*
dashsprite_new_from_pix8(
    struct DashPix8* pix8,
    struct DashPixPalette* palette)
{
    struct DashSprite* sprite = (struct DashSprite*)malloc(sizeof(struct DashSprite));
    memset(sprite, 0, sizeof(struct DashSprite));
    sprite->pixels_argb = dashpix8_to_argb(pix8, palette);
    sprite->width = pix8->width;
    sprite->height = pix8->height;
    return sprite;
}

struct DashSprite*
dashsprite_new_from_pix32(struct DashPix32* pix32)
{
    struct DashSprite* sprite = (struct DashSprite*)malloc(sizeof(struct DashSprite));
    memset(sprite, 0, sizeof(struct DashSprite));

    int y = 0;
    int width = pix32->stride_x;
    int height = pix32->stride_y;

    int* pixels = malloc(pix32->draw_width * pix32->draw_height * sizeof(int));
    memset(pixels, 0, pix32->draw_width * pix32->draw_height * sizeof(int));

    if( pix32->stride_y > pix32->draw_height )
    {
        height = pix32->draw_height;
    }

    if( pix32->stride_x > pix32->draw_width )
    {
        width = pix32->draw_width;
    }

    int write_x = 0;
    int write_y = pix32->crop_y;
    for( ; y < height; y++ )
    {
        write_x = pix32->crop_x;
        for( int x = 0; x < width; x++ )
        {
            int pixel_index = x + y * pix32->stride_x;
            int write_index = write_x + write_y * pix32->draw_width;
            if( write_index < 0 || write_index >= pix32->draw_width * pix32->draw_height )
                continue;
            assert(write_index < pix32->draw_width * pix32->draw_height);
            pixels[write_index] = pix32->pixels[pixel_index];
            write_x++;
        }
        write_y++;
    }

    sprite->pixels_argb = pixels;
    sprite->width = pix32->draw_width;
    sprite->height = pix32->draw_height;
    return sprite;
}

struct DashSprite*
dashsprite_new_from_argb_owned(
    uint32_t* pixels_argb,
    int width,
    int height)
{
    if( !pixels_argb || width <= 0 || height <= 0 )
        return NULL;
    struct DashSprite* sprite = (struct DashSprite*)malloc(sizeof(struct DashSprite));
    if( !sprite )
        return NULL;
    memset(sprite, 0, sizeof(struct DashSprite));
    sprite->pixels_argb = pixels_argb;
    sprite->width = width;
    sprite->height = height;
    return sprite;
}

void
dashpix8_free(struct DashPix8* pix8)
{
    free(pix8->pixels);
    free(pix8);
}

void
dashpixpalette_free(struct DashPixPalette* palette)
{
    free(palette->palette);
    free(palette);
}

void
dash2d_blit_sprite(
    struct DashGraphics* dash,
    struct DashSprite* sprite,
    struct DashViewPort* view_port,
    int x_offset,
    int y_offset,
    int* pixel_buffer)
{
    if( !sprite )
        return;
    dash2d_blit_sprite_subrect(
        dash,
        sprite,
        view_port,
        x_offset,
        y_offset,
        0,
        0,
        sprite->width,
        sprite->height,
        pixel_buffer);
}

void
dash2d_blit_sprite_subrect(
    struct DashGraphics* dash,
    struct DashSprite* sprite,
    struct DashViewPort* view_port,
    int x_offset,
    int y_offset,
    int src_x,
    int src_y,
    int src_w,
    int src_h,
    int* pixel_buffer)
{
    (void)dash;
    if( !sprite || !sprite->pixels_argb || src_w <= 0 || src_h <= 0 )
        return;
    if( src_x < 0 || src_y < 0 || src_x + src_w > sprite->width || src_y + src_h > sprite->height )
        return;
    /* Client.ts Pix8.draw(x,y): destination is (x + cropX, y + cropY) */
    x_offset += sprite->crop_x;
    y_offset += sprite->crop_y;

    int cl = view_port->clip_left;
    int ct = view_port->clip_top;
    int cr = view_port->clip_right;
    int cb = view_port->clip_bottom;
    int stride = view_port->stride;
    int sw = sprite->width;

    for( int y = 0; y < src_h; y++ )
    {
        int dst_y = y + y_offset;
        if( dst_y < ct || dst_y >= cb )
            continue;
        for( int x = 0; x < src_w; x++ )
        {
            int dst_x = x + x_offset;
            if( dst_x < cl || dst_x >= cr )
                continue;

            int pixel_buffer_index = dst_y * stride + dst_x;
            int sx = src_x + x;
            int sy = src_y + y;
            int pixel = sprite->pixels_argb[sx + sy * sw];
            if( pixel == 0 )
                continue;

            pixel_buffer[pixel_buffer_index] = pixel;
        }
    }
}

void
dashsprite_flip_horizontal(struct DashSprite* sprite)
{
    if( !sprite || !sprite->pixels_argb || sprite->width <= 0 || sprite->height <= 0 )
        return;
    int w = sprite->width;
    int h = sprite->height;
    uint32_t* p = sprite->pixels_argb;
    for( int y = 0; y < h; y++ )
        for( int x = 0; x < (w / 2); x++ )
        {
            int a = x + y * w;
            int b = (w - 1 - x) + y * w;
            uint32_t t = p[a];
            p[a] = p[b];
            p[b] = t;
        }
}

void
dashsprite_flip_vertical(struct DashSprite* sprite)
{
    if( !sprite || !sprite->pixels_argb || sprite->width <= 0 || sprite->height <= 0 )
        return;
    int w = sprite->width;
    int h = sprite->height;
    uint32_t* p = sprite->pixels_argb;
    for( int y = 0; y < (h / 2); y++ )
        for( int x = 0; x < w; x++ )
        {
            int a = x + y * w;
            int b = x + (h - 1 - y) * w;
            uint32_t t = p[a];
            p[a] = p[b];
            p[b] = t;
        }
}

void
dashsprite_free(struct DashSprite* sprite)
{
    if( !sprite )
        return;
    free(sprite->pixels_argb);
    free(sprite);
}

static void
dashfont_draw_mask(
    int w,
    int h,
    int* src,
    int src_offset,
    int src_step,
    int* dst,
    int dst_offset,
    int dst_step,
    int rgb)
{
    int hw = -(w >> 2);
    w = -(w & 0x3);
    for( int y = -h; y < 0; y++ )
    {
        for( int x = hw; x < 0; x++ )
        {
            if( src[src_offset++] == 0 )
            {
                dst_offset++;
            }
            else
            {
                dst[dst_offset++] = rgb;
            }

            if( src[src_offset++] == 0 )
            {
                dst_offset++;
            }
            else
            {
                dst[dst_offset++] = rgb;
            }

            if( src[src_offset++] == 0 )
            {
                dst_offset++;
            }
            else
            {
                dst[dst_offset++] = rgb;
            }

            if( src[src_offset++] == 0 )
            {
                dst_offset++;
            }
            else
            {
                dst[dst_offset++] = rgb;
            }
        }

        for( int x = w; x < 0; x++ )
        {
            if( src[src_offset++] == 0 )
            {
                dst_offset++;
            }
            else
            {
                dst[dst_offset++] = rgb;
            }
        }

        dst_offset += dst_step;
        src_offset += src_step;
    }
}

/* Like dashfont_draw_mask but only writes pixels inside [clip_left, clip_right) x [clip_top,
 * clip_bottom). base_x, base_y = top-left of character in buffer coords; stride used to compute
 * row. */
static void
dashfont_draw_mask_clipped(
    int w,
    int h,
    int* src,
    int src_offset,
    int src_step,
    int* dst,
    int dst_offset,
    int dst_step,
    int stride,
    int base_x,
    int base_y,
    int clip_left,
    int clip_top,
    int clip_right,
    int clip_bottom,
    int rgb)
{
    int hw = -(w >> 2);
    int w_rem = -(w & 0x3);
    for( int y = -h; y < 0; y++ )
    {
        int row = h + y;
        int buf_y = base_y + row;
        if( buf_y < clip_top || buf_y >= clip_bottom )
        {
            src_offset += (w >> 2) * 4 + (w & 3);
            src_offset += src_step;
            dst_offset += w + dst_step;
            continue;
        }
        int col = 0;
        for( int x = hw; x < 0; x++ )
        {
            for( int i = 0; i < 4 && col < w; i++, col++ )
            {
                int buf_x = base_x + col;
                if( buf_x >= clip_left && buf_x < clip_right && src[src_offset] != 0 )
                    dst[dst_offset] = rgb;
                src_offset++;
                dst_offset++;
            }
        }
        for( int x = w_rem; x < 0; x++, col++ )
        {
            int buf_x = base_x + col;
            if( buf_x >= clip_left && buf_x < clip_right && src[src_offset] != 0 )
                dst[dst_offset] = rgb;
            src_offset++;
            dst_offset++;
        }
        dst_offset += dst_step;
        src_offset += src_step;
    }
}

void
dashfont_draw_text(
    struct DashPixFont* pixfont,
    // 163 is '£'
    uint8_t* text,
    int x,
    int y,
    int color_rgb,
    int* pixels,
    int stride)
{
    // Calculate length of UTF16 string (null-terminated)
    int length = strlen(text);

    for( int i = 0; i < length; i++ )
    {
        uint8_t code_point = text[i];
        int c = 0;
        c = DASH_FONT_CHARCODESET[code_point];

        if( c < DASH_FONT_CHAR_COUNT )
        {
            int w = pixfont->char_mask_width[c];
            int h = pixfont->char_mask_height[c];
            int* mask = pixfont->char_mask[c];
            /* Row-major: pixel (px, py) is at py*stride + px; include y so text draws at correct
             * row */
            int dst_offset =
                y * stride + x + pixfont->char_offset_x[c] + pixfont->char_offset_y[c] * stride;
            dashfont_draw_mask(w, h, mask, 0, 0, pixels, dst_offset, stride - w, color_rgb);
        }
        int adv = pixfont->char_advance[c];
        if( adv <= 0 )
            adv = 4; /* space (and any empty glyph) must still advance */
        x += adv;
    }
}

void
dashfont_draw_text_clipped(
    struct DashPixFont* pixfont,
    uint8_t* text,
    int x,
    int y,
    int color_rgb,
    int* pixels,
    int stride,
    int clip_left,
    int clip_top,
    int clip_right,
    int clip_bottom)
{
    if( clip_left >= clip_right || clip_top >= clip_bottom )
        return;
    int length = (int)strlen((char*)text);
    for( int i = 0; i < length; i++ )
    {
        uint8_t code_point = text[i];
        int c = DASH_FONT_CHARCODESET[code_point];
        if( c < DASH_FONT_CHAR_COUNT )
        {
            int w = pixfont->char_mask_width[c];
            int h = pixfont->char_mask_height[c];
            int* mask = pixfont->char_mask[c];
            int base_x = x + pixfont->char_offset_x[c];
            int base_y = y + pixfont->char_offset_y[c];
            int dst_offset = base_y * stride + base_x;
            dashfont_draw_mask_clipped(
                w,
                h,
                mask,
                0,
                0,
                pixels,
                dst_offset,
                stride - w,
                stride,
                base_x,
                base_y,
                clip_left,
                clip_top,
                clip_right,
                clip_bottom,
                color_rgb);
        }
        int adv = pixfont->char_advance[c];
        if( adv <= 0 )
            adv = 4; /* space (and any empty glyph) must still advance */
        x += adv;
    }
}

int
dashfont_text_width_taggable(
    struct DashPixFont* pixfont,
    uint8_t* text)
{
    int width = 0;
    size_t length = strlen((char*)text);
    for( size_t i = 0; i < length; i++ )
    {
        if( text[i] == '@' && i + 5 <= length && text[i + 4] == '@' )
        {
            i += 4;
            continue;
        }
        uint8_t code_point = text[i];
        int c = DASH_FONT_CHARCODESET[code_point];
        if( c < DASH_FONT_CHAR_COUNT )
        {
            int adv = pixfont->char_advance[c];
            if( adv <= 0 )
                adv = 4;
            width += adv;
        }
    }
    return width;
}

int
dashfont_text_width(
    struct DashPixFont* pixfont,
    uint8_t* text)
{
    int width = 0;
    size_t length = strlen((char*)text);
    for( size_t i = 0; i < length; i++ )
    {
        uint8_t code_point = text[i];
        int c = DASH_FONT_CHARCODESET[code_point];
        if( c < DASH_FONT_CHAR_COUNT )
        {
            int adv = pixfont->char_advance[c];
            if( adv <= 0 )
                adv = 4; /* space (and any empty glyph) must still advance */
            width += adv;
        }
    }
    return width;
}

int
dashfont_charcode_to_glyph(uint8_t code_point)
{
    return DASH_FONT_CHARCODESET[code_point];
}

struct DashFontAtlas*
dashfont_build_atlas(struct DashPixFont* pixfont)
{
    if( !pixfont )
        return NULL;

    int total_w = 0;
    int max_h = 0;
    for( int i = 0; i < DASH_FONT_CHAR_COUNT; i++ )
    {
        int w = pixfont->char_mask_width[i];
        int h = pixfont->char_mask_height[i];
        if( w > 0 )
            total_w += w + 1;
        if( h > max_h )
            max_h = h;
    }
    if( total_w <= 0 || max_h <= 0 )
        return NULL;

    struct DashFontAtlas* atlas = (struct DashFontAtlas*)malloc(sizeof(struct DashFontAtlas));
    memset(atlas, 0, sizeof(struct DashFontAtlas));
    atlas->atlas_width = total_w;
    atlas->atlas_height = max_h;
    atlas->rgba_pixels = (uint8_t*)calloc((size_t)total_w * (size_t)max_h * 4u, 1);

    int cursor_x = 0;
    for( int i = 0; i < DASH_FONT_CHAR_COUNT; i++ )
    {
        int gw = pixfont->char_mask_width[i];
        int gh = pixfont->char_mask_height[i];
        atlas->glyph_x[i] = cursor_x;
        atlas->glyph_y[i] = 0;
        atlas->glyph_w[i] = gw;
        atlas->glyph_h[i] = gh;

        int* mask = pixfont->char_mask[i];
        if( mask && gw > 0 && gh > 0 )
        {
            for( int row = 0; row < gh; row++ )
            {
                for( int col = 0; col < gw; col++ )
                {
                    int src_idx = row * gw + col;
                    size_t dst_idx =
                        ((size_t)row * (size_t)total_w + (size_t)(cursor_x + col)) * 4u;
                    if( mask[src_idx] != 0 )
                    {
                        atlas->rgba_pixels[dst_idx + 0] = 0xFF;
                        atlas->rgba_pixels[dst_idx + 1] = 0xFF;
                        atlas->rgba_pixels[dst_idx + 2] = 0xFF;
                        atlas->rgba_pixels[dst_idx + 3] = 0xFF;
                    }
                }
            }
        }

        if( gw > 0 )
            cursor_x += gw + 1;
    }

    return atlas;
}

void
dashfont_free_atlas(struct DashFontAtlas* atlas)
{
    if( !atlas )
        return;
    free(atlas->rgba_pixels);
    free(atlas);
}

void
dashpixfont_free(struct DashPixFont* font)
{
    if( !font )
        return;
    for( int i = 0; i < DASH_FONT_CHAR_COUNT; i++ )
        free(font->char_mask[i]);
    dashfont_free_atlas(font->atlas);
    free(font->charcode_set);
    free(font);
}

/* Evaluate @XXX@ color tag. Returns new color or -1 if not a color tag. */
static int
dashfont_evaluate_tag(const char* tag)
{
    if( tag[0] == 'c' && tag[1] == 'y' && tag[2] == 'a' )
        return CYAN;
    if( tag[0] == 'w' && tag[1] == 'h' && tag[2] == 'i' )
        return WHITE;
    if( tag[0] == 'y' && tag[1] == 'e' && tag[2] == 'l' )
        return YELLOW;
    if( tag[0] == 'r' && tag[1] == 'e' && tag[2] == 'd' )
        return RED;
    if( tag[0] == 'g' && tag[1] == 'r' && tag[2] == 'e' )
        return GREEN;
    if( tag[0] == 'm' && tag[1] == 'a' && tag[2] == 'g' )
        return MAGENTA;
    if( tag[0] == 'b' && tag[1] == 'l' && tag[2] == 'a' )
        return BLACK;
    if( tag[0] == 'o' && tag[1] == 'r' && tag[2] == '1' )
        return ORANGE1;
    if( tag[0] == 'o' && tag[1] == 'r' && tag[2] == '2' )
        return ORANGE2;
    if( tag[0] == 'o' && tag[1] == 'r' && tag[2] == '3' )
        return ORANGE3;
    if( tag[0] == 'g' && tag[1] == 'r' && tag[2] == '1' )
        return GREEN1;
    if( tag[0] == 'g' && tag[1] == 'r' && tag[2] == '2' )
        return GREEN2;
    if( tag[0] == 'g' && tag[1] == 'r' && tag[2] == '3' )
        return GREEN3;
    return -1;
}

int
dashfont_evaluate_color_tag(const char* tag)
{
    return dashfont_evaluate_tag(tag);
}

void
dashfont_draw_text_ex(
    struct DashPixFont* pixfont,
    uint8_t* text,
    int x,
    int y,
    int default_color_rgb,
    int* pixels,
    int stride)
{
    int length = (int)strlen((char*)text);
    int color = default_color_rgb;

    for( int i = 0; i < length; i++ )
    {
        if( text[i] == '@' && i + 5 <= length && text[i + 4] == '@' )
        {
            int new_color = dashfont_evaluate_tag((char*)&text[i + 1]);
            if( new_color >= 0 )
                color = new_color;

            if( i + 6 <= length && text[i + 5] == ' ' )
                i += 5;
            else
                i += 4;
            continue;
        }
        uint8_t code_point = text[i];
        int c = DASH_FONT_CHARCODESET[code_point];
        if( c < DASH_FONT_CHAR_COUNT )
        {
            int w = pixfont->char_mask_width[c];
            int h = pixfont->char_mask_height[c];
            int* mask = pixfont->char_mask[c];
            int dst_offset =
                y * stride + x + pixfont->char_offset_x[c] + pixfont->char_offset_y[c] * stride;
            dashfont_draw_mask(w, h, mask, 0, 0, pixels, dst_offset, stride - w, color);
        }
        int adv = pixfont->char_advance[c];
        if( adv <= 0 )
            adv = 4;
        x += adv;
    }
}

void
dashfont_draw_text_clipped_taggable(
    struct DashPixFont* pixfont,
    uint8_t* text,
    int x,
    int y,
    int default_color_rgb,
    int* pixels,
    int stride,
    int clip_left,
    int clip_top,
    int clip_right,
    int clip_bottom,
    bool shadowed)
{
    if( clip_left >= clip_right || clip_top >= clip_bottom )
        return;
    int length = (int)strlen((char*)text);
    int color = default_color_rgb;
    int shadow_color = 0xFF000000 | BLACK;
    for( int i = 0; i < length; i++ )
    {
        if( text[i] == '@' && i + 5 <= length && text[i + 4] == '@' )
        {
            int new_color = dashfont_evaluate_tag((char*)&text[i + 1]);
            if( new_color >= 0 )
                color = 0xFF000000 | new_color;
            i += 4;
            continue;
        }
        uint8_t code_point = text[i];
        int c = DASH_FONT_CHARCODESET[code_point];
        if( c < DASH_FONT_CHAR_COUNT )
        {
            int w = pixfont->char_mask_width[c];
            int h = pixfont->char_mask_height[c];
            int* mask = pixfont->char_mask[c];
            int base_x = x + pixfont->char_offset_x[c];
            int base_y = y + pixfont->char_offset_y[c];
            if( shadowed )
            {
                int shadow_x = base_x + 1;
                int shadow_y = base_y + 1;
                int shadow_offset = shadow_y * stride + shadow_x;
                dashfont_draw_mask_clipped(
                    w,
                    h,
                    mask,
                    0,
                    0,
                    pixels,
                    shadow_offset,
                    stride - w,
                    stride,
                    shadow_x,
                    shadow_y,
                    clip_left,
                    clip_top,
                    clip_right,
                    clip_bottom,
                    shadow_color);
            }
            int dst_offset = base_y * stride + base_x;
            dashfont_draw_mask_clipped(
                w,
                h,
                mask,
                0,
                0,
                pixels,
                dst_offset,
                stride - w,
                stride,
                base_x,
                base_y,
                clip_left,
                clip_top,
                clip_right,
                clip_bottom,
                color);
        }
        int adv = (c >= 0 && c < DASH_FONT_CHAR_COUNT) ? pixfont->char_advance[c] : 4;
        if( adv <= 0 )
            adv = 4;
        x += adv;
    }
}

int
dash_texture_average_hsl(struct DashTexture* texture)
{
    if( texture->average_hsl != 0 )
        return texture->average_hsl;

    // let red = 0;
    // let green = 0;
    // let blue = 0;

    // const colourCount = sprite.palette.length;
    // for (let i = 0; i < colourCount; i++) {
    //     red += (sprite.palette[i] >> 16) & 0xff;
    //     green += (sprite.palette[i] >> 8) & 0xff;
    //     blue += sprite.palette[i] & 0xff;
    // }

    // const averageRgb =
    //     ((red / colourCount) << 16) + ((green / colourCount) << 8) + ((blue / colourCount) | 0);

    // averageHsl = rgbToHsl(averageRgb);

    int red = 0;
    int green = 0;
    int blue = 0;
    int colourCount = texture->width * texture->height;
    for( int i = 0; i < colourCount; i++ )
    {
        red += (texture->texels[i] >> 16) & 0xff;
        green += (texture->texels[i] >> 8) & 0xff;
        blue += texture->texels[i] & 0xff;
    }

    int averageRgb =
        ((red / colourCount) << 16) + ((green / colourCount) << 8) + ((blue / colourCount) | 0);

    int average_hsl = palette_rgb_to_hsl16(averageRgb);
    texture->average_hsl = average_hsl;

    return average_hsl;
}

void
dash2d_fill_rect(
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    int color_rgb)
{
    for( int i = 0; i < height; i++ )
    {
        for( int j = 0; j < width; j++ )
        {
            int pixel_buffer_index = (y + i) * stride + (x + j);
            pixel_buffer[pixel_buffer_index] = color_rgb;
        }
    }
}

void
dash2d_fill_rect_clipped(
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    int color_rgb,
    int clip_left,
    int clip_top,
    int clip_right,
    int clip_bottom)
{
    int rx = x < clip_left ? clip_left : x;
    int ry = y < clip_top ? clip_top : y;
    int rx2 = x + width > clip_right ? clip_right : x + width;
    int ry2 = y + height > clip_bottom ? clip_bottom : y + height;
    int rw = rx2 - rx;
    int rh = ry2 - ry;
    if( rw <= 0 || rh <= 0 )
        return;
    for( int i = 0; i < rh; i++ )
    {
        for( int j = 0; j < rw; j++ )
        {
            int pixel_buffer_index = (ry + i) * stride + (rx + j);
            pixel_buffer[pixel_buffer_index] = color_rgb;
        }
    }
}

void
dash2d_draw_rect(
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    int color_rgb)
{
    // Top and bottom edges
    for( int j = 0; j < width; j++ )
    {
        pixel_buffer[y * stride + (x + j)] = color_rgb;
        pixel_buffer[(y + height - 1) * stride + (x + j)] = color_rgb;
    }
    // Left and right edges
    for( int i = 1; i < height - 1; i++ )
    {
        pixel_buffer[(y + i) * stride + x] = color_rgb;
        pixel_buffer[(y + i) * stride + (x + width - 1)] = color_rgb;
    }
}

void
dash2d_fill_rect_alpha(
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    int color_rgb,
    int alpha)
{
    int r = (color_rgb >> 16) & 0xFF;
    int g = (color_rgb >> 8) & 0xFF;
    int b = color_rgb & 0xFF;

    for( int i = 0; i < height; i++ )
    {
        for( int j = 0; j < width; j++ )
        {
            int pixel_buffer_index = (y + i) * stride + (x + j);
            int dst_pixel = pixel_buffer[pixel_buffer_index];
            int dst_r = (dst_pixel >> 16) & 0xFF;
            int dst_g = (dst_pixel >> 8) & 0xFF;
            int dst_b = dst_pixel & 0xFF;

            // Alpha blend
            int out_r = (r * alpha + dst_r * (256 - alpha)) >> 8;
            int out_g = (g * alpha + dst_g * (256 - alpha)) >> 8;
            int out_b = (b * alpha + dst_b * (256 - alpha)) >> 8;

            pixel_buffer[pixel_buffer_index] = (out_r << 16) | (out_g << 8) | out_b;
        }
    }
}

static void
dash2d_fill_triangle_alpha(
    int* pixel_buffer,
    int stride,
    int x1,
    int y1,
    int x2,
    int y2,
    int x3,
    int y3,
    int color_rgb,
    int alpha,
    int clip_left,
    int clip_top,
    int clip_right,
    int clip_bottom)
{
    int min_x = x1;
    if( x2 < min_x )
        min_x = x2;
    if( x3 < min_x )
        min_x = x3;
    int max_x = x1;
    if( x2 > max_x )
        max_x = x2;
    if( x3 > max_x )
        max_x = x3;
    int min_y = y1;
    if( y2 < min_y )
        min_y = y2;
    if( y3 < min_y )
        min_y = y3;
    int max_y = y1;
    if( y2 > max_y )
        max_y = y2;
    if( y3 > max_y )
        max_y = y3;
    if( min_x > clip_right || max_x < clip_left || min_y > clip_bottom || max_y < clip_top )
        return;
    if( min_x < clip_left )
        min_x = clip_left;
    if( max_x > clip_right )
        max_x = clip_right;
    if( min_y < clip_top )
        min_y = clip_top;
    if( max_y > clip_bottom )
        max_y = clip_bottom;
    /* Ensure we never write past the buffer: clip_right/clip_bottom are inclusive,
     * so valid indices are [0, stride-1] and y in [0, clip_bottom]. */
    if( max_x >= stride )
        max_x = stride - 1;
    if( min_x < 0 )
        min_x = 0;
    if( min_y < 0 )
        min_y = 0;
    if( max_y >= clip_bottom )
        max_y = clip_bottom - 1;
    if( min_x > max_x || min_y > max_y )
        return;

    int r = (color_rgb >> 16) & 0xFF;
    int g = (color_rgb >> 8) & 0xFF;
    int b = color_rgb & 0xFF;

    for( int py = min_y; py <= max_y; py++ )
    {
        for( int px = min_x; px <= max_x; px++ )
        {
            if( !triangle_contains_point(x1, y1, x2, y2, x3, y3, px, py) )
                continue;
            int idx = py * stride + px;
            int dst_pixel = pixel_buffer[idx];
            int dst_r = (dst_pixel >> 16) & 0xFF;
            int dst_g = (dst_pixel >> 8) & 0xFF;
            int dst_b = dst_pixel & 0xFF;
            int out_r = (r * alpha + dst_r * (256 - alpha)) >> 8;
            int out_g = (g * alpha + dst_g * (256 - alpha)) >> 8;
            int out_b = (b * alpha + dst_b * (256 - alpha)) >> 8;
            pixel_buffer[idx] = (out_r << 16) | (out_g << 8) | out_b;
        }
    }
}

void
dash2d_fill_polygon_alpha(
    int* pixel_buffer,
    int stride,
    const int* x,
    const int* y,
    int n,
    int color_rgb,
    int alpha,
    int clip_left,
    int clip_top,
    int clip_right,
    int clip_bottom)
{
    if( n < 3 )
        return;
    int cx = 0;
    int cy = 0;
    for( int i = 0; i < n; i++ )
    {
        cx += x[i];
        cy += y[i];
    }
    cx /= n;
    cy /= n;
    for( int i = 0; i < n; i++ )
    {
        int j = (i + 1) % n;
        dash2d_fill_triangle_alpha(
            pixel_buffer,
            stride,
            cx,
            cy,
            x[i],
            y[i],
            x[j],
            y[j],
            color_rgb,
            alpha,
            clip_left,
            clip_top,
            clip_right,
            clip_bottom);
    }
}

void
dash2d_draw_rect_alpha(
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    int color_rgb,
    int alpha)
{
    int r = (color_rgb >> 16) & 0xFF;
    int g = (color_rgb >> 8) & 0xFF;
    int b = color_rgb & 0xFF;

    // Top and bottom edges
    for( int j = 0; j < width; j++ )
    {
        int top_idx = y * stride + (x + j);
        int bot_idx = (y + height - 1) * stride + (x + j);

        int dst_pixel = pixel_buffer[top_idx];
        int dst_r = (dst_pixel >> 16) & 0xFF;
        int dst_g = (dst_pixel >> 8) & 0xFF;
        int dst_b = dst_pixel & 0xFF;
        int out_r = (r * alpha + dst_r * (256 - alpha)) >> 8;
        int out_g = (g * alpha + dst_g * (256 - alpha)) >> 8;
        int out_b = (b * alpha + dst_b * (256 - alpha)) >> 8;
        pixel_buffer[top_idx] = (out_r << 16) | (out_g << 8) | out_b;

        dst_pixel = pixel_buffer[bot_idx];
        dst_r = (dst_pixel >> 16) & 0xFF;
        dst_g = (dst_pixel >> 8) & 0xFF;
        dst_b = dst_pixel & 0xFF;
        out_r = (r * alpha + dst_r * (256 - alpha)) >> 8;
        out_g = (g * alpha + dst_g * (256 - alpha)) >> 8;
        out_b = (b * alpha + dst_b * (256 - alpha)) >> 8;
        pixel_buffer[bot_idx] = (out_r << 16) | (out_g << 8) | out_b;
    }

    // Left and right edges
    for( int i = 1; i < height - 1; i++ )
    {
        int left_idx = (y + i) * stride + x;
        int right_idx = (y + i) * stride + (x + width - 1);

        int dst_pixel = pixel_buffer[left_idx];
        int dst_r = (dst_pixel >> 16) & 0xFF;
        int dst_g = (dst_pixel >> 8) & 0xFF;
        int dst_b = dst_pixel & 0xFF;
        int out_r = (r * alpha + dst_r * (256 - alpha)) >> 8;
        int out_g = (g * alpha + dst_g * (256 - alpha)) >> 8;
        int out_b = (b * alpha + dst_b * (256 - alpha)) >> 8;
        pixel_buffer[left_idx] = (out_r << 16) | (out_g << 8) | out_b;

        dst_pixel = pixel_buffer[right_idx];
        dst_r = (dst_pixel >> 16) & 0xFF;
        dst_g = (dst_pixel >> 8) & 0xFF;
        dst_b = dst_pixel & 0xFF;
        out_r = (r * alpha + dst_r * (256 - alpha)) >> 8;
        out_g = (g * alpha + dst_g * (256 - alpha)) >> 8;
        out_b = (b * alpha + dst_b * (256 - alpha)) >> 8;
        pixel_buffer[right_idx] = (out_r << 16) | (out_g << 8) | out_b;
    }
}

void
dash2d_draw_line_alpha(
    int* pixel_buffer,
    int stride,
    int x0,
    int y0,
    int x1,
    int y1,
    int color_rgb,
    int alpha,
    int clip_left,
    int clip_top,
    int clip_right,
    int clip_bottom)
{
    int r = (color_rgb >> 16) & 0xFF;
    int g = (color_rgb >> 8) & 0xFF;
    int b = color_rgb & 0xFF;

    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x1 >= x0) ? 1 : -1;
    int sy = (y1 >= y0) ? 1 : -1;

    if( dx >= dy )
    {
        int err = (dy << 1) - dx;
        while( 1 )
        {
            if( x0 >= clip_left && x0 < clip_right && y0 >= clip_top && y0 < clip_bottom )
            {
                int idx = y0 * stride + x0;
                int dst = pixel_buffer[idx];
                int dr = (dst >> 16) & 0xFF;
                int dg = (dst >> 8) & 0xFF;
                int db = dst & 0xFF;
                int or_ = (r * alpha + dr * (256 - alpha)) >> 8;
                int og = (g * alpha + dg * (256 - alpha)) >> 8;
                int ob = (b * alpha + db * (256 - alpha)) >> 8;
                pixel_buffer[idx] = (or_ << 16) | (og << 8) | ob;
            }
            if( x0 == x1 )
                break;
            x0 += sx;
            if( err > 0 )
            {
                y0 += sy;
                err -= (dx << 1);
            }
            err += (dy << 1);
        }
    }
    else
    {
        int err = (dx << 1) - dy;
        while( 1 )
        {
            if( x0 >= clip_left && x0 < clip_right && y0 >= clip_top && y0 < clip_bottom )
            {
                int idx = y0 * stride + x0;
                int dst = pixel_buffer[idx];
                int dr = (dst >> 16) & 0xFF;
                int dg = (dst >> 8) & 0xFF;
                int db = dst & 0xFF;
                int or_ = (r * alpha + dr * (256 - alpha)) >> 8;
                int og = (g * alpha + dg * (256 - alpha)) >> 8;
                int ob = (b * alpha + db * (256 - alpha)) >> 8;
                pixel_buffer[idx] = (or_ << 16) | (og << 8) | ob;
            }
            if( y0 == y1 )
                break;
            y0 += sy;
            if( err > 0 )
            {
                x0 += sx;
                err -= (dy << 1);
            }
            err += (dx << 1);
        }
    }
}

void
dash2d_blit_sprite_alpha(
    struct DashGraphics* dash,
    struct DashSprite* sprite,
    struct DashViewPort* view_port,
    int x,
    int y,
    int alpha,
    int* pixel_buffer)
{
    if( !sprite )
        return;
    x += sprite->crop_x;
    y += sprite->crop_y;

    int* src_pixels = sprite->pixels_argb;
    int src_width = sprite->width;
    int src_height = sprite->height;
    int stride = view_port->stride;

    for( int src_y = 0; src_y < src_height; src_y++ )
    {
        for( int src_x = 0; src_x < src_width; src_x++ )
        {
            int src_pixel = src_pixels[src_y * src_width + src_x];
            if( src_pixel == 0 )
                continue;

            int dst_x = x + src_x;
            int dst_y = y + src_y;

            // Apply clipping bounds
            if( dst_x < view_port->clip_left || dst_x >= view_port->clip_right ||
                dst_y < view_port->clip_top || dst_y >= view_port->clip_bottom )
                continue;

            int dst_idx = dst_y * stride + dst_x;
            int dst_pixel = pixel_buffer[dst_idx];

            int src_r = (src_pixel >> 16) & 0xFF;
            int src_g = (src_pixel >> 8) & 0xFF;
            int src_b = src_pixel & 0xFF;

            int dst_r = (dst_pixel >> 16) & 0xFF;
            int dst_g = (dst_pixel >> 8) & 0xFF;
            int dst_b = dst_pixel & 0xFF;

            int out_r = (src_r * alpha + dst_r * (256 - alpha)) >> 8;
            int out_g = (src_g * alpha + dst_g * (256 - alpha)) >> 8;
            int out_b = (src_b * alpha + dst_b * (256 - alpha)) >> 8;

            pixel_buffer[dst_idx] = (out_r << 16) | (out_g << 8) | out_b;
        }
    }
}

void
dash2d_set_bounds(
    struct DashViewPort* view_port,
    int left,
    int top,
    int right,
    int bottom)
{
    view_port->clip_left = left;
    view_port->clip_top = top;
    view_port->clip_right = right;
    view_port->clip_bottom = bottom;
}

static int g_minimap_tile_rotation_map[4][16] = {
    { 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15 },
    { 12, 8,  4,  0,  13, 9,  5,  1,  14, 10, 6,  2,  15, 11, 7,  3  },
    { 15, 14, 13, 12, 11, 10, 9,  8,  7,  6,  5,  4,  3,  2,  1,  0  },
    { 3,  7,  11, 15, 2,  6,  10, 14, 1,  5,  9,  13, 0,  4,  8,  12 },
};

static int g_minimap_tile_mask[16][16] = {
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
    { 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 1, 1, 1, 1 },
    { 1, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 },
    { 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1 },
    { 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
    { 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1 },
    { 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0 },
    { 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 0, 1, 1 },
    { 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 1, 1 }
};

void
dash2d_fill_minimap_tile(
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int background_rgb,
    int foreground_rgb,
    int angle,
    int shape,
    int clip_width,
    int clip_height)
{
    assert(shape >= 0 && shape < 16);
    assert(angle >= 0 && angle < 4);

    // Early exit if tile is completely outside bounds
    if( x + 3 < 0 || x >= clip_width || y + 3 < 0 || y >= clip_height )
    {
        return;
    }

    int* mask = g_minimap_tile_mask[shape];
    int* rotation = g_minimap_tile_rotation_map[angle];

    int offset = (x) + (y)*stride;
    if( foreground_rgb == 0 )
    {
        if( background_rgb != 0 )
        {
            for( int i = 0; i < 4; i++ )
            {
                int current_y = y + i;
                if( current_y >= 0 && current_y < clip_height )
                {
                    if( x >= 0 && x < clip_width )
                        pixel_buffer[offset] = background_rgb;
                    if( x + 1 >= 0 && x + 1 < clip_width )
                        pixel_buffer[offset + 1] = background_rgb;
                    if( x + 2 >= 0 && x + 2 < clip_width )
                        pixel_buffer[offset + 2] = background_rgb;
                    if( x + 3 >= 0 && x + 3 < clip_width )
                        pixel_buffer[offset + 3] = background_rgb;
                }
                offset += stride;
            }
        }
        return;
    }

    int shape_vertex_index = 0;
    if( background_rgb != 0 )
    {
        for( int i = 0; i < 4; i++ )
        {
            int current_y = y + i;
            if( current_y >= 0 && current_y < clip_height )
            {
                int color0 =
                    mask[rotation[shape_vertex_index++]] == 0 ? background_rgb : foreground_rgb;
                int color1 =
                    mask[rotation[shape_vertex_index++]] == 0 ? background_rgb : foreground_rgb;
                int color2 =
                    mask[rotation[shape_vertex_index++]] == 0 ? background_rgb : foreground_rgb;
                int color3 =
                    mask[rotation[shape_vertex_index++]] == 0 ? background_rgb : foreground_rgb;

                if( x >= 0 && x < clip_width )
                    pixel_buffer[offset] = color0;
                if( x + 1 >= 0 && x + 1 < clip_width )
                    pixel_buffer[offset + 1] = color1;
                if( x + 2 >= 0 && x + 2 < clip_width )
                    pixel_buffer[offset + 2] = color2;
                if( x + 3 >= 0 && x + 3 < clip_width )
                    pixel_buffer[offset + 3] = color3;
            }
            else
            {
                // Skip shape_vertex_index even if we're not drawing
                shape_vertex_index += 4;
            }
            offset += stride;
        }
        return;
    }

    for( int i = 0; i < 4; i++ )
    {
        int current_y = y + i;
        if( current_y >= 0 && current_y < clip_height )
        {
            if( mask[rotation[shape_vertex_index++]] != 0 )
            {
                if( x >= 0 && x < clip_width )
                    pixel_buffer[offset] = foreground_rgb;
            }
            if( mask[rotation[shape_vertex_index++]] != 0 )
            {
                if( x + 1 >= 0 && x + 1 < clip_width )
                    pixel_buffer[offset + 1] = foreground_rgb;
            }
            if( mask[rotation[shape_vertex_index++]] != 0 )
            {
                if( x + 2 >= 0 && x + 2 < clip_width )
                    pixel_buffer[offset + 2] = foreground_rgb;
            }
            if( mask[rotation[shape_vertex_index++]] != 0 )
            {
                if( x + 3 >= 0 && x + 3 < clip_width )
                    pixel_buffer[offset + 3] = foreground_rgb;
            }
        }
        else
        {
            // Skip shape_vertex_index even if we're not drawing
            shape_vertex_index += 4;
        }
        offset += stride;
    }
}

void
dash2d_draw_minimap_wall(
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int wall,
    int clip_width,
    int clip_height)
{
    // Early exit if tile is completely outside bounds
    if( x + 3 < 0 || x >= clip_width || y + 3 < 0 || y >= clip_height )
    {
        return;
    }

    int rgb = 0xFFFFFFFF;
    int offset = y * stride + x;

    // Draw WEST wall (left edge, vertical line)
    if( wall & MINIMAP_WALL_WEST )
    {
        for( int p = 0; p < 4; p++ )
        {
            int current_y = y + p;
            if( current_y >= 0 && current_y < clip_height && x >= 0 && x < clip_width )
            {
                pixel_buffer[offset + p * stride] = rgb;
            }
        }
    }

    // Draw NORTH wall (top edge, horizontal line)
    if( wall & MINIMAP_WALL_NORTH )
    {
        for( int p = 0; p < 4; p++ )
        {
            int current_x = x + p;
            if( current_x >= 0 && current_x < clip_width && y >= 0 && y < clip_height )
            {
                pixel_buffer[offset + p] = rgb;
            }
        }
    }

    // Draw EAST wall (right edge, vertical line)
    if( wall & MINIMAP_WALL_EAST )
    {
        for( int p = 0; p < 4; p++ )
        {
            int current_y = y + p;
            int current_x = x + 3;
            if( current_y >= 0 && current_y < clip_height && current_x >= 0 &&
                current_x < clip_width )
            {
                pixel_buffer[offset + 3 + p * stride] = rgb;
            }
        }
    }

    // Draw SOUTH wall (bottom edge, horizontal line)
    if( wall & MINIMAP_WALL_SOUTH )
    {
        for( int p = 0; p < 4; p++ )
        {
            int current_x = x + p;
            int current_y = y + 3;
            if( current_x >= 0 && current_x < clip_width && current_y >= 0 &&
                current_y < clip_height )
            {
                pixel_buffer[offset + p + 3 * stride] = rgb;
            }
        }
    }

    // Draw NORTHEAST_SOUTHWEST diagonal (top-right to bottom-left)
    if( wall & MINIMAP_WALL_NORTHEAST_SOUTHWEST )
    {
        // Draw diagonal: (x+3, y+0), (x+2, y+1), (x+1, y+2), (x+0, y+3)
        for( int p = 0; p < 4; p++ )
        {
            int current_x = x + (3 - p);
            int current_y = y + p;
            if( current_x >= 0 && current_x < clip_width && current_y >= 0 &&
                current_y < clip_height )
            {
                pixel_buffer[offset + p * stride + (3 - p)] = rgb;
            }
        }
    }

    // Draw NORTHWEST_SOUTHEAST diagonal (top-left to bottom-right)
    if( wall & MINIMAP_WALL_NORTHWEST_SOUTHEAST )
    {
        // Draw diagonal: (x+0, y+0), (x+1, y+1), (x+2, y+2), (x+3, y+3)
        for( int p = 0; p < 4; p++ )
        {
            int current_x = x + p;
            int current_y = y + p;
            if( current_x >= 0 && current_x < clip_width && current_y >= 0 &&
                current_y < clip_height )
            {
                pixel_buffer[offset + p * stride + p] = rgb;
            }
        }
    }
}

void
dash2d_blit_rotated(
    struct DashSprite* sprite,
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    int anchor_x,
    int anchor_y,
    int angle_r2pi2048)
{
    // try {
    int zoom = 1;
    int centerX = (-width / 2);
    int centerY = (-height / 2);

    int sin = (dash_sin(angle_r2pi2048)) | 0;
    int cos = (dash_cos(angle_r2pi2048)) | 0;
    int sinZoom = (sin * zoom) >> 8;
    int cosZoom = (cos * zoom) >> 8;

    int leftX = (anchor_x << 16) + centerY * sinZoom + centerX * cosZoom;
    int leftY = (anchor_y << 16) + (centerY * cosZoom - centerX * sinZoom);
    int leftOff = x + y * stride;

    for( int i = 0; i < height; i++ )
    {
        int dstOff = i * stride;
        int dstX = leftOff + dstOff;

        int srcX = leftX + cosZoom * dstOff;
        int srcY = leftY - sinZoom * dstOff;

        for( int j = -stride; j < 0; j++ )
        {
            pixel_buffer[dstX++] = sprite->pixels_argb[(srcX >> 16) + (srcY >> 16) * sprite->width];
            srcX += cosZoom;
            srcY -= sinZoom;
        }

        leftX += sinZoom;
        leftY += cosZoom;
        leftOff += stride;
    }
    // } catch (e) {
    //     /* empty */
    // }
}

void
dashframe_free(struct DashFrame* frame)
{
    if( !frame )
        return;

    free(frame->index_frame_ids);
    free(frame->translator_arg_x);
    free(frame->translator_arg_y);
    free(frame->translator_arg_z);
    free(frame);
    frame = NULL;
}

void
dashframemap_free(struct DashFramemap* framemap)
{
    if( !framemap )
        return;

    for( int i = 0; i < framemap->length; i++ )
    {
        free(framemap->bone_groups[i]);
    }
    free(framemap->bone_groups);
    free(framemap->bone_groups_lengths);
    free(framemap->types);
    free(framemap);
}