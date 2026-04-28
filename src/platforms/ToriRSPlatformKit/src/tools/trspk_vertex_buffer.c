#include "trspk_vertex_buffer.h"

#include "../../include/ToriRSPlatformKit/trspk_math.h"
#include "trspk_vertex_format.h"

static inline void
write_index(
    TRSPK_VertexBuffer* dest,
    int offset,
    uint32_t index)
{
    switch( dest->index_format )
    {
    case TRSPK_INDEX_FORMAT_U16:
        dest->indices.as_u16[offset] = (uint16_t)index + dest->index_base;
        break;
    case TRSPK_INDEX_FORMAT_U32:
        dest->indices.as_u32[offset] = index + dest->index_base;
        break;
    default:
        break;
    }
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
    float vv,
    uint16_t tex_id,
    const TRSPK_BakeTransform* bake)
{
    float px = x;
    float py = y;
    float pz = z;
    trspk_bake_transform_apply(bake, &px, &py, &pz);
    switch( dest->format )
    {
    case TRSPK_VERTEX_FORMAT_TRSPK:
        dest->vertices.as_trspk[offset].position[0] = px;
        dest->vertices.as_trspk[offset].position[1] = py;
        dest->vertices.as_trspk[offset].position[2] = pz;
        dest->vertices.as_trspk[offset].position[3] = 1.0f;
        dest->vertices.as_trspk[offset].color[0] = color[0];
        dest->vertices.as_trspk[offset].color[1] = color[1];
        dest->vertices.as_trspk[offset].color[2] = color[2];
        dest->vertices.as_trspk[offset].color[3] = color[3];
        dest->vertices.as_trspk[offset].texcoord[0] = u;
        dest->vertices.as_trspk[offset].texcoord[1] = vv;
        dest->vertices.as_trspk[offset].tex_id = (float)tex_id;
        dest->vertices.as_trspk[offset].uv_mode = 0.0f;
        break;
    case TRSPK_VERTEX_FORMAT_WEBGL1:
        dest->vertices.as_webgl1[offset].position[0] = px;
        dest->vertices.as_webgl1[offset].position[1] = py;
        dest->vertices.as_webgl1[offset].position[2] = pz;
        dest->vertices.as_webgl1[offset].position[3] = 1.0f;
        dest->vertices.as_webgl1[offset].color[0] = color[0];
        dest->vertices.as_webgl1[offset].color[1] = color[1];
        dest->vertices.as_webgl1[offset].color[2] = color[2];
        dest->vertices.as_webgl1[offset].color[3] = color[3];
        dest->vertices.as_webgl1[offset].texcoord[0] = u;
        dest->vertices.as_webgl1[offset].texcoord[1] = vv;
        dest->vertices.as_webgl1[offset].tex_id = (float)tex_id;
        dest->vertices.as_webgl1[offset].uv_mode = 0.0f;
        break;
    case TRSPK_VERTEX_FORMAT_METAL:
        dest->vertices.as_metal[offset].position[0] = px;
        dest->vertices.as_metal[offset].position[1] = py;
        dest->vertices.as_metal[offset].position[2] = pz;
        dest->vertices.as_metal[offset].position[3] = 1.0f;
        dest->vertices.as_metal[offset].color[0] = color[0];
        dest->vertices.as_metal[offset].color[1] = color[1];
        dest->vertices.as_metal[offset].color[2] = color[2];
        dest->vertices.as_metal[offset].color[3] = color[3];
        dest->vertices.as_metal[offset].texcoord[0] = u;
        dest->vertices.as_metal[offset].texcoord[1] = vv;
        dest->vertices.as_metal[offset].tex_id = (uint16_t)tex_id;
        dest->vertices.as_metal[offset].uv_mode = 0u;
        break;
    case TRSPK_VERTEX_FORMAT_WEBGL1_ARRAY:
        dest->vertices.as_webgl1_array.position_x[offset] = px;
        dest->vertices.as_webgl1_array.position_y[offset] = py;
        dest->vertices.as_webgl1_array.position_z[offset] = pz;
        dest->vertices.as_webgl1_array.position_w[offset] = 1.0f;
        dest->vertices.as_webgl1_array.color_r[offset] = color[0];
        dest->vertices.as_webgl1_array.color_g[offset] = color[1];
        dest->vertices.as_webgl1_array.color_b[offset] = color[2];
        dest->vertices.as_webgl1_array.color_a[offset] = color[3];
        dest->vertices.as_webgl1_array.texcoord_u[offset] = u;
        dest->vertices.as_webgl1_array.texcoord_v[offset] = vv;
        dest->vertices.as_webgl1_array.tex_id[offset] = (float)tex_id;
        dest->vertices.as_webgl1_array.uv_mode[offset] = 0.0f;
        break;
    case TRSPK_VERTEX_FORMAT_METAL_ARRAY:
        dest->vertices.as_metal_array.position_x[offset] = px;
        dest->vertices.as_metal_array.position_y[offset] = py;
        dest->vertices.as_metal_array.position_z[offset] = pz;
        dest->vertices.as_metal_array.position_w[offset] = 1.0f;
        dest->vertices.as_metal_array.color_r[offset] = color[0];
        dest->vertices.as_metal_array.color_g[offset] = color[1];
        dest->vertices.as_metal_array.color_b[offset] = color[2];
        dest->vertices.as_metal_array.color_a[offset] = color[3];
        dest->vertices.as_metal_array.texcoord_u[offset] = u;
        dest->vertices.as_metal_array.texcoord_v[offset] = vv;
        dest->vertices.as_metal_array.tex_id[offset] = (uint16_t)tex_id;
        dest->vertices.as_metal_array.uv_mode[offset] = 0u;
        break;
    default:
        break;
    }
}

