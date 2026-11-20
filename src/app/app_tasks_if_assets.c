/*
 * PARALLEL TASKS: INTERFACE MODELS, CHATHEADS, INVENTORY ICONS.
 *
 * What belongs here: what IF_SETMODEL, IF_SETOBJECT, IF_SETNPCHEAD(_ACTIVE),
 * IF_SETPLAYERHEAD and IF_SETANIM put on a widget once its models are in, and
 * an inventory's item icons -- keyed on the component and its interface's
 * close serial, checked before applying against what the widget asks for
 * now.
 *
 * Each task here has the one shape (game/rs_parallel_task.h,
 * app/app_parallel_task.h): it embeds struct RS_ParallelTask, carries its
 * own data copied from the packet, is added with App_ParallelTaskAdd to the
 * parallel queue (app->frame_runner) as an ordinary task, and its run
 * function reads top to bottom -- await, cancelled?, still wanted?, apply --
 * with its policies as `if`s in that body and no `switch` in it. Its free
 * calls App_ParallelTaskRelease. Its kinds come from
 * RS_PENDING_KINDS_IF_ASSETS_FIRST .. RS_PENDING_KINDS_IF_ASSETS_LAST
 * (game/rs_pending.h), defined in this file; what other units of the layer
 * call into this family is declared in this family's own section of
 * app/app_tasks.h.
 *
 * The two tasks:
 *
 *   Task_IfModel   one per interface MODEL widget's head (the store in
 *                  ui/uitree_if_models.h is the record of what the server
 *                  asked for; app_if_head_poll in app/app_if_models.c binds
 *                  what landed whenever the tree moves). Keyed on the
 *                  component and the close serial its entry was set under; a
 *                  newer set on the component cancels it.
 *                  The same task loads a LOOSE head, one no component asked
 *                  for (app_if_head_enqueue), keyed on what it loads.
 *   Task_InvIcon   one per inventory slot whose item has no icon, raised when
 *                  the slot's container changes. Keyed on the container and
 *                  the slot; a slot that changes again cancels it.
 *   Task_SpriteFill one per scene id the bridge reserved for a sprite that was
 *                  not resident when a node was bound to it
 *                  (UITreeSceneBridge_BindSprite). Keyed on the sprite's
 *                  cache id; it waits for the sprite through the assets
 *                  table, asks again with the SPRITE back-off while it does
 *                  not come, and fills the reservation. (The bridge kept its
 *                  own list of these with its own retry clock, pumped every
 *                  frame.)
 *
 * IF_SETANIM needs no task: its sequence is only drawn, the widget shows its
 * rest pose until it is here, and the set wants it at once (app_if_models.c).
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; nothing outside the layer may include app/app_internal.h.
 */

#include "app/app_internal.h"
#include "mem/torirs_mem.h"

#include "engine/task_obj_model_load.h"

#include <stddef.h>

enum
{
    /* A head on an interface MODEL widget: RS_PendingKey_Component(the
     * component, the close serial its store entry was set under). */
    APP_TASK_IF_MODEL = RS_PENDING_KINDS_IF_ASSETS_FIRST,
    /* An inventory slot's icon: RS_PendingKey_Id(app_inv_icon_slot_id, 0). */
    APP_TASK_INV_ICON,
    /* A head no component asked for (app_if_head_enqueue: a plugin's own
     * MODEL control): RS_PendingKey_Id(the npc, obj or model id -- -1 for the
     * player's head --, the head kind). */
    APP_TASK_IF_MODEL_LOOSE,
    /* A sprite reservation's fill: RS_PendingKey_Id(the sprite's cache id,
     * 0). */
    APP_TASK_SPRITE_FILL,
};
_Static_assert((int)APP_TASK_SPRITE_FILL <= (int)RS_PENDING_KINDS_IF_ASSETS_LAST, "if_assets kinds fit the range");

/* ---- Task_IfModel ------------------------------------------------------- */

#define APP_IFMODEL_MODELS_MAX 32
#define APP_IFMODEL_TEXTURES_MAX 128
/* The scene's texture map (UITreeSceneBridge_PublishTextures). */
#define APP_IFMODEL_TEXTURE_ID_MAX 2048

