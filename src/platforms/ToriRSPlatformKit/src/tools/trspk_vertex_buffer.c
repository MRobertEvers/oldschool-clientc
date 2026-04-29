#include "trspk_vertex_buffer.h"

#include "../../include/ToriRSPlatformKit/trspk_math.h"
#include "trspk_d3d8_fvf_bake.h"
#include "trspk_resource_cache.h"
#include "trspk_vertex_format.h"

#include <math.h>
#include <string.h>

static void
trspk_vertex_buffer_bake_array_to_interleaved_vertices(
    const TRSPK_VertexBuffer* src,
    const TRSPK_BakeTransform* bake,
    TRSPK_VertexBuffer* out,
    double d3d8_frame_clock);

void
trspk_vertex_buffer_apply_bake(
    TRSPK_VertexBuffer* vb,
    const TRSPK_BakeTransform* bake)
{
    if( !vb || !trspk_vertex_buffer_has_vertex_payload(vb) )
        return;
    const uint32_t n = vb->vertex_count;
    switch( vb->format )
    {
    case TRSPK_VERTEX_FORMAT_TRSPK:
        for( uint32_t i = 0; i < n; ++i )
        {
            float* p = vb->vertices.as_trspk[i].position;
            trspk_bake_transform_apply(bake, &p[0], &p[1], &p[2]);
        }
        break;
    case TRSPK_VERTEX_FORMAT_WEBGL1:
        for( uint32_t i = 0; i < n; ++i )
        {
            float* p = vb->vertices.as_webgl1[i].position;
            trspk_bake_transform_apply(bake, &p[0], &p[1], &p[2]);
        }
        break;
    case TRSPK_VERTEX_FORMAT_METAL:
        for( uint32_t i = 0; i < n; ++i )
        {
            float* p = vb->vertices.as_metal[i].position;
            trspk_bake_transform_apply(bake, &p[0], &p[1], &p[2]);
        }
        break;
    case TRSPK_VERTEX_FORMAT_D3D8:
        for( uint32_t i = 0; i < n; ++i )
        {
            float* px = &vb->vertices.as_d3d8[i].x;
            float* py = &vb->vertices.as_d3d8[i].y;
            float* pz = &vb->vertices.as_d3d8[i].z;
            trspk_bake_transform_apply(bake, px, py, pz);
        }
        break;
    default:
        break;
    }
}