static inline void
write_hidden_vertex(
    TRSPK_VertexBuffer* dest,
    int offset,
    float alpha)
{
    float color[4] = { 0.0f, 0.0f, 0.0f, alpha };
    write_vertex(dest, offset, 0.0f, 0.0f, 0.0f, color, 0.0f, 0.0f, TRSPK_INVALID_TEXTURE_ID, NULL);
}

bool
trspk_vertex_buffer_write_textured(
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
        write_index(dest, (int)face_base_index + 0, face_base_index + 0u);
        write_index(dest, (int)face_base_index + 1, face_base_index + 1u);
        write_index(dest, (int)face_base_index + 2, face_base_index + 2u);

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
            write_hidden_vertex(dest, (int)face_base_index + 0, alpha_float);
            write_hidden_vertex(dest, (int)face_base_index + 1, alpha_float);
            write_hidden_vertex(dest, (int)face_base_index + 2, alpha_float);
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
            (int)face_base_index + 0,
            (float)vertices_x[ia],
            (float)vertices_y[ia],
            (float)vertices_z[ia],
            color_a,
            uv.u1,
            uv.v1,
            tex_id,
            bake);
        write_vertex(
            dest,
            (int)face_base_index + 1,
            (float)vertices_x[ib],
            (float)vertices_y[ib],
            (float)vertices_z[ib],
            color_b,
            uv.u2,
            uv.v2,
            tex_id,
            bake);
        write_vertex(
            dest,
            (int)face_base_index + 2,
            (float)vertices_x[ic],
            (float)vertices_y[ic],
            (float)vertices_z[ic],
            color_c,
            uv.u3,
            uv.v3,
            tex_id,
            bake);
    }
    return true;
}

bool
trspk_vertex_buffer_write(
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
        write_index(dest, (int)face_base_index + 0, face_base_index + 0u);
        write_index(dest, (int)face_base_index + 1, face_base_index + 1u);
        write_index(dest, (int)face_base_index + 2, face_base_index + 2u);

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
            write_hidden_vertex(dest, (int)face_base_index + 0, alpha_float);
            write_hidden_vertex(dest, (int)face_base_index + 1, alpha_float);
            write_hidden_vertex(dest, (int)face_base_index + 2, alpha_float);
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
            (int)face_base_index + 0,
            (float)vertices_x[ia],
            (float)vertices_y[ia],
            (float)vertices_z[ia],
            color_a,
            0.0f,
            0.0f,
            TRSPK_INVALID_TEXTURE_ID,
            bake);

        write_vertex(
            dest,
            (int)face_base_index + 1,
            (float)vertices_x[ib],
            (float)vertices_y[ib],
            (float)vertices_z[ib],
            color_b,
            0.0f,
            0.0f,
            TRSPK_INVALID_TEXTURE_ID,
            bake);

        write_vertex(
            dest,
            (int)face_base_index + 2,
            (float)vertices_x[ic],
            (float)vertices_y[ic],
            (float)vertices_z[ic],
            color_c,
            0.0f,
            0.0f,
            TRSPK_INVALID_TEXTURE_ID,
            bake);
    }

    return true;
}
