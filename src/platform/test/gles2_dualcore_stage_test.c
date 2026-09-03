/*
 * The dual-core lane's model stage, run on a scratch view of a scene next to
 * the same stage run on the scene itself, must give the same answers -- and
 * the two benches must not see each other.
 *
 * What is proved here, without a GPU and without a thread:
 *
 *   1. PARITY. For a set of models (some with translucent faces, some picked
 *      by the mouse point, some culled), the cull verdict, the pick answer,
 *      the projected depth, the sorted count and the face order the stage
 *      appends to its arena are what ToriDraw_RenderModel1ProjectWithTable /
 *      the pick tests / ToriDraw_RenderModel2SortFacesWithTable give on the
 *      scene's own bench.
 *   2. INDEPENDENCE. Interleaving the view's stage with projections and sorts
 *      on the scene leaves both sets of answers unchanged: a scratch view
 *      owns its scratch.
 *   3. THE DEPTH PATH'S SORT GATE. In zbuffer mode only a model with a
 *      blended face is sorted, and GLES2DualCoreStage_ModelHasBlendedFaces
 *      agrees with the alpha rule the renderer classifies faces by.
 *   4. EXHAUSTION. When the arena runs out of results or of order storage
 *      the stage appends nothing, says so, and the next frame's arena is
 *      larger.
 *   5. CLAIMS. A model is computed by whichever side claims it first; the
 *      other side sees the claim, and a placeholder keeps the count paired.
 *   6. SYNC. A view re-synced after the scene gained an element sees it.
 *
 * Run on both scene tiers: the SMALL tier (the client's, CSR sorter and the
 * bitonic+radix keys) and the FULL one (dense bucket tables), since the
 * view allocates whichever groups the scene has resident.
 *
 * Build/run: make -C src test-gles2-dualcore-stage
 */
#include "platform/platform_renderer_gles2_dualcore_stage.h"

#include "toridraw.h"
#include "toridraw_model.h"
#include "toridraw_raster_kernel.h"
#include "toridraw_scene.h"

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIEW_W 256
#define VIEW_H 256
#define CAMERA_DISTANCE 900
#define EXTENT 160
#define MODEL_COUNT 24
#define MODEL_VERTICES 24
#define MODEL_FACES 40

static int failures;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            failures++;                                                                            \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                   \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fputc('\n', stderr);                                                                   \
        }                                                                                          \
    } while( 0 )

/* --- a deterministic model cloud ------------------------------------------------ */

static uint32_t rng_state = 0x9E3779B9u;

static uint32_t
rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static int
rng_range(int lo, int hi)
{
    return lo + (int)(rng() % (uint32_t)(hi - lo + 1));
}

/* A model the scene owns: a vertex cloud in a box, faces over random distinct
 * triples. `translucent` gives every fourth face an alpha the renderer would
 * class as blended; `hide_some` hides a few faces outright, which the sort
 * drops and the blend test must skip. */
static struct ToriDraw_Model*
make_model(bool translucent, bool hide_some)
{
    struct ToriDraw_Model* model = ToriDraw_ModelNew(MODEL_VERTICES, MODEL_FACES, 0);
    int i;

    model->vertices_x = calloc(MODEL_VERTICES, sizeof(vertexint_t));
    model->vertices_y = calloc(MODEL_VERTICES, sizeof(vertexint_t));
    model->vertices_z = calloc(MODEL_VERTICES, sizeof(vertexint_t));
    model->face_indices_a = calloc(MODEL_FACES, sizeof(faceint_t));
    model->face_indices_b = calloc(MODEL_FACES, sizeof(faceint_t));
    model->face_indices_c = calloc(MODEL_FACES, sizeof(faceint_t));
    model->face_colors_a = calloc(MODEL_FACES, sizeof(hsl16_t));
    model->face_colors_b = calloc(MODEL_FACES, sizeof(hsl16_t));
    model->face_colors_c = calloc(MODEL_FACES, sizeof(hsl16_t));
    model->face_infos = calloc(MODEL_FACES, sizeof(int));
    if( translucent )
        model->face_alphas = calloc(MODEL_FACES, sizeof(alphaint_t));
    assert(model->vertices_x && model->vertices_y && model->vertices_z);
    assert(model->face_indices_a && model->face_indices_b && model->face_indices_c);
    assert(model->face_colors_a && model->face_colors_b && model->face_colors_c);
    assert(model->face_infos);

    for( i = 0; i < MODEL_VERTICES; i++ )
    {
        model->vertices_x[i] = (vertexint_t)rng_range(-EXTENT, EXTENT);
        model->vertices_y[i] = (vertexint_t)rng_range(-EXTENT, EXTENT);
        model->vertices_z[i] = (vertexint_t)rng_range(-EXTENT, EXTENT);
    }
    for( i = 0; i < MODEL_FACES; i++ )
    {
        int a = rng_range(0, MODEL_VERTICES - 1);
        int b = rng_range(0, MODEL_VERTICES - 1);
        int c = rng_range(0, MODEL_VERTICES - 1);
        while( b == a )
            b = rng_range(0, MODEL_VERTICES - 1);
        while( c == a || c == b )
            c = rng_range(0, MODEL_VERTICES - 1);
        model->face_indices_a[i] = (faceint_t)a;
        model->face_indices_b[i] = (faceint_t)b;
        model->face_indices_c[i] = (faceint_t)c;
        model->face_colors_a[i] = (hsl16_t)rng_range(1, 0x7FFE);
        model->face_colors_b[i] = (hsl16_t)rng_range(1, 0x7FFE);
        model->face_colors_c[i] = (hsl16_t)rng_range(1, 0x7FFE);
        model->face_infos[i] = 0;
        if( hide_some && (i % 7) == 3 )
            model->face_colors_c[i] = TORIDRAWHSL16_HIDDEN;
        if( translucent && (i % 4) == 1 )
            /* The renderer's rule reads 0xff - alpha: 0x40 stored is 0xbf drawn,
             * a blended face. */
            model->face_alphas[i] = (alphaint_t)0x40;
    }
    model->has_bounds_cylinder = true;
    model->bounds_cylinder.radius = EXTENT * 2;
    model->bounds_cylinder.min_y = -EXTENT;
    model->bounds_cylinder.max_y = EXTENT;
    model->bounds_cylinder.center_to_top_edge = EXTENT * 2;
    model->bounds_cylinder.center_to_bottom_edge = EXTENT * 2;
    model->bounds_cylinder.min_z_depth_any_rotation = EXTENT * 2;
    return model;
}

