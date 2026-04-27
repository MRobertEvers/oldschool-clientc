#ifndef TORIRS_PLATFORM_KIT_METAL_VERTEX_H
#define TORIRS_PLATFORM_KIT_METAL_VERTEX_H

#include "../../../include/ToriRSPlatformKit/trspk_types.h"

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GPU3DMeshVertexMetal
{
    float position[4];
    float color[4];
    float texcoord[2];
    uint16_t tex_id;
    uint16_t uv_mode;
} GPU3DMeshVertexMetal;

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
    GPU3DMeshVertexMetal* dst = (GPU3DMeshVertexMetal*)dst_vertices;
    for( uint32_t i = 0; i < vertex_count; ++i )
    {
        memcpy(dst[i].position, src_vertices[i].position, sizeof(dst[i].position));
        memcpy(dst[i].color, src_vertices[i].color, sizeof(dst[i].color));
        memcpy(dst[i].texcoord, src_vertices[i].texcoord, sizeof(dst[i].texcoord));
        dst[i].tex_id = trspk_metal_vertex_u16(src_vertices[i].tex_id);
        dst[i].uv_mode = trspk_metal_vertex_u16(src_vertices[i].uv_mode);
    }
}

static inline const TRSPK_VertexFormat*
trspk_metal_vertex_format(void)
{
    static const TRSPK_VertexFormat fmt = {
        (uint32_t)sizeof(GPU3DMeshVertexMetal),
        trspk_metal_vertex_convert,
    };
    return &fmt;
}

#ifdef __cplusplus
}
#endif

#endif
