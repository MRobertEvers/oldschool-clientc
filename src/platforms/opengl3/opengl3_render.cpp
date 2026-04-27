#include "platforms/opengl3/opengl3_render.h"

#include "platforms/opengl3/events/opengl3_events.h"
#include "tori_rs.h"
#include "tori_rs_render.h"

void
opengl3_render_dispatch_commands(
    OpenGL3RenderCtx* ctx,
    struct ToriRSRenderCommandBuffer* command_buffer,
    const LogicalViewportRect* default_logical_vp)
{
    if( !ctx || !command_buffer || !default_logical_vp )
        return;

    struct ToriRSRenderCommand cmd = { 0 };
    while( LibToriRS_FrameNextCommand(ctx->game, command_buffer, &cmd, true) )
    {
        switch( cmd.kind )
        {
        case TORIRS_GFX_STATE_BEGIN_3D:
            opengl3_event_begin_3d(ctx, &cmd, default_logical_vp);
            break;
        case TORIRS_GFX_STATE_END_3D:
            opengl3_event_end_3d(ctx);
            break;
        case TORIRS_GFX_STATE_CLEAR_RECT:
            opengl3_event_clear_rect(ctx, &cmd);
            break;
        case TORIRS_GFX_RES_TEX_LOAD:
            opengl3_event_res_tex_load(ctx, &cmd);
            break;
        case TORIRS_GFX_RES_MODEL_LOAD:
            opengl3_event_model_load(ctx, &cmd);
            break;
        case TORIRS_GFX_RES_MODEL_UNLOAD:
            opengl3_event_model_unload(ctx, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_BEGIN:
            opengl3_event_batch3d_begin(ctx, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_MODEL_ADD:
            opengl3_event_batch3d_model_add(ctx, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_END:
            opengl3_event_batch3d_end(ctx, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_CLEAR:
            opengl3_event_batch3d_clear(ctx, &cmd);
            break;
        case TORIRS_GFX_DRAW_MODEL:
            opengl3_event_draw_model(ctx, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_ANIM_ADD:
            opengl3_event_model_animation_load(ctx, &cmd);
            break;
        default:
            break;
        }
    }
}
