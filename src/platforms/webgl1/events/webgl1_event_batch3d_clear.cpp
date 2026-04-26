#ifdef __EMSCRIPTEN__

#    include "platforms/webgl1/events/webgl1_events.h"
#    include "platforms/webgl1/ctx.h"

#    include <GLES2/gl2.h>

static void
webgl1_delete_gl_buffer(GPUResourceHandle h)
{
    if( !h )
        return;
    GLuint name = (GLuint)(uintptr_t)h;
    glDeleteBuffers(1, &name);
}

void
webgl1_event_batch3d_clear(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !cmd )
        return;
    const uint32_t bid = cmd->_batch.batch_id;
    GPU3DCache2SceneBatchEntry cleared = ctx->renderer->model_cache2.SceneBatchClear(bid);
    for( uint16_t mid : cleared.scene_model_ids )
        ctx->renderer->model_cache2.ClearModel(mid);
    webgl1_delete_gl_buffer(cleared.resource.vbo);
    webgl1_delete_gl_buffer(cleared.resource.ebo);
}

#endif
