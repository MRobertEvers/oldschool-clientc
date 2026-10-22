/*
 * Depth-buffered world pass. The rationale and the contract are in
 * platform_sdl2_renderer_gl3zb.h; this file is the implementation.
 *
 * Everything it shares with the painter renderer — the renderer struct, the
 * constants, gl3_bind_group_attribs, gl3_ensure_gpu_ibo, gl3_ensure_index16 —
 * comes from the internal header, so neither file owns a private copy of state
 * the other also writes.
 */

#include "platform/platform_sdl2_renderer_gl3zb.h"
#include "toridraw_element_id.h"
#include <assert.h>

#include "platform/platform_sdl2_renderer_gl3_internal.h"

#include "mem/torirs_mem.h"
#include "perf/torirs_perf.h"

#include "toridraw.h"
#include "toridraw_model.h"
#include "toridraw_scene.h"
#include "toridraw_types.h"

#include <stdlib.h>
#include <string.h>


/*
 * Which pass a world face belongs to.
 *
 * Read off the model the frame is already holding — face type, colour, the
 * pose's final alpha, and whether the face's texture is opaque. No pixels are
 * scanned: `texture->opaque` was decided when the texture was decoded.
 *
 * The alpha test comes before the texture test on purpose. Animation baking has
 * already written this pose's final face alpha, and a textured face can still
 * be genuinely translucent — classifying by texture first would send a faded
 * model to the opaque pass, where it would write depth and punch a hole in
 * whatever is behind it.
 */
static enum GL3WorldFacePass
gl3_world_face_pass(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    struct ToriDraw_ModelHandle handle,
    uint32_t face)
{
    if( ToriDraw_ModelKindIsFull(handle.kind) )
    {
        struct ToriDraw_Model* model = handle.u.model.model;
        int raw_type;
        int tex_id;
        uint8_t alpha;

        if( !model || face >= (uint32_t)model->face_count )
            return GL3_WORLD_FACE_SKIP;
        raw_type = model->face_infos ? model->face_infos[face] : 0;
        if( raw_type == 2 || raw_type < 0 || raw_type > 3 ||
            model->face_colors_c[face] == TORIDRAWHSL16_HIDDEN )
            return GL3_WORLD_FACE_SKIP;
        alpha = model->face_alphas ? (uint8_t)(0xffu - model->face_alphas[face]) : 0xffu;
        if( alpha <= 1u )
            return GL3_WORLD_FACE_SKIP;
        if( alpha != 0xffu )
            return GL3_WORLD_FACE_BLENDED;
        tex_id = model->face_textures ? (int)model->face_textures[face] : -1;
        if( tex_id >= 0 )
        {
            struct ToriDraw_Texture* texture =
                renderer && renderer->scene
                    ? ToriDraw_TextureMapGet(
                          &ToriDraw_SceneTexState(renderer->scene)->texture_map, tex_id)
                    : NULL;
            return texture && texture->opaque ? GL3_WORLD_FACE_OPAQUE : GL3_WORLD_FACE_CUTOUT;
        }
        return GL3_WORLD_FACE_OPAQUE;
    }
    if( handle.kind == TORIDRAWMK_GROUND )
    {
        struct ToriDraw_ModelGround* ground = handle.u.model.ground;
        if( !ground || face >= (uint32_t)ground->face_count ||
            ground->face_colors_c[face] == TORIDRAWHSL16_HIDDEN )
            return GL3_WORLD_FACE_SKIP;
        return GL3_WORLD_FACE_OPAQUE;
    }
    return GL3_WORLD_FACE_SKIP;
}

/* --- face classification cache ------------------------------------------- */

static void
gl3_material_pose_clear(struct GL3MaterialPose* pose)
{
    assert(pose);
    ToriMem_Free(ToriMem_Renderer(), pose->face_passes);
    memset(pose, 0, sizeof(*pose));
}

