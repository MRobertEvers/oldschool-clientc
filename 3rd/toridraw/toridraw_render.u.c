#include "graphics/dash_restrict.h"
#include "graphics/projection.h"
#include "toridraw_math.h"
#include "toridraw_model_internal.h"
#include "toridraw_types.h"

#include <assert.h>
#include <limits.h>
#include <string.h>
#ifdef TORIDRAW_NEAR_CLIP_STATS
#include <stdio.h>
#include <stdlib.h>
#endif

#ifndef VERTEXINT_BITS
#define VERTEXINT_BITS 16
#endif

// clang-format off
#include "graphics/projection16_simd.u.c"
#include "graphics/projection_zdiv_simd.u.c"
// clang-format on

/** Far plane for bounding-cylinder frustum cull. */
// #define TORIDRAW_CYLINDER_FAR_PLANE_Z 3500
#define TORIDRAW_CYLINDER_FAR_PLANE_Z 7500

static inline int
div3_fast_fixedpoint(int z_sum)
{
    return (z_sum * 21845) >> 16;
}

static inline int
ToriDraw_AabbCull(
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
ToriDraw_FastCull(
    struct ToriDraw_AABB* aabb,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_Camera* camera,
    struct ProjectedVertex* projected_vertex)
{
    assert(hnd.kind != TORIDRAWMK_NONE);
    int model_yaw = ToriDraw_NormalizeAngle(position->yaw);
    int scene_x = position->x;
    int scene_y = position->y;
    int scene_z = position->z;

    int camera_pitch = ToriDraw_NormalizeAngle(camera->pitch);
    int camera_yaw = ToriDraw_NormalizeAngle(camera->yaw);
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

    /* worldRender culls midZ>=3500; objRender / inventory icons do not.
     * Icon cameras already use near_plane_z=1 (world uses 50). */
    if( near_plane_z >= 50 && mid_z > TORIDRAW_CYLINDER_FAR_PLANE_Z )
        return TORIDRAW_CULL_FAST;

    int mid_x = projected_vertex->x;
    int mid_y = projected_vertex->y;

    bool const parallel = toridraw_proj_is_parallel(camera->proj_mode);
    int const zoom16 = camera->parallel_zoom16 ? camera->parallel_zoom16 : TORIDRAW_ORTHO_ZOOM_UNIT;

    /* Depth only reaches the perspective extents through the divide; a parallel
     * projection's screen size is the same at any depth, so the clamp below --
     * which exists purely to keep that divisor off the near plane -- would be
     * meaningless there. */
    if( !parallel && mid_z < near_plane_z )
        mid_z = near_plane_z;

    int ortho_screen_x_min = mid_x - model_edge_radius;
    int ortho_screen_x_max = mid_x + model_edge_radius;

    int screen_x_min_unoffset;
    int screen_x_max_unoffset;
    if( parallel )
    {
        screen_x_min_unoffset = (ortho_screen_x_min * zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        screen_x_max_unoffset = (ortho_screen_x_max * zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
    }
    else
    {
        screen_x_min_unoffset = project_divide(
            ortho_screen_x_min,
            mid_z,
            toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048));
        screen_x_max_unoffset = project_divide(
            ortho_screen_x_max,
            mid_z,
            toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048));
    }
    int screen_edge_width = view_port->width >> 1;

    if( screen_x_min_unoffset > screen_edge_width || screen_x_max_unoffset < -screen_edge_width )
        return TORIDRAW_CULL_FAST;

    int model_center_to_top_edge = bc->center_to_top_edge;

    int model_center_to_bottom_edge =
        (bc->center_to_bottom_edge * g_cos_table[camera_pitch] >> 16) +
        (model_edge_radius * g_sin_table[camera_pitch] >> 16);

    int screen_y_min_unoffset;
    int screen_y_max_unoffset;
    if( parallel )
    {
        screen_y_min_unoffset =
            ((mid_y - abs(model_center_to_bottom_edge)) * zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
        screen_y_max_unoffset =
            ((mid_y + abs(model_center_to_top_edge)) * zoom16) >> TORIDRAW_ORTHO_ZOOM_SHIFT;
    }
    else
    {
        screen_y_min_unoffset = project_divide(
            mid_y - abs(model_center_to_bottom_edge),
            mid_z,
            toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048));
        screen_y_max_unoffset = project_divide(
            mid_y + abs(model_center_to_top_edge),
            mid_z,
            toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048));
    }
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
ToriDraw_CalculateCylinderAabb8point(
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

    int const model_pitch = ToriDraw_NormalizeAngle(position->pitch);
    int const model_yaw = ToriDraw_NormalizeAngle(position->yaw);
    int const model_roll = ToriDraw_NormalizeAngle(position->roll);
    int const camera_roll = ToriDraw_NormalizeAngle(camera->roll);

    /*
     * The clipping family, unlike ToriDraw_Project: the min/max sweep below
     * reads sc_x directly, so a corner behind the near plane has to come back
     * as the sentinel to drag min_screen_x out to -5000 and keep the box
     * conservative. Eight vertices, so the per-vertex cost is noise.
     */
    if( toridraw_proj_is_parallel(camera->proj_mode) )
    {
        /* No sentinel needed: with no divide a corner behind the near plane
         * still projects to a real coordinate, so the min/max sweep below can
         * use it directly and the box is tighter than the perspective one. */
        int const zoom16 =
            camera->parallel_zoom16 ? camera->parallel_zoom16 : TORIDRAW_ORTHO_ZOOM_UNIT;
        if( model_pitch != 0 || model_roll != 0 || camera_roll != 0 )
        {
            project_vertices_array_ortho6_fused_notex_noclip(
                sc_x, sc_y, sc_z, bb_x, bb_y, bb_z, 8,
                model_pitch, model_yaw, model_roll, 0,
                position->x, position->y, position->z,
                zoom16, camera->pitch, camera->yaw, camera_roll);
        }
        else
        {
            project_vertices_array_ortho_fused_notex_noclip(
                sc_x, sc_y, sc_z, bb_x, bb_y, bb_z, 8,
                model_yaw, 0,
                position->x, position->y, position->z,
                zoom16, camera->pitch, camera->yaw);
        }
    }
    else if( model_pitch != 0 || model_roll != 0 || camera_roll != 0 )
    {
        project_vertices_array6_fused_notex_clip(
            sc_x,
            sc_y,
            sc_z,
            bb_x,
            bb_y,
            bb_z,
            8,
            model_pitch,
            model_yaw,
            model_roll,
            0,
            position->x,
            position->y,
            position->z,
            camera->near_plane_z,
            toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048),
            camera->pitch,
            camera->yaw,
            camera_roll);
    }
    else
    {
        project_vertices_array_fused_notex_clip(
            sc_x,
            sc_y,
            sc_z,
            bb_x,
            bb_y,
            bb_z,
            8,
            model_yaw,
            0,
            position->x,
            position->y,
            position->z,
            camera->near_plane_z,
            toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048),
            camera->pitch,
            camera->yaw);
    }

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
                /* The << 9 fixes bucket capacity at 512 faces per depth level.
                 * Nothing bounded `count` before, so a model with more than
                 * 512 front-facing triangles at one quantized depth (a large
                 * flat wall seen edge-on, a terrain patch) wrote into the next
                 * depth's bucket and silently corrupted the draw order. Drop
                 * the overflow instead. */
                if( count < 512 )
                {
                    face_depth_bucket_counts[depth_avg] = count + 1;
                    face_depth_buckets[(depth_avg << 9) + count] = (faceint_t)f;

                    if( depth_avg < min_d )
                        min_d = depth_avg;
                    if( depth_avg > max_d )
                        max_d = depth_avg;
                }
            }
        }
    }

    if( min_d > max_d )
        return 0;
    return (min_d) | (max_d << 16);
}

