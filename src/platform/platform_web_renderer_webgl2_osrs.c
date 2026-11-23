/**
 * The OSRS z-buffer renderer for WebGL2 -- a WHOLE renderer, the third beside
 * the painter (platform_web_renderer_webgl2_painter.c) and the depth renderer
 * (platform_web_renderer_webgl2_zbuffer.c).
 *
 * It draws the world the way the rev-241 client does under `::zbuf 1`
 * (docs/toridraw_rszbuffer.md), which the software renderer "Software (OSRS
 * z-buffer)" reproduces exactly. On the GPU the point is the depth buffer's
 * RESULT rather than the client's rasterizer, and the four things the plain
 * depth renderer lacks are what make OSRS content z-fight there:
 *
 *   the depth key   es3_world_osrs_vertex_shader keys every corner at its
 *                   camera depth less twice its face's depth bias
 *                   (face_z_offsets, carried by the OSRS vertex,
 *                   es3_osrs_vertex.h), with the client's formula, so a decal
 *                   authored on a surface wins that surface everywhere.
 *   the tie rule    the buffer clears to 0 and the test is GL_GREATER: an
 *                   equal key keeps what is there, as the client's does.
 *   two-pass        every player and every npc whose type carries opcode 147
 *                   (TORIDRAW_MODEL_FLAG_RSZBUF_TWO_PASS) draws its faces in
 *                   render-priority order writing colour only, then the same
 *                   faces writing depth only: the painter's rules layer the
 *                   model against itself, the depth buffer against the world.
 *   translucency    after the opaque pass, every model with blended faces in
 *                   the order the world arrived in (the painter's: the app
 *                   collects TORIRS_WORLD_PAINTER for this renderer), its
 *                   blended faces far to near by depth bucket and NO render
 *                   priorities, blending and WRITING depth. A two-pass model's
 *                   blended faces go colour then depth here too.
 *
 * Everything else is the depth renderer's, composed rather than copied: the
 * depth world (its material tables, the opaque array ranges and index
 * buckets, every retained-store command) through the es3_zbuffer_* pieces in
 * es3_core.h. What this file owns is the world pass: begin_3d, the draw, the
 * per-pass queues of two-pass models and translucent runs, end_3d and the
 * issue loop that sets the write masks.
 *
 * It needs the OSRS vertex, so its handle is made by
 * TRSPK_Renderer_ES3_NewOsrs, not TRSPK_Renderer_ES3_New.
 */

#include "es3/es3_core.h"

#include "log/torirs_log.h"
#include "mem/torirs_mem.h"
#include "perf/torirs_perf.h"
#include "platform/platform_window.h"
#include "toridraw.h"
#include "toridraw_element_id.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "core/trspk_math.h"
#include "toridraw_math.h"

/*
 * One queued run of a model's faces, as indices into the binding it was
 * baked into, in the order they draw.
 *
 *   two_pass   drawn twice: colour only, then depth only. Every run of a
 *              two-pass model; a translucent run of any other model is drawn
 *              once, colour and depth.
 */
struct ES3OsrsRun
{
    uint32_t binding;
    uint32_t vertex_min;
    uint32_t vertex_max;
    uint32_t index_offset;
    uint32_t index_count;
    bool two_pass;
};

struct ES3OsrsRunList
{
    struct ES3OsrsRun* runs;
    uint32_t count;
    uint32_t capacity;
};

/* A blended face and its depth bucket, for the client's sorted order. */
struct ES3OsrsFaceKey
{
    int bucket;
    int face;
};

struct ES3OsrsWorld
{
    /* Both lists' indices, in one array. */
    uint32_t* indices;
    uint32_t index_count;
    uint32_t index_capacity;
    /* Two-pass models' opaque and cutout faces, drawn after the opaque
     * batches and before anything translucent. */
    struct ES3OsrsRunList opaque_two_pass;
    /* Blended faces, every model's, in arrival order. */
    struct ES3OsrsRunList translucent;

