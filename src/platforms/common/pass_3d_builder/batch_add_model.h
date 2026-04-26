#ifndef BATCH_ADD_MODEL_H
#define BATCH_ADD_MODEL_H

#include "graphics/dash.h"
#include "graphics/uv_pnm.h"
#include "platforms/common/gpu_3d.h"
#include "platforms/common/pass_3d_builder/gpu_3d_cache2.h"

#include <cassert>
#include <cstdint>

/** Expand Dash model faces into `BatchBuffer::{vbo,ebo,tracking}`; `pose_index` from
 *  `GPU3DCache2::AllocatePoseSlot`. */
template<typename BatchBuffer>
inline void
BatchAddModeli16(
    BatchBuffer& buf,
    uint32_t pose_index,
    uint16_t model_id,
    uint32_t vertex_count,
    int16_t* vertices_x,
    int16_t* vertices_y,
    int16_t* vertices_z,
    uint32_t faces_count,
    uint16_t* faces_a,
    uint16_t* faces_b,
    uint16_t* faces_c,
    uint16_t* faces_a_color_hsl16,
    uint16_t* faces_b_color_hsl16,
    uint16_t* faces_c_color_hsl16,
    uint8_t* face_alphas,
    const int* face_infos_nullable = nullptr,
    const GPU3DBakeTransform& bake = GPU3DBakeTransform{})
{
    using VertexT = typename BatchBuffer::vertex_type;
    using IndexT = typename BatchBuffer::index_type;
    (void)vertex_count;
    assert(faces_count < 4096);

    const uint32_t vbo_vertex_start = (uint32_t)buf.vbo.size();
    const uint32_t ebo_start = (uint32_t)buf.ebo.size();
    const uint32_t new_vertex_count = faces_count * 3u;

    for( uint32_t i = 0; i < faces_count; i++ )
    {
        const uint32_t vo = (uint32_t)buf.vbo.size();
        buf.ebo.push_back(static_cast<IndexT>(vo));
        buf.ebo.push_back(static_cast<IndexT>(vo + 1u));
        buf.ebo.push_back(static_cast<IndexT>(vo + 2u));

        uint8_t alpha = 0xFF;
        if( face_alphas )
            alpha = 0xFF - face_alphas[i];

        float alpha_float = alpha / 255.0f;

        const bool skip_face = (face_infos_nullable && ((face_infos_nullable[(int)i] & 3) == 2)) ||
                               (faces_c_color_hsl16[i] == (uint16_t)DASHHSL16_HIDDEN);

        const int face_a = (int)faces_a[i];
        const int face_b = (int)faces_b[i];
        const int face_c = (int)faces_c[i];
        const bool bad_idx = face_a < 0 || face_b < 0 || face_c < 0 ||
                             face_a >= (int)vertex_count || face_b >= (int)vertex_count ||
                             face_c >= (int)vertex_count;

        if( skip_face || bad_idx )
        {
            for( int k = 0; k < 3; k++ )
            {
                CommonVertex v{};
                v.position[0] = 0.0f;
                v.position[1] = 0.0f;
                v.position[2] = 0.0f;
                v.position[3] = 1.0f;
                v.color[0] = v.color[1] = v.color[2] = 0.0f;
                v.color[3] = alpha_float;
                v.texcoord[0] = v.texcoord[1] = 0.0f;
                v.tex_id = 0xFFFFu;
                v.uv_mode = 0u;
                buf.vbo.push_back(VertexT::FromCommon(v));
            }
            continue;
        }

        const uint16_t hsl_a = faces_a_color_hsl16[i];
        const uint16_t hsl_b = faces_b_color_hsl16[i];
        const uint16_t hsl_c = faces_c_color_hsl16[i];
        float color_a[4], color_b[4], color_c[4];
        if( hsl_c == (uint16_t)DASHHSL16_FLAT )
        {
            gpu3d_hsl16_to_float_rgba(hsl_a, color_a, alpha_float);
            color_b[0] = color_a[0];
            color_b[1] = color_a[1];
            color_b[2] = color_a[2];
            color_b[3] = color_a[3];
            color_c[0] = color_a[0];
            color_c[1] = color_a[1];
            color_c[2] = color_a[2];
            color_c[3] = color_a[3];
        }
        else
        {
            gpu3d_hsl16_to_float_rgba(hsl_a, color_a, alpha_float);
            gpu3d_hsl16_to_float_rgba(hsl_b, color_b, alpha_float);
            gpu3d_hsl16_to_float_rgba(hsl_c, color_c, alpha_float);
        }

        CommonVertex va{};
        va.position[0] = (float)vertices_x[face_a];
        va.position[1] = (float)vertices_y[face_a];
        va.position[2] = (float)vertices_z[face_a];
        va.position[3] = 1.0f;
        va.color[0] = color_a[0];
        va.color[1] = color_a[1];
        va.color[2] = color_a[2];
        va.color[3] = color_a[3];
        va.texcoord[0] = 0.0f;
        va.texcoord[1] = 0.0f;
        va.tex_id = 0xFFFFu;
        va.uv_mode = 0u;
        GPU3DBakeTransformApplyToPosition(bake, va.position[0], va.position[1], va.position[2]);
        buf.vbo.push_back(VertexT::FromCommon(va));

        CommonVertex vb{};
        vb.position[0] = (float)vertices_x[face_b];
        vb.position[1] = (float)vertices_y[face_b];
        vb.position[2] = (float)vertices_z[face_b];
        vb.position[3] = 1.0f;
        vb.color[0] = color_b[0];
        vb.color[1] = color_b[1];
        vb.color[2] = color_b[2];
        vb.color[3] = color_b[3];
        vb.texcoord[0] = 0.0f;
        vb.texcoord[1] = 0.0f;
        vb.tex_id = 0xFFFFu;
        vb.uv_mode = 0u;
        GPU3DBakeTransformApplyToPosition(bake, vb.position[0], vb.position[1], vb.position[2]);
        buf.vbo.push_back(VertexT::FromCommon(vb));

        CommonVertex vc{};
        vc.position[0] = (float)vertices_x[face_c];
        vc.position[1] = (float)vertices_y[face_c];
        vc.position[2] = (float)vertices_z[face_c];
        vc.position[3] = 1.0f;
        vc.color[0] = color_c[0];
        vc.color[1] = color_c[1];
        vc.color[2] = color_c[2];
        vc.color[3] = color_c[3];
        vc.texcoord[0] = 0.0f;
        vc.texcoord[1] = 0.0f;
        vc.tex_id = 0xFFFFu;
        vc.uv_mode = 0u;
        GPU3DBakeTransformApplyToPosition(bake, vc.position[0], vc.position[1], vc.position[2]);

        buf.vbo.push_back(VertexT::FromCommon(vc));
    }

    buf.tracking.push_back(
        BatchedQueueModel::CreateModel(
            model_id, pose_index, new_vertex_count, vbo_vertex_start, faces_count, ebo_start));
}

