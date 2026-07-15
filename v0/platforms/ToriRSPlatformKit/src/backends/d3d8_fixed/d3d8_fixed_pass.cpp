/**
 * d3d8_fixed pass submission: single ib_ring lock per frame, state-tracking + merged DIPs.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d8.h>
#include <cstring>

#include "d3d8_fixed_internal.h"
#include "d3d8_fixed_pass.h"
#include "d3d8_fixed_state.h"

static const DWORD kFvfWorld = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;

/* === helpers shared with per-id submit path ================================ */

static void
d3d8_pass_apply_texture_matrix_translate(IDirect3DDevice8* dev, float du, float dv)
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
d3d8_pass_disable_texture_transform(IDirect3DDevice8* dev)
{
    D3DMATRIX id;
    memset(&id, 0, sizeof(id));
    id._11 = id._22 = id._33 = id._44 = 1.0f;
    dev->SetTransform(D3DTS_TEXTURE0, &id);
    dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
}

/* === pass reset ============================================================ */

void
d3d8_fixed_reset_pass(D3D8FixedInternal* p)
{
    if( !p )
        return;
    p->pass_state.ib_scratch.clear();
    p->pass_state.subdraws.clear();
    p->pass_state.worlds.clear();
}

/* === pass submit =========================================================== */

