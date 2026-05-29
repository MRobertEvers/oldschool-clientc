#include "platform_sdl2_renderer_d3d9.h"

#include "graphics/uv_pnm.h"
#include "libtorirs.h"
#include "platformkit/core/trspk_vbo.h"
#include "render/libtorirs_render.h"
#include "toridraw/toridraw.h"

#include <SDL.h>
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)

#include <SDL_syswm.h>

#include <d3d9.h>

#define TRSPK_D3D9_VB_CAPACITY 4096
#define TRSPK_EBO_SIZE 4096

struct LibToriPlatformSDL2_RendererD3D9
{
    SDL_Window* window;
    IDirect3D9* d3d;
    IDirect3DDevice9* device;
    IDirect3DVertexBuffer9* vb;
    IDirect3DIndexBuffer9* ib;

    struct TRSPK_VBO* vbo_cpu;
    uint16_t ibo_16[TRSPK_EBO_SIZE];
    uint16_t ibo_16_size;

    int width;
    int height;

    float view[16];
    float proj[16];
    struct LibToriRS_RenderCommand_Begin3D cur_3d;
    bool has_3d;
    bool in3d;
    double frame_clock;
};

/* -----------------------------------------------------------------------
 * Matrix / clip-space helpers
 * ----------------------------------------------------------------------- */

/* Multiply two column-major 4x4 matrices: out = a * b. */
static void
mat4_mul_colmajor(const float* a, const float* b, float* out)
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

/* Remap projection Z from OpenGL [-1,1] NDC to D3D9 [0,1] NDC. */
static void
d3d9_remap_projection_z(float* proj_colmajor)
{
    static const float k_clip_z[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.5f, 0.0f,
        0.0f, 0.0f, 0.5f, 1.0f,
    };
    float tmp[16];
    mat4_mul_colmajor(k_clip_z, proj_colmajor, tmp);
    memcpy(proj_colmajor, tmp, sizeof(tmp));
}

/* -----------------------------------------------------------------------
 * Math helpers (identical to opengl3 / webgl1 renderers)
 * ----------------------------------------------------------------------- */

