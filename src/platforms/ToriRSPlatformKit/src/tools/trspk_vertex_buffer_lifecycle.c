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
    case TRSPK_VERTEX_FORMAT_D3D8:
        vb->vertices.as_d3d8 =
            (TRSPK_VertexD3D8*)malloc((size_t)vertex_count * sizeof(TRSPK_VertexD3D8));
        if( !vb->vertices.as_d3d8 )
            goto fail;
        return true;
    case TRSPK_VERTEX_FORMAT_WEBGL1_SOAOS:
        if( !trspk_webgl1_vertex_soaos_alloc(&vb->vertices.as_webgl1_soaos, vertex_count) )
            goto fail;
        return true;
    case TRSPK_VERTEX_FORMAT_METAL_SOAOS:
        if( !trspk_metal_vertex_soaos_alloc(&vb->vertices.as_metal_soaos, vertex_count) )
            goto fail;
        return true;
    case TRSPK_VERTEX_FORMAT_D3D8_SOAOS:
        if( !trspk_d8_vertex_soaos_alloc(&vb->vertices.as_d3d8_soaos, vertex_count) )
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
    case TRSPK_VERTEX_FORMAT_D3D8:
        free(m->vertices.as_d3d8);
        m->vertices.as_d3d8 = NULL;
        break;
    case TRSPK_VERTEX_FORMAT_WEBGL1_SOAOS:
        trspk_webgl1_vertex_soaos_free(&m->vertices.as_webgl1_soaos);
        break;
    case TRSPK_VERTEX_FORMAT_METAL_SOAOS:
        trspk_metal_vertex_soaos_free(&m->vertices.as_metal_soaos);
        break;
    case TRSPK_VERTEX_FORMAT_D3D8_SOAOS:
        trspk_d8_vertex_soaos_free(&m->vertices.as_d3d8_soaos);
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
    case TRSPK_VERTEX_FORMAT_D3D8:
        return m->vertices.as_d3d8 != NULL;
    case TRSPK_VERTEX_FORMAT_WEBGL1_SOAOS:
    {
        const TRSPK_VertexWebGL1Soaos* a = &m->vertices.as_webgl1_soaos;
        return a->vertex_count > 0u && a->blocks != NULL;
    }
    case TRSPK_VERTEX_FORMAT_METAL_SOAOS:
    {
        const TRSPK_VertexMetalSoaos* a = &m->vertices.as_metal_soaos;
        return a->vertex_count > 0u && a->blocks != NULL;
    }
    case TRSPK_VERTEX_FORMAT_D3D8_SOAOS:
    {
        const TRSPK_VertexD3D8Soaos* a = &m->vertices.as_d3d8_soaos;
        return a->vertex_count > 0u && a->blocks != NULL;
    }
    default:
        return false;
    }
}

