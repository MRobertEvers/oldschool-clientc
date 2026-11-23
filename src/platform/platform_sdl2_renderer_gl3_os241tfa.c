/*
 * "OpenGL (Post-TFA OSRS)", --opengl3-os241tfa: the rev-241 client's world
 * pass (platform_sdl2_renderer_gl3zb.h, GL3ZB_Rev241*) over the os241tfa
 * vertex, which carries each textured face's texture projector so the
 * program (shaders/vertex_os241tfa.glsl, fragment_os241tfa.glsl) finds the
 * texel along the eye ray as the client does [class171.method5476]. That is
 * what the Ocellus void's P/M/N textures need: their projector is not in the
 * face's plane, so a baked texcoord is right only where the eye looks along
 * the face.
 *
 * Its own frame, dispatch and bake chain; the chain's steps are the ones the
 * os1 chain is made of (platform_sdl2_renderer_gl3_internal.h), with this
 * vertex's writer. docs/toridraw_rszbuffer.md.
 */

#include "platform/platform_sdl2_renderer_gl3_internal.h"
#include "platform/platform_sdl2_renderer_gl3zb.h"

#include <assert.h>
#include <string.h>

_Static_assert(
    sizeof(struct TRSPK_VertexOpenGl3Os241Tfa) == 84u,
    "the os241tfa GL3 vertex is the os1 one and nine projector floats");

/* One os241tfa vertex. `projector` is the face's P, U, V (nine floats, world
 * space) or NULL for none; P is stored less the corner. */
static void
gl3_os241_write_vertex(
    struct TRSPK_VBO* vbo,
    uint32_t index,
    float x,
    float y,
    float z,
    float color[4],
    float u,
    float v,
    float tex_id,
    float uv_mode,
    float depth_bias,
    const float* projector)
{
    struct TRSPK_VertexOpenGl3Os241Tfa* vertex = &vbo->vertices.as_opengl3_os241tfa[index];
    vertex->position[0] = x;
    vertex->position[1] = y;
    vertex->position[2] = z;
    vertex->position[3] = depth_bias;
    memcpy(vertex->color, color, sizeof(vertex->color));
    vertex->texcoord[0] = u;
    vertex->texcoord[1] = v;
    vertex->tex_id = tex_id;
    vertex->uv_mode = uv_mode;
    if( !projector )
    {
        memset(vertex->projector_p, 0, sizeof(vertex->projector_p));
        memset(vertex->projector_u, 0, sizeof(vertex->projector_u));
        memset(vertex->projector_v, 0, sizeof(vertex->projector_v));
        return;
    }
    vertex->projector_p[0] = projector[0] - x;
    vertex->projector_p[1] = projector[1] - y;
    vertex->projector_p[2] = projector[2] - z;
    memcpy(vertex->projector_u, projector + 3, sizeof(vertex->projector_u));
    memcpy(vertex->projector_v, projector + 6, sizeof(vertex->projector_v));
}

/* ---- the bake chain: the os241tfa vertex ---------------------------------- */

static void
gl3_os241_bake_pose(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    struct TRSPK_VBO* vbo,
    struct TRSPK_Triangles* triangles,
    uint32_t base,
    struct ToriDraw_ModelHandle model_handle,
    const struct ToriDraw_Position* world_position)
{
    const uint32_t tri_count = (uint32_t)trspk_toridraw_face_count(model_handle);
    struct TRSPK_WorldPlacement placement;
    assert(renderer->scene);
    assert(vbo);
    assert(triangles);
    assert(vbo->format == TRSPK_VERTEX_FORMAT_OPENGL3_OS241TFA);

    trspk_toridraw_placement_init(&placement, world_position);
    for( uint32_t face_index = 0; face_index < tri_count; face_index++ )
    {
        const uint32_t vi = base + face_index * 3u;
        struct TRSPK_ToriDrawBakeFaceVerts face;
        float depth_bias;
        float projector[9];
        const float* face_projector = NULL;
        if( !gl3_bake_face(
                renderer, model_handle, face_index, &placement, triangles, base, &face,
                &depth_bias) )
            continue;
        /* A tile has none: its texture lies in its plane. */
        if( face.tex_id >= 0 && ToriDraw_ModelKindIsFull(model_handle.kind) )
        {
            trspk_toridraw_face_projector(
                model_handle.u.model.model, face_index, &placement, projector, projector + 3,
                projector + 6);
            face_projector = projector;
        }
        gl3_os241_write_vertex(
            vbo, vi + 0u, face.wx_a, face.wy_a, face.wz_a, face.color_a, face.uv.u1, face.uv.v1,
            face.tex_id_encoded, face.uv_mode, depth_bias, face_projector);
        gl3_os241_write_vertex(
            vbo, vi + 1u, face.wx_b, face.wy_b, face.wz_b, face.color_b, face.uv.u2, face.uv.v2,
            face.tex_id_encoded, face.uv_mode, depth_bias, face_projector);
        gl3_os241_write_vertex(
            vbo, vi + 2u, face.wx_c, face.wy_c, face.wz_c, face.color_c, face.uv.u3, face.uv.v3,
            face.tex_id_encoded, face.uv_mode, depth_bias, face_projector);
    }
    trspk_vbo_mark_dirty_range(vbo, base, tri_count * 3u);
}

