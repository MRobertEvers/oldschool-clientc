/*
 * PARALLEL TASKS: THE VISUAL PARTS OF INTERFACE MOUNTS, PLAYER MODELS.
 *
 * What belongs here: the visual half of an interface mount (IF_OPENTOP /
 * OPENSUB / MOVESUB / RESYNC and old-format slot mounts): the pack's models,
 * kit models and seed inventory models, which the mount task no longer waits
 * for (the pack, the bake, the load scripts and the transmits stay required,
 * on the packet queue); and a player-model widget's build
 * (IF_SETPLAYERMODEL_*), keyed on its component and a composition generation
 * so a stale build never overwrites a newer one, the last built model kept
 * until the new one is applied.
 *
 * Each task here has the one shape (game/rs_parallel_task.h,
 * app/app_parallel_task.h): it embeds struct RS_ParallelTask, carries its
 * own data copied from the packet, is added with App_ParallelTaskAdd to the
 * parallel queue (app->frame_runner) as an ordinary task, and its run
 * function reads top to bottom -- await, cancelled?, still wanted?, apply --
 * with its policies as `if`s in that body and no `switch` in it. Its free
 * calls App_ParallelTaskRelease. Its kinds come from
 * RS_PENDING_KINDS_MOUNTS_FIRST .. RS_PENDING_KINDS_MOUNTS_LAST
 * (game/rs_pending.h), defined in this file; what other units of the layer
 * call into this family is declared in this family's own section of
 * app/app_tasks.h.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; nothing outside the layer may include app/app_internal.h.
 */

#include "app/app_internal.h"
#include "mem/torirs_mem.h"

#include "engine/task_obj_model_load.h"
#include "engine/uitree_builder/task_interface_open.h"
#include "engine/uitree_builder/task_pack_assets_load.h"

#include <stddef.h>

/* ---- a player-model widget's build ------------------------------------- */

enum
{
    APP_IF_PLAYER_MODEL_MAX_MODELS = 64,
};

struct Task_IfPlayerModelBuild
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_ParallelTask par;
    struct App* app;
    /* The widget, its record's close serial and the composition this build
     * is of, and the composition itself, copied when the packet committed
     * it. */
    int component_id;
    uint32_t close_serial;
    uint32_t composition;
    int slots[12];
    int colors[5];
    int gender;
    int model_count;
    int model_ids[APP_IF_PLAYER_MODEL_MAX_MODELS];
};

static int
Task_IfPlayerModelBuild_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_IfPlayerModelBuild* self = (struct Task_IfPlayerModelBuild*)base;
    struct App* app = self->app;
    (void)io;

    PT_BEGIN(&self->pt);

    /* 1. The idk and obj config behind every slot, then every gendered wear
     *    model they name: the chain, one link at a time. */
    for( int i = 0; i < 12; i++ )
    {
        enum AppearanceSlotKind const kind = Appearance_SlotKind(self->slots[i]);
        if( kind == APPEARANCE_SLOT_KIT )
            (void)Assets_Await(
                app->assets,
                AssetKey_Make(ASSET_KIND_CONFIG, Appearance_SlotKit(self->slots[i]), ASSET_CONFIG_IDK),
                &self->par.join);
        else if( kind == APPEARANCE_SLOT_OBJ )
            (void)Assets_Await(
                app->assets,
                AssetKey_Make(ASSET_KIND_CONFIG, Appearance_SlotObj(self->slots[i]), ASSET_CONFIG_OBJ),
                &self->par.join);
    }
    PT_PARALLEL_AWAIT_ASSETS(app->assets);
    if( RS_ParallelTask_Cancelled(&self->par) )
        PT_EXIT(&self->pt);
    self->model_count = PlayerModel_CollectAppearanceModelIds(
        app->provider, self->slots, self->gender, self->model_ids, APP_IF_PLAYER_MODEL_MAX_MODELS);
    for( int i = 0; i < self->model_count; i++ )
        if( self->model_ids[i] >= 0 )
            (void)Assets_Await(
                app->assets, AssetKey_Make(ASSET_KIND_MODEL, self->model_ids[i], 0), &self->par.join);
    PT_PARALLEL_AWAIT_ASSETS(app->assets);

    /* 2. A newer composition of the widget replaced this one. */
    if( RS_ParallelTask_Cancelled(&self->par) )
        PT_EXIT(&self->pt);

    /* 3. Still wanted: the record is still the widget's, of the same opening
     *    of its interface, and still holds this composition. */
    {
        int const index = UIIfModels_PlayerModelIndex(&app->if_models, self->component_id);
        struct UIIfPlayerModel* model;

        if( index < 0 )
            PT_EXIT(&self->pt);
        model = &app->if_models.player_models[index];
        if( model->close_serial != self->close_serial || model->composition != self->composition )
            PT_EXIT(&self->pt);
        if( app->tree &&
            UITree_GroupCloseSerial(app->tree, (self->component_id >> 16) & 0xffff) >
                self->close_serial )
            PT_EXIT(&self->pt);

        /* 4. Build. A part that did not load fails the build: the widget
         *    keeps the model it had. */
        if( UITreeSceneBridge_BuildInterfacePlayerModel(
                &app->bridge, model->scene_id, self->slots, self->colors, self->gender) >= 0 )
        {
            model->built = 1;
            model->applied_gen = 0;
            RedrawRequest_Raise(&app->redraw_request);
        }
    }

    PT_END(&self->pt);
}