struct Task_IfModel
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_ParallelTask par;
    struct App* app;

    /* What the packet asked for, copied from the store entry it set. */
    enum UIIfHeadKind kind;
    int component_id;
    /* The npc, obj or model id; -1 for the player's head. */
    int source_id;
    /* IF_SETOBJECT's third field: an IF1 zoom or an IF3 quantity. */
    int zoom;
    /* IF_SETPLAYERHEAD: the local player's appearance when it arrived. */
    int has_appearance;
    int slots[12];
    int colors[5];
    int gender;

    /* What it found it needs, link by link. */
    int resolved_npc_id;
    int shown_obj_id;
    int model_ids[APP_IFMODEL_MODELS_MAX];
    int model_count;
    int texture_ids[APP_IFMODEL_TEXTURES_MAX];
    int texture_count;
    int i;
};

/* Private to this unit, declared up front so definition order is free. */
static int
Task_IfModel_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io);
static void
Task_IfModel_Free(struct ToriRS_Task* base);
static void
if_model_npc_head_models(struct Task_IfModel* self);
static void
if_model_collect_textures(struct Task_IfModel* self);
static int
if_model_compose(struct Task_IfModel* self);
static void
if_model_copy_appearance(
    struct Task_IfModel* task,
    struct App* app);
static int
Task_InvIcon_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io);
static void
Task_InvIcon_Free(struct ToriRS_Task* base);

static struct ToriRS_TaskVTable Task_IfModel_VTable = {
    .run = Task_IfModel_Run,
    .free = Task_IfModel_Free,
};

static struct RS_PendingKey
if_model_key(struct UIIfHead const* head)
{
    return RS_PendingKey_Component(head->com_id, head->close_serial);
}

/* The npc's chathead models (reference NpcType.getHead skips a -1 slot). */
static void
if_model_npc_head_models(struct Task_IfModel* self)
{
    struct ToriRS_Npctype* npc;

    self->model_count = 0;
    if( self->resolved_npc_id < 0 )
        return; /* positional -1: intentionally hidden */
    npc = CacheProvider_NpctypeGet(self->app->provider, self->resolved_npc_id);
    if( !npc || !npc->heads )
        return;
    for( int h = 0; h < npc->heads_count && self->model_count < APP_IFMODEL_MODELS_MAX; h++ )
        if( npc->heads[h] >= 0 )
            self->model_ids[self->model_count++] = npc->heads[h];
}

/*
 * The textures the head's models draw with that are not in the scene yet: SD
 * ids only (ModelData.light()'s isSd gate drops the others before lighting),
 * each once. Read from the models resident now; one that did not come has no
 * faces to ask for.
 */
static void
if_model_collect_textures(struct Task_IfModel* self)
{
    struct App* app = self->app;

    self->texture_count = 0;
    for( int m = 0; m < self->model_count; m++ )
    {
        struct ToriRS_Model* model;

        if( !CacheProvider_ModelHas(app->provider, self->model_ids[m]) )
            continue;
        model = CacheProvider_ModelGet(app->provider, self->model_ids[m]);
        if( !model || !model->face_textures )
            continue;
        for( int f = 0; f < model->face_count; f++ )
        {
            int const id = (int)model->face_textures[f];
            int seen = 0;

            if( id < 0 || id >= APP_IFMODEL_TEXTURE_ID_MAX )
                continue;
            if( UITreeSceneBridge_TextureResident(&app->bridge, id) ||
                !CacheProvider_TextureIsSd(app->provider, id) )
                continue;
            for( int t = 0; t < self->texture_count && !seen; t++ )
                seen = self->texture_ids[t] == id;
            if( !seen && self->texture_count < APP_IFMODEL_TEXTURES_MAX )
                self->texture_ids[self->texture_count++] = id;
        }
    }
}

/* Composite the head from what is resident now. -1: nothing to show. */
static int
if_model_compose(struct Task_IfModel* self)
{
    struct App* app = self->app;

    if( self->kind == UI_IFHEAD_NPC )
        return self->resolved_npc_id < 0
                   ? -1
                   : UITreeSceneBridge_EnsureNpcHead(&app->bridge, self->resolved_npc_id);
    if( self->kind == UI_IFHEAD_OBJ )
    {
        /* Both: the bind draws whichever the component asks for (the count
         * variant on an IF3 component), and finds it composited. */
        int const base = UITreeSceneBridge_EnsureObjModel(&app->bridge, self->source_id);
        int const shown = self->shown_obj_id != self->source_id
                              ? UITreeSceneBridge_EnsureObjModel(&app->bridge, self->shown_obj_id)
                              : base;
        return base >= 0 ? base : shown;
    }
    if( self->kind == UI_IFHEAD_MODEL )
        return UITreeSceneBridge_EnsureModel(&app->bridge, self->source_id);
    if( !self->has_appearance )
        return -1;
    return UITreeSceneBridge_EnsurePlayerHead(&app->bridge, self->slots, self->colors, self->gender);
}

