/*
 * PARALLEL TASKS: LOC CHANGES, LOC ANIMATIONS, MERGES, ZONE RESETS, ZONE
 * PACKETS FOR UNBUILT VIEWS.
 *
 * Three tasks, each the one shape (game/rs_parallel_task.h,
 * app/app_parallel_task.h): it embeds struct RS_ParallelTask, carries its own
 * data copied from the packet, is added with App_ParallelTaskAdd to the
 * parallel queue (app->frame_runner) as an ordinary task, and its run
 * function reads top to bottom -- await, cancelled?, still wanted?, apply --
 * with no `switch` in it. Its free calls App_ParallelTaskRelease.
 *
 * TASK_LOCCHANGE (LOC_ADD_CHANGE / LOC_DEL, a varp re-place, the editor, the
 * re-place after a rebuild). ONE pending change per view, tile, level and
 * layer: a later change to the same key cancels the pending one and takes its
 * place, as the deob keeps one pending spawn per key and overwrites it
 * (Statics.method5669). There is no global order: one slow door no longer
 * holds every other loc change in the scene. Its apply writes scenery,
 * collision and the scene element together once the loc's config chain, its
 * models and its sequence are loaded (the reference's changeLocAvailable
 * gate, deob locChangeDoQueue) -- the timing a player observes. A delete
 * loads nothing and applies on the task's first pass.
 *
 * A LOC_ANIM for a tile with a change pending is a field on that change (the
 * deob's class69.field666): App_WorldSceneryAnim (app_world_apply.c) finds
 * the change through the pending index and hands it the sequence under the
 * deob's conditions (app_loc_change_carry / _take); otherwise it animates the
 * standing loc at once.
 *
 * TASK_LOCMERGE (LOC_MERGE): a hide with a time to apply and a time to
 * restore, counted on the client's cycle for the view it was sent to. Every
 * view's merge fires, a deck's included (the countdown records used to be
 * ticked for the root world alone).
 *
 * TASK_LOCSEQBIND: a sequence the rebuilt scene's locs play that it does not
 * hold yet. The rebuild waits for its map squares and loc models (every
 * packet behind it addresses the new scene), not for these frame archives,
 * which are only drawn: the locs are built still, and this task binds the
 * sequence to them when it arrives.
 *
 * TASK_ZONEHELD: a zone sub-packet for a view that is not built yet, with its
 * zone base, plane and view as of arrival. It waits for THAT view (not the
 * root's) and for the packets held for the view before it, then applies --
 * a FULL_FOLLOWS reset among them, in its place. Cancelled at logout like
 * every pending task.
 *
 * A REBUILD decides what is carried over (App_WorldViewLoadFinish): the
 * changes that had landed are re-placed from the world's records
 * (World_LocChangesTakeStanding), and a change still loading carries its
 * absolute tile into the new scene; merges are dropped, as the deob unlinks
 * every pending spawn that is not permanent (client.method2278). A change
 * whose loads finish while its scene is mid-rebuild waits for the rebuild
 * instead of being dropped, so the carry-over finds it.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; nothing outside the layer may include app/app_internal.h.
 */

#include "app/app_internal.h"
#include "app/app_gameproto_exec.h"
#include "game/rs_server_state.h"
#include "mem/torirs_mem.h"
#include "net/rev/revpacket.h"
#include "torirs_env.h"

enum AppLocTaskKind
{
    APP_TASK_LOC_CHANGE = RS_PENDING_KINDS_LOCS_FIRST,
    APP_TASK_LOC_MERGE,
    APP_TASK_ZONE_HELD,
    APP_TASK_LOC_SEQ_BIND,
    APP_TASK_LOCS_END
};
_Static_assert(APP_TASK_LOCS_END - 1 <= RS_PENDING_KINDS_LOCS_LAST, "the locs family's range");

/* How deep a multiloc chain a change follows before it stops (the lane's
 * bound, kept). */
enum
{
    APP_LOC_RESOLVE_DEPTH = 16
};

/* ---- keys ---------------------------------------------------------------- */

/* A view's generation in a key: the server's world-entity id it was spawned
 * under, which changes when its slot is reused for another. */
static uint32_t
loc_view_gen(
    struct App* app,
    int view)
{
    return (uint32_t)WorldviewRegistry_Get(&app->worldviews, view)->wire_id;
}

static struct RS_PendingKey
loc_key(
    struct App* app,
    int view,
    int scene_x,
    int scene_z,
    int level,
    int shape)
{
    return RS_PendingKey_View(
        view, scene_x, scene_z, level, World_LocShapeToLayer(shape), loc_view_gen(app, view));
}

static struct RS_PendingKey
zone_key(
    struct App* app,
    int view)
{
    return RS_PendingKey_View(view, 0, 0, 0, 0, loc_view_gen(app, view));
}

/* Is the view's scene being rebuilt right now -- the scene this task was made
 * in, not yet complete again? Then the rebuild's finish decides about it. */
static bool
loc_scene_building(
    struct App* app,
    struct RS_ParallelTask const* par)
{
    struct Worldview* wv;

    if( !WorldviewRegistry_IsLive(&app->worldviews, par->view) )
        return false;
    wv = WorldviewRegistry_Get(&app->worldviews, par->view);
    if( (uint32_t)wv->wire_id != par->key.gen )
        return false;
    return wv->world && !wv->world->load_complete && wv->world->load_seq == par->scene_serial;
}

/* ---- the apply ------------------------------------------------------------ */

