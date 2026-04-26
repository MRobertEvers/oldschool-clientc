// System ObjC/Metal headers must come before any game headers.
#include "platforms/metal/events/metal_events.h"
#include "platforms/metal/metal_internal.h"

void
metal_event_batch3d_model_add(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
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
    metal_cache2_batch_push_model_mesh(
        ctx,
        ctx->renderer->batch3d_staging,
        model,
        model_id,
        bid,
        GPU_MODEL_ANIMATION_NONE_IDX,
        0u,
        bake);
}
