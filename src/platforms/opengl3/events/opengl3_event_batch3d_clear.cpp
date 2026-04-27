#include "platforms/opengl3/events/opengl3_events.h"
#include "platforms/opengl3/opengl3_ctx.h"
#include "platforms/opengl3/opengl3_renderer_core.h"
#include "tori_rs_render.h"

static void
opengl3_delete_gl_buffer_evt(GPUResourceHandle h)
{
    if( !h )
        return;
    GLuint name = (GLuint)(uintptr_t)h;
    glDeleteBuffers(1, &name);
}

void
opengl3_event_batch3d_clear(
    OpenGL3RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !cmd )
        return;
    const uint32_t bid = cmd->_batch.batch_id;
    GPU3DCache2SceneBatchEntry cleared = ctx->renderer->model_cache2.SceneBatchClear(bid);
    for( uint16_t mid : cleared.scene_model_ids )
        ctx->renderer->model_cache2.ClearModel(mid);
    opengl3_delete_gl_buffer_evt(cleared.resource.vbo);
    opengl3_delete_gl_buffer_evt(cleared.resource.ebo);
}
