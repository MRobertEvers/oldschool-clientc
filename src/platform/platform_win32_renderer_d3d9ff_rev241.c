/*
 * The rev-241 client's world pass on XP's fixed-function D3D9 -- as close as a
 * pipeline with no vertex stage gets. Shared by "Direct3D 9 (Sailing OSRS)"
 * and "(Post-TFA OSRS)" on the win32 lane
 * (platform_win32_renderer_d3d9ff_os235sailing.c, _os241tfa.c).
 *
 * What it keeps of the client (platform_win32_renderer_d3d9_rev241.h):
 *   the tie rule     depth clears to the far plane and tests LESS, so a
 *                    fragment level with what is drawn keeps it (the client's
 *                    strict GREATER on its key);
 *   two-pass models  colour only, then depth only, in priority order;
 *   translucency     the painter's order, faces far to near by bucket,
 *                    blending and writing depth;
 *   the texel        Post-TFA's projected faces through camera-space texture
 *                    generation and a projective texture matrix: the eye-ray
 *                    texel, per pixel, exactly.
 * What it approximates: the depth KEY (the hardware's hyperbolic depth orders
 * the same, but interpolates its own curve) and the face bias (one
 * D3DRS_DEPTHBIAS per bias run, converted at the model's depth, where the
 * device has it; none where it does not).
 */

#include "platform/platform_win32_renderer_d3d9ff_rev241.h"

#include <assert.h>
#include <string.h>

static DWORD
d3d9ff_float_bits(float value)
{
    DWORD bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void
d3d9ff_rev241_begin_3d(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    const struct ToriRS_RenderCommand_Begin3D* command)
{
    if( !d3d9_begin_3d_viewport(renderer, command) )
        return;
    d3d9_rev241_reset(renderer);
    /* Resets the depth state's queues and clears depth to 1. */
    d3d9_zbuffer_begin_pass(renderer);
    d3d9_begin_3d_matrices(renderer, command);
    d3d9_zbuffer_setup_projection(renderer, command);
    d3d9_begin_3d_transforms(renderer);
}

/* The states every one of the pass's draws starts from. */
static void
d3d9ff_pass_states(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    bool blend)
{
    IDirect3DDevice9* device = renderer->device;
    d3d9_set_world_states(renderer);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ZFUNC, D3DCMP_LESS);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ZWRITEENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ALPHABLENDENABLE, blend ? TRUE : FALSE);
}

/* Colour writes off or back on; a device without the cap blends to the
 * destination instead (ZERO, ONE), which writes the same nothing. */
static void
d3d9ff_color_writes(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    bool enabled,
    bool blend)
{
    IDirect3DDevice9* device = renderer->device;
    if( renderer->caps.PrimitiveMiscCaps & D3DPMISCCAPS_COLORWRITEENABLE )
    {
        IDirect3DDevice9_SetRenderState(device, D3DRS_COLORWRITEENABLE, enabled ? 0xFu : 0u);
        return;
    }
    if( enabled )
    {
        IDirect3DDevice9_SetRenderState(device, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        IDirect3DDevice9_SetRenderState(device, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        IDirect3DDevice9_SetRenderState(device, D3DRS_ALPHABLENDENABLE, blend ? TRUE : FALSE);
        return;
    }
    IDirect3DDevice9_SetRenderState(device, D3DRS_ALPHABLENDENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_SRCBLEND, D3DBLEND_ZERO);
    IDirect3DDevice9_SetRenderState(device, D3DRS_DESTBLEND, D3DBLEND_ONE);
}

/* A two-pass run: colour only, then the same faces depth only. */
static void
d3d9ff_draw_two_pass(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    const struct D3D9Rev241Run* run)
{
    IDirect3DDevice9* device = renderer->device;
    d3d9ff_pass_states(renderer, run->translucent);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ZWRITEENABLE, FALSE);
    d3d9_draw_ranges_configured(renderer);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ZWRITEENABLE, TRUE);
    d3d9ff_color_writes(renderer, false, run->translucent);
    d3d9_draw_ranges_configured(renderer);
    d3d9ff_color_writes(renderer, true, run->translucent);
}

/* A bias run: its camera-depth bias as a window-depth offset at the model's
 * depth. The projection maps depth d to A + B/d. */
