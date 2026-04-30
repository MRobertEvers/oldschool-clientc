#ifndef TORIRS_PLATFORM_KIT_D3D8_FIXED_CACHE_H
#define TORIRS_PLATFORM_KIT_D3D8_FIXED_CACHE_H

#include "d3d8_fixed_internal.h"

/**
 * Upload ResourceCache atlas pixels into p->atlas_texture (ATLAS mode).
 * Creates the D3D8 managed texture on first call. No-op in PER_ID mode.
 */
void
d3d8_fixed_cache_refresh_atlas(D3D8FixedInternal* p, IDirect3DDevice8* dev);

/**
 * Upload batch_staging chunks as GPU VBOs/EBOs for batch_id, register poses in
 * ResourceCache. Releases any previously registered chunk buffers for this batch_id.
 */
void
d3d8_fixed_cache_batch_submit(
    D3D8FixedInternal* p,
    IDirect3DDevice8* dev,
    uint32_t batch_id);

/** Invalidate batch poses and release associated GPU chunk buffers. */
void
d3d8_fixed_cache_batch_clear(D3D8FixedInternal* p, uint32_t batch_id);

/**
 * Return (or lazily create) a GPU VBO for the LRU entry keyed by (visual_id, seg, frame_idx).
 * The VBO is a D3DPOOL_MANAGED copy of the LRU's TRSPK_VertexD3D8 vertex data.
 * Returns nullptr if the LRU entry does not exist.
 */
IDirect3DVertexBuffer8*
d3d8_fixed_lazy_ensure_lru_vbo(
    D3D8FixedInternal* p,
    IDirect3DDevice8* dev,
    uint16_t visual_id,
    uint8_t seg,
    uint16_t frame_idx);

#endif