/**
 * One traversal of the depth buckets that does everything the old
 * parition_faces_by_priority() + the accumulation half of
 * sort_face_draw_order() did between them: fills the per-priority face
 * buckets, sums the per-priority depths, and lays out the two flexible-priority
 * arrays. Both old loops visited the same faces in the same order and unpacked
 * the same priority nibble, so folding them is order-preserving; the running
 * index they each used (face_priority_bucket_counts[prio] and counts[prio])
 * was always the same number.
 */
static inline void
partition_and_accumulate_faces_by_priority(
    faceint_t* face_priority_buckets,
    faceint_t* face_priority_bucket_counts,
    faceint_t* priority_depths,
    int* flex_prio11_face_to_depth,
    int* flex_prio12_face_to_depth,
    int* counts,
    faceint_t* face_depth_buckets,
    faceint_t* face_depth_bucket_counts,
    const uint8_t* face_priorities,
    int depth_lower_bound,
    int depth_upper_bound)
{
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
            int n = counts[prio];

            face_priority_buckets[prio * 2000 + n] = face_idx;

            if( prio < 10 )
            {
                priority_depths[prio] += (faceint_t)depth;
            }
            else if( prio == 10 )
            {
                flex_prio11_face_to_depth[n] = depth | (face_idx << 16);
            }
            else
            {
                flex_prio12_face_to_depth[n] = depth | (face_idx << 16);
            }

            counts[prio] = n + 1;
            face_priority_bucket_counts[prio] = (faceint_t)(n + 1);
        }
    }
}

/**
 * Emission half of the old sort_face_draw_order(): interleaves the flexible
 * priorities with the fixed priority runs. Takes the counts and depth sums that
 * partition_and_accumulate_faces_by_priority() already produced.
 */
static inline int
sort_face_draw_order(
    faceint_t* priority_depths,
    int* flex_prio11_face_to_depth,
    int* flex_prio12_face_to_depth,
    int* face_draw_order,
    faceint_t* face_priority_buckets,
    int* counts)
{
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
ToriDraw_ComputeProjectedFaceOrder(
    struct ToriDraw_Scene* scene,
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
        scene->tmp_depth_face_count,
        0,
        (size_t)scene->depth_levels * sizeof(scene->tmp_depth_face_count[0]));

    int bounds = bucket_sort_by_average_depth(
        scene->tmp_depth_faces,
        scene->tmp_depth_face_count,
        model_min_depth,
        face_count,
        scene->screen_vertices_x,
        scene->screen_vertices_y,
        scene->screen_vertices_z,
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
            int bucket_count = (int)scene->tmp_depth_face_count[depth];
            if( bucket_count == 0 )
                continue;

            faceint_t* faces = &scene->tmp_depth_faces[depth << 9];
            for( int j = 0; j < bucket_count; j++ )
            {
                scene->tmp_face_order[order_index++] = faces[j];
            }
        }
        scene->tmp_face_order_count = order_index;
        return;
    }

    memset(scene->tmp_priority_depth_sum, 0, 12 * sizeof(faceint_t));
    memset(scene->tmp_priority_face_count, 0, 12 * sizeof(faceint_t));

    int counts[12] = { 0 };

    partition_and_accumulate_faces_by_priority(
        scene->tmp_priority_faces,
        scene->tmp_priority_face_count,
        scene->tmp_priority_depth_sum,
        scene->tmp_flex_prio11_face_to_depth,
        scene->tmp_flex_prio12_face_to_depth,
        counts,
        scene->tmp_depth_faces,
        scene->tmp_depth_face_count,
        face_priorities,
        model_min_depth,
        model_max_depth);

    scene->tmp_face_order_count = sort_face_draw_order(
        scene->tmp_priority_depth_sum,
        scene->tmp_flex_prio11_face_to_depth,
        scene->tmp_flex_prio12_face_to_depth,
        scene->tmp_face_order,
        scene->tmp_priority_faces,
        counts);
}

static inline int
bucket_sort_by_average_depth_small(
    struct ToriDraw_Scene* scene,
    int model_min_depth,
    int num_faces,
    const int* RESTRICT vx,
    const int* RESTRICT vy,
    const int* RESTRICT vz,
    const faceint_t* RESTRICT face_a,
    const faceint_t* RESTRICT face_b,
    const faceint_t* RESTRICT face_c)
{
    const int depth_levels = scene->depth_levels;
    int min_d = depth_levels;
    int max_d = 0;

    memset(scene->sm_depth_offset, 0, (size_t)depth_levels * sizeof(int));

    for( int f = 0; f < num_faces; f++ )
    {
        scene->sm_face_depth[f] = -1;

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

            if( (unsigned int)depth_avg < (unsigned int)depth_levels )
            {
                scene->sm_face_depth[f] = (faceint_t)depth_avg;
                scene->sm_depth_offset[depth_avg]++;

                if( depth_avg < min_d )
                    min_d = depth_avg;
                if( depth_avg > max_d )
                    max_d = depth_avg;
            }
        }
    }

    if( min_d > max_d )
        return 0;

    int total = 0;
    for( int d = 0; d < depth_levels; d++ )
    {
        int count = scene->sm_depth_offset[d];
        scene->sm_depth_offset[d] = total;
        total += count;
    }
    scene->sm_depth_offset[depth_levels] = total;

    memcpy(scene->sm_depth_cursor, scene->sm_depth_offset, (size_t)depth_levels * sizeof(int));

    for( int f = 0; f < num_faces; f++ )
    {
        int depth_avg = scene->sm_face_depth[f];
        if( depth_avg < 0 )
            continue;

        int write = scene->sm_depth_cursor[depth_avg]++;
        scene->sm_faces_by_depth[write] = (faceint_t)f;
    }

    return (min_d) | (max_d << 16);
}

