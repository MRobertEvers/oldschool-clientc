#include "graphics/dash_restrict.h"
#include "graphics/projection.h"
#include "toridraw_math.h"
#include "toridraw_model_internal.h"
#include "toridraw_types.h"

#include <assert.h>
#include <limits.h>
#include <string.h>

#ifndef VERTEXINT_BITS
#define VERTEXINT_BITS 16
#endif

// clang-format off
#include "graphics/projection16_simd.u.c"
#include "graphics/projection_zdiv_simd.u.c"
// clang-format on

/** Far plane for bounding-cylinder frustum cull. */
#define TORIDRAW_CYLINDER_FAR_PLANE_Z 3500

static inline int
div3_fast_fixedpoint(int z_sum)
{
    return (z_sum * 21845) >> 16;
}

static inline int
toridraw_aabb_cull(
    struct ToriDraw_AABB* aabb,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera)
{
    (void)camera;
    int screen_width = view_port->width;
    int screen_height = view_port->height;

    if( aabb->min_screen_x >= screen_width )
        return TORIDRAW_CULL_AABB;
    if( aabb->min_screen_y >= screen_height )
        return TORIDRAW_CULL_AABB;
    if( aabb->max_screen_x < 0 )
        return TORIDRAW_CULL_AABB;
    if( aabb->max_screen_y < 0 )
        return TORIDRAW_CULL_AABB;

    return TORIDRAW_CULL_VISIBLE;
}

static inline int
toridraw_fast_cull(
    struct ToriDraw_AABB* aabb,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_Camera* camera,
    struct ProjectedVertex* projected_vertex)
{
    assert(hnd.kind != TORIDRAWMK_NONE);
    int model_yaw = position->yaw;
    int scene_x = position->x;
    int scene_y = position->y;
    int scene_z = position->z;

    int camera_pitch = camera->pitch;
    int camera_yaw = camera->yaw;
    int near_plane_z = camera->near_plane_z;

    int cull_mx = 0;
    int cull_my = 0;
    int cull_mz = 0;
    assert(hnd.kind == TORIDRAWMK_MODEL);

    project_orthographic_fast(
        projected_vertex,
        cull_mx,
        cull_my,
        cull_mz,
        model_yaw,
        scene_x,
        scene_y,
        scene_z,
        camera_pitch,
        camera_yaw);

    const struct ToriDraw_BoundsCylinder* bc = model_bounds_cylinder(hnd);
    if( !bc )
        return TORIDRAW_CULL_ERROR;

    int model_edge_radius = bc->radius;

    int mid_z = projected_vertex->z;
    int max_z = model_edge_radius + mid_z;
    if( max_z < near_plane_z )
        return TORIDRAW_CULL_FAST;

    if( mid_z > TORIDRAW_CYLINDER_FAR_PLANE_Z )
        return TORIDRAW_CULL_FAST;

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
        return TORIDRAW_CULL_FAST;

    int model_center_to_top_edge = bc->center_to_top_edge;

    int model_center_to_bottom_edge =
        (bc->center_to_bottom_edge * g_cos_table[camera_pitch] >> 16) +
        (model_edge_radius * g_sin_table[camera_pitch] >> 16);

    int screen_y_min_unoffset =
        project_divide(mid_y - abs(model_center_to_bottom_edge), mid_z, camera->fov_rpi2048);
    int screen_y_max_unoffset =
        project_divide(mid_y + abs(model_center_to_top_edge), mid_z, camera->fov_rpi2048);
    int screen_edge_height = view_port->height >> 1;
    if( screen_y_min_unoffset > screen_edge_height || screen_y_max_unoffset < -screen_edge_height )
        return TORIDRAW_CULL_FAST;

    aabb->min_screen_x = screen_x_min_unoffset + view_port->x_center;
    aabb->min_screen_y = screen_y_min_unoffset + view_port->y_center;
    aabb->max_screen_x = screen_x_max_unoffset + view_port->x_center;
    aabb->max_screen_y = screen_y_max_unoffset + view_port->y_center;
    aabb->kind = TORIDRAW_AABB_KIND_CYLINDER_4POINT;

