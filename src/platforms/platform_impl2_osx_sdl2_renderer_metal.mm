// System ObjC/Metal headers must come before any game headers.
#include "platform_impl2_osx_sdl2_renderer_metal.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "nuklear/backends/torirs_nk_metal_sdl.h"
#include "platforms/common/torirs_nk_ui_bridge.h"
#include "platforms/common/torirs_nuklear_debug_panel.h"

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "graphics/uv_pnm.h"

extern "C" {
#include "graphics/dash.h"
#include "graphics/shared_tables.h"
#include "osrs/game.h"
#include "platforms/common/platform_memory.h"
#include "tori_rs.h"
#include "tori_rs_render.h"
}

static struct TorirsNkMetalUi* s_mtl_nk;
static Uint64 s_mtl_ui_prev_perf;

// Must match src/osrs/pix3dglcore.u.cpp (used by Pix3DGL / OpenGL3 renderer).
static void
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

static void
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

// OpenGL NDC z is in [-1, 1]; Metal requires z/w in [0, 1] after divide.
// clip_out.z = 0.5 * clip_in.z + 0.5 * clip_in.w; clip_out.w unchanged.
static void
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

// ---------------------------------------------------------------------------
// Vertex layout that matches Shaders.metal (stride 64, 16-byte aligned)
// ---------------------------------------------------------------------------
struct MetalVertex
{
    float position[4]; // xyz + unused w (1.0)
    float color[4];    // r, g, b, a
    float texcoord[2];
    float texBlend;         // 1.0 = sample texture * color (Pix3DGL), 0.0 = color only
    float textureOpaque;    // 1.0 = opaque tex; 0.0 = alpha-tested like GL path
    float textureAnimSpeed; // signed, same encoding as pix3dgl_load_texture
    float _pad_vertex;
};

// ---------------------------------------------------------------------------
// Uniform buffer that matches Shaders.metal
// ---------------------------------------------------------------------------
struct MetalUniforms
{
    float modelViewMatrix[16];
    float projectionMatrix[16];
    float uClock;
    float _pad_uniform[3];
};

// ---------------------------------------------------------------------------
// Viewport helpers (identical to opengl3 renderer)
// ---------------------------------------------------------------------------
struct MTLViewportRect
{
    int x, y, width, height;
};

struct LogicalViewportRect
{
    int x, y, width, height;
};

static LogicalViewportRect
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

// Same as platform_impl2_osx_sdl2_renderer_opengl3.cpp: rect.y is OpenGL
// bottom-left Y (not top-left).
static MTLViewportRect
compute_gl_world_viewport_rect(
    int fb_width,
    int fb_height,
    int win_width,
    int win_height,
    const LogicalViewportRect& lr)
{
    MTLViewportRect rect = { 0, 0, fb_width, fb_height };
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
    rect.y = fb_height - (clamped_top_y + clamped_h); // OpenGL bottom-left Y
    rect.width = clamped_w;
    rect.height = clamped_h;
    return rect;
}

static uintptr_t
model_gpu_cache_key(const struct DashModel* model)
{
    if( !model )
        return 0;
#if UINTPTR_MAX == 0xffffffffu
    uintptr_t key = 2166136261u;
#else
    uintptr_t key = 1469598103934665603ull;
#endif
    const uintptr_t fnv_prime = (uintptr_t)16777619u;
    auto mix_word = [&](uintptr_t word) {
        key ^= word;
        key *= fnv_prime;
    };

    mix_word((uintptr_t)dashmodel_vertices_x_const(model));
    mix_word((uintptr_t)dashmodel_face_indices_a_const(model));
    mix_word((uintptr_t)dashmodel_face_indices_b_const(model));
    mix_word((uintptr_t)dashmodel_face_indices_c_const(model));
    mix_word((uintptr_t)dashmodel_face_count(model));

    struct DashModel* mw = (struct DashModel*)model;
    const bool is_animated = dashmodel_original_vertices_x(mw) && dashmodel_original_vertices_y(mw) &&
                             dashmodel_original_vertices_z(mw) && dashmodel_vertex_count(model) > 0;
    if( is_animated )
    {
        const vertexint_t* vx = dashmodel_vertices_x_const(model);
        const vertexint_t* vy = dashmodel_vertices_y_const(model);
        const vertexint_t* vz = dashmodel_vertices_z_const(model);
        int vc = dashmodel_vertex_count(model);
        for( int i = 0; i < vc; ++i )
        {
            mix_word((uintptr_t)(uint32_t)vx[i]);
            mix_word((uintptr_t)(uint32_t)vy[i]);
            mix_word((uintptr_t)(uint32_t)vz[i]);
        }
    }
    return key;
}

// Same encoding as pix3dgl_load_texture (direction → sign, speed scale).
static float
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

// Append one face (3 vertices) in model draw order. Skips invisible / invalid faces.
// effective_tex_id is -1 for untextured, or a loaded texture id (caller drops missing atlas slots).
static void
append_model_face_vertices(
    const struct DashModel* model,
    int f,
    float world_x,
    float world_y,
    float world_z,
    float cos_yaw,
    float sin_yaw,
    int effective_tex_id,
    float texture_anim_speed,
    bool texture_opaque,
    std::vector<MetalVertex>& out)
{
    const int* face_infos = dashmodel_face_infos_const(model);
    if( face_infos && face_infos[f] == 2 )
        return;
    const hsl16_t* hsl_c_arr = dashmodel_face_colors_c_const(model);
    if( hsl_c_arr && hsl_c_arr[f] == DASHHSL16_HIDDEN )
        return;

    const faceint_t* face_ia = dashmodel_face_indices_a_const(model);
    const faceint_t* face_ib = dashmodel_face_indices_b_const(model);
    const faceint_t* face_ic = dashmodel_face_indices_c_const(model);
    const int ia = face_ia[f];
    const int ib = face_ib[f];
    const int ic = face_ic[f];
    const int vcount = dashmodel_vertex_count(model);
    if( ia < 0 || ia >= vcount || ib < 0 || ib >= vcount || ic < 0 || ic >= vcount )
        return;

    const hsl16_t* hsl_a_arr = dashmodel_face_colors_a_const(model);
    const hsl16_t* hsl_b_arr = dashmodel_face_colors_b_const(model);
    if( !hsl_a_arr || !hsl_b_arr || !hsl_c_arr )
        return;

    int hsl_a = (int)hsl_a_arr[f];
    int hsl_b = (int)hsl_b_arr[f];
    int hsl_c = (int)hsl_c_arr[f];
    int rgb_a, rgb_b, rgb_c;
    if( hsl_c == DASHHSL16_FLAT )
        rgb_a = rgb_b = rgb_c = g_hsl16_to_rgb_table[hsl_a & 65535];
    else
    {
        rgb_a = g_hsl16_to_rgb_table[hsl_a & 65535];
        rgb_b = g_hsl16_to_rgb_table[hsl_b & 65535];
        rgb_c = g_hsl16_to_rgb_table[hsl_c & 65535];
    }

    float face_alpha = 1.0f;
    const alphaint_t* face_alphas = dashmodel_face_alphas_const(model);
    if( face_alphas )
    {
        face_alpha = (float)(0xFF - face_alphas[f]) / 255.0f;
    }

    float u_corner[3] = { 0.0f, 0.0f, 0.0f };
    float v_corner[3] = { 0.0f, 0.0f, 0.0f };

    if( effective_tex_id >= 0 )
    {
        int texture_face_idx = f;
        int tp = 0, tm = 0, tn = 0;
        const faceint_t* ftc = dashmodel_face_texture_coords_const(model);
        const faceint_t* tcp = dashmodel_textured_p_coordinate_const(model);
        const faceint_t* tcm = dashmodel_textured_m_coordinate_const(model);
        const faceint_t* tcn = dashmodel_textured_n_coordinate_const(model);
        if( ftc && ftc[f] != -1 && tcp && tcm && tcn )
        {
            texture_face_idx = ftc[f];
            tp = tcp[texture_face_idx];
            tm = tcm[texture_face_idx];
            tn = tcn[texture_face_idx];
        }
        else
        {
            tp = face_ia[texture_face_idx];
            tm = face_ib[texture_face_idx];
            tn = face_ic[texture_face_idx];
        }

        const vertexint_t* vtx = dashmodel_vertices_x_const(model);
        const vertexint_t* vty = dashmodel_vertices_y_const(model);
        const vertexint_t* vtz = dashmodel_vertices_z_const(model);
        struct UVFaceCoords uv;
        uv_pnm_compute(
            &uv,
            (float)vtx[tp],
            (float)vty[tp],
            (float)vtz[tp],
            (float)vtx[tm],
            (float)vty[tm],
            (float)vtz[tm],
            (float)vtx[tn],
            (float)vty[tn],
            (float)vtz[tn],
            (float)vtx[ia],
            (float)vty[ia],
            (float)vtz[ia],
            (float)vtx[ib],
            (float)vty[ib],
            (float)vtz[ib],
            (float)vtx[ic],
            (float)vty[ic],
            (float)vtz[ic]);
        u_corner[0] = uv.u1;
        u_corner[1] = uv.u2;
        u_corner[2] = uv.u3;
        v_corner[0] = uv.v1;
        v_corner[1] = uv.v2;
        v_corner[2] = uv.v3;
    }

    const float texBlend = effective_tex_id >= 0 ? 1.0f : 0.0f;
    const float opaque_f = texture_opaque ? 1.0f : 0.0f;

    const int verts[3] = { ia, ib, ic };
    const int rgbs[3] = { rgb_a, rgb_b, rgb_c };
    const vertexint_t* vx = dashmodel_vertices_x_const(model);
    const vertexint_t* vy = dashmodel_vertices_y_const(model);
    const vertexint_t* vz = dashmodel_vertices_z_const(model);
    for( int vi = 0; vi < 3; ++vi )
    {
        const int vi_idx = verts[vi];
        float lx = (float)vx[vi_idx];
        float ly = (float)vy[vi_idx];
        float lz = (float)vz[vi_idx];

        MetalVertex mv;
        mv.position[0] = cos_yaw * lx + sin_yaw * lz + world_x;
        mv.position[1] = ly + world_y;
        mv.position[2] = -sin_yaw * lx + cos_yaw * lz + world_z;
        mv.position[3] = 1.0f;

        int rgb = rgbs[vi];
        mv.color[0] = ((rgb >> 16) & 0xFF) / 255.0f;
        mv.color[1] = ((rgb >> 8) & 0xFF) / 255.0f;
        mv.color[2] = (rgb & 0xFF) / 255.0f;
        mv.color[3] = face_alpha;

        mv.texcoord[0] = u_corner[vi];
        mv.texcoord[1] = v_corner[vi];
        mv.texBlend = texBlend;
        mv.textureOpaque = opaque_f;
        mv.textureAnimSpeed = texture_anim_speed;
        mv._pad_vertex = 0.0f;
        out.push_back(mv);
    }
}

