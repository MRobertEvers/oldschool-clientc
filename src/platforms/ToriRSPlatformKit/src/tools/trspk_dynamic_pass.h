#ifndef TORIRS_PLATFORM_KIT_TRSPK_DYNAMIC_PASS_H
#define TORIRS_PLATFORM_KIT_TRSPK_DYNAMIC_PASS_H

#include "trspk_batch16.h"
#include "trspk_batch32.h"

#ifdef __cplusplus
extern "C" {
#endif

struct DashModel;

typedef struct TRSPK_DynamicMesh
{
    void* vertices;
    void* indices;
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t vertex_bytes;
    uint32_t index_bytes;
    uint8_t chunk_index;
} TRSPK_DynamicMesh;

bool
trspk_dynamic_mesh_build(
    struct DashModel* model,
    TRSPK_VertexFormat vertex_format,
    const TRSPK_BakeTransform* bake,
    TRSPK_DynamicMesh* out_mesh);

void
trspk_dynamic_mesh_clear(TRSPK_DynamicMesh* mesh);

#ifdef __cplusplus
}
#endif

#endif
