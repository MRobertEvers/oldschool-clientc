#include "platforms/platform_impl2_sdl2_renderer_webgl1.h"
#include "platforms/webgl1/ctx.h"

#ifdef __EMSCRIPTEN__

#    include <GLES2/gl2.h>

#    include <cmath>
#    include <cstdio>
#    include <cstring>

extern "C" {
#    include "graphics/dash.h"
#    include "osrs/game.h"
}

#ifndef M_PI
#    define M_PI 3.14159265358979323846
#endif

float
webgl1_dash_yaw_to_radians(int dash_yaw)
{
    return ((float)dash_yaw * 2.0f * (float)M_PI) / 2048.0f;
}

void
webgl1_compute_view_matrix(
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

void
webgl1_compute_projection_matrix(
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

LogicalViewportRect
compute_logical_viewport_rect(int window_width, int window_height, const struct GGame* game)
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
compute_gl_world_viewport_rect(
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

void
webgl1_clamped_gl_scissor(
    int fbw,
    int fbh,
    int win_w,
    int win_h,
    int dst_x,
    int dst_y,
    int dst_w,
    int dst_h)
{
    if( fbw <= 0 || fbh <= 0 || win_w <= 0 || win_h <= 0 || dst_w <= 0 || dst_h <= 0 )
    {
        glScissor(0, 0, (GLsizei)(fbw > 0 ? fbw : 1), (GLsizei)(fbh > 0 ? fbh : 1));
        return;
    }

    LogicalViewportRect lr = { dst_x, dst_y, dst_w, dst_h };
    ToriGlViewportPixels gr = compute_gl_world_viewport_rect(fbw, fbh, win_w, win_h, lr);

    GLint msx = (GLint)gr.x;
    GLint msy = (GLint)fbh - (GLint)gr.y - (GLint)gr.height;
    GLint msw = (GLint)gr.width;
    GLint msh = (GLint)gr.height;

    if( msx < 0 )
    {
        msw += msx;
        msx = 0;
    }
    if( msy < 0 )
    {
        msh += msy;
        msy = 0;
    }
    if( msx + msw > fbw )
        msw = fbw - msx;
    if( msy + msh > fbh )
        msh = fbh - msy;
    if( msw <= 0 || msh <= 0 )
    {
        glScissor(0, 0, (GLsizei)(fbw > 0 ? fbw : 1), (GLsizei)(fbh > 0 ? fbh : 1));
        return;
    }

    glScissor(msx, msy, msw, msh);
}

float
webgl1_texture_animation_signed(int animation_direction, int animation_speed)
{
    if( animation_direction == 0 )
        return 0.0f;
    float speed = ((float)animation_speed) / 128.0f;
    if( animation_direction == 2 || animation_direction == 4 )
        return speed;
    return -speed;
}

void
webgl1_sync_drawable_size(Platform2_SDL2_Renderer_WebGL1* renderer)
{
    if( !renderer || !renderer->platform || !renderer->platform->window )
        return;
    int w = 0;
    int h = 0;
    SDL_GL_GetDrawableSize(renderer->platform->window, &w, &h);
    if( w > 0 && h > 0 )
    {
        renderer->width = w;
        renderer->height = h;
    }
}

#endif