    struct ES3OsrsFaceKey* face_keys;
    uint32_t face_key_capacity;

    /* This pass keys depth the client's way (a perspective camera); false
     * for a parallel one, which draws through the matrix's depth row with
     * the standard test instead. */
    bool key_depth;
};

static struct ES3OsrsWorld*
es3o_state(struct TRSPK_Renderer_ES3* renderer)
{
    assert(renderer);
    assert(renderer->osrs);
    return renderer->osrs;
}

/* --- lifetime ------------------------------------------------------------------ */

static void
es3o_create(struct TRSPK_Renderer_ES3* renderer)
{
    assert(renderer);
    assert(!renderer->osrs);
    renderer->osrs =
        (struct ES3OsrsWorld*)ToriMem_Calloc(ToriMem_Renderer(), 1u, sizeof(struct ES3OsrsWorld));
    assert(renderer->osrs);
}

void
es3_osrs_destroy(struct TRSPK_Renderer_ES3* renderer)
{
    struct ES3OsrsWorld* world;
    if( !renderer || !renderer->osrs )
        return;
    world = renderer->osrs;
    ToriMem_Free(ToriMem_Renderer(), world->indices);
    ToriMem_Free(ToriMem_Renderer(), world->opaque_two_pass.runs);
    ToriMem_Free(ToriMem_Renderer(), world->translucent.runs);
    ToriMem_Free(ToriMem_Renderer(), world->face_keys);
    ToriMem_Free(ToriMem_Renderer(), world);
    renderer->osrs = NULL;
}

static void
es3o_reset_pass(struct ES3OsrsWorld* world)
{
    assert(world);
    world->index_count = 0u;
    world->opaque_two_pass.count = 0u;
    world->translucent.count = 0u;
}

/* --- the queues ---------------------------------------------------------------- */

static void
es3o_queue_run(
    struct ES3OsrsWorld* world,
    struct ES3OsrsRunList* list,
    const struct ES3ModelPlacement* placement,
    bool two_pass,
    const uint32_t* indices,
    uint32_t index_count)
{
    struct ES3OsrsRun* run;
    uint32_t needed;

    assert(world);
    assert(list);
    assert(placement);
    assert(indices);
    if( index_count == 0u )
        return;
    needed = world->index_count + index_count;
    if( needed > world->index_capacity )
    {
        uint32_t capacity = world->index_capacity ? world->index_capacity : 1024u;
        uint32_t* grown;
        while( capacity < needed )
            capacity *= 2u;
        grown = (uint32_t*)ToriMem_Realloc(
            ToriMem_Renderer(), world->indices, (size_t)capacity * sizeof(*grown));
        assert(grown);
        world->indices = grown;
        world->index_capacity = capacity;
    }
    if( list->count == list->capacity )
    {
        uint32_t capacity = list->capacity ? list->capacity * 2u : 64u;
        struct ES3OsrsRun* grown = (struct ES3OsrsRun*)ToriMem_Realloc(
            ToriMem_Renderer(), list->runs, (size_t)capacity * sizeof(*grown));
        assert(grown);
        list->runs = grown;
        list->capacity = capacity;
    }
    memcpy(world->indices + world->index_count, indices, (size_t)index_count * sizeof(*indices));
    run = &list->runs[list->count++];
    run->binding = placement->binding;
    run->vertex_min = placement->absolute_base;
    run->vertex_max = placement->absolute_base + (uint32_t)placement->face_count * 3u - 1u;
    run->index_offset = world->index_count;
    run->index_count = index_count;
    run->two_pass = two_pass;
    world->index_count = needed;
}

/* The face's three indices, appended. Faces are baked in index order on this
 * path (face f at absolute_base + 3f), as the depth renderer bakes them. */
static inline uint32_t
es3o_write_face(
    uint32_t* destination,
    uint32_t written,
    const struct ES3ModelPlacement* placement,
    uint32_t face)
{
    uint32_t const base = placement->absolute_base + face * 3u;
    destination[written] = base;
    destination[written + 1u] = base + 1u;
    destination[written + 2u] = base + 2u;
    return written + 3u;
}

