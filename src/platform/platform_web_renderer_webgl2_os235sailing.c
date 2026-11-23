/**
 * "WebGL 2 (Sailing OSRS)": the OSRS z-buffer renderer for WebGL2 -- a WHOLE renderer,
 * beside the painter (platform_web_renderer_webgl2_painter.c), the depth
 * renderer (platform_web_renderer_webgl2_zbuffer.c) and Post-TFA OSRS
 * (platform_web_renderer_webgl2_os241tfa.c). Neither OSRS file calls the other.
 *
 * It draws the world the way the rev-241 client does under `::zbuf 1`
 * (docs/toridraw_rszbuffer.md), which the software renderer "Software (OSRS
 * z-buffer)" reproduces exactly. On the GPU the point is the depth buffer's
 * RESULT rather than the client's rasterizer, and the four things the plain
 * depth renderer lacks are what make OSRS content z-fight there:
 *
 *   the depth key   the OSRS world vertex shaders key every corner at its
 *                   camera depth less twice its face's depth bias
 *                   (face_z_offsets, carried by the OSRS vertices), with the
 *                   client's formula, so a decal
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
 * buckets, the retained-store commands) through the shared es3_zbuffer_* and
 * es3_* pieces in es3_core.h. What this file owns is its bake chain (its own
 * vertex, written around the shared steps) and the world pass: begin_3d, the
 * draw, the per-pass queues of two-pass models and translucent runs, end_3d
 * and the issue loop that sets the write masks.
 *
 * Its handle is made by TRSPK_Renderer_ES3_NewOs235Sailing: the world is baked
 * in the Sailing vertex (es3_os235sailing_vertex.h), and its world programs
 * are es3_world_os235sailing_vertex_shader over the shared fragment programs.
 * Textures map from the baked UVs, which is right for every model before The
 * Fractured Archive.
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
#include "render/trspk_pose_pool.h"
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

struct ES3Os235SailingWorld
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

static struct ES3Os235SailingWorld*
es3_os235_state(struct TRSPK_Renderer_ES3* renderer)
{
    assert(renderer);
    assert(renderer->os235sailing);
    return renderer->os235sailing;
}

/* --- lifetime ------------------------------------------------------------------ */

static void
es3_os235_create(struct TRSPK_Renderer_ES3* renderer)
{
    assert(renderer);
    assert(!renderer->os235sailing);
    renderer->os235sailing =
        (struct ES3Os235SailingWorld*)ToriMem_Calloc(ToriMem_Renderer(), 1u, sizeof(struct ES3Os235SailingWorld));
    assert(renderer->os235sailing);
}

void
es3_os235sailing_destroy(struct TRSPK_Renderer_ES3* renderer)
{
    struct ES3Os235SailingWorld* world;
    if( !renderer || !renderer->os235sailing )
        return;
    world = renderer->os235sailing;
    ToriMem_Free(ToriMem_Renderer(), world->indices);
    ToriMem_Free(ToriMem_Renderer(), world->opaque_two_pass.runs);
    ToriMem_Free(ToriMem_Renderer(), world->translucent.runs);
    ToriMem_Free(ToriMem_Renderer(), world->face_keys);
    ToriMem_Free(ToriMem_Renderer(), world);
    renderer->os235sailing = NULL;
}

static void
es3_os235_reset_pass(struct ES3Os235SailingWorld* world)
{
    assert(world);
    world->index_count = 0u;
    world->opaque_two_pass.count = 0u;
    world->translucent.count = 0u;
}

/* --- the queues ---------------------------------------------------------------- */