// Pre-cache placeholder (we still track load keys, but actual vertex buffers
// are built per-draw with the correct world offset so this is a no-op).
static void
preload_model_key(
    struct Platform2_OSX_SDL2_Renderer_Metal* renderer,
    const struct DashModel* model)
{
    if( !model )
        return;
    uintptr_t key = model_gpu_cache_key(model);
    if( renderer->model_index_by_key.find(key) == renderer->model_index_by_key.end() )
        renderer->model_index_by_key[key] = renderer->next_model_index++;
}

// ---------------------------------------------------------------------------
// Nuklear overlay (Metal)
// ---------------------------------------------------------------------------
static void
render_nuklear_overlay(
    struct Platform2_OSX_SDL2_Renderer_Metal* renderer,
    struct GGame* game,
    id<MTLRenderCommandEncoder> encoder)
{
    if( !s_mtl_nk )
        return;

    double const dt = torirs_nk_ui_frame_delta_sec(&s_mtl_ui_prev_perf);

    TorirsNkDebugPanelParams params = {};
    params.window_title = "Metal Debug";
    params.delta_time_sec = dt;
    params.view_w_cap = 4096;
    params.view_h_cap = 4096;
    params.include_soft3d_extras = 0;
    params.include_gpu_frame_stats = 1;
    params.gpu_model_draws = renderer->debug_model_draws;
    params.gpu_tris = renderer->debug_triangles;

    struct nk_context* nk = torirs_nk_metal_ctx(s_mtl_nk);
    torirs_nk_debug_panel_draw(nk, game, &params);
    torirs_nk_ui_after_frame(nk);
    torirs_nk_metal_resize(s_mtl_nk, renderer->width, renderer->height);
    torirs_nk_metal_render(
        s_mtl_nk,
        (__bridge void*)encoder,
        renderer->width,
        renderer->height,
        NK_ANTI_ALIASING_ON);
}

