/**
 * The painter's-algorithm renderer for OpenGL ES 3.0 -- a WHOLE renderer,
 * not a hook layer.
 *
 * The legacy RS ordering: no depth buffer at all. Every model's faces are
 * sorted back-to-front on the CPU by ToriDraw_RenderModel2SortFaces and drawn
 * in that order, and correctness lives entirely in the submission order.
 *
 * This file owns its own frame: the GL context it asks for (no depth bits),
 * begin_3d, draw_model, end_3d, the draw sequence's issue loop, the command
 * dispatch and the frame lifecycle. It COMPOSES those out of
 * platform_androidarmv7_renderer_opengles3_core.c, which is a toolkit: the
 * scene walk, the bake, the arenas, the streams, the uploads, the shaders
 * and the state cache, with no test anywhere in it for which world path is
 * running. platform_androidarmv7_renderer_opengles3_zbuffer.c is the
 * depth-tested peer, written the same way; neither calls the other.
 *
 * That separation is not tidiness. A shared core that branched on
 * ::zbuffer is what made the ES3 family 27% slower than ES2 (five Android
 * optimisations gated behind a lever that defaulted off for one of them),
 * and it is what hid the flicker: the painter path never uploaded the static
 * arena it indexed, es3_bind_stream returned false, and the draw loop
 * silently `continue`d. One file serving two renderers is how both stayed
 * invisible.
 *
 * WHY THIS ONE IS SO MUCH SMALLER THAN THE GLES2 PAINTER
 *
 * The retained world is far larger than a 16-bit index can address: a loaded
 * region is ~960k static vertices, and painter order hops between chunks tile
 * by tile, because the terrain and the locs standing on it were baked
 * hundreds of thousands of vertices apart. On OpenGL ES 2.0 an index reaches
 * 65,536 vertices from wherever the attributes are bound and rebinding means
 * a fresh draw call, so the GLES2 painter cannot index that world at all. It
 * carries two machines to get around it:
 *
 *   - a RESIDENT WINDOW, a GPU ring that each drawn static model is copied
 *     into so that it lands somewhere a U16 index can reach, with per-entry
 *     placement serials, an overwrite guard, and a fragmentation compaction
 *     with hysteresis;
 *   - a GATHER, which copies each sorted face's 84 bytes out of the retained
 *     bake into one ordered per-frame stream for everything the ring refuses.
 *
 * A 32-bit index makes both unnecessary. Here a retained model is drawn
 * WHERE IT WAS BAKED: six bytes of index per face, no vertex traffic, no
 * residency to track, and no copy. Consecutive models merge into one draw
 * whatever part of the buffer they live in.
 *
 * What still goes through the per-frame stream is an actor: its pose is this
 * frame's, it has no retained home, and it is baked straight into the stream
 * in sorted face order, which is an array draw with no indices at all. That
 * is the whole of what `dynamic` means on this path -- there is no dynamic
 * arena here, and the depth renderer's one is not reached.
 */

#include "es3/es3_core.h"

#include "core/trspk_math.h"
#include "engine/boot_bar.h"
#include "log/torirs_log.h"
#include "painters/painters.h"
#include "perf/torirs_perf.h"
#include "platform/platform_androidarmv7_renderer_opengles3_placement.h"
#include "es3/es3_indices.h"
#include "toridraw.h"
#include "toridraw_element_id.h"
#include "toridraw_math.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* es3p_draw_model rebakes a missing animation through the load path. */
static void
es3p_animation_load(
    struct TRSPK_Renderer_ES3* renderer,
    const struct ToriRS_RenderCommand_AnimLoad* command);

/* ---- world state and ordering -------------------------------------------------------- */

static void
es3p_apply_world_states(struct TRSPK_Renderer_ES3* renderer)
{
    assert(renderer);
    /* Painter order: the submission order IS the depth order, so the depth
     * test must never reject. And no culling: the painter sorts faces and has
     * its own reasons to see every one of them. */
    es3_set_depth(renderer, false, false);
    es3_set_cull(renderer, false);
    es3_set_blend(renderer, true);
}

static int
es3p_sort_faces(
    struct TRSPK_Renderer_ES3* renderer,
    const struct ToriRS_RenderCommand_Model* command,
    int* out_sorted_face_count)
{
    int face_count;

    assert(renderer);
    assert(command);
    assert(out_sorted_face_count);
    face_count = ToriDraw_RenderModel2SortFacesWithTable(
        command->model, renderer->scene, renderer->kernel);
    /* Every face this mode draws is a sorted face: the two counts are the same
     * number, and the core wants both. */
    *out_sorted_face_count = face_count;
    return face_count;
}

static void
es3p_push_indexed(
    struct TRSPK_Renderer_ES3* renderer,
    uint32_t binding,
    uint32_t address,
    uint32_t source_face_limit,
    const int* faces,
    uint32_t count)
{
    uint32_t* indices;
    uint32_t span;

    assert(renderer);
    assert(faces);
    if( count == 0u || source_face_limit == 0u )
        return;
    span = source_face_limit * 3u;
    renderer->painter_stat_faces_indexed += count;
    /* Written straight into the draw sequence's staging: no scratch, no copy. */
    indices = es3_sequence_reserve_indexed(renderer, count * 3u);
    assert(indices);
    es3_painter_write_indices_ex(
        indices, address, source_face_limit, faces, count, /*use_neon=*/true);
    es3_sequence_commit_indexed(
        renderer, binding, address, address + span - 1u, true, false, count * 3u);
}

