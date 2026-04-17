#include "platform_impl2_osx_sdl2_renderer_d3d11.h"

#include "nuklear/torirs_nuklear.h"

#include "platforms/common/torirs_nk_ui_bridge.h"
#include "platforms/common/torirs_nuklear_debug_panel.h"

#include <SDL.h>
#include <SDL_syswm.h>

extern "C" {
#include "tori_rs.h"
}

#define NK_D3D11_IMPLEMENTATION
#include "nuklear/backends/d3d11/nuklear_d3d11.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static struct nk_context* s_d3d11_nk;
static Uint64 s_d3d11_ui_prev_perf;

#include "graphics/uv_pnm.h"

extern "C" {
#include "graphics/dash.h"
#include "graphics/shared_tables.h"
#include "osrs/game.h"
#include "tori_rs_render.h"
}

// ---------------------------------------------------------------------------
// Vertex layout matching Metal's MetalVertex (stride 64 bytes, 16-byte aligned)
// ---------------------------------------------------------------------------
struct D3D11Vertex
{
    float position[4]; // xyz + w=1
    float color[4];    // rgba
    float texcoord[2]; // uv
    float texBlend;
    float textureOpaque;
    float textureAnimSpeed;
    float _pad;
};

// ---------------------------------------------------------------------------
// Constant buffer matching Metal's MetalUniforms
// ---------------------------------------------------------------------------
struct D3D11Uniforms
{
    float modelViewMatrix[16];
    float projectionMatrix[16];
    float uClock;
    float _pad[3];
};

// ---------------------------------------------------------------------------
// HLSL shaders embedded as string literals
// ---------------------------------------------------------------------------

static const char* g_world_vs = R"(
cbuffer Uniforms : register(b0)
{
    float4x4 modelViewMatrix;
    float4x4 projectionMatrix;
    float uClock;
    float3 _pad;
};

struct VS_INPUT
{
    float4 position : POSITION;
    float4 color : COLOR;
    float2 texcoord : TEXCOORD0;
    float texBlend : TEXCOORD1;
    float textureOpaque : TEXCOORD2;
    float textureAnimSpeed : TEXCOORD3;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 texcoord : TEXCOORD0;
    float texBlend : TEXCOORD1;
    float textureOpaque : TEXCOORD2;
    float textureAnimSpeed : TEXCOORD3;
};

PS_INPUT main(VS_INPUT input)
{
    PS_INPUT output;
    float4 pos = float4(input.position.xyz, 1.0);
    output.position = mul(projectionMatrix, mul(modelViewMatrix, pos));
    output.color = input.color;
    output.texcoord = input.texcoord;
    output.texBlend = input.texBlend;
    output.textureOpaque = input.textureOpaque;
    output.textureAnimSpeed = input.textureAnimSpeed;
    return output;
}
)";

static const char* g_world_ps = R"(
cbuffer Uniforms : register(b0)
{
    float4x4 modelViewMatrix;
    float4x4 projectionMatrix;
    float uClock;
    float3 _pad;
};

Texture2D tex : register(t0);
SamplerState samp : register(s0);

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 texcoord : TEXCOORD0;
    float texBlend : TEXCOORD1;
    float textureOpaque : TEXCOORD2;
    float textureAnimSpeed : TEXCOORD3;
};

float4 main(PS_INPUT input) : SV_Target
{
    float2 localUV = input.texcoord;
    if (input.texBlend > 0.5)
    {
        if (input.textureAnimSpeed > 0.0)
            localUV.x = localUV.x + (uClock * input.textureAnimSpeed);
        else if (input.textureAnimSpeed < 0.0)
            localUV.y = localUV.y - (uClock * (-input.textureAnimSpeed));
        localUV.x = clamp(localUV.x, 0.008, 0.992);
        localUV.y = clamp(frac(localUV.y), 0.008, 0.992);
    }

    float4 texColor = tex.Sample(samp, localUV);
    float3 finalColor = lerp(input.color.rgb, texColor.rgb * input.color.rgb, input.texBlend);
    float finalAlpha = input.color.a;

    if (input.texBlend > 0.5)
    {
        if (input.textureOpaque < 0.5)
        {
            if (texColor.a < 0.5)
                discard;
        }
    }

    return float4(finalColor, finalAlpha);
}
)";

static const char* g_sprite_vs = R"(
struct VS_INPUT
{
    float2 pos : POSITION;
    float2 uv : TEXCOORD0;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

PS_INPUT main(VS_INPUT input)
{
    PS_INPUT output;
    output.position = float4(input.pos, 0.0, 1.0);
    output.uv = input.uv;
    return output;
}
)";

static const char* g_sprite_ps = R"(
Texture2D tex : register(t0);
SamplerState samp : register(s0);

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_Target
{
    float4 c = tex.Sample(samp, input.uv);
    if (c.a < 0.01)
        discard;
    return c;
}
)";

static const char* g_font_vs = R"(
struct VS_INPUT
{
    float2 pos : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
};

PS_INPUT main(VS_INPUT input)
{
    PS_INPUT output;
    output.position = float4(input.pos, 0.0, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}
)";

static const char* g_font_ps = R"(
Texture2D tex : register(t0);
SamplerState samp : register(s0);

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
};

float4 main(PS_INPUT input) : SV_Target
{
    float a = tex.Sample(samp, input.uv).a;
    if (a < 0.01)
        discard;
    return float4(input.color.rgb, a * input.color.a);
}
)";