bool
trspk_vertex_buffer_bake_array_to_interleaved(
    const TRSPK_VertexBuffer* src,
    const TRSPK_BakeTransform* bake,
    TRSPK_VertexBuffer* out,
    double d3d8_frame_clock)
{
    if( !src || !bake || !out )
        return false;
    trspk_vertex_buffer_free(out);
    memset(out, 0, sizeof(*out));
    if( !trspk_vertex_buffer_has_vertex_payload(src) )
        return false;
    TRSPK_VertexFormat dst_format;
    if( src->format == TRSPK_VERTEX_FORMAT_METAL_SOAOS )
        dst_format = TRSPK_VERTEX_FORMAT_METAL;
    else if( src->format == TRSPK_VERTEX_FORMAT_WEBGL1_SOAOS )
        dst_format = TRSPK_VERTEX_FORMAT_WEBGL1;
    else if( src->format == TRSPK_VERTEX_FORMAT_D3D8_SOAOS )
        dst_format = TRSPK_VERTEX_FORMAT_D3D8;
    else
        return false;
    if( src->index_format != TRSPK_INDEX_FORMAT_U32 || !src->indices.as_u32 )
        return false;

    if( !trspk_vertex_buffer_allocate_mesh(out, src->vertex_count, src->index_count, dst_format) )
        return false;
    out->index_base = src->index_base;
    memcpy(out->indices.as_u32, src->indices.as_u32, (size_t)src->index_count * sizeof(uint32_t));

    trspk_vertex_buffer_bake_array_to_interleaved_vertices(src, bake, out, d3d8_frame_clock);
    return true;
}

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
    float tex_id_vertex,
    float packed_gpu_uv_mode,
    const TRSPK_BakeTransform* bake,
    double d3d8_frame_clock,
    int d3d8_diffuse_legacy_win32,
    uint16_t d3d8_diffuse_hsl16)
{
    float px = x;
    float py = y;
    float pz = z;
    trspk_bake_transform_apply(bake, &px, &py, &pz);
    const uint16_t tex_u16 = (uint16_t)fmaxf(0.0f, fminf(65535.0f, floorf(tex_id_vertex + 0.5f)));
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
        dest->vertices.as_trspk[offset].tex_id = tex_id_vertex;
        dest->vertices.as_trspk[offset].uv_mode = packed_gpu_uv_mode;
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
        dest->vertices.as_webgl1[offset].tex_id = tex_id_vertex;
        dest->vertices.as_webgl1[offset].uv_mode = packed_gpu_uv_mode;
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
        dest->vertices.as_metal[offset].tex_id = tex_u16;
        dest->vertices.as_metal[offset].uv_mode =
            (uint16_t)fmaxf(0.0f, fminf(65535.0f, floorf(packed_gpu_uv_mode + 0.5f)));
        break;
    case TRSPK_VERTEX_FORMAT_D3D8:
        trspk_d3d8_fvf_from_model_vertex(
            px,
            py,
            pz,
            color,
            u,
            vv,
            tex_id_vertex,
            packed_gpu_uv_mode,
            d3d8_frame_clock,
            &dest->vertices.as_d3d8[offset]);
        if( d3d8_diffuse_legacy_win32 )
            dest->vertices.as_d3d8[offset].diffuse =
                trspk_d3d8_pack_diffuse_legacy_win32(d3d8_diffuse_hsl16, color[3]);
        break;
    case TRSPK_VERTEX_FORMAT_WEBGL1_SOAOS:
    {
        const uint32_t o = (uint32_t)offset;
        TRSPK_VertexWebGL1SoaosBlock* blk =
            &dest->vertices.as_webgl1_soaos.blocks[TRSPK_VERTEX_SOAOS_BLOCK_IDX(o)];
        const uint32_t lane = TRSPK_VERTEX_SOAOS_LANE_IDX(o);
        blk->position_x[lane] = px;
        blk->position_y[lane] = py;
        blk->position_z[lane] = pz;
        blk->position_w[lane] = 1.0f;
        blk->color_r[lane] = color[0];
        blk->color_g[lane] = color[1];
        blk->color_b[lane] = color[2];
        blk->color_a[lane] = color[3];
        blk->texcoord_u[lane] = u;
        blk->texcoord_v[lane] = vv;
        blk->tex_id[lane] = tex_id_vertex;
        blk->uv_mode[lane] = packed_gpu_uv_mode;
        break;
    }
    case TRSPK_VERTEX_FORMAT_D3D8_SOAOS:
    {
        const uint32_t o = (uint32_t)offset;
        TRSPK_VertexD3D8SoaosBlock* blk =
            &dest->vertices.as_d3d8_soaos.blocks[TRSPK_VERTEX_SOAOS_BLOCK_IDX(o)];
        const uint32_t lane = TRSPK_VERTEX_SOAOS_LANE_IDX(o);
        blk->position_x[lane] = px;
        blk->position_y[lane] = py;
        blk->position_z[lane] = pz;
        blk->position_w[lane] = 1.0f;
        blk->color_r[lane] = color[0];
        blk->color_g[lane] = color[1];
        blk->color_b[lane] = color[2];
        blk->color_a[lane] = color[3];
        blk->texcoord_u[lane] = u;
        blk->texcoord_v[lane] = vv;
        blk->tex_id[lane] = tex_id_vertex;
        blk->uv_mode[lane] = packed_gpu_uv_mode;
        (void)d3d8_frame_clock;
        (void)d3d8_diffuse_legacy_win32;
        (void)d3d8_diffuse_hsl16;
        break;
    }
    case TRSPK_VERTEX_FORMAT_METAL_SOAOS:
    {
        const uint32_t o = (uint32_t)offset;
        TRSPK_VertexMetalSoaosBlock* blk =
            &dest->vertices.as_metal_soaos.blocks[TRSPK_VERTEX_SOAOS_BLOCK_IDX(o)];
        const uint32_t lane = TRSPK_VERTEX_SOAOS_LANE_IDX(o);
        blk->position_x[lane] = px;
        blk->position_y[lane] = py;
        blk->position_z[lane] = pz;
        blk->position_w[lane] = 1.0f;
        blk->color_r[lane] = color[0];
        blk->color_g[lane] = color[1];
        blk->color_b[lane] = color[2];
        blk->color_a[lane] = color[3];
        blk->texcoord_u[lane] = u;
        blk->texcoord_v[lane] = vv;
        blk->tex_id[lane] = tex_u16;
        blk->uv_mode[lane] =
            (uint16_t)fmaxf(0.0f, fminf(65535.0f, floorf(packed_gpu_uv_mode + 0.5f)));
        break;
    }
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
    const float pack = trspk_pack_gpu_uv_mode_float(0.0f, 0.0f);
    write_vertex(
        dest,
        offset,
        0.0f,
        0.0f,
        0.0f,
        color,
        0.0f,
        0.0f,
        (float)TRSPK_INVALID_TEXTURE_ID,
        pack,
        NULL,
        0.0,
        0,
        0u);
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
    TRSPK_UVCalculationMode uv_calc_mode,
    const TRSPK_ResourceCache* atlas_tile_meta,
    const TRSPK_BakeTransform* bake,
    TRSPK_VertexBuffer* dest,
    double d3d8_frame_clock)
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

        uint16_t hsl_a = faces_a_color_hsl16[i];
        uint16_t hsl_b = faces_b_color_hsl16[i];
        uint16_t hsl_c = faces_c_color_hsl16[i];
        if( faces_c_color_hsl16[i] == (uint16_t)TRSPK_HSL16_FLAT )
            hsl_b = hsl_c = hsl_a;

        TRSPK_UVFaceCoords uv = { 0 };
        uint16_t tex_id = TRSPK_INVALID_TEXTURE_ID;
        uint32_t tp;
        uint32_t tm;
        uint32_t tn;
        if( uv_calc_mode == TRSPK_UV_CALC_FIRST_FACE )
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

        float tex_vertex = (float)tex_id;
        float packed_gpu = trspk_pack_gpu_uv_mode_float(0.0f, 0.0f);
        if( atlas_tile_meta && tex_id != TRSPK_INVALID_TEXTURE_ID &&
            (uint32_t)tex_id < TRSPK_MAX_TEXTURES )
        {
            const TRSPK_AtlasTile* at =
                trspk_resource_cache_get_atlas_tile(atlas_tile_meta, (TRSPK_TextureId)tex_id);
            if( at )
            {
                packed_gpu = trspk_pack_gpu_uv_mode_float(at->anim_u, at->anim_v);
                if( at->opaque < 0.5f )
                    tex_vertex = (float)tex_id + 256.0f;
            }
        }

        write_vertex(
            dest,
            (int)face_base_index + 0,
            (float)vertices_x[ia],
            (float)vertices_y[ia],
            (float)vertices_z[ia],
            color_a,
            uv.u1,
            uv.v1,
            tex_vertex,
            packed_gpu,
            bake,
            d3d8_frame_clock,
            1,
            hsl_a);
        write_vertex(
            dest,
            (int)face_base_index + 1,
            (float)vertices_x[ib],
            (float)vertices_y[ib],
            (float)vertices_z[ib],
            color_b,
            uv.u2,
            uv.v2,
            tex_vertex,
            packed_gpu,
            bake,
            d3d8_frame_clock,
            1,
            hsl_b);
        write_vertex(
            dest,
            (int)face_base_index + 2,
            (float)vertices_x[ic],
            (float)vertices_y[ic],
            (float)vertices_z[ic],
            color_c,
            uv.u3,
            uv.v3,
            tex_vertex,
            packed_gpu,
            bake,
            d3d8_frame_clock,
            1,
            hsl_c);
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
    TRSPK_VertexBuffer* dest,
    double d3d8_frame_clock)
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

        uint16_t hsl_a = faces_a_color_hsl16[i];
        uint16_t hsl_b = faces_b_color_hsl16[i];
        uint16_t hsl_c = faces_c_color_hsl16[i];
        if( faces_c_color_hsl16[i] == (uint16_t)TRSPK_HSL16_FLAT )
            hsl_b = hsl_c = hsl_a;

        const float untextured_pack = trspk_pack_gpu_uv_mode_float(0.0f, 0.0f);
        write_vertex(
            dest,
            (int)face_base_index + 0,
            (float)vertices_x[ia],
            (float)vertices_y[ia],
            (float)vertices_z[ia],
            color_a,
            0.0f,
            0.0f,
            (float)TRSPK_INVALID_TEXTURE_ID,
            untextured_pack,
            bake,
            d3d8_frame_clock,
            1,
            hsl_a);

        write_vertex(
            dest,
            (int)face_base_index + 1,
            (float)vertices_x[ib],
            (float)vertices_y[ib],
            (float)vertices_z[ib],
            color_b,
            0.0f,
            0.0f,
            (float)TRSPK_INVALID_TEXTURE_ID,
            untextured_pack,
            bake,
            d3d8_frame_clock,
            1,
            hsl_b);

        write_vertex(
            dest,
            (int)face_base_index + 2,
            (float)vertices_x[ic],
            (float)vertices_y[ic],
            (float)vertices_z[ic],
            color_c,
            0.0f,
            0.0f,
            (float)TRSPK_INVALID_TEXTURE_ID,
            untextured_pack,
            bake,
            d3d8_frame_clock,
            1,
            hsl_c);
    }

    return true;
}

#include "trspk_vertex_buffer_simd.u.c"