static void
es3_os235_queue_run(
    struct ES3Os235SailingWorld* world,
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
es3_os235_write_face(
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
es3_os235_ensure_projected(
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
es3_os235_compare_face_key(const void* lhs, const void* rhs)
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
es3_os235_sorted_blended_indices(
    struct TRSPK_Renderer_ES3* renderer,
    struct ES3Os235SailingWorld* world,
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
    qsort(world->face_keys, key_count, sizeof(world->face_keys[0]), es3_os235_compare_face_key);
    for( uint32_t i = 0u; i < key_count; i++ )
        written = es3_os235_write_face(
            renderer->model_indices, written, placement, (uint32_t)world->face_keys[i].face);
    return written;
}

/*
 * A two-pass model: every face in render-priority order -- the painter's face
 * sort, back faces dropped -- split into the opaque run (opaque and cutout
 * faces) and the translucent one (blended faces), each keeping that order.
 */
static void
es3_os235_emit_two_pass(
    struct TRSPK_Renderer_ES3* renderer,
    struct ES3Os235SailingWorld* world,
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
        if( !es3_os235_ensure_projected(renderer, command, placement) )
            return;
        sorted_face_count =
            ToriDraw_RenderModel2SortFacesWithTable(command->model, renderer->scene, renderer->kernel);
        face_order = ToriDraw_FaceOrder(renderer->scene);
    }
    if( sorted_face_count <= 0 )
        return;

    /* model_indices holds face_count * 3 (es3_zbuffer_place_end reserved
     * it), and each pass below writes at most that. */
    written = 0u;
    for( int i = 0; i < sorted_face_count; i++ )
    {
        uint8_t pass;
        if( face_order[i] < 0 || (uint32_t)face_order[i] >= material->face_count )
            continue;
        pass = material->face_passes[face_order[i]];
        if( pass == ES3_WORLD_FACE_OPAQUE || pass == ES3_WORLD_FACE_CUTOUT )
            written = es3_os235_write_face(renderer->model_indices, written, placement, (uint32_t)face_order[i]);
    }
    es3_os235_queue_run(world, &world->opaque_two_pass, placement, true, renderer->model_indices, written);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_Z_OPAQUE_TRIANGLES, written / 3u);

    if( material->blended_count == 0u )
        return;
    written = 0u;
    for( int i = 0; i < sorted_face_count; i++ )
    {
        if( face_order[i] < 0 || (uint32_t)face_order[i] >= material->face_count )
            continue;
        if( material->face_passes[face_order[i]] == ES3_WORLD_FACE_BLENDED )
            written = es3_os235_write_face(renderer->model_indices, written, placement, (uint32_t)face_order[i]);
    }
    es3_os235_queue_run(world, &world->translucent, placement, true, renderer->model_indices, written);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_Z_BLENDED_TRIANGLES, written / 3u);
}

static void
es3_os235_emit_model(
    struct TRSPK_Renderer_ES3* renderer,
    const struct ToriRS_RenderCommand_Model* command,
    const struct ES3ModelPlacement* placement)
{
    struct ES3Os235SailingWorld* world = es3_os235_state(renderer);
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
        es3_os235_emit_two_pass(renderer, world, command, placement, material);
        return;
    }

    /* Model order in the client's opaque pass: the depth buffer decides, so
     * the depth world's batches serve as they are. */
    es3_zbuffer_queue_opaque(renderer, placement, material);
    if( material->blended_count == 0u )
        return;
    /* Only a full model carries face alpha; a tile is never blended. */
    assert(ToriDraw_ModelKindIsFull(command->model.kind));
    if( !es3_os235_ensure_projected(renderer, command, placement) )
        return;
    written = es3_os235_sorted_blended_indices(renderer, world, command, placement, material);
    es3_os235_queue_run(world, &world->translucent, placement, false, renderer->model_indices, written);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_Z_SORTED_MODELS, 1);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_Z_BLENDED_TRIANGLES, written / 3u);
}

/* Push one list's runs onto the draw sequence. A two-pass run goes on twice,
 * colour only then depth only, before the next run: the client finishes a
 * model before it starts the next. */
static void
es3_os235_push_runs(
    struct TRSPK_Renderer_ES3* renderer,
    struct ES3Os235SailingWorld* world,
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
es3_os235_set_color_writes(bool* current, bool enabled)
{
    if( *current == enabled )
        return;
    glColorMask(enabled, enabled, enabled, enabled);
    *current = enabled;
}

static void
es3_os235_sequence_issue(
    struct TRSPK_Renderer_ES3* renderer,
    uint32_t index_base_bytes)
{
    struct ES3Os235SailingWorld* world = es3_os235_state(renderer);
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
        es3_os235_set_color_writes(&color_writes, item->writes != ES3_WRITES_DEPTH);
        if( program_cutout != (int)item->cutout )
        {
            es3_use_world_program(renderer, item->cutout != 0u);
            program_cutout = (int)item->cutout;
        }
        if( !es3_bind_stream_os235sailing(renderer, item->binding) )
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
    es3_os235_set_color_writes(&color_writes, true);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DRAW_CALLS, draw_calls);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DRAW_RANGES, renderer->draw_item_count);
}

