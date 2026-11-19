/*
 * The ground family's parallel tasks (app/app_tasks_ground.c): a ground
 * stack's model and a widget's obj icon, on a zeroed App with a hand-fed
 * cache, a real scene, and a fake assets loader whose reads the test answers.
 *
 * Ground stacks (the row is the server's and exists at once; its model is a
 * task's when it is not resident):
 *   - a cold OBJ_ADD is a row at once, with a task waiting for its model, and
 *     gets its element, name and menu when the model arrives;
 *   - G3: a warm OBJ_ADD whose model will not build (every part resident, the
 *     obj gives none) keeps its row; it used to drop the add;
 *   - G1: an OBJ_COUNT whose new count variant is not resident loads it and
 *     re-points the element, the old model drawn meanwhile; it used to keep
 *     the old model for good;
 *   - G2: an OBJ_CUSTOMISE whose replacement model is not resident loads it
 *     and re-applies it to the element; it used to load it and forget it;
 *   - a row picked up while it loads cancels its task; a rebuild in progress
 *     is waited out and the row is landed after it.
 * Widget icons (what inv/widget_icon_park did, as tasks):
 *   - one task per cell; a rewrite cancels the older one (newest wins);
 *   - none released in the tick it was asked in (a named cycle wait on the
 *     release's tick); at most twelve a tick, the
 *     rest in their places in line;
 *   - a cell whose parts are still missing asks again after its back-off.
 *
 * Run: make -C src test-tasks-ground
 */

#include "app.h"
#include "app/app_internal.h"
#include "engine/cache_provider.h"
#include "mem/torirs_mem.h"
#include "ui/uitree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;

#define CHECK(cond, what)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/* ---- the fixture --------------------------------------------------------- */

#define STARTS_MAX 256

struct Fixture
{
    struct App* app;
    struct World* world;
    struct ToriRS_IOBatch* io;
    /* Every load the fake loader was asked to start, in order. */
    struct AssetKey started[STARTS_MAX];
    int started_count;
};

static struct Fixture g_fx;

static enum AssetStart
fake_start(
    void* data,
    struct Assets* assets,
    struct AssetKey key)
{
    (void)data;
    (void)assets;
    assert(g_fx.started_count < STARTS_MAX);
    g_fx.started[g_fx.started_count++] = key;
    return ASSET_START_LOADING;
}

static bool
fake_resident(
    void* data,
    struct AssetKey key)
{
    return App_AssetResident((struct App*)data, key);
}

static int
starts_of(
    enum AssetKind kind,
    int id,
    int variant)
{
    int n = 0;
    for( int i = 0; i < g_fx.started_count; i++ )
        if( g_fx.started[i].kind == (int32_t)kind && g_fx.started[i].id == id &&
            g_fx.started[i].variant == variant )
            n++;
    return n;
}

static struct ToriRS_Model*
fake_model(void)
{
    struct ToriRS_Model* model = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*model));
    assert(model);
    model->vertex_count = 3;
    model->face_count = 1;
    model->vertices_x = ToriMem_Calloc(ToriMem_Assets(), 3, sizeof(*model->vertices_x));
    model->vertices_y = ToriMem_Calloc(ToriMem_Assets(), 3, sizeof(*model->vertices_y));
    model->vertices_z = ToriMem_Calloc(ToriMem_Assets(), 3, sizeof(*model->vertices_z));
    model->face_indices_a = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*model->face_indices_a));
    model->face_indices_b = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*model->face_indices_b));
    model->face_indices_c = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*model->face_indices_c));
    model->face_colors = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*model->face_colors));
    model->face_colors_a = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*model->face_colors_a));
    model->face_colors_b = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*model->face_colors_b));
    model->face_colors_c = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*model->face_colors_c));
    assert(model->face_colors_a && model->face_colors_b && model->face_colors_c);
    assert(model->vertices_x && model->vertices_y && model->vertices_z);
    assert(model->face_indices_a && model->face_indices_b && model->face_indices_c);
    assert(model->face_colors);
    model->vertices_x[1] = 10;
    model->vertices_z[2] = 10;
    model->face_indices_b[0] = 1;
    model->face_indices_c[0] = 2;
    return model;
}

