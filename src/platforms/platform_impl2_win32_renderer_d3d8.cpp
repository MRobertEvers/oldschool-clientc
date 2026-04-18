#if defined(_WIN32) && defined(TORIRS_HAS_D3D8)

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#ifndef WINVER
#define WINVER 0x0501
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <d3d8.h>

#include <cstdarg>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

extern "C" {
#include "graphics/dash.h"
#include "graphics/dash_model_internal.h"
#include "graphics/shared_tables.h"
#include "graphics/uv_pnm.h"
#include "osrs/game.h"
#include "osrs/ui_dirty.h"
#include "tori_rs.h"
#include "tori_rs_render.h"
}

#include "platform_impl2_win32_renderer_d3d8.h"

#include "nuklear/torirs_nuklear.h"
extern "C" {
#include "nuklear/backends/rawfb/nuklear_rawfb.h"
struct nk_context* torirs_rawfb_get_nk_context(struct rawfb_context* rawfb);
}

#if defined(TORIRS_D3D8_STATIC_LINK)
extern "C" IDirect3D8* WINAPI Direct3DCreate8(UINT SDKVersion);
#endif

#ifndef D3D_SDK_VERSION
#define D3D_SDK_VERSION 220
#endif

namespace
{

static void
d3d8_log(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("[d3d8] ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    fflush(stderr);
    va_end(ap);
}

typedef IDirect3D8*(WINAPI* PFN_Direct3DCreate8)(UINT);

struct D3D8WorldVertex
{
    float x, y, z;
    DWORD diffuse;
    float u, v;
};

struct D3D8UiVertex
{
    float x, y, z, rhw;
    DWORD diffuse;
    float u, v;
};

static const DWORD kFvfWorld = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;
static const DWORD kFvfUi = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

/** Set true to skip the full-screen Nuklear overlay quad for A/B testing (3D vs overlay). */
static const bool kSkipNuklearOverlayQuad = false;

struct D3D8ModelGpu
{
    IDirect3DVertexBuffer8* vbo;
    int face_count;
    std::vector<int> per_face_raw_tex_id;
    /** CPU copy of the first face's three vertices (for one-shot MODEL_DRAW diagnostics). */
    D3D8WorldVertex first_face_verts[3];
    /** CPU copy of the last face's three vertices (for one-shot MODEL_DRAW diagnostics). */
    D3D8WorldVertex last_face_verts[3];
};

struct D3D8BatchModelEntry
{
    uint64_t model_key;
    int model_id;
    int vertex_base;
    int face_count;
    std::vector<int> per_face_raw_tex_id;
};

struct D3D8ModelBatch
{
    uint32_t batch_id = 0;
    IDirect3DVertexBuffer8* vbo = nullptr;
    IDirect3DIndexBuffer8* ebo = nullptr;
    int total_vertex_count = 0;
    std::vector<D3D8WorldVertex> pending_verts;
    std::vector<uint16_t> pending_indices;
    std::unordered_map<uint64_t, D3D8BatchModelEntry> entries_by_key;
};

static void
d3d8_mul_row_vec_d3dmatrix(const float v[4], const D3DMATRIX* m, float out[4])
{
    out[0] = v[0] * m->_11 + v[1] * m->_21 + v[2] * m->_31 + v[3] * m->_41;
    out[1] = v[0] * m->_12 + v[1] * m->_22 + v[2] * m->_32 + v[3] * m->_42;
    out[2] = v[0] * m->_13 + v[1] * m->_23 + v[2] * m->_33 + v[3] * m->_43;
    out[3] = v[0] * m->_14 + v[1] * m->_24 + v[2] * m->_34 + v[3] * m->_44;
}

enum PassKind
{
    PASS_NONE = 0,
    PASS_3D = 1,
    PASS_2D = 2,
};

struct D3D8Internal
{
    HMODULE h_dll;
    IDirect3D8* d3d;
    IDirect3DDevice8* device;
    D3DPRESENT_PARAMETERS pp;

    IDirect3DTexture8* scene_rt_tex;
    IDirect3DSurface8* scene_rt_surf;
    IDirect3DSurface8* scene_ds;
    IDirect3DIndexBuffer8* ib_ring;
    size_t ib_ring_size_bytes;
    IDirect3DTexture8* nk_overlay_tex;

    UINT client_w;
    UINT client_h;

    std::unordered_map<int, IDirect3DTexture8*> texture_by_id;
    std::unordered_map<int, float> texture_anim_speed_by_id;
    std::unordered_map<int, bool> texture_opaque_by_id;

    std::unordered_map<uint64_t, D3D8ModelGpu*> model_gpu_by_key;

    std::unordered_map<uint64_t, IDirect3DTexture8*> sprite_by_slot;
    std::unordered_map<uint64_t, std::pair<int, int>> sprite_size_by_slot;

    std::unordered_map<int, IDirect3DTexture8*> font_atlas_by_id;

    std::vector<uint16_t> scratch_indices;

    PassKind current_pass;
    int current_font_id;

    unsigned int debug_model_draws;
    unsigned int debug_triangles;

    size_t ib_ring_write_offset;

    /** Merged world-rebuild batches (TORIRS_GFX_BATCH_* / MODEL_BATCHED_LOAD). */
    D3D8ModelBatch* current_batch = nullptr;
    std::unordered_map<uint32_t, D3D8ModelBatch*> batches_by_id;
    std::unordered_map<uint64_t, D3D8ModelBatch*> batched_model_batch_by_key;

    /** Completed frames (incremented at end of Render). `< 3` = verbose tracing window. */
    unsigned int frame_count;
    bool trace_first_gfx[32];

    /** Viewports for the current frame, applied by d3d8_ensure_pass on pass transitions. */
    D3DVIEWPORT8 frame_vp_3d;
    D3DVIEWPORT8 frame_vp_2d;

    /** View/projection for this frame (for MODEL_DRAW clip-space diagnostics). */
    D3DMATRIX frame_view;
    D3DMATRIX frame_proj;
};

/** First 3 completed frames: verbose. After that, log each GFX kind only once. */
static constexpr int kD3d8GfxKindSlots = 32;

static inline bool
d3d8_trace_on(const D3D8Internal* p)
{
    (void)p;
    return false;
}

static bool
d3d8_should_log_cmd(D3D8Internal* p, int kind)
{
    if( kind < 0 || kind >= kD3d8GfxKindSlots )
        return d3d8_trace_on(p);
    if( d3d8_trace_on(p) )
        return true;
    return !p->trace_first_gfx[(size_t)kind];
}

/** After logging a command: only mark "seen" when past the verbose window, so first 3 frames log every instance of each kind. */
static void
d3d8_mark_cmd_logged(D3D8Internal* p, int kind)
{
    if( d3d8_trace_on(p) )
        return;
    if( kind >= 0 && kind < kD3d8GfxKindSlots )
        p->trace_first_gfx[(size_t)kind] = true;
}

static D3D8Internal*
d3d8_priv(struct Platform2_Win32_Renderer_D3D8* r)
{
    return (D3D8Internal*)r->_internal;
}

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

static void
d3d8_remap_projection_z(float* proj_colmajor)
{
    static const float k_clip_z[16] = {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 1.0f,
    };
    float tmp[16];
    mat4_mul_colmajor(k_clip_z, proj_colmajor, tmp);
    memcpy(proj_colmajor, tmp, sizeof(tmp));
}

static void
d3d8_compute_view_matrix(
    float* out_matrix,
    float camera_x,
    float camera_y,
    float camera_z,
    float pitch,
    float yaw)
{
    (void)camera_x;
    (void)camera_y;
    (void)camera_z;
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

    out_matrix[12] = 0.0f;
    out_matrix[13] = 0.0f;
    out_matrix[14] = 0.0f;
    out_matrix[15] = 1.0f;
}

static void
d3d8_compute_projection_matrix(float* out_matrix, float fov, float screen_width, float screen_height)
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
colmajor_to_d3dmatrix(const float* cm, D3DMATRIX* m)
{
    /* `cm` stores a matrix designed for column-vector math (v' = M * v), the
     * same convention our Metal / D3D11 shaders use. D3D8 fixed-function uses
     * row-vector math (v' = v * M), so we must TRANSPOSE when loading:
     * column k of the source becomes row k+1 of the D3DMATRIX. */
    m->_11 = cm[0];
    m->_12 = cm[1];
    m->_13 = cm[2];
    m->_14 = cm[3];
    m->_21 = cm[4];
    m->_22 = cm[5];
    m->_23 = cm[6];
    m->_24 = cm[7];
    m->_31 = cm[8];
    m->_32 = cm[9];
    m->_33 = cm[10];
    m->_34 = cm[11];
    m->_41 = cm[12];
    m->_42 = cm[13];
    m->_43 = cm[14];
    m->_44 = cm[15];
}

static float
d3d8_texture_animation_signed(int animation_direction, int animation_speed)
{
    if( animation_direction == 0 )
        return 0.0f;
    float speed = ((float)animation_speed) / 128.0f;
    if( animation_direction == 2 || animation_direction == 4 )
        return speed;
    return -speed;
}

static int
model_id_from_model_cache_key(uint64_t k)
{
    return (int)(uint32_t)(k >> 24);
}

static void
d3d8_vertex_fill_invisible(D3D8WorldVertex* v)
{
    memset(v, 0, sizeof(*v));
    v->z = 0.0f;
    *((unsigned char*)&v->diffuse + 3) = 0;
}

static unsigned s_fill_face_fail_logs = 0;
static unsigned s_model_draw_debug = 0;
static unsigned s_dip_full_log = 0;

static void
d3d8_log_fill_face_fail(int f, const char* reason)
{
    if( s_fill_face_fail_logs >= 64u )
        return;
    s_fill_face_fail_logs++;
    d3d8_log("fill_model_face: f=%d %s", f, reason);
}

static bool
fill_model_face_vertices_model_local(
    const struct DashModel* model,
    int f,
    int raw_tex,
    D3D8WorldVertex out[3])
{
    const int* face_infos = dashmodel_face_infos_const(model);
    if( face_infos && face_infos[f] == 2 )
    {
        d3d8_log_fill_face_fail(f, "face_infos==2 (skip)");
        return false;
    }
    const hsl16_t* hsl_c_arr = dashmodel_face_colors_c_const(model);
    if( hsl_c_arr && hsl_c_arr[f] == DASHHSL16_HIDDEN )
    {
        d3d8_log_fill_face_fail(f, "HIDDEN hsl");
        return false;
    }

    const faceint_t* face_ia = dashmodel_face_indices_a_const(model);
    const faceint_t* face_ib = dashmodel_face_indices_b_const(model);
    const faceint_t* face_ic = dashmodel_face_indices_c_const(model);
    const int ia = face_ia[f];
    const int ib = face_ib[f];
    const int ic = face_ic[f];
    const int vcount = dashmodel_vertex_count(model);
    if( ia < 0 || ia >= vcount || ib < 0 || ib >= vcount || ic < 0 || ic >= vcount )
    {
        d3d8_log_fill_face_fail(
            f,
            "bad vertex index ia/ib/ic vs vcount");
        return false;
    }

    const hsl16_t* hsl_a_arr = dashmodel_face_colors_a_const(model);
    const hsl16_t* hsl_b_arr = dashmodel_face_colors_b_const(model);
    if( !hsl_a_arr || !hsl_b_arr || !hsl_c_arr )
    {
        d3d8_log_fill_face_fail(f, "missing hsl_a/b/c arrays");
        return false;
    }

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
        face_alpha = (float)(0xFF - face_alphas[f]) / 255.0f;

    float u_corner[3] = { 0.0f, 0.0f, 0.0f };
    float v_corner[3] = { 0.0f, 0.0f, 0.0f };

    if( raw_tex >= 0 )
    {
        int texture_face_idx = f;
        int tp = 0, tm = 0, tn = 0;
        const faceint_t* ftc = dashmodel_face_texture_coords_const(model);
        const faceint_t* tcp = dashmodel_textured_p_coordinate_const(model);
        const faceint_t* tcm = dashmodel_textured_m_coordinate_const(model);
        const faceint_t* tcn = dashmodel_textured_n_coordinate_const(model);
        if( dashmodel__is_ground_va(model) )
        {
            tp = (int)face_ia[0];
            tm = (int)face_ib[0];
            tn = (int)face_ic[0];
            if( tp < 0 || tp >= vcount || tm < 0 || tm >= vcount || tn < 0 || tn >= vcount )
            {
                d3d8_log_fill_face_fail(f, "ground_va bad tp/tm/tn");
                return false;
            }
        }
        else if( ftc && ftc[f] != -1 && tcp && tcm && tcn )
        {
            texture_face_idx = ftc[f];
            tp = tcp[texture_face_idx];
            tm = tcm[texture_face_idx];
            tn = tcn[texture_face_idx];
        }
        else
        {
            tp = face_ia[f];
            tm = face_ib[f];
            tn = face_ic[f];
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

    const int verts[3] = { ia, ib, ic };
    const int rgbs[3] = { rgb_a, rgb_b, rgb_c };
    const vertexint_t* vx = dashmodel_vertices_x_const(model);
    const vertexint_t* vy = dashmodel_vertices_y_const(model);
    const vertexint_t* vz = dashmodel_vertices_z_const(model);
    for( int vi = 0; vi < 3; ++vi )
    {
        const int vi_idx = verts[vi];
        int rgb = rgbs[vi];
        unsigned char rr = (unsigned char)((rgb >> 16) & 0xFF);
        unsigned char gg = (unsigned char)((rgb >> 8) & 0xFF);
        unsigned char bb = (unsigned char)(rgb & 0xFF);
        unsigned char aa = (unsigned char)(face_alpha * 255.0f);
        out[vi].x = (float)vx[vi_idx];
        out[vi].y = (float)vy[vi_idx];
        out[vi].z = (float)vz[vi_idx];
        out[vi].diffuse = ((DWORD)aa << 24) | ((DWORD)rr << 16) | ((DWORD)gg << 8) | (DWORD)bb;
        out[vi].u = u_corner[vi];
        out[vi].v = v_corner[vi];
        if( raw_tex >= 0 )
        {
            out[vi].u = out[vi].u * 0.984f + 0.008f;
            out[vi].v = out[vi].v * 0.984f + 0.008f;
        }
    }
    return true;
}

static unsigned s_build_model_gpu_ok_logs = 0;
static unsigned s_build_model_gpu_enter_logs = 0;

static D3D8ModelGpu*
d3d8_build_model_gpu(
    D3D8Internal* priv,
    IDirect3DDevice8* device,
    const struct DashModel* model,
    uint64_t model_key)
{
    if( !model || !priv || !device )
    {
        d3d8_log("build_model_gpu: abort null model/priv/device");
        return nullptr;
    }
    if( dashmodel_face_count(model) <= 0 )
    {
        d3d8_log("build_model_gpu: abort face_count<=0");
        return nullptr;
    }
    if( !dashmodel_face_colors_a_const(model) || !dashmodel_face_colors_b_const(model) ||
        !dashmodel_face_colors_c_const(model) )
    {
        d3d8_log("build_model_gpu: abort missing face color arrays");
        return nullptr;
    }
    if( !dashmodel_vertices_x_const(model) || !dashmodel_vertices_y_const(model) ||
        !dashmodel_vertices_z_const(model) )
    {
        d3d8_log("build_model_gpu: abort missing vertex xyz arrays");
        return nullptr;
    }
    if( !dashmodel_face_indices_a_const(model) || !dashmodel_face_indices_b_const(model) ||
        !dashmodel_face_indices_c_const(model) )
    {
        d3d8_log("build_model_gpu: abort missing face index arrays");
        return nullptr;
    }

    const int fc = dashmodel_face_count(model);
    const faceint_t* ftex = dashmodel_face_textures_const(model);
    if( s_build_model_gpu_enter_logs < 32u )
    {
        s_build_model_gpu_enter_logs++;
        d3d8_log(
            "build_model_gpu: enter model=%p device=%p key=0x%016llX fc=%d ftex=%p (log %u/32)",
            (void*)model,
            (void*)device,
            (unsigned long long)model_key,
            fc,
            (void*)ftex,
            s_build_model_gpu_enter_logs);
    }
    std::vector<D3D8WorldVertex> verts((size_t)fc * 3u);
    auto* gpu = new D3D8ModelGpu();
    gpu->face_count = fc;
    gpu->per_face_raw_tex_id.resize((size_t)fc);
    for( int f = 0; f < fc; ++f )
    {
        int raw = ftex ? (int)ftex[f] : -1;
        gpu->per_face_raw_tex_id[(size_t)f] = raw;
        D3D8WorldVertex tri[3];
        if( !fill_model_face_vertices_model_local(model, f, raw, tri) )
        {
            d3d8_vertex_fill_invisible(&tri[0]);
            d3d8_vertex_fill_invisible(&tri[1]);
            d3d8_vertex_fill_invisible(&tri[2]);
        }
        verts[(size_t)f * 3u + 0u] = tri[0];
        verts[(size_t)f * 3u + 1u] = tri[1];
        verts[(size_t)f * 3u + 2u] = tri[2];
        if( f == 0 )
        {
            gpu->first_face_verts[0] = tri[0];
            gpu->first_face_verts[1] = tri[1];
            gpu->first_face_verts[2] = tri[2];
        }
        if( f == fc - 1 )
        {
            gpu->last_face_verts[0] = tri[0];
            gpu->last_face_verts[1] = tri[1];
            gpu->last_face_verts[2] = tri[2];
        }
    }

    const UINT bytes = (UINT)(verts.size() * sizeof(D3D8WorldVertex));
    IDirect3DVertexBuffer8* vbo = nullptr;
    HRESULT hrvb =
        device->CreateVertexBuffer(bytes, D3DUSAGE_WRITEONLY, kFvfWorld, D3DPOOL_MANAGED, &vbo);
    if( hrvb != D3D_OK )
    {
        d3d8_log(
            "build_model_gpu: CreateVertexBuffer failed hr=0x%08lX bytes=%u fc=%d",
            (unsigned long)hrvb,
            (unsigned)bytes,
            fc);
        delete gpu;
        return nullptr;
    }
    void* dst = nullptr;
    HRESULT hrl = vbo->Lock(0, bytes, (BYTE**)&dst, 0);
    if( hrl != D3D_OK )
    {
        d3d8_log(
            "build_model_gpu: VBO Lock failed hr=0x%08lX bytes=%u",
            (unsigned long)hrl,
            (unsigned)bytes);
        vbo->Release();
        delete gpu;
        return nullptr;
    }
    memcpy(dst, verts.data(), bytes);
    vbo->Unlock();
    gpu->vbo = vbo;
    priv->model_gpu_by_key[model_key] = gpu;
    if( s_build_model_gpu_ok_logs < 8u )
    {
        s_build_model_gpu_ok_logs++;
        d3d8_log(
            "build_model_gpu: ok key=0x%016llX fc=%d bytes=%u (log %u/8)",
            (unsigned long long)model_key,
            fc,
            (unsigned)bytes,
            s_build_model_gpu_ok_logs);
    }
    return gpu;
}

static void
d3d8_release_model_gpu(D3D8ModelGpu* gpu)
{
    if( !gpu )
        return;
    if( gpu->vbo )
        gpu->vbo->Release();
    delete gpu;
}

static void
d3d8_release_model_batch(D3D8ModelBatch* batch)
{
    if( !batch )
        return;
    if( batch->vbo )
        batch->vbo->Release();
    if( batch->ebo )
        batch->ebo->Release();
    delete batch;
}

static D3D8ModelGpu*
d3d8_lookup_or_build_model_gpu(
    D3D8Internal* priv,
    IDirect3DDevice8* device,
    const struct DashModel* model,
    uint64_t model_key)
{
    auto it = priv->model_gpu_by_key.find(model_key);
    if( it != priv->model_gpu_by_key.end() && it->second )
        return it->second;
    return d3d8_build_model_gpu(priv, device, model, model_key);
}

static void
d3d8_apply_texture_matrix_translate(IDirect3DDevice8* dev, float du, float dv)
{
    D3DMATRIX m;
    memset(&m, 0, sizeof(m));
    m._11 = 1.0f;
    m._22 = 1.0f;
    m._33 = 1.0f;
    m._44 = 1.0f;
    m._41 = du;
    m._42 = dv;
    dev->SetTransform(D3DTS_TEXTURE0, &m);
    dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
}

static void
d3d8_disable_texture_transform(IDirect3DDevice8* dev)
{
    D3DMATRIX id;
    memset(&id, 0, sizeof(id));
    id._11 = id._22 = id._33 = id._44 = 1.0f;
    dev->SetTransform(D3DTS_TEXTURE0, &id);
    dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
}

static void
d3d8_apply_default_render_state(IDirect3DDevice8* dev)
{
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHAREF, 128);
    dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);

    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
}

/** dst: client pixel rect (left, top, right, bottom) for the scaled game image. */
static void
compute_letterbox_dst(int buf_w, int buf_h, int window_w, int window_h, RECT* out_dst)
{
    out_dst->left = 0;
    out_dst->top = 0;
    out_dst->right = buf_w;
    out_dst->bottom = buf_h;
    if( window_w <= 0 || window_h <= 0 )
        return;
    float src_aspect = (float)buf_w / (float)buf_h;
    float window_aspect = (float)window_w / (float)window_h;
    int dst_x = 0;
    int dst_y = 0;
    int dst_w = buf_w;
    int dst_h = buf_h;
    if( src_aspect > window_aspect )
    {
        dst_w = window_w;
        dst_h = (int)(window_w / src_aspect);
        dst_x = 0;
        dst_y = (window_h - dst_h) / 2;
    }
    else
    {
        dst_h = window_h;
        dst_w = (int)(window_h * src_aspect);
        dst_y = 0;
        dst_x = (window_w - dst_w) / 2;
    }
    out_dst->left = dst_x;
    out_dst->top = dst_y;
    out_dst->right = dst_x + dst_w;
    out_dst->bottom = dst_y + dst_h;
}

} /* namespace */

struct Platform2_Win32_Renderer_D3D8*
PlatformImpl2_Win32_Renderer_D3D8_New(
    int width,
    int height,
    int max_width,
    int max_height)
{
    d3d8_log(
        "D3D8_New: enter width=%d height=%d max_width=%d max_height=%d",
        width,
        height,
        max_width,
        max_height);
    struct Platform2_Win32_Renderer_D3D8* r =
        (struct Platform2_Win32_Renderer_D3D8*)calloc(1, sizeof(struct Platform2_Win32_Renderer_D3D8));
    if( !r )
    {
        d3d8_log("D3D8_New: calloc(renderer) failed");
        return nullptr;
    }
    r->width = width;
    r->height = height;
    r->initial_width = width;
    r->initial_height = height;
    r->max_width = max_width;
    r->max_height = max_height;
    r->dash_offset_x = 0;
    r->dash_offset_y = 0;
    if( !QueryPerformanceFrequency(&r->nk_qpc_freq) )
        r->nk_qpc_freq.QuadPart = 0;

    size_t const pc = (size_t)width * (size_t)height;
    r->nk_pixel_buffer = (int*)malloc(pc * sizeof(int));
    if( !r->nk_pixel_buffer )
    {
        d3d8_log("D3D8_New: malloc(nk_pixel_buffer) failed (bytes=%zu)", pc * sizeof(int));
        free(r);
        return nullptr;
    }
    memset(r->nk_pixel_buffer, 0, pc * sizeof(int));

    auto* priv = new D3D8Internal();
    priv->current_pass = PASS_NONE;
    priv->current_font_id = -1;
    r->_internal = priv;
    d3d8_log("D3D8_New: ok renderer=%p priv=%p", (void*)r, (void*)priv);
    return r;
}

static void
d3d8_destroy_internal(D3D8Internal* p)
{
    if( !p )
        return;
    for( auto& kv : p->texture_by_id )
        if( kv.second )
            kv.second->Release();
    p->texture_by_id.clear();
    p->texture_anim_speed_by_id.clear();
    p->texture_opaque_by_id.clear();

    for( auto& kv : p->model_gpu_by_key )
        d3d8_release_model_gpu(kv.second);
    p->model_gpu_by_key.clear();

    if( p->current_batch )
    {
        d3d8_release_model_batch(p->current_batch);
        p->current_batch = nullptr;
    }
    for( auto& kv : p->batches_by_id )
        d3d8_release_model_batch(kv.second);
    p->batches_by_id.clear();
    p->batched_model_batch_by_key.clear();

    for( auto& kv : p->sprite_by_slot )
        if( kv.second )
            kv.second->Release();
    p->sprite_by_slot.clear();
    p->sprite_size_by_slot.clear();

    for( auto& kv : p->font_atlas_by_id )
        if( kv.second )
            kv.second->Release();
    p->font_atlas_by_id.clear();

    if( p->nk_overlay_tex )
        p->nk_overlay_tex->Release();
    if( p->ib_ring )
        p->ib_ring->Release();
    if( p->scene_rt_surf )
        p->scene_rt_surf->Release();
    if( p->scene_rt_tex )
        p->scene_rt_tex->Release();
    if( p->scene_ds )
        p->scene_ds->Release();
    if( p->device )
        p->device->Release();
    if( p->d3d )
        p->d3d->Release();
#if !defined(TORIRS_D3D8_STATIC_LINK)
    if( p->h_dll )
        FreeLibrary(p->h_dll);
#endif
    delete p;
}

void
PlatformImpl2_Win32_Renderer_D3D8_Free(struct Platform2_Win32_Renderer_D3D8* renderer)
{
    if( !renderer )
        return;
    if( renderer->nk_rawfb )
    {
        nk_rawfb_shutdown((struct rawfb_context*)renderer->nk_rawfb);
        renderer->nk_rawfb = nullptr;
    }
    if( renderer->platform )
        renderer->platform->nk_ctx_for_input = nullptr;
    free(renderer->nk_font_tex_mem);
    renderer->nk_font_tex_mem = nullptr;
    d3d8_destroy_internal(d3d8_priv(renderer));
    renderer->_internal = nullptr;
    free(renderer->nk_pixel_buffer);
    free(renderer);
}

static bool
d3d8_reset_device(struct Platform2_Win32_Renderer_D3D8* ren, D3D8Internal* p, HWND hwnd)
{
    if( !p->device || !hwnd )
    {
        d3d8_log(
            "d3d8_reset_device: abort device=%p hwnd=%p",
            (void*)p->device,
            (void*)hwnd);
        return false;
    }

    d3d8_log(
        "d3d8_reset_device: enter scene_rt=%dx%d client pending...",
        ren->width,
        ren->height);

    if( p->nk_overlay_tex )
    {
        p->nk_overlay_tex->Release();
        p->nk_overlay_tex = nullptr;
    }
    if( p->ib_ring )
    {
        p->ib_ring->Release();
        p->ib_ring = nullptr;
    }
    p->ib_ring_size_bytes = 0;
    if( p->scene_rt_surf )
    {
        p->scene_rt_surf->Release();
        p->scene_rt_surf = nullptr;
    }
    if( p->scene_rt_tex )
    {
        p->scene_rt_tex->Release();
        p->scene_rt_tex = nullptr;
    }
    if( p->scene_ds )
    {
        p->scene_ds->Release();
        p->scene_ds = nullptr;
    }

    RECT cr;
    GetClientRect(hwnd, &cr);
    int cw = cr.right - cr.left;
    int ch = cr.bottom - cr.top;
    if( cw <= 0 )
        cw = 1;
    if( ch <= 0 )
        ch = 1;
    p->client_w = (UINT)cw;
    p->client_h = (UINT)ch;

    d3d8_log(
        "d3d8_reset_device: client=%ux%u scene_rt=%dx%d",
        (unsigned)p->client_w,
        (unsigned)p->client_h,
        ren->width,
        ren->height);

    p->pp.BackBufferWidth = p->client_w;
    p->pp.BackBufferHeight = p->client_h;
    HRESULT hr = p->device->Reset(&p->pp);
    if( FAILED(hr) )
    {
        d3d8_log("d3d8_reset_device: device->Reset failed hr=0x%08lX", (unsigned long)hr);
        return false;
    }
    d3d8_log("d3d8_reset_device: Reset ok hr=0x%08lX", (unsigned long)hr);

    UINT w = (UINT)ren->width;
    UINT h = (UINT)ren->height;
    hr = p->device->CreateTexture(
        w,
        h,
        1,
        D3DUSAGE_RENDERTARGET,
        D3DFMT_X8R8G8B8,
        D3DPOOL_DEFAULT,
        &p->scene_rt_tex);
    if( FAILED(hr) || !p->scene_rt_tex )
    {
        d3d8_log(
            "d3d8_reset_device: CreateTexture(scene_rt) failed hr=0x%08lX tex=%p",
            (unsigned long)hr,
            (void*)p->scene_rt_tex);
        return false;
    }
    d3d8_log("d3d8_reset_device: CreateTexture(scene_rt) ok %ux%u", (unsigned)w, (unsigned)h);
    hr = p->scene_rt_tex->GetSurfaceLevel(0, &p->scene_rt_surf);
    if( FAILED(hr) )
    {
        d3d8_log(
            "d3d8_reset_device: GetSurfaceLevel(scene_rt) failed hr=0x%08lX",
            (unsigned long)hr);
        return false;
    }
    hr = p->device->CreateDepthStencilSurface(
        w, h, D3DFMT_D16, D3DMULTISAMPLE_NONE, &p->scene_ds);
    if( FAILED(hr) )
    {
        d3d8_log(
            "d3d8_reset_device: CreateDepthStencilSurface failed hr=0x%08lX",
            (unsigned long)hr);
        return false;
    }
    d3d8_log("d3d8_reset_device: depth-stencil ok");

    hr = p->device->CreateTexture(
        w,
        h,
        1,
        D3DUSAGE_DYNAMIC,
        D3DFMT_A8R8G8B8,
        D3DPOOL_DEFAULT,
        &p->nk_overlay_tex);
    if( FAILED(hr) )
    {
        d3d8_log(
            "d3d8_reset_device: CreateTexture(nk_overlay) failed hr=0x%08lX",
            (unsigned long)hr);
        return false;
    }
    d3d8_log("d3d8_reset_device: nk_overlay texture ok");

    const size_t ib_need = 65536 * sizeof(uint16_t);
    p->ib_ring_size_bytes = ib_need;
    hr = p->device->CreateIndexBuffer(
        (UINT)ib_need,
        D3DUSAGE_DYNAMIC,
        D3DFMT_INDEX16,
        D3DPOOL_DEFAULT,
        &p->ib_ring);
    if( !SUCCEEDED(hr) || !p->ib_ring )
    {
        d3d8_log(
            "d3d8_reset_device: CreateIndexBuffer failed hr=0x%08lX ib=%p",
            (unsigned long)hr,
            (void*)p->ib_ring);
        return false;
    }
    d3d8_log("d3d8_reset_device: index ring ok bytes=%zu", ib_need);
    d3d8_log("d3d8_reset_device: complete ok");
    return true;
}

static bool
d3d8_create_device(struct Platform2_Win32_Renderer_D3D8* ren, D3D8Internal* p, HWND hwnd)
{
    d3d8_log(
        "d3d8_create_device: enter hwnd=%p ren=%p D3D_SDK_VERSION=%u",
        (void*)hwnd,
        (void*)ren,
        (unsigned)D3D_SDK_VERSION);
#if defined(TORIRS_D3D8_STATIC_LINK)
    d3d8_log("d3d8_create_device: using static link (Direct3DCreate8 import)");
    p->d3d = Direct3DCreate8(D3D_SDK_VERSION);
#else
    d3d8_log("d3d8_create_device: dynamic load LoadLibraryA(\"d3d8.dll\")");
    p->h_dll = LoadLibraryA("d3d8.dll");
    if( !p->h_dll )
    {
        d3d8_log(
            "d3d8_create_device: LoadLibraryA(d3d8.dll) failed GetLastError=%lu",
            (unsigned long)GetLastError());
        return false;
    }
    d3d8_log(
        "d3d8_create_device: d3d8.dll loaded ok h_dll=%p",
        (void*)p->h_dll);
    PFN_Direct3DCreate8 fn = reinterpret_cast<PFN_Direct3DCreate8>(
        GetProcAddress(p->h_dll, "Direct3DCreate8"));
    if( !fn )
    {
        d3d8_log(
            "d3d8_create_device: GetProcAddress(Direct3DCreate8) failed GetLastError=%lu",
            (unsigned long)GetLastError());
        return false;
    }
    d3d8_log(
        "d3d8_create_device: GetProcAddress(Direct3DCreate8) ok fn=%p",
        (void*)fn);
    p->d3d = fn(D3D_SDK_VERSION);
#endif
    if( !p->d3d )
    {
        d3d8_log("d3d8_create_device: Direct3DCreate8 returned NULL");
        return false;
    }
    d3d8_log("d3d8_create_device: IDirect3D8*=%p", (void*)p->d3d);

    ZeroMemory(&p->pp, sizeof(p->pp));
    p->pp.Windowed = TRUE;
    p->pp.SwapEffect = D3DSWAPEFFECT_COPY;
    p->pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    p->pp.hDeviceWindow = hwnd;
    p->pp.EnableAutoDepthStencil = FALSE;

    RECT cr;
    GetClientRect(hwnd, &cr);
    int cw = cr.right - cr.left;
    int ch = cr.bottom - cr.top;
    if( cw <= 0 )
        cw = 800;
    if( ch <= 0 )
        ch = 600;
    p->client_w = (UINT)cw;
    p->client_h = (UINT)ch;
    p->pp.BackBufferWidth = p->client_w;
    p->pp.BackBufferHeight = p->client_h;

    d3d8_log(
        "d3d8_create_device: present_params client=%dx%d BackBufferFormat=0x%X Windowed=%u",
        cw,
        ch,
        (unsigned)p->pp.BackBufferFormat,
        (unsigned)p->pp.Windowed);

    D3DCAPS8 caps;
    ZeroMemory(&caps, sizeof(caps));
    DWORD create_flags = D3DCREATE_HARDWARE_VERTEXPROCESSING;
    HRESULT capshr = p->d3d->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &caps);
    if( capshr == D3D_OK )
    {
        d3d8_log(
            "d3d8_create_device: MaxVertexIndex=%lu",
            (unsigned long)caps.MaxVertexIndex);
        if( caps.MaxVertexIndex < 0xFFFFu )
        {
            d3d8_log(
                "d3d8_create_device: WARNING MaxVertexIndex < 0xFFFF (unexpected); "
                "32-bit indices still valid with SW VP");
        }
        if( !(caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) )
        {
            create_flags = D3DCREATE_SOFTWARE_VERTEXPROCESSING;
            d3d8_log(
                "d3d8_create_device: GetDeviceCaps ok -> SOFTWARE_VERTEXPROCESSING "
                "(no HW T&L)");
        }
        else
            d3d8_log(
                "d3d8_create_device: GetDeviceCaps ok -> HARDWARE_VERTEXPROCESSING");
    }
    else
    {
        d3d8_log(
            "d3d8_create_device: GetDeviceCaps failed hr=0x%08lX (using HW VP flags)",
            (unsigned long)capshr);
    }