/* Classify every face of one baked pose, once. */
static bool
gl3_material_pose_set(
    struct GL3MaterialPose* pose,
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    struct ToriDraw_ModelHandle handle)
{
    int face_count = trspk_toridraw_face_count(handle);
    uint8_t* passes;

    if( face_count <= 0 )
        return false;
    assert(pose);
    passes =
        pose->face_count == (uint32_t)face_count && pose->face_passes
            ? pose->face_passes
            : (uint8_t*)ToriMem_Realloc(ToriMem_Renderer(), pose->face_passes, (size_t)face_count);
    pose->face_passes = passes;
    pose->face_count = (uint32_t)face_count;
    pose->opaque_count = 0u;
    pose->cutout_count = 0u;
    pose->blended_count = 0u;
    for( uint32_t face = 0u; face < pose->face_count; face++ )
    {
        enum GL3WorldFacePass pass = gl3_world_face_pass(renderer, handle, face);
        pose->face_passes[face] = (uint8_t)pass;
        if( pass == GL3_WORLD_FACE_OPAQUE )
            pose->opaque_count++;
        else if( pass == GL3_WORLD_FACE_CUTOUT )
            pose->cutout_count++;
        else if( pass == GL3_WORLD_FACE_BLENDED )
            pose->blended_count++;
    }
    return true;
}

static const struct GL3MaterialPose*
gl3_material_table_get(
    const struct GL3MaterialTable* table,
    int element_id,
    int anim_index,
    int pose_id)
{
    const struct GL3MaterialTrack* track;
    if( !table || !table->elements || element_id < 0 ||
        (uint32_t)ToriDraw_ElementIndexOfRaw(element_id) >= table->element_count || anim_index < 0 ||
        anim_index >= TRSPK_POSE_TRACK_COUNT || pose_id < 0 )
        return NULL;
    track = &table->elements[ToriDraw_ElementIndexOfRaw(element_id)].tracks[anim_index];
    if( !track->poses || (uint32_t)pose_id >= track->pose_count ||
        !track->poses[pose_id].face_passes )
        return NULL;
    return &track->poses[pose_id];
}

static const struct GL3MaterialPose*
gl3_material_table_set(
    struct GL3MaterialTable* table,
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle handle)
{
    struct GL3MaterialTrack* track;
    uint32_t needed;

    if( !table || element_id < 0 || anim_index < 0 ||
        anim_index >= TRSPK_POSE_TRACK_COUNT || pose_id < 0 )
        return NULL;
    needed = (uint32_t)ToriDraw_ElementIndexOfRaw(element_id) + 1u;
    if( needed > table->element_capacity )
    {
        uint32_t capacity = table->element_capacity ? table->element_capacity : 64u;
        struct GL3MaterialElement* grown;
        while( capacity < needed )
            capacity *= 2u;
        grown = (struct GL3MaterialElement*)ToriMem_Realloc(
            ToriMem_Renderer(), table->elements, (size_t)capacity * sizeof(*grown));
        memset(
            grown + table->element_capacity, 0,
            (size_t)(capacity - table->element_capacity) * sizeof(*grown));
        table->elements = grown;
        table->element_capacity = capacity;
    }
    if( table->element_count < needed )
        table->element_count = needed;

    track = &table->elements[ToriDraw_ElementIndexOfRaw(element_id)].tracks[anim_index];
    needed = (uint32_t)pose_id + 1u;
    if( needed > track->pose_capacity )
    {
        uint32_t capacity = track->pose_capacity ? track->pose_capacity : 8u;
        struct GL3MaterialPose* grown;
        while( capacity < needed )
            capacity *= 2u;
        grown = (struct GL3MaterialPose*)ToriMem_Realloc(
            ToriMem_Renderer(), track->poses, (size_t)capacity * sizeof(*grown));
        memset(
            grown + track->pose_capacity, 0,
            (size_t)(capacity - track->pose_capacity) * sizeof(*grown));
        track->poses = grown;
        track->pose_capacity = capacity;
    }
    if( track->pose_count < needed )
        track->pose_count = needed;
    if( !gl3_material_pose_set(&track->poses[pose_id], renderer, handle) )
        return NULL;
    return &track->poses[pose_id];
}