/* The scene's bench must hold this model's projection before a sort reads
 * it: the draw projects only for a pick under the cursor. */
static bool
es3o_ensure_projected(
    struct TRSPK_Renderer_ES3* renderer,
    const struct ToriRS_RenderCommand_Model* command,
    const struct ES3ModelPlacement* placement)
{
    struct ToriDraw_Position position;

    assert(renderer);
    assert(command);
    assert(placement);
    if( placement->projected_in_scene )
        return true;
    if( placement->staged )
        renderer->stage_reprojected_models++;
    position = command->position;
    return ToriDraw_RenderModel1ProjectWithTable(
               command->model,
               renderer->scene,
               &position,
               &renderer->current_3d.view_port,
               &renderer->current_3d.camera,
               renderer->kernel) == TORIDRAW_CULL_VISIBLE;
}

static int
es3o_compare_face_key(const void* lhs, const void* rhs)
{
    const struct ES3OsrsFaceKey* a = (const struct ES3OsrsFaceKey*)lhs;
    const struct ES3OsrsFaceKey* b = (const struct ES3OsrsFaceKey*)rhs;
    /* The client walks its buckets from the farthest down, each in face
     * index order [class174.method5697]. */
    if( a->bucket != b->bucket )
        return a->bucket > b->bucket ? -1 : 1;
    return a->face < b->face ? -1 : (a->face > b->face ? 1 : 0);
}

/*
 * A model's blended faces in the client's "sorted" order: by the bucket of
 * their corners' average depth, far to near, index order within a bucket, and
 * no render priorities [walk.rszbuf.u.c rszbuf_bucket_faces / draw_by_depth].
 * Needs the model's projection on the scene's bench. Back faces are left in:
 * the GPU culls them.
 */
static uint32_t
es3o_sorted_blended_indices(
    struct TRSPK_Renderer_ES3* renderer,
    struct ES3OsrsWorld* world,
    const struct ToriRS_RenderCommand_Model* command,
    const struct ES3ModelPlacement* placement,
    const struct ES3MaterialPose* material)
{
    const struct ToriDraw_Model* model;
    const int* depth;
    uint32_t key_count = 0u;
    uint32_t written = 0u;

    assert(ToriDraw_ModelKindIsFull(command->model.kind));
    model = ToriDraw_ModelRead(command->model);
    assert(model);
    depth = renderer->scene->screen_vertices_z;
    if( material->blended_count > world->face_key_capacity )
    {
        uint32_t capacity = world->face_key_capacity ? world->face_key_capacity : 256u;
        struct ES3OsrsFaceKey* grown;
        while( capacity < material->blended_count )
            capacity *= 2u;
        grown = (struct ES3OsrsFaceKey*)ToriMem_Realloc(
            ToriMem_Renderer(), world->face_keys, (size_t)capacity * sizeof(*grown));
        assert(grown);
        world->face_keys = grown;
        world->face_key_capacity = capacity;
    }
    for( uint32_t face = 0u; face < material->face_count; face++ )
    {
        struct ES3OsrsFaceKey* key;
        if( material->face_passes[face] != ES3_WORLD_FACE_BLENDED )
            continue;
        assert(key_count < material->blended_count);
        key = &world->face_keys[key_count++];
        key->face = (int)face;
        key->bucket = (depth[model->face_indices_a[face]] + depth[model->face_indices_b[face]] +
                       depth[model->face_indices_c[face]]) /
                      3;
    }
    qsort(world->face_keys, key_count, sizeof(world->face_keys[0]), es3o_compare_face_key);
    for( uint32_t i = 0u; i < key_count; i++ )
        written = es3o_write_face(
            renderer->model_indices, written, placement, (uint32_t)world->face_keys[i].face);
    return written;
}

