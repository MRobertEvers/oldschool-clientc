/*
 * PARALLEL TASKS: VISUAL SCRIPT OPERATIONS.
 *
 * What belongs here: the client-script operations that only put something
 * on screen (a model set on a widget, an object on a MODEL widget, a
 * component's models, a loc model): the operation writes what was asked for
 * onto the widget and returns, and a task here waits for the asset, keyed on
 * the component and its interface's close serial. Operations whose value the
 * script reads stay required and wait in the script, as today. A burst of
 * them made from inside a script keeps the remedy the client has for it
 * (released a bounded number per logic tick), expressed as tasks.
 *
 * Each task here has the one shape (game/rs_parallel_task.h,
 * app/app_parallel_task.h): it embeds struct RS_ParallelTask, carries its
 * own data copied from the packet, is added with App_ParallelTaskAdd to the
 * parallel queue (app->frame_runner) as an ordinary task, and its run
 * function reads top to bottom -- await, cancelled?, still wanted?, apply --
 * with its policies as `if`s in that body and no `switch` in it. Its free
 * calls App_ParallelTaskRelease. Its kinds come from
 * RS_PENDING_KINDS_SCRIPTS_FIRST .. RS_PENDING_KINDS_SCRIPTS_LAST
 * (game/rs_pending.h), defined in this file; what other units of the layer
 * call into this family is declared in this family's own section of
 * app/app_tasks.h.
 *
 * Which operations are visual and which required is ONE table, next to the
 * yield plans in game/task_cs2_run.c. The three visual ones that have their
 * task here (the model, npc head and icon forms are app/app_if_models.c's and
 * app/app_tasks_ground.c's):
 *
 *   Task_ScriptLocModel    cc_setlocmodel: the loc's config, then each of
 *                          its models (a chain: the ids are in the config).
 *   Task_ScriptObjModel    cc_setobject on a MODEL widget: the obj's model
 *                          composite, then the host's model half
 *                          (RS_CS2Host_SetObjectModelLanded; the set
 *                          already wrote the pose).
 *   Task_ScriptPackModels  a pack a script's COMPONENT yield baked: the
 *                          models, npc configs and player kit its widgets
 *                          name, then the bind the bake did when they were
 *                          awaited before it.
 *
 * A newer set on the same widget cancels the older task (the latest set
 * wins: RS_CS2Host.widget_visual_cancel). A task keeps no pointer across a
 * wait: the widget is a UITreeNodeRef plus its group's close serial, looked up
 * again when the asset is here, and "gone" or "closed since" is a plain exit.
 *
 * Also here: the assets table's rows for what a script READS
 * (ASSET_KIND_CONFIG from ASSET_CONFIG_SCRIPTS_FIRST), the required side of
 * the same table.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; nothing outside the layer may include app/app_internal.h.
 */

#include "app/app_internal.h"
#include "mem/torirs_mem.h"

#include "engine/player_appearance.h"

/* ---- the required reads' rows in the assets table ----------------------- */

bool
app_script_config_resident(
    struct App* app,
    struct AssetKey key)
{
    struct CacheProvider* p;
    int size;

    assert(app);
    assert(app->provider);
    assert(key.kind == ASSET_KIND_CONFIG);
    assert(key.variant >= ASSET_CONFIG_SCRIPTS_FIRST);
    assert(key.variant < ASSET_CONFIG_SCRIPTS_END);
    p = app->provider;
    if( key.variant == ASSET_CONFIG_SCRIPT )
        return CacheProvider_ClientScriptHas(p, key.id);
    if( key.variant == ASSET_CONFIG_ENUM )
        return CacheProvider_EnumHas(p, key.id);
    if( key.variant == ASSET_CONFIG_STRUCT )
        return CacheProvider_StructHas(p, key.id);
    if( key.variant == ASSET_CONFIG_PARAM )
        return CacheProvider_ParamHas(p, key.id);
    if( key.variant == ASSET_CONFIG_INVTYPE )
        return CacheProvider_InvtypeGet(p, key.id, &size);
    if( key.variant == ASSET_CONFIG_NPC )
        return CacheProvider_NpctypeHas(p, key.id);
    if( key.variant == ASSET_CONFIG_LOC )
        return CacheProvider_LocationHas(p, key.id);
    if( key.variant == ASSET_CONFIG_DBROW )
        return CacheProvider_DbRowHas(p, key.id);
    if( key.variant == ASSET_CONFIG_DBTABLE )
        return CacheProvider_DbTableHas(p, key.id);
    if( key.variant == ASSET_CONFIG_DBINDEX )
        return CacheProvider_DbTableIndexHas(p, key.id);
    if( key.variant == ASSET_CONFIG_FONT )
        return CacheProvider_FontHas(p, key.id);
    if( key.variant == ASSET_CONFIG_MAPELEMENT )
        return CacheProvider_MapElementHas(p, key.id);
    if( key.variant == ASSET_CONFIG_WORLDMAP )
        return CacheProvider_WorldMapGet(p) != NULL;
    assert(key.variant == ASSET_CONFIG_OBJ_ALL);
    return p->objtypes_all_loaded;
}