static void
es3p_emit_model(
    struct TRSPK_Renderer_ES3* renderer,
    const struct ES3ModelPlacement* placement)
{
    const struct TRSPK_VBO* source_vbo;
    const struct TRSPK_Triangles* source_triangles;
    const int* face_order;
    uint32_t source_face_limit;
    uint32_t face_count;

    assert(renderer);
    assert(placement);
    if( placement->sorted_face_count <= 0 )
        return;
    face_count = (uint32_t)placement->sorted_face_count;

    /* An actor was baked into the stream in sorted order by the core; its
     * placement already names the stream. */
    if( placement->binding == ES3_FRAME_STREAM_BINDING )
    {
        renderer->painter_stat_faces_actor += face_count;
        es3_sequence_push_array(
            renderer,
            ES3_FRAME_STREAM_BINDING,
            placement->absolute_base,
            face_count * 3u,
            true,
            false);
        return;
    }

    /* Whose ever bench it came from -- the scene's, or a stage source's. */
    face_order = placement->face_order;
    assert(face_order);

    /*
     * A batch entry knows its own baked vertex count, which is the bound on
     * the faces the order may name; that is the whole per-model cost here,
     * with no CPU source to resolve. The GLES2 painter has to reach the
     * chunk's CPU vertices for every model it places, because placing means
     * copying them.
     */
    if( placement->binding == ES3_STATIC_PAGE_BINDING &&
        placement->entry_index != UINT32_MAX && placement->entry_vertex_count > 0u )
    {
        es3p_push_indexed(
            renderer,
            ES3_STATIC_PAGE_BINDING,
            placement->absolute_base,
            placement->entry_vertex_count / 3u,
            face_order,
            face_count);
        return;
    }

    /*
     * Anything else retained -- an arena pose, or a static page whose Batch16
     * entry is not known -- still needs its bake's extent, and only the CPU
     * copy knows it. The order names faces by their index in the MODEL, which
     * runs past the sorted count, so placement->face_count is NOT a bound on
     * face indices; the bake is.
     */
    if( !es3_binding_cpu_source(
            renderer, placement->binding, placement->page_id, &source_vbo, &source_triangles) ||
        source_vbo->format != TRSPK_VERTEX_FORMAT_GLES2 )
        return;
    (void)source_triangles;
    if( placement->chunk_base > source_vbo->vertex_count )
        return;
    source_face_limit = (source_vbo->vertex_count - placement->chunk_base) / 3u;
    es3p_push_indexed(
        renderer,
        placement->binding,
        placement->absolute_base,
        source_face_limit,
        face_order,
        face_count);
}

void
es3p_sequence_issue(
    struct TRSPK_Renderer_ES3* renderer,
    uint32_t index_base_bytes)
{
    uint32_t item_index, draw_calls = 0;
    uint32_t skipped = 0u, skipped_binding = 0u, skipped_count = 0u;
    int program_cutout = -1;
    GLuint element_buffer = renderer->index_stream.buffers[renderer->frame_slot];
    es3_world_block_upload(renderer);
    es3p_apply_world_states(renderer);

    for( item_index = 0u; item_index < renderer->draw_item_count; item_index++ )
    {
        const struct ES3DrawItem* item = &renderer->draw_items[item_index];
        if( program_cutout != (int)item->cutout )
        {
            es3_use_world_program(renderer, item->cutout != 0u);
            program_cutout = (int)item->cutout;
        }
        if( !es3_bind_stream(renderer, item->binding) )
        {
            /*
             * The binding has no GPU buffer this frame, so every face in this
             * item is simply NOT DRAWN -- silently, until now. That is a model
             * (or a run of them) missing for one frame, which is what a
             * flicker is. @see draw_audit.
             */
            if( skipped == 0u )
            {
                skipped_binding = item->binding;
                skipped_count = item->count;
            }
            skipped++;
            continue;
        }
        if( item->indexed )
        {
            /*
             * The attachment is this VAO's, so it survives every switch away
             * and back: one glBindBuffer per binding per frame, not one per
             * draw item. es3_bind_vao must NOT clear this -- that is what made
             * it a per-item cost. @see TRSPK_Renderer_ES3::vao_element_buffer.
             */
            if( renderer->vao_element_buffer[item->binding] != element_buffer )
            {
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, element_buffer);
                renderer->vao_element_buffer[item->binding] = element_buffer;
            }
            glDrawRangeElements(
                GL_TRIANGLES,
                item->index_min,
                item->index_max,
                (GLsizei)item->count,
                GL_UNSIGNED_INT,
                (const void*)(uintptr_t)(index_base_bytes +
                                         (size_t)item->first * sizeof(uint32_t)));
        }
        else
            glDrawArrays(GL_TRIANGLES, (GLint)item->first, (GLsizei)item->count);
        draw_calls++;
    }
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DRAW_CALLS, draw_calls);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DRAW_RANGES, renderer->draw_item_count);
    if( renderer->draw_audit )
    {
        GLenum audit_error = glGetError();
        if( skipped != 0u || audit_error != GL_NO_ERROR )
            TORIRS_ERR(
                "%s: draw audit -- items %u, drawn %u, SKIPPED %u (first binding %u, %u "
                "indices), glGetError 0x%x | group0 gpu %u cpu_verts %u dirty %d cap %u | "
                "group1 gpu %u | static_batch_vbo %u\n",
                es3_log_name(),
                (unsigned)renderer->draw_item_count,
                (unsigned)draw_calls,
                (unsigned)skipped,
                (unsigned)skipped_binding,
                (unsigned)skipped_count,
                (unsigned)audit_error,
                (unsigned)renderer->groups[TRSPK_VBO_GROUP_STATIC].vbo_gpu,
                (unsigned)(renderer->groups[TRSPK_VBO_GROUP_STATIC].vbo_cpu
                               ? renderer->groups[TRSPK_VBO_GROUP_STATIC].vbo_cpu->vertex_count
                               : 0u),
                renderer->groups[TRSPK_VBO_GROUP_STATIC].vbo_cpu
                    ? (int)trspk_vbo_is_dirty(renderer->groups[TRSPK_VBO_GROUP_STATIC].vbo_cpu)
                    : -1,
                (unsigned)renderer->groups[TRSPK_VBO_GROUP_STATIC].gpu_capacity,
                (unsigned)renderer->groups[TRSPK_VBO_GROUP_DYNAMIC].vbo_gpu,
                (unsigned)renderer->static_batch_vbo);
    }
}

void
es3p_sequence_draw(struct TRSPK_Renderer_ES3* renderer)
{
    uint32_t index_base_bytes = 0u;

    assert(renderer);
    if( renderer->draw_item_count == 0u )
        return;
    if( renderer->ibo_staging_count > 0u )
    {
        uint32_t bytes = renderer->ibo_staging_count * (uint32_t)sizeof(uint32_t);
        /* The element binding belongs to whichever VAO is bound, so the
         * upload is made against the default one; es3p_sequence_issue
         * attaches the finished buffer to each VAO it draws from. */
        es3_bind_vao(renderer, 0u);
        index_base_bytes = es3_stream_set_append(
            &renderer->index_stream,
            renderer->frame_slot,
            GL_ELEMENT_ARRAY_BUFFER,
            ES3_INDEX_STREAM_INIT_BYTES,
            renderer->ibo_staging,
            bytes,
            false);
        renderer->ibo = renderer->index_stream.buffers[renderer->frame_slot];
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_IBO_UPLOAD_BYTES, (int64_t)bytes);
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_IBO_UPLOADS, 1);
    }

    es3p_sequence_issue(renderer, index_base_bytes);
}

