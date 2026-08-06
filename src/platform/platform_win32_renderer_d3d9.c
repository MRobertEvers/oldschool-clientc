#include "platform/platform_win32_renderer_d3d9.h"

#include "core/trspk_atlas.h"
#include "core/trspk_drawrangeex.h"
#include "core/trspk_drawrangelist.h"
#include "core/trspk_ibo.h"
#include "core/trspk_math.h"
#include "core/trspk_modelarena.h"
#include "core/trspk_pose.h"
#include "core/trspk_triangles.h"
#include "core/trspk_vbo.h"
#include "render/torirs_frame.h"
#include "render/trspk_toridraw.h"
#include "platform/platform_sdl2_renderer_soft3d.h"

#include "toridraw.h"
#include "toridraw_math.h"
#include "toridraw_model.h"
#include "toridraw_scene.h"
#include "toridraw_types.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* D3D9Ex is a Vista API.  Defining this before d3d9.h keeps even its types out
 * of this translation unit and makes accidental use a compile-time error. */
#ifndef D3D_DISABLE_9EX
#define D3D_DISABLE_9EX 1
#endif
#ifndef COBJMACROS
#define COBJMACROS 1
#endif
#include <windows.h>
#include <d3d9.h>

#define D3D9_ATLAS_DIM 2048u
#define D3D9_ATLAS_COLS 16u
#define D3D9_ATLAS_SLOTS 256u
#define D3D9_VBO_PAGE 65536u
#define D3D9_DRAWRANGE_CAP 4096u
#define D3D9_GPU_BUFFER_INIT 4096u
#define D3D9_POSE_ARENA_TRACK_STRIDE 4096

#define D3D9_WORLD_FVF (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1)
#define D3D9_OVERLAY_FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)

struct D3D9OverlayVertex
{
    float x;
    float y;
    float z;
    float rhw;
    D3DCOLOR color;
    float u;
    float v;
};

struct D3D9ModelGroup
{
    struct TRSPK_VBO* vbo_cpu;
    IDirect3DVertexBuffer9* vbo_gpu;
    uint32_t gpu_capacity;
    struct TRSPK_ModelArena* arena;
    struct TRSPK_Triangles triangles;
    bool reset_each_frame;
};

struct ToriRS_D3D9
{
    struct ToriDraw_Scene* scene;
    HWND hwnd;
    IDirect3D9* d3d;
    IDirect3DDevice9* device;
    D3DPRESENT_PARAMETERS present;
    D3DCAPS9 caps;
    bool reset_pending;
    bool scene_active;

    int width;
    int height;
    int client_w;
    int client_h;
    int lb_x;
    int lb_y;
    int lb_w;
    int lb_h;

    struct TRSPK_Atlas atlas;
    IDirect3DTexture9* atlas_texture;
    int tex_slot_of_id[TORIDRAW_TEXTURE_ID_CAPACITY];
    uint8_t tex_resident[D3D9_ATLAS_SLOTS];
    uint32_t tex_slot_next;
    IDirect3DTexture9* animated_textures[TORIDRAW_TEXTURE_ID_CAPACITY];

    struct D3D9ModelGroup groups[TRSPK_VBO_GROUP_COUNT];
    IDirect3DIndexBuffer9* ibo;
    uint32_t gpu_ibo_capacity;
    struct TRSPK_PoseTable poses;
    struct TRSPK_IBOChain* ibo_chain;
    struct TRSPK_DrawRangeList* draw_ranges;

    float view[16];
    float proj[16];
    struct ToriRS_RenderCommand_Begin3D cur_3d;
    bool has_3d;
    bool in3d;
    double frame_clock;

    int* overlay_black;
    int* overlay_white;
    size_t overlay_pixel_capacity;
    struct ToriRS_Soft3D soft_black;
    struct ToriRS_Soft3D soft_white;
    IDirect3DTexture9* overlay_texture;
    UINT overlay_tex_w;
    UINT overlay_tex_h;
    bool in2d;

    bool pick_enabled;
    int pick_mouse_x;
    int pick_mouse_y;
    struct ToriRS_PickHits pick_hits;
};

static void
d3d9_log_hr(const char* where, HRESULT hr)
{
    fprintf(stderr, "D3D9: %s failed (HRESULT 0x%08lx)\n", where, (unsigned long)hr);
}

static UINT
d3d9_next_pow2(UINT value)
{
    UINT out = 1u;
    while( out < value && out <= 0x40000000u )
        out <<= 1u;
    return out;
}

static int
d3d9_clampi(int value, int lo, int hi)
{
    if( value < lo )
        return lo;
    if( value > hi )
        return hi;
    return value;
}

static void
d3d9_identity(D3DMATRIX* matrix)
{
    memset(matrix, 0, sizeof(*matrix));
    matrix->_11 = 1.0f;
    matrix->_22 = 1.0f;
    matrix->_33 = 1.0f;
    matrix->_44 = 1.0f;
}

static void
d3d9_float16_to_matrix(const float* matrix, D3DMATRIX* out)
{
    /* TRSPK stores column-major matrices for column vectors.  The identical
     * bytes describe the transposed D3D row-vector matrix. */
    memcpy(out, matrix, sizeof(*out));
}

static void
d3d9_mat4_mul_colmajor(const float* a, const float* b, float* out)
{
    int column;
    int row;
    int k;
    for( column = 0; column < 4; column++ )
        for( row = 0; row < 4; row++ )
        {
            float sum = 0.0f;
            for( k = 0; k < 4; k++ )
                sum += a[k * 4 + row] * b[column * 4 + k];
            out[column * 4 + row] = sum;
        }
}

static void
d3d9_remap_projection_z(float* projection)
{
    static const float clip_z[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.5f, 0.0f,
        0.0f, 0.0f, 0.5f, 1.0f,
    };
    float remapped[16];
    d3d9_mat4_mul_colmajor(clip_z, projection, remapped);
    memcpy(projection, remapped, sizeof(remapped));
}

