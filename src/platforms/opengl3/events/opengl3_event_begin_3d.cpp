#include "platforms/opengl3/events/opengl3_events.h"
#include "platforms/opengl3/opengl3_ctx.h"
#include "platforms/opengl3/opengl3_renderer_core.h"
#include "tori_rs_render.h"

void
opengl3_event_begin_3d(
    OpenGL3RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd,
    const LogicalViewportRect* default_logical_vp)
{
    if( !ctx || !ctx->renderer || !default_logical_vp )
        return;

    Platform2_SDL2_Renderer_OpenGL3* renderer = ctx->renderer;

    LogicalViewportRect pass_lr = *default_logical_vp;
    if( cmd && cmd->_begin_3d.w > 0 && cmd->_begin_3d.h > 0 )
    {
        pass_lr.x = cmd->_begin_3d.x;
        pass_lr.y = cmd->_begin_3d.y;
        pass_lr.width = cmd->_begin_3d.w;
        pass_lr.height = cmd->_begin_3d.h;
    }

    const ToriGlViewportPixels gl_vp = opengl3_compute_gl_world_viewport_rect(
        renderer->width,
        renderer->height,
        ctx->renderer->pass.win_width,
        ctx->renderer->pass.win_height,
        pass_lr);
    ctx->renderer->pass.pass_3d_dst_logical = pass_lr;
    ctx->renderer->pass.world_gl_vp = gl_vp;

    glViewport(gl_vp.x, gl_vp.y, gl_vp.width, gl_vp.height);
    glEnable(GL_SCISSOR_TEST);
    opengl3_clamped_gl_scissor(
        renderer->width,
        renderer->height,
        ctx->renderer->pass.win_width,
        ctx->renderer->pass.win_height,
        pass_lr.x,
        pass_lr.y,
        pass_lr.width,
        pass_lr.height);

    struct ToriRSRenderCommand world_clear = { 0 };
    world_clear.kind = TORIRS_GFX_STATE_CLEAR_RECT;
    world_clear._clear_rect.x = pass_lr.x;
    world_clear._clear_rect.y = pass_lr.y;
    world_clear._clear_rect.w = pass_lr.width;
    world_clear._clear_rect.h = pass_lr.height;
    if( world_clear._clear_rect.w > 0 && world_clear._clear_rect.h > 0 )
        opengl3_event_clear_rect(ctx, &world_clear);

    glViewport(gl_vp.x, gl_vp.y, gl_vp.width, gl_vp.height);
    opengl3_clamped_gl_scissor(
        renderer->width,
        renderer->height,
        ctx->renderer->pass.win_width,
        ctx->renderer->pass.win_height,
        pass_lr.x,
        pass_lr.y,
        pass_lr.width,
        pass_lr.height);

    renderer->pass3d_builder.Begin();
}
