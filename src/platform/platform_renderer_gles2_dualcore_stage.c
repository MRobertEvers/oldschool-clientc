/*
 * The dual-core lane's model stage. See the header for the contract; see
 * gles2_draw_model (platform_renderer_gles2_core.c) for the sequence this
 * mirrors -- it is that function's first half, with the scene's bench
 * replaced by a scratch view's.
 */
#include "platform/platform_renderer_gles2_dualcore_stage.h"

#include "toridraw.h"
#include "toridraw_model.h"
#include "toridraw_scene.h"
#include "toridraw_types.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* The order store's floor: a Lumbridge frame sorts ~30k faces; a store that
 * starts at 256k ints (1 MB) never grows on that scene and grows once on a
 * larger one. */
#define GLES2_DUALCORE_STAGE_ORDER_CAPACITY_MIN (256u * 1024u)

struct GLES2DualCoreStageCrumb g_gles2_dualcore_stage_crumb;
#define CRUMB(s) (g_gles2_dualcore_stage_crumb.step = (s))

void
GLES2DualCoreStageArena_Init(struct GLES2DualCoreStageArena* arena)
{
    assert(arena);
    memset(arena, 0, sizeof(*arena));
    atomic_init(&arena->ready, 0u);
    atomic_init(&arena->finished, GLES2_DUALCORE_STAGE_DONE);
    arena->lead = GLES2_DUALCORE_STAGE_LEAD_DEFAULT;
}

void
GLES2DualCoreStageArena_Free(struct GLES2DualCoreStageArena* arena)
{
    if( !arena )
        return;
    free(arena->results);
    free(arena->claims);
    free(arena->orders);
    memset(arena, 0, sizeof(*arena));
}

void
GLES2DualCoreStageArena_BeginFrame(
    struct GLES2DualCoreStageArena* arena,
    uint32_t model_commands)
{
    assert(arena);
    /* Between frames: nobody reads the arena now, so the atomics are plain
     * stores as far as ordering goes; the thread hand-over that follows
     * (a mutex) publishes them. */
    assert(atomic_load_explicit(&arena->finished, memory_order_relaxed) !=
           GLES2_DUALCORE_STAGE_RUNNING);

    if( arena->exhausted )
    {
        /* Whatever ran out, double it; the frame that ran out is the size
         * the next one will be. */
        uint32_t const orders = arena->order_capacity ? arena->order_capacity * 2u
                                                      : GLES2_DUALCORE_STAGE_ORDER_CAPACITY_MIN;
        int32_t* grown = (int32_t*)realloc(arena->orders, (size_t)orders * sizeof(*grown));
        assert(grown);
        arena->orders = grown;
        arena->order_capacity = orders;
        arena->exhausted = false;
        arena->exhausted_frames++;
    }
    if( !arena->orders )
    {
        arena->orders = (int32_t*)malloc(
            (size_t)GLES2_DUALCORE_STAGE_ORDER_CAPACITY_MIN * sizeof(*arena->orders));
        assert(arena->orders);
        arena->order_capacity = GLES2_DUALCORE_STAGE_ORDER_CAPACITY_MIN;
    }
    if( model_commands > arena->result_capacity )
    {
        struct GLES2DualCoreStageResult* grown = (struct GLES2DualCoreStageResult*)realloc(
            arena->results, (size_t)model_commands * sizeof(*grown));
        atomic_uint* claims =
            (atomic_uint*)realloc(arena->claims, (size_t)model_commands * sizeof(*claims));
        assert(grown);
        assert(claims);
        arena->results = grown;
        arena->claims = claims;
        arena->result_capacity = model_commands;
    }
    /* Every slot unclaimed. A plain memset: nobody polls the claims between
     * frames, and the thread hand-over that follows publishes them. */
    memset(arena->claims, 0, (size_t)arena->result_capacity * sizeof(*arena->claims));
    arena->result_count = 0u;
    arena->order_count = 0u;
    atomic_store_explicit(&arena->ready, 0u, memory_order_relaxed);
    atomic_store_explicit(&arena->consumer_index, 0u, memory_order_relaxed);
    atomic_store_explicit(&arena->finished, GLES2_DUALCORE_STAGE_RUNNING, memory_order_relaxed);
}

