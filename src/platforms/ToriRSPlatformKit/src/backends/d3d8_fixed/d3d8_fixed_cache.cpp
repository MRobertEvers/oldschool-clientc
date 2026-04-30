/**
 * d3d8_fixed cache helpers: atlas refresh, batch submit/clear, lazy per-instance VBO creation.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d8.h>
#include <cstring>

#include "../../tools/trspk_batch16.h"
#include "../../tools/trspk_resource_cache.h"
#include "../../tools/trspk_lru_model_cache.h"

#include "d3d8_fixed_internal.h"
#include "d3d8_fixed_cache.h"

static const DWORD kFvfWorld = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;

/* === atlas refresh ========================================================= */

void
d3d8_fixed_cache_refresh_atlas(D3D8FixedInternal* p, IDirect3DDevice8* dev)
{
    if( !p || !dev || !p->cache )
        return;
    if( p->tex_mode != D3D8_FIXED_TEX_ATLAS )
        return;

    const uint8_t* pixels = trspk_resource_cache_get_atlas_pixels(p->cache);
    const uint32_t width  = trspk_resource_cache_get_atlas_width(p->cache);
    const uint32_t height = trspk_resource_cache_get_atlas_height(p->cache);
    if( !pixels || width == 0u || height == 0u )
        return;

    IDirect3DTexture8* tex = p->atlas_texture;
    if( !tex )
    {
        HRESULT hr = dev->CreateTexture(
            width, height, 1u, 0u, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex);
        if( FAILED(hr) || !tex )
        {
            trspk_d3d8_fixed_log(
                "cache_refresh_atlas: CreateTexture(%ux%u) failed hr=0x%08lX",
                (unsigned)width,
                (unsigned)height,
                (unsigned long)hr);
            return;
        }
        p->atlas_texture = tex;
    }

    D3DLOCKED_RECT lr;
    HRESULT hr_lk = tex->LockRect(0, &lr, nullptr, 0u);
    if( FAILED(hr_lk) )
        return;

    const uint32_t src_pitch = width * 4u;
    for( uint32_t y = 0u; y < height; ++y )
    {
        const uint8_t* src = pixels + (size_t)y * (size_t)src_pitch;
        uint8_t*       dst = reinterpret_cast<uint8_t*>(lr.pBits) +
                             (size_t)y * (size_t)lr.Pitch;
        /* RGBA (trspk) → BGRA (D3DFMT_A8R8G8B8 in memory on LE). */
        for( uint32_t x = 0u; x < width; ++x )
        {
            const uint8_t* s = src + (size_t)x * 4u;
            uint8_t*       d = dst + (size_t)x * 4u;
            d[0] = s[2]; /* B */
            d[1] = s[1]; /* G */
            d[2] = s[0]; /* R */
            d[3] = s[3]; /* A */
        }
    }
    /* Sentinel opaque white texel at atlas origin for untextured FVF vertices. */
    uint8_t* sentinel = reinterpret_cast<uint8_t*>(lr.pBits);
    sentinel[0] = sentinel[1] = sentinel[2] = sentinel[3] = 255u;
    tex->UnlockRect(0);
}

/* === batch submit ========================================================== */

