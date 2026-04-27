#include "platforms/common/gpu_3d.h"
#include "platforms/opengl3/gpu_3d_cache2_opengl3.h"
#include "platforms/opengl3/events/opengl3_events.h"
#include "platforms/opengl3/opengl3_atlas_resources.h"
#include "platforms/opengl3/opengl3_ctx.h"
#include "platforms/opengl3/opengl3_renderer_core.h"
#include "tori_rs_render.h"

extern "C" {
#include "graphics/dash_model_internal.h"
}

void
opengl3_event_model_load(
    OpenGL3RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer )
        return;

    struct DashModel* model = cmd->_model_load.model;
    const int model_id = cmd->_model_load.model_id;
    if( !model || model_id <= 0 )
        return;

    if( dashmodel__is_ground_va(model) )
        return;
    if( !dashmodel_face_colors_a_const(model) || !dashmodel_face_colors_b_const(model) ||
        !dashmodel_face_colors_c_const(model) || !dashmodel_vertices_x_const(model) ||
        !dashmodel_vertices_y_const(model) || !dashmodel_vertices_z_const(model) ||
        !dashmodel_face_indices_a_const(model) || !dashmodel_face_indices_b_const(model) ||
        !dashmodel_face_indices_c_const(model) || dashmodel_face_count(model) <= 0 )
        return;

    GPU3DCache2Resource prev =
        ctx->renderer->model_cache2.TakeStandaloneRetainedBuffers((uint16_t)model_id);
    opengl3_delete_gl_buffer(prev.vbo);
    opengl3_delete_gl_buffer(prev.ebo);

    OpenGL3BatchBuffer solo{};
    const GPU3DBakeTransform identity_bake{};
    opengl3_event_cache2_batch_push_model_mesh(
        ctx,
        solo,
        model,
        model_id,
        kSceneBatchSlotNone,
        GPU_MODEL_ANIMATION_NONE_IDX,
        0u,
        identity_bake);

    BatchBuffersOpenGL3 v2bufs{};
    GPU3DCache2BatchSubmitOpenGL3(
        ctx->renderer->model_cache2,
        solo,
        v2bufs,
        kSceneBatchSlotNone,
        (ToriRS_UsageHint)cmd->_model_load.usage_hint);

    ctx->renderer->model_cache2.SetStandaloneRetainedBuffers(
        (uint16_t)model_id,
        (GPUResourceHandle)(uintptr_t)v2bufs.vbo,
        (GPUResourceHandle)(uintptr_t)v2bufs.ebo);
    v2bufs.vbo = 0u;
    v2bufs.ebo = 0u;
}