/* Cached lookup, classifying on first use. */
static const struct GL3MaterialPose*
gl3_material_for(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle handle)
{
    const struct GL3MaterialPose* pose =
        gl3_material_table_get(&renderer->materials, element_id, anim_index, pose_id);
    if( pose && pose->face_count == (uint32_t)trspk_toridraw_face_count(handle) )
        return pose;
    return gl3_material_table_set(
        &renderer->materials, renderer, element_id, anim_index, pose_id, handle);
}

void
GL3ZB_ForgetElement(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    int element_id)
{
    struct GL3MaterialTable* table;
    assert(renderer);
    table = &renderer->materials;
    if( !table->elements || element_id < 0 || (uint32_t)ToriDraw_ElementIndexOfRaw(element_id) >= table->element_count )
        return;
    for( int t = 0; t < TRSPK_POSE_TRACK_COUNT; t++ )
    {
        struct GL3MaterialTrack* track = &table->elements[ToriDraw_ElementIndexOfRaw(element_id)].tracks[t];
        for( uint32_t i = 0u; i < track->pose_count; i++ )
            gl3_material_pose_clear(&track->poses[i]);
        ToriMem_Free(ToriMem_Renderer(), track->poses);
        track->poses = NULL;
        track->pose_count = 0u;
        track->pose_capacity = 0u;
    }
}

void
GL3ZB_ForgetTrack(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    int element_id,
    int anim_index)
{
    struct GL3MaterialTable* table;
    struct GL3MaterialTrack* track;
    assert(renderer);
    table = &renderer->materials;
    if( !table->elements || element_id < 0 || (uint32_t)ToriDraw_ElementIndexOfRaw(element_id) >= table->element_count ||
        anim_index < 0 || anim_index >= TRSPK_POSE_TRACK_COUNT )
        return;
    track = &table->elements[ToriDraw_ElementIndexOfRaw(element_id)].tracks[anim_index];
    for( uint32_t i = 0u; i < track->pose_count; i++ )
        gl3_material_pose_clear(&track->poses[i]);
    ToriMem_Free(ToriMem_Renderer(), track->poses);
    track->poses = NULL;
    track->pose_count = 0u;
    track->pose_capacity = 0u;
}

static void
gl3_material_table_free(struct GL3MaterialTable* table)
{
    if( !table || !table->elements )
        return;
    for( uint32_t e = 0u; e < table->element_count; e++ )
        for( int t = 0; t < TRSPK_POSE_TRACK_COUNT; t++ )
        {
            struct GL3MaterialTrack* track = &table->elements[e].tracks[t];
            for( uint32_t i = 0u; i < track->pose_count; i++ )
                gl3_material_pose_clear(&track->poses[i]);
            ToriMem_Free(ToriMem_Renderer(), track->poses);
        }
    ToriMem_Free(ToriMem_Renderer(), table->elements);
    memset(table, 0, sizeof(*table));
}

/*
 * Signed area of the face's projected triangle.
 *
 * The painter path gets back-face rejection for free from
 * ToriDraw_RenderModel2SortFaces, which the depth path deliberately does not
 * run for opaque faces. Without this the pass submits both sides of every
 * closed model — twice the triangles, and the depth test cannot tell them
 * apart because the far side is genuinely behind.
 */
