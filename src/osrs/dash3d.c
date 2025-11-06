#include "dash3d.h"

#include "dash3d_intqueue.u.c"
#include "dash3d_painters.h"
#include "graphics/projection.u.c"
#include "tables/model.h"

static inline int
project_vertices_model_textured(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    struct AABB* aabb,
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
    int model_radius,
    int model_cylinder_center_to_top_edge,
    int model_cylinder_center_to_bottom_edge,
    int model_min_y,
    int model_max_y,
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
    struct ProjectedVertex projected_vertex;

    project_orthographic_fast(
        &projected_vertex, 0, 0, 0, model_yaw, scene_x, scene_y, scene_z, camera_pitch, camera_yaw);

    int model_edge_radius = model_radius;

    // int a = (scene_z * cos_camera_yaw - scene_x * sin_camera_yaw) >> 16;
    // // b is the z projection of the models origin (imagine a vertex at x=0,y=0 and z=0).
    // // So the depth is the z projection distance from the origin of the model.
    // int b = (scene_y * sin_camera_pitch + a * cos_camera_pitch) >> 16;

    /**
     * These checks are a significant performance improvement.
     */
    int mid_z = projected_vertex.z;
    int max_z = model_edge_radius + mid_z;
    if( max_z < near_plane_z )
    {
        // The edge of the model that is farthest from the camera is too close to the near plane.
        return 0;
    }

    if( mid_z > 3500 )
    {
        // Model too far away.
        return 0;
    }

    int mid_x = projected_vertex.x;
    int mid_y = projected_vertex.y;

    if( mid_z < near_plane_z )
        mid_z = near_plane_z;

    int ortho_screen_x_min = mid_x - model_edge_radius;
    int ortho_screen_x_max = mid_x + model_edge_radius;

    int screen_x_min_unoffset =
        project_divide(ortho_screen_x_min, mid_z + model_edge_radius, camera_fov);
    int screen_x_max_unoffset =
        project_divide(ortho_screen_x_max, mid_z + model_edge_radius, camera_fov);
    int screen_edge_width = screen_width >> 1;

    if( screen_x_min_unoffset > screen_edge_width || screen_x_max_unoffset < -screen_edge_width )
    {
        // All parts of the model left or right edges are projected off screen.
        return 0;
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

    int model_center_to_top_edge = model_cylinder_center_to_top_edge;

    int model_center_to_bottom_edge =
        (model_cylinder_center_to_bottom_edge * g_cos_table[camera_pitch] >> 16) +
        (model_edge_radius * g_sin_table[camera_pitch] >> 16);

    int screen_y_min_unoffset =
        project_divide(mid_y - abs(model_center_to_bottom_edge), mid_z, camera_fov);
    int screen_y_max_unoffset =
        project_divide(mid_y + abs(model_center_to_top_edge), mid_z, camera_fov);
    int screen_edge_height = screen_height >> 1;
    if( screen_y_min_unoffset > screen_edge_height || screen_y_max_unoffset < -screen_edge_height )
    {
        // All parts of the model top or bottom edges are projected off screen.
        return 0;
    }

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

    if( aabb->min_screen_x >= screen_width )
        return 0;
    if( aabb->min_screen_y >= screen_height )
        return 0;
    if( aabb->max_screen_x < 0 )
        return 0;
    if( aabb->max_screen_y < 0 )
        return 0;

    //  int cos_camera_radius = model_edge_radius * g_cos_table[camera_pitch] >> 16;
    //  int sin_camera_radius = model_edge_radius * g_sin_table[camera_pitch] >> 16;
    //  int min_z = mid_z - (cos_camera_radius);
    //  if( min_z < near_plane_z )
    //      min_z = near_plane_z;

    //  int height_sin = model_cylinder_center_to_bottom_edge * g_sin_table[camera_pitch] >> 16;

    //  int min_screen_x = mid_x - model_edge_radius;
    //  if( mid_x > 0 )
    //  {
    //      aabb->min_screen_x =
    //          project_divide(mid_x - model_edge_radius - model_edge_radius, max_z, camera_fov) +
    //          screen_width / 2;

    //      aabb->max_screen_x =
    //          project_divide(
    //              mid_x + (model_edge_radius + height_sin + sin_camera_radius), min_z, camera_fov)
    //              +
    //          screen_width / 2;
    //  }
    //  else
    //  {
    //      aabb->min_screen_x =
    //          project_divide(
    //              mid_x - model_edge_radius - height_sin - sin_camera_radius, min_z, camera_fov) +
    //          screen_width / 2;
    //      aabb->max_screen_x =
    //          project_divide(mid_x + model_edge_radius + model_edge_radius, max_z, camera_fov) +
    //          screen_width / 2;
    //  }

    //  int height_cos = model_cylinder_center_to_bottom_edge;

    //  int highest_radius = model_edge_radius * g_sin_table[camera_pitch] >> 16;

    //  int min_screen_y = mid_y - (model_edge_radius)-height_cos;
    //  int max_screen_y = mid_y + (highest_radius) + height_sin;
    //  if( mid_y > 0 )
    //  {
    //      if( mid_y - model_cylinder_center_to_bottom_edge > 0 )
    //          aabb->min_screen_y =
    //              project_divide(min_screen_y, max_z, camera_fov) + screen_height / 2;
    //      else
    //          aabb->min_screen_y =
    //              project_divide(min_screen_y, min_z, camera_fov) + screen_height / 2;

    //      aabb->max_screen_y = project_divide(max_screen_y, min_z, camera_fov) + screen_height /
    //      2;
    //  }
    //  else
    //  {
    //      // min_screen_y -= highest_projected;
    //      aabb->min_screen_y = project_divide(min_screen_y, min_z, camera_fov) + screen_height /
    //      2;

    //      aabb->max_screen_y = project_divide(max_screen_y, max_z, camera_fov) + screen_height /
    //      2;
    //  }

    project_vertices_array(
        orthographic_vertices_x,
        orthographic_vertices_y,
        orthographic_vertices_z,
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        vertex_x,
        vertex_y,
        vertex_z,
        num_vertices,
        model_yaw,
        mid_z,
        scene_x,
        scene_y,
        scene_z,
        near_plane_z,
        camera_fov,
        camera_pitch,
        camera_yaw);

    return 1;
}

/**
 * Terrain is treated as a single, so the origin test does not apply.
 */
static inline int
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
    struct ProjectedVertex projected_vertex;

    // TODO: This is normally solved by the frustrum cullmap,
    // but that still has some issues if the cullmap is too generous.
    // We run into overflow issues.

    project_orthographic_fast(
        &projected_vertex,
        vertex_x[0] + 64,
        vertex_y[0],
        vertex_z[0] + 64,
        model_yaw,
        -camera_x,
        // Camera z is the y axis in OSRS
        -camera_z,
        -camera_y,
        camera_pitch,
        camera_yaw);

    int model_edge_radius = 100;
    int max_z = projected_vertex.z + model_edge_radius;

    if( max_z < near_plane_z )
    {
        // The edge of the model that is farthest from the camera is too close to the near plane.
        return 0;
    }

    int screen_edge_width = screen_width >> 1;
    int mid_x = projected_vertex.x;

    int left_x = mid_x + model_edge_radius;
    int right_x = mid_x - model_edge_radius;

    if( project_divide(left_x, max_z, camera_fov) < -screen_edge_width ||
        project_divide(right_x, max_z, camera_fov) > screen_edge_width )
    {
        // All parts of the model left or right edges are projected off screen.
        return 0;
    }

    // Calculate FOV scale based on the angle using sin/cos tables
    // fov is in units of (2π/2048) radians
    // For perspective projection, we need tan(fov/2)
    // tan(x) = sin(x)/cos(x)
    int fov_half = camera_fov >> 1; // fov/2

    // fov_scale = 1/tan(fov/2)
    // cot(x) = 1/tan(x)
    // cot(x) = tan(pi/2 - x)
    // cot(x + pi) = cot(x)
    // assert(fov_half < 1536);
    int cot_fov_half_ish16 = g_tan_table[1536 - fov_half];

    static const int scale_angle = 1;
    int cot_fov_half_ish_scaled = cot_fov_half_ish16 >> scale_angle;

    // Checked on 09/15/2025, this does get vectorized on Mac, using arm neon.
    for( int i = 0; i < num_vertices; i++ )
    {
        project_orthographic_fast(
            &projected_vertex,
            vertex_x[i],
            vertex_y[i],
            vertex_z[i],
            model_yaw,
            -camera_x,
            -camera_z,
            -camera_y,
            camera_pitch,
            camera_yaw);

        int x = projected_vertex.x;
        int y = projected_vertex.y;
        int z = projected_vertex.z;

        x *= cot_fov_half_ish_scaled;
        y *= cot_fov_half_ish_scaled;
        x >>= 16 - scale_angle;
        y >>= 16 - scale_angle;

        // So we can increase x_bits_max to 11 by reducing the angle scale by 1.
        int screen_x = SCALE_UNIT(x);
        int screen_y = SCALE_UNIT(y);
        // screen_x *= cot_fov_half_ish_scaled;
        // screen_y *= cot_fov_half_ish_scaled;
        // screen_x >>= 16 - scale_angle;
        // screen_y >>= 16 - scale_angle;

        // Set the projected triangle

        orthographic_vertices_x[i] = projected_vertex.x;
        orthographic_vertices_y[i] = projected_vertex.y;
        orthographic_vertices_z[i] = projected_vertex.z;

        screen_vertices_x[i] = screen_x;
        screen_vertices_y[i] = screen_y;
        screen_vertices_z[i] = z;
    }

    // int cot_fov_half_ish15 = calc_cot_fov_half_ish15(camera_fov);

    // for( int i = 0; i < num_vertices; i++ )
    // {
    //     int x = orthographic_vertices_x[i];
    //     int y = orthographic_vertices_y[i];
    //     int z = orthographic_vertices_z[i];

    //     // Apply FOV scaling to x and y coordinates

    //     // unit scale 9, angle scale 16
    //     // then 6 bits of valid x/z. (31 - 25 = 6), signed int.

    //     // if valid x is -Screen_width/2 to Screen_width/2
    //     // And the max resolution we allow is 1600 (either dimension)
    //     // then x_bits_max = 10; because 2^10 = 1024 > (1600/2)

    //     // If we have 6 bits of valid x then x_bits_max - z_clip_bits < 6
    //     // i.e. x/z < 2^6 or x/z < 64

    //     // Suppose we allow z > 16, so z_clip_bits = 4
    //     // then x_bits_max < 10, so 2^9, which is 512
    // }

    for( int i = 0; i < num_vertices; i++ )
    {
        // project_perspective_fast_fov2(
        //     &projected_vertex,
        //     orthographic_vertices_x[i],
        //     orthographic_vertices_y[i],
        //     orthographic_vertices_z[i],
        //     cot_fov_half_ish15,
        //     near_plane_z);

        // project_perspective_fast(
        //     &projected_vertex,
        //     orthographic_vertices_x[i],
        //     orthographic_vertices_y[i],
        //     orthographic_vertices_z[i],
        //     camera_fov,
        //     near_plane_z);

        int z = screen_vertices_z[i];

        bool clipped = false;
        if( z < near_plane_z )
            clipped = true;

        // If vertex is too close to camera, set it to a large negative value
        // This will cause it to be clipped in the rasterization step
        // screen_vertices_z[i] = projected_vertex.z - mid_z;
        screen_vertices_z[i] = z;

        if( clipped )
        {
            screen_vertices_x[i] = -5000;
        }
        else
        {
            screen_vertices_x[i] = screen_vertices_x[i] / z;
            // TODO: The actual renderer from the deob marks that a face was clipped.
            // so it doesn't have to worry about a value actually being -5000.
            if( screen_vertices_x[i] == -5000 )
                screen_vertices_x[i] = -5001;
            screen_vertices_y[i] = screen_vertices_y[i] / z;
        }
    }

    return 1;
}