static void
d3d9_set_no_texture(IDirect3DDevice9* device)
{
    IDirect3DDevice9_SetTexture(device, 0, NULL);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(device, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    IDirect3DDevice9_SetTextureStageState(device, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
}

static void
d3d9_disable_texture_transform(IDirect3DDevice9* device)
{
    D3DMATRIX identity;
    d3d9_identity(&identity);
    IDirect3DDevice9_SetTransform(device, D3DTS_TEXTURE0, &identity);
    IDirect3DDevice9_SetTextureStageState(
        device, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_TEXCOORDINDEX, 0u);
}

static void
d3d9_set_world_states(struct ToriRS_D3D9* renderer)
{
    IDirect3DDevice9* device = renderer->device;
    if( !device )
        return;

    IDirect3DDevice9_SetVertexShader(device, NULL);
    IDirect3DDevice9_SetPixelShader(device, NULL);
    IDirect3DDevice9_SetFVF(device, D3D9_WORLD_FVF);
    IDirect3DDevice9_SetRenderState(device, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_FOGENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ZENABLE, D3DZB_FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ZWRITEENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ALPHABLENDENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    IDirect3DDevice9_SetRenderState(device, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    IDirect3DDevice9_SetRenderState(device, D3DRS_COLORVERTEX, TRUE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ALPHATESTENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
    /* Texture alpha is binary in the cache.  A threshold of one drops its
     * transparent texels without incorrectly dropping translucent vertex
     * colours on untextured faces. */
    IDirect3DDevice9_SetRenderState(device, D3DRS_ALPHAREF, 1u);
    IDirect3DDevice9_SetRenderState(device, D3DRS_SCISSORTESTENABLE, FALSE);
    IDirect3DDevice9_SetSamplerState(device, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(device, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(device, 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    IDirect3DDevice9_SetSamplerState(device, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    IDirect3DDevice9_SetSamplerState(device, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    d3d9_disable_texture_transform(device);
}

static void
d3d9_bind_modulated_texture(IDirect3DDevice9* device, IDirect3DTexture9* texture)
{
    IDirect3DDevice9_SetTexture(device, 0, (IDirect3DBaseTexture9*)texture);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(device, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    IDirect3DDevice9_SetTextureStageState(device, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
}

static float
d3d9_texture_animation_signed(int direction, int speed)
{
    float amount;
    if( direction == TORIDRAW_TEXANIM_DIRECTION_NONE || speed == 0 )
        return 0.0f;
    amount = (float)speed / 128.0f;
    return direction == TORIDRAW_TEXANIM_DIRECTION_U_DOWN ||
                   direction == TORIDRAW_TEXANIM_DIRECTION_U_UP
               ? amount
               : -amount;
}

static void
d3d9_apply_texture_scroll(IDirect3DDevice9* device, float amount, float clock_value)
{
    D3DMATRIX matrix;
    d3d9_identity(&matrix);
    if( amount > 0.0f )
        matrix._31 = clock_value * amount;
    else if( amount < 0.0f )
        matrix._32 = -fmodf(clock_value * -amount, 1.0f);
    IDirect3DDevice9_SetTextureStageState(
        device, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
    IDirect3DDevice9_SetTransform(device, D3DTS_TEXTURE0, &matrix);
}

static void
d3d9_bind_atlas(struct ToriRS_D3D9* renderer)
{
    d3d9_disable_texture_transform(renderer->device);
    if( renderer->atlas_texture )
        d3d9_bind_modulated_texture(renderer->device, renderer->atlas_texture);
    else
        d3d9_set_no_texture(renderer->device);
    IDirect3DDevice9_SetSamplerState(
        renderer->device, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    IDirect3DDevice9_SetSamplerState(
        renderer->device, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
}

static void
d3d9_bind_animated(struct ToriRS_D3D9* renderer, int tex_id)
{
    struct ToriDraw_Texture* texture = NULL;
    float amount = 0.0f;
    if( tex_id >= 0 && tex_id < TORIDRAW_TEXTURE_ID_CAPACITY && renderer->scene )
        texture = ToriDraw_TextureMapGet(
            &ToriDraw_SceneTexState(renderer->scene)->texture_map, tex_id);
    if( texture )
        amount = d3d9_texture_animation_signed(
            texture->animation_direction, texture->animation_speed);

    d3d9_bind_modulated_texture(renderer->device, renderer->animated_textures[tex_id]);
    IDirect3DDevice9_SetSamplerState(
        renderer->device, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    IDirect3DDevice9_SetSamplerState(
        renderer->device, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    d3d9_apply_texture_scroll(renderer->device, amount, (float)renderer->frame_clock);
}

static bool
d3d9_read_client_size(struct ToriRS_D3D9* renderer, int* out_w, int* out_h)
{
    RECT client;
    if( !renderer->hwnd || !GetClientRect(renderer->hwnd, &client) )
        return false;
    *out_w = (int)(client.right - client.left);
    *out_h = (int)(client.bottom - client.top);
    return true;
}

static void
d3d9_update_letterbox(struct ToriRS_D3D9* renderer)
{
    struct TRSPK_Letterbox box;
    if( renderer->width <= 0 || renderer->height <= 0 ||
        renderer->client_w <= 0 || renderer->client_h <= 0 )
    {
        renderer->lb_x = 0;
        renderer->lb_y = 0;
        renderer->lb_w = 0;
        renderer->lb_h = 0;
        return;
    }
    trspk_compute_letterbox(
        renderer->width, renderer->height, renderer->client_w, renderer->client_h, &box);
    renderer->lb_x = box.x;
    renderer->lb_y = box.y;
    renderer->lb_w = box.w;
    renderer->lb_h = box.h;
}

static void
d3d9_release_default_pool(struct ToriRS_D3D9* renderer)
{
    /* SetIndices retains a device-side reference. Unbind it before dropping
     * our reference or Reset can still see a live DEFAULT-pool resource. */
    if( renderer->device )
        IDirect3DDevice9_SetIndices(renderer->device, NULL);
    if( renderer->ibo )
    {
        IDirect3DIndexBuffer9_Release(renderer->ibo);
        renderer->ibo = NULL;
    }
    renderer->gpu_ibo_capacity = 0u;
}

static void
d3d9_restore_after_reset(struct ToriRS_D3D9* renderer)
{
    D3DMATRIX identity;
    d3d9_identity(&identity);
    IDirect3DDevice9_SetTransform(renderer->device, D3DTS_WORLD, &identity);
    IDirect3DDevice9_SetTransform(renderer->device, D3DTS_VIEW, &identity);
    IDirect3DDevice9_SetTransform(renderer->device, D3DTS_PROJECTION, &identity);
    d3d9_set_world_states(renderer);
}

static bool
d3d9_reset_device(struct ToriRS_D3D9* renderer, int width, int height)
{
    HRESULT hr;
    if( !renderer->device || width <= 0 || height <= 0 )
        return false;

    d3d9_release_default_pool(renderer);
    renderer->present.BackBufferWidth = (UINT)width;
    renderer->present.BackBufferHeight = (UINT)height;
    hr = IDirect3DDevice9_Reset(renderer->device, &renderer->present);
    if( FAILED(hr) )
    {
        renderer->reset_pending = true;
        if( hr != D3DERR_DEVICELOST )
            d3d9_log_hr("Reset", hr);
        return false;
    }
    renderer->client_w = width;
    renderer->client_h = height;
    renderer->reset_pending = false;
    d3d9_update_letterbox(renderer);
    d3d9_restore_after_reset(renderer);
    return true;
}

static bool
d3d9_device_ready(struct ToriRS_D3D9* renderer)
{
    int width;
    int height;
    HRESULT cooperative;
    bool size_changed;

    if( !renderer || !renderer->device || renderer->scene_active )
        return renderer && renderer->device && renderer->scene_active;
    if( !d3d9_read_client_size(renderer, &width, &height) || width <= 0 || height <= 0 )
        return false;

    size_changed = width != renderer->client_w || height != renderer->client_h;
    cooperative = IDirect3DDevice9_TestCooperativeLevel(renderer->device);
    if( cooperative == D3DERR_DEVICELOST )
    {
        renderer->reset_pending = true;
        return false;
    }
    if( cooperative != D3D_OK && cooperative != D3DERR_DEVICENOTRESET )
    {
        d3d9_log_hr("TestCooperativeLevel", cooperative);
        return false;
    }
    if( size_changed || renderer->reset_pending || cooperative == D3DERR_DEVICENOTRESET )
        return d3d9_reset_device(renderer, width, height);

    d3d9_update_letterbox(renderer);
    return true;
}

static bool
d3d9_begin_frame_scene(struct ToriRS_D3D9* renderer)
{
    HRESULT hr;
    D3DRECT logical_rect;
    if( !d3d9_device_ready(renderer) )
        return false;

    IDirect3DDevice9_Clear(
        renderer->device, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0u);
    if( renderer->lb_w > 0 && renderer->lb_h > 0 )
    {
        logical_rect.x1 = renderer->lb_x;
        logical_rect.y1 = renderer->lb_y;
        logical_rect.x2 = renderer->lb_x + renderer->lb_w;
        logical_rect.y2 = renderer->lb_y + renderer->lb_h;
        IDirect3DDevice9_Clear(
            renderer->device,
            1u,
            &logical_rect,
            D3DCLEAR_TARGET,
            (D3DCOLOR)TORIRS_D3D9_BG,
            1.0f,
            0u);
    }
    hr = IDirect3DDevice9_BeginScene(renderer->device);
    if( FAILED(hr) )
    {
        d3d9_log_hr("BeginScene", hr);
        return false;
    }
    renderer->scene_active = true;
    return true;
}

static void
d3d9_end_frame_scene(struct ToriRS_D3D9* renderer)
{
    HRESULT hr;
    if( !renderer || !renderer->device || !renderer->scene_active )
        return;
    hr = IDirect3DDevice9_EndScene(renderer->device);
    renderer->scene_active = false;
    if( FAILED(hr) )
        d3d9_log_hr("EndScene", hr);
}

static bool
d3d9_ensure_overlay_buffers(struct ToriRS_D3D9* renderer)
{
    size_t pixels;
    int* black;
    int* white;
    if( renderer->width <= 0 || renderer->height <= 0 )
        return false;
    pixels = (size_t)renderer->width * (size_t)renderer->height;
    if( pixels <= renderer->overlay_pixel_capacity &&
        renderer->overlay_black && renderer->overlay_white )
        return true;

    black = (int*)malloc(pixels * sizeof(int));
    white = (int*)malloc(pixels * sizeof(int));
    if( !black || !white )
    {
        free(black);
        free(white);
        fprintf(stderr, "D3D9: failed to allocate the 2D compatibility buffers\n");
        return false;
    }
    free(renderer->overlay_black);
    free(renderer->overlay_white);
    renderer->overlay_black = black;
    renderer->overlay_white = white;
    renderer->overlay_pixel_capacity = pixels;
    return true;
}

static void
d3d9_release_overlay_texture(struct ToriRS_D3D9* renderer)
{
    if( renderer->overlay_texture )
    {
        IDirect3DTexture9_Release(renderer->overlay_texture);
        renderer->overlay_texture = NULL;
    }
    renderer->overlay_tex_w = 0u;
    renderer->overlay_tex_h = 0u;
}

static bool
d3d9_ensure_overlay_texture(struct ToriRS_D3D9* renderer)
{
    UINT tex_w;
    UINT tex_h;
    HRESULT hr;
    if( !renderer->device || renderer->width <= 0 || renderer->height <= 0 )
        return false;
    tex_w = d3d9_next_pow2((UINT)renderer->width);
    tex_h = d3d9_next_pow2((UINT)renderer->height);
    if( renderer->caps.TextureCaps & D3DPTEXTURECAPS_SQUAREONLY )
    {
        UINT side = tex_w > tex_h ? tex_w : tex_h;
        tex_w = side;
        tex_h = side;
    }
    else if( renderer->caps.MaxTextureAspectRatio != 0u )
    {
        const UINT ratio = renderer->caps.MaxTextureAspectRatio;
        while( (uint64_t)tex_w > (uint64_t)tex_h * ratio &&
               tex_h < renderer->caps.MaxTextureHeight )
            tex_h <<= 1u;
        while( (uint64_t)tex_h > (uint64_t)tex_w * ratio &&
               tex_w < renderer->caps.MaxTextureWidth )
            tex_w <<= 1u;
    }
    if( tex_w > renderer->caps.MaxTextureWidth || tex_h > renderer->caps.MaxTextureHeight ||
        (renderer->caps.MaxTextureAspectRatio != 0u &&
            ((uint64_t)tex_w >
                    (uint64_t)tex_h * renderer->caps.MaxTextureAspectRatio ||
                (uint64_t)tex_h >
                    (uint64_t)tex_w * renderer->caps.MaxTextureAspectRatio)) )
    {
        fprintf(
            stderr,
            "D3D9: logical canvas %dx%d exceeds texture caps %lux%lu\n",
            renderer->width,
            renderer->height,
            (unsigned long)renderer->caps.MaxTextureWidth,
            (unsigned long)renderer->caps.MaxTextureHeight);
        return false;
    }
    if( renderer->overlay_texture && renderer->overlay_tex_w == tex_w &&
        renderer->overlay_tex_h == tex_h )
        return true;

    d3d9_release_overlay_texture(renderer);
    hr = IDirect3DDevice9_CreateTexture(
        renderer->device,
        tex_w,
        tex_h,
        1u,
        0u,
        D3DFMT_A8R8G8B8,
        D3DPOOL_MANAGED,
        &renderer->overlay_texture,
        NULL);
    if( FAILED(hr) )
    {
        d3d9_log_hr("CreateTexture(2D overlay)", hr);
        return false;
    }
    renderer->overlay_tex_w = tex_w;
    renderer->overlay_tex_h = tex_h;
    return true;
}

static bool
d3d9_upload_overlay(struct ToriRS_D3D9* renderer)
{
    D3DLOCKED_RECT locked;
    HRESULT hr;
    int y;
    if( !d3d9_ensure_overlay_texture(renderer) )
        return false;
    hr = IDirect3DTexture9_LockRect(renderer->overlay_texture, 0u, &locked, NULL, 0u);
    if( FAILED(hr) )
    {
        d3d9_log_hr("LockRect(2D overlay)", hr);
        return false;
    }

    memset(locked.pBits, 0, (size_t)locked.Pitch * renderer->overlay_tex_h);
    for( y = 0; y < renderer->height; y++ )
    {
        const uint32_t* black =
            (const uint32_t*)renderer->overlay_black + (size_t)y * renderer->width;
        const uint32_t* white =
            (const uint32_t*)renderer->overlay_white + (size_t)y * renderer->width;
        uint8_t* dst = (uint8_t*)locked.pBits + (size_t)y * locked.Pitch;
        int x;
        for( x = 0; x < renderer->width; x++, dst += 4 )
        {
            int br = (int)((black[x] >> 16) & 0xffu);
            int bg = (int)((black[x] >> 8) & 0xffu);
            int bb = (int)(black[x] & 0xffu);
            int wr = (int)((white[x] >> 16) & 0xffu);
            int wg = (int)((white[x] >> 8) & 0xffu);
            int wb = (int)(white[x] & 0xffu);
            int transparent =
                (d3d9_clampi(wr - br, 0, 255) +
                    d3d9_clampi(wg - bg, 0, 255) +
                    d3d9_clampi(wb - bb, 0, 255) + 1) /
                3;
            int alpha = 255 - transparent;
            /* The black result is the source's premultiplied colour. */
            dst[0] = (uint8_t)d3d9_clampi(bb, 0, alpha);
            dst[1] = (uint8_t)d3d9_clampi(bg, 0, alpha);
            dst[2] = (uint8_t)d3d9_clampi(br, 0, alpha);
            dst[3] = (uint8_t)alpha;
        }
    }
    IDirect3DTexture9_UnlockRect(renderer->overlay_texture, 0u);
    return true;
}

static void
d3d9_draw_overlay(struct ToriRS_D3D9* renderer)
{
    struct D3D9OverlayVertex quad[4];
    float right;
    float bottom;
    float u1;
    float v1;
    IDirect3DDevice9* device = renderer->device;
    if( !renderer->scene_active || !d3d9_upload_overlay(renderer) ||
        renderer->lb_w <= 0 || renderer->lb_h <= 0 )
        return;

    right = (float)(renderer->lb_x + renderer->lb_w) - 0.5f;
    bottom = (float)(renderer->lb_y + renderer->lb_h) - 0.5f;
    u1 = (float)renderer->width / (float)renderer->overlay_tex_w;
    v1 = (float)renderer->height / (float)renderer->overlay_tex_h;
    quad[0] = (struct D3D9OverlayVertex){
        (float)renderer->lb_x - 0.5f, (float)renderer->lb_y - 0.5f,
        0.0f, 1.0f, 0xffffffffu, 0.0f, 0.0f };
    quad[1] = (struct D3D9OverlayVertex){
        right, (float)renderer->lb_y - 0.5f,
        0.0f, 1.0f, 0xffffffffu, u1, 0.0f };
    quad[2] = (struct D3D9OverlayVertex){
        (float)renderer->lb_x - 0.5f, bottom,
        0.0f, 1.0f, 0xffffffffu, 0.0f, v1 };
    quad[3] = (struct D3D9OverlayVertex){
        right, bottom, 0.0f, 1.0f, 0xffffffffu, u1, v1 };

    IDirect3DDevice9_SetVertexShader(device, NULL);
    IDirect3DDevice9_SetPixelShader(device, NULL);
    IDirect3DDevice9_SetFVF(device, D3D9_OVERLAY_FVF);
    IDirect3DDevice9_SetRenderState(device, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ZENABLE, D3DZB_FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ZWRITEENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ALPHATESTENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ALPHABLENDENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_SRCBLEND, D3DBLEND_ONE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    IDirect3DDevice9_SetRenderState(device, D3DRS_SCISSORTESTENABLE, FALSE);
    IDirect3DDevice9_SetTexture(
        device, 0, (IDirect3DBaseTexture9*)renderer->overlay_texture);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    IDirect3DDevice9_SetTextureStageState(device, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    IDirect3DDevice9_SetTextureStageState(device, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    IDirect3DDevice9_SetSamplerState(device, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(device, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(device, 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    IDirect3DDevice9_SetSamplerState(device, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    IDirect3DDevice9_SetSamplerState(device, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    d3d9_disable_texture_transform(device);
    IDirect3DDevice9_DrawPrimitiveUP(
        device, D3DPT_TRIANGLESTRIP, 2u, quad, (UINT)sizeof(quad[0]));
}

static void
d3d9_decode_texture_rgba(
    const struct ToriDraw_Texture* texture,
    uint32_t tile_size,
    uint8_t* rgba)
{
    uint32_t y;
    memset(rgba, 0, (size_t)tile_size * tile_size * 4u);
    if( !texture || !texture->texels || texture->width <= 0 || texture->height <= 0 )
        return;

    for( y = 0u; y < tile_size; y++ )
    {
        uint32_t sy = y * (uint32_t)texture->height / tile_size;
        uint32_t x;
        for( x = 0u; x < tile_size; x++ )
        {
            uint32_t sx = x * (uint32_t)texture->width / tile_size;
            uint32_t src = (uint32_t)texture->texels[
                sy * (uint32_t)texture->width + sx];
            uint8_t* out = rgba + ((size_t)y * tile_size + x) * 4u;
            out[0] = (uint8_t)((src >> 16) & 0xffu);
            out[1] = (uint8_t)((src >> 8) & 0xffu);
            out[2] = (uint8_t)(src & 0xffu);
            out[3] = (uint8_t)((texture->opaque || src != 0u) ? 255u : 0u);
        }
    }
}

static bool
d3d9_upload_rgba_texture(
    struct ToriRS_D3D9* renderer,
    IDirect3DTexture9** destination,
    const uint8_t* rgba,
    UINT width,
    UINT height)
{
    IDirect3DTexture9* texture = NULL;
    D3DLOCKED_RECT locked;
    HRESULT hr;
    UINT y;

    if( *destination )
    {
        IDirect3DTexture9_Release(*destination);
        *destination = NULL;
    }
    hr = IDirect3DDevice9_CreateTexture(
        renderer->device,
        width,
        height,
        1u,
        0u,
        D3DFMT_A8R8G8B8,
        D3DPOOL_MANAGED,
        &texture,
        NULL);
    if( FAILED(hr) )
    {
        d3d9_log_hr("CreateTexture(world)", hr);
        return false;
    }
    hr = IDirect3DTexture9_LockRect(texture, 0u, &locked, NULL, 0u);
    if( FAILED(hr) )
    {
        d3d9_log_hr("LockRect(world)", hr);
        IDirect3DTexture9_Release(texture);
        return false;
    }
    for( y = 0u; y < height; y++ )
    {
        const uint8_t* src = rgba + (size_t)y * width * 4u;
        uint8_t* dst = (uint8_t*)locked.pBits + (size_t)y * locked.Pitch;
        UINT x;
        for( x = 0u; x < width; x++, src += 4, dst += 4 )
        {
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            dst[3] = src[3];
        }
    }
    IDirect3DTexture9_UnlockRect(texture, 0u);
    *destination = texture;
    return true;
}

static bool
d3d9_upload_atlas(struct ToriRS_D3D9* renderer)
{
    D3DLOCKED_RECT locked;
    HRESULT hr;
    uint32_t y;
    if( !renderer->device || !trspk_atlas_is_initialized(&renderer->atlas) ||
        !renderer->atlas.pixels )
        return false;

    if( !renderer->atlas_texture )
    {
        hr = IDirect3DDevice9_CreateTexture(
            renderer->device,
            D3D9_ATLAS_DIM,
            D3D9_ATLAS_DIM,
            1u,
            0u,
            D3DFMT_A8R8G8B8,
            D3DPOOL_MANAGED,
            &renderer->atlas_texture,
            NULL);
        if( FAILED(hr) )
        {
            d3d9_log_hr("CreateTexture(world atlas)", hr);
            return false;
        }
    }
    hr = IDirect3DTexture9_LockRect(renderer->atlas_texture, 0u, &locked, NULL, 0u);
    if( FAILED(hr) )
    {
        d3d9_log_hr("LockRect(world atlas)", hr);
        return false;
    }
    for( y = 0u; y < D3D9_ATLAS_DIM; y++ )
    {
        const uint8_t* src = renderer->atlas.pixels + (size_t)y * renderer->atlas.stride;
        uint8_t* dst = (uint8_t*)locked.pBits + (size_t)y * locked.Pitch;
        uint32_t x;
        for( x = 0u; x < D3D9_ATLAS_DIM; x++, src += 4, dst += 4 )
        {
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            dst[3] = src[3];
        }
    }
    IDirect3DTexture9_UnlockRect(renderer->atlas_texture, 0u);
    trspk_atlas_clear_dirty(&renderer->atlas);
    return true;
}

static int
d3d9_texture_slot(struct ToriRS_D3D9* renderer, int tex_id)
{
    int slot;
    if( tex_id < 0 || tex_id >= TORIDRAW_TEXTURE_ID_CAPACITY )
        return -1;
    slot = renderer->tex_slot_of_id[tex_id];
    if( slot >= 0 )
        return slot;
    if( renderer->tex_slot_next >= D3D9_ATLAS_SLOTS )
    {
        static bool warned;
        if( !warned )
        {
            fprintf(stderr, "D3D9: the 2048x2048 world texture atlas is full\n");
            warned = true;
        }
        return -1;
    }
    slot = (int)renderer->tex_slot_next++;
    renderer->tex_slot_of_id[tex_id] = slot;
    return slot;
}

static bool
d3d9_load_texture_object(
    struct ToriRS_D3D9* renderer,
    int tex_id,
    const struct ToriDraw_Texture* texture)
{
    static uint8_t rgba[TRSPK_ATLAS_TILE * TRSPK_ATLAS_TILE * 4u];
    if( !renderer->device || tex_id < 0 || tex_id >= TORIDRAW_TEXTURE_ID_CAPACITY ||
        !texture || !texture->texels )
        return false;
    d3d9_decode_texture_rgba(texture, TRSPK_ATLAS_TILE, rgba);

    if( texture->animation_direction != TORIDRAW_TEXANIM_DIRECTION_NONE )
        return d3d9_upload_rgba_texture(
            renderer,
            &renderer->animated_textures[tex_id],
            rgba,
            TRSPK_ATLAS_TILE,
            TRSPK_ATLAS_TILE);
    else
    {
        int slot = d3d9_texture_slot(renderer, tex_id);
        if( slot < 0 )
            return false;
        if( !trspk_atlas_grid_insert_at(
                &renderer->atlas,
                (uint32_t)slot,
                rgba,
                TRSPK_ATLAS_TILE * 4u,
                TRSPK_ATLAS_TILE,
                TRSPK_ATLAS_TILE,
                NULL) )
            return false;
        renderer->tex_resident[slot] = 1u;
        return true;
    }
}

static int
d3d9_ensure_static_texture(struct ToriRS_D3D9* renderer, int tex_id)
{
    struct ToriDraw_Texture* texture;
    int slot;
    if( tex_id < 0 || tex_id >= TORIDRAW_TEXTURE_ID_CAPACITY || !renderer->scene )
        return -1;
    slot = d3d9_texture_slot(renderer, tex_id);
    if( slot < 0 )
        return -1;
    if( renderer->tex_resident[slot] )
        return slot;
    texture = ToriDraw_TextureMapGet(
        &ToriDraw_SceneTexState(renderer->scene)->texture_map, tex_id);
    if( !texture || texture->animation_direction != TORIDRAW_TEXANIM_DIRECTION_NONE ||
        !d3d9_load_texture_object(renderer, tex_id, texture) )
        return -1;
    return slot;
}

static bool
d3d9_ensure_animated_texture(struct ToriRS_D3D9* renderer, int tex_id)
{
    struct ToriDraw_Texture* texture;
    if( tex_id < 0 || tex_id >= TORIDRAW_TEXTURE_ID_CAPACITY || !renderer->scene )
        return false;
    if( renderer->animated_textures[tex_id] )
        return true;
    texture = ToriDraw_TextureMapGet(
        &ToriDraw_SceneTexState(renderer->scene)->texture_map, tex_id);
    return texture && texture->animation_direction != TORIDRAW_TEXANIM_DIRECTION_NONE &&
           d3d9_load_texture_object(renderer, tex_id, texture);
}

static void
d3d9_unload_texture(struct ToriRS_D3D9* renderer, int tex_id)
{
    int slot;
    if( tex_id < 0 || tex_id >= TORIDRAW_TEXTURE_ID_CAPACITY )
        return;
    if( renderer->animated_textures[tex_id] )
    {
        IDirect3DTexture9_Release(renderer->animated_textures[tex_id]);
        renderer->animated_textures[tex_id] = NULL;
    }
    slot = renderer->tex_slot_of_id[tex_id];
    if( slot >= 0 && (uint32_t)slot < D3D9_ATLAS_SLOTS && renderer->atlas.pixels )
    {
        struct TRSPK_AtlasTile tile;
        if( trspk_atlas_grid_tile_for_slot(&renderer->atlas, (uint32_t)slot, &tile) )
        {
            uint32_t y;
            for( y = 0u; y < tile.h; y++ )
                memset(
                    renderer->atlas.pixels +
                        (size_t)(tile.y + y) * renderer->atlas.stride +
                        (size_t)tile.x * renderer->atlas.channels,
                    0,
                    (size_t)tile.w * renderer->atlas.channels);
            trspk_atlas_set_dirty(&renderer->atlas);
            renderer->tex_resident[slot] = 0u;
        }
    }
}

static void
d3d9_map_atlas_uv(int slot, float local_u, float local_v, float* out_u, float* out_v)
{
    const float cell = (float)TRSPK_ATLAS_TILE / (float)D3D9_ATLAS_DIM;
    if( slot < 0 )
    {
        slot = 0;
        local_u = 0.5f;
        local_v = 0.5f;
    }
    /* Match v1's fixed-function path: clamp the vertex coordinates to the
     * interior of the tile.  Applying fract() here is not equivalent to the
     * GL fragment shader -- at the usual 0/1 endpoints it turns both vertices
     * into zero and collapses the interpolated texture into a streak. */
    if( local_u < 0.008f )
        local_u = 0.008f;
    else if( local_u > 0.992f )
        local_u = 0.992f;
    if( local_v < 0.008f )
        local_v = 0.008f;
    else if( local_v > 0.992f )
        local_v = 0.992f;
    *out_u = (float)(slot % (int)D3D9_ATLAS_COLS) * cell + local_u * cell;
    *out_v = (float)(slot / (int)D3D9_ATLAS_COLS) * cell + local_v * cell;
}

static void
d3d9_map_animated_uv(float* u, float* v)
{
    /* Keep interpolation intact.  Animated textures live in standalone
     * buffers; D3DTADDRESS_WRAP handles V after interpolation and after the
     * fixed-function scroll transform. */
    if( *u < 0.008f )
        *u = 0.008f;
    else if( *u > 0.992f )
        *u = 0.992f;
    if( *v < 0.008f )
        *v = 0.008f;
    else if( *v > 0.992f )
        *v = 0.992f;
}

static bool
d3d9_upload_group(struct ToriRS_D3D9* renderer, struct D3D9ModelGroup* group)
{
    uint32_t vertex_count;
    UINT byte_count;
    void* locked = NULL;
    HRESULT hr;
    if( !renderer->device || !group->vbo_cpu )
        return false;
    vertex_count = group->vbo_cpu->vertex_count;
    if( vertex_count == 0u )
    {
        trspk_vbo_clear_dirty(group->vbo_cpu);
        return true;
    }
    if( !group->reset_each_frame && !trspk_vbo_is_dirty(group->vbo_cpu) )
        return group->vbo_gpu != NULL;

    if( !group->vbo_gpu || vertex_count > group->gpu_capacity )
    {
        uint32_t capacity = group->gpu_capacity ? group->gpu_capacity : D3D9_GPU_BUFFER_INIT;
        while( capacity < vertex_count )
            capacity *= 2u;
        if( group->vbo_gpu )
            IDirect3DVertexBuffer9_Release(group->vbo_gpu);
        group->vbo_gpu = NULL;
        hr = IDirect3DDevice9_CreateVertexBuffer(
            renderer->device,
            (UINT)(capacity * sizeof(struct TRSPK_VertexD3D9)),
            D3DUSAGE_WRITEONLY,
            0u,
            D3DPOOL_MANAGED,
            &group->vbo_gpu,
            NULL);
        if( FAILED(hr) )
        {
            group->gpu_capacity = 0u;
            d3d9_log_hr("CreateVertexBuffer", hr);
            return false;
        }
        group->gpu_capacity = capacity;
    }
    byte_count = (UINT)(vertex_count * sizeof(struct TRSPK_VertexD3D9));
    hr = IDirect3DVertexBuffer9_Lock(group->vbo_gpu, 0u, byte_count, &locked, 0u);
    if( FAILED(hr) )
    {
        d3d9_log_hr("Lock(vertex buffer)", hr);
        return false;
    }
    memcpy(locked, group->vbo_cpu->vertices.as_d3d9, byte_count);
    IDirect3DVertexBuffer9_Unlock(group->vbo_gpu);
    trspk_vbo_clear_dirty(group->vbo_cpu);
    return true;
}

static bool
d3d9_ensure_ibo(struct ToriRS_D3D9* renderer, uint32_t index_count)
{
    uint32_t capacity;
    HRESULT hr;
    if( renderer->ibo && renderer->gpu_ibo_capacity >= index_count )
        return true;
    capacity = renderer->gpu_ibo_capacity ? renderer->gpu_ibo_capacity : D3D9_GPU_BUFFER_INIT;
    while( capacity < index_count )
        capacity *= 2u;
    if( renderer->ibo )
        IDirect3DIndexBuffer9_Release(renderer->ibo);
    renderer->ibo = NULL;
    hr = IDirect3DDevice9_CreateIndexBuffer(
        renderer->device,
        (UINT)(capacity * sizeof(uint16_t)),
        D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
        D3DFMT_INDEX16,
        D3DPOOL_DEFAULT,
        &renderer->ibo,
        NULL);
    if( FAILED(hr) )
    {
        renderer->gpu_ibo_capacity = 0u;
        d3d9_log_hr("CreateIndexBuffer", hr);
        return false;
    }
    renderer->gpu_ibo_capacity = capacity;
    return true;
}

static void
d3d9_reset_group(struct D3D9ModelGroup* group)
{
    if( group->arena )
        trspk_modelarena_clear(group->arena);
}

static uint32_t
d3d9_bake_into_arena(
    struct ToriRS_D3D9* renderer,
    struct D3D9ModelGroup* group,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle model_handle,
    const struct ToriDraw_Position* world_position,
    bool update_pose_table)
{
    int face_count;
    uint32_t vertex_count;
    int arena_element_id;
    int arena_pose_id;
    uint32_t slot_index;
    const struct TRSPK_ModelSlot* model_slot;
    uint32_t face_index;

    if( !renderer || !renderer->scene || !group || !group->arena || !group->vbo_cpu )
        return UINT32_MAX;
    face_count = trspk_toridraw_face_count(model_handle);
    if( face_count <= 0 )
        return UINT32_MAX;
    vertex_count = (uint32_t)face_count * 3u;
    if( vertex_count > D3D9_VBO_PAGE )
    {
        fprintf(
            stderr,
            "D3D9: model has %lu vertices and cannot fit a 16-bit TRSPK page\n",
            (unsigned long)vertex_count);
        return UINT32_MAX;
    }

    anim_index = d3d9_clampi(anim_index, 0, TRSPK_POSE_TRACK_COUNT - 1);
    if( pose_id < 0 )
        pose_id = 0;
    arena_element_id = element_id >= 0 ? element_id : 0;
    arena_pose_id = anim_index * D3D9_POSE_ARENA_TRACK_STRIDE + pose_id;

    if( update_pose_table && element_id < 0 )
        update_pose_table = false;
    if( update_pose_table )
    {
        uint32_t old_slot =
            trspk_modelarena_find(group->arena, arena_element_id, arena_pose_id);
        if( old_slot != TRSPK_MODELSLOT_NULL_IDX )
            trspk_modelarena_unload(group->arena, old_slot);
    }
    slot_index = trspk_modelarena_load(
        group->arena, arena_element_id, arena_pose_id, vertex_count);
    model_slot = trspk_modelarena_get(group->arena, slot_index);
    if( !model_slot )
        return UINT32_MAX;

    for( face_index = 0u; face_index < (uint32_t)face_count; face_index++ )
    {
        struct TRSPK_ToriDrawBakeFaceVerts face;
        uint32_t vertex = model_slot->vertex_base + face_index * 3u;
        float ua = 0.5f;
        float va = 0.5f;
        float ub = 0.5f;
        float vb = 0.5f;
        float uc = 0.5f;
        float vc = 0.5f;
        int triangle_config = TRSPK_TRIANGLES_ATLAS;
        int slot = 0;

        memset(&face, 0, sizeof(face));
        if( !trspk_toridraw_bake_face_handle(
                model_handle,
                face_index,
                world_position,
                renderer->scene,
                true,
                &face) )
            continue;

        if( face.tex_id >= 0 && face.is_animated &&
            d3d9_ensure_animated_texture(renderer, face.tex_id) )
        {
            triangle_config = face.tex_id;
            ua = face.uv.u1;
            va = face.uv.v1;
            ub = face.uv.u2;
            vb = face.uv.v2;
            uc = face.uv.u3;
            vc = face.uv.v3;
            d3d9_map_animated_uv(&ua, &va);
            d3d9_map_animated_uv(&ub, &vb);
            d3d9_map_animated_uv(&uc, &vc);
        }
        else
        {
            if( face.tex_id >= 0 )
            {
                int resident = d3d9_ensure_static_texture(renderer, face.tex_id);
                if( resident >= 0 )
                    slot = resident;
            }
            d3d9_map_atlas_uv(
                face.tex_id >= 0 ? slot : -1, face.uv.u1, face.uv.v1, &ua, &va);
            d3d9_map_atlas_uv(
                face.tex_id >= 0 ? slot : -1, face.uv.u2, face.uv.v2, &ub, &vb);
            d3d9_map_atlas_uv(
                face.tex_id >= 0 ? slot : -1, face.uv.u3, face.uv.v3, &uc, &vc);
        }

        trspk_triangles_set(
            &group->triangles,
            trspk_triangles_index_from_vertex(vertex),
            triangle_config);
        trspk_vbo_write_vertex_d3d9(
            group->vbo_cpu,
            vertex,
            face.wx_a,
            face.wy_a,
            face.wz_a,
            face.color_a,
            ua,
            va,
            (float)face.tex_id);
        trspk_vbo_write_vertex_d3d9(
            group->vbo_cpu,
            vertex + 1u,
            face.wx_b,
            face.wy_b,
            face.wz_b,
            face.color_b,
            ub,
            vb,
            (float)face.tex_id);
        trspk_vbo_write_vertex_d3d9(
            group->vbo_cpu,
            vertex + 2u,
            face.wx_c,
            face.wy_c,
            face.wz_c,
            face.color_c,
            uc,
            vc,
            (float)face.tex_id);
    }

    if( update_pose_table )
        trspk_pose_table_set(
            &renderer->poses,
            element_id,
            anim_index,
            pose_id,
            model_slot->vertex_base);
    return model_slot->vertex_base;
}

static void
d3d9_model_unload(struct ToriRS_D3D9* renderer, int element_id)
{
    if( element_id < 0 || !renderer->groups[TRSPK_VBO_GROUP_STATIC].arena )
        return;
    trspk_modelarena_unload_element(
        renderer->groups[TRSPK_VBO_GROUP_STATIC].arena, element_id);
    trspk_pose_table_remove_element(&renderer->poses, element_id);
}

static void
d3d9_model_load(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand_ModelLoad* command)
{
    if( !command || command->element_id < 0 || command->model.kind == TORIDRAWMK_NONE )
        return;
    (void)d3d9_bake_into_arena(
        renderer,
        &renderer->groups[TRSPK_VBO_GROUP_STATIC],
        command->element_id,
        0,
        0,
        command->model,
        &command->world_position,
        true);
}

static void
d3d9_animation_load(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand_AnimLoad* command)
{
    struct ToriDraw_Model* source;
    int frame;
    if( !command || command->element_id < 0 || !command->animation ||
        !command->animation->base || !command->animation->frames ||
        command->animation->frame_count <= 0 ||
        command->model.kind != TORIDRAWMK_MODEL || !command->model.u.model.model )
        return;
    source = command->model.u.model.model;
    for( frame = 0; frame < command->animation->frame_count; frame++ )
    {
        struct ToriDraw_Model* baked = ToriDraw_ModelCopy(source);
        struct ToriDraw_ModelHandle handle;
        if( !baked )
            continue;
        ToriDraw_ModelCaptureOriginalVertices(baked);
        ToriDraw_ModelAnimateReset(baked);
        ToriDraw_ModelAnimateFrame(
            baked, command->animation->base, &command->animation->frames[frame]);
        memset(&handle, 0, sizeof(handle));
        handle.kind = TORIDRAWMK_MODEL;
        handle.u.model.model = baked;
        (void)d3d9_bake_into_arena(
            renderer,
            &renderer->groups[TRSPK_VBO_GROUP_STATIC],
            command->element_id,
            0,
            frame,
            handle,
            &command->world_position,
            true);
        ToriDraw_ModelFree(baked);
    }
}

static void
d3d9_begin_3d(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand_Begin3D* command)
{
    const struct ToriDraw_ViewPort* viewport;
    int pass_w;
    int pass_h;
    int logical_x;
    int logical_y;
    D3DVIEWPORT9 d3d_viewport;
    D3DMATRIX view;
    D3DMATRIX projection;
    D3DMATRIX world;
    uint32_t group;

    if( !renderer->device || !command )
        return;
    renderer->cur_3d = *command;
    renderer->has_3d = true;
    renderer->in3d = true;
    viewport = &renderer->cur_3d.view_port;
    pass_w = viewport->width > 0 ? viewport->width : renderer->width;
    pass_h = viewport->height > 0 ? viewport->height : renderer->height;
    logical_x = viewport->x_center - pass_w / 2;
    logical_y = viewport->y_center - pass_h / 2;

    memset(&d3d_viewport, 0, sizeof(d3d_viewport));
    d3d_viewport.X = (DWORD)d3d9_clampi(
        renderer->lb_x + logical_x * renderer->lb_w / renderer->width,
        0,
        renderer->client_w - 1);
    d3d_viewport.Y = (DWORD)d3d9_clampi(
        renderer->lb_y + logical_y * renderer->lb_h / renderer->height,
        0,
        renderer->client_h - 1);
    d3d_viewport.Width = (DWORD)d3d9_clampi(
        pass_w * renderer->lb_w / renderer->width,
        1,
        renderer->client_w - (int)d3d_viewport.X);
    d3d_viewport.Height = (DWORD)d3d9_clampi(
        pass_h * renderer->lb_h / renderer->height,
        1,
        renderer->client_h - (int)d3d_viewport.Y);
    d3d_viewport.MinZ = 0.0f;
    d3d_viewport.MaxZ = 1.0f;
    IDirect3DDevice9_SetViewport(renderer->device, &d3d_viewport);

    trspk_compute_pass_matrices(
        renderer->view,
        renderer->proj,
        (float)command->camera_position.x,
        (float)command->camera_position.y,
        (float)command->camera_position.z,
        ToriDraw_AngleToRadians(command->camera.pitch),
        ToriDraw_AngleToRadians(command->camera.yaw),
        pass_w,
        pass_h);
    d3d9_remap_projection_z(renderer->proj);
    d3d9_float16_to_matrix(renderer->view, &view);
    d3d9_float16_to_matrix(renderer->proj, &projection);
    d3d9_identity(&world);
    IDirect3DDevice9_SetTransform(renderer->device, D3DTS_WORLD, &world);
    IDirect3DDevice9_SetTransform(renderer->device, D3DTS_VIEW, &view);
    IDirect3DDevice9_SetTransform(renderer->device, D3DTS_PROJECTION, &projection);

    for( group = 0u; group < TRSPK_VBO_GROUP_COUNT; group++ )
        if( renderer->groups[group].reset_each_frame )
            d3d9_reset_group(&renderer->groups[group]);
}

static void
d3d9_draw_model(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand_Model* command)
{
    struct ToriDraw_Position projected_position;
    int face_count;
    int* face_order;
    bool dynamic;
    int anim_index;
    int pose_id;
    uint32_t vertex_base;
    uint32_t page_base;
    uint32_t local_base;
    uint32_t group;
    int i;

    if( !renderer->has_3d || !renderer->scene || !renderer->ibo_chain || !command ||
        command->model.kind == TORIDRAWMK_NONE )
        return;
    if( command->animation && command->element_id >= 0 )
        ToriDraw_SceneElementApplyAnimation(
            renderer->scene,
            command->element_id,
            command->anim_index == 0,
            command->anim_frame);
    projected_position = command->position;
    if( ToriDraw_RenderModel1Project(
            command->model,
            renderer->scene,
            &projected_position,
            &renderer->cur_3d.view_port,
            &renderer->cur_3d.camera) != TORIDRAW_CULL_VISIBLE )
        return;

    if( renderer->pick_enabled && command->pickable && command->element_id >= 0 &&
        (command->pick_aabb
                ? ToriDraw_ProjectedModelContainsAabb(
                      renderer->scene, renderer->pick_mouse_x, renderer->pick_mouse_y)
                : ToriDraw_ProjectedModelContainsPoint(
                      renderer->scene,
                      command->model,
                      &renderer->cur_3d.view_port,
                      renderer->pick_mouse_x,
                      renderer->pick_mouse_y)) )
        ToriRS_PickHitsAdd(
            &renderer->pick_hits,
            command->element_id,
            command->pick_terrain,
            command->pick_tile_x,
            command->pick_tile_z,
            command->pick_tile_level);

    face_count = ToriDraw_RenderModel2SortFaces(command->model, renderer->scene);
    if( face_count <= 0 )
        return;
    dynamic = command->dynamic || command->element_id < 0;
    anim_index = dynamic ? d3d9_clampi(command->anim_index, 0, TRSPK_POSE_TRACK_COUNT - 1) : 0;
    pose_id = dynamic && command->anim_frame >= 0 ? command->anim_frame : 0;
    group = dynamic ? TRSPK_VBO_GROUP_DYNAMIC : TRSPK_VBO_GROUP_STATIC;
    if( dynamic )
    {
        vertex_base = d3d9_bake_into_arena(
            renderer,
            &renderer->groups[group],
            command->element_id,
            anim_index,
            pose_id,
            command->model,
            &command->world_position,
            false);
    }
    else if( !trspk_pose_table_get(
                 &renderer->poses,
                 command->element_id,
                 anim_index,
                 pose_id,
                 &vertex_base) )
    {
        vertex_base = d3d9_bake_into_arena(
            renderer,
            &renderer->groups[group],
            command->element_id,
            anim_index,
            pose_id,
            command->model,
            &command->world_position,
            true);
    }
    if( vertex_base == UINT32_MAX )
        return;

    page_base = vertex_base & ~(D3D9_VBO_PAGE - 1u);
    local_base = vertex_base - page_base;
    face_order = ToriDraw_FaceOrder(renderer->scene);
    for( i = 0; i < face_count; i++ )
    {
        uint32_t face = (uint32_t)face_order[i];
        uint32_t base = local_base + face * 3u;
        uint16_t indices[3];
        if( base + 2u >= D3D9_VBO_PAGE )
            continue;
        indices[0] = (uint16_t)base;
        indices[1] = (uint16_t)(base + 1u);
        indices[2] = (uint16_t)(base + 2u);
        trspk_ibochain_push16(renderer->ibo_chain, group, page_base, indices, 3u);
    }
}

static void
d3d9_set_full_viewport(struct ToriRS_D3D9* renderer)
{
    D3DVIEWPORT9 viewport;
    if( renderer->client_w <= 0 || renderer->client_h <= 0 )
        return;
    memset(&viewport, 0, sizeof(viewport));
    viewport.Width = (DWORD)renderer->client_w;
    viewport.Height = (DWORD)renderer->client_h;
    viewport.MinZ = 0.0f;
    viewport.MaxZ = 1.0f;
    IDirect3DDevice9_SetViewport(renderer->device, &viewport);
}

static bool
d3d9_grow_draw_ranges(struct ToriRS_D3D9* renderer, uint32_t needed)
{
    struct TRSPK_DrawRange* grown;
    uint32_t capacity;
    if( needed <= renderer->draw_ranges->capacity )
        return true;
    capacity = renderer->draw_ranges->capacity;
    while( capacity < needed )
        capacity *= 2u;
    grown = (struct TRSPK_DrawRange*)realloc(
        renderer->draw_ranges->items, (size_t)capacity * sizeof(*grown));
    if( !grown )
        return false;
    renderer->draw_ranges->items = grown;
    renderer->draw_ranges->capacity = capacity;
    return true;
}

static void
d3d9_end_3d(struct ToriRS_D3D9* renderer)
{
    uint32_t total_indices = 0u;
    struct TRSPK_IBOChainNode* node;
    void* locked = NULL;
    const struct TRSPK_Triangles* triangles[TRSPK_VBO_GROUP_COUNT];
    uint32_t built;
    const struct TRSPK_DrawRange* range;
    uint32_t last_group = UINT32_MAX;
    uint32_t last_config = UINT32_MAX;
    uint32_t group;

    if( !renderer->has_3d )
        goto done;
    if( trspk_atlas_is_dirty(&renderer->atlas) && !d3d9_upload_atlas(renderer) )
        goto done;
    for( group = 0u; group < TRSPK_VBO_GROUP_COUNT; group++ )
        if( !d3d9_upload_group(renderer, &renderer->groups[group]) )
            goto done;
    if( !renderer->ibo_chain || !renderer->ibo_chain->head )
        goto done;
    for( node = renderer->ibo_chain->head; node; node = node->next )
        total_indices += node->ibo.index_count;
    if( total_indices == 0u || !d3d9_ensure_ibo(renderer, total_indices) ||
        !d3d9_grow_draw_ranges(renderer, total_indices / 3u + 1u) )
        goto done;
    if( FAILED(IDirect3DIndexBuffer9_Lock(
            renderer->ibo,
            0u,
            (UINT)(total_indices * sizeof(uint16_t)),
            &locked,
            D3DLOCK_DISCARD)) )
        goto done;

    triangles[TRSPK_VBO_GROUP_STATIC] =
        &renderer->groups[TRSPK_VBO_GROUP_STATIC].triangles;
    triangles[TRSPK_VBO_GROUP_DYNAMIC] =
        &renderer->groups[TRSPK_VBO_GROUP_DYNAMIC].triangles;
    built = trspk_drawrangeex_build16(
        renderer->draw_ranges, triangles, renderer->ibo_chain, (uint16_t*)locked);
    IDirect3DIndexBuffer9_Unlock(renderer->ibo);
    if( built != total_indices )
        goto done;

    d3d9_set_world_states(renderer);
    IDirect3DDevice9_SetIndices(renderer->device, renderer->ibo);
    range = trspk_drawrangelist_head(renderer->draw_ranges);
    while( range )
    {
        uint32_t index_count = range->end - range->start;
        if( index_count >= 3u && range->group < TRSPK_VBO_GROUP_COUNT )
        {
            if( range->group != last_group )
            {
                IDirect3DVertexBuffer9* vbo = renderer->groups[range->group].vbo_gpu;
                if( !vbo )
                {
                    range = trspk_drawrangelist_next(renderer->draw_ranges, range);
                    continue;
                }
                IDirect3DDevice9_SetStreamSource(
                    renderer->device,
                    0u,
                    vbo,
                    0u,
                    (UINT)sizeof(struct TRSPK_VertexD3D9));
                last_group = range->group;
            }
            if( range->config_idx != last_config )
            {
                if( range->config_idx == 0u )
                    d3d9_bind_atlas(renderer);
                else
                {
                    int tex_id = (int)range->config_idx - 1;
                    if( tex_id >= 0 && tex_id < TORIDRAW_TEXTURE_ID_CAPACITY &&
                        renderer->animated_textures[tex_id] )
                        d3d9_bind_animated(renderer, tex_id);
                    else
                    {
                        d3d9_disable_texture_transform(renderer->device);
                        d3d9_set_no_texture(renderer->device);
                    }
                }
                last_config = range->config_idx;
            }
            IDirect3DDevice9_DrawIndexedPrimitive(
                renderer->device,
                D3DPT_TRIANGLELIST,
                (INT)range->base_offset,
                (UINT)range->min_vertex,
                (UINT)(range->max_vertex - range->min_vertex + 1u),
                (UINT)range->start,
                (UINT)(index_count / 3u));
        }
        range = trspk_drawrangelist_next(renderer->draw_ranges, range);
    }
    d3d9_disable_texture_transform(renderer->device);

done:
    renderer->has_3d = false;
    renderer->in3d = false;
    if( renderer->ibo_chain )
        trspk_ibochain_reset(renderer->ibo_chain);
    d3d9_set_full_viewport(renderer);
}

static void
d3d9_begin_2d(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand* command)
{
    size_t pixels;
    size_t i;
    if( !renderer->scene || !d3d9_ensure_overlay_buffers(renderer) )
    {
        renderer->in2d = false;
        return;
    }
    pixels = (size_t)renderer->width * (size_t)renderer->height;
    for( i = 0u; i < pixels; i++ )
    {
        renderer->overlay_black[i] = (int)0xff000000u;
        renderer->overlay_white[i] = (int)0xffffffffu;
    }
    ToriRS_Soft3D_Init(
        &renderer->soft_black,
        renderer->scene,
        renderer->overlay_black,
        renderer->width,
        renderer->height);
    ToriRS_Soft3D_Init(
        &renderer->soft_white,
        renderer->scene,
        renderer->overlay_white,
        renderer->width,
        renderer->height);
    renderer->in2d = true;
    ToriRS_Soft3D_Execute(&renderer->soft_black, command);
    ToriRS_Soft3D_Execute(&renderer->soft_white, command);
}

static void
d3d9_end_2d(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand* command)
{
    if( !renderer->in2d )
        return;
    ToriRS_Soft3D_Execute(&renderer->soft_black, command);
    ToriRS_Soft3D_Execute(&renderer->soft_white, command);
    d3d9_set_full_viewport(renderer);
    d3d9_draw_overlay(renderer);
    renderer->in2d = false;
}

static void
d3d9_batch_add(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand_Batch* command,
    bool animated)
{
    if( !command || command->element_id < 0 || command->model.kind == TORIDRAWMK_NONE )
        return;
    (void)d3d9_bake_into_arena(
        renderer,
        &renderer->groups[TRSPK_VBO_GROUP_STATIC],
        command->element_id,
        animated ? command->anim_index : 0,
        command->pose_id,
        command->model,
        &command->world_position,
        true);
}

static void
d3d9_dispatch(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand* command)
{
    if( !renderer || !command )
        return;
    switch( command->kind )
    {
    case TORIRSRC_BEGIN_3D:
        d3d9_begin_3d(renderer, &command->u.begin_3d);
        break;
    case TORIRSRC_END_3D:
        d3d9_end_3d(renderer);
        break;
    case TORIRSRC_BEGIN_2D:
        d3d9_begin_2d(renderer, command);
        break;
    case TORIRSRC_END_2D:
        d3d9_end_2d(renderer, command);
        break;
    case TORIRSRC_TEX_LOAD:
        if( command->u.tex_load.texture )
            (void)d3d9_load_texture_object(
                renderer,
                command->u.tex_load.texture_id,
                command->u.tex_load.texture);
        break;
    case TORIRSRC_TEX_UNLOAD:
        d3d9_unload_texture(renderer, command->u.tex_load.texture_id);
        break;
    case TORIRSRC_MODEL_LOAD:
        d3d9_model_load(renderer, &command->u.model_load);
        break;
    case TORIRSRC_MODEL_UNLOAD:
        d3d9_model_unload(renderer, command->u.model_load.element_id);
        break;
    case TORIRSRC_ANIM_LOAD:
        d3d9_animation_load(renderer, &command->u.anim_load);
        break;
    case TORIRSRC_ANIM_UNLOAD:
        d3d9_model_unload(renderer, command->u.model_load.element_id);
        break;
    case TORIRSRC_BATCH3D_BEGIN:
        break;
    case TORIRSRC_BATCH3D_MODEL_ADD:
        d3d9_batch_add(renderer, &command->u.batch, false);
        break;
    case TORIRSRC_BATCH3D_ANIM_ADD:
        d3d9_batch_add(renderer, &command->u.batch, true);
        break;
    case TORIRSRC_BATCH3D_END:
        if( renderer->groups[TRSPK_VBO_GROUP_STATIC].vbo_cpu )
            trspk_vbo_set_dirty(renderer->groups[TRSPK_VBO_GROUP_STATIC].vbo_cpu);
        break;
    case TORIRSRC_BATCH3D_CLEAR:
        trspk_pose_table_clear(&renderer->poses);
        d3d9_reset_group(&renderer->groups[TRSPK_VBO_GROUP_STATIC]);
        break;
    case TORIRSRC_DRAW_MODEL:
        d3d9_draw_model(renderer, &command->u.model);
        break;

    case TORIRSRC_CLEAR_RECT:
    case TORIRSRC_FILL_RECT:
    case TORIRSRC_DRAW_MODEL_WIDGET:
    case TORIRSRC_SPRITE:
    case TORIRSRC_FONT:
    case TORIRSRC_LINE:
    case TORIRSRC_TEX_BEGIN:
    case TORIRSRC_TEX_END:
    case TORIRSRC_SPRITE_BEGIN:
    case TORIRSRC_SPRITE_END:
    case TORIRSRC_FONT_BEGIN:
    case TORIRSRC_FONT_END:
        if( renderer->in2d )
        {
            ToriRS_Soft3D_Execute(&renderer->soft_black, command);
            ToriRS_Soft3D_Execute(&renderer->soft_white, command);
        }
        break;

    /* Sprite/font assets live in ToriDraw_Scene.  The compatibility Soft3D
     * executors intentionally no-op these retained-resource notifications. */
    case TORIRSRC_SPRITE_LOAD:
    case TORIRSRC_SPRITE_UNLOAD:
    case TORIRSRC_FONT_LOAD:
    case TORIRSRC_FONT_UNLOAD:
    case TORIRSRC_NONE:
        break;
    }
}

static void
d3d9_draw_solid_rect(
    struct ToriRS_D3D9* renderer,
    int x,
    int y,
    int width,
    int height,
    D3DCOLOR color)
{
    struct D3D9OverlayVertex quad[4];
    float x0;
    float y0;
    float x1;
    float y1;
    if( width <= 0 || height <= 0 || renderer->width <= 0 || renderer->height <= 0 )
        return;
    x0 = (float)renderer->lb_x + (float)x * renderer->lb_w / renderer->width - 0.5f;
    y0 = (float)renderer->lb_y + (float)y * renderer->lb_h / renderer->height - 0.5f;
    x1 = (float)renderer->lb_x + (float)(x + width) * renderer->lb_w / renderer->width - 0.5f;
    y1 = (float)renderer->lb_y + (float)(y + height) * renderer->lb_h / renderer->height - 0.5f;
    quad[0] = (struct D3D9OverlayVertex){ x0, y0, 0.0f, 1.0f, color, 0.0f, 0.0f };
    quad[1] = (struct D3D9OverlayVertex){ x1, y0, 0.0f, 1.0f, color, 0.0f, 0.0f };
    quad[2] = (struct D3D9OverlayVertex){ x0, y1, 0.0f, 1.0f, color, 0.0f, 0.0f };
    quad[3] = (struct D3D9OverlayVertex){ x1, y1, 0.0f, 1.0f, color, 0.0f, 0.0f };
    IDirect3DDevice9_SetVertexShader(renderer->device, NULL);
    IDirect3DDevice9_SetPixelShader(renderer->device, NULL);
    IDirect3DDevice9_SetFVF(renderer->device, D3D9_OVERLAY_FVF);
    IDirect3DDevice9_SetRenderState(renderer->device, D3DRS_ZENABLE, D3DZB_FALSE);
    IDirect3DDevice9_SetRenderState(renderer->device, D3DRS_ZWRITEENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(renderer->device, D3DRS_ALPHATESTENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(renderer->device, D3DRS_ALPHABLENDENABLE, FALSE);
    d3d9_set_no_texture(renderer->device);
    IDirect3DDevice9_DrawPrimitiveUP(
        renderer->device, D3DPT_TRIANGLESTRIP, 2u, quad, (UINT)sizeof(quad[0]));
}

struct ToriRS_D3D9*
ToriRS_D3D9_New(int width, int height)
{
    struct ToriRS_D3D9* renderer;
    static uint8_t white_tile[TRSPK_ATLAS_TILE * TRSPK_ATLAS_TILE * 4u];
    uint32_t group;
    int texture;
    if( width <= 0 || height <= 0 )
        return NULL;
    renderer = (struct ToriRS_D3D9*)calloc(1u, sizeof(*renderer));
    if( !renderer )
        return NULL;
    renderer->width = width;
    renderer->height = height;
    renderer->tex_slot_next = 1u;
    for( texture = 0; texture < TORIDRAW_TEXTURE_ID_CAPACITY; texture++ )
        renderer->tex_slot_of_id[texture] = -1;

    trspk_pose_table_init(&renderer->poses);
    renderer->ibo_chain = trspk_ibochain_create(TRSPK_INDEX_FORMAT_U16);
    renderer->draw_ranges = trspk_drawrangelist_create(D3D9_DRAWRANGE_CAP);
    if( !renderer->ibo_chain || !renderer->draw_ranges ||
        !trspk_atlas_init_grid(
            &renderer->atlas,
            D3D9_ATLAS_DIM,
            D3D9_ATLAS_DIM,
            TRSPK_ATLAS_TILE,
            TRSPK_ATLAS_TILE,
            4u) )
    {
        ToriRS_D3D9_Free(renderer);
        return NULL;
    }
    memset(white_tile, 0xff, sizeof(white_tile));
    if( !trspk_atlas_grid_insert_at(
            &renderer->atlas,
            0u,
            white_tile,
            TRSPK_ATLAS_TILE * 4u,
            TRSPK_ATLAS_TILE,
            TRSPK_ATLAS_TILE,
            NULL) )
    {
        ToriRS_D3D9_Free(renderer);
        return NULL;
    }
    renderer->tex_resident[0] = 1u;

    for( group = 0u; group < TRSPK_VBO_GROUP_COUNT; group++ )
    {
        struct D3D9ModelGroup* model_group = &renderer->groups[group];
        model_group->vbo_cpu = trspk_vbo_create(0u, TRSPK_VERTEX_FORMAT_D3D9);
        if( !model_group->vbo_cpu )
        {
            ToriRS_D3D9_Free(renderer);
            return NULL;
        }
        model_group->arena = trspk_modelarena_create(
            model_group->vbo_cpu, &model_group->triangles, D3D9_VBO_PAGE, 64u);
        if( !model_group->arena )
        {
            ToriRS_D3D9_Free(renderer);
            return NULL;
        }
        model_group->reset_each_frame = group == TRSPK_VBO_GROUP_DYNAMIC;
    }
    if( !d3d9_ensure_overlay_buffers(renderer) )
    {
        ToriRS_D3D9_Free(renderer);
        return NULL;
    }
    return renderer;
}

void
ToriRS_D3D9_Free(struct ToriRS_D3D9* renderer)
{
    uint32_t group;
    int texture;
    if( !renderer )
        return;
    if( renderer->scene_active )
        d3d9_end_frame_scene(renderer);
    d3d9_release_default_pool(renderer);
    d3d9_release_overlay_texture(renderer);
    if( renderer->atlas_texture )
        IDirect3DTexture9_Release(renderer->atlas_texture);
    for( texture = 0; texture < TORIDRAW_TEXTURE_ID_CAPACITY; texture++ )
        if( renderer->animated_textures[texture] )
            IDirect3DTexture9_Release(renderer->animated_textures[texture]);
    for( group = 0u; group < TRSPK_VBO_GROUP_COUNT; group++ )
    {
        if( renderer->groups[group].vbo_gpu )
            IDirect3DVertexBuffer9_Release(renderer->groups[group].vbo_gpu);
    }
    if( renderer->device )
        IDirect3DDevice9_Release(renderer->device);
    if( renderer->d3d )
        IDirect3D9_Release(renderer->d3d);

    for( group = 0u; group < TRSPK_VBO_GROUP_COUNT; group++ )
    {
        if( renderer->groups[group].arena )
            trspk_modelarena_free(renderer->groups[group].arena);
        if( renderer->groups[group].vbo_cpu )
            trspk_vbo_free(renderer->groups[group].vbo_cpu);
        trspk_triangles_free(&renderer->groups[group].triangles);
    }
    if( renderer->ibo_chain )
        trspk_ibochain_free(renderer->ibo_chain);
    if( renderer->draw_ranges )
        trspk_drawrangelist_free(renderer->draw_ranges);
    trspk_pose_table_free(&renderer->poses);
    trspk_atlas_free(&renderer->atlas);
    free(renderer->overlay_black);
    free(renderer->overlay_white);
    free(renderer);
}

bool
ToriRS_D3D9_Init(
    struct ToriRS_D3D9* renderer,
    void* native_window,
    struct ToriDraw_Scene* scene)
{
    DWORD behavior;
    HRESULT hr;
    int width;
    int height;
    if( !renderer || !native_window || !scene || renderer->device )
        return false;
    renderer->hwnd = (HWND)native_window;
    renderer->scene = scene;
    if( !d3d9_read_client_size(renderer, &width, &height) || width <= 0 || height <= 0 )
        return false;
    renderer->client_w = width;
    renderer->client_h = height;
    renderer->d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if( !renderer->d3d )
    {
        fprintf(stderr, "D3D9: Direct3DCreate9 failed\n");
        return false;
    }
    memset(&renderer->caps, 0, sizeof(renderer->caps));
    (void)IDirect3D9_GetDeviceCaps(
        renderer->d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &renderer->caps);
    if( renderer->caps.MaxTextureWidth < D3D9_ATLAS_DIM ||
        renderer->caps.MaxTextureHeight < D3D9_ATLAS_DIM )
    {
        fprintf(
            stderr,
            "D3D9: adapter texture cap %lux%lu is below the required 2048x2048 atlas\n",
            (unsigned long)renderer->caps.MaxTextureWidth,
            (unsigned long)renderer->caps.MaxTextureHeight);
        return false;
    }

    memset(&renderer->present, 0, sizeof(renderer->present));
    renderer->present.BackBufferWidth = (UINT)width;
    renderer->present.BackBufferHeight = (UINT)height;
    renderer->present.BackBufferFormat = D3DFMT_UNKNOWN;
    renderer->present.BackBufferCount = 1u;
    renderer->present.MultiSampleType = D3DMULTISAMPLE_NONE;
    renderer->present.SwapEffect = D3DSWAPEFFECT_DISCARD;
    renderer->present.hDeviceWindow = renderer->hwnd;
    renderer->present.Windowed = TRUE;
    renderer->present.EnableAutoDepthStencil = FALSE;
    renderer->present.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
    behavior = D3DCREATE_FPU_PRESERVE;
    if( renderer->caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT )
        behavior |= D3DCREATE_HARDWARE_VERTEXPROCESSING;
    else
        behavior |= D3DCREATE_SOFTWARE_VERTEXPROCESSING;
    hr = IDirect3D9_CreateDevice(
        renderer->d3d,
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        renderer->hwnd,
        behavior,
        &renderer->present,
        &renderer->device);
    if( FAILED(hr) && (behavior & D3DCREATE_HARDWARE_VERTEXPROCESSING) )
    {
        behavior &= ~D3DCREATE_HARDWARE_VERTEXPROCESSING;
        behavior |= D3DCREATE_SOFTWARE_VERTEXPROCESSING;
        hr = IDirect3D9_CreateDevice(
            renderer->d3d,
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL,
            renderer->hwnd,
            behavior,
            &renderer->present,
            &renderer->device);
    }
    if( FAILED(hr) )
    {
        d3d9_log_hr("CreateDevice", hr);
        renderer->device = NULL;
        return false;
    }
    (void)IDirect3DDevice9_GetDeviceCaps(renderer->device, &renderer->caps);
    d3d9_update_letterbox(renderer);
    d3d9_restore_after_reset(renderer);
    if( !d3d9_upload_atlas(renderer) )
        return false;
    return true;
}

void
ToriRS_D3D9_SetViewport(struct ToriRS_D3D9* renderer, int width, int height)
{
    if( !renderer || width <= 0 || height <= 0 ||
        (renderer->width == width && renderer->height == height) )
        return;
    renderer->width = width;
    renderer->height = height;
    d3d9_update_letterbox(renderer);
    renderer->in2d = false;
    (void)d3d9_ensure_overlay_buffers(renderer);
}

void
ToriRS_D3D9_SetPick(struct ToriRS_D3D9* renderer, int mouse_x, int mouse_y)
{
    if( !renderer )
        return;
    renderer->pick_enabled = true;
    renderer->pick_mouse_x = mouse_x;
    renderer->pick_mouse_y = mouse_y;
    ToriRS_PickHitsReset(&renderer->pick_hits);
}

struct ToriRS_PickHits const*
ToriRS_D3D9_PickHits(struct ToriRS_D3D9 const* renderer)
{
    return renderer ? &renderer->pick_hits : NULL;
}

void
ToriRS_D3D9_Execute(
    struct ToriRS_D3D9* renderer,
    struct ToriRS_RenderCommand const* command)
{
    d3d9_dispatch(renderer, command);
}

void
ToriRS_D3D9_DrawBootBar(struct ToriRS_D3D9* renderer, int progress)
{
    int bar_w;
    int bar_h = 12;
    int bar_x;
    int bar_y;
    int fill_w;
    if( !renderer || !d3d9_begin_frame_scene(renderer) )
        return;
    progress = d3d9_clampi(progress, 0, 100);
    d3d9_set_full_viewport(renderer);
    bar_w = renderer->width / 3;
    bar_x = (renderer->width - bar_w) / 2;
    bar_y = (renderer->height - bar_h) / 2;
    fill_w = bar_w * progress / 100;
    d3d9_draw_solid_rect(renderer, bar_x - 1, bar_y - 1, bar_w + 2, bar_h + 2, 0xff8b0000u);
    if( fill_w > 0 )
        d3d9_draw_solid_rect(renderer, bar_x, bar_y, fill_w, bar_h, 0xff8b0000u);
    if( fill_w < bar_w )
        d3d9_draw_solid_rect(
            renderer, bar_x + fill_w, bar_y, bar_w - fill_w, bar_h, 0xff000000u);
    d3d9_end_frame_scene(renderer);
}

void
ToriRS_D3D9_RenderFrame(struct ToriRS_D3D9* renderer, struct ToriRS_Frame* frame)
{
    struct ToriRS_RenderCommand command;
    if( !renderer || !frame || !d3d9_begin_frame_scene(renderer) )
        return;
    renderer->has_3d = false;
    renderer->in3d = false;
    renderer->in2d = false;
    renderer->frame_clock += 1.0;
    ToriRS_FrameBegin(frame);
    while( ToriRS_FrameNextCommand(frame, &command) )
        d3d9_dispatch(renderer, &command);
    ToriRS_FrameEnd(frame);
    if( renderer->in3d )
        d3d9_end_3d(renderer);
    if( renderer->in2d )
    {
        struct ToriRS_RenderCommand end_command;
        memset(&end_command, 0, sizeof(end_command));
        end_command.kind = TORIRSRC_END_2D;
        d3d9_end_2d(renderer, &end_command);
    }
    d3d9_end_frame_scene(renderer);
}

void
ToriRS_D3D9_Present(struct ToriRS_D3D9* renderer)
{
    HRESULT hr;
    if( !renderer || renderer->scene_active || !d3d9_device_ready(renderer) )
        return;
    hr = IDirect3DDevice9_Present(renderer->device, NULL, NULL, NULL, NULL);
    if( hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET )
        renderer->reset_pending = true;
    else if( FAILED(hr) )
        d3d9_log_hr("Present", hr);
}