    return TORIDRAW_CULL_VISIBLE;
}

static void
toridraw_calculate_cylinder_aabb_8point(
    struct ToriDraw_AABB* aabb,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera)
{
    const struct ToriDraw_BoundsCylinder* bcyl = model_bounds_cylinder(hnd);
    assert(bcyl);
    int model_edge_radius = bcyl->radius;
    int model_min_y = bcyl->min_y;
    int model_max_y = bcyl->max_y;

    int mx = 0;
    int mz = 0;

    vertexint_t bb_x[8] = {
        (vertexint_t)(mx + model_edge_radius), (vertexint_t)(mx + model_edge_radius),
        (vertexint_t)(mx + model_edge_radius), (vertexint_t)(mx + model_edge_radius),
        (vertexint_t)(mx - model_edge_radius), (vertexint_t)(mx - model_edge_radius),
        (vertexint_t)(mx - model_edge_radius), (vertexint_t)(mx - model_edge_radius)
    };
    vertexint_t bb_y[8] = { (vertexint_t)model_min_y, (vertexint_t)model_min_y,
                            (vertexint_t)model_max_y, (vertexint_t)model_max_y,
                            (vertexint_t)model_min_y, (vertexint_t)model_min_y,
                            (vertexint_t)model_max_y, (vertexint_t)model_max_y };
    vertexint_t bb_z[8] = {
        (vertexint_t)(mz + model_edge_radius), (vertexint_t)(mz - model_edge_radius),
        (vertexint_t)(mz + model_edge_radius), (vertexint_t)(mz - model_edge_radius),
        (vertexint_t)(mz + model_edge_radius), (vertexint_t)(mz - model_edge_radius),
        (vertexint_t)(mz + model_edge_radius), (vertexint_t)(mz - model_edge_radius)
    };

    int sc_x[8];
    int sc_y[8];
    int sc_z[8];

    project_vertices_array_fused_notex(
        sc_x,
        sc_y,
        sc_z,
        bb_x,
        bb_y,
        bb_z,
        8,
        position->yaw,
        0,
        position->x,
        position->y,
        position->z,
        camera->near_plane_z,
        camera->fov_rpi2048,
        camera->pitch,
        camera->yaw);

    int min_sx = sc_x[0];
    int max_sx = sc_x[0];
    int min_sy = sc_y[0];
    int max_sy = sc_y[0];

    for( int i = 1; i < 8; i++ )
    {
        int sx = sc_x[i];
        int sy = sc_y[i];

        if( sx < min_sx )
            min_sx = sx;
        else if( sx > max_sx )
            max_sx = sx;

        if( sy < min_sy )
            min_sy = sy;
        else if( sy > max_sy )
            max_sy = sy;
    }

    int cx = view_port->x_center;
    int cy = view_port->y_center;

    aabb->min_screen_x = min_sx + cx;
    aabb->max_screen_x = max_sx + cx;
    aabb->min_screen_y = min_sy + cy;
    aabb->max_screen_y = max_sy + cy;

    aabb->kind = TORIDRAW_AABB_KIND_CYLINDER_8POINT;
}

