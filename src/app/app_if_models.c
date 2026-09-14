/*
 * Interface models: chatheads, player-model compositions, and the tasks that
 * load them.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Interface chathead: load the npctype/appearance + head models, composite the
 * head into the scene (UITreeSceneBridge_Ensure*Head), and bind it onto the
 * MODEL widget the dialogue set (reference IfType.getModel type 2/3, resolved
 * lazily — here via the async provider). */
enum AppIfHeadKind
{
    APP_IFHEAD_NPC = 0,
    APP_IFHEAD_PLAYER,
    /* IF_SETOBJECT (reference IfType model1Type 4): the obj's lit inventory
     * model bound to a MODEL widget — e.g. the combat-tab weapon. npc_id
     * carries the obj id; zoom carries the wire zoom. */
    APP_IFHEAD_OBJ,
    /* IF_SETMODEL (reference IfType model1Type 1): npc_id carries the raw
     * cache model id. */
    APP_IFHEAD_MODEL,
};

#define APP_IFHEAD_MAX_HEADS 24

struct Task_AppIfHead
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    enum AppIfHeadKind kind;
    int component_id;
    int npc_id;
    int resolved_npc_id;
    int model_i;
    int slot_i;
    int head_ids[APP_IFHEAD_MAX_HEADS]; /* player: idk + worn-obj head model ids to load */
    int head_count;
};

/* ---- Revision-239 per-widget player compositions ----------------------- */

enum AppIfPlayerModelOp
{
    APP_IFPLAYER_SELF = 0,
    APP_IFPLAYER_BASECOLOUR,
    APP_IFPLAYER_BODYTYPE,
    APP_IFPLAYER_OBJ,
};

enum
{
    APP_IFPLAYER_MAX_MODELS = 64,
};

struct Task_AppIfPlayerModel
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    enum AppIfPlayerModelOp op;
    int component_id;
    int arg0;
    int arg1;
    int cfg_i;
    int model_i;
    int model_count;
    int model_ids[APP_IFPLAYER_MAX_MODELS];
    int slots[12];
    int colors[5];
    int gender;
    uint32_t version;
};

/* Private to this unit, declared up front so definition order is free. */
static int
Task_AppIfHead_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io);
static void
Task_AppIfHead_Free(struct ToriRS_Task* base);
static void
app_if_head_enqueue(
    struct App* app,
    enum AppIfHeadKind kind,
    int component_id,
    int npc_id);
static void
app_if_head_store(
    struct App* app,
    enum AppIfHeadKind kind,
    int com_id,
    int npc_id);
static struct AppIfPlayerModel*
app_if_player_model_find(
    struct App* app,
    int com_id);
static struct AppIfPlayerModel*
app_if_player_model_get(
    struct App* app,
    int com_id);
static int
app_if_player_model_find_kit(
    struct App* app,
    int design_part,
    int body_type);
static int
Task_AppIfPlayerModel_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io);
static void
Task_AppIfPlayerModel_Free(struct ToriRS_Task* base);
static void
app_if_player_model_enqueue(
    struct App* app,
    enum AppIfPlayerModelOp op,
    int component_id,
    int arg0,
    int arg1);