void
es3p_begin_3d(
    struct TRSPK_Renderer_ES3* renderer,
    const struct ToriRS_RenderCommand_Begin3D* command)
{
    const struct ToriDraw_ViewPort* viewport;
    int pass_w;
    int pass_h;
    int logical_x;
    int logical_y;
    int left;
    int top;
    int right;
    int bottom;
    uint32_t group;

    assert(renderer);
    assert(command);
    if( !renderer->gl_context )
        return;
    es3_sequence_reset(renderer);
    renderer->current_3d = *command;
#if defined(TORIRS_MODEL_CHAIN_CAPTURE)
    g_chain_pass++;
#endif
#if defined(TORIRS_ANIM_CHAIN_CAPTURE)
    ToriDraw_AnimCaptureBeginPass();
#endif
    renderer->has_3d = true;
    renderer->in3d = true;
    /* Publish the prepared camera block: the prepared projection kernels are
     * gated on this pointer being the one the projection is called with. */
    if( renderer->scene )
        ToriDraw_ScenePrepareProjectionCamera(renderer->scene, &renderer->current_3d.camera);
    /* Every scene event of the frame has been dispatched by now (the frame
     * drains them before its first non-event command): a stage source may
     * start reading and posing models. */
    if( renderer->model_stage_source && renderer->model_stage_source->begin_3d )
        renderer->model_stage_source->begin_3d(renderer->model_stage_source->user, command);

    viewport = &renderer->current_3d.view_port;
    pass_w = viewport->width > 0 ? viewport->width : renderer->width;
    pass_h = viewport->height > 0 ? viewport->height : renderer->height;
    logical_x = viewport->x_center - pass_w / 2;
    logical_y = viewport->y_center - pass_h / 2;
    left = renderer->letterbox_x +
           (int)((int64_t)logical_x * renderer->letterbox_width / renderer->width);
    top = renderer->letterbox_top +
          (int)((int64_t)logical_y * renderer->letterbox_height / renderer->height);
    right = renderer->letterbox_x +
            (int)((int64_t)(logical_x + pass_w) * renderer->letterbox_width / renderer->width);
    bottom = renderer->letterbox_top +
             (int)((int64_t)(logical_y + pass_h) * renderer->letterbox_height / renderer->height);
    if( right <= left )
        right = left + 1;
    if( bottom <= top )
        bottom = top + 1;
    renderer->world_viewport.x = left;
    renderer->world_viewport.y = renderer->target_height - bottom;
    renderer->world_viewport.width = right - left;
    renderer->world_viewport.height = bottom - top;
    glViewport(
        renderer->world_viewport.x,
        renderer->world_viewport.y,
        renderer->world_viewport.width,
        renderer->world_viewport.height);

    trspk_compute_pass_matrices(
        renderer->view,
        renderer->projection,
        (float)command->camera_position.x,
        (float)command->camera_position.y,
        (float)command->camera_position.z,
        ToriDraw_AngleToRadians(command->camera.pitch),
        ToriDraw_AngleToRadians(command->camera.yaw),
        pass_w,
        pass_h,
        (int)command->camera.projection_mode,
        command->camera.projection_scale,
        command->camera.fov_rpi2048,
        command->camera.parallel_zoom16);
    /* No depth remap: trspk_compute_pass_matrices leaves clip z at a
     * constant, which is exactly right for a pass that never reads it. */
    es3_mat4_multiply(renderer->projection, renderer->view, renderer->model_view_projection);

    for( group = 0u; group < TRSPK_VBO_GROUP_COUNT; group++ )
        if( renderer->groups[group].reset_each_frame )
            es3_reset_group(&renderer->groups[group]);
}