struct Dash3D
{
    short tmp_depth_face_count[1500];
    short tmp_depth_faces[1500 * 512];
    short tmp_priority_face_count[12];
    short tmp_priority_depth_sum[12];
    short tmp_priority_faces[12 * 2000];
    int tmp_flex_prio11_face_to_depth[1024];
    int tmp_flex_prio12_face_to_depth[512];
    int tmp_face_order[1024];

    int tmp_screen_vertices_x[4096];
    int tmp_screen_vertices_y[4096];
    int tmp_screen_vertices_z[4096];

    int tmp_orthographic_vertices_x[4096];
    int tmp_orthographic_vertices_y[4096];
    int tmp_orthographic_vertices_z[4096];

    struct IntQueue tmp_painters_queue;
    struct IntQueue tmp_painters_catchup_queue;

    int tmp_painters_ops_length;
    struct Dash3DPainterOp tmp_painters_ops[4096 * 3];
    struct PaintersElement tmp_painters_elements[32000];

    int camera_x;
    int camera_y;
    int camera_z;
    int camera_pitch;
    int camera_yaw;
    int camera_fov;

    int screen_width;
    int screen_height;
    int near_plane_z;
};

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
    struct Dash3D* dash3d,
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
    short* priority_depths = dash3d->tmp_priority_depth_sum;
    int* flex_prio11_face_to_depth = dash3d->tmp_flex_prio11_face_to_depth;
    int* flex_prio12_face_to_depth = dash3d->tmp_flex_prio12_face_to_depth;

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

