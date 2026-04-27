#include "trspk_cache_model_loader16.h"

#include "../../include/ToriRSPlatformKit/trspk_math.h"

#include <stdlib.h>

static void
trspk_make_hidden_vertex(
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
trspk_make_vertex(
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

static int
trspk_batch16_add_model_impl(
    TRSPK_Batch16* batch,
    uint16_t model_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index,
    uint32_t vertex_count,
    const int16_t* vx,
    const int16_t* vy,
    const int16_t* vz,
    uint32_t face_count,
    const uint16_t* fa,
    const uint16_t* fb,
    const uint16_t* fc,
    const uint16_t* ca,
    const uint16_t* cb,
    const uint16_t* cc,
    const int16_t* faces_textures,
    const uint16_t* textured_faces,
    const uint16_t* tfa,
    const uint16_t* tfb,
    const uint16_t* tfc,
    const uint8_t* face_alphas,
    const int32_t* face_infos,
    TRSPK_UVMode uv_mode,
    const TRSPK_BakeTransform* bake,
    bool textured)
{
    if( !batch || !vx || !vy || !vz || !fa || !fb || !fc || !ca || !cb || !cc || face_count == 0u )
        return 0;

    const uint32_t max_faces_per_chunk = 65535u / 3u;
    uint32_t submitted = 0u;
    for( uint32_t start_face = 0; start_face < face_count; start_face += max_faces_per_chunk )
    {
        uint32_t slice_faces = face_count - start_face;
        if( slice_faces > max_faces_per_chunk )
            slice_faces = max_faces_per_chunk;
        const uint32_t vcount = slice_faces * 3u;
        TRSPK_Vertex* vertices = (TRSPK_Vertex*)calloc(vcount, sizeof(TRSPK_Vertex));
        uint16_t* indices = (uint16_t*)calloc(vcount, sizeof(uint16_t));
        if( !vertices || !indices )
        {
            free(vertices);
            free(indices);
            break;
        }

        for( uint32_t local_face = 0; local_face < slice_faces; ++local_face )
        {
            const uint32_t i = start_face + local_face;
            const uint32_t base = local_face * 3u;
            indices[base + 0u] = (uint16_t)(base + 0u);
            indices[base + 1u] = (uint16_t)(base + 1u);
            indices[base + 2u] = (uint16_t)(base + 2u);

            uint8_t alpha = 0xFFu;
            if( face_alphas )
                alpha = (uint8_t)(0xFFu - face_alphas[i]);
            const float alpha_float = (float)alpha / 255.0f;

            const bool skip = (face_infos && ((face_infos[i] & 3) == 2)) ||
                              (cc[i] == (uint16_t)TRSPK_HSL16_HIDDEN);
            const uint32_t ia = fa[i];
            const uint32_t ib = fb[i];
            const uint32_t ic = fc[i];
            const bool bad = ia >= vertex_count || ib >= vertex_count || ic >= vertex_count;
            if( skip || bad )
            {
                trspk_make_hidden_vertex(&vertices[base + 0u], alpha_float);
                trspk_make_hidden_vertex(&vertices[base + 1u], alpha_float);
                trspk_make_hidden_vertex(&vertices[base + 2u], alpha_float);
                continue;
            }

            float col_a[4], col_b[4], col_c[4];
            if( cc[i] == (uint16_t)TRSPK_HSL16_FLAT )
            {
                trspk_hsl16_to_rgba(ca[i], col_a, alpha_float);
                col_b[0] = col_c[0] = col_a[0];
                col_b[1] = col_c[1] = col_a[1];
                col_b[2] = col_c[2] = col_a[2];
                col_b[3] = col_c[3] = col_a[3];
            }
            else
            {
                trspk_hsl16_to_rgba(ca[i], col_a, alpha_float);
                trspk_hsl16_to_rgba(cb[i], col_b, alpha_float);
                trspk_hsl16_to_rgba(cc[i], col_c, alpha_float);
            }

            TRSPK_UVFaceCoords uv = { 0 };
            uint16_t tex_id = TRSPK_INVALID_TEXTURE_ID;
            if( textured )
            {
                uint32_t tp;
                uint32_t tm;
                uint32_t tn;
                if( uv_mode == TRSPK_UV_MODE_FIRST_FACE )
                {
                    tp = (uint32_t)fa[0];
                    tm = (uint32_t)fb[0];
                    tn = (uint32_t)fc[0];
                }
                else if( textured_faces && textured_faces[i] != TRSPK_INVALID_TEXTURE_ID )
                {
                    const uint16_t tf = textured_faces[i];
                    tp = (uint32_t)tfa[tf];
                    tm = (uint32_t)tfb[tf];
                    tn = (uint32_t)tfc[tf];
                }
                else
                {
                    tp = ia;
                    tm = ib;
                    tn = ic;
                }
                trspk_uv_pnm_compute(
                    &uv,
                    (float)vx[tp],
                    (float)vy[tp],
                    (float)vz[tp],
                    (float)vx[tm],
                    (float)vy[tm],
                    (float)vz[tm],
                    (float)vx[tn],
                    (float)vy[tn],
                    (float)vz[tn],
                    (float)vx[ia],
                    (float)vy[ia],
                    (float)vz[ia],
                    (float)vx[ib],
                    (float)vy[ib],
                    (float)vz[ib],
                    (float)vx[ic],
                    (float)vy[ic],
                    (float)vz[ic]);
                if( faces_textures && faces_textures[i] != -1 )
                    tex_id = (uint16_t)faces_textures[i];
            }

            trspk_make_vertex(
                &vertices[base + 0u],
                (float)vx[ia],
                (float)vy[ia],
                (float)vz[ia],
                col_a,
                uv.u1,
                uv.v1,
                tex_id,
                bake);
            trspk_make_vertex(
                &vertices[base + 1u],
                (float)vx[ib],
                (float)vy[ib],
                (float)vz[ib],
                col_b,
                uv.u2,
                uv.v2,
                tex_id,
                bake);
            trspk_make_vertex(
                &vertices[base + 2u],
                (float)vx[ic],
                (float)vy[ic],
                (float)vz[ic],
                col_c,
                uv.u3,
                uv.v3,
                tex_id,
                bake);
        }

        if( trspk_batch16_add_mesh(
                batch,
                vertices,
                vcount,
                indices,
                vcount,
                model_id,
                gpu_segment_slot,
                frame_index) >= 0 )
            submitted++;
        free(vertices);
        free(indices);
    }
    return (int)submitted;
}

int
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
    return trspk_batch16_add_model_impl(
        batch,
        model_id,
        gpu_segment_slot,
        frame_index,
        vertex_count,
        vertices_x,
        vertices_y,
        vertices_z,
        face_count,
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
        false);
}

int
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
    return trspk_batch16_add_model_impl(
        batch,
        model_id,
        gpu_segment_slot,
        frame_index,
        vertex_count,
        vertices_x,
        vertices_y,
        vertices_z,
        face_count,
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
        true);
}