/**
 * Small-scene twin of partition_and_accumulate_faces_by_priority(): the same
 * fold of the old parition_faces_by_priority_small() and the accumulation half
 * of sort_face_draw_order_small() into one traversal.
 */
static inline void
partition_and_accumulate_faces_by_priority_small(
    struct ToriDraw_Scene* scene,
    faceint_t* priority_depths,
    int* counts,
    const uint8_t* face_priorities,
    int depth_lower_bound,
    int depth_upper_bound)
{
    const int depth_levels = scene->depth_levels;
    const int max_faces = scene->max_faces;

    if( depth_upper_bound >= depth_levels )
        depth_upper_bound = depth_levels - 1;

    memset(scene->sm_prio_count, 0, sizeof(scene->sm_prio_count));

    for( int depth = depth_upper_bound; depth >= depth_lower_bound; depth-- )
    {
        int start = scene->sm_depth_offset[depth];
        int end = scene->sm_depth_offset[depth + 1];
        for( int i = start; i < end; i++ )
        {
            faceint_t face_idx = scene->sm_faces_by_depth[i];
            int prio = faceprio_unpack(face_priorities, face_idx);
            int n = counts[prio];

            scene->sm_prio_faces[prio * max_faces + n] = face_idx;

            if( prio < 10 )
            {
                priority_depths[prio] += (faceint_t)depth;
            }
            else if( prio == 10 )
            {
                scene->sm_flex_prio11_face_to_depth[n] = depth | (face_idx << 16);
            }
            else
            {
                scene->sm_flex_prio12_face_to_depth[n] = depth | (face_idx << 16);
            }

            counts[prio] = n + 1;
            scene->sm_prio_count[prio] = n + 1;
        }
    }
}

static inline int
sort_face_draw_order_small(
    struct ToriDraw_Scene* scene,
    int* face_draw_order,
    faceint_t* priority_depths,
    int* counts)
{
    const int max_faces = scene->max_faces;

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
        scene->sm_flex_prio11_face_to_depth[counts[10] + i] =
            scene->sm_flex_prio12_face_to_depth[i];
    }
    counts[10] += counts[11];

    int flexible_face_index = 0;
    int order_index = 0;

    while( flexible_face_index < counts[10] &&
           (scene->sm_flex_prio11_face_to_depth[flexible_face_index] & 0xFFFF) > average_depth1_2 )
    {
        face_draw_order[order_index++] =
            scene->sm_flex_prio11_face_to_depth[flexible_face_index] >> 16;
        flexible_face_index++;
    }

    for( int prio = 0; prio < 3; prio++ )
    {
        for( int i = 0; i < counts[prio]; i++ )
        {
            face_draw_order[order_index++] = scene->sm_prio_faces[prio * max_faces + i];
        }
    }

    while( flexible_face_index < counts[10] &&
           (scene->sm_flex_prio11_face_to_depth[flexible_face_index] & 0xFFFF) > average_depth3_4 )
    {
        face_draw_order[order_index++] =
            scene->sm_flex_prio11_face_to_depth[flexible_face_index] >> 16;
        flexible_face_index++;
    }

    for( int prio = 3; prio < 5; prio++ )
    {
        for( int i = 0; i < counts[prio]; i++ )
        {
            face_draw_order[order_index++] = scene->sm_prio_faces[prio * max_faces + i];
        }
    }

    while( flexible_face_index < counts[10] &&
           (scene->sm_flex_prio11_face_to_depth[flexible_face_index] & 0xFFFF) > average_depth6_8 )
    {
        face_draw_order[order_index++] =
            scene->sm_flex_prio11_face_to_depth[flexible_face_index] >> 16;
        flexible_face_index++;
    }

    for( int prio = 5; prio < 10; prio++ )
    {
        for( int i = 0; i < counts[prio]; i++ )
        {
            face_draw_order[order_index++] = scene->sm_prio_faces[prio * max_faces + i];
        }
    }

    while( flexible_face_index < counts[10] )
    {
        face_draw_order[order_index++] =
            scene->sm_flex_prio11_face_to_depth[flexible_face_index] >> 16;
        flexible_face_index++;
    }

    return order_index;
}

static inline void
ToriDraw_ComputeProjectedFaceOrderSmall(
    struct ToriDraw_Scene* scene,
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

    int bounds = bucket_sort_by_average_depth_small(
        scene,
        model_min_depth,
        face_count,
        scene->screen_vertices_x,
        scene->screen_vertices_y,
        scene->screen_vertices_z,
        fia,
        fib,
        fic);

    model_min_depth = bounds & 0xFFFF;
    int model_max_depth = bounds >> 16;

    if( bounds == 0 )
    {
        scene->tmp_face_order_count = 0;
        return;
    }

    if( !face_priorities )
    {
        int order_index = 0;
        for( int depth = model_max_depth; depth < scene->depth_levels && depth >= model_min_depth;
             depth-- )
        {
            int start = scene->sm_depth_offset[depth];
            int end = scene->sm_depth_offset[depth + 1];
            for( int j = start; j < end; j++ )
                scene->tmp_face_order[order_index++] = scene->sm_faces_by_depth[j];
        }
        scene->tmp_face_order_count = order_index;
        return;
    }

    faceint_t priority_depths[12] = { 0 };
    int counts[12] = { 0 };

    partition_and_accumulate_faces_by_priority_small(
        scene, priority_depths, counts, face_priorities, model_min_depth, model_max_depth);

    scene->tmp_face_order_count =
        sort_face_draw_order_small(scene, scene->tmp_face_order, priority_depths, counts);
}

/*
 * The model-shape dispatch, written out once per near-clip family.
 *
 * The near-clip question is answered once per model in ToriDraw_Project and
 * the answer picks one of these two; below this point every kernel is
 * specialized, so no `may_clip` argument is threaded down and nothing tests it
 * per vertex. Passing a flag down instead and trusting the optimizer to
 * unswitch it was measurably worse: it held at -O2/-O3 but left the scalar
 * path ~20%% slower than the code it replaced at -O1, where the flag stopped
 * being a constant and blocked auto-vectorization.
 */
