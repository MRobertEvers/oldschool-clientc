// System ObjC/Metal headers must come before any game headers.
#include "platforms/common/pass_3d_builder/gpu_3d_cache2_metal.h"
#include "platforms/metal/events/metal_events.h"
#include "platforms/metal/metal_internal.h"

void
metal_event_batch3d_end(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !cmd )
        return;
    const uint32_t bid = cmd->_batch.batch_id;
    id<MTLDevice> device = (__bridge id<MTLDevice>)ctx->renderer->mtl_device;

    BatchBuffers v2bufs{};
    GPU3DCache2BatchSubmitMetal(
        ctx->renderer->model_cache2,
        ctx->renderer->batch3d_staging,
        device,
        v2bufs,
        bid,
        (ToriRS_UsageHint)cmd->_batch.usage_hint);
    ctx->renderer->batch3d_staging = MetalBatchBuffer{};

    GPU3DCache2Resource res{};
    res.vbo = (GPUResourceHandle)(uintptr_t)v2bufs.vbo;
    res.ebo = (GPUResourceHandle)(uintptr_t)v2bufs.ebo;
    res.valid = (v2bufs.vbo != nullptr && v2bufs.ebo != nullptr);
    res.policy = GPU3DCache2PolicyForUsageHint((ToriRS_UsageHint)cmd->_batch.usage_hint);
    ctx->renderer->model_cache2.SceneBatchSetResource(bid, res);
    v2bufs.vbo = nullptr;
    v2bufs.ebo = nullptr;

    ctx->renderer->pass.current_model_batch_id = 0;
    ctx->renderer->pass.current_model_batch_active = false;
}