/*
 * Place `c` (or remove the loc in its layer, for a delete) in `world` now:
 * the record a rebuild and a zone reset read, scenery, collision and the
 * scene element (WorldBuilder_ApplyLocChange), the placement's own menu, a
 * sequence a LOC_ANIM handed it, and its LOC_ADD trigger. The one place a
 * loc change touches the scene. `end_time` >= 0 marks the record as a
 * merge's (not permanent: a rebuild drops it).
 */
static void
loc_place(
    struct App* app,
    int view,
    struct World* world,
    struct AppLocChange const* c,
    int end_time,
    int carried,
    int carried_seq)
{
    struct Worldview* wv;
    int old_type = -1;
    int old_angle = 0;
    int old_shape = -1;
    int idx;

    assert(app);
    assert(world);
    assert(c);
    wv = WorldviewRegistry_Get(&app->worldviews, view);
    assert(wv->builder);
    idx = World_SceneryFindAt(world, c->x, c->z, c->level, c->shape);
    if( idx >= 0 )
    {
        struct WorldEntity_Scenery* old = World_EntityPoolGet(&world->entities.scenery, idx);
        if( old )
        {
            old_type = old->loc_id;
            old_angle = old->angle;
            old_shape = old->shape;
        }
    }
    World_LocChangePushOps(
        world,
        c->level,
        World_LocShapeToLayer(c->shape),
        c->x,
        c->z,
        old_type,
        old_angle,
        old_shape,
        c->loc_id,
        c->angle,
        c->shape,
        (int)world->cycle,
        end_time,
        c->op_flags,
        c->ops);
    WorldBuilder_ApplyLocChange(wv->builder, c->x, c->z, c->level, c->loc_id, c->shape, c->angle);
    if( c->loc_id >= 0 )
    {
        int placed_idx;
        struct WorldEntity_Scenery* placed;

        /*
         * The placement's own menu, over the loctype's. After the spawn: the
         * scenery entity is created by ApplyLocChange with the loctype's
         * actions copied in, so this is the first point where both the entity
         * and the override exist (deob class108: the scene loc carries the
         * mask and labels, the menu builder lets the placement win).
         */
        placed_idx = World_SceneryFindAt(world, c->x, c->z, c->level, c->shape);
        placed = placed_idx >= 0 ? World_EntityPoolGet(&world->entities.scenery, placed_idx) : NULL;
        if( !placed )
        {
            /* The spawn was refused (unknown loc, off-scene), or the loc is a
             * multiloc whose varbit picks -1 now and ApplyLocChange kept the
             * placement as a hidden record: the menu rides the record and
             * comes back with the re-placement (deob DynamicObject). */
            (void)WorldBuilder_HiddenLocSetOps(
                wv->builder, c->x, c->z, c->level, c->shape, c->op_flags, c->ops);
        }
        else
        {
            struct WorldEntity_SceneryInfo probe = *placed->info;
            char const* replacements[5];
            bool has_action = false;

            for( int i = 0; i < 5; i++ )
                replacements[i] = c->ops[i];
            placed->placement_op_mask = (uint8_t)c->op_flags;
            placed->placement_op_overrides = WorldEntity_SceneryApplyPlacementOps(
                &probe, (unsigned)c->op_flags, replacements, &has_action);
            placed->info = World_SceneryInfoIntern(world, &probe);
            if( has_action )
                placed->interactive = 1;
        }
        /* A LOC_ANIM the pending change carried: the placed loc starts on it
         * instead of on its `anim=` (deob Statics.method6346 -> method13799
         * -> method8559, whose seq argument wins when it is >= 0). */
        if( carried )
            (void)app_world_scenery_anim_apply(
                app, world, c->x, c->z, c->level, c->shape, carried_seq);
        /*
         * The cache's "a loc was placed" script, for a loc that arrived AFTER
         * the scene was built (a Dwarf multicannon's hud, clientscript 6672,
         * is bound to exactly this). After the ops: the script reads the
         * finished entity's type and coord.
         */
        placed_idx = World_SceneryFindAt(world, c->x, c->z, c->level, c->shape);
        placed = placed_idx >= 0 ? World_EntityPoolGet(&world->entities.scenery, placed_idx) : NULL;
        if( placed )
            app_client_trigger_loc(app, world, placed, RS_TRIGGER_LOC_ADD);
    }
    app_sync_textures(app);
    RedrawRequest_Raise(&app->redraw_request);
}

/* ---- TASK_LOCCHANGE ------------------------------------------------------- */

static int
Task_LocChange_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io);
static void
Task_LocChange_Free(struct ToriRS_Task* base);

static struct ToriRS_TaskVTable Task_LocChange_VTable = {
    .run = Task_LocChange_Run,
    .free = Task_LocChange_Free,
};

/* One link of the chain: the record `self->resolved_id` is here (or failed).
 * Note what the change's own record says, and step to the child the varps
 * pick. Returns false when the chain ends here. Not a protothread. */
static bool
loc_change_resolve_step(struct Task_LocChange* self)
{
    struct App* app = self->app;
    struct ToriRS_Location* cfg = CacheProvider_LocationGet(app->provider, self->resolved_id);
    int next;

    if( !cfg )
    {
        self->resolved_id = -1;
        return false;
    }
    if( self->resolve_depth == 0 )
    {
        self->base_seq = cfg->seq_id;
        /* The record the change NAMES, not its transform child: the deob
         * reads class393.field4940 off the pending spawn's own id
         * (method6184(class69.field663)). */
        self->defers_anim = cfg->defer_anim_change;
    }
    if( cfg->transform_count <= 0 || !cfg->transforms )
        return false;
    next = VarPManager_ResolveTransform(
        &app->varps, cfg->transforms, cfg->transform_count, cfg->transform_varbit,
        cfg->transform_varp);
    if( next == self->resolved_id )
        return false;
    self->resolved_id = next;
    return true;
}

