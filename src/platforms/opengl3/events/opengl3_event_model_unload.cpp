#include "platforms/opengl3/events/opengl3_events.h"
#include "platforms/opengl3/opengl3_atlas_resources.h"
#include "platforms/opengl3/opengl3_ctx.h"
#include "platforms/opengl3/opengl3_renderer_core.h"
#include "tori_rs_render.h"

void
opengl3_event_model_unload(
    OpenGL3RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    const int mid = cmd->_model_load.model_id;
    if( mid <= 0 || !ctx || !ctx->renderer )
        return;
    GPU3DCache2Resource solo =
        ctx->renderer->model_cache2.TakeStandaloneRetainedBuffers((uint16_t)mid);
    opengl3_delete_gl_buffer(solo.vbo);
    opengl3_delete_gl_buffer(solo.ebo);
    ctx->renderer->model_cache2.ClearModel((uint16_t)mid);
}