static struct ToriDraw_ViewPort
viewport(void)
{
    struct ToriDraw_ViewPort vp = {
        .width = VIEW_W,
        .height = VIEW_H,
        .stride = VIEW_W,
        .x_center = VIEW_W / 2,
        .y_center = VIEW_H / 2,
        .clip_left = 0,
        .clip_top = 0,
        .clip_right = VIEW_W,
        .clip_bottom = VIEW_H,
    };
    return vp;
}

static struct ToriDraw_Camera
camera(void)
{
    struct ToriDraw_Camera cam = {
        .projection_mode = TORIDRAW_PROJECTION_MODE_SCALE,
        .projection_scale = TORIDRAW_PROJECTION_SCALE_DEFAULT,
        .near_plane_z = 50,
    };
    return cam;
}

/* --- the scene under test -------------------------------------------------------- */

struct Fixture
{
    struct ToriDraw_Scene* scene;
    const struct ToriDraw_Kernel* kernel;
    struct ToriDraw_ViewPort view_port;
    /* Stable address: the prepared projection block is keyed on it. */
    struct ToriDraw_Camera camera;
    struct ToriRS_RenderCommand_Model commands[MODEL_COUNT];
    int element_ids[MODEL_COUNT];
    int mouse_x;
    int mouse_y;
};