/* Every model the resolved child names and its sequence, counted on the
 * header's join TOGETHER: independent reads with one consumer, which awaited
 * one after another were a round trip each (an open-door variant is commonly
 * absent from the map build's preload). Not a protothread. */
static void
loc_change_want_models(struct Task_LocChange* self)
{
    struct App* app = self->app;
    struct ToriRS_Location* cfg =
        self->resolved_id >= 0 ? CacheProvider_LocationGet(app->provider, self->resolved_id) : NULL;
    int entries = 0;

    assert(self->par.join == 0);
    if( cfg && cfg->models && cfg->lengths )
        entries = cfg->shapes ? cfg->shapes_and_model_count : 1;
    for( int i = 0; i < entries; i++ )
        for( int j = 0; j < cfg->lengths[i]; j++ )
            if( cfg->models[i][j] >= 0 )
                (void)Assets_Await(
                    app->assets, AssetKey_Make(ASSET_KIND_MODEL, cfg->models[i][j], 0),
                    &self->par.join);
    self->seq_id = cfg && cfg->seq_id >= 0 ? cfg->seq_id : self->base_seq;
    if( self->seq_id >= 0 )
        (void)Assets_Await(
            app->assets, AssetKey_Make(ASSET_KIND_SEQUENCE, self->seq_id, 0), &self->par.join);
}

static int
Task_LocChange_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_LocChange* self = (struct Task_LocChange*)base;
    struct App* app = self->app;
    struct World* world;
    int carried_seq = -1;
    int carried;

    (void)io;
    PT_BEGIN(&self->pt);

    /* 1. Wait for what it places: the config chain, one record at a time
     * (each reveals the next), then the child's models and sequence. A
     * delete places nothing and waits for nothing. */
    if( self->change.loc_id >= 0 )
    {
        self->resolved_id = self->change.loc_id;
        self->base_seq = -1;
        for( self->resolve_depth = 0;
             self->resolved_id >= 0 && self->resolve_depth < APP_LOC_RESOLVE_DEPTH;
             self->resolve_depth++ )
        {
            PT_PARALLEL_AWAIT_ASSET(
                app->assets, AssetKey_Make(ASSET_KIND_CONFIG, self->resolved_id, ASSET_CONFIG_LOC));
            if( RS_ParallelTask_Cancelled(&self->par) )
                PT_EXIT(&self->pt);
            if( !loc_change_resolve_step(self) )
                break;
        }
        loc_change_want_models(self);
        PT_PARALLEL_AWAIT_ASSETS(app->assets);
    }

    /* 2. Replaced by a newer change for this key, or its scope went. */
    if( RS_ParallelTask_Cancelled(&self->par) )
        PT_EXIT(&self->pt);

    /* 3. Still wanted? Its scene mid-rebuild is not an answer yet: the
     * rebuild of its view is waited out, by name, and its finish carries
     * the change over (or drops it) by cancelling it. */
    PT_APP_AWAIT_VIEW_REBUILT(app, !loc_scene_building(app, &self->par));
    if( RS_ParallelTask_Cancelled(&self->par) )
        PT_EXIT(&self->pt);
    world = App_ParallelTaskWorld(app, &self->par, "loc change");
    if( !world )
        PT_EXIT(&self->pt);
    /* A loc whose own record the cache could not serve cannot be placed (the
     * builder resolves it from that record): the reference's gate would hold
     * it forever; it is dropped, said. */
    if( self->change.loc_id >= 0 && !CacheProvider_LocationGet(app->provider, self->change.loc_id) )
    {
        TORIRS_LOG(
            "loc change: loc %d at %d,%d,l%d dropped, its config could not be loaded\n",
            self->change.loc_id, self->change.x, self->change.z, self->change.level);
        PT_EXIT(&self->pt);
    }

    /* 4. Apply. */
    carried = self->change.loc_id >= 0 && app_loc_change_carried(self, &carried_seq);
    loc_place(app, self->par.view, world, &self->change, -1, carried, carried_seq);
    if( torirs_env_loc_lane_trace() )
    {
        unsigned const fences = RS_ServerTick_Serial(&app->server_tick) - self->tick;

        TORIRS_REPORT(
            "loc_change: apply loc=%d shape=%d at %d,%d,l%d view=%d fences=%u cycles=%u seq=%d%s%s\n",
            self->change.loc_id,
            self->change.shape,
            self->change.x,
            self->change.z,
            self->change.level,
            self->par.view,
            fences,
            App_ParallelTaskLate(app, &self->par),
            carried ? carried_seq : -1,
            self->carry.take_set ? " (taken)" : "",
            fences >= 2 ? " LATE" : "");
    }

    PT_END(&self->pt);
}

static void
Task_LocChange_Free(struct ToriRS_Task* base)
{
    struct Task_LocChange* self = (struct Task_LocChange*)base;
    struct App* app = self->app;

    App_ParallelTaskRelease(app, &self->par);
    app->loc_task_serial++;
    ToriMem_Free(ToriMem_Process(), self);
}

/* Cancel what is pending for the key (a change or a merge): a later packet
 * for a tile and layer overwrites the deob's one pending spawn there. */
