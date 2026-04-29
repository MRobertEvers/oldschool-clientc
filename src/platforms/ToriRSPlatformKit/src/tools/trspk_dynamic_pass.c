#include "trspk_dynamic_pass.h"

#include "graphics/dash.h"
#include "trspk_dash.h"
#include "trspk_vertex_format.h"

#include <stdlib.h>
#include <string.h>

static bool
trspk_dynamic_mesh_copy(
    const void* vertices,
    uint32_t vertex_bytes,
    const void* indices,
    uint32_t index_bytes,
    uint32_t vertex_count,
    uint32_t index_count,
    uint8_t chunk_index,
    TRSPK_DynamicMesh* out_mesh)
{
    if( !vertices || !indices || vertex_bytes == 0u || index_bytes == 0u || !out_mesh )
        return false;
    void* v = malloc(vertex_bytes);
    void* i = malloc(index_bytes);
    if( !v || !i )
    {
        free(v);
        free(i);
        return false;
    }
    memcpy(v, vertices, vertex_bytes);
    memcpy(i, indices, index_bytes);
    memset(out_mesh, 0, sizeof(*out_mesh));
    out_mesh->vertices = v;
    out_mesh->indices = i;
    out_mesh->vertex_count = vertex_count;
    out_mesh->index_count = index_count;
    out_mesh->vertex_bytes = vertex_bytes;
    out_mesh->index_bytes = index_bytes;
    out_mesh->chunk_index = chunk_index;
    return true;
}

static bool
trspk_dynamic_mesh_build_with_batch16(
    struct DashModel* model,
    TRSPK_VertexFormat vertex_format,
    const TRSPK_BakeTransform* bake,
    TRSPK_ResourceCache* resource_cache,
    TRSPK_DynamicMesh* out_mesh)
{
    TRSPK_Batch16* batch = trspk_batch16_create(65535u, 65535u, vertex_format);
    if( !batch )
        return false;
    trspk_batch16_begin(batch);
    trspk_dash_batch_add_model16(
        batch, model, 1u, TRSPK_GPU_ANIM_NONE_IDX, 0u, bake, resource_cache);
    trspk_batch16_end(batch);
    const bool one_chunk = trspk_batch16_chunk_count(batch) == 1u;
    const TRSPK_Batch16Entry* entry = trspk_batch16_get_entry(batch, 0u);
    const void* vertices = NULL;
    const void* indices = NULL;
    uint32_t vertex_bytes = 0u;
    uint32_t index_bytes = 0u;
    if( one_chunk )
        trspk_batch16_get_chunk(batch, 0u, &vertices, &vertex_bytes, &indices, &index_bytes);
    const uint32_t stride = trspk_vertex_format_stride(vertex_format);
    const bool ok = one_chunk && entry &&
                    trspk_dynamic_mesh_copy(
                        vertices,
                        vertex_bytes,
                        indices,
                        index_bytes,
                        stride ? vertex_bytes / stride : 0u,
                        (uint32_t)(index_bytes / sizeof(uint16_t)),
                        entry ? entry->chunk_index : 0u,
                        out_mesh);
    trspk_batch16_destroy(batch);
    return ok;
}

static bool
trspk_dynamic_mesh_build_with_batch32(
    struct DashModel* model,
    TRSPK_VertexFormat vertex_format,
    const TRSPK_BakeTransform* bake,
    TRSPK_ResourceCache* resource_cache,
    TRSPK_DynamicMesh* out_mesh)
{
    TRSPK_Batch32* batch = trspk_batch32_create(4096u, 12288u, vertex_format);
    if( !batch )
        return false;
    trspk_batch32_begin(batch);
    trspk_dash_batch_add_model32(
        batch, model, 1u, TRSPK_GPU_ANIM_NONE_IDX, 0u, bake, resource_cache);
    trspk_batch32_end(batch);
    const TRSPK_Batch32Entry* entry = trspk_batch32_get_entry(batch, 0u);
    const void* vertices = NULL;
    const void* indices = NULL;
    uint32_t vertex_bytes = 0u;
    uint32_t index_bytes = 0u;
    trspk_batch32_get_data(batch, &vertices, &vertex_bytes, &indices, &index_bytes);
    const uint32_t stride = trspk_vertex_format_stride(vertex_format);
    const bool ok = entry && trspk_dynamic_mesh_copy(
                                 vertices,
                                 vertex_bytes,
                                 indices,
                                 index_bytes,
                                 stride ? vertex_bytes / stride : 0u,
                                 (uint32_t)(index_bytes / sizeof(uint32_t)),
                                 0u,
                                 out_mesh);
    trspk_batch32_destroy(batch);
    return ok;
}

bool
trspk_dynamic_mesh_build(
    struct DashModel* model,
    TRSPK_VertexFormat vertex_format,
    const TRSPK_BakeTransform* bake,
    TRSPK_ResourceCache* resource_cache,
    TRSPK_DynamicMesh* out_mesh)
{
    if( out_mesh )
        memset(out_mesh, 0, sizeof(*out_mesh));
    if( !model || !out_mesh || dashmodel_face_count(model) <= 0 )
        return false;

    switch( vertex_format )
    {
    case TRSPK_VERTEX_FORMAT_WEBGL1:
        return trspk_dynamic_mesh_build_with_batch16(
            model, vertex_format, bake, resource_cache, out_mesh);
    case TRSPK_VERTEX_FORMAT_TRSPK:
    case TRSPK_VERTEX_FORMAT_METAL:
        return trspk_dynamic_mesh_build_with_batch32(
            model, vertex_format, bake, resource_cache, out_mesh);
    default:
        return false;
    }
}

void
trspk_dynamic_mesh_clear(TRSPK_DynamicMesh* mesh)
{
    if( !mesh )
        return;
    free(mesh->vertices);
    free(mesh->indices);
    memset(mesh, 0, sizeof(*mesh));
}
