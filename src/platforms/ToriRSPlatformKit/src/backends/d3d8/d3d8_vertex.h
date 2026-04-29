#ifndef TORIRS_PLATFORM_KIT_D3D8_VERTEX_H
#define TORIRS_PLATFORM_KIT_D3D8_VERTEX_H

#include "../../../include/ToriRSPlatformKit/trspk_types.h"

#include <stdbool.h>
#include <stdint.h>
#include "../../../include/ToriRSPlatformKit/trspk_vertex_soaos_config.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <malloc.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Interleaved vertex for D3D8 fixed-function: D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1. */
typedef struct TRSPK_VertexD3D8
{
    float x;
    float y;
    float z;
    uint32_t diffuse;
    float u;
    float v;
} TRSPK_VertexD3D8;

typedef struct TRSPK_VertexD3D8SoaosBlock
{
    TRSPK_VERTEX_SOAOS_MEMBER_ALIGN(TRSPK_VERTEX_SOAOS_ALIGNMENT) float position_x[TRSPK_VERTEX_SIMD_LANES];
    TRSPK_VERTEX_SOAOS_MEMBER_ALIGN(TRSPK_VERTEX_SOAOS_ALIGNMENT) float position_y[TRSPK_VERTEX_SIMD_LANES];
    TRSPK_VERTEX_SOAOS_MEMBER_ALIGN(TRSPK_VERTEX_SOAOS_ALIGNMENT) float position_z[TRSPK_VERTEX_SIMD_LANES];
    TRSPK_VERTEX_SOAOS_MEMBER_ALIGN(TRSPK_VERTEX_SOAOS_ALIGNMENT) float position_w[TRSPK_VERTEX_SIMD_LANES];
    TRSPK_VERTEX_SOAOS_MEMBER_ALIGN(TRSPK_VERTEX_SOAOS_ALIGNMENT) float color_r[TRSPK_VERTEX_SIMD_LANES];
    TRSPK_VERTEX_SOAOS_MEMBER_ALIGN(TRSPK_VERTEX_SOAOS_ALIGNMENT) float color_g[TRSPK_VERTEX_SIMD_LANES];
    TRSPK_VERTEX_SOAOS_MEMBER_ALIGN(TRSPK_VERTEX_SOAOS_ALIGNMENT) float color_b[TRSPK_VERTEX_SIMD_LANES];
    TRSPK_VERTEX_SOAOS_MEMBER_ALIGN(TRSPK_VERTEX_SOAOS_ALIGNMENT) float color_a[TRSPK_VERTEX_SIMD_LANES];
    TRSPK_VERTEX_SOAOS_MEMBER_ALIGN(TRSPK_VERTEX_SOAOS_ALIGNMENT) float texcoord_u[TRSPK_VERTEX_SIMD_LANES];
    TRSPK_VERTEX_SOAOS_MEMBER_ALIGN(TRSPK_VERTEX_SOAOS_ALIGNMENT) float texcoord_v[TRSPK_VERTEX_SIMD_LANES];
    TRSPK_VERTEX_SOAOS_MEMBER_ALIGN(TRSPK_VERTEX_SOAOS_ALIGNMENT) float tex_id[TRSPK_VERTEX_SIMD_LANES];
    TRSPK_VERTEX_SOAOS_MEMBER_ALIGN(TRSPK_VERTEX_SOAOS_ALIGNMENT) float uv_mode[TRSPK_VERTEX_SIMD_LANES];
} TRSPK_VertexD3D8SoaosBlock;

typedef struct TRSPK_VertexD3D8Soaos
{
    uint32_t vertex_count;
    uint32_t block_count;
    TRSPK_VertexD3D8SoaosBlock* blocks;
} TRSPK_VertexD3D8Soaos;

static inline void*
trspk_d8_soaos_aligned_alloc(size_t alignment, size_t size)
{
    if( size == 0u )
        return NULL;
    size_t sz = size;
    if( (sz % alignment) != 0u )
        sz = ((sz + alignment - 1u) / alignment) * alignment;
#if defined(_WIN32)
    return _aligned_malloc(sz, alignment);
#else
    return aligned_alloc(alignment, sz);
#endif
}

static inline void
trspk_d8_soaos_aligned_free(void* p)
{
    if( !p )
        return;
#if defined(_WIN32)
    _aligned_free(p);
#else
    free(p);
#endif
}

void
trspk_d8_vertex_convert(
    void* dst_vertices,
    const TRSPK_Vertex* src_vertices,
    uint32_t vertex_count,
    double frame_clock);

static inline void
trspk_d8_vertex_soaos_free(TRSPK_VertexD3D8Soaos* a)
{
    if( !a )
        return;
    trspk_d8_soaos_aligned_free(a->blocks);
    memset(a, 0, sizeof(*a));
}

static inline bool
trspk_d8_vertex_soaos_alloc(TRSPK_VertexD3D8Soaos* out, uint32_t n)
{
    if( !out )
        return false;
    memset(out, 0, sizeof(*out));
    if( n == 0u )
        return true;
    out->vertex_count = n;
    const uint32_t bc = TRSPK_VERTEX_SOAOS_BLOCK_COUNT(n);
    out->block_count = bc;
    const size_t bytes = (size_t)bc * sizeof(TRSPK_VertexD3D8SoaosBlock);
    void* p = trspk_d8_soaos_aligned_alloc((size_t)TRSPK_VERTEX_SOAOS_ALIGNMENT, bytes);
    if( !p )
        return false;
    out->blocks = (TRSPK_VertexD3D8SoaosBlock*)p;
    memset(out->blocks, 0, bytes);
    return true;
}

static inline bool
trspk_d8_vertex_soaos_from_trspk(
    TRSPK_VertexD3D8Soaos* out,
    const TRSPK_Vertex* src,
    uint32_t n)
{
    if( !out || (n > 0u && !src) )
        return false;
    if( !trspk_d8_vertex_soaos_alloc(out, n) )
        return false;
    if( n == 0u )
        return true;
    for( uint32_t i = 0; i < n; ++i )
    {
        const uint32_t bi = TRSPK_VERTEX_SOAOS_BLOCK_IDX(i);
        const uint32_t li = TRSPK_VERTEX_SOAOS_LANE_IDX(i);
        TRSPK_VertexD3D8SoaosBlock* blk = &out->blocks[bi];
        blk->position_x[li] = src[i].position[0];
        blk->position_y[li] = src[i].position[1];
        blk->position_z[li] = src[i].position[2];
        blk->position_w[li] = src[i].position[3];
        blk->color_r[li] = src[i].color[0];
        blk->color_g[li] = src[i].color[1];
        blk->color_b[li] = src[i].color[2];
        blk->color_a[li] = src[i].color[3];
        blk->texcoord_u[li] = src[i].texcoord[0];
        blk->texcoord_v[li] = src[i].texcoord[1];
        blk->tex_id[li] = src[i].tex_id;
        blk->uv_mode[li] = src[i].uv_mode;
    }
    return true;
}

#ifdef __cplusplus
}
#endif

#endif
