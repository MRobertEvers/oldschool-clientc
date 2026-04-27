#include "platforms/opengl3/events/opengl3_events.h"
#include "platforms/opengl3/opengl3_ctx.h"
#include "platforms/opengl3/opengl3_renderer_core.h"
#include "tori_rs_render.h"

void
opengl3_event_batch3d_begin(
    OpenGL3RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !cmd )
        return;
    const uint32_t bid = cmd->_batch.batch_id;
    ctx->renderer->pass.current_model_batch_id = bid;
    ctx->renderer->pass.current_model_batch_active = true;
    ctx->renderer->batch3d_staging = OpenGL3BatchBuffer{};
    (void)ctx->renderer->model_cache2.SceneBatchBegin(
        bid, (ToriRS_UsageHint)cmd->_batch.usage_hint);
}
