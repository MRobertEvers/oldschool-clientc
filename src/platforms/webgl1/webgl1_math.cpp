#include "platforms/webgl1/webgl1_internal.h"

#include <cmath>
#include <cstring>

static void
mat4_mul_colmajor(
    const float* a,
    const float* b,
    float* out)
{
    for( int c = 0; c < 4; ++c )
        for( int r = 0; r < 4; ++r )
        {
            float s = 0.0f;
            for( int k = 0; k < 4; ++k )
                s += a[k * 4 + r] * b[c * 4 + k];
            out[c * 4 + r] = s;
        }
}

float
wg1_dash_yaw_to_radians(int dash_yaw)
{
    return ((float)dash_yaw * 2.0f * (float)M_PI) / 2048.0f;
}

void
wg1_prebake_model_yaw_cos_sin(int dash_yaw, float* cos_out, float* sin_out)
{
    if( !cos_out || !sin_out )
        return;
    const float yaw_rad = wg1_dash_yaw_to_radians(dash_yaw);
    *cos_out = cosf(yaw_rad);
    *sin_out = sinf(yaw_rad);
}

float
wg1_texture_animation_signed(int animation_direction, int animation_speed)
{
    if( animation_direction == 0 )
        return 0.0f;
    float speed = ((float)animation_speed) / 128.0f;
    if( animation_direction == 2 || animation_direction == 4 )
        return speed;
    return -speed;
}

static void
wg1_compute_view_matrix(
    float* out_matrix,
    float camera_x,
    float camera_y,
    float camera_z,
    float pitch,
    float yaw)
{
    float cosPitch = cosf(-pitch);
    float sinPitch = sinf(-pitch);
    float cosYaw = cosf(-yaw);
    float sinYaw = sinf(-yaw);

    out_matrix[0] = cosYaw;
    out_matrix[1] = sinYaw * sinPitch;
    out_matrix[2] = sinYaw * cosPitch;
    out_matrix[3] = 0.0f;

    out_matrix[4] = 0.0f;
    out_matrix[5] = cosPitch;
    out_matrix[6] = -sinPitch;
    out_matrix[7] = 0.0f;

    out_matrix[8] = -sinYaw;
    out_matrix[9] = cosYaw * sinPitch;
    out_matrix[10] = cosYaw * cosPitch;
    out_matrix[11] = 0.0f;

    out_matrix[12] = -camera_x * cosYaw + camera_z * sinYaw;
    out_matrix[13] =
        -camera_x * sinYaw * sinPitch - camera_y * cosPitch - camera_z * cosYaw * sinPitch;
    out_matrix[14] =
        -camera_x * sinYaw * cosPitch + camera_y * sinPitch - camera_z * cosYaw * cosPitch;
    out_matrix[15] = 1.0f;
}

static void
wg1_compute_projection_matrix(
    float* out_matrix,
    float fov,
    float screen_width,
    float screen_height)
{
    float y = 1.0f / tanf(fov * 0.5f);
    float x = y;

    out_matrix[0] = x * 512.0f / (screen_width / 2.0f);
    out_matrix[1] = 0.0f;
    out_matrix[2] = 0.0f;
    out_matrix[3] = 0.0f;

    out_matrix[4] = 0.0f;
    out_matrix[5] = -y * 512.0f / (screen_height / 2.0f);
    out_matrix[6] = 0.0f;
    out_matrix[7] = 0.0f;

    out_matrix[8] = 0.0f;
    out_matrix[9] = 0.0f;
    out_matrix[10] = 0.0f;
    out_matrix[11] = 1.0f;

    out_matrix[12] = 0.0f;
    out_matrix[13] = 0.0f;
    out_matrix[14] = -1.0f;
    out_matrix[15] = 0.0f;
}

void
wg1_matrices_for_frame(
    struct GGame* game,
    float projection_width,
    float projection_height,
    float* out_modelview16,
    float* out_proj16)
{
    if( !game || !out_modelview16 || !out_proj16 )
        return;
    const float pitch_rad = wg1_dash_yaw_to_radians(game->camera_pitch);
    const float yaw_rad = wg1_dash_yaw_to_radians(game->camera_yaw);
    wg1_compute_view_matrix(out_modelview16, 0.0f, 0.0f, 0.0f, pitch_rad, yaw_rad);
    wg1_compute_projection_matrix(
        out_proj16,
        (90.0f * (float)M_PI) / 180.0f,
        projection_width,
        projection_height);
}

