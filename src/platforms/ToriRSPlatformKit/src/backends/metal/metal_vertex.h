#ifndef TORIRS_PLATFORM_KIT_METAL_VERTEX_H
#define TORIRS_PLATFORM_KIT_METAL_VERTEX_H

#include "../../../include/ToriRSPlatformKit/trspk_types.h"

#include <stdlib.h>
#include <string.h>

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

/** Structure-of-arrays layout (one malloc per field). */
typedef struct TRSPK_VertexMetalArray
{
    float* position_x;
    float* position_y;
    float* position_z;
    float* position_w;
    float* color_r;
    float* color_g;
    float* color_b;
    float* color_a;
    float* texcoord_u;
    float* texcoord_v;
    uint16_t* tex_id;
    uint16_t* uv_mode;
} TRSPK_VertexMetalArray;

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
trspk_metal_vertex_array_free(TRSPK_VertexMetalArray* a)
{
    if( !a )
        return;
    free(a->position);
    free(a->color);
    free(a->texcoord);
    free(a->tex_id);
    free(a->uv_mode);
    memset(a, 0, sizeof(*a));
}

/** Allocates SoA from TRSPK vertices; on failure leaves *out cleared. */
static inline bool
trspk_metal_vertex_array_from_trspk(
    TRSPK_VertexMetalArray* out,
    const TRSPK_Vertex* src,
    uint32_t n)
{
    if( !out || (n > 0u && !src) )
        return false;
    memset(out, 0, sizeof(*out));
    if( n == 0u )
        return true;
    const size_t nf = (size_t)n;
    out->position = (float*)malloc(nf * 4u * sizeof(float));
    out->color = (float*)malloc(nf * 4u * sizeof(float));
    out->texcoord = (float*)malloc(nf * 2u * sizeof(float));
    out->tex_id = (uint16_t*)malloc(nf * sizeof(uint16_t));
    out->uv_mode = (uint16_t*)malloc(nf * sizeof(uint16_t));
    if( !out->position || !out->color || !out->texcoord || !out->tex_id || !out->uv_mode )
    {
        trspk_metal_vertex_array_free(out);
        return false;
    }
    for( uint32_t i = 0; i < n; ++i )
    {
        memcpy(
            out->position + (size_t)i * 4u, src[i].position, 4u * sizeof(float));
        memcpy(out->color + (size_t)i * 4u, src[i].color, 4u * sizeof(float));
        memcpy(
            out->texcoord + (size_t)i * 2u, src[i].texcoord, 2u * sizeof(float));
        out->tex_id[i] = trspk_metal_vertex_u16(src[i].tex_id);
        out->uv_mode[i] = trspk_metal_vertex_u16(src[i].uv_mode);
    }
    return true;
}

#ifdef __cplusplus
}
#endif

#endif
