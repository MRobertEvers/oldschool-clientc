/*
 * Depth-buffered world pass. The rationale and the contract are in
 * platform_sdl2_renderer_webgl1zb.h; this file is the implementation.
 *
 * Everything it shares with the painter renderer — the renderer struct, the
 * constants, webgl1_bind_group_attribs, webgl1_ensure_gpu_ibo, webgl1_ensure_index16 —
 * comes from the internal header, so neither file owns a private copy of state
 * the other also writes.
 */

#include "platform/platform_sdl2_renderer_webgl1zb.h"
#include "toridraw_element_id.h"
#include <assert.h>

#include "platform/platform_sdl2_renderer_webgl1_internal.h"

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
static enum WebGL1WorldFacePass
webgl1_world_face_pass(
    struct ToriRS_GL3* renderer,
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
            return WEBGL1_WORLD_FACE_SKIP;
        raw_type = model->face_infos ? model->face_infos[face] : 0;
        if( raw_type == 2 || raw_type < 0 || raw_type > 3 ||
            model->face_colors_c[face] == TORIDRAWHSL16_HIDDEN )
            return WEBGL1_WORLD_FACE_SKIP;
        alpha = model->face_alphas ? (uint8_t)(0xffu - model->face_alphas[face]) : 0xffu;
        if( alpha <= 1u )
            return WEBGL1_WORLD_FACE_SKIP;
        if( alpha != 0xffu )
            return WEBGL1_WORLD_FACE_BLENDED;
        tex_id = model->face_textures ? (int)model->face_textures[face] : -1;
        if( tex_id >= 0 )
        {
            struct ToriDraw_Texture* texture =
                renderer && renderer->scene
                    ? ToriDraw_TextureMapGet(
                          &ToriDraw_SceneTexState(renderer->scene)->texture_map, tex_id)
                    : NULL;
            return texture && texture->opaque ? WEBGL1_WORLD_FACE_OPAQUE : WEBGL1_WORLD_FACE_CUTOUT;
        }
        return WEBGL1_WORLD_FACE_OPAQUE;
    }
    if( handle.kind == TORIDRAWMK_GROUND )
    {
        struct ToriDraw_ModelGround* ground = handle.u.model.ground;
        if( !ground || face >= (uint32_t)ground->face_count ||
            ground->face_colors_c[face] == TORIDRAWHSL16_HIDDEN )
            return WEBGL1_WORLD_FACE_SKIP;
        return WEBGL1_WORLD_FACE_OPAQUE;
    }
    return WEBGL1_WORLD_FACE_SKIP;
}

/* --- face classification cache ------------------------------------------- */

static void
webgl1_material_pose_clear(struct WebGL1MaterialPose* pose)
{
    assert(pose);
    free(pose->face_passes);
    memset(pose, 0, sizeof(*pose));
}

/* Classify every face of one baked pose, once. */
static bool
webgl1_material_pose_set(
    struct WebGL1MaterialPose* pose,
    struct ToriRS_GL3* renderer,
    struct ToriDraw_ModelHandle handle)
{
    int face_count = trspk_toridraw_face_count(handle);
    uint8_t* passes;

    if( face_count <= 0 )
        return false;
    assert(pose);
    passes = pose->face_count == (uint32_t)face_count && pose->face_passes
                 ? pose->face_passes
                 : (uint8_t*)realloc(pose->face_passes, (size_t)face_count);
    if( !passes )
        return false;
    pose->face_passes = passes;
    pose->face_count = (uint32_t)face_count;
    pose->opaque_count = 0u;
    pose->cutout_count = 0u;
    pose->blended_count = 0u;
    for( uint32_t face = 0u; face < pose->face_count; face++ )
    {
        enum WebGL1WorldFacePass pass = webgl1_world_face_pass(renderer, handle, face);
        pose->face_passes[face] = (uint8_t)pass;
        if( pass == WEBGL1_WORLD_FACE_OPAQUE )
            pose->opaque_count++;
        else if( pass == WEBGL1_WORLD_FACE_CUTOUT )
            pose->cutout_count++;
        else if( pass == WEBGL1_WORLD_FACE_BLENDED )
            pose->blended_count++;
    }
    return true;
}

