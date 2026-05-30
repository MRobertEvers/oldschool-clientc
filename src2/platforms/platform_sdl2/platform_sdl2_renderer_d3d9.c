#include "platform_sdl2_renderer_d3d9.h"

#include "graphics/uv_pnm.h"
#include "libtorirs.h"
#include "platformkit/core/trspk_atlas.h"
#include "platformkit/core/trspk_drawrangelist.h"
#include "platformkit/core/trspk_vbo.h"
#include "render/libtorirs_render.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_model.h"
#include "toridraw/toridraw_types.h"

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

/* Atlas layout constants */
#define TRSPK_D3D9_ATLAS_DIM      2048
#define TRSPK_D3D9_ATLAS_TILE     128
#define TRSPK_D3D9_ATLAS_COLS     16

/* Maximum draw ranges per frame (worst-case: every face alternates) */
#define TRSPK_D3D9_DRAWRANGE_CAP  4096

/* Per-face animated vertex info for per-frame UV recomputation */
struct D3D9_AnimVertInfo
{
    float base_u;
    float base_v;
    float scroll_u; /* per clock-unit scroll in atlas U */
    float scroll_v; /* per clock-unit scroll in atlas V */
    int   tex_id;   /* model texture id (used to compute atlas slot) */
};

struct LibToriPlatformSDL2_RendererD3D9
{
    SDL_Window*        window;
    IDirect3D9*        d3d;
    IDirect3DDevice9*  device;

    /* Split vertex buffers */
    IDirect3DVertexBuffer9*      vb_stream0;       /* Buffer A: XYZColor, all verts, MANAGED  */
    IDirect3DVertexBuffer9*      vb_static_uv;     /* Buffer B: static UV,   MANAGED          */
    IDirect3DVertexBuffer9*      vb_dynamic_uv;    /* Buffer C: animated UV, DEFAULT+DYNAMIC  */
    IDirect3DIndexBuffer9*       ib;
    IDirect3DVertexDeclaration9* vertex_decl;

    /* Atlas */
    struct TRSPK_Atlas   atlas;
    bool                 atlas_initialized;
    IDirect3DTexture9*   atlas_tex;
    bool                 atlas_dirty;

    /* CPU-side VBO (D3D9_SPLIT format) */
    struct TRSPK_VBO* vbo_cpu;

    /* Per-model split data */
    struct ToriDraw_Model* model;
    uint32_t  face_count_stored;
    uint32_t  gpu_vertex_count;
    uint32_t  split_vertex;       /* first animated vertex index in the arrays */
    uint32_t  anim_vertex_count;  /* total animated vertices */
    bool      static_dirty;

    /* Per-face arrays (indexed by model face index) */
    uint32_t* vertex_base_per_face; /* IBO-ready base index for face f */
    uint8_t*  face_animated;        /* 1 = animated, 0 = static         */

    /* Per animated vertex info for per-frame UV recomputation */
    float* anim_base_u;
    float* anim_base_v;
    float* anim_scroll_u;
    float* anim_scroll_v;
    int*   anim_tex_id;

    /* Per-frame working buffers */
    uint16_t* ibo_cpu;
    uint32_t  ibo_cpu_cap;
    struct TRSPK_DrawRangeList* draw_ranges;

    int    width;
    int    height;

    float  view[16];
    float  proj[16];
    struct LibToriRS_RenderCommand_Begin3D cur_3d;
    bool   has_3d;
    bool   in3d;
    double frame_clock;
};

/* -----------------------------------------------------------------------
 * Atlas slot helpers
 * ----------------------------------------------------------------------- */

/* Real textures go to slot tex_id+1; slot 0 is the opaque-white tile used
 * for non-textured faces (MODULATE with white passes diffuse colour through). */
static inline int
d3d9_atlas_slot(int tex_id)
{
    return tex_id >= 0 ? tex_id + 1 : 0;
}

/* Map a local [0,1] UV within a texture tile to an atlas UV coordinate.
 * For non-textured faces (tex_id < 0) the UV is fixed at the centre of the
 * white tile (slot 0).  U is clamped; V is wrapped (fract) then clamped
 * to match the GL shader reference in platform_sdl2_renderer_opengl3_old.c. */
static void
d3d9_atlas_map_uv(int tex_id, float lu, float lv, float* out_u, float* out_v)
{
    const float dim = (float)TRSPK_D3D9_ATLAS_DIM;
    const float tile = (float)TRSPK_D3D9_ATLAS_TILE;
    const float du = tile / dim;

    int slot = d3d9_atlas_slot(tex_id);

    float col = (float)(slot % TRSPK_D3D9_ATLAS_COLS);
    float row = (float)(slot / TRSPK_D3D9_ATLAS_COLS);

    if( lu < 0.008f )
        lu = 0.008f;
    if( lu > 0.992f )
        lu = 0.992f;
    if( lv < 0.008f )
        lv = 0.008f;
    if( lv > 0.992f )
        lv = 0.992f;

    *out_u = col * du + lu * du;
    *out_v = row * du + lv * du;
}

