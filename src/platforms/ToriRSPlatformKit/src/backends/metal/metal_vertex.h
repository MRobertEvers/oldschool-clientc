#ifndef TORIRS_PLATFORM_KIT_METAL_VERTEX_H
#define TORIRS_PLATFORM_KIT_METAL_VERTEX_H

#include "../../../include/ToriRSPlatformKit/trspk_types.h"
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

typedef struct TRSPK_VertexMetal
{
    float position[4];
    float color[4];
    float texcoord[2];
    uint16_t tex_id;
    uint16_t uv_mode;
} TRSPK_VertexMetal;

/** One AoSoA block: per-field lanes for SIMD (alignment matches register width). */
typedef struct TRSPK_VertexMetalSoaosBlock
{
    _Alignas(TRSPK_VERTEX_SOAOS_ALIGNMENT) float position_x[TRSPK_VERTEX_SIMD_LANES];
    _Alignas(TRSPK_VERTEX_SOAOS_ALIGNMENT) float position_y[TRSPK_VERTEX_SIMD_LANES];
    _Alignas(TRSPK_VERTEX_SOAOS_ALIGNMENT) float position_z[TRSPK_VERTEX_SIMD_LANES];
    _Alignas(TRSPK_VERTEX_SOAOS_ALIGNMENT) float position_w[TRSPK_VERTEX_SIMD_LANES];
    _Alignas(TRSPK_VERTEX_SOAOS_ALIGNMENT) float color_r[TRSPK_VERTEX_SIMD_LANES];
    _Alignas(TRSPK_VERTEX_SOAOS_ALIGNMENT) float color_g[TRSPK_VERTEX_SIMD_LANES];
    _Alignas(TRSPK_VERTEX_SOAOS_ALIGNMENT) float color_b[TRSPK_VERTEX_SIMD_LANES];
    _Alignas(TRSPK_VERTEX_SOAOS_ALIGNMENT) float color_a[TRSPK_VERTEX_SIMD_LANES];
    _Alignas(TRSPK_VERTEX_SOAOS_ALIGNMENT) float texcoord_u[TRSPK_VERTEX_SIMD_LANES];
    _Alignas(TRSPK_VERTEX_SOAOS_ALIGNMENT) float texcoord_v[TRSPK_VERTEX_SIMD_LANES];
    _Alignas(TRSPK_VERTEX_SOAOS_ALIGNMENT) uint16_t tex_id[TRSPK_VERTEX_SIMD_LANES];
    _Alignas(TRSPK_VERTEX_SOAOS_ALIGNMENT) uint16_t uv_mode[TRSPK_VERTEX_SIMD_LANES];
} TRSPK_VertexMetalSoaosBlock;

/** Array of blocks (AoSoA); single aligned allocation for `blocks`. */
typedef struct TRSPK_VertexMetalSoaos
{
    uint32_t vertex_count;
    uint32_t block_count;
    TRSPK_VertexMetalSoaosBlock* blocks;
} TRSPK_VertexMetalSoaos;

static inline void*
trspk_soaos_aligned_alloc(size_t alignment, size_t size)
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
trspk_soaos_aligned_free(void* p)
{
    if( !p )
        return;
#if defined(_WIN32)
    _aligned_free(p);
#else
    free(p);
#endif
}

static inline uint16_t
trspk_metal_vertex_u16(float v)
{
    if( v <= 0.0f )
        return 0u;
    if( v >= 65535.0f )
        return 65535u;
    return (uint16_t)v;
}

static inline void
trspk_metal_vertex_convert(
    void* dst_vertices,
    const TRSPK_Vertex* src_vertices,
    uint32_t vertex_count)
{
    TRSPK_VertexMetal* dst = (TRSPK_VertexMetal*)dst_vertices;
    for( uint32_t i = 0; i < vertex_count; ++i )
    {
        memcpy(dst[i].position, src_vertices[i].position, sizeof(dst[i].position));
        memcpy(dst[i].color, src_vertices[i].color, sizeof(dst[i].color));
        memcpy(dst[i].texcoord, src_vertices[i].texcoord, sizeof(dst[i].texcoord));
        dst[i].tex_id = trspk_metal_vertex_u16(src_vertices[i].tex_id);
        dst[i].uv_mode = trspk_metal_vertex_u16(src_vertices[i].uv_mode);
    }
}

static inline void
trspk_metal_vertex_soaos_free(TRSPK_VertexMetalSoaos* a)
{
    if( !a )
        return;
    trspk_soaos_aligned_free(a->blocks);
    memset(a, 0, sizeof(*a));
}

/** Allocates empty AoSoA buffers for n vertices; on failure leaves *out cleared. */
static inline bool
trspk_metal_vertex_soaos_alloc(TRSPK_VertexMetalSoaos* out, uint32_t n)
{
    if( !out )
        return false;
    memset(out, 0, sizeof(*out));
    if( n == 0u )
        return true;
    out->vertex_count = n;
    const uint32_t bc = TRSPK_VERTEX_SOAOS_BLOCK_COUNT(n);
    out->block_count = bc;
    const size_t bytes = (size_t)bc * sizeof(TRSPK_VertexMetalSoaosBlock);
    void* p = trspk_soaos_aligned_alloc((size_t)TRSPK_VERTEX_SOAOS_ALIGNMENT, bytes);
    if( !p )
        return false;
    out->blocks = (TRSPK_VertexMetalSoaosBlock*)p;
    memset(out->blocks, 0, bytes);
    return true;
}

/** Allocates AoSoA from TRSPK vertices; on failure leaves *out cleared. */
static inline bool
trspk_metal_vertex_soaos_from_trspk(
    TRSPK_VertexMetalSoaos* out,
    const TRSPK_Vertex* src,
    uint32_t n)
{
    if( !out || (n > 0u && !src) )
        return false;
    if( !trspk_metal_vertex_soaos_alloc(out, n) )
        return false;
    if( n == 0u )
        return true;
    for( uint32_t i = 0; i < n; ++i )
    {
        const uint32_t bi = TRSPK_VERTEX_SOAOS_BLOCK_IDX(i);
        const uint32_t li = TRSPK_VERTEX_SOAOS_LANE_IDX(i);
        TRSPK_VertexMetalSoaosBlock* blk = &out->blocks[bi];
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
        blk->tex_id[li] = trspk_metal_vertex_u16(src[i].tex_id);
        blk->uv_mode[li] = trspk_metal_vertex_u16(src[i].uv_mode);
    }
    return true;
}

#ifdef __cplusplus
}
#endif

#endif
