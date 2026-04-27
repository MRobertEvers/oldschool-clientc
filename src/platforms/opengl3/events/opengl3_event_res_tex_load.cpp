#include "platforms/opengl3/events/opengl3_events.h"

void
opengl3_event_res_tex_load(
    OpenGL3RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    opengl3_event_texture_load(ctx, cmd);
}