/* Map a local UV for an animated face into the atlas, matching the fixed-function
 * approach from trspk_d3d8_fvf_from_model_vertex:
 *
 *   shrink into tile: local = local * 0.984 + 0.008
 *   add per-face-constant offset:
 *       lu += off_u   (bounded scroll; same value for all 3 verts)
 *       lv -= off_v
 *   map into atlas tile: uv = tile_origin + local * du
 *
 * No per-vertex fract/clamp: that would make the three vertices of a face
 * diverge and produce streaks across the triangle.  off_u/off_v are already
 * reduced mod 1 by the caller so they never grow unbounded. */
static void
d3d9_atlas_map_uv_anim(
    int tex_id, float lu, float lv, float off_u, float off_v,
    float* out_u, float* out_v)
{
    const float dim = (float)TRSPK_D3D9_ATLAS_DIM;
    const float tile = (float)TRSPK_D3D9_ATLAS_TILE;
    const float du = tile / dim;

    int slot = d3d9_atlas_slot(tex_id);
    float col = (float)(slot % TRSPK_D3D9_ATLAS_COLS);
    float row = (float)(slot / TRSPK_D3D9_ATLAS_COLS);

    /* Shrink base UV into tile interior, then apply scroll offset. */
    lu = lu * 0.984f + 0.008f + off_u;
    lv = lv * 0.984f + 0.008f - off_v;

    *out_u = col * du + lu * du;
    *out_v = row * du + lv * du;
}

/* -----------------------------------------------------------------------
 * Matrix / clip-space helpers
 * ----------------------------------------------------------------------- */

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
 * Model helpers
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
    uint32_t face_a = (uint32_t)model->face_indices_a[face_index];
    uint32_t face_b = (uint32_t)model->face_indices_b[face_index];
    uint32_t face_c = (uint32_t)model->face_indices_c[face_index];

    uint32_t p_vertex = face_a;
    uint32_t m_vertex = face_b;
    uint32_t n_vertex = face_c;

    if( model->face_texture_coords && model->face_texture_coords[face_index] != -1 )
    {
        assert(model->textured_p_coordinate != NULL);
        assert(model->textured_m_coordinate != NULL);
        assert(model->textured_n_coordinate != NULL);

        uint32_t texture_face = (uint32_t)model->face_texture_coords[face_index];
        p_vertex = (uint32_t)model->textured_p_coordinate[texture_face];
        m_vertex = (uint32_t)model->textured_m_coordinate[texture_face];
        n_vertex = (uint32_t)model->textured_n_coordinate[texture_face];
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
 * D3D9 matrix helper
 * ----------------------------------------------------------------------- */

static void
float16_to_d3dmatrix(
    const float* m,
    D3DMATRIX* out)
{
    memcpy(out, m, 16 * sizeof(float));
}

/* -----------------------------------------------------------------------
 * Render state helpers
 * ----------------------------------------------------------------------- */

static void
d3d9_set_base_render_states(IDirect3DDevice9* dev)
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
    /* Discard fully transparent texels (equivalent to the GL shader's
     * "if (texColor.a < 0.5) discard" for cutout textures). */
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ALPHATESTENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ALPHAREF, 1);
}