// ---------------------------------------------------------------------------
// Shader compilation helper
// ---------------------------------------------------------------------------
static bool
compile_shader(
    const char* source,
    const char* entry,
    const char* target,
    ID3DBlob** blob_out)
{
    ID3DBlob* error_blob = nullptr;
    HRESULT hr = D3DCompile(
        source,
        strlen(source),
        nullptr,
        nullptr,
        nullptr,
        entry,
        target,
        0,
        0,
        blob_out,
        &error_blob);
    if( FAILED(hr) )
    {
        if( error_blob )
        {
            printf("Shader compile error: %s\n", (const char*)error_blob->GetBufferPointer());
            error_blob->Release();
        }
        return false;
    }
    if( error_blob )
        error_blob->Release();
    return true;
}

// ---------------------------------------------------------------------------
// Matrix helpers (must match Metal / pix3dglcore.u.cpp)
// ---------------------------------------------------------------------------
static void
d3d11_compute_view_matrix(
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
d3d11_compute_projection_matrix(
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

// D3D11 NDC z is [0,1] like Metal, so apply same remapping from OpenGL [-1,1]
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

static void
d3d11_remap_projection_z(float* proj_colmajor)
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
// Viewport helpers (identical to Metal/OpenGL3 renderers)
// ---------------------------------------------------------------------------
struct D3D11ViewportRect
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

static D3D11ViewportRect
compute_d3d11_world_viewport_rect(
    int fb_width,
    int fb_height,
    int win_width,
    int win_height,
    const LogicalViewportRect& lr)
{
    D3D11ViewportRect rect = { 0, 0, fb_width, fb_height };
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

    // D3D11 uses top-left origin (same as Metal after conversion)
    rect.x = clamped_x;
    rect.y = clamped_top_y;
    rect.width = clamped_w;
    rect.height = clamped_h;
    return rect;
}

// ---------------------------------------------------------------------------
// Model helpers (identical to Metal renderer)
// ---------------------------------------------------------------------------
static uint64_t
model_gpu_cache_key(const struct DashModel* model)
{
    if( !model )
        return 0;
    uint64_t key = 14695981039346656037ULL;
    const uint64_t fnv_prime = 1099511628211ULL;
    auto mix_word = [&](uint64_t word) {
        key ^= word;
        key *= fnv_prime;
    };

    mix_word((uint64_t)(uintptr_t)dashmodel_vertices_x_const(model));
    mix_word((uint64_t)(uintptr_t)dashmodel_face_indices_a_const(model));
    mix_word((uint64_t)(uintptr_t)dashmodel_face_indices_b_const(model));
    mix_word((uint64_t)(uintptr_t)dashmodel_face_indices_c_const(model));
    mix_word((uint64_t)dashmodel_face_count(model));

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
            mix_word((uint64_t)(uint32_t)vx[i]);
            mix_word((uint64_t)(uint32_t)vy[i]);
            mix_word((uint64_t)(uint32_t)vz[i]);
        }
    }
    return key;
}

static float
d3d11_texture_animation_signed(
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
    std::vector<D3D11Vertex>& out)
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

        D3D11Vertex mv;
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
        mv._pad = 0.0f;
        out.push_back(mv);
    }
}

static void
preload_model_key(
    struct Platform2_OSX_SDL2_Renderer_D3D11* renderer,
    const struct DashModel* model)
{
    if( !model )
        return;
    uint64_t key = model_gpu_cache_key(model);
    renderer->loaded_model_keys.insert(key);
    if( renderer->model_index_by_key.find(key) == renderer->model_index_by_key.end() )
        renderer->model_index_by_key[key] = renderer->next_model_index++;
}

// ---------------------------------------------------------------------------
// Sync drawable size from SDL window
// ---------------------------------------------------------------------------
static void
sync_drawable_size(struct Platform2_OSX_SDL2_Renderer_D3D11* renderer)
{
    if( !renderer || !renderer->platform || !renderer->platform->window )
        return;

    int w = 0, h = 0;
    SDL_GetWindowSize(renderer->platform->window, &w, &h);
    if( w > 0 && h > 0 )
    {
        renderer->width = w;
        renderer->height = h;
    }
}

// ---------------------------------------------------------------------------
// Resize swap chain buffers when window size changes
// ---------------------------------------------------------------------------
static bool
d3d11_resize_buffers(struct Platform2_OSX_SDL2_Renderer_D3D11* renderer)
{
    if( !renderer->swap_chain || !renderer->device )
        return false;

    if( renderer->render_target_view )
    {
        renderer->render_target_view->Release();
        renderer->render_target_view = nullptr;
    }
    if( renderer->depth_stencil_view )
    {
        renderer->depth_stencil_view->Release();
        renderer->depth_stencil_view = nullptr;
    }
    if( renderer->depth_stencil_buffer )
    {
        renderer->depth_stencil_buffer->Release();
        renderer->depth_stencil_buffer = nullptr;
    }

    HRESULT hr = renderer->swap_chain->ResizeBuffers(
        0, (UINT)renderer->width, (UINT)renderer->height, DXGI_FORMAT_UNKNOWN, 0);
    if( FAILED(hr) )
        return false;

    ID3D11Texture2D* back_buffer = nullptr;
    hr = renderer->swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back_buffer);
    if( FAILED(hr) || !back_buffer )
        return false;

    hr = renderer->device->CreateRenderTargetView(
        back_buffer, nullptr, &renderer->render_target_view);
    back_buffer->Release();
    if( FAILED(hr) )
        return false;

    D3D11_TEXTURE2D_DESC depth_desc = {};
    depth_desc.Width = (UINT)renderer->width;
    depth_desc.Height = (UINT)renderer->height;
    depth_desc.MipLevels = 1;
    depth_desc.ArraySize = 1;
    depth_desc.Format = DXGI_FORMAT_D32_FLOAT;
    depth_desc.SampleDesc.Count = 1;
    depth_desc.Usage = D3D11_USAGE_DEFAULT;
    depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    hr = renderer->device->CreateTexture2D(&depth_desc, nullptr, &renderer->depth_stencil_buffer);
    if( FAILED(hr) )
        return false;

    hr = renderer->device->CreateDepthStencilView(
        renderer->depth_stencil_buffer, nullptr, &renderer->depth_stencil_view);
    if( FAILED(hr) )
        return false;

    return true;
}