    HRESULT hr = p->d3d->CreateDevice(
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        hwnd,
        create_flags,
        &p->pp,
        &p->device);
    if( FAILED(hr) || !p->device )
    {
        d3d8_log(
            "d3d8_create_device: CreateDevice failed hr=0x%08lX device=%p",
            (unsigned long)hr,
            (void*)p->device);
        return false;
    }
    d3d8_log(
        "d3d8_create_device: CreateDevice ok device=%p flags=0x%08lX",
        (void*)p->device,
        (unsigned long)create_flags);

    d3d8_apply_default_render_state(p->device);
    d3d8_log("d3d8_create_device: default render state applied, calling d3d8_reset_device");
    if( !d3d8_reset_device(ren, p, hwnd) )
    {
        d3d8_log("d3d8_create_device: d3d8_reset_device failed");
        return false;
    }
    d3d8_log("d3d8_create_device: complete ok");
    return true;
}

bool
PlatformImpl2_Win32_Renderer_D3D8_Init(
    struct Platform2_Win32_Renderer_D3D8* renderer,
    struct Platform2_Win32* platform)
{
    d3d8_log(
        "D3D8_Init: enter renderer=%p platform=%p",
        (void*)renderer,
        (void*)platform);
    if( !renderer )
    {
        d3d8_log("D3D8_Init: abort renderer is NULL");
        return false;
    }
    if( !platform )
    {
        d3d8_log("D3D8_Init: abort platform is NULL");
        return false;
    }
    if( !platform->hwnd )
    {
        d3d8_log("D3D8_Init: abort platform->hwnd is NULL");
        return false;
    }
    renderer->platform = platform;

