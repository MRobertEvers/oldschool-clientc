/*
 * PARALLEL TASKS: LOC CHANGES, LOC ANIMATIONS, MERGES, ZONE RESETS, ZONE
 * PACKETS FOR UNBUILT VIEWS.
 *
 * The loc change and the merge write game state, so they are not this
 * client's alone: their run functions are game/rs_loc_tasks.c's, the one copy
 * both clients run on their own parallel queue (the headless client's is
 * scriptrun/scriptrun_tasks.c). This unit is this client's host for them
 * (loc_host: its queue, index, cache records, varps, views' scenes, and
 * loc_place, which also builds the scene element), and keeps the two tasks
 * that are this client's own: the zone packets held for an unbuilt view and
 * the loc sequence bind.
 *
 * Four tasks, each the one shape (game/rs_parallel_task.h,
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
#include "game/rs_loc_tasks.h"
#include "game/rs_server_state.h"
#include "mem/torirs_mem.h"
#include "net/rev/revpacket.h"
#include "torirs_env.h"

enum AppLocTaskKind
{
    /* After the two the clients share (game/rs_loc_tasks.h: the change and
     * the merge). */
    APP_TASK_ZONE_HELD = RS_LOC_TASKS_END,
    APP_TASK_LOC_SEQ_BIND,
    APP_TASK_LOCS_END
};
_Static_assert(APP_TASK_LOCS_END - 1 <= RS_PENDING_KINDS_LOCS_LAST, "the locs family's range");

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
    struct RS_LocChange const* c,
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
    /* RuneLite's ObjectDespawned and ObjectSpawned events (all four
     * object kinds): the scenery shows the change from here on. */
    app_plugin_loc_changed(
        app, world, c->x, c->z, c->level, c->shape, c->angle, c->loc_id, old_type, old_shape, old_angle);
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

/* ---- the loc tasks' host (game/rs_loc_tasks.h) ------------------------------ */

/* The change and the merge are the clients' shared tasks; this is this
 * client's half of them: its queue and index, its cache's records and its
 * varps, its views' scenes, and the place above. */

static uint32_t
loc_host_cycle(void* ctx)
{
    return App_ParallelCycle(ctx);
}

static unsigned
loc_host_tick(void* ctx)
{
    return RS_ServerTick_Serial(&((struct App*)ctx)->server_tick);
}

static void
loc_host_add(
    void* ctx,
    struct ToriRS_Task* task,
    struct RS_ParallelTask* par,
    int kind,
    struct RS_PendingKey key,
    int view)
{
    App_ParallelTaskAdd(ctx, task, par, kind, key, view);
}

static void
loc_host_release(
    void* ctx,
    struct RS_ParallelTask* par)
{
    App_ParallelTaskRelease(ctx, par);
}

static void
loc_host_changed(void* ctx)
{
    ((struct App*)ctx)->loc_task_serial++;
}

static uint32_t
loc_host_view_gen(
    void* ctx,
    int view)
{
    return loc_view_gen(ctx, view);
}

static struct World*
loc_host_view_world(
    void* ctx,
    int view)
{
    struct App* app = ctx;

    assert(WorldviewRegistry_IsLive(&app->worldviews, view));
    return WorldviewRegistry_Get(&app->worldviews, view)->world;
}

static int
loc_host_cache_level(
    void* ctx,
    int view,
    int x,
    int z,
    int zone_level)
{
    return World_LocCacheLevel(loc_host_view_world(ctx, view), x, z, zone_level);
}

static struct ToriRS_Location*
loc_host_location(
    void* ctx,
    int loc_id)
{
    return CacheProvider_LocationGet(((struct App*)ctx)->provider, loc_id);
}

static struct VarPManager const*
loc_host_varps(void* ctx)
{
    return &((struct App*)ctx)->varps;
}

static struct ToriRS_Trigger*
loc_host_built_trigger(
    void* ctx,
    int view)
{
    return App_ViewBuiltTrigger(ctx, view);
}

static bool
loc_host_scene_building(
    void* ctx,
    struct RS_ParallelTask const* par)
{
    return loc_scene_building(ctx, par);
}

static struct World*
loc_host_world(
    void* ctx,
    struct RS_ParallelTask const* par,
    char const* owner)
{
    return App_ParallelTaskWorld(ctx, par, owner);
}

static void
loc_host_place(
    void* ctx,
    int view,
    struct World* world,
    struct RS_LocChange const* c,
    int end_time,
    int carried,
    int carried_seq)
{
    loc_place(ctx, view, world, c, end_time, carried, carried_seq);
}

