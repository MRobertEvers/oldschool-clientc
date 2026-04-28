#include "trspk_cache_model_loader16.h"

#include "trspk_vertex_buffer.h"

#include <stdlib.h>
#include <string.h>

bool
trspk_batch16_build_model_slice_untextured(
    TRSPK_VertexBuffer* m,
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
    const TRSPK_BakeTransform* bake)
{
    if( !m )
        return false;
    memset(m, 0, sizeof(*m));
    m->format = TRSPK_VERTEX_FORMAT_NONE;
    if( !vertices_x || !vertices_y || !vertices_z || !faces_a || !faces_b || !faces_c ||
        !faces_a_color_hsl16 || !faces_b_color_hsl16 || !faces_c_color_hsl16 || slice_face_count == 0u )
        return false;
    return trspk_vertex_buffer_from_face_slice_u16_untextured(
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
        face_alphas,
        face_infos,
        bake,
        m);
}

bool
trspk_batch16_build_model_slice_textured(
    TRSPK_VertexBuffer* m,
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
    const TRSPK_BakeTransform* bake)
{
    if( !m )
        return false;
    memset(m, 0, sizeof(*m));
    m->format = TRSPK_VERTEX_FORMAT_NONE;
    if( !vertices_x || !vertices_y || !vertices_z || !faces_a || !faces_b || !faces_c ||
        !faces_a_color_hsl16 || !faces_b_color_hsl16 || !faces_c_color_hsl16 || slice_face_count == 0u )
        return false;
    return trspk_vertex_buffer_from_face_slice_u16_textured(
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
        m);
}

static int
trspk_batch16_add_model_impl(
    TRSPK_Batch16* batch,
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
        return 0;

    const uint32_t max_faces_per_chunk = 65535u / 3u;
    uint32_t submitted = 0u;
    for( uint32_t start_face = 0; start_face < face_count; start_face += max_faces_per_chunk )
    {
        uint32_t slice_faces = face_count - start_face;
        if( slice_faces > max_faces_per_chunk )
            slice_faces = max_faces_per_chunk;
        TRSPK_VertexBuffer mesh = { 0 };
        mesh.format = TRSPK_VERTEX_FORMAT_NONE;
        bool ok = false;
        if( textured )
            ok = trspk_batch16_build_model_slice_textured(
                &mesh,
                start_face,
                slice_faces,
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
                bake);
        else
            ok = trspk_batch16_build_model_slice_untextured(
                &mesh,
                start_face,
                slice_faces,
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
                bake);
        if( !ok )
            break;
        const uint32_t ic = mesh.index_count;
        uint16_t* i16 = (uint16_t*)malloc((size_t)ic * sizeof(uint16_t));
        if( !i16 )
        {
            trspk_vertex_buffer_free(&mesh);
            break;
        }
        for( uint32_t j = 0; j < ic; ++j )
            i16[j] = (uint16_t)mesh.indices[j];
        if( trspk_batch16_add_mesh(
                batch,
                mesh.vertices.as_trspk,
                mesh.vertex_count,
                i16,
                ic,
                model_id,
                gpu_segment_slot,
                frame_index) >= 0 )
            submitted++;
        free(i16);
        trspk_vertex_buffer_free(&mesh);
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