template<typename BatchBuffer>
inline void
BatchAddModelTexturedi16(
    BatchBuffer& buf,
    uint32_t pose_index,
    uint16_t model_id,
    uint32_t vertex_count,
    int16_t* vertices_x,
    int16_t* vertices_y,
    int16_t* vertices_z,
    uint32_t faces_count,
    uint16_t* faces_a,
    uint16_t* faces_b,
    uint16_t* faces_c,
    uint16_t* faces_a_color_hsl16,
    uint16_t* faces_b_color_hsl16,
    uint16_t* faces_c_color_hsl16,
    uint8_t* face_alphas,
    int16_t* faces_textures,
    uint16_t* textured_faces,
    uint16_t* textured_faces_a,
    uint16_t* textured_faces_b,
    uint16_t* textured_faces_c,
    const int* face_infos_nullable,
    GPU3DCache2UVCalculationMode uv_calculation_mode =
        GPU3DCache2UVCalculationMode::TexturedFaceArray,
    const GPU3DBakeTransform& bake = GPU3DBakeTransform{})
{
    using VertexT = typename BatchBuffer::vertex_type;
    using IndexT = typename BatchBuffer::index_type;
    assert(faces_count < 4096);

    const uint32_t vbo_vertex_start = (uint32_t)buf.vbo.size();
    const uint32_t ebo_start = (uint32_t)buf.ebo.size();
    const uint32_t new_vertex_count = faces_count * 3u;

    uint32_t tp_vertex = 0;
    uint32_t tm_vertex = 0;
    uint32_t tn_vertex = 0;
    struct UVFaceCoords uv;

    for( uint32_t i = 0; i < faces_count; i++ )
    {
        const uint32_t vbo_offset = (uint32_t)buf.vbo.size();
        buf.ebo.push_back(static_cast<IndexT>(vbo_offset));
        buf.ebo.push_back(static_cast<IndexT>(vbo_offset + 1u));
        buf.ebo.push_back(static_cast<IndexT>(vbo_offset + 2u));

        uint8_t alpha = 0xFF;
        if( face_alphas )
            alpha = 0xFF - face_alphas[i];
        float alpha_float = alpha / 255.0f;

        const bool skip_face = (face_infos_nullable && ((face_infos_nullable[(int)i] & 3) == 2)) ||
                               (faces_c_color_hsl16[i] == (uint16_t)DASHHSL16_HIDDEN);

        const int face_a = (int)faces_a[i];
        const int face_b = (int)faces_b[i];
        const int face_c = (int)faces_c[i];
        const bool bad_idx = face_a < 0 || face_b < 0 || face_c < 0 ||
                             face_a >= (int)vertex_count || face_b >= (int)vertex_count ||
                             face_c >= (int)vertex_count;

        if( skip_face || bad_idx )
        {
            for( int k = 0; k < 3; k++ )
            {
                CommonVertex v{};
                v.position[0] = v.position[1] = v.position[2] = 0.0f;
                v.position[3] = 1.0f;
                v.color[0] = v.color[1] = v.color[2] = 0.0f;
                v.color[3] = alpha_float;
                v.texcoord[0] = v.texcoord[1] = 0.0f;
                v.tex_id = 0xFFFFu;
                v.uv_mode = 0u;
                buf.vbo.push_back(VertexT::FromCommon(v));
            }
            continue;
        }

        if( uv_calculation_mode == GPU3DCache2UVCalculationMode::FirstFace )
        {
            tp_vertex = faces_a[0];
            tm_vertex = faces_b[0];
            tn_vertex = faces_c[0];
        }
        else if( textured_faces && textured_faces[i] != 0xFFFF )
        {
            tp_vertex = textured_faces_a[textured_faces[i]];
            tm_vertex = textured_faces_b[textured_faces[i]];
            tn_vertex = textured_faces_c[textured_faces[i]];
        }
        else
        {
            tp_vertex = faces_a[i];
            tm_vertex = faces_b[i];
            tn_vertex = faces_c[i];
        }

        uv_pnm_compute(
            &uv,
            (float)vertices_x[tp_vertex],
            (float)vertices_y[tp_vertex],
            (float)vertices_z[tp_vertex],
            (float)vertices_x[tm_vertex],
            (float)vertices_y[tm_vertex],
            (float)vertices_z[tm_vertex],
            (float)vertices_x[tn_vertex],
            (float)vertices_y[tn_vertex],
            (float)vertices_z[tn_vertex],
            (float)vertices_x[face_a],
            (float)vertices_y[face_a],
            (float)vertices_z[face_a],
            (float)vertices_x[face_b],
            (float)vertices_y[face_b],
            (float)vertices_z[face_b],
            (float)vertices_x[face_c],
            (float)vertices_y[face_c],
            (float)vertices_z[face_c]);

        uint16_t tex_id;
        if( faces_textures[i] != -1 )
            tex_id = (uint16_t)faces_textures[i];
        else
            tex_id = 0xFFFFu;

        const uint16_t hsl_a = faces_a_color_hsl16[i];
        const uint16_t hsl_b = faces_b_color_hsl16[i];
        const uint16_t hsl_c = faces_c_color_hsl16[i];
        float color_a[4], color_b[4], color_c[4];
        if( hsl_c == (uint16_t)DASHHSL16_FLAT )
        {
            gpu3d_hsl16_to_float_rgba(hsl_a, color_a, alpha_float);
            color_b[0] = color_a[0];
            color_b[1] = color_a[1];
            color_b[2] = color_a[2];
            color_b[3] = color_a[3];
            color_c[0] = color_a[0];
            color_c[1] = color_a[1];
            color_c[2] = color_a[2];
            color_c[3] = color_a[3];
        }
        else
        {
            gpu3d_hsl16_to_float_rgba(hsl_a, color_a, alpha_float);
            gpu3d_hsl16_to_float_rgba(hsl_b, color_b, alpha_float);
            gpu3d_hsl16_to_float_rgba(hsl_c, color_c, alpha_float);
        }

        CommonVertex va{};
        va.position[0] = (float)vertices_x[face_a];
        va.position[1] = (float)vertices_y[face_a];
        va.position[2] = (float)vertices_z[face_a];
        va.position[3] = 1.0f;
        va.color[0] = color_a[0];
        va.color[1] = color_a[1];
        va.color[2] = color_a[2];
        va.color[3] = color_a[3];
        va.texcoord[0] = uv.u1;
        va.texcoord[1] = uv.v1;
        va.tex_id = tex_id;
        va.uv_mode = 0u;
        GPU3DBakeTransformApplyToPosition(bake, va.position[0], va.position[1], va.position[2]);
        buf.vbo.push_back(VertexT::FromCommon(va));

        CommonVertex vb{};
        vb.position[0] = (float)vertices_x[face_b];
        vb.position[1] = (float)vertices_y[face_b];
        vb.position[2] = (float)vertices_z[face_b];
        vb.position[3] = 1.0f;
        vb.color[0] = color_b[0];
        vb.color[1] = color_b[1];
        vb.color[2] = color_b[2];
        vb.color[3] = color_b[3];
        vb.texcoord[0] = uv.u2;
        vb.texcoord[1] = uv.v2;
        vb.tex_id = tex_id;
        vb.uv_mode = 0u;
        GPU3DBakeTransformApplyToPosition(bake, vb.position[0], vb.position[1], vb.position[2]);
        buf.vbo.push_back(VertexT::FromCommon(vb));

        CommonVertex vc{};
        vc.position[0] = (float)vertices_x[face_c];
        vc.position[1] = (float)vertices_y[face_c];
        vc.position[2] = (float)vertices_z[face_c];
        vc.position[3] = 1.0f;
        vc.color[0] = color_c[0];
        vc.color[1] = color_c[1];
        vc.color[2] = color_c[2];
        vc.color[3] = color_c[3];
        vc.texcoord[0] = uv.u3;
        vc.texcoord[1] = uv.v3;
        vc.tex_id = tex_id;
        vc.uv_mode = 0u;
        GPU3DBakeTransformApplyToPosition(bake, vc.position[0], vc.position[1], vc.position[2]);
        buf.vbo.push_back(VertexT::FromCommon(vc));
    }

    buf.tracking.push_back(
        BatchedQueueModel::CreateModel(
            model_id, pose_index, new_vertex_count, vbo_vertex_start, faces_count, ebo_start));
}

#endif