static const struct WebGL1MaterialPose*
webgl1_material_table_get(
    const struct WebGL1MaterialTable* table,
    int element_id,
    int anim_index,
    int pose_id)
{
    const struct WebGL1MaterialTrack* track;
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

static const struct WebGL1MaterialPose*
webgl1_material_table_set(
    struct WebGL1MaterialTable* table,
    struct ToriRS_GL3* renderer,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle handle)
{
    struct WebGL1MaterialTrack* track;
    uint32_t needed;

    if( !table || element_id < 0 || anim_index < 0 ||
        anim_index >= TRSPK_POSE_TRACK_COUNT || pose_id < 0 )
        return NULL;
    needed = (uint32_t)ToriDraw_ElementIndexOfRaw(element_id) + 1u;
    if( needed > table->element_capacity )
    {
        uint32_t capacity = table->element_capacity ? table->element_capacity : 64u;
        struct WebGL1MaterialElement* grown;
        while( capacity < needed )
            capacity *= 2u;
        grown = (struct WebGL1MaterialElement*)realloc(
            table->elements, (size_t)capacity * sizeof(*grown));
        if( !grown )
            return NULL;
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
        struct WebGL1MaterialPose* grown = (struct WebGL1MaterialPose*)realloc(
            track->poses, (size_t)capacity * sizeof(*grown));
        while( capacity < needed )
        {
            capacity *= 2u;
            grown = (struct WebGL1MaterialPose*)realloc(
                track->poses, (size_t)capacity * sizeof(*grown));
        }
        if( !grown )
            return NULL;
        memset(
            grown + track->pose_capacity, 0,
            (size_t)(capacity - track->pose_capacity) * sizeof(*grown));
        track->poses = grown;
        track->pose_capacity = capacity;
    }
    if( track->pose_count < needed )
        track->pose_count = needed;
    if( !webgl1_material_pose_set(&track->poses[pose_id], renderer, handle) )
        return NULL;
    return &track->poses[pose_id];
}

/* Cached lookup, classifying on first use. */
static const struct WebGL1MaterialPose*
webgl1_material_for(
    struct ToriRS_GL3* renderer,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle handle)
{
    const struct WebGL1MaterialPose* pose =
        webgl1_material_table_get(&renderer->materials, element_id, anim_index, pose_id);
    if( pose && pose->face_count == (uint32_t)trspk_toridraw_face_count(handle) )
        return pose;
    return webgl1_material_table_set(
        &renderer->materials, renderer, element_id, anim_index, pose_id, handle);
}

void
WEBGL1ZB_ForgetElement(
    struct ToriRS_GL3* renderer,
    int element_id)
{
    struct WebGL1MaterialTable* table;
    assert(renderer);
    table = &renderer->materials;
    if( !table->elements || element_id < 0 || (uint32_t)ToriDraw_ElementIndexOfRaw(element_id) >= table->element_count )
        return;
    for( int t = 0; t < TRSPK_POSE_TRACK_COUNT; t++ )
    {
        struct WebGL1MaterialTrack* track = &table->elements[ToriDraw_ElementIndexOfRaw(element_id)].tracks[t];
        for( uint32_t i = 0u; i < track->pose_count; i++ )
            webgl1_material_pose_clear(&track->poses[i]);
        free(track->poses);
        track->poses = NULL;
        track->pose_count = 0u;
        track->pose_capacity = 0u;
    }
}

void
WEBGL1ZB_ForgetTrack(
    struct ToriRS_GL3* renderer,
    int element_id,
    int anim_index)
{
    struct WebGL1MaterialTable* table;
    struct WebGL1MaterialTrack* track;
    assert(renderer);
    table = &renderer->materials;
    if( !table->elements || element_id < 0 || (uint32_t)ToriDraw_ElementIndexOfRaw(element_id) >= table->element_count ||
        anim_index < 0 || anim_index >= TRSPK_POSE_TRACK_COUNT )
        return;
    track = &table->elements[ToriDraw_ElementIndexOfRaw(element_id)].tracks[anim_index];
    for( uint32_t i = 0u; i < track->pose_count; i++ )
        webgl1_material_pose_clear(&track->poses[i]);
    free(track->poses);
    track->poses = NULL;
    track->pose_count = 0u;
    track->pose_capacity = 0u;
}

static void
webgl1_material_table_free(struct WebGL1MaterialTable* table)
{
    if( !table || !table->elements )
        return;
    for( uint32_t e = 0u; e < table->element_count; e++ )
        for( int t = 0; t < TRSPK_POSE_TRACK_COUNT; t++ )
        {
            struct WebGL1MaterialTrack* track = &table->elements[e].tracks[t];
            for( uint32_t i = 0u; i < track->pose_count; i++ )
                webgl1_material_pose_clear(&track->poses[i]);
            free(track->poses);
        }
    free(table->elements);
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
webgl1_reserve_model_indices(
    struct ToriRS_GL3* renderer,
    uint32_t count)
{
    if( count <= renderer->model_index_capacity )
        return true;
    {
        uint32_t cap = renderer->model_index_capacity ? renderer->model_index_capacity : 1024u;
        uint32_t* grown;
        while( cap < count )
            cap *= 2u;
        grown = (uint32_t*)realloc(renderer->model_indices, (size_t)cap * sizeof(uint32_t));
        assert(grown);
        renderer->model_indices = grown;
        renderer->model_index_capacity = cap;
    }
    return true;
}

/* Hold one model's translucent faces for the sorted pass. */
static bool
webgl1_queue_alpha_submission(
    struct ToriRS_GL3* renderer,
    uint32_t group,
    int depth,
    const uint32_t* indices,
    uint32_t index_count)
{
    struct WebGL1AlphaSubmission* submission;

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
        grown = (uint32_t*)realloc(renderer->alpha_indices, (size_t)cap * sizeof(uint32_t));
        assert(grown);
        renderer->alpha_indices = grown;
        renderer->alpha_index_capacity = cap;
    }
    if( renderer->alpha_submission_count >= renderer->alpha_submission_capacity )
    {
        uint32_t cap =
            renderer->alpha_submission_capacity ? renderer->alpha_submission_capacity * 2u : 128u;
        struct WebGL1AlphaSubmission* grown = (struct WebGL1AlphaSubmission*)realloc(
            renderer->alpha_submissions, (size_t)cap * sizeof(*grown));
        if( !grown )
            return false;
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
WEBGL1ZB_SubmitModel(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand_Model const* mcmd,
    struct ToriDraw_Scene* ctx,
    uint32_t group,
    uint32_t vertex_base,
    int face_count)
{
    const struct WebGL1MaterialPose* material;
    uint32_t written = 0u;
    int sorted_face_count;
    int* face_order;
    int anim_index = mcmd->anim_index < 0 ? 0 : mcmd->anim_index;
    int pose_id = mcmd->animation && mcmd->anim_frame >= 0 ? mcmd->anim_frame : 0;

    if( face_count <= 0 || !webgl1_reserve_model_indices(renderer, (uint32_t)face_count * 3u) )
        return;
    if( anim_index >= TRSPK_POSE_TRACK_COUNT )
        anim_index = TRSPK_POSE_TRACK_COUNT - 1;

    /* Classified once per baked pose, not once per frame. */
    material = webgl1_material_for(
        renderer, mcmd->element_id, anim_index, pose_id, mcmd->model);
    if( !material || material->face_count != (uint32_t)face_count )
        return;

    for( int i = 0; i < face_count; i++ )
    {
        const uint32_t face = (uint32_t)i;
        const uint8_t pass = material->face_passes[face];
        uint32_t b;

        if( pass != WEBGL1_WORLD_FACE_OPAQUE && pass != WEBGL1_WORLD_FACE_CUTOUT )
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
        ToriDraw_RenderModel2SortFacesWithKernel(mcmd->model, ctx, renderer->kernel);
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
            material->face_passes[face] != WEBGL1_WORLD_FACE_BLENDED )
            continue;
        b = vertex_base + face * 3u;
        renderer->model_indices[written++] = b;
        renderer->model_indices[written++] = b + 1u;
        renderer->model_indices[written++] = b + 2u;
    }
    if( written == 0u )
        return;
    (void)webgl1_queue_alpha_submission(
        renderer, group, ctx->projected_vertex.z, renderer->model_indices, written);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_Z_BLENDED_TRIANGLES, written / 3u);
}

/*
 * Upload and draw one contiguous run of absolute 32-bit indices.
 *
 * The blended pass cannot share the frame's main index upload: its order is
 * only known after every model has been submitted. Runs are per translucent
 * model and there are few of them, so an upload each is cheaper than the
 * bookkeeping to merge them.
 *
 * On WebGL1 the run still has to be expressed in 16-bit windows, which is the
 * same sliding split the main pass uses — inlined here because it is over one
 * contiguous array rather than a draw-range list.
 */
static void
webgl1_draw_indices32(
    struct ToriRS_GL3* renderer,
    uint32_t group,
    const uint32_t* idx,
    uint32_t count)
{
    if( count < 3u || !webgl1_ensure_gpu_ibo(renderer, count) )
        return;
    if( !webgl1_ensure_index16(renderer, count) )
        return;
    {
        uint32_t i = 0u;
        while( i + 2u < count )
        {
            uint32_t lo = idx[i];
            uint32_t hi = idx[i];
            const uint32_t start = i;
            uint32_t span;
            for( uint32_t k = 1u; k < 3u; k++ )
            {
                if( idx[i + k] < lo )
                    lo = idx[i + k];
                if( idx[i + k] > hi )
                    hi = idx[i + k];
            }
            i += 3u;
            while( i + 2u < count )
            {
                uint32_t nlo = lo;
                uint32_t nhi = hi;
                for( uint32_t k = 0u; k < 3u; k++ )
                {
                    if( idx[i + k] < nlo )
                        nlo = idx[i + k];
                    if( idx[i + k] > nhi )
                        nhi = idx[i + k];
                }
                if( nhi - nlo > 65535u )
                    break;
                lo = nlo;
                hi = nhi;
                i += 3u;
            }
            span = i - start;
            for( uint32_t k = 0u; k < span; k++ )
                renderer->idx16[k] = (uint16_t)(idx[start + k] - lo);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->ebo);
            glBufferSubData(
                GL_ELEMENT_ARRAY_BUFFER, 0, (GLsizeiptr)(span * sizeof(uint16_t)),
                renderer->idx16);
            webgl1_bind_group_attribs(renderer, &renderer->groups[group], lo);
            glDrawElements(GL_TRIANGLES, (GLsizei)span, GL_UNSIGNED_SHORT, (const void*)0);
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DRAW_CALLS, 1);
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_ATTRIB_REBINDS, 1);
        }
    }
}