static int
Task_IfModel_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_IfModel* self = (struct Task_IfModel*)base;
    struct App* app = self->app;
    int index;
    int scene_id;

    PT_BEGIN(&self->pt);

    /* Replaced before it first ran (two sets in one packet burst): it asks
     * for nothing. */
    if( RS_ParallelTask_Cancelled(&self->par) )
        PT_EXIT(&self->pt);

    /* 1. Wait for what the head needs: its configs, its models, then their
     *    textures. */
    if( self->kind == UI_IFHEAD_NPC )
    {
        /* IF_SETNPCHEAD carries the NPC type the server is talking through.
         * For a multiNpc that is the model-less shell, just like NPC_INFO.
         * Resolve it under this client's vars before asking for chathead
         * models. The resolver also makes every selected config resident, so
         * a cold child cannot be mistaken for a terminal shell. */
        PT_TASK_AWAITSELF_IF(CreateTask_NpcMultiResolve(app, self->source_id, &self->resolved_npc_id));
        if_model_npc_head_models(self);
    }
    else if( self->kind == UI_IFHEAD_OBJ )
    {
        /* The obj, its inventory model and that model's textures: one asset
         * (OBJ_MODEL), which EnsureObjModel composites and publishes. */
        PT_PARALLEL_AWAIT_ASSET(
            app->assets, AssetKey_Make(ASSET_KIND_OBJ_MODEL, self->source_id, 1));
        if( RS_ParallelTask_Cancelled(&self->par) )
            PT_EXIT(&self->pt);
        /* U1: on an IF3 component the third field is a quantity and the
         * count variant is what draws (ObjType.getCountObj) -- another obj,
         * another model, another asset. Which format the component is may not
         * be known yet (the packet can come before its interface), so the
         * variant is loaded whenever the quantity names one. */
        self->shown_obj_id = app_if_obj_count_variant(app, self->source_id, self->zoom);
        if( self->shown_obj_id != self->source_id )
            PT_PARALLEL_AWAIT_ASSET(
                app->assets, AssetKey_Make(ASSET_KIND_OBJ_MODEL, self->source_id, self->zoom));
    }
    else if( self->kind == UI_IFHEAD_MODEL )
    {
        self->model_ids[0] = self->source_id;
        self->model_count = 1;
    }
    else
    {
        /* The local player's head (reference ClientPlayer.getHeadModel): the
         * idk configs and the worn objs' configs name the head models. */
        for( self->i = 0; self->has_appearance && self->i < 12; self->i++ )
        {
            if( Appearance_SlotKind(self->slots[self->i]) == APPEARANCE_SLOT_KIT )
                PT_PARALLEL_AWAIT_ASSET(
                    app->assets,
                    AssetKey_Make(
                        ASSET_KIND_CONFIG, Appearance_SlotKit(self->slots[self->i]), ASSET_CONFIG_IDK));
            else if( Appearance_SlotKind(self->slots[self->i]) == APPEARANCE_SLOT_OBJ )
                PT_PARALLEL_AWAIT_ASSET(
                    app->assets,
                    AssetKey_Make(
                        ASSET_KIND_CONFIG, Appearance_SlotObj(self->slots[self->i]), ASSET_CONFIG_OBJ));
            if( RS_ParallelTask_Cancelled(&self->par) )
                PT_EXIT(&self->pt);
        }
        self->model_count = self->has_appearance
                                ? PlayerHeadModel_CollectHeadModelIds(
                                      app->provider,
                                      self->slots,
                                      self->gender,
                                      self->model_ids,
                                      APP_IFMODEL_MODELS_MAX)
                                : 0;
    }

    for( self->i = 0; self->i < self->model_count; self->i++ )
    {
        PT_PARALLEL_AWAIT_ASSET(
            app->assets, AssetKey_Make(ASSET_KIND_MODEL, self->model_ids[self->i], 0));
        if( RS_ParallelTask_Cancelled(&self->par) )
            PT_EXIT(&self->pt);
    }
    /* U2: and the textures, for every kind of head. Only IF_SETMODEL asked
     * for them before (through the texture pump); an npc head, a player head
     * and an obj drew their textured faces only if the world happened to
     * have published those textures already. */
    if_model_collect_textures(self);
    for( self->i = 0; self->i < self->texture_count; self->i++ )
    {
        PT_PARALLEL_AWAIT_ASSET(
            app->assets, AssetKey_Make(ASSET_KIND_TEXTURE, self->texture_ids[self->i], 0));
        if( RS_ParallelTask_Cancelled(&self->par) )
            PT_EXIT(&self->pt);
    }

    /* 2. Cancelled while it waited: a newer set replaced it. */
    if( RS_ParallelTask_Cancelled(&self->par) )
        PT_EXIT(&self->pt);

    /* 3. Still wanted: the component still holds the head this task was
     *    made for (U2: a load had no check at landing and always
     *    composited). A replaced or closed head has nothing to apply. A
     *    loose head (no component) has no store entry: whoever asked for it
     *    asks the bridge again, and finds the composite. */
    index = -1;
    if( self->component_id >= 0 )
    {
        index = app_if_head_current(
            app, self->component_id, self->kind, self->source_id, self->zoom, self->par.key.gen);
        if( index < 0 )
            PT_EXIT(&self->pt);
    }

    /* 4. Apply: publish the textures that came, composite from the cache as
     *    it is now, and land the composite on the head for the bind. A head
     *    whose parts did not all come lands with nothing to show and is not
     *    attempted again until it is set again. */
    for( self->i = 0; self->i < self->texture_count; self->i++ )
        if( CacheProvider_TextureHas(app->provider, self->texture_ids[self->i]) &&
            !UITreeSceneBridge_TextureResident(&app->bridge, self->texture_ids[self->i]) )
            (void)UITreeSceneBridge_PublishTextures(&app->bridge, &self->texture_ids[self->i], 1);
    scene_id = if_model_compose(self);
    if( index >= 0 )
        UIIfModels_HeadLanded(&app->if_models, index, scene_id, self->resolved_npc_id);
    RedrawRequest_Raise(&app->redraw_request);
    if( scene_id < 0 && torirs_env_net_debug() )
        TORIRS_LOG(
            "if-head: component=0x%x kind=%d could not composite head (id=%d)\n",
            (unsigned)self->component_id,
            (int)self->kind,
            self->source_id);

    PT_END(&self->pt);
}

