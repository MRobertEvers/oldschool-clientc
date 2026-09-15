/* Diagnostic build only (-DTORIRS_MODEL_CHAIN_CAPTURE=1), single draw thread.
 * Run the production GL-free model stage on each real command, snapshot its
 * posed inputs and outputs, then restore the renderer's prepared camera.
 * Capture does extra work and is never used as a performance measurement. */
#include "platform/platform_renderer_gles2_dualcore_stage.h"
#include "tools/perf/model_chain_format.h"

static uint32_t g_chain_pass;

static void
gles2_chain_capture(struct ToriRS_GLES2* renderer,
                    const struct ToriRS_RenderCommand_Model* command)
{
    static int initialized, first_pass, pass_count;
    static uint32_t ordinal;
    static FILE* out;
    static struct GLES2DualCoreStageArena arena;
    static struct GLES2DualCoreStageContext context;
    struct ModelChainHeader h = {0};
    const struct ToriDraw_Model* model;
    const struct GLES2DualCoreStageResult* result;
    if( !initialized )
    {
        const char* path = getenv("TORIRS_MODEL_CHAIN_CAPTURE");
        const char* value;
        initialized = 1;
        if( !path || !*path ) return;
        if( renderer->zbuffer || renderer->model_stage_source )
        { fprintf(stderr, "chain capture requires single-threaded --gles2\n"); abort(); }
        value = getenv("TORIRS_MODEL_CHAIN_FIRST_PASS");
        first_pass = value ? atoi(value) : 120;
        value = getenv("TORIRS_MODEL_CHAIN_PASSES");
        pass_count = value ? atoi(value) : 4;
        if( first_pass < 1 || pass_count < 1 ) abort();
        out = fopen(path, "wb");
        if( !out ) { perror("model chain capture"); abort(); }
        GLES2DualCoreStageArena_Init(&arena);
        arena.result_capacity = 1;
        arena.results = calloc(1, sizeof(*arena.results));
        arena.order_capacity = renderer->scene->max_faces;
        arena.orders = malloc((size_t)arena.order_capacity * sizeof(*arena.orders));
        if( !arena.results || !arena.orders ) abort();
    }
    if( !out || g_chain_pass < (unsigned)first_pass ) return;
    if( g_chain_pass >= (unsigned)(first_pass + pass_count) )
    {
        struct ModelChainHeader end = { .magic = MODEL_CHAIN_END,
            .version = MODEL_CHAIN_VERSION, .ordinal = ordinal, .pass = (uint32_t)pass_count };
        if( fwrite(&end, 1, sizeof(end), out) != sizeof(end) ) abort();
        if( fclose(out) ) abort();
        out = NULL;
        GLES2DualCoreStageArena_Free(&arena);
        fprintf(stderr, "model chain capture complete: %u commands\n", ordinal);
        return;
    }
    if( !ToriDraw_ModelKindIsFull(command->model.kind) ) return;
    context.scene = renderer->scene;
    context.kernel = renderer->kernel;
    context.zbuffer = false;
    context.pick_enabled = renderer->pick_enabled;
    context.pick_mouse_x = renderer->pick_mouse_x;
    context.pick_mouse_y = renderer->pick_mouse_y;
    GLES2DualCoreStage_BeginPass(&context, &renderer->current_3d);
    arena.result_count = 0;
    arena.order_count = 0;
    if( !GLES2DualCoreStage_ComputeModel(&context, &arena, command) ) abort();
    model = ToriDraw_ModelRead(command->model);
    result = &arena.results[0];
    h.magic = MODEL_CHAIN_MAGIC; h.version = MODEL_CHAIN_VERSION;
    h.pass = g_chain_pass; h.ordinal = ordinal++;
    h.vertices = model->vertex_count; h.faces = model->face_count;
    h.textures = model->textured_face_count;
    h.tile_kernel = command->model.kind == TORIDRAWMK_MODEL ? model->tile_sort_kernel : 0;
    h.model_flags = model->flags;
    h.options = (model->face_priorities ? MODEL_CHAIN_PRIORITIES : 0) |
        (model->face_colors_c ? MODEL_CHAIN_COLOUR_C : 0) |
        (model->has_bounds_cylinder ? MODEL_CHAIN_BOUNDS : 0) |
        (command->animation ? MODEL_CHAIN_ANIMATED : 0) |
        (command->dynamic ? MODEL_CHAIN_DYNAMIC : 0) |
        ((uint32_t)command->model.kind << MODEL_CHAIN_KIND_SHIFT);
    h.max_vertices = renderer->scene->max_vertices;
    h.max_faces = renderer->scene->max_faces;
    h.depth_levels = renderer->scene->depth_levels;
    h.element_id = command->element_id; h.model_token = (int32_t)(uintptr_t)model;
    h.pick_enabled = renderer->pick_enabled;
    h.pick_x = renderer->pick_mouse_x; h.pick_y = renderer->pick_mouse_y;
    h.pick_flags = (command->pickable ? MODEL_CHAIN_PICKABLE : 0) |
        (command->pick_aabb ? MODEL_CHAIN_PICK_AABB : 0) |
        (command->pick_terrain ? MODEL_CHAIN_PICK_TERRAIN : 0) |
        (command->pick_only ? MODEL_CHAIN_PICK_ONLY : 0);
    h.cull = result->cull; h.sorted_count = result->sorted_face_count;
    h.pick_hit = result->pick_hit; h.projected_depth = result->projected_depth;
    h.near_clipped = renderer->scene->near_clipped;
    memcpy(h.projected_box, renderer->scene->projected_box, sizeof(h.projected_box));
    h.position = command->position; h.begin = renderer->current_3d;
    h.bounds = model->bounds_cylinder;
#define WRITE_ARRAY(p_, size_) do { size_t n_ = (size_); \
    if( n_ && fwrite((p_), 1, n_, out) != n_ ) { perror("chain write"); abort(); } } while(0)
    WRITE_ARRAY(&h, sizeof(h));
    WRITE_ARRAY(model->vertices_x, (size_t)h.vertices * 2);
    WRITE_ARRAY(model->vertices_y, (size_t)h.vertices * 2);
    WRITE_ARRAY(model->vertices_z, (size_t)h.vertices * 2);
    WRITE_ARRAY(model->face_indices_a, (size_t)h.faces * 2);
    WRITE_ARRAY(model->face_indices_b, (size_t)h.faces * 2);
    WRITE_ARRAY(model->face_indices_c, (size_t)h.faces * 2);
    if( h.options & MODEL_CHAIN_PRIORITIES ) WRITE_ARRAY(model->face_priorities, ((size_t)h.faces + 1) / 2);
    if( h.options & MODEL_CHAIN_COLOUR_C ) WRITE_ARRAY(model->face_colors_c, (size_t)h.faces * 2);
    if( h.cull == TORIDRAW_CULL_VISIBLE )
    {
        WRITE_ARRAY(renderer->scene->screen_vertices_x, (size_t)h.vertices * 4);
        WRITE_ARRAY(renderer->scene->screen_vertices_y, (size_t)h.vertices * 4);
        WRITE_ARRAY(renderer->scene->screen_vertices_z, (size_t)h.vertices * 4);
        WRITE_ARRAY(arena.orders, (size_t)h.sorted_count * 4);
    }
#undef WRITE_ARRAY
    ToriDraw_ScenePrepareProjectionCamera(renderer->scene, &renderer->current_3d.camera);
}
