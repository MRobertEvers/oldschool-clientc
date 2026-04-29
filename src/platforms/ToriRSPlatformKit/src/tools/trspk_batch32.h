#ifndef TORIRS_PLATFORM_KIT_TRSPK_BATCH32_H
#define TORIRS_PLATFORM_KIT_TRSPK_BATCH32_H

#include "../../include/ToriRSPlatformKit/trspk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TRSPK_VertexBuffer TRSPK_VertexBuffer;
typedef struct TRSPK_ResourceCache TRSPK_ResourceCache;

typedef struct TRSPK_Batch32
{
    uint8_t* vertices;
    uint32_t* indices;
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t vertex_capacity;
    uint32_t index_capacity;
    TRSPK_VertexFormat vertex_format;
    TRSPK_Batch32Entry* entries;
    uint32_t entry_count;
    uint32_t entry_capacity;
    bool building;
} TRSPK_Batch32;

TRSPK_Batch32*
trspk_batch32_create(
    uint32_t initial_vertex_capacity,
    uint32_t initial_index_capacity,
    TRSPK_VertexFormat vertex_format);
void
trspk_batch32_destroy(TRSPK_Batch32* batch);
void
trspk_batch32_begin(TRSPK_Batch32* batch);
void
trspk_batch32_end(TRSPK_Batch32* batch);
void
trspk_batch32_clear(TRSPK_Batch32* batch);

bool
trspk_batch32_add_mesh(
    TRSPK_Batch32* batch,
    const TRSPK_Vertex* vertices,
    uint32_t vertex_count,
    const uint32_t* indices,
    uint32_t index_count,
    TRSPK_ModelId model_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index);

void
trspk_batch32_get_data(
    const TRSPK_Batch32* batch,
    const void** out_vertices,
    uint32_t* out_vertex_bytes,
    const void** out_indices,
    uint32_t* out_index_bytes);

uint32_t
trspk_batch32_entry_count(const TRSPK_Batch32* batch);
const TRSPK_Batch32Entry*
trspk_batch32_get_entry(
    const TRSPK_Batch32* batch,
    uint32_t i);

bool
trspk_batch32_prepare_vertex_buffer(
    TRSPK_Batch32* batch,
    TRSPK_VertexBuffer* vb,
    uint32_t vertex_count,
    uint32_t index_count,
    TRSPK_VertexFormat vertex_format);

void
trspk_batch32_add_model(
    TRSPK_Batch32* batch,
    uint16_t model_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index,
    uint32_t vertex_count,
    const int16_t* vertices_x,
    const int16_t* vertices_y,
    const int16_t* vertices_z,
    uint32_t face_count,
    const uint16_t* faces_a,
    const uint16_t* faces_b,
    const uint16_t* faces_c,
    const uint16_t* faces_a_color_hsl16,
    const uint16_t* faces_b_color_hsl16,
    const uint16_t* faces_c_color_hsl16,
    const uint8_t* face_alphas,
    const int32_t* face_infos,
    const TRSPK_BakeTransform* bake);

void
trspk_batch32_add_model_textured(
    TRSPK_Batch32* batch,
    uint16_t model_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index,
    uint32_t vertex_count,
    const int16_t* vertices_x,
    const int16_t* vertices_y,
    const int16_t* vertices_z,
    uint32_t face_count,
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
    const TRSPK_BakeTransform* bake);

#ifdef __cplusplus
}
#endif

#endif