struct Dash3D*
dash3d_new(int screen_width, int screen_height, int near_plane_z)
{
    struct Dash3D* dash3d = (struct Dash3D*)malloc(sizeof(struct Dash3D));
    memset(dash3d, 0, sizeof(struct Dash3D));

    dash3d->screen_width = screen_width;
    dash3d->screen_height = screen_height;
    dash3d->near_plane_z = near_plane_z;

    int_queue_init(&dash3d->tmp_painters_queue, 32000);
    int_queue_init(&dash3d->tmp_painters_catchup_queue, 32000);

    return dash3d;
}

void
dash3d_free(struct Dash3D* dash3d)
{
    free(dash3d);
}

void
dash3d_set_screen_size(struct Dash3D* dash3d, int screen_width, int screen_height)
{
    dash3d->screen_width = screen_width;
    dash3d->screen_height = screen_height;
}

struct Dash3DModelPainterIter
{
    struct Dash3D* dash3d;
    struct SceneModel* scene_model;

    int index;
    int valid_faces;
    int value_face;
    struct AABB aabb;
};

void
dash3d_model_painters_iter_new(struct Dash3D* dash3d, struct SceneModel* scene_model)
{
    struct Dash3DModelPainterIter* iter =
        (struct Dash3DModelPainterIter*)malloc(sizeof(struct Dash3DModelPainterIter));
    memset(iter, 0, sizeof(struct Dash3DModelPainterIter));
    iter->dash3d = dash3d;
    iter->scene_model = scene_model;
}

