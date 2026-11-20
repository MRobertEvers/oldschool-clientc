/*
 * The loc tasks both clients run (game/rs_loc_tasks.h): a loc change and a
 * loc merge, each the one shape of a parallel task (game/rs_parallel_task.h)
 * -- it carries its own data, its run function reads top to bottom, await,
 * cancelled?, still wanted?, apply, with no `switch` in it, and its free
 * makes the one release call.
 *
 * App-free: the client is reached only through struct RS_LocHost.
 */

#include "game/rs_loc_tasks.h"

#include "engine/torirs_types.h"
#include "log/torirs_log.h"
#include "mem/torirs_mem.h"
#include "varp/varp_manager.h"
#include "world/world.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

static void
host_changed(struct RS_LocHost const* host)
{
    if( host->ops->changed )
        host->ops->changed(host->ctx);
}


/* ---- keys ----------------------------------------------------------------- */

struct RS_PendingKey
RS_LocTasks_Key(
    struct RS_LocHost const* host,
    int view,
    int scene_x,
    int scene_z,
    int level,
    int shape)
{
    assert(host);
    return RS_PendingKey_View(
        view, scene_x, scene_z, level, World_LocShapeToLayer(shape),
        host->ops->view_gen(host->ctx, view));
}

int
RS_LocTasks_CancelKey(
    struct RS_LocHost const* host,
    struct RS_PendingKey key)
{
    int n;

    assert(host);
    n = RS_Pending_CancelKey(host->pending, RS_LOC_TASK_CHANGE, key) +
        RS_Pending_CancelKey(host->pending, RS_LOC_TASK_MERGE, key);
    if( n > 0 )
        host_changed(host);
    return n;
}

void
RS_LocChange_Fill(
    struct RS_LocChange* c,
    int scene_x,
    int scene_z,
    int level,
    int loc_id,
    int shape,
    int angle,
    int op_flags,
    const char ops[5][32])
{
    assert(c);
    assert(ops);
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
    struct RS_LocHost const* host = &self->host;
    struct ToriRS_Location* cfg = host->ops->location(host->ctx, self->resolved_id);
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
        host->ops->varps(host->ctx), cfg->transforms, cfg->transform_count,
        cfg->transform_varbit, cfg->transform_varp);
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
    struct RS_LocHost const* host = &self->host;
    struct Assets* assets = host->assets;
    struct ToriRS_Location* cfg =
        self->resolved_id >= 0 ? host->ops->location(host->ctx, self->resolved_id) : NULL;
    int entries = 0;

    assert(self->par.join == 0);
    if( cfg && cfg->models && cfg->lengths )
        entries = cfg->shapes ? cfg->shapes_and_model_count : 1;
    for( int i = 0; i < entries; i++ )
        for( int j = 0; j < cfg->lengths[i]; j++ )
            if( cfg->models[i][j] >= 0 )
                (void)Assets_Await(
                    assets, AssetKey_Make(ASSET_KIND_MODEL, cfg->models[i][j], 0),
                    &self->par.join);
    self->seq_id = cfg && cfg->seq_id >= 0 ? cfg->seq_id : self->base_seq;
    if( self->seq_id >= 0 )
        (void)Assets_Await(
            assets, AssetKey_Make(ASSET_KIND_SEQUENCE, self->seq_id, 0), &self->par.join);
}

