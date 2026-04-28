#include "trspk_vertex_buffer.h"
#include "trspk_vertex_format.h"

#include <stdlib.h>
#include <string.h>

bool
trspk_vertex_buffer_allocate_mesh(
    TRSPK_VertexBuffer* vb,
    uint32_t vertex_count,
    uint32_t index_count,
    TRSPK_VertexFormat format)
{
    if( !vb || vertex_count == 0u || index_count == 0u || format == TRSPK_VERTEX_FORMAT_NONE )
        return false;

    trspk_vertex_buffer_free(vb);
    memset(vb, 0, sizeof(*vb));

    uint32_t* idx = (uint32_t*)malloc((size_t)index_count * sizeof(uint32_t));
    if( !idx )
        return false;

    vb->indices.as_u32 = idx;
    vb->index_format = TRSPK_INDEX_FORMAT_U32;
    vb->index_base = 0u;
    vb->index_count = index_count;
    vb->vertex_count = vertex_count;
    vb->format = format;
    vb->status = TRSPK_VERTEX_BUFFER_STATUS_READY;

    switch( format )
    {
    case TRSPK_VERTEX_FORMAT_TRSPK:
        vb->vertices.as_trspk = (TRSPK_Vertex*)malloc((size_t)vertex_count * sizeof(TRSPK_Vertex));
        if( !vb->vertices.as_trspk )
            goto fail;
        return true;
    case TRSPK_VERTEX_FORMAT_WEBGL1:
        vb->vertices.as_webgl1 =
            (TRSPK_VertexWebGL1*)malloc((size_t)vertex_count * sizeof(TRSPK_VertexWebGL1));
        if( !vb->vertices.as_webgl1 )
            goto fail;
        return true;
    case TRSPK_VERTEX_FORMAT_METAL:
        vb->vertices.as_metal =
            (TRSPK_VertexMetal*)malloc((size_t)vertex_count * sizeof(TRSPK_VertexMetal));
        if( !vb->vertices.as_metal )
            goto fail;
        return true;
    case TRSPK_VERTEX_FORMAT_WEBGL1_ARRAY:
        if( !trspk_webgl1_vertex_array_alloc(&vb->vertices.as_webgl1_array, vertex_count) )
            goto fail;
        return true;
    case TRSPK_VERTEX_FORMAT_METAL_ARRAY:
        if( !trspk_metal_vertex_array_alloc(&vb->vertices.as_metal_array, vertex_count) )
            goto fail;
        return true;
    case TRSPK_VERTEX_FORMAT_NONE:
    default:
        break;
    }
fail:
    trspk_vertex_buffer_free(vb);
    return false;
}

void
trspk_vertex_buffer_free(TRSPK_VertexBuffer* m)
{
    if( !m )
        return;
    if( m->status == TRSPK_VERTEX_BUFFER_STATUS_BATCH_VIEW )
    {
        memset(m, 0, sizeof(*m));
        m->format = TRSPK_VERTEX_FORMAT_NONE;
        return;
    }

    if( m->index_format == TRSPK_INDEX_FORMAT_U32 )
        free(m->indices.as_u32);
    else
        free(m->indices.as_u16);
    m->indices.as_u32 = NULL;
    m->indices.as_u16 = NULL;
    m->index_count = 0u;
    m->vertex_count = 0u;
    if( m->format == TRSPK_VERTEX_FORMAT_NONE )
    {
        memset(m, 0, sizeof(*m));
        m->format = TRSPK_VERTEX_FORMAT_NONE;
        return;
    }
    switch( m->format )
    {
    case TRSPK_VERTEX_FORMAT_TRSPK:
        free(m->vertices.as_trspk);
        m->vertices.as_trspk = NULL;
        break;
    case TRSPK_VERTEX_FORMAT_WEBGL1:
        free(m->vertices.as_webgl1);
        m->vertices.as_webgl1 = NULL;
        break;
    case TRSPK_VERTEX_FORMAT_METAL:
        free(m->vertices.as_metal);
        m->vertices.as_metal = NULL;
        break;
    case TRSPK_VERTEX_FORMAT_WEBGL1_ARRAY:
        trspk_webgl1_vertex_array_free(&m->vertices.as_webgl1_array);
        break;
    case TRSPK_VERTEX_FORMAT_METAL_ARRAY:
        trspk_metal_vertex_array_free(&m->vertices.as_metal_array);
        break;
    case TRSPK_VERTEX_FORMAT_NONE:
        break;
    default:
        free(m->vertices.as_trspk);
        m->vertices.as_trspk = NULL;
        break;
    }
    memset(m, 0, sizeof(*m));
    m->format = TRSPK_VERTEX_FORMAT_NONE;
}