static void
loc_host_applied(
    void* ctx,
    struct Task_LocChange const* self,
    int carried,
    int carried_seq)
{
    struct App* app = ctx;
    unsigned fences;

    if( !torirs_env_loc_lane_trace() )
        return;
    fences = RS_ServerTick_Serial(&app->server_tick) - self->tick;
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

static struct RS_LocHostOps const k_loc_host_ops = {
    .cycle = loc_host_cycle,
    .tick = loc_host_tick,
    .add = loc_host_add,
    .release = loc_host_release,
    .changed = loc_host_changed,
    .view_gen = loc_host_view_gen,
    .view_world = loc_host_view_world,
    .cache_level = loc_host_cache_level,
    .location = loc_host_location,
    .varps = loc_host_varps,
    .built_trigger = loc_host_built_trigger,
    .scene_building = loc_host_scene_building,
    .world = loc_host_world,
    .place = loc_host_place,
    .applied = loc_host_applied,
};

static struct RS_LocHost
loc_host(struct App* app)
{
    struct RS_LocHost host;

    assert(app);
    host.ops = &k_loc_host_ops;
    host.ctx = app;
    host.pending = app->pending;
    host.assets = app->assets;
    host.queue = app->frame_runner.queue;
    host.clock = App_CycleClock(app);
    return host;
}

/* ---- loc changes ------------------------------------------------------------ */


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
    struct RS_LocHost const host = loc_host(app);
    struct RS_LocChange c;

    assert(ops);
    RS_LocChange_Fill(&c, scene_x, scene_z, level, loc_id, shape, angle, op_flags, ops);
    (void)RS_LocChange_Add(
        &host, WorldviewRegistry_Active(&app->worldviews), &c, NULL,
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
    struct RS_LocHost const host = loc_host(app);
    int const view = WorldviewRegistry_Active(&app->worldviews);
    int const layer = World_LocShapeToLayer(shape);
    struct RS_LocChange c;

    assert(ops);
    /* A change already pending for the tile and layer is newer than what the
     * scene shows, and it resolves its own transform chain when it loads:
     * re-placing the standing loc over it would undo the server's change. */
    if( RS_LocChange_Pending(&host, view, scene_x, scene_z, level, layer) ||
        RS_LocMerge_Pending(&host, view, scene_x, scene_z, level, layer) )
        return;
    RS_LocChange_Fill(&c, scene_x, scene_z, level, loc_id, shape, angle, op_flags, ops);
    (void)RS_LocChange_Add(&host, view, &c, NULL, RS_ServerTick_Serial(&app->server_tick), 0);
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
    struct RS_LocHost host;

    assert(app);
    assert(app->pending);
    if( !WorldviewRegistry_IsLive(&app->worldviews, view) )
        return NULL;
    host = loc_host(app);
    return RS_LocChange_Pending(&host, view, scene_x, scene_z, level, layer);
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
    {
        struct RS_LocHost const host = loc_host(app);

        n = RS_LocChange_OfView(&host, WORLDVIEW_ROOT, tasks, WORLD_LOC_CHANGE_MAX);
    }
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

/* ---- loc merges --------------------------------------------------------- */

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
    struct RS_LocHost host;

    assert(app);
    /* No world yet is a real state (fa5680f77). A zone packet reaches here
     * only for a built view (App_ZonePacketHold holds the rest). */
    if( !app->world )
        return;
    assert(App_ActiveWorldview(app)->world->load_complete);

    /* Countdown: hide after start_cycle cycles, restore after end_cycle --
     * Client-TS locChangeCreate(..., t1+1, t2+1), on the client's cycle for
     * whichever view it was sent to (game/rs_loc_tasks.h, the merge). */
    host = loc_host(app);
    RS_LocMerge_Add(
        &host, WorldviewRegistry_Active(&app->worldviews), scene_x, scene_z, level, shape,
        start_cycle, end_cycle);

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
App_LocChangesZoneReset(
    struct App* app,
    int view,
    int base_x,
    int base_z,
    int zone_level)
{
    struct RS_LocHost host;

    assert(app);
    assert(WorldviewRegistry_IsLive(&app->worldviews, view));
    assert(WorldviewRegistry_Get(&app->worldviews, view)->world);
    host = loc_host(app);
    return RS_LocTasks_ZoneReset(
        &host, view, base_x, base_z, zone_level, RS_ServerTick_Serial(&app->server_tick));
}

int
App_LocChangesCancelZone(
    struct App* app,
    int view,
    int base_x,
    int base_z,
    int zone_level)
{
    struct RS_LocHost host;

    assert(app);
    assert(WorldviewRegistry_IsLive(&app->worldviews, view));
    assert(WorldviewRegistry_Get(&app->worldviews, view)->world);
    host = loc_host(app);
    return RS_LocTasks_CancelZone(&host, view, base_x, base_z, zone_level);
}

void
App_LocChangesCancelAll(struct App* app)
{
    struct RS_LocHost const host = loc_host(app);

    RS_LocTasks_CancelAll(&host);
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
    struct RS_LocHost const host = loc_host(app);
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
    n = RS_LocChange_OfView(&host, view, older, WORLD_LOC_CHANGE_MAX);
    assert(n <= WORLD_LOC_CHANGE_MAX);
    for( int i = 0; i < n; i++ )
    {
        if( older[i]->par.scene_serial == world->load_seq )
            continue;
        (void)RS_Pending_CancelKey(app->pending, RS_LOC_TASK_CHANGE, older[i]->par.key);
        older[kept++] = older[i];
    }
    for( int i = 0; i < count; i++ )
    {
        struct RS_LocChange c;

        RS_LocChange_Fill(
            &c, standing[i].x, standing[i].z, standing[i].level, standing[i].new_type,
            standing[i].new_shape, standing[i].new_angle, standing[i].op_flags, standing[i].ops);
        (void)RS_LocChange_Add(&host, view, &c, NULL, tick, 0);
    }
    for( int i = 0; i < kept; i++ )
    {
        struct Task_LocChange const* old = older[i];
        struct RS_LocChange c = old->change;

        c.x = old->abs_x - world->_base_tile_x;
        c.z = old->abs_z - world->_base_tile_z;
        if( c.x < 0 || c.z < 0 || c.x >= world->_scene_size || c.z >= world->_scene_size )
            continue;
        {
            struct Task_LocChange* carried =
                RS_LocChange_Add(&host, view, &c, &old->carry, old->tick, old->drive_row);
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