bool
trspk_vertex_buffer_duplicate(const TRSPK_VertexBuffer* src, TRSPK_VertexBuffer* out)
{
    if( !out )
        return false;
    trspk_vertex_buffer_free(out);
    memset(out, 0, sizeof(*out));
    if( !src || src->status == TRSPK_VERTEX_BUFFER_STATUS_BATCH_VIEW ||
        !trspk_vertex_buffer_has_vertex_payload(src) )
        return false;
    if( !trspk_vertex_buffer_allocate_mesh(
            out, src->vertex_count, src->index_count, src->format ) )
        return false;
    out->index_base = src->index_base;
    if( src->index_format == TRSPK_INDEX_FORMAT_U32 )
        memcpy(
            out->indices.as_u32,
            src->indices.as_u32,
            (size_t)src->index_count * sizeof(uint32_t));
    else if( src->index_format == TRSPK_INDEX_FORMAT_U16 && src->indices.as_u16 )
    {
        for( uint32_t i = 0; i < src->index_count; ++i )
            out->indices.as_u32[i] = (uint32_t)src->indices.as_u16[i];
    }
    else
    {
        trspk_vertex_buffer_free(out);
        memset(out, 0, sizeof(*out));
        return false;
    }

    const uint32_t n = src->vertex_count;
    switch( src->format )
    {
    case TRSPK_VERTEX_FORMAT_TRSPK:
        memcpy(
            out->vertices.as_trspk,
            src->vertices.as_trspk,
            (size_t)n * sizeof(TRSPK_Vertex));
        break;
    case TRSPK_VERTEX_FORMAT_WEBGL1:
        memcpy(
            out->vertices.as_webgl1,
            src->vertices.as_webgl1,
            (size_t)n * sizeof(TRSPK_VertexWebGL1));
        break;
    case TRSPK_VERTEX_FORMAT_METAL:
        memcpy(
            out->vertices.as_metal,
            src->vertices.as_metal,
            (size_t)n * sizeof(TRSPK_VertexMetal));
        break;
    case TRSPK_VERTEX_FORMAT_D3D8:
        memcpy(
            out->vertices.as_d3d8,
            src->vertices.as_d3d8,
            (size_t)n * sizeof(TRSPK_VertexD3D8));
        break;
    case TRSPK_VERTEX_FORMAT_WEBGL1_SOAOS:
    {
        const TRSPK_VertexWebGL1Soaos* s = &src->vertices.as_webgl1_soaos;
        TRSPK_VertexWebGL1Soaos* d = &out->vertices.as_webgl1_soaos;
        const size_t nb =
            (size_t)s->block_count * sizeof(TRSPK_VertexWebGL1SoaosBlock);
        memcpy(d->blocks, s->blocks, nb);
        break;
    }
    case TRSPK_VERTEX_FORMAT_METAL_SOAOS:
    {
        const TRSPK_VertexMetalSoaos* s = &src->vertices.as_metal_soaos;
        TRSPK_VertexMetalSoaos* d = &out->vertices.as_metal_soaos;
        const size_t nb =
            (size_t)s->block_count * sizeof(TRSPK_VertexMetalSoaosBlock);
        memcpy(d->blocks, s->blocks, nb);
        break;
    }
    case TRSPK_VERTEX_FORMAT_D3D8_SOAOS:
    {
        const TRSPK_VertexD3D8Soaos* s = &src->vertices.as_d3d8_soaos;
        TRSPK_VertexD3D8Soaos* d = &out->vertices.as_d3d8_soaos;
        const size_t nb =
            (size_t)s->block_count * sizeof(TRSPK_VertexD3D8SoaosBlock);
        memcpy(d->blocks, s->blocks, nb);
        break;
    }
    default:
        trspk_vertex_buffer_free(out);
        memset(out, 0, sizeof(*out));
        return false;
    }
    return true;
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
    if( dst_format == TRSPK_VERTEX_FORMAT_WEBGL1_SOAOS )
    {
        TRSPK_VertexWebGL1Soaos soa;
        if( !trspk_webgl1_vertex_soaos_from_trspk(&soa, src, n) )
            return false;
        free(src);
        m->vertices.as_webgl1_soaos = soa;
        m->format = TRSPK_VERTEX_FORMAT_WEBGL1_SOAOS;
        return true;
    }
    if( dst_format == TRSPK_VERTEX_FORMAT_METAL_SOAOS )
    {
        TRSPK_VertexMetalSoaos soa;
        if( !trspk_metal_vertex_soaos_from_trspk(&soa, src, n) )
            return false;
        free(src);
        m->vertices.as_metal_soaos = soa;
        m->format = TRSPK_VERTEX_FORMAT_METAL_SOAOS;
        return true;
    }
    if( dst_format == TRSPK_VERTEX_FORMAT_D3D8_SOAOS )
    {
        TRSPK_VertexD3D8Soaos soa;
        if( !trspk_d8_vertex_soaos_from_trspk(&soa, src, n) )
            return false;
        free(src);
        m->vertices.as_d3d8_soaos = soa;
        m->format = TRSPK_VERTEX_FORMAT_D3D8_SOAOS;
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
    case TRSPK_VERTEX_FORMAT_D3D8:
        m->vertices.as_d3d8 = (TRSPK_VertexD3D8*)buf;
        break;
    case TRSPK_VERTEX_FORMAT_NONE:
    case TRSPK_VERTEX_FORMAT_WEBGL1_SOAOS:
    case TRSPK_VERTEX_FORMAT_METAL_SOAOS:
    case TRSPK_VERTEX_FORMAT_D3D8_SOAOS:
    default:
        free(buf);
        m->format = TRSPK_VERTEX_FORMAT_TRSPK;
        m->vertices.as_trspk = NULL;
        return false;
    }
    return true;
}
