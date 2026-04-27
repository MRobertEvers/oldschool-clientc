#include "platforms/opengl3/events/opengl3_events.h"
#include "platforms/opengl3/opengl3_ctx.h"
#include "platforms/opengl3/opengl3_renderer_core.h"
#include "tori_rs_render.h"

extern "C" {
#include "graphics/dash_model_internal.h"
}

void
opengl3_frame_event_model_animation_load(
    OpenGL3RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !cmd )
        return;
    const int mid = cmd->_animation_load.model_gpu_id;
    const int fidx = cmd->_animation_load.frame_index;
    if( !cmd->_animation_load.model || mid <= 0 )
        return;

    if( !ctx->renderer->pass.current_model_batch_active )
        return;
    const uint32_t bid = ctx->renderer->pass.current_model_batch_id;

    struct DashModel* model = cmd->_animation_load.model;
    dashmodel_animate(model, cmd->_animation_load.frame, cmd->_animation_load.framemap);
    const uint8_t gpu_seg = cmd->_animation_load.animation_index == 1
                                ? GPU_MODEL_ANIMATION_SECONDARY_IDX
                                : GPU_MODEL_ANIMATION_PRIMARY_IDX;
    const GPU3DBakeTransform abake =
        ctx->renderer->model_cache2.GetModelBakeTransform((uint16_t)mid);
    opengl3_cache2_batch_push_model_mesh(
        ctx, ctx->renderer->batch3d_staging, model, mid, bid, gpu_seg, (uint16_t)fidx, abake);
    (void)cmd->_animation_load.usage_hint;
}
