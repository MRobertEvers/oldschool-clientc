/*
 * PARALLEL TASKS: GROUND ITEMS AND WIDGET ICONS.
 *
 * Two kinds of work that must not hold the packet queue, each one task on the
 * parallel queue (app->frame_runner), added as an ordinary task, carrying its
 * own data, listed in the pending index (app->pending) while it lives. To see
 * what happens to a ground item's model or a widget's icon after its packet
 * or script, read the task's run function: it reads top to bottom -- await,
 * cancelled?, still wanted?, apply -- with its policies as `if`s in that body
 * and no `switch` in it.
 *
 * THE GROUND STACK'S MODEL (OBJ_ADD, OBJ_REVEAL, OBJ_COUNT, OBJ_CUSTOMISE,
 * OBJ_UNCUSTOMISE and their _SPECIFIC forms). The row is the server's and is
 * written at packet time (app_world_rebuild.c); only its picture waits. The
 * packet handler asks app_ground_stack_show to show the model the row's state
 * selects (its obj, the count variant, its customisation): at once when every
 * part is resident, otherwise by a Task_GroundStackModel that awaits the obj
 * model (ASSET_KIND_OBJ_MODEL: the objtype, its count variant, the model and
 * its textures) and, for a customised stack, the replacement model, then
 * gives the row its element or re-points the element it has. Until then a
 * row without an element draws nothing (it is listed, and clickable through
 * the overlay), and a row with one keeps the model it had.
 *
 *   key        the row: its pool index and its serial (World_ObjStackAdd
 *              numbers every row), never a pointer. A newer packet for the
 *              same row (a count, a customisation) cancels the older task and
 *              adds its own; a removed row cancels its task.
 *   a rebuild  does not drop it: the row outlives a rebuild (the shift moves
 *              it, or deletes it when it leaves the scene), so the task waits
 *              for the scene to be complete and applies to the row where it
 *              is then. A row that is gone is a normal exit.
 *   failure    the task goes back for an obj model up to the OBJ_MODEL
 *              policy's four attempts (engine/asset_requests.c), at once;
 *              after that the row has no model, which is what the reference
 *              draws for it too. A
 *              rebuild asks once more for every row still without one
 *              (app_ground_stacks_sweep).
 *
 * THE WIDGET ICON a clientscript set on a GRAPHIC cell whose obj was not
 * resident (RS_CS2Host.widget_obj_icon_lazy). The cell carries the obj (its
 * ops, tooltip and stack number all read it) with no picture until its
 * Task_WidgetIcon bakes the icon and binds it.
 *
 *   key        the component and its interface's close serial: a closed
 *              interface's cell is not the one asked for, even if the group
 *              opens again. A script that rewrites a cell -- the make-menu
 *              steps its product through a dozen objs under a held arrow key
 *              -- cancels the older task; asking again for what is already
 *              pending only makes it due at once.
 *   release    a script runs inside the settle, and the settle drains the
 *              parallel queue, so a load made from a script is stepped by the
 *              settle that is running the script, in the same frame: the load
 *              never leaves the frame it was meant to leave. Measured: taking
 *              the skill guide's 82 obj icons off the script's yield onto a
 *              task each changed its frame by nothing at all (12.3 -> 12.8
 *              ms, 7.9 -> 7.5, 7.7 -> 8.1). So a widget icon waits first for
 *              a LOGIC TICK after the one it was asked in, and a tick releases
 *              at most APP_TASKS_GROUND_ICON_BURST of them; the others keep
 *              their places in the queue for the next tick. This is the one
 *              case the brief names; nothing else here is paced.
 *   failure    never given up on while the cell wants the obj. The read that
 *              came back empty may be a dropped JS5 socket or a browser
 *              storage error, and a cap of a few asks back to back spent them
 *              all inside the outage: a shop whose stock never changes
 *              (nothing re-runs its script) kept the cells blank for the
 *              session (RSProx 4262, Ivan's Supplies, model reads failed for
 *              three seconds as it opened). The task asks again after a
 *              pause, doubling from 500 ms to 4 s (the INV_ICONS policy); the
 *              cell closing or being set to something else ends it.
 *
 * Kinds come from RS_PENDING_KINDS_GROUND_FIRST .. _LAST (game/rs_pending.h);
 * what other units of the layer call is declared in this family's section of
 * app/app_tasks.h.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; nothing outside the layer may include app/app_internal.h.
 */

