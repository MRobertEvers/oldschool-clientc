# Generate d3d8_fixed_events.cpp from d3d8_old Render() switch bodies (brace-aware).
import re

legacy = "src/platforms/d3d8_old/platform_impl2_win32_renderer_d3d8.cpp"
outp = "src/platforms/ToriRSPlatformKit/src/backends/d3d8_fixed/d3d8_fixed_events.cpp"

lines = open(legacy, encoding="utf-8", errors="replace").readlines()

# 0-based line index of "        case TORIRS_GFX_RES_TEX_LOAD:"
CASE_LINE0 = 1614
# First line after switch closing (exclusive)
SWITCH_END0 = 2834


def unindent_line(ln, n):
    if ln.startswith(" " * n):
        return ln[n:]
    return ln


def unindent_text(s, n):
    return "".join(unindent_line(x, n) for x in s.splitlines(True))


def extract_cases():
    """Return list of (TORIRS_GFX_*, body_without_outer_brace)."""
    i = CASE_LINE0
    pending = []
    out = []
    while i < SWITCH_END0:
        # Accumulate consecutive case/default labels before the shared `{`.
        while i < SWITCH_END0:
            m = re.match(r"^\s*case (TORIRS_GFX_[A-Za-z0-9_]+):\s*$", lines[i])
            md = re.match(r"^\s*default:\s*$", lines[i])
            if m:
                pending.append(m.group(1))
                i += 1
                continue
            if md:
                pending.append("TORIRS_GFX_NONE")
                i += 1
                continue
            break
        if not pending:
            i += 1
            continue
        while i < SWITCH_END0 and lines[i].strip() == "":
            i += 1
        if i >= SWITCH_END0:
            break
        if not lines[i].lstrip().startswith("{"):
            i += 1
            continue
        depth = 0
        start_i = i
        while i < SWITCH_END0:
            ln = lines[i]
            depth += ln.count("{")
            depth -= ln.count("}")
            i += 1
            if depth <= 0:
                break
        block = "".join(lines[start_i:i])
        while i < SWITCH_END0 and lines[i].strip() == "":
            i += 1
        if i < SWITCH_END0 and re.match(r"^\s*break\s*;\s*$", lines[i]):
            i += 1
        b = block.strip()
        if b.startswith("{") and b.endswith("}"):
            b = b[1:-1].strip("\n")
            if b.endswith("\r"):
                b = b[:-1]
        for lab in pending:
            out.append((lab, b))
        pending.clear()
    return out


def transform_body(body):
    if not body:
        return ""
    b = unindent_text(body, 8)
    repls = [
        ("command.", "cmd->"),
        ("d3d8_should_log_cmd", "d3d8_fixed_should_log_cmd"),
        ("d3d8_mark_cmd_logged", "d3d8_fixed_mark_cmd_logged"),
        ("d3d8_log(", "trspk_d3d8_fixed_log("),
        ("d3d8_trace_on(p)", "false"),
        ("d3d8_ensure_pass", "d3d8_fixed_ensure_pass"),
        ("flush_font()", "p->flush_font()"),
    ]
    for a, x in repls:
        b = b.replace(a, x)
    b = re.sub(r"\bfont_verts\b", "p->pending_font_verts", b)
    # switch-case breaks -> return from handler (legacy `break` exited Render's switch).
    b = re.sub(r"(?m)^\s+break;\s*$", "return;", b)
    # Restore `break` that must exit an inner loop, not the lifted handler.
    b = re.sub(
        r"(if\( p->ib_ring_write_offset \+ nidx_bytes > p->ib_ring_size_bytes \)\s*\n)(\s+)return;",
        r"\1\2break;",
        b,
    )
    b = re.sub(
        r"(MODEL_DRAW ib_ring->Lock failed[^\n]+\n(?:[^\n]+\n){3})(\s+)return;",
        r"\1\2break;",
        b,
    )
    b = re.sub(
        r"(if\( pi >= \(int\)sizeof\(preview\) - 1 \)\s*\n)(\s+)return;",
        r"\1\2break;",
        b,
    )
    return b