void
es3p_draw_model(
    struct TRSPK_Renderer_ES3* renderer,
    const struct ToriRS_RenderCommand_Model* command)
{
    struct ToriDraw_Position projected_position;
    struct ES3ModelPlacement placement;
    struct ES3ModelStage stage;
    struct ES3StaticPrimary static_placement;
    int static_state;
    bool staged = false;
    const int* face_order;
    bool projected_in_scene;
    int projected_depth;
    int face_count;
    int sorted_face_count = 0;
    bool dynamic;
    int anim_index;
    int pose_id;
    uint32_t vertex_base;
    /* Where the model's bake begins in its binding's CPU copy, and where it
     * begins in that binding's GPU buffer. Equal for everything but a
     * Batch16 chunk, whose CPU copy starts at its own zero. */
    uint32_t chunk_base = 0u;
    uint32_t absolute_base = 0u;
    uint32_t binding;
    uint32_t group;

    assert(renderer);
    assert(command);
    /* The stage source, when one is installed, is asked about EVERY model
     * command before any early return: it hands results out in dispatch
     * order and pairs them with the asks by count. */
    if( renderer->model_stage_source )
        staged =
            renderer->model_stage_source->take(renderer->model_stage_source->user, command, &stage);
    if( !renderer->has_3d || !renderer->scene || command->model.kind == TORIDRAWMK_NONE )
        return;
#if defined(TORIRS_MODEL_CHAIN_CAPTURE)
    es3_chain_capture(renderer, command);
#endif
    placement.page_id = UINT32_MAX;
    placement.batch_slot = UINT32_MAX;
    placement.entry_index = UINT32_MAX;
    placement.entry_vertex_count = 0u;
    if( staged )
    {
        /* Pose, cull, projection, pick test and sort were done by the source
         * (the dual-core lane's worker, on its own scratch view of the
         * scene); this thread consumes. The pose the source applied is the
         * one the bakes below read -- its results were published after it. */
        if( stage.cull != TORIDRAW_CULL_VISIBLE )
            return;
        if( renderer->pick_enabled && command->pickable && command->element_id >= 0 &&
            stage.pick_hit )
            ToriRS_PickHitsAdd(
                &renderer->pick_hits,
                command->element_id,
                command->pick_terrain,
                command->pick_tile_x,
                command->pick_tile_z,
                command->pick_tile_level,
                command->pick_view);
        if( command->pick_only )
            return;
        /* The painter draws sorted faces and nothing else; an unsorted
         * stage here is the producer's bug, not a case. */
        assert(stage.sorted);
        face_count = stage.sorted_face_count;
        sorted_face_count = face_count;
        face_order = stage.sorted ? stage.face_order : NULL;
        projected_depth = stage.projected_depth;
        projected_in_scene = false;
    }
    else
    {
        if( !renderer->poses_prepared && command->animation && command->element_id >= 0 )
        {
            es3_apply_animation(renderer, command);
        }
        projected_position = command->position;
        if( ToriDraw_RenderModel1ProjectWithTable(
                command->model,
                renderer->scene,
                &projected_position,
                &renderer->current_3d.view_port,
                &renderer->current_3d.camera,
                renderer->kernel) != TORIDRAW_CULL_VISIBLE )
            return;

        if( renderer->pick_enabled && command->pickable && command->element_id >= 0 &&
            (command->pick_aabb
                 ? ToriDraw_ProjectedModelContainsAabb(
                       renderer->scene, renderer->pick_mouse_x, renderer->pick_mouse_y)
             : command->pick_terrain ? ToriDraw_ProjectedTileMouseHitTest(
                                           renderer->scene,
                                           command->model,
                                           &renderer->current_3d.view_port,
                                           renderer->pick_mouse_x,
                                           renderer->pick_mouse_y)
                                     : ToriDraw_ProjectedModelMouseHitTest(
                                           renderer->scene,
                                           command->model,
                                           &renderer->current_3d.view_port,
                                           renderer->pick_mouse_x,
                                           renderer->pick_mouse_y)) )
            ToriRS_PickHitsAdd(
                &renderer->pick_hits,
                command->element_id,
                command->pick_terrain,
                command->pick_tile_x,
                command->pick_tile_z,
                command->pick_tile_level,
                command->pick_view);
        if( command->pick_only )
            return;

        /* The painter must sort before it can count: the order it sorts
         * into IS the submission order. */
        face_count = es3p_sort_faces(renderer, command, &sorted_face_count);
        face_order = ToriDraw_FaceOrder(renderer->scene);
        projected_depth = renderer->scene->projected_vertex.z;
        projected_in_scene = true;
    }
    /* The sort census is a debug readout (TORIRS_ES3_DEBUG); it costs a
     * second trspk_toridraw_face_count per model, so it is gated where it
     * is gathered, not only where it is printed. */
    if( renderer->debug )
    {
        int model_faces = trspk_toridraw_face_count(command->model);
        int bucket = model_faces <= 2     ? 0
                     : model_faces <= 16  ? 1
                     : model_faces <= 64  ? 2
                     : model_faces <= 256 ? 3
                                          : 4;
        renderer->painter_stat_sort_models[bucket]++;
        renderer->painter_stat_sort_faces_in += (uint32_t)(model_faces > 0 ? model_faces : 0);
        renderer->painter_stat_sort_faces_out += (uint32_t)(face_count > 0 ? face_count : 0);
    }
    if( face_count <= 0 || (uint32_t)face_count > UINT32_MAX / 3u )
        return;
    dynamic = command->dynamic || command->element_id < 0;
    anim_index = es3_clampi(command->anim_index, 0, TRSPK_POSE_TRACK_COUNT - 1);
    pose_id = command->animation && command->anim_frame >= 0 ? command->anim_frame : 0;
    if( command->animation && command->animation->frame_count > 0 &&
        pose_id >= command->animation->frame_count )
        pose_id = 0;
    group = dynamic ? TRSPK_VBO_GROUP_DYNAMIC : TRSPK_VBO_GROUP_STATIC;
    binding = group;
    if( dynamic )
    {
        /* Painter path: an actor is baked straight into the frame stream in
         * its sorted face order, so it needs neither the dynamic arena nor an
         * index. The placement names the stream and the sorted count. */
        uint32_t first = es3_frame_stream_reserve(renderer, (uint32_t)sorted_face_count * 3u);
        if( !es3_bake_pose_vertices(
                renderer,
                renderer->frame_stream_cpu,
                &renderer->frame_stream_triangles,
                first,
                command->model,
                &command->world_position,
                face_order,
                sorted_face_count,
                /*ordered_painter=*/face_order != NULL) )
            return;
        placement.face_order = face_order;
        placement.projected_in_scene = projected_in_scene;
        placement.projected_depth = projected_depth;
        placement.binding = ES3_FRAME_STREAM_BINDING;
        placement.chunk_base = first;
        placement.absolute_base = first;
        placement.face_count = face_count;
        placement.sorted_face_count = sorted_face_count;
        placement.anim_index = anim_index;
        placement.pose_id = pose_id;
        placement.dynamic = true;
        es3p_emit_model(renderer, &placement);
        return;
    }
    if( (static_state = es3_static_resolve_recorded(
             renderer, command->element_id, anim_index, pose_id, &static_placement)) != 0 )
    {
        if( static_state < 0 )
            return;
        renderer->stat_static_page += 1.0;
        binding = ES3_STATIC_PAGE_BINDING;
        placement.page_id = static_placement.page_id;
        placement.batch_slot = static_placement.batch_slot;
        placement.entry_index = static_placement.entry_index;
        placement.entry_vertex_count = static_placement.vertex_count;
        vertex_base = static_placement.vertex_base;
        chunk_base = static_placement.vertex_base;
        absolute_base = static_placement.page_base + static_placement.vertex_base;
    }
    else if( !trspk_pose_table_get(
                 &renderer->poses, command->element_id, anim_index, pose_id, &vertex_base) )
    {
        renderer->stat_static_rebake += 1.0;
        /* A live static element without its load event (renderer creation,
         * an event-queue overflow): bake the complete animation once on the
         * first miss, not one pose every frame. */
        if( command->animation )
        {
            struct ToriRS_RenderCommand_AnimLoad load;
            memset(&load, 0, sizeof(load));
            load.element_id = command->element_id;
            load.anim_index = anim_index;
            load.animation = command->animation;
            load.model = command->model;
            load.world_position = command->world_position;
            es3p_animation_load(renderer, &load);
        }
        if( !trspk_pose_table_get(
                &renderer->poses, command->element_id, anim_index, pose_id, &vertex_base) )
            vertex_base = es3_bake_into_arena(
                renderer,
                &renderer->groups[group],
                command->element_id,
                anim_index,
                pose_id,
                command->model,
                &command->world_position,
                true);
    }
    if( vertex_base == UINT32_MAX )
        return;
    if( !dynamic && binding < ES3_STATIC_PAGE_BINDING )
        renderer->stat_static_pose_table += 1.0;

    /*
     * An arena's CPU copy IS its GPU buffer's contents, so the two bases are
     * the same number. A Batch16 chunk's CPU copy starts at the chunk's own
     * zero while the GPU buffer packs every chunk of every batch together,
     * so there the GPU base adds the chunk's page offset -- which the static
     * branch above has already worked out.
     *
     * Neither is split against a 64K page. The GLES2 renderer splits both,
     * because that is the only way a U16 index can name a vertex; here an
     * index reaches the whole buffer.
     */
    if( binding < ES3_STATIC_PAGE_BINDING )
    {
        chunk_base = vertex_base;
        absolute_base = vertex_base;
    }
    placement.binding = binding;
    placement.chunk_base = chunk_base;
    placement.absolute_base = absolute_base;
    placement.face_count = face_count;
    placement.sorted_face_count = sorted_face_count;
    placement.anim_index = anim_index;
    placement.pose_id = pose_id;
    placement.dynamic = dynamic;
    placement.face_order = face_order;
    placement.projected_in_scene = projected_in_scene;
    placement.projected_depth = projected_depth;
    es3p_emit_model(renderer, &placement);
}