    static const int FONT_TEX_BYTES = 512 * 512;
    renderer->nk_font_tex_mem = (uint8_t*)malloc((size_t)FONT_TEX_BYTES);
    if( !renderer->nk_font_tex_mem )
        d3d8_log("D3D8_Init: malloc(nk_font_tex_mem) failed; nk UI may be unavailable");
    if( renderer->nk_font_tex_mem && renderer->nk_pixel_buffer )
    {
        struct rawfb_pl pl;
        pl.bytesPerPixel = 4;
        pl.rshift = 16;
        pl.gshift = 8;
        pl.bshift = 0;
        pl.ashift = 24;
        pl.rloss = 0;
        pl.gloss = 0;
        pl.bloss = 0;
        pl.aloss = 0;
        d3d8_log("D3D8_Init: nk_rawfb_init ...");
        renderer->nk_rawfb = nk_rawfb_init(
            renderer->nk_pixel_buffer,
            renderer->nk_font_tex_mem,
            (unsigned)renderer->width,
            (unsigned)renderer->height,
            (unsigned)(renderer->width * (int)sizeof(int)),
            pl);
        if( renderer->nk_rawfb )
        {
            d3d8_log("D3D8_Init: nk_rawfb_init ok rawfb=%p", (void*)renderer->nk_rawfb);
            struct nk_context* nk =
                torirs_rawfb_get_nk_context((struct rawfb_context*)renderer->nk_rawfb);
            platform->nk_ctx_for_input = (void*)nk;
            if( nk )
                nk_style_hide_cursor(nk);
        }
        else
        {
            d3d8_log("D3D8_Init: nk_rawfb_init failed");
            free(renderer->nk_font_tex_mem);
            renderer->nk_font_tex_mem = nullptr;
        }
    }
    else
        d3d8_log(
            "D3D8_Init: skipping nk_rawfb (font_tex=%p nk_pixels=%p)",
            (void*)renderer->nk_font_tex_mem,
            (void*)renderer->nk_pixel_buffer);

    D3D8Internal* p = d3d8_priv(renderer);
    if( !d3d8_create_device(renderer, p, (HWND)platform->hwnd) )
    {
        d3d8_log("D3D8_Init: d3d8_create_device failed");
        return false;
    }

    d3d8_log("D3D8_Init: complete ok");
    return true;
}

void
PlatformImpl2_Win32_Renderer_D3D8_SetDashOffset(
    struct Platform2_Win32_Renderer_D3D8* renderer,
    int offset_x,
    int offset_y)
{
    if( renderer )
    {
        renderer->dash_offset_x = offset_x;
        renderer->dash_offset_y = offset_y;
    }
}

void
PlatformImpl2_Win32_Renderer_D3D8_SetDynamicPixelSize(
    struct Platform2_Win32_Renderer_D3D8* renderer,
    bool dynamic)
{
    if( renderer )
        renderer->pixel_size_dynamic = dynamic;
}

void
PlatformImpl2_Win32_Renderer_D3D8_MarkResizeDirty(struct Platform2_Win32_Renderer_D3D8* renderer)
{
    if( renderer )
        renderer->resize_dirty = true;
}