static void
loc_key_cancel(
    struct App* app,
    struct RS_PendingKey key)
{
    if( RS_Pending_CancelKey(app->pending, APP_TASK_LOC_CHANGE, key) +
            RS_Pending_CancelKey(app->pending, APP_TASK_LOC_MERGE, key) >
        0 )
        app->loc_task_serial++;
}

/* Add a change for `view` as `c` says, carrying `carry` (NULL: none) and
 * read on server tick `tick`. Replaces whatever is pending for its key. */
static struct Task_LocChange*
loc_change_add(
    struct App* app,
    int view,
    struct AppLocChange const* c,
    struct AppLocCarry const* carry,
    unsigned tick,
    int drive_row)
{
    struct Task_LocChange* task;
    struct World const* world;
    struct RS_PendingKey key;

    assert(app);
    assert(c);
    assert(WorldviewRegistry_IsLive(&app->worldviews, view));
    world = WorldviewRegistry_Get(&app->worldviews, view)->world;
    key = loc_key(app, view, c->x, c->z, c->level, c->shape);
    loc_key_cancel(app, key);
    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_LocChange_VTable;
    strncpy(task->task.name, "LocChange", sizeof(task->task.name) - 1);
    PT_INIT(&task->pt);
    task->app = app;
    task->change = *c;
    task->abs_x = (world ? world->_base_tile_x : 0) + c->x;
    task->abs_z = (world ? world->_base_tile_z : 0) + c->z;
    task->tick = tick;
    task->drive_row = drive_row;
    task->resolved_id = -1;
    task->base_seq = -1;
    task->seq_id = -1;
    if( carry )
        task->carry = *carry;
    App_ParallelTaskAdd(app, &task->task, &task->par, APP_TASK_LOC_CHANGE, key, view);
    app->loc_task_serial++;
    return task;
}

static void
loc_change_fill(
    struct AppLocChange* c,
    int scene_x,
    int scene_z,
    int level,
    int loc_id,
    int shape,
    int angle,
    int op_flags,
    const char ops[5][32])
{
    memset(c, 0, sizeof(*c));
    c->x = scene_x;
    c->z = scene_z;
    c->level = level;
    c->loc_id = loc_id;
    c->shape = shape;
    c->angle = angle;
    c->op_flags = op_flags;
    memcpy(c->ops, ops, sizeof(c->ops));
}

/* Zone LOC_ADD_CHANGE / LOC_DEL (reference locChangeCreate +
 * locChangeDoQueue), the loc editor and the map editor: a change in the view
 * the packet cursor addresses. loc_id < 0 = delete. */
void
App_WorldLocChange(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int loc_id,
    int shape,
    int angle)
{
    static const char none[5][32] = { { 0 }, { 0 }, { 0 }, { 0 }, { 0 } };

    /* All five bits: the loctype's menu, unchanged. Not a "no data" stand-in
     * -- it is the real menu of every loc placed by anything but a door
     * script. */
    App_WorldLocChangeOps(app, scene_x, scene_z, level, loc_id, shape, angle, 0x1f, none);
}

void
App_WorldLocChangeOps(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int loc_id,
    int shape,
    int angle,
    int op_flags,
    const char ops[5][32])
{
    struct AppLocChange c;

    assert(app);
    assert(ops);
    loc_change_fill(&c, scene_x, scene_z, level, loc_id, shape, angle, op_flags, ops);
    (void)loc_change_add(
        app, WorldviewRegistry_Active(&app->worldviews), &c, NULL,
        RS_ServerTick_Serial(&app->server_tick), 1);
}

void
app_world_loc_refresh_ops(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int loc_id,
    int shape,
    int angle,
    int op_flags,
    const char ops[5][32])
{
    int const view = WorldviewRegistry_Active(&app->worldviews);
    struct RS_PendingKey key;
    struct AppLocChange c;

    assert(app);
    assert(ops);
    /* A change already pending for the tile and layer is newer than what the
     * scene shows, and it resolves its own transform chain when it loads:
     * re-placing the standing loc over it would undo the server's change. */
    key = loc_key(app, view, scene_x, scene_z, level, shape);
    if( RS_Pending_Find(app->pending, APP_TASK_LOC_CHANGE, key) ||
        RS_Pending_Find(app->pending, APP_TASK_LOC_MERGE, key) )
        return;
    loc_change_fill(&c, scene_x, scene_z, level, loc_id, shape, angle, op_flags, ops);
    (void)loc_change_add(app, view, &c, NULL, RS_ServerTick_Serial(&app->server_tick), 0);
}

struct Task_LocChange*
app_loc_change_pending(
    struct App* app,
    int view,
    int scene_x,
    int scene_z,
    int level,
    int layer)
{
    struct RS_ParallelTask* par;
    struct Task_LocChange* task;

    assert(app);
    assert(app->pending);
    if( !WorldviewRegistry_IsLive(&app->worldviews, view) )
        return NULL;
    par = RS_Pending_Find(
        app->pending,
        APP_TASK_LOC_CHANGE,
        RS_PendingKey_View(view, scene_x, scene_z, level, layer, loc_view_gen(app, view)));
    if( !par )
        return NULL;
    task = (struct Task_LocChange*)((char*)par - offsetof(struct Task_LocChange, par));
    assert(task->task.vtable == &Task_LocChange_VTable);
    return task;
}