static inline int
bucket_sort_by_average_depth(
    faceint_t* RESTRICT face_depth_buckets,
    faceint_t* RESTRICT face_depth_bucket_counts,
    int model_min_depth,
    int num_faces,
    const int* RESTRICT vx,
    const int* RESTRICT vy,
    const int* RESTRICT vz,
    const faceint_t* RESTRICT face_a,
    const faceint_t* RESTRICT face_b,
    const faceint_t* RESTRICT face_c)
{
    int min_d = 1500;
    int max_d = 0;

    for( int f = 0; f < num_faces; f++ )
    {
        const uint32_t a = face_a[f];
        const uint32_t b = face_b[f];
        const uint32_t c = face_c[f];

        const int dx1 = vx[a] - vx[b];
        const int dy1 = vy[a] - vy[b];
        const int dx2 = vx[c] - vx[b];
        const int dy2 = vy[c] - vy[b];

        if( (dx1 * dy2 - dy1 * dx2) > 0 )
        {
            int z_sum = vz[a] + vz[b] + vz[c];
            int depth_avg = div3_fast_fixedpoint(z_sum) + model_min_depth;

            if( (unsigned int)depth_avg < 1500 )
            {
                const int count = face_depth_bucket_counts[depth_avg];
                face_depth_bucket_counts[depth_avg] = count + 1;
                face_depth_buckets[(depth_avg << 9) + count] = (faceint_t)f;

                if( depth_avg < min_d )
                    min_d = depth_avg;
                if( depth_avg > max_d )
                    max_d = depth_avg;
            }
        }
    }

    if( min_d > max_d )
        return 0;
    return (min_d) | (max_d << 16);
}

static inline void
parition_faces_by_priority(
    faceint_t* face_priority_buckets,
    faceint_t* face_priority_bucket_counts,
    faceint_t* face_depth_buckets,
    faceint_t* face_depth_bucket_counts,
    int num_faces,
    const uint8_t* face_priorities,
    int depth_lower_bound,
    int depth_upper_bound)
{
    (void)num_faces;
    if( depth_upper_bound >= 1500 )
        depth_upper_bound = 1499;

    for( int depth = depth_upper_bound; depth >= depth_lower_bound; depth-- )
    {
        int face_count = (int)face_depth_bucket_counts[depth];
        if( face_count == 0 )
            continue;

        faceint_t* faces = &face_depth_buckets[depth << 9];
        for( int i = 0; i < face_count; i++ )
        {
            faceint_t face_idx = faces[i];
            int prio = faceprio_unpack(face_priorities, face_idx);
            int priority_face_count = face_priority_bucket_counts[prio]++;
            face_priority_buckets[prio * 2000 + priority_face_count] = face_idx;
        }
    }
}