// ---------------------------------------------------------------------------
// Nuklear overlay (nk_d3d11)
// ---------------------------------------------------------------------------
static void
render_nuklear_overlay(
    struct Platform2_OSX_SDL2_Renderer_D3D11* renderer,
    struct GGame* game)
{
    if( !s_d3d11_nk || !renderer || !game )
        return;

    double const dt = torirs_nk_ui_frame_delta_sec(&s_d3d11_ui_prev_perf);

    TorirsNkDebugPanelParams params = {};
    params.window_title = "D3D11 Debug";
    params.delta_time_sec = dt;
    params.view_w_cap = 4096;
    params.view_h_cap = 4096;
    params.sdl_window = renderer->platform ? renderer->platform->window : nullptr;
    params.soft3d = nullptr;
    params.include_soft3d_extras = 0;
    params.include_load_counts = 1;
    params.loaded_models = renderer->loaded_model_keys.size();
    params.loaded_scenes = renderer->loaded_scene_element_keys.size();
    params.loaded_textures = renderer->loaded_texture_ids.size();
    params.include_gpu_frame_stats = 1;
    params.gpu_model_draws = renderer->debug_model_draws;
    params.gpu_tris = renderer->debug_triangles;

    torirs_nk_debug_panel_draw(s_d3d11_nk, game, &params);
    torirs_nk_ui_after_frame(s_d3d11_nk);
    nk_d3d11_render(renderer->device_context, NK_ANTI_ALIASING_ON);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

struct Platform2_OSX_SDL2_Renderer_D3D11*
PlatformImpl2_OSX_SDL2_Renderer_D3D11_New(
    int width,
    int height)
{
    auto* renderer = new Platform2_OSX_SDL2_Renderer_D3D11();
    renderer->device = nullptr;
    renderer->device_context = nullptr;
    renderer->swap_chain = nullptr;
    renderer->render_target_view = nullptr;
    renderer->depth_stencil_view = nullptr;
    renderer->depth_stencil_buffer = nullptr;
    renderer->depth_stencil_state = nullptr;
    renderer->rasterizer_state = nullptr;
    renderer->blend_state = nullptr;
    renderer->sampler_state = nullptr;
    renderer->constant_buffer = nullptr;
    renderer->world_vs = nullptr;
    renderer->world_ps = nullptr;
    renderer->world_input_layout = nullptr;
    renderer->sprite_vs = nullptr;
    renderer->sprite_ps = nullptr;
    renderer->sprite_input_layout = nullptr;
    renderer->font_vs = nullptr;
    renderer->font_ps = nullptr;
    renderer->font_input_layout = nullptr;
    renderer->dummy_texture = nullptr;
    renderer->dummy_srv = nullptr;
    renderer->platform = nullptr;
    renderer->width = width;
    renderer->height = height;
    renderer->d3d11_ready = false;
    renderer->debug_model_draws = 0;
    renderer->debug_triangles = 0;
    renderer->next_model_index = 1;
    return renderer;
}

void
PlatformImpl2_OSX_SDL2_Renderer_D3D11_Free(struct Platform2_OSX_SDL2_Renderer_D3D11* renderer)
{
    if( !renderer )
        return;

    for( auto& kv : renderer->texture_by_id )
        if( kv.second )
            kv.second->Release();
    for( auto& kv : renderer->texture_srv_by_id )
        if( kv.second )
            kv.second->Release();

    for( auto& kv : renderer->sprite_texture_by_ptr )
        if( kv.second )
            kv.second->Release();
    for( auto& kv : renderer->sprite_srv_by_ptr )
        if( kv.second )
            kv.second->Release();

    for( auto& kv : renderer->font_atlas_cache )
    {
        if( kv.second.texture )
            kv.second.texture->Release();
        if( kv.second.srv )
            kv.second.srv->Release();
    }

    if( s_d3d11_nk )
    {
        torirs_nk_ui_clear_active();
        nk_d3d11_shutdown();
        s_d3d11_nk = nullptr;
    }

    if( renderer->dummy_srv )
        renderer->dummy_srv->Release();
    if( renderer->dummy_texture )
        renderer->dummy_texture->Release();
    if( renderer->font_input_layout )
        renderer->font_input_layout->Release();
    if( renderer->font_ps )
        renderer->font_ps->Release();
    if( renderer->font_vs )
        renderer->font_vs->Release();
    if( renderer->sprite_input_layout )
        renderer->sprite_input_layout->Release();
    if( renderer->sprite_ps )
        renderer->sprite_ps->Release();
    if( renderer->sprite_vs )
        renderer->sprite_vs->Release();
    if( renderer->world_input_layout )
        renderer->world_input_layout->Release();
    if( renderer->world_ps )
        renderer->world_ps->Release();
    if( renderer->world_vs )
        renderer->world_vs->Release();
    if( renderer->constant_buffer )
        renderer->constant_buffer->Release();
    if( renderer->sampler_state )
        renderer->sampler_state->Release();
    if( renderer->blend_state )
        renderer->blend_state->Release();
    if( renderer->rasterizer_state )
        renderer->rasterizer_state->Release();
    if( renderer->depth_stencil_state )
        renderer->depth_stencil_state->Release();
    if( renderer->depth_stencil_view )
        renderer->depth_stencil_view->Release();
    if( renderer->depth_stencil_buffer )
        renderer->depth_stencil_buffer->Release();
    if( renderer->render_target_view )
        renderer->render_target_view->Release();
    if( renderer->swap_chain )
        renderer->swap_chain->Release();
    if( renderer->device_context )
        renderer->device_context->Release();
    if( renderer->device )
        renderer->device->Release();

    delete renderer;
}

bool
PlatformImpl2_OSX_SDL2_Renderer_D3D11_Init(
    struct Platform2_OSX_SDL2_Renderer_D3D11* renderer,
    struct Platform2_OSX_SDL2* platform)
{
    if( !renderer || !platform || !platform->window )
        return false;