void
app_loc_change_carry(
    struct Task_LocChange* pending,
    int seq_id,
    int stood)
{
    assert(pending);
    /* Deob, the LOC_ANIM decode: a pending spawn whose loctype defers takes
     * every sequence (class69.field666 = seq after method1508); one that does
     * not takes it only when method1508 found nothing to attach it to. */
    pending->carry.any_seq = seq_id;
    pending->carry.any_set = 1;
    if( !stood )
    {
        pending->carry.unfound_seq = seq_id;
        pending->carry.unfound_set = 1;
    }
}

void
app_loc_change_take(
    struct Task_LocChange* landed,
    int seq_id)
{
    assert(landed);
    assert(landed->change.loc_id >= 0);
    landed->carry.take_seq = seq_id;
    landed->carry.take_set = 1;
}

int
app_loc_change_carried(
    struct Task_LocChange const* task,
    int* out_seq_id)
{
    assert(task);
    assert(out_seq_id);
    if( task->carry.take_set )
    {
        *out_seq_id = task->carry.take_seq;
        return 1;
    }
    if( task->defers_anim ? !task->carry.any_set : !task->carry.unfound_set )
        return 0;
    *out_seq_id = task->defers_anim ? task->carry.any_seq : task->carry.unfound_seq;
    return 1;
}

/* Every pending change of `view` (cancelled ones skipped), in arrival order:
 * the parallel queue holds them in the order they were added. Writes up to
 * `cap`; returns how many there are. */
static int
loc_changes_of_view(
    struct App* app,
    int view,
    struct Task_LocChange** out,
    int cap)
{
    int n = 0;

    assert(app);
    if( !app->frame_runner.queue )
        return 0;
    for( struct ToriRS_Task* t = app->frame_runner.queue->head; t; t = t->next )
    {
        struct Task_LocChange* task;

        if( t->vtable != &Task_LocChange_VTable )
            continue;
        task = (struct Task_LocChange*)t;
        if( task->par.cancelled || task->par.view != view )
            continue;
        if( n < cap )
            out[n] = task;
        n++;
    }
    return n;
}

int
App_DriveLocChangesPending(
    struct App* app,
    struct App_DriveLocChange* out,
    int cap)
{
    /* The root's pending changes: the driver's loc rows are the root's. */
    static struct Task_LocChange* tasks[WORLD_LOC_CHANGE_MAX];
    struct World* world;
    int n;
    int kept = 0;

    assert(app);
    assert(out);
    (void)cap; /* read only by the assert below */
    world = app->world;
    assert(world);
    n = loc_changes_of_view(app, WORLDVIEW_ROOT, tasks, WORLD_LOC_CHANGE_MAX);
    assert(n <= WORLD_LOC_CHANGE_MAX);
    for( int i = 0; i < n; i++ )
    {
        struct Task_LocChange const* task = tasks[i];
        struct App_DriveLocChange* row;

        /* Made in another scene: the rebuild's finish carries it over as a
         * new task in this one, or it is gone. */
        if( task->par.scene_serial != world->load_seq || !task->drive_row )
            continue;
        assert(kept < cap);
        row = &out[kept++];
        memset(row, 0, sizeof(*row));
        row->x = world->_base_tile_x + task->change.x;
        row->z = world->_base_tile_z + task->change.z;
        row->level = task->change.level;
        row->layer = World_LocShapeToLayer(task->change.shape);
        row->loc_id = task->change.loc_id;
        row->shape = task->change.shape;
        row->angle = task->change.angle;
        row->base_x = world->_base_tile_x;
        row->base_z = world->_base_tile_z;
        row->carried = task->carried;
    }
    return kept;
}

/* ---- TASK_LOCMERGE -------------------------------------------------------- */

struct Task_LocMerge
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_ParallelTask par;
    struct App* app;
    /* The tile and layer it hides (scene tile of its view, cache level). */
    int x;
    int z;
    int level;
    int shape;
    /* Client cycles (App_ParallelCycle) the hide and the restore are due. */
    uint32_t hide_at;
    uint32_t restore_at;
};

static int
Task_LocMerge_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io);
static void
Task_LocMerge_Free(struct ToriRS_Task* base);

static struct ToriRS_TaskVTable Task_LocMerge_VTable = {
    .run = Task_LocMerge_Run,
    .free = Task_LocMerge_Free,
};

/* The record for the merge's key after its hide: what the key's FIRST change
 * replaced, which the deob's countdown puts back. Not a protothread. */
static bool
loc_merge_restore_of(
    struct World const* world,
    struct Task_LocMerge const* self,
    struct AppLocChange* out)
{
    static const char none[5][32] = { { 0 }, { 0 }, { 0 }, { 0 }, { 0 } };
    int const layer = World_LocShapeToLayer(self->shape);

    for( int i = 0; i < world->loc_change_count; i++ )
    {
        struct World_LocChange const* rec = &world->loc_changes[i];

        if( rec->level != self->level || rec->layer != layer || rec->x != self->x ||
            rec->z != self->z )
            continue;
        loc_change_fill(
            out, self->x, self->z, self->level, rec->old_type,
            rec->old_shape >= 0 ? rec->old_shape : self->shape, rec->old_angle, 0x1f, none);
        return true;
    }
    return false;
}