static bool
gl3_reserve_model_indices(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    uint32_t count)
{
    if( count <= renderer->model_index_capacity )
        return true;
    {
        uint32_t cap = renderer->model_index_capacity ? renderer->model_index_capacity : 1024u;
        uint32_t* grown;
        while( cap < count )
            cap *= 2u;
        grown = (uint32_t*)ToriMem_Realloc(
            ToriMem_Renderer(), renderer->model_indices, (size_t)cap * sizeof(uint32_t));
        assert(grown);
        renderer->model_indices = grown;
        renderer->model_index_capacity = cap;
    }
    return true;
}

/* Hold one model's translucent faces for the sorted pass. */
static bool
gl3_queue_alpha_submission(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    uint32_t group,
    int depth,
    const uint32_t* indices,
    uint32_t index_count)
{
    struct GL3AlphaSubmission* submission;

    if( index_count == 0u )
        return false;
    assert(renderer);
    assert(indices);
    if( renderer->alpha_index_count > UINT32_MAX - index_count )
        return false;
    if( renderer->alpha_index_count + index_count > renderer->alpha_index_capacity )
    {
        uint32_t cap = renderer->alpha_index_capacity ? renderer->alpha_index_capacity : 1024u;
        uint32_t* grown;
        while( cap < renderer->alpha_index_count + index_count )
            cap *= 2u;
        grown = (uint32_t*)ToriMem_Realloc(
            ToriMem_Renderer(), renderer->alpha_indices, (size_t)cap * sizeof(uint32_t));
        assert(grown);
        renderer->alpha_indices = grown;
        renderer->alpha_index_capacity = cap;
    }
    if( renderer->alpha_submission_count >= renderer->alpha_submission_capacity )
    {
        uint32_t cap =
            renderer->alpha_submission_capacity ? renderer->alpha_submission_capacity * 2u : 128u;
        struct GL3AlphaSubmission* grown = (struct GL3AlphaSubmission*)ToriMem_Realloc(
            ToriMem_Renderer(), renderer->alpha_submissions, (size_t)cap * sizeof(*grown));
        renderer->alpha_submissions = grown;
        renderer->alpha_submission_capacity = cap;
    }

    submission = &renderer->alpha_submissions[renderer->alpha_submission_count++];
    submission->group = group;
    submission->index_start = renderer->alpha_index_count;
    submission->index_count = index_count;
    submission->depth = depth;
    memcpy(
        renderer->alpha_indices + renderer->alpha_index_count,
        indices,
        (size_t)index_count * sizeof(uint32_t));
    renderer->alpha_index_count += index_count;
    return true;
}

/*
 * Submit one world model under the depth pass.
 *
 * Two halves, and only the second one costs a sort:
 *
 *   opaque + cutout   natural face order, front-facing only. Depth-order
 *                     independent by definition, so the whole reason the
 *                     painter path sorts is gone.
 *   blended           kept in the priority/depth order the model itself wants,
 *                     queued for the back-to-front pass at the end of the frame.
 *
 * A model with no translucent faces never runs the sort at all, which is what
 * the mode is for.
 */