static void
es3_os235_sequence_draw(struct TRSPK_Renderer_ES3* renderer)
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
    es3_os235_sequence_issue(renderer, index_base_bytes);
}

/* --- the world pass ---------------------------------------------------------------- */

static void
es3_os235_begin_3d(
    struct TRSPK_Renderer_ES3* renderer,
    const struct ToriRS_RenderCommand_Begin3D* command)
{
    struct ES3Os235SailingWorld* world = es3_os235_state(renderer);
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
    es3_os235_reset_pass(world);
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

/* ---- the bake chain: the os235sailing vertex -------------------------------------------- */

/*
 * One pose into `vbo` at `vertex_base`, three Sailing vertices per face: the
 * fields every bake shares (trspk_toridraw_bake_face_handle,
 * es3_bake_face_atlas) and the face's depth bias. A face the bake skips keeps
 * whatever its triplet held, as the os1 bake leaves it.
 */
static bool
es3_os235_bake_pose(
    struct TRSPK_Renderer_ES3* renderer,
    struct TRSPK_VBO* vbo,
    struct TRSPK_Triangles* triangles,
    uint32_t vertex_base,
    struct ToriDraw_ModelHandle model_handle,
    const struct ToriDraw_Position* world_position)
{
    struct TRSPK_WorldPlacement placement;
    int const face_count = trspk_toridraw_face_count(model_handle);
    /* A tile (TORIDRAWMK_GROUND) has neither a bias nor a projector. */
    const struct ToriDraw_Model* const model =
        ToriDraw_ModelKindIsFull(model_handle.kind) ? ToriDraw_ModelRead(model_handle) : NULL;
    const int8_t* const biases = model ? model->face_z_offsets : NULL;

    assert(renderer);
    assert(vbo);
    assert(triangles);
    assert(vbo->format == TRSPK_VERTEX_FORMAT_ES3_OS235SAILING);
    if( face_count <= 0 )
        return false;
    trspk_toridraw_placement_init(&placement, world_position);
    for( uint32_t face_index = 0u; face_index < (uint32_t)face_count; face_index++ )
    {
        struct TRSPK_ToriDrawBakeFaceVerts face;
        struct ES3FaceAtlas atlas;
        uint32_t const vertex = vertex_base + face_index * 3u;
        int8_t const bias = biases ? biases[face_index] : 0;

        if( !trspk_toridraw_bake_face_handle(
                model_handle, face_index, &placement, NULL, true, TRSPK_BAKE_COLOR_ARGB, &face) )
            continue;
        es3_bake_face_atlas(renderer, &face, &atlas);

        trspk_triangles_set(triangles, trspk_triangles_index_from_vertex(vertex), atlas.config);
        trspk_vbo_write_vertex_es3_os235sailing(vbo, vertex, face.wx_a, face.wy_a, face.wz_a,
            es3_argb_to_rgba_bytes(face.argb_a), atlas.u[0], atlas.v[0], atlas.tile_col,
            atlas.tile_row, atlas.anim_u, atlas.anim_v, bias);
        trspk_vbo_write_vertex_es3_os235sailing(vbo, vertex + 1u, face.wx_b, face.wy_b, face.wz_b,
            es3_argb_to_rgba_bytes(face.argb_b), atlas.u[1], atlas.v[1], atlas.tile_col,
            atlas.tile_row, atlas.anim_u, atlas.anim_v, bias);
        trspk_vbo_write_vertex_es3_os235sailing(vbo, vertex + 2u, face.wx_c, face.wy_c, face.wz_c,
            es3_argb_to_rgba_bytes(face.argb_c), atlas.u[2], atlas.v[2], atlas.tile_col,
            atlas.tile_row, atlas.anim_u, atlas.anim_v, bias);
    }
    trspk_vbo_mark_dirty_range(vbo, vertex_base, (uint32_t)face_count * 3u);
    return true;
}

/* An arena bake: the shared reserve and commit around this vertex's bake. */
static uint32_t
es3_os235_bake_into_arena(
    struct TRSPK_Renderer_ES3* renderer,
    struct ES3ModelGroup* group,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle model_handle,
    const struct ToriDraw_Position* world_position,
    bool update_pose_table)
{
    uint32_t const vertex_base = es3_arena_reserve(group, element_id, anim_index,
        pose_id, trspk_toridraw_face_count(model_handle), update_pose_table);
    if( vertex_base == UINT32_MAX ||
        !es3_os235_bake_pose(renderer, group->vbo_cpu, &group->triangles, vertex_base, model_handle,
            world_position) )
        return UINT32_MAX;
    es3_arena_commit(renderer, element_id, anim_index, pose_id, vertex_base, update_pose_table);
    return vertex_base;
}

/* A world-build record: taken, baked in this vertex, classified into the
 * build's material table. What this renderer hands es3_world_prefill and
 * es3_world_bake_on_draw. */
static void
es3_os235_world_record_bake(
    struct TRSPK_Renderer_ES3* renderer,
    struct TRSPK_WorldSlot* slot,
    int record)
{
    struct TRSPK_WorldRecord const* baking;
    struct TRSPK_Batch32Reservation reservation;
    assert(renderer);
    assert(slot);
    baking = &slot->records[record];
    trspk_world_record_take(slot, record, &reservation);
    (void)es3_os235_bake_pose(renderer, reservation.vbo, reservation.triangles,
        reservation.vertex_base, baking->model, &baking->world_position);
    es3_zbuffer_batch_pose_baked(
        renderer, slot, baking->element_id, baking->anim_index, baking->pose_id, baking->model);
}

/* An announced track's pose for this draw (es3_track_pose_model), baked into
 * the static arena under the track's id. *out_baked is freed by the caller. */
static bool
es3_os235_bake_track_pose(
    struct TRSPK_Renderer_ES3* renderer,
    const struct ToriRS_RenderCommand_Model* command,
    int anim_index,
    int pose_id,
    uint32_t* out_vertex_base,
    struct ToriDraw_Model** out_baked)
{
    const struct TRSPK_AnimTrack* track = NULL;
    struct ToriDraw_ModelHandle handle;
    struct ToriDraw_Model* const baked =
        es3_track_pose_model(renderer, command, anim_index, pose_id, &track);
    uint32_t vertex_base;

    if( !baked )
        return false;
    memset(&handle, 0, sizeof(handle));
    handle.kind = TORIDRAWMK_MODEL;
    handle.u.model.model = baked;
    vertex_base = es3_os235_bake_into_arena(renderer, &renderer->groups[TRSPK_VBO_GROUP_STATIC],
        track->announced_id, anim_index, pose_id, handle, &track->world_position, true);
    if( vertex_base == UINT32_MAX )
    {
        ToriDraw_ModelFree(baked);
        return false;
    }
    *out_vertex_base = vertex_base;
    *out_baked = baked;
    return true;
}

/* A dynamic model: the pose pool's copy when it has one, this vertex's bake
 * kept there otherwise. */
static uint32_t
es3_os235_bake_dynamic(
    struct TRSPK_Renderer_ES3* renderer,
    struct ES3ModelGroup* group,
    const struct ToriRS_RenderCommand_Model* command,
    int anim_index,
    int pose_id)
{
    struct TRSPK_PoseKey key;
    bool keyed;
    uint32_t vertex_base =
        es3_zbuffer_dynamic_reuse(renderer, group, command, anim_index, pose_id, &key, &keyed);
    if( vertex_base != UINT32_MAX )
        return vertex_base;
    vertex_base = es3_os235_bake_into_arena(renderer, group, command->element_id, anim_index, pose_id,
        command->model, &command->world_position, false);
    es3_zbuffer_dynamic_keep(renderer, group, command, &key, keyed, vertex_base);
    return vertex_base;
}

/* A DRAW_MODEL's pose, found or baked in this vertex, between the shared
 * es3_zbuffer_place_begin and es3_zbuffer_place_end. */
static bool
es3_os235_place_model(
    struct TRSPK_Renderer_ES3* renderer,
    const struct ToriRS_RenderCommand_Model* command,
    struct ES3ModelPlacement* placement)
{
    uint32_t const group = placement->dynamic ? TRSPK_VBO_GROUP_DYNAMIC : TRSPK_VBO_GROUP_STATIC;
    int const anim_index = placement->anim_index;
    int const pose_id = placement->pose_id;
    uint32_t binding = group;
    uint32_t vertex_base = UINT32_MAX;
    int world;

    if( !placement->dynamic )
        (void)es3_world_bake_on_draw(
            renderer, command->element_id, anim_index, pose_id, es3_os235_world_record_bake);
    if( placement->dynamic )
        vertex_base = es3_os235_bake_dynamic(renderer, &renderer->groups[group], command, anim_index, pose_id);
    else if( (world = es3_zbuffer_place_static_world(renderer, command, placement, &vertex_base)) != 0 )
    {
        if( world < 0 )
            return false;
        binding = ES3_WORLD_BINDING;
    }
    else if( !trspk_pose_table_get(
                 &renderer->poses, command->element_id, anim_index, pose_id, &vertex_base) )
    {
        struct ToriDraw_Model* posed = NULL;
        renderer->stat_static_rebake += 1.0;
        if( command->animation &&
            !es3_os235_bake_track_pose(renderer, command, anim_index, pose_id, &vertex_base, &posed) )
        {
            es3_zbuffer_announce_from_draw(renderer, command, anim_index);
            (void)es3_os235_bake_track_pose(renderer, command, anim_index, pose_id, &vertex_base, &posed);
        }
        if( posed )
        {
            struct ToriDraw_ModelHandle handle;
            memset(&handle, 0, sizeof(handle));
            handle.kind = TORIDRAWMK_MODEL;
            handle.u.model.model = posed;
            es3_zbuffer_pose_baked(renderer, command->element_id, anim_index, pose_id, handle);
            ToriDraw_ModelFree(posed);
        }
        else if( !trspk_pose_table_get(
                     &renderer->poses, command->element_id, anim_index, pose_id, &vertex_base) )
        {
            vertex_base = es3_os235_bake_into_arena(renderer, &renderer->groups[group],
                command->element_id, anim_index, pose_id, command->model, &command->world_position,
                true);
            if( vertex_base != UINT32_MAX )
                es3_zbuffer_pose_baked(
                    renderer, command->element_id, anim_index, pose_id, command->model);
        }
    }
    return es3_zbuffer_place_end(renderer, placement, binding, vertex_base);
}

/* MODEL_LOAD: the element's old poses dropped, the model baked in this
 * vertex and classified. */
static void
es3_os235_model_load(
    struct TRSPK_Renderer_ES3* renderer,
    const struct ToriRS_RenderCommand_ModelLoad* command)
{
    assert(renderer);
    assert(command);
    if( command->element_id < 0 || command->model.kind == TORIDRAWMK_NONE )
        return;
    es3_zbuffer_model_drop(renderer, command->element_id);
    if( es3_os235_bake_into_arena(renderer, &renderer->groups[TRSPK_VBO_GROUP_STATIC],
            command->element_id, 0, 0, command->model, &command->world_position, true) != UINT32_MAX )
        es3_zbuffer_pose_baked(renderer, command->element_id, 0, 0, command->model);
}

static void
es3_os235_draw_model(
    struct TRSPK_Renderer_ES3* renderer,
    const struct ToriRS_RenderCommand_Model* command)
{
    struct ES3ModelPlacement placement;

    if( es3_zbuffer_place_begin(renderer, command, &placement) &&
        es3_os235_place_model(renderer, command, &placement) )
        es3_os235_emit_model(renderer, command, &placement);
}

static void
es3_os235_end_3d(struct TRSPK_Renderer_ES3* renderer)
{
    struct ES3Os235SailingWorld* world = es3_os235_state(renderer);

    if( renderer->scene )
        ToriDraw_SceneClearProjectionCamera(renderer->scene);
    if( !renderer->has_3d )
        goto done;
    if( !es3_upload_atlas(renderer) )
        goto done;
    /* The client's opaque pass: the batches (order-free), then the two-pass
     * models; then its translucent pass in arrival order. */
    es3_zbuffer_flush_opaque(renderer);
    es3_os235_push_runs(renderer, world, &world->opaque_two_pass, false);
    if( !es3_upload_geometry(renderer) )
        goto done;
    es3_os235_push_runs(renderer, world, &world->translucent, true);
    es3_os235_sequence_draw(renderer);

done:
    renderer->has_3d = false;
    renderer->in3d = false;
    es3_sequence_reset(renderer);
    es3_zbuffer_reset_pass(renderer);
    es3_os235_reset_pass(world);
    es3_set_letterbox_viewport(renderer);
    es3_set_depth(renderer, false, false);
    es3_set_cull(renderer, false);
    es3_set_scissor(renderer, NULL);
}

/* ---- dispatch and the frame ------------------------------------------------------------ */

static void
es3_os235_dispatch(
    struct TRSPK_Renderer_ES3* renderer,
    const struct ToriRS_RenderCommand* command)
{
    assert(renderer);
    assert(command);
    if( command->kind == TORIRSRC_BEGIN_3D )
        es3_os235_begin_3d(renderer, &command->u.begin_3d);
    else if( command->kind == TORIRSRC_END_3D )
        es3_os235_end_3d(renderer);
    else if( command->kind == TORIRSRC_DRAW_MODEL )
        es3_os235_draw_model(renderer, &command->u.model);
    else if( command->kind == TORIRSRC_MODEL_LOAD )
        es3_os235_model_load(renderer, &command->u.model_load);
    else
        es3_zbuffer_dispatch_retained(renderer, command);
}

void
TRSPK_Renderer_ES3_Os235SailingExecute(
    struct TRSPK_Renderer_ES3* renderer,
    struct ToriRS_RenderCommand const* command)
{
    es3_os235_dispatch(renderer, command);
}

/* The boot bar draws no world: the depth renderer's frame and bar serve. */
void
TRSPK_Renderer_ES3_Os235SailingDrawBootBar(
    struct TRSPK_Renderer_ES3* renderer,
    int progress,
    int caption_font_id,
    char const* caption)
{
    TRSPK_Renderer_ES3_ZBufferDrawBootBar(renderer, progress, caption_font_id, caption);
}

void
TRSPK_Renderer_ES3_Os235SailingRenderFrame(
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
        es3_os235_dispatch(renderer, &command);
    }
    ToriRS_FrameEnd(frame);
    if( renderer->in3d )
        es3_os235_end_3d(renderer);
    if( renderer->in2d )
        es3_end_2d(renderer);
    if( renderer->target_offscreen )
        es3_scale_target_present(renderer);
    es3_frame_readback(renderer);
    es3_world_prefill(renderer, render_start_us, es3_os235_world_record_bake);
}

