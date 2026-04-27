#ifndef TORIRS_PLATFORM_KIT_TRSPK_PASS_BATCH32_H
#define TORIRS_PLATFORM_KIT_TRSPK_PASS_BATCH32_H

#include "../../include/ToriRSPlatformKit/trspk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif
