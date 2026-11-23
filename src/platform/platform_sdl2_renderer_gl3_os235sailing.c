/*
 * "OpenGL (Sailing OSRS)", --opengl3-os235sailing: the rev-241 client's world
 * pass (platform_sdl2_renderer_gl3zb.h, GL3ZB_Rev241*) over the os1 vertex,
 * whose position.w carries each face's depth bias, drawn by
 * shaders/vertex_os235sailing.glsl's depth key.
 *
 * Its own frame and dispatch: the world commands are this renderer's, the
 * rest go through gl3_dispatch. The bake is the os1 chain the painter and
 * the depth pass share. docs/toridraw_rszbuffer.md.
 */

#include "platform/platform_sdl2_renderer_gl3_internal.h"
#include "platform/platform_sdl2_renderer_gl3zb.h"

#include <assert.h>

static void
gl3_os235_model_draw(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    struct ToriRS_RenderCommand* command)
{
    struct ToriRS_RenderCommand_Model const* mcmd = &command->u.model;
    int face_count;
    int anim_index;
    int pose_id;
    uint32_t group;
    uint32_t vertex_base;
    bool projected;
    if( !gl3_model_draw_begin(renderer, command) )
        return;
    if( !gl3_model_cull_depth(renderer, mcmd, &projected) )
        return;
    if( mcmd->pick_only )
        return;
    face_count = trspk_toridraw_face_count(mcmd->model);
    if( face_count <= 0 )
        return;
    gl3_model_pose_ids(mcmd, &anim_index, &pose_id);
    if( !gl3_model_vertices_os1(renderer, mcmd, anim_index, pose_id, &group, &vertex_base) )
        return;
    GL3ZB_SubmitModelRev241(
        renderer, mcmd, renderer->scene, group, vertex_base, face_count, projected);
}

static void
gl3_os235_dispatch(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    struct ToriRS_RenderCommand* command)
{
    switch( command->kind )
    {
    case TORIRSRC_BEGIN_3D:
        GL3ZB_Rev241Begin3D(renderer, command);
        break;
    case TORIRSRC_END_3D:
        GL3ZB_Rev241End3D(renderer);
        break;
    case TORIRSRC_DRAW_MODEL:
        gl3_os235_model_draw(renderer, command);
        break;
    default:
        gl3_dispatch(renderer, command);
        break;
    }
}

bool
ToriPlatformSDL2_Renderer_GL3_Os235SailingInit(
    struct ToriPlatformSDL2_Renderer_GL3* gl3,
    ToriPlatform_GLWindow* window,
    struct ToriDraw_Scene* scene)
{
    assert(gl3);
    assert(gl3->vertex_format == TRSPK_VERTEX_FORMAT_OPENGL3_OS1);
    /* A depth context; the fragment stage is the generic one. */
    return gl3_init(
        gl3, window, scene, true, trspk_opengl3_os235sailing_vertex_shader,
        trspk_opengl3_fragment_shader);
}

void
ToriPlatformSDL2_Renderer_GL3_Os235SailingRenderFrame(
    struct ToriPlatformSDL2_Renderer_GL3* gl3,
    struct ToriRS_Frame* frame)
{
    struct GL3FrameState state;
    struct ToriRS_RenderCommand command;
    uint64_t deadline;
    gl3_frame_begin(gl3, frame, &state);
    while( ToriRS_FrameNextCommand(frame, &command) )
    {
        gl3_os235_dispatch(gl3, &command);
        gl3_frame_command_timed(gl3, &state, &command);
    }
    gl3_frame_present(gl3, frame, &state);
    deadline = gl3_frame_prefill_deadline(gl3, &state);
    if( deadline )
        gl3_world_prefill_os1(gl3, deadline);
    gl3_frame_finish(gl3, &state);
}