struct ToriRS_Task*
app_script_config_load(
    struct App* app,
    struct AssetKey key)
{
    struct CacheProvider* p;

    assert(app);
    assert(app->provider);
    assert(key.kind == ASSET_KIND_CONFIG);
    assert(key.variant >= ASSET_CONFIG_SCRIPTS_FIRST);
    assert(key.variant < ASSET_CONFIG_SCRIPTS_END);
    p = app->provider;
    if( key.variant == ASSET_CONFIG_SCRIPT )
        return CreateTask_ClientScriptLoad(p, key.id);
    if( key.variant == ASSET_CONFIG_ENUM )
        return CreateTask_EnumLoad(p, key.id);
    if( key.variant == ASSET_CONFIG_STRUCT )
        return CreateTask_StructLoad(p, key.id);
    if( key.variant == ASSET_CONFIG_PARAM )
        return CreateTask_ParamLoad(p, key.id);
    if( key.variant == ASSET_CONFIG_INVTYPE )
        return CreateTask_InvtypeLoad(p, key.id);
    if( key.variant == ASSET_CONFIG_NPC )
        return CreateTask_NpcLoad(p, key.id);
    if( key.variant == ASSET_CONFIG_LOC )
        return CreateTask_LocLoad(p, key.id);
    if( key.variant == ASSET_CONFIG_DBROW )
        return CreateTask_DbRowLoad(p, key.id);
    if( key.variant == ASSET_CONFIG_DBTABLE )
        return CreateTask_DbTableLoad(p, key.id);
    if( key.variant == ASSET_CONFIG_DBINDEX )
        return CreateTask_DbTableIndexLoad(p, key.id);
    if( key.variant == ASSET_CONFIG_FONT )
        return CreateTask_FontLoad(p, key.id);
    if( key.variant == ASSET_CONFIG_MAPELEMENT )
        return CreateTask_MapElementLoad(p, key.id);
    if( key.variant == ASSET_CONFIG_WORLDMAP )
        return CreateTask_WorldMapLoad(p);
    assert(key.variant == ASSET_CONFIG_OBJ_ALL);
    return CreateTask_ObjLoadAll(p);
}

/* ---- the widget a visual task binds onto ------------------------------- */

/* Who a task binds onto: the node, and its group's close serial when the set
 * was made. A key, never a pointer. */
struct AppScriptWidget
{
    int component_id;
    int group;
    uint32_t close_serial;
    struct UITreeNodeRef ref;
};

static struct AppScriptWidget
app_script_widget(
    struct App* app,
    int component_id)
{
    struct AppScriptWidget w;

    assert(app->tree);
    w.component_id = component_id;
    w.group = (component_id >> 16) & 0xffff;
    w.close_serial = UITree_GroupCloseSerial(app->tree, w.group);
    w.ref = UITree_RefAt(app->tree, UITree_FindByComponentId(app->tree, component_id));
    return w;
}

static struct RS_PendingKey
app_script_widget_key(struct AppScriptWidget const* w)
{
    return RS_PendingKey_Component(w->component_id, w->close_serial);
}

/* Is the widget still the one the set was made on? Its node is live and its
 * group has not been closed since. -1 when not. */
static int32_t
app_script_widget_live(
    struct App* app,
    struct AppScriptWidget const* w)
{
    int32_t idx;

    if( UITree_GroupCloseSerial(app->tree, w->group) != w->close_serial )
        return -1;
    idx = UITree_ResolveRef(app->tree, w->ref);
    if( idx < 0 || app->tree->components[idx].type != UIELEM_RS_MODEL )
        return -1;
    return idx;
}

/* ---- Task_ScriptLocModel -------------------------------------------------- */

struct Task_ScriptLocModel
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_ParallelTask par;
    struct App* app;
    struct AppScriptWidget widget;
    int loc_id;
    int model_i;
};