void
es3p_end_3d(struct TRSPK_Renderer_ES3* renderer)
{
    assert(renderer);
    /* The prepared block describes a camera about to go out of scope;
     * unpublishing it is what stops a later pass reading a stale one. */
    if( renderer->scene )
        ToriDraw_SceneClearProjectionCamera(renderer->scene);
    if( !renderer->has_3d )
        goto done;
    if( !es3_upload_atlas(renderer) )
        goto done;
    /*
     * The painter path indexes the retained geometry IN PLACE, so all of
     * it has to be on the GPU -- not just the frame stream.
     *
     * This used to say "the frame stream is the only thing that has to go
     * up here", which was wrong in the one way that is invisible: a
     * static element whose pose is in a recorded batch page draws from
     * ES3_STATIC_PAGE_BINDING, but one that falls back to the model ARENA
     * draws from TRSPK_VBO_GROUP_STATIC -- and nothing on this path ever
     * uploaded that arena. es3_bind_stream then found no buffer for the
     * binding and RETURNED FALSE, which the draw loop answered with
     * `continue`: the item was dropped with no GL error and no log. So
     * those models were never drawn, and which models those are changes
     * as poses move between the pages and the arena -- geometry blinking
     * in and out.
     *
     * The GLES2 painter never needed this because it copies every model
     * into its resident ring; indexing the retained store in place is
     * exactly what the 32-bit index bought, and it buys the obligation to
     * upload that store with it. The depth path already did this.
     * @see the draw_audit lever, which is what found it.
     */
    if( !es3_upload_geometry(renderer) )
        goto done;
    es3_frame_stream_upload(renderer);
    es3p_sequence_draw(renderer);
    renderer->painter_stat_frames++;
    renderer->painter_stat_draws += renderer->draw_item_count;
    if( renderer->debug && renderer->painter_stat_frames == 300u )
    {
        es3_report_line(
            "opengles3 painter/frame: faces indexed %.0f actor %.0f; "
            "draws %.1f; static pages %u %u vertices; static models: page %.1f "
            "pose_table %.1f REBAKED %.1f",
            renderer->painter_stat_faces_indexed / 300.0,
            renderer->painter_stat_faces_actor / 300.0,
            renderer->painter_stat_draws / 300.0,
            renderer->static_page_count,
            renderer->static_batch_gpu_vertex_used,
            renderer->stat_static_page / 300.0,
            renderer->stat_static_pose_table / 300.0,
            renderer->stat_static_rebake / 300.0);
        es3_report_line(
            "opengles3 sort/frame: models by bake size tile2 %.0f <=16 %.0f <=64 %.0f <=256 %.0f "
            "larger %.0f; faces in %.0f out %.0f; radix shallow %.1f two-pass %.1f; "
            "prio uniform %.1f varied %.1f; k16 %.1f declined %.1f",
            renderer->painter_stat_sort_models[0] / 300.0,
            renderer->painter_stat_sort_models[1] / 300.0,
            renderer->painter_stat_sort_models[2] / 300.0,
            renderer->painter_stat_sort_models[3] / 300.0,
            renderer->painter_stat_sort_models[4] / 300.0,
            renderer->painter_stat_sort_faces_in / 300.0,
            renderer->painter_stat_sort_faces_out / 300.0,
            g_toridraw_radix_shallow_models / 300.0,
            g_toridraw_radix_two_pass_models / 300.0,
            g_toridraw_prio_uniform_models / 300.0,
            g_toridraw_prio_varied_models / 300.0,
            g_toridraw_sort_k16_models / 300.0,
            g_toridraw_sort_k16_declined / 300.0);
        es3_report_line(
            "webgl2 draws/frame: world %.1f; ui batches %.1f (ended by texture %.1f atlas "
            "%.1f scissor %.1f overflow %.1f asked %.1f) rotmask %.1f widget %.1f; ui "
            "upload %.0f B",
            renderer->painter_stat_draws / 300.0,
            renderer->ui_stat_draws_batch / 300.0,
            renderer->ui_stat_break_texture / 300.0,
            renderer->ui_stat_break_atlas / 300.0,
            renderer->ui_stat_break_scissor / 300.0,
            renderer->ui_stat_break_overflow / 300.0,
            ((double)renderer->ui_stat_draws_batch - renderer->ui_stat_break_texture -
             renderer->ui_stat_break_atlas - renderer->ui_stat_break_scissor -
             renderer->ui_stat_break_overflow) /
                300.0,
            renderer->ui_stat_draws_rotmask / 300.0,
            renderer->ui_stat_draws_widget / 300.0,
            renderer->ui_stat_upload_bytes / 300.0);
        es3_report_line(
            "project/frame: models %.1f cull_fast %.1f cull_aabb %.1f error %.1f projected "
            "%.1f vertices %.0f tail_models %.1f",
            g_toridraw_project_census.calls / 300.0,
            g_toridraw_project_census.cull_fast / 300.0,
            g_toridraw_project_census.cull_aabb / 300.0,
            g_toridraw_project_census.cull_error / 300.0,
            g_toridraw_project_census.projected / 300.0,
            g_toridraw_project_census.projected_vertices / 300.0,
            g_toridraw_project_census.tail_models / 300.0);
        es3_report_line(
            "paint/frame: walks %.2f same_inputs %.2f pops %.0f commands %.0f entities %.1f",
            g_torirs_paint_census.walks / 300.0,
            g_torirs_paint_census.same_inputs / 300.0,
            g_torirs_paint_census.pops / 300.0,
            g_torirs_paint_census.commands / 300.0,
            g_torirs_paint_census.entity_commands / 300.0);
        /* A call-site counting shim, when one was built in with -include
         * (scratch tooling; the symbol is absent in every normal build). */
        {
            extern void torirs_shim_dump(void) __attribute__((weak));
            if( torirs_shim_dump )
                torirs_shim_dump();
        }
    }
    if( renderer->painter_stat_frames >= 300u )
    {
        renderer->painter_stat_frames = 0u;
        renderer->stat_static_page = 0.0;
        renderer->stat_static_pose_table = 0.0;
        renderer->stat_static_rebake = 0.0;
        renderer->painter_stat_faces_indexed = 0u;
        renderer->painter_stat_faces_actor = 0u;
        renderer->painter_stat_draws = 0u;
        memset(
            renderer->painter_stat_sort_models, 0, sizeof(renderer->painter_stat_sort_models));
        renderer->painter_stat_sort_faces_in = 0u;
        renderer->painter_stat_sort_faces_out = 0u;
        g_toridraw_radix_shallow_models = 0;
        g_toridraw_radix_two_pass_models = 0;
        g_toridraw_prio_uniform_models = 0;
        g_toridraw_prio_varied_models = 0;
        g_toridraw_sort_k16_models = 0;
        g_toridraw_sort_k16_declined = 0;
        renderer->ui_stat_draws_batch = 0u;
        renderer->ui_stat_draws_rotmask = 0u;
        renderer->ui_stat_draws_widget = 0u;
        renderer->ui_stat_break_texture = 0u;
        renderer->ui_stat_break_atlas = 0u;
        renderer->ui_stat_break_scissor = 0u;
        renderer->ui_stat_break_overflow = 0u;
        renderer->ui_stat_upload_bytes = 0u;
        memset(&g_toridraw_project_census, 0, sizeof(g_toridraw_project_census));
        memset(&g_torirs_paint_census, 0, sizeof(g_torirs_paint_census));
    }

done:
    renderer->has_3d = false;
    renderer->in3d = false;
    es3_sequence_reset(renderer);
    /* The world pass leaves a world-sized viewport and depth state; restore
     * so 2D that follows is neither clipped nor occluded. */
    es3_set_letterbox_viewport(renderer);
    es3_set_depth(renderer, false, false);
    es3_set_cull(renderer, false);
    es3_set_scissor(renderer, NULL);
}