void
dash3d_model_painters_iter_free(struct Dash3DModelPainterIter* iter)
{
    free(iter);
}

void
dash3d_model_painters_iter_begin(struct Dash3DModelPainterIter* iter)
{
    memset(iter, 0, sizeof(struct Dash3DModelPainterIter));

    struct Dash3D* iter_dash3d = iter->dash3d;
    struct SceneModel* iter_scene_model = iter->scene_model;

    struct CacheModel* model = iter_scene_model->model;
    iter->index = 0;

    int* vertices_x = model->vertices_x;
    int* vertices_y = model->vertices_y;
    int* vertices_z = model->vertices_z;

    int* face_indices_a = model->face_indices_a;
    int* face_indices_b = model->face_indices_b;
    int* face_indices_c = model->face_indices_c;

    int* screen_vertices_x = iter_dash3d->tmp_screen_vertices_x;
    int* screen_vertices_y = iter_dash3d->tmp_screen_vertices_y;
    int* screen_vertices_z = iter_dash3d->tmp_screen_vertices_z;

    int* orthographic_vertices_x = iter_dash3d->tmp_orthographic_vertices_x;
    int* orthographic_vertices_y = iter_dash3d->tmp_orthographic_vertices_y;
    int* orthographic_vertices_z = iter_dash3d->tmp_orthographic_vertices_z;

    int scene_x = iter_scene_model->region_x - iter_dash3d->camera_x;
    int scene_y = iter_scene_model->region_height - iter_dash3d->camera_y;
    int scene_z = iter_scene_model->region_z - iter_dash3d->camera_z;
    int yaw = iter_scene_model->yaw + iter_dash3d->camera_yaw;
    // yaw %= 2048;
    yaw &= 0x7FF;

    scene_x += iter_scene_model->offset_x;
    scene_y += iter_scene_model->offset_height;
    scene_z += iter_scene_model->offset_z;

    if( !iter_scene_model->bounds_cylinder )
    {
        iter_scene_model->bounds_cylinder =
            (struct BoundsCylinder*)malloc(sizeof(struct BoundsCylinder));
        *iter_scene_model->bounds_cylinder = calculate_bounds_cylinder(
            model->vertex_count, model->vertices_x, model->vertices_y, model->vertices_z);
    }

    int success = project_vertices_model_textured(
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        &iter->aabb,
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
        iter_scene_model->bounds_cylinder->radius,
        iter_scene_model->bounds_cylinder->center_to_top_edge,
        iter_scene_model->bounds_cylinder->center_to_bottom_edge,
        iter_scene_model->bounds_cylinder->min_y,
        iter_scene_model->bounds_cylinder->max_y,
        scene_x,
        scene_y,
        scene_z,
        iter_dash3d->camera_pitch,
        iter_dash3d->camera_yaw,
        0,
        iter_dash3d->camera_fov,
        iter_dash3d->screen_width,
        iter_dash3d->screen_height,
        iter_dash3d->near_plane_z);

    if( !success )
        return;

    int model_min_depth = iter_scene_model->bounds_cylinder->min_z_depth_any_rotation;

    memset(
        iter_dash3d->tmp_depth_face_count,
        0,
        (model_min_depth * 2 + 1) * sizeof(iter_dash3d->tmp_depth_face_count[0]));

    int bounds = bucket_sort_by_average_depth(
        iter_dash3d->tmp_depth_faces,
        iter_dash3d->tmp_depth_face_count,
        model_min_depth,
        model->face_count,
        screen_vertices_x,
        screen_vertices_y,
        screen_vertices_z,
        face_indices_a,
        face_indices_b,
        face_indices_c);

    model_min_depth = bounds & 0xFFFF;
    int model_max_depth = bounds >> 16;

    if( !model->face_priorities )
    {
        for( int depth = model_max_depth; depth < 1500 && depth >= model_min_depth; depth-- )
        {
            short bucket_count = iter_dash3d->tmp_depth_face_count[depth];
            if( bucket_count == 0 )
                continue;
            short* faces = &iter_dash3d->tmp_depth_faces[depth << 9];
            for( int j = 0; j < bucket_count; j++ )
            {
                short face = faces[j];
                iter_dash3d->tmp_face_order[iter->valid_faces++] = face;
            }
        }
    }
    else
    {
        memset(
            iter_dash3d->tmp_priority_face_count, 0, sizeof(iter_dash3d->tmp_priority_face_count));
        memset(iter_dash3d->tmp_priority_depth_sum, 0, sizeof(iter_dash3d->tmp_priority_depth_sum));

        parition_faces_by_priority(
            iter_dash3d->tmp_priority_faces,
            iter_dash3d->tmp_priority_face_count,
            iter_dash3d->tmp_depth_faces,
            iter_dash3d->tmp_depth_face_count,
            model->face_count,
            model->face_priorities,
            model_min_depth,
            model_max_depth);

        int valid_faces = sort_face_draw_order(
            iter_dash3d,
            iter_dash3d->tmp_face_order,
            iter_dash3d->tmp_depth_faces,
            iter_dash3d->tmp_depth_face_count,
            iter_dash3d->tmp_priority_faces,
            iter_dash3d->tmp_priority_face_count,
            model->face_count,
            model->face_priorities,
            model_min_depth,
            model_max_depth);

        iter->valid_faces = valid_faces;
    }
}

