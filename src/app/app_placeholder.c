/*
 * PLACEHOLDERS: a thing that exists in the world NOW with an asset it does
 * not have yet, and the task that lands the asset later.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 *
 * THE RULE. A load that is not critical never parks the packet FIFO. The
 * server's packet stream is applied in order, on the exec runner, and the
 * frame is withheld until a tick's packets have all applied -- that is what
 * keeps a tick atomic on screen. A packet whose handler waits for a cache
 * read inside that pipeline therefore stops the world for the read's round
 * trip, and every packet behind it with it. Through a browser cache a round
 * trip is 40-75 ms; a fight's worth of first-seen drops, graphics and gear
 * was hundreds of milliseconds of frozen picture (memory: web frame drops
 * are withheld frames, 2026-09-18). The reference client never blocks on
 * these: an entity is created at once and drawn when its model exists.
 *
 * So the entity is created at once, with the asset marked absent, and a
 * PLACEHOLDER task loads the asset on the asset runner -- as a stream, never
 * render-blocking -- and lands it when it arrives. The land is guarded, not
 * asserted, against everything that can legitimately happen while it loads:
 * the view can die (a boat despawns), the scene can be rebuilt (World.load_seq
 * moves on), the entity can be removed (the item was picked up), and the
 * entity can change (the stack's count crossed a model-variant threshold).
 * A guard that fires is a no-op, never a stale asset in the wrong place.
 *
 * WHAT COUNTS AS CRITICAL is the one thing this file does not decide: the
 * scene rebuild stays on the FIFO (REBUILD parks on Task_WorldLoad behind the
 * loading screen, by design), and an npc's config read stays inline (it is a
 * preloaded config group, an IndexedDB hit, not a wire read).
 *
 * THE FAMILY. This unit owns the mechanism and the ground-stack kind. The same
 * shape was built, one at a time, before it existed, and each is the same rule
 * with its own landing:
 *   - effect spawns (spotanim, projectile): app_spawn_effect_queue, app_world_edit.c
 *   - loc changes and loc anims, in order: app_spawn_loc_lane_queue, app_world_edit.c
 *   - npc and player bodies: NpcBodyLand / PlayerBodyLand, game/task_entity_assets.c
 *   - interface models and chatheads: App_SetInterfaceModel, app_if_models.c
 *   - inventory icons: InvIconReconcile, app_ui_host.c
 * A new non-critical load belongs here, as a kind: an enum value, the fields
 * its land needs, one await, one land.
 */
#include "app/app_internal.h"

#include "engine/task_obj_model_load.h"

/* How many times a placeholder may go back for a model that the loader says
 * is still not there. A model the cache cannot serve at all would otherwise
 * be asked for forever, one round trip a time; after this the entity simply
 * has no model, which is what the reference draws for it too. */
enum
{
    APP_PLACEHOLDER_ATTEMPT_MAX = 4,
};

struct Task_AppPlaceholder
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    enum AppPlaceholderKind kind;
    /* The scene the entity was created in: the view and its generation as
     * of creation. Both are re-checked at land -- see the unit comment. */
    int view;
    unsigned world_load_seq;
    /* APP_PLACEHOLDER_OBJ_STACK: the stack's tile, obj and the count its
     * model was asked for. The count travels with the id because a
     * stackable's model is the count variant's (ObjModelLoad_RenderObjId). */
    int scene_x;
    int scene_z;
    int level;
    int obj_id;
    int count;
    int attempt;
};

static int
Task_AppPlaceholder_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io);
static void
Task_AppPlaceholder_Free(struct ToriRS_Task* base);

static struct ToriRS_TaskVTable Task_AppPlaceholder_VTable = {
    .run = Task_AppPlaceholder_Run,
    .free = Task_AppPlaceholder_Free,
};

/* The world a placeholder lands in, or NULL when the scene it was made for
 * is gone -- the view died, or a rebuild replaced the world under it. */