static int
Task_LocChange_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_LocChange* self = (struct Task_LocChange*)base;
    struct RS_LocHost const* host = &self->host;
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
             self->resolved_id >= 0 && self->resolve_depth < RS_LOC_RESOLVE_DEPTH;
             self->resolve_depth++ )
        {
            PT_PARALLEL_AWAIT_ASSET(
                host->assets,
                AssetKey_Make(ASSET_KIND_CONFIG, self->resolved_id, ASSET_CONFIG_LOC));
            if( RS_ParallelTask_Cancelled(&self->par) )
                PT_EXIT(&self->pt);
            if( !loc_change_resolve_step(self) )
                break;
        }
        loc_change_want_models(self);
        PT_PARALLEL_AWAIT_ASSETS(host->assets);
    }

    /* 2. Replaced by a newer change for this key, or its scope went. */
    if( RS_ParallelTask_Cancelled(&self->par) )
        PT_EXIT(&self->pt);

    /* 3. Still wanted? Its scene mid-rebuild is not an answer yet: the
     * rebuild of its view is waited out, by name, and its finish carries
     * the change over (or drops it) by cancelling it. */
    if( host->ops->built_trigger )
        PT_PARALLEL_AWAIT_REBUILD(
            host->ops->built_trigger(host->ctx, self->par.view),
            !host->ops->scene_building(host->ctx, &self->par));
    if( RS_ParallelTask_Cancelled(&self->par) )
        PT_EXIT(&self->pt);
    world = host->ops->world(host->ctx, &self->par, "loc change");
    if( !world )
        PT_EXIT(&self->pt);
    /* A loc whose own record the cache could not serve cannot be placed (the
     * builder resolves it from that record): the reference's gate would hold
     * it forever; it is dropped, said. */
    if( self->change.loc_id >= 0 && !host->ops->location(host->ctx, self->change.loc_id) )
    {
        TORIRS_LOG(
            "loc change: loc %d at %d,%d,l%d dropped, its config could not be loaded\n",
            self->change.loc_id, self->change.x, self->change.z, self->change.level);
        PT_EXIT(&self->pt);
    }

    /* 4. Apply. */
    carried = self->change.loc_id >= 0 && RS_LocChange_Carried(self, &carried_seq);
    host->ops->place(host->ctx, self->par.view, world, &self->change, -1, carried, carried_seq);
    if( host->ops->applied )
        host->ops->applied(host->ctx, self, carried, carried_seq);

    PT_END(&self->pt);
}

static void
Task_LocChange_Free(struct ToriRS_Task* base)
{
    struct Task_LocChange* self = (struct Task_LocChange*)base;
    struct RS_LocHost const host = self->host;

    host.ops->release(host.ctx, &self->par);
    ToriMem_Free(ToriMem_Process(), self);
    host_changed(&host);
}

struct Task_LocChange*
RS_LocChange_Add(
    struct RS_LocHost const* host,
    int view,
    struct RS_LocChange const* c,
    struct RS_LocCarry const* carry,
    unsigned tick,
    int drive_row)
{
    struct Task_LocChange* task;
    struct World const* world;
    struct RS_PendingKey key;

    assert(host);
    assert(host->ops);
    assert(c);
    world = host->ops->view_world(host->ctx, view);
    key = RS_LocTasks_Key(host, view, c->x, c->z, c->level, c->shape);
    (void)RS_LocTasks_CancelKey(host, key);
    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_LocChange_VTable;
    strncpy(task->task.name, "LocChange", sizeof(task->task.name) - 1);
    PT_INIT(&task->pt);
    task->host = *host;
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
    host->ops->add(host->ctx, &task->task, &task->par, RS_LOC_TASK_CHANGE, key, view);
    host_changed(host);
    return task;
}

static struct Task_LocChange*
loc_change_of(struct RS_ParallelTask* par)
{
    struct Task_LocChange* task;

    if( !par )
        return NULL;
    task = (struct Task_LocChange*)((char*)par - offsetof(struct Task_LocChange, par));
    assert(task->task.vtable == &Task_LocChange_VTable);
    return task;
}

struct Task_LocChange*
RS_LocChange_Pending(
    struct RS_LocHost const* host,
    int view,
    int scene_x,
    int scene_z,
    int level,
    int layer)
{
    assert(host);
    return loc_change_of(RS_Pending_Find(
        host->pending,
        RS_LOC_TASK_CHANGE,
        RS_PendingKey_View(
            view, scene_x, scene_z, level, layer, host->ops->view_gen(host->ctx, view))));
}

bool
RS_LocMerge_Pending(
    struct RS_LocHost const* host,
    int view,
    int scene_x,
    int scene_z,
    int level,
    int layer)
{
    assert(host);
    return RS_Pending_Find(
               host->pending,
               RS_LOC_TASK_MERGE,
               RS_PendingKey_View(
                   view, scene_x, scene_z, level, layer,
                   host->ops->view_gen(host->ctx, view))) != NULL;
}