void
d3d8_fixed_cache_batch_submit(
    D3D8FixedInternal* p,
    IDirect3DDevice8* dev,
    uint32_t batch_id)
{
    if( !p || !dev || !p->batch_staging || !p->cache )
        return;

    const uint32_t chunk_count = trspk_batch16_chunk_count(p->batch_staging);
    if( chunk_count > TRSPK_MAX_WEBGL1_CHUNKS )
    {
        trspk_d3d8_fixed_log(
            "cache_batch_submit: chunk_count=%u > TRSPK_MAX_WEBGL1_CHUNKS — skipping",
            (unsigned)chunk_count);
        return;
    }

    /* Release any old chunk buffers still alive from a previous submit. */
    for( uint32_t c = 0u; c < p->batch_chunk_count; ++c )
    {
        if( p->batch_chunk_vbos[c] )
        {
            p->batch_chunk_vbos[c]->Release();
            p->batch_chunk_vbos[c] = nullptr;
        }
        if( p->batch_chunk_ebos[c] )
        {
            p->batch_chunk_ebos[c]->Release();
            p->batch_chunk_ebos[c] = nullptr;
        }
    }
    p->batch_chunk_count = 0u;

    /* Upload each chunk VBO + EBO. */
    for( uint32_t c = 0u; c < chunk_count; ++c )
    {
        const void* vertices   = nullptr;
        const void* indices    = nullptr;
        uint32_t    vb_bytes   = 0u;
        uint32_t    ib_bytes   = 0u;
        trspk_batch16_get_chunk(p->batch_staging, c, &vertices, &vb_bytes, &indices, &ib_bytes);

        if( vb_bytes == 0u || ib_bytes == 0u || !vertices || !indices )
            continue;

        IDirect3DVertexBuffer8* vb = nullptr;
        HRESULT hrv = dev->CreateVertexBuffer(
            vb_bytes, D3DUSAGE_WRITEONLY, kFvfWorld, D3DPOOL_MANAGED, &vb);
        if( FAILED(hrv) || !vb )
        {
            trspk_d3d8_fixed_log(
                "cache_batch_submit: CreateVertexBuffer(chunk %u, %u bytes) failed hr=0x%08lX",
                (unsigned)c,
                (unsigned)vb_bytes,
                (unsigned long)hrv);
            continue;
        }
        BYTE* vdst = nullptr;
        if( SUCCEEDED(vb->Lock(0u, 0u, &vdst, 0u)) && vdst )
        {
            memcpy(vdst, vertices, (size_t)vb_bytes);
            vb->Unlock();
        }

        IDirect3DIndexBuffer8* ib = nullptr;
        HRESULT hri = dev->CreateIndexBuffer(
            ib_bytes, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib);
        if( FAILED(hri) || !ib )
        {
            trspk_d3d8_fixed_log(
                "cache_batch_submit: CreateIndexBuffer(chunk %u, %u bytes) failed hr=0x%08lX",
                (unsigned)c,
                (unsigned)ib_bytes,
                (unsigned long)hri);
            vb->Release();
            continue;
        }
        BYTE* idst = nullptr;
        if( SUCCEEDED(ib->Lock(0u, 0u, &idst, 0u)) && idst )
        {
            memcpy(idst, indices, (size_t)ib_bytes);
            ib->Unlock();
        }

        p->batch_chunk_vbos[c] = vb;
        p->batch_chunk_ebos[c] = ib;
    }
    p->batch_chunk_count = chunk_count;

    /* Register chunk handles in ResourceCache. */
    TRSPK_GPUHandle hvbos[TRSPK_MAX_WEBGL1_CHUNKS]{};
    TRSPK_GPUHandle hebos[TRSPK_MAX_WEBGL1_CHUNKS]{};
    for( uint32_t c = 0u; c < chunk_count; ++c )
    {
        hvbos[c] = (TRSPK_GPUHandle)(uintptr_t)p->batch_chunk_vbos[c];
        hebos[c] = (TRSPK_GPUHandle)(uintptr_t)p->batch_chunk_ebos[c];
    }
    trspk_resource_cache_batch_set_chunks(
        p->cache, (TRSPK_BatchId)batch_id, hvbos, hebos, chunk_count);

    /* Set per-entry poses in ResourceCache. */
    const uint32_t entry_count = trspk_batch16_entry_count(p->batch_staging);
    for( uint32_t e = 0u; e < entry_count; ++e )
    {
        const TRSPK_Batch16Entry* ent = trspk_batch16_get_entry(p->batch_staging, e);
        if( !ent )
            continue;
        const uint32_t pose_idx = trspk_resource_cache_allocate_pose_slot(
            p->cache,
            ent->model_id,
            ent->gpu_segment_slot,
            (int)ent->frame_index);
        TRSPK_ModelPose pose;
        memset(&pose, 0, sizeof(pose));
        pose.vbo          = hvbos[ent->chunk_index];
        pose.ebo          = hebos[ent->chunk_index];
        pose.vbo_offset   = ent->vbo_offset;
        pose.ebo_offset   = ent->ebo_offset;
        pose.element_count = ent->element_count;
        pose.batch_id     = (TRSPK_BatchId)batch_id;
        pose.chunk_index  = ent->chunk_index;
        pose.valid        = true;
        trspk_resource_cache_set_model_pose(p->cache, ent->model_id, pose_idx, &pose);
    }
}