static int
Task_AppIfHead_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_AppIfHead* self = (struct Task_AppIfHead*)base;
    struct App* app = self->app;
    int scene_id = -1;

    PT_BEGIN(&self->pt);

    if( self->kind == APP_IFHEAD_NPC )
    {
        /* IF_SETNPCHEAD carries the NPC type the server is talking through.
         * For a multiNpc that is the model-less shell, just like NPC_INFO.
         * Resolve it under this client's vars before asking for chathead
         * models. The resolver also makes every selected config
         * resident, so a cold child cannot be mistaken for a terminal shell. */
        PT_TASK_AWAITSELF_IF(CreateTask_NpcMultiResolve(app, self->npc_id, &self->resolved_npc_id));
        if( self->resolved_npc_id < 0 )
            PT_EXIT(&self->pt); /* positional -1: intentionally hidden */
        /* Load each head model (re-derived from persistent model_i; -1 slots
         * are skipped — reference NpcType.getHead ignores them). */
        for( self->model_i = 0;; self->model_i++ )
        {
            struct ToriRS_Npctype* npc =
                CacheProvider_NpctypeGet(app->provider, self->resolved_npc_id);
            if( !npc || self->model_i >= npc->heads_count )
                break;
            if( npc->heads[self->model_i] < 0 )
                continue;
            PT_TASK_AWAITSELF_IF(CreateTask_ModelLoad(app->provider, npc->heads[self->model_i]));
        }
        scene_id = UITreeSceneBridge_EnsureNpcHead(&app->bridge, self->resolved_npc_id);
    }
    else if( self->kind == APP_IFHEAD_OBJ )
    {
        /* IF_SETOBJECT: objtype + its inventory model, then the lit interface
         * model (npc_id carries the obj id). */
        PT_TASK_AWAITSELF_IF(CreateTask_ObjLoad(app->provider, self->npc_id));
        {
            struct ToriRS_Objtype* obj = CacheProvider_ObjtypeGet(app->provider, self->npc_id);
            if( obj && obj->inventory_model_id > 0 )
                PT_TASK_AWAITSELF_IF(CreateTask_ModelLoad(app->provider, obj->inventory_model_id));
        }
        scene_id = UITreeSceneBridge_EnsureObjModel(&app->bridge, self->npc_id);
    }
    else if( self->kind == APP_IFHEAD_MODEL )
    {
        PT_TASK_AWAITSELF_IF(CreateTask_ModelLoad(app->provider, self->npc_id));
        scene_id = UITreeSceneBridge_EnsureModel(&app->bridge, self->npc_id);
    }
    else
    {
        /* Load the local player's real-appearance idk configs + head models,
         * then composite (reference ClientPlayer.getHeadModel). The idk configs
         * are usually already resident from the world body build; await the
         * appearance load first as a baseline. */
        PT_TASK_AWAITSELF_IF(CreateTask_PlayerAppearanceLoad(app->provider));
        /* Ensure worn-equipment obj configs are resident so their head-model
         * ids (manhead/womanhead) can be gathered below — the body build loads
         * the wear models but never the head models (reference getHeadModel
         * pulls ObjType.getHeadModelNoCheck for slots >= 512). */
        for( self->slot_i = 0; self->slot_i < 12; self->slot_i++ )
        {
            struct WorldEntity_Player* lp = app_local_player(app);
            int slot = lp ? lp->appearance.slots[self->slot_i] : 0;
            if( Appearance_SlotKind(slot) == APPEARANCE_SLOT_OBJ )
                PT_TASK_AWAITSELF_IF(CreateTask_ObjLoad(app->provider, Appearance_SlotObj(slot)));
        }
        {
            struct WorldEntity_Player* lp = app_local_player(app);
            self->head_count = lp ? PlayerHeadModel_CollectHeadModelIds(
                                        app->provider,
                                        lp->appearance.slots,
                                        lp->gender,
                                        self->head_ids,
                                        APP_IFHEAD_MAX_HEADS)
                                  : 0;
        }
        for( self->model_i = 0; self->model_i < self->head_count; self->model_i++ )
            PT_TASK_AWAITSELF_IF(
                CreateTask_ModelLoad(app->provider, self->head_ids[self->model_i]));
        {
            struct WorldEntity_Player* lp = app_local_player(app);
            if( lp )
                scene_id = UITreeSceneBridge_EnsurePlayerHead(
                    &app->bridge, lp->appearance.slots, lp->appearance.colors, lp->gender);
        }
    }

    /* Compose only — app_if_head_poll binds the scene model onto the MODEL node
     * once it is mounted (the head packet usually precedes the interface). Force
     * a redraw so the poll runs now that the head is composited. */
    if( scene_id >= 0 )
        app->need_redraw = 1;
    else if( torirs_env_net_debug() )
        TORIRS_LOG(
            "if-head: component=0x%x kind=%d could not composite head (npc=%d)\n",
            (unsigned)self->component_id,
            (int)self->kind,
            self->npc_id);

    PT_END(&self->pt);
}

static void
Task_AppIfHead_Free(struct ToriRS_Task* base)
{
    free(base);
}

static struct ToriRS_TaskVTable Task_AppIfHead_VTable = {
    .run = Task_AppIfHead_Run,
    .free = Task_AppIfHead_Free,
};