void
GL3ZB_SubmitModel(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    struct ToriRS_RenderCommand_Model const* mcmd,
    struct ToriDraw_Scene* ctx,
    uint32_t group,
    uint32_t vertex_base,
    int face_count)
{
    const struct GL3MaterialPose* material;
    uint32_t written = 0u;
    int sorted_face_count;
    int* face_order;
    int anim_index = mcmd->anim_index < 0 ? 0 : mcmd->anim_index;
    int pose_id = mcmd->animation && mcmd->anim_frame >= 0 ? mcmd->anim_frame : 0;

    if( face_count <= 0 || !gl3_reserve_model_indices(renderer, (uint32_t)face_count * 3u) )
        return;
    if( anim_index >= TRSPK_POSE_TRACK_COUNT )
        anim_index = TRSPK_POSE_TRACK_COUNT - 1;

    /* Classified once per baked pose, not once per frame -- except a dynamic
     * model, whose faces' alphas change under the same key (an
     * npc_transparency fade, a spotanim merged onto an actor). */
    if( mcmd->dynamic )
        material = gl3_material_pose_set(&renderer->dynamic_material, renderer, mcmd->model)
                       ? &renderer->dynamic_material
                       : NULL;
    else
        material = gl3_material_for(
            renderer, mcmd->element_id, anim_index, pose_id, mcmd->model);
    if( !material || material->face_count != (uint32_t)face_count )
        return;

    for( int i = 0; i < face_count; i++ )
    {
        const uint32_t face = (uint32_t)i;
        const uint8_t pass = material->face_passes[face];
        uint32_t b;

        if( pass != GL3_WORLD_FACE_OPAQUE && pass != GL3_WORLD_FACE_CUTOUT )
            continue;
        b = vertex_base + face * 3u;
        renderer->model_indices[written++] = b;
        renderer->model_indices[written++] = b + 1u;
        renderer->model_indices[written++] = b + 2u;
    }
    if( written > 0u )
    {
        trspk_ibochain_push32(renderer->ibo_chain, group, 0u, renderer->model_indices, written);
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_Z_OPAQUE_TRIANGLES, written / 3u);
    }

    if( material->blended_count == 0u )
        return;

    /* Only now, and only for models that really have translucency. */
    sorted_face_count =
        ToriDraw_RenderModel2SortFacesWithTable(mcmd->model, ctx, renderer->kernel);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_Z_SORTED_MODELS, 1);
    if( sorted_face_count <= 0 )
        return;
    face_order = ToriDraw_FaceOrder(ctx);
    written = 0u;
    for( int i = 0; i < sorted_face_count; i++ )
    {
        uint32_t face;
        uint32_t b;
        if( face_order[i] < 0 )
            continue;
        face = (uint32_t)face_order[i];
        if( face >= material->face_count ||
            material->face_passes[face] != GL3_WORLD_FACE_BLENDED )
            continue;
        b = vertex_base + face * 3u;
        renderer->model_indices[written++] = b;
        renderer->model_indices[written++] = b + 1u;
        renderer->model_indices[written++] = b + 2u;
    }
    if( written == 0u )
        return;
    (void)gl3_queue_alpha_submission(
        renderer, group, ctx->projected_vertex.z, renderer->model_indices, written);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_Z_BLENDED_TRIANGLES, written / 3u);
}

static int
gl3_alpha_order_cmp(
    void const* a,
    void const* b,
    void* user)
{
    const struct GL3AlphaSubmission* subs = (const struct GL3AlphaSubmission*)user;
    const uint32_t ia = *(const uint32_t*)a;
    const uint32_t ib = *(const uint32_t*)b;
    /* Back to front: larger depth first. */
    if( subs[ia].depth != subs[ib].depth )
        return subs[ia].depth > subs[ib].depth ? -1 : 1;
    /* Ties keep submission order, so a frame is deterministic. */
    return ia < ib ? -1 : (ia > ib ? 1 : 0);
}

/*
 * Sort the translucent models back to front and lay their indices out in that
 * order at the tail of the frame's index staging.
 *
 * The order is only known once every model has been submitted, which is why
 * these are not in the main chain — but by END_3D it IS known, before the
 * frame's one upload. So they go up in that upload instead of one each.
 *
 * One upload each is what this replaced, and on macOS it cost more than the
 * whole world pass: every run was a glBufferSubData at offset 0 of the element
 * buffer the opaque draws had just been queued against, and Apple's GL waits
 * for the GPU to finish reading a buffer before it lets the CPU overwrite it.
 * Live Varrock, 2026-10-21: 9 ms of a 20 ms frame blocked in
 * GL3ZB_DrawAlphaPass -> glBufferSubData, which made the depth lane slower
 * than soft3d.
 */
