/**
 * The D3D9 renderer core: device lifetime, the 2D/UI stack, textures and every
 * retained CPU/GPU buffer.
 *
 * What it deliberately does not know is how the world's triangles get ordered.
 * There are two implementations of that, they share nothing with each other,
 * and each lives in its own translation unit:
 *
 *   platform_win32_renderer_d3d9_painter.c   painter's algorithm
 *   platform_win32_renderer_d3d9_zbuffer.c   hardware depth test
 *
 * ToriRS_D3D9_Init picks one by creating (or not creating) the depth
 * implementation's state.  ::zbuffer is that state and doubles as the selector,
 * so the handful of places below that care read as a plain test and a direct
 * call.  See platform_win32_renderer_d3d9_core.h for the contract.
 */

#include "platform/platform_win32_renderer_d3d9_core.h"
#include <assert.h>

#include "core/trspk_drawrangeex.h"
#include "core/trspk_math.h"
#include "perf/torirs_perf.h"
#include "render/trspk_sprite.h"

#include "toridraw.h"
#include "toridraw_math.h"
#include "toridraw_model_sprite.h"
#include "toridraw_sprite.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void d3d9_set_no_texture(IDirect3DDevice9* device);
static void d3d9_disable_texture_transform(IDirect3DDevice9* device);
static void d3d9_bind_modulated_texture(IDirect3DDevice9* device, IDirect3DTexture9* texture);
static void d3d9_set_full_viewport(struct ToriRS_D3D9* renderer);
static bool d3d9_upload_atlas(struct ToriRS_D3D9* renderer);
static int d3d9_texture_slot(struct ToriRS_D3D9* renderer, int tex_id);
static int d3d9_ensure_static_texture(struct ToriRS_D3D9* renderer, int tex_id);
static bool d3d9_ensure_animated_texture(struct ToriRS_D3D9* renderer, int tex_id);
static void d3d9_map_atlas_uv(
    int slot, float local_u, float local_v, float* out_u, float* out_v);
static void d3d9_map_animated_uv(float* u, float* v);

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

static float
d3d9_ui_x(const struct ToriRS_D3D9* renderer, float logical_x)
{
    return (float)renderer->lb_x + logical_x * (float)renderer->lb_w /
            (float)renderer->width -
        0.5f;
}

static float
d3d9_ui_y(const struct ToriRS_D3D9* renderer, float logical_y)
{
    return (float)renderer->lb_y + logical_y * (float)renderer->lb_h /
            (float)renderer->height -
        0.5f;
}

static bool
d3d9_ui_scissor_rect(
    const struct ToriRS_D3D9* renderer,
    int x,
    int y,
    int width,
    int height,
    RECT* out)
{
    int x0;
    int y0;
    int x1;
    int y1;
    if( !renderer || !out || width <= 0 || height <= 0 || renderer->width <= 0 ||
        renderer->height <= 0 || renderer->lb_w <= 0 || renderer->lb_h <= 0 )
        return false;
    x0 = d3d9_clampi(x, 0, renderer->width);
    y0 = d3d9_clampi(y, 0, renderer->height);
    x1 = d3d9_clampi(x + width, 0, renderer->width);
    y1 = d3d9_clampi(y + height, 0, renderer->height);
    if( x1 <= x0 || y1 <= y0 )
        return false;
    out->left = renderer->lb_x + (LONG)((int64_t)x0 * renderer->lb_w / renderer->width);
    out->top = renderer->lb_y + (LONG)((int64_t)y0 * renderer->lb_h / renderer->height);
    out->right = renderer->lb_x +
        (LONG)(((int64_t)x1 * renderer->lb_w + renderer->width - 1) / renderer->width);
    out->bottom = renderer->lb_y +
        (LONG)(((int64_t)y1 * renderer->lb_h + renderer->height - 1) / renderer->height);
    out->left = d3d9_clampi((int)out->left, 0, renderer->client_w);
    out->top = d3d9_clampi((int)out->top, 0, renderer->client_h);
    out->right = d3d9_clampi((int)out->right, (int)out->left, renderer->client_w);
    out->bottom = d3d9_clampi((int)out->bottom, (int)out->top, renderer->client_h);
    return out->right > out->left && out->bottom > out->top;
}

static bool
d3d9_ui_intersect_scissor_rect(
    const struct ToriRS_D3D9* renderer,
    int scissor_x,
    int scissor_y,
    int scissor_w,
    int scissor_h,
    int box_x,
    int box_y,
    int box_w,
    int box_h,
    RECT* out)
{
    int x;
    int y;
    int w;
    int h;
    trspk_rect_intersect(
        scissor_x,
        scissor_y,
        scissor_w,
        scissor_h,
        box_x,
        box_y,
        box_w,
        box_h,
        &x,
        &y,
        &w,
        &h);
    return d3d9_ui_scissor_rect(renderer, x, y, w, h, out);
}

static bool
d3d9_ui_rect_equal(const RECT* a, const RECT* b)
{
    return a->left == b->left && a->top == b->top && a->right == b->right &&
        a->bottom == b->bottom;
}

static bool
d3d9_upload_ui_atlas(struct ToriRS_D3D9* renderer);