static void
compute_view_matrix(
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
compute_projection_matrix(
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

static void
compute_pass_matrices(
    float view[16],
    float proj[16],
    const struct LibToriRS_RenderCommand_Begin3D* b3d,
    int fallback_w,
    int fallback_h)
{
    const struct ToriDraw_Position* cam_pos = &b3d->camera_position;
    const struct ToriDraw_Camera* cam = &b3d->camera;
    const struct ToriDraw_ViewPort* vp = &b3d->view_port;
    const int pass_w = vp->width > 0 ? vp->width : fallback_w;
    const int pass_h = vp->height > 0 ? vp->height : fallback_h;

    compute_view_matrix(
        view,
        -(float)cam_pos->x,
        -(float)cam_pos->y,
        -(float)cam_pos->z,
        toridraw_angle_to_radians(cam->pitch),
        toridraw_angle_to_radians(cam->yaw));
    compute_projection_matrix(proj, (90.0f * (float)M_PI) / 180.0f, (float)pass_w, (float)pass_h);
    d3d9_remap_projection_z(proj);
}

/* -----------------------------------------------------------------------
 * Colour conversion
 * ----------------------------------------------------------------------- */

static void
hsl16_to_rgba(
    uint16_t hsl16,
    uint8_t alpha,
    float rgba[4])
{
    uint32_t rgb = toridraw_hsl16_to_rgb(hsl16);
    rgba[0] = (float)((rgb >> 16) & 0xFFu) / 255.0f;
    rgba[1] = (float)((rgb >> 8) & 0xFFu) / 255.0f;
    rgba[2] = (float)(rgb & 0xFFu) / 255.0f;
    rgba[3] = (float)alpha / 255.0f;
}

/* -----------------------------------------------------------------------
 * Model helpers (mirrors opengl3 / webgl1)
 * ----------------------------------------------------------------------- */

static struct ToriDraw_Model*
get_model(struct ToriDraw_ModelHandle model_handle)
{
    return model_handle.u.model.model;
}

static struct ToriDraw_Context*
get_context(struct LibToriRS_Instance* instance)
{
    return LibToriRS_GetCurrentToriDrawContext(instance);
}

static void
uv_pnm_face(
    struct UVFaceCoords* out,
    struct ToriDraw_Model* model,
    uint32_t face_index)
{
    uint32_t face_a = model->face_indices_a[face_index];
    uint32_t face_b = model->face_indices_b[face_index];
    uint32_t face_c = model->face_indices_c[face_index];

    uint32_t texture_face = face_index;
    uint32_t p_vertex = face_a;
    uint32_t m_vertex = face_b;
    uint32_t n_vertex = face_c;

    if( model->face_texture_coords && model->face_texture_coords[face_index] != -1 )
    {
        assert(model->textured_p_coordinate != NULL);
        assert(model->textured_m_coordinate != NULL);
        assert(model->textured_n_coordinate != NULL);

        texture_face = (uint32_t)model->face_texture_coords[face_index];
        p_vertex = model->textured_p_coordinate[texture_face];
        m_vertex = model->textured_m_coordinate[texture_face];
        n_vertex = model->textured_n_coordinate[texture_face];
    }
    else
    {
        texture_face = face_index;
        p_vertex = model->face_indices_a[texture_face];
        m_vertex = model->face_indices_b[texture_face];
        n_vertex = model->face_indices_c[texture_face];
    }

    uv_pnm_compute(
        out,
        model->vertices_x[p_vertex],
        model->vertices_y[p_vertex],
        model->vertices_z[p_vertex],
        model->vertices_x[m_vertex],
        model->vertices_y[m_vertex],
        model->vertices_z[m_vertex],
        model->vertices_x[n_vertex],
        model->vertices_y[n_vertex],
        model->vertices_z[n_vertex],
        model->vertices_x[face_a],
        model->vertices_y[face_a],
        model->vertices_z[face_a],
        model->vertices_x[face_b],
        model->vertices_y[face_b],
        model->vertices_z[face_b],
        model->vertices_x[face_c],
        model->vertices_y[face_c],
        model->vertices_z[face_c]);
}

/* -----------------------------------------------------------------------
 * D3D9 helpers
 * ----------------------------------------------------------------------- */

/* Load a column-major float[16] into a D3DMATRIX.
 * Our matrices are column-major (v' = M * v). D3D uses row-vector convention
 * (v' = v * M), so a column-major source matrix must be transposed when loaded.
 * D3DMATRIX stores elements in row-major order, so a raw memcpy of a
 * column-major matrix achieves the required transpose automatically. */
static void
float16_to_d3dmatrix(
    const float* m,
    D3DMATRIX* out)
{
    memcpy(out, m, 16 * sizeof(float));
}

static void
d3d9_set_static_render_states(IDirect3DDevice9* dev)
{
    IDirect3DDevice9_SetRenderState(dev, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ZENABLE, D3DZB_FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ZWRITEENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_COLORVERTEX, TRUE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);

    /* No texturing initially; use diffuse vertex colour only. */
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    IDirect3DDevice9_SetTextureStageState(dev, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
}

/* -----------------------------------------------------------------------
 * Render-command dispatch
 * ----------------------------------------------------------------------- */

static void
d3d9_handle_render_command(
    struct LibToriPlatformSDL2_RendererD3D9* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    IDirect3DDevice9* dev = renderer->device;

    switch( command->kind )
    {
    case TORIRSRC_BEGIN_3D:
    {
        if( !renderer->in3d )
            d3d9_set_static_render_states(dev);

        renderer->cur_3d = command->u.begin_3d;
        renderer->has_3d = true;
        renderer->in3d = true;

        compute_pass_matrices(
            renderer->view, renderer->proj, &renderer->cur_3d, renderer->width, renderer->height);

        {
            const struct ToriDraw_ViewPort* vp = &renderer->cur_3d.view_port;
            DWORD vp_w = (DWORD)(vp->width > 0 ? vp->width : renderer->width);
            DWORD vp_h = (DWORD)(vp->height > 0 ? vp->height : renderer->height);
            D3DVIEWPORT9 d3d_vp = { 0, 0, vp_w, vp_h, 0.0f, 1.0f };
            IDirect3DDevice9_SetViewport(dev, &d3d_vp);
        }

        D3DMATRIX d3d_view;
        D3DMATRIX d3d_proj;
        float16_to_d3dmatrix(renderer->view, &d3d_view);
        float16_to_d3dmatrix(renderer->proj, &d3d_proj);
        IDirect3DDevice9_SetTransform(dev, D3DTS_VIEW, &d3d_view);
        IDirect3DDevice9_SetTransform(dev, D3DTS_PROJECTION, &d3d_proj);

        D3DMATRIX identity = { { { 1.0f,
                                   0.0f,
                                   0.0f,
                                   0.0f,
                                   0.0f,
                                   1.0f,
                                   0.0f,
                                   0.0f,
                                   0.0f,
                                   0.0f,
                                   1.0f,
                                   0.0f,
                                   0.0f,
                                   0.0f,
                                   0.0f,
                                   1.0f } } };
        IDirect3DDevice9_SetTransform(dev, D3DTS_WORLD, &identity);
        break;
    }

    case TORIRSRC_END_3D:
        renderer->has_3d = false;
        renderer->in3d = false;
        break;

    case TORIRSRC_BEGIN_2D:
    case TORIRSRC_END_2D:
    case TORIRSRC_CLEAR_RECT:
        break;

    case TORIRSRC_MODEL_LOAD:
    {
        struct ToriDraw_Model* model = get_model(command->u.model_load.model);
        if( !model )
            break;

        uint32_t gpu_vertex_count = model->face_count * 3;
        trspk_vbo_ensure_capacity(renderer->vbo_cpu, gpu_vertex_count);

        uint32_t face_index = 0;
        for( uint32_t i = 0; i < gpu_vertex_count; i += 3, face_index++ )
        {
            uint32_t face_a = model->face_indices_a[face_index];
            uint32_t face_b = model->face_indices_b[face_index];
            uint32_t face_c = model->face_indices_c[face_index];

            uint16_t color_a_hsl16 = model->face_colors_a[face_index];
            uint16_t color_b_hsl16 = model->face_colors_b[face_index];
            uint16_t color_c_hsl16 = model->face_colors_c[face_index];
            uint8_t alpha = model->face_alphas ? model->face_alphas[face_index] : 0xFFu;

            float color_a[4], color_b[4], color_c[4];
            hsl16_to_rgba(color_a_hsl16, alpha, color_a);
            hsl16_to_rgba(color_b_hsl16, alpha, color_b);
            hsl16_to_rgba(color_c_hsl16, alpha, color_c);

            struct UVFaceCoords uv;
            uv_pnm_face(&uv, model, face_index);

            trspk_vbo_write_vertex_d3d9(
                renderer->vbo_cpu,
                i + 0,
                model->vertices_x[face_a],
                model->vertices_y[face_a],
                model->vertices_z[face_a],
                color_a,
                uv.u1,
                uv.v1,
                -1.0f);
            trspk_vbo_write_vertex_d3d9(
                renderer->vbo_cpu,
                i + 1,
                model->vertices_x[face_b],
                model->vertices_y[face_b],
                model->vertices_z[face_b],
                color_b,
                uv.u2,
                uv.v2,
                -1.0f);
            trspk_vbo_write_vertex_d3d9(
                renderer->vbo_cpu,
                i + 2,
                model->vertices_x[face_c],
                model->vertices_y[face_c],
                model->vertices_z[face_c],
                color_c,
                uv.u3,
                uv.v3,
                -1.0f);
        }

        /* Upload cpu-side vertices into the D3D9 vertex buffer. */
        if( renderer->vb )
        {
            void* locked = NULL;
            UINT byte_size = (UINT)(gpu_vertex_count * sizeof(struct TRSPK_VertexD3D9));
            if( SUCCEEDED(IDirect3DVertexBuffer9_Lock(
                    renderer->vb, 0, byte_size, &locked, D3DLOCK_DISCARD)) )
            {
                memcpy(locked, renderer->vbo_cpu->vertices.as_d3d9, byte_size);
                IDirect3DVertexBuffer9_Unlock(renderer->vb);
            }
            else
            {
                fprintf(stderr, "D3D9: Lock failed\n");
                fprintf(stderr, "D3D9: Error: %s\n", SDL_GetError());
                assert(0);
                break;
            }
        }

        break;
    }

    case TORIRSRC_MODEL_UNLOAD:
        break;

    case TORIRSRC_DRAW_MODEL:
    {
        (void)command->u.model.model;
        (void)command->u.model.position;
        (void)command->u.model.element_id;

        if( !renderer->has_3d || !renderer->vbo_cpu || renderer->vbo_cpu->vertex_count == 0u )
            break;

        struct ToriDraw_Context* ctx = get_context(instance);
        if( !ctx )
            break;

        uint32_t face_count = toridraw_face_order_count(ctx);
        if( face_count == 0u )
            break;

        int* face_order = toridraw_face_order(ctx);
        for( uint32_t i = 0; i < face_count; i++ )
        {
            uint32_t face = (uint32_t)face_order[i];
            uint16_t vertex_base = (uint16_t)(face * 3);
            renderer->ibo_16[i * 3 + 0] = vertex_base + 0;
            renderer->ibo_16[i * 3 + 1] = vertex_base + 1;
            renderer->ibo_16[i * 3 + 2] = vertex_base + 2;
        }
        renderer->ibo_16_size = (uint16_t)(face_count * 3);

        /* Upload indices. */
        if( renderer->ib )
        {
            void* locked = NULL;
            UINT byte_size = (UINT)(renderer->ibo_16_size * sizeof(uint16_t));
            if( SUCCEEDED(IDirect3DIndexBuffer9_Lock(
                    renderer->ib, 0, byte_size, &locked, D3DLOCK_DISCARD)) )
            {
                memcpy(locked, renderer->ibo_16, byte_size);
                IDirect3DIndexBuffer9_Unlock(renderer->ib);
            }
        }

        IDirect3DDevice9_SetFVF(dev, D3DFVF_TRSPK_COMPAT);
        IDirect3DDevice9_SetStreamSource(
            dev, 0, renderer->vb, 0, (UINT)sizeof(struct TRSPK_VertexD3D9));
        IDirect3DDevice9_SetIndices(dev, renderer->ib);
        IDirect3DDevice9_DrawIndexedPrimitive(
            dev, D3DPT_TRIANGLELIST, 0, 0, renderer->vbo_cpu->vertex_count, 0, (UINT)(face_count));
        break;
    }

    default:
        break;
    }
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

struct LibToriPlatformSDL2_RendererD3D9*
LibToriPlatformSDL2_RendererD3D9_New(
    int width,
    int height)
{
    struct LibToriPlatformSDL2_RendererD3D9* renderer =
        (struct LibToriPlatformSDL2_RendererD3D9*)malloc(
            sizeof(struct LibToriPlatformSDL2_RendererD3D9));
    if( !renderer )
        return NULL;
    memset(renderer, 0, sizeof(struct LibToriPlatformSDL2_RendererD3D9));
    renderer->width = width;
    renderer->height = height;
    renderer->vbo_cpu = trspk_vbo_create(0, TRSPK_VERTEX_FORMAT_D3D9);
    return renderer;
}

void
LibToriPlatformSDL2_RendererD3D9_Free(struct LibToriPlatformSDL2_RendererD3D9* renderer)
{
    if( !renderer )
        return;
    if( renderer->ib )
    {
        IDirect3DIndexBuffer9_Release(renderer->ib);
        renderer->ib = NULL;
    }
    if( renderer->vb )
    {
        IDirect3DVertexBuffer9_Release(renderer->vb);
        renderer->vb = NULL;
    }
    if( renderer->device )
    {
        IDirect3DDevice9_Release(renderer->device);
        renderer->device = NULL;
    }
    if( renderer->d3d )
    {
        IDirect3D9_Release(renderer->d3d);
        renderer->d3d = NULL;
    }
    if( renderer->vbo_cpu )
    {
        trspk_vbo_free(renderer->vbo_cpu);
        renderer->vbo_cpu = NULL;
    }
    free(renderer);
}

bool
LibToriPlatformSDL2_RendererD3D9_Init(
    struct LibToriPlatformSDL2_RendererD3D9* renderer,
    SDL_Window* window)
{
    /* Get the native Win32 HWND from SDL. */
    SDL_SysWMinfo wm_info;
    SDL_VERSION(&wm_info.version);
    if( !SDL_GetWindowWMInfo(window, &wm_info) )
    {
        fprintf(stderr, "D3D9: SDL_GetWindowWMInfo failed: %s\n", SDL_GetError());
        return false;
    }
    HWND hwnd = wm_info.info.win.window;

    renderer->d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if( !renderer->d3d )
    {
        fprintf(stderr, "D3D9: Direct3DCreate9 failed\n");
        return false;
    }

    D3DPRESENT_PARAMETERS pp;
    memset(&pp, 0, sizeof(pp));
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.hDeviceWindow = hwnd;
    pp.EnableAutoDepthStencil = FALSE;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    HRESULT hr = IDirect3D9_CreateDevice(
        renderer->d3d,
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        hwnd,
        D3DCREATE_HARDWARE_VERTEXPROCESSING,
        &pp,
        &renderer->device);

    if( FAILED(hr) )
    {
        /* Fallback to software vertex processing (e.g. WARP / reference). */
        hr = IDirect3D9_CreateDevice(
            renderer->d3d,
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL,
            hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            &pp,
            &renderer->device);
    }

    if( FAILED(hr) )
    {
        fprintf(stderr, "D3D9: CreateDevice failed (hr=0x%08lx)\n", (unsigned long)hr);
        IDirect3D9_Release(renderer->d3d);
        renderer->d3d = NULL;
        return false;
    }

    /* Create a dynamic vertex buffer. */
    hr = IDirect3DDevice9_CreateVertexBuffer(
        renderer->device,
        (UINT)(TRSPK_D3D9_VB_CAPACITY * sizeof(struct TRSPK_VertexD3D9)),
        D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
        D3DFVF_TRSPK_COMPAT,
        D3DPOOL_DEFAULT,
        &renderer->vb,
        NULL);
    if( FAILED(hr) )
    {
        fprintf(stderr, "D3D9: CreateVertexBuffer failed (hr=0x%08lx)\n", (unsigned long)hr);
        goto fail;
    }

    /* Create a dynamic index buffer (16-bit indices). */
    hr = IDirect3DDevice9_CreateIndexBuffer(
        renderer->device,
        (UINT)(TRSPK_EBO_SIZE * sizeof(uint16_t)),
        D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
        D3DFMT_INDEX16,
        D3DPOOL_DEFAULT,
        &renderer->ib,
        NULL);
    if( FAILED(hr) )
    {
        fprintf(stderr, "D3D9: CreateIndexBuffer failed (hr=0x%08lx)\n", (unsigned long)hr);
        goto fail;
    }

    d3d9_set_static_render_states(renderer->device);
    renderer->window = window;
    return true;

fail:
    LibToriPlatformSDL2_RendererD3D9_Free(renderer);
    return false;
}

void
LibToriPlatformSDL2_RendererD3D9_Render(
    struct LibToriPlatformSDL2_RendererD3D9* renderer,
    struct LibToriRS_Instance* instance)
{
    if( !renderer || !instance || !renderer->device )
        return;

    /* Track resize. */
    SDL_GetWindowSize(renderer->window, &renderer->width, &renderer->height);

    IDirect3DDevice9* dev = renderer->device;

    IDirect3DDevice9_Clear(dev, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);

    if( FAILED(IDirect3DDevice9_BeginScene(dev)) )
        return;

    renderer->in3d = false;
    renderer->has_3d = false;
    renderer->frame_clock += 1.0 / 50.0;

    LibToriRS_FrameBegin(instance);
    struct LibToriRS_RenderCommand command;
    while( LibToriRS_FrameNextCommand(instance, &command) )
        d3d9_handle_render_command(renderer, instance, &command);
    LibToriRS_FrameEnd(instance);

    IDirect3DDevice9_EndScene(dev);
    IDirect3DDevice9_Present(dev, NULL, NULL, NULL, NULL);
}

/* -----------------------------------------------------------------------
 * Non-Windows stubs (allow the file to compile on any host)
 * ----------------------------------------------------------------------- */
#else /* !_WIN32 */

struct LibToriPlatformSDL2_RendererD3D9
{
    int width;
    int height;
};

struct LibToriPlatformSDL2_RendererD3D9*
LibToriPlatformSDL2_RendererD3D9_New(
    int width,
    int height)
{
    (void)width;
    (void)height;
    return NULL;
}

void
LibToriPlatformSDL2_RendererD3D9_Free(struct LibToriPlatformSDL2_RendererD3D9* renderer)
{
    (void)renderer;
}

bool
LibToriPlatformSDL2_RendererD3D9_Init(
    struct LibToriPlatformSDL2_RendererD3D9* renderer,
    SDL_Window* window)
{
    (void)renderer;
    (void)window;
    return false;
}

void
LibToriPlatformSDL2_RendererD3D9_Render(
    struct LibToriPlatformSDL2_RendererD3D9* renderer,
    struct LibToriRS_Instance* instance)
{
    (void)renderer;
    (void)instance;
}

#endif /* _WIN32 */