void
GL3ZB_StageAlphaPass(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    uint32_t* staging)
{
    uint32_t staged = 0u;

    assert(renderer);
    if( renderer->alpha_submission_count == 0u )
        return;
    assert(staging);
    if( renderer->alpha_submission_count > renderer->alpha_order_capacity )
    {
        uint32_t cap = renderer->alpha_order_capacity ? renderer->alpha_order_capacity : 128u;
        uint32_t* grown;
        while( cap < renderer->alpha_submission_count )
            cap *= 2u;
        grown = (uint32_t*)ToriMem_Realloc(
            ToriMem_Renderer(), renderer->alpha_order, (size_t)cap * sizeof(uint32_t));
        assert(grown);
        renderer->alpha_order = grown;
        renderer->alpha_order_capacity = cap;
    }
    for( uint32_t i = 0u; i < renderer->alpha_submission_count; i++ )
        renderer->alpha_order[i] = i;
    /* Insertion sort: the count is the number of translucent models on screen,
     * which is small, and it keeps the tie-break stable without a qsort_r whose
     * argument order differs between platforms. */
    for( uint32_t i = 1u; i < renderer->alpha_submission_count; i++ )
    {
        const uint32_t key = renderer->alpha_order[i];
        uint32_t j = i;
        while( j > 0u &&
               gl3_alpha_order_cmp(
                   &renderer->alpha_order[j - 1u], &key, renderer->alpha_submissions) > 0 )
        {
            renderer->alpha_order[j] = renderer->alpha_order[j - 1u];
            j--;
        }
        renderer->alpha_order[j] = key;
    }

    for( uint32_t oi = 0u; oi < renderer->alpha_submission_count; oi++ )
    {
        struct GL3AlphaSubmission* sub =
            &renderer->alpha_submissions[renderer->alpha_order[oi]];
        sub->staged_start = staged;
        memcpy(
            staging + staged,
            renderer->alpha_indices + sub->index_start,
            (size_t)sub->index_count * sizeof(uint32_t));
        staged += sub->index_count;
    }
    assert(staged == renderer->alpha_index_count);
}

/*
 * Draw the staged translucent models, back to front, after the opaque pass.
 *
 * Depth-tested against the opaque geometry so a blended face behind a wall is
 * correctly hidden, but writing no depth: two translucent surfaces must both be
 * visible through each other, and a depth write from the nearer one would erase
 * the farther. That is the same split D3D9 makes.
 *
 * Neighbours in the sorted order that share a vertex group are contiguous in
 * the staged tail, so they draw as one call without changing the order.
 */
void
GL3ZB_DrawAlphaPass(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    uint32_t index_base)
{
    GLuint bound_vao = 0u;

    assert(renderer);
    if( renderer->alpha_submission_count == 0u )
        return;

    glDepthMask(GL_FALSE);
    for( uint32_t oi = 0u; oi < renderer->alpha_submission_count; )
    {
        const struct GL3AlphaSubmission* first =
            &renderer->alpha_submissions[renderer->alpha_order[oi]];
        uint32_t count = first->index_count;
        GLuint vao;

        for( oi++; oi < renderer->alpha_submission_count; oi++ )
        {
            const struct GL3AlphaSubmission* next =
                &renderer->alpha_submissions[renderer->alpha_order[oi]];
            if( next->group != first->group )
                break;
            count += next->index_count;
        }
        vao = renderer->groups[first->group].vao;
        if( vao != bound_vao )
        {
            glBindVertexArray(vao);
            bound_vao = vao;
        }
        glDrawElements(
            GL_TRIANGLES,
            (GLsizei)count,
            GL_UNSIGNED_INT,
            (const void*)(uintptr_t)((index_base + first->staged_start) * sizeof(uint32_t)));
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DRAW_CALLS, 1);
    }
    glDepthMask(GL_TRUE);
}