    renderer->platform = platform;

    SDL_SysWMinfo wm_info;
    SDL_VERSION(&wm_info.version);
    if( !SDL_GetWindowWMInfo(platform->window, &wm_info) )
    {
        printf("D3D11 init failed: SDL_GetWindowWMInfo failed: %s\n", SDL_GetError());
        return false;
    }
    HWND hwnd = wm_info.info.win.window;

    sync_drawable_size(renderer);

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = (UINT)renderer->width;
    sd.BufferDesc.Height = (UINT)renderer->height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 0;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT create_flags = 0;
#if defined(_DEBUG)
    create_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL feature_level;
    D3D_FEATURE_LEVEL feature_levels[] = { D3D_FEATURE_LEVEL_11_0,
                                           D3D_FEATURE_LEVEL_10_1,
                                           D3D_FEATURE_LEVEL_10_0 };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        create_flags,
        feature_levels,
        3,
        D3D11_SDK_VERSION,
        &sd,
        &renderer->swap_chain,
        &renderer->device,
        &feature_level,
        &renderer->device_context);
    if( FAILED(hr) )
    {
        printf("D3D11 init failed: D3D11CreateDeviceAndSwapChain returned 0x%08lx\n", hr);
        return false;
    }

    if( !d3d11_resize_buffers(renderer) )
    {
        printf("D3D11 init failed: could not create render target / depth buffer\n");
        return false;
    }

    // Depth stencil state: always pass (matches pix3dgl_new(false, true))
    {
        D3D11_DEPTH_STENCIL_DESC ds_desc = {};
        ds_desc.DepthEnable = TRUE;
        ds_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        ds_desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
        ds_desc.StencilEnable = FALSE;
        hr = renderer->device->CreateDepthStencilState(&ds_desc, &renderer->depth_stencil_state);
        if( FAILED(hr) )
            return false;
    }

    // Rasterizer state: no cull, fill solid
    {
        D3D11_RASTERIZER_DESC rs_desc = {};
        rs_desc.FillMode = D3D11_FILL_SOLID;
        rs_desc.CullMode = D3D11_CULL_NONE;
        rs_desc.FrontCounterClockwise = FALSE;
        rs_desc.DepthClipEnable = TRUE;
        rs_desc.ScissorEnable = FALSE;
        hr = renderer->device->CreateRasterizerState(&rs_desc, &renderer->rasterizer_state);
        if( FAILED(hr) )
            return false;
    }

    // Blend state: alpha blending
    {
        D3D11_BLEND_DESC blend_desc = {};
        blend_desc.RenderTarget[0].BlendEnable = TRUE;
        blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        hr = renderer->device->CreateBlendState(&blend_desc, &renderer->blend_state);
        if( FAILED(hr) )
            return false;
    }

    // Sampler state: linear filter, clamp U / repeat V (matches Metal/GL)
    {
        D3D11_SAMPLER_DESC samp_desc = {};
        samp_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samp_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samp_desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        samp_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samp_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        samp_desc.MinLOD = 0;
        samp_desc.MaxLOD = D3D11_FLOAT32_MAX;
        hr = renderer->device->CreateSamplerState(&samp_desc, &renderer->sampler_state);
        if( FAILED(hr) )
            return false;
    }

    // Constant buffer
    {
        D3D11_BUFFER_DESC cb_desc = {};
        cb_desc.ByteWidth = sizeof(D3D11Uniforms);
        cb_desc.Usage = D3D11_USAGE_DYNAMIC;
        cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = renderer->device->CreateBuffer(&cb_desc, nullptr, &renderer->constant_buffer);
        if( FAILED(hr) )
            return false;
    }

