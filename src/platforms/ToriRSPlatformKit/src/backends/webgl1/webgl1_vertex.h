#ifndef TORIRS_PLATFORM_KIT_WEBGL1_VERTEX_H
#define TORIRS_PLATFORM_KIT_WEBGL1_VERTEX_H

#include "../../../include/ToriRSPlatformKit/trspk_types.h"

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

#ifdef __cplusplus
}
#endif

#endif
