#include "platforms/common/pass_3d_builder/gpu_3d_cache2_opengl3.h"
#include "platforms/opengl3/events/opengl3_events.h"
#include "platforms/opengl3/opengl3_ctx.h"
#include "platforms/opengl3/opengl3_renderer_core.h"
#include "tori_rs_render.h"

void
opengl3_event_batch3d_end(
    OpenGL3RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !cmd )
        return;
    const uint32_t bid = cmd->_batch.batch_id;

    BatchBuffersOpenGL3 v2bufs{};
    GPU3DCache2BatchSubmitOpenGL3(
        ctx->renderer->model_cache2,
        ctx->renderer->batch3d_staging,
        v2bufs,
        bid,
        (ToriRS_UsageHint)cmd->_batch.usage_hint);
    ctx->renderer->batch3d_staging = OpenGL3BatchBuffer{};

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