    // Compile world shaders
    {
        ID3DBlob* vs_blob = nullptr;
        ID3DBlob* ps_blob = nullptr;
        if( !compile_shader(g_world_vs, "main", "vs_4_0", &vs_blob) )
            return false;
        if( !compile_shader(g_world_ps, "main", "ps_4_0", &ps_blob) )
        {
            vs_blob->Release();
            return false;
        }
        renderer->device->CreateVertexShader(
            vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &renderer->world_vs);
        renderer->device->CreatePixelShader(
            ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &renderer->world_ps);

        D3D11_INPUT_ELEMENT_DESC world_layout[] = {
            { "POSITION",
             0, DXGI_FORMAT_R32G32B32A32_FLOAT,
             0, offsetof(D3D11Vertex, position),
             D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",
             0, DXGI_FORMAT_R32G32B32A32_FLOAT,
             0, offsetof(D3D11Vertex, color),
             D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD",
             0, DXGI_FORMAT_R32G32_FLOAT,
             0, offsetof(D3D11Vertex, texcoord),
             D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD",
             1, DXGI_FORMAT_R32_FLOAT,
             0, offsetof(D3D11Vertex, texBlend),
             D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD",
             2, DXGI_FORMAT_R32_FLOAT,
             0, offsetof(D3D11Vertex, textureOpaque),
             D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD",
             3, DXGI_FORMAT_R32_FLOAT,
             0, offsetof(D3D11Vertex, textureAnimSpeed),
             D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        renderer->device->CreateInputLayout(
            world_layout,
            _countof(world_layout),
            vs_blob->GetBufferPointer(),
            vs_blob->GetBufferSize(),
            &renderer->world_input_layout);
        vs_blob->Release();
        ps_blob->Release();
    }

    // Compile sprite shaders
    {
        ID3DBlob* vs_blob = nullptr;
        ID3DBlob* ps_blob = nullptr;
        if( !compile_shader(g_sprite_vs, "main", "vs_4_0", &vs_blob) )
            return false;
        if( !compile_shader(g_sprite_ps, "main", "ps_4_0", &ps_blob) )
        {
            vs_blob->Release();
            return false;
        }
        renderer->device->CreateVertexShader(
            vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &renderer->sprite_vs);
        renderer->device->CreatePixelShader(
            ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &renderer->sprite_ps);

        D3D11_INPUT_ELEMENT_DESC sprite_layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        renderer->device->CreateInputLayout(
            sprite_layout,
            _countof(sprite_layout),
            vs_blob->GetBufferPointer(),
            vs_blob->GetBufferSize(),
            &renderer->sprite_input_layout);
        vs_blob->Release();
        ps_blob->Release();
    }

    // Compile font shaders
    {
        ID3DBlob* vs_blob = nullptr;
        ID3DBlob* ps_blob = nullptr;
        if( !compile_shader(g_font_vs, "main", "vs_4_0", &vs_blob) )
            return false;
        if( !compile_shader(g_font_ps, "main", "ps_4_0", &ps_blob) )
        {
            vs_blob->Release();
            return false;
        }
        renderer->device->CreateVertexShader(
            vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &renderer->font_vs);
        renderer->device->CreatePixelShader(
            ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &renderer->font_ps);

        D3D11_INPUT_ELEMENT_DESC font_layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 8,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        renderer->device->CreateInputLayout(
            font_layout,
            _countof(font_layout),
            vs_blob->GetBufferPointer(),
            vs_blob->GetBufferSize(),
            &renderer->font_input_layout);
        vs_blob->Release();
        ps_blob->Release();
    }

    // 1x1 white dummy texture
    {
        D3D11_TEXTURE2D_DESC tex_desc = {};
        tex_desc.Width = 1;
        tex_desc.Height = 1;
        tex_desc.MipLevels = 1;
        tex_desc.ArraySize = 1;
        tex_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        tex_desc.SampleDesc.Count = 1;
        tex_desc.Usage = D3D11_USAGE_DEFAULT;
        tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        uint8_t white_rgba[4] = { 255, 255, 255, 255 };
        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem = white_rgba;
        init.SysMemPitch = 4;
        renderer->device->CreateTexture2D(&tex_desc, &init, &renderer->dummy_texture);

        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = 1;
        renderer->device->CreateShaderResourceView(
            renderer->dummy_texture, &srv_desc, &renderer->dummy_srv);
    }

    s_d3d11_nk = nk_d3d11_init(
        renderer->device,
        renderer->width,
        renderer->height,
        512 * 1024,
        128 * 1024);
    if( !s_d3d11_nk )
    {
        printf("Nuklear D3D11 init failed\n");
        return false;
    }
    {
        struct nk_font_atlas* atlas = nullptr;
        nk_d3d11_font_stash_begin(&atlas);
        nk_font_atlas_add_default(atlas, 13.0f * platform->display_scale, nullptr);
        nk_d3d11_font_stash_end();
    }
    torirs_nk_ui_set_active(s_d3d11_nk, nullptr, nullptr);
    s_d3d11_ui_prev_perf = SDL_GetPerformanceCounter();

    renderer->d3d11_ready = true;
    return true;
}

void
PlatformImpl2_OSX_SDL2_Renderer_D3D11_Render(
    struct Platform2_OSX_SDL2_Renderer_D3D11* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    if( !renderer || !renderer->d3d11_ready || !game || !render_command_buffer ||
        !renderer->platform || !renderer->platform->window )
        return;

    // Check if we need to resize
    int new_w = 0, new_h = 0;
    SDL_GetWindowSize(renderer->platform->window, &new_w, &new_h);
    if( new_w > 0 && new_h > 0 && (new_w != renderer->width || new_h != renderer->height) )
    {
        renderer->width = new_w;
        renderer->height = new_h;
        d3d11_resize_buffers(renderer);
        if( s_d3d11_nk )
            nk_d3d11_resize(renderer->device_context, renderer->width, renderer->height);
    }

    if( !renderer->render_target_view )
        return;

    renderer->debug_model_draws = 0;
    renderer->debug_triangles = 0;

    ID3D11DeviceContext* ctx = renderer->device_context;

    // Clear
    float clear_color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    ctx->ClearRenderTargetView(renderer->render_target_view, clear_color);
    ctx->ClearDepthStencilView(renderer->depth_stencil_view, D3D11_CLEAR_DEPTH, 1.0f, 0);
    ctx->OMSetRenderTargets(1, &renderer->render_target_view, renderer->depth_stencil_view);

    // Set shared state
    ctx->OMSetDepthStencilState(renderer->depth_stencil_state, 0);
    ctx->RSSetState(renderer->rasterizer_state);
    const float blend_factor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    ctx->OMSetBlendState(renderer->blend_state, blend_factor, 0xffffffff);
    ctx->PSSetSamplers(0, 1, &renderer->sampler_state);

    // Viewport
    int win_width = renderer->platform->game_screen_width;
    int win_height = renderer->platform->game_screen_height;
    if( win_width <= 0 || win_height <= 0 )
        SDL_GetWindowSize(renderer->platform->window, &win_width, &win_height);

    const LogicalViewportRect logical_vp =
        compute_logical_viewport_rect(win_width, win_height, game);
    const D3D11ViewportRect world_vp = compute_d3d11_world_viewport_rect(
        renderer->width, renderer->height, win_width, win_height, logical_vp);

    D3D11_VIEWPORT d3d_vp = {};
    d3d_vp.TopLeftX = (float)world_vp.x;
    d3d_vp.TopLeftY = (float)world_vp.y;
    d3d_vp.Width = (float)world_vp.width;
    d3d_vp.Height = (float)world_vp.height;
    d3d_vp.MinDepth = 0.0f;
    d3d_vp.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &d3d_vp);

    const float projection_width = (float)logical_vp.width;
    const float projection_height = (float)logical_vp.height;

    // Build uniforms
    D3D11Uniforms uniforms;
    const float pitch_rad = ((float)game->camera_pitch * 2.0f * (float)M_PI) / 2048.0f;
    const float yaw_rad = ((float)game->camera_yaw * 2.0f * (float)M_PI) / 2048.0f;
    d3d11_compute_view_matrix(uniforms.modelViewMatrix, 0.0f, 0.0f, 0.0f, pitch_rad, yaw_rad);
    d3d11_compute_projection_matrix(
        uniforms.projectionMatrix,
        (90.0f * (float)M_PI) / 180.0f,
        projection_width,
        projection_height);
    d3d11_remap_projection_z(uniforms.projectionMatrix);
    uniforms.uClock = (float)(SDL_GetTicks64() / 20);
    uniforms._pad[0] = 0.0f;
    uniforms._pad[1] = 0.0f;
    uniforms._pad[2] = 0.0f;

    // Upload uniforms
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if( ctx->Map(renderer->constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped) == S_OK )
        {
            memcpy(mapped.pData, &uniforms, sizeof(uniforms));
            ctx->Unmap(renderer->constant_buffer, 0);
        }
    }