enum GLES2DualCoreStageClaimResult
GLES2DualCoreStageArena_ClaimNextForProducer(struct GLES2DualCoreStageArena* arena)
{
    unsigned expected = GLES2_DUALCORE_CLAIM_FREE;

    uint32_t slot;
    uint32_t consumer;

    assert(arena);
    slot = arena->result_count;
    if( slot >= arena->result_capacity )
    {
        arena->exhausted = true;
        return GLES2_DUALCORE_CLAIM_EXHAUSTED;
    }
    /* The consumer is right behind: this slot is its. Handing it over is a
     * claim on its behalf (a lost race here means it claimed it already),
     * and the answer is the same either way. */
    consumer = atomic_load_explicit(&arena->consumer_index, memory_order_relaxed);
    if( slot <= consumer + arena->lead )
    {
        (void)atomic_compare_exchange_strong_explicit(
            &arena->claims[slot],
            &expected,
            GLES2_DUALCORE_CLAIM_CONSUMER,
            memory_order_acq_rel,
            memory_order_acquire);
        return GLES2_DUALCORE_CLAIM_TAKEN_BY_DRAW;
    }
    if( atomic_compare_exchange_strong_explicit(
            &arena->claims[slot],
            &expected,
            GLES2_DUALCORE_CLAIM_PRODUCER,
            memory_order_acq_rel,
            memory_order_acquire) )
        return GLES2_DUALCORE_CLAIMED;
    assert(expected == GLES2_DUALCORE_CLAIM_CONSUMER);
    return GLES2_DUALCORE_CLAIM_TAKEN_BY_DRAW;
}

void
GLES2DualCoreStageArena_PublishTakenByDraw(struct GLES2DualCoreStageArena* arena)
{
    struct GLES2DualCoreStageResult result;

    assert(arena);
    assert(arena->result_count < arena->result_capacity);
    memset(&result, 0, sizeof(result));
    result.cull = TORIDRAW_CULL_ERROR;
    result.taken_by_draw = 1u;
    arena->results[arena->result_count++] = result;
    atomic_store_explicit(&arena->ready, arena->result_count, memory_order_release);
}

bool
GLES2DualCoreStageArena_ClaimForConsumer(
    struct GLES2DualCoreStageArena* arena,
    uint32_t index)
{
    unsigned expected = GLES2_DUALCORE_CLAIM_FREE;

    assert(arena);
    assert(index < arena->result_capacity);
    return atomic_compare_exchange_strong_explicit(
        &arena->claims[index],
        &expected,
        GLES2_DUALCORE_CLAIM_CONSUMER,
        memory_order_acq_rel,
        memory_order_acquire);
}

void
GLES2DualCoreStage_BeginPass(
    struct GLES2DualCoreStageContext* context,
    const struct ToriRS_RenderCommand_Begin3D* command)
{
    assert(context);
    assert(command);
    assert(context->scene);
    context->view_port = command->view_port;
    context->camera = command->camera;
    context->in_pass = true;
    ToriDraw_ScenePrepareProjectionCamera(context->scene, &context->camera);
}

void
GLES2DualCoreStage_EndPass(struct GLES2DualCoreStageContext* context)
{
    assert(context);
    if( !context->in_pass )
        return;
    context->in_pass = false;
    ToriDraw_SceneClearProjectionCamera(context->scene);
}

bool
GLES2DualCoreStage_ModelHasBlendedFaces(struct ToriDraw_ModelHandle handle)
{
    const struct ToriDraw_Model* model;
    int face;

    if( !ToriDraw_ModelKindIsFull(handle.kind) )
        return false;
    model = handle.u.model.model;
    if( !model || !model->face_alphas )
        return false;
    for( face = 0; face < model->face_count; face++ )
    {
        int const raw_type = model->face_infos ? model->face_infos[face] : 0;
        unsigned alpha;
        if( raw_type == 2 || raw_type < 0 || raw_type > 3 ||
            model->face_colors_c[face] == TORIDRAWHSL16_HIDDEN )
            continue;
        alpha = 0xffu - (unsigned)(uint8_t)model->face_alphas[face];
        if( alpha <= 1u )
            continue;
        if( alpha != 0xffu )
            return true;
    }
    return false;
}

