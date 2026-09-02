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
    GLES2DualCoreStageArena_BeginFrame(&arena, 3u);
    context_init(&context, f, view, false);

    /* Slot 0: the consumer gets there first. */
    CHECK(GLES2DualCoreStageArena_ClaimForConsumer(&arena, 0u), "claims: consumer claim 0");
    CHECK(GLES2DualCoreStageArena_ClaimNextForProducer(&arena) == GLES2_DUALCORE_CLAIM_TAKEN_BY_DRAW,
        "claims: producer should see slot 0 taken");
    GLES2DualCoreStageArena_PublishTakenByDraw(&arena);
    CHECK(atomic_load(&arena.ready) == 1u, "claims: placeholder not published");
    CHECK(arena.results[0].taken_by_draw, "claims: placeholder not marked");

    /* Slot 1: the producer first; the consumer's claim then fails and the
     * result is a real one. */
    CHECK(GLES2DualCoreStageArena_ClaimNextForProducer(&arena) == GLES2_DUALCORE_CLAIMED,
        "claims: producer claim 1");
    CHECK(!GLES2DualCoreStageArena_ClaimForConsumer(&arena, 1u), "claims: consumer must lose 1");
    CHECK(GLES2DualCoreStage_ComputeModel(&context, &arena, &f->commands[visible]),
        "claims: compute 1");
    CHECK(atomic_load(&arena.ready) == 2u && !arena.results[1].taken_by_draw &&
              arena.results[1].cull == TORIDRAW_CULL_VISIBLE,
        "claims: slot 1 should be a real visible result");

    /* Slot 2: producer; slot 3 does not exist. */
    CHECK(GLES2DualCoreStageArena_ClaimNextForProducer(&arena) == GLES2_DUALCORE_CLAIMED,
        "claims: producer claim 2");
    CHECK(GLES2DualCoreStage_ComputeModel(&context, &arena, &f->commands[visible]),
        "claims: compute 2");
    CHECK(GLES2DualCoreStageArena_ClaimNextForProducer(&arena) == GLES2_DUALCORE_CLAIM_EXHAUSTED,
        "claims: slot 3 should be exhausted");
    CHECK(arena.exhausted, "claims: exhaustion not flagged");
    GLES2DualCoreStage_EndPass(&context);

    /* The next frame starts with every claim free again. */
    atomic_store(&arena.finished, GLES2_DUALCORE_STAGE_EXHAUSTED);
    GLES2DualCoreStageArena_BeginFrame(&arena, 3u);
    CHECK(GLES2DualCoreStageArena_ClaimForConsumer(&arena, 0u), "claims: slot 0 not freed");
    CHECK(GLES2DualCoreStageArena_ClaimForConsumer(&arena, 1u), "claims: slot 1 not freed");

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
    case_sync(&fixture);
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