static void
d3d9_ui_set_states(struct ToriRS_D3D9* renderer)
{
    IDirect3DDevice9* device = renderer->device;
    IDirect3DDevice9_SetVertexShader(device, NULL);
    IDirect3DDevice9_SetPixelShader(device, NULL);
    IDirect3DDevice9_SetFVF(device, D3D9_OVERLAY_FVF);
    IDirect3DDevice9_SetRenderState(device, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ZENABLE, D3DZB_FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ZWRITEENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ALPHABLENDENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    IDirect3DDevice9_SetRenderState(device, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ALPHATESTENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ALPHAREF, 1u);
    IDirect3DDevice9_SetRenderState(device, D3DRS_SCISSORTESTENABLE, FALSE);
    IDirect3DDevice9_SetSamplerState(device, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(device, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(device, 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    IDirect3DDevice9_SetSamplerState(device, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    IDirect3DDevice9_SetSamplerState(device, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    IDirect3DDevice9_SetTexture(device, 1u, NULL);
    IDirect3DDevice9_SetTextureStageState(device, 1u, D3DTSS_COLOROP, D3DTOP_DISABLE);
    IDirect3DDevice9_SetTextureStageState(device, 1u, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    d3d9_disable_texture_transform(device);
}

static void
d3d9_ui_batch_reset(struct ToriRS_D3D9* renderer)
{
    renderer->ui_batch.vertex_count = 0u;
    renderer->ui_batch.texture = NULL;
    renderer->ui_batch.uses_sprite_atlas = false;
    renderer->ui_batch.scissor_enabled = false;
    memset(&renderer->ui_batch.scissor, 0, sizeof(renderer->ui_batch.scissor));
}

static void
d3d9_ui_flush(struct ToriRS_D3D9* renderer)
{
    struct D3D9UIBatch* batch;
    assert(renderer);
    if( !renderer->device )
        return;
    batch = &renderer->ui_batch;
    if( batch->vertex_count == 0u )
        return;
    if( batch->uses_sprite_atlas && trspk_atlas_is_dirty(&renderer->ui_sprite_atlas) &&
        !d3d9_upload_ui_atlas(renderer) )
    {
        batch->vertex_count = 0u;
        return;
    }
    if( batch->uses_sprite_atlas )
        batch->texture = renderer->ui_sprite_atlas_texture;
    if( batch->texture )
        d3d9_bind_modulated_texture(renderer->device, batch->texture);
    else
        d3d9_set_no_texture(renderer->device);
    IDirect3DDevice9_SetRenderState(
        renderer->device, D3DRS_SCISSORTESTENABLE, batch->scissor_enabled ? TRUE : FALSE);
    if( batch->scissor_enabled )
        IDirect3DDevice9_SetScissorRect(renderer->device, &batch->scissor);
    IDirect3DDevice9_DrawPrimitiveUP(
        renderer->device,
        D3DPT_TRIANGLELIST,
        batch->vertex_count / 3u,
        batch->vertices,
        (UINT)sizeof(batch->vertices[0]));
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_UI_BATCH_DRAWS, 1);
    batch->vertex_count = 0u;
}

static bool
d3d9_ui_prepare_batch(
    struct ToriRS_D3D9* renderer,
    IDirect3DTexture9* texture,
    bool uses_sprite_atlas,
    const RECT* scissor,
    uint32_t additional_vertices)
{
    struct D3D9UIBatch* batch = &renderer->ui_batch;
    bool state_changed = batch->vertex_count > 0u &&
        (batch->texture != texture || batch->uses_sprite_atlas != uses_sprite_atlas ||
            batch->scissor_enabled != (scissor != NULL) ||
            (scissor && !d3d9_ui_rect_equal(&batch->scissor, scissor)));
    if( state_changed || batch->vertex_count + additional_vertices > D3D9_UI_BATCH_MAX_VERTS )
        d3d9_ui_flush(renderer);
    if( additional_vertices > D3D9_UI_BATCH_MAX_VERTS || !batch->vertices )
        return false;
    batch->texture = texture;
    batch->uses_sprite_atlas = uses_sprite_atlas;
    batch->scissor_enabled = scissor != NULL;
    if( scissor )
        batch->scissor = *scissor;
    return true;
}

static void
d3d9_ui_append_quad_vertices(
    struct ToriRS_D3D9* renderer,
    IDirect3DTexture9* texture,
    bool uses_sprite_atlas,
    const RECT* scissor,
    const float positions[4][2],
    const float uv[4][2],
    D3DCOLOR color)
{
    static const uint8_t order[6] = { 0u, 1u, 2u, 0u, 2u, 3u };
    struct D3D9OverlayVertex* dst;
    uint32_t i;
    if( !d3d9_ui_prepare_batch(renderer, texture, uses_sprite_atlas, scissor, 6u) )
        return;
    dst = &renderer->ui_batch.vertices[renderer->ui_batch.vertex_count];
    for( i = 0u; i < 6u; i++ )
    {
        uint8_t corner = order[i];
        dst[i].x = d3d9_ui_x(renderer, positions[corner][0]);
        dst[i].y = d3d9_ui_y(renderer, positions[corner][1]);
        dst[i].z = 0.0f;
        dst[i].rhw = 1.0f;
        dst[i].color = color;
        dst[i].u = uv ? uv[corner][0] : 0.0f;
        dst[i].v = uv ? uv[corner][1] : 0.0f;
    }
    renderer->ui_batch.vertex_count += 6u;
}

static void
d3d9_ui_append_quad(
    struct ToriRS_D3D9* renderer,
    IDirect3DTexture9* texture,
    bool uses_sprite_atlas,
    const RECT* scissor,
    float x0,
    float y0,
    float x1,
    float y1,
    float u0,
    float v0,
    float u1,
    float v1,
    D3DCOLOR color)
{
    const float positions[4][2] = { { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 } };
    const float uv[4][2] = { { u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 } };
    d3d9_ui_append_quad_vertices(
        renderer, texture, uses_sprite_atlas, scissor, positions, uv, color);
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
    if( renderer->zbuffer )
        d3d9_zbuffer_apply_world_states(renderer);
    else
        d3d9_painter_apply_world_states(renderer);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
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

static void
d3d9_texture_animation_offsets(
    int direction,
    int speed,
    float* out_u,
    float* out_v)
{
    float amount = (float)speed / 128.0f;
    *out_u = 0.0f;
    *out_v = 0.0f;
    /* Match ToriDraw_TextureAnimate's source-coordinate shift: DOWN samples
     * from the negative direction and UP from the positive direction. */
    switch( direction )
    {
    case TORIDRAW_TEXANIM_DIRECTION_U_DOWN:
        *out_u = -amount;
        break;
    case TORIDRAW_TEXANIM_DIRECTION_U_UP:
        *out_u = amount;
        break;
    case TORIDRAW_TEXANIM_DIRECTION_V_DOWN:
        *out_v = -amount;
        break;
    case TORIDRAW_TEXANIM_DIRECTION_V_UP:
        *out_v = amount;
        break;
    default:
        break;
    }
}

static void
d3d9_apply_texture_scroll(
    IDirect3DDevice9* device,
    float amount_u,
    float amount_v,
    float clock_value)
{
    D3DMATRIX matrix;
    d3d9_identity(&matrix);
    matrix._31 = fmodf(clock_value * amount_u, 1.0f);
    matrix._32 = fmodf(clock_value * amount_v, 1.0f);
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
    float amount_u = 0.0f;
    float amount_v = 0.0f;
    if( tex_id >= 0 && tex_id < TORIDRAW_TEXTURE_ID_CAPACITY && renderer->scene )
        texture = ToriDraw_TextureMapGet(
            &ToriDraw_SceneTexState(renderer->scene)->texture_map, tex_id);
    if( texture )
        d3d9_texture_animation_offsets(
            texture->animation_direction,
            texture->animation_speed,
            &amount_u,
            &amount_v);

    d3d9_bind_modulated_texture(renderer->device, renderer->animated_textures[tex_id]);
    IDirect3DDevice9_SetSamplerState(
        renderer->device,
        0,
        D3DSAMP_ADDRESSU,
        amount_u != 0.0f ? D3DTADDRESS_WRAP : D3DTADDRESS_CLAMP);
    IDirect3DDevice9_SetSamplerState(
        renderer->device,
        0,
        D3DSAMP_ADDRESSV,
        amount_v != 0.0f ? D3DTADDRESS_WRAP : D3DTADDRESS_CLAMP);
    d3d9_apply_texture_scroll(
        renderer->device, amount_u, amount_v, (float)renderer->frame_clock);
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
    {
        IDirect3DDevice9_SetIndices(renderer->device, NULL);
        IDirect3DDevice9_SetStreamSource(renderer->device, 0u, NULL, 0u, 0u);
    }
    if( renderer->ibo )
    {
        IDirect3DIndexBuffer9_Release(renderer->ibo);
        renderer->ibo = NULL;
    }
    renderer->gpu_ibo_capacity = 0u;
    if( renderer->groups[TRSPK_VBO_GROUP_DYNAMIC].vbo_gpu )
    {
        IDirect3DVertexBuffer9_Release(
            renderer->groups[TRSPK_VBO_GROUP_DYNAMIC].vbo_gpu);
        renderer->groups[TRSPK_VBO_GROUP_DYNAMIC].vbo_gpu = NULL;
    }
    renderer->groups[TRSPK_VBO_GROUP_DYNAMIC].gpu_capacity = 0u;
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

    assert(renderer);
    if( !renderer->device || renderer->scene_active )
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
    assert(renderer);
    if( !renderer->device || !renderer->scene_active )
        return;
    hr = IDirect3DDevice9_EndScene(renderer->device);
    renderer->scene_active = false;
    if( FAILED(hr) )
        d3d9_log_hr("EndScene", hr);
}

static uint32_t
d3d9_ui_normalize_argb(uint32_t pixel)
{
    uint32_t rgb = pixel & 0x00ffffffu;
    uint32_t alpha = pixel >> 24;
    if( alpha == 0u && rgb != 0u )
        alpha = 255u;
    return rgb | (alpha << 24);
}

static void
d3d9_ui_rotmask_release_slot(struct D3D9UIRotmaskSlot* slot)
{
    if( !slot )
        return;
    if( slot->source_texture )
        IDirect3DTexture9_Release(slot->source_texture);
    if( slot->mask_texture )
        IDirect3DTexture9_Release(slot->mask_texture);
    memset(slot, 0, sizeof(*slot));
}

static void
d3d9_ui_rotmask_invalidate(struct ToriRS_D3D9* renderer, int scene_id)
{
    uint32_t i;
    if( scene_id <= 0 )
        return;
    assert(renderer);
    for( i = 0u; i < renderer->ui_rotmask_count; i++ )
    {
        struct D3D9UIRotmaskSlot* slot = &renderer->ui_rotmasks[i];
        if( slot->used &&
            (slot->scene_id == scene_id || slot->mask_scene_id == scene_id) )
            d3d9_ui_rotmask_release_slot(slot);
    }
}

static int
d3d9_ui_sprite_slot_index(struct ToriRS_D3D9* renderer, int scene_id, bool create)
{
    int free_index = -1;
    int i;
    for( i = 0; i < D3D9_UI_SPRITE_CAP; i++ )
    {
        if( renderer->ui_sprite_slots[i].scene_id == scene_id )
            return i;
        if( free_index < 0 && renderer->ui_sprite_slots[i].scene_id <= 0 )
            free_index = i;
    }
    if( !create || free_index < 0 )
        return -1;
    renderer->ui_sprite_slots[free_index].scene_id = scene_id;
    renderer->ui_sprite_slots[free_index].count = 0;
    free(renderer->ui_sprite_slots[free_index].uvs);
    free(renderer->ui_sprite_slots[free_index].loaded);
    renderer->ui_sprite_slots[free_index].uvs = NULL;
    renderer->ui_sprite_slots[free_index].loaded = NULL;
    return free_index;
}

static void
d3d9_ui_sprite_invalidate(struct ToriRS_D3D9* renderer, int scene_id)
{
    int slot_index;
    uint32_t i;
    slot_index = d3d9_ui_sprite_slot_index(renderer, scene_id, false);
    if( slot_index >= 0 )
    {
        struct D3D9UISpriteSlot* slot = &renderer->ui_sprite_slots[slot_index];
        free(slot->uvs);
        free(slot->loaded);
        memset(slot, 0, sizeof(*slot));
    }
    for( i = 0u; i < D3D9_UI_VARIANT_CAP; i++ )
        if( renderer->ui_variants[i].valid &&
            renderer->ui_variants[i].scene_id == scene_id )
            renderer->ui_variants[i].valid = false;
    d3d9_ui_rotmask_invalidate(renderer, scene_id);
}

static bool
d3d9_ui_upload_sprite_pixels(
    struct ToriRS_D3D9* renderer,
    const uint32_t* source,
    int width,
    int height,
    float out_uv[4])
{
    struct TRSPK_AtlasTile tile;
    uint32_t* argb;
    size_t count;
    size_t i;
    bool inserted;
    if( width <= 0 || height <= 0 )
        return false;
    assert(source);
    count = (size_t)width * (size_t)height;
    argb = (uint32_t*)malloc(count * sizeof(*argb));
    assert(argb);
    for( i = 0u; i < count; i++ )
        argb[i] = d3d9_ui_normalize_argb(source[i]);
    inserted = trspk_atlas_binpack_insert(
        &renderer->ui_sprite_atlas,
        (const uint8_t*)argb,
        (uint32_t)width * 4u,
        (uint32_t)width,
        (uint32_t)height,
        &tile);
    free(argb);
    if( !inserted )
        return false;
    out_uv[0] = tile.u_start;
    out_uv[1] = tile.v_start;
    out_uv[2] = tile.u_end;
    out_uv[3] = tile.v_end;
    return true;
}

static bool
d3d9_ui_sprite_ensure_base(
    struct ToriRS_D3D9* renderer,
    int scene_id,
    int atlas_index,
    struct ToriDraw_Sprite** out_sprite,
    float out_uv[4])
{
    struct ToriDraw_Sprite** sprites;
    struct ToriDraw_Sprite* sprite;
    struct D3D9UISpriteSlot* slot;
    int count = 0;
    int slot_index;
    if( !renderer->scene || scene_id <= 0 )
        return false;
    sprites = ToriDraw_SceneSpriteGet(renderer->scene, scene_id, &count);
    if( !sprites || atlas_index < 0 || atlas_index >= count )
        return false;
    sprite = sprites[atlas_index];
    if( !sprite || !sprite->pixels_argb || sprite->width <= 0 || sprite->height <= 0 )
        return false;
    slot_index = d3d9_ui_sprite_slot_index(renderer, scene_id, true);
    if( slot_index < 0 )
        return false;
    slot = &renderer->ui_sprite_slots[slot_index];
    if( slot->count != count )
    {
        float* uvs = (float*)calloc((size_t)count * 4u, sizeof(float));
        uint8_t* loaded = (uint8_t*)calloc((size_t)count, sizeof(uint8_t));
        assert(loaded);
        if( !uvs )
        {
            free(uvs);
            free(loaded);
            return false;
        }
        free(slot->uvs);
        free(slot->loaded);
        slot->uvs = uvs;
        slot->loaded = loaded;
        slot->count = count;
    }
    if( !slot->loaded[atlas_index] )
    {
        float uv[4];
        if( !d3d9_ui_upload_sprite_pixels(
                renderer, sprite->pixels_argb, sprite->width, sprite->height, uv) )
            return false;
        memcpy(&slot->uvs[atlas_index * 4], uv, sizeof(uv));
        slot->loaded[atlas_index] = 1u;
    }
    *out_sprite = sprite;
    memcpy(out_uv, &slot->uvs[atlas_index * 4], sizeof(float) * 4u);
    return true;
}

static struct D3D9UISpriteVariant*
d3d9_ui_sprite_variant(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand_Sprite* command,
    bool create)
{
    struct D3D9UISpriteVariant* free_variant = NULL;
    uint32_t i;
    for( i = 0u; i < D3D9_UI_VARIANT_CAP; i++ )
    {
        struct D3D9UISpriteVariant* variant = &renderer->ui_variants[i];
        if( !variant->valid )
        {
            if( !free_variant )
                free_variant = variant;
            continue;
        }
        if( variant->scene_id == command->scene_id &&
            variant->atlas_index == command->atlas_index &&
            variant->outline == command->outline &&
            variant->graphic_shadow == command->graphic_shadow &&
            variant->angle == command->sprite_angle_r2pi65536 &&
            variant->flip_h == command->flip_h && variant->flip_v == command->flip_v &&
            variant->if3_transform == (uint8_t)(command->if3 && !command->tiled) )
            return variant;
    }
    if( !create || !free_variant )
        return NULL;
    memset(free_variant, 0, sizeof(*free_variant));
    free_variant->scene_id = command->scene_id;
    free_variant->atlas_index = command->atlas_index;
    free_variant->outline = command->outline;
    free_variant->graphic_shadow = command->graphic_shadow;
    free_variant->angle = command->sprite_angle_r2pi65536;
    free_variant->flip_h = command->flip_h;
    free_variant->flip_v = command->flip_v;
    free_variant->if3_transform = (uint8_t)(command->if3 && !command->tiled);
    return free_variant;
}

static uint32_t*
d3d9_ui_clamp_sprite(
    const uint32_t* source,
    int source_width,
    int source_height,
    int offset_x,
    int offset_y,
    int width,
    int height)
{
    uint32_t* result;
    int x;
    int y;
    if( source_width <= 0 || source_height <= 0 || width <= 0 || height <= 0 )
        return NULL;
    assert(source);
    result = (uint32_t*)calloc((size_t)width * (size_t)height, sizeof(*result));
    assert(result);
    for( y = 0; y < source_height; y++ )
    {
        int dst_y = y + offset_y;
        if( dst_y < 0 || dst_y >= height )
            continue;
        for( x = 0; x < source_width; x++ )
        {
            int dst_x = x + offset_x;
            if( dst_x >= 0 && dst_x < width )
                result[dst_y * width + dst_x] = source[y * source_width + x];
        }
    }
    return result;
}

static bool
d3d9_ui_sprite_ensure_variant(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand_Sprite* command,
    struct ToriDraw_Sprite** out_sprite,
    float out_uv[4],
    int* out_offset_x,
    int* out_offset_y,
    int* out_width,
    int* out_height)
{
    struct ToriDraw_Sprite* sprite;
    struct D3D9UISpriteVariant* variant;
    float base_uv[4];
    uint32_t* pixels;
    int width;
    int height;
    int offset_x;
    int offset_y;
    int nominal_width;
    int nominal_height;
    if( !d3d9_ui_sprite_ensure_base(
            renderer, command->scene_id, command->atlas_index, &sprite, base_uv) )
        return false;
    if( command->outline <= 0 && command->graphic_shadow == 0 && !command->flip_h &&
        !command->flip_v && command->sprite_angle_r2pi65536 == 0 )
    {
        *out_sprite = sprite;
        memcpy(out_uv, base_uv, sizeof(base_uv));
        *out_offset_x = sprite->crop_x;
        *out_offset_y = sprite->crop_y;
        *out_width = sprite->width;
        *out_height = sprite->height;
        return true;
    }
    variant = d3d9_ui_sprite_variant(renderer, command, true);
    if( !variant )
        return false;
    if( variant->valid )
    {
        *out_sprite = sprite;
        out_uv[0] = variant->u0;
        out_uv[1] = variant->v0;
        out_uv[2] = variant->u1;
        out_uv[3] = variant->v1;
        *out_offset_x = variant->ox;
        *out_offset_y = variant->oy;
        *out_width = variant->width;
        *out_height = variant->height;
        return true;
    }
    width = sprite->width;
    height = sprite->height;
    offset_x = sprite->crop_x;
    offset_y = sprite->crop_y;
    nominal_width = sprite->width;
    nominal_height = sprite->height;
    pixels = (uint32_t*)malloc((size_t)width * (size_t)height * sizeof(*pixels));
    assert(pixels);
    memcpy(pixels, sprite->pixels_argb, (size_t)width * (size_t)height * sizeof(*pixels));
    if( command->outline > 0 )
    {
        int next_width = 0;
        int next_height = 0;
        uint32_t* next = ToriDraw_SpriteNewGraphicOutline(
            pixels, width, height, command->outline, &next_width, &next_height);
        if( next )
        {
            free(pixels);
            pixels = next;
            width = next_width;
            height = next_height;
        }
    }
    if( command->graphic_shadow != 0 )
    {
        int next_width = 0;
        int next_height = 0;
        uint32_t* next = ToriDraw_SpriteNewGraphicShadow(
            pixels,
            width,
            height,
            command->graphic_shadow,
            &next_width,
            &next_height);
        if( next )
        {
            free(pixels);
            pixels = next;
            width = next_width;
            height = next_height;
        }
    }
    if( command->if3 && !command->tiled )
    {
        ToriDraw_SpriteTransformPixels(
            &pixels, &width, &height, command->flip_h, command->flip_v, 0);
        if( offset_x != 0 || offset_y != 0 || width != nominal_width ||
            height != nominal_height )
        {
            uint32_t* next = d3d9_ui_clamp_sprite(
                pixels,
                width,
                height,
                offset_x,
                offset_y,
                nominal_width,
                nominal_height);
            if( next )
            {
                free(pixels);
                pixels = next;
                width = nominal_width;
                height = nominal_height;
                offset_x = 0;
                offset_y = 0;
            }
        }
        ToriDraw_SpriteTransformPixels(
            &pixels, &width, &height, 0, 0, command->sprite_angle_r2pi65536);
    }
    else
        ToriDraw_SpriteTransformPixels(
            &pixels,
            &width,
            &height,
            command->flip_h,
            command->flip_v,
            command->sprite_angle_r2pi65536);
    if( !d3d9_ui_upload_sprite_pixels(renderer, pixels, width, height, out_uv) )
    {
        free(pixels);
        return false;
    }
    free(pixels);
    variant->u0 = out_uv[0];
    variant->v0 = out_uv[1];
    variant->u1 = out_uv[2];
    variant->v1 = out_uv[3];
    variant->ox = offset_x;
    variant->oy = offset_y;
    variant->width = width;
    variant->height = height;
    variant->valid = true;
    *out_sprite = sprite;
    *out_offset_x = offset_x;
    *out_offset_y = offset_y;
    *out_width = width;
    *out_height = height;
    return true;
}

static struct D3D9UIRotmaskSlot*
d3d9_ui_rotmask_slot(
    struct ToriRS_D3D9* renderer,
    int scene_id,
    int atlas_index,
    int mask_scene_id,
    int mask_atlas_index,
    int width,
    int height,
    int source_width,
    int source_height)
{
    uint32_t free_index = UINT32_MAX;
    uint32_t i;
    for( i = 0u; i < renderer->ui_rotmask_count; i++ )
    {
        struct D3D9UIRotmaskSlot* slot = &renderer->ui_rotmasks[i];
        if( slot->used && slot->scene_id == scene_id &&
            slot->atlas_index == atlas_index && slot->mask_scene_id == mask_scene_id &&
            slot->mask_atlas_index == mask_atlas_index && slot->width == width &&
            slot->height == height && slot->source_width == source_width &&
            slot->source_height == source_height )
            return slot;
        if( free_index == UINT32_MAX && !slot->used )
            free_index = i;
    }
    if( free_index == UINT32_MAX )
    {
        if( renderer->ui_rotmask_count == renderer->ui_rotmask_capacity )
        {
            uint32_t old_capacity = renderer->ui_rotmask_capacity;
            uint32_t new_capacity = old_capacity ? old_capacity * 2u
                                                 : D3D9_UI_ROTMASK_INIT_CAP;
            struct D3D9UIRotmaskSlot* grown =
                (struct D3D9UIRotmaskSlot*)realloc(
                    renderer->ui_rotmasks, (size_t)new_capacity * sizeof(*grown));
            if( !grown )
                return NULL;
            memset(
                grown + old_capacity,
                0,
                (size_t)(new_capacity - old_capacity) * sizeof(*grown));
            renderer->ui_rotmasks = grown;
            renderer->ui_rotmask_capacity = new_capacity;
        }
        free_index = renderer->ui_rotmask_count++;
    }
    d3d9_ui_rotmask_release_slot(&renderer->ui_rotmasks[free_index]);
    renderer->ui_rotmasks[free_index].scene_id = scene_id;
    renderer->ui_rotmasks[free_index].atlas_index = atlas_index;
    renderer->ui_rotmasks[free_index].mask_scene_id = mask_scene_id;
    renderer->ui_rotmasks[free_index].mask_atlas_index = mask_atlas_index;
    renderer->ui_rotmasks[free_index].width = width;
    renderer->ui_rotmasks[free_index].height = height;
    renderer->ui_rotmasks[free_index].source_width = source_width;
    renderer->ui_rotmasks[free_index].source_height = source_height;
    renderer->ui_rotmasks[free_index].used = true;
    return &renderer->ui_rotmasks[free_index];
}

static bool
d3d9_ui_rotmask_create_texture(
    struct ToriRS_D3D9* renderer,
    int width,
    int height,
    IDirect3DTexture9** out_texture,
    UINT* out_texture_width,
    UINT* out_texture_height,
    D3DLOCKED_RECT* out_locked,
    const char* label)
{
    UINT texture_width = d3d9_next_pow2((UINT)width);
    UINT texture_height = d3d9_next_pow2((UINT)height);
    IDirect3DTexture9* texture = NULL;
    HRESULT hr;
    if( !renderer || !renderer->device || !out_texture || !out_texture_width ||
        !out_texture_height || !out_locked || width <= 0 || height <= 0 ||
        texture_width > renderer->caps.MaxTextureWidth ||
        texture_height > renderer->caps.MaxTextureHeight )
        return false;
    hr = IDirect3DDevice9_CreateTexture(
        renderer->device,
        texture_width,
        texture_height,
        1u,
        0u,
        D3DFMT_A8R8G8B8,
        D3DPOOL_MANAGED,
        &texture,
        NULL);
    if( FAILED(hr) )
    {
        d3d9_log_hr(label, hr);
        return false;
    }
    hr = IDirect3DTexture9_LockRect(texture, 0u, out_locked, NULL, 0u);
    if( FAILED(hr) )
    {
        d3d9_log_hr(label, hr);
        IDirect3DTexture9_Release(texture);
        return false;
    }
    *out_texture = texture;
    *out_texture_width = texture_width;
    *out_texture_height = texture_height;
    return true;
}

static void
d3d9_ui_record_rotmask_upload(
    struct ToriRS_D3D9* renderer, int width, int height)
{
    uint64_t bytes = (uint64_t)width * (uint64_t)height * 4u;
    renderer->ui_texture_upload_bytes += bytes;
    renderer->ui_texture_upload_count++;
    TORIRS_PERF_COUNT(
        TORIRS_PERF_CTR_D3D9_UI_TEXTURE_UPLOAD_BYTES, (int64_t)bytes);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_UI_TEXTURE_UPLOADS, 1);
}

static bool
d3d9_ui_rotmask_upload_source(
    struct ToriRS_D3D9* renderer,
    struct D3D9UIRotmaskSlot* slot,
    const struct ToriDraw_Sprite* sprite)
{
    IDirect3DTexture9* texture;
    UINT texture_width;
    UINT texture_height;
    D3DLOCKED_RECT locked;
    int sx;
    int sy;
    int y;
    assert(sprite);
    if( !sprite->pixels_argb )
        return false;
    assert(renderer);
    assert(slot);
    if( slot->source_texture )
    {
        /* Content refresh (e.g. minimap floor change, world map rebake): the
         * slot's cache key (scene/atlas ids, dims) is unchanged, but the
         * sprite's pixels_argb may have been rewritten in place since the
         * texture was last uploaded, so re-copy every frame rather than
         * trusting a one-time upload. */
        HRESULT hr = IDirect3DTexture9_LockRect(slot->source_texture, 0u, &locked, NULL, 0u);
        if( FAILED(hr) )
        {
            d3d9_log_hr("LockTexture(rotated sprite source refresh)", hr);
            return false;
        }
        texture = slot->source_texture;
        texture_width = slot->source_texture_width;
        texture_height = slot->source_texture_height;
    }
    else if( !d3d9_ui_rotmask_create_texture(
            renderer,
            slot->source_width,
            slot->source_height,
            &texture,
            &texture_width,
            &texture_height,
            &locked,
            "Create/LockTexture(rotated sprite source)") )
        return false;
    for( y = 0; y < (int)texture_height; y++ )
        memset((uint8_t*)locked.pBits + (size_t)y * locked.Pitch, 0,
            (size_t)texture_width * sizeof(uint32_t));
    for( sy = 0; sy < slot->source_height; sy++ )
    {
        int source_y = sprite->crop_y + sy;
        uint32_t* dst;
        if( source_y < 0 || source_y >= sprite->height )
            continue;
        dst = (uint32_t*)((uint8_t*)locked.pBits + (size_t)sy * locked.Pitch);
        for( sx = 0; sx < slot->source_width; sx++ )
        {
            int source_x = sprite->crop_x + sx;
            if( source_x >= 0 && source_x < sprite->width )
                dst[sx] = d3d9_ui_normalize_argb(
                    sprite->pixels_argb[(size_t)source_y * sprite->width + source_x]);
        }
    }
    IDirect3DTexture9_UnlockRect(texture, 0u);
    slot->source_texture = texture;
    slot->source_texture_width = texture_width;
    slot->source_texture_height = texture_height;
    d3d9_ui_record_rotmask_upload(
        renderer, (int)texture_width, (int)texture_height);
    return true;
}

static bool
d3d9_ui_rotmask_upload_mask(
    struct ToriRS_D3D9* renderer,
    struct D3D9UIRotmaskSlot* slot,
    const struct ToriDraw_Sprite* mask)
{
    IDirect3DTexture9* texture;
    UINT texture_width;
    UINT texture_height;
    D3DLOCKED_RECT locked;
    int x;
    int y;
    assert(mask);
    if( !mask->pixels_argb )
        return false;
    assert(renderer);
    assert(slot);
    if( slot->mask_texture )
    {
        HRESULT hr = IDirect3DTexture9_LockRect(slot->mask_texture, 0u, &locked, NULL, 0u);
        if( FAILED(hr) )
        {
            d3d9_log_hr("LockTexture(rotated sprite mask refresh)", hr);
            return false;
        }
        texture = slot->mask_texture;
        texture_width = slot->mask_texture_width;
        texture_height = slot->mask_texture_height;
    }
    else if( !d3d9_ui_rotmask_create_texture(
            renderer,
            slot->width,
            slot->height,
            &texture,
            &texture_width,
            &texture_height,
            &locked,
            "Create/LockTexture(rotated sprite mask)") )
        return false;
    for( y = 0; y < (int)texture_height; y++ )
        memset((uint8_t*)locked.pBits + (size_t)y * locked.Pitch, 0,
            (size_t)texture_width * sizeof(uint32_t));
    for( y = 0; y < mask->height; y++ )
    {
        int dst_y = mask->crop_y + y;
        uint32_t* dst;
        if( dst_y < 0 || dst_y >= slot->height )
            continue;
        dst = (uint32_t*)((uint8_t*)locked.pBits + (size_t)dst_y * locked.Pitch);
        for( x = 0; x < mask->width; x++ )
        {
            int dst_x = mask->crop_x + x;
            if( dst_x >= 0 && dst_x < slot->width &&
                mask->pixels_argb[(size_t)y * mask->width + x] != 0u )
                dst[dst_x] = 0xffffffffu;
        }
    }
    IDirect3DTexture9_UnlockRect(texture, 0u);
    slot->mask_texture = texture;
    slot->mask_texture_width = texture_width;
    slot->mask_texture_height = texture_height;
    d3d9_ui_record_rotmask_upload(
        renderer, (int)texture_width, (int)texture_height);
    return true;
}

static void d3d9_ui_rotated_sprite_quad(
    int dst_x,
    int dst_y,
    int dst_anchor_x,
    int dst_anchor_y,
    int src_anchor_x,
    int src_anchor_y,
    int src_width,
    int src_height,
    int rotation_r2pi2048,
    const float tile_uv[4],
    float positions[4][2],
    float uv[4][2]);

static void
d3d9_ui_draw_rotmask_native(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand_Sprite* command,
    const struct D3D9UIRotmaskSlot* slot,
    const RECT* scissor,
    D3DCOLOR color)
{
    static const uint8_t order[6] = { 0u, 1u, 2u, 0u, 2u, 3u };
    const float source_tile_uv[4] = {
        0.0f,
        0.0f,
        (float)slot->source_width / (float)slot->source_texture_width,
        (float)slot->source_height / (float)slot->source_texture_height,
    };
    float positions[4][2];
    float source_uv[4][2];
    struct D3D9RotmaskVertex corners[4];
    struct D3D9RotmaskVertex vertices[6];
    IDirect3DDevice9* device = renderer->device;
    DWORD mask_arg = D3DTA_TEXTURE;
    int i;
    if( !device || !slot->source_texture || !slot->mask_texture || !scissor )
        return;

    /* Draw only the forward-rotated source domain.  The destination-box
     * scissor supplied by the caller removes its overhang, so stage 0 never
     * needs atlas-unsafe clamp/border sampling outside the retained texture. */
    d3d9_ui_rotated_sprite_quad(
        command->x,
        command->y,
        command->dst_anchor_x,
        command->dst_anchor_y,
        command->src_anchor_x,
        command->src_anchor_y,
        slot->source_width,
        slot->source_height,
        command->rotation_r2pi2048,
        source_tile_uv,
        positions,
        source_uv);
    for( i = 0; i < 4; i++ )
    {
        corners[i].x = d3d9_ui_x(renderer, positions[i][0]);
        corners[i].y = d3d9_ui_y(renderer, positions[i][1]);
        corners[i].z = 0.0f;
        corners[i].rhw = 1.0f;
        corners[i].color = color;
        corners[i].source_u = source_uv[i][0];
        corners[i].source_v = source_uv[i][1];
        /* The stencil is axis aligned even though these vertices are not.
         * Equal RHW values make this an affine screen-space map; after the box
         * scissor, every surviving pixel addresses the matching mask pixel. */
        corners[i].mask_u =
            (positions[i][0] - (float)command->x) /
            (float)slot->mask_texture_width;
        corners[i].mask_v =
            (positions[i][1] - (float)command->y) /
            (float)slot->mask_texture_height;
    }
    for( i = 0; i < 6; i++ )
        vertices[i] = corners[order[i]];

    d3d9_ui_flush(renderer);
    d3d9_set_full_viewport(renderer);
    d3d9_ui_set_states(renderer);
    IDirect3DDevice9_SetFVF(device, D3D9_ROTMASK_FVF);
    IDirect3DDevice9_SetRenderState(device, D3DRS_SCISSORTESTENABLE, TRUE);
    IDirect3DDevice9_SetScissorRect(device, scissor);

    IDirect3DDevice9_SetTexture(
        device, 0u, (IDirect3DBaseTexture9*)slot->source_texture);
    IDirect3DDevice9_SetTextureStageState(device, 0u, D3DTSS_COLOROP, D3DTOP_MODULATE);
    IDirect3DDevice9_SetTextureStageState(device, 0u, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    IDirect3DDevice9_SetTextureStageState(device, 0u, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(device, 0u, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    IDirect3DDevice9_SetTextureStageState(device, 0u, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    IDirect3DDevice9_SetTextureStageState(device, 0u, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(device, 0u, D3DTSS_TEXCOORDINDEX, 0u);
    IDirect3DDevice9_SetSamplerState(device, 0u, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(device, 0u, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(device, 0u, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    IDirect3DDevice9_SetSamplerState(device, 0u, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    IDirect3DDevice9_SetSamplerState(device, 0u, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

    if( !command->mask_keep_opaque )
        mask_arg |= D3DTA_COMPLEMENT;
    IDirect3DDevice9_SetTexture(
        device, 1u, (IDirect3DBaseTexture9*)slot->mask_texture);
    IDirect3DDevice9_SetTextureStageState(device, 1u, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(device, 1u, D3DTSS_COLORARG1, D3DTA_CURRENT);
    IDirect3DDevice9_SetTextureStageState(device, 1u, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    IDirect3DDevice9_SetTextureStageState(device, 1u, D3DTSS_ALPHAARG1, D3DTA_CURRENT);
    IDirect3DDevice9_SetTextureStageState(device, 1u, D3DTSS_ALPHAARG2, mask_arg);
    IDirect3DDevice9_SetTextureStageState(device, 1u, D3DTSS_TEXCOORDINDEX, 1u);
    IDirect3DDevice9_SetTextureStageState(
        device, 1u, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    IDirect3DDevice9_SetSamplerState(device, 1u, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(device, 1u, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(device, 1u, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    IDirect3DDevice9_SetSamplerState(device, 1u, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    IDirect3DDevice9_SetSamplerState(device, 1u, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    IDirect3DDevice9_SetTextureStageState(device, 2u, D3DTSS_COLOROP, D3DTOP_DISABLE);
    IDirect3DDevice9_SetTextureStageState(device, 2u, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    IDirect3DDevice9_DrawPrimitiveUP(
        device, D3DPT_TRIANGLELIST, 2u, vertices, (UINT)sizeof(vertices[0]));
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_UI_ROTMASK_DRAWS, 1);

    /* Do not leave device-owned references to slots that an unload event can
     * release before the next draw.  The normal 2D batch re-establishes every
     * state it consumes when it next flushes. */
    IDirect3DDevice9_SetTexture(device, 1u, NULL);
    IDirect3DDevice9_SetTextureStageState(device, 1u, D3DTSS_COLOROP, D3DTOP_DISABLE);
    IDirect3DDevice9_SetTextureStageState(device, 1u, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    IDirect3DDevice9_SetTexture(device, 0u, NULL);
    IDirect3DDevice9_SetSamplerState(device, 0u, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    IDirect3DDevice9_SetSamplerState(device, 0u, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    d3d9_ui_set_states(renderer);
}

/* The software rotated blit inverse-maps an axis-aligned destination box and
 * skips pixels whose source coordinate falls outside the sprite.  Mapping the
 * whole box as one atlas-textured quad cannot express that skip: its corner
 * UVs leave the tile and point sampling then reads neighboring atlas entries.
 *
 * The inverse map's valid domain is the forward-rotated source rectangle.
 * Draw that rectangle and scissor it to the destination box instead.  The
 * half-pixel terms are the algebraic inverse of trspk_sprite_local_to_uv(), so
 * identity rotation still maps [dst_x,dst_x+w) exactly to [u0,u1). */
static void
d3d9_ui_rotated_sprite_quad(
    int dst_x,
    int dst_y,
    int dst_anchor_x,
    int dst_anchor_y,
    int src_anchor_x,
    int src_anchor_y,
    int src_width,
    int src_height,
    int rotation_r2pi2048,
    const float tile_uv[4],
    float positions[4][2],
    float uv[4][2])
{
    const float source_corners[4][2] = {
        { -0.5f, -0.5f },
        { (float)src_width - 0.5f, -0.5f },
        { (float)src_width - 0.5f, (float)src_height - 0.5f },
        { -0.5f, (float)src_height - 0.5f },
    };
    const int angle = ToriDraw_NormalizeAngle(rotation_r2pi2048);
    const float cosine = (float)ToriDraw_Cos(angle) / 65536.0f;
    const float sine = (float)ToriDraw_Sin(angle) / 65536.0f;
    int i;
    for( i = 0; i < 4; i++ )
    {
        float rel_x = source_corners[i][0] - (float)src_anchor_x;
        float rel_y = source_corners[i][1] - (float)src_anchor_y;
        positions[i][0] = (float)dst_x + 0.5f + (float)dst_anchor_x +
            rel_x * cosine - rel_y * sine;
        positions[i][1] = (float)dst_y + 0.5f + (float)dst_anchor_y +
            rel_x * sine + rel_y * cosine;
    }
    uv[0][0] = tile_uv[0];
    uv[0][1] = tile_uv[1];
    uv[1][0] = tile_uv[2];
    uv[1][1] = tile_uv[1];
    uv[2][0] = tile_uv[2];
    uv[2][1] = tile_uv[3];
    uv[3][0] = tile_uv[0];
    uv[3][1] = tile_uv[3];
}

static void
d3d9_ui_draw_sprite(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand_Sprite* command)
{
    struct ToriDraw_Sprite* sprite = NULL;
    RECT scissor;
    const RECT* scissor_ptr;
    float uv[4];
    int width;
    int height;
    int offset_x;
    int offset_y;
    int alpha;
    D3DCOLOR color;
    if( !renderer->in2d || !renderer->scene || command->scene_id <= 0 )
        return;
    if( !d3d9_ui_scissor_rect(
            renderer,
            command->scissor_x,
            command->scissor_y,
            command->scissor_w,
            command->scissor_h,
            &scissor) )
        return;
    scissor_ptr = &scissor;
    alpha = d3d9_clampi(255 - command->trans, 0, 255);
    color = D3DCOLOR_ARGB(alpha, 255, 255, 255);
    if( command->rotated && command->mask_scene_id > 0 )
    {
        struct ToriDraw_Sprite** sprites;
        struct ToriDraw_Sprite** masks;
        struct ToriDraw_Sprite* mask;
        struct D3D9UIRotmaskSlot* slot;
        RECT box_scissor;
        int sprite_count = 0;
        int mask_count = 0;
        int dst_width;
        int dst_height;
        int source_width;
        int source_height;
        sprites = ToriDraw_SceneSpriteGet(renderer->scene, command->scene_id, &sprite_count);
        masks = ToriDraw_SceneSpriteGet(renderer->scene, command->mask_scene_id, &mask_count);
        if( !sprites || !masks || command->atlas_index < 0 ||
            command->atlas_index >= sprite_count || command->mask_atlas_index < 0 ||
            command->mask_atlas_index >= mask_count )
            return;
        sprite = sprites[command->atlas_index];
        mask = masks[command->mask_atlas_index];
        if( !sprite || !mask || !sprite->pixels_argb || !mask->pixels_argb )
            return;
        dst_width = command->w > 0 ? command->w : sprite->width;
        dst_height = command->h > 0 ? command->h : sprite->height;
        source_width = sprite->crop_width > 0 ? sprite->crop_width : sprite->width;
        source_height = sprite->crop_height > 0 ? sprite->crop_height : sprite->height;
        if( dst_width <= 0 || dst_height <= 0 || source_width <= 0 ||
            source_height <= 0 ||
            !d3d9_ui_intersect_scissor_rect(
                renderer,
                command->scissor_x,
                command->scissor_y,
                command->scissor_w,
                command->scissor_h,
                command->x,
                command->y,
                dst_width,
                dst_height,
                &box_scissor) )
            return;
        slot = d3d9_ui_rotmask_slot(
            renderer,
            command->scene_id,
            command->atlas_index,
            command->mask_scene_id,
            command->mask_atlas_index,
            dst_width,
            dst_height,
            source_width,
            source_height);
        if( slot && d3d9_ui_rotmask_upload_source(renderer, slot, sprite) &&
            d3d9_ui_rotmask_upload_mask(renderer, slot, mask) )
            d3d9_ui_draw_rotmask_native(
                renderer, command, slot, &box_scissor, color);
        return;
    }
    if( !d3d9_ui_sprite_ensure_variant(
            renderer,
            command,
            &sprite,
            uv,
            &offset_x,
            &offset_y,
            &width,
            &height) )
        return;
    if( command->rotated )
    {
        RECT box_scissor;
        float positions[4][2];
        float rotated_uv[4][2];
        int dst_width = command->w > 0 ? command->w : width;
        int dst_height = command->h > 0 ? command->h : height;
        int src_width = sprite->crop_width > 0 ? sprite->crop_width : sprite->width;
        int src_height = sprite->crop_height > 0 ? sprite->crop_height : sprite->height;
        if( dst_width <= 0 || dst_height <= 0 || src_width <= 0 || src_height <= 0 ||
            !d3d9_ui_intersect_scissor_rect(
                renderer,
                command->scissor_x,
                command->scissor_y,
                command->scissor_w,
                command->scissor_h,
                command->x,
                command->y,
                dst_width,
                dst_height,
                &box_scissor) )
            return;
        d3d9_ui_rotated_sprite_quad(
            command->x,
            command->y,
            command->dst_anchor_x,
            command->dst_anchor_y,
            command->src_anchor_x,
            command->src_anchor_y,
            src_width,
            src_height,
            command->rotation_r2pi2048,
            uv,
            positions,
            rotated_uv);
        d3d9_ui_append_quad_vertices(
            renderer,
            renderer->ui_sprite_atlas_texture,
            true,
            &box_scissor,
            positions,
            rotated_uv,
            color);
        return;
    }
    if( command->tiled )
    {
        RECT tile_scissor;
        int tile_width = width > 0 ? width : 1;
        int tile_height = height > 0 ? height : 1;
        int dst_width = command->w > 0 ? command->w : tile_width;
        int dst_height = command->h > 0 ? command->h : tile_height;
        int start_x;
        int start_y;
        int x;
        int y;
        if( !d3d9_ui_intersect_scissor_rect(
                renderer,
                command->scissor_x,
                command->scissor_y,
                command->scissor_w,
                command->scissor_h,
                command->x,
                command->y,
                dst_width,
                dst_height,
                &tile_scissor) )
            return;
        trspk_sprite_tile_phase_origin(
            command->x,
            command->y,
            command->x + sprite->crop_x,
            command->y + sprite->crop_y,
            tile_width,
            tile_height,
            &start_x,
            &start_y);
        for( y = start_y; y < command->y + dst_height; y += tile_height )
            for( x = start_x; x < command->x + dst_width; x += tile_width )
                d3d9_ui_append_quad(
                    renderer,
                    renderer->ui_sprite_atlas_texture,
                    true,
                    &tile_scissor,
                    (float)x,
                    (float)y,
                    (float)(x + tile_width),
                    (float)(y + tile_height),
                    uv[0],
                    uv[1],
                    uv[2],
                    uv[3],
                    color);
        return;
    }
    if( command->if3 )
    {
        RECT box_scissor;
        int nominal_width = sprite->width > 0 ? sprite->width : (width > 0 ? width : 1);
        int nominal_height = sprite->height > 0 ? sprite->height : (height > 0 ? height : 1);
        int box_width = command->w > 0 ? command->w : nominal_width;
        int box_height = command->h > 0 ? command->h : nominal_height;
        float scale_x = (float)box_width / (float)nominal_width;
        float scale_y = (float)box_height / (float)nominal_height;
        float x0 = (float)command->x + offset_x * scale_x;
        float y0 = (float)command->y + offset_y * scale_y;
        if( !d3d9_ui_intersect_scissor_rect(
                renderer,
                command->scissor_x,
                command->scissor_y,
                command->scissor_w,
                command->scissor_h,
                command->x,
                command->y,
                box_width,
                box_height,
                &box_scissor) )
            return;
        d3d9_ui_append_quad(
            renderer,
            renderer->ui_sprite_atlas_texture,
            true,
            &box_scissor,
            x0,
            y0,
            x0 + width * scale_x,
            y0 + height * scale_y,
            uv[0],
            uv[1],
            uv[2],
            uv[3],
            color);
        return;
    }
    d3d9_ui_append_quad(
        renderer,
        renderer->ui_sprite_atlas_texture,
        true,
        scissor_ptr,
        (float)(command->x + offset_x),
        (float)(command->y + offset_y),
        (float)(command->x + offset_x + width),
        (float)(command->y + offset_y + height),
        uv[0],
        uv[1],
        uv[2],
        uv[3],
        color);
}

static void
d3d9_ui_draw_clear_rect(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand_ClearRect* command)
{
    RECT scissor;
    if( !renderer->in2d || command->w <= 0 || command->h <= 0 ||
        !d3d9_ui_scissor_rect(
            renderer, 0, 0, renderer->width, renderer->height, &scissor) )
        return;
    d3d9_ui_append_quad(
        renderer,
        NULL,
        false,
        &scissor,
        (float)command->x,
        (float)command->y,
        (float)(command->x + command->w),
        (float)(command->y + command->h),
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        (D3DCOLOR)TORIRS_D3D9_BG);
}

static void
d3d9_ui_draw_fill_rect(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand_FillRect* command)
{
    RECT scissor;
    D3DCOLOR color;
    if( !renderer->in2d || command->w <= 0 || command->h <= 0 ||
        !d3d9_ui_scissor_rect(
            renderer,
            command->scissor_x,
            command->scissor_y,
            command->scissor_w,
            command->scissor_h,
            &scissor) )
        return;
    color = (D3DCOLOR)(uint32_t)command->argb;
    if( command->filled )
    {
        d3d9_ui_append_quad(
            renderer,
            NULL,
            false,
            &scissor,
            (float)command->x,
            (float)command->y,
            (float)(command->x + command->w),
            (float)(command->y + command->h),
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            color);
        return;
    }
    d3d9_ui_append_quad(
        renderer, NULL, false, &scissor, (float)command->x, (float)command->y,
        (float)(command->x + command->w), (float)(command->y + 1), 0, 0, 0, 0, color);
    if( command->h > 1 )
        d3d9_ui_append_quad(
            renderer, NULL, false, &scissor, (float)command->x,
            (float)(command->y + command->h - 1), (float)(command->x + command->w),
            (float)(command->y + command->h), 0, 0, 0, 0, color);
    if( command->h > 2 )
    {
        d3d9_ui_append_quad(
            renderer, NULL, false, &scissor, (float)command->x,
            (float)(command->y + 1), (float)(command->x + 1),
            (float)(command->y + command->h - 1), 0, 0, 0, 0, color);
        if( command->w > 1 )
            d3d9_ui_append_quad(
                renderer, NULL, false, &scissor,
                (float)(command->x + command->w - 1), (float)(command->y + 1),
                (float)(command->x + command->w),
                (float)(command->y + command->h - 1), 0, 0, 0, 0, color);
    }
}

static void
d3d9_ui_draw_line(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand_Line* command)
{
    RECT scissor;
    float positions[4][2];
    float dx;
    float dy;
    float length;
    float half;
    float px;
    float py;
    float x0;
    float y0;
    float x1;
    float y1;
    int thickness;
    if( !renderer->in2d ||
        !d3d9_ui_scissor_rect(
            renderer,
            command->scissor_x,
            command->scissor_y,
            command->scissor_w,
            command->scissor_h,
            &scissor) )
        return;
    thickness = command->line_width > 0 ? command->line_width : 1;
    x0 = (float)command->x;
    y0 = (float)(command->line_direction ? command->y + command->h : command->y);
    x1 = (float)(command->x + command->w);
    y1 = (float)(command->line_direction ? command->y : command->y + command->h);
    dx = x1 - x0;
    dy = y1 - y0;
    length = sqrtf(dx * dx + dy * dy);
    half = (float)thickness * 0.5f;
    if( length <= 0.0001f )
    {
        d3d9_ui_append_quad(
            renderer, NULL, false, &scissor, x0 - half, y0 - half, x0 + half,
            y0 + half, 0, 0, 0, 0, (D3DCOLOR)(uint32_t)command->argb);
        return;
    }
    px = -dy * half / length;
    py = dx * half / length;
    positions[0][0] = x0 + px;
    positions[0][1] = y0 + py;
    positions[1][0] = x1 + px;
    positions[1][1] = y1 + py;
    positions[2][0] = x1 - px;
    positions[2][1] = y1 - py;
    positions[3][0] = x0 - px;
    positions[3][1] = y0 - py;
    d3d9_ui_append_quad_vertices(
        renderer,
        NULL,
        false,
        &scissor,
        positions,
        NULL,
        (D3DCOLOR)(uint32_t)command->argb);
}

static int
d3d9_ui_font_slot_index(struct ToriRS_D3D9* renderer, int font_id, bool create)
{
    int free_index = -1;
    int i;
    if( font_id < 0 )
        return -1;
    for( i = 0; i < D3D9_UI_FONT_CAP; i++ )
    {
        if( renderer->ui_fonts[i].font_id == font_id )
            return i;
        if( free_index < 0 && renderer->ui_fonts[i].font_id < 0 )
            free_index = i;
    }
    if( !create || free_index < 0 )
        return -1;
    renderer->ui_fonts[free_index].font_id = font_id;
    renderer->ui_fonts[free_index].font = NULL;
    renderer->ui_fonts[free_index].baked = false;
    return free_index;
}

static void
d3d9_ui_font_release_slot(struct D3D9UIFontSlot* slot)
{
    if( slot->texture )
        IDirect3DTexture9_Release(slot->texture);
    slot->texture = NULL;
    slot->texture_width = 0u;
    slot->texture_height = 0u;
    slot->atlas_width = 0;
    slot->atlas_height = 0;
    slot->baked = false;
    memset(slot->glyph_uv, 0, sizeof(slot->glyph_uv));
}

static bool
d3d9_ui_bake_font(
    struct ToriRS_D3D9* renderer,
    struct D3D9UIFontSlot* slot)
{
    struct ToriDraw_Font* font;
    uint32_t* pixels;
    D3DLOCKED_RECT locked;
    UINT texture_width;
    UINT texture_height;
    HRESULT hr;
    int atlas_width = 0;
    int atlas_height = 0;
    int atlas_y = 0;
    int i;
    int y;
    assert(slot);
    if( !(font = slot->font) )
        return false;
    if( slot->baked )
        return true;
    for( i = 0; i < TORIDRAW_FONT_GLYPH_COUNT; i++ )
    {
        if( font->glyph_width[i] > atlas_width )
            atlas_width = font->glyph_width[i];
        if( font->glyph_height[i] > 0 )
            atlas_height += font->glyph_height[i];
    }
    if( atlas_width <= 0 || atlas_height <= 0 )
        return false;
    texture_width = d3d9_next_pow2((UINT)atlas_width);
    texture_height = d3d9_next_pow2((UINT)atlas_height);
    if( texture_width > renderer->caps.MaxTextureWidth ||
        texture_height > renderer->caps.MaxTextureHeight )
        return false;
    d3d9_ui_flush(renderer);
    d3d9_ui_font_release_slot(slot);
    pixels = (uint32_t*)calloc(
        (size_t)texture_width * (size_t)texture_height, sizeof(*pixels));
    if( !pixels )
        return false;
    for( i = 0; i < TORIDRAW_FONT_GLYPH_COUNT; i++ )
    {
        int glyph_width = font->glyph_width[i];
        int glyph_height = font->glyph_height[i];
        const uint8_t* alpha = font->glyph_alpha[i];
        int x;
        if( alpha && glyph_width > 0 && glyph_height > 0 )
            for( y = 0; y < glyph_height; y++ )
                for( x = 0; x < glyph_width; x++ )
                    pixels[(size_t)(atlas_y + y) * texture_width + x] =
                        ((uint32_t)alpha[(size_t)y * glyph_width + x] << 24) |
                        0x00ffffffu;
        slot->glyph_uv[i * 4 + 0] = 0.0f;
        slot->glyph_uv[i * 4 + 1] = (float)atlas_y / (float)texture_height;
        slot->glyph_uv[i * 4 + 2] = (float)glyph_width / (float)texture_width;
        slot->glyph_uv[i * 4 + 3] =
            (float)(atlas_y + glyph_height) / (float)texture_height;
        atlas_y += glyph_height;
    }
    hr = IDirect3DDevice9_CreateTexture(
        renderer->device,
        texture_width,
        texture_height,
        1u,
        0u,
        D3DFMT_A8R8G8B8,
        D3DPOOL_MANAGED,
        &slot->texture,
        NULL);
    if( FAILED(hr) )
    {
        d3d9_log_hr("CreateTexture(font atlas)", hr);
        free(pixels);
        return false;
    }
    hr = IDirect3DTexture9_LockRect(slot->texture, 0u, &locked, NULL, 0u);
    if( FAILED(hr) )
    {
        d3d9_log_hr("LockRect(font atlas)", hr);
        free(pixels);
        d3d9_ui_font_release_slot(slot);
        return false;
    }
    for( y = 0; y < (int)texture_height; y++ )
        memcpy(
            (uint8_t*)locked.pBits + (size_t)y * locked.Pitch,
            pixels + (size_t)y * texture_width,
            (size_t)texture_width * 4u);
    IDirect3DTexture9_UnlockRect(slot->texture, 0u);
    free(pixels);
    slot->texture_width = texture_width;
    slot->texture_height = texture_height;
    slot->atlas_width = atlas_width;
    slot->atlas_height = atlas_height;
    slot->baked = true;
    renderer->ui_texture_upload_bytes += (uint64_t)texture_width * texture_height * 4u;
    renderer->ui_texture_upload_count++;
    TORIRS_PERF_COUNT(
        TORIRS_PERF_CTR_D3D9_UI_TEXTURE_UPLOAD_BYTES,
        (int64_t)((uint64_t)texture_width * texture_height * 4u));
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_UI_TEXTURE_UPLOADS, 1);
    return true;
}

static struct D3D9UIFontSlot*
d3d9_ui_ensure_font(struct ToriRS_D3D9* renderer, int font_id)
{
    int slot_index = d3d9_ui_font_slot_index(renderer, font_id, true);
    struct D3D9UIFontSlot* slot;
    if( slot_index < 0 )
        return NULL;
    slot = &renderer->ui_fonts[slot_index];
    if( !slot->font && renderer->scene )
        slot->font = ToriDraw_SceneFontGet(renderer->scene, font_id);
    if( slot->font && !slot->baked && !d3d9_ui_bake_font(renderer, slot) )
        return NULL;
    return slot;
}

struct D3D9UIFontGlyphContext
{
    struct ToriRS_D3D9* renderer;
    struct D3D9UIFontSlot* slot;
    RECT scissor;
    bool shadow;
};

static void
d3d9_ui_font_glyph(
    void* opaque,
    struct ToriDraw_Font* font,
    int glyph_index,
    int x,
    int y,
    int color_rgb)
{
    struct D3D9UIFontGlyphContext* context =
        (struct D3D9UIFontGlyphContext*)opaque;
    struct D3D9UIFontSlot* slot = context->slot;
    int width;
    int height;
    D3DCOLOR color;
    (void)font;
    if( glyph_index < 0 || glyph_index >= TORIDRAW_FONT_GLYPH_COUNT )
        return;
    width = slot->font->glyph_width[glyph_index];
    height = slot->font->glyph_height[glyph_index];
    if( width <= 0 || height <= 0 )
        return;
    if( context->shadow )
        color_rgb = 0;
    color = (D3DCOLOR)(0xff000000u | ((uint32_t)color_rgb & 0x00ffffffu));
    d3d9_ui_append_quad(
        context->renderer,
        slot->texture,
        false,
        &context->scissor,
        (float)x,
        (float)y,
        (float)(x + width),
        (float)(y + height),
        slot->glyph_uv[glyph_index * 4 + 0],
        slot->glyph_uv[glyph_index * 4 + 1],
        slot->glyph_uv[glyph_index * 4 + 2],
        slot->glyph_uv[glyph_index * 4 + 3],
        color);
}

static void
d3d9_ui_draw_font_rules(
    struct ToriRS_D3D9* renderer,
    struct ToriDraw_Font* font,
    const RECT* scissor,
    const char* text,
    int x,
    int y,
    bool center);

static void
d3d9_ui_draw_font_text(
    struct ToriRS_D3D9* renderer,
    struct D3D9UIFontSlot* slot,
    const RECT* scissor,
    const char* text,
    int x,
    int y,
    int color,
    bool shadow,
    bool center)
{
    struct D3D9UIFontGlyphContext context;
    if( !text || !text[0] || !slot || !slot->font || !slot->baked || !slot->texture )
        return;
    context.renderer = renderer;
    context.slot = slot;
    context.scissor = *scissor;
    context.shadow = shadow;
    ToriDraw_FontVisitGlyphsStyled(
        slot->font,
        text,
        x,
        y,
        color,
        center,
        d3d9_ui_font_glyph,
        &context);
    if( !shadow )
        d3d9_ui_draw_font_rules(renderer, slot->font, scissor, text, x, y, center);
}

static bool
d3d9_ui_char_equal_ignore_case(char a, char b)
{
    if( a >= 'A' && a <= 'Z' )
        a = (char)(a + ('a' - 'A'));
    if( b >= 'A' && b <= 'Z' )
        b = (char)(b + ('a' - 'A'));
    return a == b;
}

static bool
d3d9_ui_font_line_break(const char* text, int* advance)
{
    assert(text);
    if( !text[0] )
        return false;
    if( text[0] == '\\' && text[1] == 'n' )
    {
        *advance = 2;
        return true;
    }
    if( text[0] == '\r' && text[1] == '\n' )
    {
        *advance = 2;
        return true;
    }
    if( text[0] == '\r' || text[0] == '\n' )
    {
        *advance = 1;
        return true;
    }
    /* Guard each successive byte before looking farther ahead.  This walker is
     * called at every byte, including a trailing "<" or "<b", so fixed-index
     * tag probes must not read past the string's terminating NUL. */
    if( text[0] == '<' && text[1] != '\0' && text[2] != '\0' &&
        d3d9_ui_char_equal_ignore_case(text[1], 'b') &&
        d3d9_ui_char_equal_ignore_case(text[2], 'r') && text[3] == '>' )
    {
        *advance = 4;
        return true;
    }
    if( text[0] == '<' && text[1] != '\0' && text[2] != '\0' && text[3] != '\0' &&
        d3d9_ui_char_equal_ignore_case(text[1], 'b') &&
        d3d9_ui_char_equal_ignore_case(text[2], 'r') && text[3] == '/' &&
        text[4] == '>' )
    {
        *advance = 5;
        return true;
    }
    return false;
}

static const char*
d3d9_ui_font_next_line(const char* text, int* length, int* advance)
{
    const char* cursor = text;
    while( cursor[0] )
    {
        if( d3d9_ui_font_line_break(cursor, advance) )
        {
            *length = (int)(cursor - text);
            return cursor;
        }
        cursor++;
    }
    *length = (int)(cursor - text);
    *advance = 0;
    return cursor;
}

static int
d3d9_ui_font_measure_range(struct ToriDraw_Font* font, const char* text, int length)
{
    char buffer[4096];
    if( length <= 0 )
        return 0;
    if( length >= (int)sizeof(buffer) )
        length = (int)sizeof(buffer) - 1;
    memcpy(buffer, text, (size_t)length);
    buffer[length] = '\0';
    return ToriDraw2D_MeasureString(font, buffer);
}

static int
d3d9_ui_font_char_advance(const struct ToriDraw_Font* font, unsigned char ch)
{
    int glyph_index;
    int advance;
    if( ch == ' ' || ch == '|' )
        glyph_index = TORIDRAW_FONT_GLYPH_COUNT;
    else
    {
        glyph_index = (unsigned char)font->charcodeset[ch];
        if( glyph_index > TORIDRAW_FONT_GLYPH_COUNT )
        {
            glyph_index = (unsigned char)font->charcodeset[(unsigned char)' '];
            if( glyph_index > TORIDRAW_FONT_GLYPH_COUNT )
                glyph_index = TORIDRAW_FONT_GLYPH_COUNT;
        }
    }
    advance = font->advance[glyph_index];
    return advance > 0 ? advance : 4;
}

static void
d3d9_ui_append_rule_quad(
    struct ToriRS_D3D9* renderer,
    const RECT* scissor,
    int x,
    int y,
    int advance,
    int rgb)
{
    d3d9_ui_append_quad(
        renderer,
        NULL,
        false,
        scissor,
        (float)x,
        (float)y,
        (float)(x + advance),
        (float)(y + 1),
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        (D3DCOLOR)(0xff000000u | ((uint32_t)rgb & 0x00ffffffu)));
}

/* Underline and strikethrough: the glyph pass draws glyphs only, so the rules
 * get their own walk over the same tokens. */
static void
d3d9_ui_draw_font_rule_range(
    struct ToriRS_D3D9* renderer,
    struct ToriDraw_Font* font,
    const RECT* scissor,
    const char* text,
    int length,
    int x,
    int y)
{
    int ascent = font->line_height > 0 ? font->line_height : 1;
    int underline_rgb = -1;
    int strike_rgb = -1;
    int underline_y = y + ascent + 1;
    int strike_y = y + (ascent * 7) / 10;
    int i = 0;
    while( i < length )
    {
        unsigned char emit_char = 0;
        int consumed = ToriDraw_FontMarkupTokenLength(text, length, i, &emit_char);
        int advance;
        if( consumed > 0 )
        {
            if( consumed == 4 && strncmp(text + i, "</u>", 4) == 0 )
                underline_rgb = -1;
            else if( consumed == 3 && strncmp(text + i, "<u>", 3) == 0 )
                underline_rgb = 0;
            else if( (consumed == 10 || consumed == 12) &&
                strncmp(text + i, "<u=", 3) == 0 )
            {
                int parsed = ToriDraw_FontParseHexColor(text + i + 3, consumed - 4);
                if( parsed >= 0 )
                    underline_rgb = parsed;
            }
            else if( consumed == 6 && strncmp(text + i, "</str>", 6) == 0 )
                strike_rgb = -1;
            else if( consumed == 5 && strncmp(text + i, "<str>", 5) == 0 )
                strike_rgb = TORIDRAW_FONT_STRIKE_DEFAULT_RGB;
            else if( (consumed == 12 || consumed == 14) &&
                strncmp(text + i, "<str=", 5) == 0 )
            {
                int parsed = ToriDraw_FontParseHexColor(text + i + 5, consumed - 6);
                if( parsed >= 0 )
                    strike_rgb = parsed;
            }
            i += consumed;
            if( emit_char == 0 )
                continue;
        }
        else
            emit_char = (unsigned char)text[i++];

        advance = d3d9_ui_font_char_advance(font, emit_char);
        if( advance > 0 )
        {
            if( strike_rgb >= 0 )
                d3d9_ui_append_rule_quad(
                    renderer, scissor, x, strike_y, advance, strike_rgb);
            if( underline_rgb >= 0 )
                d3d9_ui_append_rule_quad(
                    renderer, scissor, x, underline_y, advance, underline_rgb);
        }
        x += advance;
    }
}

static void
d3d9_ui_draw_font_rules(
    struct ToriRS_D3D9* renderer,
    struct ToriDraw_Font* font,
    const RECT* scissor,
    const char* text,
    int x,
    int y,
    bool center)
{
    int line_step = font->line_height > 0 ? font->line_height : 1;
    const char* rest = text;
    for( ;; )
    {
        int length = 0;
        int advance = 0;
        int line_x = x;
        const char* break_at = d3d9_ui_font_next_line(rest, &length, &advance);
        if( center && length > 0 )
            line_x -= d3d9_ui_font_measure_range(font, rest, length) / 2;
        if( length > 0 )
            d3d9_ui_draw_font_rule_range(
                renderer, font, scissor, rest, length, line_x, y);
        if( advance == 0 )
            break;
        y += line_step;
        rest = break_at + advance;
    }
}

static void
d3d9_ui_font_vertical_metrics(
    const struct ToriDraw_Font* font,
    int* ascent_out,
    int* descent_out)
{
    int fallback = font->line_height > 0 ? font->line_height : 1;
    int min_y = 0;
    int max_bottom = 0;
    bool any = false;
    int i;
    for( i = 0; i < TORIDRAW_FONT_GLYPH_COUNT; i++ )
    {
        int bottom;
        if( font->glyph_width[i] <= 0 || font->glyph_height[i] <= 0 ||
            !font->glyph_alpha[i] )
            continue;
        bottom = font->offset_y[i] + font->glyph_height[i];
        if( !any || font->offset_y[i] < min_y )
            min_y = font->offset_y[i];
        if( !any || bottom > max_bottom )
            max_bottom = bottom;
        any = true;
    }
    if( !any )
    {
        *ascent_out = fallback;
        *descent_out = 0;
        return;
    }
    *ascent_out = fallback - min_y;
    *descent_out = max_bottom - fallback;
    if( *ascent_out <= 0 )
        *ascent_out = fallback;
    if( *descent_out < 0 )
        *descent_out = 0;
}

static bool
d3d9_ui_font_append_line(
    const char* lines[],
    int lengths[],
    int* count,
    const char* text,
    int length)
{
    if( *count >= D3D9_UI_FONT_BOX_MAX_LINES )
        return false;
    lines[*count] = text;
    lengths[*count] = length;
    (*count)++;
    return true;
}

static bool
d3d9_ui_font_segment_has_visible_content(const char* text, int length)
{
    int i;
    if( length <= 0 )
        return false;
    assert(text);
    for( i = 0; i < length; i++ )
    {
        unsigned char emit_char = 0;
        int consumed;
        if( text[i] == ' ' || text[i] == '|' )
            continue;
        consumed = ToriDraw_FontMarkupTokenLength(text, length, i, &emit_char);
        if( consumed > 0 )
        {
            if( emit_char != 0 )
                return true;
            i += consumed - 1;
            continue;
        }
        return true;
    }
    return false;
}

static bool
d3d9_ui_font_wrap_segment(
    struct ToriDraw_Font* font,
    const char* text,
    int length,
    int max_width,
    const char* lines[],
    int lengths[],
    int* count)
{
    int space_width = d3d9_ui_font_measure_range(font, " ", 1);
    int current_start = -1;
    int current_length = 0;
    int current_width = 0;
    int word_start = 0;
    int i;
    if( length <= 0 )
        return d3d9_ui_font_append_line(lines, lengths, count, text, 0);
    if( !d3d9_ui_font_segment_has_visible_content(text, length) )
        return d3d9_ui_font_append_line(lines, lengths, count, text, 0);
    for( i = 0; i <= length; i++ )
    {
        bool at_end = i == length;
        bool space = !at_end && (text[i] == ' ' || text[i] == '|');
        int word_length;
        int word_width;
        if( !at_end && !space )
            continue;
        word_length = i - word_start;
        if( word_length <= 0 )
        {
            word_start = at_end ? i : i + 1;
            continue;
        }
        word_width = d3d9_ui_font_measure_range(font, text + word_start, word_length);
        if( current_length <= 0 )
        {
            current_start = word_start;
            current_length = word_length;
            current_width = word_width;
        }
        else if( current_width + space_width + word_width > max_width )
        {
            if( !d3d9_ui_font_append_line(
                    lines, lengths, count, text + current_start, current_length) )
                return false;
            current_start = word_start;
            current_length = word_length;
            current_width = word_width;
        }
        else
        {
            current_length = i - current_start;
            current_width += space_width + word_width;
        }
        word_start = at_end ? i : i + 1;
    }
    if( current_length > 0 )
        return d3d9_ui_font_append_line(
            lines, lengths, count, text + current_start, current_length);
    return true;
}

static int
d3d9_ui_font_collect_lines(
    struct ToriDraw_Font* font,
    const struct ToriRS_RenderCommand_Font* command,
    const char* lines[],
    int lengths[])
{
    const char* rest = command->text;
    int line_height = command->line_height > 0
        ? command->line_height
        : (font->line_height > 0 ? font->line_height : 1);
    int ascent;
    int descent;
    int count = 0;
    bool wrap;
    d3d9_ui_font_vertical_metrics(font, &ascent, &descent);
    wrap = command->w > 0 && command->h > 0 &&
        !(command->h < line_height + ascent + descent && command->h < line_height * 2);
    while( rest && rest[0] && count < D3D9_UI_FONT_BOX_MAX_LINES )
    {
        int length = 0;
        int advance = 0;
        const char* line_end = d3d9_ui_font_next_line(rest, &length, &advance);
        if( wrap )
        {
            if( !d3d9_ui_font_wrap_segment(
                    font, rest, length, command->w > 0 ? command->w : 1,
                    lines, lengths, &count) )
                break;
        }
        else if( !d3d9_ui_font_append_line(lines, lengths, &count, rest, length) )
            break;
        if( advance == 0 )
            break;
        rest = line_end + advance;
    }
    return count;
}

static void
d3d9_ui_draw_font_range(
    struct ToriRS_D3D9* renderer,
    struct D3D9UIFontSlot* slot,
    const RECT* scissor,
    const char* text,
    int length,
    int x,
    int y,
    int color,
    bool shadow)
{
    char buffer[4096];
    if( length <= 0 )
        return;
    if( length >= (int)sizeof(buffer) )
        length = (int)sizeof(buffer) - 1;
    memcpy(buffer, text, (size_t)length);
    buffer[length] = '\0';
    d3d9_ui_draw_font_text(
        renderer, slot, scissor, buffer, x, y, color, shadow, false);
}

static void
d3d9_ui_draw_font(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand_Font* command)
{
    struct D3D9UIFontSlot* slot;
    struct ToriDraw_Font* font;
    RECT scissor;
    if( !renderer->in2d || command->font_id < 0 || !command->text ||
        !command->text[0] ||
        !d3d9_ui_scissor_rect(
            renderer,
            command->scissor_x,
            command->scissor_y,
            command->scissor_w,
            command->scissor_h,
            &scissor) )
        return;
    slot = d3d9_ui_ensure_font(renderer, command->font_id);
    font = slot ? slot->font : NULL;
    if( !font || !slot->baked || !ToriDraw_FontValidate(font) )
        return;
    if( command->baseline )
    {
        int y = command->y - font->line_height;
        bool center = command->center != 0;
        if( command->shadowed )
            d3d9_ui_draw_font_text(
                renderer, slot, &scissor, command->text, command->x + 1, y + 1,
                command->color, true, center);
        d3d9_ui_draw_font_text(
            renderer, slot, &scissor, command->text, command->x, y,
            command->color, false, center);
        return;
    }
    {
        const char* lines[D3D9_UI_FONT_BOX_MAX_LINES];
        int lengths[D3D9_UI_FONT_BOX_MAX_LINES];
        int count = d3d9_ui_font_collect_lines(font, command, lines, lengths);
        int line_height = command->line_height > 0
            ? command->line_height
            : (font->line_height > 0 ? font->line_height : 1);
        int font_ascent = font->line_height > 0 ? font->line_height : line_height;
        int ascent;
        int descent;
        int logical_height;
        int first_baseline;
        int i;
        if( count <= 0 )
            return;
        d3d9_ui_font_vertical_metrics(font, &ascent, &descent);
        logical_height = command->h > 0
            ? command->h
            : line_height * (count - 1) + ascent + descent;
        first_baseline = ascent;
        if( command->y_align == 1 )
            first_baseline = ascent +
                (logical_height - ascent - descent - line_height * (count - 1)) / 2;
        else if( command->y_align == 2 )
            first_baseline = logical_height - descent - line_height * (count - 1);
        for( i = 0; i < count; i++ )
        {
            int x = command->x;
            int y;
            if( lengths[i] <= 0 )
                continue;
            if( command->center != 0 )
            {
                int text_width = d3d9_ui_font_measure_range(font, lines[i], lengths[i]);
                if( command->center == 1 )
                    x += ((command->w > 0 ? command->w : 1) - text_width) / 2;
                else if( command->center == 2 )
                    x += (command->w > 0 ? command->w : 1) - text_width;
            }
            y = command->y + first_baseline + i * line_height - font_ascent;
            if( command->shadowed )
                d3d9_ui_draw_font_range(
                    renderer, slot, &scissor, lines[i], lengths[i], x + 1, y + 1,
                    command->color, true);
            d3d9_ui_draw_font_range(
                renderer, slot, &scissor, lines[i], lengths[i], x, y,
                command->color, false);
        }
    }
}

static void
d3d9_widget_model_transform_vertex(
    const struct ToriDraw_WidgetModelTransform* transform,
    int vx,
    int vy,
    int vz,
    int* out_x,
    int* out_y,
    int* out_z)
{
    int rotated;
    if( transform->var3 != 0 )
    {
        rotated = (vy * transform->var14 + vx * transform->var15) >> 16;
        vy = (vy * transform->var15 - vx * transform->var14) >> 16;
        vx = rotated;
    }
    if( transform->var10 != 0 )
    {
        rotated = (vy * transform->var11 - vz * transform->var10) >> 16;
        vz = (vy * transform->var10 + vz * transform->var11) >> 16;
        vy = rotated;
    }
    if( transform->var2 != 0 )
    {
        rotated = (vz * transform->var12 + vx * transform->var13) >> 16;
        vz = (vz * transform->var13 - vx * transform->var12) >> 16;
        vx = rotated;
    }
    vx += transform->var5;
    vy += transform->var6;
    vz += transform->var7;
    rotated = (vy * transform->var17 - vz * transform->var16) >> 16;
    vz = (vy * transform->var16 + vz * transform->var17) >> 16;
    *out_x = vx;
    *out_y = rotated;
    *out_z = vz;
}

/* Populate the same projected arrays the reference widget rasterizer uses.
 * Besides preserving its back-face cull and priority/depth ordering, this lets
 * the native path share the scene's existing bounded face-sort workspace. */
static bool
d3d9_widget_model_project(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand_ModelWidget* command,
    const struct ToriDraw_WidgetModelTransform* transform,
    float* out_origin_x,
    float* out_origin_y)
{
    struct ToriDraw_Model* model = command->model.u.model.model;
    struct ToriDraw_Scene* scene = renderer->scene;
    int vertex_count = model->vertex_count;
    int vertex;
    if( !model->vertices_x || !model->vertices_y || !model->vertices_z ||
        !model->face_indices_a || !model->face_indices_b || !model->face_indices_c ||
        !model->face_colors_a || !model->face_colors_b || !model->face_colors_c ||
        vertex_count <= 0 || vertex_count > scene->max_vertices ||
        model->face_count <= 0 || model->face_count > scene->max_faces )
        return false;
    scene->active_hnd = command->model;
    if( transform->orthographic )
    {
        int bounds_width = 0;
        int bounds_height = 0;
        int bounds_dx = 0;
        int bounds_dy = 0;
        int orthographic_scale = transform->zoom2d > 100 ? transform->zoom2d : 100;
        int64_t depth_sum = 0;
        int depth_mid;
        if( !ToriDraw_WidgetModelBounds(
                command->model,
                transform,
                &bounds_width,
                &bounds_height,
                &bounds_dx,
                &bounds_dy) )
            return false;
        (void)bounds_height;
        (void)bounds_dx;
        (void)bounds_dy;
        *out_origin_x = (float)(command->x + command->w / 2 - bounds_width / 2);
        *out_origin_y = (float)(command->y + command->h / 2 - bounds_height / 2);
        for( vertex = 0; vertex < vertex_count; vertex++ )
        {
            int cx;
            int cy;
            int cz;
            d3d9_widget_model_transform_vertex(
                transform,
                model->vertices_x[vertex],
                model->vertices_y[vertex],
                model->vertices_z[vertex],
                &cx,
                &cy,
                &cz);
            scene->orthographic_vertices_x[vertex] = cx;
            scene->orthographic_vertices_y[vertex] = cy;
            scene->orthographic_vertices_z[vertex] = cz;
            scene->screen_vertices_x[vertex] = (int)*out_origin_x +
                (int)((int64_t)cx * transform->zoom3d / orthographic_scale);
            scene->screen_vertices_y[vertex] = (int)*out_origin_y +
                (int)((int64_t)cy * transform->zoom3d / orthographic_scale);
            scene->screen_vertices_z[vertex] = cz;
            depth_sum += cz;
        }
        depth_mid = (int)(depth_sum / vertex_count);
        for( vertex = 0; vertex < vertex_count; vertex++ )
            scene->screen_vertices_z[vertex] -= depth_mid;
        return true;
    }
    else
    {
        int depth_mid =
            (transform->var6 * transform->var16 + transform->var7 * transform->var17) >>
            16;
        int visible_vertices = 0;
        *out_origin_x = (float)(command->x + command->w / 2);
        *out_origin_y = (float)(command->y + command->h / 2);
        for( vertex = 0; vertex < vertex_count; vertex++ )
        {
            int cx;
            int cy;
            int cz;
            d3d9_widget_model_transform_vertex(
                transform,
                model->vertices_x[vertex],
                model->vertices_y[vertex],
                model->vertices_z[vertex],
                &cx,
                &cy,
                &cz);
            scene->orthographic_vertices_x[vertex] = cx;
            scene->orthographic_vertices_y[vertex] = cy;
            scene->orthographic_vertices_z[vertex] = cz;
            scene->screen_vertices_z[vertex] = cz - depth_mid;
            if( (float)cz <= D3D9_WIDGET_MODEL_NEAR )
            {
                scene->screen_vertices_x[vertex] = -5000;
                scene->screen_vertices_y[vertex] = 0;
                continue;
            }
            scene->screen_vertices_x[vertex] = (int)*out_origin_x +
                (int)((int64_t)cx * transform->zoom3d / cz);
            scene->screen_vertices_y[vertex] = (int)*out_origin_y +
                (int)((int64_t)cy * transform->zoom3d / cz);
            visible_vertices++;
        }
        return visible_vertices > 0;
    }
}

static bool
d3d9_widget_model_prepare_textures(
    struct ToriRS_D3D9* renderer,
    const struct ToriDraw_Model* model,
    const int* face_order,
    int face_count)
{
    uint8_t seen[TORIDRAW_TEXTURE_ID_CAPACITY] = { 0 };
    int order_index;
    for( order_index = 0; order_index < face_count; order_index++ )
    {
        int face = face_order[order_index];
        int texture_id;
        if( face < 0 || face >= model->face_count || !model->face_textures )
            continue;
        texture_id = (int)model->face_textures[face];
        if( texture_id < 0 || texture_id >= TORIDRAW_TEXTURE_ID_CAPACITY ||
            seen[texture_id] )
            continue;
        seen[texture_id] = 1u;
        if( trspk_toridraw_texture_is_animated(renderer->scene, texture_id) )
        {
            if( !d3d9_ensure_animated_texture(renderer, texture_id) )
                (void)d3d9_texture_slot(renderer, texture_id);
        }
        else
        {
            if( d3d9_texture_slot(renderer, texture_id) >= 0 )
                (void)d3d9_ensure_static_texture(renderer, texture_id);
        }
    }
    return !trspk_atlas_is_dirty(&renderer->atlas) || d3d9_upload_atlas(renderer);
}

static struct D3D9WidgetVertex
d3d9_widget_vertex_lerp(
    const struct D3D9WidgetVertex* a,
    const struct D3D9WidgetVertex* b,
    float amount)
{
    struct D3D9WidgetVertex out;
    int channel;
    out.cx = a->cx + (b->cx - a->cx) * amount;
    out.cy = a->cy + (b->cy - a->cy) * amount;
    out.cz = a->cz + (b->cz - a->cz) * amount;
    for( channel = 0; channel < 4; channel++ )
        out.color[channel] = a->color[channel] +
            (b->color[channel] - a->color[channel]) * amount;
    out.u = a->u + (b->u - a->u) * amount;
    out.v = a->v + (b->v - a->v) * amount;
    return out;
}

static int
d3d9_widget_model_clip_near(
    const struct D3D9WidgetVertex input[3],
    struct D3D9WidgetVertex output[4])
{
    int output_count = 0;
    int edge;
    for( edge = 0; edge < 3; edge++ )
    {
        const struct D3D9WidgetVertex* a = &input[edge];
        const struct D3D9WidgetVertex* b = &input[(edge + 1) % 3];
        bool a_inside = a->cz > D3D9_WIDGET_MODEL_NEAR;
        bool b_inside = b->cz > D3D9_WIDGET_MODEL_NEAR;
        if( a_inside )
            output[output_count++] = *a;
        if( a_inside != b_inside )
        {
            float amount = (D3D9_WIDGET_MODEL_NEAR - a->cz) / (b->cz - a->cz);
            output[output_count++] = d3d9_widget_vertex_lerp(a, b, amount);
        }
    }
    return output_count;
}

static void
d3d9_widget_model_overlay_vertex(
    const struct ToriRS_D3D9* renderer,
    const struct ToriDraw_WidgetModelTransform* transform,
    float origin_x,
    float origin_y,
    const struct D3D9WidgetVertex* source,
    struct D3D9OverlayVertex* out)
{
    float logical_x;
    float logical_y;
    if( transform->orthographic )
    {
        float scale = (float)(transform->zoom2d > 100 ? transform->zoom2d : 100);
        logical_x = origin_x + source->cx * (float)transform->zoom3d / scale;
        logical_y = origin_y + source->cy * (float)transform->zoom3d / scale;
        out->rhw = 1.0f;
    }
    else
    {
        logical_x = origin_x + source->cx * (float)transform->zoom3d / source->cz;
        logical_y = origin_y + source->cy * (float)transform->zoom3d / source->cz;
        out->rhw = 1.0f / source->cz;
    }
    out->x = d3d9_ui_x(renderer, logical_x);
    out->y = d3d9_ui_y(renderer, logical_y);
    out->z = 0.0f;
    out->color = (D3DCOLOR)trspk_d3d9_pack_argb(
        source->color[0], source->color[1], source->color[2], source->color[3]);
    out->u = source->u;
    out->v = source->v;
}

static void
d3d9_widget_model_flush_vertices(
    struct ToriRS_D3D9* renderer,
    int texture_config,
    uint32_t vertex_count)
{
    if( vertex_count == 0u )
        return;
    if( texture_config >= 0 )
        d3d9_bind_animated(renderer, texture_config);
    else if( texture_config == D3D9_WIDGET_CONFIG_ATLAS )
        d3d9_bind_atlas(renderer);
    else
        d3d9_set_no_texture(renderer->device);
    IDirect3DDevice9_DrawPrimitiveUP(
        renderer->device,
        D3DPT_TRIANGLELIST,
        vertex_count / 3u,
        renderer->ui_batch.vertices,
        (UINT)sizeof(renderer->ui_batch.vertices[0]));
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_UI_WIDGET_DRAWS, 1);
}

static int
d3d9_widget_model_face_texture(
    struct ToriRS_D3D9* renderer,
    const struct TRSPK_ToriDrawBakeFaceVerts* face,
    float uv[3][2])
{
    if( face->tex_id < 0 )
        return D3D9_WIDGET_CONFIG_NONE;
    if( face->tex_id < TORIDRAW_TEXTURE_ID_CAPACITY && face->is_animated &&
        renderer->animated_textures[face->tex_id] )
    {
        d3d9_map_animated_uv(&uv[0][0], &uv[0][1]);
        d3d9_map_animated_uv(&uv[1][0], &uv[1][1]);
        d3d9_map_animated_uv(&uv[2][0], &uv[2][1]);
        return face->tex_id;
    }
    if( face->tex_id >= TORIDRAW_TEXTURE_ID_CAPACITY ||
        renderer->tex_slot_of_id[face->tex_id] < 0 )
        return D3D9_WIDGET_CONFIG_SKIP;
    {
        int slot = renderer->tex_slot_of_id[face->tex_id];
        int corner;
        for( corner = 0; corner < 3; corner++ )
        {
            float mapped_u;
            float mapped_v;
            d3d9_map_atlas_uv(slot, uv[corner][0], uv[corner][1], &mapped_u, &mapped_v);
            uv[corner][0] = mapped_u;
            uv[corner][1] = mapped_v;
        }
    }
    return D3D9_WIDGET_CONFIG_ATLAS;
}

/* Widget models are drawn straight into the backbuffer as sorted transient
 * triangles.  This is one native pass: no canvas-sized raster, alpha scan, or
 * per-frame texture upload.  Perspective faces are clipped at the legacy near
 * plane before D3D receives them, and RHW retains perspective-correct UVs. */
static void
d3d9_ui_draw_model_widget(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand_ModelWidget* command)
{
    struct ToriDraw_WidgetModelTransform transform;
    struct ToriDraw_Model* model;
    RECT scissor;
    const int* face_order;
    int sorted_face_count;
    float origin_x;
    float origin_y;
    bool atlas_ready;
    int current_config = INT_MIN;
    uint32_t pending_vertices = 0u;
    int order_index;
    if( !renderer->in2d || !renderer->scene || !renderer->ui_batch.vertices ||
        command->model.kind != TORIDRAWMK_MODEL ||
        !(model = command->model.u.model.model) || command->w <= 0 || command->h <= 0 )
        return;
    if( !d3d9_ui_scissor_rect(
            renderer,
            command->scissor_x,
            command->scissor_y,
            command->scissor_w,
            command->scissor_h,
            &scissor) )
        return;
    ToriDraw_WidgetModelTransformInit(
        &transform,
        command->model_zoom > 0 ? command->model_zoom : 2000,
        command->model_xan,
        command->model_yan,
        command->model_zan,
        command->model_x_offset,
        command->model_y_offset,
        0,
        command->model_orthog != 0,
        command->model_fixed_zoom != 0);
    if( !d3d9_widget_model_project(
            renderer, command, &transform, &origin_x, &origin_y) )
        return;
    sorted_face_count = ToriDraw_RenderModel2SortFaces(command->model, renderer->scene);
    if( sorted_face_count <= 0 )
        return;
    face_order = ToriDraw_FaceOrder(renderer->scene);
    d3d9_ui_flush(renderer);
    atlas_ready = d3d9_widget_model_prepare_textures(
        renderer, model, face_order, sorted_face_count);
    d3d9_set_full_viewport(renderer);
    d3d9_ui_set_states(renderer);
    IDirect3DDevice9_SetRenderState(renderer->device, D3DRS_SCISSORTESTENABLE, TRUE);
    IDirect3DDevice9_SetScissorRect(renderer->device, &scissor);
    for( order_index = 0; order_index < sorted_face_count; order_index++ )
    {
        struct TRSPK_ToriDrawBakeFaceVerts face;
        struct D3D9WidgetVertex input[3];
        struct D3D9WidgetVertex clipped[4];
        const float* colors[3];
        float uv[3][2];
        int indices[3];
        int texture_config;
        int clipped_count;
        int face_index = face_order[order_index];
        int corner;
        int triangle;
        if( face_index < 0 || face_index >= model->face_count )
            continue;
        indices[0] = (int)model->face_indices_a[face_index];
        indices[1] = (int)model->face_indices_b[face_index];
        indices[2] = (int)model->face_indices_c[face_index];
        if( indices[0] < 0 || indices[0] >= model->vertex_count || indices[1] < 0 ||
            indices[1] >= model->vertex_count || indices[2] < 0 ||
            indices[2] >= model->vertex_count )
            continue;
        if( !trspk_toridraw_bake_face_handle(
                command->model,
                (uint32_t)face_index,
                NULL,
                renderer->scene,
                true,
                &face) ||
            (face.color_a[3] <= (1.0f / 255.0f) &&
             face.color_b[3] <= (1.0f / 255.0f) &&
             face.color_c[3] <= (1.0f / 255.0f)) )
            continue;
        colors[0] = face.color_a;
        colors[1] = face.color_b;
        colors[2] = face.color_c;
        uv[0][0] = face.uv.u1;
        uv[0][1] = face.uv.v1;
        uv[1][0] = face.uv.u2;
        uv[1][1] = face.uv.v2;
        uv[2][0] = face.uv.u3;
        uv[2][1] = face.uv.v3;
        texture_config = d3d9_widget_model_face_texture(renderer, &face, uv);
        if( texture_config == D3D9_WIDGET_CONFIG_SKIP ||
            (texture_config == D3D9_WIDGET_CONFIG_ATLAS && !atlas_ready) )
            continue;
        for( corner = 0; corner < 3; corner++ )
        {
            int channel;
            int vertex_index = indices[corner];
            input[corner].cx = (float)renderer->scene->orthographic_vertices_x[vertex_index];
            input[corner].cy = (float)renderer->scene->orthographic_vertices_y[vertex_index];
            input[corner].cz = (float)renderer->scene->orthographic_vertices_z[vertex_index];
            for( channel = 0; channel < 4; channel++ )
                input[corner].color[channel] = colors[corner][channel];
            input[corner].u = uv[corner][0];
            input[corner].v = uv[corner][1];
        }
        if( transform.orthographic )
        {
            clipped[0] = input[0];
            clipped[1] = input[1];
            clipped[2] = input[2];
            clipped_count = 3;
        }
        else
            clipped_count = d3d9_widget_model_clip_near(input, clipped);
        for( triangle = 1; triangle + 1 < clipped_count; triangle++ )
        {
            const int polygon_indices[3] = { 0, triangle, triangle + 1 };
            if( current_config != texture_config ||
                pending_vertices + 3u > D3D9_UI_BATCH_MAX_VERTS )
            {
                d3d9_widget_model_flush_vertices(
                    renderer, current_config, pending_vertices);
                pending_vertices = 0u;
                current_config = texture_config;
            }
            for( corner = 0; corner < 3; corner++ )
                d3d9_widget_model_overlay_vertex(
                    renderer,
                    &transform,
                    origin_x,
                    origin_y,
                    &clipped[polygon_indices[corner]],
                    &renderer->ui_batch.vertices[pending_vertices++]);
        }
    }
    d3d9_widget_model_flush_vertices(renderer, current_config, pending_vertices);
    d3d9_ui_set_states(renderer);
    d3d9_ui_batch_reset(renderer);
}

static void
d3d9_decode_texture_rgba(
    const struct ToriDraw_Texture* texture,
    uint32_t tile_size,
    uint8_t* rgba)
{
    uint32_t y;
    memset(rgba, 0, (size_t)tile_size * tile_size * 4u);
    assert(texture);
    if( !texture->texels || texture->width <= 0 || texture->height <= 0 )
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
    TORIRS_PERF_COUNT(
        TORIRS_PERF_CTR_D3D9_ANIMATED_TEXTURE_UPLOAD_BYTES,
        (int64_t)((uint64_t)width * height * 4u));
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_ANIMATED_TEXTURE_UPLOADS, 1);
    return true;
}

static bool
d3d9_upload_atlas(struct ToriRS_D3D9* renderer)
{
    D3DLOCKED_RECT locked;
    struct TRSPK_AtlasDirtyRect dirty;
    RECT lock_rect;
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
        /* Async model baking may already reference reserved-but-not-loaded
         * slots. Initialize their GPU pixels from the CPU atlas's zeros once;
         * later writes stay bounded to the merged dirty rectangle. */
        trspk_atlas_set_dirty(&renderer->atlas);
    }
    if( !trspk_atlas_get_dirty_rect(&renderer->atlas, &dirty) )
        return true;
    lock_rect.left = (LONG)dirty.x;
    lock_rect.top = (LONG)dirty.y;
    lock_rect.right = (LONG)(dirty.x + dirty.w);
    lock_rect.bottom = (LONG)(dirty.y + dirty.h);
    hr = IDirect3DTexture9_LockRect(
        renderer->atlas_texture, 0u, &locked, &lock_rect, 0u);
    if( FAILED(hr) )
    {
        d3d9_log_hr("LockRect(world atlas)", hr);
        return false;
    }
    for( y = 0u; y < dirty.h; y++ )
    {
        const uint8_t* src = renderer->atlas.pixels +
            (size_t)(dirty.y + y) * renderer->atlas.stride + (size_t)dirty.x * 4u;
        uint8_t* dst = (uint8_t*)locked.pBits + (size_t)y * locked.Pitch;
        uint32_t x;
        for( x = 0u; x < dirty.w; x++, src += 4, dst += 4 )
        {
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            dst[3] = src[3];
        }
    }
    IDirect3DTexture9_UnlockRect(renderer->atlas_texture, 0u);
    TORIRS_PERF_COUNT(
        TORIRS_PERF_CTR_D3D9_WORLD_ATLAS_UPLOAD_BYTES,
        (int64_t)((uint64_t)dirty.w * dirty.h * 4u));
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_WORLD_ATLAS_UPLOADS, 1);
    trspk_atlas_clear_dirty(&renderer->atlas);
    return true;
}

/* UI sprites are stored in the byte layout D3DFMT_A8R8G8B8 expects on
 * little-endian Windows (B,G,R,A).  Unlike the old compatibility canvas this
 * texture is retained: only the rectangle touched by a newly-seen sprite or
 * transformed variant is locked and copied. */
static bool
d3d9_upload_ui_atlas(struct ToriRS_D3D9* renderer)
{
    struct TRSPK_AtlasDirtyRect dirty;
    D3DLOCKED_RECT locked;
    RECT lock_rect;
    HRESULT hr;
    uint32_t y;
    if( !renderer || !renderer->device ||
        !trspk_atlas_is_initialized(&renderer->ui_sprite_atlas) ||
        !renderer->ui_sprite_atlas.pixels )
        return false;
    if( !renderer->ui_sprite_atlas_texture )
    {
        hr = IDirect3DDevice9_CreateTexture(
            renderer->device,
            D3D9_UI_ATLAS_DIM,
            D3D9_UI_ATLAS_DIM,
            1u,
            0u,
            D3DFMT_A8R8G8B8,
            D3DPOOL_MANAGED,
            &renderer->ui_sprite_atlas_texture,
            NULL);
        if( FAILED(hr) )
        {
            d3d9_log_hr("CreateTexture(UI sprite atlas)", hr);
            return false;
        }
        hr = IDirect3DTexture9_LockRect(
            renderer->ui_sprite_atlas_texture, 0u, &locked, NULL, 0u);
        if( FAILED(hr) )
        {
            d3d9_log_hr("LockRect(clear UI sprite atlas)", hr);
            IDirect3DTexture9_Release(renderer->ui_sprite_atlas_texture);
            renderer->ui_sprite_atlas_texture = NULL;
            return false;
        }
        memset(locked.pBits, 0, (size_t)locked.Pitch * D3D9_UI_ATLAS_DIM);
        IDirect3DTexture9_UnlockRect(renderer->ui_sprite_atlas_texture, 0u);
    }
    if( !trspk_atlas_get_dirty_rect(&renderer->ui_sprite_atlas, &dirty) )
        return true;
    lock_rect.left = (LONG)dirty.x;
    lock_rect.top = (LONG)dirty.y;
    lock_rect.right = (LONG)(dirty.x + dirty.w);
    lock_rect.bottom = (LONG)(dirty.y + dirty.h);
    hr = IDirect3DTexture9_LockRect(
        renderer->ui_sprite_atlas_texture, 0u, &locked, &lock_rect, 0u);
    if( FAILED(hr) )
    {
        d3d9_log_hr("LockRect(UI sprite atlas)", hr);
        return false;
    }
    for( y = 0u; y < dirty.h; y++ )
    {
        const uint8_t* src = renderer->ui_sprite_atlas.pixels +
            (size_t)(dirty.y + y) * renderer->ui_sprite_atlas.stride +
            (size_t)dirty.x * 4u;
        uint8_t* dst = (uint8_t*)locked.pBits + (size_t)y * locked.Pitch;
        memcpy(dst, src, (size_t)dirty.w * 4u);
    }
    IDirect3DTexture9_UnlockRect(renderer->ui_sprite_atlas_texture, 0u);
    renderer->ui_texture_upload_bytes += (uint64_t)dirty.w * dirty.h * 4u;
    renderer->ui_texture_upload_count++;
    TORIRS_PERF_COUNT(
        TORIRS_PERF_CTR_D3D9_UI_TEXTURE_UPLOAD_BYTES,
        (int64_t)((uint64_t)dirty.w * dirty.h * 4u));
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_UI_TEXTURE_UPLOADS, 1);
    trspk_atlas_clear_dirty(&renderer->ui_sprite_atlas);
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

static void
d3d9_reclassify_texture_faces(
    struct ToriRS_D3D9* renderer,
    int tex_id,
    bool animated);

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
    {
        bool loaded = d3d9_upload_rgba_texture(
            renderer,
            &renderer->animated_textures[tex_id],
            rgba,
            TRSPK_ATLAS_TILE,
            TRSPK_ATLAS_TILE);
        if( loaded )
            d3d9_reclassify_texture_faces(renderer, tex_id, true);
        return loaded;
    }
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
        d3d9_reclassify_texture_faces(renderer, tex_id, false);
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
    bool was_animated = false;
    if( tex_id < 0 || tex_id >= TORIDRAW_TEXTURE_ID_CAPACITY )
        return;
    if( renderer->animated_textures[tex_id] )
    {
        was_animated = true;
        IDirect3DTexture9_Release(renderer->animated_textures[tex_id]);
        renderer->animated_textures[tex_id] = NULL;
    }
    /* Keep a missing animated texture invisible.  Its retained faces cannot
     * stay on a now-unbound standalone configuration (which would render as
     * diffuse-only); remap them to a reserved transparent atlas tile until a
     * subsequent load promotes them again. */
    if( was_animated )
        d3d9_reclassify_texture_faces(renderer, tex_id, false);
    slot = renderer->tex_slot_of_id[tex_id];
    if( slot >= 0 && (uint32_t)slot < D3D9_ATLAS_SLOTS && renderer->atlas.pixels )
    {
        struct TRSPK_AtlasTile tile;
        if( trspk_atlas_grid_tile_for_slot(&renderer->atlas, (uint32_t)slot, &tile) )
        {
            (void)trspk_atlas_clear_rect(
                &renderer->atlas, tile.x, tile.y, tile.w, tile.h);
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
d3d9_reclassify_face_range(
    struct TRSPK_VBO* vbo,
    struct TRSPK_Triangles* triangles,
    uint32_t vertex_base,
    uint32_t vertex_count,
    int tex_id,
    bool animated,
    int slot,
    float cell,
    float origin_u,
    float origin_v)
{
    bool changed = false;
    uint32_t vertex_offset;

    if( !vbo || vbo->format != TRSPK_VERTEX_FORMAT_D3D9 ||
        !vbo->vertices.as_d3d9 || !triangles || !triangles->config ||
        vertex_count % 3u != 0u || vertex_base > vbo->vertex_count ||
        vertex_count > vbo->vertex_count - vertex_base )
        return false;

    for( vertex_offset = 0u; vertex_offset < vertex_count; vertex_offset += 3u )
    {
        uint32_t vertex_index = vertex_base + vertex_offset;
        uint32_t triangle_index =
            trspk_triangles_index_from_vertex(vertex_index);
        struct TRSPK_VertexD3D9* first =
            &vbo->vertices.as_d3d9[vertex_index];
        int baked_tex_id;
        int config;
        uint32_t corner;

        if( triangle_index >= triangles->cap || first->texdata[0] < 0.0f )
            continue;
        baked_tex_id = (int)(first->texdata[0] + 0.5f);
        if( baked_tex_id != tex_id )
            continue;
        config = trspk_triangles_get(triangles, triangle_index);

        if( animated )
        {
            if( config != TRSPK_TRIANGLES_ATLAS )
                continue;
            for( corner = 0u; corner < 3u; corner++ )
            {
                struct TRSPK_VertexD3D9* vertex =
                    &vbo->vertices.as_d3d9[vertex_index + corner];
                vertex->texcoord[0] = (vertex->texcoord[0] - origin_u) / cell;
                vertex->texcoord[1] = (vertex->texcoord[1] - origin_v) / cell;
                d3d9_map_animated_uv(&vertex->texcoord[0], &vertex->texcoord[1]);
            }
            trspk_triangles_set(triangles, triangle_index, tex_id);
        }
        else
        {
            if( config != tex_id )
                continue;
            for( corner = 0u; corner < 3u; corner++ )
            {
                struct TRSPK_VertexD3D9* vertex =
                    &vbo->vertices.as_d3d9[vertex_index + corner];
                float mapped_u;
                float mapped_v;
                d3d9_map_atlas_uv(
                    slot,
                    vertex->texcoord[0],
                    vertex->texcoord[1],
                    &mapped_u,
                    &mapped_v);
                vertex->texcoord[0] = mapped_u;
                vertex->texcoord[1] = mapped_v;
            }
            trspk_triangles_set(
                triangles, triangle_index, TRSPK_TRIANGLES_ATLAS);
        }
        changed = true;
    }
    return changed;
}

/* Geometry can be retained before its asynchronously requested texture is in
 * the scene map.  Unknown textures are initially mapped to their reserved
 * atlas tile.  If the eventual texture is animated, promote every already
 * baked face to the standalone texture configuration and recover its local UV
 * from that tile.  The reverse path handles a texture replacement whose class
 * changes from animated to static. */
static void
d3d9_reclassify_texture_faces(
    struct ToriRS_D3D9* renderer,
    int tex_id,
    bool animated)
{
    const float cell = (float)TRSPK_ATLAS_TILE / (float)D3D9_ATLAS_DIM;
    int slot;
    float origin_u;
    float origin_v;
    uint32_t group_index;

    if( tex_id < 0 || tex_id >= TORIDRAW_TEXTURE_ID_CAPACITY )
        return;
    assert(renderer);

    slot = renderer->tex_slot_of_id[tex_id];
    if( !animated && slot < 0 )
        slot = d3d9_texture_slot(renderer, tex_id);
    if( slot < 0 )
        return;

    origin_u = (float)(slot % (int)D3D9_ATLAS_COLS) * cell;
    origin_v = (float)(slot / (int)D3D9_ATLAS_COLS) * cell;

    for( group_index = 0u; group_index < TRSPK_VBO_GROUP_COUNT; group_index++ )
    {
        struct D3D9ModelGroup* group = &renderer->groups[group_index];
        struct TRSPK_ModelArena* arena = group->arena;
        struct TRSPK_VBO* vbo = group->vbo_cpu;
        bool changed = false;
        uint32_t slot_index;

        if( !arena || !vbo || !vbo->vertices.as_d3d9 )
            continue;

        for( slot_index = 0u; slot_index < arena->slot_count; slot_index++ )
        {
            const struct TRSPK_ModelSlot* model_slot = &arena->slots[slot_index];
            if( !trspk_modelslot_is_alive(model_slot) )
                continue;
            changed = d3d9_reclassify_face_range(
                          vbo,
                          &group->triangles,
                          model_slot->vertex_base,
                          model_slot->tri_count * 3u,
                          tex_id,
                          animated,
                          slot,
                          cell,
                          origin_u,
                          origin_v) ||
                changed;
        }

        if( changed )
            trspk_vbo_set_dirty(vbo);
    }

    /* Batch pages are scanned only when a texture changes class.  Normal
     * frames keep both their CPU geometry and managed D3D9 buffers untouched. */
    for( group_index = 0u; group_index < renderer->static_batch_count; group_index++ )
    {
        struct D3D9StaticBatch* batch = &renderer->static_batches[group_index];
        uint32_t chunk_count;
        uint32_t chunk_index;
        if( !batch->cpu || (!batch->active && !batch->building) )
            continue;
        chunk_count = trspk_batch16_chunk_count(batch->cpu);
        for( chunk_index = 0u; chunk_index < chunk_count; chunk_index++ )
        {
            struct TRSPK_Batch16Chunk* chunk =
                trspk_batch16_get_chunk(batch->cpu, chunk_index);
            if( chunk && d3d9_reclassify_face_range(
                    chunk->vbo,
                    &chunk->triangles,
                    0u,
                    chunk->vertex_count,
                    tex_id,
                    animated,
                    slot,
                    cell,
                    origin_u,
                    origin_v) )
            {
                trspk_vbo_set_dirty(chunk->vbo);
                renderer->static_batch_upload_pending = true;
            }
        }
    }
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
            D3DUSAGE_WRITEONLY |
                (group->reset_each_frame ? D3DUSAGE_DYNAMIC : 0u),
            0u,
            group->reset_each_frame ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED,
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
    hr = IDirect3DVertexBuffer9_Lock(
        group->vbo_gpu,
        0u,
        byte_count,
        &locked,
        group->reset_each_frame ? D3DLOCK_DISCARD : 0u);
    if( FAILED(hr) )
    {
        d3d9_log_hr("Lock(vertex buffer)", hr);
        return false;
    }
    memcpy(locked, group->vbo_cpu->vertices.as_d3d9, byte_count);
    IDirect3DVertexBuffer9_Unlock(group->vbo_gpu);
    if( group->reset_each_frame )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_DYNAMIC_VBO_UPLOAD_BYTES, byte_count);
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_DYNAMIC_VBO_UPLOADS, 1);
    }
    else
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_STATIC_VBO_UPLOAD_BYTES, byte_count);
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_STATIC_VBO_UPLOADS, 1);
    }
    trspk_vbo_clear_dirty(group->vbo_cpu);
    return true;
}

static bool
d3d9_grow_static_batches(struct ToriRS_D3D9* renderer, uint32_t needed)
{
    struct D3D9StaticBatch* grown;
    uint32_t capacity;
    if( needed <= renderer->static_batch_capacity )
        return true;
    capacity = renderer->static_batch_capacity ? renderer->static_batch_capacity : 8u;
    while( capacity < needed )
    {
        if( capacity > UINT32_MAX / 2u )
            return false;
        capacity *= 2u;
    }
    grown = (struct D3D9StaticBatch*)realloc(
        renderer->static_batches, (size_t)capacity * sizeof(*grown));
    if( !grown )
        return false;
    memset(
        grown + renderer->static_batch_capacity,
        0,
        (size_t)(capacity - renderer->static_batch_capacity) * sizeof(*grown));
    renderer->static_batches = grown;
    renderer->static_batch_capacity = capacity;
    return true;
}

static int
d3d9_static_batch_slot(
    struct ToriRS_D3D9* renderer,
    int batch_id,
    bool create)
{
    uint32_t i;
    uint32_t reusable = UINT32_MAX;
    if( batch_id < 0 )
        return -1;
    assert(renderer);
    for( i = 0u; i < renderer->static_batch_count; i++ )
    {
        if( renderer->static_batches[i].batch_id == batch_id )
            return (int)i;
        if( reusable == UINT32_MAX && !renderer->static_batches[i].active &&
            !renderer->static_batches[i].building &&
            trspk_batch16_entry_count(renderer->static_batches[i].cpu) == 0u )
            reusable = i;
    }
    if( create && reusable != UINT32_MAX )
    {
        renderer->static_batches[reusable].batch_id = batch_id;
        return (int)reusable;
    }
    if( !create || !d3d9_grow_static_batches(
            renderer, renderer->static_batch_count + 1u) )
        return -1;
    i = renderer->static_batch_count++;
    renderer->static_batches[i].batch_id = batch_id;
    renderer->static_batches[i].cpu =
        trspk_batch16_create(TRSPK_VERTEX_FORMAT_D3D9);
    if( !renderer->static_batches[i].cpu )
    {
        renderer->static_batch_count--;
        memset(&renderer->static_batches[i], 0, sizeof(renderer->static_batches[i]));
        return -1;
    }
    return (int)i;
}

static void
d3d9_rebuild_batch_pose_table(struct ToriRS_D3D9* renderer)
{
    uint32_t batch_slot;
    assert(renderer);
    trspk_pose_table_clear(&renderer->batch_poses);
    for( batch_slot = 0u; batch_slot < renderer->static_batch_count; batch_slot++ )
    {
        const struct D3D9StaticBatch* batch = &renderer->static_batches[batch_slot];
        uint32_t entry_count;
        uint32_t i;
        if( !batch->active || !batch->cpu )
            continue;
        entry_count = trspk_batch16_entry_count(batch->cpu);
        for( i = 0u; i < entry_count; i++ )
        {
            const struct TRSPK_Batch16Entry* entry =
                trspk_batch16_get_entry(batch->cpu, i);
            uint32_t page_id;
            uint32_t locator;
            if( !entry || entry->element_id < 0 ||
                entry->chunk_index >= batch->page_id_capacity ||
                entry->vertex_base > D3D9_BATCH_POSE_BASE_MASK )
                continue;
            page_id = batch->page_ids[entry->chunk_index];
            if( page_id > D3D9_BATCH_POSE_PAGE_MASK ||
                page_id >= renderer->static_page_count ||
                !renderer->static_pages[page_id].valid )
                continue;
            locator = D3D9_BATCH_POSE_FLAG | (page_id << 16u) |
                entry->vertex_base;
            trspk_pose_table_set(
                &renderer->batch_poses,
                entry->element_id,
                entry->anim_index,
                entry->pose_id,
                locator);
        }
    }
}

static bool
d3d9_grow_static_pages(struct ToriRS_D3D9* renderer, uint32_t needed)
{
    struct D3D9StaticPageRef* grown;
    uint32_t capacity;
    if( needed <= renderer->static_page_capacity )
        return true;
    if( needed > D3D9_BATCH_PAGE_LIMIT )
        return false;
    capacity = renderer->static_page_capacity ? renderer->static_page_capacity : 32u;
    while( capacity < needed )
    {
        if( capacity >= D3D9_BATCH_PAGE_LIMIT / 2u )
        {
            capacity = D3D9_BATCH_PAGE_LIMIT;
            break;
        }
        capacity *= 2u;
    }
    grown = (struct D3D9StaticPageRef*)realloc(
        renderer->static_pages, (size_t)capacity * sizeof(*grown));
    if( !grown )
        return false;
    memset(
        grown + renderer->static_page_capacity,
        0,
        (size_t)(capacity - renderer->static_page_capacity) * sizeof(*grown));
    renderer->static_pages = grown;
    renderer->static_page_capacity = capacity;
    return true;
}

static bool
d3d9_static_batch_ensure_chunk_storage(
    struct ToriRS_D3D9* renderer,
    uint32_t batch_slot,
    uint32_t chunk_count)
{
    struct D3D9StaticBatch* batch;
    uint32_t* grown_ids;
    uint32_t old_capacity;
    uint32_t capacity;
    uint32_t i;
    assert(renderer);
    if( batch_slot >= renderer->static_batch_count )
        return false;
    batch = &renderer->static_batches[batch_slot];
    if( chunk_count <= batch->page_id_capacity )
        return true;
    old_capacity = batch->page_id_capacity;
    capacity = batch->page_id_capacity;
    if( capacity == 0u )
        capacity = 4u;
    while( capacity < chunk_count )
    {
        if( capacity > UINT32_MAX / 2u )
            return false;
        capacity *= 2u;
    }
    grown_ids = (uint32_t*)realloc(
        batch->page_ids, (size_t)capacity * sizeof(*grown_ids));
    if( !grown_ids )
        return false;
    batch->page_ids = grown_ids;
    for( i = old_capacity; i < capacity; i++ )
    {
        batch->page_ids[i] = UINT32_MAX;
    }
    batch->page_id_capacity = capacity;
    return true;
}

static bool
d3d9_static_batch_assign_page(
    struct ToriRS_D3D9* renderer,
    uint32_t batch_slot,
    uint32_t chunk_index)
{
    struct D3D9StaticBatch* batch;
    uint32_t page_id;
    assert(renderer);
    if( batch_slot >= renderer->static_batch_count )
        return false;
    batch = &renderer->static_batches[batch_slot];
    if( chunk_index >= batch->page_id_capacity )
        return false;
    page_id = batch->page_ids[chunk_index];
    if( page_id == UINT32_MAX )
    {
        if( renderer->static_page_count >= D3D9_BATCH_PAGE_LIMIT ||
            !d3d9_grow_static_pages(renderer, renderer->static_page_count + 1u) )
            return false;
        page_id = renderer->static_page_count++;
        batch->page_ids[chunk_index] = page_id;
    }
    renderer->static_pages[page_id].batch_slot = batch_slot;
    renderer->static_pages[page_id].chunk_index = chunk_index;
    renderer->static_pages[page_id].valid = true;
    return true;
}

static bool
d3d9_ensure_static_batch_vbo(
    struct ToriRS_D3D9* renderer,
    uint32_t required_pages,
    bool* out_recreated)
{
    IDirect3DVertexBuffer9* replacement = NULL;
    uint32_t capacity;
    uint64_t vertex_capacity;
    uint64_t byte_capacity;
    HRESULT hr;
    if( out_recreated )
        *out_recreated = false;
    if( required_pages == 0u )
        return renderer != NULL;
    assert(renderer);
    if( renderer->static_batch_vbo &&
        renderer->static_batch_gpu_page_capacity >= required_pages )
        return true;
    if( !renderer->device )
        return true;

    capacity = renderer->static_batch_gpu_page_capacity
        ? renderer->static_batch_gpu_page_capacity
        : 4u;
    while( capacity < required_pages )
    {
        if( capacity > D3D9_BATCH_PAGE_LIMIT / 2u )
        {
            capacity = D3D9_BATCH_PAGE_LIMIT;
            break;
        }
        capacity *= 2u;
    }
    vertex_capacity = (uint64_t)capacity * D3D9_VBO_PAGE;
    byte_capacity = vertex_capacity * sizeof(struct TRSPK_VertexD3D9);
    if( vertex_capacity > INT_MAX || byte_capacity > UINT_MAX )
        return false;
    hr = IDirect3DDevice9_CreateVertexBuffer(
        renderer->device,
        (UINT)byte_capacity,
        D3DUSAGE_WRITEONLY,
        0u,
        D3DPOOL_MANAGED,
        &replacement,
        NULL);
    if( FAILED(hr) )
    {
        d3d9_log_hr("CreateVertexBuffer(static batch arena)", hr);
        return false;
    }
    if( renderer->device )
        IDirect3DDevice9_SetStreamSource(renderer->device, 0u, NULL, 0u, 0u);
    if( renderer->static_batch_vbo )
        IDirect3DVertexBuffer9_Release(renderer->static_batch_vbo);
    renderer->static_batch_vbo = replacement;
    renderer->static_batch_gpu_page_capacity = capacity;
    if( out_recreated )
        *out_recreated = true;
    return true;
}

static bool
d3d9_upload_static_batch_chunk(
    struct ToriRS_D3D9* renderer,
    struct D3D9StaticBatch* batch,
    uint32_t chunk_index)
{
    struct TRSPK_Batch16Chunk* chunk;
    uint32_t page_id;
    UINT byte_offset;
    uint32_t vertex_count;
    UINT byte_count;
    void* locked = NULL;
    HRESULT hr;
    if( !renderer || !batch || !batch->cpu ||
        chunk_index >= batch->page_id_capacity )
        return false;
    chunk = trspk_batch16_get_chunk(batch->cpu, chunk_index);
    if( !chunk || !chunk->vbo )
        return false;
    vertex_count = chunk->vertex_count;
    if( vertex_count == 0u )
    {
        trspk_vbo_clear_dirty(chunk->vbo);
        return true;
    }
    if( !renderer->device )
        return true;
    if( !renderer->static_batch_vbo )
        return false;
    if( !trspk_vbo_is_dirty(chunk->vbo) )
        return true;
    if( chunk_index >= batch->page_id_capacity )
        return false;
    page_id = batch->page_ids[chunk_index];
    if( page_id == UINT32_MAX || page_id >= renderer->static_page_count ||
        page_id >= renderer->static_batch_gpu_page_capacity )
        return false;
    byte_offset = (UINT)((uint64_t)page_id * D3D9_VBO_PAGE *
        sizeof(struct TRSPK_VertexD3D9));
    byte_count = (UINT)(vertex_count * sizeof(struct TRSPK_VertexD3D9));
    hr = IDirect3DVertexBuffer9_Lock(
        renderer->static_batch_vbo,
        byte_offset,
        byte_count,
        &locked,
        0u);
    if( FAILED(hr) )
    {
        d3d9_log_hr("Lock(static batch vertex buffer)", hr);
        return false;
    }
    memcpy(locked, chunk->vbo->vertices.as_d3d9, byte_count);
    hr = IDirect3DVertexBuffer9_Unlock(renderer->static_batch_vbo);
    if( FAILED(hr) )
    {
        d3d9_log_hr("Unlock(static batch vertex buffer)", hr);
        return false;
    }
    trspk_vbo_clear_dirty(chunk->vbo);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_STATIC_VBO_UPLOAD_BYTES, byte_count);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_STATIC_VBO_UPLOADS, 1);
    return true;
}

static bool
d3d9_upload_dirty_static_batches(struct ToriRS_D3D9* renderer);

static void
d3d9_mark_active_static_batches_dirty(struct ToriRS_D3D9* renderer)
{
    uint32_t batch_slot;
    assert(renderer);
    for( batch_slot = 0u; batch_slot < renderer->static_batch_count; batch_slot++ )
    {
        struct D3D9StaticBatch* batch = &renderer->static_batches[batch_slot];
        uint32_t chunk_count;
        uint32_t chunk_index;
        if( !batch->active || !batch->cpu )
            continue;
        chunk_count = trspk_batch16_chunk_count(batch->cpu);
        for( chunk_index = 0u; chunk_index < chunk_count; chunk_index++ )
        {
            struct TRSPK_Batch16Chunk* chunk =
                trspk_batch16_get_chunk(batch->cpu, chunk_index);
            if( chunk && chunk->vbo )
                trspk_vbo_set_dirty(chunk->vbo);
        }
    }
    renderer->static_batch_upload_pending = true;
}

static bool
d3d9_static_batch_commit(struct ToriRS_D3D9* renderer, uint32_t batch_slot)
{
    struct D3D9StaticBatch* batch;
    uint32_t chunk_count;
    uint32_t i;
    bool recreated = false;
    assert(renderer);
    if( batch_slot >= renderer->static_batch_count )
        return false;
    batch = &renderer->static_batches[batch_slot];
    if( !batch->cpu )
        return false;
    batch->active = false;
    chunk_count = trspk_batch16_chunk_count(batch->cpu);
    if( !d3d9_static_batch_ensure_chunk_storage(
            renderer, batch_slot, chunk_count) )
        return false;
    for( i = 0u; i < batch->page_id_capacity; i++ )
    {
        uint32_t page_id = batch->page_ids[i];
        if( page_id != UINT32_MAX && page_id < renderer->static_page_count )
            renderer->static_pages[page_id].valid = false;
    }
    for( i = 0u; i < chunk_count; i++ )
        if( !d3d9_static_batch_assign_page(renderer, batch_slot, i) )
            goto fail;
    if( !d3d9_ensure_static_batch_vbo(
            renderer, renderer->static_page_count, &recreated) )
        goto fail;
    if( recreated )
    {
        d3d9_mark_active_static_batches_dirty(renderer);
        if( !d3d9_upload_dirty_static_batches(renderer) )
            goto fail;
    }
    for( i = 0u; i < chunk_count; i++ )
        if( !d3d9_upload_static_batch_chunk(renderer, batch, i) )
            goto fail;
    batch->active = true;
    d3d9_rebuild_batch_pose_table(renderer);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_STATIC_BATCH_BUILDS, 1);
    return true;

fail:
    for( i = 0u; i < batch->page_id_capacity; i++ )
    {
        uint32_t page_id = batch->page_ids[i];
        if( page_id != UINT32_MAX && page_id < renderer->static_page_count )
            renderer->static_pages[page_id].valid = false;
    }
    d3d9_rebuild_batch_pose_table(renderer);
    return false;
}

static bool
d3d9_upload_dirty_static_batches(struct ToriRS_D3D9* renderer)
{
    uint32_t batch_slot;
    assert(renderer);
    if( !renderer->static_batch_upload_pending )
        return true;
    for( batch_slot = 0u; batch_slot < renderer->static_batch_count; batch_slot++ )
    {
        struct D3D9StaticBatch* batch = &renderer->static_batches[batch_slot];
        uint32_t chunk_count;
        uint32_t chunk;
        if( !batch->active || !batch->cpu )
            continue;
        chunk_count = trspk_batch16_chunk_count(batch->cpu);
        for( chunk = 0u; chunk < chunk_count; chunk++ )
            if( !d3d9_upload_static_batch_chunk(renderer, batch, chunk) )
                return false;
    }
    renderer->static_batch_upload_pending = false;
    return true;
}

static bool
d3d9_resolve_static_page(
    struct ToriRS_D3D9* renderer,
    uint32_t page_id,
    struct D3D9StaticBatch** out_batch,
    struct TRSPK_Batch16Chunk** out_chunk,
    IDirect3DVertexBuffer9** out_vbo)
{
    const struct D3D9StaticPageRef* ref;
    struct D3D9StaticBatch* batch;
    struct TRSPK_Batch16Chunk* chunk;
    if( !renderer || page_id >= renderer->static_page_count ||
        !renderer->static_pages[page_id].valid )
        return false;
    ref = &renderer->static_pages[page_id];
    if( ref->batch_slot >= renderer->static_batch_count )
        return false;
    batch = &renderer->static_batches[ref->batch_slot];
    if( !batch->active || !batch->cpu ||
        ref->chunk_index >= trspk_batch16_chunk_count(batch->cpu) )
        return false;
    chunk = trspk_batch16_get_chunk(batch->cpu, ref->chunk_index);
    if( !chunk )
        return false;
    if( out_batch )
        *out_batch = batch;
    if( out_chunk )
        *out_chunk = chunk;
    if( out_vbo )
        *out_vbo = renderer->static_batch_vbo;
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

static void
d3d9_rebuild_static_pose_table(struct ToriRS_D3D9* renderer)
{
    struct TRSPK_ModelArena* arena;
    uint32_t slot_index;

    assert(renderer);
    arena = renderer->groups[TRSPK_VBO_GROUP_STATIC].arena;
    trspk_pose_table_clear(&renderer->poses);
    if( !arena )
        return;

    for( slot_index = 0u; slot_index < arena->slot_count; slot_index++ )
    {
        const struct TRSPK_ModelSlot* slot = &arena->slots[slot_index];
        int anim_index;
        int pose_id;
        if( !trspk_modelslot_is_alive(slot) || slot->element_id < 0 ||
            slot->pose_id < 0 )
            continue;
        anim_index = slot->pose_id % TRSPK_POSE_TRACK_COUNT;
        pose_id = slot->pose_id / TRSPK_POSE_TRACK_COUNT;
        trspk_pose_table_set(
            &renderer->poses,
            slot->element_id,
            anim_index,
            pose_id,
            slot->vertex_base);
    }
}

static void
d3d9_compact_static_group(struct ToriRS_D3D9* renderer)
{
    struct TRSPK_ModelArena* arena;
    struct TRSPK_ModelArenaGCResult result;
    assert(renderer);
    arena = renderer->groups[TRSPK_VBO_GROUP_STATIC].arena;
    if( !arena )
        return;
    result = trspk_modelarena_gc(arena);
    if( result.did_compact )
        d3d9_rebuild_static_pose_table(renderer);
}

static bool
d3d9_pose_element_is_retained(
    const struct ToriRS_D3D9* renderer,
    int element_id)
{
    uint32_t element_index;
    int track;
    if( element_id < 0 )
        return false;
    assert(renderer);
    element_index = (uint32_t)element_id;
    for( track = 0; track < TRSPK_POSE_TRACK_COUNT; track++ )
        if( renderer->poses.elements &&
            element_index < renderer->poses.element_count &&
            renderer->poses.elements[element_index].tracks[track].pose_count > 0u )
            return true;
    return false;
}

static bool
d3d9_pose_track_is_retained(
    const struct ToriRS_D3D9* renderer,
    int element_id,
    int anim_index)
{
    uint32_t element_index;
    if( !renderer || element_id < 0 || anim_index < 0 ||
        anim_index >= TRSPK_POSE_TRACK_COUNT )
        return false;
    element_index = (uint32_t)element_id;
    return renderer->poses.elements && element_index < renderer->poses.element_count &&
        renderer->poses.elements[element_index].tracks[anim_index].pose_count > 0u;
}

static bool
d3d9_bake_pose_vertices(
    struct ToriRS_D3D9* renderer,
    struct TRSPK_VBO* vbo,
    struct TRSPK_Triangles* triangles,
    uint32_t vertex_base,
    struct ToriDraw_ModelHandle model_handle,
    const struct ToriDraw_Position* world_position)
{
    int face_count;
    uint32_t face_index;

    if( !renderer || !renderer->scene || !vbo || !triangles )
        return false;
    face_count = trspk_toridraw_face_count(model_handle);
    if( face_count <= 0 )
        return false;

    for( face_index = 0u; face_index < (uint32_t)face_count; face_index++ )
    {
        struct TRSPK_ToriDrawBakeFaceVerts face;
        uint32_t vertex = vertex_base + face_index * 3u;
        float ua = 0.5f;
        float va = 0.5f;
        float ub = 0.5f;
        float vb = 0.5f;
        float uc = 0.5f;
        float vc = 0.5f;
        int triangle_config = TRSPK_TRIANGLES_ATLAS;
        int slot = 0;
        bool missing_texture_slot = false;

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
                /* Reserve and encode the final tile even when the texture is
                 * still loading.  The old fallback left retained vertices on
                 * white slot zero forever after the asynchronous upload filled
                 * a different tile. */
                int assigned = d3d9_texture_slot(renderer, face.tex_id);
                if( assigned >= 0 )
                {
                    slot = assigned;
                    (void)d3d9_ensure_static_texture(renderer, face.tex_id);
                }
                else
                    missing_texture_slot = true;
            }
            d3d9_map_atlas_uv(
                face.tex_id >= 0 ? slot : -1, face.uv.u1, face.uv.v1, &ua, &va);
            d3d9_map_atlas_uv(
                face.tex_id >= 0 ? slot : -1, face.uv.u2, face.uv.v2, &ub, &vb);
            d3d9_map_atlas_uv(
                face.tex_id >= 0 ? slot : -1, face.uv.u3, face.uv.v3, &uc, &vc);
        }

        if( missing_texture_slot )
        {
            /* Slot zero is intentionally opaque white for genuinely
             * untextured faces.  An invalid/full textured face must not use it
             * as a visible fallback. */
            face.color_a[3] = 0.0f;
            face.color_b[3] = 0.0f;
            face.color_c[3] = 0.0f;
        }

        trspk_triangles_set(
            triangles,
            trspk_triangles_index_from_vertex(vertex),
            triangle_config);
        trspk_vbo_write_vertex_d3d9(
            vbo,
            vertex,
            face.wx_a,
            face.wy_a,
            face.wz_a,
            face.color_a,
            ua,
            va,
            (float)face.tex_id);
        trspk_vbo_write_vertex_d3d9(
            vbo,
            vertex + 1u,
            face.wx_b,
            face.wy_b,
            face.wz_b,
            face.color_b,
            ub,
            vb,
            (float)face.tex_id);
        trspk_vbo_write_vertex_d3d9(
            vbo,
            vertex + 2u,
            face.wx_c,
            face.wy_c,
            face.wz_c,
            face.color_c,
            uc,
            vc,
            (float)face.tex_id);
    }

    return true;
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

    if( !renderer || !renderer->scene || !group || !group->arena || !group->vbo_cpu )
        return UINT32_MAX;
    face_count = trspk_toridraw_face_count(model_handle);
    if( face_count <= 0 || (uint32_t)face_count > UINT32_MAX / 3u )
        return UINT32_MAX;
    vertex_count = (uint32_t)face_count * 3u;
    if( vertex_count > TRSPK_BATCH16_MAX_VERTICES )
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
    if( pose_id > (INT_MAX - anim_index) / TRSPK_POSE_TRACK_COUNT )
        return UINT32_MAX;
    arena_element_id = element_id >= 0 ? element_id : 0;
    arena_pose_id = pose_id * TRSPK_POSE_TRACK_COUNT + anim_index;

    if( update_pose_table && element_id < 0 )
        update_pose_table = false;
    if( update_pose_table )
    {
        uint32_t old_slot =
            trspk_modelarena_find(group->arena, arena_element_id, arena_pose_id);
        if( old_slot != TRSPK_MODELSLOT_NULL_IDX )
        {
            trspk_modelarena_unload(group->arena, old_slot);
            if( group == &renderer->groups[TRSPK_VBO_GROUP_STATIC] )
                d3d9_compact_static_group(renderer);
        }
    }
    slot_index = trspk_modelarena_load(
        group->arena, arena_element_id, arena_pose_id, vertex_count);
    model_slot = trspk_modelarena_get(group->arena, slot_index);
    if( !model_slot || !d3d9_bake_pose_vertices(
            renderer,
            group->vbo_cpu,
            &group->triangles,
            model_slot->vertex_base,
            model_handle,
            world_position) )
        return UINT32_MAX;

    if( update_pose_table )
    {
        trspk_pose_table_set(
            &renderer->poses,
            element_id,
            anim_index,
            pose_id,
            model_slot->vertex_base);
        d3d9_zbuffer_pose_baked(
            renderer, element_id, anim_index, pose_id, model_handle);
    }
    return model_slot->vertex_base;
}

static void
d3d9_model_unload(struct ToriRS_D3D9* renderer, int element_id)
{
    /* Individual unloads own only the legacy arena. Batch geometry and its
     * pose map remain immutable until the matching batch rebuild/clear. */
    if( !renderer || element_id < 0 ||
        !renderer->groups[TRSPK_VBO_GROUP_STATIC].arena ||
        !d3d9_pose_element_is_retained(renderer, element_id) )
        return;
    trspk_modelarena_unload_element(
        renderer->groups[TRSPK_VBO_GROUP_STATIC].arena, element_id);
    trspk_pose_table_remove_element(&renderer->poses, element_id);
    d3d9_zbuffer_element_dropped(renderer, element_id);
    d3d9_compact_static_group(renderer);
}

static void
d3d9_animation_track_unload(
    struct ToriRS_D3D9* renderer,
    int element_id,
    int anim_index)
{
    struct TRSPK_ModelArena* arena;
    uint32_t slot_index;

    /* A pose-track unload likewise must not punch holes in a committed batch. */
    if( !renderer || element_id < 0 || anim_index < 0 ||
        anim_index >= TRSPK_POSE_TRACK_COUNT )
        return;
    arena = renderer->groups[TRSPK_VBO_GROUP_STATIC].arena;
    if( !arena || !d3d9_pose_track_is_retained(renderer, element_id, anim_index) )
        return;

    for( slot_index = 0u; slot_index < arena->slot_count; slot_index++ )
    {
        const struct TRSPK_ModelSlot* slot = &arena->slots[slot_index];
        if( trspk_modelslot_is_alive(slot) && slot->element_id == element_id &&
            slot->pose_id % TRSPK_POSE_TRACK_COUNT == anim_index )
            trspk_modelarena_unload(arena, slot_index);
    }
    trspk_pose_table_remove_track(&renderer->poses, element_id, anim_index);
    d3d9_zbuffer_track_dropped(renderer, element_id, anim_index);
    d3d9_compact_static_group(renderer);
}

static void
d3d9_model_load(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand_ModelLoad* command)
{
    assert(command);
    if( command->element_id < 0 || command->model.kind == TORIDRAWMK_NONE )
        return;
    /* A model replacement invalidates every pose from the old geometry, not
     * only the rest-pose slot. */
    d3d9_model_unload(renderer, command->element_id);
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
    struct ToriDraw_Animation* animation;
    struct ToriDraw_SkeletalAnim* skeletal;
    struct ToriDraw_Model* source;
    int anim_index;
    int frame;
    if( !command || command->element_id < 0 || !command->animation ||
        command->animation->frame_count <= 0 ||
        command->model.kind != TORIDRAWMK_MODEL || !command->model.u.model.model )
        return;
    animation = command->animation;
    skeletal = animation->skeletal;
    if( !skeletal && (!animation->base || !animation->frames) )
        return;

    anim_index = d3d9_clampi(command->anim_index, 0, TRSPK_POSE_TRACK_COUNT - 1);
    /* Pose keys do not carry a sequence id.  Clear the old track so frame zero
     * cannot keep resolving to MODEL_LOAD's rest pose and a shorter replacement
     * cannot serve stale tail frames. */
    d3d9_animation_track_unload(renderer, command->element_id, anim_index);
    source = command->model.u.model.model;
    for( frame = 0; frame < animation->frame_count; frame++ )
    {
        struct ToriDraw_Model* baked = ToriDraw_ModelCopy(source);
        struct ToriDraw_ModelHandle handle;
        if( !baked )
            continue;

        /* A lazy retained bake can run after the scene model was posed for a
         * previous draw.  ModelCopy copies the current vertices, not the
         * captured rest arrays, so explicitly seed the copy from the source's
         * rest pose when one exists. */
        if( source->original_vertices_x && source->original_vertices_y &&
            source->original_vertices_z && baked->vertex_count == source->vertex_count )
        {
            size_t vertex_bytes =
                (size_t)baked->vertex_count * sizeof(*baked->vertices_x);
            memcpy(baked->vertices_x, source->original_vertices_x, vertex_bytes);
            memcpy(baked->vertices_y, source->original_vertices_y, vertex_bytes);
            memcpy(baked->vertices_z, source->original_vertices_z, vertex_bytes);
        }
        if( baked->face_alphas && source->original_face_alphas &&
            baked->face_count == source->face_count )
            memcpy(
                baked->face_alphas,
                source->original_face_alphas,
                (size_t)baked->face_count * sizeof(*baked->face_alphas));

        ToriDraw_ModelCaptureOriginalVertices(baked);
        /* Missing/empty cache frames hold the rest pose.  Calling the classic
         * animator for one would assert instead of producing that pose. */
        if( skeletal )
        {
            int skeletal_frame = frame < skeletal->frame_count ? frame : 0;
            if( skeletal->frame_count > 0 && skeletal->matrices &&
                baked->animaya_vertex_count > 0 && baked->animaya_group_counts &&
                baked->animaya_groups && baked->animaya_scales )
                ToriDraw_ModelAnimateSkeletal(baked, skeletal, skeletal_frame);
        }
        else if( animation->frames[frame].length > 0 )
            ToriDraw_ModelAnimateFrame(
                baked, animation->base, &animation->frames[frame]);
        memset(&handle, 0, sizeof(handle));
        handle.kind = TORIDRAWMK_MODEL;
        handle.u.model.model = baked;
        (void)d3d9_bake_into_arena(
            renderer,
            &renderer->groups[TRSPK_VBO_GROUP_STATIC],
            command->element_id,
            anim_index,
            frame,
            handle,
            &command->world_position,
            true);
        ToriDraw_ModelFree(baked);
    }
}

bool
d3d9_reserve_model_indices(struct ToriRS_D3D9* renderer, uint32_t needed)
{
    uint16_t* grown;
    uint32_t capacity;
    size_t byte_count;
    assert(renderer);
    if( needed <= renderer->model_index_capacity )
        return true;
    capacity = renderer->model_index_capacity
        ? renderer->model_index_capacity
        : 256u;
    while( capacity < needed )
    {
        if( capacity > UINT32_MAX / 2u )
        {
            capacity = needed;
            break;
        }
        capacity *= 2u;
    }
    byte_count = (size_t)capacity * sizeof(*grown);
    if( capacity != 0u && byte_count / sizeof(*grown) != capacity )
        return false;
    grown = (uint16_t*)realloc(renderer->model_indices, byte_count);
    assert(grown);
    renderer->model_indices = grown;
    renderer->model_index_capacity = capacity;
    return true;
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

    if( !renderer->device )
        return;
    assert(command);
    if( renderer->ibo_chain )
        trspk_ibochain_reset(renderer->ibo_chain);
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
    if( renderer->zbuffer )
        d3d9_zbuffer_begin_pass(renderer);

    trspk_compute_pass_matrices(
        renderer->view,
        renderer->proj,
        (float)command->camera_position.x,
        (float)command->camera_position.y,
        (float)command->camera_position.z,
        ToriDraw_AngleToRadians(command->camera.pitch),
        ToriDraw_AngleToRadians(command->camera.yaw),
        pass_w,
        pass_h,
        /* See the note at the GL caller: this used to project at a hardcoded
         * 512 regardless of what the camera asked for. */
        (int)command->camera.proj_mode,
        command->camera.proj_scale,
        command->camera.fov_rpi2048,
        command->camera.parallel_zoom16);
    if( renderer->zbuffer )
        d3d9_zbuffer_setup_projection(renderer, command);
    else
        d3d9_painter_setup_projection(renderer);
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
    struct D3D9ModelPlacement placement;
    int face_count;
    int sorted_face_count = 0;
    bool dynamic;
    int anim_index;
    int pose_id;
    uint32_t vertex_base;
    uint32_t page_base = 0u;
    uint32_t local_base;
    uint32_t binding;
    uint32_t group;
    uint32_t index_count;

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
         : command->pick_terrain
             ? ToriDraw_ProjectedTileMouseHitTest(
                   renderer->scene,
                   command->model,
                   &renderer->cur_3d.view_port,
                   renderer->pick_mouse_x,
                   renderer->pick_mouse_y)
             : ToriDraw_ProjectedModelMouseHitTest(
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

    if( command->pick_only )
        return;
    /* The depth path classifies per face during emission and needs no order
     * up front; the painter path must sort before it can count. */
    face_count = renderer->zbuffer
                     ? trspk_toridraw_face_count(command->model)
                     : d3d9_painter_sort_faces(renderer, command, &sorted_face_count);
    if( face_count <= 0 )
        return;
    dynamic = command->dynamic || command->element_id < 0;
    anim_index = d3d9_clampi(command->anim_index, 0, TRSPK_POSE_TRACK_COUNT - 1);
    pose_id = command->animation && command->anim_frame >= 0
                  ? command->anim_frame
                  : 0;
    if( command->animation && command->animation->frame_count > 0 &&
        pose_id >= command->animation->frame_count )
        pose_id = 0;
    group = dynamic ? TRSPK_VBO_GROUP_DYNAMIC : TRSPK_VBO_GROUP_STATIC;
    binding = group;
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
    else if( trspk_pose_table_get(
                 &renderer->batch_poses,
                 command->element_id,
                 anim_index,
                 pose_id,
                 &vertex_base) )
    {
        uint32_t page_id;
        if( (vertex_base & D3D9_BATCH_POSE_FLAG) == 0u )
            return;
        page_id = (vertex_base >> 16u) & D3D9_BATCH_POSE_PAGE_MASK;
        if( page_id >= renderer->static_page_count ||
            !renderer->static_pages[page_id].valid )
            return;
        binding = D3D9_STATIC_PAGE_BINDING_BASE;
        page_base = page_id * D3D9_VBO_PAGE;
        vertex_base &= D3D9_BATCH_POSE_BASE_MASK;
    }
    else if( !trspk_pose_table_get(
                  &renderer->poses,
                  command->element_id,
                  anim_index,
                  pose_id,
                  &vertex_base) )
    {
        /* Renderer creation or an event-queue overflow can leave a live static
         * element without its load event.  Preserve the retained contract by
         * baking the complete animation once on the first miss, not one pose
         * every frame. */
        if( command->animation )
        {
            struct ToriRS_RenderCommand_AnimLoad load;
            memset(&load, 0, sizeof(load));
            load.element_id = command->element_id;
            load.anim_index = anim_index;
            load.animation = command->animation;
            load.model = command->model;
            load.world_position = command->world_position;
            d3d9_animation_load(renderer, &load);
        }
        if( !trspk_pose_table_get(
                &renderer->poses,
                command->element_id,
                anim_index,
                pose_id,
                &vertex_base) )
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

    if( binding < D3D9_STATIC_PAGE_BINDING_BASE )
    {
        page_base = vertex_base & ~(D3D9_VBO_PAGE - 1u);
        local_base = vertex_base - page_base;
    }
    else
        local_base = vertex_base;
    if( (uint32_t)face_count > UINT32_MAX / 3u )
        return;
    index_count = (uint32_t)face_count * 3u;
    if( !d3d9_reserve_model_indices(renderer, index_count) )
        return;

    placement.binding = binding;
    placement.page_base = page_base;
    placement.local_base = local_base;
    placement.face_count = face_count;
    placement.sorted_face_count = sorted_face_count;
    placement.anim_index = anim_index;
    placement.pose_id = pose_id;
    placement.dynamic = dynamic;
    if( renderer->zbuffer )
        d3d9_zbuffer_emit_model(renderer, command, &placement);
    else
        d3d9_painter_emit_model(renderer, &placement);
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

static bool
d3d9_measure_ibo_chain(
    const struct TRSPK_IBOChain* chain,
    uint32_t* out_indices,
    uint32_t* out_nodes,
    uint32_t* out_binding_switches)
{
    const struct TRSPK_IBOChainNode* node;
    uint64_t indices = 0u;
    uint32_t nodes = 0u;
    uint32_t switches = 0u;
    uint32_t previous_group = UINT32_MAX;
    uint32_t previous_offset = UINT32_MAX;
    if( !chain || !out_indices || !out_nodes || !out_binding_switches ||
        chain->index_format != TRSPK_INDEX_FORMAT_U16 )
        return false;
    for( node = chain->head; node; node = node->next )
    {
        if( node->ibo.index_format != TRSPK_INDEX_FORMAT_U16 ||
            node->ibo.index_count % 3u != 0u ||
            (node->ibo.index_count != 0u && !node->ibo.indices.as_u16) )
            return false;
        if( nodes != 0u &&
            (node->group != previous_group || node->ibo.offset != previous_offset) )
            switches++;
        indices += node->ibo.index_count;
        if( indices > UINT32_MAX || nodes == UINT32_MAX )
            return false;
        previous_group = node->group;
        previous_offset = node->ibo.offset;
        nodes++;
    }
    *out_indices = (uint32_t)indices;
    *out_nodes = nodes;
    *out_binding_switches = switches;
    return true;
}

static bool
d3d9_binding_cpu_source(
    struct ToriRS_D3D9* renderer,
    uint32_t binding,
    uint32_t base_offset,
    const struct TRSPK_VBO** out_vbo,
    const struct TRSPK_Triangles** out_triangles)
{
    assert(renderer);
    assert(out_vbo);
    assert(out_triangles);
    if( binding < TRSPK_VBO_GROUP_COUNT )
    {
        *out_vbo = renderer->groups[binding].vbo_cpu;
        *out_triangles = &renderer->groups[binding].triangles;
        return *out_vbo != NULL;
    }
    else
    {
        struct TRSPK_Batch16Chunk* chunk = NULL;
        uint32_t page_id;
        if( binding != D3D9_STATIC_PAGE_BINDING_BASE ||
            base_offset % D3D9_VBO_PAGE != 0u )
            return false;
        page_id = base_offset / D3D9_VBO_PAGE;
        if( !d3d9_resolve_static_page(
                renderer, page_id, NULL, &chunk, NULL) )
            return false;
        *out_vbo = chunk->vbo;
        *out_triangles = &chunk->triangles;
        return *out_vbo != NULL;
    }
}

static uint32_t
d3d9_build_draw_ranges16(
    struct ToriRS_D3D9* renderer,
    const struct TRSPK_IBOChain* chain,
    uint16_t* dst)
{
    const struct TRSPK_IBOChainNode* node;
    uint32_t gpu_cursor = 0u;
    assert(renderer);
    if( !renderer->draw_ranges )
        return UINT32_MAX;
    assert(chain);
    assert(dst);
    trspk_drawrangelist_clear(renderer->draw_ranges);

    for( node = chain->head; node; node = node->next )
    {
        const struct TRSPK_VBO* vbo;
        const struct TRSPK_Triangles* triangles;
        const uint16_t* src = node->ibo.indices.as_u16;
        uint32_t count = node->ibo.index_count;
        uint32_t absolute_offset = node->group < TRSPK_VBO_GROUP_COUNT
            ? node->ibo.offset
            : 0u;
        uint32_t i = 0u;
        if( !d3d9_binding_cpu_source(
                renderer, node->group, node->ibo.offset, &vbo, &triangles) )
            return UINT32_MAX;
        memcpy(dst + gpu_cursor, src, (size_t)count * sizeof(*dst));
        while( i < count )
        {
            uint32_t range_start = i;
            uint32_t absolute;
            uint32_t triangle;
            uint32_t config;
            uint32_t min_vertex;
            uint32_t max_vertex;
            if( i + 2u >= count || absolute_offset > UINT32_MAX - src[i] )
                return UINT32_MAX;
            absolute = absolute_offset + src[i];
            if( absolute + 2u >= vbo->vertex_count )
                return UINT32_MAX;
            triangle = trspk_triangles_index_from_vertex(absolute);
            if( !triangles->config || triangle >= triangles->cap )
                return UINT32_MAX;
            config = trspk_triangles_encode_draw_config(
                trspk_triangles_get(triangles, triangle));
            min_vertex = src[i];
            max_vertex = src[i];
            do
            {
                uint32_t corner;
                for( corner = 0u; corner < 3u; corner++ )
                {
                    uint32_t local = src[i + corner];
                    uint32_t resolved;
                    if( absolute_offset > UINT32_MAX - local )
                        return UINT32_MAX;
                    resolved = absolute_offset + local;
                    if( resolved >= vbo->vertex_count )
                        return UINT32_MAX;
                    if( local < min_vertex )
                        min_vertex = local;
                    if( local > max_vertex )
                        max_vertex = local;
                }
                i += 3u;
                if( i >= count )
                    break;
                absolute = absolute_offset + src[i];
                triangle = trspk_triangles_index_from_vertex(absolute);
                if( triangle >= triangles->cap ||
                    trspk_triangles_encode_draw_config(
                        trspk_triangles_get(triangles, triangle)) != config )
                    break;
            } while( true );
            if( !trspk_drawrangelist_push(
                    renderer->draw_ranges,
                    gpu_cursor + range_start,
                    gpu_cursor + i,
                    node->ibo.offset,
                    config,
                    node->group,
                    min_vertex,
                    max_vertex) )
                return UINT32_MAX;
        }
        gpu_cursor += count;
    }
    return gpu_cursor;
}

static void
d3d9_bind_world_config(struct ToriRS_D3D9* renderer, uint32_t config_idx)
{
    if( config_idx == 0u )
        d3d9_bind_atlas(renderer);
    else
    {
        int tex_id = (int)config_idx - 1;
        if( tex_id >= 0 && tex_id < TORIDRAW_TEXTURE_ID_CAPACITY &&
            renderer->animated_textures[tex_id] )
            d3d9_bind_animated(renderer, tex_id);
        else
        {
            d3d9_disable_texture_transform(renderer->device);
            d3d9_set_no_texture(renderer->device);
        }
    }
}

void
d3d9_draw_retained(
    struct ToriRS_D3D9* renderer,
    struct TRSPK_IBOChain* chain,
    bool blended_pass)
{
    uint32_t total_indices = 0u;
    uint32_t chain_nodes = 0u;
    uint32_t page_switches = 0u;
    uint32_t draw_calls = 0u;
    uint32_t texture_switches = 0u;
    uint32_t stream_switches = 0u;
    void* locked = NULL;
    uint32_t built;
    const struct TRSPK_DrawRange* range;
    uint32_t last_binding = UINT32_MAX;
    uint32_t last_config = UINT32_MAX;
    uint32_t group;

    for( group = 0u; group < TRSPK_VBO_GROUP_COUNT; group++ )
        if( !d3d9_upload_group(renderer, &renderer->groups[group]) )
            return;
    if( !d3d9_upload_dirty_static_batches(renderer) )
        return;
    if( !chain || !chain->head )
        return;
    if( !d3d9_measure_ibo_chain(
            chain,
            &total_indices,
            &chain_nodes,
            &page_switches) ||
        total_indices == 0u || !d3d9_ensure_ibo(renderer, total_indices) ||
        !d3d9_grow_draw_ranges(renderer, total_indices / 3u + 1u) )
        return;
    if( FAILED(IDirect3DIndexBuffer9_Lock(
            renderer->ibo,
            0u,
            (UINT)(total_indices * sizeof(uint16_t)),
            &locked,
            D3DLOCK_DISCARD)) )
        return;

    built = d3d9_build_draw_ranges16(renderer, chain, (uint16_t*)locked);
    IDirect3DIndexBuffer9_Unlock(renderer->ibo);
    TORIRS_PERF_COUNT(
        TORIRS_PERF_CTR_D3D9_IBO_UPLOAD_BYTES,
        (int64_t)((uint64_t)total_indices * sizeof(uint16_t)));
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_IBO_UPLOADS, 1);
    if( built != total_indices )
        return;
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_IBO_INDICES, total_indices);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_IBO_CHAIN_NODES, chain_nodes);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_DRAW_RANGES, renderer->draw_ranges->count);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_PAGE_SWITCHES, page_switches);

    d3d9_set_world_states(renderer);
    if( renderer->zbuffer )
        d3d9_zbuffer_apply_pass_states(renderer, blended_pass);
    IDirect3DDevice9_SetIndices(renderer->device, renderer->ibo);
    range = trspk_drawrangelist_head(renderer->draw_ranges);
    while( range )
    {
        uint32_t index_count = range->end - range->start;
        if( index_count >= 3u )
        {
            if( range->group != last_binding )
            {
                IDirect3DVertexBuffer9* vbo = NULL;
                if( range->group < TRSPK_VBO_GROUP_COUNT )
                    vbo = renderer->groups[range->group].vbo_gpu;
                else if( range->group == D3D9_STATIC_PAGE_BINDING_BASE )
                    vbo = renderer->static_batch_vbo;
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
                last_binding = range->group;
                stream_switches++;
            }
            if( range->config_idx != last_config )
            {
                d3d9_bind_world_config(renderer, range->config_idx);
                last_config = range->config_idx;
                texture_switches++;
            }
            {
                uint32_t first_index = range->start;
                uint32_t primitives_left = index_count / 3u;
                uint32_t primitive_cap = renderer->caps.MaxPrimitiveCount;
                if( primitive_cap == 0u )
                    primitive_cap = primitives_left;
                while( primitives_left > 0u )
                {
                    uint32_t primitive_count = primitives_left < primitive_cap
                        ? primitives_left
                        : primitive_cap;
                    IDirect3DDevice9_DrawIndexedPrimitive(
                        renderer->device,
                        D3DPT_TRIANGLELIST,
                        (INT)range->base_offset,
                        (UINT)range->min_vertex,
                        (UINT)(range->max_vertex - range->min_vertex + 1u),
                        (UINT)first_index,
                        (UINT)primitive_count);
                    draw_calls++;
                    first_index += primitive_count * 3u;
                    primitives_left -= primitive_count;
                }
            }
        }
        range = trspk_drawrangelist_next(renderer->draw_ranges, range);
    }
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_DRAW_CALLS, draw_calls);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_TEXTURE_SWITCHES, texture_switches);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_D3D9_STREAM_SWITCHES, stream_switches);
    d3d9_disable_texture_transform(renderer->device);
}

static void
d3d9_end_3d(struct ToriRS_D3D9* renderer)
{
    uint32_t active_pages = 0u;
    uint32_t page;
    if( !renderer->has_3d )
        goto done;
    if( trspk_atlas_is_dirty(&renderer->atlas) && !d3d9_upload_atlas(renderer) )
        goto done;
    if( renderer->ibo_chain && renderer->ibo_chain->head )
        d3d9_draw_retained(renderer, renderer->ibo_chain, false);
    if( renderer->zbuffer )
        d3d9_zbuffer_end_pass(renderer);

done:
    for( page = 0u; page < renderer->static_page_count; page++ )
        if( renderer->static_pages[page].valid )
            active_pages++;
    TORIRS_PERF_COUNT_SET(
        TORIRS_PERF_CTR_D3D9_STATIC_BATCH_ACTIVE_PAGES, active_pages);
    renderer->has_3d = false;
    renderer->in3d = false;
    if( renderer->ibo_chain )
        trspk_ibochain_reset(renderer->ibo_chain);
    if( renderer->zbuffer )
        d3d9_zbuffer_reset_pass(renderer);
    d3d9_set_full_viewport(renderer);
}

static void
d3d9_begin_2d(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand* command)
{
    (void)command;
    if( !renderer->scene || !renderer->ui_batch.vertices )
    {
        renderer->in2d = false;
        return;
    }
    d3d9_set_full_viewport(renderer);
    d3d9_ui_set_states(renderer);
    d3d9_ui_flush(renderer);
    d3d9_ui_batch_reset(renderer);
    renderer->in2d = true;
}

static void
d3d9_end_2d(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand* command)
{
    (void)command;
    if( !renderer->in2d )
        return;
    d3d9_ui_flush(renderer);
    IDirect3DDevice9_SetRenderState(renderer->device, D3DRS_SCISSORTESTENABLE, FALSE);
    renderer->in2d = false;
}

static void
d3d9_batch_begin(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand_Batch* command)
{
    struct D3D9StaticBatch* batch;
    uint32_t i;
    int slot;
    assert(command);
    if( command->batch_id < 0 )
        return;
    assert(renderer);
    slot = d3d9_static_batch_slot(renderer, command->batch_id, true);
    if( slot < 0 )
        return;
    batch = &renderer->static_batches[slot];
    d3d9_zbuffer_batch_dropped(renderer, batch->cpu);
    for( i = 0u; i < batch->page_id_capacity; i++ )
    {
        uint32_t page_id = batch->page_ids[i];
        if( page_id != UINT32_MAX && page_id < renderer->static_page_count )
            renderer->static_pages[page_id].valid = false;
    }
    trspk_batch16_begin(batch->cpu);
    batch->active = false;
    batch->building = true;
    d3d9_rebuild_batch_pose_table(renderer);
    renderer->current_batch_slot = slot;
}

static void
d3d9_batch_add(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand_Batch* command,
    bool animated)
{
    struct D3D9StaticBatch* batch;
    struct TRSPK_Batch16Reservation reservation;
    int anim_index;
    int pose_id;
    int face_count;
    uint32_t vertex_count;
    assert(command);
    if( command->element_id < 0 || command->model.kind == TORIDRAWMK_NONE )
        return;
    if( renderer->current_batch_slot < 0 ||
        (uint32_t)renderer->current_batch_slot >= renderer->static_batch_count )
        return;
    batch = &renderer->static_batches[renderer->current_batch_slot];
    if( !batch->building || batch->batch_id != command->batch_id )
        return;
    face_count = trspk_toridraw_face_count(command->model);
    if( face_count <= 0 || (uint32_t)face_count > UINT32_MAX / 3u )
        return;
    vertex_count = (uint32_t)face_count * 3u;
    anim_index = animated
        ? d3d9_clampi(command->anim_index, 0, TRSPK_POSE_TRACK_COUNT - 1)
        : 0;
    pose_id = command->pose_id >= 0 ? command->pose_id : 0;
    if( !trspk_batch16_reserve_pose(
            batch->cpu,
            command->element_id,
            anim_index,
            pose_id,
            vertex_count,
            &reservation) )
        return;
    if( d3d9_bake_pose_vertices(
            renderer,
            reservation.vbo,
            reservation.triangles,
            reservation.vertex_base,
            command->model,
            &command->world_position) )
        d3d9_zbuffer_batch_pose_baked(
            renderer, command->element_id, anim_index, pose_id, command->model);
}

static void
d3d9_batch_end(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand_Batch* command)
{
    struct D3D9StaticBatch* batch;
    int slot;
    assert(renderer);
    assert(command);
    slot = renderer->current_batch_slot;
    if( slot < 0 || (uint32_t)slot >= renderer->static_batch_count )
        return;
    batch = &renderer->static_batches[slot];
    if( !batch->building || batch->batch_id != command->batch_id )
        return;
    trspk_batch16_end(batch->cpu);
    batch->building = false;
    (void)d3d9_static_batch_commit(renderer, (uint32_t)slot);
    renderer->current_batch_slot = -1;
}

static void
d3d9_batch_clear(
    struct ToriRS_D3D9* renderer,
    int batch_id,
    bool clear_all)
{
    uint32_t i;
    assert(renderer);
    if( renderer->device )
        IDirect3DDevice9_SetStreamSource(renderer->device, 0u, NULL, 0u, 0u);
    for( i = 0u; i < renderer->static_batch_count; i++ )
    {
        struct D3D9StaticBatch* batch = &renderer->static_batches[i];
        uint32_t page;
        if( !clear_all && batch->batch_id != batch_id )
            continue;
        d3d9_zbuffer_batch_dropped(renderer, batch->cpu);
        for( page = 0u; page < batch->page_id_capacity; page++ )
        {
            uint32_t page_id = batch->page_ids[page];
            if( page_id != UINT32_MAX && page_id < renderer->static_page_count )
                renderer->static_pages[page_id].valid = false;
        }
        trspk_batch16_clear(batch->cpu);
        batch->active = false;
        batch->building = false;
    }
    d3d9_rebuild_batch_pose_table(renderer);
    renderer->current_batch_slot = -1;
}

static void
d3d9_dispatch(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand* command)
{
    assert(renderer);
    assert(command);
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
        d3d9_animation_track_unload(
            renderer,
            command->u.anim_load.element_id,
            command->u.anim_load.anim_index);
        break;
    case TORIRSRC_BATCH3D_BEGIN:
        d3d9_batch_begin(renderer, &command->u.batch);
        break;
    case TORIRSRC_BATCH3D_MODEL_ADD:
        d3d9_batch_add(renderer, &command->u.batch, false);
        break;
    case TORIRSRC_BATCH3D_ANIM_ADD:
        d3d9_batch_add(renderer, &command->u.batch, true);
        break;
    case TORIRSRC_BATCH3D_END:
        d3d9_batch_end(renderer, &command->u.batch);
        break;
    case TORIRSRC_BATCH3D_CLEAR:
        d3d9_batch_clear(
            renderer, command->u.batch.batch_id, command->u.batch.clear_all);
        if( command->u.batch.clear_all )
        {
            trspk_pose_table_clear(&renderer->poses);
            d3d9_reset_group(&renderer->groups[TRSPK_VBO_GROUP_STATIC]);
        }
        break;
    case TORIRSRC_DRAW_MODEL:
        d3d9_draw_model(renderer, &command->u.model);
        break;

    case TORIRSRC_CLEAR_RECT:
        d3d9_ui_draw_clear_rect(renderer, &command->u.clear_rect);
        break;
    case TORIRSRC_FILL_RECT:
        d3d9_ui_draw_fill_rect(renderer, &command->u.fill_rect);
        break;
    case TORIRSRC_DRAW_MODEL_WIDGET:
        d3d9_ui_draw_model_widget(renderer, &command->u.model_widget);
        break;
    case TORIRSRC_SPRITE:
        d3d9_ui_draw_sprite(renderer, &command->u.sprite);
        break;
    case TORIRSRC_FONT:
        d3d9_ui_draw_font(renderer, &command->u.font);
        break;
    case TORIRSRC_LINE:
        d3d9_ui_draw_line(renderer, &command->u.line);
        break;
    case TORIRSRC_TEX_BEGIN:
    case TORIRSRC_TEX_END:
    case TORIRSRC_SPRITE_BEGIN:
    case TORIRSRC_SPRITE_END:
    case TORIRSRC_FONT_BEGIN:
    case TORIRSRC_FONT_END:
        break;

    case TORIRSRC_SPRITE_LOAD:
        /* The scene owns pixels. Upload stays lazy so assets never drawn by
         * this backend consume atlas space or transfer bandwidth. */
        break;
    case TORIRSRC_SPRITE_UNLOAD:
        d3d9_ui_flush(renderer);
        d3d9_ui_sprite_invalidate(renderer, command->u.sprite_load.element_id);
        break;
    case TORIRSRC_FONT_LOAD:
    {
        int slot_index = d3d9_ui_font_slot_index(
            renderer, command->u.font_load.font_id, true);
        if( slot_index >= 0 )
        {
            struct D3D9UIFontSlot* slot = &renderer->ui_fonts[slot_index];
            if( slot->font != command->u.font_load.font )
            {
                d3d9_ui_flush(renderer);
                d3d9_ui_font_release_slot(slot);
                slot->font = command->u.font_load.font;
            }
        }
        break;
    }
    case TORIRSRC_FONT_UNLOAD:
    {
        int slot_index = d3d9_ui_font_slot_index(
            renderer, command->u.font_load.font_id, false);
        if( slot_index >= 0 )
        {
            d3d9_ui_flush(renderer);
            d3d9_ui_font_release_slot(&renderer->ui_fonts[slot_index]);
            renderer->ui_fonts[slot_index].font = NULL;
            renderer->ui_fonts[slot_index].font_id = -1;
        }
        break;
    }
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
    assert(renderer);
    renderer->width = width;
    renderer->height = height;
    renderer->tex_slot_next = 1u;
    renderer->current_batch_slot = -1;
    for( texture = 0; texture < TORIDRAW_TEXTURE_ID_CAPACITY; texture++ )
        renderer->tex_slot_of_id[texture] = -1;
    for( texture = 0; texture < D3D9_UI_FONT_CAP; texture++ )
        renderer->ui_fonts[texture].font_id = -1;

    trspk_pose_table_init(&renderer->poses);
    trspk_pose_table_init(&renderer->batch_poses);
    renderer->ibo_chain = trspk_ibochain_create(TRSPK_INDEX_FORMAT_U16);
    renderer->draw_ranges = trspk_drawrangelist_create(D3D9_DRAWRANGE_CAP);
    renderer->ui_batch.vertices = (struct D3D9OverlayVertex*)malloc(
        D3D9_UI_BATCH_MAX_VERTS * sizeof(renderer->ui_batch.vertices[0]));
    /* ::zbuffer stays NULL here, so a renderer that never reaches Init -- the
     * headless retained test API attaches a scene without one -- runs the
     * painter path, which needs no setup. */
    if( !renderer->ibo_chain || !renderer->draw_ranges ||
        !renderer->ui_batch.vertices ||
        !trspk_atlas_init_grid(
            &renderer->atlas,
            D3D9_ATLAS_DIM,
            D3D9_ATLAS_DIM,
            TRSPK_ATLAS_TILE,
            TRSPK_ATLAS_TILE,
            4u) ||
        !trspk_atlas_init_binpack(
            &renderer->ui_sprite_atlas, D3D9_UI_ATLAS_DIM, D3D9_UI_ATLAS_DIM, 4u) )
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
    d3d9_ui_batch_reset(renderer);
    return renderer;
}

#if defined(TORIRS_D3D9_RETAINED_TEST_API)
_Static_assert(
    TORIRS_D3D9_RETAINED_GROUP_COUNT == TRSPK_VBO_GROUP_COUNT,
    "retained test stats must cover every D3D9 model group");

bool
ToriRS_D3D9_AttachSceneHeadlessForTest(
    struct ToriRS_D3D9* renderer,
    struct ToriDraw_Scene* scene)
{
    assert(renderer);
    if( renderer->device )
        return false;
    assert(scene);
    renderer->scene = scene;
    return true;
}

bool
ToriRS_D3D9_GetRetainedStats(
    const struct ToriRS_D3D9* renderer,
    struct ToriRS_D3D9RetainedStats* out_stats)
{
    uint32_t group_index;
    assert(renderer);
    assert(out_stats);
    memset(out_stats, 0, sizeof(*out_stats));
    for( group_index = 0u; group_index < TRSPK_VBO_GROUP_COUNT; group_index++ )
    {
        const struct D3D9ModelGroup* group = &renderer->groups[group_index];
        struct ToriRS_D3D9RetainedGroupStats* out =
            &out_stats->groups[group_index];
        uint32_t slot_index;
        if( group->arena )
        {
            out->write_cursor = group->arena->write_cursor;
            out->slot_count = group->arena->slot_count;
            out->slot_capacity = group->arena->slot_capacity;
            for( slot_index = 0u; slot_index < group->arena->slot_count; slot_index++ )
                if( trspk_modelslot_is_alive(&group->arena->slots[slot_index]) )
                    out->alive_slot_count++;
        }
        if( group->vbo_cpu )
        {
            out->vertex_count = group->vbo_cpu->vertex_count;
            out->vertex_capacity = group->vbo_cpu->capacity;
        }
    }
    out_stats->pose_element_count = renderer->poses.element_count;
    out_stats->pose_element_capacity = renderer->poses.element_cap;
    return true;
}

bool
ToriRS_D3D9_GetPoseBase(
    const struct ToriRS_D3D9* renderer,
    int element_id,
    int anim_index,
    int pose_id,
    uint32_t* out_vertex_base)
{
    uint32_t locator;
    assert(renderer);
    assert(out_vertex_base);
    if( trspk_pose_table_get(
            &renderer->batch_poses,
            element_id,
            anim_index,
            pose_id,
            &locator) )
    {
        *out_vertex_base = locator & D3D9_BATCH_POSE_BASE_MASK;
        return true;
    }
    return trspk_pose_table_get(
            &renderer->poses,
            element_id,
            anim_index,
            pose_id,
            out_vertex_base);
}
#endif

void
ToriRS_D3D9_Free(struct ToriRS_D3D9* renderer)
{
    uint32_t batch;
    uint32_t group;
    int texture;
    if( !renderer )
        return;
    if( renderer->scene_active )
        d3d9_end_frame_scene(renderer);
    d3d9_release_default_pool(renderer);
    if( renderer->atlas_texture )
        IDirect3DTexture9_Release(renderer->atlas_texture);
    if( renderer->ui_sprite_atlas_texture )
        IDirect3DTexture9_Release(renderer->ui_sprite_atlas_texture);
    for( texture = 0; texture < D3D9_UI_FONT_CAP; texture++ )
        d3d9_ui_font_release_slot(&renderer->ui_fonts[texture]);
    for( texture = 0; texture < (int)renderer->ui_rotmask_count; texture++ )
        d3d9_ui_rotmask_release_slot(&renderer->ui_rotmasks[texture]);
    for( texture = 0; texture < TORIDRAW_TEXTURE_ID_CAPACITY; texture++ )
        if( renderer->animated_textures[texture] )
            IDirect3DTexture9_Release(renderer->animated_textures[texture]);
    for( group = 0u; group < TRSPK_VBO_GROUP_COUNT; group++ )
    {
        if( renderer->groups[group].vbo_gpu )
            IDirect3DVertexBuffer9_Release(renderer->groups[group].vbo_gpu);
    }
    if( renderer->static_batch_vbo )
        IDirect3DVertexBuffer9_Release(renderer->static_batch_vbo);
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
    trspk_pose_table_free(&renderer->batch_poses);
    d3d9_zbuffer_destroy(renderer);
    for( batch = 0u; batch < renderer->static_batch_count; batch++ )
    {
        trspk_batch16_destroy(renderer->static_batches[batch].cpu);
        free(renderer->static_batches[batch].page_ids);
    }
    trspk_atlas_free(&renderer->atlas);
    trspk_atlas_free(&renderer->ui_sprite_atlas);
    for( texture = 0; texture < D3D9_UI_SPRITE_CAP; texture++ )
    {
        free(renderer->ui_sprite_slots[texture].uvs);
        free(renderer->ui_sprite_slots[texture].loaded);
    }
    free(renderer->ui_rotmasks);
    free(renderer->ui_batch.vertices);
    free(renderer->model_indices);
    free(renderer->static_pages);
    free(renderer->static_batches);
    free(renderer);
}

bool
ToriRS_D3D9_Init(
    struct ToriRS_D3D9* renderer,
    void* native_window,
    struct ToriDraw_Scene* scene,
    bool z_buffer_enabled)
{
    DWORD behavior;
    HRESULT hr;
    int width;
    int height;
    assert(renderer);
    if( renderer->device )
        return false;
    assert(native_window);
    assert(scene);
    /* This is the one place the two world implementations are chosen between.
     * Creating the depth state selects d3d9_zbuffer_*; leaving it NULL selects
     * d3d9_painter_*, which has nothing to allocate. */
    d3d9_zbuffer_destroy(renderer);
    if( z_buffer_enabled && !d3d9_zbuffer_create(renderer) )
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
    renderer->present.EnableAutoDepthStencil = z_buffer_enabled ? TRUE : FALSE;
    renderer->present.AutoDepthStencilFormat = D3DFMT_D16;
    /* The client loop owns the 50 Hz deadline.  A second 60 Hz D3D wait
     * quantizes capped frames onto alternating refreshes (visible judder) and
     * makes --uncapped profiles advance game time while blocked in Present.
     * Classic IMMEDIATE is available on XP and keeps both Windows lanes on
     * the same pacing contract. */
    renderer->present.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
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
    if( renderer->static_page_count > 0u )
    {
        bool recreated = false;
        if( !d3d9_ensure_static_batch_vbo(
                renderer, renderer->static_page_count, &recreated) )
            return false;
        if( recreated )
            d3d9_mark_active_static_batches_dirty(renderer);
        if( !d3d9_upload_dirty_static_batches(renderer) )
            return false;
    }
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
    d3d9_ui_batch_reset(renderer);
}

void
ToriRS_D3D9_SetPick(struct ToriRS_D3D9* renderer, int mouse_x, int mouse_y)
{
    assert(renderer);
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
    assert(renderer);
    if( !d3d9_begin_frame_scene(renderer) )
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
    assert(renderer);
    if( !d3d9_begin_frame_scene(renderer) )
        return;
    assert(frame);
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
    assert(renderer);
    if( renderer->scene_active || !d3d9_device_ready(renderer) )
        return;
    hr = IDirect3DDevice9_Present(renderer->device, NULL, NULL, NULL, NULL);
    if( hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET )
        renderer->reset_pending = true;
    else if( FAILED(hr) )
        d3d9_log_hr("Present", hr);
}
