#ifndef TORIRS_PLATFORM_KIT_TRSPK_VERTEX_FORMAT_H
#define TORIRS_PLATFORM_KIT_TRSPK_VERTEX_FORMAT_H

#include "../../include/ToriRSPlatformKit/trspk_types.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t
trspk_vertex_format_stride(TRSPK_VertexFormat fmt);

void
trspk_vertex_format_convert(
    void* dst_vertices,
    const TRSPK_Vertex* src_vertices,
    uint32_t vertex_count,
    TRSPK_VertexFormat fmt,
    double d3d8_frame_clock);

#ifdef __cplusplus
}
#endif

#endif
