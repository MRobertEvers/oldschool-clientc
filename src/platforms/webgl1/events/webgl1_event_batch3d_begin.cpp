#ifdef __EMSCRIPTEN__

#    include "platforms/webgl1/events/webgl1_events.h"
#    include "platforms/webgl1/ctx.h"

void
webgl1_event_batch3d_begin(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !cmd )
        return;
    const uint32_t bid = cmd->_batch.batch_id;
    ctx->renderer->pass.current_model_batch_id = bid;
    ctx->renderer->pass.current_model_batch_active = true;
    ctx->renderer->batch3d_staging = WebGL1BatchBuffer{};
    (void)ctx->renderer->model_cache2.SceneBatchBegin(bid, (ToriRS_UsageHint)cmd->_batch.usage_hint);
}

#endif
