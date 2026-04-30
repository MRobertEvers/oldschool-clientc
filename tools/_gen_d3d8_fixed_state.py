# one-off generator — slice legacy d3d8_old helpers without duplicate structs / D3D8Internal / d3d8_priv
import re

path = "src/platforms/d3d8_old/platform_impl2_win32_renderer_d3d8.cpp"
lines = open(path, encoding="utf-8", errors="replace").readlines()

def join_ranges(ranges):
    out = []
    for a, b in ranges:
        out.extend(lines[a:b])
    return "".join(out)

chunk = join_ranges(
    [
        (80, 86),  # kFvfWorld, kFvfUi, kSkipNuklear comment line
        (117, 125),  # d3d8_mul_row_vec_d3dmatrix
        (225, 782),  # mat4..compute_letterbox_dst (excludes d3d8_priv)
    ]
)

chunk = chunk.replace("D3D8Internal*", "D3D8FixedInternal*")
chunk = chunk.replace("D3D8Internal", "D3D8FixedInternal")
chunk = chunk.replace("d3d8_log(", "trspk_d3d8_fixed_log(")
chunk = chunk.replace("d3d8_should_log_cmd", "d3d8_fixed_should_log_cmd")
chunk = chunk.replace("d3d8_mark_cmd_logged", "d3d8_fixed_mark_cmd_logged")
chunk = chunk.replace("static inline bool\nd3d8_trace_on", "static inline bool\nd3d8_fixed_trace_on")
chunk = chunk.replace("d3d8_trace_on(", "d3d8_fixed_trace_on(")

hdr = r"""/**
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

"""

tail = r"""
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
"""

outp = "src/platforms/ToriRSPlatformKit/src/backends/d3d8_fixed/d3d8_fixed_state.cpp"
open(outp, "w", encoding="utf-8", newline="\n").write(hdr + chunk + tail)
print("wrote", outp, "len", len(hdr + chunk + tail))