static int
Task_ScriptLocModel_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_ScriptLocModel* self = (struct Task_ScriptLocModel*)base;
    struct App* app = self->app;
    int32_t idx;
    int scene_id;

    (void)io;
    PT_BEGIN(&self->pt);

    /* The loc's config names its models; then each of them. */
    PT_PARALLEL_AWAIT_ASSET(
        app->assets, AssetKey_Make(ASSET_KIND_CONFIG, self->loc_id, ASSET_CONFIG_LOC));
    for( self->model_i = 0; !RS_ParallelTask_Cancelled(&self->par); self->model_i++ )
    {
        int const* ids;
        int count;
        struct ToriRS_Location* loc;

        if( !CacheProvider_LocationHas(app->provider, self->loc_id) )
            break;
        loc = CacheProvider_LocationGet(app->provider, self->loc_id);
        count = loc ? UITreeSceneBridge_LocModelIds(loc, &ids) : 0;
        if( self->model_i >= count )
            break;
        PT_PARALLEL_AWAIT_ASSET(app->assets, AssetKey_Make(ASSET_KIND_MODEL, ids[self->model_i], 0));
    }

    /* A newer set on the widget replaced this one. */
    if( RS_ParallelTask_Cancelled(&self->par) )
        PT_EXIT(&self->pt);
    /* Its interface closed, or the node went, while it loaded. */
    idx = app_script_widget_live(app, &self->widget);
    if( idx < 0 )
        PT_EXIT(&self->pt);
    /* What did arrive: a loc or a model that never loads leaves the widget
     * drawing nothing, which is what the set wrote. */
    scene_id = UITreeSceneBridge_EnsureLocModel(&app->bridge, self->loc_id);
    if( scene_id < 0 )
        PT_EXIT(&self->pt);
    (void)UITree_SetModelAt(app->tree, idx, scene_id);
    RedrawRequest_Raise(&app->redraw_request);

    PT_END(&self->pt);
}

static void
Task_ScriptLocModel_Free(struct ToriRS_Task* base)
{
    struct Task_ScriptLocModel* self = (struct Task_ScriptLocModel*)base;
    App_ParallelTaskRelease(self->app, &self->par);
    ToriMem_Free(ToriMem_Process(), self);
}

static struct ToriRS_TaskVTable Task_ScriptLocModel_VTable = {
    .run = Task_ScriptLocModel_Run,
    .free = Task_ScriptLocModel_Free,
};

void
app_cs2_widget_loc_model_lazy(
    void* user,
    int component_id,
    int loc_id)
{
    struct App* app = (struct App*)user;
    struct Task_ScriptLocModel* task;

    assert(app);
    assert(loc_id >= 0);
    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_ScriptLocModel_VTable;
    strncpy(task->task.name, "ScriptLocModel", sizeof(task->task.name) - 1);
    task->app = app;
    task->widget = app_script_widget(app, component_id);
    task->loc_id = loc_id;
    PT_INIT(&task->pt);
    RS_Pending_CancelKey(app->pending, APP_SCRIPT_TASK_LOC_MODEL, app_script_widget_key(&task->widget));
    App_ParallelTaskAdd(
        app,
        &task->task,
        &task->par,
        APP_SCRIPT_TASK_LOC_MODEL,
        app_script_widget_key(&task->widget),
        -1);
}

/* ---- Task_ScriptObjModel -------------------------------------------------- */

struct Task_ScriptObjModel
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_ParallelTask par;
    struct App* app;
    struct AppScriptWidget widget;
    int obj_id;
    int count;
    int num_mode;
};

static int
Task_ScriptObjModel_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_ScriptObjModel* self = (struct Task_ScriptObjModel*)base;
    struct App* app = self->app;
    int32_t idx;

    (void)io;
    PT_BEGIN(&self->pt);

    /* The objtype, its count variant, its inventory model and that model's
     * textures: one composite entry (ObjModelLoad). */
    PT_PARALLEL_AWAIT_ASSET(
        app->assets, AssetKey_Make(ASSET_KIND_OBJ_MODEL, self->obj_id, self->count));

    if( RS_ParallelTask_Cancelled(&self->par) )
        PT_EXIT(&self->pt);
    idx = app_script_widget_live(app, &self->widget);
    if( idx < 0 )
        PT_EXIT(&self->pt);
    /* A later set that made no task of its own -- a clear (`cc_setobject`
     * with -1), or an obj drawn at once from its icon -- wrote the node
     * without cancelling this one. The node holds what the latest set put
     * there; this task's obj is not it any more. */
    if( app->tree->components[idx].item_id != self->obj_id ||
        app->tree->components[idx].item_count != self->count )
        PT_EXIT(&self->pt);
    /* Failed or not, finish as a script that waited would have: the model
     * from what arrived, else the obj's icon. Not the pose: the set wrote it,
     * and a cc_setmodelangle the script made after the set must stay. */
    RS_CS2Host_SetObjectModelLanded(
        &app->host, self->widget.component_id, self->obj_id, self->count, self->num_mode);
    RedrawRequest_Raise(&app->redraw_request);

    PT_END(&self->pt);
}