static void
d3d9ff_draw_biased(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    const struct D3D9Rev241Run* run)
{
    IDirect3DDevice9* device = renderer->device;
    bool const has_bias = (renderer->caps.RasterCaps & D3DPRASTERCAPS_DEPTHBIAS) != 0u;
    d3d9ff_pass_states(renderer, false);
    if( has_bias && run->view_depth > 1.0f )
    {
        float a;
        float b;
        float biased = run->view_depth - run->depth_bias;
        d3d9_rev241_depth_mapping(renderer, &a, &b);
        (void)a;
        if( biased < 1.0f )
            biased = 1.0f;
        IDirect3DDevice9_SetRenderState(
            device, D3DRS_DEPTHBIAS, d3d9ff_float_bits(b / biased - b / run->view_depth));
    }
    d3d9_draw_ranges_configured(renderer);
    if( has_bias )
        IDirect3DDevice9_SetRenderState(device, D3DRS_DEPTHBIAS, d3d9ff_float_bits(0.0f));
}

/*
 * A projected run: texcoords generated from camera-space position, carried to
 * world space by the inverse view and through the projector's rows, divided
 * per pixel (D3DTTFF_PROJECTED): u = s/q, v = t/q -- the client's eye-ray
 * texel. The texture is the projected one on its own, u clamped, v wrapped.
 */
static void
d3d9ff_draw_projected(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    const struct D3D9Rev241Run* run)
{
    IDirect3DDevice9* device = renderer->device;
    D3DMATRIX view;
    D3DMATRIX view_inverse;
    D3DMATRIX rows;
    D3DMATRIX texture;
    d3d9ff_pass_states(renderer, run->translucent);
    if( !d3d9_rev241_bind_projector_texture(renderer, run) )
        return;
    memcpy(&view, renderer->view, sizeof(view));
    if( !d3d9_rev241_invert_affine(&view, &view_inverse) )
        return;
    memset(&rows, 0, sizeof(rows));
    for( int r = 0; r < 4; r++ )
        for( int c = 0; c < 3; c++ )
            rows.m[r][c] = run->projector[r][c];
    rows.m[3][3] = 1.0f;
    d3d9_rev241_multiply(&view_inverse, &rows, &texture);
    IDirect3DDevice9_SetTransform(device, D3DTS_TEXTURE0, &texture);
    IDirect3DDevice9_SetTextureStageState(
        device, 0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
    IDirect3DDevice9_SetTextureStageState(
        device, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT3 | D3DTTFF_PROJECTED);
    d3d9_draw_ranges_bound(renderer);
}

static void
d3d9ff_draw_run(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    const struct D3D9Rev241Run* run)
{
    if( !d3d9_rev241_prepare_run(renderer, run) )
        return;
    if( run->kind == D3D9_REV241_RUN_TWO_PASS )
        d3d9ff_draw_two_pass(renderer, run);
    else if( run->kind == D3D9_REV241_RUN_BIASED )
        d3d9ff_draw_biased(renderer, run);
    else if( run->kind == D3D9_REV241_RUN_PROJECTED )
        d3d9ff_draw_projected(renderer, run);
    else
    {
        d3d9ff_pass_states(renderer, true);
        d3d9_draw_ranges_configured(renderer);
    }
}

void
d3d9ff_rev241_end_3d(struct ToriPlatformWin32_Renderer_D3D9* renderer)
{
    struct D3D9Rev241World* world = renderer->rev241;
    if( d3d9_end_3d_begin(renderer) )
    {
        if( d3d9_rev241_prepare_opaque(renderer) )
        {
            d3d9ff_pass_states(renderer, false);
            d3d9_draw_ranges_configured(renderer);
        }
        for( uint32_t i = 0u; i < world->run_count; i++ )
            if( !world->runs[i].translucent )
                d3d9ff_draw_run(renderer, &world->runs[i]);
        for( uint32_t i = 0u; i < world->run_count; i++ )
            if( world->runs[i].translucent )
                d3d9ff_draw_run(renderer, &world->runs[i]);
    }
    d3d9_end_3d_close(renderer);
    d3d9_zbuffer_reset_pass(renderer);
    d3d9_rev241_reset(renderer);
}

bool
d3d9ff_rev241_init(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    void* native_window,
    struct ToriDraw_Scene* scene)
{
    static const D3DFORMAT depth_formats[] = { D3DFMT_D24X8, D3DFMT_D24S8, D3DFMT_D16 };
    assert(renderer);
    if( renderer->device )
        return false;
    if( !d3d9_rev241_create(renderer) )
        return false;
    return d3d9_init_device(
        renderer, native_window, scene,
        d3d9_pick_depth_format(renderer, depth_formats, sizeof(depth_formats) / sizeof(depth_formats[0])));
}