static struct ToriRS_Objtype*
fake_objtype(
    char const* name,
    int model_id)
{
    struct ToriRS_Objtype* obj = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*obj));
    assert(obj);
    obj->cert_link = -1;
    obj->cert_template = -1;
    obj->placeholder_link = -1;
    obj->placeholder_template = -1;
    obj->inventory_model_id = model_id;
    strncpy(obj->name, name, sizeof(obj->name) - 1);
    snprintf(obj->ground_actions[2], sizeof(obj->ground_actions[2]), "Take");
    return obj;
}

static void
add_obj(
    int obj_id,
    char const* name,
    int model_id)
{
    CacheProvider_ObjtypeAdd(g_fx.app->provider, obj_id, fake_objtype(name, model_id));
}

static void
add_model(int model_id)
{
    CacheProvider_ModelAdd(g_fx.app->provider, model_id, fake_model());
}

/* The read for `key` came back. */
static void
arrive(
    enum AssetKind kind,
    int id,
    int variant,
    bool ok)
{
    Assets_Finished(g_fx.app->assets, AssetKey_Make(kind, id, variant), ok);
}

/* Step the parallel queue until nothing more moves, as the frame does. */
static void
pump(void)
{
    struct ToriRS_TaskQueue* queue = g_fx.app->frame_runner.queue;

    for( int pass = 0; pass < 8; pass++ )
    {
        struct ToriRS_Task* task = queue->head;
        while( task )
        {
            struct ToriRS_Task* next = task->next;
            task->blocked = 0;
            (void)ToriRS_TaskQueue_RunTask(queue, g_fx.io, task);
            task = next;
        }
    }
}

static int
queued(void)
{
    int n = 0;
    for( struct ToriRS_Task* t = g_fx.app->frame_runner.queue->head; t; t = t->next )
        n++;
    return n;
}

static void
fixture_init(void)
{
    static char dummy_builder[1];
    struct AssetLoader loader;
    struct App* app;

    memset(&g_fx, 0, sizeof(g_fx));
    app = calloc(1, sizeof(*app));
    assert(app);
    g_fx.app = app;
    app->provider = calloc(1, sizeof(*app->provider));
    assert(app->provider);
    CacheProvider_InitEngineCaches(app->provider);
    app->scene = ToriDraw_SceneNew(
        TORIDRAW_SCENE_SMALL | TORIDRAW_SCENE_DEPTH_16K | TORIDRAW_SCENE_MODEL_ZBUFFER,
        TORIDRAW_SCRATCH_BUFFER_VERYHIGH_16K);
    assert(app->scene);
    UITreeSceneBridge_Init(&app->bridge, app->scene, app->provider);

    app->assets = Assets_New();
    loader.start = fake_start;
    loader.resident = fake_resident;
    loader.data = app;
    Assets_SetLoader(app->assets, ASSET_KIND_OBJ_MODEL, &loader);
    Assets_SetLoader(app->assets, ASSET_KIND_MODEL, &loader);
    App_PendingInit(app);
    app->frame_runner.queue = ToriRS_TaskQueue_New();
    g_fx.io = ToriRS_IOBatch_New();

    g_fx.world = World_New();
    World_ResetScene(g_fx.world, 0, 0, 104);
    World_SetLoadComplete(g_fx.world, true);
    app->world = g_fx.world;
    WorldviewRegistry_Init(&app->worldviews);
    WorldviewRegistry_RegisterRoot(
        &app->worldviews, g_fx.world, (struct WorldBuilder*)dummy_builder);
    app->host.client.now_ms = 1000;
}

static void
fixture_free(void)
{
    struct App* app = g_fx.app;

    Assets_Close(app->assets);
    ToriRS_TaskQueue_Free(app->frame_runner.queue);
    ToriRS_IOBatch_Free(g_fx.io);
    App_PendingFree(app);
    Assets_Free(app->assets);
    app_tasks_ground_free(app);
    if( app->tree )
        UITree_Free(app->tree);
    WorldviewRegistry_Free(&app->worldviews);
    World_Free(g_fx.world);
    free(app);
}