void
d3d8_fixed_submit_pass(D3D8FixedInternal* p, IDirect3DDevice8* dev)
{
    if( !p || !dev )
        return;
    if( p->pass_state.subdraws.empty() )
        return;

    const std::vector<D3D8FixedSubDraw>& subdraws = p->pass_state.subdraws;
    const std::vector<D3DMATRIX>&        worlds    = p->pass_state.worlds;
    const std::vector<uint16_t>&         ib_scratch = p->pass_state.ib_scratch;

    /* Set 3D state (viewport, FVF, texture filters). */
    d3d8_fixed_ensure_pass(p, dev, PASS_3D);

    /* --- Atlas mode: bind the atlas once, disable alpha-test. --- */
    if( p->tex_mode == D3D8_FIXED_TEX_ATLAS )
    {
        if( p->atlas_texture )
            dev->SetTexture(0, p->atlas_texture);
        else
            dev->SetTexture(0, nullptr);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    }

    /* Lock ib_ring once with DISCARD to upload all staged indices. */
    const UINT ib_count_u16 = (UINT)ib_scratch.size();
    const UINT ib_bytes     = ib_count_u16 * sizeof(uint16_t);
    if( ib_bytes == 0u || !p->ib_ring )
    {
        d3d8_fixed_reset_pass(p);
        return;
    }
    if( ib_bytes > (UINT)p->ib_ring_size_bytes )
    {
        trspk_d3d8_fixed_log(
            "submit_pass: ib_scratch (%u bytes) > ib_ring (%zu bytes) — skipping frame pass",
            (unsigned)ib_bytes,
            p->ib_ring_size_bytes);
        d3d8_fixed_reset_pass(p);
        return;
    }

    BYTE* lock_dst = nullptr;
    HRESULT hr_lock = p->ib_ring->Lock(0u, ib_bytes, &lock_dst, D3DLOCK_DISCARD);
    if( FAILED(hr_lock) || !lock_dst )
    {
        trspk_d3d8_fixed_log(
            "submit_pass: ib_ring->Lock(DISCARD) failed hr=0x%08lX",
            (unsigned long)hr_lock);
        d3d8_fixed_reset_pass(p);
        return;
    }
    memcpy(lock_dst, ib_scratch.data(), (size_t)ib_bytes);
    p->ib_ring->Unlock();

    /* Walk subdraws with state-tracking; merge consecutive same-state runs. */
    IDirect3DVertexBuffer8* last_vbo        = nullptr;
    UINT                    last_vbo_off    = ~0u;
    uint16_t                last_world_idx  = 0xFFFFu;

    /* Per-id additional tracking */
    int   last_tex_id    = INT_MIN;
    bool  last_opaque    = true;
    float last_anim_sgn  = 0.0f;

    const UINT sd_count = (UINT)subdraws.size();
    UINT i = 0;
    while( i < sd_count )
    {
        const D3D8FixedSubDraw& first = subdraws[i];

        /* --- state changes ------------------------------------------------ */
        if( first.vbo != last_vbo )
        {
            dev->SetStreamSource(0, first.vbo, sizeof(D3D8WorldVertex));
            last_vbo = first.vbo;
        }
        if( first.vbo_offset_vertices != last_vbo_off )
        {
            dev->SetIndices(p->ib_ring, first.vbo_offset_vertices);
            last_vbo_off = first.vbo_offset_vertices;
        }
        if( first.world_idx != last_world_idx && first.world_idx < (uint16_t)worlds.size() )
        {
            dev->SetTransform(D3DTS_WORLD, &worlds[first.world_idx]);
            last_world_idx = first.world_idx;
        }

        if( p->tex_mode == D3D8_FIXED_TEX_PER_ID )
        {
            if( first.tex_id != last_tex_id || first.opaque != last_opaque ||
                first.anim_signed != last_anim_sgn )
            {
                if( first.tex_id < 0 )
                {
                    dev->SetTexture(0, nullptr);
                    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
                    d3d8_pass_disable_texture_transform(dev);
                    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
                }
                else
                {
                    auto tex_it = p->texture_by_id.find(first.tex_id);
                    dev->SetTexture(
                        0, tex_it != p->texture_by_id.end() ? tex_it->second : nullptr);
                    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
                    if( first.anim_signed > 0.0f )
                        d3d8_pass_apply_texture_matrix_translate(
                            dev, p->frame_u_clock * first.anim_signed, 0.0f);
                    else if( first.anim_signed < 0.0f )
                        d3d8_pass_apply_texture_matrix_translate(
                            dev, 0.0f, p->frame_u_clock * first.anim_signed);
                    else
                        d3d8_pass_disable_texture_transform(dev);
                    dev->SetRenderState(
                        D3DRS_ALPHATESTENABLE, first.opaque ? FALSE : TRUE);
                }
                last_tex_id   = first.tex_id;
                last_opaque   = first.opaque;
                last_anim_sgn = first.anim_signed;
            }
        }

        /* --- find end of mergeable run ------------------------------------- */
        UINT j = i + 1u;
        while( j < sd_count )
        {
            const D3D8FixedSubDraw& next = subdraws[j];
            bool same = (next.vbo == first.vbo) &&
                        (next.vbo_offset_vertices == first.vbo_offset_vertices) &&
                        (next.world_idx == first.world_idx);
            if( p->tex_mode == D3D8_FIXED_TEX_PER_ID )
            {
                same = same && (next.tex_id == first.tex_id) &&
                       (next.opaque == first.opaque) &&
                       (next.anim_signed == first.anim_signed);
            }
            if( !same )
                break;
            ++j;
        }

        /* --- issue merged DIP ---------------------------------------------- */
        const UINT merged_start  = first.pool_start_indices;
        const D3D8FixedSubDraw& last_sd = subdraws[j - 1u];
        const UINT merged_count  = (last_sd.pool_start_indices + last_sd.index_count) - merged_start;
        const UINT tri_count     = merged_count / 3u;

        /* NumVertices: max index accessible = face_count * 3 for per-instance;
         * or element_count for batch. We scan the merged range to find max index. */
        uint16_t max_local = 0u;
        const uint16_t* base = ib_scratch.data() + merged_start;
        for( UINT k = 0u; k < merged_count; ++k )
        {
            if( base[k] > max_local )
                max_local = base[k];
        }
        const UINT num_vertices = (UINT)max_local + 1u;

        if( tri_count > 0u )
        {
            HRESULT hr_dip = dev->DrawIndexedPrimitive(
                D3DPT_TRIANGLELIST,
                0u,
                num_vertices,
                merged_start,
                tri_count);
            if( FAILED(hr_dip) )
                trspk_d3d8_fixed_log(
                    "submit_pass: DIP failed hr=0x%08lX start=%u tris=%u",
                    (unsigned long)hr_dip,
                    (unsigned)merged_start,
                    (unsigned)tri_count);
            p->debug_model_draws++;
            p->debug_triangles += tri_count;
        }

        i = j;
    }

    if( p->tex_mode == D3D8_FIXED_TEX_PER_ID )
    {
        d3d8_pass_disable_texture_transform(dev);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    }

    d3d8_fixed_reset_pass(p);
}