/* Models that can reach behind the near plane. */
static inline void
toridraw_project_vertices_clip(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_Camera* camera,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int camera_roll,
    int model_mid_z)
{
    /* Full 6DOF when model/camera roll is set (obj-icon zan2d, etc.). yaw-only and
     * pitch+yaw keep the SIMD fused paths; array6_fused matches v0 Dash. */
    if( model_roll != 0 || camera_roll != 0 )
    {
        if( model_has_textures(hnd) )
        {
            project_vertices_array6_fused_clip(
                scene->orthographic_vertices_x,
                scene->orthographic_vertices_y,
                scene->orthographic_vertices_z,
                scene->screen_vertices_x,
                scene->screen_vertices_y,
                scene->screen_vertices_z,
                model_vertices_x(hnd),
                model_vertices_y(hnd),
                model_vertices_z(hnd),
                model_vertex_count(hnd),
                model_pitch,
                model_yaw,
                model_roll,
                model_mid_z,
                position->x,
                position->y,
                position->z,
                camera->near_plane_z,
                toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048),
                camera->pitch,
                camera->yaw,
                camera_roll);
        }
        else
        {
            project_vertices_array6_fused_notex_clip(
                scene->screen_vertices_x,
                scene->screen_vertices_y,
                scene->screen_vertices_z,
                model_vertices_x(hnd),
                model_vertices_y(hnd),
                model_vertices_z(hnd),
                model_vertex_count(hnd),
                model_pitch,
                model_yaw,
                model_roll,
                model_mid_z,
                position->x,
                position->y,
                position->z,
                camera->near_plane_z,
                toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048),
                camera->pitch,
                camera->yaw,
                camera_roll);
        }
    }
    else if( model_pitch != 0 )
    {
        if( model_has_textures(hnd) )
        {
            project_vertices_array_pitchyaw_fused_clip(
                scene->orthographic_vertices_x,
                scene->orthographic_vertices_y,
                scene->orthographic_vertices_z,
                scene->screen_vertices_x,
                scene->screen_vertices_y,
                scene->screen_vertices_z,
                model_vertices_x(hnd),
                model_vertices_y(hnd),
                model_vertices_z(hnd),
                model_vertex_count(hnd),
                model_pitch,
                model_yaw,
                model_mid_z,
                position->x,
                position->y,
                position->z,
                camera->near_plane_z,
                toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048),
                camera->pitch,
                camera->yaw);
        }
        else
        {
            project_vertices_array_pitchyaw_fused_notex_clip(
                scene->screen_vertices_x,
                scene->screen_vertices_y,
                scene->screen_vertices_z,
                model_vertices_x(hnd),
                model_vertices_y(hnd),
                model_vertices_z(hnd),
                model_vertex_count(hnd),
                model_pitch,
                model_yaw,
                model_mid_z,
                position->x,
                position->y,
                position->z,
                camera->near_plane_z,
                toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048),
                camera->pitch,
                camera->yaw);
        }
    }
    else if( model_has_textures(hnd) )
    {
        project_vertices_array_fused_clip(
            scene->orthographic_vertices_x,
            scene->orthographic_vertices_y,
            scene->orthographic_vertices_z,
            scene->screen_vertices_x,
            scene->screen_vertices_y,
            scene->screen_vertices_z,
            model_vertices_x(hnd),
            model_vertices_y(hnd),
            model_vertices_z(hnd),
            model_vertex_count(hnd),
            model_yaw,
            model_mid_z,
            position->x,
            position->y,
            position->z,
            camera->near_plane_z,
            toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048),
            camera->pitch,
            camera->yaw);
    }
    else
    {
        project_vertices_array_fused_notex_clip(
            scene->screen_vertices_x,
            scene->screen_vertices_y,
            scene->screen_vertices_z,
            model_vertices_x(hnd),
            model_vertices_y(hnd),
            model_vertices_z(hnd),
            model_vertex_count(hnd),
            model_yaw,
            model_mid_z,
            position->x,
            position->y,
            position->z,
            camera->near_plane_z,
            toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048),
            camera->pitch,
            camera->yaw);
    }

}

/* Models that provably cannot; no sentinel, no near-plane test. */
static inline void
toridraw_project_vertices_noclip(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_Camera* camera,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int camera_roll,
    int model_mid_z)
{
    /* Full 6DOF when model/camera roll is set (obj-icon zan2d, etc.). yaw-only and
     * pitch+yaw keep the SIMD fused paths; array6_fused matches v0 Dash. */
    if( model_roll != 0 || camera_roll != 0 )
    {
        if( model_has_textures(hnd) )
        {
            project_vertices_array6_fused_noclip(
                scene->orthographic_vertices_x,
                scene->orthographic_vertices_y,
                scene->orthographic_vertices_z,
                scene->screen_vertices_x,
                scene->screen_vertices_y,
                scene->screen_vertices_z,
                model_vertices_x(hnd),
                model_vertices_y(hnd),
                model_vertices_z(hnd),
                model_vertex_count(hnd),
                model_pitch,
                model_yaw,
                model_roll,
                model_mid_z,
                position->x,
                position->y,
                position->z,
                camera->near_plane_z,
                toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048),
                camera->pitch,
                camera->yaw,
                camera_roll);
        }
        else
        {
            project_vertices_array6_fused_notex_noclip(
                scene->screen_vertices_x,
                scene->screen_vertices_y,
                scene->screen_vertices_z,
                model_vertices_x(hnd),
                model_vertices_y(hnd),
                model_vertices_z(hnd),
                model_vertex_count(hnd),
                model_pitch,
                model_yaw,
                model_roll,
                model_mid_z,
                position->x,
                position->y,
                position->z,
                camera->near_plane_z,
                toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048),
                camera->pitch,
                camera->yaw,
                camera_roll);
        }
    }
    else if( model_pitch != 0 )
    {
        if( model_has_textures(hnd) )
        {
            project_vertices_array_pitchyaw_fused_noclip(
                scene->orthographic_vertices_x,
                scene->orthographic_vertices_y,
                scene->orthographic_vertices_z,
                scene->screen_vertices_x,
                scene->screen_vertices_y,
                scene->screen_vertices_z,
                model_vertices_x(hnd),
                model_vertices_y(hnd),
                model_vertices_z(hnd),
                model_vertex_count(hnd),
                model_pitch,
                model_yaw,
                model_mid_z,
                position->x,
                position->y,
                position->z,
                camera->near_plane_z,
                toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048),
                camera->pitch,
                camera->yaw);
        }
        else
        {
            project_vertices_array_pitchyaw_fused_notex_noclip(
                scene->screen_vertices_x,
                scene->screen_vertices_y,
                scene->screen_vertices_z,
                model_vertices_x(hnd),
                model_vertices_y(hnd),
                model_vertices_z(hnd),
                model_vertex_count(hnd),
                model_pitch,
                model_yaw,
                model_mid_z,
                position->x,
                position->y,
                position->z,
                camera->near_plane_z,
                toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048),
                camera->pitch,
                camera->yaw);
        }
    }
    else if( model_has_textures(hnd) )
    {
        project_vertices_array_fused_noclip(
            scene->orthographic_vertices_x,
            scene->orthographic_vertices_y,
            scene->orthographic_vertices_z,
            scene->screen_vertices_x,
            scene->screen_vertices_y,
            scene->screen_vertices_z,
            model_vertices_x(hnd),
            model_vertices_y(hnd),
            model_vertices_z(hnd),
            model_vertex_count(hnd),
            model_yaw,
            model_mid_z,
            position->x,
            position->y,
            position->z,
            camera->near_plane_z,
            toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048),
            camera->pitch,
            camera->yaw);
    }
    else
    {
        project_vertices_array_fused_notex_noclip(
            scene->screen_vertices_x,
            scene->screen_vertices_y,
            scene->screen_vertices_z,
            model_vertices_x(hnd),
            model_vertices_y(hnd),
            model_vertices_z(hnd),
            model_vertex_count(hnd),
            model_yaw,
            model_mid_z,
            position->x,
            position->y,
            position->z,
            camera->near_plane_z,
            toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048),
            camera->pitch,
            camera->yaw);
    }

}