/* ---- the retained model store ------------------------------------------------------- */

static void
es3p_model_load(
    struct TRSPK_Renderer_ES3* renderer,
    const struct ToriRS_RenderCommand_ModelLoad* command)
{
    assert(renderer);
    assert(command);
    if( command->element_id < 0 || command->model.kind == TORIDRAWMK_NONE )
        return;
    /* A model replacement invalidates every pose from the old geometry. */
    (void)es3_model_unload(renderer, command->element_id);
    (void)es3_bake_into_arena(
        renderer,
        &renderer->groups[TRSPK_VBO_GROUP_STATIC],
        command->element_id,
        0,
        0,
        command->model,
        &command->world_position,
        true);
}

static void
es3p_animation_load(
    struct TRSPK_Renderer_ES3* renderer,
    const struct ToriRS_RenderCommand_AnimLoad* command)
{
    int anim_index = 0;
    int frame;

    assert(renderer);
    assert(command);
    if( !es3_animation_load_check(command, &anim_index) )
        return;
    /* Pose keys do not carry a sequence id. Clear the old track so frame zero
     * cannot keep resolving to MODEL_LOAD's rest pose and a shorter
     * replacement cannot serve stale tail frames. */
    (void)es3_animation_track_unload(renderer, command->element_id, anim_index);
    for( frame = 0; frame < command->animation->frame_count; frame++ )
    {
        struct ToriDraw_Model* baked = es3_animation_pose_frame(command, frame);
        struct ToriDraw_ModelHandle handle;
        memset(&handle, 0, sizeof(handle));
        handle.kind = TORIDRAWMK_MODEL;
        handle.u.model.model = baked;
        (void)es3_bake_into_arena(
            renderer,
            &renderer->groups[TRSPK_VBO_GROUP_STATIC],
            command->element_id,
            anim_index,
            frame,
            handle,
            &command->world_position,
            true);
        ToriDraw_ModelFree(baked);
    }
}

/* ---- batch commands ----------------------------------------------------------------- */

static void
es3p_batch_begin(
    struct TRSPK_Renderer_ES3* renderer,
    const struct ToriRS_RenderCommand_Batch* command)
{
    struct ES3StaticBatch* batch;
    int slot;
    assert(renderer);
    assert(command);
    if( command->batch_id < 0 )
        return;
    slot = es3_static_batch_slot(renderer, command->batch_id, true);
    if( slot < 0 )
        return;
    batch = &renderer->static_batches[slot];
    es3_invalidate_batch_pages(renderer, batch);
    trspk_batch16_begin(batch->cpu);
    batch->active = false;
    batch->building = true;
    es3_rebuild_batch_pose_table(renderer);
    renderer->current_batch_slot = slot;
}