static void
Task_IfModel_Free(struct ToriRS_Task* base)
{
    struct Task_IfModel* self = (struct Task_IfModel*)base;
    App_ParallelTaskRelease(self->app, &self->par);
    ToriMem_Free(ToriMem_Process(), self);
}

void
App_IfModelWant(
    struct App* app,
    int com_id)
{
    struct Task_IfModel* task;
    struct UIIfHead* head;
    int index;

    assert(app);
    index = UIIfModels_HeadIndex(&app->if_models, com_id);
    assert(index >= 0);
    head = &app->if_models.heads[index];
    (void)RS_Pending_CancelKey(app->pending, APP_TASK_IF_MODEL, if_model_key(head));
    /* Pending again: what it lands is what the bind shows. */
    head->landed = 0;
    head->scene_id = -1;
    head->applied_gen = 0;

    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_IfModel_VTable;
    strncpy(task->task.name, "IfModel", sizeof(task->task.name) - 1);
    task->app = app;
    task->kind = (enum UIIfHeadKind)head->kind;
    task->component_id = com_id;
    task->source_id = head->npc_id;
    task->zoom = head->zoom;
    task->resolved_npc_id = -1;
    task->shown_obj_id = head->npc_id;
    if( task->kind == UI_IFHEAD_PLAYER )
        if_model_copy_appearance(task, app);
    PT_INIT(&task->pt);
    /* The parallel queue, as an ordinary task: the request is already stored
     * and the bind shows it on whichever frame it lands, so nothing orders
     * this against the packets, and holding the frame for a chathead's round
     * trip is exactly the pause the store exists to avoid. */
    App_ParallelTaskAdd(app, &task->task, &task->par, APP_TASK_IF_MODEL, if_model_key(head), -1);
}

/* The appearance a player head is composited from: the local player's now
 * (`has` 0 when there is none). */
