#ifdef __EMSCRIPTEN__

#    include "platforms/webgl1/events/webgl1_events.h"

void
webgl1_event_batch2d_tex_begin(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    (void)ctx;
    (void)cmd;
}

#endif