static struct World*
app_placeholder_world(struct Task_AppPlaceholder const* self)
{
    struct App* app;
    struct Worldview* wv;

    assert(self);
    app = self->app;
    assert(app);
    if( !WorldviewRegistry_IsLive(&app->worldviews, self->view) )
        return NULL;
    wv = WorldviewRegistry_Get(&app->worldviews, self->view);
    if( !wv->world )
        return NULL;
    if( wv->world->load_seq != self->world_load_seq )
    {
        TORIRS_LOG(
            "placeholder: kind=%d dropped, scene rebuilt while its asset loaded\n",
            (int)self->kind);
        return NULL;
    }
    return wv->world;
}

static void
app_placeholder_queue(
    struct App* app,
    struct Task_AppPlaceholder* task)
{
    struct World const* world = NULL;

    assert(app);
    assert(task);
    task->task.vtable = &Task_AppPlaceholder_VTable;
    strncpy(task->task.name, "AppPlaceholder", sizeof(task->task.name) - 1);
    task->app = app;
    task->view = app->active_world;
    if( WorldviewRegistry_IsLive(&app->worldviews, task->view) )
        world = WorldviewRegistry_Get(&app->worldviews, task->view)->world;
    task->world_load_seq = world ? world->load_seq : 0;
    PT_INIT(&task->pt);
    /* A stream on the asset runner: nothing behind it in the packet stream
     * addresses the asset, and a frame published over it is a correct frame
     * (the entity is already there, drawing nothing). */
    ToriRS_TaskQueue_Add(app->runner.queue, &task->task);
}

void
app_placeholder_obj_stack(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int obj_id,
    int count)
{
    struct Task_AppPlaceholder* task;

    assert(app);
    assert(obj_id >= 0);
    task = calloc(1, sizeof(*task));
    assert(task);
    task->kind = APP_PLACEHOLDER_OBJ_STACK;
    task->scene_x = scene_x;
    task->scene_z = scene_z;
    task->level = level;
    task->obj_id = obj_id;
    task->count = count > 0 ? count : 1;
    app_placeholder_queue(app, task);
}

/*
 * The ground stack's land: the model for the count the stack has NOW.
 *
 * The stack may be gone (picked up, or its tile cleared), may already have a
 * model (a second placeholder for the same stack landed first), or may have
 * changed count across a variant threshold while this one loaded -- in which
 * case the loader is asked again for the count it has, up to the attempt cap.
 */
static void
app_placeholder_land_obj_stack(struct Task_AppPlaceholder* self)
{
    struct App* app = self->app;
    struct World* world = app_placeholder_world(self);
    struct WorldEntity_ObjStack* stack;
    int idx;

    if( !world )
        return;
    idx = World_ObjStackFind(world, self->scene_x, self->scene_z, self->level, self->obj_id);
    if( idx < 0 )
        return;
    stack = World_EntityPoolGet(&world->entities.obj_stack, idx);
    assert(stack);
    if( stack->element_id >= 0 )
        return;
    if( app_obj_stack_land(app, world, idx) )
        return;
    if( self->attempt + 1 >= APP_PLACEHOLDER_ATTEMPT_MAX )
    {
        TORIRS_LOG(
            "placeholder: obj %d x%d at %d,%d,%d has no model after %d loads\n",
            self->obj_id,
            stack->count,
            self->scene_x,
            self->scene_z,
            self->level,
            self->attempt + 1);
        return;
    }
    {
        struct Task_AppPlaceholder* again = calloc(1, sizeof(*again));
        assert(again);
        again->kind = self->kind;
        again->scene_x = self->scene_x;
        again->scene_z = self->scene_z;
        again->level = self->level;
        again->obj_id = self->obj_id;
        again->count = stack->count > 0 ? stack->count : 1;
        again->attempt = self->attempt + 1;
        app_placeholder_queue(app, again);
    }
}

static int
Task_AppPlaceholder_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_AppPlaceholder* self = (struct Task_AppPlaceholder*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);
    switch( self->kind )
    {
    case APP_PLACEHOLDER_OBJ_STACK:
        PT_TASK_AWAITSELF_IF(
            CreateTask_ObjModelLoad(app->provider, &self->obj_id, &self->count, 1));
        app_placeholder_land_obj_stack(self);
        break;
    }
    PT_END(&self->pt);
}

static void
Task_AppPlaceholder_Free(struct ToriRS_Task* base)
{
    free(base);
}