/* === batch clear =========================================================== */

void
d3d8_fixed_cache_batch_clear(D3D8FixedInternal* p, uint32_t batch_id)
{
    if( !p || !p->cache )
        return;

    trspk_resource_cache_invalidate_poses_for_batch(p->cache, (TRSPK_BatchId)batch_id);
    TRSPK_BatchResource old =
        trspk_resource_cache_batch_clear(p->cache, (TRSPK_BatchId)batch_id);

    for( uint32_t c = 0u; c < old.chunk_count; ++c )
    {
        if( old.chunk_vbos[c] )
            reinterpret_cast<IDirect3DVertexBuffer8*>((uintptr_t)old.chunk_vbos[c])->Release();
        if( old.chunk_ebos[c] )
            reinterpret_cast<IDirect3DIndexBuffer8*>((uintptr_t)old.chunk_ebos[c])->Release();
    }

    /* Also clear our local chunk tracking if it was this batch. */
    for( uint32_t c = 0u; c < p->batch_chunk_count; ++c )
    {
        /* The ResourceCache owns the GPU handles we registered; just null our local copies
         * if they were released above (ResourceCache gave them back in old.chunk_*). */
        p->batch_chunk_vbos[c] = nullptr;
        p->batch_chunk_ebos[c] = nullptr;
    }
    p->batch_chunk_count = 0u;
}

/* === lazy LRU VBO creation ================================================= */

IDirect3DVertexBuffer8*
d3d8_fixed_lazy_ensure_lru_vbo(
    D3D8FixedInternal* p,
    IDirect3DDevice8* dev,
    uint16_t visual_id,
    uint8_t  seg,
    uint16_t frame_idx)
{
    if( !p || !dev || !p->cache )
        return nullptr;

    const uint64_t key = d3d8_fixed_lru_vbo_key(visual_id, seg, frame_idx);
    auto it = p->lru_vbo_by_key.find(key);
    if( it != p->lru_vbo_by_key.end() )
        return it->second;

    TRSPK_LruModelCache* lru = trspk_resource_cache_lru_model_cache(p->cache);
    if( !lru )
        return nullptr;

    const TRSPK_VertexBuffer* vb = trspk_lru_model_cache_get(lru, visual_id, seg, frame_idx);
    if( !vb || !vb->vertices.as_d3d8 || vb->vertex_count == 0u )
        return nullptr;

    const UINT vb_bytes = (UINT)(vb->vertex_count * sizeof(TRSPK_VertexD3D8));
    IDirect3DVertexBuffer8* gpu_vb = nullptr;
    HRESULT hr = dev->CreateVertexBuffer(
        vb_bytes, D3DUSAGE_WRITEONLY, kFvfWorld, D3DPOOL_MANAGED, &gpu_vb);
    if( FAILED(hr) || !gpu_vb )
    {
        trspk_d3d8_fixed_log(
            "lazy_ensure_lru_vbo: CreateVertexBuffer(%u bytes) failed hr=0x%08lX "
            "visual=%u seg=%u frame=%u",
            (unsigned)vb_bytes,
            (unsigned long)hr,
            (unsigned)visual_id,
            (unsigned)seg,
            (unsigned)frame_idx);
        return nullptr;
    }
    BYTE* dst = nullptr;
    if( SUCCEEDED(gpu_vb->Lock(0u, 0u, &dst, 0u)) && dst )
    {
        memcpy(dst, vb->vertices.as_d3d8, (size_t)vb_bytes);
        gpu_vb->Unlock();
    }

    p->lru_vbo_by_key[key] = gpu_vb;
    return gpu_vb;
}
