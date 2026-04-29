#include "trspk_vertex_format.h"

#include "../backends/d3d8/d3d8_vertex.h"
#include "../backends/metal/metal_vertex.h"
#include "../backends/webgl1/webgl1_vertex.h"

#include <string.h>

uint32_t
trspk_vertex_format_stride(TRSPK_VertexFormat fmt)
{
    switch( fmt )
    {
    case TRSPK_VERTEX_FORMAT_TRSPK:
        return (uint32_t)sizeof(TRSPK_Vertex);
    case TRSPK_VERTEX_FORMAT_WEBGL1:
        return (uint32_t)sizeof(TRSPK_VertexWebGL1);
    case TRSPK_VERTEX_FORMAT_METAL:
        return (uint32_t)sizeof(TRSPK_VertexMetal);
    case TRSPK_VERTEX_FORMAT_D3D8:
        return (uint32_t)sizeof(TRSPK_VertexD3D8);
    case TRSPK_VERTEX_FORMAT_NONE:
    case TRSPK_VERTEX_FORMAT_WEBGL1_SOAOS:
    case TRSPK_VERTEX_FORMAT_METAL_SOAOS:
    case TRSPK_VERTEX_FORMAT_D3D8_SOAOS:
        return 0u;
    default:
        return (uint32_t)sizeof(TRSPK_Vertex);
    }
}

void
trspk_vertex_format_convert(
    void* dst_vertices,
    const TRSPK_Vertex* src_vertices,
    uint32_t vertex_count,
    TRSPK_VertexFormat fmt,
    double d3d8_frame_clock)
{
    switch( fmt )
    {
    case TRSPK_VERTEX_FORMAT_TRSPK:
        (void)d3d8_frame_clock;
        memcpy(dst_vertices, src_vertices, (size_t)vertex_count * sizeof(TRSPK_Vertex));
        return;
    case TRSPK_VERTEX_FORMAT_WEBGL1:
        (void)d3d8_frame_clock;
        trspk_webgl1_vertex_convert(dst_vertices, src_vertices, vertex_count);
        return;
    case TRSPK_VERTEX_FORMAT_METAL:
        (void)d3d8_frame_clock;
        trspk_metal_vertex_convert(dst_vertices, src_vertices, vertex_count);
        return;
    case TRSPK_VERTEX_FORMAT_D3D8:
        trspk_d8_vertex_convert(dst_vertices, src_vertices, vertex_count, d3d8_frame_clock);
        return;
    case TRSPK_VERTEX_FORMAT_NONE:
    case TRSPK_VERTEX_FORMAT_WEBGL1_SOAOS:
    case TRSPK_VERTEX_FORMAT_METAL_SOAOS:
    case TRSPK_VERTEX_FORMAT_D3D8_SOAOS:
        (void)d3d8_frame_clock;
        return;
    default:
        (void)d3d8_frame_clock;
        memcpy(dst_vertices, src_vertices, (size_t)vertex_count * sizeof(TRSPK_Vertex));
        return;
    }
}