static void
Task_IfPlayerModelBuild_Free(struct ToriRS_Task* base)
{
    struct Task_IfPlayerModelBuild* self = (struct Task_IfPlayerModelBuild*)base;
    App_ParallelTaskRelease(self->app, &self->par);
    ToriMem_Free(ToriMem_Process(), self);
}

static struct ToriRS_TaskVTable Task_IfPlayerModelBuild_VTable = {
    .run = Task_IfPlayerModelBuild_Run,
    .free = Task_IfPlayerModelBuild_Free,
};

void
App_IfPlayerModelBuildAdd(
    struct App* app,
    struct UIIfPlayerModel const* model)
{
    struct Task_IfPlayerModelBuild* task;
    struct RS_PendingKey key;

    assert(app);
    assert(model);
    assert(model->com_id >= 0);
    key = RS_PendingKey_Component(model->com_id, model->close_serial);
    /* Newest wins: an older composition still loading is never built. */
    (void)RS_Pending_CancelKey(app->pending, APP_TASK_IF_PLAYER_MODEL_BUILD, key);
    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_IfPlayerModelBuild_VTable;
    strncpy(task->task.name, "IfPlayerModelBuild", sizeof(task->task.name) - 1);
    task->app = app;
    task->component_id = model->com_id;
    task->close_serial = model->close_serial;
    task->composition = model->composition;
    memcpy(task->slots, model->slots, sizeof(task->slots));
    memcpy(task->colors, model->colors, sizeof(task->colors));
    task->gender = model->gender;
    PT_INIT(&task->pt);
    App_ParallelTaskAdd(app, &task->task, &task->par, APP_TASK_IF_PLAYER_MODEL_BUILD, key, -1);
}

/* ---- a mounted interface's models ---------------------------------------- *
 *
 * One task per model a mounted widget still waits for: its model, its active
 * model, or the default avatar on a player-preview widget. Made by whatever
 * mounted it -- a sub mount (App_MountVisualsAdd, the group's nodes), an
 * old-format slot mount (App_SlotVisualsAdd, the nodes under the slot) or the
 * root build (App_RootVisualsAdd, every model node of the new tree) -- each
 * with the node's ref (index and incarnation) copied when it was made.
 *
 * One model per task, so a step binds one widget: binding a whole mount's
 * models in one step cost 5.85 ms on a loaded machine. The seed inventory
 * icons are the inventory-icon tasks' (App_InvIconsWant), one slot a task.
 *
 * Keyed RS_PendingKey_Component(the node's component id, its group's close
 * serial), one kind per want, so a close of the group cancels the lot
 * (RS_PendingScope_Group), a slot cleared or remounted cancels the keys of
 * the nodes it held (App_SlotVisualsCancel: a dat1 component id names no
 * group), a new root cancels every one (App_MountsTreeCleared), and a logout
 * cancels everything. A node with no component id (none a mount bakes from a
 * pack) gets none. */

struct Task_MountModel
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_ParallelTask par;
    struct App* app;
    /* The widget, and its group's close serial when it was mounted. */
    struct UITreeNodeRef ref;
    int group;
    uint32_t close_serial;
    /* What it waits for: the model (-1 for the avatar). */
    enum InterfaceOpenModelWantKind want;
    int model_id;
};

