#include "platforms/opengl3/events/opengl3_events.h"
#include "platforms/opengl3/opengl3_ctx.h"
#include "platforms/opengl3/opengl3_renderer_core.h"

#include <cstdio>

void
opengl3_event_clear_rect(
    OpenGL3RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !cmd )
        return;
    Platform2_SDL2_Renderer_OpenGL3* rr = ctx->renderer;
    const int rx = cmd->_clear_rect.x;
    const int ry = cmd->_clear_rect.y;
    const int rw = cmd->_clear_rect.w;
    const int rh = cmd->_clear_rect.h;
    if( rw <= 0 || rh <= 0 )
        return;

    if( ctx->renderer->pass.clear_rect_slot >= kOpenGL3MaxClearRectsPerFrame )
    {
        static bool s_warned;
        if( !s_warned )
        {
            fprintf(
                stderr,
                "[opengl3] CLEAR_RECT: more than %d clears in one frame; extra clears ignored\n",
                kOpenGL3MaxClearRectsPerFrame);
            s_warned = true;
        }
        return;
    }
    ctx->renderer->pass.clear_rect_slot++;

    glViewport(0, 0, rr->width, rr->height);
    glEnable(GL_SCISSOR_TEST);
    opengl3_clamped_gl_scissor(
        rr->width,
        rr->height,
        ctx->renderer->pass.win_width,
        ctx->renderer->pass.win_height,
        rx,
        ry,
        rw,
        rh);
    glDepthMask(GL_TRUE);
    glClearDepth(1.0);
    glClear(GL_DEPTH_BUFFER_BIT);
    glScissor(0, 0, rr->width > 0 ? rr->width : 1, rr->height > 0 ? rr->height : 1);
}