void
GL3ZB_Free(struct ToriPlatformSDL2_Renderer_GL3* renderer)
{
    if( !renderer )
        return;
    ToriMem_Free(ToriMem_Renderer(), renderer->alpha_submissions);
    ToriMem_Free(ToriMem_Renderer(), renderer->alpha_indices);
    ToriMem_Free(ToriMem_Renderer(), renderer->alpha_order);
    ToriMem_Free(ToriMem_Renderer(), renderer->model_indices);
    gl3_material_table_free(&renderer->materials);
    gl3_material_pose_clear(&renderer->dynamic_material);
    renderer->alpha_submissions = NULL;
    renderer->alpha_indices = NULL;
    renderer->alpha_order = NULL;
    renderer->model_indices = NULL;
    renderer->alpha_submission_capacity = 0u;
    renderer->alpha_index_capacity = 0u;
    renderer->alpha_order_capacity = 0u;
    renderer->model_index_capacity = 0u;
    GL3ZB_ResetFrame(renderer);
}

void
GL3ZB_ResetFrame(struct ToriPlatformSDL2_Renderer_GL3* renderer)
{
    assert(renderer);
    renderer->alpha_submission_count = 0u;
    renderer->alpha_index_count = 0u;
}

void
GL3ZB_BeginPass(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    int gl_x,
    int gl_y,
    int gl_w,
    int gl_h)
{
    assert(renderer);
    if( !renderer->z_buffer_enabled )
        return;
    /*
     * Once per world pass, scissored to the world viewport so the UI drawn
     * around it is untouched.
     *
     * The depth mask is forced on for the clear: glClear obeys it, and a clear
     * issued while the mask happens to be false silently does nothing — which
     * leaves last frame's depth to reject this frame's geometry, and looks like
     * random missing models rather than like a state bug.
     */
    glEnable(GL_SCISSOR_TEST);
    glScissor(gl_x, gl_y, gl_w, gl_h);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
}

void
GL3ZB_ApplyProjectionDepth(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    const struct ToriDraw_Camera* camera)
{
    float near_z;
    float far_z = TRSPK_GL3_WORLD_FAR;
    float range;

    assert(renderer);
    assert(camera);
    /*
     * Give the matrix a real depth row.
     *
     * The painter projection leaves clip.z constant (-1) because nothing reads
     * it; under a depth buffer that would put every fragment at the same depth
     * and the test would decide nothing. GL's clip volume is z in [-w, w],
     * unlike D3D's [0, w], so this is the GL mapping rather than a copy of
     * d3d9_set_projection_zbuffer.
     */
    near_z = (float)camera->near_plane_z;
    if( near_z < 1.0f )
        near_z = TRSPK_GL3_WORLD_NEAR;
    if( far_z <= near_z )
        far_z = near_z + 1.0f;
    range = far_z - near_z;
    renderer->proj[10] = (far_z + near_z) / range;
    renderer->proj[11] = 1.0f;
    renderer->proj[14] = -(2.0f * far_z * near_z) / range;
    renderer->proj[15] = 0.0f;
}

void
GL3ZB_BindDrawState(struct ToriPlatformSDL2_Renderer_GL3* renderer)
{
    (void)renderer;
    /* LEQUAL, not LESS: coplanar geometry submitted twice (a decor plane on its
     * floor tile) must keep the later one, which is what painter order did and
     * what the content is authored against. */
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    /* The GPU does the front-facing test, as it does on the D3D9 z-buffer
     * lane. The software check this replaces re-derived screen-space
     * winding from the projection for every face of every model.
     *
     * GL_CCW front + cull GL_BACK is the GL spelling of D3DCULL_CW, which
     * is the handedness measured on the D3D9 lane. It is NOT verified
     * here: these lanes do not run on the XP box, and a projection that
     * flips Y would flip the winding with it. If a model looks inside
     * out, that is the first thing to suspect -- glDisable(GL_CULL_FACE)
     * restores the previous drawing, since the depth buffer never needed
     * the cull to be correct. */
    glFrontFace(GL_CCW);
    glCullFace(GL_BACK);
    glEnable(GL_CULL_FACE);
}