static void
gl3_os241_world_record_bake(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    struct TRSPK_WorldSlot* w,
    int r)
{
    struct TRSPK_WorldRecord const* record = &w->records[r];
    struct TRSPK_Batch32Reservation reservation;
    trspk_world_record_take(w, r, &reservation);
    gl3_os241_bake_pose(
        renderer,
        reservation.vbo,
        reservation.triangles,
        reservation.vertex_base,
        record->model,
        &record->world_position);
    gl3_world_record_count(renderer, record->model);
}

/* 1: the deadline came first. */
static bool
gl3_os241_world_prefill_slot(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    struct TRSPK_WorldSlot* w,
    uint64_t deadline)
{
    while( trspk_world_prefill_more(w) )
    {
        uint64_t const t0 = SDL_GetPerformanceCounter();
        int r;
        if( t0 >= deadline )
            return true;
        r = trspk_world_prefill_next(w);
        if( r < 0 )
            continue;
        gl3_os241_world_record_bake(renderer, w, r);
        gl3_world_prefill_unit(renderer, t0);
    }
    return false;
}

static void
gl3_os241_world_prefill(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    uint64_t deadline)
{
    struct TRSPK_WorldBuild* build = &renderer->build;
    if( build->world.drawable && gl3_os241_world_prefill_slot(renderer, &build->world, deadline) )
        return;
    if( trspk_world_build_stage_open(build) )
    {
        uint64_t const t0 = gl3_world_stage_prefill_begin(renderer);
        gl3_os241_world_prefill_slot(renderer, &build->stage, deadline);
        gl3_world_stage_prefill_end(renderer, t0);
        return;
    }
    gl3_world_stage_release(renderer, deadline);
}

static const struct TRSPK_Batch32Entry*
gl3_os241_world_pose(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    struct ToriRS_RenderCommand_Model const* mcmd,
    int anim_index,
    int pose_id)
{
    int const r = gl3_world_pose_unbaked(renderer, mcmd, anim_index, pose_id);
    if( r >= 0 )
    {
        uint64_t const t0 = GL3_TICKS(renderer);
        gl3_os241_world_record_bake(renderer, &renderer->build.world, r);
        gl3_world_draw_bake_timed(renderer, t0);
    }
    return gl3_world_pose_entry(renderer, mcmd, anim_index, pose_id);
}

static uint32_t
gl3_os241_bake_into_arena(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    struct GL3ModelGroup* g,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle model_handle,
    const struct ToriDraw_Position* world_position,
    bool update_pose_table)
{
    uint32_t const base = gl3_arena_reserve(
        g, element_id, anim_index, pose_id, trspk_toridraw_face_count(model_handle),
        update_pose_table);
    if( base == UINT32_MAX )
        return UINT32_MAX;
    gl3_os241_bake_pose(renderer, g->vbo_cpu, &g->triangles, base, model_handle, world_position);
    gl3_arena_commit(renderer, element_id, anim_index, pose_id, base, update_pose_table);
    return base;
}

static void
gl3_os241_bake_track_pose(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    int element_id,
    int anim_index,
    int frame,
    struct ToriDraw_Model* source,
    struct ToriDraw_Animation* animation,
    const struct ToriDraw_Position* world_position)
{
    struct ToriDraw_Model* baked = gl3_track_pose_copy(source, animation, frame);
    struct ToriDraw_ModelHandle baked_handle = {
        .kind = TORIDRAWMK_MODEL,
        .u.model.model = baked,
    };
    gl3_os241_bake_into_arena(renderer, &renderer->groups[TRSPK_VBO_GROUP_STATIC],
        element_id, anim_index, frame, baked_handle, world_position, true);
    ToriDraw_ModelFree(baked);
}

static uint32_t
gl3_os241_bake_dynamic(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    struct GL3ModelGroup* g,
    struct ToriRS_RenderCommand_Model const* mcmd,
    int anim_index,
    int pose_id)
{
    struct TRSPK_PoseKey key;
    bool keyed;
    uint32_t vertex_base = gl3_dynamic_reuse(renderer, g, mcmd, anim_index, pose_id, &key, &keyed);
    if( vertex_base != UINT32_MAX )
        return vertex_base;
    vertex_base = gl3_os241_bake_into_arena(
        renderer, g, mcmd->element_id, anim_index, pose_id, mcmd->model, &mcmd->world_position, false);
    gl3_dynamic_keep(renderer, g, mcmd, &key, keyed, vertex_base);
    return vertex_base;
}

