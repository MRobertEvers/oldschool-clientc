#ifdef __EMSCRIPTEN__

#    include "platforms/webgl1/events/webgl1_events.h"
#    include "platforms/webgl1/ctx.h"

void
webgl1_event_res_tex_load(WebGL1RenderCtx* ctx, const struct ToriRSRenderCommand* cmd)
{
    webgl1_frame_event_texture_load(ctx, cmd);
}

#endif