static int
Task_LocMerge_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    static const char none[5][32] = { { 0 }, { 0 }, { 0 }, { 0 }, { 0 } };
    struct Task_LocMerge* self = (struct Task_LocMerge*)base;
    struct App* app = self->app;
    struct World* world;
    struct AppLocChange c;

    (void)io;
    PT_BEGIN(&self->pt);

    /* 1. Wait for the hide's time (Client-TS locChangeCreate's t1+1). */
    PT_APP_AWAIT_CYCLE(app, self->hide_at);
    if( RS_ParallelTask_Cancelled(&self->par) )
        PT_EXIT(&self->pt);
    world = App_ParallelTaskWorld(app, &self->par, "loc merge");
    if( !world )
        PT_EXIT(&self->pt);
    /* The hide: a delete, which loads nothing. Its record is a merge's. */
    loc_change_fill(&c, self->x, self->z, self->level, -1, self->shape, 0, 0x1f, none);
    loc_place(
        app, self->par.view, world, &c, (int)(self->restore_at - App_ParallelCycle(app)), 0, -1);

    /* 2. Wait for the restore's time (t2+1). */
    PT_APP_AWAIT_CYCLE(app, self->restore_at);
    if( RS_ParallelTask_Cancelled(&self->par) )
        PT_EXIT(&self->pt);
    world = App_ParallelTaskWorld(app, &self->par, "loc merge");
    if( !world )
        PT_EXIT(&self->pt);
    /* The restore is a change like any other: it waits for the old loc's
     * models, and replaces this task as its key's pending entry. */
    if( loc_merge_restore_of(world, self, &c) )
        (void)loc_change_add(
            app, self->par.view, &c, NULL, RS_ServerTick_Serial(&app->server_tick), 0);

    PT_END(&self->pt);
}

static void
Task_LocMerge_Free(struct ToriRS_Task* base)
{
    struct Task_LocMerge* self = (struct Task_LocMerge*)base;
    struct App* app = self->app;

    App_ParallelTaskRelease(app, &self->par);
    app->loc_task_serial++;
    ToriMem_Free(ToriMem_Process(), self);
}

void
App_WorldLocMerge(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int loc_id,
    int shape,
    int angle,
    int start_cycle,
    int end_cycle,
    int player_pid)
{
    struct WorldEntity_Player* player = NULL;
    struct Task_LocMerge* task;
    struct RS_PendingKey key;
    int const view = WorldviewRegistry_Active(&app->worldviews);
    uint32_t const now = App_ParallelCycle(app);

    assert(app);
    /* No world yet is a real state (fa5680f77). A zone packet reaches here
     * only for a built view (App_ZonePacketHold holds the rest). */
    if( !app->world )
        return;
    assert(App_ActiveWorldview(app)->world->load_complete);

    /* Countdown: hide after start_cycle cycles, restore after end_cycle --
     * Client-TS locChangeCreate(..., t1+1, t2+1), on the client's cycle for
     * whichever view it was sent to. */
    key = loc_key(app, view, scene_x, scene_z, level, shape);
    loc_key_cancel(app, key);
    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_LocMerge_VTable;
    strncpy(task->task.name, "LocMerge", sizeof(task->task.name) - 1);
    PT_INIT(&task->pt);
    task->app = app;
    task->x = scene_x;
    task->z = scene_z;
    task->level = level;
    task->shape = shape;
    task->hide_at = now + (uint32_t)start_cycle + 1u;
    task->restore_at = now + (uint32_t)end_cycle + 1u;
    App_ParallelTaskAdd(app, &task->task, &task->par, APP_TASK_LOC_MERGE, key, view);
    app->loc_task_serial++;

    /* The player the loc model rides with: players are the root world's. */
    if( player_pid == app->esync.local_pid )
        player = app_local_player(app);
    else
        player = World_PlayerGetByServerPid(app->world, player_pid);
    if( player )
    {
        player->loc_start_cycle = app->world->cycle + start_cycle;
        player->loc_stop_cycle = app->world->cycle + end_cycle;
        player->loc_merge_id = loc_id;
        player->loc_merge_shape = shape;
        player->loc_merge_angle = angle;
    }
}

/* ---- cancels ------------------------------------------------------------- */

int
App_LocChangesCancelZone(
    struct App* app,
    int view,
    int base_x,
    int base_z,
    int zone_level)
{
    struct World* world;
    uint32_t gen;
    int cancelled = 0;

    assert(app);
    assert(WorldviewRegistry_IsLive(&app->worldviews, view));
    world = WorldviewRegistry_Get(&app->worldviews, view)->world;
    assert(world);
    gen = loc_view_gen(app, view);
    for( int dz = 0; dz < 8; dz++ )
    {
        for( int dx = 0; dx < 8; dx++ )
        {
            int const x = base_x + dx;
            int const z = base_z + dz;
            /* The level a loc packet means on this tile (zone_loc_tile_at's
             * bridge climb), which is what the key was made with. */
            int const level = World_LocCacheLevel(world, x, z, zone_level);

            for( int layer = 0; layer < 4; layer++ )
            {
                struct RS_PendingKey key = RS_PendingKey_View(view, x, z, level, layer, gen);

                cancelled += RS_Pending_CancelKey(app->pending, APP_TASK_LOC_CHANGE, key);
                cancelled += RS_Pending_CancelKey(app->pending, APP_TASK_LOC_MERGE, key);
            }
        }
    }
    if( cancelled > 0 )
        app->loc_task_serial++;
    return cancelled;
}

void
App_LocChangesCancelAll(struct App* app)
{
    assert(app);
    if( RS_Pending_Cancel(app->pending, RS_PendingScope_Kind(APP_TASK_LOC_CHANGE)) +
            RS_Pending_Cancel(app->pending, RS_PendingScope_Kind(APP_TASK_LOC_MERGE)) >
        0 )
        app->loc_task_serial++;
}