static struct WorldEntity_ObjStack*
row(int idx)
{
    return World_EntityPoolGet(&g_fx.world->entities.obj_stack, idx);
}

static void const*
element_model(int element_id)
{
    struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(g_fx.app->scene, element_id);
    assert(el);
    return (void const*)el->model.u.model.model;
}

/* ---- ground stacks ------------------------------------------------------- */

static void
test_cold_add_lands_when_the_model_arrives(void)
{
    int idx;

    printf("TEST: a cold OBJ_ADD is a row at once and lands its model later\n");
    fixture_init();
    idx = App_WorldObjStackAdd(g_fx.app, 10, 12, 0, 100, 1);
    CHECK(idx >= 0, "the row exists at packet time");
    CHECK(row(idx)->element_id < 0, "without an element");
    CHECK(app_ground_stack_pending(g_fx.app, g_fx.world, idx), "a task waits for its model");
    CHECK(queued() == 1, "on the parallel queue");
    pump();
    CHECK(starts_of(ASSET_KIND_OBJ_MODEL, 100, 1) == 1, "its obj model loads once the queue is stepped");
    CHECK(row(idx)->element_id < 0, "nothing applies before the read is answered");

    add_obj(100, "Bones", 500);
    add_model(500);
    arrive(ASSET_KIND_OBJ_MODEL, 100, 1, true);
    pump();
    CHECK(row(idx)->element_id >= 0, "the element arrives with the model");
    CHECK(strcmp(row(idx)->name, "Bones") == 0, "and the name with the objtype");
    CHECK(!app_ground_stack_pending(g_fx.app, g_fx.world, idx), "the task is gone");
    CHECK(queued() == 0, "and off the queue");
    fixture_free();
}

static void
test_g3_warm_add_without_a_model_keeps_its_row(void)
{
    int idx;

    printf("TEST: G3 a warm OBJ_ADD whose model will not build keeps its row\n");
    fixture_init();
    /* Resident, and gives no ground model: nothing to load, nothing to build. */
    add_obj(101, "Nothing", 0);
    idx = App_WorldObjStackAdd(g_fx.app, 11, 12, 0, 101, 1);
    CHECK(idx >= 0, "the add is not refused");
    CHECK(idx >= 0 && row(idx)->element_id < 0, "the row has no element");
    CHECK(idx >= 0 && strcmp(row(idx)->name, "Nothing") == 0, "and is named and listed");
    CHECK(World_ObjStackFind(g_fx.world, 11, 12, 0, 101) == idx, "found on its tile");
    CHECK(idx >= 0 && !app_ground_stack_pending(g_fx.app, g_fx.world, idx), "nothing to wait for");
    CHECK(g_fx.started_count == 0, "and nothing loaded");
    fixture_free();
}

