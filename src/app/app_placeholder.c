/*
 * PLACEHOLDERS: a thing that exists in the world NOW with an asset it does
 * not have yet, and the task that lands the asset later.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 *
 * THE RULE. A load that is not critical never parks the packet queue. The
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
 * scene rebuild stays on the FIFO (the REBUILD's packet task awaits the
 * gather, CreateTask_WorldGather, and builds inline, behind the loading
 * screen, by design), and an npc's config read stays inline (it is a
 * preloaded config group, an IndexedDB hit, not a wire read).
 *
 * THE FAMILY. This unit owns the mechanism and the ground-stack kind. The same
 * shape was built, one at a time, before it existed, and each is the same rule
 * with its own landing:
 *   - effect spawns (spotanim, projectile): app_spawn_effect_queue, app_world_edit.c
 *   - loc changes, one per tile and layer: Task_LocChange, app_tasks_locs.c
 *   - npc and player bodies: NpcBodyLand / PlayerBodyLand, game/task_entity_assets.c
 *   - interface models and chatheads: App_SetInterfaceModel, app_if_models.c
 *   - inventory icons: InvIconReconcile, app_ui_host.c
 * and one that is a kind here rather than a machine of its own:
 *   - widget obj icons: APP_PLACEHOLDER_WIDGET_ICON, below
 *
 * WHO ASKS DECIDES WHEN IT RUNS. `app_placeholder_queue` adds to the ASSET
 * runner, and a packet handler on the exec runner is not that runner, so a
 * ground stack's placeholder is genuinely deferred. A CLIENTSCRIPT is not:
 * `app_settle_cs2_frame` drains `app->frame_runner` to a fixed point, so a
 * placeholder queued from inside a script is picked up by the settle that is
 * running the script, in the same frame -- the load never leaves the frame it
 * was supposed to leave. Measured: taking the skill guide's 82 obj icons off
 * the script's yield and onto a placeholder each changed its frame by nothing
 * at all (12.3 -> 12.8 ms, 7.9 -> 7.5, 7.7 -> 8.1). So the widget-icon kind
 * parks its requests and a LOGIC TICK releases them, a bounded burst at a
 * time; the park is also what bounds the burst. A future kind asked for by a
 * script needs the same seam, which is why the park is here and not in the
 * caller.
 * A new non-critical load does NOT belong here: it is a parallel task of its
 * family (app/app_parallel_task.h; the family units are listed in
 * app/app_tasks.h), and the kinds above move there as their families convert
 * them.
 */
#include "app/app_internal.h"
#include "mem/torirs_mem.h"

#include "engine/task_obj_model_load.h"
#include "inv/widget_icon_park.h"

/* How many times a ground stack goes back for a model the loader says is
 * still not there is the assets table's OBJ_MODEL policy (give up after 4,
 * engine/asset_requests.c): a model the cache cannot serve at all would
 * otherwise be asked for forever, one round trip a time; after that the entity
 * simply has no model, which is what the reference draws for it too. (A widget
 * icon is not capped: see app_placeholder_land_widget_icon.) */
enum
{
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

/* The park (inv/widget_icon_park.h). One pointer on `struct App`, made by
 * this unit on the first request. */

struct Task_AppPlaceholder
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    enum AppPlaceholderKind kind;
    /* The obj and the count its model was asked for. */
    int obj_id;
    int count;
    /* APP_PLACEHOLDER_WIDGET_ICON: the GRAPHIC cell the icon belongs to. The
     * cell itself carries which obj it currently wants, so the land re-reads
     * it rather than trusting the id this task set out with. */
    int component_id;
    /* The pause it was released after (0 for the script's own request). */
    uint32_t wait_ms;
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
    assert(app);
    assert(task);
    task->task.vtable = &Task_AppPlaceholder_VTable;
    strncpy(task->task.name, "AppPlaceholder", sizeof(task->task.name) - 1);
    task->app = app;
    PT_INIT(&task->pt);
    /* A stream on the asset runner: nothing behind it in the packet stream
     * addresses the asset, and a frame published over it is a correct frame
     * (the entity is already there, drawing nothing). */
    ToriRS_TaskQueue_Add(app->frame_runner.queue, &task->task);
}

/*
 * The ground stack's land, from the obj model load's end: the model for the
 * count the stack has NOW.
 *
 * The handle is where the stack was asked for -- its tile, and the view and
 * scene generation it was created in -- never a pointer: the stack may be gone
 * (picked up, or its tile cleared), may already have a model (a second want
 * for the same stack landed first), the scene may have been rebuilt, or the
 * count may have crossed a variant threshold while this loaded -- in which
 * case the model for the count it has is wanted, re-capturing the view. The
 * table went back for a model the loader said was still missing up to the
 * OBJ_MODEL policy's four times before landing FAILED.
 */