/* ---- the rebuild's carry-over ---------------------------------------------- */

/*
 * After view `view`'s scene was rebuilt from the map: every loc a zone packet
 * changed is back to the map's, and a zone that stayed in view is not resent
 * (RSProx 4392: the CoX herb patch's growth stages are sent once). Re-place
 * what had landed (World_LocChangesTakeStanding), then carry each change still
 * pending from the old scene into this one by its absolute tile -- after, so a
 * pending change, the newer, replaces a standing one for its key. Deob
 * client.method2278: a permanent pending spawn is placed again in the new
 * scene, any other unlinked (a merge's task simply finds its scene gone).
 */
static void
loc_changes_carry_over(
    struct App* app,
    int view)
{
    /* ~54 KB: static, not on a web build's 64 KB stack. One App per process,
     * and nothing below re-enters this. */
    static struct World_LocChange standing[WORLD_LOC_CHANGE_MAX];
    static struct Task_LocChange* older[WORLD_LOC_CHANGE_MAX];
    struct World* world;
    unsigned const tick = RS_ServerTick_Serial(&app->server_tick);
    int count;
    int n;
    int kept = 0;

    assert(app);
    world = WorldviewRegistry_Get(&app->worldviews, view)->world;
    assert(world);
    count = World_LocChangesTakeStanding(world, standing, WORLD_LOC_CHANGE_MAX);
    /* The changes made for the old scene, taken and cancelled before any is
     * added: they were made for the old scene whether or not their tile is
     * in the new one. */
    n = loc_changes_of_view(app, view, older, WORLD_LOC_CHANGE_MAX);
    assert(n <= WORLD_LOC_CHANGE_MAX);
    for( int i = 0; i < n; i++ )
    {
        if( older[i]->par.scene_serial == world->load_seq )
            continue;
        (void)RS_Pending_CancelKey(app->pending, APP_TASK_LOC_CHANGE, older[i]->par.key);
        older[kept++] = older[i];
    }
    for( int i = 0; i < count; i++ )
    {
        struct AppLocChange c;

        loc_change_fill(
            &c, standing[i].x, standing[i].z, standing[i].level, standing[i].new_type,
            standing[i].new_shape, standing[i].new_angle, standing[i].op_flags, standing[i].ops);
        (void)loc_change_add(app, view, &c, NULL, tick, 0);
    }
    for( int i = 0; i < kept; i++ )
    {
        struct Task_LocChange const* old = older[i];
        struct AppLocChange c = old->change;

        c.x = old->abs_x - world->_base_tile_x;
        c.z = old->abs_z - world->_base_tile_z;
        if( c.x < 0 || c.z < 0 || c.x >= world->_scene_size || c.z >= world->_scene_size )
            continue;
        {
            struct Task_LocChange* carried =
                loc_change_add(app, view, &c, &old->carry, old->tick, old->drive_row);
            carried->carried = 1;
        }
    }
    app->loc_task_serial++;
}

/* ---- TASK_LOCSEQBIND ------------------------------------------------------ */

struct Task_LocSeqBind
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_ParallelTask par;
    struct App* app;
    int seq_id;
};

static int
Task_LocSeqBind_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io);
static void
Task_LocSeqBind_Free(struct ToriRS_Task* base);

static struct ToriRS_TaskVTable Task_LocSeqBind_VTable = {
    .run = Task_LocSeqBind_Run,
    .free = Task_LocSeqBind_Free,
};

static int
Task_LocSeqBind_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_LocSeqBind* self = (struct Task_LocSeqBind*)base;
    struct App* app = self->app;

    (void)io;
    PT_BEGIN(&self->pt);
    PT_PARALLEL_AWAIT_ASSET(app->assets, AssetKey_Make(ASSET_KIND_SEQUENCE, self->seq_id, 0));
    if( RS_ParallelTask_Cancelled(&self->par) || !RS_ParallelTask_AssetHere(&self->par) )
        PT_EXIT(&self->pt);
    if( !App_ParallelTaskWorld(app, &self->par, "loc sequence") )
        PT_EXIT(&self->pt);
    if( WorldBuilder_BindLocSequence(
            WorldviewRegistry_Get(&app->worldviews, self->par.view)->builder, self->seq_id) > 0 )
        RedrawRequest_Raise(&app->redraw_request);
    PT_END(&self->pt);
}

static void
Task_LocSeqBind_Free(struct ToriRS_Task* base)
{
    struct Task_LocSeqBind* self = (struct Task_LocSeqBind*)base;

    App_ParallelTaskRelease(self->app, &self->par);
    ToriMem_Free(ToriMem_Process(), self);
}

/* Want every sequence view `view`'s new scene plays and does not hold: one
 * task per sequence, binding it when it is here. */
static void
loc_sequences_want(
    struct App* app,
    int view)
{
    static int missing[4096];
    struct WorldBuilder* builder = WorldviewRegistry_Get(&app->worldviews, view)->builder;
    int n;

    assert(builder);
    n = WorldBuilder_LocSequencesMissing(builder, missing, (int)(sizeof(missing) / sizeof(missing[0])));
    for( int i = 0; i < n; i++ )
    {
        struct RS_PendingKey const key = RS_PendingKey_Id(missing[i], loc_view_gen(app, view));
        struct Task_LocSeqBind* task;

        if( RS_Pending_Find(app->pending, APP_TASK_LOC_SEQ_BIND, key) )
            continue;
        task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
        assert(task);
        task->task.vtable = &Task_LocSeqBind_VTable;
        strncpy(task->task.name, "LocSeqBind", sizeof(task->task.name) - 1);
        PT_INIT(&task->pt);
        task->app = app;
        task->seq_id = missing[i];
        App_ParallelTaskAdd(app, &task->task, &task->par, APP_TASK_LOC_SEQ_BIND, key, view);
    }
}

