#include "trspk_batch16.h"

#include "trspk_vertex_buffer.h"
#include "trspk_vertex_format.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static bool
trspk_batch16_reserve_chunk(
    TRSPK_Batch16Chunk* chunk,
    uint32_t vertices,
    uint32_t indices,
    uint32_t vertex_stride)
{
    if( vertex_stride == 0u )
        return false;
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
    TRSPK_VertexFormat vertex_format)
{
    (void)max_vertices;
    (void)max_indices;
    TRSPK_Batch16* batch = (TRSPK_Batch16*)calloc(1u, sizeof(TRSPK_Batch16));
    if( batch )
        batch->vertex_format = vertex_format;
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
            chunk,
            vstart + vertex_count,
            istart + index_count,
            trspk_vertex_format_stride(batch->vertex_format)) ||
        !trspk_batch16_reserve_entries(batch, batch->entry_count + 1u) )
        return -1;

    trspk_vertex_format_convert(
        chunk->vertices + (size_t)vstart * trspk_vertex_format_stride(batch->vertex_format),
        vertices,
        vertex_count,
        batch->vertex_format);

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
        *out_vertex_bytes = c->vertex_count * trspk_vertex_format_stride(batch->vertex_format);
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

bool
trspk_batch16_prepare_vertex_buffer(
    TRSPK_Batch16* batch,
    TRSPK_VertexBuffer* vb,
    uint32_t vertex_count,
    uint32_t index_count,
    TRSPK_VertexFormat vertex_format)
{
    if( !batch || !vb || !batch->building || vertex_count == 0u || index_count == 0u )
        return false;
    if( vertex_count > 65535u )
        return false;
    if( vertex_format != batch->vertex_format )
        return false;
    switch( vertex_format )
    {
    case TRSPK_VERTEX_FORMAT_TRSPK:
    case TRSPK_VERTEX_FORMAT_WEBGL1:
    case TRSPK_VERTEX_FORMAT_METAL:
        break;
    default:
        return false;
    }

    const uint32_t stride = trspk_vertex_format_stride(vertex_format);
    if( stride == 0u )
        return false;

    trspk_vertex_buffer_free(vb);

    TRSPK_Batch16Chunk* chunk = &batch->chunks[batch->current_chunk];
    assert(chunk->vertex_count + vertex_count <= 65535u);
    assert(chunk->index_count + index_count <= 65535u);
    const uint32_t vstart = chunk->vertex_count;
    const uint32_t istart = chunk->index_count;
    if( !trspk_batch16_reserve_chunk(
            chunk,
            vstart + vertex_count,
            istart + index_count,
            trspk_vertex_format_stride(batch->vertex_format)) )
        return false;

    uint8_t* vbase = chunk->vertices + (size_t)vstart * (size_t)stride;
    uint16_t* ibase = chunk->indices + istart;

    vb->format = vertex_format;
    switch( vertex_format )
    {
    case TRSPK_VERTEX_FORMAT_TRSPK:
        vb->vertices.as_trspk = (TRSPK_Vertex*)vbase;
        break;
    case TRSPK_VERTEX_FORMAT_WEBGL1:
        vb->vertices.as_webgl1 = (TRSPK_VertexWebGL1*)vbase;
        break;
    case TRSPK_VERTEX_FORMAT_METAL:
        vb->vertices.as_metal = (TRSPK_VertexMetal*)vbase;
        break;
    default:
        return false;
    }
    vb->index_base = vstart;
    vb->vertex_count = vertex_count;
    vb->index_count = index_count;
    vb->index_format = TRSPK_INDEX_FORMAT_U16;
    vb->indices.as_u16 = ibase;
    vb->status = TRSPK_VERTEX_BUFFER_STATUS_BATCH_VIEW;
    return true;
}