LogicalViewportRect
wg1_compute_logical_viewport_rect(
    int window_width,
    int window_height,
    const struct GGame* game)
{
    LogicalViewportRect rect = { 0, 0, window_width, window_height };
    if( window_width <= 0 || window_height <= 0 || !game || !game->view_port )
        return rect;

    int x = game->viewport_offset_x;
    int y = game->viewport_offset_y;
    int w = game->view_port->width;
    int h = game->view_port->height;
    if( w <= 0 || h <= 0 )
        return rect;

    if( x < 0 )
        x = 0;
    if( y < 0 )
        y = 0;
    if( x >= window_width || y >= window_height )
        return rect;
    if( x + w > window_width )
        w = window_width - x;
    if( y + h > window_height )
        h = window_height - y;
    if( w <= 0 || h <= 0 )
        return rect;

    rect.x = x;
    rect.y = y;
    rect.width = w;
    rect.height = h;
    return rect;
}

ToriGlViewportPixels
wg1_compute_gl_world_viewport_rect(
    int fb_width,
    int fb_height,
    int win_width,
    int win_height,
    const LogicalViewportRect& lr)
{
    ToriGlViewportPixels rect = { 0, 0, fb_width, fb_height };
    if( fb_width <= 0 || fb_height <= 0 || win_width <= 0 || win_height <= 0 )
        return rect;

    const double sx = (double)fb_width / (double)win_width;
    const double sy = (double)fb_height / (double)win_height;

    int scaled_x = (int)lround((double)lr.x * sx);
    int scaled_top_y = (int)lround((double)lr.y * sy);
    int scaled_w = (int)lround((double)lr.width * sx);
    int scaled_h = (int)lround((double)lr.height * sy);

    int clamped_x = scaled_x < 0 ? 0 : scaled_x;
    int clamped_top_y = scaled_top_y < 0 ? 0 : scaled_top_y;
    if( clamped_x >= fb_width || clamped_top_y >= fb_height )
        return rect;

    int clamped_w = scaled_w;
    int clamped_h = scaled_h;
    if( clamped_x + clamped_w > fb_width )
        clamped_w = fb_width - clamped_x;
    if( clamped_top_y + clamped_h > fb_height )
        clamped_h = fb_height - clamped_top_y;
    if( clamped_w <= 0 || clamped_h <= 0 )
        return rect;

    rect.x = clamped_x;
    rect.y = fb_height - (clamped_top_y + clamped_h);
    rect.width = clamped_w;
    rect.height = clamped_h;
    return rect;
}

GLViewportRect
wg1_compute_world_viewport_in_letterbox(
    int lb_w,
    int lb_h,
    int window_width,
    int window_height,
    const LogicalViewportRect& logical_rect)
{
    GLViewportRect rect = { 0, 0, lb_w, lb_h };
    if( lb_w <= 0 || lb_h <= 0 || window_width <= 0 || window_height <= 0 )
        return rect;

    const double scale_x =
        (window_width > 0) ? ((double)lb_w / (double)window_width) : 1.0;
    const double scale_y =
        (window_height > 0) ? ((double)lb_h / (double)window_height) : 1.0;

    int scaled_x = (int)lround((double)logical_rect.x * scale_x);
    int scaled_top_y = (int)lround((double)logical_rect.y * scale_y);
    int scaled_w = (int)lround((double)logical_rect.width * scale_x);
    int scaled_h = (int)lround((double)logical_rect.height * scale_y);

    int clamped_x = scaled_x < 0 ? 0 : scaled_x;
    int clamped_top_y = scaled_top_y < 0 ? 0 : scaled_top_y;
    if( clamped_x >= lb_w || clamped_top_y >= lb_h )
        return rect;

    int clamped_w = scaled_w;
    int clamped_h = scaled_h;
    if( clamped_x + clamped_w > lb_w )
        clamped_w = lb_w - clamped_x;
    if( clamped_top_y + clamped_h > lb_h )
        clamped_h = lb_h - clamped_top_y;
    if( clamped_w <= 0 || clamped_h <= 0 )
        return rect;

    rect.x = clamped_x;
    rect.y = lb_h - (clamped_top_y + clamped_h);
    rect.width = clamped_w;
    rect.height = clamped_h;
    return rect;
}