#include "app/app_internal.h"
#include "mem/torirs_mem.h"

#include "engine/task_obj_model_load.h"

#include <stddef.h>

enum AppTasksGroundKind
{
    /* A ground stack's model: Task_GroundStackModel. */
    APP_TASKS_GROUND_STACK_MODEL = RS_PENDING_KINDS_GROUND_FIRST,
    /* A widget's obj icon: Task_WidgetIcon. */
    APP_TASKS_GROUND_WIDGET_ICON,
};

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
    APP_TASKS_GROUND_ICON_BURST = 12,
};

/* app->tasks_ground: the logic tick the widget icons count from -- the
 * clock their cycle wait parks on (PT_PARALLEL_AWAIT_CYCLE), advanced in one
 * place, app_tasks_ground_tick, on a client cycle whose packet transaction
 * closed -- and how many this tick has released. Made on the first icon
 * request. */
struct AppTasksGround
{
    struct ToriRS_Clock clock;
    int released;
};

static struct AppTasksGround*
ground_state(struct App* app)
{
    assert(app);
    if( !app->tasks_ground )
    {
        app->tasks_ground = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*app->tasks_ground));
        assert(app->tasks_ground);
        ToriRS_Clock_Init(&app->tasks_ground->clock, "the widget icons' release tick", 0);
    }
    return app->tasks_ground;
}

void
app_tasks_ground_tick(struct App* app)
{
    assert(app);
    /* No icon asked for yet: nothing counts from a tick. */
    if( !app->tasks_ground )
        return;
    app->tasks_ground->released = 0;
    /* The one place this clock moves: the icons due this tick wake. */
    ToriRS_Clock_Advance(&app->tasks_ground->clock, app->tasks_ground->clock.now + 1u);
}

void
app_tasks_ground_free(struct App* app)
{
    assert(app);
    if( app->tasks_ground )
    {
        /* Its storage goes: an icon still parked on it wakes (and finds
         * itself cancelled, or its tick come). */
        ToriRS_Clock_Retire(&app->tasks_ground->clock);
        ToriMem_Free(ToriMem_Process(), app->tasks_ground);
    }
    app->tasks_ground = NULL;
}

/* ---- the ground stack's model -------------------------------------------- */

struct Task_GroundStackModel
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_ParallelTask par;
    struct App* app;
    /* The row (the key) and the view it lives in (par.view). */
    int idx;
    uint32_t serial;
    /* What it awaits: the row's state when the task was made. */
    int obj_id;
    int count;
    int custom_model; /* -1: none */
    /* Failed reads of the obj model in a row (the give-up policy). */
    int attempts;
};

static struct RS_PendingKey
ground_stack_key(
    int idx,
    uint32_t serial)
{
    return RS_PendingKey_Id(idx, serial);
}

/* The row (idx, serial) in `world`, or NULL when it is gone: picked up, its
 * tile cleared, shifted off the scene, or its slot reused by another row. */
static struct WorldEntity_ObjStack*
ground_stack_row(
    struct World* world,
    int idx,
    uint32_t serial)
{
    struct World_EntityPool* pool;
    struct WorldEntity_ObjStack* stack;

    assert(world);
    pool = &world->entities.obj_stack;
    if( !World_EntityPoolIsActive(pool, idx) )
        return NULL;
    stack = World_EntityPoolGet(pool, idx);
    if( !stack || stack->serial != serial )
        return NULL;
    return stack;
}

/* Can a ground task look at its view now? Yes when the view or its world is
 * gone (it will find nothing) or the scene is complete; no while a rebuild is
 * building it, which is waited out. */
static bool
ground_view_settled(
    struct App* app,
    int view)
{
    struct Worldview* wv;

    if( !WorldviewRegistry_IsLive(&app->worldviews, view) )
        return true;
    wv = WorldviewRegistry_Get(&app->worldviews, view);
    return !wv->world || wv->world->load_complete;
}