void
App_WorldViewLoadFinish(
    struct App* app,
    int view,
    int server_driven)
{
    struct World* world;

    assert(app);
    assert(WorldviewRegistry_IsLive(&app->worldviews, view));
    world = WorldviewRegistry_Get(&app->worldviews, view)->world;
    assert(world);
    assert(world->load_complete);
    /* Every loc in the new scene gets its LOC_ADD trigger. */
    app_client_triggers_world_loaded(app, world);
    /* Ground items still without a model and with no task waiting for one
     * (their loads gave up): asked for again, once per rebuild. */
    app_ground_stacks_sweep(app, world, view);
    /* The sequences its locs play that the build did not wait for. */
    loc_sequences_want(app, view);
    /* Server-driven only: an offline or editor load builds from text that
     * already holds its edits. */
    if( server_driven )
        loc_changes_carry_over(app, view);
    /* The root's finish syncs its textures in its own tail. */
    if( view != WORLDVIEW_ROOT )
    {
        app_sync_textures(app);
        RedrawRequest_Raise(&app->redraw_request);
    }
}

/* ---- TASK_ZONEHELD -------------------------------------------------------- */

struct Task_ZoneHeld
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_ParallelTask par;
    struct App* app;
    /* The packet, and where it applies as of its arrival. */
    struct PktZoneSubPacket pkt;
    int base_x;
    int base_z;
    int level;
};

static int
Task_ZoneHeld_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io);
static void
Task_ZoneHeld_Free(struct ToriRS_Task* base);

static struct ToriRS_TaskVTable Task_ZoneHeld_VTable = {
    .run = Task_ZoneHeld_Run,
    .free = Task_ZoneHeld_Free,
};

/* The held packet's view is gone (despawned, or its slot reused). */
static bool
zone_view_gone(
    struct App* app,
    struct RS_ParallelTask const* par)
{
    return !WorldviewRegistry_IsLive(&app->worldviews, par->view) ||
           loc_view_gen(app, par->view) != par->key.gen;
}

static bool
zone_view_built(
    struct App* app,
    int view)
{
    struct World const* world = WorldviewRegistry_Get(&app->worldviews, view)->world;

    return world && world->load_complete;
}

static int
Task_ZoneHeld_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_ZoneHeld* self = (struct Task_ZoneHeld*)base;
    struct App* app = self->app;

    (void)io;
    PT_BEGIN(&self->pt);

    /* 1. Wait for ITS view to be built -- the rebuild of its view, by name:
     * none under way and one done (or the view gone) -- and for the packets
     * held for it before this one. */
    PT_APP_AWAIT_VIEW_REBUILT(
        app, zone_view_gone(app, &self->par) || zone_view_built(app, self->par.view));
    PT_PARALLEL_AWAIT_TURN(app->pending);

    /* 2, 3. Cancelled (a logout, a replay's reset), or its view is gone. */
    if( RS_ParallelTask_Cancelled(&self->par) || zone_view_gone(app, &self->par) )
        PT_EXIT(&self->pt);

    /* 4. Apply, under the cursor and zone base as of arrival. */
    {
        struct RS_ServerState state;

        App_ServerStateBind(app, &state);
        App_GameProto_ApplyHeldZoneOp(
            app, &state, &self->pkt, self->base_x, self->base_z, self->level, self->par.view);
        RedrawRequest_Raise(&app->redraw_request);
    }

    PT_END(&self->pt);
}

static void
Task_ZoneHeld_Free(struct ToriRS_Task* base)
{
    struct Task_ZoneHeld* self = (struct Task_ZoneHeld*)base;

    App_ParallelTaskRelease(self->app, &self->par);
    ToriMem_Free(ToriMem_Process(), self);
}

void
App_ZonePacketHold(
    struct App* app,
    struct PktZoneSubPacket const* pkt,
    int base_x,
    int base_z,
    int level,
    int view)
{
    struct Task_ZoneHeld* task;

    assert(app);
    assert(pkt);
    /* The cursor names a live view (SET_ACTIVE_WORLD refuses the rest). */
    assert(WorldviewRegistry_IsLive(&app->worldviews, view));
    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_ZoneHeld_VTable;
    strncpy(task->task.name, "ZoneHeld", sizeof(task->task.name) - 1);
    PT_INIT(&task->pt);
    task->app = app;
    task->pkt = *pkt;
    task->base_x = base_x;
    task->base_z = base_z;
    task->level = level;
    App_ParallelTaskAdd(app, &task->task, &task->par, APP_TASK_ZONE_HELD, zone_key(app, view), view);
}

bool
App_ZonePacketsHeld(
    struct App* app,
    int view)
{
    assert(app);
    if( !WorldviewRegistry_IsLive(&app->worldviews, view) )
        return false;
    return RS_Pending_Find(app->pending, APP_TASK_ZONE_HELD, zone_key(app, view)) != NULL;
}

void
App_ZonePacketsDiscard(struct App* app)
{
    assert(app);
    (void)RS_Pending_Cancel(app->pending, RS_PendingScope_Kind(APP_TASK_ZONE_HELD));
}