bool
trspk_vertex_buffer_has_vertex_payload(const TRSPK_VertexBuffer* m)
{
    if( !m )
        return false;
    switch( m->format )
    {
    case TRSPK_VERTEX_FORMAT_NONE:
        return false;
    case TRSPK_VERTEX_FORMAT_TRSPK:
        return m->vertices.as_trspk != NULL;
    case TRSPK_VERTEX_FORMAT_WEBGL1:
        return m->vertices.as_webgl1 != NULL;
    case TRSPK_VERTEX_FORMAT_METAL:
        return m->vertices.as_metal != NULL;
    case TRSPK_VERTEX_FORMAT_WEBGL1_ARRAY:
    {
        const TRSPK_VertexWebGL1Array* a = &m->vertices.as_webgl1_array;
        return a->position_x && a->position_y && a->position_z && a->position_w && a->color_r &&
               a->color_g && a->color_b && a->color_a && a->texcoord_u && a->texcoord_v &&
               a->tex_id && a->uv_mode;
    }
    case TRSPK_VERTEX_FORMAT_METAL_ARRAY:
    {
        const TRSPK_VertexMetalArray* a = &m->vertices.as_metal_array;
        return a->position_x && a->position_y && a->position_z && a->position_w && a->color_r &&
               a->color_g && a->color_b && a->color_a && a->texcoord_u && a->texcoord_v &&
               a->tex_id && a->uv_mode;
    }
    default:
        return false;
    }
}

bool
trspk_vertex_buffer_convert_from_trspk(
    TRSPK_VertexBuffer* m,
    TRSPK_VertexFormat dst_format)
{
    if( !m || m->format != TRSPK_VERTEX_FORMAT_TRSPK || !m->vertices.as_trspk )
        return false;
    if( dst_format == TRSPK_VERTEX_FORMAT_NONE || dst_format == TRSPK_VERTEX_FORMAT_TRSPK )
        return dst_format != TRSPK_VERTEX_FORMAT_NONE;
    if( m->vertex_count == 0u )
        return false;
    const uint32_t n = m->vertex_count;
    TRSPK_Vertex* src = m->vertices.as_trspk;
    if( dst_format == TRSPK_VERTEX_FORMAT_WEBGL1_ARRAY )
    {
        TRSPK_VertexWebGL1Array soa;
        if( !trspk_webgl1_vertex_array_from_trspk(&soa, src, n) )
            return false;
        free(src);
        m->vertices.as_webgl1_array = soa;
        m->format = TRSPK_VERTEX_FORMAT_WEBGL1_ARRAY;
        return true;
    }
    if( dst_format == TRSPK_VERTEX_FORMAT_METAL_ARRAY )
    {
        TRSPK_VertexMetalArray soa;
        if( !trspk_metal_vertex_array_from_trspk(&soa, src, n) )
            return false;
        free(src);
        m->vertices.as_metal_array = soa;
        m->format = TRSPK_VERTEX_FORMAT_METAL_ARRAY;
        return true;
    }
    const size_t bytes = (size_t)trspk_vertex_format_stride(dst_format) * (size_t)n;
    if( bytes == 0u )
        return false;
    void* buf = malloc(bytes);
    if( !buf )
        return false;
    trspk_vertex_format_convert(buf, src, n, dst_format);
    free(src);
    m->vertices.as_trspk = NULL;
    m->format = dst_format;
    switch( dst_format )
    {
    case TRSPK_VERTEX_FORMAT_TRSPK:
        m->vertices.as_trspk = (TRSPK_Vertex*)buf;
        break;
    case TRSPK_VERTEX_FORMAT_WEBGL1:
        m->vertices.as_webgl1 = (TRSPK_VertexWebGL1*)buf;
        break;
    case TRSPK_VERTEX_FORMAT_METAL:
        m->vertices.as_metal = (TRSPK_VertexMetal*)buf;
        break;
    case TRSPK_VERTEX_FORMAT_NONE:
    case TRSPK_VERTEX_FORMAT_WEBGL1_ARRAY:
    case TRSPK_VERTEX_FORMAT_METAL_ARRAY:
    default:
        free(buf);
        m->format = TRSPK_VERTEX_FORMAT_TRSPK;
        m->vertices.as_trspk = NULL;
        return false;
    }
    return true;
}
