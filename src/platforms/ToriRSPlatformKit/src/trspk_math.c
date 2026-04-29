#include "../include/ToriRSPlatformKit/trspk_math.h"

#include "graphics/dash_hsl16.h"
#include "osrs/game.h"

/** Keep TRSPK sentinels in sync with graphics/dash_hsl16.h and batch_add_model.h. */
_Static_assert(
    (uint32_t)TRSPK_HSL16_FLAT == (uint32_t)DASHHSL16_FLAT,
    "TRSPK_HSL16_FLAT must match DASHHSL16_FLAT");
_Static_assert(
    (uint32_t)TRSPK_HSL16_HIDDEN == (uint32_t)DASHHSL16_HIDDEN,
    "TRSPK_HSL16_HIDDEN must match DASHHSL16_HIDDEN");

float
trspk_dash_yaw_to_radians(int32_t yaw_2048)
{
    return ((float)yaw_2048 * 2.0f * TRSPK_PI) / 2048.0f;
}

float
trspk_texture_animation_signed(
    int animation_direction,
    int animation_speed)
{
    if( animation_direction == 0 )
        return 0.0f;
    float speed = ((float)animation_speed) / 128.0f;
    if( animation_direction == 2 || animation_direction == 4 )
        return speed;
    return -speed;
}

float
trspk_pack_gpu_uv_mode_float(float anim_u, float anim_v)
{
    unsigned mag_u = (unsigned)fmaxf(0.0f, fminf(255.0f, anim_u * 256.0f + 0.5f));
    unsigned mag_v = (unsigned)fmaxf(0.0f, fminf(255.0f, anim_v * 256.0f + 0.5f));
    unsigned enc = 0u;
    if( mag_u > 0u )
        enc = 1u + mag_u;
    else if( mag_v > 0u )
        enc = 257u + mag_v;
    return 2.0f * (float)enc;
}

uint16_t
trspk_pack_gpu_uv_mode_u16(float anim_u, float anim_v)
{
    const float f = trspk_pack_gpu_uv_mode_float(anim_u, anim_v);
    return (uint16_t)fminf(65535.0f, f + 0.5f);
}

static int32_t
trspk_round_to_i32(double v)
{
    return (int32_t)(v >= 0.0 ? v + 0.5 : v - 0.5);
}

void
trspk_pass_logical_from_game(
    TRSPK_Rect* out,
    int window_width,
    int window_height,
    const struct GGame* game)
{
    if( !out )
        return;
    out->x = 0;
    out->y = 0;
    out->width = (int32_t)window_width;
    out->height = (int32_t)window_height;
    if( window_width <= 0 || window_height <= 0 || !game || !game->view_port )
        return;

    int x = game->viewport_offset_x;
    int y = game->viewport_offset_y;
    int w = game->view_port->width;
    int h = game->view_port->height;
    if( w <= 0 || h <= 0 )
        return;

    if( x < 0 )
        x = 0;
    if( y < 0 )
        y = 0;
    if( x >= window_width || y >= window_height )
        return;
    if( x + w > window_width )
        w = window_width - x;
    if( y + h > window_height )
        h = window_height - y;
    if( w <= 0 || h <= 0 )
        return;

    out->x = (int32_t)x;
    out->y = (int32_t)y;
    out->width = (int32_t)w;
    out->height = (int32_t)h;
}

void
trspk_logical_rect_to_gl_viewport_pixels(
    TRSPK_Rect* out_gl_lower_left,
    uint32_t fb_w,
    uint32_t fb_h,
    uint32_t win_w,
    uint32_t win_h,
    const TRSPK_Rect* logical)
{
    if( !out_gl_lower_left )
        return;
    const int32_t fw = (int32_t)(fb_w > 0u ? fb_w : 1u);
    const int32_t fh = (int32_t)(fb_h > 0u ? fb_h : 1u);
    out_gl_lower_left->x = 0;
    out_gl_lower_left->y = 0;
    out_gl_lower_left->width = fw;
    out_gl_lower_left->height = fh;

    if( !logical || fb_w == 0u || fb_h == 0u || win_w == 0u || win_h == 0u || logical->width <= 0 ||
        logical->height <= 0 )
        return;

    const double sx = (double)fb_w / (double)win_w;
    const double sy = (double)fb_h / (double)win_h;
    const int32_t scaled_x = trspk_round_to_i32((double)logical->x * sx);
    const int32_t scaled_top_y = trspk_round_to_i32((double)logical->y * sy);
    const int32_t scaled_w = trspk_round_to_i32((double)logical->width * sx);
    const int32_t scaled_h = trspk_round_to_i32((double)logical->height * sy);

    int32_t clamped_x = scaled_x < 0 ? 0 : scaled_x;
    int32_t clamped_top_y = scaled_top_y < 0 ? 0 : scaled_top_y;
    if( clamped_x >= fw || clamped_top_y >= fh )
        return;

    int32_t clamped_w = scaled_w;
    int32_t clamped_h = scaled_h;
    if( clamped_x + clamped_w > fw )
        clamped_w = fw - clamped_x;
    if( clamped_top_y + clamped_h > fh )
        clamped_h = fh - clamped_top_y;
    if( clamped_w <= 0 || clamped_h <= 0 )
        return;

    out_gl_lower_left->x = clamped_x;
    out_gl_lower_left->y = fh - (clamped_top_y + clamped_h);
    out_gl_lower_left->width = clamped_w;
    out_gl_lower_left->height = clamped_h;
}
