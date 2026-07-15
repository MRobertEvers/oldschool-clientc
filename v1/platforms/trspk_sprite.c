#include "trspk_sprite.h"

#include "toridraw/toridraw_math.h"

void
trspk_sprite_local_to_uv(
    int local_x,
    int local_y,
    int dst_anchor_x,
    int dst_anchor_y,
    int src_anchor_x,
    int src_anchor_y,
    int crop_w,
    int crop_h,
    int rotation_r2pi2048,
    float u0,
    float v0,
    float u1,
    float v1,
    float* out_u,
    float* out_v)
{
    const int ang = ToriDraw_NormalizeAngle(rotation_r2pi2048);
    const int cos = ToriDraw_Cos(ang);
    const int sin = ToriDraw_Sin(ang);
    const float rel_x = (float)local_x - 0.5f - (float)dst_anchor_x;
    const float rel_y = (float)local_y - 0.5f - (float)dst_anchor_y;
    const int src_rel_x = (int)(rel_x * (float)cos + rel_y * (float)sin) >> 16;
    const int src_rel_y = (int)(-rel_x * (float)sin + rel_y * (float)cos) >> 16;
    const float src_x = (float)(src_anchor_x + src_rel_x);
    const float src_y = (float)(src_anchor_y + src_rel_y);
    const float du = u1 - u0;
    const float dv = v1 - v0;
    *out_u = u0 + (crop_w > 0 ? ((src_x + 0.5f) / (float)crop_w) * du : 0.0f);
    *out_v = v0 + (crop_h > 0 ? ((src_y + 0.5f) / (float)crop_h) * dv : 0.0f);
}

void
trspk_sprite_rotated_corners(
    int dst_x,
    int dst_y,
    int dst_w,
    int dst_h,
    int dst_anchor_x,
    int dst_anchor_y,
    int src_anchor_x,
    int src_anchor_y,
    int crop_w,
    int crop_h,
    int rotation_r2pi2048,
    float u0,
    float v0,
    float u1,
    float v1,
    float pos[4][2],
    float uv[4][2])
{
    const int local_corners[4][2] = {
        { 0,     0     },
        { dst_w, 0     },
        { dst_w, dst_h },
        { 0,     dst_h },
    };

    for( int i = 0; i < 4; i++ )
    {
        pos[i][0] = (float)(dst_x + local_corners[i][0]);
        pos[i][1] = (float)(dst_y + local_corners[i][1]);
        trspk_sprite_local_to_uv(
            local_corners[i][0],
            local_corners[i][1],
            dst_anchor_x,
            dst_anchor_y,
            src_anchor_x,
            src_anchor_y,
            crop_w,
            crop_h,
            rotation_r2pi2048,
            u0,
            v0,
            u1,
            v1,
            &uv[i][0],
            &uv[i][1]);
    }
}

void
trspk_sprite_tile_phase_origin(
    int dest_x,
    int dest_y,
    int origin_x,
    int origin_y,
    int tile_w,
    int tile_h,
    int* out_start_x,
    int* out_start_y)
{
    int start_x = dest_x;
    int start_y = dest_y;
    if( tile_w > 0 )
    {
        const int phase_x = ((start_x - origin_x) % tile_w + tile_w) % tile_w;
        start_x -= phase_x;
    }
    if( tile_h > 0 )
    {
        const int phase_y = ((start_y - origin_y) % tile_h + tile_h) % tile_h;
        start_y -= phase_y;
    }
    *out_start_x = start_x;
    *out_start_y = start_y;
}