/* The view's world when it is live and complete, NULL otherwise. */
static struct World*
ground_view_world(
    struct App* app,
    int view)
{
    struct Worldview* wv;

    if( !WorldviewRegistry_IsLive(&app->worldviews, view) )
        return NULL;
    wv = WorldviewRegistry_Get(&app->worldviews, view);
    if( !wv->world || !wv->world->load_complete )
        return NULL;
    return wv->world;
}

static void
ground_stack_no_model(
    struct WorldEntity_ObjStack const* stack,
    char const* why)
{
    TORIRS_LOG(
        "ground: obj %d x%d at %d,%d,%d has no model (%s)\n",
        stack->obj_id,
        stack->count,
        stack->grid_position.x,
        stack->grid_position.z,
        stack->grid_position.level,
        why);
}

static int
Task_GroundStackModel_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_GroundStackModel* self = (struct Task_GroundStackModel*)base;
    struct App* app = self->app;
    struct World* world;
    struct WorldEntity_ObjStack* stack;
    int applied;

    (void)io;
    PT_BEGIN(&self->pt);
    for( ;; )
    {
        /* 1. What the row's state selects. A stackable's model is its count
         *    variant's (ObjModelLoad_RenderObjId), so the count is part of
         *    the key; every count <= 1 is the base. */
        PT_PARALLEL_AWAIT_ASSET(
            app->assets,
            AssetKey_Make(ASSET_KIND_OBJ_MODEL, self->obj_id, self->count > 0 ? self->count : 1));
        /* 2. Replaced by a newer packet for the row, or the row was removed. */
        if( RS_ParallelTask_Cancelled(&self->par) )
            PT_EXIT(&self->pt);
        /* A read that came back without it is asked again at once, up to the
         * OBJ_MODEL policy's attempts (the table ends an awaiter's wait at
         * each failed attempt); after that the row has no model, which is
         * what the reference draws for one the cache cannot serve. */
        if( !RS_ParallelTask_AssetHere(&self->par) )
        {
            self->attempts++;
            if( self->attempts < Assets_Policy(ASSET_KIND_OBJ_MODEL)->fail_attempts )
                continue;
            TORIRS_LOG(
                "ground: obj %d x%d has no model after %d loads\n",
                self->obj_id,
                self->count,
                self->attempts);
            PT_EXIT(&self->pt);
        }
        self->attempts = 0;
        /* A customised stack's replacement model (OBJ_CUSTOMISE). */
        if( self->custom_model > -1 )
        {
            PT_PARALLEL_AWAIT_ASSET(
                app->assets, AssetKey_Make(ASSET_KIND_MODEL, self->custom_model, 0));
            if( RS_ParallelTask_Cancelled(&self->par) )
                PT_EXIT(&self->pt);
            if( !RS_ParallelTask_AssetHere(&self->par) )
            {
                TORIRS_LOG(
                    "ground: obj %d's replacement model %d did not load\n",
                    self->obj_id,
                    self->custom_model);
                PT_EXIT(&self->pt);
            }
        }
        /* 3. Still wanted? A rebuild of its view is waited out, by name:
         *    the row outlives it. Then the row itself, which may be gone. */
        PT_APP_AWAIT_VIEW_REBUILT(app, ground_view_settled(app, self->par.view));
        if( RS_ParallelTask_Cancelled(&self->par) )
            PT_EXIT(&self->pt);
        world = ground_view_world(app, self->par.view);
        if( !world )
            PT_EXIT(&self->pt);
        stack = ground_stack_row(world, self->idx, self->serial);
        if( !stack )
            PT_EXIT(&self->pt);
        /* 4. Apply, building from the cache now. */
        applied = app_obj_stack_apply_model(app, world, self->idx, 1);
        if( applied > 0 )
            PT_EXIT(&self->pt);
        if( applied < 0 )
        {
            ground_stack_no_model(stack, "every part resident");
            PT_EXIT(&self->pt);
        }
        /* A part is missing again: the cache evicted it while this waited,
         * or the row's state moved on without a packet replacing this task.
         * Wait for what the row selects now. */
        self->obj_id = stack->obj_id;
        self->count = stack->count;
        self->custom_model = stack->customised ? stack->custom_model : -1;
    }
    PT_END(&self->pt);
}