kind_to_fn = {
    "TORIRS_GFX_RES_TEX_LOAD": "trspk_d3d8_fixed_event_tex_load",
    "TORIRS_GFX_RES_MODEL_LOAD": "trspk_d3d8_fixed_event_res_model_load",
    "TORIRS_GFX_RES_MODEL_UNLOAD": "trspk_d3d8_fixed_event_res_model_unload",
    "TORIRS_GFX_BATCH3D_BEGIN": "trspk_d3d8_fixed_event_batch3d_begin",
    "TORIRS_GFX_BATCH3D_MODEL_ADD": "trspk_d3d8_fixed_event_batch3d_model_add",
    "TORIRS_GFX_BATCH3D_END": "trspk_d3d8_fixed_event_batch3d_end",
    "TORIRS_GFX_BATCH3D_CLEAR": "trspk_d3d8_fixed_event_batch3d_clear",
    "TORIRS_GFX_RES_SPRITE_LOAD": "trspk_d3d8_fixed_event_sprite_load",
    "TORIRS_GFX_RES_SPRITE_UNLOAD": "trspk_d3d8_fixed_event_sprite_unload",
    "TORIRS_GFX_RES_FONT_LOAD": "trspk_d3d8_fixed_event_font_load",
    "TORIRS_GFX_STATE_CLEAR_RECT": "trspk_d3d8_fixed_event_state_clear_rect",
    "TORIRS_GFX_DRAW_MODEL": "trspk_d3d8_fixed_event_draw_model",
    "TORIRS_GFX_DRAW_SPRITE": "trspk_d3d8_fixed_event_draw_sprite",
    "TORIRS_GFX_DRAW_FONT": "trspk_d3d8_fixed_event_draw_font",
    "TORIRS_GFX_STATE_BEGIN_3D": "trspk_d3d8_fixed_event_state_begin_3d",
    "TORIRS_GFX_STATE_END_3D": "trspk_d3d8_fixed_event_state_end_3d",
    "TORIRS_GFX_STATE_BEGIN_2D": "trspk_d3d8_fixed_event_state_begin_2d",
    "TORIRS_GFX_STATE_END_2D": "trspk_d3d8_fixed_event_state_end_2d",
    "TORIRS_GFX_NONE": "trspk_d3d8_fixed_event_none_or_default",
}

hdr = r'''/**
 * GFX event handlers — bodies from d3d8_old Render(); included by d3d8_fixed_state.cpp.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d8.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "graphics/dash.h"
#include "graphics/dash_model_internal.h"
#include "graphics/shared_tables.h"
#include "graphics/uv_pnm.h"
#include "tori_rs_render.h"
}

#include "d3d8_fixed_internal.h"
#include "d3d8_fixed_state.h"
#include "trspk_d3d8_fixed.h"

#ifdef __cplusplus
#define TRSPK_D3D8_FIXED_EVT extern "C"
#else
#define TRSPK_D3D8_FIXED_EVT
#endif

#define D3D8_FIXED_EVENT_HEAD                                                                                           \
    D3D8FixedInternal* p = trspk_d3d8_fixed_priv(ctx->renderer);                                                        \
    if( !p || !p->device || !ctx->platform_renderer || !ctx->game )                                                     \
        return;                                                                                                         \
    IDirect3DDevice8* dev = p->device;                                                                                  \
    struct Platform2_Win32_Renderer_D3D8* renderer = ctx->platform_renderer;                                            \
    struct GGame* game = ctx->game;                                                                                     \
    const float u_clock = ctx->u_clock;                                                                                 \
    const int vp_w = ctx->frame_vp_w;                                                                                   \
    const int vp_h = ctx->frame_vp_h;                                                                                   \
    (void)renderer;                                                                                                     \
    (void)dev;                                                                                                          \
    (void)p

'''

cases = extract_cases()
buf = [hdr]

merged = cases

seen_fn = set()
for label, body in merged:
    fn = kind_to_fn.get(label)
    if not fn or fn in seen_fn:
        continue
    seen_fn.add(fn)
    tb = transform_body(body)
    buf.append(
        f"TRSPK_D3D8_FIXED_EVT void\n{fn}(\n"
        f"    TRSPK_D3D8Fixed_EventContext* ctx,\n"
        f"    const struct ToriRSRenderCommand* cmd)\n{{\n"
        f"    D3D8_FIXED_EVENT_HEAD;\n"
    )
    if tb.strip():
        buf.append("    ")
        buf.append(tb.replace("\n", "\n    "))
        buf.append("\n")
    buf.append("}\n\n")

buf.append(
    r"""
void
trspk_d3d8_fixed_event_res_anim_load(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    trspk_d3d8_fixed_event_none_or_default(ctx, cmd);
}

void
trspk_d3d8_fixed_event_batch3d_anim_add(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    trspk_d3d8_fixed_event_none_or_default(ctx, cmd);
}

"""
)

open(outp, "w", encoding="utf-8", newline="\n").write("".join(buf))
print("wrote", outp, "merged entries", len(merged), "unique handlers", len(seen_fn))
