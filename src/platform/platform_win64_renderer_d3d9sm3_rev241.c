/*
 * The rev-241 client's world pass on win64's D3D9, through vs_3_0/ps_3_0
 * programs (3rd/trspk/d3d9/shaders/rev241.hlsl, bytecode compiled offline into
 * d3d9_rev241_shaders.h -- no compiler at run time). Shared by "Direct3D 9
 * (Sailing OSRS)" and "(Post-TFA OSRS)" on win64
 * (platform_win64_renderer_d3d9sm3_os235sailing.c, _os241tfa.c).
 *
 * As GL3 and WebGL2 do: the vertex stage writes the client's depth key, with
 * each face's bias (texdata.y), into window z; the pass clears to 0 and tests
 * GREATER. A parallel camera has no depth in w and runs the standard mapping,
 * biased, with LESS. Two-pass models, the translucent order and Post-TFA's
 * eye-ray texel are the runs of platform_win32_renderer_d3d9_rev241.h; a bias
 * run needs nothing of its own here, the vertex carries the bias.
 *
 * The XP lane has no programmable stages and gets as close as it can with
 * the same runs (platform_win32_renderer_d3d9ff_rev241.c).
 */

#include "platform/platform_win64_renderer_d3d9sm3_rev241.h"

#include "d3d9/d3d9_rev241_shaders.h"

#include <assert.h>
#include <string.h>

enum
{
    D3D9SM3_WORLD_VS = 0,
    D3D9SM3_PROJECTED_VS,
    D3D9SM3_WORLD_PS,
    D3D9SM3_PROJECTED_PS,
};

/* Registers (rev241.hlsl). */
#define D3D9SM3_C_VIEW_PROJ 0u
#define D3D9SM3_C_DEPTH 4u
#define D3D9SM3_C_PROJECTOR 6u

/* The vertex the core bakes, as the programs read it: texdata is TEXCOORD1. */
#define D3D9SM3_WORLD_FVF (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX2)

_Static_assert(
    D3D9_VS_TEXTURE_SCROLL_REGISTER == 5u, "rev241.hlsl reads the scroll from c5");

static IDirect3DVertexShader9*
d3d9sm3_vs(struct ToriPlatformWin32_Renderer_D3D9* renderer, int which)
{
    return (IDirect3DVertexShader9*)renderer->rev241->lane_objects[which];
}

static IDirect3DPixelShader9*
d3d9sm3_ps(struct ToriPlatformWin32_Renderer_D3D9* renderer, int which)
{
    return (IDirect3DPixelShader9*)renderer->rev241->lane_objects[which];
}

static bool
d3d9sm3_keyed(const struct ToriPlatformWin32_Renderer_D3D9* renderer)
{
    return renderer->cur_3d.camera.projection_mode != TORIDRAW_PROJECTION_MODE_PARALLEL;
}

bool
d3d9sm3_rev241_init(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    void* native_window,
    struct ToriDraw_Scene* scene)
{
    static const D3DFORMAT depth_formats[] = { D3DFMT_D24X8, D3DFMT_D24S8, D3DFMT_D16 };
    IDirect3DVertexShader9* vs = NULL;
    IDirect3DPixelShader9* ps = NULL;
    assert(renderer);
    if( renderer->device )
        return false;
    if( !d3d9_rev241_create(renderer) )
        return false;
    if( !d3d9_init_device(
            renderer, native_window, scene,
            d3d9_pick_depth_format(renderer, depth_formats, sizeof(depth_formats) / sizeof(depth_formats[0]))) )
        return false;
    if( D3DSHADER_VERSION_MAJOR(renderer->caps.VertexShaderVersion) < 3u ||
        D3DSHADER_VERSION_MAJOR(renderer->caps.PixelShaderVersion) < 3u )
    {
        fprintf(stderr, "D3D9: the OSRS renderers need shader model 3 (vs %lu.%lu, ps %lu.%lu)\n",
            (unsigned long)D3DSHADER_VERSION_MAJOR(renderer->caps.VertexShaderVersion),
            (unsigned long)D3DSHADER_VERSION_MINOR(renderer->caps.VertexShaderVersion),
            (unsigned long)D3DSHADER_VERSION_MAJOR(renderer->caps.PixelShaderVersion),
            (unsigned long)D3DSHADER_VERSION_MINOR(renderer->caps.PixelShaderVersion));
        return false;
    }
    if( FAILED(IDirect3DDevice9_CreateVertexShader(
            renderer->device, (const DWORD*)d3d9_rev241_world_vs, &vs)) )
        return false;
    renderer->rev241->lane_objects[D3D9SM3_WORLD_VS] = (IUnknown*)vs;
    if( FAILED(IDirect3DDevice9_CreateVertexShader(
            renderer->device, (const DWORD*)d3d9_rev241_projected_vs, &vs)) )
        return false;
    renderer->rev241->lane_objects[D3D9SM3_PROJECTED_VS] = (IUnknown*)vs;
    if( FAILED(IDirect3DDevice9_CreatePixelShader(
            renderer->device, (const DWORD*)d3d9_rev241_world_ps, &ps)) )
        return false;
    renderer->rev241->lane_objects[D3D9SM3_WORLD_PS] = (IUnknown*)ps;
    if( FAILED(IDirect3DDevice9_CreatePixelShader(
            renderer->device, (const DWORD*)d3d9_rev241_projected_ps, &ps)) )
        return false;
    renderer->rev241->lane_objects[D3D9SM3_PROJECTED_PS] = (IUnknown*)ps;
    return true;
}