/*
 * Parallel-projection dispatch, one per near-clip family, mirroring the
 * perspective pair above. Same split for the same reason: the choice is made
 * once per model in ToriDraw_Project and everything below is specialized.
 */
static inline void
toridraw_project_vertices_parallel_clip(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_Camera* camera,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int camera_roll,
    int model_mid_z)
{
    int const zoom16 = camera->parallel_zoom16 ? camera->parallel_zoom16 : TORIDRAW_ORTHO_ZOOM_UNIT;

    if( model_pitch != 0 || model_roll != 0 || camera_roll != 0 )
    {
        if( model_has_textures(hnd) )
            project_vertices_array_ortho6_fused_clip(
                scene->orthographic_vertices_x, scene->orthographic_vertices_y,
                scene->orthographic_vertices_z, scene->screen_vertices_x,
                scene->screen_vertices_y, scene->screen_vertices_z,
                model_vertices_x(hnd), model_vertices_y(hnd), model_vertices_z(hnd),
                model_vertex_count(hnd), model_pitch, model_yaw, model_roll, model_mid_z,
                position->x, position->y, position->z, camera->near_plane_z,
                zoom16, camera->pitch, camera->yaw, camera_roll);
        else
            project_vertices_array_ortho6_fused_notex_clip(
                scene->screen_vertices_x, scene->screen_vertices_y, scene->screen_vertices_z,
                model_vertices_x(hnd), model_vertices_y(hnd), model_vertices_z(hnd),
                model_vertex_count(hnd), model_pitch, model_yaw, model_roll, model_mid_z,
                position->x, position->y, position->z, camera->near_plane_z,
                zoom16, camera->pitch, camera->yaw, camera_roll);
    }
    else if( model_has_textures(hnd) )
    {
        project_vertices_array_ortho_fused_clip(
            scene->orthographic_vertices_x, scene->orthographic_vertices_y,
            scene->orthographic_vertices_z, scene->screen_vertices_x,
            scene->screen_vertices_y, scene->screen_vertices_z,
            model_vertices_x(hnd), model_vertices_y(hnd), model_vertices_z(hnd),
            model_vertex_count(hnd), model_yaw, model_mid_z,
            position->x, position->y, position->z, camera->near_plane_z,
            zoom16, camera->pitch, camera->yaw);
    }
    else
    {
        project_vertices_array_ortho_fused_notex_clip(
            scene->screen_vertices_x, scene->screen_vertices_y, scene->screen_vertices_z,
            model_vertices_x(hnd), model_vertices_y(hnd), model_vertices_z(hnd),
            model_vertex_count(hnd), model_yaw, model_mid_z,
            position->x, position->y, position->z, camera->near_plane_z,
            zoom16, camera->pitch, camera->yaw);
    }
}

static inline void
toridraw_project_vertices_parallel_noclip(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_Camera* camera,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int camera_roll,
    int model_mid_z)
{
    int const zoom16 = camera->parallel_zoom16 ? camera->parallel_zoom16 : TORIDRAW_ORTHO_ZOOM_UNIT;

    if( model_pitch != 0 || model_roll != 0 || camera_roll != 0 )
    {
        if( model_has_textures(hnd) )
            project_vertices_array_ortho6_fused_noclip(
                scene->orthographic_vertices_x, scene->orthographic_vertices_y,
                scene->orthographic_vertices_z, scene->screen_vertices_x,
                scene->screen_vertices_y, scene->screen_vertices_z,
                model_vertices_x(hnd), model_vertices_y(hnd), model_vertices_z(hnd),
                model_vertex_count(hnd), model_pitch, model_yaw, model_roll, model_mid_z,
                position->x, position->y, position->z,
                zoom16, camera->pitch, camera->yaw, camera_roll);
        else
            project_vertices_array_ortho6_fused_notex_noclip(
                scene->screen_vertices_x, scene->screen_vertices_y, scene->screen_vertices_z,
                model_vertices_x(hnd), model_vertices_y(hnd), model_vertices_z(hnd),
                model_vertex_count(hnd), model_pitch, model_yaw, model_roll, model_mid_z,
                position->x, position->y, position->z,
                zoom16, camera->pitch, camera->yaw, camera_roll);
    }
    else if( model_has_textures(hnd) )
    {
        project_vertices_array_ortho_fused_noclip(
            scene->orthographic_vertices_x, scene->orthographic_vertices_y,
            scene->orthographic_vertices_z, scene->screen_vertices_x,
            scene->screen_vertices_y, scene->screen_vertices_z,
            model_vertices_x(hnd), model_vertices_y(hnd), model_vertices_z(hnd),
            model_vertex_count(hnd), model_yaw, model_mid_z,
            position->x, position->y, position->z,
            zoom16, camera->pitch, camera->yaw);
    }
    else
    {
        project_vertices_array_ortho_fused_notex_noclip(
            scene->screen_vertices_x, scene->screen_vertices_y, scene->screen_vertices_z,
            model_vertices_x(hnd), model_vertices_y(hnd), model_vertices_z(hnd),
            model_vertex_count(hnd), model_yaw, model_mid_z,
            position->x, position->y, position->z,
            zoom16, camera->pitch, camera->yaw);
    }
}