int
RS_LocChange_OfView(
    struct RS_LocHost const* host,
    int view,
    struct Task_LocChange** out,
    int cap)
{
    struct ToriRS_TaskQueue* queue;
    int n = 0;

    assert(host);
    queue = host->queue;
    if( !queue )
        return 0;
    for( struct ToriRS_Task* t = queue->head; t; t = t->next )
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

void
RS_LocChange_Carry(
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
RS_LocChange_Take(
    struct Task_LocChange* landed,
    int seq_id)
{
    assert(landed);
    assert(landed->change.loc_id >= 0);
    landed->carry.take_seq = seq_id;
    landed->carry.take_set = 1;
}

int
RS_LocChange_Carried(
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

/* ---- TASK_LOCMERGE -------------------------------------------------------- */

struct Task_LocMerge
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_ParallelTask par;
    struct RS_LocHost host;
    /* The tile and layer it hides (scene tile of its view, cache level). */
    int x;
    int z;
    int level;
    int shape;
    /* Client cycles the hide and the restore are due. */
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
    struct RS_LocChange* out)
{
    static const char none[5][32] = { { 0 }, { 0 }, { 0 }, { 0 }, { 0 } };
    int const layer = World_LocShapeToLayer(self->shape);

    for( int i = 0; i < world->loc_change_count; i++ )
    {
        struct World_LocChange const* rec = &world->loc_changes[i];

        if( rec->level != self->level || rec->layer != layer || rec->x != self->x ||
            rec->z != self->z )
            continue;
        RS_LocChange_Fill(
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
    struct RS_LocHost const* host = &self->host;
    struct World* world;
    struct RS_LocChange c;

    (void)io;
    PT_BEGIN(&self->pt);

    /* 1. Wait for the hide's time (Client-TS locChangeCreate's t1+1). */
    PT_PARALLEL_AWAIT_CYCLE(host->clock, self->hide_at);
    if( RS_ParallelTask_Cancelled(&self->par) )
        PT_EXIT(&self->pt);
    world = host->ops->world(host->ctx, &self->par, "loc merge");
    if( !world )
        PT_EXIT(&self->pt);
    /* The hide: a delete, which loads nothing. Its record is a merge's. */
    RS_LocChange_Fill(&c, self->x, self->z, self->level, -1, self->shape, 0, 0x1f, none);
    host->ops->place(
        host->ctx, self->par.view, world, &c,
        (int)(self->restore_at - host->ops->cycle(host->ctx)), 0, -1);

    /* 2. Wait for the restore's time (t2+1). */
    PT_PARALLEL_AWAIT_CYCLE(host->clock, self->restore_at);
    if( RS_ParallelTask_Cancelled(&self->par) )
        PT_EXIT(&self->pt);
    world = host->ops->world(host->ctx, &self->par, "loc merge");
    if( !world )
        PT_EXIT(&self->pt);
    /* The restore is a change like any other: it waits for the old loc's
     * models, and replaces this task as its key's pending entry. */
    if( loc_merge_restore_of(world, self, &c) )
        (void)RS_LocChange_Add(host, self->par.view, &c, NULL, host->ops->tick(host->ctx), 0);

    PT_END(&self->pt);
}

static void
Task_LocMerge_Free(struct ToriRS_Task* base)
{
    struct Task_LocMerge* self = (struct Task_LocMerge*)base;
    struct RS_LocHost const host = self->host;

    host.ops->release(host.ctx, &self->par);
    ToriMem_Free(ToriMem_Process(), self);
    host_changed(&host);
}

void
RS_LocMerge_Add(
    struct RS_LocHost const* host,
    int view,
    int scene_x,
    int scene_z,
    int level,
    int shape,
    int start_cycle,
    int end_cycle)
{
    struct Task_LocMerge* task;
    struct RS_PendingKey key;
    uint32_t now;

    assert(host);
    assert(host->ops);
    now = host->ops->cycle(host->ctx);
    key = RS_LocTasks_Key(host, view, scene_x, scene_z, level, shape);
    (void)RS_LocTasks_CancelKey(host, key);
    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_LocMerge_VTable;
    strncpy(task->task.name, "LocMerge", sizeof(task->task.name) - 1);
    PT_INIT(&task->pt);
    task->host = *host;
    task->x = scene_x;
    task->z = scene_z;
    task->level = level;
    task->shape = shape;
    task->hide_at = now + (uint32_t)start_cycle + 1u;
    task->restore_at = now + (uint32_t)end_cycle + 1u;
    host->ops->add(host->ctx, &task->task, &task->par, RS_LOC_TASK_MERGE, key, view);
    host_changed(host);
}

/* ---- cancels ------------------------------------------------------------- */

int
RS_LocTasks_CancelZone(
    struct RS_LocHost const* host,
    int view,
    int base_x,
    int base_z,
    int zone_level)
{
    uint32_t gen;
    int cancelled = 0;

    assert(host);
    gen = host->ops->view_gen(host->ctx, view);
    for( int dz = 0; dz < 8; dz++ )
    {
        for( int dx = 0; dx < 8; dx++ )
        {
            int const x = base_x + dx;
            int const z = base_z + dz;
            /* The level a loc packet means on this tile (zone_loc_tile_at's
             * bridge climb), which is what the key was made with. */
            int const level = host->ops->cache_level(host->ctx, view, x, z, zone_level);

            for( int layer = 0; layer < 4; layer++ )
            {
                struct RS_PendingKey key = RS_PendingKey_View(view, x, z, level, layer, gen);

                cancelled += RS_Pending_CancelKey(host->pending, RS_LOC_TASK_CHANGE, key);
                cancelled += RS_Pending_CancelKey(host->pending, RS_LOC_TASK_MERGE, key);
            }
        }
    }
    if( cancelled > 0 )
        host_changed(host);
    return cancelled;
}

int
RS_LocZone_ResetRecords(
    struct World* world,
    int base_x,
    int base_z,
    int const levels[8][8],
    bool built,
    struct RS_LocChange out[RS_LOC_ZONE_KEYS])
{
    static const char none[5][32] = { { 0 }, { 0 }, { 0 }, { 0 }, { 0 } };
    int count;
    int write = 0;
    int restores = 0;

    assert(world);
    assert(levels);
    assert(out);
    count = world->loc_change_count;
    for( int i = 0; i < count; i++ )
    {
        struct World_LocChange* rec = &world->loc_changes[i];
        int const dx = rec->x - base_x;
        int const dz = rec->z - base_z;
        int in_zone = dx >= 0 && dx < 8 && dz >= 0 && dz < 8 && rec->level == levels[dx][dz];
        int shape;

        if( in_zone && !built )
            continue; /* dropped: the rebuild is the map's */
        if( in_zone )
        {
            shape = rec->old_shape >= 0 ? rec->old_shape : rec->new_shape;
            if( !(rec->old_type < 0 && rec->new_type < 0) &&
                !(rec->old_type == rec->new_type && rec->old_angle == rec->new_angle &&
                  shape == rec->new_shape) )
            {
                assert(restores < RS_LOC_ZONE_KEYS);
                RS_LocChange_Fill(
                    &out[restores++], rec->x, rec->z, rec->level, rec->old_type, shape,
                    rec->old_angle, 0x1f, none);
            }
            rec->old_shape = shape;
            rec->new_type = rec->old_type;
            rec->new_angle = rec->old_angle;
            rec->new_shape = shape;
            rec->start_time = -1;
            rec->end_time = -1;
        }
        if( write != i )
            world->loc_changes[write] = world->loc_changes[i];
        write++;
    }
    world->loc_change_count = write;
    return restores;
}

int
RS_LocTasks_ZoneReset(
    struct RS_LocHost const* host,
    int view,
    int base_x,
    int base_z,
    int zone_level,
    unsigned tick)
{
    static struct RS_LocChange restores[RS_LOC_ZONE_KEYS];
    int levels[8][8];
    struct World* world;
    bool built;
    int count;

    assert(host);
    world = host->ops->view_world(host->ctx, view);
    assert(world);
    built = !host->ops->built_trigger || world->load_complete;
    for( int dx = 0; dx < 8; dx++ )
        for( int dz = 0; dz < 8; dz++ )
            levels[dx][dz] =
                host->ops->cache_level(host->ctx, view, base_x + dx, base_z + dz, zone_level);
    /* A change still pending in the zone never reached the scene or the
     * records: cancelled, so it cannot land after the reset and the state
     * behind it. */
    if( built )
        (void)RS_LocTasks_CancelZone(host, view, base_x, base_z, zone_level);
    count = RS_LocZone_ResetRecords(world, base_x, base_z, levels, built, restores);
    for( int i = 0; i < count; i++ )
        (void)RS_LocChange_Add(host, view, &restores[i], NULL, tick, 1);
    return count;
}

void
RS_LocTasks_CancelAll(struct RS_LocHost const* host)
{
    assert(host);
    if( RS_Pending_Cancel(host->pending, RS_PendingScope_Kind(RS_LOC_TASK_CHANGE)) +
            RS_Pending_Cancel(host->pending, RS_PendingScope_Kind(RS_LOC_TASK_MERGE)) >
        0 )
        host_changed(host);
}
