#ifndef PLATFORMS_OPENGL3_OPENGL3_RENDER_H
#define PLATFORMS_OPENGL3_OPENGL3_RENDER_H

#include "platforms/opengl3/opengl3_ctx.h"

struct LogicalViewportRect;
struct ToriRSRenderCommandBuffer;

void
opengl3_render_dispatch_commands(
    OpenGL3RenderCtx* ctx,
    struct ToriRSRenderCommandBuffer* command_buffer,
    const LogicalViewportRect* default_logical_vp);

#endif