static int
webgl1_alpha_order_cmp(
    void const* a,
    void const* b,
    void* user)
{
    const struct WebGL1AlphaSubmission* subs = (const struct WebGL1AlphaSubmission*)user;
    const uint32_t ia = *(const uint32_t*)a;
    const uint32_t ib = *(const uint32_t*)b;
    /* Back to front: larger depth first. */
    if( subs[ia].depth != subs[ib].depth )
        return subs[ia].depth > subs[ib].depth ? -1 : 1;
    /* Ties keep submission order, so a frame is deterministic. */
    return ia < ib ? -1 : (ia > ib ? 1 : 0);
}

/*
 * Draw the translucent models, back to front, after the opaque pass.
 *
 * Depth-tested against the opaque geometry so a blended face behind a wall is
 * correctly hidden, but writing no depth: two translucent surfaces must both be
 * visible through each other, and a depth write from the nearer one would erase
 * the farther. That is the same split D3D9 makes.
 *
 * The index stream is rebuilt here rather than folded into the main chain
 * because the order is only known once every model has been submitted.
 */
void
WEBGL1ZB_DrawAlphaPass(struct ToriRS_GL3* renderer)
{
    if( !renderer->z_buffer_enabled || renderer->alpha_submission_count == 0u )
        return;
    if( renderer->alpha_submission_count > renderer->alpha_order_capacity )
    {
        uint32_t cap = renderer->alpha_order_capacity ? renderer->alpha_order_capacity : 128u;
        uint32_t* grown;
        while( cap < renderer->alpha_submission_count )
            cap *= 2u;
        grown = (uint32_t*)realloc(renderer->alpha_order, (size_t)cap * sizeof(uint32_t));
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
               webgl1_alpha_order_cmp(
                   &renderer->alpha_order[j - 1u], &key, renderer->alpha_submissions) > 0 )
        {
            renderer->alpha_order[j] = renderer->alpha_order[j - 1u];
            j--;
        }
        renderer->alpha_order[j] = key;
    }

    glDepthMask(GL_FALSE);
    for( uint32_t oi = 0u; oi < renderer->alpha_submission_count; oi++ )
    {
        const struct WebGL1AlphaSubmission* sub =
            &renderer->alpha_submissions[renderer->alpha_order[oi]];
        if( sub->index_count == 0u )
            continue;
        webgl1_draw_indices32(
            renderer,
            sub->group,
            renderer->alpha_indices + sub->index_start,
            sub->index_count);
    }
    glDepthMask(GL_TRUE);
}