static void
app_placeholder_obj_stack_landed(
    void* ctx,
    struct AssetKey key,
    struct AssetWaiterHandle handle,
    enum AssetOutcome outcome,
    uint32_t late_ms)
{
    struct App* app = ctx;
    int const scene_x = handle.v[0];
    int const scene_z = handle.v[1];
    int const level = handle.v[2] & 0xf;
    int const view = handle.v[2] >> 4;
    unsigned const world_load_seq = (unsigned)handle.v[3];
    int const obj_id = key.id;
    struct World* world;
    struct WorldEntity_ObjStack* stack;
    int count;
    int idx;

    (void)late_ms;
    world = app_deferred_land_world(
        app, view, world_load_seq, "placeholder", (int)APP_PLACEHOLDER_OBJ_STACK);
    if( !world )
        return;
    idx = World_ObjStackFind(world, scene_x, scene_z, level, obj_id);
    if( idx < 0 )
        return;
    stack = World_EntityPoolGet(&world->entities.obj_stack, idx);
    assert(stack);
    if( stack->element_id >= 0 )
        return;
    if( outcome == ASSET_OUTCOME_LANDED && app_obj_stack_land(app, world, idx) )
        return;
    count = stack->count > 0 ? stack->count : 1;
    if( outcome != ASSET_OUTCOME_LANDED || count == key.variant )
    {
        TORIRS_LOG(
            "placeholder: obj %d x%d at %d,%d,%d has no model after %d loads\n",
            obj_id,
            stack->count,
            scene_x,
            scene_z,
            level,
            outcome != ASSET_OUTCOME_LANDED ? Assets_Policy(ASSET_KIND_OBJ_MODEL)->fail_attempts
                                            : 1);
        return;
    }
    app_placeholder_obj_stack(app, scene_x, scene_z, level, obj_id, count);
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
    struct AssetWaiter waiter;
    struct World const* world = NULL;
    int view;

    assert(app);
    assert(obj_id >= 0);
    assert(level >= 0 && level < 16);
    view = WorldviewRegistry_Active(&app->worldviews);
    assert(view >= 0);
    if( WorldviewRegistry_IsLive(&app->worldviews, view) )
        world = WorldviewRegistry_Get(&app->worldviews, view)->world;
    memset(&waiter, 0, sizeof(waiter));
    waiter.land = app_placeholder_obj_stack_landed;
    waiter.ctx = app;
    waiter.handle.v[0] = scene_x;
    waiter.handle.v[1] = scene_z;
    waiter.handle.v[2] = level | (view << 4);
    waiter.handle.v[3] = (int32_t)(world ? world->load_seq : 0);
    /* The count travels with the id because a stackable's model is the count
     * variant's (ObjModelLoad_RenderObjId). A model already resident lands at
     * once, as a task that found nothing to load always did. */
    if( Assets_Want(
            app->assets,
            AssetKey_Make(ASSET_KIND_OBJ_MODEL, obj_id, count > 0 ? count : 1),
            &waiter) == ASSET_STATE_HERE )
        app_placeholder_obj_stack_landed(
            app,
            AssetKey_Make(ASSET_KIND_OBJ_MODEL, obj_id, count > 0 ? count : 1),
            waiter.handle,
            ASSET_OUTCOME_LANDED,
            0);
}

/* The cell's row in the park, made if it has none; `*found` says which. */
static struct WidgetIconRequest*
app_placeholder_park_row(
    struct App* app,
    int component_id,
    bool* found)
{
    assert(app);
    assert(found);

    if( !app->placeholder_park )
        app->placeholder_park = WidgetIconPark_New();
    return WidgetIconPark_Row(app->placeholder_park, component_id, found);
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
    struct WidgetIconRequest* row;
    bool found;

    assert(app);
    assert(obj_id >= 0);

    row = app_placeholder_park_row(app, component_id, &found);
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
    row->due_ms = 0;
    row->wait_ms = 0;
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
    struct WidgetIconRequest due[APP_PLACEHOLDER_WIDGET_ICON_BURST];
    int release;

    assert(app);
    if( !app->placeholder_park || WidgetIconPark_Count(app->placeholder_park) == 0 )
        return;

    /* The due rows from the head, so the rest keep their place in line. */
    release = WidgetIconPark_TakeDue(
        app->placeholder_park, (uint64_t)app->host.client.now_ms, APP_PLACEHOLDER_WIDGET_ICON_BURST, due);
    for( int i = 0; i < release; i++ )
    {
        struct Task_AppPlaceholder* task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
        assert(task);
        task->kind = APP_PLACEHOLDER_WIDGET_ICON;
        task->component_id = due[i].com_id;
        task->obj_id = due[i].obj_id;
        task->count = due[i].count;
        task->wait_ms = due[i].wait_ms;
        app_placeholder_queue(app, task);
    }
}

void
app_placeholder_park_free(struct App* app)
{
    assert(app);
    WidgetIconPark_Free(app->placeholder_park);
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
        RedrawRequest_Raise(&app->redraw_request);
        return;
    }
    /* Every part resident and still no icon (an obj with no inventory
     * model): asking again would load nothing new. */
    if( !ObjModelLoad_NeedsWork(app->provider, self->obj_id, self->count) )
        return;
    /*
     * Parts still missing: back to the park, due after a pause.
     *
     * Never given up on while the cell wants this obj. The read that came
     * back empty may be a dropped JS5 socket or a browser storage error, and
     * a cap of a few asks back to back spent them all inside the outage: a
     * shop whose stock never changes (nothing re-runs its script) kept the
     * cells blank for the session (RSProx 4262, Ivan's Supplies, model reads
     * failed for three seconds as it opened). The guards above end it --
     * the cell closes or is set to something else -- and the pause bounds
     * what an obj the cache cannot serve costs meanwhile.
     *
     * A newer request for the cell, parked by the script while this one
     * loaded, already names what the cell wants; it is left alone.
     */
    {
        bool found;
        struct WidgetIconRequest* row =
            app_placeholder_park_row(app, self->component_id, &found);

        if( found )
            return;
        row->obj_id = self->obj_id;
        row->count = self->count;
        row->wait_ms =
            Assets_BackoffNext(Assets_Policy(ASSET_KIND_INV_ICONS), self->wait_ms);
        row->due_ms = (uint64_t)app->host.client.now_ms + row->wait_ms;
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
    if( self->kind == APP_PLACEHOLDER_WIDGET_ICON )
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
    ToriMem_Free(ToriMem_Process(), base);
}