void
PlatformImpl2_Win32_Renderer_D3D8_SetViewportChangedCallback(
    struct Platform2_Win32_Renderer_D3D8* renderer,
    void (*callback)(struct GGame* game, int new_width, int new_height, void* userdata),
    void* userdata)
{
    if( !renderer )
        return;
    renderer->on_viewport_changed = callback;
    renderer->on_viewport_changed_userdata = userdata;
}

void
PlatformImpl2_Win32_Renderer_D3D8_PresentOrInvalidate(
    struct Platform2_Win32_Renderer_D3D8* renderer,
    HDC hdc,
    int client_w,
    int client_h)
{
    (void)hdc;
    if( !renderer || !renderer->platform )
        return;
    D3D8Internal* p = d3d8_priv(renderer);
    if( !p || !p->device )
        return;
    if( client_w > 0 && client_h > 0 )
    {
        if( (UINT)client_w != p->client_w || (UINT)client_h != p->client_h )
            renderer->resize_dirty = true;
    }
}

static void
d3d8_ensure_pass(D3D8Internal* priv, IDirect3DDevice8* dev, PassKind want)
{
    if( priv->current_pass == want )
        return;
    if( want == PASS_3D )
    {
        dev->SetViewport(&priv->frame_vp_3d);
        dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
        dev->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
        dev->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
        dev->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
        dev->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
        dev->SetVertexShader(kFvfWorld);
    }
    else
    {
        dev->SetViewport(&priv->frame_vp_2d);
        dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
        dev->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
        dev->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
        dev->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
        dev->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
        dev->SetVertexShader(kFvfUi);
    }
    priv->current_pass = want;
}