/*
 * A two-pass model: every face in render-priority order -- the painter's face
 * sort, back faces dropped -- split into the opaque run (opaque and cutout
 * faces) and the translucent one (blended faces), each keeping that order.
 */
static void
es3o_emit_two_pass(
    struct TRSPK_Renderer_ES3* renderer,
    struct ES3OsrsWorld* world,
    const struct ToriRS_RenderCommand_Model* command,
    const struct ES3ModelPlacement* placement,
    const struct ES3MaterialPose* material)
{
    const int* face_order;
    int sorted_face_count;
    uint32_t written;

    if( placement->face_order )
    {
        face_order = placement->face_order;
        sorted_face_count = placement->sorted_face_count;
    }
    else
    {
        if( !es3o_ensure_projected(renderer, command, placement) )
            return;
        sorted_face_count =
            ToriDraw_RenderModel2SortFacesWithTable(command->model, renderer->scene, renderer->kernel);
        face_order = ToriDraw_FaceOrder(renderer->scene);
    }
    if( sorted_face_count <= 0 )
        return;

    /* model_indices holds face_count * 3 (es3_zbuffer_place_model reserved
     * it), and each pass below writes at most that. */
    written = 0u;
    for( int i = 0; i < sorted_face_count; i++ )
    {
        uint8_t pass;
        if( face_order[i] < 0 || (uint32_t)face_order[i] >= material->face_count )
            continue;
        pass = material->face_passes[face_order[i]];
        if( pass == ES3_WORLD_FACE_OPAQUE || pass == ES3_WORLD_FACE_CUTOUT )
            written = es3o_write_face(renderer->model_indices, written, placement, (uint32_t)face_order[i]);
    }
    es3o_queue_run(world, &world->opaque_two_pass, placement, true, renderer->model_indices, written);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_Z_OPAQUE_TRIANGLES, written / 3u);

    if( material->blended_count == 0u )
        return;
    written = 0u;
    for( int i = 0; i < sorted_face_count; i++ )
    {
        if( face_order[i] < 0 || (uint32_t)face_order[i] >= material->face_count )
            continue;
        if( material->face_passes[face_order[i]] == ES3_WORLD_FACE_BLENDED )
            written = es3o_write_face(renderer->model_indices, written, placement, (uint32_t)face_order[i]);
    }
    es3o_queue_run(world, &world->translucent, placement, true, renderer->model_indices, written);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_Z_BLENDED_TRIANGLES, written / 3u);
}

static void
es3o_emit_model(
    struct TRSPK_Renderer_ES3* renderer,
    const struct ToriRS_RenderCommand_Model* command,
    const struct ES3ModelPlacement* placement)
{
    struct ES3OsrsWorld* world = es3o_state(renderer);
    const struct ES3MaterialPose* material;
    uint32_t written;

    assert(command);
    assert(placement);
    material = es3_zbuffer_material(renderer, command, placement);
    if( !material )
        return;

    if( ToriDraw_ModelKindIsFull(command->model.kind) &&
        (ToriDraw_ModelRead(command->model)->flags & TORIDRAW_MODEL_FLAG_RSZBUF_TWO_PASS) )
    {
        es3o_emit_two_pass(renderer, world, command, placement, material);
        return;
    }

    /* Model order in the client's opaque pass: the depth buffer decides, so
     * the depth world's batches serve as they are. */
    es3_zbuffer_queue_opaque(renderer, placement, material);
    if( material->blended_count == 0u )
        return;
    /* Only a full model carries face alpha; a tile is never blended. */
    assert(ToriDraw_ModelKindIsFull(command->model.kind));
    if( !es3o_ensure_projected(renderer, command, placement) )
        return;
    written = es3o_sorted_blended_indices(renderer, world, command, placement, material);
    es3o_queue_run(world, &world->translucent, placement, false, renderer->model_indices, written);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_Z_SORTED_MODELS, 1);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_Z_BLENDED_TRIANGLES, written / 3u);
}

