#include "trspk_batch32.h"

#include "trspk_vertex_buffer.h"
#include "trspk_vertex_format.h"

#include <stdlib.h>
#include <string.h>

static bool
trspk_batch32_reserve_vertices(
    TRSPK_Batch32* batch,
    uint32_t count)
{
    if( !batch )
        return false;
    const uint32_t stride = trspk_vertex_format_stride(batch->vertex_format);
    if( stride == 0u )
        return false;
    if( count <= batch->vertex_capacity )
        return true;
    uint32_t cap = batch->vertex_capacity ? batch->vertex_capacity : 4096u;
    while( cap < count )
        cap *= 2u;
    uint8_t* nv = (uint8_t*)realloc(
        batch->vertices, (size_t)trspk_vertex_format_stride(batch->vertex_format) * cap);
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
    TRSPK_VertexFormat vertex_format)
{
    TRSPK_Batch32* batch = (TRSPK_Batch32*)calloc(1u, sizeof(TRSPK_Batch32));
    if( !batch )
        return NULL;
    batch->vertex_format = vertex_format;
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

    trspk_vertex_format_convert(
        batch->vertices + (size_t)vstart * trspk_vertex_format_stride(batch->vertex_format),
        vertices,
        vertex_count,
        batch->vertex_format,
        0.0);

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
        *out_vertex_bytes =
            batch ? batch->vertex_count * trspk_vertex_format_stride(batch->vertex_format) : 0u;
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

bool
trspk_batch32_prepare_vertex_buffer(
    TRSPK_Batch32* batch,
    TRSPK_VertexBuffer* vb,
    uint32_t vertex_count,
    uint32_t index_count,
    TRSPK_VertexFormat vertex_format)
{
    if( !batch || !vb || !batch->building || vertex_count == 0u || index_count == 0u )
        return false;
    if( vertex_format != batch->vertex_format )
        return false;

    const uint32_t stride = trspk_vertex_format_stride(vertex_format);
    if( stride == 0u )
        return false;

    trspk_vertex_buffer_free(vb);

    const uint32_t vstart = batch->vertex_count;
    const uint32_t istart = batch->index_count;
    if( !trspk_batch32_reserve_vertices(batch, vstart + vertex_count) ||
        !trspk_batch32_reserve_indices(batch, istart + index_count) )
        return false;

    uint8_t* vbase = batch->vertices + (size_t)vstart * (size_t)stride;
    uint32_t* ibase = batch->indices + istart;

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
    vb->index_format = TRSPK_INDEX_FORMAT_U32;
    vb->indices.as_u32 = ibase;
    vb->status = TRSPK_VERTEX_BUFFER_STATUS_BATCH_VIEW;
    return true;
}

static bool
batch32_finalize_inplace_mesh_after_write(
    TRSPK_Batch32* batch,
    uint32_t vstart,
    uint32_t istart,
    uint32_t vertex_count,
    uint32_t index_count,
    TRSPK_ModelId model_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index)
{
    if( !batch || !batch->building || vertex_count == 0u || index_count == 0u )
        return false;
    if( batch->vertex_count != vstart || batch->index_count != istart )
        return false;
    if( !batch->vertices || !batch->indices )
        return false;

    if( !trspk_batch32_reserve_entries(batch, batch->entry_count + 1u) )
        return false;

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
    const TRSPK_BakeTransform* bake)
{
    if( !batch || !batch->building || !vertices_x || !vertices_y || !vertices_z || !faces_a ||
        !faces_b || !faces_c || !faces_a_color_hsl16 || !faces_b_color_hsl16 ||
        !faces_c_color_hsl16 || vertex_count == 0u || face_count == 0u )
        return;

    const uint32_t corner_count = face_count * 3u;
    const uint32_t v0 = batch->vertex_count;
    const uint32_t i0 = batch->index_count;

    struct TRSPK_VertexBuffer vb = { 0 };
    if( !trspk_batch32_prepare_vertex_buffer(
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
            &vb,
            0.0,
            1.0f) )
    {
        trspk_vertex_buffer_free(&vb);
        return;
    }

    (void)batch32_finalize_inplace_mesh_after_write(
        batch,
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
    const TRSPK_BakeTransform* bake)
{
    if( !batch || !batch->building || !vertices_x || !vertices_y || !vertices_z || !faces_a ||
        !faces_b || !faces_c || !faces_a_color_hsl16 || !faces_b_color_hsl16 ||
        !faces_c_color_hsl16 || vertex_count == 0u || face_count == 0u )
        return;

    const uint32_t corner_count = face_count * 3u;
    const uint32_t v0 = batch->vertex_count;
    const uint32_t i0 = batch->index_count;

    struct TRSPK_VertexBuffer vb = { 0 };
    if( !trspk_batch32_prepare_vertex_buffer(
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
            uv_calc_mode,
            atlas_tile_meta,
            bake,
            &vb,
            0.0,
            1.0f) )
    {
        trspk_vertex_buffer_free(&vb);
        return;
    }

    (void)batch32_finalize_inplace_mesh_after_write(
        batch,
        v0,
        i0,
        corner_count,
        corner_count,
        (TRSPK_ModelId)model_id,
        gpu_segment_slot,
        frame_index);

    trspk_vertex_buffer_free(&vb);
}