static void
if_model_copy_appearance(
    struct Task_IfModel* task,
    struct App* app)
{
    struct WorldEntity_Player* lp = app_local_player(app);

    task->has_appearance = lp != NULL;
    if( !lp )
        return;
    memcpy(task->slots, lp->appearance.slots, sizeof(task->slots));
    memcpy(task->colors, lp->appearance.colors, sizeof(task->colors));
    task->gender = lp->gender;
}

static struct Task_IfModel*
if_model_loose_pending(
    struct App const* app,
    struct RS_PendingKey key)
{
    struct RS_ParallelTask* par = RS_Pending_Find(app->pending, APP_TASK_IF_MODEL_LOOSE, key);
    return par ? (struct Task_IfModel*)((char*)par - offsetof(struct Task_IfModel, par)) : NULL;
}

/*
 * A head no component asked for: load and composite (kind, id) into the
 * scene, for a caller that binds the composite itself once the bridge has it
 * (a plugin's owned MODEL control, App_PluginModelScene). The same task as a
 * component's head (Task_IfModel), on the parallel queue, keyed on what it
 * loads; a request for a head already on its way adds nothing, and a player
 * head whose appearance changed since its task was made replaces that task.
 * A head that came with nothing to show is asked for again by the next
 * request (the assets table remembers what failed).
 *
 * `component_id` is -1: a head a component shows is the store's
 * (App_IfModelWant), which this is not.
 */
void
app_if_head_enqueue(
    struct App* app,
    enum UIIfHeadKind kind,
    int component_id,
    int npc_id)
{
    struct Task_IfModel* task;
    struct Task_IfModel* pending;
    struct RS_PendingKey key;
    int const source_id = kind == UI_IFHEAD_PLAYER ? -1 : npc_id;

    assert(app);
    assert(component_id < 0);
    (void)component_id;
    key = RS_PendingKey_Id(source_id, (uint32_t)kind);

    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_IfModel_VTable;
    strncpy(task->task.name, "IfModelLoose", sizeof(task->task.name) - 1);
    task->app = app;
    task->kind = kind;
    task->component_id = -1;
    task->source_id = source_id;
    task->zoom = 0;
    task->resolved_npc_id = -1;
    task->shown_obj_id = source_id;
    if( kind == UI_IFHEAD_PLAYER )
        if_model_copy_appearance(task, app);

    pending = if_model_loose_pending(app, key);
    if( pending && (kind != UI_IFHEAD_PLAYER ||
                    (pending->has_appearance == task->has_appearance &&
                     memcmp(pending->slots, task->slots, sizeof(task->slots)) == 0 &&
                     memcmp(pending->colors, task->colors, sizeof(task->colors)) == 0 &&
                     pending->gender == task->gender)) )
    {
        /* Already on its way. */
        ToriMem_Free(ToriMem_Process(), task);
        return;
    }
    if( pending )
        (void)RS_Pending_CancelKey(app->pending, APP_TASK_IF_MODEL_LOOSE, key);
    PT_INIT(&task->pt);
    App_ParallelTaskAdd(app, &task->task, &task->par, APP_TASK_IF_MODEL_LOOSE, key, -1);
}

bool
App_IfModelLoosePending(
    struct App const* app,
    enum UIIfHeadKind kind,
    int id)
{
    assert(app);
    return if_model_loose_pending(
               app, RS_PendingKey_Id(kind == UI_IFHEAD_PLAYER ? -1 : id, (uint32_t)kind)) != NULL;
}

void
App_IfModelCancel(
    struct App* app,
    int com_id)
{
    int index;

    assert(app);
    index = UIIfModels_HeadIndex(&app->if_models, com_id);
    if( index < 0 )
        return; /* no head, so no task */
    (void)RS_Pending_CancelKey(
        app->pending, APP_TASK_IF_MODEL, if_model_key(&app->if_models.heads[index]));
}

bool
App_IfModelPending(
    struct App const* app,
    int com_id)
{
    int index;

    assert(app);
    index = UIIfModels_HeadIndex(&app->if_models, com_id);
    return index >= 0 &&
           RS_Pending_Find(app->pending, APP_TASK_IF_MODEL, if_model_key(&app->if_models.heads[index])) !=
               NULL;
}

