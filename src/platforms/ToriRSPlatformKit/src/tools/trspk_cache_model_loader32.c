#include "trspk_cache_model_loader32.h"

#include "trspk_vertex_buffer.h"

#include <stdlib.h>
#include <string.h>

bool
trspk_batch32_build_model_untextured(
    TRSPK_VertexBuffer* m,
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
    if( !m )
        return false;
    memset(m, 0, sizeof(*m));
    m->format = TRSPK_VERTEX_FORMAT_NONE;
    if( !vertices_x || !vertices_y || !vertices_z || !faces_a || !faces_b || !faces_c ||
        !faces_a_color_hsl16 || !faces_b_color_hsl16 || !faces_c_color_hsl16 || face_count == 0u )
        return false;
    return trspk_vertex_buffer_from_face_slice_u32_untextured(
        0u,
        face_count,
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
        face_alphas,
        face_infos,
        bake,
        m);
}

bool
trspk_batch32_build_model_textured(
    TRSPK_VertexBuffer* m,
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
    if( !m )
        return false;
    memset(m, 0, sizeof(*m));
    m->format = TRSPK_VERTEX_FORMAT_NONE;
    if( !vertices_x || !vertices_y || !vertices_z || !faces_a || !faces_b || !faces_c ||
        !faces_a_color_hsl16 || !faces_b_color_hsl16 || !faces_c_color_hsl16 || face_count == 0u )
        return false;
    return trspk_vertex_buffer_from_face_slice_u32_textured(
        0u,
        face_count,
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
        m);
}

static void
trspk_batch32_add_model_impl(
    TRSPK_Batch32* batch,
    uint16_t model_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index,
    bool textured,
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
    if( !batch || !vertices_x || !vertices_y || !vertices_z || !faces_a || !faces_b || !faces_c ||
        !faces_a_color_hsl16 || !faces_b_color_hsl16 || !faces_c_color_hsl16 || face_count == 0u )
        return;
    TRSPK_VertexBuffer mesh = { 0 };
    mesh.format = TRSPK_VERTEX_FORMAT_NONE;
    if( textured )
    {
        if( !trspk_batch32_build_model_textured(
                &mesh,
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
                bake) )
            return;
    }
    else
    {
        if( !trspk_batch32_build_model_untextured(
                &mesh,
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
                face_alphas,
                face_infos,
                bake) )
            return;
    }
    trspk_batch32_add_mesh(
        batch,
        mesh.vertices.as_trspk,
        mesh.vertex_count,
        mesh.indices,
        mesh.index_count,
        model_id,
        gpu_segment_slot,
        frame_index);
    trspk_vertex_buffer_free(&mesh);
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
    trspk_batch32_add_model_impl(
        batch,
        model_id,
        gpu_segment_slot,
        frame_index,
        false,
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
        bake);
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
    TRSPK_UVMode uv_mode,
    const TRSPK_BakeTransform* bake)
{
    trspk_batch32_add_model_impl(
        batch,
        model_id,
        gpu_segment_slot,
        frame_index,
        true,
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
        bake);
}
