// System ObjC/Metal headers must come before any game headers.
#include "platforms/metal/events/metal_events.h"
#include "platforms/metal/metal_internal.h"

void
metal_event_batch3d_clear(MetalRenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !cmd )
        return;
    const uint32_t bid = cmd->_batch.batch_id;
    GPU3DCache2SceneBatchEntry cleared = ctx->renderer->model_cache2.SceneBatchClear(bid);
    for( uint16_t mid : cleared.scene_model_ids )
        ctx->renderer->model_cache2.ClearModel(mid);
    if( cleared.resource.vbo )
        CFRelease((CFTypeRef)(void*)cleared.resource.vbo);
    if( cleared.resource.ebo )
        CFRelease((CFTypeRef)(void*)cleared.resource.ebo);
}