void
WEBGL1ZB_Free(struct ToriRS_GL3* renderer)
{
    if( !renderer )
        return;
    free(renderer->alpha_submissions);
    free(renderer->alpha_indices);
    free(renderer->alpha_order);
    free(renderer->model_indices);
    webgl1_material_table_free(&renderer->materials);
    renderer->alpha_submissions = NULL;
    renderer->alpha_indices = NULL;
    renderer->alpha_order = NULL;
    renderer->model_indices = NULL;
    renderer->alpha_submission_capacity = 0u;
    renderer->alpha_index_capacity = 0u;
    renderer->alpha_order_capacity = 0u;
    renderer->model_index_capacity = 0u;
    WEBGL1ZB_ResetFrame(renderer);
}

void
WEBGL1ZB_ResetFrame(struct ToriRS_GL3* renderer)
{
    assert(renderer);
    renderer->alpha_submission_count = 0u;
    renderer->alpha_index_count = 0u;
}

void
WEBGL1ZB_BeginPass(
    struct ToriRS_GL3* renderer,
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
WEBGL1ZB_ApplyProjectionDepth(
    struct ToriRS_GL3* renderer,
    const struct ToriDraw_Camera* camera)
{
    float near_z;
    float far_z = TRSPK_WEBGL1_WORLD_FAR;
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
        near_z = TRSPK_WEBGL1_WORLD_NEAR;
    if( far_z <= near_z )
        far_z = near_z + 1.0f;
    range = far_z - near_z;
    renderer->proj[10] = (far_z + near_z) / range;
    renderer->proj[11] = 1.0f;
    renderer->proj[14] = -(2.0f * far_z * near_z) / range;
    renderer->proj[15] = 0.0f;
}

void
WEBGL1ZB_BindDrawState(struct ToriRS_GL3* renderer)
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
