/**
 * D3D8 world shaders: vs_1_1 + ps_1_4 via D3DXAssembleShader (d3dx9_*.dll / d3dx8.dll).
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <d3d8.h>
#include <d3d8types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "d3d8_internal.h"
#include "d3d8_vertex.h"
#include "trspk_d3d8.h"

#ifndef D3DVSD_STREAMDATASHIFT
#define D3DVSD_STREAMDATASHIFT 16
#endif
#ifndef D3DVSD_STREAMDATA_TOKEN
#define D3DVSD_STREAMDATA_TOKEN 0x00000002u
#endif
#define TRSPK_D3DVSD_STREAMDATA(stride) \
    ((DWORD)(((DWORD)(stride) << D3DVSD_STREAMDATASHIFT) | D3DVSD_STREAMDATA_TOKEN))

namespace
{

typedef HRESULT(WINAPI PFN_D3DXAssembleShader)(
    LPCSTR pSrcData,
    UINT SrcDataLen,
    const void* pDefines,
    void* pInclude,
    DWORD Flags,
    void** ppShader,
    void** ppCompileErrors);

static HRESULT(WINAPI *g_D3DXAssembleShader)(LPCSTR, UINT, const void*, void*, DWORD, void**, void**) =
    nullptr;

static bool
d3dx_load(void)
{
    if( g_D3DXAssembleShader )
        return true;
    static const char* dlls[] = {
        "d3dx9_43.dll",
        "d3dx9_42.dll",
        "d3dx9_41.dll",
        "d3dx9_38.dll",
        "d3dx8.dll",
    };
    for( size_t i = 0; i < sizeof(dlls) / sizeof(dlls[0]); ++i )
    {
        HMODULE h = LoadLibraryA(dlls[i]);
        if( !h )
            continue;
        auto* fn =
            reinterpret_cast<PFN_D3DXAssembleShader>(GetProcAddress(h, "D3DXAssembleShader"));
        if( fn )
        {
            g_D3DXAssembleShader = fn;
            return true;
        }
    }
    return false;
}

static void*
d3dx_buf_ptr(void* buf)
{
    void** vt = *reinterpret_cast<void***>(buf);
    typedef void*(STDMETHODCALLTYPE * GP)(void*);
    return reinterpret_cast<GP>(vt[3])(buf);
}

static void
d3dx_buf_release(void* buf)
{
    if( !buf )
        return;
    void** vt = *reinterpret_cast<void***>(buf);
    typedef ULONG(STDMETHODCALLTYPE * Rel)(void*);
    reinterpret_cast<Rel>(vt[2])(buf);
}

static HRESULT
assemble(const char* src, size_t len, void** out_bytes, DWORD* out_size)
{
    *out_bytes = nullptr;
    *out_size = 0u;
    if( !d3dx_load() || !g_D3DXAssembleShader )
        return E_FAIL;
    void* code_buf = nullptr;
    void* errs = nullptr;
    HRESULT hr =
        g_D3DXAssembleShader(src, (UINT)len, nullptr, nullptr, 0u, &code_buf, &errs);
    if( errs )
        d3dx_buf_release(errs);
    if( FAILED(hr) || !code_buf )
    {
        if( code_buf )
            d3dx_buf_release(code_buf);
        return FAILED(hr) ? hr : E_FAIL;
    }
    void* p = d3dx_buf_ptr(code_buf);
    void** vt = *reinterpret_cast<void***>(code_buf);
    typedef DWORD(STDMETHODCALLTYPE * GS)(void*);
    DWORD sz = reinterpret_cast<GS>(vt[4])(code_buf);
    void* copy = malloc((size_t)sz);
    if( !copy )
    {
        d3dx_buf_release(code_buf);
        return E_OUTOFMEMORY;
    }
    memcpy(copy, p, (size_t)sz);
    *out_bytes = copy;
    *out_size = sz;
    d3dx_buf_release(code_buf);
    return S_OK;
}

static const char kVsAsm[] =
    "vs.1.1\n"
    "dcl_position v0\n"
    "dcl_color v1\n"
    "dcl_texcoord0 v2\n"
    "dcl_texcoord1 v3\n"
    "dcl_texcoord2 v4\n"
    "dp4 r0.x, v0, c0\n"
    "dp4 r0.y, v0, c1\n"
    "dp4 r0.z, v0, c2\n"
    "dp4 r0.w, v0, c3\n"
    "dp4 r1.x, r0, c4\n"
    "dp4 r1.y, r0, c5\n"
    "dp4 r1.z, r0, c6\n"
    "dp4 r1.w, r0, c7\n"
    "mov oPos, r1\n"
    "mov oD0, v1\n"
    "mov oT0.xy, v2\n"
    "mov oT1.x, v3.x\n"
    "mov oT2.x, v4.x\n";

/** Minimal Stage-0 sample × diffuse; atlas UV should be computed per-pixel — TRSPK matches WebGL
 *  fragment shader most closely when UV logic lives here; expanded in follow-ups. */
