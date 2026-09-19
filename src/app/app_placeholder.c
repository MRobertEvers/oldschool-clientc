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
 * and one that is a kind here rather than a machine of its own:
 *   - widget obj icons: APP_PLACEHOLDER_WIDGET_ICON, below
 *
 * WHO ASKS DECIDES WHEN IT RUNS. `app_placeholder_queue` adds to the ASSET
 * runner, and a packet handler on the exec runner is not that runner, so a
 * ground stack's placeholder is genuinely deferred. A CLIENTSCRIPT is not:
 * `app_settle_cs2_frame` drains `app->runner` to a fixed point, so a
 * placeholder queued from inside a script is picked up by the settle that is
 * running the script, in the same frame -- the load never leaves the frame it
 * was supposed to leave. Measured: taking the skill guide's 82 obj icons off
 * the script's yield and onto a placeholder each changed its frame by nothing
 * at all (12.3 -> 12.8 ms, 7.9 -> 7.5, 7.7 -> 8.1). So the widget-icon kind
 * parks its requests and a LOGIC TICK releases them, a bounded burst at a
 * time; the park is also what bounds the burst. A future kind asked for by a
 * script needs the same seam, which is why the park is here and not in the
 * caller.
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
    /*
     * Widget icons released per logic tick.
     *
     * Small on purpose. A panel is whatever its script built -- the skill
     * guide's Attack/Weapons tab is 82 cells of eighty-two DIFFERENT items --
     * and releasing them all at once only moves the long frame from the
     * settle to the tick that releases them. Twelve is a slice whose worst
     * case, every obj a cold group read, is a fraction of a 20 ms frame, and
     * a column fills over a handful of ticks the way the reference's does.
     */
    APP_PLACEHOLDER_WIDGET_ICON_BURST = 12,
};

/* A widget-icon request waiting for a tick to release it. */
struct AppPlaceholderIconRequest
{
    int com_id;
    int obj_id;
    int count;
};

/* The park. One pointer on `struct App`; everything about it is this unit's. */
struct AppPlaceholderPark
{
    struct AppPlaceholderIconRequest* icons;
    int count;
    int cap;
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
    /* APP_PLACEHOLDER_WIDGET_ICON: the GRAPHIC cell the icon belongs to. The
     * cell itself carries which obj it currently wants, so the land re-reads
     * it rather than trusting the id this task set out with. */
    int component_id;
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

/*
 * The world a deferred land belongs to, or NULL when the scene it was aimed
 * at is gone.
 *
 * Shared with the effect spawns in app_world_spawn.c, which had a copy of
 * this function to the line: the two mechanisms defer for the same reason,
 * so they drop for the same reasons too. Three of them, and all three are
 * guards rather than asserts -- a parked task cannot be told the world moved
 * under it:
 *
 *   - the VIEW died (a boat despawned);
 *   - a REBUILD replaced the world, and the same tile coordinates now name a
 *     different place (the reference drops its pending spot anims here too);
 *   - a rebuild is IN PROGRESS, and this runner is the one pumping it, so
 *     landing would put an element in a scene being reset. The rebuild's
 *     finish re-queues what still needs it (app_placeholder_obj_stacks_sweep),
 *     so dropping here loses nothing.
 *
 * `owner` and `kind` only name the dropped task in the log.
 */
struct World*
app_deferred_land_world(
    struct App* app,
    int view,
    unsigned world_load_seq,
    char const* owner,
    int kind)
{
    struct Worldview* wv;

    assert(app);
    assert(owner);
    if( !WorldviewRegistry_IsLive(&app->worldviews, view) )
        return NULL;
    wv = WorldviewRegistry_Get(&app->worldviews, view);
    if( !wv->world )
        return NULL;
    if( wv->world->load_seq != world_load_seq )
    {
        TORIRS_LOG("%s: kind=%d dropped, scene rebuilt while its assets loaded\n", owner, kind);
        return NULL;
    }
    if( !wv->world->load_complete )
    {
        TORIRS_LOG("%s: kind=%d dropped, scene mid-rebuild\n", owner, kind);
        return NULL;
    }
    return wv->world;
}

static struct World*
app_placeholder_world(struct Task_AppPlaceholder const* self)
{
    assert(self);
    return app_deferred_land_world(
        self->app, self->view, self->world_load_seq, "placeholder", (int)self->kind);
}

void
app_placeholder_obj_stacks_sweep(
    struct App* app,
    struct World* world)
{
    struct World_EntityPool* pool;