// ---------------------------------------------------------------------------
// Sync drawable size from SDL Metal view
// ---------------------------------------------------------------------------
static void
sync_drawable_size(struct Platform2_OSX_SDL2_Renderer_Metal* renderer)
{
    if( !renderer || !renderer->metal_view )
        return;

    CAMetalLayer* layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(renderer->metal_view);
    if( !layer )
        return;

    CGSize sz = layer.drawableSize;
    if( sz.width > 0 )
        renderer->width = (int)sz.width;
    if( sz.height > 0 )
        renderer->height = (int)sz.height;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

struct Platform2_OSX_SDL2_Renderer_Metal*
PlatformImpl2_OSX_SDL2_Renderer_Metal_New(
    int width,
    int height)
{
    auto* renderer = new Platform2_OSX_SDL2_Renderer_Metal();
    renderer->mtl_device = nil;
    renderer->mtl_command_queue = nil;
    renderer->mtl_pipeline_state = nil;
    renderer->mtl_ui_sprite_pipeline = nil;
    renderer->mtl_clear_rect_pipeline = nil;
    renderer->mtl_depth_stencil = nil;
    renderer->mtl_uniform_buffer = nil;
    renderer->mtl_sampler_state = nil;
    renderer->mtl_dummy_texture = nil;
    renderer->metal_view = nullptr;
    renderer->platform = nullptr;
    renderer->width = width;
    renderer->height = height;
    renderer->metal_ready = false;
    renderer->debug_model_draws = 0;
    renderer->debug_triangles = 0;
    renderer->next_model_index = 1;
    renderer->mtl_depth_texture = nullptr;
    renderer->depth_texture_width = 0;
    renderer->depth_texture_height = 0;
    renderer->mtl_model_vertex_buf = nullptr;
    renderer->mtl_model_vertex_buf_size = 0;
    renderer->mtl_sprite_quad_buf = nullptr;
    renderer->mtl_font_pipeline = nullptr;
    renderer->mtl_font_vbo = nullptr;
    return renderer;
}

void
PlatformImpl2_OSX_SDL2_Renderer_Metal_Free(struct Platform2_OSX_SDL2_Renderer_Metal* renderer)
{
    if( !renderer )
        return;

    // Release cached world textures
    for( auto& kv : renderer->texture_by_id )
        if( kv.second )
            CFRelease(kv.second);

    // Release cached sprite textures
    for( auto& kv : renderer->sprite_texture_by_ptr )
        if( kv.second )
            CFRelease(kv.second);
    renderer->sprite_texture_by_ptr.clear();

    // Release cached font atlas textures
    for( auto& kv : renderer->font_atlas_textures )
        if( kv.second )
            CFRelease(kv.second);
    renderer->font_atlas_textures.clear();

    if( renderer->mtl_font_vbo )
    {
        CFRelease(renderer->mtl_font_vbo);
        renderer->mtl_font_vbo = nullptr;
    }
    if( renderer->mtl_font_pipeline )
    {
        CFRelease(renderer->mtl_font_pipeline);
        renderer->mtl_font_pipeline = nullptr;
    }

    if( s_mtl_nk )
    {
        torirs_nk_ui_clear_active();
        torirs_nk_metal_shutdown(s_mtl_nk);
        s_mtl_nk = NULL;
    }

    // ARC manages the Metal objects; clear pointers so the bridged refs release
    renderer->texture_anim_speed_by_id.clear();
    renderer->texture_opaque_by_id.clear();

    if( renderer->mtl_model_vertex_buf )
    {
        CFRelease(renderer->mtl_model_vertex_buf);
        renderer->mtl_model_vertex_buf = nullptr;
        renderer->mtl_model_vertex_buf_size = 0;
    }
    if( renderer->mtl_sprite_quad_buf )
    {
        CFRelease(renderer->mtl_sprite_quad_buf);
        renderer->mtl_sprite_quad_buf = nullptr;
    }
    if( renderer->mtl_depth_texture )
    {
        CFRelease(renderer->mtl_depth_texture);
        renderer->mtl_depth_texture = nullptr;
    }
    if( renderer->mtl_dummy_texture )
    {
        CFRelease(renderer->mtl_dummy_texture);
        renderer->mtl_dummy_texture = nullptr;
    }
    if( renderer->mtl_sampler_state )
    {
        CFRelease(renderer->mtl_sampler_state);
        renderer->mtl_sampler_state = nullptr;
    }
    if( renderer->mtl_uniform_buffer )
    {
        CFRelease(renderer->mtl_uniform_buffer);
        renderer->mtl_uniform_buffer = nullptr;
    }
    if( renderer->mtl_depth_stencil )
    {
        CFRelease(renderer->mtl_depth_stencil);
        renderer->mtl_depth_stencil = nullptr;
    }
    if( renderer->mtl_ui_sprite_pipeline )
    {
        CFRelease(renderer->mtl_ui_sprite_pipeline);
        renderer->mtl_ui_sprite_pipeline = nullptr;
    }
    if( renderer->mtl_clear_rect_pipeline )
    {
        CFRelease(renderer->mtl_clear_rect_pipeline);
        renderer->mtl_clear_rect_pipeline = nullptr;
    }
    if( renderer->mtl_pipeline_state )
    {
        CFRelease(renderer->mtl_pipeline_state);
        renderer->mtl_pipeline_state = nullptr;
    }
    if( renderer->mtl_command_queue )
    {
        CFRelease(renderer->mtl_command_queue);
        renderer->mtl_command_queue = nullptr;
    }
    if( renderer->mtl_device )
    {
        CFRelease(renderer->mtl_device);
        renderer->mtl_device = nullptr;
    }

    if( renderer->metal_view )
    {
        SDL_Metal_DestroyView(renderer->metal_view);
        renderer->metal_view = nullptr;
    }

    delete renderer;
}

bool
PlatformImpl2_OSX_SDL2_Renderer_Metal_Init(
    struct Platform2_OSX_SDL2_Renderer_Metal* renderer,
    struct Platform2_OSX_SDL2* platform)
{
    if( !renderer || !platform || !platform->window )
        return false;

    renderer->platform = platform;
    renderer->metal_view = SDL_Metal_CreateView(platform->window);
    if( !renderer->metal_view )
    {
        printf("Metal init failed: SDL_Metal_CreateView returned null: %s\n", SDL_GetError());
        return false;
    }

    CAMetalLayer* layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(renderer->metal_view);
    if( !layer )
    {
        printf("Metal init failed: could not obtain CAMetalLayer\n");
        return false;
    }

    // Disable vsync (display sync) so presents don't wait for vblank.
    // `displaySyncEnabled` is macOS-only and not available on all SDKs, so guard it.
    if( [layer respondsToSelector:@selector(setDisplaySyncEnabled:)] )
        layer.displaySyncEnabled = NO;

    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if( !device )
    {
        printf("Metal init failed: MTLCreateSystemDefaultDevice returned nil\n");
        return false;
    }
    layer.device = device;
    renderer->mtl_device = (__bridge_retained void*)device;

    id<MTLCommandQueue> queue = [device newCommandQueue];
    renderer->mtl_command_queue = (__bridge_retained void*)queue;

    // -----------------------------------------------------------------------
    // Pipeline state — load Shaders.metal compiled library
    // -----------------------------------------------------------------------
    NSError* error = nil;

    // Resolve Shaders.metallib: same directory as this binary (CMake places it there),
    // then cwd, then bundle resources / source-tree fallbacks.
    NSMutableArray<NSString*>* candidates = [NSMutableArray array];
    NSString* exePath = [[NSBundle mainBundle] executablePath];
    if( exePath.length > 0 )
    {
        NSString* exeDir = [exePath stringByDeletingLastPathComponent];
        [candidates addObject:[exeDir stringByAppendingPathComponent:@"Shaders.metallib"]];
    }
    NSString* bundleShader = [[NSBundle mainBundle] pathForResource:@"Shaders" ofType:@"metallib"];
    if( bundleShader.length > 0 )
        [candidates addObject:bundleShader];
    [candidates addObject:@"Shaders.metallib"];
    [candidates addObject:@"../Shaders.metallib"];
    [candidates addObject:@"../src/Shaders.metallib"];
    NSArray<NSString*>* candidatePaths = candidates;

    id<MTLLibrary> shaderLibrary = nil;
    for( NSString* path in candidatePaths )
    {
        if( path.length == 0 )
            continue;
        NSData* data = [NSData dataWithContentsOfFile:path];
        if( !data )
            continue;
        dispatch_data_t dd = dispatch_data_create(
            data.bytes, data.length, dispatch_get_main_queue(), DISPATCH_DATA_DESTRUCTOR_DEFAULT);
        shaderLibrary = [device newLibraryWithData:dd error:&error];
        if( shaderLibrary )
            break;
    }

    if( !shaderLibrary )
    {
        printf(
            "Metal init failed: could not load Shaders.metallib: %s\n",
            error ? error.localizedDescription.UTF8String : "unknown");
        return false;
    }

    id<MTLFunction> vertFn = [shaderLibrary newFunctionWithName:@"vertexShader"];
    id<MTLFunction> fragFn = [shaderLibrary newFunctionWithName:@"fragmentShader"];
    if( !vertFn || !fragFn )
    {
        printf("Metal init failed: shader functions not found in library\n");
        return false;
    }

    // Vertex descriptor matching MetalVertex layout (see Shaders.metal struct Vertex)
    MTLVertexDescriptor* vtxDesc = [[MTLVertexDescriptor alloc] init];
    vtxDesc.attributes[0].format = MTLVertexFormatFloat4;
    vtxDesc.attributes[0].offset = offsetof(MetalVertex, position);
    vtxDesc.attributes[0].bufferIndex = 0;
    vtxDesc.attributes[1].format = MTLVertexFormatFloat4;
    vtxDesc.attributes[1].offset = offsetof(MetalVertex, color);
    vtxDesc.attributes[1].bufferIndex = 0;
    vtxDesc.attributes[2].format = MTLVertexFormatFloat2;
    vtxDesc.attributes[2].offset = offsetof(MetalVertex, texcoord);
    vtxDesc.attributes[2].bufferIndex = 0;
    vtxDesc.attributes[3].format = MTLVertexFormatFloat;
    vtxDesc.attributes[3].offset = offsetof(MetalVertex, texBlend);
    vtxDesc.attributes[3].bufferIndex = 0;
    vtxDesc.attributes[4].format = MTLVertexFormatFloat;
    vtxDesc.attributes[4].offset = offsetof(MetalVertex, textureOpaque);
    vtxDesc.attributes[4].bufferIndex = 0;
    vtxDesc.attributes[5].format = MTLVertexFormatFloat;
    vtxDesc.attributes[5].offset = offsetof(MetalVertex, textureAnimSpeed);
    vtxDesc.attributes[5].bufferIndex = 0;
    vtxDesc.layouts[0].stride = sizeof(MetalVertex);
    vtxDesc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

    MTLRenderPipelineDescriptor* pipeDesc = [[MTLRenderPipelineDescriptor alloc] init];
    pipeDesc.vertexFunction = vertFn;
    pipeDesc.fragmentFunction = fragFn;
    pipeDesc.vertexDescriptor = vtxDesc;
    pipeDesc.colorAttachments[0].pixelFormat = layer.pixelFormat;

    // Enable alpha blending for transparent faces
    pipeDesc.colorAttachments[0].blendingEnabled = YES;
    pipeDesc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    pipeDesc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    pipeDesc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pipeDesc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    pipeDesc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    pipeDesc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pipeDesc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

    id<MTLRenderPipelineState> pipeState = [device newRenderPipelineStateWithDescriptor:pipeDesc
                                                                                  error:&error];
    if( !pipeState )
    {
        printf(
            "Metal init failed: could not create pipeline state: %s\n",
            error ? error.localizedDescription.UTF8String : "unknown");
        return false;
    }
    renderer->mtl_pipeline_state = (__bridge_retained void*)pipeState;

    id<MTLFunction> uiVertFn = [shaderLibrary newFunctionWithName:@"uiSpriteVert"];
    id<MTLFunction> uiFragFn = [shaderLibrary newFunctionWithName:@"uiSpriteFrag"];
    if( uiVertFn && uiFragFn )
    {
        MTLVertexDescriptor* uiVtx = [[MTLVertexDescriptor alloc] init];
        uiVtx.attributes[0].format = MTLVertexFormatFloat2;
        uiVtx.attributes[0].offset = 0;
        uiVtx.attributes[0].bufferIndex = 0;
        uiVtx.attributes[1].format = MTLVertexFormatFloat2;
        uiVtx.attributes[1].offset = 8;
        uiVtx.attributes[1].bufferIndex = 0;
        uiVtx.layouts[0].stride = 16;
        uiVtx.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

        MTLRenderPipelineDescriptor* uiPipeDesc = [[MTLRenderPipelineDescriptor alloc] init];
        uiPipeDesc.vertexFunction = uiVertFn;
        uiPipeDesc.fragmentFunction = uiFragFn;
        uiPipeDesc.vertexDescriptor = uiVtx;
        uiPipeDesc.colorAttachments[0].pixelFormat = layer.pixelFormat;
        uiPipeDesc.colorAttachments[0].blendingEnabled = YES;
        uiPipeDesc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        uiPipeDesc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        uiPipeDesc.colorAttachments[0].destinationRGBBlendFactor =
            MTLBlendFactorOneMinusSourceAlpha;
        uiPipeDesc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        uiPipeDesc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        uiPipeDesc.colorAttachments[0].destinationAlphaBlendFactor =
            MTLBlendFactorOneMinusSourceAlpha;
        uiPipeDesc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

        NSError* uiErr = nil;
        id<MTLRenderPipelineState> uiPipe = [device newRenderPipelineStateWithDescriptor:uiPipeDesc
                                                                                   error:&uiErr];
        if( uiPipe )
            renderer->mtl_ui_sprite_pipeline = (__bridge_retained void*)uiPipe;
        else
            printf(
                "Metal: UI sprite pipeline creation failed: %s\n",
                uiErr ? uiErr.localizedDescription.UTF8String : "unknown");

        id<MTLFunction> clearFragFn = [shaderLibrary newFunctionWithName:@"torirsClearRectFrag"];
        if( uiPipe && clearFragFn )
        {
            MTLRenderPipelineDescriptor* clrDesc = [[MTLRenderPipelineDescriptor alloc] init];
            clrDesc.vertexFunction = uiVertFn;
            clrDesc.fragmentFunction = clearFragFn;
            clrDesc.vertexDescriptor = uiVtx;
            clrDesc.colorAttachments[0].pixelFormat = layer.pixelFormat;
            clrDesc.colorAttachments[0].blendingEnabled = NO;
            clrDesc.depthAttachmentPixelFormat = MTLPixelFormatInvalid;
            NSError* clrErr = nil;
            id<MTLRenderPipelineState> clrPipe =
                [device newRenderPipelineStateWithDescriptor:clrDesc error:&clrErr];
            if( clrPipe )
                renderer->mtl_clear_rect_pipeline = (__bridge_retained void*)clrPipe;
            else
                printf(
                    "Metal: clear rect pipeline creation failed: %s\n",
                    clrErr ? clrErr.localizedDescription.UTF8String : "unknown");
        }
    }

    id<MTLFunction> fontVertFn = [shaderLibrary newFunctionWithName:@"uiFontVert"];
    id<MTLFunction> fontFragFn = [shaderLibrary newFunctionWithName:@"uiFontFrag"];
    if( fontVertFn && fontFragFn )
    {
        MTLVertexDescriptor* fontVtx = [[MTLVertexDescriptor alloc] init];
        fontVtx.attributes[0].format = MTLVertexFormatFloat2;
        fontVtx.attributes[0].offset = 0;
        fontVtx.attributes[0].bufferIndex = 0;
        fontVtx.attributes[1].format = MTLVertexFormatFloat2;
        fontVtx.attributes[1].offset = 8;
        fontVtx.attributes[1].bufferIndex = 0;
        fontVtx.attributes[2].format = MTLVertexFormatFloat4;
        fontVtx.attributes[2].offset = 16;
        fontVtx.attributes[2].bufferIndex = 0;
        fontVtx.layouts[0].stride = 32;
        fontVtx.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

        MTLRenderPipelineDescriptor* fontPipeDesc = [[MTLRenderPipelineDescriptor alloc] init];
        fontPipeDesc.vertexFunction = fontVertFn;
        fontPipeDesc.fragmentFunction = fontFragFn;
        fontPipeDesc.vertexDescriptor = fontVtx;
        fontPipeDesc.colorAttachments[0].pixelFormat = layer.pixelFormat;
        fontPipeDesc.colorAttachments[0].blendingEnabled = YES;
        fontPipeDesc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        fontPipeDesc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        fontPipeDesc.colorAttachments[0].destinationRGBBlendFactor =
            MTLBlendFactorOneMinusSourceAlpha;
        fontPipeDesc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        fontPipeDesc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        fontPipeDesc.colorAttachments[0].destinationAlphaBlendFactor =
            MTLBlendFactorOneMinusSourceAlpha;
        fontPipeDesc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

        NSError* fontErr = nil;
        id<MTLRenderPipelineState> fontPipe =
            [device newRenderPipelineStateWithDescriptor:fontPipeDesc error:&fontErr];
        if( fontPipe )
            renderer->mtl_font_pipeline = (__bridge_retained void*)fontPipe;
        else
            printf(
                "Metal: font pipeline creation failed: %s\n",
                fontErr ? fontErr.localizedDescription.UTF8String : "unknown");
    }

    // Match pix3dgl_new(false, true): z-buffer off → GL_ALWAYS / no depth write
    MTLDepthStencilDescriptor* dsDesc = [[MTLDepthStencilDescriptor alloc] init];
    dsDesc.depthCompareFunction = MTLCompareFunctionAlways;
    dsDesc.depthWriteEnabled = NO;
    id<MTLDepthStencilState> dsState = [device newDepthStencilStateWithDescriptor:dsDesc];
    renderer->mtl_depth_stencil = (__bridge_retained void*)dsState;

    // Uniform buffer
    id<MTLBuffer> unifBuf = [device newBufferWithLength:sizeof(MetalUniforms)
                                                options:MTLResourceStorageModeShared];
    renderer->mtl_uniform_buffer = (__bridge_retained void*)unifBuf;

    MTLSamplerDescriptor* sampDesc = [[MTLSamplerDescriptor alloc] init];
    sampDesc.minFilter = MTLSamplerMinMagFilterLinear;
    sampDesc.magFilter = MTLSamplerMinMagFilterLinear;
    sampDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sampDesc.tAddressMode = MTLSamplerAddressModeRepeat;
    id<MTLSamplerState> sampState = [device newSamplerStateWithDescriptor:sampDesc];
    renderer->mtl_sampler_state = (__bridge_retained void*)sampState;

    MTLTextureDescriptor* dumDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:1
                                                          height:1
                                                       mipmapped:NO];
    dumDesc.usage = MTLTextureUsageShaderRead;
    dumDesc.storageMode = MTLStorageModeShared;
    id<MTLTexture> dumTex = [device newTextureWithDescriptor:dumDesc];
    const uint8_t whiteRgba[4] = { 255, 255, 255, 255 };
    [dumTex replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
              mipmapLevel:0
                withBytes:whiteRgba
              bytesPerRow:4];
    renderer->mtl_dummy_texture = (__bridge_retained void*)dumTex;

    // Sync initial drawable size
    sync_drawable_size(renderer);

    // Model vertex buffer: reusable staging for flush_batch(); grown on demand.
    {
        const size_t initial_bytes = 4u * 1024u * 1024u; // 4 MB
        id<MTLBuffer> mvb = [device newBufferWithLength:(NSUInteger)initial_bytes
                                                options:MTLResourceStorageModeShared];
        renderer->mtl_model_vertex_buf = (__bridge_retained void*)mvb;
        renderer->mtl_model_vertex_buf_size = initial_bytes;
    }

    // Pre-allocate a sprite quad vertex buffer large enough for 4096 sprites per frame.
    // Each sprite occupies one slot: 6 verts × (float2 pos + float2 uv) = 96 bytes.
    // Using per-slot offsets avoids overwriting data before the GPU executes.
    id<MTLBuffer> sqb = [device newBufferWithLength:(NSUInteger)(4096 * 6 * 4 * sizeof(float))
                                            options:MTLResourceStorageModeShared];
    renderer->mtl_sprite_quad_buf = (__bridge_retained void*)sqb;

    // Font vertex buffer: 4096 glyphs × 6 verts × 8 floats (pos2 + uv2 + color4) × 4 bytes
    id<MTLBuffer> fontVbo = [device newBufferWithLength:(NSUInteger)(4096 * 6 * 8 * sizeof(float))
                                                options:MTLResourceStorageModeShared];
    renderer->mtl_font_vbo = (__bridge_retained void*)fontVbo;

    // -----------------------------------------------------------------------
    // Nuklear (Metal)
    // -----------------------------------------------------------------------
    s_mtl_nk = torirs_nk_metal_init(
        (__bridge void*)device, renderer->width, renderer->height, 512 * 1024, 128 * 1024);
    if( !s_mtl_nk )
    {
        printf("Nuklear Metal init failed\n");
        renderer->metal_ready = false;
        return false;
    }
    {
        struct nk_font_atlas* atlas = NULL;
        torirs_nk_metal_font_stash_begin(s_mtl_nk, &atlas);
        nk_font_atlas_add_default(atlas, 13.0f, NULL);
        torirs_nk_metal_font_stash_end(s_mtl_nk);
    }
    torirs_nk_ui_set_active(torirs_nk_metal_ctx(s_mtl_nk), NULL, NULL);
    s_mtl_ui_prev_perf = SDL_GetPerformanceCounter();

    renderer->metal_ready = true;
    return true;
}