static void
fixture_init(struct Fixture* f, uint32_t scene_flags)
{
    int i;

    memset(f, 0, sizeof(*f));
    rng_state = 0x9E3779B9u;
    f->scene = ToriDraw_SceneNew(scene_flags, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    assert(f->scene);
    f->kernel = ToriDraw_KernelGetGpu();
    assert(f->kernel);
    f->view_port = viewport();
    f->camera = camera();
    f->mouse_x = VIEW_W / 2;
    f->mouse_y = VIEW_H / 2;

    for( i = 0; i < MODEL_COUNT; i++ )
    {
        struct ToriDraw_Model* model = make_model((i % 3) == 1, (i % 5) == 2);
        struct ToriRS_RenderCommand_Model* command = &f->commands[i];
        int const id = ToriDraw_SceneElementAdd(f->scene);
        assert(id >= 0);
        ToriDraw_SceneElementSetModel(f->scene, id, ToriDraw_ModelHandleOwned(model));
        f->element_ids[i] = id;

        command->model = ToriDraw_SceneElementGet(f->scene, id)->model;
        command->element_id = id;
        /* A spread of placements: on the axis (the mouse point hits), off to
         * a side, and far enough off to be culled. */
        command->position.x = (i % 4) == 0 ? 0 : rng_range(-EXTENT * 3, EXTENT * 3);
        command->position.y = (i % 4) == 0 ? 0 : rng_range(-EXTENT, EXTENT);
        command->position.z = CAMERA_DISTANCE + rng_range(-EXTENT, EXTENT * 2);
        /* Behind the camera: the near plane culls it. */
        if( (i % 6) == 5 )
            command->position.z = -CAMERA_DISTANCE;
        command->position.yaw = rng_range(0, 2047);
        command->world_position = command->position;
        command->pickable = true;
        command->pick_aabb = (i % 2) == 0;
        command->pick_only = (i % 9) == 8;
        command->anim_index = 0;
        command->anim_frame = -1;
    }
}

static void
fixture_free(struct Fixture* f)
{
    ToriDraw_SceneFree(f->scene);
    memset(f, 0, sizeof(*f));
}

/* --- the reference: the same stage, on the scene's own bench ------------------ */

struct Reference
{
    int cull;
    bool pick_hit;
    int depth;
    bool sorted;
    int sorted_face_count;
    int order[MODEL_FACES];
};

static void
reference_stage(
    struct Fixture* f,
    const struct ToriRS_RenderCommand_Model* command,
    bool zbuffer,
    struct Reference* out)
{
    struct ToriDraw_Position position = command->position;

    memset(out, 0, sizeof(*out));
    out->cull = ToriDraw_RenderModel1ProjectWithTable(
        command->model, f->scene, &position, &f->view_port, &f->camera, f->kernel);
    if( out->cull != TORIDRAW_CULL_VISIBLE )
        return;
    out->pick_hit = command->pick_aabb
                        ? ToriDraw_ProjectedModelContainsAabb(f->scene, f->mouse_x, f->mouse_y)
                        : ToriDraw_ProjectedModelMouseHitTest(
                              f->scene, command->model, &f->view_port, f->mouse_x, f->mouse_y);
    out->depth = f->scene->projected_vertex.z;
    if( command->pick_only )
        return;
    if( zbuffer && !GLES2DualCoreStage_ModelHasBlendedFaces(command->model) )
        return;
    out->sorted = true;
    out->sorted_face_count =
        ToriDraw_RenderModel2SortFacesWithTable(command->model, f->scene, f->kernel);
    if( out->sorted_face_count > 0 )
    {
        assert(out->sorted_face_count <= MODEL_FACES);
        memcpy(
            out->order,
            ToriDraw_FaceOrder(f->scene),
            (size_t)out->sorted_face_count * sizeof(out->order[0]));
    }
}

static void
check_result_against_reference(
    const struct GLES2DualCoreStageArena* arena,
    uint32_t index,
    const struct Reference* ref,
    const struct ToriRS_RenderCommand_Model* command,
    char const* what)
{
    const struct GLES2DualCoreStageResult* r = &arena->results[index];
    (void)command;
    CHECK(r->cull == ref->cull, "%s: model %u cull %d vs %d", what, index, r->cull, ref->cull);
    if( ref->cull != TORIDRAW_CULL_VISIBLE )
        return;
    CHECK((r->pick_hit != 0) == ref->pick_hit, "%s: model %u pick %d vs %d", what, index,
        r->pick_hit, ref->pick_hit);
    CHECK(r->projected_depth == ref->depth, "%s: model %u depth %d vs %d", what, index,
        r->projected_depth, ref->depth);
    CHECK((r->sorted != 0) == ref->sorted, "%s: model %u sorted %d vs %d", what, index,
        r->sorted, ref->sorted);
    if( !ref->sorted )
        return;
    CHECK(r->sorted_face_count == ref->sorted_face_count, "%s: model %u count %d vs %d", what,
        index, r->sorted_face_count, ref->sorted_face_count);
    if( r->sorted_face_count == ref->sorted_face_count && ref->sorted_face_count > 0 )
        CHECK(
            memcmp(
                arena->orders + r->order_offset,
                ref->order,
                (size_t)ref->sorted_face_count * sizeof(int32_t)) == 0,
            "%s: model %u face order differs", what, index);
}

/* --- the cases ------------------------------------------------------------------------ */

static void
context_init(
    struct GLES2DualCoreStageContext* context,
    struct Fixture* f,
    struct ToriDraw_Scene* view,
    bool zbuffer)
{
    struct ToriRS_RenderCommand_Begin3D begin;
    memset(context, 0, sizeof(*context));
    memset(&begin, 0, sizeof(begin));
    begin.view_port = f->view_port;
    begin.camera = f->camera;
    context->scene = view;
    context->kernel = f->kernel;
    context->pick_enabled = true;
    context->pick_mouse_x = f->mouse_x;
    context->pick_mouse_y = f->mouse_y;
    context->zbuffer = zbuffer;
    GLES2DualCoreStage_BeginPass(context, &begin);
}

static void
case_parity(struct Fixture* f, bool zbuffer)
{
    struct ToriDraw_Scene* view = ToriDraw_SceneScratchViewNew(f->scene);
    struct GLES2DualCoreStageArena arena;
    struct GLES2DualCoreStageContext context;
    struct Reference refs[MODEL_COUNT];
    int visible = 0;
    int picked = 0;
    int sorted = 0;
    int i;
    char const* what = zbuffer ? "parity/zbuffer" : "parity/painter";

    ToriDraw_ScenePrepareProjectionCamera(f->scene, &f->camera);
    for( i = 0; i < MODEL_COUNT; i++ )
    {
        reference_stage(f, &f->commands[i], zbuffer, &refs[i]);
        visible += refs[i].cull == TORIDRAW_CULL_VISIBLE;
        picked += refs[i].pick_hit;
        sorted += refs[i].sorted;
    }
    /* The fixture must exercise every branch, or the parity is vacuous. */
    CHECK(visible > MODEL_COUNT / 2, "%s: only %d visible", what, visible);
    CHECK(visible < MODEL_COUNT, "%s: nothing culled", what);
    CHECK(picked > 0, "%s: nothing picked", what);
    CHECK(sorted > 0, "%s: nothing sorted", what);
    if( zbuffer )
        CHECK(sorted < visible, "%s: every visible model sorted on the depth path", what);

    GLES2DualCoreStageArena_Init(&arena);
    GLES2DualCoreStageArena_BeginFrame(&arena, MODEL_COUNT);
    context_init(&context, f, view, zbuffer);
    for( i = 0; i < MODEL_COUNT; i++ )
        CHECK(GLES2DualCoreStage_ComputeModel(&context, &arena, &f->commands[i]),
            "%s: model %d refused", what, i);
    GLES2DualCoreStage_EndPass(&context);
    CHECK(atomic_load(&arena.ready) == MODEL_COUNT, "%s: ready %u", what, atomic_load(&arena.ready));
    for( i = 0; i < MODEL_COUNT; i++ )
        check_result_against_reference(&arena, (uint32_t)i, &refs[i], &f->commands[i], what);

    GLES2DualCoreStageArena_Free(&arena);
    ToriDraw_SceneClearProjectionCamera(f->scene);
    ToriDraw_SceneScratchViewFree(view);
}

/* The view's stage and the scene's own projection and sort, interleaved at
 * every step, both still right. */
static void
case_independence(struct Fixture* f)
{
    struct ToriDraw_Scene* view = ToriDraw_SceneScratchViewNew(f->scene);
    struct GLES2DualCoreStageArena arena;
    struct GLES2DualCoreStageContext context;
    struct Reference refs[MODEL_COUNT];
    int i;

    ToriDraw_ScenePrepareProjectionCamera(f->scene, &f->camera);
    for( i = 0; i < MODEL_COUNT; i++ )
        reference_stage(f, &f->commands[i], false, &refs[i]);

    GLES2DualCoreStageArena_Init(&arena);
    GLES2DualCoreStageArena_BeginFrame(&arena, MODEL_COUNT);
    context_init(&context, f, view, false);
    for( i = 0; i < MODEL_COUNT; i++ )
    {
        /* The scene projects model i+1 while the view is between its
         * projection and its sort of model i: neither bench may show it. */
        struct ToriDraw_Position position = f->commands[(i + 1) % MODEL_COUNT].position;
        int const next = (i + 1) % MODEL_COUNT;
        int cull;
        CHECK(GLES2DualCoreStage_ComputeModel(&context, &arena, &f->commands[i]),
            "independence: model %d refused", i);
        cull = ToriDraw_RenderModel1ProjectWithTable(
            f->commands[next].model, f->scene, &position, &f->view_port, &f->camera, f->kernel);
        CHECK(cull == refs[next].cull, "independence: scene cull of %d changed", next);
        if( cull == TORIDRAW_CULL_VISIBLE && !f->commands[next].pick_only )
        {
            int const count = ToriDraw_RenderModel2SortFacesWithTable(
                f->commands[next].model, f->scene, f->kernel);
            CHECK(count == refs[next].sorted_face_count, "independence: scene sort count of %d changed",
                next);
            if( count == refs[next].sorted_face_count && count > 0 )
                CHECK(memcmp(ToriDraw_FaceOrder(f->scene), refs[next].order,
                          (size_t)count * sizeof(int)) == 0,
                    "independence: scene face order of %d changed", next);
        }
    }
    GLES2DualCoreStage_EndPass(&context);
    for( i = 0; i < MODEL_COUNT; i++ )
        check_result_against_reference(&arena, (uint32_t)i, &refs[i], &f->commands[i],
            "independence");

    GLES2DualCoreStageArena_Free(&arena);
    ToriDraw_SceneClearProjectionCamera(f->scene);
    ToriDraw_SceneScratchViewFree(view);
}

static void
case_blended_gate(struct Fixture* f)
{
    int with = 0;
    int without = 0;
    int i;
    for( i = 0; i < MODEL_COUNT; i++ )
    {
        bool const has = GLES2DualCoreStage_ModelHasBlendedFaces(f->commands[i].model);
        bool const built_translucent = (i % 3) == 1;
        CHECK(has == built_translucent, "blended gate: model %d says %d, built %d", i, has,
            built_translucent);
        with += has;
        without += !has;
    }
    CHECK(with > 0 && without > 0, "blended gate: fixture has %d with, %d without", with, without);
    {
        /* A translucent face that is hidden does not count. */
        struct ToriDraw_Model* model = make_model(true, false);
        struct ToriDraw_ModelHandle handle = ToriDraw_ModelHandleOwned(model);
        CHECK(GLES2DualCoreStage_ModelHasBlendedFaces(handle), "blended gate: fresh translucent");
        for( i = 0; i < MODEL_FACES; i++ )
            if( model->face_alphas[i] != 0 )
                model->face_colors_c[i] = TORIDRAWHSL16_HIDDEN;
        CHECK(!GLES2DualCoreStage_ModelHasBlendedFaces(handle), "blended gate: all hidden");
        for( i = 0; i < MODEL_FACES; i++ )
            if( model->face_alphas[i] != 0 )
            {
                model->face_colors_c[i] = 100;
                /* 0xff stored reads as alpha 0: not drawn at all. */
                model->face_alphas[i] = (alphaint_t)0xff;
            }
        CHECK(!GLES2DualCoreStage_ModelHasBlendedFaces(handle), "blended gate: all invisible");
        ToriDraw_ModelFree(model);
    }
}

static void
case_exhaustion(struct Fixture* f)
{
    struct ToriDraw_Scene* view = ToriDraw_SceneScratchViewNew(f->scene);
    struct GLES2DualCoreStageArena arena;
    struct GLES2DualCoreStageContext context;
    uint32_t orders_before;
    int visible_unsorted_free = -1;
    int i;

    /* Find a visible model the painter sorts, for the order-store case. */
    ToriDraw_ScenePrepareProjectionCamera(f->scene, &f->camera);
    for( i = 0; i < MODEL_COUNT && visible_unsorted_free < 0; i++ )
    {
        struct Reference ref;
        reference_stage(f, &f->commands[i], false, &ref);
        if( ref.cull == TORIDRAW_CULL_VISIBLE && ref.sorted && ref.sorted_face_count > 2 )
            visible_unsorted_free = i;
    }
    ToriDraw_SceneClearProjectionCamera(f->scene);
    CHECK(visible_unsorted_free >= 0, "exhaustion: no sortable model in the fixture");

    /* Results: room for one. */
    GLES2DualCoreStageArena_Init(&arena);
    GLES2DualCoreStageArena_BeginFrame(&arena, 1u);
    context_init(&context, f, view, false);
    CHECK(GLES2DualCoreStage_ComputeModel(&context, &arena, &f->commands[visible_unsorted_free]),
        "exhaustion: first model refused");
    CHECK(!GLES2DualCoreStage_ComputeModel(&context, &arena, &f->commands[visible_unsorted_free]),
        "exhaustion: second model accepted with one result slot");
    CHECK(arena.exhausted, "exhaustion: not flagged");
    CHECK(atomic_load(&arena.ready) == 1u, "exhaustion: ready %u after refusal",
        atomic_load(&arena.ready));
    GLES2DualCoreStage_EndPass(&context);
    /* The frame after an exhausted one is larger. */
    orders_before = arena.order_capacity;
    atomic_store(&arena.finished, GLES2_DUALCORE_STAGE_EXHAUSTED);
    GLES2DualCoreStageArena_BeginFrame(&arena, 4u);
    CHECK(arena.order_capacity > orders_before, "exhaustion: order store did not grow");
    CHECK(arena.exhausted_frames == 1u, "exhaustion: frames %u", arena.exhausted_frames);
    CHECK(!arena.exhausted, "exhaustion: flag not cleared");
    CHECK(arena.result_capacity >= 4u, "exhaustion: results %u", arena.result_capacity);

    /* Orders: room for two ints. */
    arena.order_capacity = 2u;
    context_init(&context, f, view, false);
    CHECK(!GLES2DualCoreStage_ComputeModel(&context, &arena, &f->commands[visible_unsorted_free]),
        "exhaustion: model accepted with two order ints");
    CHECK(arena.exhausted, "exhaustion: order store not flagged");
    CHECK(atomic_load(&arena.ready) == 0u, "exhaustion: a refused model was published");
    GLES2DualCoreStage_EndPass(&context);
    atomic_store(&arena.finished, GLES2_DUALCORE_STAGE_EXHAUSTED);
    GLES2DualCoreStageArena_BeginFrame(&arena, 4u);
    CHECK(arena.order_capacity == 4u, "exhaustion: order store %u, wanted 4", arena.order_capacity);

    GLES2DualCoreStageArena_Free(&arena);
    ToriDraw_SceneScratchViewFree(view);
}

/* Whoever claims a model first computes it; the other side sees the claim. */
static void
case_claims(struct Fixture* f)
{
    struct ToriDraw_Scene* view = ToriDraw_SceneScratchViewNew(f->scene);
    struct GLES2DualCoreStageArena arena;
    struct GLES2DualCoreStageContext context;
    int visible = -1;
    int i;

    ToriDraw_ScenePrepareProjectionCamera(f->scene, &f->camera);
    for( i = 0; i < MODEL_COUNT && visible < 0; i++ )
    {
        struct Reference ref;
        reference_stage(f, &f->commands[i], false, &ref);
        if( ref.cull == TORIDRAW_CULL_VISIBLE && !f->commands[i].pick_only )
            visible = i;
    }
    ToriDraw_SceneClearProjectionCamera(f->scene);
    CHECK(visible >= 0, "claims: no visible model");

    GLES2DualCoreStageArena_Init(&arena);
    arena.lead = 1u;
    GLES2DualCoreStageArena_BeginFrame(&arena, 3u);
    context_init(&context, f, view, false);

    /* The producer is not ahead of the consumer (both at 0): it hands the
     * slot over rather than claim it, and the claim word says so. */
    atomic_store(&arena.consumer_index, 0u);
    CHECK(GLES2DualCoreStageArena_ClaimNextForProducer(&arena) == GLES2_DUALCORE_CLAIM_TAKEN_BY_DRAW,
        "claims: producer at the consumer's slot must hand it over");
    CHECK(atomic_load(&arena.claims[0]) == GLES2_DUALCORE_CLAIM_CONSUMER,
        "claims: handed-over slot not marked for the consumer");
    CHECK(!GLES2DualCoreStageArena_ClaimForConsumer(&arena, 0u),
        "claims: a handed-over slot is already the consumer's");
    GLES2DualCoreStageArena_PublishTakenByDraw(&arena);
    CHECK(atomic_load(&arena.ready) == 1u, "claims: hand-off placeholder not published");
    /* Far enough ahead again: the producer claims for itself. */
    atomic_store(&arena.finished, GLES2_DUALCORE_STAGE_DONE);
    GLES2DualCoreStageArena_BeginFrame(&arena, 3u);
    atomic_store(&arena.consumer_index, 0u);
    /* Pretend the producer is at slot 2 with the consumer at 0. */
    arena.result_count = 2u;
    CHECK(GLES2DualCoreStageArena_ClaimNextForProducer(&arena) == GLES2_DUALCORE_CLAIMED,
        "claims: producer two ahead must claim for itself");
    atomic_store(&arena.finished, GLES2_DUALCORE_STAGE_DONE);
    GLES2DualCoreStageArena_BeginFrame(&arena, 3u);
    /* From here the consumer is kept far behind so the older cases hold. */
    atomic_store(&arena.consumer_index, 0u);
    arena.result_count = 0u;

    /* Slot 0: the consumer gets there first. */
    atomic_store(&arena.finished, GLES2_DUALCORE_STAGE_DONE);
    GLES2DualCoreStageArena_BeginFrame(&arena, 4u);
    atomic_store(&arena.consumer_index, 0u);
    CHECK(GLES2DualCoreStageArena_ClaimForConsumer(&arena, 0u), "claims: consumer claim 0");
    CHECK(GLES2DualCoreStageArena_ClaimNextForProducer(&arena) == GLES2_DUALCORE_CLAIM_TAKEN_BY_DRAW,
        "claims: producer should see slot 0 taken");
    GLES2DualCoreStageArena_PublishTakenByDraw(&arena);
    CHECK(atomic_load(&arena.ready) == 1u, "claims: placeholder not published");
    CHECK(arena.results[0].taken_by_draw, "claims: placeholder not marked");

    /* Slot 1: the consumer is still at 0, within the lead -- handed over. */
    CHECK(GLES2DualCoreStageArena_ClaimNextForProducer(&arena) == GLES2_DUALCORE_CLAIM_TAKEN_BY_DRAW,
        "claims: slot 1 within the lead must be handed over");
    GLES2DualCoreStageArena_PublishTakenByDraw(&arena);

    /* Slots 2 and 3: two ahead of the consumer, the producer's own; the
     * consumer's claim on 2 then fails and the result is a real one. */
    CHECK(GLES2DualCoreStageArena_ClaimNextForProducer(&arena) == GLES2_DUALCORE_CLAIMED,
        "claims: producer claim 2");
    CHECK(!GLES2DualCoreStageArena_ClaimForConsumer(&arena, 2u), "claims: consumer must lose 2");
    CHECK(GLES2DualCoreStage_ComputeModel(&context, &arena, &f->commands[visible]),
        "claims: compute 2");
    CHECK(atomic_load(&arena.ready) == 3u && !arena.results[2].taken_by_draw &&
              arena.results[2].cull == TORIDRAW_CULL_VISIBLE,
        "claims: slot 2 should be a real visible result");
    CHECK(GLES2DualCoreStageArena_ClaimNextForProducer(&arena) == GLES2_DUALCORE_CLAIMED,
        "claims: producer claim 3");
    CHECK(GLES2DualCoreStage_ComputeModel(&context, &arena, &f->commands[visible]),
        "claims: compute 3");
    /* Slot 4 does not exist. */
    CHECK(GLES2DualCoreStageArena_ClaimNextForProducer(&arena) == GLES2_DUALCORE_CLAIM_EXHAUSTED,
        "claims: slot 4 should be exhausted");
    CHECK(arena.exhausted, "claims: exhaustion not flagged");
    GLES2DualCoreStage_EndPass(&context);

    /* The next frame starts with every claim free again. */
    atomic_store(&arena.finished, GLES2_DUALCORE_STAGE_EXHAUSTED);
    GLES2DualCoreStageArena_BeginFrame(&arena, 4u);
    CHECK(GLES2DualCoreStageArena_ClaimForConsumer(&arena, 0u), "claims: slot 0 not freed");
    CHECK(GLES2DualCoreStageArena_ClaimForConsumer(&arena, 2u), "claims: slot 2 not freed");

    GLES2DualCoreStageArena_Free(&arena);
    ToriDraw_SceneScratchViewFree(view);
}

static void
case_sync(struct Fixture* f)
{
    struct ToriDraw_Scene* view = ToriDraw_SceneScratchViewNew(f->scene);
    struct ToriDraw_Model* model = make_model(false, false);
    int id;

    id = ToriDraw_SceneElementAdd(f->scene);
    ToriDraw_SceneElementSetModel(f->scene, id, ToriDraw_ModelHandleOwned(model));
    ToriDraw_SceneScratchViewSync(view, f->scene);
    CHECK(ToriDraw_SceneElementIsLive(view, id), "sync: the view does not see the new element");
    CHECK(ToriDraw_SceneElementGet(view, id) == ToriDraw_SceneElementGet(f->scene, id),
        "sync: the view's element is not the scene's");
    CHECK(ToriDraw_FaceOrder(view) != ToriDraw_FaceOrder(f->scene),
        "sync: the view shares the scene's face order");
    CHECK(view->screen_vertices_x != f->scene->screen_vertices_x,
        "sync: the view shares the scene's projected vertices");
    ToriDraw_SceneScratchViewFree(view);
}

/* --- two benches sorting at once ------------------------------------------------ */

/*
 * The frame thread on the scene and the stage worker on its view sort at the
 * same time, so nothing the sort reaches may be shared between benches. The
 * radix sort's count tables were file statics until 2026-09-02: two threads
 * scattering through one table corrupted each other's runs, and on the phone
 * that surfaced as a foreign face index in the priority partition and a
 * SIGSEGV on the worker. Each thread here projects and sorts every model
 * many times over on its own bench and holds the order to the serial
 * reference. The radix path needs more accepted keys than the bitonic
 * limit, so the makefile runs this suite a second time with
 * TORIDRAW_SORT_BITONIC_MAX low enough that every model takes it.
 */
#define CONCURRENT_SORT_ROUNDS 1500

struct SortWorker
{
    struct Fixture* f;
    struct ToriDraw_Scene* bench;
    const struct Reference* refs;
    int cull_mismatches;
    int count_mismatches;
    int order_mismatches;
};

static void*
sort_worker_main(void* arg)
{
    struct SortWorker* worker = (struct SortWorker*)arg;
    struct Fixture* f = worker->f;
    int round;
    int i;

    ToriDraw_ScenePrepareProjectionCamera(worker->bench, &f->camera);
    for( round = 0; round < CONCURRENT_SORT_ROUNDS; round++ )
    {
        for( i = 0; i < MODEL_COUNT; i++ )
        {
            const struct ToriRS_RenderCommand_Model* command = &f->commands[i];
            const struct Reference* ref = &worker->refs[i];
            struct ToriDraw_Position position = command->position;
            int count;
            int const cull = ToriDraw_RenderModel1ProjectWithTable(
                command->model, worker->bench, &position, &f->view_port, &f->camera, f->kernel);
            if( cull != ref->cull )
            {
                worker->cull_mismatches++;
                continue;
            }
            if( cull != TORIDRAW_CULL_VISIBLE || command->pick_only )
                continue;
            count = ToriDraw_RenderModel2SortFacesWithTable(command->model, worker->bench, f->kernel);
            if( count != ref->sorted_face_count )
            {
                worker->count_mismatches++;
                continue;
            }
            if( count > 0 &&
                memcmp(ToriDraw_FaceOrder(worker->bench), ref->order, (size_t)count * sizeof(int)) != 0 )
                worker->order_mismatches++;
        }
    }
    ToriDraw_SceneClearProjectionCamera(worker->bench);
    return NULL;
}

static void
case_concurrent_sort(struct Fixture* f)
{
    struct ToriDraw_Scene* view = ToriDraw_SceneScratchViewNew(f->scene);
    struct Reference refs[MODEL_COUNT];
    struct SortWorker workers[2];
    pthread_t threads[2];
    int i;

    ToriDraw_ScenePrepareProjectionCamera(f->scene, &f->camera);
    for( i = 0; i < MODEL_COUNT; i++ )
        reference_stage(f, &f->commands[i], false, &refs[i]);
    ToriDraw_SceneClearProjectionCamera(f->scene);

    memset(workers, 0, sizeof(workers));
    workers[0].f = f;
    workers[0].bench = f->scene;
    workers[0].refs = refs;
    workers[1].f = f;
    workers[1].bench = view;
    workers[1].refs = refs;
    for( i = 0; i < 2; i++ )
        CHECK(pthread_create(&threads[i], NULL, sort_worker_main, &workers[i]) == 0,
            "concurrent: thread %d did not start", i);
    for( i = 0; i < 2; i++ )
        pthread_join(threads[i], NULL);
    for( i = 0; i < 2; i++ )
    {
        char const* const bench = i == 0 ? "scene" : "view";
        CHECK(workers[i].cull_mismatches == 0, "concurrent: %s cull differed %d times", bench,
            workers[i].cull_mismatches);
        CHECK(workers[i].count_mismatches == 0, "concurrent: %s sort count differed %d times",
            bench, workers[i].count_mismatches);
        CHECK(workers[i].order_mismatches == 0, "concurrent: %s face order differed %d times",
            bench, workers[i].order_mismatches);
    }
    ToriDraw_SceneScratchViewFree(view);
}

/*
 * The command feed: what the consumer pushes is what the producer takes, in
 * order and in full; an open feed with nothing new says PENDING; a closed
 * one says ENDED only after the last entry; an overflowed one says OVERFLOW
 * only after the last entry it did take, the pushes after it are refused,
 * and the next frame's feed is bigger.
 */
static void
case_feed(struct Fixture* f)
{
    struct GLES2DualCoreStageArena arena;
    const struct ToriRS_RenderCommand* entry = NULL;
    struct ToriRS_RenderCommand begin;
    struct ToriRS_RenderCommand end;
    struct ToriRS_RenderCommand model;
    uint32_t capacity;
    uint32_t i;

    memset(&begin, 0, sizeof(begin));
    begin.kind = TORIRSRC_BEGIN_3D;
    begin.u.begin_3d.camera = f->camera;
    begin.u.begin_3d.view_port.width = VIEW_W;
    begin.u.begin_3d.view_port.height = VIEW_H;
    memset(&end, 0, sizeof(end));
    end.kind = TORIRSRC_END_3D;
    memset(&model, 0, sizeof(model));
    model.kind = TORIRSRC_DRAW_MODEL;

    GLES2DualCoreStageArena_Init(&arena);
    GLES2DualCoreStageArena_BeginFrame(&arena, (uint32_t)MODEL_COUNT);
    CHECK(GLES2DualCoreStageArena_FeedTake(&arena, 0u, &entry) == GLES2_DUALCORE_FEED_PENDING,
        "feed: an empty open feed must be pending");

    /* A pass: BEGIN_3D, every model (translated in place through
     * reserve/publish, as the draw does), END_3D; the producer reads it back
     * entry for entry. */
    CHECK(GLES2DualCoreStageArena_FeedPush(&arena, &begin), "feed: push BEGIN_3D");
    for( i = 0; i < (uint32_t)MODEL_COUNT; i++ )
    {
        struct ToriRS_RenderCommand* slot = GLES2DualCoreStageArena_FeedReserve(&arena);
        CHECK(slot != NULL, "feed: reserve model %u", i);
        /* Reserved is not published. */
        CHECK(GLES2DualCoreStageArena_FeedTake(&arena, 1u + i, &entry) ==
                  GLES2_DUALCORE_FEED_PENDING,
            "feed: a reserved entry leaked before publish");
        slot->kind = TORIRSRC_DRAW_MODEL;
        slot->u.model = f->commands[i];
        GLES2DualCoreStageArena_FeedPublish(&arena);
    }
    CHECK(GLES2DualCoreStageArena_FeedPush(&arena, &end), "feed: push END_3D");

    CHECK(GLES2DualCoreStageArena_FeedTake(&arena, 0u, &entry) == GLES2_DUALCORE_FEED_READY &&
              entry->kind == TORIRSRC_BEGIN_3D,
        "feed: entry 0 is not the BEGIN_3D");
    CHECK(memcmp(&entry->u.begin_3d, &begin.u.begin_3d, sizeof(begin.u.begin_3d)) == 0,
        "feed: BEGIN_3D not copied");
    for( i = 0; i < (uint32_t)MODEL_COUNT; i++ )
    {
        CHECK(GLES2DualCoreStageArena_FeedTake(&arena, 1u + i, &entry) == GLES2_DUALCORE_FEED_READY &&
                  entry->kind == TORIRSRC_DRAW_MODEL,
            "feed: entry %u is not a model", 1u + i);
        CHECK(memcmp(&entry->u.model, &f->commands[i], sizeof(f->commands[i])) == 0,
            "feed: model %u not copied", i);
    }
    CHECK(GLES2DualCoreStageArena_FeedTake(&arena, 1u + MODEL_COUNT, &entry) ==
                  GLES2_DUALCORE_FEED_READY &&
              entry->kind == TORIRSRC_END_3D,
        "feed: last entry is not the END_3D");
    /* Past the end while open: pending, not ended. */
    CHECK(GLES2DualCoreStageArena_FeedTake(&arena, 2u + MODEL_COUNT, &entry) ==
              GLES2_DUALCORE_FEED_PENDING,
        "feed: past the end of an open feed must be pending");
    GLES2DualCoreStageArena_FeedClose(&arena);
    CHECK(GLES2DualCoreStageArena_FeedTake(&arena, 2u + MODEL_COUNT, &entry) ==
              GLES2_DUALCORE_FEED_ENDED,
        "feed: past the end of a closed feed must be ended");
    /* Closing does not take back what was published. */
    CHECK(GLES2DualCoreStageArena_FeedTake(&arena, 5u, &entry) == GLES2_DUALCORE_FEED_READY,
        "feed: a closed feed still serves its entries");

    /* Overflow: fill the feed to capacity, then one more. */
    atomic_store(&arena.finished, GLES2_DUALCORE_STAGE_DONE);
    GLES2DualCoreStageArena_BeginFrame(&arena, (uint32_t)MODEL_COUNT);
    capacity = arena.feed_capacity;
    CHECK(capacity >= (uint32_t)MODEL_COUNT + 2u, "feed: capacity %u for %d models", capacity,
        MODEL_COUNT);
    for( i = 0; i < capacity; i++ )
    {
        model.u.model = f->commands[i % MODEL_COUNT];
        CHECK(GLES2DualCoreStageArena_FeedPush(&arena, &model), "feed: push %u of %u refused", i,
            capacity);
    }
    CHECK(GLES2DualCoreStageArena_FeedReserve(&arena) == NULL,
        "feed: reserve past capacity succeeded");
    CHECK(atomic_load(&arena.feed_state) == GLES2_DUALCORE_FEED_OVERFLOWED,
        "feed: overflow not recorded");
    CHECK(atomic_load(&arena.feed_published) == capacity, "feed: overflow changed the count");
    /* Everything published is still served, in order; then OVERFLOW. */
    CHECK(GLES2DualCoreStageArena_FeedTake(&arena, capacity - 1u, &entry) ==
              GLES2_DUALCORE_FEED_READY,
        "feed: last published entry not served after overflow");
    CHECK(GLES2DualCoreStageArena_FeedTake(&arena, capacity, &entry) ==
              GLES2_DUALCORE_FEED_OVERFLOW,
        "feed: past the end of an overflowed feed must say overflow");
    /* A close after an overflow does not soften the verdict. */
    GLES2DualCoreStageArena_FeedClose(&arena);
    CHECK(GLES2DualCoreStageArena_FeedTake(&arena, capacity, &entry) ==
              GLES2_DUALCORE_FEED_OVERFLOW,
        "feed: close overrode overflow");

    /* The next frame grows it and opens it. */
    atomic_store(&arena.finished, GLES2_DUALCORE_STAGE_DONE);
    GLES2DualCoreStageArena_BeginFrame(&arena, (uint32_t)MODEL_COUNT);
    CHECK(arena.feed_capacity >= capacity * 2u, "feed: did not grow after overflow (%u -> %u)",
        capacity, arena.feed_capacity);
    CHECK(arena.feed_overflow_frames == 1u, "feed: overflow frames %u", arena.feed_overflow_frames);
    CHECK(atomic_load(&arena.feed_state) == GLES2_DUALCORE_FEED_OPEN, "feed: not reopened");
    CHECK(atomic_load(&arena.feed_published) == 0u, "feed: count not reset");
    CHECK(GLES2DualCoreStageArena_FeedTake(&arena, 0u, &entry) == GLES2_DUALCORE_FEED_PENDING,
        "feed: reopened feed must be pending");

    atomic_store(&arena.finished, GLES2_DUALCORE_STAGE_DONE);
    GLES2DualCoreStageArena_Free(&arena);
}

static void
run_tier(char const* name, uint32_t flags)
{
    struct Fixture fixture;
    int const before = failures;

    fixture_init(&fixture, flags);
    case_parity(&fixture, false);
    case_parity(&fixture, true);
    case_independence(&fixture);
    case_blended_gate(&fixture);
    case_exhaustion(&fixture);
    case_claims(&fixture);
    case_feed(&fixture);
    case_sync(&fixture);
    case_concurrent_sort(&fixture);
    fixture_free(&fixture);
    printf("%s tier: %s\n", name, failures == before ? "ok" : "FAILED");
}

int
main(void)
{
    /* The trig and hsl tables the projection reads. */
    ToriDraw_Init();
    run_tier("small", TORIDRAW_SCENE_SMALL | TORIDRAW_SCENE_DEPTH_16K | TORIDRAW_SCENE_MODEL_ZBUFFER);
    run_tier("full", TORIDRAW_SCENE_FULL | TORIDRAW_SCENE_MODEL_ZBUFFER);
    if( failures )
    {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("gles2_dualcore_stage_test: all passed\n");
    return 0;
}
