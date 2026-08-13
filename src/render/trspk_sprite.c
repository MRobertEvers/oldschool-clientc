#include "render/trspk_sprite.h"

#include "toridraw_math.h"
#include "toridraw_sprite.h"

#include <stdlib.h>
#include <string.h>

void
trspk_sprite_argb_to_rgba(
    uint32_t const* src,
    uint32_t* dst,
    size_t count)
{
    for( size_t i = 0; i < count; i++ )
    {
        uint32_t const pix = src[i];
        uint8_t const a_hi = (uint8_t)((pix >> 24) & 0xFFu);
        uint32_t const rgb = pix & 0x00FFFFFFu;
        uint8_t const a = (a_hi != 0u) ? a_hi : (rgb != 0u ? 0xFFu : 0u);
        dst[i] = (uint32_t)((pix >> 16) & 0xFFu) | ((uint32_t)((pix >> 8) & 0xFFu) << 8) |
                 ((uint32_t)(pix & 0xFFu) << 16) | ((uint32_t)a << 24);
    }
}

uint32_t const*
trspk_sprite_rotmask_bake(
    struct TRSPK_RotmaskBake* bake,
    struct ToriRS_RenderCommand_Sprite const* cmd,
    struct ToriDraw_Sprite* sp,
    struct ToriDraw_Sprite* mask_sp,
    int dst_w,
    int dst_h)
{
    struct ToriDraw_ViewPort vp = { 0 };
    size_t need;
    bool grew = false;
    if( !bake || !cmd || !sp || !mask_sp || dst_w <= 0 || dst_h <= 0 )
        return NULL;
    need = (size_t)dst_w * (size_t)dst_h;
    if( bake->capacity < need )
    {
        uint32_t* grown = realloc(bake->pixels, need * sizeof(uint32_t));
        if( !grown )
            return NULL;
        bake->pixels = grown;
        bake->capacity = need;
        grew = true;
    }
    /* Whether the pixels the blit will not touch need wiping is the caller's to
     * state -- see ToriRS_RenderCommand_Sprite.mask_needs_clear. A buffer that
     * just grew is wiped either way: realloc leaves the new tail
     * uninitialised, so honouring "no clear" there would upload heap garbage
     * rather than the previous frame the caller meant to keep. */
    if( cmd->mask_needs_clear || grew )
        memset(bake->pixels, 0, need * sizeof(uint32_t));

    vp.width = dst_w;
    vp.height = dst_h;
    vp.clip_left = 0;
    vp.clip_top = 0;
    vp.clip_right = dst_w;
    vp.clip_bottom = dst_h;
    vp.stride = dst_w;
    ToriDraw2D_BlitSpriteRotatedMaskedEx(
        sp,
        mask_sp,
        cmd->mask_keep_opaque,
        &vp,
        0,
        0,
        dst_w,
        dst_h,
        cmd->dst_anchor_x,
        cmd->dst_anchor_y,
        cmd->src_anchor_x,
        cmd->src_anchor_y,
        cmd->rotation_r2pi2048,
        (int*)bake->pixels);
    trspk_sprite_argb_to_rgba(bake->pixels, bake->pixels, need);
    return bake->pixels;
}

void
trspk_sprite_rotmask_bake_release(struct TRSPK_RotmaskBake* bake)
{
    if( !bake )
        return;
    free(bake->pixels);
    bake->pixels = NULL;
    bake->capacity = 0;
}

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
