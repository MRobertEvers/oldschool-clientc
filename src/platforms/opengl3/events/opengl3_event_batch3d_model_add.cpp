#include "platforms/common/pass_3d_builder/batch_add_model.h"
#include "platforms/common/pass_3d_builder/gpu_3d_cache2_opengl3.h"
#include "platforms/opengl3/events/opengl3_events.h"
#include "platforms/opengl3/opengl3_ctx.h"
#include "platforms/opengl3/opengl3_renderer_core.h"
#include "tori_rs_render.h"

extern "C" {
#include "graphics/dash.h"
#include "graphics/dash_model_internal.h"
}

void
opengl3_event_cache2_batch_push_model_mesh(
    OpenGL3RenderCtx* ctx,
    OpenGL3BatchBuffer& buf,
    struct DashModel* model,
    int model_id,
    SceneBatchId scene_batch_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index,
    const GPU3DBakeTransform& bake)
{
    if( !ctx || !ctx->renderer || !model || model_id <= 0 )
        return;

    if( !dashmodel_face_colors_a_const(model) || !dashmodel_face_colors_b_const(model) ||
        !dashmodel_face_colors_c_const(model) || !dashmodel_vertices_x_const(model) ||
        !dashmodel_vertices_y_const(model) || !dashmodel_vertices_z_const(model) ||
        !dashmodel_face_indices_a_const(model) || !dashmodel_face_indices_b_const(model) ||
        !dashmodel_face_indices_c_const(model) || dashmodel_face_count(model) <= 0 )
        return;

    const vertexint_t* vertices_x = dashmodel_vertices_x_const(model);
    const vertexint_t* vertices_y = dashmodel_vertices_y_const(model);
    const vertexint_t* vertices_z = dashmodel_vertices_z_const(model);

    const hsl16_t* face_colors_a = dashmodel_face_colors_a_const(model);
    const hsl16_t* face_colors_b = dashmodel_face_colors_b_const(model);
    const hsl16_t* face_colors_c = dashmodel_face_colors_c_const(model);
    const uint8_t* face_alphas = dashmodel_face_alphas_const(model);

    const faceint_t* face_indices_a = dashmodel_face_indices_a_const(model);
    const faceint_t* face_indices_b = dashmodel_face_indices_b_const(model);
    const faceint_t* face_indices_c = dashmodel_face_indices_c_const(model);

    const int face_count = dashmodel_face_count(model);
    const int vertex_count = dashmodel_vertex_count(model);
    const int* face_infos = dashmodel_face_infos_const(model);

    GPU3DCache2<GPU3DMeshVertexWebGL1>& cache = ctx->renderer->model_cache2;

    if( scene_batch_id < kGPU3DCache2MaxSceneBatches )
        cache.SceneBatchAddModel(scene_batch_id, (uint16_t)model_id);

    const uint32_t pose_index =
        cache.AllocatePoseSlot((uint16_t)model_id, gpu_segment_slot, frame_index);

    if( dashmodel_has_textures(model) )
    {
        const faceint_t* faces_textures = dashmodel_face_textures_const(model);
        const faceint_t* textured_faces = dashmodel_face_texture_coords_const(model);
        const faceint_t* textured_faces_a = dashmodel_textured_p_coordinate_const(model);
        const faceint_t* textured_faces_b = dashmodel_textured_m_coordinate_const(model);
        const faceint_t* textured_faces_c = dashmodel_textured_n_coordinate_const(model);

        GPU3DCache2UVCalculationMode uv_calculation_mode;
        if( dashmodel__is_ground_va(model) )
            uv_calculation_mode = GPU3DCache2UVCalculationMode::FirstFace;
        else
            uv_calculation_mode = GPU3DCache2UVCalculationMode::TexturedFaceArray;

        BatchAddModelTexturedi16(
            buf,
            pose_index,
            (uint16_t)model_id,
            (uint32_t)vertex_count,
            const_cast<vertexint_t*>(vertices_x),
            const_cast<vertexint_t*>(vertices_y),
            const_cast<vertexint_t*>(vertices_z),
            (uint32_t)face_count,
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(face_indices_a)),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(face_indices_b)),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(face_indices_c)),
            const_cast<uint16_t*>(face_colors_a),
            const_cast<uint16_t*>(face_colors_b),
            const_cast<uint16_t*>(face_colors_c),
            const_cast<uint8_t*>(face_alphas),
            const_cast<faceint_t*>(faces_textures),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(textured_faces)),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(textured_faces_a)),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(textured_faces_b)),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(textured_faces_c)),
            face_infos,
            uv_calculation_mode,
            bake);
    }
    else
    {
        BatchAddModeli16(
            buf,
            pose_index,
            (uint16_t)model_id,
            (uint32_t)vertex_count,
            const_cast<vertexint_t*>(vertices_x),
            const_cast<vertexint_t*>(vertices_y),
            const_cast<vertexint_t*>(vertices_z),
            (uint32_t)face_count,
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(face_indices_a)),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(face_indices_b)),
            reinterpret_cast<uint16_t*>(const_cast<faceint_t*>(face_indices_c)),
            const_cast<uint16_t*>(face_colors_a),
            const_cast<uint16_t*>(face_colors_b),
            const_cast<uint16_t*>(face_colors_c),
            const_cast<uint8_t*>(face_alphas),
            face_infos,
            bake);
    }
}

void
opengl3_event_batch3d_model_add(
    OpenGL3RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    struct DashModel* model = cmd->_model_load.model;
    const int model_id = cmd->_model_load.model_id;
    if( !ctx || !ctx->renderer )
        return;
    const uint32_t bid = ctx->renderer->pass.current_model_batch_id;
    if( !model || model_id <= 0 || !ctx->renderer->pass.current_model_batch_active )
        return;

    GPU3DBakeTransform bake = GPU3DBakeTransform::FromYawTranslate(
        cmd->_model_load.world_x,
        cmd->_model_load.world_y,
        cmd->_model_load.world_z,
        cmd->_model_load.world_yaw_r2pi2048);
    ctx->renderer->model_cache2.SetModelBakeTransform((uint16_t)model_id, bake);
    opengl3_event_cache2_batch_push_model_mesh(
        ctx,
        ctx->renderer->batch3d_staging,
        model,
        model_id,
        bid,
        GPU_MODEL_ANIMATION_NONE_IDX,
        0u,
        bake);
}
