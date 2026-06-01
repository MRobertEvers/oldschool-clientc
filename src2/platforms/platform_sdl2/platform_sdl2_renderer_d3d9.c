#include "platform_sdl2_renderer_d3d9.h"

#include "graphics/uv_pnm.h"
#include "libtorirs.h"
#include "platformkit/core/trspk_atlas.h"
#include "platformkit/core/trspk_drawrangeex.h"
#include "platformkit/core/trspk_drawrangelist.h"
#include "platformkit/core/trspk_ibo.h"
#include "platformkit/core/trspk_modelarena.h"
#include "platformkit/core/trspk_pose.h"
#include "platformkit/core/trspk_triangles.h"
#include "platformkit/core/trspk_vbo.h"
#include "platformkit/d3d9/d3d9_vertex.h"
#include "platforms/trspk_toridraw.h"
#include "render/libtorirs_render.h"
#include "toridraw/toridraw_model.h"
#include "toridraw/toridraw_types.h"

#include <SDL.h>
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)

#include <SDL_syswm.h>

#include <d3d9.h>

/* Atlas layout constants */
#define TRSPK_D3D9_ATLAS_DIM 2048
#define TRSPK_D3D9_ATLAS_TILE 128
#define TRSPK_D3D9_ATLAS_COLS 16

/* Maximum draw ranges per frame (worst-case: every face alternates) */
#define TRSPK_D3D9_DRAWRANGE_CAP 4096

/* 16-bit index page size (models never straddle a page). */
#define TRSPK_D3D9_VBO_PAGE 65536u

struct LibToriPlatformSDL2_RendererD3D9
{
    SDL_Window* window;
    IDirect3D9* d3d;
    IDirect3DDevice9* device;

    IDirect3DVertexBuffer9* vbo_static;
    IDirect3DIndexBuffer9* ibo;
    IDirect3DVertexDeclaration9* vertex_decl;

    /* Standalone textures for animated tex-ids (not in atlas). */
    IDirect3DTexture9* tex_buffers[256];

    /* Atlas (static / non-animated textures only) */
    struct TRSPK_Atlas atlas;
    IDirect3DTexture9* atlas_tex;

    struct TRSPK_VBO* vbo_static_cpu;
    uint32_t gpu_ibo_capacity;

    struct TRSPK_ModelArena* model_arena;

    /* Per global triangle: atlas/static vs animated tex_id. */
    struct TRSPK_Triangles triangles;

    struct TRSPK_PoseTable poses;