/* ---- Task_InvIcon -------------------------------------------------------- *
 *
 * Server UPDATE_INV_FULL/PARTIAL (rs_gameproto_exec.c) write item ids into the
 * inv containers but leave scene_id = INV_MANAGER_NO_SCENE_ID, because the
 * inventory model may not be resident and rasterizing needs it loaded. The
 * emit path (emit_rs_inv_slots) only draws a slot when scene_id >= 0. This
 * mirrors task_interface_open's seed-time icon step (load the models, then
 * UITreeSceneBridge_EnsureObjIcon and stamp the scene id back), for the items
 * that arrive after the interface is open.
 *
 * One task per slot, raised when its container changes (App_InvIconsWant):
 * the old pass scanned every slot of every container whenever one was due.
 *
 * A slot whose parts are all resident and still make no icon (an obj with no
 * inventory model) is stamped with a distinct sentinel; the server replacing
 * the item resets scene_id to NO_SCENE_ID and raises a new task. A slot whose
 * parts did NOT arrive is never stamped and never given up on: the read that
 * came back empty may be a dropped JS5 socket or a browser storage error. Its
 * task waits (500 ms doubling to 4 s, the INV_ICONS policy) and asks again
 * for as long as the slot holds the item. Baking before the model's textures
 * arrive would cache the icon blank for the session, which is why the asset
 * awaited is the whole OBJ_MODEL chain, textures included. */
#define APP_INV_ICON_SCENE_FAILED (-2)
#define APP_INV_ICON_SLOT_BITS 12

struct Task_InvIcon
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_ParallelTask par;
    struct App* app;
    /* The slot and what it held when the task was made. */
    int container_id;
    int slot;
    int obj_id;
    int count;
    /* The pause before asking again, and the client cycle it is over. */
    uint32_t wait_ms;
    uint32_t due_cycle;
};

static struct ToriRS_TaskVTable Task_InvIcon_VTable = {
    .run = Task_InvIcon_Run,
    .free = Task_InvIcon_Free,
};

static struct RS_PendingKey
inv_icon_key(
    int container_id,
    int slot)
{
    assert(container_id >= 0);
    assert(container_id < (1 << (30 - APP_INV_ICON_SLOT_BITS)));
    assert(slot >= 0);
    assert(slot < (1 << APP_INV_ICON_SLOT_BITS));
    /* INV_MANAGER_MAX_SLOTS fits the slot's bits. */
    return RS_PendingKey_Id((container_id << APP_INV_ICON_SLOT_BITS) | slot, 0);
}

static int
inv_icon_count(struct InvSlot const* slot)
{
    return slot->obj_count > 0 ? slot->obj_count : 1;
}

/* The slot, if it still holds this task's item without an icon. */
static struct InvSlot*
inv_icon_slot(struct Task_InvIcon const* self)
{
    struct InvContainer* c = InvManager_GetContainer(&self->app->invs, self->container_id);
    struct InvSlot* slot;

    if( !c || !c->slots || self->slot >= c->slot_count )
        return NULL;
    slot = &c->slots[self->slot];
    if( !InvManager_SlotWantsIcon(slot) || slot->obj_id != self->obj_id ||
        inv_icon_count(slot) != self->count )
        return NULL;
    return slot;
}

static int
Task_InvIcon_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_InvIcon* self = (struct Task_InvIcon*)base;
    struct App* app = self->app;
    struct InvSlot* slot;
    int scene_id;
    (void)io;

    PT_BEGIN(&self->pt);

    /* 1. Wait for the obj, its count variant, its model and the model's
     *    textures; ask again after a pause while they do not come. */
    for( ;; )
    {
        PT_PARALLEL_AWAIT_ASSET(
            app->assets, AssetKey_Make(ASSET_KIND_OBJ_MODEL, self->obj_id, self->count));
        /* 2. A newer item in the slot cancelled it. */
        if( RS_ParallelTask_Cancelled(&self->par) )
            PT_EXIT(&self->pt);
        /* 3. Still wanted: the slot holds the item and has no icon yet (the
         *    interface's own seed may have baked it meanwhile). */
        if( !inv_icon_slot(self) )
            PT_EXIT(&self->pt);
        if( !ObjModelLoad_NeedsWork(app->provider, self->obj_id, self->count) )
            break;
        /* The back-off, waited as a client cycle (a timer, by name). */
        self->wait_ms = Assets_BackoffNext(Assets_Policy(ASSET_KIND_INV_ICONS), self->wait_ms);
        self->due_cycle = App_ParallelCycle(app) + App_ParallelCyclesFor(self->wait_ms);
        PT_APP_AWAIT_CYCLE(app, self->due_cycle);
        if( RS_ParallelTask_Cancelled(&self->par) )
            PT_EXIT(&self->pt);
    }

    /* 4. Apply: bake the icon and stamp it on the slot; every part resident
     *    and still no icon is the sentinel, so the slot is not asked for
     *    again until it changes. */
    slot = inv_icon_slot(self);
    assert(slot);
    scene_id = UITreeSceneBridge_EnsureObjIcon(&app->bridge, self->obj_id, self->count);
    slot->scene_id = scene_id >= 0 ? scene_id : APP_INV_ICON_SCENE_FAILED;
    slot->atlas_index = 0;
    UITree_HostInputsChanged(
        &app->ui_host,
        UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_INVENTORY) |
            UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_ASSETS));
    RedrawRequest_Raise(&app->redraw_request);

    PT_END(&self->pt);
}

