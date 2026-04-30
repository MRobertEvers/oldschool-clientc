#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <d3d8.h>
#include <stdio.h>

#include "../../tools/trspk_resource_cache.h"
#include "d3d8_internal.h"
#include "d3d8_vertex.h"
#include "trspk_d3d8.h"

extern "C" {

void
trspk_d3d8_cache_init_atlas(
    TRSPK_D3D8Renderer* r,
    uint32_t width,
    uint32_t height)
{
    if( !r || !r->cache )
        return;
    trspk_resource_cache_init_atlas(r->cache, width, height);
    trspk_d3d8_cache_refresh_atlas(r);
}

void
trspk_d3d8_cache_load_texture_128(
    TRSPK_D3D8Renderer* r,
    TRSPK_TextureId id,
    const uint8_t* rgba_128x128,
    float anim_u,
    float anim_v,
    bool opaque)
{
    if( !r || !r->cache )
        return;
    (void)trspk_resource_cache_load_texture_128(r->cache, id, rgba_128x128, anim_u, anim_v, opaque);
}

void
trspk_d3d8_cache_refresh_atlas(TRSPK_D3D8Renderer* r)
{
    if( !r || !r->cache || !r->com_device )
        return;
    const uint8_t* pixels = trspk_resource_cache_get_atlas_pixels(r->cache);
    const uint32_t width = trspk_resource_cache_get_atlas_width(r->cache);
    const uint32_t height = trspk_resource_cache_get_atlas_height(r->cache);
    if( !pixels || width == 0u || height == 0u )
        return;

    auto* dev = reinterpret_cast<IDirect3DDevice8*>((uintptr_t)r->com_device);
    IDirect3DTexture8* tex = nullptr;
    if( r->atlas_texture != 0u )
        tex = reinterpret_cast<IDirect3DTexture8*>((uintptr_t)r->atlas_texture);
    else
    {
        HRESULT hr = dev->CreateTexture(
            width,
            height,
            1u,
            0u,
            D3DFMT_A8R8G8B8,
            D3DPOOL_MANAGED,
            &tex);
        if( FAILED(hr) || !tex )
            return;
        r->atlas_texture = (GLuint)(uintptr_t)tex;
    }

    D3DLOCKED_RECT lr;
    HRESULT lrhr = tex->LockRect(0, &lr, nullptr, 0u);
    if( FAILED(lrhr) )
        return;
    const uint32_t src_pitch = width * 4u;
    for( uint32_t y = 0u; y < height; ++y )
    {
        const uint8_t* src_row = pixels + (size_t)y * (size_t)src_pitch;
        uint8_t* dst_row = reinterpret_cast<uint8_t*>(lr.pBits) + (size_t)y * (size_t)lr.Pitch;
        /* Legacy Win32 D3D8: DashTexture texels are DWORD ARGB (A in high byte; B,G,R in LE bytes).
         * trspk_dash_fill_rgba128 gives linear RGBA bytes (R,G,B,A). D3DLOCKED_RECT for
         * D3DFMT_A8R8G8B8 is B,G,R,A per pixel in memory — map s[0..3] RGBA -> d[0..3] BGRA. */
        for( uint32_t x = 0u; x < width; ++x )
        {
            const uint8_t* s = src_row + (size_t)x * 4u;
            uint8_t* d = dst_row + (size_t)x * 4u;
            d[0] = s[2];
            d[1] = s[1];
            d[2] = s[0];
            d[3] = s[3];
        }
    }
    /* Opaque white texel at atlas origin for untextured FVF vertices: world draw always uses
     * MODULATE(TEX0, DIFFUSE) with this atlas bound (legacy untextured path used no texture +
     * SELECTARG2). See trspk_d3d8_fvf_from_model_vertex untextured branch. */
    uint8_t* sentinel = reinterpret_cast<uint8_t*>(lr.pBits);
    sentinel[0] = sentinel[1] = sentinel[2] = sentinel[3] = 255u; /* B,G,R,A */
    tex->UnlockRect(0);

    dev->SetTexture(0u, tex);
    dev->SetTextureStageState(0u, D3DTSS_MINFILTER, D3DTEXF_POINT);
    dev->SetTextureStageState(0u, D3DTSS_MAGFILTER, D3DTEXF_POINT);
    dev->SetTextureStageState(0u, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetTextureStageState(0u, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
}

void
trspk_d3d8_cache_batch_submit(
    TRSPK_D3D8Renderer* r,
    TRSPK_BatchId batch_id,
    int usage_hint)
{
    (void)usage_hint;
    if( !r || !r->batch_staging || !r->cache || !r->com_device )
        return;
    const uint32_t chunk_count = trspk_batch16_chunk_count(r->batch_staging);
    if( chunk_count > TRSPK_MAX_WEBGL1_CHUNKS )
        return;

    auto* dev = reinterpret_cast<IDirect3DDevice8*>((uintptr_t)r->com_device);

    for( uint32_t i = 0; i < r->chunk_count; ++i )
    {
        if( r->batch_chunk_vbos[i] )
        {
            reinterpret_cast<IDirect3DVertexBuffer8*>((uintptr_t)r->batch_chunk_vbos[i])->Release();
            r->batch_chunk_vbos[i] = 0u;
        }
        if( r->batch_chunk_ebos[i] )
        {
            reinterpret_cast<IDirect3DIndexBuffer8*>((uintptr_t)r->batch_chunk_ebos[i])->Release();
            r->batch_chunk_ebos[i] = 0u;
        }
    }

    static const DWORD kFvfWorld = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;
    for( uint32_t c = 0; c < chunk_count; ++c )
    {
        const void* vertices = nullptr;
        const void* indices = nullptr;
        uint32_t vertex_bytes = 0u;
        uint32_t index_bytes = 0u;
        trspk_batch16_get_chunk(
            r->batch_staging, c, &vertices, &vertex_bytes, &indices, &index_bytes);

        IDirect3DVertexBuffer8* vb = nullptr;
        HRESULT hrv = dev->CreateVertexBuffer(
            vertex_bytes,
            D3DUSAGE_WRITEONLY,
            kFvfWorld,
            D3DPOOL_MANAGED,
            &vb);
        if( FAILED(hrv) || !vb )
            continue;
        BYTE* dst = nullptr;
        if( SUCCEEDED(vb->Lock(0u, 0u, &dst, 0u)) && dst )
        {
            memcpy(dst, vertices, (size_t)vertex_bytes);
            vb->Unlock();
        }

        IDirect3DIndexBuffer8* ib = nullptr;
        HRESULT hri = dev->CreateIndexBuffer(
            index_bytes,
            D3DUSAGE_WRITEONLY,
            D3DFMT_INDEX16,
            D3DPOOL_MANAGED,
            &ib);
        if( FAILED(hri) || !ib )
        {
            vb->Release();
            continue;
        }
        BYTE* idst = nullptr;
        if( SUCCEEDED(ib->Lock(0u, 0u, &idst, 0u)) && idst )
        {
            memcpy(idst, indices, (size_t)index_bytes);
            ib->Unlock();
        }

        r->batch_chunk_vbos[c] = (GLuint)(uintptr_t)vb;
        r->batch_chunk_ebos[c] = (GLuint)(uintptr_t)ib;
    }

    r->chunk_count = chunk_count;
    TRSPK_GPUHandle hvbos[TRSPK_MAX_WEBGL1_CHUNKS] = { 0 };
    TRSPK_GPUHandle hebos[TRSPK_MAX_WEBGL1_CHUNKS] = { 0 };
    for( uint32_t c = 0; c < chunk_count; ++c )
    {
        hvbos[c] = (TRSPK_GPUHandle)r->batch_chunk_vbos[c];
        hebos[c] = (TRSPK_GPUHandle)r->batch_chunk_ebos[c];
    }
    trspk_resource_cache_batch_set_chunks(r->cache, batch_id, hvbos, hebos, chunk_count);
    const uint32_t entry_count = trspk_batch16_entry_count(r->batch_staging);
    for( uint32_t i = 0; i < entry_count; ++i )
    {
        const TRSPK_Batch16Entry* e = trspk_batch16_get_entry(r->batch_staging, i);
        if( !e )
            continue;
        const uint32_t pose_idx = trspk_resource_cache_allocate_pose_slot(
            r->cache, e->model_id, e->gpu_segment_slot, e->frame_index);
        TRSPK_ModelPose pose = { 0 };
        pose.vbo = hvbos[e->chunk_index];
        pose.ebo = hebos[e->chunk_index];
        pose.vbo_offset = e->vbo_offset;
        pose.ebo_offset = e->ebo_offset;
        pose.element_count = e->element_count;
        pose.batch_id = batch_id;
        pose.chunk_index = e->chunk_index;
        pose.valid = true;
        trspk_resource_cache_set_model_pose(r->cache, e->model_id, pose_idx, &pose);
    }
}

void
trspk_d3d8_cache_batch_clear(TRSPK_D3D8Renderer* r, TRSPK_BatchId batch_id)
{
    if( !r || !r->cache )
        return;
    trspk_resource_cache_invalidate_poses_for_batch(r->cache, batch_id);
    TRSPK_BatchResource old = trspk_resource_cache_batch_clear(r->cache, batch_id);
    for( uint32_t i = 0; i < old.chunk_count; ++i )
    {
        GLuint v = (GLuint)old.chunk_vbos[i];
        GLuint e = (GLuint)old.chunk_ebos[i];
        if( v )
            reinterpret_cast<IDirect3DVertexBuffer8*>((uintptr_t)v)->Release();
        if( e )
            reinterpret_cast<IDirect3DIndexBuffer8*>((uintptr_t)e)->Release();
    }
}

void
trspk_d3d8_cache_unload_model(TRSPK_D3D8Renderer* r, TRSPK_ModelId id)
{
    if( r && r->cache )
        trspk_resource_cache_clear_model(r->cache, id);
}

} /* extern "C" */