static void
Task_GroundStackModel_Free(struct ToriRS_Task* base)
{
    struct Task_GroundStackModel* self = (struct Task_GroundStackModel*)base;

    App_ParallelTaskRelease(self->app, &self->par);
    ToriMem_Free(ToriMem_Process(), self);
}

static struct ToriRS_TaskVTable Task_GroundStackModel_VTable = {
    .run = Task_GroundStackModel_Run,
    .free = Task_GroundStackModel_Free,
};

void
app_ground_stack_show(
    struct App* app,
    struct World* world,
    int view,
    int idx,
    int announce)
{
    struct WorldEntity_ObjStack const* stack;
    struct Task_GroundStackModel* task;
    struct RS_PendingKey key;
    int applied;

    assert(app);
    assert(app->pending);
    assert(world);
    assert(view >= 0);
    assert(idx >= 0);
    stack = World_EntityPoolGet(&world->entities.obj_stack, idx);
    assert(stack);
    key = ground_stack_key(idx, stack->serial);
    /* Newest wins: what an older task waits for is no longer what the row
     * selects. */
    RS_Pending_CancelKey(app->pending, APP_TASKS_GROUND_STACK_MODEL, key);
    applied = app_obj_stack_apply_model(app, world, idx, announce);
    if( applied > 0 )
        return;
    if( applied < 0 )
    {
        ground_stack_no_model(stack, "every part resident");
        return;
    }
    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_GroundStackModel_VTable;
    strncpy(task->task.name, "GroundStackModel", sizeof(task->task.name) - 1);
    PT_INIT(&task->pt);
    task->app = app;
    task->idx = idx;
    task->serial = stack->serial;
    task->obj_id = stack->obj_id;
    task->count = stack->count;
    task->custom_model = stack->customised ? stack->custom_model : -1;
    App_ParallelTaskAdd(app, &task->task, &task->par, APP_TASKS_GROUND_STACK_MODEL, key, view);
}

void
app_ground_stack_forget(
    struct App* app,
    struct World* world,
    int idx)
{
    struct WorldEntity_ObjStack const* stack;

    assert(app);
    assert(app->pending);
    assert(world);
    assert(idx >= 0);
    stack = World_EntityPoolGet(&world->entities.obj_stack, idx);
    assert(stack);
    RS_Pending_CancelKey(
        app->pending, APP_TASKS_GROUND_STACK_MODEL, ground_stack_key(idx, stack->serial));
}

int
app_ground_stack_pending(
    struct App* app,
    struct World* world,
    int idx)
{
    struct WorldEntity_ObjStack const* stack;

    assert(app);
    assert(app->pending);
    assert(world);
    assert(idx >= 0);
    stack = World_EntityPoolGet(&world->entities.obj_stack, idx);
    assert(stack);
    return RS_Pending_Find(
               app->pending, APP_TASKS_GROUND_STACK_MODEL, ground_stack_key(idx, stack->serial)) !=
           NULL;
}

void
app_ground_stacks_sweep(
    struct App* app,
    struct World* world,
    int view)
{
    struct World_EntityPool* pool;

    assert(app);
    assert(world);
    assert(WorldviewRegistry_IsLive(&app->worldviews, view));
    assert(world == WorldviewRegistry_Get(&app->worldviews, view)->world);
    assert(world->load_complete);
    pool = &world->entities.obj_stack;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_ObjStack const* stack = World_EntityPoolGet(pool, i);
        if( !stack || stack->element_id >= 0 )
            continue;
        if( RS_Pending_Find(
                app->pending, APP_TASKS_GROUND_STACK_MODEL, ground_stack_key(i, stack->serial)) )
            continue;
        app_ground_stack_show(app, world, view, i, 1);
    }
}

/* ---- the widget icon ----------------------------------------------------- */