/* Push one list's runs onto the draw sequence. A two-pass run goes on twice,
 * colour only then depth only, before the next run: the client finishes a
 * model before it starts the next. */
static void
es3o_push_runs(
    struct TRSPK_Renderer_ES3* renderer,
    struct ES3OsrsWorld* world,
    const struct ES3OsrsRunList* list,
    bool blended)
{
    for( uint32_t i = 0u; i < list->count; i++ )
    {
        const struct ES3OsrsRun* run = &list->runs[i];
        const uint32_t* indices = world->indices + run->index_offset;
        /* The cutout program for every run: a run keeps its draw order, so
         * it cannot be split by program, and the cutout program draws an
         * opaque face exactly as the plain one does. */
        if( run->two_pass )
        {
            es3_sequence_push_indexed_writes(
                renderer, run->binding, run->vertex_min, run->vertex_max, true, blended,
                ES3_WRITES_COLOR, indices, run->index_count);
            es3_sequence_push_indexed_writes(
                renderer, run->binding, run->vertex_min, run->vertex_max, true, blended,
                ES3_WRITES_DEPTH, indices, run->index_count);
        }
        else
            es3_sequence_push_indexed_writes(
                renderer, run->binding, run->vertex_min, run->vertex_max, true, blended,
                ES3_WRITES_COLOR_DEPTH, indices, run->index_count);
    }
}

/* --- the issue loop -------------------------------------------------------------- */

static void
es3o_set_color_writes(bool* current, bool enabled)
{
    if( *current == enabled )
        return;
    glColorMask(enabled, enabled, enabled, enabled);
    *current = enabled;
}

static void
es3o_sequence_issue(
    struct TRSPK_Renderer_ES3* renderer,
    uint32_t index_base_bytes)
{
    struct ES3OsrsWorld* world = es3o_state(renderer);
    uint32_t draw_calls = 0u;
    int program_cutout = -1;
    int blend_on = -1;
    int depth_write = -1;
    bool color_writes = true;
    GLuint element_buffer = renderer->index_stream.buffers[renderer->frame_slot];

    renderer->world_block_osrs_depth = world->key_depth ? 1.0f : 0.0f;
    es3_world_block_upload(renderer);
    /* The depth renderer's cull and depth-test enable, then this renderer's
     * test: strictly nearer keys, or the standard test for a parallel pass. */
    es3_zbuffer_apply_world_states(renderer);
    glDepthFunc(world->key_depth ? GL_GREATER : GL_LEQUAL);

    for( uint32_t item_index = 0u; item_index < renderer->draw_item_count; item_index++ )
    {
        const struct ES3DrawItem* item = &renderer->draw_items[item_index];
        /* Translucent faces write depth here, unlike the depth renderer's:
         * the client's sorted pass does. Only a colour-only draw does not. */
        int const wants_depth_write = item->writes != ES3_WRITES_COLOR;
        if( blend_on != (int)item->blended )
        {
            es3_set_blend(renderer, item->blended != 0u);
            blend_on = (int)item->blended;
        }
        if( depth_write != wants_depth_write )
        {
            es3_set_depth(renderer, true, wants_depth_write != 0);
            depth_write = wants_depth_write;
        }
        es3o_set_color_writes(&color_writes, item->writes != ES3_WRITES_DEPTH);
        if( program_cutout != (int)item->cutout )
        {
            es3_use_world_program(renderer, item->cutout != 0u);
            program_cutout = (int)item->cutout;
        }
        if( !es3_bind_stream(renderer, item->binding) )
            continue;
        if( item->indexed )
        {
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
                (const void*)(uintptr_t)(index_base_bytes + (size_t)item->first * sizeof(uint32_t)));
        }
        else
            glDrawArrays(GL_TRIANGLES, (GLint)item->first, (GLsizei)item->count);
        draw_calls++;
    }
    es3o_set_color_writes(&color_writes, true);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DRAW_CALLS, draw_calls);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DRAW_RANGES, renderer->draw_item_count);
}