static void
test_g1_count_variant_loads_and_repoints(void)
{
    struct ToriRS_Objtype* coins;
    void const* before;
    int idx;
    int element;

    printf("TEST: G1 an OBJ_COUNT into a variant not resident loads it\n");
    fixture_init();
    add_obj(102, "Coins", 502);
    add_model(502);
    coins = CacheProvider_ObjtypeGet(g_fx.app->provider, 102);
    coins->stackable = 1;
    coins->count_obj[0] = 103;
    coins->count_co[0] = 5;
    idx = App_WorldObjStackAdd(g_fx.app, 12, 12, 0, 102, 1);
    CHECK(idx >= 0 && row(idx)->element_id >= 0, "a warm add draws at once");
    element = row(idx)->element_id;
    before = element_model(element);

    App_WorldObjStackSetCount(g_fx.app, 12, 12, 0, 102, 10);
    CHECK(row(idx)->count == 10, "the count is the packet's at once");
    CHECK(app_ground_stack_pending(g_fx.app, g_fx.world, idx), "the new variant is waited for");
    pump();
    CHECK(starts_of(ASSET_KIND_OBJ_MODEL, 102, 10) == 1, "and loading");
    CHECK(element_model(element) == before, "the old model stays drawn meanwhile");

    add_obj(103, "Coins", 503);
    add_model(503);
    arrive(ASSET_KIND_OBJ_MODEL, 102, 10, true);
    pump();
    CHECK(row(idx)->element_id == element, "the same element");
    CHECK(element_model(element) != before, "now holds the variant's model");
    CHECK(queued() == 0, "the task is done");

    /* Inside the variant: nothing to load and nothing re-pointed. */
    before = element_model(element);
    App_WorldObjStackSetCount(g_fx.app, 12, 12, 0, 102, 11);
    CHECK(!app_ground_stack_pending(g_fx.app, g_fx.world, idx), "a count inside a variant waits for nothing");
    CHECK(element_model(element) == before, "and keeps its model");
    fixture_free();
}

static void
test_g2_customise_loads_and_reapplies(void)
{
    struct WorldEntity_ObjCustomise custom;
    void const* before;
    int idx;
    int element;

    printf("TEST: G2 an OBJ_CUSTOMISE on a drawn stack loads its model and re-applies\n");
    fixture_init();
    add_obj(100, "Bones", 500);
    add_model(500);
    idx = App_WorldObjStackAdd(g_fx.app, 13, 12, 0, 100, 1);
    CHECK(idx >= 0 && row(idx)->element_id >= 0, "drawn at once");
    element = row(idx)->element_id;
    before = element_model(element);

    memset(&custom, 0, sizeof(custom));
    custom.model = 600;
    custom.tint = -1;
    App_WorldObjStackCustomise(g_fx.app, 13, 12, 0, 100, 1, &custom);
    CHECK(row(idx)->customised && row(idx)->custom_model == 600, "the customisation is recorded at once");
    CHECK(app_ground_stack_pending(g_fx.app, g_fx.world, idx), "its model is waited for");
    pump();
    CHECK(starts_of(ASSET_KIND_MODEL, 600, 0) == 1, "and loading");
    CHECK(element_model(element) == before, "the old model stays drawn meanwhile");

    add_model(600);
    arrive(ASSET_KIND_MODEL, 600, 0, true);
    pump();
    CHECK(row(idx)->element_id == element, "the same element");
    CHECK(element_model(element) != before, "re-shaped onto the replacement model");
    CHECK(queued() == 0, "the task is done");
    fixture_free();
}

static void
test_a_model_never_served_is_asked_four_times(void)
{
    int const attempts = Assets_Policy(ASSET_KIND_OBJ_MODEL)->fail_attempts;
    int idx;

    printf("TEST: a model the cache cannot serve is asked for %d times, then none\n", attempts);
    fixture_init();
    idx = App_WorldObjStackAdd(g_fx.app, 16, 12, 0, 106, 1);
    pump();
    for( int i = 0; i < attempts; i++ )
    {
        CHECK(starts_of(ASSET_KIND_OBJ_MODEL, 106, 1) == i + 1, "asked again at once");
        arrive(ASSET_KIND_OBJ_MODEL, 106, 1, false);
        pump();
    }
    CHECK(starts_of(ASSET_KIND_OBJ_MODEL, 106, 1) == attempts, "and then not again");
    CHECK(queued() == 0, "the task gave up");
    CHECK(row(idx)->element_id < 0, "the row stays, without a model");
    /* A rebuild asks once more for a row still without one. */
    app_ground_stacks_sweep(g_fx.app, g_fx.world, WORLDVIEW_ROOT);
    pump();
    CHECK(starts_of(ASSET_KIND_OBJ_MODEL, 106, 1) == attempts + 1, "the sweep asks again");
    CHECK(app_ground_stack_pending(g_fx.app, g_fx.world, idx), "with a task of its own");
    fixture_free();
}

