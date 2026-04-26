// System ObjC/Metal headers must come before any game headers.
#include "platforms/metal/events/metal_events.h"
#include "platforms/metal/metal_internal.h"

void
metal_event_batch3d_begin(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !cmd )
        return;
    const uint32_t bid = cmd->_batch.batch_id;
    ctx->renderer->pass.current_model_batch_id = bid;
    ctx->renderer->pass.current_model_batch_active = true;
    ctx->renderer->batch3d_staging = MetalBatchBuffer{};
    (void)ctx->renderer->model_cache2.SceneBatchBegin(bid, (ToriRS_UsageHint)cmd->_batch.usage_hint);
}