static void
d3d9_set_no_texture_stages(IDirect3DDevice9* dev)
{
    IDirect3DDevice9_SetTexture(dev, 0, NULL);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    IDirect3DDevice9_SetTextureStageState(dev, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
}

/* Bind atlas texture and set MODULATE blend (texture * diffuse). */
static void
d3d9_bind_atlas_texture(IDirect3DDevice9* dev, IDirect3DTexture9* atlas_tex)
{
    IDirect3DDevice9_SetTexture(dev, 0, (IDirect3DBaseTexture9*)atlas_tex);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    /* MODULATE: out_alpha = texture_alpha * diffuse_alpha.
     * The white tile has alpha 255, so non-textured faces pass diffuse through.
     * Cutout texels (alpha 0 from decode) multiply to 0 and are rejected by the
     * alpha test set in d3d9_set_base_render_states. */
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    IDirect3DDevice9_SetTextureStageState(dev, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
}

/* -----------------------------------------------------------------------
 * Atlas management
 * ----------------------------------------------------------------------- */

static bool
d3d9_ensure_atlas(struct LibToriPlatformSDL2_RendererD3D9* r)
{
    if( r->atlas_initialized )
        return true;
    if( !trspk_atlas_init_grid(
            &r->atlas,
            TRSPK_D3D9_ATLAS_DIM,
            TRSPK_D3D9_ATLAS_DIM,
            TRSPK_D3D9_ATLAS_TILE,
            TRSPK_D3D9_ATLAS_TILE,
            4) )
        return false;

    r->atlas_initialized = true;

    /* Reserve slot 0: opaque white tile (non-textured faces sample here; MODULATE
     * white × diffuse = diffuse, so vertex colour passes through unchanged). */
    static uint8_t white_tile[TRSPK_D3D9_ATLAS_TILE * TRSPK_D3D9_ATLAS_TILE * 4];
    memset(white_tile, 0xFF, sizeof(white_tile));
    trspk_atlas_grid_insert_at(
        &r->atlas,
        0,
        white_tile,
        TRSPK_D3D9_ATLAS_TILE * 4,
        TRSPK_D3D9_ATLAS_TILE,
        TRSPK_D3D9_ATLAS_TILE,
        NULL);

    return true;
}

/* Decode ToriDraw_Texture texels into a 128×128 RGBA scratch buffer.
 * Texels are packed int 0x00RRGGBB.  Alpha = 255 when opaque or texel != 0. */
static void
d3d9_decode_texture_rgba(
    const struct ToriDraw_Texture* tex,
    uint8_t* out_rgba)
{
    uint32_t w = (uint32_t)(tex->width < TRSPK_D3D9_ATLAS_TILE ? tex->width : TRSPK_D3D9_ATLAS_TILE);
    uint32_t h = (uint32_t)(tex->height < TRSPK_D3D9_ATLAS_TILE ? tex->height : TRSPK_D3D9_ATLAS_TILE);

    memset(out_rgba, 0, TRSPK_D3D9_ATLAS_TILE * TRSPK_D3D9_ATLAS_TILE * 4);

    for( uint32_t row = 0; row < h; row++ )
    {
        for( uint32_t col = 0; col < w; col++ )
        {
            int texel = tex->texels[row * (uint32_t)tex->width + col];
            uint8_t rv = (uint8_t)((texel >> 16) & 0xFF);
            uint8_t gv = (uint8_t)((texel >> 8) & 0xFF);
            uint8_t bv = (uint8_t)(texel & 0xFF);
            uint8_t av = (tex->opaque || texel != 0) ? 255u : 0u;
            uint32_t idx = (row * TRSPK_D3D9_ATLAS_TILE + col) * 4u;
            out_rgba[idx + 0] = rv;
            out_rgba[idx + 1] = gv;
            out_rgba[idx + 2] = bv;
            out_rgba[idx + 3] = av;
        }
    }
}

/* (Re)create the D3D9 atlas texture from the CPU atlas pixels.
 * Converts RGBA → BGRA (D3DFMT_A8R8G8B8 byte order). */
static void
d3d9_refresh_atlas_texture(struct LibToriPlatformSDL2_RendererD3D9* r)
{
    if( !r->atlas_initialized || !r->device )
        return;

    if( r->atlas_tex )
    {
        IDirect3DTexture9_Release(r->atlas_tex);
        r->atlas_tex = NULL;
    }

    HRESULT hr = IDirect3DDevice9_CreateTexture(
        r->device,
        TRSPK_D3D9_ATLAS_DIM,
        TRSPK_D3D9_ATLAS_DIM,
        1,
        0,
        D3DFMT_A8R8G8B8,
        D3DPOOL_MANAGED,
        &r->atlas_tex,
        NULL);
    if( FAILED(hr) )
        return;

    D3DLOCKED_RECT lr;
    hr = IDirect3DTexture9_LockRect(r->atlas_tex, 0, &lr, NULL, 0);
    if( FAILED(hr) )
        return;

    const uint8_t* src = r->atlas.pixels;
    uint8_t* dst_base = (uint8_t*)lr.pBits;
    const uint32_t src_stride = r->atlas.stride; /* = ATLAS_DIM * 4 */

    for( uint32_t row = 0; row < TRSPK_D3D9_ATLAS_DIM; row++ )
    {
        const uint8_t* s = src + row * src_stride;
        uint8_t* d = dst_base + row * (uint32_t)lr.Pitch;
        for( uint32_t col = 0; col < TRSPK_D3D9_ATLAS_DIM; col++, s += 4, d += 4 )
        {
            /* RGBA → BGRA (D3DFMT_A8R8G8B8 memory layout) */
            d[0] = s[2]; /* B */
            d[1] = s[1]; /* G */
            d[2] = s[0]; /* R */
            d[3] = s[3]; /* A */
        }
    }

    IDirect3DTexture9_UnlockRect(r->atlas_tex, 0);
    r->atlas_dirty = false;
}

/* -----------------------------------------------------------------------
 * Split buffer management
 * ----------------------------------------------------------------------- */

static void
d3d9_free_model_gpu_resources(struct LibToriPlatformSDL2_RendererD3D9* r)
{
    if( r->vb_stream0 )
    {
        IDirect3DVertexBuffer9_Release(r->vb_stream0);
        r->vb_stream0 = NULL;
    }
    if( r->vb_static_uv )
    {
        IDirect3DVertexBuffer9_Release(r->vb_static_uv);
        r->vb_static_uv = NULL;
    }
    if( r->vb_dynamic_uv )
    {
        IDirect3DVertexBuffer9_Release(r->vb_dynamic_uv);
        r->vb_dynamic_uv = NULL;
    }
    if( r->ib )
    {
        IDirect3DIndexBuffer9_Release(r->ib);
        r->ib = NULL;
    }

    free(r->vertex_base_per_face);
    r->vertex_base_per_face = NULL;
    free(r->face_animated);
    r->face_animated = NULL;
    free(r->anim_base_u);
    r->anim_base_u = NULL;
    free(r->anim_base_v);
    r->anim_base_v = NULL;
    free(r->anim_scroll_u);
    r->anim_scroll_u = NULL;
    free(r->anim_scroll_v);
    r->anim_scroll_v = NULL;
    free(r->anim_tex_id);
    r->anim_tex_id = NULL;
    free(r->ibo_cpu);
    r->ibo_cpu = NULL;
    r->ibo_cpu_cap = 0;

    r->face_count_stored = 0;
    r->gpu_vertex_count = 0;
    r->split_vertex = 0;
    r->anim_vertex_count = 0;
}

/* Classify faces, partition vertices (static first, animated second),
 * fill CPU split VBO, upload GPU Buffers A/B/C and the index buffer.
 * Called lazily on the first DRAW_MODEL after a model load or atlas change. */
static bool
d3d9_build_split_buffers(
    struct LibToriPlatformSDL2_RendererD3D9* r,
    struct ToriDraw_Context* ctx)
{
    /* All local pointers declared up-front so the cleanup label is always safe. */
    uint8_t* face_anim = NULL;
    uint32_t* vbase = NULL;
    float* ab_u = NULL;
    float* ab_v = NULL;
    float* sc_u = NULL;
    float* sc_v = NULL;
    int* at_id = NULL;
    uint16_t* ibo = NULL;
    IDirect3DVertexBuffer9* vb_a = NULL;
    IDirect3DVertexBuffer9* vb_b = NULL;
    IDirect3DVertexBuffer9* vb_c = NULL;
    IDirect3DIndexBuffer9* ib_gpu = NULL;
    HRESULT hr;

    struct ToriDraw_Model* model = r->model;
    if( !model || !r->device )
        return false;

    d3d9_free_model_gpu_resources(r);

    const uint32_t face_count = (uint32_t)model->face_count;
    if( face_count == 0 )
    {
        r->static_dirty = false;
        return true;
    }

    /* ---- Classify faces: animated = has animated texture ---- */
    face_anim = (uint8_t*)calloc(face_count, 1);
    if( !face_anim )
        goto build_fail;

    {
        uint32_t anim_face_count = 0;
        uint32_t f;
        for( f = 0; f < face_count; f++ )
        {
            int tex_id = (model->face_textures) ? (int)model->face_textures[f] : -1;
            bool is_anim = false;
            if( tex_id >= 0 && ctx )
            {
                struct ToriDraw_Texture* tex = toridraw_texturemap_get(&ctx->texture_map, tex_id);
                if( tex && tex->animation_direction != TORIDRAW_TEXANIM_DIRECTION_NONE )
                    is_anim = true;
            }
            face_anim[f] = is_anim ? 1u : 0u;
            if( is_anim )
                anim_face_count++;
        }

        const uint32_t static_face_count = face_count - anim_face_count;
        const uint32_t split_vertex = static_face_count * 3u;
        const uint32_t anim_vertex_count = anim_face_count * 3u;
        const uint32_t gpu_vertex_count = face_count * 3u;

        /* ---- Assign per-face vertex bases ---- */
        vbase = (uint32_t*)malloc(face_count * sizeof(uint32_t));
        if( !vbase )
            goto build_fail;

        {
            uint32_t s_cur = 0;
            uint32_t a_cur = split_vertex;
            for( f = 0; f < face_count; f++ )
            {
                if( !face_anim[f] )
                {
                    vbase[f] = s_cur;
                    s_cur += 3u;
                }
                else
                {
                    vbase[f] = a_cur;
                    a_cur += 3u;
                }
            }
        }

        /* ---- Fill CPU split VBO ---- */
        trspk_vbo_ensure_capacity(r->vbo_cpu, gpu_vertex_count);

        for( f = 0; f < face_count; f++ )
        {
            uint32_t base = vbase[f];
            int tex_id = (model->face_textures) ? (int)model->face_textures[f] : -1;

            uint32_t fa = (uint32_t)model->face_indices_a[f];
            uint32_t fb = (uint32_t)model->face_indices_b[f];
            uint32_t fc = (uint32_t)model->face_indices_c[f];

            uint8_t alpha = model->face_alphas ? model->face_alphas[f] : 0xFFu;
            float ca[4], cb[4], cc[4];
            hsl16_to_rgba(model->face_colors_a[f], alpha, ca);
            hsl16_to_rgba(model->face_colors_b[f], alpha, cb);
            hsl16_to_rgba(model->face_colors_c[f], alpha, cc);

            trspk_vbo_write_vertex_d3d9_split_xyzcolor(
                r->vbo_cpu, base + 0u,
                (float)model->vertices_x[fa],
                (float)model->vertices_y[fa],
                (float)model->vertices_z[fa],
                ca);
            trspk_vbo_write_vertex_d3d9_split_xyzcolor(
                r->vbo_cpu, base + 1u,
                (float)model->vertices_x[fb],
                (float)model->vertices_y[fb],
                (float)model->vertices_z[fb],
                cb);
            trspk_vbo_write_vertex_d3d9_split_xyzcolor(
                r->vbo_cpu, base + 2u,
                (float)model->vertices_x[fc],
                (float)model->vertices_y[fc],
                (float)model->vertices_z[fc],
                cc);

            /* Static faces: atlas-map UV now and bake it into the CPU VBO.
             * Animated face UV slots are filled per-frame in DRAW_MODEL. */
            if( !face_anim[f] )
            {
                struct UVFaceCoords uv;
                float u0, v0, u1, v1, u2, v2;
                uv_pnm_face(&uv, model, f);
                d3d9_atlas_map_uv(tex_id, uv.u1, uv.v1, &u0, &v0);
                d3d9_atlas_map_uv(tex_id, uv.u2, uv.v2, &u1, &v1);
                d3d9_atlas_map_uv(tex_id, uv.u3, uv.v3, &u2, &v2);
                trspk_vbo_write_vertex_d3d9_split_uv(
                    r->vbo_cpu, base + 0u, u0, v0, (float)tex_id, 0.0f);
                trspk_vbo_write_vertex_d3d9_split_uv(
                    r->vbo_cpu, base + 1u, u1, v1, (float)tex_id, 0.0f);
                trspk_vbo_write_vertex_d3d9_split_uv(
                    r->vbo_cpu, base + 2u, u2, v2, (float)tex_id, 0.0f);
            }
        }

        /* ---- Populate per-animated-vertex info for per-frame UV update ---- */
        if( anim_vertex_count > 0u )
        {
            uint32_t ai = 0;
            ab_u = (float*)malloc(anim_vertex_count * sizeof(float));
            ab_v = (float*)malloc(anim_vertex_count * sizeof(float));
            sc_u = (float*)malloc(anim_vertex_count * sizeof(float));
            sc_v = (float*)malloc(anim_vertex_count * sizeof(float));
            at_id = (int*)malloc(anim_vertex_count * sizeof(int));
            if( !ab_u || !ab_v || !sc_u || !sc_v || !at_id )
                goto build_fail;

            for( f = 0; f < face_count; f++ )
            {
                struct UVFaceCoords uv;
                int tex_id;
                float su, sv;

                if( !face_anim[f] )
                    continue;

                tex_id = (int)model->face_textures[f];
                su = 0.0f;
                sv = 0.0f;
                if( ctx )
                {
                    struct ToriDraw_Texture* tex =
                        toridraw_texturemap_get(&ctx->texture_map, tex_id);
                    if( tex )
                    {
                        float speed = (float)tex->animation_speed / 128.0f;
                        int dir = tex->animation_direction;
                        if( dir == TORIDRAW_TEXANIM_DIRECTION_U_DOWN ||
                            dir == TORIDRAW_TEXANIM_DIRECTION_U_UP )
                            su = speed;
                        else
                            sv = speed;
                    }
                }

                uv_pnm_face(&uv, model, f);
                ab_u[ai + 0u] = uv.u1;
                ab_v[ai + 0u] = uv.v1;
                ab_u[ai + 1u] = uv.u2;
                ab_v[ai + 1u] = uv.v2;
                ab_u[ai + 2u] = uv.u3;
                ab_v[ai + 2u] = uv.v3;
                sc_u[ai + 0u] = sc_u[ai + 1u] = sc_u[ai + 2u] = su;
                sc_v[ai + 0u] = sc_v[ai + 1u] = sc_v[ai + 2u] = sv;
                at_id[ai + 0u] = at_id[ai + 1u] = at_id[ai + 2u] = tex_id;
                ai += 3u;
            }
        }

        /* ---- Allocate per-frame IBO CPU buffer ---- */
        ibo = (uint16_t*)malloc(gpu_vertex_count * sizeof(uint16_t));
        if( !ibo )
            goto build_fail;

        /* ---- Create GPU Buffer A: Stream0, XYZColor, MANAGED ---- */
        {
            UINT sz = (UINT)(gpu_vertex_count * sizeof(struct TRSPK_VertexD3D9_XYZColor));
            hr = IDirect3DDevice9_CreateVertexBuffer(
                r->device, sz, D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &vb_a, NULL);
            if( FAILED(hr) )
                goto build_fail;
            void* locked = NULL;
            if( SUCCEEDED(IDirect3DVertexBuffer9_Lock(vb_a, 0, sz, &locked, 0)) )
            {
                memcpy(locked, r->vbo_cpu->vertices.as_d3d9_split.xyz_color, sz);
                IDirect3DVertexBuffer9_Unlock(vb_a);
            }
        }

        /* ---- Create GPU Buffer B: Stream1, static UV, MANAGED ---- */
        if( split_vertex > 0u )
        {
            UINT sz = (UINT)(split_vertex * sizeof(struct TRSPK_VertexD3D9_UV));
            hr = IDirect3DDevice9_CreateVertexBuffer(
                r->device, sz, D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &vb_b, NULL);
            if( FAILED(hr) )
                goto build_fail;
            void* locked = NULL;
            if( SUCCEEDED(IDirect3DVertexBuffer9_Lock(vb_b, 0, sz, &locked, 0)) )
            {
                memcpy(locked, r->vbo_cpu->vertices.as_d3d9_split.uv, sz);
                IDirect3DVertexBuffer9_Unlock(vb_b);
            }
        }

        /* ---- Create GPU Buffer C: Stream1, dynamic animated UV, DEFAULT+DYNAMIC ---- */
        if( anim_vertex_count > 0u )
        {
            UINT sz = (UINT)(anim_vertex_count * sizeof(struct TRSPK_VertexD3D9_UV));
            hr = IDirect3DDevice9_CreateVertexBuffer(
                r->device, sz,
                D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
                0, D3DPOOL_DEFAULT, &vb_c, NULL);
            if( FAILED(hr) )
                goto build_fail;
        }

        /* ---- Create index buffer ---- */
        {
            UINT sz = (UINT)(gpu_vertex_count * sizeof(uint16_t));
            hr = IDirect3DDevice9_CreateIndexBuffer(
                r->device, sz,
                D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
                D3DFMT_INDEX16, D3DPOOL_DEFAULT,
                &ib_gpu, NULL);
            if( FAILED(hr) )
                goto build_fail;
        }

        /* ---- Commit all state ---- */
        r->vb_stream0 = vb_a;
        r->vb_static_uv = vb_b;
        r->vb_dynamic_uv = vb_c;
        r->ib = ib_gpu;
        r->vertex_base_per_face = vbase;
        r->face_animated = face_anim;
        r->anim_base_u = ab_u;
        r->anim_base_v = ab_v;
        r->anim_scroll_u = sc_u;
        r->anim_scroll_v = sc_v;
        r->anim_tex_id = at_id;
        r->ibo_cpu = ibo;
        r->ibo_cpu_cap = gpu_vertex_count;
        r->face_count_stored = face_count;
        r->gpu_vertex_count = gpu_vertex_count;
        r->split_vertex = split_vertex;
        r->anim_vertex_count = anim_vertex_count;
        r->static_dirty = false;
        return true;
    }

build_fail:
    if( ib_gpu )
        IDirect3DIndexBuffer9_Release(ib_gpu);
    if( vb_c )
        IDirect3DVertexBuffer9_Release(vb_c);
    if( vb_b )
        IDirect3DVertexBuffer9_Release(vb_b);
    if( vb_a )
        IDirect3DVertexBuffer9_Release(vb_a);
    free(ibo);
    free(at_id);
    free(sc_v);
    free(sc_u);
    free(ab_v);
    free(ab_u);
    free(vbase);
    free(face_anim);
    return false;
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
    case TORIRSRC_TEX_LOAD:
    {
        int tex_id = command->u.tex_load.texture_id;
        struct ToriDraw_Texture* tex = command->u.tex_load.texture;
        if( tex_id < 0 || tex_id >= 255 || !tex || !tex->texels )
            break;

        if( !d3d9_ensure_atlas(renderer) )
            break;

        static uint8_t rgba_scratch[TRSPK_D3D9_ATLAS_TILE * TRSPK_D3D9_ATLAS_TILE * 4];
        d3d9_decode_texture_rgba(tex, rgba_scratch);

        /* Real textures go into slot tex_id+1; slot 0 is reserved for the white tile. */
        trspk_atlas_grid_insert_at(
            &renderer->atlas,
            (uint32_t)(tex_id + 1),
            rgba_scratch,
            TRSPK_D3D9_ATLAS_TILE * 4u,
            TRSPK_D3D9_ATLAS_TILE,
            TRSPK_D3D9_ATLAS_TILE,
            NULL);

        renderer->atlas_dirty = true;
        renderer->static_dirty = true;
        break;
    }

    case TORIRSRC_TEX_UNLOAD:
    {
        int tex_id = command->u.tex_load.texture_id;
        if( tex_id < 0 || tex_id >= 255 || !renderer->atlas_initialized )
            break;

        /* Zero out the atlas tile for this texture (fill with transparent black). */
        trspk_atlas_grid_insert_at(
            &renderer->atlas,
            (uint32_t)(tex_id + 1),
            NULL,
            0,
            0,
            0,
            NULL);

        renderer->atlas_dirty = true;
        renderer->static_dirty = true;
        break;
    }

    case TORIRSRC_BEGIN_3D:
    {
        if( !renderer->in3d )
            d3d9_set_base_render_states(dev);

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

        D3DMATRIX d3d_view, d3d_proj;
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

        /* Refresh atlas texture if needed and bind it. */
        if( renderer->atlas_dirty && renderer->atlas_initialized )
            d3d9_refresh_atlas_texture(renderer);

        if( renderer->atlas_tex )
            d3d9_bind_atlas_texture(dev, renderer->atlas_tex);
        else
            d3d9_set_no_texture_stages(dev);

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

        /* Store the model pointer and defer GPU buffer builds until the first
         * DRAW_MODEL, by which time all TEX_LOAD events have been processed. */
        renderer->model = model;
        renderer->static_dirty = true;
        break;
    }

    case TORIRSRC_MODEL_UNLOAD:
        renderer->model = NULL;
        renderer->static_dirty = true;
        d3d9_free_model_gpu_resources(renderer);
        break;

    case TORIRSRC_DRAW_MODEL:
    {
        if( !renderer->has_3d || !renderer->model )
            break;

        struct ToriDraw_Context* ctx = get_context(instance);
        if( !ctx )
            break;

        /* Lazy split-buffer build after model load or atlas change. */
        if( renderer->static_dirty )
        {
            if( !d3d9_build_split_buffers(renderer, ctx) )
                break;
        }

        if( renderer->face_count_stored == 0 )
            break;

        uint32_t frame_face_count = (uint32_t)toridraw_face_order_count(ctx);
        if( frame_face_count == 0u )
            break;
        if( frame_face_count > renderer->face_count_stored )
            frame_face_count = renderer->face_count_stored;

        int* face_order = toridraw_face_order(ctx);

        /* ---- Update dynamic UV Buffer C ---- */
        if( renderer->anim_vertex_count > 0u && renderer->vb_dynamic_uv )
        {
            float clk = (float)renderer->frame_clock;
            /* Write into the animated region of the CPU VBO (starts at split_vertex). */
            struct TRSPK_VertexD3D9_UV* cpu_uv =
                &renderer->vbo_cpu->vertices.as_d3d9_split.uv[renderer->split_vertex];

            for( uint32_t ai = 0; ai < renderer->anim_vertex_count; ai++ )
            {
                /* Compute a bounded per-face-constant scroll offset in [0,1).
                 * Using fract keeps it from growing unbounded over time and
                 * ensures all three vertices of a face get the exact same delta
                 * (no per-vertex fract discontinuity → no streaks). */
                float raw_u = clk * renderer->anim_scroll_u[ai];
                float raw_v = clk * renderer->anim_scroll_v[ai];
                float off_u = raw_u - floorf(raw_u);
                float off_v = raw_v - floorf(raw_v);

                float out_u, out_v;
                d3d9_atlas_map_uv_anim(
                    renderer->anim_tex_id[ai],
                    renderer->anim_base_u[ai],
                    renderer->anim_base_v[ai],
                    off_u, off_v,
                    &out_u, &out_v);
                cpu_uv[ai].u = out_u;
                cpu_uv[ai].v = out_v;
                cpu_uv[ai].tex_id = (float)renderer->anim_tex_id[ai];
                cpu_uv[ai].uv_mode = 0.0f;
            }

            void* locked = NULL;
            UINT c_sz = (UINT)(renderer->anim_vertex_count * sizeof(struct TRSPK_VertexD3D9_UV));
            if( SUCCEEDED(IDirect3DVertexBuffer9_Lock(
                    renderer->vb_dynamic_uv, 0, c_sz, &locked, D3DLOCK_DISCARD)) )
            {
                memcpy(locked, cpu_uv, c_sz);
                IDirect3DVertexBuffer9_Unlock(renderer->vb_dynamic_uv);
            }
        }

        /* ---- Build per-frame IBO and draw-range list ---- */
        trspk_drawrangelist_clear(renderer->draw_ranges);

        uint32_t idx_cursor = 0;
        int cur_anim = -1;
        uint32_t range_start = 0;

        for( uint32_t i = 0; i < frame_face_count; i++ )
        {
            uint32_t face = (uint32_t)face_order[i];
            if( face >= renderer->face_count_stored )
                continue;

            uint32_t base = renderer->vertex_base_per_face[face];
            int is_anim = (int)renderer->face_animated[face];

            /* Animated face indices are rebased to [0, anim_vertex_count) so
             * they address Buffer C (and the animated region of Buffer A) from 0. */
            uint32_t idx_base = is_anim ? (base - renderer->split_vertex) : base;

            if( is_anim != cur_anim )
            {
                if( cur_anim >= 0 && idx_cursor > range_start )
                    trspk_drawrangelist_push(
                        renderer->draw_ranges,
                        range_start,
                        idx_cursor,
                        (uint32_t)cur_anim,
                        0);
                cur_anim = is_anim;
                range_start = idx_cursor;
            }

            if( idx_cursor + 3u > renderer->ibo_cpu_cap )
                break;

            renderer->ibo_cpu[idx_cursor++] = (uint16_t)(idx_base + 0u);
            renderer->ibo_cpu[idx_cursor++] = (uint16_t)(idx_base + 1u);
            renderer->ibo_cpu[idx_cursor++] = (uint16_t)(idx_base + 2u);
        }
        /* Close the final run. */
        if( cur_anim >= 0 && idx_cursor > range_start )
            trspk_drawrangelist_push(
                renderer->draw_ranges, range_start, idx_cursor, (uint32_t)cur_anim, 0);

        if( idx_cursor == 0u )
            break;

        /* Upload index buffer. */
        {
            void* locked = NULL;
            UINT ib_bytes = (UINT)(idx_cursor * sizeof(uint16_t));
            if( FAILED(IDirect3DIndexBuffer9_Lock(
                    renderer->ib, 0, ib_bytes, &locked, D3DLOCK_DISCARD)) )
                break;
            memcpy(locked, renderer->ibo_cpu, ib_bytes);
            IDirect3DIndexBuffer9_Unlock(renderer->ib);
        }

        /* ---- Execute draw ranges ---- */
        IDirect3DDevice9_SetVertexDeclaration(dev, renderer->vertex_decl);
        IDirect3DDevice9_SetIndices(dev, renderer->ib);

        UINT stride_xyz = (UINT)sizeof(struct TRSPK_VertexD3D9_XYZColor);
        UINT stride_uv = (UINT)sizeof(struct TRSPK_VertexD3D9_UV);

        int last_anim = -1;

        const struct TRSPK_DrawRange* range = trspk_drawrangelist_head(renderer->draw_ranges);
        while( range )
        {
            int buf = (int)range->buffer_idx; /* 0 = static, 1 = animated */

            if( buf != last_anim )
            {
                if( buf == 0 )
                {
                    /* Static region: Stream0 from start of A, Stream1 = B */
                    IDirect3DDevice9_SetStreamSource(dev, 0, renderer->vb_stream0, 0, stride_xyz);
                    if( renderer->vb_static_uv )
                        IDirect3DDevice9_SetStreamSource(
                            dev, 1, renderer->vb_static_uv, 0, stride_uv);
                }
                else
                {
                    /* Animated region: Stream0 offset into A by split_vertex,
                     * Stream1 = C (always starts at index 0 in the buffer). */
                    UINT off = (UINT)(renderer->split_vertex * stride_xyz);
                    IDirect3DDevice9_SetStreamSource(dev, 0, renderer->vb_stream0, off, stride_xyz);
                    if( renderer->vb_dynamic_uv )
                        IDirect3DDevice9_SetStreamSource(
                            dev, 1, renderer->vb_dynamic_uv, 0, stride_uv);
                }
                last_anim = buf;
            }

            uint32_t index_count = range->end - range->start;
            uint32_t prim_count = index_count / 3u;
            UINT num_verts =
                (buf == 0) ? renderer->split_vertex : renderer->anim_vertex_count;

            IDirect3DDevice9_DrawIndexedPrimitive(
                dev,
                D3DPT_TRIANGLELIST,
                0,            /* BaseVertexIndex */
                0,            /* MinIndex */
                num_verts,    /* NumVertices */
                range->start, /* StartIndex */
                (UINT)prim_count);

            range = trspk_drawrangelist_next(renderer->draw_ranges, range);
        }
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
        (struct LibToriPlatformSDL2_RendererD3D9*)calloc(
            1, sizeof(struct LibToriPlatformSDL2_RendererD3D9));
    if( !renderer )
        return NULL;

    renderer->width = width;
    renderer->height = height;
    renderer->vbo_cpu = trspk_vbo_create(0, TRSPK_VERTEX_FORMAT_D3D9_SPLIT);
    renderer->draw_ranges = trspk_drawrangelist_create(TRSPK_D3D9_DRAWRANGE_CAP);
    renderer->static_dirty = true;

    if( !renderer->vbo_cpu || !renderer->draw_ranges )
    {
        if( renderer->vbo_cpu )
            trspk_vbo_free(renderer->vbo_cpu);
        if( renderer->draw_ranges )
            trspk_drawrangelist_free(renderer->draw_ranges);
        free(renderer);
        return NULL;
    }

    return renderer;
}

void
LibToriPlatformSDL2_RendererD3D9_Free(struct LibToriPlatformSDL2_RendererD3D9* renderer)
{
    if( !renderer )
        return;

    d3d9_free_model_gpu_resources(renderer);

    if( renderer->atlas_tex )
    {
        IDirect3DTexture9_Release(renderer->atlas_tex);
        renderer->atlas_tex = NULL;
    }
    if( renderer->atlas_initialized )
    {
        trspk_atlas_free(&renderer->atlas);
        renderer->atlas_initialized = false;
    }
    if( renderer->vertex_decl )
    {
        IDirect3DVertexDeclaration9_Release(renderer->vertex_decl);
        renderer->vertex_decl = NULL;
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
    if( renderer->draw_ranges )
    {
        trspk_drawrangelist_free(renderer->draw_ranges);
        renderer->draw_ranges = NULL;
    }

    free(renderer);
}

bool
LibToriPlatformSDL2_RendererD3D9_Init(
    struct LibToriPlatformSDL2_RendererD3D9* renderer,
    SDL_Window* window)
{
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

    /* Two-stream vertex declaration:
     *   Stream 0: POSITION (float3) + DIFFUSE (D3DCOLOR)  — stride 16
     *   Stream 1: TEXCOORD0 (float2)                       — stride 16 (remaining bytes ignored) */
    static const D3DVERTEXELEMENT9 k_split_decl[] = {
        { 0, 0,  D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    0 },
        { 1, 0,  D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
        D3DDECL_END()
    };

    hr = IDirect3DDevice9_CreateVertexDeclaration(
        renderer->device, k_split_decl, &renderer->vertex_decl);
    if( FAILED(hr) )
    {
        fprintf(stderr, "D3D9: CreateVertexDeclaration failed (hr=0x%08lx)\n", (unsigned long)hr);
        goto fail;
    }

    d3d9_set_base_render_states(renderer->device);
    d3d9_set_no_texture_stages(renderer->device);
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
 * Non-Windows stubs
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
