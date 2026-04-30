/**
 * Legacy Win32 D3D8 helpers (from d3d8_old), adapted for D3D8FixedInternal.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d8.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "graphics/dash.h"
#include "graphics/dash_model_internal.h"
#include "graphics/shared_tables.h"
#include "graphics/uv_pnm.h"
}

#include "d3d8_fixed_internal.h"
#include "d3d8_fixed_state.h"

#include "osrs/game.h"
#include "../../../../platform_impl2_win32_renderer_d3d8.h"

D3D8FixedInternal*
trspk_d3d8_fixed_priv(TRSPK_D3D8FixedRenderer* r)
{
    return r ? static_cast<D3D8FixedInternal*>(r->opaque_internal) : nullptr;
}

void
trspk_d3d8_fixed_log(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("[d3d8-fixed] ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    fflush(stderr);
    va_end(ap);
}

static const DWORD kFvfWorld = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;
static const DWORD kFvfUi = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

/** Set true to skip the full-screen Nuklear overlay quad for A/B testing (3D vs overlay). */
static const bool kSkipNuklearOverlayQuad = false;

static void
d3d8_mul_row_vec_d3dmatrix(const float v[4], const D3DMATRIX* m, float out[4])
{
    out[0] = v[0] * m->_11 + v[1] * m->_21 + v[2] * m->_31 + v[3] * m->_41;
    out[1] = v[0] * m->_12 + v[1] * m->_22 + v[2] * m->_32 + v[3] * m->_42;
    out[2] = v[0] * m->_13 + v[1] * m->_23 + v[2] * m->_33 + v[3] * m->_43;
    out[3] = v[0] * m->_14 + v[1] * m->_24 + v[2] * m->_34 + v[3] * m->_44;
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
    trspk_d3d8_fixed_log("fill_model_face: f=%d %s", f, reason);
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
    D3D8FixedInternal* priv,
    IDirect3DDevice8* device,
    const struct DashModel* model,
    uint64_t model_key)
{
    if( !model || !priv || !device )
    {
        trspk_d3d8_fixed_log("build_model_gpu: abort null model/priv/device");
        return nullptr;
    }
    if( dashmodel_face_count(model) <= 0 )
    {
        trspk_d3d8_fixed_log("build_model_gpu: abort face_count<=0");
        return nullptr;
    }
    if( !dashmodel_face_colors_a_const(model) || !dashmodel_face_colors_b_const(model) ||
        !dashmodel_face_colors_c_const(model) )
    {
        trspk_d3d8_fixed_log("build_model_gpu: abort missing face color arrays");
        return nullptr;
    }
    if( !dashmodel_vertices_x_const(model) || !dashmodel_vertices_y_const(model) ||
        !dashmodel_vertices_z_const(model) )
    {
        trspk_d3d8_fixed_log("build_model_gpu: abort missing vertex xyz arrays");
        return nullptr;
    }
    if( !dashmodel_face_indices_a_const(model) || !dashmodel_face_indices_b_const(model) ||
        !dashmodel_face_indices_c_const(model) )
    {
        trspk_d3d8_fixed_log("build_model_gpu: abort missing face index arrays");
        return nullptr;
    }

    const int fc = dashmodel_face_count(model);
    const faceint_t* ftex = dashmodel_face_textures_const(model);
    if( s_build_model_gpu_enter_logs < 32u )
    {
        s_build_model_gpu_enter_logs++;
        trspk_d3d8_fixed_log(
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
        trspk_d3d8_fixed_log(
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
        trspk_d3d8_fixed_log(
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
        trspk_d3d8_fixed_log(
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
    D3D8FixedInternal* priv,
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

static constexpr int kD3d8GfxKindSlots = 32;

bool
d3d8_fixed_should_log_cmd(D3D8FixedInternal* p, int kind)
{
    if( !p )
        return false;
    if( kind < 0 || kind >= kD3d8GfxKindSlots )
        return false;
    return !p->trace_first_gfx[(size_t)kind];
}

void
d3d8_fixed_mark_cmd_logged(D3D8FixedInternal* p, int kind)
{
    if( !p )
        return;
    if( kind >= 0 && kind < kD3d8GfxKindSlots )
        p->trace_first_gfx[(size_t)kind] = true;
}

void
D3D8FixedInternal::flush_font()
{
    IDirect3DDevice8* dev = device;
    if( !dev )
        return;
    if( pending_font_verts.empty() || current_font_id < 0 )
    {
        pending_font_verts.clear();
        return;
    }
    auto it = font_atlas_by_id.find(current_font_id);
    if( it == font_atlas_by_id.end() || !it->second )
    {
        pending_font_verts.clear();
        return;
    }
    d3d8_fixed_ensure_pass(this, dev, PASS_2D);
    dev->SetTexture(0, it->second);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    dev->DrawPrimitiveUP(
        D3DPT_TRIANGLELIST,
        (UINT)(pending_font_verts.size() / 3),
        pending_font_verts.data(),
        sizeof(D3D8UiVertex));
    pending_font_verts.clear();
}

void
d3d8_fixed_ensure_pass(D3D8FixedInternal* priv, IDirect3DDevice8* dev, PassKind want)
{
    if( !priv || !dev )
        return;
    if( priv->current_pass == want )
        return;
    static const DWORD kFvfWorld = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;
    static const DWORD kFvfUi = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
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

#include "d3d8_fixed_events.cpp"

void
d3d8_fixed_destroy_internal(D3D8FixedInternal* p)
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
    {
        if( kv.second )
        {
            if( kv.second->vbo )
                kv.second->vbo->Release();
            delete kv.second;
        }
    }
    p->model_gpu_by_key.clear();

    if( p->current_batch )
    {
        if( p->current_batch->vbo )
            p->current_batch->vbo->Release();
        if( p->current_batch->ebo )
            p->current_batch->ebo->Release();
        delete p->current_batch;
        p->current_batch = nullptr;
    }
    for( auto& kv : p->batches_by_id )
    {
        if( kv.second )
        {
            if( kv.second->vbo )
                kv.second->vbo->Release();
            if( kv.second->ebo )
                kv.second->ebo->Release();
            delete kv.second;
        }
    }
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

extern "C" void
TRSPK_D3D8Fixed_FrameBegin(
    TRSPK_D3D8FixedRenderer* renderer,
    int view_w,
    int view_h,
    struct GGame* game)
{
    if( !renderer || !game )
        return;
    D3D8FixedInternal* p = trspk_d3d8_fixed_priv(renderer);
    if( !p || !p->device )
        return;
    IDirect3DDevice8* dev = p->device;

    int vp_w = view_w > 0 ? view_w : (game->view_port ? game->view_port->width : (int)renderer->width);
    int vp_h = view_h > 0 ? view_h : (game->view_port ? game->view_port->height : (int)renderer->height);

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

    p->frame_u_clock = (float)((DWORD)GetTickCount() / 20u);
    p->frame_vp_w = vp_w;
    p->frame_vp_h = vp_h;
    renderer->frame_u_clock = p->frame_u_clock;
    renderer->frame_vp_w = vp_w;
    renderer->frame_vp_h = vp_h;

    p->ib_ring_write_offset = 0;
    p->current_pass = PASS_NONE;
    p->debug_model_draws = 0;
    p->debug_triangles = 0;
    p->current_font_id = -1;
    p->pending_font_verts.clear();

    HRESULT hr_rt = dev->SetRenderTarget(p->scene_rt_surf, p->scene_ds);
    if( FAILED(hr_rt) )
        trspk_d3d8_fixed_log(
            "FrameBegin: SetRenderTarget(scene_rt) failed hr=0x%08lX",
            (unsigned long)hr_rt);
    p->frame_vp_2d = { 0, 0, (DWORD)renderer->width, (DWORD)renderer->height, 0.0f, 1.0f };
    DWORD vp3d_x = (DWORD)(renderer->dash_offset_x > 0 ? renderer->dash_offset_x : 0);
    DWORD vp3d_y = (DWORD)(renderer->dash_offset_y > 0 ? renderer->dash_offset_y : 0);
    DWORD vp3d_w = (DWORD)vp_w;
    DWORD vp3d_h = (DWORD)vp_h;
    if( (int)(vp3d_x + vp3d_w) > (int)renderer->width )
        vp3d_w = (DWORD)((int)renderer->width - (int)vp3d_x);
    if( (int)(vp3d_y + vp3d_h) > (int)renderer->height )
        vp3d_h = (DWORD)((int)renderer->height - (int)vp3d_y);
    if( (int)vp3d_w < 1 )
        vp3d_w = 1u;
    if( (int)vp3d_h < 1 )
        vp3d_h = 1u;
    p->frame_vp_3d = { vp3d_x, vp3d_y, vp3d_w, vp3d_h, 0.0f, 1.0f };

    HRESULT hr_vp = dev->SetViewport(&p->frame_vp_2d);
    if( FAILED(hr_vp) )
        trspk_d3d8_fixed_log(
            "FrameBegin: SetViewport(scene 2d) failed hr=0x%08lX",
            (unsigned long)hr_vp);

    d3d8_apply_default_render_state(dev);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);

    dev->SetViewport(&p->frame_vp_3d);
    dev->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
    dev->SetViewport(&p->frame_vp_2d);

    HRESULT hr_bs = dev->BeginScene();
    if( FAILED(hr_bs) )
        trspk_d3d8_fixed_log(
            "FrameBegin: BeginScene failed hr=0x%08lX",
            (unsigned long)hr_bs);

    dev->SetTransform(D3DTS_VIEW, &d3d_view);
    dev->SetTransform(D3DTS_PROJECTION, &d3d_proj);
    d3d8_disable_texture_transform(dev);
}

extern "C" void
TRSPK_D3D8Fixed_FlushFont(TRSPK_D3D8FixedRenderer* renderer)
{
    if( !renderer )
        return;
    D3D8FixedInternal* p = trspk_d3d8_fixed_priv(renderer);
    if( !p )
        return;
    p->flush_font();
}

extern "C" void
TRSPK_D3D8Fixed_FrameEnd(TRSPK_D3D8FixedRenderer* renderer)
{
    if( !renderer )
        return;
    D3D8FixedInternal* p = trspk_d3d8_fixed_priv(renderer);
    if( !p || !p->device )
        return;
    IDirect3DDevice8* dev = p->device;
    HRESULT hr_es1 = dev->EndScene();
    if( FAILED(hr_es1) )
        trspk_d3d8_fixed_log(
            "FrameEnd: EndScene failed hr=0x%08lX",
            (unsigned long)hr_es1);
    renderer->diag_frame_submitted_draws = p->debug_model_draws;
    renderer->diag_frame_pass_subdraws = p->debug_triangles;
}

extern "C" void
TRSPK_D3D8Fixed_Present(
    TRSPK_D3D8FixedRenderer* renderer,
    int window_width,
    int window_height,
    struct Platform2_Win32_Renderer_D3D8* platform)
{
    if( !renderer || !platform )
        return;
    D3D8FixedInternal* p = trspk_d3d8_fixed_priv(renderer);
    if( !p || !p->device )
        return;
    IDirect3DDevice8* dev = p->device;

    IDirect3DSurface8* bb = nullptr;
    HRESULT hr_gb = dev->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &bb);
    if( hr_gb == D3D_OK && bb )
    {
        HRESULT hr_sbb = dev->SetRenderTarget(bb, nullptr);
        if( FAILED(hr_sbb) )
            trspk_d3d8_fixed_log(
                "Present: SetRenderTarget(backbuffer) failed hr=0x%08lX",
                (unsigned long)hr_sbb);
        bb->Release();
    }
    else
    {
        if( bb )
            bb->Release();
    }
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1.0f, 0);

    RECT dst;
    compute_letterbox_dst(
        platform->width,
        platform->height,
        window_width,
        window_height,
        &dst);
    platform->present_dst_x = dst.left;
    platform->present_dst_y = dst.top;
    platform->present_dst_w = (int)(dst.right - dst.left);
    platform->present_dst_h = (int)(dst.bottom - dst.top);

    HRESULT hr_bs2 = dev->BeginScene();
    if( FAILED(hr_bs2) )
        trspk_d3d8_fixed_log(
            "Present: BeginScene(letterbox) failed hr=0x%08lX",
            (unsigned long)hr_bs2);
    d3d8_fixed_ensure_pass(p, dev, PASS_2D);
    dev->SetTexture(0, p->scene_rt_tex);
    dev->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);

    D3DVIEWPORT8 bbvp = {
        0,
        0,
        (DWORD)(window_width > 0 ? window_width : 1),
        (DWORD)(window_height > 0 ? window_height : 1),
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
        trspk_d3d8_fixed_log(
            "Present: EndScene failed hr=0x%08lX",
            (unsigned long)hr_es2);

    HRESULT hr_pr = dev->Present(nullptr, nullptr, nullptr, nullptr);
    if( FAILED(hr_pr) )
        trspk_d3d8_fixed_log("Present: Present failed hr=0x%08lX", (unsigned long)hr_pr);

    p->frame_count++;
}
