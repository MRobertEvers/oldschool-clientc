#ifndef TORIRS_PLATFORM_KIT_TRSPK_BATCH16_H
#define TORIRS_PLATFORM_KIT_TRSPK_BATCH16_H

#include "../../include/ToriRSPlatformKit/trspk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TRSPK_VertexBuffer TRSPK_VertexBuffer;
typedef struct TRSPK_ResourceCache TRSPK_ResourceCache;

typedef struct TRSPK_Batch16Chunk
{
    uint8_t* vertices;
    uint16_t* indices;
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t vertex_capacity;
    uint32_t index_capacity;
} TRSPK_Batch16Chunk;

typedef struct TRSPK_Batch16
{
    TRSPK_Batch16Chunk chunks[TRSPK_MAX_WEBGL1_CHUNKS];
    uint32_t chunk_count;
    uint32_t current_chunk;
    TRSPK_VertexFormat vertex_format;
    TRSPK_Batch16Entry* entries;
    uint32_t entry_count;
    uint32_t entry_capacity;
    bool building;
    /** Baked into D3D8 FVF atlas UVs when writing D3D8/D3D8_SOAOS vertices (see trspk_vertex_buffer_write). */
    double d3d8_vertex_frame_clock;
    /** V-axis tiling multiplier for D3D8 atlas UV bake. 1.0 = no tiling. d3d8_fixed sets 2.0. */
    float d3d8_v_repeat;
} TRSPK_Batch16;

TRSPK_Batch16*
trspk_batch16_create(
    uint32_t max_vertices,
    uint32_t max_indices,
    TRSPK_VertexFormat vertex_format);
void
trspk_batch16_destroy(TRSPK_Batch16* batch);
void
trspk_batch16_begin(TRSPK_Batch16* batch);
void
trspk_batch16_end(TRSPK_Batch16* batch);
void
trspk_batch16_clear(TRSPK_Batch16* batch);

int32_t
trspk_batch16_add_mesh(
    TRSPK_Batch16* batch,
    const TRSPK_Vertex* vertices,
    uint32_t vertex_count,
    const uint16_t* indices,
    uint32_t index_count,
    TRSPK_ModelId model_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index);

bool
trspk_batch16_append_triangle(
    TRSPK_Batch16* batch,
    const TRSPK_Vertex vertices[3],
    TRSPK_ModelId model_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index);

uint32_t
trspk_batch16_chunk_count(const TRSPK_Batch16* batch);
void
trspk_batch16_get_chunk(
    const TRSPK_Batch16* batch,
    uint32_t chunk_index,
    const void** out_vertices,
    uint32_t* out_vertex_bytes,
    const void** out_indices,
    uint32_t* out_index_bytes);

uint32_t
trspk_batch16_entry_count(const TRSPK_Batch16* batch);
const TRSPK_Batch16Entry*
trspk_batch16_get_entry(
    const TRSPK_Batch16* batch,
    uint32_t i);

bool
trspk_batch16_prepare_vertex_buffer(
    TRSPK_Batch16* batch,
    TRSPK_VertexBuffer* vb,
    uint32_t vertex_count,
    uint32_t index_count,
    TRSPK_VertexFormat vertex_format);

void
trspk_batch16_add_model(
    TRSPK_Batch16* batch,
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
trspk_batch16_add_model_textured(
    TRSPK_Batch16* batch,
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