    // Set world shader state
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetInputLayout(renderer->world_input_layout);
    ctx->VSSetShader(renderer->world_vs, nullptr, 0);
    ctx->PSSetShader(renderer->world_ps, nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, &renderer->constant_buffer);
    ctx->PSSetConstantBuffers(0, 1, &renderer->constant_buffer);

    // Process render command buffer
    LibToriRS_FrameBegin(game, render_command_buffer);

    static std::vector<ToriRSRenderCommand> sprite_cmds;
    sprite_cmds.clear();

    static std::vector<D3D11Vertex> batch_verts;
    batch_verts.clear();
    int batch_tex_id = -0x7fffffff;

    auto flush_batch = [&]() {
        if( batch_verts.empty() )
            return;
        ID3D11ShaderResourceView* bind_srv = renderer->dummy_srv;
        if( batch_tex_id >= 0 )
        {
            auto it = renderer->texture_srv_by_id.find(batch_tex_id);
            if( it == renderer->texture_srv_by_id.end() || !it->second )
            {
                batch_verts.clear();
                return;
            }
            bind_srv = it->second;
        }
        ctx->PSSetShaderResources(0, 1, &bind_srv);

        D3D11_BUFFER_DESC vb_desc = {};
        vb_desc.ByteWidth = (UINT)(batch_verts.size() * sizeof(D3D11Vertex));
        vb_desc.Usage = D3D11_USAGE_DEFAULT;
        vb_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vb_init = {};
        vb_init.pSysMem = batch_verts.data();
        ID3D11Buffer* vb = nullptr;
        if( renderer->device->CreateBuffer(&vb_desc, &vb_init, &vb) == S_OK )
        {
            UINT stride = sizeof(D3D11Vertex);
            UINT offset = 0;
            ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
            ctx->Draw((UINT)batch_verts.size(), 0);
            renderer->debug_triangles += (unsigned int)(batch_verts.size() / 3);
            vb->Release();
        }
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
                renderer->loaded_texture_ids.insert(tex_id);
                struct DashTexture* tex = cmd._texture_load.texture_nullable;
                if( !tex || !tex->texels )
                    break;

                // Release old
                {
                    auto it_tex = renderer->texture_by_id.find(tex_id);
                    if( it_tex != renderer->texture_by_id.end() && it_tex->second )
                        it_tex->second->Release();
                    auto it_srv = renderer->texture_srv_by_id.find(tex_id);
                    if( it_srv != renderer->texture_srv_by_id.end() && it_srv->second )
                        it_srv->second->Release();
                }

                renderer->texture_anim_speed_by_id[tex_id] =
                    d3d11_texture_animation_signed(tex->animation_direction, tex->animation_speed);
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

                D3D11_TEXTURE2D_DESC tex_desc = {};
                tex_desc.Width = (UINT)w;
                tex_desc.Height = (UINT)h;
                tex_desc.MipLevels = 1;
                tex_desc.ArraySize = 1;
                tex_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                tex_desc.SampleDesc.Count = 1;
                tex_desc.Usage = D3D11_USAGE_DEFAULT;
                tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA init = {};
                init.pSysMem = rgba.data();
                init.SysMemPitch = (UINT)w * 4;
                ID3D11Texture2D* d3d_tex = nullptr;
                renderer->device->CreateTexture2D(&tex_desc, &init, &d3d_tex);

                D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
                srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                srv_desc.Texture2D.MipLevels = 1;
                ID3D11ShaderResourceView* srv = nullptr;
                renderer->device->CreateShaderResourceView(d3d_tex, &srv_desc, &srv);

                renderer->texture_by_id[tex_id] = d3d_tex;
                renderer->texture_srv_by_id[tex_id] = srv;
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
                int rx = cmd._clear_rect.x;
                int ry = cmd._clear_rect.y;
                int rw = cmd._clear_rect.w;
                int rh = cmd._clear_rect.h;
                if( rw <= 0 || rh <= 0 )
                    break;
                LogicalViewportRect lr = { rx, ry, rw, rh };
                D3D11ViewportRect cr = compute_d3d11_world_viewport_rect(
                    renderer->width, renderer->height, win_width, win_height, lr);
                if( cr.width <= 0 || cr.height <= 0 )
                    break;
                ID3D11DeviceContext1* ctx1 = nullptr;
                if( SUCCEEDED(ctx->QueryInterface(IID_ID3D11DeviceContext1, (void**)&ctx1)) )
                {
                    float transparent[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                    D3D11_RECT rc = { cr.x, cr.y, cr.x + cr.width, cr.y + cr.height };
                    ctx1->ClearView(renderer->render_target_view, transparent, &rc, 1u);
                    ctx1->Release();
                }
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

                struct DashPosition draw_position = cmd._model_draw.position;
                int face_order_count = dash3d_prepare_projected_face_order(
                    game->sys_dash, model, &draw_position, game->view_port, game->camera);
                const int* face_order =
                    dash3d_projected_face_order(game->sys_dash, &face_order_count);

                const float draw_yaw_rad = (draw_position.yaw * 2.0f * (float)M_PI) / 2048.0f;
                const float cos_yaw = cosf(draw_yaw_rad);
                const float sin_yaw = sinf(draw_yaw_rad);

                renderer->debug_model_draws++;
                for( int fi = 0; fi < face_order_count; ++fi )
                {
                    const int f = face_order ? face_order[fi] : fi;
                    if( f < 0 || f >= dashmodel_face_count(model) )
                        continue;

                    const faceint_t* ftex = dashmodel_face_textures_const(model);
                    int raw_tex = ftex ? (int)ftex[f] : -1;
                    int eff_tex = raw_tex;
                    if( eff_tex >= 0 && renderer->texture_srv_by_id.find(eff_tex) ==
                                            renderer->texture_srv_by_id.end() )
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
                }
                break;
            }

            case TORIRS_GFX_SPRITE_LOAD:
            {
                struct DashSprite* sp = cmd._sprite_load.sprite;
                if( !sp || !sp->pixels_argb || sp->width <= 0 || sp->height <= 0 )
                    break;

                {
                    auto it = renderer->sprite_texture_by_ptr.find(sp);
                    if( it != renderer->sprite_texture_by_ptr.end() && it->second )
                    {
                        it->second->Release();
                        renderer->sprite_texture_by_ptr.erase(it);
                    }
                    auto sit = renderer->sprite_srv_by_ptr.find(sp);
                    if( sit != renderer->sprite_srv_by_ptr.end() && sit->second )
                    {
                        sit->second->Release();
                        renderer->sprite_srv_by_ptr.erase(sit);
                    }
                }

                const int sw = sp->width;
                const int sh = sp->height;
                std::vector<uint8_t> rgba((size_t)sw * (size_t)sh * 4u);
                for( int p = 0; p < sw * sh; ++p )
                {
                    uint32_t pix = sp->pixels_argb[p];
                    rgba[(size_t)p * 4u + 0u] = (uint8_t)((pix >> 16) & 0xFFu);
                    rgba[(size_t)p * 4u + 1u] = (uint8_t)((pix >> 8) & 0xFFu);
                    rgba[(size_t)p * 4u + 2u] = (uint8_t)(pix & 0xFFu);
                    rgba[(size_t)p * 4u + 3u] = (pix != 0u) ? 0xFFu : 0x00u;
                }

                D3D11_TEXTURE2D_DESC td = {};
                td.Width = (UINT)sw;
                td.Height = (UINT)sh;
                td.MipLevels = 1;
                td.ArraySize = 1;
                td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                td.SampleDesc.Count = 1;
                td.Usage = D3D11_USAGE_DEFAULT;
                td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA init = {};
                init.pSysMem = rgba.data();
                init.SysMemPitch = (UINT)sw * 4u;
                ID3D11Texture2D* sp_tex = nullptr;
                renderer->device->CreateTexture2D(&td, &init, &sp_tex);

                D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
                srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                srv_desc.Texture2D.MipLevels = 1;
                ID3D11ShaderResourceView* sp_srv = nullptr;
                renderer->device->CreateShaderResourceView(sp_tex, &srv_desc, &sp_srv);

                renderer->sprite_texture_by_ptr[sp] = sp_tex;
                renderer->sprite_srv_by_ptr[sp] = sp_srv;
                break;
            }

            case TORIRS_GFX_SPRITE_UNLOAD:
            {
                struct DashSprite* sp = cmd._sprite_load.sprite;
                auto it = renderer->sprite_texture_by_ptr.find(sp);
                if( it != renderer->sprite_texture_by_ptr.end() )
                {
                    if( it->second )
                        it->second->Release();
                    renderer->sprite_texture_by_ptr.erase(it);
                }
                auto sit = renderer->sprite_srv_by_ptr.find(sp);
                if( sit != renderer->sprite_srv_by_ptr.end() )
                {
                    if( sit->second )
                        sit->second->Release();
                    renderer->sprite_srv_by_ptr.erase(sit);
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
                    renderer->font_atlas_cache.find(font) == renderer->font_atlas_cache.end() )
                {
                    struct DashFontAtlas* atlas = font->atlas;

                    D3D11_TEXTURE2D_DESC td = {};
                    td.Width = (UINT)atlas->atlas_width;
                    td.Height = (UINT)atlas->atlas_height;
                    td.MipLevels = 1;
                    td.ArraySize = 1;
                    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                    td.SampleDesc.Count = 1;
                    td.Usage = D3D11_USAGE_DEFAULT;
                    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                    D3D11_SUBRESOURCE_DATA init = {};
                    init.pSysMem = atlas->rgba_pixels;
                    init.SysMemPitch = (UINT)atlas->atlas_width * 4u;
                    ID3D11Texture2D* atlas_tex = nullptr;
                    renderer->device->CreateTexture2D(&td, &init, &atlas_tex);

                    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
                    srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                    srv_desc.Texture2D.MipLevels = 1;
                    ID3D11ShaderResourceView* atlas_srv = nullptr;
                    renderer->device->CreateShaderResourceView(atlas_tex, &srv_desc, &atlas_srv);

                    D3D11FontAtlasEntry entry;
                    entry.texture = atlas_tex;
                    entry.srv = atlas_srv;
                    renderer->font_atlas_cache[font] = entry;
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

    // -----------------------------------------------------------------------
    // Sprite draws (2D pass)
    // -----------------------------------------------------------------------
    {
        D3D11_VIEWPORT full_vp = {};
        full_vp.Width = (float)renderer->width;
        full_vp.Height = (float)renderer->height;
        full_vp.MinDepth = 0.0f;
        full_vp.MaxDepth = 1.0f;
        ctx->RSSetViewports(1, &full_vp);

        ctx->IASetInputLayout(renderer->sprite_input_layout);
        ctx->VSSetShader(renderer->sprite_vs, nullptr, 0);
        ctx->PSSetShader(renderer->sprite_ps, nullptr, 0);

        for( const auto& sc : sprite_cmds )
        {
            if( sc.kind != TORIRS_GFX_SPRITE_DRAW )
                continue;
            struct DashSprite* sp = sc._sprite_draw.sprite;
            if( !sp || sp->width <= 0 || sp->height <= 0 )
                continue;

            auto srv_it = renderer->sprite_srv_by_ptr.find(sp);
            if( srv_it == renderer->sprite_srv_by_ptr.end() || !srv_it->second )
                continue;

            const int dst_x = sc._sprite_draw.x + sp->crop_x;
            const int dst_y = sc._sprite_draw.y + sp->crop_y;
            const float w = (float)sp->width;
            const float h = (float)sp->height;
            const float x0 = (float)dst_x;
            const float y0 = (float)dst_y;
            const float x1 = x0 + w;
            const float y1 = y0 + h;
            const float cx = 0.5f * (x0 + x1);
            const float cy = 0.5f * (y0 + y1);
            const float angle = (float)(sc._sprite_draw.rotation_r2pi2048 * (2.0 * M_PI) / 2048.0);
            const float ca = cosf(angle);
            const float sa = sinf(angle);
            const float hw = 0.5f * w;
            const float hh = 0.5f * h;

            float px[4], py[4];
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

            float verts[6 * 4] = {
                c0x, c0y, 0.0f, 0.0f, c1x, c1y, 1.0f, 0.0f, c2x, c2y, 1.0f, 1.0f,
                c0x, c0y, 0.0f, 0.0f, c2x, c2y, 1.0f, 1.0f, c3x, c3y, 0.0f, 1.0f,
            };

            D3D11_BUFFER_DESC vb_desc = {};
            vb_desc.ByteWidth = sizeof(verts);
            vb_desc.Usage = D3D11_USAGE_DEFAULT;
            vb_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA vb_init = {};
            vb_init.pSysMem = verts;
            ID3D11Buffer* vb = nullptr;
            if( renderer->device->CreateBuffer(&vb_desc, &vb_init, &vb) == S_OK )
            {
                UINT stride = 4 * sizeof(float);
                UINT offset = 0;
                ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
                ctx->PSSetShaderResources(0, 1, &srv_it->second);
                ctx->Draw(6, 0);
                vb->Release();
            }
        }
    }

    // -----------------------------------------------------------------------
    // Font draws
    // -----------------------------------------------------------------------
    {
        ctx->IASetInputLayout(renderer->font_input_layout);
        ctx->VSSetShader(renderer->font_vs, nullptr, 0);
        ctx->PSSetShader(renderer->font_ps, nullptr, 0);

        static std::vector<float> font_verts;
        font_verts.clear();

        struct DashPixFont* current_font_ptr = nullptr;
        ID3D11ShaderResourceView* current_atlas_srv = nullptr;

        const float fbw = (float)(win_width > 0 ? win_width : renderer->width);
        const float fbh = (float)(win_height > 0 ? win_height : renderer->height);

        auto flush_font_batch = [&]() {
            if( font_verts.empty() || !current_atlas_srv )
                return;
            int vert_count = (int)(font_verts.size() / 8);

            D3D11_BUFFER_DESC vb_desc = {};
            vb_desc.ByteWidth = (UINT)(font_verts.size() * sizeof(float));
            vb_desc.Usage = D3D11_USAGE_DEFAULT;
            vb_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA vb_init = {};
            vb_init.pSysMem = font_verts.data();
            ID3D11Buffer* vb = nullptr;
            if( renderer->device->CreateBuffer(&vb_desc, &vb_init, &vb) == S_OK )
            {
                UINT stride = 8 * sizeof(float);
                UINT offset = 0;
                ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
                ctx->PSSetShaderResources(0, 1, &current_atlas_srv);
                ctx->Draw((UINT)vert_count, 0);
                vb->Release();
            }
            font_verts.clear();
        };

        for( const auto& sc : sprite_cmds )
        {
            if( sc.kind != TORIRS_GFX_FONT_DRAW )
                continue;
            struct DashPixFont* f = sc._font_draw.font;
            if( !f || !f->atlas || !sc._font_draw.text || f->height2d <= 0 )
                continue;

            auto texIt = renderer->font_atlas_cache.find(f);
            if( texIt == renderer->font_atlas_cache.end() || !texIt->second.srv )
                continue;

            if( current_font_ptr != f )
            {
                flush_font_batch();
                current_font_ptr = f;
                current_atlas_srv = texIt->second.srv;
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
            float color_a = 1.0f;
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
                            cx0, cy0, u0, v0,      cr,  cg,  cb, color_a, cx1, cy0, u1, v0,
                            cr,  cg,  cb, color_a, cx1, cy1, u1, v1,      cr,  cg,  cb, color_a,
                            cx0, cy0, u0, v0,      cr,  cg,  cb, color_a, cx1, cy1, u1, v1,
                            cr,  cg,  cb, color_a, cx0, cy1, u0, v1,      cr,  cg,  cb, color_a,
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

    LibToriRS_FrameEnd(game);

    // Nuklear overlay
    render_nuklear_overlay(renderer, game);

    renderer->swap_chain->Present(0, 0);
}