static const char kPsAsm[] =
    "ps.1.4\n"
    "texld r0, t0\n"
    "mul r0, r0, v0\n";

} /* namespace */

extern "C" {

static void
trspk_d3d8_fill_shader_locs(TRSPK_D3D8WorldShaderLocs* locs)
{
    if( !locs )
        return;
    locs->a_position = 0;
    locs->a_color = 1;
    locs->a_texcoord = 2;
    locs->a_tex_id = 3;
    locs->a_uv_mode = 4;
    locs->u_modelViewMatrix = 0;
    locs->u_projectionMatrix = 4;
    locs->u_clock = 8;
    locs->s_atlas = 0;
}

GLuint
trspk_d3d8_create_world_program(TRSPK_D3D8WorldShaderLocs* locs)
{
    /** Legacy entrypoint (WebGL1 builds shaders here). D3D8 builds after IDirect3DDevice8 exists;
     *  we only fill semantic constant slot hints for portable draw code. */
    trspk_d3d8_fill_shader_locs(locs);
    return 1u;
}

HRESULT
trspk_d3d8_world_shaders_create_for_device(TRSPK_D3D8Renderer* r)
{
    if( !r || r->com_device == 0u )
        return E_INVALIDARG;
    auto* device = reinterpret_cast<IDirect3DDevice8*>(r->com_device);

    DWORD vs_len = 0u;
    DWORD ps_len = 0u;
    void* vs_bytes = nullptr;
    void* ps_bytes = nullptr;
    HRESULT hvs = assemble(kVsAsm, sizeof(kVsAsm) - 1u, &vs_bytes, &vs_len);
    HRESULT hps = assemble(kPsAsm, sizeof(kPsAsm) - 1u, &ps_bytes, &ps_len);
    if( FAILED(hvs) || FAILED(hps) || !vs_bytes || !ps_bytes )
    {
        fprintf(
            stderr,
            "TRSPK D3D8: shader assemble failed (HRESULT vs=%08lx ps=%08lx). Install DirectX "
            "June 2010 redist for d3dx9_43.dll.\n",
            (unsigned long)hvs,
            (unsigned long)hps);
        free(vs_bytes);
        free(ps_bytes);
        return FAILED(hvs) ? hvs : (FAILED(hps) ? hps : E_FAIL);
    }

    static const DWORD decl[] = {
        D3DVSD_STREAM(0),
        TRSPK_D3DVSD_STREAMDATA(sizeof(TRSPK_VertexD3D8)),
        D3DVSD_REG(0, D3DVSDT_FLOAT4),
        D3DVSD_REG(1, D3DVSDT_FLOAT4),
        D3DVSD_REG(2, D3DVSDT_FLOAT2),
        D3DVSD_REG(3, D3DVSDT_FLOAT1),
        D3DVSD_REG(4, D3DVSDT_FLOAT1),
        D3DVSD_END()};

    DWORD vs_handle = 0u;
    DWORD ps_handle = 0u;
    HRESULT hr = device->CreateVertexShader(decl, (DWORD*)vs_bytes, &vs_handle, 0u);
    free(vs_bytes);
    if( FAILED(hr) )
    {
        fprintf(stderr, "TRSPK D3D8: CreateVertexShader failed hr=%08lx\n", (unsigned long)hr);
        free(ps_bytes);
        return hr;
    }
    hr = device->CreatePixelShader((DWORD*)ps_bytes, &ps_handle);
    free(ps_bytes);
    if( FAILED(hr) )
    {
        fprintf(stderr, "TRSPK D3D8: CreatePixelShader failed hr=%08lx\n", (unsigned long)hr);
        device->DeleteVertexShader(vs_handle);
        return hr;
    }

    r->prog_world3d = (GLuint)vs_handle;
    r->ps_world3d = (GLuint)ps_handle;
    trspk_d3d8_fill_shader_locs(&r->world_locs);
    return S_OK;
}

void
trspk_d3d8_bind_world_attribs(TRSPK_D3D8Renderer* r)
{
    (void)r;
}

} /* extern "C" */