static void
Task_InvIcon_Free(struct ToriRS_Task* base)
{
    struct Task_InvIcon* self = (struct Task_InvIcon*)base;
    App_ParallelTaskRelease(self->app, &self->par);
    ToriMem_Free(ToriMem_Process(), self);
}

static struct Task_InvIcon*
inv_icon_pending(
    struct App const* app,
    struct RS_PendingKey key)
{
    struct RS_ParallelTask* par = RS_Pending_Find(app->pending, APP_TASK_INV_ICON, key);
    return par ? (struct Task_InvIcon*)((char*)par - offsetof(struct Task_InvIcon, par)) : NULL;
}

void
App_InvIconsWant(
    struct App* app,
    int container_id)
{
    struct InvContainer const* c;

    assert(app);
    c = InvManager_FindContainer(&app->invs, container_id);
    assert(c);
    for( int s = 0; c->slots && s < c->slot_count; s++ )
    {
        struct InvSlot const* slot = &c->slots[s];
        struct RS_PendingKey const key = inv_icon_key(container_id, s);
        struct Task_InvIcon* pending = inv_icon_pending(app, key);
        int const wants = InvManager_SlotWantsIcon(slot);
        struct Task_InvIcon* task;

        if( pending && wants && pending->obj_id == slot->obj_id &&
            pending->count == inv_icon_count(slot) )
            continue; /* already on its way */
        if( pending )
            (void)RS_Pending_CancelKey(app->pending, APP_TASK_INV_ICON, key);
        if( !wants )
            continue;

        task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
        assert(task);
        task->task.vtable = &Task_InvIcon_VTable;
        strncpy(task->task.name, "InvIcon", sizeof(task->task.name) - 1);
        task->app = app;
        task->container_id = container_id;
        task->slot = s;
        task->obj_id = slot->obj_id;
        task->count = inv_icon_count(slot);
        PT_INIT(&task->pt);
        /* The parallel queue: it is added from the tick that applied the
         * inventory packets, so it already runs after them, and the slot
         * stays blank until its icon is built -- ordering needs nothing from
         * the packet queue. Equipping a set of unseen items on the packet
         * queue held every packet, and the picture, on a wave of obj and
         * model reads; here the world moves and the cells fill as the icons
         * land. */
        App_ParallelTaskAdd(app, &task->task, &task->par, APP_TASK_INV_ICON, key, -1);
    }
}

int
App_InvIconsPending(struct App const* app)
{
    assert(app);
    return RS_Pending_Count(app->pending, APP_TASK_INV_ICON);
}

void
App_IfAssetsAfterRestore(struct App* app)
{
    assert(app);
    for( int ci = 0; ci < app->invs.container_count; ci++ )
        App_InvIconsWant(app, app->invs.containers[ci].inv_id);
}

/* ---- Task_SpriteFill ----------------------------------------------------- *
 *
 * A node bound to a sprite that is not resident holds a scene id with nothing
 * behind it (UITreeSceneBridge_BindSprite): one read that came back empty
 * must not blank a mounted interface for the session. This task fills it. The
 * load is the assets table's (ASSET_KIND_SPRITE: one in flight however many
 * nodes bind the sprite); a load that came back without it is asked for again
 * after the SPRITE back-off (500 ms doubling to 4 s), waited as client
 * cycles, for as long as the sprites table may list it -- never given up on.
 * An id the table answers it does not list (CacheProvider_SpriteAbsent) ends
 * it: the slot stays empty, as an unlisted graphic always drew. */
