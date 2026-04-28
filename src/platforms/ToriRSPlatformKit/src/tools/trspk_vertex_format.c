#include "trspk_vertex_format.h"

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
    case TRSPK_VERTEX_FORMAT_NONE:
    case TRSPK_VERTEX_FORMAT_WEBGL1_ARRAY:
    case TRSPK_VERTEX_FORMAT_METAL_ARRAY:
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
    TRSPK_VertexFormat fmt)
{
    switch( fmt )
    {
    case TRSPK_VERTEX_FORMAT_TRSPK:
        memcpy(dst_vertices, src_vertices, (size_t)vertex_count * sizeof(TRSPK_Vertex));
        return;
    case TRSPK_VERTEX_FORMAT_WEBGL1:
        trspk_webgl1_vertex_convert(dst_vertices, src_vertices, vertex_count);
        return;
    case TRSPK_VERTEX_FORMAT_METAL:
        trspk_metal_vertex_convert(dst_vertices, src_vertices, vertex_count);
        return;
    case TRSPK_VERTEX_FORMAT_NONE:
    case TRSPK_VERTEX_FORMAT_WEBGL1_ARRAY:
    case TRSPK_VERTEX_FORMAT_METAL_ARRAY:
        return;
    default:
        memcpy(dst_vertices, src_vertices, (size_t)vertex_count * sizeof(TRSPK_Vertex));
        return;
    }
}
