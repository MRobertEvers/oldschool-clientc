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
    free(a->position_x);
    free(a->position_y);
    free(a->position_z);
    free(a->position_w);
    free(a->color_r);
    free(a->color_g);
    free(a->color_b);
    free(a->color_a);
    free(a->texcoord_u);
    free(a->texcoord_v);
    free(a->tex_id);
    free(a->uv_mode);
    memset(a, 0, sizeof(*a));
}

/** Allocates empty SoA buffers for n vertices; on failure leaves *out cleared. */
static inline bool
trspk_metal_vertex_array_alloc(TRSPK_VertexMetalArray* out, uint32_t n)
{
    if( !out )
        return false;
    memset(out, 0, sizeof(*out));
    if( n == 0u )
        return true;
    const size_t nf = (size_t)n;
    out->position_x = (float*)malloc(nf * sizeof(float));
    out->position_y = (float*)malloc(nf * sizeof(float));
    out->position_z = (float*)malloc(nf * sizeof(float));
    out->position_w = (float*)malloc(nf * sizeof(float));
    out->color_r = (float*)malloc(nf * sizeof(float));
    out->color_g = (float*)malloc(nf * sizeof(float));
    out->color_b = (float*)malloc(nf * sizeof(float));
    out->color_a = (float*)malloc(nf * sizeof(float));
    out->texcoord_u = (float*)malloc(nf * sizeof(float));
    out->texcoord_v = (float*)malloc(nf * sizeof(float));
    out->tex_id = (uint16_t*)malloc(nf * sizeof(uint16_t));
    out->uv_mode = (uint16_t*)malloc(nf * sizeof(uint16_t));
    if( !out->position_x || !out->position_y || !out->position_z || !out->position_w || !out->color_r || !out->color_g || !out->color_b || !out->color_a || !out->texcoord_u || !out->texcoord_v || !out->tex_id || !out->uv_mode )
    {
        trspk_metal_vertex_array_free(out);
        return false;
    }
    return true;
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
    if( !trspk_metal_vertex_array_alloc(out, n) )
        return false;
    if( n == 0u )
        return true;
    for( uint32_t i = 0; i < n; ++i )
    {
        out->position_x[i] = src[i].position[0];
        out->position_y[i] = src[i].position[1];
        out->position_z[i] = src[i].position[2];
        out->position_w[i] = src[i].position[3];
        out->color_r[i] = src[i].color[0];
        out->color_g[i] = src[i].color[1];
        out->color_b[i] = src[i].color[2];
        out->color_a[i] = src[i].color[3];
        out->texcoord_u[i] = src[i].texcoord[0];
        out->texcoord_v[i] = src[i].texcoord[1];
        out->tex_id[i] = trspk_metal_vertex_u16(src[i].tex_id);
        out->uv_mode[i] = trspk_metal_vertex_u16(src[i].uv_mode);
    }
    return true;
}

#ifdef __cplusplus
}
#endif

#endif