static void
test_picked_up_while_loading(void)
{
    int idx;

    printf("TEST: a row picked up while its model loads cancels its task\n");
    fixture_init();
    idx = App_WorldObjStackAdd(g_fx.app, 14, 12, 0, 104, 1);
    CHECK(app_ground_stack_pending(g_fx.app, g_fx.world, idx), "waiting");
    pump();
    CHECK(starts_of(ASSET_KIND_OBJ_MODEL, 104, 1) == 1, "for its read");
    App_WorldObjStackDel(g_fx.app, 14, 12, 0, 104);
    pump();
    CHECK(queued() == 0, "the task exits without waiting for its read");
    add_obj(104, "Ashes", 504);
    add_model(504);
    arrive(ASSET_KIND_OBJ_MODEL, 104, 1, true);
    pump();
    CHECK(World_ObjStackFind(g_fx.world, 14, 12, 0, 104) < 0, "and nothing comes back");
    fixture_free();
}

static void
test_a_rebuild_in_progress_is_waited_out(void)
{
    int idx;

    printf("TEST: a rebuild in progress is waited out, and the row lands after it\n");
    fixture_init();
    idx = App_WorldObjStackAdd(g_fx.app, 15, 12, 0, 105, 1);
    pump();
    World_SetLoadComplete(g_fx.world, false);
    add_obj(105, "Logs", 505);
    add_model(505);
    arrive(ASSET_KIND_OBJ_MODEL, 105, 1, true);
    pump();
    CHECK(row(idx)->element_id < 0, "nothing lands in a scene being built");
    CHECK(app_ground_stack_pending(g_fx.app, g_fx.world, idx), "the task waits");
    World_SetLoadComplete(g_fx.world, true);
    pump();
    CHECK(row(idx)->element_id >= 0, "it lands when the scene is complete");
    CHECK(queued() == 0, "and is done");
    fixture_free();
}

/* ---- widget icons -------------------------------------------------------- */

#define ICON_GROUP 0x0123
#define ICON_CELLS 20

static int
cell(int i)
{
    return (ICON_GROUP << 16) | i;
}

static void
icons_tree(void)
{
    struct App* app = g_fx.app;

    app->tree = UITree_New(ICON_CELLS + 2);
    for( int i = 0; i < ICON_CELLS; i++ )
    {
        struct UITreeNodeSpec spec;
        memset(&spec, 0, sizeof(spec));
        spec.type = UIELEM_RS_GRAPHIC;
        spec.component_id = cell(i);
        spec.width = 32;
        spec.height = 32;
        CHECK(UITree_Push(app->tree, -1, &spec) >= 0, "a cell");
        /* What a script's cc_setobject leaves on a cell whose icon is not
         * baked: the obj, and no picture. */
        (void)UITree_ApplyObject(app->tree, cell(i), 200 + i, 1, -1, 0, 0);
    }
}

static void
test_widget_icons_one_task_per_cell_newest_wins(void)
{
    int obj = -1;

    printf("TEST: widget icons: one task per cell, newest wins\n");
    fixture_init();
    icons_tree();
    for( int i = 0; i < ICON_CELLS; i++ )
        app_cs2_widget_obj_icon_lazy(g_fx.app, cell(i), 200 + i, 1);
    CHECK(app_widget_icons_pending(g_fx.app) == ICON_CELLS, "every new cell is a task");
    app_cs2_widget_obj_icon_lazy(g_fx.app, cell(0), 200, 1);
    CHECK(app_widget_icons_pending(g_fx.app) == ICON_CELLS, "asking again is still one task");
    app_cs2_widget_obj_icon_lazy(g_fx.app, cell(0), 300, 1);
    CHECK(app_widget_icons_pending(g_fx.app) == ICON_CELLS, "a rewritten cell is still one task");
    CHECK(app_widget_icon_pending(g_fx.app, cell(0), &obj) && obj == 300, "naming the newest obj");
    pump();
    CHECK(queued() == ICON_CELLS, "the replaced one exits");
    fixture_free();
}

