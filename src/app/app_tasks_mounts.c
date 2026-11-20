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

/* ---- a mounted group's visual assets ------------------------------------ */

struct Task_MountVisuals
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_ParallelTask par;
    struct App* app;
    /* The group and its close serial when it was mounted. */
    int group;
    uint32_t close_serial;
    /* Copied from the pack and the containers when it was mounted. */
    struct PackVisualAssets visual;
    int seed_obj_ids[INTERFACE_OPEN_SEED_OBJ_MAX];
    int seed_obj_count;
};

static int
Task_MountVisuals_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_MountVisuals* self = (struct Task_MountVisuals*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);

    /* 1. The pack's models and the seed inventory models, all at once; then
     *    the default avatar's kit models when a widget shows it. */
    for( int i = 0; i < self->visual.model_count; i++ )
        (void)Assets_Await(
            app->assets, AssetKey_Make(ASSET_KIND_MODEL, self->visual.model_ids[i], 0), &self->par.join);
    for( int i = 0; i < self->seed_obj_count; i++ )
        (void)Assets_Await(
            app->assets, AssetKey_Make(ASSET_KIND_OBJ_MODEL, self->seed_obj_ids[i], 1), &self->par.join);
    PT_PARALLEL_AWAIT_ASSETS(app->assets);
    if( RS_ParallelTask_Cancelled(&self->par) )
        PT_EXIT(&self->pt);
    if( self->visual.needs_player )
        PT_TASK_AWAITSELF_IF(CreateTask_PlayerAppearanceLoad(app->provider, app->assets));

    /* 2. A remount of the group, its close, or a logout cancelled it. */
    if( RS_ParallelTask_Cancelled(&self->par) )
        PT_EXIT(&self->pt);

    /* The icons are the containers', not the group's: bound whatever became
     * of the group, as the mount bound the ones that were here. */
    InterfaceOpen_BindSeedIcons(&app->invs, &app->bridge);
    RedrawRequest_Raise(&app->redraw_request);

    /* 3. Still the opening of the group it was made for. */
    assert(app->tree);
    if( !UITree_GroupPresent(app->tree, self->group) ||
        UITree_GroupCloseSerial(app->tree, self->group) != self->close_serial )
        PT_EXIT(&self->pt);

    /* 4. Bind what is here now onto the group's model widgets. */
    (void)InterfaceOpen_BindGroupModels(app->tree, &app->bridge, self->group);

    PT_END(&self->pt);
}

static void
Task_MountVisuals_Free(struct ToriRS_Task* base)
{
    struct Task_MountVisuals* self = (struct Task_MountVisuals*)base;
    App_ParallelTaskRelease(self->app, &self->par);
    PackAssets_FreeVisual(ToriMem_Process(), &self->visual);
    ToriMem_Free(ToriMem_Process(), self);
}

static struct ToriRS_TaskVTable Task_MountVisuals_VTable = {
    .run = Task_MountVisuals_Run,
    .free = Task_MountVisuals_Free,
};

void
App_MountVisualsAdd(
    struct App* app,
    int group)
{
    struct ToriRS_ComponentPack const* pack;
    struct Task_MountVisuals* task;
    struct RS_PendingKey key;
    int missing = 0;

    assert(app);
    assert(app->tree);
    assert(group > 0);
    pack = CacheProvider_ComponentPackGet(app->provider, group);
    /* The cache can drop a pack between the mount's bake and here (its load
     * scripts may have yielded): what the mount bound is all there is. */
    if( !pack )
        return;

    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    PackAssets_CollectVisual(pack, ToriMem_Process(), &task->visual);
    task->seed_obj_count =
        InterfaceOpen_CollectSeedObjs(&app->invs, task->seed_obj_ids, INTERFACE_OPEN_SEED_OBJ_MAX);

    /* Nothing missing: the mount bound it all. */
    for( int i = 0; !missing && i < task->visual.model_count; i++ )
        missing = !CacheProvider_ModelHas(app->provider, task->visual.model_ids[i]);
    for( int i = 0; !missing && i < task->seed_obj_count; i++ )
        missing = ObjModelLoad_NeedsWork(app->provider, task->seed_obj_ids[i], 1);
    if( !missing && task->visual.needs_player )
        missing = app->bridge.player_scene_id <= 0;
    if( !missing )
    {
        PackAssets_FreeVisual(ToriMem_Process(), &task->visual);
        ToriMem_Free(ToriMem_Process(), task);
        return;
    }

    key = RS_PendingKey_Component(group << 16, UITree_GroupCloseSerial(app->tree, group));
    /* Newest wins: a remount of the group binds for both. */
    (void)RS_Pending_CancelKey(app->pending, APP_TASK_MOUNT_VISUALS, key);
    task->task.vtable = &Task_MountVisuals_VTable;
    strncpy(task->task.name, "MountVisuals", sizeof(task->task.name) - 1);
    task->app = app;
    task->group = group;
    task->close_serial = key.gen;
    PT_INIT(&task->pt);
    App_ParallelTaskAdd(app, &task->task, &task->par, APP_TASK_MOUNT_VISUALS, key, -1);
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