void
PlatformImpl2_Win32_Renderer_D3D8_Render(
    struct Platform2_Win32_Renderer_D3D8* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    if( !renderer || !game || !render_command_buffer || !renderer->platform ||
        !renderer->platform->hwnd )
        return;

    D3D8Internal* p = d3d8_priv(renderer);
    if( !p || !p->device )
        return;

    HWND hwnd = (HWND)renderer->platform->hwnd;

    game->viewport_offset_x = renderer->dash_offset_x;
    game->viewport_offset_y = renderer->dash_offset_y;

    int window_width = 0;
    int window_height = 0;
    RECT cr;
    if( GetClientRect(hwnd, &cr) )
    {
        window_width = cr.right - cr.left;
        window_height = cr.bottom - cr.top;
    }
    if( window_width <= 0 || window_height <= 0 )
    {
        window_width = renderer->width;
        window_height = renderer->height;
    }

    int new_width = window_width > renderer->max_width ? renderer->max_width : window_width;
    int new_height = window_height > renderer->max_height ? renderer->max_height : window_height;

    if( renderer->initial_view_port_width == 0 && game->view_port && game->view_port->width > 0 )
    {
        renderer->initial_view_port_width = game->view_port->width;
        renderer->initial_view_port_height = game->view_port->height;
    }

    if( game->iface_view_port )
    {
        game->iface_view_port->width = renderer->width;
        game->iface_view_port->height = renderer->height;
        game->iface_view_port->x_center = renderer->width / 2;
        game->iface_view_port->y_center = renderer->height / 2;
        game->iface_view_port->stride = renderer->width;
        game->iface_view_port->clip_left = 0;
        game->iface_view_port->clip_top = 0;
        game->iface_view_port->clip_right = renderer->width;
        game->iface_view_port->clip_bottom = renderer->height;
    }

    if( game->view_port && renderer->initial_view_port_width > 0 && renderer->initial_width > 0 )
    {
        if( renderer->pixel_size_dynamic )
        {
            float scale_x = (float)new_width / (float)renderer->initial_width;
            float scale_y = (float)new_height / (float)renderer->initial_height;
            float scale = scale_x < scale_y ? scale_x : scale_y;
            int new_vp_w = (int)(renderer->initial_view_port_width * scale);
            int new_vp_h = (int)(renderer->initial_view_port_height * scale);
            if( new_vp_w < 1 )
                new_vp_w = 1;
            if( new_vp_h < 1 )
                new_vp_h = 1;
            if( new_vp_w > renderer->max_width )
                new_vp_w = renderer->max_width;
            if( new_vp_h > renderer->max_height )
                new_vp_h = renderer->max_height;

            if( new_vp_w != game->view_port->width || new_vp_h != game->view_port->height )
            {
                LibToriRS_GameSetWorldViewportSize(game, new_vp_w, new_vp_h);
                if( renderer->on_viewport_changed )
                {
                    renderer->on_viewport_changed(
                        game, new_vp_w, new_vp_h, renderer->on_viewport_changed_userdata);
                }
            }
        }
        else
        {
            if( game->view_port->width != renderer->initial_view_port_width ||
                game->view_port->height != renderer->initial_view_port_height )
            {
                LibToriRS_GameSetWorldViewportSize(
                    game, renderer->initial_view_port_width, renderer->initial_view_port_height);
            }
        }
    }

    if( game->view_port )
    {
        game->view_port->x_center = game->view_port->width / 2;
        game->view_port->y_center = game->view_port->height / 2;
        game->view_port->stride = renderer->width;
    }

    if( renderer->resize_dirty )
    {
        bool resize_ok = d3d8_reset_device(renderer, p, hwnd);
        if( d3d8_trace_on(p) || !resize_ok )
            d3d8_log(
                "Render: resize_dirty d3d8_reset_device -> %s",
                resize_ok ? "ok" : "FAILED");
        if( resize_ok )
            ui_dirty_invalidate_all(game);
        renderer->resize_dirty = false;
    }

    HRESULT coop = p->device->TestCooperativeLevel();
    if( d3d8_trace_on(p) || coop != D3D_OK )
        d3d8_log(
            "Render: TestCooperativeLevel hr=0x%08lX",
            (unsigned long)coop);
    if( coop == D3DERR_DEVICELOST )
        return;
    if( coop == D3DERR_DEVICENOTRESET )
    {
        bool r2 = d3d8_reset_device(renderer, p, hwnd);
        if( d3d8_trace_on(p) || !r2 )
            d3d8_log(
                "Render: DEVICENOTRESET d3d8_reset_device -> %s",
                r2 ? "ok" : "FAILED");
    }

    IDirect3DDevice8* dev = p->device;

    const int vp_w = game->view_port ? game->view_port->width : renderer->width;
    const int vp_h = game->view_port ? game->view_port->height : renderer->height;

    if( d3d8_trace_on(p) )
        d3d8_log(
            "Render frame=%u window=%dx%d vp=%dx%d",
            p->frame_count,
            window_width,
            window_height,
            vp_w,
            vp_h);
    const float projection_width = (float)vp_w;
    const float projection_height = (float)vp_h;
    const float pitch_rad = ((float)game->camera_pitch * 2.0f * (float)M_PI) / 2048.0f;
    const float yaw_rad = ((float)game->camera_yaw * 2.0f * (float)M_PI) / 2048.0f;

    float view_m[16];
    float proj_m[16];
    d3d8_compute_view_matrix(view_m, 0.0f, 0.0f, 0.0f, pitch_rad, yaw_rad);
    d3d8_compute_projection_matrix(
        proj_m, (90.0f * (float)M_PI) / 180.0f, projection_width, projection_height);
    d3d8_remap_projection_z(proj_m);

    D3DMATRIX d3d_view;
    D3DMATRIX d3d_proj;
    colmajor_to_d3dmatrix(view_m, &d3d_view);
    colmajor_to_d3dmatrix(proj_m, &d3d_proj);
    p->frame_view = d3d_view;
    p->frame_proj = d3d_proj;

    const float u_clock = (float)((DWORD)GetTickCount() / 20u);

    p->ib_ring_write_offset = 0;
    p->current_pass = PASS_NONE;
    p->debug_model_draws = 0;
    p->debug_triangles = 0;
    p->current_font_id = -1;

    std::vector<D3D8UiVertex> font_verts;
    auto flush_font = [&]() {
        if( font_verts.empty() || p->current_font_id < 0 )
        {
            font_verts.clear();
            return;
        }
        auto it = p->font_atlas_by_id.find(p->current_font_id);
        if( it == p->font_atlas_by_id.end() || !it->second )
        {
            font_verts.clear();
            return;
        }
        d3d8_ensure_pass(p, dev, PASS_2D);
        dev->SetTexture(0, it->second);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
        dev->DrawPrimitiveUP(
            D3DPT_TRIANGLELIST,
            (UINT)(font_verts.size() / 3),
            font_verts.data(),
            sizeof(D3D8UiVertex));
        font_verts.clear();
    };

    HRESULT hr_rt = dev->SetRenderTarget(p->scene_rt_surf, p->scene_ds);
    if( FAILED(hr_rt) )
        d3d8_log(
            "Render: SetRenderTarget(scene_rt) failed hr=0x%08lX",
            (unsigned long)hr_rt);
    p->frame_vp_2d = { 0, 0, (DWORD)renderer->width, (DWORD)renderer->height, 0.0f, 1.0f };
    DWORD vp3d_x = (DWORD)(renderer->dash_offset_x > 0 ? renderer->dash_offset_x : 0);
    DWORD vp3d_y = (DWORD)(renderer->dash_offset_y > 0 ? renderer->dash_offset_y : 0);
    DWORD vp3d_w = (DWORD)vp_w;
    DWORD vp3d_h = (DWORD)vp_h;
    if( (int)(vp3d_x + vp3d_w) > renderer->width )
        vp3d_w = (DWORD)(renderer->width - (int)vp3d_x);
    if( (int)(vp3d_y + vp3d_h) > renderer->height )
        vp3d_h = (DWORD)(renderer->height - (int)vp3d_y);
    if( (int)vp3d_w < 1 )
        vp3d_w = 1u;
    if( (int)vp3d_h < 1 )
        vp3d_h = 1u;
    p->frame_vp_3d = { vp3d_x, vp3d_y, vp3d_w, vp3d_h, 0.0f, 1.0f };

    /* Default to 2D viewport for state setup / pre-draw clears. */
    HRESULT hr_vp = dev->SetViewport(&p->frame_vp_2d);
    if( FAILED(hr_vp) )
        d3d8_log(
            "Render: SetViewport(scene 2d) failed hr=0x%08lX",
            (unsigned long)hr_vp);

    /* D3D8 FFP: reset inherited state each frame; overlay/2D paths mutate render state. */
    d3d8_apply_default_render_state(dev);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);

    /* Z-clear only within the 3D sub-rect (Clear with NULL rects clears the viewport area).
     * Color target is NOT cleared per-frame: CLEAR_RECT commands handle UI region clears. */
    dev->SetViewport(&p->frame_vp_3d);
    dev->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
    dev->SetViewport(&p->frame_vp_2d);

    HRESULT hr_bs = dev->BeginScene();
    if( FAILED(hr_bs) )
        d3d8_log(
            "Render: BeginScene failed hr=0x%08lX",
            (unsigned long)hr_bs);

    dev->SetTransform(D3DTS_VIEW, &d3d_view);
    dev->SetTransform(D3DTS_PROJECTION, &d3d_proj);
    d3d8_disable_texture_transform(dev);

    struct ToriRSRenderCommand command = { 0 };

    LibToriRS_FrameBegin(game, render_command_buffer);
    while( LibToriRS_FrameNextCommand(game, render_command_buffer, &command, true) )
    {
        switch( command.kind )
        {
        case TORIRS_GFX_TEXTURE_LOAD:
        {
            int tex_id = command._texture_load.texture_id;
            struct DashTexture* texture = command._texture_load.texture_nullable;
            {
                const int kcmd = (int)TORIRS_GFX_TEXTURE_LOAD;
                if( d3d8_should_log_cmd(p, kcmd) )
                {
                    if( texture && texture->texels )
                        d3d8_log(
                            "cmd TEXTURE_LOAD tex_id=%d %dx%d opaque=%d",
                            tex_id,
                            texture->width,
                            texture->height,
                            texture->opaque ? 1 : 0);
                    else
                        d3d8_log(
                            "cmd TEXTURE_LOAD tex_id=%d (skip: null or no texels)",
                            tex_id);
                    d3d8_mark_cmd_logged(p, kcmd);
                }
            }
            if( texture && texture->texels && d3d8_trace_on(p) )
                d3d8_log(
                    "TEXTURE_LOAD[%d]: w=%d h=%d stride_hint=%d texels=%p",
                    tex_id,
                    texture->width,
                    texture->height,
                    texture->width * 4,
                    (void*)texture->texels);
            if( game->sys_dash && texture && texture->texels )
            {
                if( d3d8_trace_on(p) )
                    d3d8_log(
                        "TEXTURE_LOAD[%d]: dash3d_add_texture begin sys_dash=%p texels=%p",
                        tex_id,
                        (void*)game->sys_dash,
                        (void*)texture->texels);
                dash3d_add_texture(game->sys_dash, tex_id, texture);
                if( d3d8_trace_on(p) )
                    d3d8_log("TEXTURE_LOAD[%d]: dash3d_add_texture done", tex_id);
            }
            if( !texture || !texture->texels )
                break;
            auto old_it = p->texture_by_id.find(tex_id);
            if( d3d8_trace_on(p) )
                d3d8_log(
                    "TEXTURE_LOAD[%d]: evicting old=%p",
                    tex_id,
                    (void*)(old_it != p->texture_by_id.end() ? old_it->second : nullptr));
            if( old_it != p->texture_by_id.end() && old_it->second )
                old_it->second->Release();
            if( d3d8_trace_on(p) )
                d3d8_log(
                    "TEXTURE_LOAD[%d]: read anim tex=%p dir=%d speed=%d opaque=%d",
                    tex_id,
                    (void*)texture,
                    texture->animation_direction,
                    texture->animation_speed,
                    texture->opaque);
            float anim_signed =
                d3d8_texture_animation_signed(texture->animation_direction, texture->animation_speed);
            if( d3d8_trace_on(p) )
                d3d8_log("TEXTURE_LOAD[%d]: anim_signed=%.4f, inserting anim_speed map", tex_id, anim_signed);
            p->texture_anim_speed_by_id[tex_id] = anim_signed;
            if( d3d8_trace_on(p) )
                d3d8_log("TEXTURE_LOAD[%d]: inserting opaque map", tex_id);
            p->texture_opaque_by_id[tex_id] = texture->opaque ? true : false;
            if( d3d8_trace_on(p) )
                d3d8_log("TEXTURE_LOAD[%d]: maps updated", tex_id);

            IDirect3DTexture8* dt = nullptr;
            if( d3d8_trace_on(p) )
                d3d8_log("TEXTURE_LOAD[%d]: CreateTexture call", tex_id);
            HRESULT htex = dev->CreateTexture(
                (UINT)texture->width,
                (UINT)texture->height,
                1,
                0,
                D3DFMT_A8R8G8B8,
                D3DPOOL_MANAGED,
                &dt);
            if( htex != D3D_OK || !dt )
            {
                d3d8_log(
                    "TEXTURE_LOAD CreateTexture failed hr=0x%08lX tex_id=%d size=%dx%d",
                    (unsigned long)htex,
                    tex_id,
                    texture->width,
                    texture->height);
                break;
            }
            if( d3d8_trace_on(p) )
                d3d8_log("TEXTURE_LOAD[%d]: CreateTexture ok tex=%p", tex_id, (void*)dt);
            D3DLOCKED_RECT lr;
            if( d3d8_trace_on(p) )
                d3d8_log("TEXTURE_LOAD[%d]: LockRect call", tex_id);
            HRESULT hlk = dt->LockRect(0, &lr, nullptr, 0);
            if( hlk != D3D_OK )
            {
                d3d8_log(
                    "TEXTURE_LOAD LockRect failed hr=0x%08lX tex_id=%d",
                    (unsigned long)hlk,
                    tex_id);
                dt->Release();
                break;
            }
            if( d3d8_trace_on(p) )
                d3d8_log(
                    "TEXTURE_LOAD[%d]: LockRect ok pBits=%p Pitch=%ld",
                    tex_id,
                    lr.pBits,
                    (long)lr.Pitch);
            if( d3d8_trace_on(p) )
                d3d8_log(
                    "TEXTURE_LOAD[%d]: memcpy %dx%d pitch=%ld bytes_per_row=%zu",
                    tex_id,
                    texture->width,
                    texture->height,
                    (long)lr.Pitch,
                    (size_t)texture->width * 4u);
            for( int row = 0; row < texture->height; ++row )
                memcpy(
                    (unsigned char*)lr.pBits + row * lr.Pitch,
                    texture->texels + row * texture->width,
                    (size_t)texture->width * 4u);
            if( d3d8_trace_on(p) )
                d3d8_log("TEXTURE_LOAD[%d]: memcpy done", tex_id);
            dt->UnlockRect(0);
            if( d3d8_trace_on(p) )
                d3d8_log("TEXTURE_LOAD[%d]: UnlockRect done", tex_id);
            p->texture_by_id[tex_id] = dt;
            if( d3d8_trace_on(p) )
                d3d8_log("TEXTURE_LOAD[%d]: cached tex=%p", tex_id, (void*)dt);
        }
        break;

        case TORIRS_GFX_MODEL_LOAD:
        {
            struct DashModel* model = command._model_load.model;
            uint64_t mk = command._model_load.model_key;
            {
                const int kcmd = (int)TORIRS_GFX_MODEL_LOAD;
                if( d3d8_should_log_cmd(p, kcmd) )
                {
                    int fc = model ? dashmodel_face_count(model) : -1;
                    d3d8_log(
                        "cmd MODEL_LOAD key=0x%016llX model=%p face_count=%d",
                        (unsigned long long)mk,
                        (void*)model,
                        fc);
                    d3d8_mark_cmd_logged(p, kcmd);
                }
            }
            if( model && p->model_gpu_by_key.find(mk) == p->model_gpu_by_key.end() )
                d3d8_build_model_gpu(p, dev, model, mk);
        }
        break;

        case TORIRS_GFX_MODEL_UNLOAD:
        {
            int mid = command._model_load.model_id;
            {
                const int kcmd = (int)TORIRS_GFX_MODEL_UNLOAD;
                if( d3d8_should_log_cmd(p, kcmd) )
                {
                    d3d8_log("cmd MODEL_UNLOAD model_id=%d", mid);
                    d3d8_mark_cmd_logged(p, kcmd);
                }
            }
            if( mid <= 0 )
                break;
            for( auto it = p->batched_model_batch_by_key.begin();
                 it != p->batched_model_batch_by_key.end(); )
            {
                if( model_id_from_model_cache_key(it->first) == mid )
                    it = p->batched_model_batch_by_key.erase(it);
                else
                    ++it;
            }
            for( auto it = p->model_gpu_by_key.begin(); it != p->model_gpu_by_key.end(); )
            {
                if( model_id_from_model_cache_key(it->first) == mid )
                {
                    d3d8_release_model_gpu(it->second);
                    it = p->model_gpu_by_key.erase(it);
                }
                else
                    ++it;
            }
        }
        break;

        case TORIRS_GFX_BATCH_MODEL_LOAD_START:
        {
            uint32_t bid = command._batch.batch_id;
            if( p->current_batch )
            {
                d3d8_log(
                    "BATCH_MODEL_LOAD_START: replacing incomplete batch id=%u",
                    (unsigned)p->current_batch->batch_id);
                d3d8_release_model_batch(p->current_batch);
                p->current_batch = nullptr;
            }
            p->current_batch = new D3D8ModelBatch();
            p->current_batch->batch_id = bid;
        }
        break;

        case TORIRS_GFX_MODEL_BATCHED_LOAD:
        {
            struct DashModel* model = command._model_load.model;
            uint64_t mk = command._model_load.model_key;
            if( !p->current_batch || !model )
                break;
            if( dashmodel_face_count(model) <= 0 )
                break;
            if( !dashmodel_face_colors_a_const(model) || !dashmodel_face_colors_b_const(model) ||
                !dashmodel_face_colors_c_const(model) )
                break;
            if( !dashmodel_vertices_x_const(model) || !dashmodel_vertices_y_const(model) ||
                !dashmodel_vertices_z_const(model) )
                break;
            if( !dashmodel_face_indices_a_const(model) || !dashmodel_face_indices_b_const(model) ||
                !dashmodel_face_indices_c_const(model) )
                break;

            const int fc = dashmodel_face_count(model);
            const faceint_t* ftex = dashmodel_face_textures_const(model);
            D3D8BatchModelEntry ent;
            ent.model_key = mk;
            ent.model_id = command._model_load.model_id;
            ent.vertex_base = p->current_batch->total_vertex_count;
            ent.face_count = fc;
            ent.per_face_raw_tex_id.resize((size_t)fc);

            for( int f = 0; f < fc; ++f )
            {
                int raw = ftex ? (int)ftex[f] : -1;
                ent.per_face_raw_tex_id[(size_t)f] = raw;
                D3D8WorldVertex tri[3];
                if( !fill_model_face_vertices_model_local(model, f, raw, tri) )
                {
                    d3d8_vertex_fill_invisible(&tri[0]);
                    d3d8_vertex_fill_invisible(&tri[1]);
                    d3d8_vertex_fill_invisible(&tri[2]);
                }
                p->current_batch->pending_verts.push_back(tri[0]);
                p->current_batch->pending_verts.push_back(tri[1]);
                p->current_batch->pending_verts.push_back(tri[2]);
                /* Per-model-local indices; BaseVertexIndex on SetIndices supplies batch offset. */
                p->current_batch->pending_indices.push_back((uint16_t)(f * 3 + 0));
                p->current_batch->pending_indices.push_back((uint16_t)(f * 3 + 1));
                p->current_batch->pending_indices.push_back((uint16_t)(f * 3 + 2));
            }
            p->current_batch->total_vertex_count += fc * 3;
            p->current_batch->entries_by_key[mk] = std::move(ent);
            p->batched_model_batch_by_key[mk] = p->current_batch;
        }
        break;

        case TORIRS_GFX_BATCH_MODEL_LOAD_END:
        {
            uint32_t bid = command._batch.batch_id;
            if( !p->current_batch || p->current_batch->batch_id != bid )
                break;
            D3D8ModelBatch* batch = p->current_batch;
            p->current_batch = nullptr;
            if( batch->pending_verts.empty() )
            {
                delete batch;
                break;
            }

            const UINT vbytes = (UINT)(batch->pending_verts.size() * sizeof(D3D8WorldVertex));
            IDirect3DVertexBuffer8* vbo = nullptr;
            HRESULT hrvb = dev->CreateVertexBuffer(
                vbytes, D3DUSAGE_WRITEONLY, kFvfWorld, D3DPOOL_MANAGED, &vbo);
            if( hrvb != D3D_OK || !vbo )
            {
                d3d8_log(
                    "BATCH_MODEL_LOAD_END: CreateVertexBuffer failed hr=0x%08lX",
                    (unsigned long)hrvb);
                delete batch;
                break;
            }
            void* vdst = nullptr;
            HRESULT hvlock = vbo->Lock(0, vbytes, (BYTE**)&vdst, 0);
            if( hvlock != D3D_OK )
            {
                d3d8_log(
                    "BATCH_MODEL_LOAD_END: VBO Lock failed hr=0x%08lX",
                    (unsigned long)hvlock);
                vbo->Release();
                delete batch;
                break;
            }
            memcpy(vdst, batch->pending_verts.data(), vbytes);
            vbo->Unlock();
            batch->vbo = vbo;
            batch->pending_verts.clear();

            static unsigned s_batch_end_logs = 0;
            uint16_t max_idx = 0;
            if( !batch->pending_indices.empty() )
            {
                for( uint16_t ix : batch->pending_indices )
                {
                    if( ix > max_idx )
                        max_idx = ix;
                }
            }
            if( s_batch_end_logs < 4u )
            {
                s_batch_end_logs++;
                d3d8_log(
                    "BATCH_MODEL_LOAD_END[%u]: batch_id=%u total_vertex_count=%d "
                    "index_count=%zu max_per_model_local_index=%u",
                    s_batch_end_logs,
                    (unsigned)bid,
                    batch->total_vertex_count,
                    batch->pending_indices.size(),
                    (unsigned)max_idx);
            }

            const UINT ibytes = (UINT)(batch->pending_indices.size() * sizeof(uint16_t));
            IDirect3DIndexBuffer8* ibo = nullptr;
            HRESULT hrib =
                dev->CreateIndexBuffer(ibytes, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ibo);
            if( hrib != D3D_OK || !ibo )
            {
                d3d8_log(
                    "BATCH_MODEL_LOAD_END: CreateIndexBuffer failed hr=0x%08lX",
                    (unsigned long)hrib);
                batch->vbo->Release();
                batch->vbo = nullptr;
                delete batch;
                break;
            }
            void* idst = nullptr;
            HRESULT hilock = ibo->Lock(0, ibytes, (BYTE**)&idst, 0);
            if( hilock != D3D_OK )
            {
                d3d8_log(
                    "BATCH_MODEL_LOAD_END: IBO Lock failed hr=0x%08lX",
                    (unsigned long)hilock);
                ibo->Release();
                batch->vbo->Release();
                batch->vbo = nullptr;
                delete batch;
                break;
            }
            memcpy(idst, batch->pending_indices.data(), ibytes);
            ibo->Unlock();
            batch->ebo = ibo;
            batch->pending_indices.clear();

            p->batches_by_id[batch->batch_id] = batch;
        }
        break;

        case TORIRS_GFX_BATCH_MODEL_CLEAR:
        {
            uint32_t bid = command._batch.batch_id;
            auto bit = p->batches_by_id.find(bid);
            if( bit == p->batches_by_id.end() || !bit->second )
                break;
            D3D8ModelBatch* batch = bit->second;
            p->batches_by_id.erase(bit);
            for( auto it = p->batched_model_batch_by_key.begin();
                 it != p->batched_model_batch_by_key.end(); )
            {
                if( it->second == batch )
                    it = p->batched_model_batch_by_key.erase(it);
                else
                    ++it;
            }
            d3d8_release_model_batch(batch);
        }
        break;

        case TORIRS_GFX_VERTEX_ARRAY_BATCHED_LOAD:
        case TORIRS_GFX_FACE_ARRAY_BATCHED_LOAD:
            break;

        case TORIRS_GFX_SPRITE_LOAD:
        {
            struct DashSprite* sp = command._sprite_load.sprite;
            {
                const int kcmd = (int)TORIRS_GFX_SPRITE_LOAD;
                if( d3d8_should_log_cmd(p, kcmd) )
                {
                    d3d8_log(
                        "cmd SPRITE_LOAD element_id=%d atlas_index=%d sp=%p",
                        command._sprite_load.element_id,
                        command._sprite_load.atlas_index,
                        (void*)sp);
                    if( sp && sp->pixels_argb )
                        d3d8_log(
                            "cmd SPRITE_LOAD size=%dx%d",
                            sp->width,
                            sp->height);
                    d3d8_mark_cmd_logged(p, kcmd);
                }
            }
            if( !sp || !sp->pixels_argb || sp->width <= 0 || sp->height <= 0 )
                break;
            uint64_t sk = torirs_sprite_cache_key(
                command._sprite_load.element_id, command._sprite_load.atlas_index);
            auto it = p->sprite_by_slot.find(sk);
            if( it != p->sprite_by_slot.end() && it->second )
            {
                it->second->Release();
                p->sprite_by_slot.erase(it);
            }
            IDirect3DTexture8* st = nullptr;
            HRESULT hsp = dev->CreateTexture(
                (UINT)sp->width,
                (UINT)sp->height,
                1,
                0,
                D3DFMT_A8R8G8B8,
                D3DPOOL_MANAGED,
                &st);
            if( hsp != D3D_OK || !st )
            {
                d3d8_log(
                    "SPRITE_LOAD CreateTexture failed hr=0x%08lX key=0x%016llX %dx%d",
                    (unsigned long)hsp,
                    (unsigned long long)sk,
                    sp->width,
                    sp->height);
                break;
            }
            D3DLOCKED_RECT lr;
            HRESULT hsl = st->LockRect(0, &lr, nullptr, 0);
            if( hsl != D3D_OK )
            {
                d3d8_log(
                    "SPRITE_LOAD LockRect failed hr=0x%08lX key=0x%016llX",
                    (unsigned long)hsl,
                    (unsigned long long)sk);
                st->Release();
                break;
            }
            for( int py = 0; py < sp->height; ++py )
            {
                uint32_t* dst = (uint32_t*)((unsigned char*)lr.pBits + py * lr.Pitch);
                for( int px = 0; px < sp->width; ++px )
                {
                    uint32_t pix = (uint32_t)sp->pixels_argb[py * sp->width + px];
                    uint8_t a_hi = (uint8_t)((pix >> 24) & 0xFFu);
                    uint32_t rgb = pix & 0x00FFFFFFu;
                    uint8_t a = 0;
                    if( a_hi != 0 )
                        a = a_hi;
                    else if( rgb != 0u )
                        a = 0xFFu;
                    dst[px] = (pix & 0x00FFFFFFu) | ((uint32_t)a << 24);
                }
            }
            st->UnlockRect(0);
            p->sprite_by_slot[sk] = st;
            p->sprite_size_by_slot[sk] = std::make_pair(sp->width, sp->height);
        }
        break;

        case TORIRS_GFX_SPRITE_UNLOAD:
        {
            {
                const int kcmd = (int)TORIRS_GFX_SPRITE_UNLOAD;
                if( d3d8_should_log_cmd(p, kcmd) )
                {
                    d3d8_log(
                        "cmd SPRITE_UNLOAD element_id=%d atlas_index=%d",
                        command._sprite_load.element_id,
                        command._sprite_load.atlas_index);
                    d3d8_mark_cmd_logged(p, kcmd);
                }
            }
            uint64_t sk = torirs_sprite_cache_key(
                command._sprite_load.element_id, command._sprite_load.atlas_index);
            auto it = p->sprite_by_slot.find(sk);
            if( it != p->sprite_by_slot.end() )
            {
                if( it->second )
                    it->second->Release();
                p->sprite_by_slot.erase(it);
            }
            p->sprite_size_by_slot.erase(sk);
        }
        break;

        case TORIRS_GFX_FONT_LOAD:
        {
            struct DashPixFont* f = command._font_load.font;
            int font_id = command._font_load.font_id;
            {
                const int kcmd = (int)TORIRS_GFX_FONT_LOAD;
                if( d3d8_should_log_cmd(p, kcmd) )
                {
                    d3d8_log(
                        "cmd FONT_LOAD font_id=%d font=%p",
                        font_id,
                        (void*)f);
                    if( f && f->atlas )
                        d3d8_log(
                            "cmd FONT_LOAD atlas %dx%d",
                            f->atlas->atlas_width,
                            f->atlas->atlas_height);
                    d3d8_mark_cmd_logged(p, kcmd);
                }
            }
            if( !f || !f->atlas || p->font_atlas_by_id.count(font_id) )
                break;
            struct DashFontAtlas* a = f->atlas;
            IDirect3DTexture8* at = nullptr;
            HRESULT hfa = dev->CreateTexture(
                (UINT)a->atlas_width,
                (UINT)a->atlas_height,
                1,
                0,
                D3DFMT_A8R8G8B8,
                D3DPOOL_MANAGED,
                &at);
            if( hfa != D3D_OK || !at )
            {
                d3d8_log(
                    "FONT_LOAD CreateTexture failed hr=0x%08lX font_id=%d atlas=%dx%d",
                    (unsigned long)hfa,
                    font_id,
                    a->atlas_width,
                    a->atlas_height);
                break;
            }
            D3DLOCKED_RECT lr;
            HRESULT hfal = at->LockRect(0, &lr, nullptr, 0);
            if( hfal != D3D_OK )
            {
                d3d8_log(
                    "FONT_LOAD LockRect failed hr=0x%08lX font_id=%d",
                    (unsigned long)hfal,
                    font_id);
                at->Release();
                break;
            }
            for( int row = 0; row < a->atlas_height; ++row )
                memcpy(
                    (unsigned char*)lr.pBits + row * lr.Pitch,
                    a->rgba_pixels + row * a->atlas_width,
                    (size_t)a->atlas_width * 4u);
            at->UnlockRect(0);
            p->font_atlas_by_id[font_id] = at;
        }
        break;

        case TORIRS_GFX_CLEAR_RECT:
        {
            flush_font();
            int cx = command._clear_rect.x;
            int cy = command._clear_rect.y;
            int cw = command._clear_rect.w;
            int ch = command._clear_rect.h;
            {
                const int kcmd = (int)TORIRS_GFX_CLEAR_RECT;
                if( d3d8_should_log_cmd(p, kcmd) )
                {
                    d3d8_log("cmd CLEAR_RECT %d,%d %dx%d", cx, cy, cw, ch);
                    d3d8_mark_cmd_logged(p, kcmd);
                }
            }
            if( cw <= 0 || ch <= 0 )
                break;
            D3DVIEWPORT8 saved;
            dev->GetViewport(&saved);
            D3DVIEWPORT8 cvp = {
                (DWORD)cx,
                (DWORD)cy,
                (DWORD)cw,
                (DWORD)ch,
                0.0f,
                1.0f
            };
            if( (int)cvp.X + (int)cvp.Width > renderer->width )
                cvp.Width = (DWORD)(renderer->width - (int)cvp.X);
            if( (int)cvp.Y + (int)cvp.Height > renderer->height )
                cvp.Height = (DWORD)(renderer->height - (int)cvp.Y);
            dev->SetViewport(&cvp);
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0x00000000, 1.0f, 0);
            dev->SetViewport(&saved);
        }
        break;

        case TORIRS_GFX_MODEL_DRAW:
        {
            struct DashModel* model = command._model_draw.model;
            if( !model || !game->sys_dash || !game->view_port || !game->camera )
                break;
            d3d8_ensure_pass(p, dev, PASS_3D);
            uint64_t mk_draw = command._model_draw.model_key;
            int vertex_index_base = 0;
            UINT num_vertices_for_draw = 0;
            D3D8ModelGpu* gpu = nullptr;
            D3D8ModelGpu batched_wrap{};
            D3D8ModelBatch* batched_batch_ptr = nullptr;

            auto bmit = p->batched_model_batch_by_key.find(mk_draw);
            if( bmit != p->batched_model_batch_by_key.end() && bmit->second )
            {
                batched_batch_ptr = bmit->second;
                auto eit = batched_batch_ptr->entries_by_key.find(mk_draw);
                if( eit == batched_batch_ptr->entries_by_key.end() )
                    break;
                const D3D8BatchModelEntry& ent = eit->second;
                vertex_index_base = ent.vertex_base;
                batched_wrap.vbo = batched_batch_ptr->vbo;
                batched_wrap.face_count = ent.face_count;
                batched_wrap.per_face_raw_tex_id = ent.per_face_raw_tex_id;
                gpu = &batched_wrap;
            }
            else
            {
                gpu = d3d8_lookup_or_build_model_gpu(p, dev, model, mk_draw);
                if( !gpu || !gpu->vbo )
                    break;
            }
            if( !gpu || !gpu->vbo )
                break;
            /* D3D8 DIP NumVertices: count for this model only; BaseVertexIndex on SetIndices
             * selects the sub-range inside a shared batched VBO. */
            num_vertices_for_draw = (UINT)(gpu->face_count * 3);

            struct DashPosition draw_position = command._model_draw.position;
            int face_order_count = dash3d_prepare_projected_face_order(
                game->sys_dash, model, &draw_position, game->view_port, game->camera);
            const int* face_order =
                dash3d_projected_face_order(game->sys_dash, &face_order_count);
            if( face_order_count <= 0 || !face_order )
                break;

            {
                const int kcmd = (int)TORIRS_GFX_MODEL_DRAW;
                if( d3d8_should_log_cmd(p, kcmd) )
                {
                    d3d8_log(
                        "cmd MODEL_DRAW key=0x%016llX pos=(%d,%d,%d) yaw=%d gpu_faces=%d "
                        "order_count=%d",
                        (unsigned long long)command._model_draw.model_key,
                        draw_position.x,
                        draw_position.y,
                        draw_position.z,
                        (int)draw_position.yaw,
                        gpu->face_count,
                        face_order_count);
                    d3d8_mark_cmd_logged(p, kcmd);
                }
            }

            unsigned int ib_batches = 0;
            unsigned int draws_before = p->debug_model_draws;
            unsigned int tris_before = p->debug_triangles;

            const float yaw_rad_m =
                (draw_position.yaw * 2.0f * (float)M_PI) / 2048.0f;
            const float cos_yaw = cosf(yaw_rad_m);
            const float sin_yaw = sinf(yaw_rad_m);

            D3DMATRIX world;
            memset(&world, 0, sizeof(world));
            world._11 = cos_yaw;
            world._12 = 0.0f;
            world._13 = -sin_yaw;
            world._14 = 0.0f;
            world._21 = 0.0f;
            world._22 = 1.0f;
            world._23 = 0.0f;
            world._24 = 0.0f;
            world._31 = sin_yaw;
            world._32 = 0.0f;
            world._33 = cos_yaw;
            world._34 = 0.0f;
            world._41 = (float)draw_position.x;
            world._42 = (float)draw_position.y;
            world._43 = (float)draw_position.z;
            world._44 = 1.0f;
            dev->SetTransform(D3DTS_WORLD, &world);

            if( s_model_draw_debug < 2u )
            {
                d3d8_log(
                    "MODEL_DRAW dbg[%u] batched=%d vertex_index_base=%d num_vertices_for_draw=%u "
                    "buf=%dx%d vp=%dx%d pos=(%d,%d,%d) yaw=%d",
                    s_model_draw_debug,
                    batched_batch_ptr ? 1 : 0,
                    vertex_index_base,
                    (unsigned)num_vertices_for_draw,
                    renderer->width,
                    renderer->height,
                    vp_w,
                    vp_h,
                    draw_position.x,
                    draw_position.y,
                    draw_position.z,
                    (int)draw_position.yaw);
                d3d8_log(
                    "MODEL_DRAW dbg world row3: %f %f %f %f",
                    world._41,
                    world._42,
                    world._43,
                    world._44);
                d3d8_log(
                    "MODEL_DRAW dbg view row3: %f %f %f %f",
                    p->frame_view._41,
                    p->frame_view._42,
                    p->frame_view._43,
                    p->frame_view._44);
                d3d8_log(
                    "MODEL_DRAW dbg proj row0: %f %f %f %f",
                    p->frame_proj._11,
                    p->frame_proj._12,
                    p->frame_proj._13,
                    p->frame_proj._14);
                d3d8_log(
                    "MODEL_DRAW dbg proj row3: %f %f %f %f",
                    p->frame_proj._41,
                    p->frame_proj._42,
                    p->frame_proj._43,
                    p->frame_proj._44);
                if( !batched_batch_ptr )
                {
                    const D3D8WorldVertex* fv = gpu->first_face_verts;
                    const D3D8WorldVertex* lv = gpu->last_face_verts;
                    float wsp[4], vsp[4], clip[4];
                    float v0[4] = { fv[0].x, fv[0].y, fv[0].z, 1.0f };
                    d3d8_mul_row_vec_d3dmatrix(v0, &world, wsp);
                    d3d8_mul_row_vec_d3dmatrix(wsp, &p->frame_view, vsp);
                    d3d8_mul_row_vec_d3dmatrix(vsp, &p->frame_proj, clip);
                    const float wclip = clip[3];
                    const float invw = (wclip != 0.0f) ? (1.0f / wclip) : 0.0f;
                    d3d8_log(
                        "MODEL_DRAW dbg first_face v_local=(%f,%f,%f) clip=(%f,%f,%f,%f) "
                        "ndc=(%f,%f,%f)",
                        fv[0].x,
                        fv[0].y,
                        fv[0].z,
                        clip[0],
                        clip[1],
                        clip[2],
                        wclip,
                        clip[0] * invw,
                        clip[1] * invw,
                        clip[2] * invw);
                    float vL[4] = { lv[0].x, lv[0].y, lv[0].z, 1.0f };
                    d3d8_mul_row_vec_d3dmatrix(vL, &world, wsp);
                    d3d8_mul_row_vec_d3dmatrix(wsp, &p->frame_view, vsp);
                    d3d8_mul_row_vec_d3dmatrix(vsp, &p->frame_proj, clip);
                    const float wclipL = clip[3];
                    const float invwL = (wclipL != 0.0f) ? (1.0f / wclipL) : 0.0f;
                    d3d8_log(
                        "MODEL_DRAW dbg last_face v0_local=(%f,%f,%f) clip=(%f,%f,%f,%f) "
                        "ndc=(%f,%f,%f)",
                        lv[0].x,
                        lv[0].y,
                        lv[0].z,
                        clip[0],
                        clip[1],
                        clip[2],
                        wclipL,
                        clip[0] * invwL,
                        clip[1] * invwL,
                        clip[2] * invwL);
                }
                else
                {
                    d3d8_log(
                        "MODEL_DRAW dbg (batched VBO: no CPU first/last_face verts for clip)");
                }
                s_model_draw_debug++;
            }

            auto eff_tex = [&](int f) -> int {
                if( f < 0 || f >= gpu->face_count )
                    return -1;
                int raw = gpu->per_face_raw_tex_id[(size_t)f];
                if( raw < 0 )
                    return -1;
                if( p->texture_by_id.find(raw) == p->texture_by_id.end() )
                    return -1;
                return raw;
            };

            dev->SetStreamSource(0, gpu->vbo, sizeof(D3D8WorldVertex));

            int run_start = 0;
            int run_tex = eff_tex(face_order[0]);
            for( int i = 1; i <= face_order_count; ++i )
            {
                int t = (i < face_order_count) ? eff_tex(face_order[i]) : INT_MIN;
                if( i < face_order_count && t == run_tex )
                    continue;

                const int nfaces = i - run_start;
                if( nfaces <= 0 )
                {
                    run_start = i;
                    run_tex = t;
                    continue;
                }

                size_t nidx = 0;
                for( int k = 0; k < nfaces; ++k )
                {
                    const int f = face_order[run_start + k];
                    if( f >= 0 && f < gpu->face_count )
                        nidx += 3u;
                }
                if( nidx == 0 )
                {
                    run_start = i;
                    run_tex = t;
                    continue;
                }

                const UINT nidx_bytes = (UINT)(nidx * sizeof(uint16_t));
                if( p->ib_ring_write_offset + nidx_bytes > p->ib_ring_size_bytes )
                    break;
                BYTE* lock_dst = nullptr;
                DWORD lock_flags =
                    p->ib_ring_write_offset == 0 ? D3DLOCK_DISCARD : D3DLOCK_NOOVERWRITE;
                HRESULT hr_ib = p->ib_ring->Lock(
                    (UINT)p->ib_ring_write_offset,
                    nidx_bytes,
                    (BYTE**)&lock_dst,
                    lock_flags);
                if( hr_ib != D3D_OK )
                {
                    d3d8_log(
                        "MODEL_DRAW ib_ring->Lock failed hr=0x%08lX off=%zu bytes=%u",
                        (unsigned long)hr_ib,
                        (size_t)p->ib_ring_write_offset,
                        (unsigned)nidx_bytes);
                    break;
                }
                uint16_t* idst = (uint16_t*)lock_dst;
                size_t wpos = 0;
                for( int k = 0; k < nfaces; ++k )
                {
                    const int f = face_order[run_start + k];
                    if( f < 0 || f >= gpu->face_count )
                        continue;
                    /* Per-model-local indices; device adds BaseVertexIndex from SetIndices. */
                    idst[wpos++] = (uint16_t)(f * 3 + 0);
                    idst[wpos++] = (uint16_t)(f * 3 + 1);
                    idst[wpos++] = (uint16_t)(f * 3 + 2);
                }
                p->ib_ring->Unlock();
                HRESULT hr_si =
                    dev->SetIndices(p->ib_ring, (UINT)vertex_index_base);

                if( run_tex < 0 )
                {
                    dev->SetTexture(0, nullptr);
                    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
                    d3d8_disable_texture_transform(dev);
                    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
                }
                else
                {
                    dev->SetTexture(0, p->texture_by_id[run_tex]);
                    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
                    float speed = p->texture_anim_speed_by_id[run_tex];
                    if( speed != 0.0f )
                    {
                        if( speed > 0.0f )
                            d3d8_apply_texture_matrix_translate(dev, u_clock * speed, 0.0f);
                        else
                            d3d8_apply_texture_matrix_translate(dev, 0.0f, u_clock * speed);
                    }
                    else
                        d3d8_disable_texture_transform(dev);

                    bool opaque = true;
                    auto oi = p->texture_opaque_by_id.find(run_tex);
                    if( oi != p->texture_opaque_by_id.end() )
                        opaque = oi->second;
                    dev->SetRenderState(D3DRS_ALPHATESTENABLE, opaque ? FALSE : TRUE);
                }

                UINT start_index = (UINT)(p->ib_ring_write_offset / sizeof(uint16_t));
                const UINT tri_count = (UINT)(nidx / 3u);
                HRESULT hr_dip = dev->DrawIndexedPrimitive(
                    D3DPT_TRIANGLELIST,
                    0,
                    num_vertices_for_draw,
                    start_index,
                    tri_count);
                if( s_dip_full_log < 4u )
                {
                    d3d8_log(
                        "MODEL_DRAW dbg DIP SetIndices(base=%u) hr_si=0x%08lX "
                        "minIndex=0 NumVertices=%u startIndex=%u primCount=%u hr_dip=0x%08lX",
                        (unsigned)vertex_index_base,
                        (unsigned long)hr_si,
                        num_vertices_for_draw,
                        (unsigned)start_index,
                        (unsigned)tri_count,
                        (unsigned long)hr_dip);
                    s_dip_full_log++;
                }
                if( FAILED(hr_dip) )
                    d3d8_log(
                        "MODEL_DRAW DrawIndexedPrimitive failed hr=0x%08lX start_index=%u "
                        "tri_count=%u",
                        (unsigned long)hr_dip,
                        (unsigned)start_index,
                        (unsigned)tri_count);
                p->ib_ring_write_offset += nidx_bytes;
                p->debug_model_draws++;
                p->debug_triangles += tri_count;
                ib_batches++;

                run_start = i;
                run_tex = t;
            }
            if( d3d8_trace_on(p) )
                d3d8_log(
                    "MODEL_DRAW done key=0x%016llX ib_batches=%u draws_this=%u tris_this=%u",
                    (unsigned long long)command._model_draw.model_key,
                    ib_batches,
                    (unsigned)(p->debug_model_draws - draws_before),
                    (unsigned)(p->debug_triangles - tris_before));
            d3d8_disable_texture_transform(dev);
            dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        }
        break;

        case TORIRS_GFX_SPRITE_DRAW:
        {
            {
                const int kcmd = (int)TORIRS_GFX_SPRITE_DRAW;
                if( d3d8_should_log_cmd(p, kcmd) )
                {
                    d3d8_log(
                        "cmd SPRITE_DRAW element=%d atlas=%d rotated=%d dst=%d,%d %dx%d src=%d,%d "
                        "%dx%d",
                        command._sprite_draw.element_id,
                        command._sprite_draw.atlas_index,
                        command._sprite_draw.rotated ? 1 : 0,
                        command._sprite_draw.dst_bb_x,
                        command._sprite_draw.dst_bb_y,
                        command._sprite_draw.dst_bb_w,
                        command._sprite_draw.dst_bb_h,
                        command._sprite_draw.src_bb_x,
                        command._sprite_draw.src_bb_y,
                        command._sprite_draw.src_bb_w,
                        command._sprite_draw.src_bb_h);
                    d3d8_mark_cmd_logged(p, kcmd);
                }
            }
            struct DashSprite* sp = command._sprite_draw.sprite;
            if( !sp )
                break;
            uint64_t sk = torirs_sprite_cache_key(
                command._sprite_draw.element_id, command._sprite_draw.atlas_index);
            auto tex_it = p->sprite_by_slot.find(sk);
            if( tex_it == p->sprite_by_slot.end() || !tex_it->second )
                break;
            int tw = sp->width;
            int th = sp->height;
            auto sz_it = p->sprite_size_by_slot.find(sk);
            if( sz_it != p->sprite_size_by_slot.end() )
            {
                tw = sz_it->second.first;
                th = sz_it->second.second;
            }
            const int iw =
                command._sprite_draw.src_bb_w > 0 ? command._sprite_draw.src_bb_w : sp->width;
            const int ih =
                command._sprite_draw.src_bb_h > 0 ? command._sprite_draw.src_bb_h : sp->height;
            const int ix = command._sprite_draw.src_bb_x;
            const int iy = command._sprite_draw.src_bb_y;
            if( ix < 0 || iy < 0 || ix + iw > sp->width || iy + ih > sp->height )
                break;

            flush_font();
            d3d8_ensure_pass(p, dev, PASS_2D);
            dev->SetTexture(0, tex_it->second);
            dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
            dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);

            float px[4];
            float py[4];
            D3DVIEWPORT8 saved_vp = {};
            bool sprite_vp_overridden = false;
            if( command._sprite_draw.rotated )
            {
                const int dw = command._sprite_draw.dst_bb_w;
                const int dh = command._sprite_draw.dst_bb_h;
                if( dw <= 0 || dh <= 0 || iw <= 0 || ih <= 0 )
                    break;
                D3DVIEWPORT8 spriteVP = {
                    (DWORD)(command._sprite_draw.dst_bb_x > 0 ? command._sprite_draw.dst_bb_x : 0),
                    (DWORD)(command._sprite_draw.dst_bb_y > 0 ? command._sprite_draw.dst_bb_y : 0),
                    (DWORD)dw,
                    (DWORD)dh,
                    0.0f,
                    1.0f
                };
                if( (int)spriteVP.X + (int)spriteVP.Width > renderer->width )
                    spriteVP.Width = (DWORD)(renderer->width - (int)spriteVP.X);
                if( (int)spriteVP.Y + (int)spriteVP.Height > renderer->height )
                    spriteVP.Height = (DWORD)(renderer->height - (int)spriteVP.Y);
                if( (int)spriteVP.Width > 0 && (int)spriteVP.Height > 0 )
                {
                    dev->GetViewport(&saved_vp);
                    dev->SetViewport(&spriteVP);
                    sprite_vp_overridden = true;
                }
                const int sax = command._sprite_draw.src_anchor_x;
                const int say = command._sprite_draw.src_anchor_y;
                const float pivot_x =
                    (float)command._sprite_draw.dst_bb_x + (float)command._sprite_draw.dst_anchor_x;
                const float pivot_y =
                    (float)command._sprite_draw.dst_bb_y + (float)command._sprite_draw.dst_anchor_y;
                const int ang = command._sprite_draw.rotation_r2pi2048 & 2047;
                const float angle = (float)ang * (float)(2.0 * M_PI / 2048.0);
                const float ca = cosf(angle);
                const float sa = sinf(angle);
                struct
                {
                    int lx, ly;
                } corners[4] = {
                    { 0, 0 },
                    { iw, 0 },
                    { iw, ih },
                    { 0, ih },
                };
                for( int k = 0; k < 4; ++k )
                {
                    float Lx = (float)(corners[k].lx - sax);
                    float Ly = (float)(corners[k].ly - say);
                    px[k] = pivot_x + ca * Lx - sa * Ly;
                    py[k] = pivot_y + sa * Lx + ca * Ly;
                }
            }
            else
            {
                const int dst_x = command._sprite_draw.dst_bb_x + sp->crop_x;
                const int dst_y = command._sprite_draw.dst_bb_y + sp->crop_y;
                const float w = (float)iw;
                const float h = (float)ih;
                const float x0 = (float)dst_x;
                const float y0 = (float)dst_y;
                px[0] = px[3] = x0;
                px[1] = px[2] = x0 + w;
                py[0] = py[1] = y0;
                py[2] = py[3] = y0 + h;
            }

            const float twf = (float)tw;
            const float thf = (float)th;
            float u0 = (float)ix / twf;
            float v0 = (float)iy / thf;
            float u1 = (float)(ix + iw) / twf;
            float v1 = (float)(iy + ih) / thf;

            DWORD white = 0xFFFFFFFF;
            D3D8UiVertex v[6] = {
                { px[0], py[0], 0.0f, 1.0f, white, u0, v0 },
                { px[1], py[1], 0.0f, 1.0f, white, u1, v0 },
                { px[2], py[2], 0.0f, 1.0f, white, u1, v1 },
                { px[0], py[0], 0.0f, 1.0f, white, u0, v0 },
                { px[2], py[2], 0.0f, 1.0f, white, u1, v1 },
                { px[3], py[3], 0.0f, 1.0f, white, u0, v1 },
            };
            dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(D3D8UiVertex));
            if( sprite_vp_overridden )
                dev->SetViewport(&saved_vp);
            dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        }
        break;

        case TORIRS_GFX_FONT_DRAW:
        {
            struct DashPixFont* f = command._font_draw.font;
            const uint8_t* text = command._font_draw.text;
            {
                const int kcmd = (int)TORIRS_GFX_FONT_DRAW;
                if( d3d8_should_log_cmd(p, kcmd) && text )
                {
                    char preview[48];
                    int pi = 0;
                    for( int ci = 0; ci < 28 && text[ci] && pi < (int)sizeof(preview) - 2; ++ci )
                    {
                        unsigned char c = text[ci];
                        if( c >= 32u && c < 127u )
                            preview[pi++] = (char)c;
                        else
                        {
                            int w = snprintf(
                                preview + pi,
                                sizeof(preview) - (size_t)pi,
                                "\\x%02x",
                                (unsigned)c);
                            if( w > 0 )
                                pi += w;
                            if( pi >= (int)sizeof(preview) - 1 )
                                break;
                        }
                    }
                    preview[pi] = '\0';
                    d3d8_log(
                        "cmd FONT_DRAW font_id=%d xy=%d,%d preview=\"%s\"",
                        command._font_draw.font_id,
                        command._font_draw.x,
                        command._font_draw.y,
                        preview);
                    d3d8_mark_cmd_logged(p, kcmd);
                }
            }
            if( !f || !f->atlas || !text || f->height2d <= 0 )
                break;
            int fid = command._font_draw.font_id;
            if( p->font_atlas_by_id.find(fid) == p->font_atlas_by_id.end() )
                break;

            if( p->current_font_id != fid )
            {
                flush_font();
                p->current_font_id = fid;
            }

            struct DashFontAtlas* atlas = f->atlas;
            const float inv_aw = 1.0f / (float)atlas->atlas_width;
            const float inv_ah = 1.0f / (float)atlas->atlas_height;

            int length = (int)strlen((const char*)text);
            int color_rgb = command._font_draw.color_rgb;
            float cr = (float)((color_rgb >> 16) & 0xFF) / 255.0f;
            float cg = (float)((color_rgb >> 8) & 0xFF) / 255.0f;
            float cb = (float)(color_rgb & 0xFF) / 255.0f;
            float ca = 1.0f;
            int pen_x = command._font_draw.x;
            int base_y = command._font_draw.y;

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

                        float gu0 = (float)atlas->glyph_x[c] * inv_aw;
                        float gv0 = (float)atlas->glyph_y[c] * inv_ah;
                        float gu1 = (float)(atlas->glyph_x[c] + gw) * inv_aw;
                        float gv1 = (float)(atlas->glyph_y[c] + gh) * inv_ah;

                        DWORD diff = ((DWORD)(ca * 255.0f) << 24) |
                            ((DWORD)(cr * 255.0f) << 16) | ((DWORD)(cg * 255.0f) << 8) |
                            (DWORD)(cb * 255.0f);

                        D3D8UiVertex q[6] = {
                            { sx0, sy0, 0, 1, diff, gu0, gv0 },
                            { sx1, sy0, 0, 1, diff, gu1, gv0 },
                            { sx1, sy1, 0, 1, diff, gu1, gv1 },
                            { sx0, sy0, 0, 1, diff, gu0, gv0 },
                            { sx1, sy1, 0, 1, diff, gu1, gv1 },
                            { sx0, sy1, 0, 1, diff, gu0, gv1 },
                        };
                        font_verts.insert(font_verts.end(), q, q + 6);
                    }
                    int adv = f->char_advance[c];
                    if( adv <= 0 )
                        adv = 4;
                    pen_x += adv;
                }
                else
                {
                    pen_x += 4;
                }
            }
        }
        break;

        case TORIRS_GFX_BEGIN_3D:
        case TORIRS_GFX_END_3D:
        case TORIRS_GFX_BEGIN_2D:
        case TORIRS_GFX_END_2D:
        case TORIRS_GFX_VERTEX_ARRAY_LOAD:
        case TORIRS_GFX_VERTEX_ARRAY_UNLOAD:
        case TORIRS_GFX_FACE_ARRAY_LOAD:
        case TORIRS_GFX_FACE_ARRAY_UNLOAD:
        case TORIRS_GFX_NONE:
        default:
        {
            int k2 = (int)command.kind;
            if( d3d8_should_log_cmd(p, k2) )
            {
                d3d8_log("cmd kind=%d (no-op in D3D8 path)", k2);
                d3d8_mark_cmd_logged(p, k2);
            }
        }
        break;
        }
    }
    LibToriRS_FrameEnd(game);

    flush_font();
    p->current_font_id = -1;

    if( d3d8_trace_on(p) )
        d3d8_log(
            "Render: commands_done draws=%u tris=%u",
            p->debug_model_draws,
            p->debug_triangles);

    /* Nuklear overlay (same panel as GDI path). Skipped entirely when kSkipNuklearOverlayQuad. */
    if( !kSkipNuklearOverlayQuad && renderer->nk_rawfb )
    {
        struct rawfb_context* rawfb = (struct rawfb_context*)renderer->nk_rawfb;
        LARGE_INTEGER now_qpc;
        QueryPerformanceCounter(&now_qpc);
        double dt = 1.0 / 60.0;
        if( renderer->nk_prev_qpc_valid && renderer->nk_qpc_freq.QuadPart > 0 )
        {
            LONGLONG ticks = now_qpc.QuadPart - renderer->nk_prev_qpc.QuadPart;
            dt = (double)ticks / (double)renderer->nk_qpc_freq.QuadPart;
            if( dt < 1e-6 )
                dt = 1e-6;
        }
        renderer->nk_prev_qpc = now_qpc;
        renderer->nk_prev_qpc_valid = 1;

        if( renderer->nk_dt_smoothed <= 0.0 )
            renderer->nk_dt_smoothed = dt;
        else
            renderer->nk_dt_smoothed += 0.1 * (dt - renderer->nk_dt_smoothed);

        double dt_ms = renderer->nk_dt_smoothed * 1000.0;
        double fps = renderer->nk_dt_smoothed > 1e-9 ? 1.0 / renderer->nk_dt_smoothed : 0.0;

        struct nk_context* nk = torirs_rawfb_get_nk_context(rawfb);
        if( nk_begin(
                nk,
                "Info",
                nk_rect(10, 10, 280, 200),
                NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_MINIMIZABLE | NK_WINDOW_TITLE) )
        {
            nk_layout_row_dynamic(nk, 18, 1);
            nk_labelf(nk, NK_TEXT_LEFT, "%7.2f ms (%6.1f FPS)", dt_ms, fps);
            nk_labelf(nk, NK_TEXT_LEFT, "Buffer: %dx%d", renderer->width, renderer->height);
            nk_labelf(nk, NK_TEXT_LEFT, "Window: %dx%d", window_width, window_height);
            if( game->view_port )
            {
                nk_labelf(
                    nk,
                    NK_TEXT_LEFT,
                    "Viewport: %dx%d",
                    game->view_port->width,
                    game->view_port->height);
            }
            nk_labelf(
                nk,
                NK_TEXT_LEFT,
                "D3D8 GPU tris: %u draws:%u",
                p->debug_triangles,
                p->debug_model_draws);
            nk_labelf(nk, NK_TEXT_LEFT, "Paint cmd: %d", game->cc);
        }
        nk_end(nk);
        /* Clear nuklear's CPU buffer to fully-transparent every frame so only
         * UI pixels become opaque; the rest stays alpha=0 and lets the 3D
         * scene through when we alpha-blend this overlay on top. */
        nk_rawfb_render(rawfb, nk_rgba(0, 0, 0, 0), 1);

        if( p->nk_overlay_tex && !kSkipNuklearOverlayQuad )
        {
            D3DLOCKED_RECT lr;
            HRESULT hr_nk = p->nk_overlay_tex->LockRect(0, &lr, nullptr, D3DLOCK_DISCARD);
            if( hr_nk == D3D_OK )
            {
                for( int row = 0; row < renderer->height; ++row )
                    memcpy(
                        (unsigned char*)lr.pBits + row * lr.Pitch,
                        renderer->nk_pixel_buffer + row * renderer->width,
                        (size_t)renderer->width * sizeof(int));
                p->nk_overlay_tex->UnlockRect(0);
            }
            else if( FAILED(hr_nk) )
                d3d8_log(
                    "nk_overlay LockRect failed hr=0x%08lX",
                    (unsigned long)hr_nk);

            d3d8_ensure_pass(p, dev, PASS_2D);
            dev->SetTexture(0, p->nk_overlay_tex);
            /* Explicitly set every piece of state this overlay draw depends
             * on so that a stray global state change elsewhere can't make
             * the overlay opaquely cover the 3D scene. */
            dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
            dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
            dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
            dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
            dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
            dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
            float rw = (float)renderer->width;
            float rh = (float)renderer->height;
            DWORD amb = 0xFFFFFFFF;
            D3D8UiVertex q[6] = {
                { 0.0f, 0.0f, 0.0f, 1.0f, amb, 0.0f, 0.0f },
                { rw, 0.0f, 0.0f, 1.0f, amb, 1.0f, 0.0f },
                { rw, rh, 0.0f, 1.0f, amb, 1.0f, 1.0f },
                { 0.0f, 0.0f, 0.0f, 1.0f, amb, 0.0f, 0.0f },
                { rw, rh, 0.0f, 1.0f, amb, 1.0f, 1.0f },
                { 0.0f, rh, 0.0f, 1.0f, amb, 0.0f, 1.0f },
            };
            dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, q, sizeof(D3D8UiVertex));
        }
    }

    HRESULT hr_es1 = dev->EndScene();
    if( FAILED(hr_es1) )
        d3d8_log(
            "Render: EndScene(1) failed hr=0x%08lX",
            (unsigned long)hr_es1);

    /* Letterbox: scene_rt_tex -> back buffer */
    IDirect3DSurface8* bb = nullptr;
    HRESULT hr_gb = dev->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &bb);
    if( hr_gb == D3D_OK && bb )
    {
        HRESULT hr_sbb = dev->SetRenderTarget(bb, nullptr);
        if( FAILED(hr_sbb) )
            d3d8_log(
                "Render: SetRenderTarget(backbuffer) failed hr=0x%08lX",
                (unsigned long)hr_sbb);
        bb->Release();
    }
    else
    {
        d3d8_log(
            "Render: GetBackBuffer failed hr=0x%08lX bb=%p",
            (unsigned long)hr_gb,
            (void*)bb);
        if( bb )
            bb->Release();
    }
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1.0f, 0);

    RECT dst;
    compute_letterbox_dst(renderer->width, renderer->height, window_width, window_height, &dst);
    if( d3d8_trace_on(p) )
        d3d8_log(
            "Render: letterbox dst=%ld,%ld %ldx%ld",
            (long)dst.left,
            (long)dst.top,
            (long)(dst.right - dst.left),
            (long)(dst.bottom - dst.top));
    renderer->present_dst_x = dst.left;
    renderer->present_dst_y = dst.top;
    renderer->present_dst_w = (int)(dst.right - dst.left);
    renderer->present_dst_h = (int)(dst.bottom - dst.top);

    HRESULT hr_bs2 = dev->BeginScene();
    if( FAILED(hr_bs2) )
        d3d8_log(
            "Render: BeginScene(2 letterbox) failed hr=0x%08lX",
            (unsigned long)hr_bs2);
    d3d8_ensure_pass(p, dev, PASS_2D);
    dev->SetTexture(0, p->scene_rt_tex);
    dev->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);

    D3DVIEWPORT8 bbvp = {
        0,
        0,
        (DWORD)window_width,
        (DWORD)window_height,
        0.0f,
        1.0f
    };
    dev->SetViewport(&bbvp);

    float x0 = (float)dst.left;
    float y0 = (float)dst.top;
    float x1 = (float)dst.right;
    float y1 = (float)dst.bottom;
    DWORD wcol = 0xFFFFFFFF;
    D3D8UiVertex qb[6] = {
        { x0, y0, 0.0f, 1.0f, wcol, 0.0f, 0.0f },
        { x1, y0, 0.0f, 1.0f, wcol, 1.0f, 0.0f },
        { x1, y1, 0.0f, 1.0f, wcol, 1.0f, 1.0f },
        { x0, y0, 0.0f, 1.0f, wcol, 0.0f, 0.0f },
        { x1, y1, 0.0f, 1.0f, wcol, 1.0f, 1.0f },
        { x0, y1, 0.0f, 1.0f, wcol, 0.0f, 1.0f },
    };
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, qb, sizeof(D3D8UiVertex));
    dev->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
    dev->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    HRESULT hr_es2 = dev->EndScene();
    if( FAILED(hr_es2) )
        d3d8_log(
            "Render: EndScene(2) failed hr=0x%08lX",
            (unsigned long)hr_es2);

    HRESULT hr_pr = dev->Present(nullptr, nullptr, nullptr, nullptr);
    if( FAILED(hr_pr) )
        d3d8_log(
            "Render: Present failed hr=0x%08lX",
            (unsigned long)hr_pr);
    else if( d3d8_trace_on(p) )
        d3d8_log(
            "Render: Present ok hr=0x%08lX",
            (unsigned long)hr_pr);

    game->soft3d_mouse_from_window = true;
    game->soft3d_present_dst_x = renderer->present_dst_x;
    game->soft3d_present_dst_y = renderer->present_dst_y;
    game->soft3d_present_dst_w = renderer->present_dst_w;
    game->soft3d_present_dst_h = renderer->present_dst_h;
    game->soft3d_buffer_w = renderer->width;
    game->soft3d_buffer_h = renderer->height;

    p->frame_count++;
}

#endif /* _WIN32 && TORIRS_HAS_D3D8 */