static void
es3o_sequence_draw(struct TRSPK_Renderer_ES3* renderer)
{
    uint32_t index_base_bytes = 0u;

    assert(renderer);
    if( renderer->draw_item_count == 0u )
        return;
    if( renderer->ibo_staging_count > 0u )
    {
        uint32_t bytes = renderer->ibo_staging_count * (uint32_t)sizeof(uint32_t);
        /* Uploaded against the default VAO; the issue loop attaches the
         * buffer to each VAO it draws from, as the depth renderer's does. */
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
    es3o_sequence_issue(renderer, index_base_bytes);
}

/* --- the world pass ---------------------------------------------------------------- */

static void
es3o_begin_3d(
    struct TRSPK_Renderer_ES3* renderer,
    const struct ToriRS_RenderCommand_Begin3D* command)
{
    struct ES3OsrsWorld* world = es3o_state(renderer);
    const struct ToriDraw_ViewPort* viewport;
    int pass_w;
    int pass_h;
    int logical_x;
    int logical_y;
    int left;
    int top;
    int right;
    int bottom;

    assert(command);
    if( !renderer->gl_context )
        return;
    es3_sequence_reset(renderer);
    es3o_reset_pass(world);
    renderer->current_3d = *command;
    renderer->has_3d = true;
    renderer->in3d = true;
    if( renderer->scene )
        ToriDraw_ScenePrepareProjectionCamera(renderer->scene, &renderer->current_3d.camera);
    if( renderer->model_stage_source && renderer->model_stage_source->begin_3d )
        renderer->model_stage_source->begin_3d(renderer->model_stage_source->user, command);

    /* The client has no parallel world camera; a parallel pass (an editor
     * view) has no depth in w to key, and draws the standard way. */
    world->key_depth = command->camera.projection_mode != TORIDRAW_PROJECTION_MODE_PARALLEL;

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
    /* The client's buffer clears to 0, farther than any key. The clear value
     * is put back at once: the frame's own clears expect 1. */
    glClearDepthf(world->key_depth ? 0.0f : 1.0f);
    es3_zbuffer_begin_pass(renderer);
    glClearDepthf(1.0f);

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
    /* w = camera depth under perspective, which is what the key is made of;
     * the depth row it also writes is what a parallel pass tests. */
    es3_zbuffer_setup_projection(renderer, command);
    es3_mat4_multiply(renderer->projection, renderer->view, renderer->model_view_projection);

    for( uint32_t group = 0u; group < TRSPK_VBO_GROUP_COUNT; group++ )
        if( renderer->groups[group].reset_each_frame )
            es3_reset_group(&renderer->groups[group]);
}

static void
es3o_draw_model(
    struct TRSPK_Renderer_ES3* renderer,
    const struct ToriRS_RenderCommand_Model* command)
{
    struct ES3ModelPlacement placement;

    if( es3_zbuffer_place_model(renderer, command, &placement) )
        es3o_emit_model(renderer, command, &placement);
}

static void
es3o_end_3d(struct TRSPK_Renderer_ES3* renderer)
{
    struct ES3OsrsWorld* world = es3o_state(renderer);

    if( renderer->scene )
        ToriDraw_SceneClearProjectionCamera(renderer->scene);
    if( !renderer->has_3d )
        goto done;
    if( !es3_upload_atlas(renderer) )
        goto done;
    /* The client's opaque pass: the batches (order-free), then the two-pass
     * models; then its translucent pass in arrival order. */
    es3_zbuffer_flush_opaque(renderer);
    es3o_push_runs(renderer, world, &world->opaque_two_pass, false);
    if( !es3_upload_geometry(renderer) )
        goto done;
    es3o_push_runs(renderer, world, &world->translucent, true);
    es3o_sequence_draw(renderer);

done:
    renderer->has_3d = false;
    renderer->in3d = false;
    es3_sequence_reset(renderer);
    es3_zbuffer_reset_pass(renderer);
    es3o_reset_pass(world);
    es3_set_letterbox_viewport(renderer);
    es3_set_depth(renderer, false, false);
    es3_set_cull(renderer, false);
    es3_set_scissor(renderer, NULL);
}

/* ---- dispatch and the frame ------------------------------------------------------------ */

static void
es3o_dispatch(
    struct TRSPK_Renderer_ES3* renderer,
    const struct ToriRS_RenderCommand* command)
{
    assert(renderer);
    assert(command);
    if( command->kind == TORIRSRC_BEGIN_3D )
        es3o_begin_3d(renderer, &command->u.begin_3d);
    else if( command->kind == TORIRSRC_END_3D )
        es3o_end_3d(renderer);
    else if( command->kind == TORIRSRC_DRAW_MODEL )
        es3o_draw_model(renderer, &command->u.model);
    else
        es3_zbuffer_dispatch_retained(renderer, command);
}

void
TRSPK_Renderer_ES3_OsrsExecute(
    struct TRSPK_Renderer_ES3* renderer,
    struct ToriRS_RenderCommand const* command)
{
    es3o_dispatch(renderer, command);
}

/* The boot bar draws no world: the depth renderer's frame and bar serve. */
void
TRSPK_Renderer_ES3_OsrsDrawBootBar(
    struct TRSPK_Renderer_ES3* renderer,
    int progress,
    int caption_font_id,
    char const* caption)
{
    TRSPK_Renderer_ES3_ZBufferDrawBootBar(renderer, progress, caption_font_id, caption);
}

void
TRSPK_Renderer_ES3_OsrsRenderFrame(
    struct TRSPK_Renderer_ES3* renderer,
    struct ToriRS_Frame* frame)
{
    struct ToriRS_RenderCommand command;
    uint64_t render_start_us;

    assert(renderer);
    assert(frame);
    render_start_us = PlatformWindow_TicksUs();
    if( !es3z_render_frame_begin(renderer) )
        return;
    ToriRS_FrameBegin(frame);
    while( ToriRS_FrameNextCommand(frame, &command) )
    {
        es3_prefetch_ahead(renderer, frame);
        es3o_dispatch(renderer, &command);
    }
    ToriRS_FrameEnd(frame);
    if( renderer->in3d )
        es3o_end_3d(renderer);
    if( renderer->in2d )
        es3_end_2d(renderer);
    if( renderer->target_offscreen )
        es3_scale_target_present(renderer);
    es3_frame_readback(renderer);
    es3_world_prefill(renderer, render_start_us, es3_zbuffer_world_record_bake);
}

/*
 * 24 depth bits, not the depth renderer's 16: the key is 1/z shaped with its
 * pole at 75, so most of the range goes to the first few tiles in front of
 * the camera, and 16 bits leave the far half of a scene too coarse for the
 * face biases to separate a decal from its surface. 24 is what the offscreen
 * target already uses (es3_scale_target_ensure_depth).
 */
bool
TRSPK_Renderer_ES3_OsrsInit(
    struct TRSPK_Renderer_ES3* renderer,
    ToriPlatform_GLWindow* window,
    struct ToriDraw_Scene* scene)
{
    assert(renderer);
    assert(window);
    assert(scene);
    /* The OSRS vertex carries the bias; a handle from TRSPK_Renderer_ES3_New
     * has none to give. */
    assert(renderer->vertex_format == TRSPK_VERTEX_FORMAT_ES3_OSRS);
    if( renderer->gl_context )
        return false;
    es3_zbuffer_destroy(renderer);
    es3_osrs_destroy(renderer);
    if( !es3_zbuffer_create(renderer) )
        return false;
    es3o_create(renderer);
    if( !es3_init_gl(renderer, window, scene, 24, "OSRS z-buffer") )
    {
        es3_osrs_destroy(renderer);
        es3_zbuffer_destroy(renderer);
        return false;
    }
    return true;
}
