#ifndef TORIRS_PLATFORM_KIT_TRSPK_VERTEX_BUFFER_H
#define TORIRS_PLATFORM_KIT_TRSPK_VERTEX_BUFFER_H

#include "../../include/ToriRSPlatformKit/trspk_types.h"
#include "../backends/metal/metal_vertex.h"
#include "../backends/webgl1/webgl1_vertex.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Expanded per-corner mesh (distinct from TRSPK_ModelId). */
typedef struct TRSPK_VertexBuffer
{
    uint32_t vertex_count;
    uint32_t index_count;
    union
    {
        uint32_t* as_u32;
        uint16_t* as_u16;
    } indices;
    TRSPK_VertexBufferStatus status;
    TRSPK_VertexFormat format;
    union
    {
        TRSPK_Vertex* as_trspk;
        TRSPK_VertexWebGL1Array as_webgl1_array;
        TRSPK_VertexWebGL1* as_webgl1;
        TRSPK_VertexMetal* as_metal;
        TRSPK_VertexMetalArray as_metal_array;
    } vertices;
} TRSPK_VertexBuffer;

void
trspk_vertex_buffer_free(TRSPK_VertexBuffer* vb);

/** True if mesh holds non-NULL vertex storage matching vb->format. */
bool
trspk_vertex_buffer_has_vertex_payload(const TRSPK_VertexBuffer* vb);

/**
 * Take ownership of TRSPK vertex buffer and index list; sets format to TRSPK.
 * On success, previous vb contents are cleared. On failure, vb left in NONE.
 */
bool
trspk_vertex_buffer_set_trspk_expanded(
    TRSPK_VertexBuffer* vb,
    TRSPK_Vertex* vertices_ownership,
    uint32_t vertex_count,
    uint32_t* indices_ownership,
    uint32_t index_count);

/**
 * Convert in place from TRSPK to dst_format. Requires vb->format == TRSPK.
 * dst_format must not be NONE; TRSPK is a no-op.
 */
bool
trspk_vertex_buffer_convert_from_trspk(
    TRSPK_VertexBuffer* vb, TRSPK_VertexFormat dst_format);

bool
trspk_vertex_buffer_from_face_slice_u32_untextured(
    uint32_t start_face,
    uint32_t slice_face_count,
    uint32_t vertex_count,
    const int16_t* vertices_x,
    const int16_t* vertices_y,
    const int16_t* vertices_z,
    const uint16_t* faces_a,
    const uint16_t* faces_b,
    const uint16_t* faces_c,
    const uint16_t* faces_a_color_hsl16,
    const uint16_t* faces_b_color_hsl16,
    const uint16_t* faces_c_color_hsl16,
    const uint8_t* face_alphas,
    const int32_t* face_infos,
    const TRSPK_BakeTransform* bake,
    TRSPK_VertexBuffer* vb);

bool
trspk_vertex_buffer_from_face_slice_u32_textured(
    uint32_t start_face,
    uint32_t slice_face_count,
    uint32_t vertex_count,
    const int16_t* vertices_x,
    const int16_t* vertices_y,
    const int16_t* vertices_z,
    const uint16_t* faces_a,
    const uint16_t* faces_b,
    const uint16_t* faces_c,
    const uint16_t* faces_a_color_hsl16,
    const uint16_t* faces_b_color_hsl16,
    const uint16_t* faces_c_color_hsl16,
    const int16_t* faces_textures,
    const uint16_t* textured_faces,
    const uint16_t* textured_faces_a,
    const uint16_t* textured_faces_b,
    const uint16_t* textured_faces_c,
    const uint8_t* face_alphas,
    const int32_t* face_infos,
    TRSPK_UVMode uv_mode,
    const TRSPK_BakeTransform* bake,
    TRSPK_VertexBuffer* vb);

bool
trspk_vertex_buffer_from_face_slice_u16_untextured(
    uint32_t start_face,
    uint32_t slice_face_count,
    uint32_t vertex_count,
    const int16_t* vertices_x,
    const int16_t* vertices_y,
    const int16_t* vertices_z,
    const uint16_t* faces_a,
    const uint16_t* faces_b,
    const uint16_t* faces_c,
    const uint16_t* faces_a_color_hsl16,
    const uint16_t* faces_b_color_hsl16,
    const uint16_t* faces_c_color_hsl16,
    const uint8_t* face_alphas,
    const int32_t* face_infos,
    const TRSPK_BakeTransform* bake,
    TRSPK_VertexBuffer* vb);

bool
trspk_vertex_buffer_from_face_slice_u16_textured(
    uint32_t start_face,
    uint32_t slice_face_count,
    uint32_t vertex_count,
    const int16_t* vertices_x,
    const int16_t* vertices_y,
    const int16_t* vertices_z,
    const uint16_t* faces_a,
    const uint16_t* faces_b,
    const uint16_t* faces_c,
    const uint16_t* faces_a_color_hsl16,
    const uint16_t* faces_b_color_hsl16,
    const uint16_t* faces_c_color_hsl16,
    const int16_t* faces_textures,
    const uint16_t* textured_faces,
    const uint16_t* textured_faces_a,
    const uint16_t* textured_faces_b,
    const uint16_t* textured_faces_c,
    const uint8_t* face_alphas,
    const int32_t* face_infos,
    TRSPK_UVMode uv_mode,
    const TRSPK_BakeTransform* bake,
    TRSPK_VertexBuffer* vb);


bool
trspk_vertex_buffer_write_texturedi16(
    uint32_t vertex_count,
    uint32_t face_count,
    const int16_t* vertices_x,
    const int16_t* vertices_y,
    const int16_t* vertices_z,
    const uint16_t* faces_a,
    const uint16_t* faces_b,
    const uint16_t* faces_c,
    const uint16_t* faces_a_color_hsl16,
    const uint16_t* faces_b_color_hsl16,
    const uint16_t* faces_c_color_hsl16,
    const int16_t* faces_textures,
    const uint16_t* textured_faces,
    const uint16_t* textured_faces_a,
    const uint16_t* textured_faces_b,
    const uint16_t* textured_faces_c,
    const uint8_t* face_alphas,
    const int32_t* face_infos,
    TRSPK_UVMode uv_mode,
    const TRSPK_BakeTransform* bake,
    TRSPK_VertexBuffer* dest);

bool
trspk_vertex_buffer_write_i16(
    uint32_t vertex_count,
    uint32_t face_count,
    const int16_t* vertices_x,
    const int16_t* vertices_y,
    const int16_t* vertices_z,
    const uint16_t* faces_a,
    const uint16_t* faces_b,
    const uint16_t* faces_c,
    const uint16_t* faces_a_color_hsl16,
    const uint16_t* faces_b_color_hsl16,
    const uint16_t* faces_c_color_hsl16,
    const uint8_t* face_alphas,
    const int32_t* face_infos,
    const TRSPK_BakeTransform* bake,
    TRSPK_VertexBuffer* dest);

#ifdef __cplusplus
}
#endif

#endif