static inline int
sort_face_draw_order(
    faceint_t* priority_depths,
    int* flex_prio11_face_to_depth,
    int* flex_prio12_face_to_depth,
    int* face_draw_order,
    faceint_t* face_depth_buckets,
    faceint_t* face_depth_bucket_counts,
    faceint_t* face_priority_buckets,
    faceint_t* face_priority_bucket_counts,
    int num_faces,
    const uint8_t* face_priorities,
    int depth_lower_bound,
    int depth_upper_bound)
{
    (void)num_faces;
    int counts[12] = { 0 };
    for( int depth = depth_upper_bound; depth >= depth_lower_bound && depth < 1500; depth-- )
    {
        int n = (int)face_depth_bucket_counts[depth];
        if( n == 0 )
            continue;

        faceint_t* faces = &face_depth_buckets[depth << 9];
        for( int i = 0; i < n; i++ )
        {
            faceint_t face_idx = faces[i];
            int prio = faceprio_unpack(face_priorities, face_idx);

            int face_count = counts[prio];

            if( prio < 10 )
            {
                priority_depths[prio] += depth;
            }
            else if( prio == 10 )
            {
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

    for( int i = 0; i < counts[11]; i++ )
    {
        flex_prio11_face_to_depth[counts[10] + i] = flex_prio12_face_to_depth[i];
    }
    counts[10] += counts[11];

    int flexible_face_index = 0;
    int order_index = 0;

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

    while( flexible_face_index < counts[10] )
    {
        face_draw_order[order_index++] = flex_prio11_face_to_depth[flexible_face_index] >> 16;
        flexible_face_index++;
    }

    return order_index;
}

static inline void
toridraw_compute_projected_face_order(
    struct ToriDraw_Context* context,
    struct ToriDraw_ModelHandle hnd)
{
    faceint_t* fia = NULL;
    faceint_t* fib = NULL;
    faceint_t* fic = NULL;
    uint8_t* face_priorities = NULL;
    int face_count = 0;

    switch( hnd.kind )
    {
    case TORIDRAWMK_MODEL:
    {
        struct ToriDraw_Model* m = model_as_full(hnd);
        fia = m->face_indices_a;
        fib = m->face_indices_b;
        fic = m->face_indices_c;
        face_priorities = m->face_priorities;
        face_count = m->face_count;
        break;
    }
    default:
        assert(0);
        break;
    }

    const struct ToriDraw_BoundsCylinder* bc = model_bounds_cylinder(hnd);
    int model_min_depth = bc ? bc->min_z_depth_any_rotation : 0;

    memset(
        context->tmp_depth_face_count,
        0,
        (size_t)(model_min_depth * 2 + 1) * sizeof(context->tmp_depth_face_count[0]));

    int bounds = bucket_sort_by_average_depth(
        context->tmp_depth_faces,
        context->tmp_depth_face_count,
        model_min_depth,
        face_count,
        context->screen_vertices_x,
        context->screen_vertices_y,
        context->screen_vertices_z,
        fia,
        fib,
        fic);

    model_min_depth = bounds & 0xFFFF;
    int model_max_depth = bounds >> 16;

    if( !face_priorities )
    {
        int order_index = 0;
        for( int depth = model_max_depth; depth < 1500 && depth >= model_min_depth; depth-- )
        {
            int bucket_count = (int)context->tmp_depth_face_count[depth];
            if( bucket_count == 0 )
                continue;

            faceint_t* faces = &context->tmp_depth_faces[depth << 9];
            for( int j = 0; j < bucket_count; j++ )
            {
                context->tmp_face_order[order_index++] = faces[j];
            }
        }
        context->tmp_face_order_count = order_index;
        return;
    }

    memset(context->tmp_priority_depth_sum, 0, sizeof(context->tmp_priority_depth_sum));
    memset(context->tmp_priority_face_count, 0, sizeof(context->tmp_priority_face_count));

    parition_faces_by_priority(
        context->tmp_priority_faces,
        context->tmp_priority_face_count,
        context->tmp_depth_faces,
        context->tmp_depth_face_count,
        face_count,
        face_priorities,
        model_min_depth,
        model_max_depth);

    context->tmp_face_order_count = sort_face_draw_order(
        context->tmp_priority_depth_sum,
        context->tmp_flex_prio11_face_to_depth,
        context->tmp_flex_prio12_face_to_depth,
        context->tmp_face_order,
        context->tmp_depth_faces,
        context->tmp_depth_face_count,
        context->tmp_priority_faces,
        context->tmp_priority_face_count,
        face_count,
        face_priorities,
        model_min_depth,
        model_max_depth);
}

static inline int
toridraw_project(
    struct ToriDraw_Context* context,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera)
{
    struct ProjectedVertex center_projection;

    int cull = TORIDRAW_CULL_VISIBLE;

    cull = toridraw_fast_cull(
        &context->cylinder_fast_aabb, view_port, hnd, position, camera, &center_projection);
    if( cull != TORIDRAW_CULL_VISIBLE )
        return cull;

    context->projected_vertex = center_projection;

    toridraw_calculate_cylinder_aabb_8point(&context->aabb, hnd, position, view_port, camera);

    cull = toridraw_aabb_cull(&context->aabb, view_port, camera);
    if( cull != TORIDRAW_CULL_VISIBLE )
        return cull;

    if( model_has_textures(hnd) )
    {
        project_vertices_array_fused(
            context->orthographic_vertices_x,
            context->orthographic_vertices_y,
            context->orthographic_vertices_z,
            context->screen_vertices_x,
            context->screen_vertices_y,
            context->screen_vertices_z,
            model_vertices_x(hnd),
            model_vertices_y(hnd),
            model_vertices_z(hnd),
            model_vertex_count(hnd),
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
        project_vertices_array_fused_notex(
            context->screen_vertices_x,
            context->screen_vertices_y,
            context->screen_vertices_z,
            model_vertices_x(hnd),
            model_vertices_y(hnd),
            model_vertices_z(hnd),
            model_vertex_count(hnd),
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

    return TORIDRAW_CULL_VISIBLE;
}