static bool
stage_pick_hit(
    struct GLES2DualCoreStageContext* context,
    const struct ToriRS_RenderCommand_Model* command)
{
    if( !context->pick_enabled || !command->pickable || command->element_id < 0 )
        return false;
    if( command->pick_aabb )
        return ToriDraw_ProjectedModelContainsAabb(
            context->scene, context->pick_mouse_x, context->pick_mouse_y);
    if( command->pick_terrain )
        return ToriDraw_ProjectedTileMouseHitTest(
            context->scene,
            command->model,
            &context->view_port,
            context->pick_mouse_x,
            context->pick_mouse_y);
    return ToriDraw_ProjectedModelMouseHitTest(
        context->scene,
        command->model,
        &context->view_port,
        context->pick_mouse_x,
        context->pick_mouse_y);
}

bool
GLES2DualCoreStage_ComputeModel(
    struct GLES2DualCoreStageContext* context,
    struct GLES2DualCoreStageArena* arena,
    const struct ToriRS_RenderCommand_Model* command)
{
    struct GLES2DualCoreStageResult result;
    struct ToriDraw_Position position;
    struct ToriDraw_Scene* scene;

    assert(context);
    assert(arena);
    assert(command);
    assert(context->in_pass);
    scene = context->scene;
    assert(scene);

    /* The slot was claimed by ClaimNextForProducer, which checked the room;
     * a caller that skipped the claim still gets the honest answer. */
    if( arena->result_count >= arena->result_capacity )
    {
        arena->exhausted = true;
        return false;
    }

    memset(&result, 0, sizeof(result));
    result.cull = TORIDRAW_CULL_VISIBLE;
    g_gles2_dualcore_stage_crumb.element_id = command->element_id;
    g_gles2_dualcore_stage_crumb.model_kind = (int)command->model.kind;
    g_gles2_dualcore_stage_crumb.model =
        ToriDraw_ModelKindIsFull(command->model.kind) ? (const void*)command->model.u.model.model
                                                      : (const void*)command->model.u.model.ground;
    g_gles2_dualcore_stage_crumb.anim_frame = command->animation ? command->anim_frame : -2;
    g_gles2_dualcore_stage_crumb.slot = (int)arena->result_count;

    /* gles2_draw_model's own early-out, mirrored so the counts stay paired:
     * such a command yields a result too -- one the draw never reads. */
    if( command->model.kind == TORIDRAWMK_NONE )
    {
        result.cull = TORIDRAW_CULL_ERROR;
        goto publish;
    }

    CRUMB(GLES2_DUALCORE_STEP_POSE);
    if( command->animation && command->element_id >= 0 )
        ToriDraw_SceneElementApplyAnimation(
            scene, command->element_id, command->anim_index == 0, command->anim_frame);
    CRUMB(GLES2_DUALCORE_STEP_PROJECT);
    position = command->position;
    result.cull = ToriDraw_RenderModel1ProjectWithTable(
        command->model, scene, &position, &context->view_port, &context->camera, context->kernel);
    if( result.cull != TORIDRAW_CULL_VISIBLE )
        goto publish;

    CRUMB(GLES2_DUALCORE_STEP_PICK);
    result.pick_hit = stage_pick_hit(context, command) ? 1u : 0u;
    result.projected_depth = scene->projected_vertex.z;
    if( command->pick_only )
        goto publish;
    CRUMB(GLES2_DUALCORE_STEP_SORT);

    /* The painter sorts every model; the depth path only one whose faces
     * must be blended, and tells the emit so with `sorted`. */
    if( !context->zbuffer || GLES2DualCoreStage_ModelHasBlendedFaces(command->model) )
    {
        int const count =
            ToriDraw_RenderModel2SortFacesWithTable(command->model, scene, context->kernel);
        result.sorted = 1u;
        result.sorted_face_count = count;
        if( count > 0 )
        {
            if( arena->order_count + (uint32_t)count > arena->order_capacity )
            {
                arena->exhausted = true;
                return false;
            }
            result.order_offset = arena->order_count;
            CRUMB(GLES2_DUALCORE_STEP_COPY);
            memcpy(
                arena->orders + arena->order_count,
                ToriDraw_FaceOrder(scene),
                (size_t)count * sizeof(*arena->orders));
            arena->order_count += (uint32_t)count;
        }
    }

publish:
    CRUMB(GLES2_DUALCORE_STEP_PUBLISH);
    arena->results[arena->result_count++] = result;
    atomic_store_explicit(&arena->ready, arena->result_count, memory_order_release);
    CRUMB(GLES2_DUALCORE_STEP_IDLE);
    return true;
}