struct Task_WidgetIcon
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_ParallelTask par;
    struct App* app;
    /* The GRAPHIC cell (par.key: it and its interface's close serial) and
     * what it was asked to show. The cell carries what it wants now, so the
     * apply compares against the cell rather than trusting these. */
    int component_id;
    int obj_id;
    int count;
    /* The logic tick it was asked in: it is released in a later one. */
    uint32_t asked_tick;
    /* The tick (AppTasksGround::tick) it may go in, not before: the one
     * after it was asked, or after a miss the end of its pause; and the
     * pause it took. */
    uint32_t due_tick;
    uint32_t wait_ms;
};

static struct RS_PendingKey
widget_icon_key(
    struct App* app,
    int component_id)
{
    assert(app->tree);
    return RS_PendingKey_Component(
        component_id, UITree_GroupCloseSerial(app->tree, (component_id >> 16) & 0xffff));
}

/* Its tick has come: does this tick's release have room for it? Taking a
 * place counts it. Not a protothread. */
static bool
widget_icon_take_place(struct App* app)
{
    struct AppTasksGround* st = ground_state(app);

    if( st->released >= APP_TASKS_GROUND_ICON_BURST )
        return false;
    st->released++;
    return true;
}

/*
 * Bake the obj the cell wants NOW and bind it. 1 when this task is done
 * (bound, or there is nothing it can still do), 0 when parts are still
 * missing and it should ask again after a pause.
 *
 * No view and no world -- a widget is not in the scene -- so the checks are
 * the tree's. The interface may have closed (its close serial moved on), the
 * cell may be gone (a CC_DELETEALL rebuilt it without this node), may already
 * have an icon, or may have been set to a different obj while this loaded. The
 * last is why this compares against the node: an older task that bound its
 * own id would put the wrong item in the cell, which is worse than the blank
 * it replaces.
 */
static int
widget_icon_apply(struct Task_WidgetIcon* self)
{
    struct App* app = self->app;
    struct UITreeComponent const* cell;
    int32_t idx;
    int scene_id;

    assert(app->tree);
    if( UITree_GroupCloseSerial(app->tree, (self->component_id >> 16) & 0xffff) !=
        self->par.key.gen )
        return 1;
    idx = UITree_FindByComponentId(app->tree, self->component_id);
    if( idx < 0 )
        return 1;
    cell = &app->tree->components[idx];
    if( cell->item_id != self->obj_id || cell->item_count != self->count )
        return 1;
    if( cell->item_scene_id > 0 )
        return 1;

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
        UITree_HostInputsChanged(&app->ui_host, UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_ASSETS));
        RedrawRequest_Raise(&app->redraw_request);
        return 1;
    }
    /* Every part resident and still no icon (an obj with no inventory
     * model): asking again would load nothing new. */
    if( !ObjModelLoad_NeedsWork(app->provider, self->obj_id, self->count) )
        return 1;
    return 0;
}

static int
Task_WidgetIcon_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_WidgetIcon* self = (struct Task_WidgetIcon*)base;
    struct App* app = self->app;

    (void)io;
    PT_BEGIN(&self->pt);
    for( ;; )
    {
        /* Its turn: a later tick than the script's (a client cycle, by
         * name), inside the tick's release; a full release waits for the
         * next tick. */
        PT_PARALLEL_AWAIT_CYCLE(&ground_state(app)->clock, self->due_tick);
        if( RS_ParallelTask_Cancelled(&self->par) )
            PT_EXIT(&self->pt);
        if( !widget_icon_take_place(app) )
        {
            self->due_tick = ground_state(app)->clock.now + 1u;
            continue;
        }
        /* 1. The obj's model and its parts. Every count <= 1 resolves alike
         *    (obj_model_resolve_count_obj_id), so -- the icon-only form,
         *    `cc_setobject($obj, -1)` -- shares the base's load. */
        PT_PARALLEL_AWAIT_ASSET(
            app->assets,
            AssetKey_Make(ASSET_KIND_OBJ_MODEL, self->obj_id, self->count > 0 ? self->count : 1));
        /* 2. Replaced by a newer request for the cell, or a logout. */
        if( RS_ParallelTask_Cancelled(&self->par) )
            PT_EXIT(&self->pt);
        /* 3 and 4: the cell's checks, then the bake. A failed load is not a
         *    reason by itself: the cell decides. */
        if( widget_icon_apply(self) )
            PT_EXIT(&self->pt);
        /* Parts still missing: ask again after a pause, counted in ticks. */
        self->wait_ms = Assets_BackoffNext(Assets_Policy(ASSET_KIND_INV_ICONS), self->wait_ms);
        self->due_tick = ground_state(app)->clock.now + App_ParallelCyclesFor(self->wait_ms);
    }
    PT_END(&self->pt);
}

