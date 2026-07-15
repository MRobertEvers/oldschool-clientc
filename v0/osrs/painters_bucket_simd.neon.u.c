#include <arm_neon.h>
#include <stdint.h>

static void
bucket_fill_distances(
    struct PainterBucketCtx* w,
    struct Painter* painter,
    int camera_sx,
    int camera_sz,
    int min_draw_x,
    int max_draw_x,
    int min_draw_z,
    int max_draw_z,
    int min_level,
    int max_level)
{
    int32_t step_arr[4] = { 0, 1, 2, 3 };
    int32x4_t v_step = vld1q_s32(step_arr);
    int32x4_t v_cam_x = vdupq_n_s32(camera_sx);
    int32x4_t v_cam_z = vdupq_n_s32(camera_sz);

    for( int s = min_level; s < max_level && s < painter->levels; s++ )
    {
        for( int z = min_draw_z; z < max_draw_z; z++ )
        {
            int32x4_t v_z = vdupq_n_s32(z);
            int32x4_t v_dist_z = vabdq_s32(v_z, v_cam_z);

            int x = min_draw_x;
            int ti_base = painter_coord_idx(painter, min_draw_x, z, s);

            for( ; x <= max_draw_x - 4; x += 4, ti_base += 4 )
            {
                int32x4_t v_x_base = vdupq_n_s32(x);
                int32x4_t v_x = vaddq_s32(v_x_base, v_step);
                int32x4_t v_dist_x = vabdq_s32(v_x, v_cam_x);
                int32x4_t v_total_dist = vaddq_s32(v_dist_x, v_dist_z);
                vst1q_s32((int32_t*)&w->dist[ti_base], v_total_dist);
            }

            for( ; x < max_draw_x; x++, ti_base++ )
            {
                w->dist[ti_base] = abs(x - camera_sx) + abs(z - camera_sz);
            }
        }
    }
}