    assert(app);
    assert(world);
    assert(world->load_complete);
    pool = &world->entities.obj_stack;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_ObjStack const* stack = World_EntityPoolGet(pool, i);
        if( !stack || stack->element_id >= 0 )
            continue;
        app_placeholder_obj_stack(
            app,
            stack->grid_position.x,
            stack->grid_position.z,
            stack->grid_position.level,
            stack->obj_id,
            stack->count);
    }
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

/*
 * Park one cell's icon, for the next tick to release. @see the unit comment.
 *
 * Keyed by component id, newest wins. A script rewrites a cell -- the
 * make-menu steps its product through a dozen objs under a held arrow key --
 * and a park that grew an entry per write would load every item it passed
 * through. The land would refuse to bind them (it compares against the cell),
 * so this is not correctness; it is not paying for a dozen model reads to
 * throw eleven away.
 */
void
app_placeholder_widget_icon(
    struct App* app,
    int component_id,
    int obj_id,
    int count)
{
    struct AppPlaceholderPark* park;
    struct AppPlaceholderIconRequest* row = NULL;

    assert(app);
    assert(obj_id > 0);

    if( !app->placeholder_park )
    {
        app->placeholder_park = calloc(1, sizeof(*app->placeholder_park));
        assert(app->placeholder_park);
    }
    park = app->placeholder_park;

    for( int i = 0; i < park->count; i++ )
        if( park->icons[i].com_id == component_id )
        {
            row = &park->icons[i];
            break;
        }
    if( !row )
    {
        if( park->count == park->cap )
        {
            int cap = park->cap ? park->cap * 2 : 32;
            park->icons = realloc(park->icons, (size_t)cap * sizeof(*park->icons));
            assert(park->icons);
            park->cap = cap;
        }
        row = &park->icons[park->count++];
    }
    row->com_id = component_id;
    row->obj_id = obj_id;
    /*
     * The count VERBATIM, not normalised to 1 like the ground stack's.
     *
     * A widget count is not a stack size: `cc_setobject($obj, -1)` is the
     * icon-only form (the skill guide's rows are all of them), and the tree
     * stores that -1 as the cell's item_count. The land compares against the
     * cell, so a normalised 1 here never matches and no icon ever arrives.
     * The loader is indifferent -- obj_model_resolve_count_obj_id treats
     * every count <= 1 alike -- and the icon cache keys the same way the
     * inline bake in exec_set_object does, which is the point.
     */
    row->count = count;
}

/* The CS2 host's hook, reached through the host's world_user. */
void
app_cs2_widget_obj_icon_lazy(
    void* user,
    int component_id,
    int obj_id,
    int count)
{
    assert(user);
    app_placeholder_widget_icon((struct App*)user, component_id, obj_id, count);
}

/* Release a burst of parked icons onto the asset runner. Called from the
 * logic tick, which is the frame boundary the park exists to put between the
 * script and the load. */
void
app_placeholder_release_tick(struct App* app)
{
    struct AppPlaceholderPark* park;
    int release;

    assert(app);
    park = app->placeholder_park;
    if( !park || park->count == 0 )
        return;

    release = park->count < APP_PLACEHOLDER_WIDGET_ICON_BURST
                  ? park->count
                  : APP_PLACEHOLDER_WIDGET_ICON_BURST;
    for( int i = 0; i < release; i++ )
    {
        struct Task_AppPlaceholder* task = calloc(1, sizeof(*task));
        assert(task);
        task->kind = APP_PLACEHOLDER_WIDGET_ICON;
        task->component_id = park->icons[i].com_id;
        task->obj_id = park->icons[i].obj_id;
        task->count = park->icons[i].count;
        app_placeholder_queue(app, task);
    }
    /* Released from the head, so the tail keeps its place in line. */
    park->count -= release;
    memmove(park->icons, park->icons + release,
            (size_t)park->count * sizeof(*park->icons));
}

void
app_placeholder_park_free(struct App* app)
{
    assert(app);
    if( !app->placeholder_park )
        return;
    free(app->placeholder_park->icons);
    free(app->placeholder_park);
    app->placeholder_park = NULL;
}

/*
 * The widget icon's land: bake the obj the cell wants NOW and bind it.
 *
 * No view and no world -- a widget is not in the scene -- so the guards are
 * the tree's. The cell may be gone (the panel closed, or a CC_DELETEALL
 * rebuilt it without this node), may already have an icon (a second
 * placeholder for the same cell landed first), or may have been set to a
 * different obj while this one loaded. The last is why the land compares
 * against the node rather than trusting `self`: a script stepping a cell
 * through a dozen objs -- the make-menu under a held arrow key -- leaves a
 * placeholder in flight per step, and only the newest names what the cell
 * wants. An older one that landed on its own id would put the wrong item in
 * the cell, which is worse than the blank it replaces.
 */
static void
app_placeholder_land_widget_icon(struct Task_AppPlaceholder* self)
{
    struct App* app = self->app;
    struct UITreeComponent const* cell;
    int32_t idx;
    int scene_id;

    if( !app->tree )
        return;
    idx = UITree_FindByComponentId(app->tree, self->component_id);
    if( idx < 0 )
        return;
    cell = &app->tree->components[idx];
    if( cell->item_id != self->obj_id || cell->item_count != self->count )
        return;
    if( cell->item_scene_id > 0 )
        return;

    scene_id = UITreeSceneBridge_EnsureObjIcon(&app->bridge, self->obj_id, self->count);
    if( scene_id >= 0 )
    {
        (void)UITree_ApplyObject(
            app->tree,
            self->component_id,
            self->obj_id,
            self->count,
            scene_id,
            0,
            cell->item_num_mode);
        UITree_HostInputsChanged(
            &app->ui_host, UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_ASSETS));
        app->need_redraw = 1;
        return;
    }
    if( self->attempt + 1 >= APP_PLACEHOLDER_ATTEMPT_MAX )
    {
        TORIRS_LOG(
            "placeholder: obj %d x%d on component 0x%08x has no icon after %d loads\n",
            self->obj_id,
            self->count,
            (unsigned)self->component_id,
            self->attempt + 1);
        return;
    }
    {
        struct Task_AppPlaceholder* again = calloc(1, sizeof(*again));
        assert(again);
        again->kind = self->kind;
        again->component_id = self->component_id;
        again->obj_id = self->obj_id;
        again->count = self->count;
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
    /*
     * Dispatched on `kind` with if/else, never a switch: a protothread IS a
     * switch on its resume point, and a nested switch captures the await's
     * `case` label. The task then resumed into the inner switch's dead end,
     * ended without stepping its child, and its item still held the child's
     * landed archive -- freed by the wrong deallocator on the way out
     * (ASan double-free on model 7760, 2026-09-18).
     */
    if( self->kind == APP_PLACEHOLDER_OBJ_STACK )
    {
        PT_TASK_AWAITSELF_IF(
            CreateTask_ObjModelLoad(app->provider, &self->obj_id, &self->count, 1));
        app_placeholder_land_obj_stack(self);
    }
    else if( self->kind == APP_PLACEHOLDER_WIDGET_ICON )
    {
        PT_TASK_AWAITSELF_IF(
            CreateTask_ObjModelLoad(app->provider, &self->obj_id, &self->count, 1));
        app_placeholder_land_widget_icon(self);
    }
    else
        assert(0 && "unknown placeholder kind");
    PT_END(&self->pt);
}

static void
Task_AppPlaceholder_Free(struct ToriRS_Task* base)
{
    free(base);
}