struct Task_SpriteFill
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_ParallelTask par;
    struct App* app;
    int cache_id;
    int scene_id;
    /* The pause before asking again, and the client cycle it is over. */
    uint32_t wait_ms;
    uint32_t due_cycle;
};

static int
Task_SpriteFill_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_SpriteFill* self = (struct Task_SpriteFill*)base;
    struct App* app = self->app;
    (void)io;

    PT_BEGIN(&self->pt);

    for( ;; )
    {
        /* 1. Wait for it. */
        PT_PARALLEL_AWAIT_ASSET(app->assets, AssetKey_Make(ASSET_KIND_SPRITE, self->cache_id, 0));
        /* 2. Cancelled: a logout (App_IfAssetsAfterLogout wants it again). */
        if( RS_ParallelTask_Cancelled(&self->par) )
            PT_EXIT(&self->pt);
        /* 4. Here: apply. */
        if( UITreeSceneBridge_FillSprite(&app->bridge, self->cache_id, self->scene_id) )
            break;
        /* 3. The table says it will never arrive: the slot stays empty. */
        if( CacheProvider_SpriteAbsent(app->provider, self->cache_id) )
            PT_EXIT(&self->pt);
        /* The back-off, waited as a client cycle (a timer, by name). */
        self->wait_ms = Assets_BackoffNext(Assets_Policy(ASSET_KIND_SPRITE), self->wait_ms);
        self->due_cycle = App_ParallelCycle(app) + App_ParallelCyclesFor(self->wait_ms);
        PT_APP_AWAIT_CYCLE(app, self->due_cycle);
        if( RS_ParallelTask_Cancelled(&self->par) )
            PT_EXIT(&self->pt);
    }

    /* The fill advanced the bridge's asset revision, which is what the tree
     * emit reads; a napping loop is woken to draw it. */
    RedrawRequest_Raise(&app->redraw_request);

    PT_END(&self->pt);
}

static void
Task_SpriteFill_Free(struct ToriRS_Task* base)
{
    struct Task_SpriteFill* self = (struct Task_SpriteFill*)base;
    App_ParallelTaskRelease(self->app, &self->par);
    ToriMem_Free(ToriMem_Process(), self);
}

static struct ToriRS_TaskVTable Task_SpriteFill_VTable = {
    .run = Task_SpriteFill_Run,
    .free = Task_SpriteFill_Free,
};

/* The bridge's filler (UITreeSceneBridgeSpriteBoundFn): one task per
 * reservation, none while one is pending for the sprite. */
static void
app_sprite_fill_want(
    void* user,
    int cache_graphic_id,
    int scene_id)
{
    struct App* app = user;
    struct Task_SpriteFill* task;

    assert(app);
    assert(cache_graphic_id >= 0);
    assert(scene_id > 0);
    if( App_SpriteFillPending(app, cache_graphic_id) )
        return;
    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_SpriteFill_VTable;
    strncpy(task->task.name, "SpriteFill", sizeof(task->task.name) - 1);
    task->app = app;
    task->cache_id = cache_graphic_id;
    task->scene_id = scene_id;
    PT_INIT(&task->pt);
    /* The parallel queue, as an ordinary task: the node draws nothing until
     * it is filled, and nothing else waits on it. */
    App_ParallelTaskAdd(
        app, &task->task, &task->par, APP_TASK_SPRITE_FILL, RS_PendingKey_Id(cache_graphic_id, 0), -1);
}

void
App_IfAssetsBindBridge(struct App* app)
{
    assert(app);
    assert(app->assets);
    assert(app->pending);
    UITreeSceneBridge_SetSpriteFiller(&app->bridge, app_sprite_fill_want, app);
}

bool
App_SpriteFillPending(
    struct App const* app,
    int cache_graphic_id)
{
    assert(app);
    return RS_Pending_Find(app->pending, APP_TASK_SPRITE_FILL, RS_PendingKey_Id(cache_graphic_id, 0)) != NULL;
}

int
App_SpriteFillsPending(struct App const* app)
{
    assert(app);
    return RS_Pending_Count(app->pending, APP_TASK_SPRITE_FILL);
}

void
App_IfAssetsAfterLogout(struct App* app)
{
    assert(app);
    UITreeSceneBridge_RewantSprites(&app->bridge);
}