void
d3d9sm3_rev241_begin_3d(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    const struct ToriRS_RenderCommand_Begin3D* command)
{
    D3DMATRIX view;
    D3DMATRIX proj;
    D3DMATRIX view_proj;
    float depth[4];
    if( !d3d9_begin_3d_viewport(renderer, command) )
        return;
    d3d9_rev241_reset(renderer);
    /* Resets the depth state's queues and clears depth to 1. */
    d3d9_zbuffer_begin_pass(renderer);
    if( d3d9sm3_keyed(renderer) )
        /* The client's key is larger when nearer: its buffer clears to 0. */
        IDirect3DDevice9_Clear(renderer->device, 0u, NULL, D3DCLEAR_ZBUFFER, 0u, 0.0f, 0u);
    d3d9_begin_3d_matrices(renderer, command);
    d3d9_zbuffer_setup_projection(renderer, command);
    d3d9_begin_3d_transforms(renderer);

    memcpy(&view, renderer->view, sizeof(view));
    memcpy(&proj, renderer->proj, sizeof(proj));
    d3d9_rev241_multiply(&view, &proj, &view_proj);
    IDirect3DDevice9_SetVertexShaderConstantF(
        renderer->device, D3D9SM3_C_VIEW_PROJ, &view_proj.m[0][0], 4u);
    depth[0] = d3d9sm3_keyed(renderer) ? 1.0f : 0.0f;
    depth[1] = 0.0f;
    d3d9_rev241_depth_mapping(renderer, &depth[2], &depth[3]);
    IDirect3DDevice9_SetVertexShaderConstantF(renderer->device, D3D9SM3_C_DEPTH, depth, 1u);
}

/* The states and programs every one of the pass's draws starts from. */
static void
d3d9sm3_pass_states(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    bool blend,
    int vs,
    int ps)
{
    IDirect3DDevice9* device = renderer->device;
    d3d9_set_world_states(renderer);
    IDirect3DDevice9_SetFVF(device, D3D9SM3_WORLD_FVF);
    IDirect3DDevice9_SetVertexShader(device, d3d9sm3_vs(renderer, vs));
    IDirect3DDevice9_SetPixelShader(device, d3d9sm3_ps(renderer, ps));
    IDirect3DDevice9_SetRenderState(
        device, D3DRS_ZFUNC, d3d9sm3_keyed(renderer) ? D3DCMP_GREATER : D3DCMP_LESS);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ZWRITEENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ALPHABLENDENABLE, blend ? TRUE : FALSE);
}

static void
d3d9sm3_draw_run(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    const struct D3D9Rev241Run* run)
{
    IDirect3DDevice9* device = renderer->device;
    if( !d3d9_rev241_prepare_run(renderer, run) )
        return;
    if( run->kind == D3D9_REV241_RUN_TWO_PASS )
    {
        d3d9sm3_pass_states(renderer, run->translucent, D3D9SM3_WORLD_VS, D3D9SM3_WORLD_PS);
        IDirect3DDevice9_SetRenderState(device, D3DRS_ZWRITEENABLE, FALSE);
        d3d9_draw_ranges_configured(renderer);
        IDirect3DDevice9_SetRenderState(device, D3DRS_ZWRITEENABLE, TRUE);
        IDirect3DDevice9_SetRenderState(device, D3DRS_COLORWRITEENABLE, 0u);
        d3d9_draw_ranges_configured(renderer);
        IDirect3DDevice9_SetRenderState(device, D3DRS_COLORWRITEENABLE, 0xFu);
    }
    else if( run->kind == D3D9_REV241_RUN_PROJECTED )
    {
        D3DMATRIX rows;
        d3d9sm3_pass_states(renderer, run->translucent, D3D9SM3_PROJECTED_VS, D3D9SM3_PROJECTED_PS);
        if( !d3d9_rev241_bind_projector_texture(renderer, run) )
            return;
        memset(&rows, 0, sizeof(rows));
        for( int r = 0; r < 4; r++ )
            for( int c = 0; c < 3; c++ )
                rows.m[r][c] = run->projector[r][c];
        rows.m[3][3] = 1.0f;
        IDirect3DDevice9_SetVertexShaderConstantF(device, D3D9SM3_C_PROJECTOR, &rows.m[0][0], 4u);
        d3d9_draw_ranges_bound(renderer);
    }
    else
    {
        /* TRANSLUCENT blends; BIASED is opaque, its bias in the vertex. */
        d3d9sm3_pass_states(renderer, run->translucent, D3D9SM3_WORLD_VS, D3D9SM3_WORLD_PS);
        d3d9_draw_ranges_configured(renderer);
    }
}

void
d3d9sm3_rev241_end_3d(struct ToriPlatformWin32_Renderer_D3D9* renderer)
{
    struct D3D9Rev241World* world = renderer->rev241;
    if( d3d9_end_3d_begin(renderer) )
    {
        if( d3d9_rev241_prepare_opaque(renderer) )
        {
            d3d9sm3_pass_states(renderer, false, D3D9SM3_WORLD_VS, D3D9SM3_WORLD_PS);
            d3d9_draw_ranges_configured(renderer);
        }
        for( uint32_t i = 0u; i < world->run_count; i++ )
            if( !world->runs[i].translucent )
                d3d9sm3_draw_run(renderer, &world->runs[i]);
        for( uint32_t i = 0u; i < world->run_count; i++ )
            if( world->runs[i].translucent )
                d3d9sm3_draw_run(renderer, &world->runs[i]);
        IDirect3DDevice9_SetVertexShader(renderer->device, NULL);
        IDirect3DDevice9_SetPixelShader(renderer->device, NULL);
    }
    d3d9_end_3d_close(renderer);
    d3d9_zbuffer_reset_pass(renderer);
    d3d9_rev241_reset(renderer);
}
