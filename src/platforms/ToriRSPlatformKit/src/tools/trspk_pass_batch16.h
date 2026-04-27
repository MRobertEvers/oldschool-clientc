#ifndef TORIRS_PLATFORM_KIT_TRSPK_PASS_BATCH16_H
#define TORIRS_PLATFORM_KIT_TRSPK_PASS_BATCH16_H

#include "../../include/ToriRSPlatformKit/trspk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

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
} TRSPK_Batch16;

TRSPK_Batch16*
trspk_batch16_create(
    uint32_t max_vertices,
    uint32_t max_indices,
    const TRSPK_VertexFormat* vertex_format);
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

#ifdef __cplusplus
}
#endif

#endif