void
PlatformImpl2_OSX_SDL2_Renderer_Metal_Render(
    struct Platform2_OSX_SDL2_Renderer_Metal* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    if( !renderer || !renderer->metal_ready || !game || !render_command_buffer ||
        !renderer->platform || !renderer->platform->window )
        return;

    @autoreleasepool
    {
        id<MTLDevice> device = (__bridge id<MTLDevice>)renderer->mtl_device;
        id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)renderer->mtl_command_queue;
        id<MTLRenderPipelineState> pipeState =
            (__bridge id<MTLRenderPipelineState>)renderer->mtl_pipeline_state;
        id<MTLDepthStencilState> dsState =
            (__bridge id<MTLDepthStencilState>)renderer->mtl_depth_stencil;
        id<MTLBuffer> unifBuf = (__bridge id<MTLBuffer>)renderer->mtl_uniform_buffer;

        CAMetalLayer* layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(renderer->metal_view);
        if( !layer )
            return;

        sync_drawable_size(renderer);

        // Resize the depth texture if the drawable size changed
        layer.drawableSize = CGSizeMake(renderer->width, renderer->height);

        id<CAMetalDrawable> drawable = [layer nextDrawable];
        if( !drawable )
            return;

        renderer->debug_model_draws = 0;
        renderer->debug_triangles = 0;

        // -----------------------------------------------------------------------
        // Build render pass descriptor with depth attachment
        // Depth texture is cached and only reallocated when drawable dimensions change.
        // -----------------------------------------------------------------------
        if( !renderer->mtl_depth_texture || renderer->depth_texture_width != renderer->width ||
            renderer->depth_texture_height != renderer->height )
        {
            if( renderer->mtl_depth_texture )
            {
                CFRelease(renderer->mtl_depth_texture);
                renderer->mtl_depth_texture = nullptr;
            }
            MTLTextureDescriptor* depthTexDesc = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                             width:(NSUInteger)renderer->width
                                            height:(NSUInteger)renderer->height
                                         mipmapped:NO];
            depthTexDesc.storageMode = MTLStorageModePrivate;
            depthTexDesc.usage = MTLTextureUsageRenderTarget;
            id<MTLTexture> newDepth = [device newTextureWithDescriptor:depthTexDesc];
            renderer->mtl_depth_texture = (__bridge_retained void*)newDepth;
            renderer->depth_texture_width = renderer->width;
            renderer->depth_texture_height = renderer->height;
        }
        id<MTLTexture> depthTex = (__bridge id<MTLTexture>)renderer->mtl_depth_texture;

        MTLRenderPassDescriptor* rpDesc = [MTLRenderPassDescriptor renderPassDescriptor];
        rpDesc.colorAttachments[0].texture = drawable.texture;
        rpDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
        rpDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
        rpDesc.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
        rpDesc.depthAttachment.texture = depthTex;
        rpDesc.depthAttachment.loadAction = MTLLoadActionClear;
        rpDesc.depthAttachment.storeAction = MTLStoreActionDontCare;
        rpDesc.depthAttachment.clearDepth = 1.0;

        // -----------------------------------------------------------------------
        // Compute viewport rects
        // -----------------------------------------------------------------------
        int win_width = renderer->platform->game_screen_width;
        int win_height = renderer->platform->game_screen_height;
        if( win_width <= 0 || win_height <= 0 )
            SDL_GetWindowSize(renderer->platform->window, &win_width, &win_height);

        const LogicalViewportRect logical_vp =
            compute_logical_viewport_rect(win_width, win_height, game);
        const MTLViewportRect gl_vp = compute_gl_world_viewport_rect(
            renderer->width, renderer->height, win_width, win_height, logical_vp);

        const float projection_width = (float)logical_vp.width;
        const float projection_height = (float)logical_vp.height;

        // Same as pix3dgl_begin_3dframe(..., 0,0,0, camera_pitch, camera_yaw, ...):
        // game angles are OSRS units (2048 = 2*pi); camera position stays at origin.
        MetalUniforms uniforms;
        const float pitch_rad = ((float)game->camera_pitch * 2.0f * (float)M_PI) / 2048.0f;
        const float yaw_rad = ((float)game->camera_yaw * 2.0f * (float)M_PI) / 2048.0f;
        metal_compute_view_matrix(uniforms.modelViewMatrix, 0.0f, 0.0f, 0.0f, pitch_rad, yaw_rad);
        metal_compute_projection_matrix(
            uniforms.projectionMatrix,
            (90.0f * (float)M_PI) / 180.0f,
            projection_width,
            projection_height);
        metal_remap_projection_opengl_to_metal_z(uniforms.projectionMatrix);
        uniforms.uClock = (float)(SDL_GetTicks64() / 20);
        uniforms._pad_uniform[0] = 0.0f;
        uniforms._pad_uniform[1] = 0.0f;
        uniforms._pad_uniform[2] = 0.0f;
        memcpy(unifBuf.contents, &uniforms, sizeof(uniforms));

        // -----------------------------------------------------------------------
        // Command buffer + render encoder
        // -----------------------------------------------------------------------
        id<MTLCommandBuffer> cmdBuf = [queue commandBuffer];
        id<MTLRenderCommandEncoder> encoder = [cmdBuf renderCommandEncoderWithDescriptor:rpDesc];

        [encoder setRenderPipelineState:pipeState];
        [encoder setDepthStencilState:dsState];
        // Metal viewport Y vs GL + column-major clip can invert winding; match painter order like
        // z-off GL.
        [encoder setCullMode:MTLCullModeNone];

        // Metal viewport: top-left origin. gl_vp.y is OpenGL bottom-left Y.
        const double metal_origin_y =
            (double)renderer->height - (double)gl_vp.y - (double)gl_vp.height;
        MTLViewport metalVp = { .originX = (double)gl_vp.x,
                                .originY = metal_origin_y,
                                .width = (double)gl_vp.width,
                                .height = (double)gl_vp.height,
                                .znear = 0.0,
                                .zfar = 1.0 };
        [encoder setViewport:metalVp];

        // -----------------------------------------------------------------------
        // Drain the render command buffer inline (streaming, no pre-cache).
        // FrameNextCommand projects each model (project_models=true); the resulting
        // sys_dash state is consumed immediately by dash3d_prepare_projected_face_order
        // so no second projection is needed.  Sprite draws require a different Metal
        // pipeline state and must come after all model draws, so they are deferred.
        // -----------------------------------------------------------------------
        LibToriRS_FrameBegin(game, render_command_buffer);

        static std::vector<ToriRSRenderCommand> sprite_cmds;
        sprite_cmds.clear();

        // Pass 1 + 2 + 3 collapsed into a single streaming loop.
        static std::vector<MetalVertex> batch_verts;
        batch_verts.clear();
        int batch_tex_id = -0x7fffffff; // sentinel: force first flush path

        id<MTLTexture> dummyTex = (__bridge id<MTLTexture>)renderer->mtl_dummy_texture;
        id<MTLSamplerState> samp = (__bridge id<MTLSamplerState>)renderer->mtl_sampler_state;

        id<MTLBuffer> modelBuf = (__bridge id<MTLBuffer>)renderer->mtl_model_vertex_buf;
        NSUInteger modelBufOffset = 0;

        auto flush_batch = [&]() {
            if( batch_verts.empty() )
                return;
            id<MTLTexture> bindTex = dummyTex;
            if( batch_tex_id >= 0 )
            {
                auto it = renderer->texture_by_id.find(batch_tex_id);
                if( it == renderer->texture_by_id.end() || !it->second )
                {
                    batch_verts.clear();
                    return;
                }
                bindTex = (__bridge id<MTLTexture>)it->second;
            }
            NSUInteger batchBytes = (NSUInteger)(batch_verts.size() * sizeof(MetalVertex));

            // Grow the reusable buffer if the accumulated frame data won't fit.
            if( modelBufOffset + batchBytes > renderer->mtl_model_vertex_buf_size )
            {
                size_t needed = (size_t)(modelBufOffset + batchBytes);
                size_t newSize = renderer->mtl_model_vertex_buf_size;
                while( newSize < needed )
                    newSize *= 2;
                if( renderer->mtl_model_vertex_buf )
                    CFRelease(renderer->mtl_model_vertex_buf);
                id<MTLBuffer> grown = [device newBufferWithLength:(NSUInteger)newSize
                                                          options:MTLResourceStorageModeShared];
                renderer->mtl_model_vertex_buf = (__bridge_retained void*)grown;
                renderer->mtl_model_vertex_buf_size = newSize;
                modelBuf = grown;
            }

            memcpy((char*)modelBuf.contents + modelBufOffset, batch_verts.data(), batchBytes);

            [encoder setFragmentTexture:bindTex atIndex:0];
            [encoder setFragmentSamplerState:samp atIndex:0];
            [encoder setVertexBuffer:modelBuf offset:modelBufOffset atIndex:0];
            [encoder setVertexBuffer:unifBuf offset:0 atIndex:1];
            [encoder setFragmentBuffer:unifBuf offset:0 atIndex:1];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                        vertexStart:0
                        vertexCount:(NSUInteger)batch_verts.size()];
            modelBufOffset += batchBytes;
            renderer->debug_triangles += (unsigned int)(batch_verts.size() / 3);
            batch_verts.clear();
        };

        {
            struct ToriRSRenderCommand cmd = { 0 };
            while( LibToriRS_FrameNextCommand(game, render_command_buffer, &cmd, true) )
            {
                switch( cmd.kind )
                {
                case TORIRS_GFX_TEXTURE_LOAD:
                {
                    const int tex_id = cmd._texture_load.texture_id;
                    struct DashTexture* tex = cmd._texture_load.texture_nullable;
                    if( !tex || !tex->texels )
                        break;

                    if( renderer->texture_by_id.count(tex_id) && renderer->texture_by_id[tex_id] )
                        CFRelease(renderer->texture_by_id[tex_id]);

                    renderer->texture_anim_speed_by_id[tex_id] = metal_texture_animation_signed(
                        tex->animation_direction, tex->animation_speed);
                    renderer->texture_opaque_by_id[tex_id] = tex->opaque;

                    const int w = tex->width;
                    const int h = tex->height;
                    std::vector<uint8_t> rgba((size_t)w * (size_t)h * 4u);
                    for( int p = 0; p < w * h; ++p )
                    {
                        int pix = tex->texels[p];
                        rgba[(size_t)p * 4u + 0] = (uint8_t)((pix >> 16) & 0xFF);
                        rgba[(size_t)p * 4u + 1] = (uint8_t)((pix >> 8) & 0xFF);
                        rgba[(size_t)p * 4u + 2] = (uint8_t)(pix & 0xFF);
                        rgba[(size_t)p * 4u + 3] = (uint8_t)((pix >> 24) & 0xFF);
                    }

                    MTLTextureDescriptor* texDesc = [MTLTextureDescriptor
                        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                     width:(NSUInteger)w
                                                    height:(NSUInteger)h
                                                 mipmapped:NO];
                    texDesc.usage = MTLTextureUsageShaderRead;
                    texDesc.storageMode = MTLStorageModeShared;
                    id<MTLTexture> mtlTex = [device newTextureWithDescriptor:texDesc];
                    [mtlTex replaceRegion:MTLRegionMake2D(0, 0, w, h)
                              mipmapLevel:0
                                withBytes:rgba.data()
                              bytesPerRow:(NSUInteger)w * 4];
                    renderer->texture_by_id[tex_id] = (__bridge_retained void*)mtlTex;
                    break;
                }

                case TORIRS_GFX_MODEL_LOAD:
                {
                    struct DashModel* model = cmd._model_load.model;
                    if( !model || !dashmodel_face_colors_a_const(model) ||
                        !dashmodel_face_colors_b_const(model) || !dashmodel_face_colors_c_const(model) ||
                        !dashmodel_vertices_x_const(model) || !dashmodel_vertices_y_const(model) ||
                        !dashmodel_vertices_z_const(model) || !dashmodel_face_indices_a_const(model) ||
                        !dashmodel_face_indices_b_const(model) || !dashmodel_face_indices_c_const(model) ||
                        dashmodel_face_count(model) <= 0 )
                        break;
                    preload_model_key(renderer, model);
                    break;
                }

                case TORIRS_GFX_CLEAR_RECT:
                {
                    flush_batch();
                    id<MTLRenderPipelineState> clrPipe =
                        renderer->mtl_clear_rect_pipeline
                            ? (__bridge id<MTLRenderPipelineState>)renderer->mtl_clear_rect_pipeline
                            : nil;
                    id<MTLBuffer> quadBuf = (__bridge id<MTLBuffer>)renderer->mtl_sprite_quad_buf;
                    if( !clrPipe || !quadBuf )
                        break;
                    int rx = cmd._clear_rect.x;
                    int ry = cmd._clear_rect.y;
                    int rw = cmd._clear_rect.w;
                    int rh = cmd._clear_rect.h;
                    if( rw <= 0 || rh <= 0 )
                        break;
                    LogicalViewportRect lr = { rx, ry, rw, rh };
                    MTLViewportRect gr =
                        compute_gl_world_viewport_rect(
                            renderer->width, renderer->height, win_width, win_height, lr);
                    const float fbw = (float)(win_width > 0 ? win_width : renderer->width);
                    const float fbh = (float)(win_height > 0 ? win_height : renderer->height);
                    if( fbw <= 0.0f || fbh <= 0.0f )
                        break;
                    auto to_clip = [&](float xp, float yp, float* ocx, float* ocy) {
                        *ocx = 2.0f * xp / fbw - 1.0f;
                        *ocy = 1.0f - 2.0f * yp / fbh;
                    };
                    float c0x, c0y, c1x, c1y, c2x, c2y, c3x, c3y;
                    to_clip((float)rx, (float)ry, &c0x, &c0y);
                    to_clip((float)(rx + rw), (float)ry, &c1x, &c1y);
                    to_clip((float)(rx + rw), (float)(ry + rh), &c2x, &c2y);
                    to_clip((float)rx, (float)(ry + rh), &c3x, &c3y);
                    float verts[6 * 4] = {
                        c0x, c0y, 0.0f, 0.0f, c1x, c1y, 1.0f, 0.0f, c2x, c2y, 1.0f, 1.0f,
                        c0x, c0y, 0.0f, 0.0f, c2x, c2y, 1.0f, 1.0f, c3x, c3y, 0.0f, 1.0f,
                    };
                    memcpy(quadBuf.contents, verts, sizeof(verts));

                    MTLViewport fullVp = { .originX = 0.0,
                                           .originY = 0.0,
                                           .width = (double)renderer->width,
                                           .height = (double)renderer->height,
                                           .znear = 0.0,
                                           .zfar = 1.0 };
                    [encoder setViewport:fullVp];
                    NSUInteger msx = (NSUInteger)gr.x;
                    NSUInteger msy = (NSUInteger)(renderer->height - gr.y - gr.height);
                    MTLScissorRect sc = { msx, msy, (NSUInteger)gr.width, (NSUInteger)gr.height };
                    [encoder setScissorRect:sc];
                    [encoder setRenderPipelineState:clrPipe];
                    [encoder setVertexBuffer:quadBuf offset:0 atIndex:0];
                    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
                    MTLScissorRect scMax = { 0,
                                             0,
                                             (NSUInteger)renderer->width,
                                             (NSUInteger)renderer->height };
                    [encoder setScissorRect:scMax];
                    [encoder setViewport:metalVp];
                    [encoder setRenderPipelineState:pipeState];
                    [encoder setDepthStencilState:dsState];
                    break;
                }

                case TORIRS_GFX_MODEL_DRAW:
                {
                    struct DashModel* model = cmd._model_draw.model;
                    if( !model || !dashmodel_face_colors_a_const(model) ||
                        !dashmodel_face_colors_b_const(model) || !dashmodel_face_colors_c_const(model) ||
                        !dashmodel_vertices_x_const(model) || !dashmodel_vertices_y_const(model) ||
                        !dashmodel_vertices_z_const(model) || !dashmodel_face_indices_a_const(model) ||
                        !dashmodel_face_indices_b_const(model) || !dashmodel_face_indices_c_const(model) ||
                        dashmodel_face_count(model) <= 0 )
                        break;

                    preload_model_key(renderer, model);

                    /* FrameNextCommand already projected this model (project_models=true).
                     * The sys_dash projection state is still valid — use it directly. */
                    struct DashPosition draw_position = cmd._model_draw.position;
                    int face_order_count = dash3d_prepare_projected_face_order(
                        game->sys_dash, model, &draw_position, game->view_port, game->camera);
                    const int* face_order =
                        dash3d_projected_face_order(game->sys_dash, &face_order_count);

                    const float yaw_rad = (draw_position.yaw * 2.0f * (float)M_PI) / 2048.0f;
                    const float cos_yaw = cosf(yaw_rad);
                    const float sin_yaw = sinf(yaw_rad);

                    renderer->debug_model_draws++;
                    for( int fi = 0; fi < face_order_count; ++fi )
                    {
                        const int f = face_order ? face_order[fi] : fi;
                        if( f < 0 || f >= dashmodel_face_count(model) )
                            continue;

                        const faceint_t* ftex = dashmodel_face_textures_const(model);
                        int raw_tex = ftex ? (int)ftex[f] : -1;
                        int eff_tex = raw_tex;
                        if( eff_tex >= 0 &&
                            renderer->texture_by_id.find(eff_tex) == renderer->texture_by_id.end() )
                            eff_tex = -1;

                        float anim_spd = 0.0f;
                        bool tex_opaque = true;
                        if( eff_tex >= 0 )
                        {
                            auto as_it = renderer->texture_anim_speed_by_id.find(eff_tex);
                            if( as_it != renderer->texture_anim_speed_by_id.end() )
                                anim_spd = as_it->second;
                            auto op_it = renderer->texture_opaque_by_id.find(eff_tex);
                            if( op_it != renderer->texture_opaque_by_id.end() )
                                tex_opaque = op_it->second;
                        }

                        const int batch_key = eff_tex;
                        if( !batch_verts.empty() && batch_key != batch_tex_id )
                            flush_batch();
                        batch_tex_id = batch_key;

                        const size_t before = batch_verts.size();
                        append_model_face_vertices(
                            model,
                            f,
                            (float)draw_position.x,
                            (float)draw_position.y,
                            (float)draw_position.z,
                            cos_yaw,
                            sin_yaw,
                            eff_tex,
                            anim_spd,
                            tex_opaque,
                            batch_verts);
                        if( batch_verts.size() == before )
                            continue;
                    }
                    break;
                }

                case TORIRS_GFX_SPRITE_LOAD:
                {
                    struct DashSprite* sp = cmd._sprite_load.sprite;
                    if( !sp || !sp->pixels_argb || sp->width <= 0 || sp->height <= 0 )
                        break;

                    // Release any stale cached texture for this pointer
                    {
                        auto it = renderer->sprite_texture_by_ptr.find(sp);
                        if( it != renderer->sprite_texture_by_ptr.end() && it->second )
                        {
                            CFRelease(it->second);
                            renderer->sprite_texture_by_ptr.erase(it);
                        }
                    }

                    const int sw = sp->width;
                    const int sh = sp->height;
                    // Pixel data is 0x00RRGGBB; pixel == 0 is the transparent color key.
                    std::vector<uint8_t> rgba((size_t)sw * (size_t)sh * 4u);
                    for( int p = 0; p < sw * sh; ++p )
                    {
                        uint32_t pix = sp->pixels_argb[p];
                        rgba[(size_t)p * 4u + 0u] = (uint8_t)((pix >> 16) & 0xFFu);
                        rgba[(size_t)p * 4u + 1u] = (uint8_t)((pix >> 8) & 0xFFu);
                        rgba[(size_t)p * 4u + 2u] = (uint8_t)(pix & 0xFFu);
                        rgba[(size_t)p * 4u + 3u] = (pix != 0u) ? 0xFFu : 0x00u;
                    }
                    MTLTextureDescriptor* td = [MTLTextureDescriptor
                        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                     width:(NSUInteger)sw
                                                    height:(NSUInteger)sh
                                                 mipmapped:NO];
                    td.usage = MTLTextureUsageShaderRead;
                    td.storageMode = MTLStorageModeShared;
                    id<MTLTexture> spTex = [device newTextureWithDescriptor:td];
                    [spTex replaceRegion:MTLRegionMake2D(0, 0, sw, sh)
                             mipmapLevel:0
                               withBytes:rgba.data()
                             bytesPerRow:(NSUInteger)sw * 4u];
                    renderer->sprite_texture_by_ptr[sp] = (__bridge_retained void*)spTex;
                    break;
                }

                case TORIRS_GFX_SPRITE_UNLOAD:
                {
                    struct DashSprite* sp = cmd._sprite_load.sprite;
                    auto it = renderer->sprite_texture_by_ptr.find(sp);
                    if( it != renderer->sprite_texture_by_ptr.end() )
                    {
                        if( it->second )
                            CFRelease(it->second);
                        renderer->sprite_texture_by_ptr.erase(it);
                    }
                    break;
                }

                case TORIRS_GFX_SPRITE_DRAW:
                    sprite_cmds.push_back(cmd);
                    break;

                case TORIRS_GFX_FONT_LOAD:
                {
                    struct DashPixFont* font = cmd._font_load.font;
                    if( font && font->atlas &&
                        renderer->font_atlas_textures.find(font) ==
                            renderer->font_atlas_textures.end() )
                    {
                        struct DashFontAtlas* atlas = font->atlas;
                        MTLTextureDescriptor* td = [MTLTextureDescriptor
                            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                         width:(NSUInteger)atlas->atlas_width
                                                        height:(NSUInteger)atlas->atlas_height
                                                     mipmapped:NO];
                        td.usage = MTLTextureUsageShaderRead;
                        td.storageMode = MTLStorageModeShared;
                        id<MTLTexture> atlasTex = [device newTextureWithDescriptor:td];
                        [atlasTex replaceRegion:MTLRegionMake2D(
                                                    0,
                                                    0,
                                                    (NSUInteger)atlas->atlas_width,
                                                    (NSUInteger)atlas->atlas_height)
                                    mipmapLevel:0
                                      withBytes:atlas->rgba_pixels
                                    bytesPerRow:(NSUInteger)atlas->atlas_width * 4u];
                        renderer->font_atlas_textures[font] = (__bridge_retained void*)atlasTex;
                    }
                    break;
                }

                case TORIRS_GFX_FONT_DRAW:
                    sprite_cmds.push_back(cmd);
                    break;

                default:
                    break;
                }
            }
        }
        flush_batch();

        id<MTLRenderPipelineState> uiPipeState =
            renderer->mtl_ui_sprite_pipeline
                ? (__bridge id<MTLRenderPipelineState>)renderer->mtl_ui_sprite_pipeline
                : nil;
        id<MTLSamplerState> uiSampler = (__bridge id<MTLSamplerState>)renderer->mtl_sampler_state;
        if( uiPipeState )
        {
            MTLViewport spriteVp = { .originX = 0.0,
                                     .originY = 0.0,
                                     .width = (double)renderer->width,
                                     .height = (double)renderer->height,
                                     .znear = 0.0,
                                     .zfar = 1.0 };
            [encoder setViewport:spriteVp];
            [encoder setRenderPipelineState:uiPipeState];
            [encoder setDepthStencilState:dsState];
            [encoder setCullMode:MTLCullModeNone];

            id<MTLBuffer> spriteQuadBuf = (__bridge id<MTLBuffer>)renderer->mtl_sprite_quad_buf;
            // Each sprite occupies a unique slot so the GPU sees distinct data per draw call.
            static const NSUInteger kSpriteSlotBytes = 6 * 4 * sizeof(float); // 96 bytes
            NSUInteger spriteSlot = 0;

            for( const auto& sc : sprite_cmds )
            {
                if( sc.kind == TORIRS_GFX_SPRITE_DRAW )
                {
                    struct DashSprite* sp = sc._sprite_draw.sprite;
                    if( !sp || sp->width <= 0 || sp->height <= 0 )
                        continue;

                    // Look up the pre-uploaded texture — no allocation or pixel conversion.
                    auto texIt = renderer->sprite_texture_by_ptr.find(sp);
                    if( texIt == renderer->sprite_texture_by_ptr.end() || !texIt->second )
                        continue;
                    id<MTLTexture> spriteTex = (__bridge id<MTLTexture>)texIt->second;

                    const int dst_x = sc._sprite_draw.dst_bb_x + sp->crop_x;
                    const int dst_y = sc._sprite_draw.dst_bb_y + sp->crop_y;
                    const int iw =
                        sc._sprite_draw.src_bb_w > 0 ? sc._sprite_draw.src_bb_w : sp->width;
                    const int ih =
                        sc._sprite_draw.src_bb_h > 0 ? sc._sprite_draw.src_bb_h : sp->height;
                    const int ix = sc._sprite_draw.src_bb_x;
                    const int iy = sc._sprite_draw.src_bb_y;
                    if( ix < 0 || iy < 0 || ix + iw > sp->width || iy + ih > sp->height )
                        continue;
                    const float tw = (float)sp->width;
                    const float th = (float)sp->height;
                    const float w = (float)iw;
                    const float h = (float)ih;
                    const float x0 = (float)dst_x;
                    const float y0 = (float)dst_y;
                    const float x1 = x0 + w;
                    const float y1 = y0 + h;
                    const float cx = 0.5f * (x0 + x1);
                    const float cy = 0.5f * (y0 + y1);
                    const float angle =
                        (float)(sc._sprite_draw.rotation_r2pi2048 * (2.0 * M_PI) / 2048.0);
                    const float ca = cosf(angle);
                    const float sa = sinf(angle);
                    const float hw = 0.5f * w;
                    const float hh = 0.5f * h;

                    float px[4];
                    float py[4];
                    auto rot_local = [&](float lx, float ly, int k) {
                        px[k] = cx + ca * lx - sa * ly;
                        py[k] = cy + sa * lx + ca * ly;
                    };
                    rot_local(-hw, -hh, 0);
                    rot_local(hw, -hh, 1);
                    rot_local(hw, hh, 2);
                    rot_local(-hw, hh, 3);

                    const float fbw = (float)(win_width > 0 ? win_width : renderer->width);
                    const float fbh = (float)(win_height > 0 ? win_height : renderer->height);
                    auto to_clip = [&](float xp, float yp, float* ocx, float* ocy) {
                        *ocx = 2.0f * xp / fbw - 1.0f;
                        *ocy = 1.0f - 2.0f * yp / fbh;
                    };

                    float c0x, c0y, c1x, c1y, c2x, c2y, c3x, c3y;
                    to_clip(px[0], py[0], &c0x, &c0y);
                    to_clip(px[1], py[1], &c1x, &c1y);
                    to_clip(px[2], py[2], &c2x, &c2y);
                    to_clip(px[3], py[3], &c3x, &c3y);

                    const float u0 = (float)ix / tw;
                    const float v0 = (float)iy / th;
                    const float u1 = (float)(ix + iw) / tw;
                    const float v1 = (float)(iy + ih) / th;

                    float verts[6 * 4] = {
                        c0x, c0y, u0, v0, c1x, c1y, u1, v0, c2x, c2y, u1, v1,
                        c0x, c0y, u0, v0, c2x, c2y, u1, v1, c3x, c3y, u0, v1,
                    };

                    NSUInteger slotOffset = spriteSlot * kSpriteSlotBytes;
                    memcpy((char*)spriteQuadBuf.contents + slotOffset, verts, sizeof(verts));
                    [encoder setVertexBuffer:spriteQuadBuf offset:slotOffset atIndex:0];
                    [encoder setFragmentTexture:spriteTex atIndex:0];
                    [encoder setFragmentSamplerState:uiSampler atIndex:0];
                    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
                    ++spriteSlot;
                }
            }
        }

        /* Font draws: emit per-glyph quads from atlas textures using the font pipeline. */
        id<MTLRenderPipelineState> fontPipeState =
            renderer->mtl_font_pipeline
                ? (__bridge id<MTLRenderPipelineState>)renderer->mtl_font_pipeline
                : nil;
        if( fontPipeState && renderer->mtl_font_vbo )
        {
            id<MTLBuffer> fontVbo = (__bridge id<MTLBuffer>)renderer->mtl_font_vbo;
            id<MTLSamplerState> fontSampler =
                (__bridge id<MTLSamplerState>)renderer->mtl_sampler_state;

            [encoder setRenderPipelineState:fontPipeState];
            [encoder setDepthStencilState:dsState];
            [encoder setCullMode:MTLCullModeNone];
            MTLViewport fontVp = { .originX = 0.0,
                                   .originY = 0.0,
                                   .width = (double)renderer->width,
                                   .height = (double)renderer->height,
                                   .znear = 0.0,
                                   .zfar = 1.0 };
            [encoder setViewport:fontVp];

            static std::vector<float> font_verts;
            font_verts.clear();

            struct DashPixFont* current_font_ptr = NULL;
            id<MTLTexture> current_atlas_tex = nil;

            const float fbw = (float)(win_width > 0 ? win_width : renderer->width);
            const float fbh = (float)(win_height > 0 ? win_height : renderer->height);

            auto flush_font_batch = [&]() {
                if( font_verts.empty() || !current_atlas_tex )
                    return;
                int vert_count = (int)(font_verts.size() / 8);
                NSUInteger byte_count = font_verts.size() * sizeof(float);
                if( byte_count > fontVbo.length )
                    return;
                memcpy(fontVbo.contents, font_verts.data(), byte_count);
                [encoder setVertexBuffer:fontVbo offset:0 atIndex:0];
                [encoder setFragmentTexture:current_atlas_tex atIndex:0];
                [encoder setFragmentSamplerState:fontSampler atIndex:0];
                [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                            vertexStart:0
                            vertexCount:(NSUInteger)vert_count];
                font_verts.clear();
            };

            for( const auto& sc : sprite_cmds )
            {
                if( sc.kind != TORIRS_GFX_FONT_DRAW )
                    continue;
                struct DashPixFont* f = sc._font_draw.font;
                if( !f || !f->atlas || !sc._font_draw.text || f->height2d <= 0 )
                    continue;

                auto texIt = renderer->font_atlas_textures.find(f);
                if( texIt == renderer->font_atlas_textures.end() || !texIt->second )
                    continue;

                if( current_font_ptr != f )
                {
                    flush_font_batch();
                    current_font_ptr = f;
                    current_atlas_tex = (__bridge id<MTLTexture>)texIt->second;
                }

                struct DashFontAtlas* atlas = f->atlas;
                const float inv_aw = 1.0f / (float)atlas->atlas_width;
                const float inv_ah = 1.0f / (float)atlas->atlas_height;

                const uint8_t* text = sc._font_draw.text;
                int length = (int)strlen((const char*)text);
                int color_rgb = sc._font_draw.color_rgb;
                float cr = (float)((color_rgb >> 16) & 0xFF) / 255.0f;
                float cg = (float)((color_rgb >> 8) & 0xFF) / 255.0f;
                float cb = (float)(color_rgb & 0xFF) / 255.0f;
                float ca = 1.0f;
                int pen_x = sc._font_draw.x;
                int base_y = sc._font_draw.y;

                for( int i = 0; i < length; i++ )
                {
                    if( text[i] == '@' && i + 5 <= length && text[i + 4] == '@' )
                    {
                        int new_color = dashfont_evaluate_color_tag((const char*)&text[i + 1]);
                        if( new_color >= 0 )
                        {
                            color_rgb = new_color;
                            cr = (float)((color_rgb >> 16) & 0xFF) / 255.0f;
                            cg = (float)((color_rgb >> 8) & 0xFF) / 255.0f;
                            cb = (float)(color_rgb & 0xFF) / 255.0f;
                        }
                        if( i + 6 <= length && text[i + 5] == ' ' )
                            i += 5;
                        else
                            i += 4;
                        continue;
                    }

                    int c = dashfont_charcode_to_glyph(text[i]);
                    if( c < DASH_FONT_CHAR_COUNT )
                    {
                        int gw = atlas->glyph_w[c];
                        int gh = atlas->glyph_h[c];
                        if( gw > 0 && gh > 0 )
                        {
                            float sx0 = (float)(pen_x + f->char_offset_x[c]);
                            float sy0 = (float)(base_y + f->char_offset_y[c]);
                            float sx1 = sx0 + (float)gw;
                            float sy1 = sy0 + (float)gh;

                            float u0 = (float)atlas->glyph_x[c] * inv_aw;
                            float v0 = (float)atlas->glyph_y[c] * inv_ah;
                            float u1 = (float)(atlas->glyph_x[c] + gw) * inv_aw;
                            float v1 = (float)(atlas->glyph_y[c] + gh) * inv_ah;

                            float cx0 = 2.0f * sx0 / fbw - 1.0f;
                            float cy0 = 1.0f - 2.0f * sy0 / fbh;
                            float cx1 = 2.0f * sx1 / fbw - 1.0f;
                            float cy1 = 1.0f - 2.0f * sy1 / fbh;

                            float v[6 * 8] = {
                                cx0, cy0, u0, v0, cr, cg, cb, ca, cx1, cy0, u1, v0, cr, cg, cb, ca,
                                cx1, cy1, u1, v1, cr, cg, cb, ca, cx0, cy0, u0, v0, cr, cg, cb, ca,
                                cx1, cy1, u1, v1, cr, cg, cb, ca, cx0, cy1, u0, v1, cr, cg, cb, ca,
                            };
                            font_verts.insert(font_verts.end(), v, v + 48);
                        }
                    }
                    int adv = f->char_advance[c];
                    if( adv <= 0 )
                        adv = 4;
                    pen_x += adv;
                }
            }
            flush_font_batch();
        }

        // -----------------------------------------------------------------------
        // Finish scene pass, render Nuklear overlay in the same render encoder
        // -----------------------------------------------------------------------
        MTLViewport fullVp = { .originX = 0.0,
                               .originY = 0.0,
                               .width = (double)renderer->width,
                               .height = (double)renderer->height,
                               .znear = 0.0,
                               .zfar = 1.0 };
        [encoder setViewport:fullVp];

        render_nuklear_overlay(renderer, game, encoder);

        LibToriRS_FrameEnd(game);

        [encoder endEncoding];
        [cmdBuf presentDrawable:drawable];
        [cmdBuf commit];

    } // @autoreleasepool
}