static inline int
ToriDraw_Project(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera)
{
    struct ProjectedVertex center_projection;

    int cull = TORIDRAW_CULL_VISIBLE;

    cull = ToriDraw_FastCull(
        &scene->cylinder_fast_aabb, view_port, hnd, position, camera, &center_projection);
    if( cull != TORIDRAW_CULL_VISIBLE )
        return cull;

    scene->projected_vertex = center_projection;

    ToriDraw_CalculateCylinderAabb8point(&scene->aabb, hnd, position, view_port, camera);

    cull = ToriDraw_AabbCull(&scene->aabb, view_port, camera);
    if( cull != TORIDRAW_CULL_VISIBLE )
        return cull;

    int const model_pitch = ToriDraw_NormalizeAngle(position->pitch);
    int const model_yaw = ToriDraw_NormalizeAngle(position->yaw);
    int const model_roll = ToriDraw_NormalizeAngle(position->roll);
    int const camera_roll = ToriDraw_NormalizeAngle(camera->roll);

    /*
     * Decide once, for the whole model, whether any vertex can land behind the
     * near plane. The reference asks the same question at the same point
     * (Client-TS Model.worldRender:1755 `clipped = midZ - radiusZ <= 50`) and
     * uses the answer to skip every per-face sentinel test in render2:1876 —
     * which is why it never has to defend a genuine -5000. We do the same, and
     * additionally hand the answer to the projection kernels so the per-vertex
     * clip compare, the sentinel blend and the -5001 nudge all disappear from
     * the common path rather than merely being ignored downstream.
     *
     * `min_z_depth_any_rotation` is max(center_to_top_edge, center_to_bottom_edge),
     * i.e. the radius of a sphere about the model origin containing every
     * vertex (toridraw_model_transform.c:736). A sphere is the right bound
     * here precisely because it is rotation-invariant: model pitch/yaw/roll and
     * camera pitch/yaw/roll all rotate about that origin, so this one test
     * covers the 6DOF and pitch+yaw paths as well as the yaw-only one. The
     * reference's `radiusZ` cannot: it folds in cos/sin of the camera pitch and
     * so is only valid for worldRender's yaw-only models.
     *
     * Must be conservative in the "may clip" direction: the no-clip kernel
     * divides by z unconditionally, so a vertex that sneaks below the near
     * plane would be a sign-flipped projection, or a SIGFPE at z == 0. Hence
     * `<` against near_plane_z (not `<=`), and the near_plane_z < 1 guard for
     * cameras that would otherwise admit a zero divisor.
     */
    struct ToriDraw_BoundsCylinder const* const proj_bc = model_bounds_cylinder(hnd);
    /*
     * The near_plane_z < 1 guard is a PERSPECTIVE-only safety rule: there it
     * would admit a zero or negative divisor, so the clipping family has to
     * take over. Parallel projection never divides, so a near plane behind the
     * camera is a perfectly ordinary request -- it is how a map editor says
     * "never hide anything" -- and forcing the clipping family on it would cost
     * every model the more expensive kernel for no reason.
     */
    bool may_clip =
        !proj_bc ||
        (!toridraw_proj_is_parallel(camera->proj_mode) && camera->near_plane_z < 1) ||
        center_projection.z - proj_bc->min_z_depth_any_rotation < camera->near_plane_z;

#ifdef TORIDRAW_NEAR_CLIP_FORCE_ALL
    /* Build with -DTORIDRAW_NEAR_CLIP_FORCE_ALL=1 to send every model down the
     * clipping kernel, i.e. the behaviour from before the gate existed. Frames
     * rendered by the two builds must be byte-identical: that equality is the
     * whole correctness argument for the gate, so keep this switch working. */
    may_clip = true;
#endif

#ifdef TORIDRAW_NEAR_CLIP_STATS
    {
        static long clipped_models = 0;
        static long total_models = 0;
        total_models++;
        if( may_clip )
            clipped_models++;
        if( (total_models % 100000) == 0 )
            fprintf(
                stderr,
                "near_clip_stats: %ld/%ld models took the clipping kernel (%.2f%%)\n",
                clipped_models,
                total_models,
                100.0 * (double)clipped_models / (double)total_models);
    }
#endif

    scene->near_clipped = may_clip;

    if( toridraw_proj_is_parallel(camera->proj_mode) )
    {
        /*
         * Same near-clip gate, different meaning. Parallel projection has no
         * singularity to avoid, so may_clip here is not a safety requirement --
         * it just says whether any vertex is near enough the view plane to need
         * hiding. A camera with a very negative near_plane_z therefore never
         * takes the clipping family at all, which is the map editor's normal
         * configuration.
         */
        if( may_clip )
            toridraw_project_vertices_parallel_clip(
                scene, hnd, position, camera,
                model_pitch, model_yaw, model_roll, camera_roll, center_projection.z);
        else
            toridraw_project_vertices_parallel_noclip(
                scene, hnd, position, camera,
                model_pitch, model_yaw, model_roll, camera_roll, center_projection.z);
    }
    else if( may_clip )
        toridraw_project_vertices_clip(
            scene, hnd, position, camera,
            model_pitch, model_yaw, model_roll, camera_roll, center_projection.z);
    else
        toridraw_project_vertices_noclip(
            scene, hnd, position, camera,
            model_pitch, model_yaw, model_roll, camera_roll, center_projection.z);

#ifdef TORIDRAW_NEAR_CLIP_STATS
    /*
     * Verification build. Two properties are checked here, on real scene data.
     *
     * Deliberately NOT done by comparing rendered frames between a gated and a
     * forced build: this client's offline boot is not frame-deterministic (the
     * same binary run twice produces different frames — async asset loads
     * settle differently), so a frame byte-compare answers "did these two runs
     * load the same things", not "do the two kernels agree". Everything below
     * runs both kernels over the same model inside one process instead.
     *
     *   1. Conservativeness. If any vertex really did land in front of the near
     *      plane, the gate must have said so. The reverse — gate says "may
     *      clip", nothing actually clips — is merely a missed optimization.
     *      A failure here means the no-clip kernel divided by a z it should
     *      not have, the one way this change can corrupt geometry.
     *   2. Equivalence. Whenever nothing actually clipped, both kernels must
     *      produce identical vertices. Checked regardless of which way the gate
     *      went, so the boundary case (gate says "may clip", reality says no)
     *      is covered too, not just the easy interior.
     *
     * The kernels write screen z as (camera-space z - model_mid_z), so the
     * camera-space z is recoverable exactly and neither check needs the
     * projection to hand anything extra back.
     */
    {
        int const vcount = model_vertex_count(hnd);
        bool actually_clipped = false;
        for( int vi = 0; vi < vcount; vi++ )
        {
            if( scene->screen_vertices_z[vi] + center_projection.z < camera->near_plane_z )
            {
                actually_clipped = true;
                break;
            }
        }

        if( actually_clipped && !may_clip )
        {
            fprintf(
                stderr,
                "near_clip_bound_violation: model clipped but the gate said it could not "
                "(mid_z=%d sphere_r=%d near=%d)\n",
                center_projection.z,
                proj_bc ? proj_bc->min_z_depth_any_rotation : -1,
                camera->near_plane_z);
            assert(0 && "near-clip gate was not conservative");
        }

        if( !actually_clipped )
        {
            static int* verify_x = NULL;
            static int* verify_y = NULL;
            static int* verify_z = NULL;
            static int verify_cap = 0;
            static long compared_models = 0;
            static long nudge_divergences = 0;

            if( vcount > verify_cap )
            {
                verify_cap = vcount;
                verify_x = (int*)realloc(verify_x, (size_t)verify_cap * sizeof(int));
                verify_y = (int*)realloc(verify_y, (size_t)verify_cap * sizeof(int));
                verify_z = (int*)realloc(verify_z, (size_t)verify_cap * sizeof(int));
                assert(verify_x && verify_y && verify_z);
            }
            memcpy(verify_x, scene->screen_vertices_x, (size_t)vcount * sizeof(int));
            memcpy(verify_y, scene->screen_vertices_y, (size_t)vcount * sizeof(int));
            memcpy(verify_z, scene->screen_vertices_z, (size_t)vcount * sizeof(int));

            /* Re-project down the opposite arm and compare. */
            bool const par = toridraw_proj_is_parallel(camera->proj_mode);
            if( may_clip && par )
                toridraw_project_vertices_parallel_noclip(
                    scene, hnd, position, camera,
                    model_pitch, model_yaw, model_roll, camera_roll, center_projection.z);
            else if( par )
                toridraw_project_vertices_parallel_clip(
                    scene, hnd, position, camera,
                    model_pitch, model_yaw, model_roll, camera_roll, center_projection.z);
            else if( may_clip )
                toridraw_project_vertices_noclip(
                    scene, hnd, position, camera,
                    model_pitch, model_yaw, model_roll, camera_roll, center_projection.z);
            else
                toridraw_project_vertices_clip(
                    scene, hnd, position, camera,
                    model_pitch, model_yaw, model_roll, camera_roll, center_projection.z);

            compared_models++;
            for( int vi = 0; vi < vcount; vi++ )
            {
                /* The one legitimate divergence: the clipping kernel nudges a
                 * genuinely projected -5000 to -5001 and the no-clip kernel
                 * does not. Counted rather than failed — it is the documented
                 * consequence of dropping the nudge, and the count says how
                 * often it is reachable at all. */
                int const lo = verify_x[vi] < scene->screen_vertices_x[vi]
                                   ? verify_x[vi]
                                   : scene->screen_vertices_x[vi];
                int const hi = verify_x[vi] < scene->screen_vertices_x[vi]
                                   ? scene->screen_vertices_x[vi]
                                   : verify_x[vi];
                if( lo == TORIDRAW_SCREEN_X_NEAR_CLIPPED_NUDGE &&
                    hi == TORIDRAW_SCREEN_X_NEAR_CLIPPED &&
                    verify_y[vi] == scene->screen_vertices_y[vi] &&
                    verify_z[vi] == scene->screen_vertices_z[vi] )
                {
                    nudge_divergences++;
                    continue;
                }
                if( verify_x[vi] != scene->screen_vertices_x[vi] ||
                    verify_y[vi] != scene->screen_vertices_y[vi] ||
                    verify_z[vi] != scene->screen_vertices_z[vi] )
                {
                    fprintf(
                        stderr,
                        "near_clip_mismatch: gate=%d vertex %d/%d "
                        "gated=(%d,%d,%d) other=(%d,%d,%d) "
                        "[mpitch=%d myaw=%d mroll=%d croll=%d tex=%d mid_z=%d near=%d]\n",
                        (int)may_clip,
                        vi,
                        vcount,
                        verify_x[vi],
                        verify_y[vi],
                        verify_z[vi],
                        scene->screen_vertices_x[vi],
                        scene->screen_vertices_y[vi],
                        scene->screen_vertices_z[vi],
                        model_pitch,
                        model_yaw,
                        model_roll,
                        camera_roll,
                        (int)model_has_textures(hnd),
                        center_projection.z,
                        camera->near_plane_z);
                    assert(0 && "the two near-clip kernels disagreed");
                }
            }

            /* Restore the gated result: the rest of the frame must render from
             * the path production would actually have taken. */
            memcpy(scene->screen_vertices_x, verify_x, (size_t)vcount * sizeof(int));
            memcpy(scene->screen_vertices_y, verify_y, (size_t)vcount * sizeof(int));
            memcpy(scene->screen_vertices_z, verify_z, (size_t)vcount * sizeof(int));

            if( (compared_models % 100000) == 0 )
                fprintf(
                    stderr,
                    "near_clip_verify: %ld models compared both kernels, "
                    "%ld nudge-only divergences\n",
                    compared_models,
                    nudge_divergences);
        }
    }
#endif

    return TORIDRAW_CULL_VISIBLE;
}

