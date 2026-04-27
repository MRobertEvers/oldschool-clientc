#include "trspk_pass_batch16.h"

#include <stdlib.h>
#include <string.h>

static void
trspk_batch16_copy_common_vertices(
    void* dst_vertices,
    const TRSPK_Vertex* src_vertices,
    uint32_t vertex_count)
{
    memcpy(dst_vertices, src_vertices, sizeof(TRSPK_Vertex) * vertex_count);
}

static TRSPK_VertexFormat
trspk_batch16_resolve_vertex_format(const TRSPK_VertexFormat* vertex_format)
{
    TRSPK_VertexFormat out;
    if( vertex_format && vertex_format->stride > 0u && vertex_format->convert )
        out = *vertex_format;
    else
    {
        out.stride = (uint32_t)sizeof(TRSPK_Vertex);
        out.convert = trspk_batch16_copy_common_vertices;
    }
    return out;
}

static bool
trspk_batch16_reserve_chunk(
    TRSPK_Batch16Chunk* chunk,
    uint32_t vertices,
    uint32_t indices,
    uint32_t vertex_stride)
{
    if( vertices > chunk->vertex_capacity )
    {
        uint32_t cap = chunk->vertex_capacity ? chunk->vertex_capacity : 1024u;
        while( cap < vertices )
            cap *= 2u;
        uint8_t* nv = (uint8_t*)realloc(chunk->vertices, (size_t)vertex_stride * cap);
        if( !nv )
            return false;
        chunk->vertices = nv;
        chunk->vertex_capacity = cap;
    }
    if( indices > chunk->index_capacity )
    {
        uint32_t cap = chunk->index_capacity ? chunk->index_capacity : 3072u;
        while( cap < indices )
            cap *= 2u;
        uint16_t* ni = (uint16_t*)realloc(chunk->indices, sizeof(uint16_t) * cap);
        if( !ni )
            return false;
        chunk->indices = ni;
        chunk->index_capacity = cap;
    }
    return true;
}

static bool
trspk_batch16_reserve_entries(
    TRSPK_Batch16* batch,
    uint32_t count)
{
    if( count <= batch->entry_capacity )
        return true;
    uint32_t cap = batch->entry_capacity ? batch->entry_capacity : 128u;
    while( cap < count )
        cap *= 2u;
    TRSPK_Batch16Entry* ne =
        (TRSPK_Batch16Entry*)realloc(batch->entries, sizeof(TRSPK_Batch16Entry) * cap);
    if( !ne )
        return false;
    batch->entries = ne;
    batch->entry_capacity = cap;
    return true;
}

static bool
trspk_batch16_roll_chunk(TRSPK_Batch16* batch)
{
    if( batch->chunk_count >= TRSPK_MAX_WEBGL1_CHUNKS )
        return false;
    batch->current_chunk = batch->chunk_count++;
    return true;
}

TRSPK_Batch16*
trspk_batch16_create(
    uint32_t max_vertices,
    uint32_t max_indices,
    const TRSPK_VertexFormat* vertex_format)
{
    (void)max_vertices;
    (void)max_indices;
    TRSPK_Batch16* batch = (TRSPK_Batch16*)calloc(1u, sizeof(TRSPK_Batch16));
    if( batch )
        batch->vertex_format = trspk_batch16_resolve_vertex_format(vertex_format);
    return batch;
}

void
trspk_batch16_clear(TRSPK_Batch16* batch)
{
    if( !batch )
        return;
    for( uint32_t i = 0; i < TRSPK_MAX_WEBGL1_CHUNKS; ++i )
    {
        batch->chunks[i].vertex_count = 0u;
        batch->chunks[i].index_count = 0u;
    }
    batch->chunk_count = 0u;
    batch->current_chunk = 0u;
    batch->entry_count = 0u;
    batch->building = false;
}

void
trspk_batch16_destroy(TRSPK_Batch16* batch)
{
    if( !batch )
        return;
    for( uint32_t i = 0; i < TRSPK_MAX_WEBGL1_CHUNKS; ++i )
    {
        free(batch->chunks[i].vertices);
        free(batch->chunks[i].indices);
    }
    free(batch->entries);
    free(batch);
}