static void
Task_ScriptObjModel_Free(struct ToriRS_Task* base)
{
    struct Task_ScriptObjModel* self = (struct Task_ScriptObjModel*)base;
    App_ParallelTaskRelease(self->app, &self->par);
    ToriMem_Free(ToriMem_Process(), self);
}

static struct ToriRS_TaskVTable Task_ScriptObjModel_VTable = {
    .run = Task_ScriptObjModel_Run,
    .free = Task_ScriptObjModel_Free,
};

void
app_cs2_widget_obj_model_lazy(
    void* user,
    int component_id,
    int obj_id,
    int count,
    int num_mode)
{
    struct App* app = (struct App*)user;
    struct Task_ScriptObjModel* task;

    assert(app);
    assert(obj_id >= 0);
    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_ScriptObjModel_VTable;
    strncpy(task->task.name, "ScriptObjModel", sizeof(task->task.name) - 1);
    task->app = app;
    task->widget = app_script_widget(app, component_id);
    task->obj_id = obj_id;
    task->count = count;
    task->num_mode = num_mode;
    PT_INIT(&task->pt);
    RS_Pending_CancelKey(app->pending, APP_SCRIPT_TASK_OBJ_MODEL, app_script_widget_key(&task->widget));
    App_ParallelTaskAdd(
        app,
        &task->task,
        &task->par,
        APP_SCRIPT_TASK_OBJ_MODEL,
        app_script_widget_key(&task->widget),
        -1);
}

/* ---- Task_ScriptPackModels ------------------------------------------------ */

struct Task_ScriptPackModels
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_ParallelTask par;
    struct App* app;
    int group;
    uint32_t close_serial;
    int needs_player;
};

/* Count every model and npc config the pack names on the task's join (what
 * Task_PackAssetsLoad loaded before the bake, less the sprites and fonts the
 * script waited for itself). */
static void
app_script_pack_models_want(struct Task_ScriptPackModels* self)
{
    struct App* app = self->app;
    struct ToriRS_ComponentPack* pack;

    assert(self->par.join == 0);
    if( !CacheProvider_ComponentPackHas(app->provider, self->group) )
        return;
    pack = CacheProvider_ComponentPackGet(app->provider, self->group);
    assert(pack);
    for( int i = 0; i < pack->component_count; i++ )
    {
        struct ToriRS_Component const* c = &pack->components[i];
        if( c->model_type == 1 && c->model_id >= 0 )
            (void)Assets_Await(
                app->assets, AssetKey_Make(ASSET_KIND_MODEL, c->model_id, 0), &self->par.join);
        else if( c->model_type == 2 && c->model_id >= 0 )
            (void)Assets_Await(
                app->assets,
                AssetKey_Make(ASSET_KIND_CONFIG, c->model_id, ASSET_CONFIG_NPC),
                &self->par.join);
        if( c->active_model_type == 1 && c->active_model_id >= 0 )
            (void)Assets_Await(
                app->assets, AssetKey_Make(ASSET_KIND_MODEL, c->active_model_id, 0), &self->par.join);
        else if( c->active_model_type == 2 && c->active_model_id >= 0 )
            (void)Assets_Await(
                app->assets,
                AssetKey_Make(ASSET_KIND_CONFIG, c->active_model_id, ASSET_CONFIG_NPC),
                &self->par.join);
        if( c->client_code == 327 || c->client_code == 328 )
            self->needs_player = 1;
    }
}

/* The bind the bake made when the models were awaited before it
 * (task_cs2_bake_pack): every MODEL node of the group names a cache model,
 * which is put in the scene now that it is here. Walks the tree's model
 * list, not the tree. */