/* As gl3_model_vertices_os1, baking this renderer's vertex. */
static bool
gl3_os241_model_vertices(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    struct ToriRS_RenderCommand_Model const* mcmd,
    int anim_index,
    int pose_id,
    uint32_t* out_group,
    uint32_t* out_vertex_base)
{
    const struct TRSPK_Batch32Entry* world_pose;
    if( mcmd->dynamic )
    {
        *out_group = TRSPK_VBO_GROUP_DYNAMIC;
        *out_vertex_base = gl3_os241_bake_dynamic(
            renderer, &renderer->groups[TRSPK_VBO_GROUP_DYNAMIC], mcmd, anim_index, pose_id);
        return *out_vertex_base != UINT32_MAX;
    }
    world_pose = gl3_os241_world_pose(renderer, mcmd, anim_index, pose_id);
    if( world_pose )
    {
        *out_group = TRSPK_VBO_GROUP_WORLD;
        *out_vertex_base = world_pose->vertex_base;
        if( renderer->dbg_timing.on )
            renderer->dbg_timing.world_hits++;
        return true;
    }
    *out_group = TRSPK_VBO_GROUP_STATIC;
    if( trspk_pose_table_get(&renderer->poses, mcmd->element_id, anim_index, pose_id, out_vertex_base) )
        return true;
    {
        uint64_t const t0 = GL3_TICKS(renderer);
        const struct TRSPK_AnimTrack* track = gl3_static_track(renderer, mcmd, anim_index, pose_id);
        if( track )
            gl3_os241_bake_track_pose(renderer, track->announced_id, anim_index, pose_id,
                mcmd->model.u.model.model, mcmd->animation, &track->world_position);
        else
            gl3_os241_bake_into_arena(renderer, &renderer->groups[TRSPK_VBO_GROUP_STATIC],
                mcmd->element_id, anim_index, pose_id, mcmd->model, &mcmd->world_position, true);
        gl3_lazy_bake_timed(renderer, mcmd, t0);
    }
    return trspk_pose_table_get(&renderer->poses, mcmd->element_id, anim_index, pose_id, out_vertex_base);
}

static void
gl3_os241_model_load(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    struct ToriRS_RenderCommand* command)
{
    int const element_id = command->u.model_load.element_id;
    assert(command->kind == TORIRSRC_MODEL_LOAD);
    gl3_model_load_forget(renderer, element_id);
    gl3_os241_bake_into_arena(renderer, &renderer->groups[TRSPK_VBO_GROUP_STATIC],
        element_id, 0, 0, command->u.model_load.model, &command->u.model_load.world_position,
        true);
}

/* ---- the world pass ----------------------------------------------------------- */

static void
gl3_os241_model_draw(
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
    if( !gl3_os241_model_vertices(renderer, mcmd, anim_index, pose_id, &group, &vertex_base) )
        return;
    GL3ZB_SubmitModelRev241(
        renderer, mcmd, renderer->scene, group, vertex_base, face_count, projected);
}

static void
gl3_os241_dispatch(
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
        gl3_os241_model_draw(renderer, command);
        break;
    case TORIRSRC_MODEL_LOAD:
        gl3_os241_model_load(renderer, command);
        break;
    default:
        gl3_dispatch(renderer, command);
        break;
    }
}

/* ---- the handle ----------------------------------------------------------------- */

struct ToriPlatformSDL2_Renderer_GL3*
ToriPlatformSDL2_Renderer_GL3_NewOs241Tfa(
    int width,
    int height)
{
    return gl3_renderer_new(width, height, TRSPK_VERTEX_FORMAT_OPENGL3_OS241TFA);
}

bool
ToriPlatformSDL2_Renderer_GL3_Os241TfaInit(
    struct ToriPlatformSDL2_Renderer_GL3* gl3,
    ToriPlatform_GLWindow* window,
    struct ToriDraw_Scene* scene)
{
    assert(gl3);
    assert(gl3->vertex_format == TRSPK_VERTEX_FORMAT_OPENGL3_OS241TFA);
    return gl3_init(
        gl3, window, scene, true, trspk_opengl3_os241tfa_vertex_shader,
        trspk_opengl3_os241tfa_fragment_shader);
}

void
ToriPlatformSDL2_Renderer_GL3_Os241TfaRenderFrame(
    struct ToriPlatformSDL2_Renderer_GL3* gl3,
    struct ToriRS_Frame* frame)
{
    struct GL3FrameState state;
    struct ToriRS_RenderCommand command;
    uint64_t deadline;
    gl3_frame_begin(gl3, frame, &state);
    while( ToriRS_FrameNextCommand(frame, &command) )
    {
        gl3_os241_dispatch(gl3, &command);
        gl3_frame_command_timed(gl3, &state, &command);
    }
    gl3_frame_present(gl3, frame, &state);
    deadline = gl3_frame_prefill_deadline(gl3, &state);
    if( deadline )
        gl3_os241_world_prefill(gl3, deadline);
    gl3_frame_finish(gl3, &state);
}