void
trspk_batch16_begin(TRSPK_Batch16* batch)
{
    if( !batch )
        return;
    trspk_batch16_clear(batch);
    batch->chunk_count = 1u;
    batch->current_chunk = 0u;
    batch->building = true;
}

void
trspk_batch16_end(TRSPK_Batch16* batch)
{
    if( batch )
        batch->building = false;
}

int32_t
trspk_batch16_add_mesh(
    TRSPK_Batch16* batch,
    const TRSPK_Vertex* vertices,
    uint32_t vertex_count,
    const uint16_t* indices,
    uint32_t index_count,
    TRSPK_ModelId model_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index)
{
    if( !batch || !batch->building || !vertices || !indices || vertex_count == 0u ||
        index_count == 0u || vertex_count > 65535u )
        return -1;
    TRSPK_Batch16Chunk* chunk = &batch->chunks[batch->current_chunk];
    if( chunk->vertex_count + vertex_count > 65535u && !trspk_batch16_roll_chunk(batch) )
        return -1;
    chunk = &batch->chunks[batch->current_chunk];
    const uint32_t vstart = chunk->vertex_count;
    const uint32_t istart = chunk->index_count;
    if( !trspk_batch16_reserve_chunk(
            chunk, vstart + vertex_count, istart + index_count, batch->vertex_format.stride) ||
        !trspk_batch16_reserve_entries(batch, batch->entry_count + 1u) )
        return -1;
    batch->vertex_format.convert(
        chunk->vertices + (size_t)vstart * batch->vertex_format.stride, vertices, vertex_count);
    for( uint32_t i = 0; i < index_count; ++i )
        chunk->indices[istart + i] = (uint16_t)(vstart + indices[i]);
    chunk->vertex_count += vertex_count;
    chunk->index_count += index_count;

    TRSPK_Batch16Entry* e = &batch->entries[batch->entry_count++];
    e->model_id = model_id;
    e->gpu_segment_slot = gpu_segment_slot;
    e->frame_index = frame_index;
    e->chunk_index = (uint8_t)batch->current_chunk;
    e->vbo_offset = vstart;
    e->ebo_offset = istart;
    e->element_count = index_count;
    return (int32_t)batch->current_chunk;
}

bool
trspk_batch16_append_triangle(
    TRSPK_Batch16* batch,
    const TRSPK_Vertex vertices[3],
    TRSPK_ModelId model_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index)
{
    static const uint16_t tri[3] = { 0u, 1u, 2u };
    return trspk_batch16_add_mesh(
               batch, vertices, 3u, tri, 3u, model_id, gpu_segment_slot, frame_index) >= 0;
}

uint32_t
trspk_batch16_chunk_count(const TRSPK_Batch16* batch)
{
    return batch ? batch->chunk_count : 0u;
}

void
trspk_batch16_get_chunk(
    const TRSPK_Batch16* batch,
    uint32_t chunk_index,
    const void** out_vertices,
    uint32_t* out_vertex_bytes,
    const void** out_indices,
    uint32_t* out_index_bytes)
{
    if( out_vertices )
        *out_vertices = NULL;
    if( out_vertex_bytes )
        *out_vertex_bytes = 0u;
    if( out_indices )
        *out_indices = NULL;
    if( out_index_bytes )
        *out_index_bytes = 0u;
    if( !batch || chunk_index >= batch->chunk_count )
        return;
    const TRSPK_Batch16Chunk* c = &batch->chunks[chunk_index];
    if( out_vertices )
        *out_vertices = c->vertices;
    if( out_vertex_bytes )
        *out_vertex_bytes = c->vertex_count * batch->vertex_format.stride;
    if( out_indices )
        *out_indices = c->indices;
    if( out_index_bytes )
        *out_index_bytes = c->index_count * (uint32_t)sizeof(uint16_t);
}

uint32_t
trspk_batch16_entry_count(const TRSPK_Batch16* batch)
{
    return batch ? batch->entry_count : 0u;
}

const TRSPK_Batch16Entry*
trspk_batch16_get_entry(
    const TRSPK_Batch16* batch,
    uint32_t i)
{
    if( !batch || i >= batch->entry_count )
        return NULL;
    return &batch->entries[i];
}
