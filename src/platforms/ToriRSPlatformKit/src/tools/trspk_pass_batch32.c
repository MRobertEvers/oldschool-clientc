#include "trspk_pass_batch32.h"

#include <stdlib.h>
#include <string.h>

static void
trspk_batch32_copy_common_vertices(
    void* dst_vertices,
    const TRSPK_Vertex* src_vertices,
    uint32_t vertex_count)
{
    memcpy(dst_vertices, src_vertices, sizeof(TRSPK_Vertex) * vertex_count);
}

static TRSPK_VertexFormat
trspk_batch32_resolve_vertex_format(const TRSPK_VertexFormat* vertex_format)
{
    TRSPK_VertexFormat out;
    if( vertex_format && vertex_format->stride > 0u && vertex_format->convert )
        out = *vertex_format;
    else
    {
        out.stride = (uint32_t)sizeof(TRSPK_Vertex);
        out.convert = trspk_batch32_copy_common_vertices;
    }
    return out;
}

static bool
trspk_batch32_reserve_vertices(
    TRSPK_Batch32* batch,
    uint32_t count)
{
    if( count <= batch->vertex_capacity )
        return true;
    uint32_t cap = batch->vertex_capacity ? batch->vertex_capacity : 4096u;
    while( cap < count )
        cap *= 2u;
    uint8_t* nv = (uint8_t*)realloc(batch->vertices, (size_t)batch->vertex_format.stride * cap);
    if( !nv )
        return false;
    batch->vertices = nv;
    batch->vertex_capacity = cap;
    return true;
}

static bool
trspk_batch32_reserve_indices(
    TRSPK_Batch32* batch,
    uint32_t count)
{
    if( count <= batch->index_capacity )
        return true;
    uint32_t cap = batch->index_capacity ? batch->index_capacity : 12288u;
    while( cap < count )
        cap *= 2u;
    uint32_t* ni = (uint32_t*)realloc(batch->indices, sizeof(uint32_t) * cap);
    if( !ni )
        return false;
    batch->indices = ni;
    batch->index_capacity = cap;
    return true;
}

static bool
trspk_batch32_reserve_entries(
    TRSPK_Batch32* batch,
    uint32_t count)
{
    if( count <= batch->entry_capacity )
        return true;
    uint32_t cap = batch->entry_capacity ? batch->entry_capacity : 128u;
    while( cap < count )
        cap *= 2u;
    TRSPK_Batch32Entry* ne =
        (TRSPK_Batch32Entry*)realloc(batch->entries, sizeof(TRSPK_Batch32Entry) * cap);
    if( !ne )
        return false;
    batch->entries = ne;
    batch->entry_capacity = cap;
    return true;
}

TRSPK_Batch32*
trspk_batch32_create(
    uint32_t initial_vertex_capacity,
    uint32_t initial_index_capacity,
    const TRSPK_VertexFormat* vertex_format)
{
    TRSPK_Batch32* batch = (TRSPK_Batch32*)calloc(1u, sizeof(TRSPK_Batch32));
    if( !batch )
        return NULL;
    batch->vertex_format = trspk_batch32_resolve_vertex_format(vertex_format);
    trspk_batch32_reserve_vertices(batch, initial_vertex_capacity);
    trspk_batch32_reserve_indices(batch, initial_index_capacity);
    return batch;
}

void
trspk_batch32_destroy(TRSPK_Batch32* batch)
{
    if( !batch )
        return;
    free(batch->vertices);
    free(batch->indices);
    free(batch->entries);
    free(batch);
}

void
trspk_batch32_clear(TRSPK_Batch32* batch)
{
    if( !batch )
        return;
    batch->vertex_count = 0u;
    batch->index_count = 0u;
    batch->entry_count = 0u;
    batch->building = false;
}

void
trspk_batch32_begin(TRSPK_Batch32* batch)
{
    if( !batch )
        return;
    trspk_batch32_clear(batch);
    batch->building = true;
}

void
trspk_batch32_end(TRSPK_Batch32* batch)
{
    if( batch )
        batch->building = false;
}

bool
trspk_batch32_add_mesh(
    TRSPK_Batch32* batch,
    const TRSPK_Vertex* vertices,
    uint32_t vertex_count,
    const uint32_t* indices,
    uint32_t index_count,
    TRSPK_ModelId model_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index)
{
    if( !batch || !batch->building || !vertices || !indices || vertex_count == 0u ||
        index_count == 0u )
        return false;
    const uint32_t vstart = batch->vertex_count;
    const uint32_t istart = batch->index_count;
    if( !trspk_batch32_reserve_vertices(batch, vstart + vertex_count) ||
        !trspk_batch32_reserve_indices(batch, istart + index_count) ||
        !trspk_batch32_reserve_entries(batch, batch->entry_count + 1u) )
        return false;

    batch->vertex_format.convert(
        batch->vertices + (size_t)vstart * batch->vertex_format.stride, vertices, vertex_count);
    for( uint32_t i = 0; i < index_count; ++i )
        batch->indices[istart + i] = vstart + indices[i];
    batch->vertex_count += vertex_count;
    batch->index_count += index_count;

    TRSPK_Batch32Entry* e = &batch->entries[batch->entry_count++];
    e->model_id = model_id;
    e->gpu_segment_slot = gpu_segment_slot;
    e->frame_index = frame_index;
    e->vbo_offset = vstart;
    e->ebo_offset = istart;
    e->element_count = index_count;
    return true;
}

void
trspk_batch32_get_data(
    const TRSPK_Batch32* batch,
    const void** out_vertices,
    uint32_t* out_vertex_bytes,
    const void** out_indices,
    uint32_t* out_index_bytes)
{
    if( out_vertices )
        *out_vertices = batch ? batch->vertices : NULL;
    if( out_vertex_bytes )
        *out_vertex_bytes = batch ? batch->vertex_count * batch->vertex_format.stride : 0u;
    if( out_indices )
        *out_indices = batch ? batch->indices : NULL;
    if( out_index_bytes )
        *out_index_bytes = batch ? batch->index_count * (uint32_t)sizeof(uint32_t) : 0u;
}

uint32_t
trspk_batch32_entry_count(const TRSPK_Batch32* batch)
{
    return batch ? batch->entry_count : 0u;
}

const TRSPK_Batch32Entry*
trspk_batch32_get_entry(
    const TRSPK_Batch32* batch,
    uint32_t i)
{
    if( !batch || i >= batch->entry_count )
        return NULL;
    return &batch->entries[i];
}