static void
es3p_batch_add(
    struct TRSPK_Renderer_ES3* renderer,
    const struct ToriRS_RenderCommand_Batch* command,
    bool animated)
{
    struct ES3StaticBatch* batch;
    struct TRSPK_Batch16Reservation reservation;
    int anim_index;
    int pose_id;
    int face_count;
    assert(renderer);
    assert(command);
    if( command->element_id < 0 || command->model.kind == TORIDRAWMK_NONE )
        return;
    if( renderer->current_batch_slot < 0 ||
        (uint32_t)renderer->current_batch_slot >= renderer->static_batch_count )
        return;
    batch = &renderer->static_batches[renderer->current_batch_slot];
    if( !batch->building || batch->batch_id != command->batch_id )
        return;
    face_count = trspk_toridraw_face_count(command->model);
    if( face_count <= 0 || (uint32_t)face_count > UINT32_MAX / 3u )
        return;
    anim_index = animated ? es3_clampi(command->anim_index, 0, TRSPK_POSE_TRACK_COUNT - 1) : 0;
    pose_id = command->pose_id >= 0 ? command->pose_id : 0;
    if( !trspk_batch16_reserve_pose(
            batch->cpu,
            command->element_id,
            anim_index,
            pose_id,
            (uint32_t)face_count * 3u,
            &reservation) )
        return;
    (void)es3_bake_pose_vertices(
        renderer,
        reservation.vbo,
        reservation.triangles,
        reservation.vertex_base,
        command->model,
        &command->world_position,
        NULL,
        0,
        false);
}


static void
es3p_batch_clear(
    struct TRSPK_Renderer_ES3* renderer,
    int batch_id,
    bool clear_all)
{
    uint32_t slot;
    assert(renderer);
    for( slot = 0u; slot < renderer->static_batch_count; slot++ )
    {
        struct ES3StaticBatch* batch = &renderer->static_batches[slot];
        if( !clear_all && batch->batch_id != batch_id )
            continue;
        es3_invalidate_batch_pages(renderer, batch);
        trspk_batch16_clear(batch->cpu);
        batch->active = false;
        batch->building = false;
    }
    if( clear_all )
    {
        /* Nothing valid remains, so the bump allocator starts over: every
         * page re-allocates its range the next time its chunk commits. */
        uint32_t page_id;
        for( page_id = 0u; page_id < renderer->static_page_count; page_id++ )
            renderer->static_pages[page_id].gpu_capacity = 0u;
        renderer->static_batch_gpu_vertex_used = 0u;
    }
    es3_rebuild_batch_pose_table(renderer);
    renderer->current_batch_slot = -1;
}


/* ---- dispatch ------------------------------------------------------------------------ */

static void
es3p_dispatch(
    struct TRSPK_Renderer_ES3* renderer,
    const struct ToriRS_RenderCommand* command)
{
    assert(renderer);
    assert(command);
    switch( command->kind )
    {
    case TORIRSRC_BEGIN_3D:
        es3p_begin_3d(renderer, &command->u.begin_3d);
        break;
    case TORIRSRC_END_3D:
        es3p_end_3d(renderer);
        break;
    case TORIRSRC_BEGIN_2D:
        es3_begin_2d(renderer);
        break;
    case TORIRSRC_END_2D:
        es3_end_2d(renderer);
        break;
    case TORIRSRC_TEX_LOAD:
        if( command->u.tex_load.texture )
            (void)es3_load_texture_object(
                renderer, command->u.tex_load.texture_id, command->u.tex_load.texture);
        break;
    case TORIRSRC_TEX_UNLOAD:
        es3_unload_texture(renderer, command->u.tex_load.texture_id);
        break;
    case TORIRSRC_MODEL_LOAD:
        es3p_model_load(renderer, &command->u.model_load);
        break;
    case TORIRSRC_MODEL_UNLOAD:
        (void)es3_model_unload(renderer, command->u.model_load.element_id);
        break;
    case TORIRSRC_ANIM_LOAD:
        es3p_animation_load(renderer, &command->u.anim_load);
        break;
    case TORIRSRC_ANIM_UNLOAD:
        (void)es3_animation_track_unload(
            renderer, command->u.anim_load.element_id, command->u.anim_load.anim_index);
        break;
    case TORIRSRC_BATCH3D_BEGIN:
        es3p_batch_begin(renderer, &command->u.batch);
        break;
    case TORIRSRC_BATCH3D_MODEL_ADD:
        es3p_batch_add(renderer, &command->u.batch, false);
        break;
    case TORIRSRC_BATCH3D_ANIM_ADD:
        es3p_batch_add(renderer, &command->u.batch, true);
        break;
    case TORIRSRC_BATCH3D_END:
        es3_batch_end(renderer, &command->u.batch);
        break;
    case TORIRSRC_BATCH3D_CLEAR:
        es3p_batch_clear(renderer, command->u.batch.batch_id, command->u.batch.clear_all);
        if( command->u.batch.clear_all )
        {
            trspk_pose_table_clear(&renderer->poses);
            es3_reset_group(&renderer->groups[TRSPK_VBO_GROUP_STATIC]);
        }
        break;
    case TORIRSRC_DRAW_MODEL:
        es3p_draw_model(renderer, &command->u.model);
        break;

    case TORIRSRC_CLEAR_RECT:
        es3_ui_draw_clear_rect(renderer, &command->u.clear_rect);
        break;
    case TORIRSRC_FILL_RECT:
        es3_ui_draw_fill_rect(renderer, &command->u.fill_rect);
        break;
    case TORIRSRC_DRAW_MODEL_WIDGET:
        es3_ui_draw_model_widget(renderer, &command->u.model_widget);
        break;
    case TORIRSRC_SPRITE:
        es3_ui_draw_sprite(renderer, &command->u.sprite);
        break;
    case TORIRSRC_FONT:
        es3_ui_draw_font(renderer, &command->u.font);
        break;
    case TORIRSRC_LINE:
        es3_ui_draw_line(renderer, &command->u.line);
        break;
    case TORIRSRC_POLYGON_BEGIN:
        es3_ui_polygon_begin(renderer, &command->u.polygon_begin);
        break;
    case TORIRSRC_POLYGON_POINT:
        es3_ui_polygon_point(renderer, &command->u.polygon_point);
        break;
    case TORIRSRC_POLYGON_END:
        es3_ui_polygon_end(renderer);
        break;
    case TORIRSRC_TEX_BEGIN:
    case TORIRSRC_TEX_END:
    case TORIRSRC_SPRITE_BEGIN:
    case TORIRSRC_SPRITE_END:
    case TORIRSRC_FONT_BEGIN:
    case TORIRSRC_FONT_END:
        break;
    case TORIRSRC_SPRITE_LOAD:
        /* The scene owns pixels. Upload stays lazy so assets never drawn by
         * this backend consume atlas space or transfer bandwidth. */
        break;
    case TORIRSRC_SPRITE_UNLOAD:
        es3_ui_sprite_invalidate(renderer, command->u.sprite_load.element_id);
        break;
    case TORIRSRC_FONT_LOAD:
        es3_ui_font_load(renderer, command->u.font_load.font_id, command->u.font_load.font);
        break;
    case TORIRSRC_FONT_UNLOAD:
        es3_ui_font_unload(renderer, command->u.font_load.font_id);
        break;
    case TORIRSRC_NONE:
        break;
    }
}


