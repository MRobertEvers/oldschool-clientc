#include "platforms/common/pass_3d_builder/pass_3d_builder2_opengl3.h"
#include "platforms/opengl3/events/opengl3_events.h"
#include "platforms/opengl3/opengl3_ctx.h"
#include "platforms/opengl3/opengl3_renderer_core.h"

#include <SDL.h>
#include <cmath>
#include <cstdio>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void
opengl3_event_end_3d(OpenGL3RenderCtx* ctx)
{
    if( !ctx || !ctx->renderer )
        return;

    Platform2_SDL2_Renderer_OpenGL3* renderer = ctx->renderer;

    if( renderer->prog_world3d == 0u )
        return;

    if( !renderer->pass3d_builder.HasCommands() )
    {
        renderer->pass3d_builder.End();
        return;
    }

    if( renderer->pass.uniform_pass_subslot >= (uint32_t)kOpenGL3Max3dPassesPerFrame )
    {
        fprintf(
            stderr,
            "opengl3_event_end_3d: more than %d 3D passes in one frame; skipping draw\n",
            kOpenGL3Max3dPassesPerFrame);
        renderer->pass3d_builder.ClearAfterSubmit();
        renderer->pass3d_builder.End();
        return;
    }

    const LogicalViewportRect& pl = ctx->renderer->pass.pass_3d_dst_logical;
    const float projection_width = (float)pl.width;
    const float projection_height = (float)pl.height;

    OpenGL3WorldUniforms uniforms{};
    struct GGame* game = ctx->game;
    const float pitch_rad = game ? opengl3_dash_yaw_to_radians(game->camera_pitch) : 0.0f;
    const float yaw_rad = game ? opengl3_dash_yaw_to_radians(game->camera_yaw) : 0.0f;
    opengl3_compute_view_matrix(
        uniforms.modelViewMatrix,
        game ? (float)game->camera_world_x : 0.0f,
        game ? (float)game->camera_world_y : 0.0f,
        game ? (float)game->camera_world_z : 0.0f,
        pitch_rad,
        yaw_rad);
    opengl3_compute_projection_matrix(
        uniforms.projectionMatrix,
        (90.0f * (float)M_PI) / 180.0f,
        projection_width > 0.0f ? projection_width : 1.0f,
        projection_height > 0.0f ? projection_height : 1.0f);
    uniforms.uClock = (float)(SDL_GetTicks64() / 20);
    uniforms._pad_uniform[0] = 0.0f;
    uniforms._pad_uniform[1] = 0.0f;
    uniforms._pad_uniform[2] = 0.0f;

    Pass3DBuilder2SubmitOpenGL3(
        renderer->pass3d_builder,
        renderer,
        renderer->prog_world3d,
        renderer->world_locs,
        renderer->cache2_atlas_tex,
        uniforms.modelViewMatrix,
        uniforms.projectionMatrix,
        uniforms.uClock);

    renderer->pass.uniform_pass_subslot += 1u;

    renderer->pass3d_builder.ClearAfterSubmit();
    renderer->pass3d_builder.End();
}