static void
test_widget_icons_released_twelve_a_tick(void)
{
    printf("TEST: widget icons: none in the asking tick, then twelve a tick\n");
    fixture_init();
    icons_tree();
    app_tasks_ground_tick(g_fx.app);
    for( int i = 0; i < ICON_CELLS; i++ )
        app_cs2_widget_obj_icon_lazy(g_fx.app, cell(i), 200 + i, 1);
    pump();
    CHECK(g_fx.started_count == 0, "nothing loads in the tick the script asked in");
    app_tasks_ground_tick(g_fx.app);
    pump();
    CHECK(g_fx.started_count == 12, "a tick releases twelve");
    for( int i = 0; i < 12; i++ )
        CHECK(starts_of(ASSET_KIND_OBJ_MODEL, 200 + i, 1) == 1, "the first twelve, in line");
    pump();
    CHECK(g_fx.started_count == 12, "and no more inside the same tick");
    app_tasks_ground_tick(g_fx.app);
    pump();
    CHECK(g_fx.started_count == ICON_CELLS, "the next tick releases the rest");
    fixture_free();
}

static void
test_widget_icon_missing_parts_ask_again_after_backoff(void)
{
    int starts;

    printf("TEST: widget icons: a cell whose parts are missing asks again after a pause\n");
    fixture_init();
    icons_tree();
    app_cs2_widget_obj_icon_lazy(g_fx.app, cell(1), 201, 1);
    app_tasks_ground_tick(g_fx.app);
    pump();
    CHECK(starts_of(ASSET_KIND_OBJ_MODEL, 201, 1) == 1, "released and loading");
    /* The read comes back empty: the cell still wants it, its parts are
     * still missing. */
    arrive(ASSET_KIND_OBJ_MODEL, 201, 1, false);
    pump();
    starts = starts_of(ASSET_KIND_OBJ_MODEL, 201, 1);
    CHECK(starts == 1, "not asked again at once");
    CHECK(app_widget_icons_pending(g_fx.app) == 1, "never given up while the cell wants it");
    /* The pause (500 ms) is waited as ticks of 20 ms (a named cycle wait,
     * PT_PARALLEL_AWAIT_CYCLE on the release's tick): 24 are not enough. */
    for( int i = 0; i < 24; i++ )
    {
        g_fx.app->host.client.now_ms += 20;
        app_tasks_ground_tick(g_fx.app);
        pump();
    }
    CHECK(starts_of(ASSET_KIND_OBJ_MODEL, 201, 1) == starts, "not before its pause is over");
    g_fx.app->host.client.now_ms += 20;
    app_tasks_ground_tick(g_fx.app);
    pump();
    CHECK(starts_of(ASSET_KIND_OBJ_MODEL, 201, 1) == starts + 1, "asked again after it");
    /* The cell is set to something else: the task ends at its next look. */
    (void)UITree_ApplyObject(g_fx.app->tree, cell(1), 999, 1, -1, 0, 0);
    arrive(ASSET_KIND_OBJ_MODEL, 201, 1, true);
    pump();
    CHECK(app_widget_icons_pending(g_fx.app) == 0, "a cell that wants another obj ends it");
    fixture_free();
}

int
main(void)
{
    printf("=== ground items and widget icons as parallel tasks ===\n");
    ToriDraw_InitSinTable();
    ToriDraw_InitCosTable();

    test_cold_add_lands_when_the_model_arrives();
    test_g3_warm_add_without_a_model_keeps_its_row();
    test_g1_count_variant_loads_and_repoints();
    test_g2_customise_loads_and_reapplies();
    test_a_model_never_served_is_asked_four_times();
    test_picked_up_while_loading();
    test_a_rebuild_in_progress_is_waited_out();
    test_widget_icons_one_task_per_cell_newest_wins();
    test_widget_icons_released_twelve_a_tick();
    test_widget_icon_missing_parts_ask_again_after_backoff();

    if( g_failures )
    {
        printf("FAILED: %d check(s)\n", g_failures);
        return 1;
    }
    printf("PASS\n");
    return 0;
}