static void
app_if_head_enqueue(
    struct App* app,
    enum AppIfHeadKind kind,
    int component_id,
    int npc_id)
{
    struct Task_AppIfHead* task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_AppIfHead_VTable;
    strncpy(task->task.name, "AppIfHead", sizeof(task->task.name) - 1);
    task->app = app;
    task->kind = kind;
    task->component_id = component_id;
    task->npc_id = npc_id;
    PT_INIT(&task->pt);
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

/* Persist the head request keyed by component id (reference IfType.list keeps
 * model1Type/model1Id): re-applied by app_if_head_poll whenever the interface
 * (re)mounts, so it survives a head packet that lands before its chat interface
 * exists. Resets applied_gen so the next poll rebinds. */
static void
app_if_head_store(
    struct App* app,
    enum AppIfHeadKind kind,
    int com_id,
    int npc_id)
{
    int i;
    for( i = 0; i < app->if_head_count; i++ )
        if( app->if_heads[i].com_id == com_id )
            break;
    if( i == app->if_head_count )
    {
        if( app->if_head_count == app->if_head_cap )
        {
            int cap = app->if_head_cap ? app->if_head_cap * 2 : 16;
            app->if_heads = realloc(app->if_heads, (size_t)cap * sizeof(*app->if_heads));
            assert(app->if_heads);
            app->if_head_cap = cap;
        }
        app->if_heads[i].anim_id = -1; /* preserved across a head update (below) */
        app->if_head_count++;
    }
    app->if_heads[i].com_id = com_id;
    app->if_heads[i].kind = (int)kind;
    app->if_heads[i].npc_id = npc_id;
    app->if_heads[i].zoom = 0;
    app->if_heads[i].applied_gen = 0;
    app->need_redraw = 1;
}

/* PlayerComposition's seven design-part -> equipment-slot table
 * (Statics.method8884 / class389.field4882 in the 239 client). */
static int const app_ifplayer_design_slots[PLAYER_APPEARANCE_PARTS] = {
    8, 11, 4, 6, 9, 7, 10,
};

static struct AppIfPlayerModel*
app_if_player_model_find(
    struct App* app,
    int com_id)
{
    for( int i = 0; i < app->if_player_model_count; i++ )
        if( app->if_player_models[i].com_id == com_id )
            return &app->if_player_models[i];
    return NULL;
}

/* Modern IfType creates its PlayerComposition by cloning the local player.
 * Keep both arrays: slots is the effective render layer, while identkit is the
 * body-under-equipment layer SELF(false) and BODYTYPE restore from. */
static struct AppIfPlayerModel*
app_if_player_model_get(
    struct App* app,
    int com_id)
{
    struct AppIfPlayerModel* model = app_if_player_model_find(app, com_id);
    struct WorldEntity_Player* lp;
    int i;

    if( model )
        return model;
    lp = app_local_player(app);
    if( !lp )
        return NULL;

    i = app->if_player_model_count;
    if( i == app->if_player_model_cap )
    {
        int cap = app->if_player_model_cap ? app->if_player_model_cap * 2 : 8;
        app->if_player_models =
            realloc(app->if_player_models, (size_t)cap * sizeof(*app->if_player_models));
        assert(app->if_player_models);
        app->if_player_model_cap = cap;
    }
    model = &app->if_player_models[i];
    memset(model, 0, sizeof(*model));
    model->com_id = com_id;
    model->scene_id = UITREE_SCENE_IF_PLAYER_MODEL_BASE + i;
    model->anim_id = -1;
    memcpy(model->slots, lp->appearance.slots, sizeof(model->slots));
    memcpy(model->identkit, lp->appearance.identkit, sizeof(model->identkit));
    memcpy(model->colors, lp->appearance.colors, sizeof(model->colors));
    model->gender = lp->gender;
    app->if_player_model_count++;
    return model;
}

static int
app_if_player_model_find_kit(
    struct App* app,
    int design_part,
    int body_type)
{
    /* IdkType.method8644(part, gender): female body-part ids are male + 7;
     * every other body type selects the male band. The reference takes the
     * first selectable id in config order. */
    int body_part_id = design_part + (body_type == 1 ? PLAYER_APPEARANCE_PARTS : 0);
    for( int id = 0; id < PLAYER_IDK_SCAN_MAX; id++ )
    {
        struct ToriRS_Idk* idk;
        if( !CacheProvider_IdkHas(app->provider, id) )
            break;
        idk = CacheProvider_IdkGet(app->provider, id);
        if( idk && !idk->not_selectable && idk->body_part_id == body_part_id )
            return id;
    }
    return -1;
}

static int
Task_AppIfPlayerModel_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_AppIfPlayerModel* self = (struct Task_AppIfPlayerModel*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);

    /* BODYTYPE scans the complete idk table; OBJ needs its wearpos triplet.
     * The task sits on the serial packet executor, preserving wire order while
     * either config load yields. */
    if( self->op == APP_IFPLAYER_BODYTYPE )
        PT_TASK_AWAITSELF_IF(CreateTask_PlayerAppearanceLoad(app->provider));
    else if( self->op == APP_IFPLAYER_OBJ )
        PT_TASK_AWAITSELF_IF(CreateTask_ObjLoad(app->provider, self->arg0));

    {
        struct AppIfPlayerModel* model = app_if_player_model_get(app, self->component_id);
        struct WorldEntity_Player* lp = app_local_player(app);
        if( !model || !lp )
            PT_EXIT(&self->pt);

        if( self->op == APP_IFPLAYER_SELF )
        {
            memcpy(model->identkit, lp->appearance.identkit, sizeof(model->identkit));
            memcpy(model->colors, lp->appearance.colors, sizeof(model->colors));
            model->gender = lp->gender;
            memcpy(
                model->slots,
                self->arg0 ? lp->appearance.slots : lp->appearance.identkit,
                sizeof(model->slots));
        }
        else if( self->op == APP_IFPLAYER_BASECOLOUR )
        {
            if( self->arg0 >= 0 && self->arg0 < 5 )
                model->colors[self->arg0] = self->arg1;
        }
        else if( self->op == APP_IFPLAYER_BODYTYPE )
        {
            int body_type = self->arg0;
            if( model->gender != body_type )
            {
                model->gender = body_type;
                for( int part = 0; part < PLAYER_APPEARANCE_PARTS; part++ )
                {
                    int slot = app_ifplayer_design_slots[part];
                    /* PlayerComposition only remaps a design-kit value. Worn
                     * objs remain exactly where they are. Returning to the
                     * local player's type restores the cloned underneath kit;
                     * switching away takes the first selectable target kit. */
                    if( Appearance_SlotKind(model->slots[slot]) != APPEARANCE_SLOT_KIT )
                        continue;
                    if( body_type == lp->gender )
                    {
                        model->slots[slot] = model->identkit[slot];
                    }
                    else
                    {
                        int id = app_if_player_model_find_kit(app, part, body_type);
                        if( id >= 0 )
                            model->slots[slot] = Appearance_PackKit(id);
                    }
                }
            }
        }
        else
        {
            struct ToriRS_Objtype* obj = CacheProvider_ObjtypeGet(app->provider, self->arg0);
            if( obj && obj->wearpos >= 0 && obj->wearpos < 12 )
            {
                model->slots[obj->wearpos] = Appearance_PackObj(self->arg0);
                if( obj->wearpos2 >= 0 && obj->wearpos2 < 12 )
                    model->slots[obj->wearpos2] = 0;
                if( obj->wearpos3 >= 0 && obj->wearpos3 < 12 )
                    model->slots[obj->wearpos3] = 0;
            }
        }

        model->version++;
        if( model->version == 0 )
            model->version = 1;
        self->version = model->version;
        memcpy(self->slots, model->slots, sizeof(self->slots));
        memcpy(self->colors, model->colors, sizeof(self->colors));
        self->gender = model->gender;
    }

    /* Resolve all configs referenced by the snapshot before collecting model
     * ids. PlayerAppearanceLoad supplies the full idk table; worn objects are
     * loaded individually, then every gendered wear model is awaited. */
    PT_TASK_AWAITSELF_IF(CreateTask_PlayerAppearanceLoad(app->provider));
    for( self->cfg_i = 0; self->cfg_i < 12; self->cfg_i++ )
    {
        if( Appearance_SlotKind(self->slots[self->cfg_i]) == APPEARANCE_SLOT_OBJ )
            PT_TASK_AWAITSELF_IF(
                CreateTask_ObjLoad(app->provider, Appearance_SlotObj(self->slots[self->cfg_i])));
    }
    self->model_count = PlayerModel_CollectAppearanceModelIds(
        app->provider, self->slots, self->gender, self->model_ids, APP_IFPLAYER_MAX_MODELS);
    for( self->model_i = 0; self->model_i < self->model_count; self->model_i++ )
        PT_TASK_AWAITSELF_IF(CreateTask_ModelLoad(app->provider, self->model_ids[self->model_i]));

    {
        struct AppIfPlayerModel* model = app_if_player_model_find(app, self->component_id);
        /* A stale build must never overwrite a newer composition. This is
         * mostly defensive—the packet queue is serial—but also makes direct
         * harness calls deterministic. */
        if( model && model->version == self->version &&
            UITreeSceneBridge_BuildInterfacePlayerModel(
                &app->bridge, model->scene_id, self->slots, self->colors, self->gender) >= 0 )
        {
            model->built_version = self->version;
            model->applied_gen = 0;
            app->need_redraw = 1;
        }
    }

    PT_END(&self->pt);
}

