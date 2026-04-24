// System ObjC/Metal headers must come before any game headers.
#include "platforms/metal/metal_internal.h"

void
metal_frame_event_model_load(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    (void)ctx;
    (void)cmd;
}

void
metal_frame_event_model_unload(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    const int mid = cmd->_model_load.model_id;
    if( mid <= 0 )
        return;
    ctx->renderer->model_cache2.ClearModel((uint16_t)mid);
}

void
metal_frame_event_model_animation_load(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    const int mid = cmd->_animation_load.model_gpu_id;
    const int fidx = cmd->_animation_load.frame_index;
    if( !cmd->_animation_load.model || mid <= 0 )
        return;

    const uint32_t bid = ctx->renderer->current_model_batch_id;
    if( bid == 0 )
        return;

    struct DashModel* model = cmd->_animation_load.model;
    dashmodel_animate(model, cmd->_animation_load.frame, cmd->_animation_load.framemap);
    const uint8_t gpu_seg = cmd->_animation_load.animation_index == 1
                                ? GPU_MODEL_ANIMATION_SECONDARY_IDX
                                : GPU_MODEL_ANIMATION_PRIMARY_IDX;
    metal_cache2_batch_push_model_mesh(ctx, model, mid, bid, gpu_seg, (uint16_t)fidx);
}