/* ---- the frame ---------------------------------------------------------------------- */

/*
 * The surface, then the framebuffer this renderer draws into, then the
 * clear. Two toolkit calls with this renderer's own two lines between them.
 */
static bool
es3p_begin_frame(
    struct TRSPK_Renderer_ES3* renderer,
    bool clear_to_black_only,
    bool allow_offscreen)
{
    if( !es3_frame_surface_begin(renderer, allow_offscreen) )
        return false;
    if( renderer->target_offscreen )
    {
        es3_scale_target_ensure(renderer);
        glBindFramebuffer(GL_FRAMEBUFFER, renderer->scale_fbo);
    }
    else
    {
        /* The limit is off or no longer binds: give the memory back now. */
        es3_scale_target_destroy_buffers(renderer);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    es3_frame_surface_clear(renderer, clear_to_black_only);
    return true;
}

void
TRSPK_Renderer_ES3_PainterDrawBootBar(
    struct TRSPK_Renderer_ES3* renderer,
    int progress,
    int caption_font_id,
    char const* caption)
{
    assert(renderer);
    /* progress < 0: clear only, no bar -- the post-login loading screen,
     * which is a black screen and the sentence alone on every lane. */
    if( !es3p_begin_frame(renderer, progress < 0, false) )
        return;
    if( progress >= 0 )
    {
        int bar_x;
        int bar_y;
        int fill_w;
        progress = es3_clampi(progress, 0, 100);
        /* The references' bar, not one of ours (engine/boot_bar.h): a filled
         * red track, a black inset one pixel in, then the fill two pixels in. */
        bar_x = renderer->width / 2 - BOOT_BAR_W / 2;
        bar_y = renderer->height / 2 - BOOT_BAR_ABOVE_CENTRE;
        fill_w = progress * BOOT_BAR_PX_PER_PERCENT;
        es3_draw_solid_rect(
            renderer, bar_x, bar_y, BOOT_BAR_W, BOOT_BAR_H, 0xff000000u | BOOT_BAR_COLOR);
        es3_draw_solid_rect(
            renderer, bar_x + 1, bar_y + 1, BOOT_BAR_W - 2, BOOT_BAR_H - 2, 0xff000000u);
        if( fill_w > 0 )
            es3_draw_solid_rect(
                renderer,
                bar_x + BOOT_BAR_INSET,
                bar_y + BOOT_BAR_INSET,
                fill_w,
                BOOT_BAR_FILL_H,
                0xff000000u | BOOT_BAR_COLOR);
    }
    if( caption && caption[0] && caption_font_id >= 0 )
        es3_draw_boot_caption(renderer, caption_font_id, caption);
}

bool
es3p_render_frame_begin(struct TRSPK_Renderer_ES3* renderer)
{
    assert(renderer);
    if( !es3p_begin_frame(renderer, false, true) )
        return false;
    renderer->has_3d = false;
    renderer->in3d = false;
    renderer->in2d = false;
    renderer->frame_clock += 1.0;
    return true;
}

void
es3p_render_frame_commands(
    struct TRSPK_Renderer_ES3* renderer,
    struct ToriRS_Frame* frame)
{
    struct ToriRS_RenderCommand command;
    assert(renderer);
    assert(frame);
    while( ToriRS_FrameNextCommand(frame, &command) )
    {
        es3_prefetch_ahead(renderer, frame);
        es3p_dispatch(renderer, &command);
    }
}

void
es3p_render_frame_end(struct TRSPK_Renderer_ES3* renderer)
{
    assert(renderer);
#if defined(TORIRS_ANIM_CHAIN_CAPTURE)
    ToriDraw_AnimCaptureEndPass();
#endif
#if defined(TORIRS_PLACEMENT_CAPTURE)
    es3_placement_capture_end();
#endif
    if( renderer->in3d )
        es3p_end_3d(renderer);
    if( renderer->in2d )
        es3_end_2d(renderer);
    if( renderer->target_offscreen )
        es3_scale_target_present(renderer);

    es3_frame_readback(renderer);
}

void
TRSPK_Renderer_ES3_PainterExecute(
    struct TRSPK_Renderer_ES3* renderer,
    struct ToriRS_RenderCommand const* command)
{
    es3p_dispatch(renderer, command);
}

void
TRSPK_Renderer_ES3_PainterRenderFrame(
    struct TRSPK_Renderer_ES3* renderer,
    struct ToriRS_Frame* frame)
{
    assert(renderer);
    assert(frame);
    if( !es3p_render_frame_begin(renderer) )
        return;
    ToriRS_FrameBegin(frame);
    es3p_render_frame_commands(renderer, frame);
    ToriRS_FrameEnd(frame);
    es3p_render_frame_end(renderer);
}


/* ---- lifetime ------------------------------------------------------------------------ */

/*
 * No depth bits: this path never reads or writes depth, and asking for a
 * buffer it will not use costs bandwidth on a tiler for nothing.
 */
bool
TRSPK_Renderer_ES3_PainterInit(
    struct TRSPK_Renderer_ES3* renderer,
    ToriPlatform_GLWindow* window,
    struct ToriDraw_Scene* scene)
{
    assert(renderer);
    assert(window);
    assert(scene);
    return es3_init_gl(renderer, window, scene, 0, "painter");
}
