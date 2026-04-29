#ifndef TORIRS_PLATFORM_KIT_TRSPK_VERTEX_BUFFER_H
#define TORIRS_PLATFORM_KIT_TRSPK_VERTEX_BUFFER_H

#include "../../include/ToriRSPlatformKit/trspk_types.h"
#include "../backends/d3d8/d3d8_vertex.h"
#include "../backends/metal/metal_vertex.h"
#include "../backends/webgl1/webgl1_vertex.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TRSPK_ResourceCache TRSPK_ResourceCache;

/** Expanded per-corner mesh (distinct from TRSPK_ModelId). */
typedef struct TRSPK_VertexBuffer
{
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t index_base;
    TRSPK_IndexFormat index_format;
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
        TRSPK_VertexWebGL1Soaos as_webgl1_soaos;
        TRSPK_VertexWebGL1* as_webgl1;
        TRSPK_VertexD3D8Soaos as_d3d8_soaos;
        TRSPK_VertexD3D8* as_d3d8;
        TRSPK_VertexMetal* as_metal;
        TRSPK_VertexMetalSoaos as_metal_soaos;
    } vertices;
} TRSPK_VertexBuffer;

void
trspk_vertex_buffer_free(TRSPK_VertexBuffer* vb);

/**
 * Heap-allocate indices (u32) and vertex storage for a standalone mesh.
 * On success: status READY, index_base 0, index_format U32.
 * On failure: leaves vb cleared (safe to retry).
 */
bool
trspk_vertex_buffer_allocate_mesh(
    TRSPK_VertexBuffer* vb,
    uint32_t vertex_count,
    uint32_t index_count,
    TRSPK_VertexFormat format);

/** True if mesh holds non-NULL vertex storage matching vb->format. */
bool
trspk_vertex_buffer_has_vertex_payload(const TRSPK_VertexBuffer* vb);

/**
 * Deep copy of a READY mesh (owning indices and vertex storage). Fails for BATCH_VIEW.
 * On failure, *out is cleared.
 */
bool
trspk_vertex_buffer_duplicate(const TRSPK_VertexBuffer* src, TRSPK_VertexBuffer* out);

/**
 * Apply position+yaw bake to interleaved vertex positions in place (TRSPK / WEBGL1 / METAL only).
 * SoA meshes: use trspk_vertex_buffer_bake_array_to_interleaved instead.
 */
void
trspk_vertex_buffer_apply_bake(TRSPK_VertexBuffer* vb, const TRSPK_BakeTransform* bake);

/**
 * CPU LRU stores AoSoA (METAL_SOAOS / WEBGL1_SOAOS). This applies world bake and writes
 * interleaved GPU vertices (METAL / WEBGL1). Indices are copied unchanged.
 */
bool
trspk_vertex_buffer_bake_array_to_interleaved(
    const TRSPK_VertexBuffer* src_array,
    const TRSPK_BakeTransform* bake,
    TRSPK_VertexBuffer* out_interleaved);

/**
 * Convert in place from TRSPK to dst_format. Requires vb->format == TRSPK.
 * dst_format must not be NONE; TRSPK is a no-op.
 */
bool
trspk_vertex_buffer_convert_from_trspk(
    TRSPK_VertexBuffer* vb, TRSPK_VertexFormat dst_format);

/**
 * `uv_calc_mode` selects PNM→UV inputs only. `atlas_tile_meta` may be NULL (defaults: opaque,
 * no scroll).
 */
bool
trspk_vertex_buffer_write_textured(
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
    TRSPK_UVCalculationMode uv_calc_mode,
    const TRSPK_ResourceCache* atlas_tile_meta,
    const TRSPK_BakeTransform* bake,
    TRSPK_VertexBuffer* dest);

bool
trspk_vertex_buffer_write(
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
