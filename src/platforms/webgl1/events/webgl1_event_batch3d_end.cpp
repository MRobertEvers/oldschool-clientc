#ifdef __EMSCRIPTEN__

#    include "platforms/common/pass_3d_builder/gpu_3d_cache2_webgl1.h"
#    include "platforms/webgl1/events/webgl1_events.h"
#    include "platforms/webgl1/ctx.h"

void
webgl1_event_batch3d_end(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !cmd )
        return;
    const uint32_t bid = cmd->_batch.batch_id;

    BatchBuffersWebGL1 v2bufs{};
    GPU3DCache2BatchSubmitWebGL1(
        ctx->renderer->model_cache2,
        ctx->renderer->batch3d_staging,
        v2bufs,
        bid,
        (ToriRS_UsageHint)cmd->_batch.usage_hint);
    ctx->renderer->batch3d_staging = WebGL1BatchBuffer{};

    GPU3DCache2Resource res{};
    res.vbo = (GPUResourceHandle)(uintptr_t)v2bufs.vbo;
    res.ebo = (GPUResourceHandle)(uintptr_t)v2bufs.ebo;
    res.valid = (v2bufs.vbo != 0u && v2bufs.ebo != 0u);
    res.policy = GPU3DCache2PolicyForUsageHint((ToriRS_UsageHint)cmd->_batch.usage_hint);
    ctx->renderer->model_cache2.SceneBatchSetResource(bid, res);
    v2bufs.vbo = 0u;
    v2bufs.ebo = 0u;

    ctx->renderer->pass.current_model_batch_id = 0;
    ctx->renderer->pass.current_model_batch_active = false;
}

#endif