static void
Task_AppIfPlayerModel_Free(struct ToriRS_Task* base)
{
    free(base);
}

static struct ToriRS_TaskVTable Task_AppIfPlayerModel_VTable = {
    .run = Task_AppIfPlayerModel_Run,
    .free = Task_AppIfPlayerModel_Free,
};

static void
app_if_player_model_enqueue(
    struct App* app,
    enum AppIfPlayerModelOp op,
    int component_id,
    int arg0,
    int arg1)
{
    struct Task_AppIfPlayerModel* task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_AppIfPlayerModel_VTable;
    strncpy(task->task.name, "AppIfPlayerModel", sizeof(task->task.name) - 1);
    task->app = app;
    task->op = op;
    task->component_id = component_id;
    task->arg0 = arg0;
    task->arg1 = arg1;
    PT_INIT(&task->pt);
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

void
App_SetInterfaceNpcHead(
    struct App* app,
    int component_id,
    int npc_id)
{
    assert(app);
    if( npc_id < 0 )
        return;
    app_if_head_store(app, APP_IFHEAD_NPC, component_id, npc_id);
    app_if_head_enqueue(app, APP_IFHEAD_NPC, component_id, npc_id);
}

void
App_SetInterfacePlayerHead(
    struct App* app,
    int component_id)
{
    assert(app);
    app_if_head_store(app, APP_IFHEAD_PLAYER, component_id, -1);
    app_if_head_enqueue(app, APP_IFHEAD_PLAYER, component_id, -1);
}

void
App_SetInterfacePlayerModelSelf(
    struct App* app,
    int component_id,
    int copy_objs)
{
    assert(app);
    app_if_player_model_enqueue(app, APP_IFPLAYER_SELF, component_id, copy_objs != 0, 0);
}

void
App_SetInterfacePlayerModelBaseColour(
    struct App* app,
    int component_id,
    int index,
    int colour)
{
    assert(app);
    app_if_player_model_enqueue(app, APP_IFPLAYER_BASECOLOUR, component_id, index, colour);
}

void
App_SetInterfacePlayerModelBodyType(
    struct App* app,
    int component_id,
    int body_type)
{
    assert(app);
    app_if_player_model_enqueue(app, APP_IFPLAYER_BODYTYPE, component_id, body_type, 0);
}

void
App_SetInterfacePlayerModelObj(
    struct App* app,
    int component_id,
    int obj_id)
{
    assert(app);
    if( obj_id < 0 )
        return;
    app_if_player_model_enqueue(app, APP_IFPLAYER_OBJ, component_id, obj_id, 0);
}

void
App_SetInterfaceModel(
    struct App* app,
    int component_id,
    int model_id)
{
    assert(app);
    if( model_id < 0 )
        return;
    app_if_head_store(app, APP_IFHEAD_MODEL, component_id, model_id);
    app_if_head_enqueue(app, APP_IFHEAD_MODEL, component_id, model_id);
}

void
App_SetInterfaceObjModel(
    struct App* app,
    int component_id,
    int obj_id,
    int zoom)
{
    assert(app);
    if( obj_id <= 0 )
        return;
    app_if_head_store(app, APP_IFHEAD_OBJ, component_id, obj_id);
    /* store resets zoom to 0; stamp the wire zoom for the poll's angle apply. */
    for( int i = 0; i < app->if_head_count; i++ )
        if( app->if_heads[i].com_id == component_id )
        {
            app->if_heads[i].zoom = zoom;
            break;
        }
    app_if_head_enqueue(app, APP_IFHEAD_OBJ, component_id, obj_id);
}

/* Bind any composited heads onto their MODEL nodes. Runs each redraw: an entry
 * applies once its scene model is ready (the load task has composited it) AND
 * its component is mounted, then only re-applies when the tree generation
 * changes (remount/rebuild) — mirroring the reference resolving getModel every
 * draw. Cheap: Ensure* is a cache hit after the first composite, and applied
 * entries at the current generation are skipped. */
void
app_if_head_poll(struct App* app)
{
    if( app->if_head_count == 0 || !app->tree )
        return;

    for( int i = 0; i < app->if_head_count; i++ )
    {
        struct AppIfHead* head = &app->if_heads[i];
        int scene_id;
        int first_apply;

        if( head->applied_gen == app->tree->generation )
            continue;
        /* UI mutations can advance the tree generation every tick.  The
         * binding still re-applies as required, but its diagnostic should say
         * when this stored request first became visible to the tree, not flood
         * the trace once per unrelated UI mutation. */
        first_apply = head->applied_gen == 0;

        if( head->kind == APP_IFHEAD_PLAYER )
        {
            struct WorldEntity_Player* lp = app_local_player(app);
            scene_id =
                lp ? UITreeSceneBridge_EnsurePlayerHead(
                         &app->bridge, lp->appearance.slots, lp->appearance.colors, lp->gender)
                   : -1;
        }
        else if( head->kind == APP_IFHEAD_OBJ )
        {
            scene_id = UITreeSceneBridge_EnsureObjModel(&app->bridge, head->npc_id);
        }
        else if( head->kind == APP_IFHEAD_MODEL )
        {
            scene_id = UITreeSceneBridge_EnsureModel(&app->bridge, head->npc_id);
        }
        else
        {
            /* Keep the stored id as the shell, matching IfType.model1Id, but
             * resolve it every time the binding is retried. The selected child
             * owns the actual heads/recolours and is the bridge cache key. */
            int resolved_npc_id = App_NpctypeResolveMultiId(app, head->npc_id);
            scene_id = resolved_npc_id < 0
                           ? -1
                           : UITreeSceneBridge_EnsureNpcHead(&app->bridge, resolved_npc_id);
        }
        if( scene_id < 0 )
            continue; /* assets not composited yet — retry next frame */

        if( UITree_ApplyModel(app->tree, head->com_id, scene_id) )
        {
            /* Server IF_SETNPCHEAD reaches this async App path rather than the
             * CS2 host opcode path, so mirror the latter's opt-in trace here.
             * It makes a composed-but-currently-tab-hidden portrait observable
             * without changing its render or visibility state. */
            if( first_apply && head->kind == APP_IFHEAD_NPC && getenv("TORIRS_NPC_HEAD_DEBUG") )
                TORIRS_LOG(
                    "npc_head: npc=%d component=0x%08x scene=%d applied=1\n",
                    head->npc_id,
                    (unsigned)head->com_id,
                    scene_id);
            /* Reference IF_SETOBJECT: modelXAn/YAn from the objtype, modelZoom
             * = zoom2d * 100 / wire zoom (Client.ts:6342). */
            if( head->kind == APP_IFHEAD_OBJ )
            {
                struct ToriRS_Objtype* obj = CacheProvider_ObjtypeGet(app->provider, head->npc_id);
                if( obj && head->zoom > 0 )
                    UITree_ApplyModelAngle(
                        app->tree,
                        head->com_id,
                        obj->xan2d,
                        obj->yan2d,
                        (obj->zoom2d > 0 ? obj->zoom2d : 2000) * 100 / head->zoom);
            }
            if( head->anim_id >= 0 )
                UITree_ApplyModelAnim(app->tree, head->com_id, head->anim_id);
            head->applied_gen = app->tree->generation;
        }
        else if( torirs_env_net_debug() )
            TORIRS_LOG(
                "if-head: reapply com=%d npc=%d gen=%u missed (node not mounted?)\n",
                head->com_id,
                head->npc_id,
                app->tree->generation);
    }
}

/* Bind completed per-widget compositions after the target interface mounts,
 * and again after every tree rebuild. The composition stays in its own scene
 * slot; applying it only changes the addressed IfType. */
void
app_if_player_model_poll(struct App* app)
{
    if( !app->tree )
        return;
    for( int i = 0; i < app->if_player_model_count; i++ )
    {
        struct AppIfPlayerModel* model = &app->if_player_models[i];
        if( model->built_version == 0 || model->built_version != model->version ||
            model->applied_gen == app->tree->generation )
            continue;
        if( UITree_ApplyModel(app->tree, model->com_id, model->scene_id) )
        {
            if( model->anim_id >= 0 )
                UITree_ApplyModelAnim(app->tree, model->com_id, model->anim_id);
            model->applied_gen = app->tree->generation;
        }
        else if( torirs_env_net_debug() )
            TORIRS_LOG(
                "if-player-model: reapply com=%d scene=%d gen=%u missed\n",
                model->com_id,
                model->scene_id,
                app->tree->generation);
    }
}

/*
 * Bind clientCode-328 MODEL widgets (the equipment-stats figure) to the LIVE
 * local player.
 *
 * The bake path (uitree_builder_bake / task_interface_open) composites a default
 * avatar for these nodes and pins it at readyanim frame 0, because at bake time
 * there is no player yet. That default is right for the character-design preview
 * (clientCode 327, which the reference genuinely poses once) and wrong here: 328
 * names the player, so it must wear what the player wears and move like them.
 *
 * Reference is xrsps `src/ui/gl/widgets-gl.ts`, which handles 327 and 328 in one
 * block and gives them the same viewing angles:
 *
 *     const angleX = 150;
 *     const angleY = ((Math.sin(cycleCntr / 40.0) * 256.0) | 0) & 2047;
 *     const angleZ = 0;
 *
 * — 84:4 ships `angles=(0,0,0)` in the cache, so without the override the figure
 * is viewed dead-on and stands perfectly still. The xAn is what tilts the camera
 * down onto it and the yAn swings it ±256/2048 (±45°) on a ~5s period. This is
 * exactly what `RS_ClientCode_Tick` already does for 327, but that pass is gated
 * to `APP_UI_LOGIC_CS1` and rev 230 is CS2, so 328 has to get it here.
 *
 * Its animation is the player's own **movement** track, frame included, not an
 * independently-ticked idle (xrsps: `getMovementSequenceState(localServerId)`
 * feeding `sequenceId` + `liveMovementFrame`). Reading the frame off the entity
 * every tick is also what stops the figure flickering when you equip something:
 * an appearance change rebuilds the composite, and a fresh composite is
 * registered in its rest pose, so a widget running its own frame clock would
 * restart from 0 and show that rest pose. Here the very next statement in
 * app_logic_tick — UITreeAnim_Advance — poses the new model at the entity's
 * current frame, in the same tick, before anything draws it.
 *
 * Runs each tick (not each redraw): the oscillation has to keep going on a
 * frame nothing else dirtied, and marking the node dirty is what asks for the
 * redraw. Re-merging is gated on the appearance actually changing, so the
 * steady state is two memcmps.
 */
void
app_player_model_poll(struct App* app)
{
    struct WorldEntity_Player* lp;
    int changed;
    int scene_id;
    int seq_id;
    int seq_frame;
    int yan;
    int bound = 0;

    if( !app->tree || !app->world )
        return;
    lp = app_local_player(app);
    if( !lp )
        return; /* offline / not spawned yet — the baked default avatar stands */

    changed =
        !app->player_model.built || app->player_model.gender != lp->gender ||
        memcmp(app->player_model.slots, lp->appearance.slots, sizeof(app->player_model.slots)) !=
            0 ||
        memcmp(app->player_model.colors, lp->appearance.colors, sizeof(app->player_model.colors)) !=
            0;

    if( changed )
    {
        scene_id = UITreeSceneBridge_BuildLocalPlayerModel(
            &app->bridge, lp->appearance.slots, lp->appearance.colors, lp->gender);
        if( scene_id < 0 )
            return; /* nothing composited yet — retry next frame */
        memcpy(app->player_model.slots, lp->appearance.slots, sizeof(app->player_model.slots));
        memcpy(app->player_model.colors, lp->appearance.colors, sizeof(app->player_model.colors));
        app->player_model.gender = lp->gender;
        app->player_model.built = 1;
    }
    else
    {
        scene_id = app->bridge.local_player_scene_id;
        if( scene_id < 0 )
            return;
    }

    /* The entity's movement track — the one the walk/run/idle seqs live on, and
     * the one the viewport model is playing. Its readyanim is the fallback for
     * the window between spawn and the first cycle that stamps the track. */
    if( lp->animation.secondary.anim_id != (uint16_t)-1 && lp->animation.secondary.anim_id != 0 )
    {
        seq_id = lp->animation.secondary.anim_id;
        seq_frame = lp->animation.secondary.frame;
    }
    else
    {
        seq_id = lp->idle_animations.readyanim >= 0 ? lp->idle_animations.readyanim
                                                    : APP_PLAYER_SEQ_READY;
        seq_frame = 0;
    }

    /* Reference angles (above). loop_cycle is the client cycle counter, so this
     * is the same swing the design preview gets from RS_ClientCode_Tick. */
    yan = ((int)(sin((double)app->logic_cycle / 40.0) * 256.0)) & 0x7ff;

    for( int mi = 0; mi < app->tree->client_code.count; mi++ )
    {
        int32_t i = app->tree->client_code.slots[mi];
        struct UITreeComponent* node;
        assert(i >= 0 && (uint32_t)i < app->tree->component_count);
        node = &app->tree->components[i];
        if( node->freed || node->type != UIELEM_RS_MODEL )
            continue;
        if( node->behavior.client_code != UITREE_CLIENT_CODE_LOCAL_PLAYER_MODEL )
            continue;
        int const anim_changed =
            node->u.rs_model.anim_frame != seq_frame || node->u.rs_model.anim_seq_id != seq_id;
        if( node->u.rs_model.gamecache_model_id != scene_id || node->u.rs_model.xan != 150 ||
            node->u.rs_model.yan != yan || node->u.rs_model.zan != 0 || anim_changed )
            app->need_redraw = 1;
        (void)UITree_SetModelAt(app->tree, i, scene_id);
        (void)UITree_SetModelPoseAt(
            app->tree, i, node->u.rs_model.x_offset, node->u.rs_model.y_offset, 150, yan, 0, 0);
        /* The entity owns this clock; the UI driver must not advance it again. */
        (void)UITree_SetModelAnimationAt(app->tree, i, seq_id, seq_frame, 0, 1);
        bound = 1;
    }

    if( changed && getenv("TORIRS_ANIM_DEBUG") )
        TORIRS_LOG(
            "player_model: rebuilt cycle=%llu scene=%d seq=%d frame=%d bound=%d\n",
            (unsigned long long)app->logic_cycle,
            scene_id,
            seq_id,
            seq_frame,
            bound);
}

void
App_SetInterfaceModelAnim(
    struct App* app,
    int component_id,
    int anim_id)
{
    assert(app);
    /* Persist onto a matching head entry so it re-applies with the head after a
     * (re)mount (reference modelAnim lives on the same IfType as the head), and
     * apply immediately for the already-mounted / plain-model-widget case. */
    for( int i = 0; i < app->if_head_count; i++ )
    {
        if( app->if_heads[i].com_id == component_id )
        {
            app->if_heads[i].anim_id = anim_id;
            app->if_heads[i].applied_gen = 0;
            app->need_redraw = 1;
            break;
        }
    }
    for( int i = 0; i < app->if_player_model_count; i++ )
    {
        if( app->if_player_models[i].com_id == component_id )
        {
            app->if_player_models[i].anim_id = anim_id;
            app->if_player_models[i].applied_gen = 0;
            app->need_redraw = 1;
            break;
        }
    }
    UITree_ApplyModelAnim(app->tree, component_id, anim_id);
}
