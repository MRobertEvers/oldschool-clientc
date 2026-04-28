#ifndef TORIRS_PLATFORM_KIT_WEBGL1_VERTEX_H
#define TORIRS_PLATFORM_KIT_WEBGL1_VERTEX_H

#include "../../../include/ToriRSPlatformKit/trspk_types.h"

#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TRSPK_VertexWebGL1
{
    float position[4];
    float color[4];
    float texcoord[2];
    float tex_id;
    float uv_mode;
} TRSPK_VertexWebGL1;

/** Structure-of-arrays layout (one malloc per field). */
typedef struct TRSPK_VertexWebGL1Array
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
    float* tex_id;      
    float* uv_mode;     
} TRSPK_VertexWebGL1Array;

static inline void
trspk_webgl1_vertex_convert(
    void* dst_vertices,
    const TRSPK_Vertex* src_vertices,
    uint32_t vertex_count)
{
    TRSPK_VertexWebGL1* dst = (TRSPK_VertexWebGL1*)dst_vertices;
    for( uint32_t i = 0; i < vertex_count; ++i )
    {
        memcpy(dst[i].position, src_vertices[i].position, sizeof(dst[i].position));
        memcpy(dst[i].color, src_vertices[i].color, sizeof(dst[i].color));
        memcpy(dst[i].texcoord, src_vertices[i].texcoord, sizeof(dst[i].texcoord));
        dst[i].tex_id = src_vertices[i].tex_id;
        dst[i].uv_mode = src_vertices[i].uv_mode;
    }
}

static inline void
trspk_webgl1_vertex_array_free(TRSPK_VertexWebGL1Array* a)
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
trspk_webgl1_vertex_array_from_trspk(
    TRSPK_VertexWebGL1Array* out,
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
    out->tex_id = (float*)malloc(nf * sizeof(float));
    out->uv_mode = (float*)malloc(nf * sizeof(float));
    if( !out->position || !out->color || !out->texcoord || !out->tex_id || !out->uv_mode )
    {
        trspk_webgl1_vertex_array_free(out);
        return false;
    }
    for( uint32_t i = 0; i < n; ++i )
    {
        memcpy(
            out->position + (size_t)i * 4u, src[i].position, 4u * sizeof(float));
        memcpy(out->color + (size_t)i * 4u, src[i].color, 4u * sizeof(float));
        memcpy(
            out->texcoord + (size_t)i * 2u, src[i].texcoord, 2u * sizeof(float));
        out->tex_id[i] = src[i].tex_id;
        out->uv_mode[i] = src[i].uv_mode;
    }
    return true;
}

#ifdef __cplusplus
}
#endif

#endif