/*
 * 24 depth bits, not the depth renderer's 16: the key is 1/z shaped with its
 * pole at 75, so most of the range goes to the first few tiles in front of
 * the camera, and 16 bits leave the far half of a scene too coarse for the
 * face biases to separate a decal from its surface. 24 is what the offscreen
 * target already uses (es3_scale_target_ensure_depth).
 */
bool
TRSPK_Renderer_ES3_Os235SailingInit(
    struct TRSPK_Renderer_ES3* renderer,
    ToriPlatform_GLWindow* window,
    struct ToriDraw_Scene* scene)
{
    assert(renderer);
    assert(window);
    assert(scene);
    /* Its world is baked in its own vertex: a TRSPK_Renderer_ES3_NewOs235Sailing
     * handle. */
    assert(renderer->vertex_format == TRSPK_VERTEX_FORMAT_ES3_OS235SAILING);
    if( renderer->gl_context )
        return false;
    es3_zbuffer_destroy(renderer);
    es3_os235sailing_destroy(renderer);
    if( !es3_zbuffer_create(renderer) )
        return false;
    es3_os235_create(renderer);
    es3_set_world_shaders(renderer, &es3_world_shaders_os235sailing);
    if( !es3_init_gl(renderer, window, scene, 24, "Sailing OSRS") )
    {
        es3_os235sailing_destroy(renderer);
        es3_zbuffer_destroy(renderer);
        return false;
    }
    return true;
}
