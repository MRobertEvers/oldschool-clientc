#ifndef TRSPK_SPRITE_H
#define TRSPK_SPRITE_H

#ifdef __cplusplus
extern "C" {
#endif

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
    float* out_v);

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
    float uv[4][2]);

void
trspk_sprite_tile_phase_origin(
    int dest_x,
    int dest_y,
    int origin_x,
    int origin_y,
    int tile_w,
    int tile_h,
    int* out_start_x,
    int* out_start_y);

#ifdef __cplusplus
}
#endif

#endif
