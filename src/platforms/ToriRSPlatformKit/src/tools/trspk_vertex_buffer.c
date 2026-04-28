#include "trspk_vertex_buffer.h"

#include "../../include/ToriRSPlatformKit/trspk_math.h"
#include "trspk_vertex_format.h"

#include <stdlib.h>
#include <string.h>

void
trspk_vertex_buffer_free(TRSPK_VertexBuffer* m)
{
    if( !m )
        return;
    free(m->indices);
    m->indices = NULL;
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
        return a->position && a->color && a->texcoord && a->tex_id && a->uv_mode;
    }
    case TRSPK_VERTEX_FORMAT_METAL_ARRAY:
    {
        const TRSPK_VertexMetalArray* a = &m->vertices.as_metal_array;
        return a->position && a->color && a->texcoord && a->tex_id && a->uv_mode;
    }
    default:
        return false;
    }
}

bool
trspk_vertex_buffer_set_trspk_expanded(
    TRSPK_VertexBuffer* m,
    TRSPK_Vertex* vertices_ownership,
    uint32_t vertex_count,
    uint32_t* indices_ownership,
    uint32_t index_count)
{
    if( !m || !vertices_ownership || !indices_ownership || vertex_count == 0u || index_count == 0u )
        return false;
    trspk_vertex_buffer_free(m);
    m->format = TRSPK_VERTEX_FORMAT_TRSPK;
    m->vertex_count = vertex_count;
    m->index_count = index_count;
    m->vertices.as_trspk = vertices_ownership;
    m->indices = indices_ownership;
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

static void
trspk_from_face_slice_hidden_vertex(
    TRSPK_Vertex* v,
    float alpha)
{
    for( int i = 0; i < 4; ++i )
    {
        v->position[i] = 0.0f;
        v->color[i] = 0.0f;
    }
    v->position[3] = 1.0f;
    v->color[3] = alpha;
    v->texcoord[0] = 0.0f;
    v->texcoord[1] = 0.0f;
    v->tex_id = (float)TRSPK_INVALID_TEXTURE_ID;
    v->uv_mode = 0.0f;
}

static void
trspk_from_face_slice_vertex(
    TRSPK_Vertex* v,
    float x,
    float y,
    float z,
    const float color[4],
    float u,
    float vv,
    uint16_t tex_id,
    const TRSPK_BakeTransform* bake)
{
    v->position[0] = x;
    v->position[1] = y;
    v->position[2] = z;
    v->position[3] = 1.0f;
    trspk_bake_transform_apply(bake, &v->position[0], &v->position[1], &v->position[2]);
    v->color[0] = color[0];
    v->color[1] = color[1];
    v->color[2] = color[2];
    v->color[3] = color[3];
    v->texcoord[0] = u;
    v->texcoord[1] = vv;
    v->tex_id = (float)tex_id;
    v->uv_mode = 0.0f;
}

static bool
trspk_vertex_buffer_from_face_slice_impl(
    uint32_t start_face,
    uint32_t slice_face_count,
    uint32_t vertex_count,
    const int16_t* vertices_x,
    const int16_t* vertices_y,
    const int16_t* vertices_z,
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
    const TRSPK_BakeTransform* bake,
    bool textured,
    bool u32_indices,
    TRSPK_Vertex** out_vertices,
    void** out_indices,
    uint32_t* out_vertex_count)
{
    if( out_vertex_count )
        *out_vertex_count = 0u;
    if( out_vertices )
        *out_vertices = NULL;
    if( out_indices )
        *out_indices = NULL;
    if( !out_vertices || !out_indices || !vertices_x || !vertices_y || !vertices_z || !faces_a ||
        !faces_b || !faces_c || !faces_a_color_hsl16 || !faces_b_color_hsl16 ||
        !faces_c_color_hsl16 || slice_face_count == 0u )
        return false;
    if( (uint64_t)start_face + (uint64_t)slice_face_count > (uint64_t)1 << 32 )
        return false;
    const uint32_t out_count = slice_face_count * 3u;
    TRSPK_Vertex* vertices = (TRSPK_Vertex*)calloc(out_count, sizeof(TRSPK_Vertex));
    void* indices = NULL;
    if( u32_indices )
        indices = calloc(out_count, sizeof(uint32_t));
    else
        indices = calloc(out_count, sizeof(uint16_t));
    if( !vertices || !indices )
    {
        free(vertices);
        free(indices);
        return false;
    }
    if( u32_indices )
    {
        uint32_t* pi = (uint32_t*)indices;
        for( uint32_t i = 0; i < out_count; ++i )
            pi[i] = i;
    }
    else
    {
        uint16_t* pi = (uint16_t*)indices;
        for( uint32_t i = 0; i < out_count; ++i )
            pi[i] = (uint16_t)i;
    }

    for( uint32_t local_face = 0; local_face < slice_face_count; ++local_face )
    {
        const uint32_t face_i = start_face + local_face;
        const uint32_t base = local_face * 3u;
        uint8_t alpha = 0xFFu;
        if( face_alphas )
            alpha = (uint8_t)(0xFFu - face_alphas[face_i]);
        const float af = (float)alpha / 255.0f;
        const uint32_t idx_a = (uint32_t)faces_a[face_i];
        const uint32_t idx_b = (uint32_t)faces_b[face_i];
        const uint32_t idx_c = (uint32_t)faces_c[face_i];
        const bool skip = (face_infos && ((face_infos[face_i] & 3) == 2)) ||
                          (faces_c_color_hsl16[face_i] == (uint16_t)TRSPK_HSL16_HIDDEN);
        const bool bad = idx_a >= vertex_count || idx_b >= vertex_count || idx_c >= vertex_count;
        if( skip || bad )
        {
            trspk_from_face_slice_hidden_vertex(&vertices[base + 0u], af);
            trspk_from_face_slice_hidden_vertex(&vertices[base + 1u], af);
            trspk_from_face_slice_hidden_vertex(&vertices[base + 2u], af);
            continue;
        }
        float col_a[4], col_b[4], col_c[4];
        if( faces_c_color_hsl16[face_i] == (uint16_t)TRSPK_HSL16_FLAT )
        {
            trspk_hsl16_to_rgba(faces_a_color_hsl16[face_i], col_a, af);
            col_b[0] = col_c[0] = col_a[0];
            col_b[1] = col_c[1] = col_a[1];
            col_b[2] = col_c[2] = col_a[2];
            col_b[3] = col_c[3] = col_a[3];
        }
        else
        {
            trspk_hsl16_to_rgba(faces_a_color_hsl16[face_i], col_a, af);
            trspk_hsl16_to_rgba(faces_b_color_hsl16[face_i], col_b, af);
            trspk_hsl16_to_rgba(faces_c_color_hsl16[face_i], col_c, af);
        }
        TRSPK_UVFaceCoords uv = { 0 };
        uint16_t tex_id = TRSPK_INVALID_TEXTURE_ID;
        if( textured )
        {
            uint32_t uv_idx_p, uv_idx_m, uv_idx_n;
            if( uv_mode == TRSPK_UV_MODE_FIRST_FACE )
            {
                uv_idx_p = (uint32_t)faces_a[0];
                uv_idx_m = (uint32_t)faces_b[0];
                uv_idx_n = (uint32_t)faces_c[0];
            }
            else if( textured_faces && textured_faces[face_i] != TRSPK_INVALID_TEXTURE_ID )
            {
                const uint16_t tf = textured_faces[face_i];
                uv_idx_p = (uint32_t)textured_faces_a[tf];
                uv_idx_m = (uint32_t)textured_faces_b[tf];
                uv_idx_n = (uint32_t)textured_faces_c[tf];
            }
            else
            {
                uv_idx_p = idx_a;
                uv_idx_m = idx_b;
                uv_idx_n = idx_c;
            }
            trspk_uv_pnm_compute(
                &uv,
                (float)vertices_x[uv_idx_p],
                (float)vertices_y[uv_idx_p],
                (float)vertices_z[uv_idx_p],
                (float)vertices_x[uv_idx_m],
                (float)vertices_y[uv_idx_m],
                (float)vertices_z[uv_idx_m],
                (float)vertices_x[uv_idx_n],
                (float)vertices_y[uv_idx_n],
                (float)vertices_z[uv_idx_n],
                (float)vertices_x[idx_a],
                (float)vertices_y[idx_a],
                (float)vertices_z[idx_a],
                (float)vertices_x[idx_b],
                (float)vertices_y[idx_b],
                (float)vertices_z[idx_b],
                (float)vertices_x[idx_c],
                (float)vertices_y[idx_c],
                (float)vertices_z[idx_c]);
            if( faces_textures && faces_textures[face_i] != -1 )
                tex_id = (uint16_t)faces_textures[face_i];
        }
        trspk_from_face_slice_vertex(
            &vertices[base + 0u],
            (float)vertices_x[idx_a],
            (float)vertices_y[idx_a],
            (float)vertices_z[idx_a],
            col_a,
            uv.u1,
            uv.v1,
            tex_id,
            bake);
        trspk_from_face_slice_vertex(
            &vertices[base + 1u],
            (float)vertices_x[idx_b],
            (float)vertices_y[idx_b],
            (float)vertices_z[idx_b],
            col_b,
            uv.u2,
            uv.v2,
            tex_id,
            bake);
        trspk_from_face_slice_vertex(
            &vertices[base + 2u],
            (float)vertices_x[idx_c],
            (float)vertices_y[idx_c],
            (float)vertices_z[idx_c],
            col_c,
            uv.u3,
            uv.v3,
            tex_id,
            bake);
    }
    *out_vertices = vertices;
    *out_indices = indices;
    *out_vertex_count = out_count;
    return true;
}

bool
trspk_vertex_buffer_from_face_slice_u32_untextured(
    uint32_t start_face,
    uint32_t slice_face_count,
    uint32_t vertex_count,
    const int16_t* vertices_x,
    const int16_t* vertices_y,
    const int16_t* vertices_z,
    const uint16_t* faces_a,
    const uint16_t* faces_b,
    const uint16_t* faces_c,
    const uint16_t* faces_a_color_hsl16,
    const uint16_t* faces_b_color_hsl16,
    const uint16_t* faces_c_color_hsl16,
    const uint8_t* face_alphas,
    const int32_t* face_infos,
    const TRSPK_BakeTransform* bake,
    TRSPK_VertexBuffer* m)
{
    if( !m )
        return false;
    TRSPK_Vertex* v = NULL;
    void* ind = NULL;
    uint32_t vcount = 0u;
    if( !trspk_vertex_buffer_from_face_slice_impl(
            start_face,
            slice_face_count,
            vertex_count,
            vertices_x,
            vertices_y,
            vertices_z,
            faces_a,
            faces_b,
            faces_c,
            faces_a_color_hsl16,
            faces_b_color_hsl16,
            faces_c_color_hsl16,
            NULL,
            NULL,
            NULL,
            NULL,
            NULL,
            face_alphas,
            face_infos,
            TRSPK_UV_MODE_TEXTURED_FACE_ARRAY,
            bake,
            false,
            true,
            &v,
            &ind,
            &vcount) )
        return false;
    if( !trspk_vertex_buffer_set_trspk_expanded(m, v, vcount, (uint32_t*)ind, vcount) )
    {
        free(v);
        free(ind);
        return false;
    }
    return true;
}

bool
trspk_vertex_buffer_from_face_slice_u32_textured(
    uint32_t start_face,
    uint32_t slice_face_count,
    uint32_t vertex_count,
    const int16_t* vertices_x,
    const int16_t* vertices_y,
    const int16_t* vertices_z,
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
    const TRSPK_BakeTransform* bake,
    TRSPK_VertexBuffer* m)
{
    if( !m )
        return false;
    TRSPK_Vertex* v = NULL;
    void* ind = NULL;
    uint32_t vcount = 0u;
    if( !trspk_vertex_buffer_from_face_slice_impl(
            start_face,
            slice_face_count,
            vertex_count,
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
            true,
            true,
            &v,
            &ind,
            &vcount) )
        return false;
    if( !trspk_vertex_buffer_set_trspk_expanded(m, v, vcount, (uint32_t*)ind, vcount) )
    {
        free(v);
        free(ind);
        return false;
    }
    return true;
}

bool
trspk_vertex_buffer_from_face_slice_u16_untextured(
    uint32_t start_face,
    uint32_t slice_face_count,
    uint32_t vertex_count,
    const int16_t* vertices_x,
    const int16_t* vertices_y,
    const int16_t* vertices_z,
    const uint16_t* faces_a,
    const uint16_t* faces_b,
    const uint16_t* faces_c,
    const uint16_t* faces_a_color_hsl16,
    const uint16_t* faces_b_color_hsl16,
    const uint16_t* faces_c_color_hsl16,
    const uint8_t* face_alphas,
    const int32_t* face_infos,
    const TRSPK_BakeTransform* bake,
    TRSPK_VertexBuffer* m)
{
    if( !m )
        return false;
    TRSPK_Vertex* v = NULL;
    void* ind = NULL;
    uint32_t vcount = 0u;
    if( !trspk_vertex_buffer_from_face_slice_impl(
            start_face,
            slice_face_count,
            vertex_count,
            vertices_x,
            vertices_y,
            vertices_z,
            faces_a,
            faces_b,
            faces_c,
            faces_a_color_hsl16,
            faces_b_color_hsl16,
            faces_c_color_hsl16,
            NULL,
            NULL,
            NULL,
            NULL,
            NULL,
            face_alphas,
            face_infos,
            TRSPK_UV_MODE_TEXTURED_FACE_ARRAY,
            bake,
            false,
            false,
            &v,
            &ind,
            &vcount) )
        return false;
    uint16_t* i16 = (uint16_t*)ind;
    uint32_t* i32 = (uint32_t*)malloc((size_t)vcount * sizeof(uint32_t));
    if( !i32 )
    {
        free(v);
        free(i16);
        return false;
    }
    for( uint32_t j = 0; j < vcount; ++j )
        i32[j] = (uint32_t)i16[j];
    free(i16);
    if( !trspk_vertex_buffer_set_trspk_expanded(m, v, vcount, i32, vcount) )
    {
        free(v);
        free(i32);
        return false;
    }
    return true;
}

bool
trspk_vertex_buffer_from_face_slice_u16_textured(
    uint32_t start_face,
    uint32_t slice_face_count,
    uint32_t vertex_count,
    const int16_t* vertices_x,
    const int16_t* vertices_y,
    const int16_t* vertices_z,
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
    const TRSPK_BakeTransform* bake,
    TRSPK_VertexBuffer* m)
{
    if( !m )
        return false;
    TRSPK_Vertex* v = NULL;
    void* ind = NULL;
    uint32_t vcount = 0u;
    if( !trspk_vertex_buffer_from_face_slice_impl(
            start_face,
            slice_face_count,
            vertex_count,
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
            true,
            false,
            &v,
            &ind,
            &vcount) )
        return false;
    uint16_t* i16 = (uint16_t*)ind;
    uint32_t* i32 = (uint32_t*)malloc((size_t)vcount * sizeof(uint32_t));
    if( !i32 )
    {
        free(v);
        free(i16);
        return false;
    }
    for( uint32_t j = 0; j < vcount; ++j )
        i32[j] = (uint32_t)i16[j];
    free(i16);
    if( !trspk_vertex_buffer_set_trspk_expanded(m, v, vcount, i32, vcount) )
    {
        free(v);
        free(i32);
        return false;
    }
    return true;
}

static inline void
write_vertex(
    TRSPK_VertexBuffer* dest,
    int offset,
    float x,
    float y,
    float z,
    const float color[4],
    float u,
    float v,
    uint16_t tex_id)
{
    switch( dest->format )
    {
    case TRSPK_VERTEX_FORMAT_TRSPK:
        dest->vertices.as_trspk[offset].position[0] = x;
        dest->vertices.as_trspk[offset].position[1] = y;
        dest->vertices.as_trspk[offset].position[2] = z;
        dest->vertices.as_trspk[offset].position[3] = 1.0f;
        dest->vertices.as_trspk[offset].color[0] = color[0];
        dest->vertices.as_trspk[offset].color[1] = color[1];
        dest->vertices.as_trspk[offset].color[2] = color[2];
        dest->vertices.as_trspk[offset].color[3] = color[3];
        dest->vertices.as_trspk[offset].texcoord[0] = u;
        dest->vertices.as_trspk[offset].texcoord[1] = v;
        dest->vertices.as_trspk[offset].tex_id = (float)tex_id;
        dest->vertices.as_trspk[offset].uv_mode = 0.0f;
        break;
    case TRSPK_VERTEX_FORMAT_WEBGL1:
        dest->vertices.as_webgl1[offset].position[0] = x;
        dest->vertices.as_webgl1[offset].position[1] = y;
        dest->vertices.as_webgl1[offset].position[2] = z;
        dest->vertices.as_webgl1[offset].position[3] = 1.0f;
        dest->vertices.as_webgl1[offset].color[0] = color[0];
        dest->vertices.as_webgl1[offset].color[1] = color[1];
        dest->vertices.as_webgl1[offset].color[2] = color[2];
        dest->vertices.as_webgl1[offset].color[3] = color[3];
        dest->vertices.as_webgl1[offset].texcoord[0] = u;
        dest->vertices.as_webgl1[offset].texcoord[1] = v;
        dest->vertices.as_webgl1[offset].tex_id = (float)tex_id;
        dest->vertices.as_webgl1[offset].uv_mode = 0.0f;
        break;
    case TRSPK_VERTEX_FORMAT_METAL:
        dest->vertices.as_metal[offset].position[0] = x;
        dest->vertices.as_metal[offset].position[1] = y;
        dest->vertices.as_metal[offset].position[2] = z;
        dest->vertices.as_metal[offset].position[3] = 1.0f;
        dest->vertices.as_metal[offset].color[0] = color[0];
        dest->vertices.as_metal[offset].color[1] = color[1];
        dest->vertices.as_metal[offset].color[2] = color[2];
        dest->vertices.as_metal[offset].color[3] = color[3];
        dest->vertices.as_metal[offset].texcoord[0] = u;
        dest->vertices.as_metal[offset].texcoord[1] = v;
        dest->vertices.as_metal[offset].tex_id = (uint16_t)tex_id;
        dest->vertices.as_metal[offset].uv_mode = 0u;
        break;
    case TRSPK_VERTEX_FORMAT_WEBGL1_ARRAY:
        dest->vertices.as_webgl1_array.position_x[offset] = x;
        dest->vertices.as_webgl1_array.position_y[offset] = y;
        dest->vertices.as_webgl1_array.position_z[offset] = z;
        dest->vertices.as_webgl1_array.position_w[offset] = 1.0f;
        dest->vertices.as_webgl1_array.color_r[offset] = color[0];
        dest->vertices.as_webgl1_array.color_g[offset] = color[1];
        dest->vertices.as_webgl1_array.color_b[offset] = color[2];
        dest->vertices.as_webgl1_array.color_a[offset] = color[3];
        dest->vertices.as_webgl1_array.texcoord_u[offset] = u;
        dest->vertices.as_webgl1_array.texcoord_v[offset] = v;
        dest->vertices.as_webgl1_array.tex_id[offset] = (float)tex_id;
        dest->vertices.as_webgl1_array.uv_mode[offset] = 0.0f;
        break;
    case TRSPK_VERTEX_FORMAT_METAL_ARRAY:
        dest->vertices.as_metal_array.position_x[offset] = x;
        dest->vertices.as_metal_array.position_y[offset] = y;
        dest->vertices.as_metal_array.position_z[offset] = z;
        dest->vertices.as_metal_array.position_w[offset] = 1.0f;
        dest->vertices.as_metal_array.color_r[offset] = color[0];
        dest->vertices.as_metal_array.color_g[offset] = color[1];
        dest->vertices.as_metal_array.color_b[offset] = color[2];
        dest->vertices.as_metal_array.color_a[offset] = color[3];
        dest->vertices.as_metal_array.texcoord_u[offset] = u;
        dest->vertices.as_metal_array.texcoord_v[offset] = v;
        dest->vertices.as_metal_array.tex_id[offset] = (uint16_t)tex_id;
        dest->vertices.as_metal_array.uv_mode[offset] = 0u;
        break;
    default:
        return false;
    }
    return true;
}

static inline void
write_hidden_vertex(
    TRSPK_VertexBuffer* dest,
    int offset)
{
    const float color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    write_vertex(dest, offset, 0.0f, 0.0f, 0.0f, color, 0.0f, 0.0f, TRSPK_INVALID_TEXTURE_ID);
}
bool
trspk_vertex_buffer_write_texturedi16(
    uint32_t vertex_count,
    uint32_t face_count,
    const int16_t* vertices_x,
    const int16_t* vertices_y,
    const int16_t* vertices_z,
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
    const TRSPK_BakeTransform* bake,
    TRSPK_VertexBuffer* dest)
{
    for( uint32_t local_face = 0; local_face < face_count; ++local_face )
    {
        const uint32_t i = local_face;
        const uint32_t face_base_index = local_face * 3u;
        dest->indices.as_u16[face_base_index + 0u] = (uint16_t)(face_base_index + 0u);
        dest->indices.as_u16[face_base_index + 1u] = (uint16_t)(face_base_index + 1u);
        dest->indices.as_u16[face_base_index + 2u] = (uint16_t)(face_base_index + 2u);

        uint8_t alpha = 0xFFu;
        if( face_alphas )
            alpha = (uint8_t)(0xFFu - face_alphas[i]);
        const float alpha_float = (float)alpha / 255.0f;

        const bool skip = (face_infos && ((face_infos[i] & 3) == 2)) ||
                          (faces_c_color_hsl16[i] == (uint16_t)TRSPK_HSL16_HIDDEN);
        const uint32_t ia = faces_a[i];
        const uint32_t ib = faces_b[i];
        const uint32_t ic = faces_c[i];
        const bool bad = ia >= vertex_count || ib >= vertex_count || ic >= vertex_count;
        if( skip || bad )
        {
            write_hidden_vertex(dest, face_base_index + 0u);
            write_hidden_vertex(dest, face_base_index + 1u);
            write_hidden_vertex(dest, face_base_index + 2u);
            continue;
        }

        float color_a[4], color_b[4], color_c[4];
        if( faces_c_color_hsl16[i] == (uint16_t)TRSPK_HSL16_FLAT )
        {
            trspk_hsl16_to_rgba(faces_a_color_hsl16[i], color_a, alpha_float);
            color_b[0] = color_c[0] = color_a[0];
            color_b[1] = color_c[1] = color_a[1];
            color_b[2] = color_c[2] = color_a[2];
            color_b[3] = color_c[3] = color_a[3];
        }
        else
        {
            trspk_hsl16_to_rgba(faces_a_color_hsl16[i], color_a, alpha_float);
            trspk_hsl16_to_rgba(faces_b_color_hsl16[i], color_b, alpha_float);
            trspk_hsl16_to_rgba(faces_c_color_hsl16[i], color_c, alpha_float);
        }

        TRSPK_UVFaceCoords uv = { 0 };
        uint16_t tex_id = TRSPK_INVALID_TEXTURE_ID;
        uint32_t tp;
        uint32_t tm;
        uint32_t tn;
        if( uv_mode == TRSPK_UV_MODE_FIRST_FACE )
        {
            tp = (uint32_t)faces_a[0];
            tm = (uint32_t)faces_b[0];
            tn = (uint32_t)faces_c[0];
        }
        else if( textured_faces && textured_faces[i] != TRSPK_INVALID_TEXTURE_ID )
        {
            const uint16_t tf = textured_faces[i];
            tp = (uint32_t)textured_faces_a[tf];
            tm = (uint32_t)textured_faces_b[tf];
            tn = (uint32_t)textured_faces_c[tf];
        }
        else
        {
            tp = ia;
            tm = ib;
            tn = ic;
        }
        trspk_uv_pnm_compute(
            &uv,
            (float)vertices_x[tp],
            (float)vertices_y[tp],
            (float)vertices_z[tp],
            (float)vertices_x[tm],
            (float)vertices_y[tm],
            (float)vertices_z[tm],
            (float)vertices_x[tn],
            (float)vertices_y[tn],
            (float)vertices_z[tn],
            (float)vertices_x[ia],
            (float)vertices_y[ia],
            (float)vertices_z[ia],
            (float)vertices_x[ib],
            (float)vertices_y[ib],
            (float)vertices_z[ib],
            (float)vertices_x[ic],
            (float)vertices_y[ic],
            (float)vertices_z[ic]);
        if( faces_textures && faces_textures[i] != -1 )
            tex_id = (uint16_t)faces_textures[i];

        write_vertex(
            dest,
            face_base_index + 0u,
            (float)vertices_x[tp],
            (float)vertices_y[tp],
            (float)vertices_z[tp],
            color_a,
            uv.u1,
            uv.v1,
            tex_id);
        write_vertex(
            dest,
            face_base_index + 1u,
            (float)vertices_x[tm],
            (float)vertices_y[tm],
            (float)vertices_z[tm],
            color_b,
            uv.u2,
            uv.v2,
            tex_id);
        write_vertex(
            dest,
            face_base_index + 2u,
            (float)vertices_x[tn],
            (float)vertices_y[tn],
            (float)vertices_z[tn],
            color_c,
            uv.u3,
            uv.v3,
            tex_id);
    }
    return true;
}

bool
trspk_vertex_buffer_write_i16(
    uint32_t vertex_count,
    uint32_t face_count,
    const int16_t* vertices_x,
    const int16_t* vertices_y,
    const int16_t* vertices_z,
    const uint16_t* faces_a,
    const uint16_t* faces_b,
    const uint16_t* faces_c,
    const uint16_t* faces_a_color_hsl16,
    const uint16_t* faces_b_color_hsl16,
    const uint16_t* faces_c_color_hsl16,
    const uint8_t* face_alphas,
    const int32_t* face_infos,
    const TRSPK_BakeTransform* bake,
    TRSPK_VertexBuffer* dest)
{
    for( uint32_t local_face = 0; local_face < face_count; ++local_face )
    {
        const uint32_t i = local_face;
        const uint32_t face_base_index = local_face * 3u;
        dest->indices.as_u16[face_base_index + 0u] = (uint16_t)(face_base_index + 0u);
        dest->indices.as_u16[face_base_index + 1u] = (uint16_t)(face_base_index + 1u);
        dest->indices.as_u16[face_base_index + 2u] = (uint16_t)(face_base_index + 2u);

        uint8_t alpha = 0xFFu;
        if( face_alphas )
            alpha = (uint8_t)(0xFFu - face_alphas[i]);
        const float alpha_float = (float)alpha / 255.0f;

        const bool skip = (face_infos && ((face_infos[i] & 3) == 2)) ||
                          (faces_c_color_hsl16[i] == (uint16_t)TRSPK_HSL16_HIDDEN);
        const uint32_t ia = faces_a[i];
        const uint32_t ib = faces_b[i];
        const uint32_t ic = faces_c[i];
        const bool bad = ia >= vertex_count || ib >= vertex_count || ic >= vertex_count;
        if( skip || bad )
        {
            write_hidden_vertex(dest, face_base_index + 0u);
            write_hidden_vertex(dest, face_base_index + 1u);
            write_hidden_vertex(dest, face_base_index + 2u);
            continue;
        }

        float color_a[4], color_b[4], color_c[4];
        if( faces_c_color_hsl16[i] == (uint16_t)TRSPK_HSL16_FLAT )
        {
            trspk_hsl16_to_rgba(faces_a_color_hsl16[i], color_a, alpha_float);
            color_b[0] = color_c[0] = color_a[0];
            color_b[1] = color_c[1] = color_a[1];
            color_b[2] = color_c[2] = color_a[2];
            color_b[3] = color_c[3] = color_a[3];
        }
        else
        {
            trspk_hsl16_to_rgba(faces_a_color_hsl16[i], color_a, alpha_float);
            trspk_hsl16_to_rgba(faces_b_color_hsl16[i], color_b, alpha_float);
            trspk_hsl16_to_rgba(faces_c_color_hsl16[i], color_c, alpha_float);
        }

        write_vertex(
            dest,
            face_base_index + 0u,
            (float)vertices_x[ia],
            (float)vertices_y[ia],
            (float)vertices_z[ia],
            color_a,
            0.0f,
            0.0f,
            TRSPK_INVALID_TEXTURE_ID);

        write_vertex(
            dest,
            face_base_index + 1u,
            (float)vertices_x[ib],
            (float)vertices_y[ib],
            (float)vertices_z[ib],
            color_b,
            0.0f,
            0.0f,
            TRSPK_INVALID_TEXTURE_ID);

        write_vertex(
            dest,
            face_base_index + 2u,
            (float)vertices_x[ic],
            (float)vertices_y[ic],
            (float)vertices_z[ic],
            color_c,
            0.0f,
            0.0f,
            TRSPK_INVALID_TEXTURE_ID);
    }

    return true;
}