    /* Per-frame working buffers */
    struct TRSPK_IBOChain* ibo_chain;
    struct TRSPK_DrawRangeList* draw_ranges;

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
d3d9_atlas_map_uv(
    int tex_id,
    float lu,
    float lv,
    float* out_u,
    float* out_v)
{
    const float dim = (float)TRSPK_D3D9_ATLAS_DIM;
    const float tile = (float)TRSPK_D3D9_ATLAS_TILE;
    const float du = tile / dim;

    int slot = d3d9_atlas_slot(tex_id);

    if( tex_id < 0 )
    {
        lu = 0.5f;
        lv = 0.5f;
    }

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

/* -----------------------------------------------------------------------
 * Matrix / clip-space helpers
 * ----------------------------------------------------------------------- */

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
d3d9_remap_projection_z(float* proj_colmajor)
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

/* Match trspk_texture_animation_signed / d3d8_fixed scroll encoding. */
static float
d3d9_texture_animation_signed(
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

static float
d3d9_anim_signed_for_tex(
    struct ToriDraw_Context* ctx,
    int tex_id)
{
    if( tex_id < 0 || !ctx )
        return 0.0f;

    struct ToriDraw_Texture* td = toridraw_texturemap_get(&ctx->texture_map, tex_id);
    if( !td )
        return 0.0f;

    return d3d9_texture_animation_signed(td->animation_direction, td->animation_speed);
}

static void
d3d9_disable_texture_transform(IDirect3DDevice9* dev);

/* Bind atlas texture and set MODULATE blend (texture * diffuse). */
static void
d3d9_bind_atlas_texture(
    IDirect3DDevice9* dev,
    IDirect3DTexture9* atlas_tex)
{
    d3d9_disable_texture_transform(dev);
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

/* MODULATE stages without binding a texture (caller sets texture). */
static void
d3d9_set_modulate_texture_stages(IDirect3DDevice9* dev)
{
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    IDirect3DDevice9_SetTextureStageState(dev, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
}

/* D3D9 tex0: u' = u*_11 + v*_21 + _31,  v' = u*_12 + v*_22 + _32  (COUNT2).
 * Translation lives in row 3 (_31/_32), not row 4 as in D3D8. */
static void
d3d9_apply_texture0_scroll_matrix(
    IDirect3DDevice9* dev,
    float anim_signed,
    float clk)
{
    D3DMATRIX m;
    memset(&m, 0, sizeof(m));
    m._11 = 1.0f;
    m._22 = 1.0f;
    m._33 = 1.0f;
    m._44 = 1.0f;

    if( anim_signed > 0.0f )
        m._31 = clk * anim_signed;
    else if( anim_signed < 0.0f )
        m._32 = -fmodf(clk * (-anim_signed), 1.0f);

    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
    IDirect3DDevice9_SetTransform(dev, D3DTS_TEXTURE0, &m);
}

static void
d3d9_bind_anim_texture(
    IDirect3DDevice9* dev,
    IDirect3DTexture9* tex,
    float anim_signed,
    float clk)
{
    IDirect3DDevice9_SetTexture(dev, 0, (IDirect3DBaseTexture9*)tex);
    d3d9_set_modulate_texture_stages(dev);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_TEXCOORDINDEX, 0);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    d3d9_apply_texture0_scroll_matrix(dev, anim_signed, clk);
}

static void
d3d9_disable_texture_transform(IDirect3DDevice9* dev)
{
    D3DMATRIX id;
    memset(&id, 0, sizeof(id));
    id._11 = id._22 = id._33 = id._44 = 1.0f;
    IDirect3DDevice9_SetTransform(dev, D3DTS_TEXTURE0, &id);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
}

/* Create or replace a 128x128 standalone texture from RGBA scratch. */
static bool
d3d9_upload_standalone_texture(
    struct LibToriPlatformSDL2_RendererD3D9* r,
    int tex_id,
    const uint8_t* rgba)
{
    assert(r->device && tex_id >= 0 && tex_id < 256);

    if( r->tex_buffers[tex_id] )
    {
        IDirect3DTexture9_Release(r->tex_buffers[tex_id]);
        r->tex_buffers[tex_id] = NULL;
    }

    IDirect3DTexture9* tex = NULL;
    HRESULT hr = IDirect3DDevice9_CreateTexture(
        r->device,
        TRSPK_D3D9_ATLAS_TILE,
        TRSPK_D3D9_ATLAS_TILE,
        1,
        0,
        D3DFMT_A8R8G8B8,
        D3DPOOL_MANAGED,
        &tex,
        NULL);
    if( FAILED(hr) )
        return false;

    D3DLOCKED_RECT lr;
    hr = IDirect3DTexture9_LockRect(tex, 0, &lr, NULL, 0);
    if( FAILED(hr) )
    {
        IDirect3DTexture9_Release(tex);
        return false;
    }

    for( uint32_t row = 0; row < TRSPK_D3D9_ATLAS_TILE; row++ )
    {
        const uint8_t* s = rgba + row * TRSPK_D3D9_ATLAS_TILE * 4u;
        uint8_t* d = (uint8_t*)lr.pBits + row * (uint32_t)lr.Pitch;
        for( uint32_t col = 0; col < TRSPK_D3D9_ATLAS_TILE; col++, s += 4, d += 4 )
        {
            d[0] = s[2];
            d[1] = s[1];
            d[2] = s[0];
            d[3] = s[3];
        }
    }

    IDirect3DTexture9_UnlockRect(tex, 0);
    r->tex_buffers[tex_id] = tex;
    return true;
}

/* -----------------------------------------------------------------------
 * Atlas management
 * ----------------------------------------------------------------------- */

static bool
d3d9_ensure_atlas(struct LibToriPlatformSDL2_RendererD3D9* r)
{
    if( trspk_atlas_is_initialized(&r->atlas) )
        return true;
    if( !trspk_atlas_init_grid(
            &r->atlas,
            TRSPK_D3D9_ATLAS_DIM,
            TRSPK_D3D9_ATLAS_DIM,
            TRSPK_D3D9_ATLAS_TILE,
            TRSPK_D3D9_ATLAS_TILE,
            4) )
        return false;

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

/* Decode ToriDraw_Texture texels into a 128×128 RGBA scratch buffer, stretching
 * the source to fill the full tile via nearest-neighbor resampling.  This handles
 * 64×64 (or other sub-tile) sources that would otherwise leave the tile partially
 * transparent and break UV math that assumes a fully-populated 128×128 slot.
 * Texels are packed int 0x00RRGGBB.  Alpha = 255 when opaque or texel != 0. */
static void
d3d9_decode_texture_rgba(
    const struct ToriDraw_Texture* tex,
    uint8_t* out_rgba)
{
    const uint32_t src_w = (uint32_t)tex->width;
    const uint32_t src_h = (uint32_t)tex->height;

    memset(out_rgba, 0, TRSPK_D3D9_ATLAS_TILE * TRSPK_D3D9_ATLAS_TILE * 4);
    if( src_w == 0u || src_h == 0u )
        return;

    for( uint32_t dst_row = 0; dst_row < TRSPK_D3D9_ATLAS_TILE; dst_row++ )
    {
        uint32_t src_row = (dst_row * src_h) / TRSPK_D3D9_ATLAS_TILE;
        for( uint32_t dst_col = 0; dst_col < TRSPK_D3D9_ATLAS_TILE; dst_col++ )
        {
            uint32_t src_col = (dst_col * src_w) / TRSPK_D3D9_ATLAS_TILE;
            int texel = tex->texels[src_row * src_w + src_col];
            uint8_t rv = (uint8_t)((texel >> 16) & 0xFF);
            uint8_t gv = (uint8_t)((texel >> 8) & 0xFF);
            uint8_t bv = (uint8_t)(texel & 0xFF);
            uint8_t av = (tex->opaque || texel != 0) ? 255u : 0u;
            uint32_t idx = (dst_row * TRSPK_D3D9_ATLAS_TILE + dst_col) * 4u;
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
    if( !trspk_atlas_is_initialized(&r->atlas) || !r->device )
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
    trspk_atlas_clear_dirty(&r->atlas);
}

/* -----------------------------------------------------------------------
 * Model registry helpers
 * ----------------------------------------------------------------------- */

static void
d3d9_release_gpu_mesh_buffers(struct LibToriPlatformSDL2_RendererD3D9* r)
{
    if( r->vbo_static )
    {
        IDirect3DVertexBuffer9_Release(r->vbo_static);
        r->vbo_static = NULL;
    }
    if( r->ibo )
    {
        IDirect3DIndexBuffer9_Release(r->ibo);
        r->ibo = NULL;
    }
    r->gpu_ibo_capacity = 0u;
}

static void
d3d9_clamp_local_uv(
    float* u,
    float* v)
{
    if( *u < 0.008f )
        *u = 0.008f;
    if( *u > 0.992f )
        *u = 0.992f;
    if( *v < 0.008f )
        *v = 0.008f;
    if( *v > 0.992f )
        *v = 0.992f;
}

static bool
d3d9_upload_vbo_if_dirty(struct LibToriPlatformSDL2_RendererD3D9* r)
{
    if( !trspk_vbo_is_dirty(r->vbo_static_cpu) || !r->vbo_static_cpu || !r->device )
        return true;

    const uint32_t vert_count = r->vbo_static_cpu->vertex_count;
    if( vert_count == 0u )
    {
        trspk_vbo_clear_dirty(r->vbo_static_cpu);
        return true;
    }

    if( r->vbo_static )
    {
        IDirect3DVertexBuffer9_Release(r->vbo_static);
        r->vbo_static = NULL;
    }

    const UINT sz = (UINT)(vert_count * sizeof(struct TRSPK_VertexD3D9));
    HRESULT hr = IDirect3DDevice9_CreateVertexBuffer(
        r->device, sz, D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &r->vbo_static, NULL);
    if( FAILED(hr) )
        return false;

    void* locked = NULL;
    if( SUCCEEDED(IDirect3DVertexBuffer9_Lock(r->vbo_static, 0, sz, &locked, 0)) )
    {
        memcpy(locked, r->vbo_static_cpu->vertices.as_d3d9, sz);
        IDirect3DVertexBuffer9_Unlock(r->vbo_static);
    }

    trspk_vbo_clear_dirty(r->vbo_static_cpu);
    return true;
}

static bool
d3d9_ensure_gpu_ibo(
    struct LibToriPlatformSDL2_RendererD3D9* r,
    uint32_t index_count)
{
    if( !r->device )
        return false;

    if( r->ibo && index_count <= r->gpu_ibo_capacity )
        return true;

    if( r->ibo )
    {
        IDirect3DIndexBuffer9_Release(r->ibo);
        r->ibo = NULL;
    }

    uint32_t cap = r->gpu_ibo_capacity ? r->gpu_ibo_capacity : 4096u;
    while( cap < index_count )
        cap *= 2u;

    HRESULT hr = IDirect3DDevice9_CreateIndexBuffer(
        r->device,
        (UINT)(cap * sizeof(uint16_t)),
        D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
        D3DFMT_INDEX16,
        D3DPOOL_DEFAULT,
        &r->ibo,
        NULL);
    if( FAILED(hr) )
        return false;

    r->gpu_ibo_capacity = cap;
    return true;
}

/* -----------------------------------------------------------------------
 * Render-command dispatch
 * ----------------------------------------------------------------------- */

static void
d3d9_ev_tex_load(
    struct LibToriPlatformSDL2_RendererD3D9* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    assert(command->kind == TORIRSRC_TEX_LOAD);
    int tex_id = command->u.tex_load.texture_id;
    struct ToriDraw_Texture* tex = command->u.tex_load.texture;
    assert(!(tex_id < 0 || tex_id >= 255 || !tex || !tex->texels));

    static uint8_t rgba_scratch[TRSPK_D3D9_ATLAS_TILE * TRSPK_D3D9_ATLAS_TILE * 4];
    d3d9_decode_texture_rgba(tex, rgba_scratch);

    if( tex->animation_direction != TORIDRAW_TEXANIM_DIRECTION_NONE )
    {
        if( !d3d9_upload_standalone_texture(renderer, tex_id, rgba_scratch) )
        {
            fprintf(stderr, "D3D9: Upload standalone texture failed\n");
            goto fail;
        }
    }
    else
    {
        //
        if( !d3d9_ensure_atlas(renderer) )
        {
            fprintf(stderr, "D3D9: Ensure atlas failed\n");
            goto fail;
        }

        trspk_atlas_grid_insert_at(
            &renderer->atlas,
            (uint32_t)(tex_id + 1),
            rgba_scratch,
            TRSPK_D3D9_ATLAS_TILE * 4u,
            TRSPK_D3D9_ATLAS_TILE,
            TRSPK_D3D9_ATLAS_TILE,
            NULL);
    }

fail:
    return;
}

static void
d3d9_ev_begin_3d(
    struct LibToriPlatformSDL2_RendererD3D9* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    assert(command->kind == TORIRSRC_BEGIN_3D);
    IDirect3DDevice9* dev = renderer->device;
    const struct LibToriRS_RenderCommand_Begin3D* b3d = &command->u.begin_3d;
    renderer->cur_3d = *b3d;
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
    if( trspk_atlas_is_dirty(&renderer->atlas) && trspk_atlas_is_initialized(&renderer->atlas) )
        d3d9_refresh_atlas_texture(renderer);

    if( renderer->atlas_tex )
        d3d9_bind_atlas_texture(dev, renderer->atlas_tex);
    else
        d3d9_set_no_texture_stages(dev);
}

static void
d3d9_invalidate_pose_from_slot(
    const struct TRSPK_ModelSlot* slot,
    void* user_data)
{
    struct TRSPK_PoseTable* poses = (struct TRSPK_PoseTable*)user_data;
    trspk_pose_table_set(poses, slot->element_id, TRSPK_POSE_VERTEX_BASE_INVALID);
}

static void
d3d9_ev_batch3d_clear(
    struct LibToriPlatformSDL2_RendererD3D9* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    (void)instance;
    (void)command;

    if( !renderer->model_arena )
        return;

    trspk_modelarena_visit_alive(
        renderer->model_arena, d3d9_invalidate_pose_from_slot, &renderer->poses);
    trspk_modelarena_clear(renderer->model_arena);
    d3d9_release_gpu_mesh_buffers(renderer);
}

static void
d3d9_ev_model_unload(
    struct LibToriPlatformSDL2_RendererD3D9* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    (void)instance;
    assert(command->kind == TORIRSRC_MODEL_UNLOAD);

    const int element_id = command->u.model_load.element_id;
    const uint32_t slot_index = trspk_modelarena_find(renderer->model_arena, element_id);
    if( slot_index == TRSPK_MODELSLOT_NULL_IDX )
        return;

    trspk_modelarena_unload(renderer->model_arena, slot_index);
    trspk_pose_table_set(&renderer->poses, element_id, TRSPK_POSE_VERTEX_BASE_INVALID);
}

static void
d3d9_ev_model_load(
    struct LibToriPlatformSDL2_RendererD3D9* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    assert(command->kind == TORIRSRC_MODEL_LOAD);
    struct ToriDraw_Model* model = get_model(command->u.model_load.model);
    if( !model || model->face_count <= 0 )
        return;

    struct ToriDraw_Context* ctx = get_context(instance);
    const int element_id = command->u.model_load.element_id;
    const uint32_t vert_count = (uint32_t)model->face_count * 3u;
    const uint32_t tri_count = (uint32_t)model->face_count;

    const uint32_t existing_slot = trspk_modelarena_find(renderer->model_arena, element_id);
    if( existing_slot != TRSPK_MODELSLOT_NULL_IDX )
        trspk_modelarena_unload(renderer->model_arena, existing_slot);

    const uint32_t slot_index =
        trspk_modelarena_load(renderer->model_arena, element_id, vert_count);
    const struct TRSPK_ModelSlot* model_slot =
        trspk_modelarena_get(renderer->model_arena, slot_index);
    assert(model_slot != NULL);

    const uint32_t base = model_slot->vertex_base;

    for( uint32_t face_index = 0; face_index < tri_count; face_index++ )
    {
        const uint32_t vi = base + face_index * 3u;

        const uint32_t face_a = (uint32_t)model->face_indices_a[face_index];
        const uint32_t face_b = (uint32_t)model->face_indices_b[face_index];
        const uint32_t face_c = (uint32_t)model->face_indices_c[face_index];

        const uint16_t color_a_hsl16 = model->face_colors_a[face_index];
        const uint16_t color_b_hsl16 = model->face_colors_b[face_index];
        const uint16_t color_c_hsl16 = model->face_colors_c[face_index];
        uint8_t alpha = model->face_alphas ? model->face_alphas[face_index] : 0xFFu;

        float color_a[4], color_b[4], color_c[4];
        if( color_c_hsl16 == TORIDRAWHSL16_HIDDEN )
        {
            alpha = 0u;
            hsl16_to_rgba(color_a_hsl16, alpha, color_a);
            color_b[0] = color_a[0];
            color_b[1] = color_a[1];
            color_b[2] = color_a[2];
            color_b[3] = color_a[3];
            color_c[0] = color_a[0];
            color_c[1] = color_a[1];
            color_c[2] = color_a[2];
            color_c[3] = color_a[3];
        }
        else if( color_c_hsl16 == TORIDRAWHSL16_FLAT )
        {
            hsl16_to_rgba(color_a_hsl16, alpha, color_a);
            color_b[0] = color_a[0];
            color_b[1] = color_a[1];
            color_b[2] = color_a[2];
            color_b[3] = color_a[3];
            color_c[0] = color_a[0];
            color_c[1] = color_a[1];
            color_c[2] = color_a[2];
            color_c[3] = color_a[3];
        }
        else
        {
            hsl16_to_rgba(color_a_hsl16, alpha, color_a);
            hsl16_to_rgba(color_b_hsl16, alpha, color_b);
            hsl16_to_rgba(color_c_hsl16, alpha, color_c);
        }

        const int tex_id = model->face_textures ? (int)model->face_textures[face_index] : -1;
        const bool is_anim = trspk_toridraw_texture_is_animated(ctx, tex_id);

        trspk_triangles_set(
            &renderer->triangles,
            (base / 3u) + face_index,
            trspk_triangles_make_config(tex_id, is_anim));

        struct UVFaceCoords uv;
        uv_pnm_face(&uv, model, face_index);

        float u_a, v_a, u_b, v_b, u_c, v_c;
        if( is_anim )
        {
            u_a = uv.u1;
            v_a = uv.v1;
            u_b = uv.u2;
            v_b = uv.v2;
            u_c = uv.u3;
            v_c = uv.v3;
            d3d9_clamp_local_uv(&u_a, &v_a);
            d3d9_clamp_local_uv(&u_b, &v_b);
            d3d9_clamp_local_uv(&u_c, &v_c);
        }
        else
        {
            d3d9_atlas_map_uv(tex_id, uv.u1, uv.v1, &u_a, &v_a);
            d3d9_atlas_map_uv(tex_id, uv.u2, uv.v2, &u_b, &v_b);
            d3d9_atlas_map_uv(tex_id, uv.u3, uv.v3, &u_c, &v_c);
        }

        trspk_vbo_write_vertex_d3d9(
            renderer->vbo_static_cpu,
            vi + 0u,
            (float)model->vertices_x[face_a],
            (float)model->vertices_y[face_a],
            (float)model->vertices_z[face_a],
            color_a,
            u_a,
            v_a,
            (float)tex_id);
        trspk_vbo_write_vertex_d3d9(
            renderer->vbo_static_cpu,
            vi + 1u,
            (float)model->vertices_x[face_b],
            (float)model->vertices_y[face_b],
            (float)model->vertices_z[face_b],
            color_b,
            u_b,
            v_b,
            (float)tex_id);
        trspk_vbo_write_vertex_d3d9(
            renderer->vbo_static_cpu,
            vi + 2u,
            (float)model->vertices_x[face_c],
            (float)model->vertices_y[face_c],
            (float)model->vertices_z[face_c],
            color_c,
            u_c,
            v_c,
            (float)tex_id);
    }

    trspk_pose_table_set(&renderer->poses, element_id, base);
}

static void
d3d9_ev_model_draw(
    struct LibToriPlatformSDL2_RendererD3D9* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    assert(command->kind == TORIRSRC_DRAW_MODEL);
    assert(renderer->has_3d && renderer->ibo_chain && "Invalid renderer or not in 3D mode");

    struct ToriDraw_Context* ctx = get_context(instance);
    assert(ctx && "Invalid context");

    uint32_t vertex_base = 0u;
    if( !trspk_pose_table_get(&renderer->poses, command->u.model.element_id, &vertex_base) )
    {
        assert(false && "Invalid element ID");
    }

    const uint32_t page_base = vertex_base & ~(TRSPK_D3D9_VBO_PAGE - 1u);
    // Compute the base of the index, relative to the page base.
    // When DrawIndexedPrimitives is called,
    // you submit Page Base + (index)
    const uint32_t local_base = vertex_base - page_base;

    const int face_count = toridraw_face_order_count(ctx);
    if( face_count <= 0 )
    {
        assert(false && "Invalid face count");
    }

    int* face_order = toridraw_face_order(ctx);
    for( int i = 0; i < face_count; i++ )
    {
        const uint32_t face = (uint32_t)face_order[i];
        const uint16_t b = (uint16_t)(local_base + face * 3u);
        const uint16_t idx[3] = { b, (uint16_t)(b + 1u), (uint16_t)(b + 2u) };
        trspk_ibochain_push16(renderer->ibo_chain, page_base, idx, 3u);
    }
}

static void
d3d9_ev_end_3d(
    struct LibToriPlatformSDL2_RendererD3D9* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    (void)command;
    (void)instance;

    IDirect3DDevice9* dev = renderer->device;
    assert(dev && renderer->has_3d && "Invalid device or not in 3D mode");

    if( trspk_atlas_is_dirty(&renderer->atlas) && trspk_atlas_is_initialized(&renderer->atlas) )
        d3d9_refresh_atlas_texture(renderer);

    if( !d3d9_upload_vbo_if_dirty(renderer) || !renderer->vbo_static )
        goto done;

    if( !renderer->ibo_chain || !renderer->ibo_chain->head )
        goto done;

    uint32_t total_indices = 0u;
    for( struct TRSPK_IBOChainNode* node = renderer->ibo_chain->head; node != NULL;
         node = node->next )
        total_indices += node->ibo.index_count;

    if( total_indices == 0u )
        goto done;

    if( !d3d9_ensure_gpu_ibo(renderer, total_indices) )
        goto done;

    void* locked = NULL;
    const UINT ib_bytes = (UINT)(total_indices * sizeof(uint16_t));
    if( FAILED(IDirect3DIndexBuffer9_Lock(renderer->ibo, 0, ib_bytes, &locked, D3DLOCK_DISCARD)) )
        goto done;

    uint16_t* dst = (uint16_t*)locked;

    uint32_t built = trspk_drawrangeex_build16(
        renderer->draw_ranges, &renderer->triangles, renderer->ibo_chain, dst);

    assert(built == total_indices);

    IDirect3DIndexBuffer9_Unlock(renderer->ibo);

    d3d9_set_base_render_states(dev);
    IDirect3DDevice9_SetVertexDeclaration(dev, renderer->vertex_decl);
    IDirect3DDevice9_SetStreamSource(
        dev, 0, renderer->vbo_static, 0, (UINT)sizeof(struct TRSPK_VertexD3D9));
    IDirect3DDevice9_SetIndices(dev, renderer->ibo);

    const float clk = (float)renderer->frame_clock;
    struct ToriDraw_Context* ctx = get_context(instance);
    uint32_t last_cfg = UINT32_MAX;

    const struct TRSPK_DrawRange* range = trspk_drawrangelist_head(renderer->draw_ranges);
    while( range )
    {
        const uint32_t index_count = range->end - range->start;
        const uint32_t prim_count = index_count / 3u;

        // 1. EARLY OUT FIRST
        // Do not pollute state or waste cycles if there is nothing to draw.
        if( prim_count == 0u )
        {
            range = trspk_drawrangelist_next(renderer->draw_ranges, range);
            continue;
        }

        const uint32_t cfg = range->config_idx;

        // 2. DECOUPLED STATE
        // Only rebind textures if the actual texture configuration changes.
        if( cfg != last_cfg )
        {
            if( cfg == 0u )
            {
                if( renderer->atlas_tex )
                    d3d9_bind_atlas_texture(dev, renderer->atlas_tex);
                else
                    d3d9_set_no_texture_stages(dev);
            }
            else
            {
                const int tex_id = (int)cfg - 1;
                IDirect3DTexture9* tex = renderer->tex_buffers[tex_id];
                if( tex )
                    d3d9_bind_anim_texture(dev, tex, d3d9_anim_signed_for_tex(ctx, tex_id), clk);
                else
                    d3d9_disable_texture_transform(dev);
            }
            last_cfg = cfg;
        }
        else if( cfg != 0u )
        {
            // 3. EFFICIENT SCROLL UPDATE
            // Configuration and page_base are decoupled, so this will now correctly
            // execute even if the page_base changes, saving a redundant texture re-bind.
            const int tex_id = (int)cfg - 1;
            if( renderer->tex_buffers[tex_id] )
                d3d9_apply_texture0_scroll_matrix(dev, d3d9_anim_signed_for_tex(ctx, tex_id), clk);
        }

        // 4. SUBMIT DRAW
        const UINT min_vertex = (UINT)range->min_vertex;
        const UINT num_verts = (UINT)(range->max_vertex - range->min_vertex + 1u);
        const uint32_t page_base = range->base_offset;

        IDirect3DDevice9_DrawIndexedPrimitive(
            dev,
            D3DPT_TRIANGLELIST,
            (INT)page_base,
            min_vertex,
            num_verts,
            range->start,
            (UINT)prim_count);

        range = trspk_drawrangelist_next(renderer->draw_ranges, range);
    }

    d3d9_disable_texture_transform(dev);

done:
    renderer->has_3d = false;
    renderer->in3d = false;
    if( renderer->ibo_chain )
        trspk_ibochain_reset(renderer->ibo_chain);
}

static void
d3d9_handle_render_command(
    struct LibToriPlatformSDL2_RendererD3D9* renderer,
    struct LibToriRS_Instance* instance,
    struct LibToriRS_RenderCommand* command)
{
    switch( command->kind )
    {
    case TORIRSRC_TEX_LOAD:
        d3d9_ev_tex_load(renderer, instance, command);
        break;

    case TORIRSRC_TEX_UNLOAD:
        assert(false && "TEX_UNLOAD not implemented");
        break;
    case TORIRSRC_BEGIN_3D:
        d3d9_ev_begin_3d(renderer, instance, command);
        break;

    case TORIRSRC_END_3D:
        d3d9_ev_end_3d(renderer, instance, command);
        break;

    case TORIRSRC_BEGIN_2D:
    case TORIRSRC_END_2D:
    case TORIRSRC_CLEAR_RECT:
        break;

    case TORIRSRC_MODEL_LOAD:
        d3d9_ev_model_load(renderer, instance, command);
        break;

    case TORIRSRC_MODEL_UNLOAD:
        d3d9_ev_model_unload(renderer, instance, command);
        break;

    case TORIRSRC_BATCH3D_CLEAR:
        d3d9_ev_batch3d_clear(renderer, instance, command);
        break;

    case TORIRSRC_DRAW_MODEL:
        d3d9_ev_model_draw(renderer, instance, command);
        break;

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
    renderer->vbo_static_cpu = trspk_vbo_create(0, TRSPK_VERTEX_FORMAT_D3D9);
    renderer->ibo_chain = trspk_ibochain_create(TRSPK_INDEX_FORMAT_U16);
    renderer->draw_ranges = trspk_drawrangelist_create(TRSPK_D3D9_DRAWRANGE_CAP);
    trspk_vbo_set_dirty(renderer->vbo_static_cpu);

    if( !renderer->vbo_static_cpu || !renderer->ibo_chain || !renderer->draw_ranges )
    {
        if( renderer->vbo_static_cpu )
            trspk_vbo_free(renderer->vbo_static_cpu);
        if( renderer->ibo_chain )
            trspk_ibochain_free(renderer->ibo_chain);
        if( renderer->draw_ranges )
            trspk_drawrangelist_free(renderer->draw_ranges);
        free(renderer);
        return NULL;
    }

    renderer->model_arena = trspk_modelarena_create(
        renderer->vbo_static_cpu, &renderer->triangles, TRSPK_D3D9_VBO_PAGE, 64u);
    if( !renderer->model_arena )
    {
        trspk_vbo_free(renderer->vbo_static_cpu);
        trspk_ibochain_free(renderer->ibo_chain);
        trspk_drawrangelist_free(renderer->draw_ranges);
        free(renderer);
        return NULL;
    }

    trspk_pose_table_init(&renderer->poses);

    return renderer;
}

void
LibToriPlatformSDL2_RendererD3D9_Free(struct LibToriPlatformSDL2_RendererD3D9* renderer)
{
    if( !renderer )
        return;

    d3d9_release_gpu_mesh_buffers(renderer);

    trspk_triangles_free(&renderer->triangles);
    trspk_pose_table_free(&renderer->poses);

    if( renderer->model_arena )
    {
        trspk_modelarena_free(renderer->model_arena);
        renderer->model_arena = NULL;
    }

    if( renderer->vbo_static_cpu )
    {
        trspk_vbo_free(renderer->vbo_static_cpu);
        renderer->vbo_static_cpu = NULL;
    }
    if( renderer->ibo_chain )
    {
        trspk_ibochain_free(renderer->ibo_chain);
        renderer->ibo_chain = NULL;
    }

    for( int i = 0; i < 256; i++ )
    {
        if( renderer->tex_buffers[i] )
        {
            IDirect3DTexture9_Release(renderer->tex_buffers[i]);
            renderer->tex_buffers[i] = NULL;
        }
    }

    if( renderer->atlas_tex )
    {
        IDirect3DTexture9_Release(renderer->atlas_tex);
        renderer->atlas_tex = NULL;
    }
    if( trspk_atlas_is_initialized(&renderer->atlas) )
        trspk_atlas_free(&renderer->atlas);
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

    /* Single-stream unified TRSPK_VertexD3D9 (stride 32). */
    static const D3DVERTEXELEMENT9 k_unified_decl[] = {
        { 0, 0,  D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    0 },
        { 0, 16, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
        D3DDECL_END()
    };

    hr = IDirect3DDevice9_CreateVertexDeclaration(
        renderer->device, k_unified_decl, &renderer->vertex_decl);
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
