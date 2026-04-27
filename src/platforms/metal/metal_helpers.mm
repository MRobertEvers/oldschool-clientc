// System ObjC/Metal headers must come before any game headers.
#include "platforms/metal/metal_internal.h"
#include <SDL_metal.h>

// Column-major 4x4 multiply: out = a * b
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
metal_dash_yaw_to_radians(int dash_yaw)
{
    return ((float)dash_yaw * 2.0f * (float)M_PI) / 2048.0f;
}

void
metal_prebake_model_yaw_cos_sin(
    int dash_yaw,
    float* cos_out,
    float* sin_out)
{
    if( !cos_out || !sin_out )
        return;
    const float yaw_rad = metal_dash_yaw_to_radians(dash_yaw);
    *cos_out = cosf(yaw_rad);
    *sin_out = sinf(yaw_rad);
}

void
metal_compute_view_matrix(
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
metal_compute_projection_matrix(
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
metal_remap_projection_opengl_to_metal_z(float* proj_colmajor)
{
    static const float k_clip_z[16] = {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 1.0f,
    };
    float tmp[16];
    mat4_mul_colmajor(k_clip_z, proj_colmajor, tmp);
    memcpy(proj_colmajor, tmp, sizeof(tmp));
}

float
metal_texture_animation_signed(
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

LogicalViewportRect
compute_logical_viewport_rect(
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

MTLScissorRect
metal_clamped_scissor_from_logical_dst_bb(
    int fbw,
    int fbh,
    int win_w,
    int win_h,
    int dst_x,
    int dst_y,
    int dst_w,
    int dst_h)
{
    MTLScissorRect full = {
        0, 0, (NSUInteger)(fbw > 0 ? fbw : 1), (NSUInteger)(fbh > 0 ? fbh : 1)
    };
    if( fbw <= 0 || fbh <= 0 || win_w <= 0 || win_h <= 0 || dst_w <= 0 || dst_h <= 0 )
        return full;

    LogicalViewportRect lr = { dst_x, dst_y, dst_w, dst_h };
    ToriGlViewportPixels gr = compute_gl_world_viewport_rect(fbw, fbh, win_w, win_h, lr);

    NSInteger msx = (NSInteger)gr.x;
    NSInteger msy = (NSInteger)fbh - (NSInteger)gr.y - (NSInteger)gr.height;
    NSInteger msw = (NSInteger)gr.width;
    NSInteger msh = (NSInteger)gr.height;

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
        return full;

    return MTLScissorRect{ (NSUInteger)msx, (NSUInteger)msy, (NSUInteger)msw, (NSUInteger)msh };
}

void
sync_drawable_size(Platform2_SDL2_Renderer_Metal* renderer)
{
    if( !renderer || !renderer->metal_view || !renderer->platform || !renderer->platform->window )
        return;

    int w = 0;
    int h = 0;
    SDL_Metal_GetDrawableSize(renderer->platform->window, &w, &h);
    if( w <= 0 || h <= 0 )
        return;

    renderer->width = w;
    renderer->height = h;

    CAMetalLayer* layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(renderer->metal_view);
    if( !layer )
        return;

    CGSize desired = CGSizeMake((CGFloat)w, (CGFloat)h);
    if( !CGSizeEqualToSize(layer.drawableSize, desired) )
        layer.drawableSize = desired;
}

namespace
{

void* g_mtl_depth_stencil = nullptr;
void* g_mtl_depth_texture = nullptr;
int g_depth_tex_w = 0;
int g_depth_tex_h = 0;

void* g_mtl_clear_rect_depth_pipeline = nullptr;
void* g_mtl_clear_rect_depth_write_ds = nullptr;
void* g_mtl_clear_quad_buf = nullptr;

} // namespace

void
metal_internal_set_depth_stencil(void* retained_depth_stencil)
{
    if( g_mtl_depth_stencil )
        CFRelease(g_mtl_depth_stencil);
    g_mtl_depth_stencil = retained_depth_stencil;
}

void*
metal_internal_depth_stencil(void)
{
    return g_mtl_depth_stencil;
}

void
metal_internal_set_depth_texture(
    void* retained_depth_texture,
    int w,
    int h)
{
    if( g_mtl_depth_texture )
        CFRelease(g_mtl_depth_texture);
    g_mtl_depth_texture = retained_depth_texture;
    g_depth_tex_w = w;
    g_depth_tex_h = h;
}

void*
metal_internal_depth_texture(void)
{
    return g_mtl_depth_texture;
}

bool
metal_internal_depth_texture_matches(
    int w,
    int h)
{
    return g_mtl_depth_texture != nullptr && g_depth_tex_w == w && g_depth_tex_h == h;
}

void
metal_internal_shutdown_depth_pass_resources(void)
{
    if( g_mtl_depth_stencil )
    {
        CFRelease(g_mtl_depth_stencil);
        g_mtl_depth_stencil = nullptr;
    }
    if( g_mtl_depth_texture )
    {
        CFRelease(g_mtl_depth_texture);
        g_mtl_depth_texture = nullptr;
    }
    g_depth_tex_w = 0;
    g_depth_tex_h = 0;
}

void
metal_internal_set_clear_rect_depth_pipeline(void* retained_pipeline)
{
    if( g_mtl_clear_rect_depth_pipeline )
        CFRelease(g_mtl_clear_rect_depth_pipeline);
    g_mtl_clear_rect_depth_pipeline = retained_pipeline;
}

void*
metal_internal_clear_rect_depth_pipeline(void)
{
    return g_mtl_clear_rect_depth_pipeline;
}

void
metal_internal_set_clear_rect_depth_write_ds(void* retained_ds)
{
    if( g_mtl_clear_rect_depth_write_ds )
        CFRelease(g_mtl_clear_rect_depth_write_ds);
    g_mtl_clear_rect_depth_write_ds = retained_ds;
}

void*
metal_internal_clear_rect_depth_write_ds(void)
{
    return g_mtl_clear_rect_depth_write_ds;
}

void
metal_internal_set_clear_quad_buf(void* retained_buffer)
{
    if( g_mtl_clear_quad_buf )
        CFRelease(g_mtl_clear_quad_buf);
    g_mtl_clear_quad_buf = retained_buffer;
}

void*
metal_internal_clear_quad_buf(void)
{
    return g_mtl_clear_quad_buf;
}

void
metal_internal_shutdown_clear_rect_aux_resources(void)
{
    if( g_mtl_clear_rect_depth_pipeline )
    {
        CFRelease(g_mtl_clear_rect_depth_pipeline);
        g_mtl_clear_rect_depth_pipeline = nullptr;
    }
    if( g_mtl_clear_rect_depth_write_ds )
    {
        CFRelease(g_mtl_clear_rect_depth_write_ds);
        g_mtl_clear_rect_depth_write_ds = nullptr;
    }
    if( g_mtl_clear_quad_buf )
    {
        CFRelease(g_mtl_clear_quad_buf);
        g_mtl_clear_quad_buf = nullptr;
    }
}

MTLViewport
metal_pass_get_metal_vp(const struct MetalRendererCore* r)
{
    if( !r )
        return {};
    const MetalPassState& p = r->pass;
    return { .originX = p.metalVp_originX,
             .originY = p.metalVp_originY,
             .width = p.metalVp_width,
             .height = p.metalVp_height,
             .znear = p.metalVp_znear,
             .zfar = p.metalVp_zfar };
}

MTLViewport
metal_pass_get_full_drawable_vp(const struct MetalRendererCore* r)
{
    if( !r )
        return {};
    const MetalPassState& p = r->pass;
    return { .originX = p.fullDrawableVp_originX,
             .originY = p.fullDrawableVp_originY,
             .width = p.fullDrawableVp_width,
             .height = p.fullDrawableVp_height,
             .znear = p.fullDrawableVp_znear,
             .zfar = p.fullDrawableVp_zfar };
}

void
metal_pass_set_metal_vp(
    struct MetalRendererCore* r,
    MTLViewport v)
{
    if( !r )
        return;
    MetalPassState& p = r->pass;
    p.metalVp_originX = v.originX;
    p.metalVp_originY = v.originY;
    p.metalVp_width = v.width;
    p.metalVp_height = v.height;
    p.metalVp_znear = v.znear;
    p.metalVp_zfar = v.zfar;
}

void
metal_pass_set_full_drawable_vp(
    struct MetalRendererCore* r,
    MTLViewport v)
{
    if( !r )
        return;
    MetalPassState& p = r->pass;
    p.fullDrawableVp_originX = v.originX;
    p.fullDrawableVp_originY = v.originY;
    p.fullDrawableVp_width = v.width;
    p.fullDrawableVp_height = v.height;
    p.fullDrawableVp_znear = v.znear;
    p.fullDrawableVp_zfar = v.zfar;
}

LogicalViewportRect
metal_pass_get_pass_3d_dst_logical(const struct MetalRendererCore* r)
{
    if( !r )
        return {};
    return {
        r->pass.pass_3d_dst_x, r->pass.pass_3d_dst_y, r->pass.pass_3d_dst_w, r->pass.pass_3d_dst_h
    };
}

void
metal_pass_set_pass_3d_dst_logical(
    struct MetalRendererCore* r,
    const LogicalViewportRect& lr)
{
    if( !r )
        return;
    r->pass.pass_3d_dst_x = lr.x;
    r->pass.pass_3d_dst_y = lr.y;
    r->pass.pass_3d_dst_w = lr.width;
    r->pass.pass_3d_dst_h = lr.height;
}
