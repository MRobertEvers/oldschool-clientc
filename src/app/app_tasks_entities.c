/*
 * PARALLEL TASKS: ENTITY BODIES AND ANIMATIONS.
 *
 * What belongs here: a player's or npc's body (the npc's composite, a
 * player's kit and wear models, a transmog, held items) and the sequences it
 * plays -- the entity exists at packet time and draws nothing until its body
 * is applied -- keyed on the entity, its type and its spawn serial, never on
 * a scene element id (those are recycled).
 *
 * Each task here has the one shape (game/rs_parallel_task.h,
 * app/app_parallel_task.h): it embeds struct RS_ParallelTask, carries its
 * own data copied from the packet, is added with App_ParallelTaskAdd to the
 * parallel queue (app->frame_runner) as an ordinary task, and its run
 * function reads top to bottom -- await, cancelled?, still wanted?, apply --
 * with its policies as `if`s in that body and no `switch` in it. Its free
 * calls App_ParallelTaskRelease. Its kinds come from
 * RS_PENDING_KINDS_ENTITIES_FIRST .. RS_PENDING_KINDS_ENTITIES_LAST
 * (game/rs_pending.h), defined in this file; what other units of the layer
 * call into this family is declared in this family's own section of
 * app/app_tasks.h.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; nothing outside the layer may include app/app_internal.h.
 */

#include "app/app_internal.h"
#include "game/task_entity_assets.h"

/* ---- kinds (RS_PENDING_KINDS_ENTITIES_FIRST .. _LAST) -------------------- */

enum AppEntityTaskKind
{
    /* An npc's body: the composite of its drawn type (ASSET_KIND_NPC_BODY,
     * id = the drawn type, variant = the shell). Key: the npc's entity key. */
    APP_TASK_NPC_BODY = RS_PENDING_KINDS_ENTITIES_FIRST,
    /* A sequence bound to a scene element once it is resident
     * (ASSET_KIND_SEQUENCE). Key: RS_PendingKey_Id(element, element serial). */
    APP_TASK_SEQ_BIND,
    APP_TASK_ENTITIES_END
};
_Static_assert(APP_TASK_ENTITIES_END - 1 <= RS_PENDING_KINDS_ENTITIES_LAST, "the entities family's range");

struct RS_PendingKey
App_EntityNpcKey(struct WorldEntity_NPC const* npc)
{
    assert(npc);
    return RS_PendingKey_Entity(RS_PENDING_ENTITY_NPC, npc->server_slot, npc->spawn_serial);
}

/* ---- the npc body --------------------------------------------------------
 *
 * The entity exists from its packet with an empty body (app_world_spawn_npc_
 * now, App_WorldApplyNpcType); this task waits for the drawn type's models
 * and stances and mounts them. One task per npc: a later retype cancels the
 * earlier one, and one load of the type (the assets table) answers every
 * npc of that type that waits on it.
 */
struct Task_NpcBody
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_ParallelTask par;
    struct App* app;
    /* What it is for, copied when it was made. */
    int server_slot;
    uint32_t spawn_serial;
    int npc_id;
    int base_npc_id;
};

static int
Task_NpcBody_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_NpcBody* self = (struct Task_NpcBody*)base;
    struct App* app = self->app;
    int world_idx = -1;
    int element_id = -1;
    struct WorldEntity_NPC* npc;

    (void)io;
    PT_BEGIN(&self->pt);

    /* 1. Wait for the body. */
    PT_PARALLEL_AWAIT_ASSET(
        app->assets, AssetKey_Make(ASSET_KIND_NPC_BODY, self->npc_id, self->base_npc_id));

    /* 2. A newer retype, the npc's removal or a logout cancelled it. */
    if( RS_ParallelTask_Cancelled(&self->par) )
        PT_EXIT(&self->pt);
    /* A body that could not be served: the npc stays as it is, drawing
     * nothing (the policy of ASSET_KIND_NPC_BODY is to give up). */
    if( !RS_ParallelTask_AssetHere(&self->par) )
        PT_EXIT(&self->pt);

    /* 3. Still the same npc, still drawn as this type? */
    if( !RS_EntitySync_FindNpc(&app->esync, self->server_slot, &world_idx, &element_id) )
        PT_EXIT(&self->pt);
    npc = World_EntityPoolGet(&app->world->entities.npc, world_idx);
    if( !npc || npc->spawn_serial != self->spawn_serial || npc->npc_id != self->npc_id ||
        npc->base_npc_id != self->base_npc_id )
        PT_EXIT(&self->pt);

    /* 4. Mount it, reading the models from the cache now. */
    App_WorldNpcBodyLanded(app, world_idx, npc->element_id, self->npc_id, self->base_npc_id);
    RedrawRequest_Raise(&app->redraw_request);

    PT_END(&self->pt);
}

static void
Task_NpcBody_Free(struct ToriRS_Task* base)
{
    struct Task_NpcBody* self = (struct Task_NpcBody*)base;
    App_ParallelTaskRelease(self->app, &self->par);
    ToriMem_Free(ToriMem_Session(), self);
}

static struct ToriRS_TaskVTable Task_NpcBody_VTable = {
    .run = Task_NpcBody_Run,
    .free = Task_NpcBody_Free,
};