bool
dash3d_model_painters_iter_next(struct Dash3DModelPainterIter* iter)
{
    if( iter->index >= iter->valid_faces )
        return false;
    iter->value_face = iter->dash3d->tmp_face_order[iter->index];
    iter->index++;
    return true;
}

int
dash3d_model_painters_iter_value_face(struct Dash3DModelPainterIter* iter)
{
    return iter->value_face;
}

struct AABB*
dash3d_model_painters_iter_aabb(struct Dash3DModelPainterIter* iter)
{
    return &iter->aabb;
}

#include "dash3d_painters.c"

struct Dash3DScenePainterIter
{
    struct Dash3D* dash3d;
    struct Scene* scene;

    int ops_length;
    struct Dash3DPainterOp* ops;
};

void
dash3d_scene_painters_iter_begin(struct Dash3DScenePainterIter* iter)
{
    scene_painters_algorithm(
        iter->scene,
        &iter->dash3d->tmp_painters_queue,
        &iter->dash3d->tmp_painters_catchup_queue,
        iter->dash3d->tmp_painters_elements,
        iter->dash3d->camera_x,
        iter->dash3d->camera_y,
        iter->dash3d->camera_z,
        iter->dash3d->tmp_painters_ops,
        iter->dash3d->tmp_painters_ops_length);
}