static bool
batch16_finalize_inplace_mesh_after_write(
    TRSPK_Batch16* batch,
    uint8_t chunk_index,
    uint32_t vstart,
    uint32_t istart,
    uint32_t expanded_vertex_count,
    uint32_t index_count,
    TRSPK_ModelId model_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index)
{
    if( !batch || !batch->building || expanded_vertex_count == 0u || index_count == 0u )
        return false;
    if( chunk_index >= batch->chunk_count || chunk_index >= TRSPK_MAX_WEBGL1_CHUNKS )
        return false;

    TRSPK_Batch16Chunk* chunk = &batch->chunks[chunk_index];
    if( chunk->vertex_count != vstart || chunk->index_count != istart )
        return false;
    if( !chunk->vertices || !chunk->indices )
        return false;

    if( !trspk_batch16_reserve_entries(batch, batch->entry_count + 1u) )
        return false;

    chunk->vertex_count += expanded_vertex_count;
    chunk->index_count += index_count;

    TRSPK_Batch16Entry* e = &batch->entries[batch->entry_count++];
    e->model_id = model_id;
    e->gpu_segment_slot = gpu_segment_slot;
    e->frame_index = frame_index;
    e->chunk_index = chunk_index;
    e->vbo_offset = vstart;
    e->ebo_offset = istart;
    e->element_count = index_count;
    return true;
}

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
    const TRSPK_BakeTransform* bake)
{
    if( !batch || !batch->building || !vertices_x || !vertices_y || !vertices_z || !faces_a ||
        !faces_b || !faces_c || !faces_a_color_hsl16 || !faces_b_color_hsl16 ||
        !faces_c_color_hsl16 || vertex_count == 0u || face_count == 0u )
        return;

    const uint32_t corner_count = face_count * 3u;
    const uint8_t chunk_index = (uint8_t)batch->current_chunk;
    TRSPK_Batch16Chunk* ch = &batch->chunks[chunk_index];
    const uint32_t v0 = ch->vertex_count;
    const uint32_t i0 = ch->index_count;

    struct TRSPK_VertexBuffer vb = { 0 };
    if( !trspk_batch16_prepare_vertex_buffer(
            batch, &vb, corner_count, corner_count, batch->vertex_format) )
        return;

    if( !trspk_vertex_buffer_write(
            vertex_count,
            face_count,
            vertices_x,
            vertices_y,
            vertices_z,
            faces_a,
            faces_b,
            faces_c,
            faces_a_color_hsl16,
            faces_b_color_hsl16,
            faces_c_color_hsl16,
            face_alphas,
            face_infos,
            bake,
            &vb) )
    {
        trspk_vertex_buffer_free(&vb);
        return;
    }

    (void)batch16_finalize_inplace_mesh_after_write(
        batch,
        chunk_index,
        v0,
        i0,
        corner_count,
        corner_count,
        (TRSPK_ModelId)model_id,
        gpu_segment_slot,
        frame_index);

    trspk_vertex_buffer_free(&vb);
}

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
    TRSPK_UVMode uv_mode,
    const TRSPK_BakeTransform* bake)
{
    if( !batch || !batch->building || !vertices_x || !vertices_y || !vertices_z || !faces_a ||
        !faces_b || !faces_c || !faces_a_color_hsl16 || !faces_b_color_hsl16 ||
        !faces_c_color_hsl16 || vertex_count == 0u || face_count == 0u )
        return;

    const uint32_t corner_count = face_count * 3u;
    const uint8_t chunk_index = (uint8_t)batch->current_chunk;
    TRSPK_Batch16Chunk* ch = &batch->chunks[chunk_index];
    const uint32_t v0 = ch->vertex_count;
    const uint32_t i0 = ch->index_count;

    struct TRSPK_VertexBuffer vb = { 0 };
    if( !trspk_batch16_prepare_vertex_buffer(
            batch, &vb, corner_count, corner_count, batch->vertex_format) )
        return;

    if( !trspk_vertex_buffer_write_textured(
            vertex_count,
            face_count,
            vertices_x,
            vertices_y,
            vertices_z,
            faces_a,
            faces_b,
            faces_c,
            faces_a_color_hsl16,
            faces_b_color_hsl16,
            faces_c_color_hsl16,
            faces_textures,
            textured_faces,
            textured_faces_a,
            textured_faces_b,
            textured_faces_c,
            face_alphas,
            face_infos,
            uv_mode,
            bake,
            &vb) )
    {
        trspk_vertex_buffer_free(&vb);
        return;
    }

    (void)batch16_finalize_inplace_mesh_after_write(
        batch,
        chunk_index,
        v0,
        i0,
        corner_count,
        corner_count,
        (TRSPK_ModelId)model_id,
        gpu_segment_slot,
        frame_index);

    trspk_vertex_buffer_free(&vb);
}