static int
Task_MountModel_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_MountModel* self = (struct Task_MountModel*)base;
    struct App* app = self->app;
    int32_t idx;

    PT_BEGIN(&self->pt);

    /* Replaced before it first ran: it asks for nothing. */
    if( RS_ParallelTask_Cancelled(&self->par) )
        PT_EXIT(&self->pt);

    /* 1. The model; or the default appearance's kit models, which no asset
     *    kind names (CreateTask_PlayerAppearanceLoad, joined). */
    if( self->want == INTERFACE_OPEN_WANT_AVATAR )
        PT_TASK_AWAITSELF_IF(CreateTask_PlayerAppearanceLoad(app->provider));
    else
        PT_PARALLEL_AWAIT_ASSET(app->assets, AssetKey_Make(ASSET_KIND_MODEL, self->model_id, 0));

    /* 2. A close of its group, a remount of its slot, a new root or a logout
     *    cancelled it. */
    if( RS_ParallelTask_Cancelled(&self->par) )
        PT_EXIT(&self->pt);
    if( self->want != INTERFACE_OPEN_WANT_AVATAR && !RS_ParallelTask_AssetHere(&self->par) )
        PT_EXIT(&self->pt); /* it did not come: the widget stays empty, as it would have */

    /* 3. Still the widget it was made for, in the same opening of its
     *    group. */
    assert(app->tree);
    idx = UITree_ResolveRef(app->tree, self->ref);
    if( idx < 0 || UITree_GroupCloseSerial(app->tree, self->group) != self->close_serial )
        PT_EXIT(&self->pt);

    /* 4. Bind what is here now onto it. */
    if( InterfaceOpen_BindModelNode(app->tree, &app->bridge, idx) )
    {
        RedrawRequest_Raise(&app->redraw_request);
        /* stderr, as the contract's own trace: TORIRS_LOG is compiled out of
         * a release build. */
        if( TaskRunner_ContractTrace() )
            fprintf(
                stderr,
                "queue_contract: trace mounts: model %d bound onto 0x%08x at frame %ld\n",
                self->model_id,
                (unsigned)app->tree->components[idx].component_id,
                g_torirs_frame_no);
    }

    PT_END(&self->pt);
}

static void
Task_MountModel_Free(struct ToriRS_Task* base)
{
    struct Task_MountModel* self = (struct Task_MountModel*)base;
    App_ParallelTaskRelease(self->app, &self->par);
    ToriMem_Free(ToriMem_Process(), self);
}

static struct ToriRS_TaskVTable Task_MountModel_VTable = {
    .run = Task_MountModel_Run,
    .free = Task_MountModel_Free,
};

static int
mount_model_kind(enum InterfaceOpenModelWantKind want)
{
    if( want == INTERFACE_OPEN_WANT_MODEL )
        return APP_TASK_MOUNT_MODEL;
    if( want == INTERFACE_OPEN_WANT_ACTIVE_MODEL )
        return APP_TASK_MOUNT_ACTIVE_MODEL;
    assert(want == INTERFACE_OPEN_WANT_AVATAR);
    return APP_TASK_MOUNT_AVATAR;
}

/* The key of node `idx`'s tasks: its component id and its group's close
 * serial. */
static struct RS_PendingKey
mount_model_key(
    struct UITree const* tree,
    int32_t idx)
{
    int const cid = tree->components[idx].component_id;

    assert(cid >= 0);
    return RS_PendingKey_Component(cid, UITree_GroupCloseSerial(tree, (cid >> 16) & 0xffff));
}

static struct Task_MountModel*
mount_model_pending(
    struct App const* app,
    int kind,
    struct RS_PendingKey key)
{
    struct RS_ParallelTask* par = RS_Pending_Find(app->pending, kind, key);
    return par ? (struct Task_MountModel*)((char*)par - offsetof(struct Task_MountModel, par)) : NULL;
}

/* A task for each model node `idx` waits for; returns how many it added. */
static int
mount_model_want_node(
    struct App* app,
    int32_t idx)
{
    struct InterfaceOpenModelWant wants[INTERFACE_OPEN_NODE_WANTS_MAX];
    int added = 0;
    int n;

    if( app->tree->components[idx].component_id < 0 )
        return 0;
    n = InterfaceOpen_NodeModelWants(app->tree, app->provider, &app->bridge, idx, wants);
    for( int w = 0; w < n; w++ )
    {
        int const kind = mount_model_kind(wants[w].kind);
        struct RS_PendingKey const key = mount_model_key(app->tree, idx);
        struct Task_MountModel* pending = mount_model_pending(app, kind, key);
        struct Task_MountModel* task;

        if( pending && pending->model_id == wants[w].model_id &&
            pending->ref.incarnation == UITree_RefAt(app->tree, idx).incarnation )
            continue; /* already on its way */
        /* Newest wins: the widget now shows something else, or was
         * rebaked. */
        if( pending )
            (void)RS_Pending_CancelKey(app->pending, kind, key);
        task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
        assert(task);
        task->task.vtable = &Task_MountModel_VTable;
        strncpy(task->task.name, "MountModel", sizeof(task->task.name) - 1);
        task->app = app;
        task->ref = UITree_RefAt(app->tree, idx);
        task->group = (app->tree->components[idx].component_id >> 16) & 0xffff;
        task->close_serial = key.gen;
        task->want = wants[w].kind;
        task->model_id = wants[w].model_id;
        PT_INIT(&task->pt);
        App_ParallelTaskAdd(app, &task->task, &task->par, kind, key, -1);
        added++;
        if( TaskRunner_ContractTrace() )
            fprintf(
                stderr,
                "queue_contract: trace mounts: 0x%08x waits for model %d at frame %ld\n",
                (unsigned)app->tree->components[idx].component_id,
                task->model_id,
                g_torirs_frame_no);
    }
    return added;
}