static void
Task_WidgetIcon_Free(struct ToriRS_Task* base)
{
    struct Task_WidgetIcon* self = (struct Task_WidgetIcon*)base;

    App_ParallelTaskRelease(self->app, &self->par);
    ToriMem_Free(ToriMem_Process(), self);
}

static struct ToriRS_TaskVTable Task_WidgetIcon_VTable = {
    .run = Task_WidgetIcon_Run,
    .free = Task_WidgetIcon_Free,
};

static struct Task_WidgetIcon*
widget_icon_task(struct RS_ParallelTask* par)
{
    return (struct Task_WidgetIcon*)((char*)par - offsetof(struct Task_WidgetIcon, par));
}

/*
 * The CS2 host's hook, reached through the host's world_user: the cell is set
 * (the script did that), its icon is a task's.
 *
 * The count VERBATIM, not normalised to 1 like the ground stack's. A widget
 * count is not a stack size: `cc_setobject($obj, -1)` is the icon-only form
 * (the skill guide's rows are all of them), and the tree stores that -1 as the
 * cell's item_count. The apply compares against the cell, so a normalised 1
 * here never matches and no icon ever arrives.
 */
void
app_cs2_widget_obj_icon_lazy(
    void* user,
    int component_id,
    int obj_id,
    int count)
{
    struct App* app = user;
    struct RS_PendingKey key;
    struct RS_ParallelTask* pending;
    struct Task_WidgetIcon* task;

    assert(app);
    assert(app->pending);
    assert(app->tree);
    assert(obj_id >= 0);
    key = widget_icon_key(app, component_id);
    pending = RS_Pending_Find(app->pending, APP_TASKS_GROUND_WIDGET_ICON, key);
    if( pending )
    {
        struct Task_WidgetIcon* same = widget_icon_task(pending);
        /* Asked again for what it already waits for: due at once, as a
         * script's request is, and in the place it already has. */
        if( same->obj_id == obj_id && same->count == count )
        {
            same->due_tick = same->asked_tick + 1u;
            same->wait_ms = 0;
            /* Parked on the release clock until an older pause ends: that
             * wait holds the due it parked with, not this one. Woken, it
             * finds its new due come and goes in the next release. */
            if( same->task.parked && same->task.park.clock == &ground_state(app)->clock )
                ToriRS_TaskWake(&same->task, TORIRS_WAKE_CYCLE);
            return;
        }
        /* The cell wants something else now: the older load is not worth a
         * read. */
        RS_Pending_CancelKey(app->pending, APP_TASKS_GROUND_WIDGET_ICON, key);
    }
    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_WidgetIcon_VTable;
    strncpy(task->task.name, "WidgetIcon", sizeof(task->task.name) - 1);
    PT_INIT(&task->pt);
    task->app = app;
    task->component_id = component_id;
    task->obj_id = obj_id;
    task->count = count;
    task->asked_tick = ground_state(app)->clock.now;
    task->due_tick = task->asked_tick + 1u;
    App_ParallelTaskAdd(app, &task->task, &task->par, APP_TASKS_GROUND_WIDGET_ICON, key, -1);
}

int
app_widget_icon_pending(
    struct App* app,
    int component_id,
    int* out_obj_id)
{
    struct RS_ParallelTask* pending;

    assert(app);
    assert(app->pending);
    assert(out_obj_id);
    pending = RS_Pending_Find(
        app->pending, APP_TASKS_GROUND_WIDGET_ICON, widget_icon_key(app, component_id));
    if( !pending )
        return 0;
    *out_obj_id = widget_icon_task(pending)->obj_id;
    return 1;
}

int
app_widget_icons_pending(struct App* app)
{
    assert(app);
    assert(app->pending);
    return RS_Pending_Count(app->pending, APP_TASKS_GROUND_WIDGET_ICON);
}