/**
 * Sign-only barycentric containment. Dividing all three coordinates by the
 * same `denominator` cannot change which of them are negative, so folding the
 * denominator's sign in and comparing the raw numerators against zero gives
 * the same answer with no divides and no float at all.
 *
 * This is the geometrically honest answer to "is the point on this triangle",
 * which is what ToriDraw_ProjectedModelContainsPoint promises. It is NOT what
 * the reference uses to pick a model under the cursor — see
 * toridraw_mouse_roughly_inside_triangle below.
 */
static inline bool
toridraw_triangle_contains_point(
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
    if( denominator == 0 )
        return false;

    int a_num = (y2 - y3) * (x - x3) + (x3 - x2) * (y - y3);
    int b_num = (y3 - y1) * (x - x3) + (x1 - x3) * (y - y3);
    int c_num = denominator - a_num - b_num; /* c = 1 - a - b, unnormalized */

    if( denominator < 0 )
    {
        a_num = -a_num;
        b_num = -b_num;
        c_num = -c_num;
    }

    return a_num >= 0 && b_num >= 0 && c_num >= 0;
}

/**
 * The reference's per-face hit test is NOT containment: it asks whether the
 * cursor, grown by `TORIDRAW_PICK_SLOP` in each direction, overlaps the
 * triangle's screen bounding box. Client-TS names it exactly what it is —
 * Model.isMouseRoughlyInsideTriangle (Model.ts:2413) — and the 239 deob
 * inlines the same four comparisons (class144.method4915:1625-1646) around a
 * slop of 5.
 *
 * The looseness is the whole point. A rock, a fence or a set of climbing
 * handholds is a handful of small triangles with gaps between them; exact
 * containment makes the gaps dead space and those locs nearly unclickable,
 * which is precisely the agility-obstacle complaint. Face bboxes overlap far
 * enough to fill the silhouette.
 */