static void
app_script_pack_models_bind(struct Task_ScriptPackModels* self)
{
    struct App* app = self->app;
    struct UITree* tree = app->tree;
    int bound = 0;

    for( int mi = 0; mi < tree->models.count; mi++ )
    {
        int32_t idx = tree->models.slots[mi];
        struct UITreeComponent* c;
        int scene_id;

        assert(idx >= 0);
        assert((uint32_t)idx < tree->component_count);
        c = &tree->components[idx];
        if( c->type != UIELEM_RS_MODEL || ((c->component_id >> 16) & 0xffff) != self->group )
            continue;
        if( c->u.rs_model.gamecache_model_id < 0 )
            continue;
        scene_id = UITreeSceneBridge_EnsureModel(&app->bridge, c->u.rs_model.gamecache_model_id);
        if( scene_id < 0 )
            continue;
        (void)UITree_SetModelAt(tree, idx, scene_id);
        UITree_MarkNodeDirty(tree, idx);
        bound++;
    }
    if( bound )
        RedrawRequest_Raise(&app->redraw_request);
}

static int
Task_ScriptPackModels_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_ScriptPackModels* self = (struct Task_ScriptPackModels*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);

    /* Every model at once: independent records, one consumer. */
    app_script_pack_models_want(self);
    PT_PARALLEL_AWAIT_ASSETS(app->assets);
    /* A player-design preview widget composes the default kit. */
    if( self->needs_player && !RS_ParallelTask_Cancelled(&self->par) )
        PT_TASK_AWAITSELF_IF(CreateTask_PlayerAppearanceLoad(app->provider, app->assets));

    /* A newer bake of the group replaced this one, or the group closed. */
    if( RS_ParallelTask_Cancelled(&self->par) )
        PT_EXIT(&self->pt);
    if( UITree_GroupCloseSerial(app->tree, self->group) != self->close_serial )
        PT_EXIT(&self->pt);
    app_script_pack_models_bind(self);

    PT_END(&self->pt);
}

static void
Task_ScriptPackModels_Free(struct ToriRS_Task* base)
{
    struct Task_ScriptPackModels* self = (struct Task_ScriptPackModels*)base;
    App_ParallelTaskRelease(self->app, &self->par);
    ToriMem_Free(ToriMem_Process(), self);
}

static struct ToriRS_TaskVTable Task_ScriptPackModels_VTable = {
    .run = Task_ScriptPackModels_Run,
    .free = Task_ScriptPackModels_Free,
};

void
app_cs2_pack_models_lazy(
    void* user,
    int group_id)
{
    struct App* app = (struct App*)user;
    struct Task_ScriptPackModels* task;
    struct RS_PendingKey key;

    assert(app);
    assert(group_id > 0);
    assert(app->tree);
    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_ScriptPackModels_VTable;
    strncpy(task->task.name, "ScriptPackModels", sizeof(task->task.name) - 1);
    task->app = app;
    task->group = group_id;
    task->close_serial = UITree_GroupCloseSerial(app->tree, group_id);
    PT_INIT(&task->pt);
    key = RS_PendingKey_Component(group_id << 16, task->close_serial);
    RS_Pending_CancelKey(app->pending, APP_SCRIPT_TASK_PACK_MODELS, key);
    App_ParallelTaskAdd(app, &task->task, &task->par, APP_SCRIPT_TASK_PACK_MODELS, key, -1);
}

/* ---- the host's hooks ------------------------------------------------------ */

void
app_tasks_scripts_bind_host(struct App* app)
{
    assert(app);
    assert(app->assets);
    app->host.widget_loc_model_lazy = app_cs2_widget_loc_model_lazy;
    app->host.widget_obj_model_lazy = app_cs2_widget_obj_model_lazy;
    app->host.pack_models_lazy = app_cs2_pack_models_lazy;
    app->host.widget_visual_cancel = app_cs2_widget_visual_cancel;
    app->host.assets = app->assets;
}

/* ---- the latest set wins -------------------------------------------------- */

void
app_cs2_widget_visual_cancel(
    void* user,
    int component_id)
{
    struct App* app = (struct App*)user;
    struct AppScriptWidget w;

    assert(app);
    assert(app->tree);
    w = app_script_widget(app, component_id);
    (void)RS_Pending_CancelKey(app->pending, APP_SCRIPT_TASK_LOC_MODEL, app_script_widget_key(&w));
    (void)RS_Pending_CancelKey(app->pending, APP_SCRIPT_TASK_OBJ_MODEL, app_script_widget_key(&w));
}

bool
App_ScriptVisualPending(
    struct App* app,
    int kind,
    int component_id)
{
    struct RS_PendingKey key;

    assert(app);
    assert(app->tree);
    assert(kind >= APP_SCRIPT_TASK_LOC_MODEL);
    assert(kind <= APP_SCRIPT_TASK_PACK_MODELS);
    key = RS_PendingKey_Component(
        component_id, UITree_GroupCloseSerial(app->tree, (component_id >> 16) & 0xffff));
    return RS_Pending_Find(app->pending, kind, key) != NULL;
}