/* The worn and backpack icons a mount shows that are not baked yet: one
 * inventory-icon task a slot (deduped there). */
static void
mount_want_seed_icons(struct App* app)
{
    static int const containers[] = {
        INV_MANAGER_CONTAINER_WORN,
        INV_MANAGER_CONTAINER_BACKPACK,
    };

    for( size_t i = 0; i < sizeof(containers) / sizeof(containers[0]); i++ )
        if( InvManager_FindContainer(&app->invs, containers[i]) )
            App_InvIconsWant(app, containers[i]);
}

int
App_MountVisualsAdd(
    struct App* app,
    int group)
{
    struct UITreeNodeSet const* gset;
    int added = 0;

    assert(app);
    assert(app->tree);
    assert(group > 0);
    mount_want_seed_icons(app);
    gset = UITree_GroupNodes(app->tree, group);
    for( int gi = 0; gset && gi < gset->count; gi++ )
        added += mount_model_want_node(app, gset->slots[gi]);
    return added;
}

int
App_SlotVisualsAdd(
    struct App* app,
    int32_t owner_index)
{
    int32_t nodes[APP_MOUNT_SUBTREE_MODELS_MAX];
    int count;
    int added = 0;

    assert(app);
    assert(app->tree);
    mount_want_seed_icons(app);
    count = InterfaceOpen_SubtreeModelNodes(app->tree, owner_index, nodes, APP_MOUNT_SUBTREE_MODELS_MAX);
    assert(count <= APP_MOUNT_SUBTREE_MODELS_MAX);
    for( int i = 0; i < count; i++ )
        added += mount_model_want_node(app, nodes[i]);
    return added;
}

void
App_SlotVisualsCancel(
    struct App* app,
    int32_t owner_index)
{
    int32_t nodes[APP_MOUNT_SUBTREE_MODELS_MAX];
    int count;

    assert(app);
    assert(app->tree);
    count = InterfaceOpen_SubtreeModelNodes(app->tree, owner_index, nodes, APP_MOUNT_SUBTREE_MODELS_MAX);
    assert(count <= APP_MOUNT_SUBTREE_MODELS_MAX);
    for( int i = 0; i < count; i++ )
    {
        struct RS_PendingKey key;

        if( app->tree->components[nodes[i]].component_id < 0 )
            continue;
        key = mount_model_key(app->tree, nodes[i]);
        (void)RS_Pending_CancelKey(app->pending, APP_TASK_MOUNT_MODEL, key);
        (void)RS_Pending_CancelKey(app->pending, APP_TASK_MOUNT_ACTIVE_MODEL, key);
        (void)RS_Pending_CancelKey(app->pending, APP_TASK_MOUNT_AVATAR, key);
    }
}

int
App_RootVisualsAdd(struct App* app)
{
    struct UITreeNodeSet const* models;
    int added = 0;

    assert(app);
    assert(app->tree);
    mount_want_seed_icons(app);
    /* The tree's model nodes, not the tree: a candidate list. */
    models = &app->tree->models;
    for( int i = 0; i < models->count; i++ )
        added += mount_model_want_node(app, models->slots[i]);
    if( TaskRunner_ContractTrace() )
        fprintf(
            stderr,
            "queue_contract: trace mounts: root %d built at frame %ld; %d model(s) left to parallel tasks\n",
            RS_BootState_InterfaceId(&app->boot),
            g_torirs_frame_no,
            added);
    return added;
}

void
App_MountsTreeCleared(struct App* app)
{
    assert(app);
    assert(app->pending);
    (void)RS_Pending_Cancel(app->pending, RS_PendingScope_Kind(APP_TASK_MOUNT_MODEL));
    (void)RS_Pending_Cancel(app->pending, RS_PendingScope_Kind(APP_TASK_MOUNT_ACTIVE_MODEL));
    (void)RS_Pending_Cancel(app->pending, RS_PendingScope_Kind(APP_TASK_MOUNT_AVATAR));
}

void
App_MountsGroupClosed(
    struct App* app,
    int group)
{
    assert(app);
    assert(group > 0);
    (void)RS_Pending_Cancel(app->pending, RS_PendingScope_Group(group));
}