#define TORIDRAW_PICK_SLOP 5

static inline bool
toridraw_mouse_roughly_inside_triangle(
    int x1,
    int y1,
    int x2,
    int y2,
    int x3,
    int y3,
    int x,
    int y)
{
    if( y + TORIDRAW_PICK_SLOP < y1 && y + TORIDRAW_PICK_SLOP < y2 &&
        y + TORIDRAW_PICK_SLOP < y3 )
        return false;
    if( y - TORIDRAW_PICK_SLOP > y1 && y - TORIDRAW_PICK_SLOP > y2 &&
        y - TORIDRAW_PICK_SLOP > y3 )
        return false;
    if( x + TORIDRAW_PICK_SLOP < x1 && x + TORIDRAW_PICK_SLOP < x2 &&
        x + TORIDRAW_PICK_SLOP < x3 )
        return false;
    if( x - TORIDRAW_PICK_SLOP > x1 && x - TORIDRAW_PICK_SLOP > x2 &&
        x - TORIDRAW_PICK_SLOP > x3 )
        return false;
    return true;
}

bool
ToriDraw_ProjectedModelContainsAabb(
    struct ToriDraw_Scene* scene,
    int screen_x,
    int screen_y)
{
    struct ToriDraw_AABB* aabb = &scene->aabb;
    return screen_x >= aabb->min_screen_x && screen_x <= aabb->max_screen_x &&
           screen_y >= aabb->min_screen_y && screen_y <= aabb->max_screen_y;
}

/**
 * Shared face walk behind both per-face tests. Which faces are eligible is the
 * same question for either one — only the triangle predicate differs, so
 * `rough` picks between them at the bottom.
 */
static bool
toridraw_projected_model_hit_face(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_ViewPort* view_port,
    int screen_x,
    int screen_y,
    bool rough)
{
    if( !ToriDraw_ProjectedModelContainsAabb(scene, screen_x, screen_y) )
        return false;

    int adjusted_screen_x = screen_x - view_port->x_center;
    int adjusted_screen_y = screen_y - view_port->y_center;

    faceint_t* fia = NULL;
    faceint_t* fib = NULL;
    faceint_t* fic = NULL;
    hsl16_t* colors_c = NULL;
    int face_count = 0;

    switch( hnd.kind )
    {
    case TORIDRAWMK_MODEL:
    {
        struct ToriDraw_Model* m = model_as_full(hnd);
        fia = m->face_indices_a;
        fib = m->face_indices_b;
        fic = m->face_indices_c;
        colors_c = m->face_colors_c;
        face_count = m->face_count;
        break;
    }
    default:
        return false;
    }

    for( int i = 0; i < face_count; i++ )
    {
        /*
         * Hidden faces do not pick. `faceColourC == -2` is the reference's one
         * hidden marker (class144.method4915:1575, Client-TS Model.ts:1856
         * spells the same thing as faceRenderType -1 because it hides on the
         * lit model instead), and ModelData.light stamps it for render type 2:
         * a fully transparent face, and every face mergeNormals removed where
         * two adjacent locs share all three vertices (class136:644/652). Here
         * that sentinel is TORIDRAWHSL16_HIDDEN, written by ToriDraw_Light.
         *
         * Note what is deliberately NOT filtered: the reference runs this pick
         * *after* computing the winding cull and ignores it (class144:1616 vs
         * :1618), so a backfacing face still picks, and so does a face the
         * depth sort later drops.
         */
        if( colors_c && colors_c[i] == TORIDRAWHSL16_HIDDEN )
            continue;

        int face_a = fia[i];
        int face_b = fib[i];
        int face_c = fic[i];

        int x1 = scene->screen_vertices_x[face_a];
        int x2 = scene->screen_vertices_x[face_b];
        int x3 = scene->screen_vertices_x[face_c];

        /*
         * Near-clipped faces do not pick either (the reference reaches its
         * pick only down the not-near-clipped arm, class144:1615). Mandatory
         * rather than cosmetic for the rough test: the projection parks a
         * behind-the-eye vertex at screen x -5000 and leaves its y
         * *undivided*, so a bounding box over it spans the screen and would
         * swallow every click behind the model.
         *
         * Gated on near_clipped exactly as the reference gates on `clipped`
         * (Model.render2:1876). Not just a saving: when the flag is clear the
         * projection ran its no-clip kernel, which skips the -5001 nudge, so a
         * legitimately projected -5000 is possible and testing for it here
         * would drop a pickable face.
         */
        if( scene->near_clipped &&
            (x1 == TORIDRAW_SCREEN_X_NEAR_CLIPPED || x2 == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
             x3 == TORIDRAW_SCREEN_X_NEAR_CLIPPED) )
            continue;

        int y1 = scene->screen_vertices_y[face_a];
        int y2 = scene->screen_vertices_y[face_b];
        int y3 = scene->screen_vertices_y[face_c];

        bool hit = rough ? toridraw_mouse_roughly_inside_triangle(
                               x1, y1, x2, y2, x3, y3, adjusted_screen_x, adjusted_screen_y)
                         : toridraw_triangle_contains_point(
                               x1, y1, x2, y2, x3, y3, adjusted_screen_x, adjusted_screen_y);
        if( hit )
            return true;
    }

    return false;
}

bool
ToriDraw_ProjectedModelContainsPoint(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_ViewPort* view_port,
    int screen_x,
    int screen_y)
{
    return toridraw_projected_model_hit_face(
        scene, hnd, view_port, screen_x, screen_y, /* rough */ false);
}

bool
ToriDraw_ProjectedModelMouseHitTest(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_ViewPort* view_port,
    int screen_x,
    int screen_y)
{
    return toridraw_projected_model_hit_face(
        scene, hnd, view_port, screen_x, screen_y, /* rough */ true);
}