void
App_EntityNpcBodyWant(
    struct App* app,
    int world_idx)
{
    struct WorldEntity_NPC* npc;
    struct Task_NpcBody* task;
    struct RS_PendingKey key;

    assert(app);
    assert(app->world);
    npc = World_EntityPoolGet(&app->world->entities.npc, world_idx);
    assert(npc);
    assert(npc->npc_id >= 0);
    key = App_EntityNpcKey(npc);
    /* Newest wins: whatever an earlier op wanted for this npc is not what it
     * is drawn as now. */
    RS_Pending_CancelKey(app->pending, APP_TASK_NPC_BODY, key);
    if( EntityAssets_NpcBodyResident(app, npc->npc_id, npc->base_npc_id) )
        return;
    task = ToriMem_Calloc(ToriMem_Session(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_NpcBody_VTable;
    strncpy(task->task.name, "NpcBody", sizeof(task->task.name) - 1);
    task->app = app;
    task->server_slot = npc->server_slot;
    task->spawn_serial = npc->spawn_serial;
    task->npc_id = npc->npc_id;
    task->base_npc_id = npc->base_npc_id;
    PT_INIT(&task->pt);
    App_ParallelTaskAdd(app, &task->task, &task->par, APP_TASK_NPC_BODY, key, -1);
}

int
App_EntityNpcBodyPending(
    struct App* app,
    struct WorldEntity_NPC const* npc)
{
    assert(app);
    assert(npc);
    return RS_Pending_Find(app->pending, APP_TASK_NPC_BODY, App_EntityNpcKey(npc)) != NULL;
}

void
App_EntityNpcGone(
    struct App* app,
    int server_slot)
{
    assert(app);
    if( server_slot < 0 )
        return;
    (void)RS_Pending_Cancel(
        app->pending, RS_PendingScope_Entity(RS_PENDING_ENTITY_NPC, server_slot));
}

/* ---- a deferred sequence bind -------------------------------------------
 *
 * app_world_apply_seq on an element whose sequence is not resident: this
 * waits for it and binds it, the clock running from when it was applied (the
 * bind catches the frames up from `start_cycle`). Keyed on the element and
 * its allocation serial, so an element freed (or its id reused) while the
 * sequence loaded is "gone". One per element: a newer sequence applied to
 * the element cancels the older bind, which would otherwise land on top of
 * it if it arrived later.
 */
struct Task_SeqBind
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_ParallelTask par;
    struct App* app;
    int element_id;
    uint32_t element_serial;
    int seq_id;
    int start_cycle;
};

static int
Task_SeqBind_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_SeqBind* self = (struct Task_SeqBind*)base;
    struct App* app = self->app;

    (void)io;
    PT_BEGIN(&self->pt);

    PT_PARALLEL_AWAIT_ASSET(app->assets, AssetKey_Make(ASSET_KIND_SEQUENCE, self->seq_id, 0));

    if( RS_ParallelTask_Cancelled(&self->par) )
        PT_EXIT(&self->pt);
    if( !RS_ParallelTask_AssetHere(&self->par) )
        PT_EXIT(&self->pt);
    /* The element went, or its id was handed to something else. */
    if( ToriDraw_SceneElementSerial(app->scene, self->element_id) != self->element_serial )
        PT_EXIT(&self->pt);

    if( app_world_try_bind_seq(app, self->element_id, self->seq_id, self->start_cycle) )
        RedrawRequest_Raise(&app->redraw_request);

    PT_END(&self->pt);
}

static void
Task_SeqBind_Free(struct ToriRS_Task* base)
{
    struct Task_SeqBind* self = (struct Task_SeqBind*)base;
    App_ParallelTaskRelease(self->app, &self->par);
    ToriMem_Free(ToriMem_Session(), self);
}

static struct ToriRS_TaskVTable Task_SeqBind_VTable = {
    .run = Task_SeqBind_Run,
    .free = Task_SeqBind_Free,
};

static struct RS_PendingKey
app_seq_bind_key(
    struct App* app,
    int element_id)
{
    return RS_PendingKey_Id(element_id, ToriDraw_SceneElementSerial(app->scene, element_id));
}

void
App_EntitySeqBindWant(
    struct App* app,
    int element_id,
    int seq_id,
    int start_cycle)
{
    struct Task_SeqBind* task;
    struct RS_PendingKey key;

    assert(app);
    assert(element_id >= 0);
    assert(seq_id >= 0);
    key = app_seq_bind_key(app, element_id);
    RS_Pending_CancelKey(app->pending, APP_TASK_SEQ_BIND, key);
    task = ToriMem_Calloc(ToriMem_Session(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_SeqBind_VTable;
    strncpy(task->task.name, "SeqBind", sizeof(task->task.name) - 1);
    task->app = app;
    task->element_id = element_id;
    task->element_serial = key.gen;
    task->seq_id = seq_id;
    task->start_cycle = start_cycle;
    PT_INIT(&task->pt);
    App_ParallelTaskAdd(app, &task->task, &task->par, APP_TASK_SEQ_BIND, key, -1);
}

void
App_EntitySeqBindCancel(
    struct App* app,
    int element_id)
{
    assert(app);
    assert(element_id >= 0);
    (void)RS_Pending_CancelKey(app->pending, APP_TASK_SEQ_BIND, app_seq_bind_key(app, element_id));
}

int
App_EntitySeqBindPending(
    struct App* app,
    int element_id)
{
    assert(app);
    return RS_Pending_Find(app->pending, APP_TASK_SEQ_BIND, app_seq_bind_key(app, element_id)) != NULL;
}
