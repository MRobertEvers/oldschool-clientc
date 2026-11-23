/*
 * Interface models: the chathead and model stores and their bind, and the
 * player-model compositions and the tasks that load them. The tasks that load
 * a chathead, an IF_SETMODEL or an IF_SETOBJECT are the interface-assets
 * family's (Task_IfModel, app/app_tasks_if_assets.c).
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"
#include "torirs_env.h"
#include "mem/torirs_mem.h"

/* Interface chathead: the store remembers what the server told a MODEL
 * widget to show (reference IfType model1Type/model1Id), a parallel task loads
 * and composites it (Task_IfModel), and app_if_head_poll binds the composite
 * onto the widget whenever the tree moves. What a head shows is
 * enum UIIfHeadKind (ui/uitree_if_models.h). */

/* ---- Revision-239 per-widget player compositions ----------------------- */

enum AppIfPlayerModelOp
{
    APP_IFPLAYER_SELF = 0,
    APP_IFPLAYER_BASECOLOUR,
    APP_IFPLAYER_BODYTYPE,
    APP_IFPLAYER_OBJ,
};

/* A player-model op whose config is not resident yet: it waits for it on
 * the packet queue (the composition needs it, and the next op on the widget
 * starts from this one's), then composes. Nothing else of the op waits there. */
struct Task_AppIfPlayerModel
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    enum AppIfPlayerModelOp op;
    int component_id;
    int arg0;
    int arg1;
    /* BODYTYPE: the idk id being scanned, and the load it waits on (the
     * assets table's count). */
    int idk_i;
    int join;
};

/* Private to this unit, declared up front so definition order is free. */
static int
app_if_head_store(
    struct App* app,
    enum UIIfHeadKind kind,
    int com_id,
    int npc_id,
    int zoom);
static struct UIIfPlayerModel*
app_if_player_model_find(
    struct App* app,
    int com_id);
static struct UIIfPlayerModel*
app_if_player_model_get(
    struct App* app,
    int com_id);
static int
Task_AppIfPlayerModel_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io);
static void
Task_AppIfPlayerModel_Free(struct ToriRS_Task* base);
static void
app_if_player_model_compose(
    struct App* app,
    enum AppIfPlayerModelOp op,
    int component_id,
    int arg0,
    int arg1);
static void
app_if_player_model_op(
    struct App* app,
    enum AppIfPlayerModelOp op,
    int component_id,
    int arg0,
    int arg1);

static uint32_t
app_if_close_serial_now(struct App const* app)
{
    return app->tree ? app->tree->group_close_serial : 0;
}

/* Was this entry set before its group was last closed? */
static int
app_if_entry_closed(
    struct App const* app,
    int com_id,
    uint32_t close_serial)
{
    if( !app->tree || com_id < 0 )
        return 0;
    return UITree_GroupCloseSerial(app->tree, (com_id >> 16) & 0xffff) > close_serial;
}

/*
 * Forget what a close unloaded.
 *
 * The reference's IF_CLOSESUB is closeInterface(.., true): the group's IfType
 * list is dropped, and with it every model1Id an IF_SETMODEL / IF_SETNPCHEAD /
 * IF_SETOBJECT / IF_SETPLAYERMODEL_* wrote, so the next open draws the cache's
 * components again. These stores outlived the close and rebound everything on
 * the remount: the Varrock Museum plaque (interface 533) puts all 21 creature
 * viewports on one spot and Jagex sets only the shown one, so every creature
 * an earlier plaque had shown came back on top of it (2026-10-23). A request
 * made after the close -- the plaque's own IF_SETMODEL, sent before its
 * IF_OPENSUB -- stays.
 */
static void
app_if_forget_closed(struct App* app)
{
    struct UIIfModels* models = &app->if_models;
    int i = 0;
    while( i < models->head_count )
    {
        if( !app_if_entry_closed(app, models->heads[i].com_id, models->heads[i].close_serial) )
        {
            i++;
            continue;
        }
        App_IfModelCancel(app, models->heads[i].com_id);
        UIIfModels_RemoveHeadAt(models, i);
    }
    /* Player compositions own their scene slot by index: free the slot, keep it. */
    for( i = 0; i < models->player_model_count; i++ )
    {
        struct UIIfPlayerModel const* model = &models->player_models[i];
        if( app_if_entry_closed(app, model->com_id, model->close_serial) )
            UIIfModels_FreePlayerModelAt(models, i);
    }
}

/* Record on `head` the npc it follows and what of it the head was set from
 * (UIIfHead.active_npc_slot). */
static void
app_if_head_follow(
    struct UIIfHead* head,
    struct WorldEntity_NPC const* npc)
{
    head->active_npc_slot = npc->server_slot;
    head->active_npc_spawn_serial = npc->spawn_serial;
    head->active_npc_id = npc->npc_id;
    head->active_custom_serial = npc->head_custom.active ? npc->head_custom.serial : 0;
    head->active_mirror = npc->head_custom.active && npc->head_custom.mirror;
}

/* An active head whose npc changed what it shows -- a chathead customisation
 * set, replaced or reset, a mirror turned on or off, a retype -- is set again
 * from the npc as it is now, by app_if_head_wants (before the frame's last
 * pump: the re-set adds a head task). The npc gone, or another in its slot: the head
 * keeps what it shows. */
static void
app_if_heads_follow(struct App* app)
{
    if( !app->world )
        return;
    for( int i = 0; i < app->if_models.head_count; i++ )
    {
        struct UIIfHead const* head = &app->if_models.heads[i];
        struct WorldEntity_NPC const* npc;
        int world_idx;

        if( head->active_npc_slot < 0 )
            continue;
        if( !RS_EntitySync_FindNpc(&app->esync, head->active_npc_slot, &world_idx, NULL) )
            continue;
        npc = World_EntityPoolGet(&app->world->entities.npc, world_idx);
        if( !npc || npc->spawn_serial != head->active_npc_spawn_serial )
            continue;
        if( npc->npc_id == head->active_npc_id &&
            (npc->head_custom.active ? npc->head_custom.serial : 0) == head->active_custom_serial &&
            (npc->head_custom.active && npc->head_custom.mirror) == (head->active_mirror != 0) )
            continue;
        App_SetInterfaceNpcHeadActive(app, head->com_id, world_idx);
    }
}

/* Persist the head request keyed by component id (reference IfType.list keeps
 * model1Type/model1Id): re-applied by app_if_head_poll whenever the interface
 * (re)mounts, so it survives a head packet that lands before its chat interface
 * exists -- until the interface is closed (app_if_forget_closed). Resets
 * applied_gen so the next poll rebinds.
 *
 * Then its task: a head that changed, whose last task has not landed, or
 * whose last task found nothing to show, gets a new one (the older is
 * cancelled -- the newest set wins). The same head set again once it has
 * landed a composite keeps it and is only bound again: a script setting a
 * widget's model on every timer tick asks for nothing. */
static int
app_if_head_store(
    struct App* app,
    enum UIIfHeadKind kind,
    int com_id,
    int npc_id,
    int zoom)
{
    struct UIIfHead* head;
    int i;

    app_if_forget_closed(app);
    /* The latest set wins over a script's visual task too: a loc or obj model
     * (or a pack's models) still loading for this widget would bind over this
     * head when it lands (app_tasks_scripts.c). A script's own set reaches
     * here after the host has cancelled them; this is the server's set. */
    if( app->tree )
        app_cs2_widget_visual_cancel(app, com_id);
    i = UIIfModels_HeadIndex(&app->if_models, com_id);
    head = i >= 0 ? &app->if_models.heads[i] : NULL;
    RedrawRequest_Raise(&app->redraw_request);
    if( head && head->landed && head->scene_id >= 0 && head->kind == (int)kind &&
        head->npc_id == npc_id && head->zoom == zoom && head->custom_serial == 0 )
    {
        head->close_serial = app_if_close_serial_now(app);
        head->applied_gen = 0;
        /* This set names a type, a model or an obj, not an npc: the head
         * follows nothing now (App_SetInterfaceNpcHeadActive records it again
         * after this when it is an active set). */
        head->active_npc_slot = -1;
        return 0;
    }
    App_IfModelCancel(app, com_id);
    (void)UIIfModels_SetHead(&app->if_models, com_id, kind, npc_id, zoom, app_if_close_serial_now(app));
    App_IfModelWant(app, com_id);
    return 1;
}

int
app_if_head_current(
    struct App* app,
    int com_id,
    enum UIIfHeadKind kind,
    int npc_id,
    int zoom,
    uint32_t close_serial)
{
    assert(app);
    app_if_forget_closed(app);
    return UIIfModels_HeadFor(&app->if_models, com_id, kind, npc_id, zoom, close_serial);
}

static struct UIIfPlayerModel*
app_if_player_model_find(
    struct App* app,
    int com_id)
{
    int const i = UIIfModels_PlayerModelIndex(&app->if_models, com_id);
    return i >= 0 ? &app->if_models.player_models[i] : NULL;
}

/* Modern IfType creates its PlayerComposition by cloning the local player.
 * Keep both arrays: slots is the effective render layer, while identkit is the
 * body-under-equipment layer SELF(false) and BODYTYPE restore from. */
static struct UIIfPlayerModel*
app_if_player_model_get(
    struct App* app,
    int com_id)
{
    struct UIIfPlayerModel* model;
    struct WorldEntity_Player* lp;

    app_if_forget_closed(app);
    model = app_if_player_model_find(app, com_id);
    if( model )
        return model;
    lp = app_local_player(app);
    if( !lp )
        return NULL;

    model = UIIfModels_ClaimPlayerModel(
        &app->if_models, com_id, UITREE_SCENE_IF_PLAYER_MODEL_BASE, app_if_close_serial_now(app));
    memcpy(model->slots, lp->appearance.slots, sizeof(model->slots));
    memcpy(model->identkit, lp->appearance.identkit, sizeof(model->identkit));
    memcpy(model->colors, lp->appearance.colors, sizeof(model->colors));
    model->gender = lp->gender;
    return model;
}

int
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

/*
 * IF_SETPLAYERMODEL_*: the composition in wire order, the build later.
 *
 * What a later packet reads is the COMPOSITION: the next op on the widget
 * starts from whatever this one committed. So the op is applied to the
 * widget's record here, by the packet, in wire order. Loading the wear
 * models and building only changes what is drawn; that is a parallel task
 * (App_IfPlayerModelBuildAdd, app/app_tasks_mounts.c) numbered with the
 * composition, so a build of an older composition never replaces a newer
 * one, and the widget keeps its last built model until the new one is
 * built. Before, the whole op -- its config, every wear model -- held the
 * packet queue, and a composition whose parts did not all load was not
 * committed at all.
 */
static void
app_if_player_model_compose(
    struct App* app,
    enum AppIfPlayerModelOp op,
    int component_id,
    int arg0,
    int arg1)
{
    struct UIIfPlayerModel* model = app_if_player_model_get(app, component_id);
    struct WorldEntity_Player* lp = app_local_player(app);

    /* No local player to clone the widget's composition from: the reference
     * has none to show either. */
    if( !model || !lp )
        return;

    if( op == APP_IFPLAYER_SELF )
    {
        memcpy(model->identkit, lp->appearance.identkit, sizeof(model->identkit));
        memcpy(model->colors, lp->appearance.colors, sizeof(model->colors));
        model->gender = lp->gender;
        memcpy(
            model->slots, arg0 ? lp->appearance.slots : lp->appearance.identkit, sizeof(model->slots));
    }
    else if( op == APP_IFPLAYER_BASECOLOUR )
    {
        if( arg0 >= 0 && arg0 < 5 )
            model->colors[arg0] = arg1;
    }
    else if( op == APP_IFPLAYER_BODYTYPE )
    {
        int body_type = arg0;
        if( model->gender != body_type )
        {
            model->gender = body_type;
            for( int part = 0; part < PLAYER_APPEARANCE_PARTS; part++ )
            {
                int slot = PlayerModel_DesignPartWearpos(part);
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
        struct ToriRS_Objtype* obj = CacheProvider_ObjtypeGet(app->provider, arg0);
        if( obj && obj->wearpos >= 0 && obj->wearpos < 12 )
        {
            model->slots[obj->wearpos] = Appearance_PackObj(arg0);
            if( obj->wearpos2 >= 0 && obj->wearpos2 < 12 )
                model->slots[obj->wearpos2] = 0;
            if( obj->wearpos3 >= 0 && obj->wearpos3 < 12 )
                model->slots[obj->wearpos3] = 0;
        }
    }

    model->composition = ++app->if_models.composition_serial;
    App_IfPlayerModelBuildAdd(app, model);
}

static int
Task_AppIfPlayerModel_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_AppIfPlayerModel* self = (struct Task_AppIfPlayerModel*)base;
    struct App* app = self->app;

    (void)io;
    PT_BEGIN(&self->pt);

    /* BODYTYPE scans the complete idk table; OBJ needs its wearpos triplet.
     * Both are REQUIRED: the composition is what the next op starts from, so
     * this waits on the packet queue, in wire order. The idk CONFIGS only:
     * the default appearance's kit models (what CreateTask_PlayerAppearanceLoad
     * also fetched) are not what BODYTYPE draws, and what it draws is the
     * build's (Task_IfPlayerModelBuild, a parallel task). Kit ids are
     * contiguous from 0, so the first that does not load ends the table. */
    if( self->op == APP_IFPLAYER_BODYTYPE )
    {
        for( self->idk_i = 0; self->idk_i < PLAYER_IDK_SCAN_MAX; self->idk_i++ )
        {
            if( CacheProvider_IdkHas(app->provider, self->idk_i) )
                continue;
            (void)Assets_Await(
                app->assets,
                AssetKey_Make(ASSET_KIND_CONFIG, self->idk_i, ASSET_CONFIG_IDK),
                &self->join);
            TASK_AWAIT_ASSETS(&self->task, &self->pt, self->join);
            if( !CacheProvider_IdkHas(app->provider, self->idk_i) )
                break;
        }
    }
    else
    {
        (void)Assets_Await(
            app->assets, AssetKey_Make(ASSET_KIND_CONFIG, self->arg0, ASSET_CONFIG_OBJ), &self->join);
        TASK_AWAIT_ASSETS(&self->task, &self->pt, self->join);
    }
    app_if_player_model_compose(app, self->op, self->component_id, self->arg0, self->arg1);

    PT_END(&self->pt);
}

static void
Task_AppIfPlayerModel_Free(struct ToriRS_Task* base)
{
    struct Task_AppIfPlayerModel* self = (struct Task_AppIfPlayerModel*)base;
    /* Freed while an idk config loads (a queue freed at shutdown): the
     * table stops counting it on this task. */
    if( self->join > 0 )
        Assets_Unjoin(self->app->assets, &self->join);
    ToriMem_Free(ToriMem_Process(), base);
}

static struct ToriRS_TaskVTable Task_AppIfPlayerModel_VTable = {
    .run = Task_AppIfPlayerModel_Run,
    .free = Task_AppIfPlayerModel_Free,
};

/* Compose now when the op's config is here (SELF and BASECOLOUR need none);
 * otherwise wait for it on the packet queue first. */
static void
app_if_player_model_op(
    struct App* app,
    enum AppIfPlayerModelOp op,
    int component_id,
    int arg0,
    int arg1)
{
    struct Task_AppIfPlayerModel* task;

    /* The latest set wins over a script's visual task, as in the head store:
     * a loc or obj model a script set still loading for this widget would
     * bind over the composition when it lands. Cancelled here, in wire
     * order, whether the op composes now or waits for its config. */
    if( app->tree )
        app_cs2_widget_visual_cancel(app, component_id);
    if( op == APP_IFPLAYER_SELF || op == APP_IFPLAYER_BASECOLOUR ||
        (op == APP_IFPLAYER_OBJ && (CacheProvider_ObjtypeHas(app->provider, arg0) ||
                                    CacheProvider_ObjtypeAbsent(app->provider, arg0))) )
    {
        app_if_player_model_compose(app, op, component_id, arg0, arg1);
        return;
    }
    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_AppIfPlayerModel_VTable;
    strncpy(task->task.name, "AppIfPlayerModel", sizeof(task->task.name) - 1);
    task->app = app;
    task->op = op;
    task->component_id = component_id;
    task->arg0 = arg0;
    task->arg1 = arg1;
    PT_INIT(&task->pt);
    TaskRunner_AddRenderBlockingSerialTask(&app->packet_runner, &task->task);
}

/* The CS2 host's lazy-model hooks (RS_CS2Host.widget_model_lazy and
 * widget_npc_head_lazy): the same store-then-land path the IF1 setters use,
 * reached through the host's world_user. */
void
app_cs2_widget_model_lazy(
    void* user,
    int component_id,
    int model_id)
{
    assert(user);
    App_SetInterfaceModel((struct App*)user, component_id, model_id);
}

void
app_cs2_widget_npc_head_lazy(
    void* user,
    int component_id,
    int npc_id)
{
    assert(user);
    App_SetInterfaceNpcHead((struct App*)user, component_id, npc_id);
}

/* RS_CS2Host.widget_model_direct: the script bound a resident model itself, so
 * whatever this layer still holds for the widget is an older set (reference
 * IfType keeps one model1Type/model1Id; the latest write wins). Forget it, or
 * the next tree generation re-binds it over the newer one. */
void
app_cs2_widget_model_direct(
    void* user,
    int component_id)
{
    struct App* app = (struct App*)user;

    assert(app);
    /* The head entry goes, and its task with it; a player-model slot is freed
     * as a close frees it, and keeps its scene id. */
    App_IfModelCancel(app, component_id);
    UIIfModels_Forget(&app->if_models, component_id);
}

/*
 * A plugin's owned MODEL control: the scene model for (kind, id) if it is
 * composited now, else -1 -- and, with `request`, the same load-and-composite
 * task an IF_SET* head queues (Task_AppIfHead, keyed by no component: the
 * plugin bridge binds the result itself when this answers >= 0). `kind` is
 * an enum UIIfHeadKind; `id` is ignored for the player.
 */
int
App_PluginModelScene(
    struct App* app,
    int kind,
    int id,
    bool request)
{
    int scene_id = -1;

    assert(app);
    if( kind == UI_IFHEAD_NPC )
        scene_id = UITreeSceneBridge_EnsureNpcHead(&app->bridge, id);
    else if( kind == UI_IFHEAD_OBJ )
        scene_id = UITreeSceneBridge_EnsureObjModel(&app->bridge, id);
    else if( kind == UI_IFHEAD_MODEL )
        scene_id = UITreeSceneBridge_EnsureModel(&app->bridge, id);
    else
    {
        struct WorldEntity_Player* lp = app_local_player(app);
        if( lp )
            scene_id = UITreeSceneBridge_EnsurePlayerHead(
                &app->bridge, lp->appearance.slots, lp->appearance.colors, lp->gender);
    }
    if( scene_id < 0 && request )
        app_if_head_enqueue(app, (enum UIIfHeadKind)kind, -1, id);
    return scene_id;
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
    (void)app_if_head_store(app, UI_IFHEAD_NPC, component_id, npc_id, 0);
}

void
App_SetInterfaceNpcHeadCustom(
    struct App* app,
    int component_id,
    int world_idx)
{
    struct WorldEntity_NPC const* npc;
    struct UIIfHead* head;
    int i;

    assert(app);
    assert(app->world);
    npc = World_EntityPoolGet(&app->world->entities.npc, world_idx);
    assert(npc);
    assert(npc->head_custom.active);
    app_if_forget_closed(app);
    if( app->tree )
        app_cs2_widget_visual_cancel(app, component_id);
    i = UIIfModels_HeadIndex(&app->if_models, component_id);
    head = i >= 0 ? &app->if_models.heads[i] : NULL;
    RedrawRequest_Raise(&app->redraw_request);
    /* The same override of the same npc, already composited: bind again. */
    if( head && head->landed && head->scene_id >= 0 && head->kind == UI_IFHEAD_NPC &&
        head->npc_id == npc->npc_id && head->custom_serial == npc->head_custom.serial )
    {
        head->close_serial = app_if_close_serial_now(app);
        head->applied_gen = 0;
        app_if_head_follow(head, npc);
        return;
    }
    App_IfModelCancel(app, component_id);
    i = UIIfModels_SetHead(&app->if_models, component_id, UI_IFHEAD_NPC, npc->npc_id, 0, app_if_close_serial_now(app));
    app->if_models.heads[i].custom_serial = npc->head_custom.serial;
    app->if_models.heads[i].custom_npc_slot = npc->server_slot;
    app->if_models.heads[i].custom_npc_spawn_serial = npc->spawn_serial;
    app_if_head_follow(&app->if_models.heads[i], npc);
    App_IfModelWant(app, component_id);
}

void
App_SetInterfaceNpcHeadActive(
    struct App* app,
    int component_id,
    int world_idx)
{
    struct WorldEntity_NPC const* npc;
    int i;

    assert(app);
    assert(app->world);
    npc = World_EntityPoolGet(&app->world->entities.npc, world_idx);
    assert(npc);
    /* A chathead customisation that mirrors (HEAD_CUSTOM 0x10) shows the
     * local player's head (deob class308.method7279: model type 6 with
     * field5094 is type 3). */
    if( npc->head_custom.active && npc->head_custom.mirror )
        App_SetInterfacePlayerHead(app, component_id);
    else if( npc->head_custom.active )
    {
        App_SetInterfaceNpcHeadCustom(app, component_id, world_idx);
        return;
    }
    else
        App_SetInterfaceNpcHead(app, component_id, npc->npc_id);
    i = UIIfModels_HeadIndex(&app->if_models, component_id);
    assert(i >= 0);
    app_if_head_follow(&app->if_models.heads[i], npc);
}

void
App_SetInterfacePlayerHead(
    struct App* app,
    int component_id)
{
    struct WorldEntity_Player* lp;

    assert(app);
    /* A transmogged local player's chathead is the npc's: LostCity
     * Client.java:7086 (IF_SETPLAYERHEAD) binds `transmog.id + 305419896`,
     * the npc-head model key, when `localPlayer.transmog != null`, and
     * ClientPlayer.getHeadModel answers `transmog.getHeadModel()`; the deob's
     * PlayerAppearance.getChatHeadId/getModelData do the same. Resolved when
     * the packet lands, as the reference's model1Id is. */
    lp = app_local_player(app);
    if( lp && lp->transmog_npc_id >= 0 )
    {
        App_SetInterfaceNpcHead(app, component_id, lp->transmog_npc_id);
        return;
    }
    (void)app_if_head_store(app, UI_IFHEAD_PLAYER, component_id, -1, 0);
}

void
App_SetInterfacePlayerModelSelf(
    struct App* app,
    int component_id,
    int copy_objs)
{
    assert(app);
    app_if_player_model_op(app, APP_IFPLAYER_SELF, component_id, copy_objs != 0, 0);
}

void
App_SetInterfacePlayerModelBaseColour(
    struct App* app,
    int component_id,
    int index,
    int colour)
{
    assert(app);
    app_if_player_model_op(app, APP_IFPLAYER_BASECOLOUR, component_id, index, colour);
}

void
App_SetInterfacePlayerModelBodyType(
    struct App* app,
    int component_id,
    int body_type)
{
    assert(app);
    app_if_player_model_op(app, APP_IFPLAYER_BODYTYPE, component_id, body_type, 0);
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
    app_if_player_model_op(app, APP_IFPLAYER_OBJ, component_id, obj_id, 0);
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
    (void)app_if_head_store(app, UI_IFHEAD_MODEL, component_id, model_id, 0);
}

void
App_SetInterfaceObjModel(
    struct App* app,
    int component_id,
    int obj_id,
    int zoom)
{
    assert(app);
    if( obj_id < 0 ) /* IF_SETOBJECT's none is -1; obj 0 is an item */
        return;
    /* The wire zoom, for the poll's angle apply. */
    (void)app_if_head_store(app, UI_IFHEAD_OBJ, component_id, obj_id, zoom);
}

/* IF_SETOBJECT has two branches in the rev-239 client (deob client.java
 * method2413, the packet-106 arm), chosen by the component's if3 flag:
 *
 *   IF1: modelXAn/YAn from the objtype, modelZoom = zoom2d * 100 / the wire's
 *        third field, which is a ZOOM.
 *   IF3: the wire's third field is a QUANTITY -- the count variant of the obj
 *        is drawn (ObjType.getCountObj) -- and the widget takes that variant's
 *        whole 2D view: x/y/z angles, offsetX2d/offsetY2d, and
 *        modelZoom = zoom2d * 32 / the component's width (else its height),
 *        and the obj itself on the component, as CC_SETOBJECT stores it.
 *
 * The IF3 half was missing: an IF3 item model (objectbox 193, the chatbox's
 * one-item box) drew with no 2D offsets and an IF1 zoom, so an obj whose icon
 * is offset -- logs among them -- sat off the centre of its box. */
static struct UITreeComponent const*
app_if_obj_node(
    struct App const* app,
    int com_id)
{
    int32_t idx = UITree_FindByComponentId(app->tree, com_id);
    return idx >= 0 ? &app->tree->components[idx] : NULL;
}

/* The obj an IF3 widget draws for `count` (ObjType.getCountObj). */
int
app_if_obj_count_variant(
    struct App* app,
    int obj_id,
    int count)
{
    struct ToriRS_Objtype* obj = CacheProvider_ObjtypeGet(app->provider, obj_id);
    int variant = -1;

    if( !obj || count <= 1 )
        return obj_id;
    for( int i = 0; i < 10; i++ )
        if( obj->count_co[i] != 0 && count >= obj->count_co[i] )
            variant = obj->count_obj[i];
    return variant > 0 ? variant : obj_id;
}

/* The obj the widget draws: the count variant on an IF3 component. */
static int
app_if_obj_shown(
    struct App* app,
    struct UIIfHead const* head)
{
    struct UITreeComponent const* node = app_if_obj_node(app, head->com_id);
    if( node && node->if3 )
        return app_if_obj_count_variant(app, head->npc_id, head->zoom);
    return head->npc_id;
}

static void
app_if_obj_pose(
    struct App* app,
    struct UIIfHead const* head)
{
    struct UITreeComponent const* node = app_if_obj_node(app, head->com_id);
    struct ToriRS_Objtype* obj =
        CacheProvider_ObjtypeGet(app->provider, app_if_obj_shown(app, head));

    if( !obj || !node )
        return;
    if( node->if3 )
    {
        int32_t const idx = UITree_FindByComponentId(app->tree, head->com_id);

        /* The IF3 arm stores the obj on the component (field4176/field4177),
         * exactly as CC_SETOBJECT does, and the draw keys the obj composition
         * on it: class163.method5491 lowers the model by half its height
         * (`field1926 / 2`) only when field4176 != -1. The emit applies that
         * term and the zoom-per-box divide for any node carrying an obj
         * (uitree_emit.c), so the pose takes the raw zoom2d as the CS2 path
         * does. Without the obj the chatbox objectbox drew its item high by
         * half the model. */
        (void)UITree_SetObjectAt(
            app->tree, idx, head->npc_id, head->zoom, -1, 0, node->item_num_mode);
        (void)UITree_SetModelPoseAt(
            app->tree,
            idx,
            obj->offset_x2d,
            obj->offset_y2d,
            obj->xan2d,
            obj->yan2d,
            obj->zan2d,
            obj->zoom2d > 0 ? obj->zoom2d : 2000);
        return;
    }
    if( head->zoom > 0 )
        UITree_ApplyModelAngle(
            app->tree,
            head->com_id,
            obj->xan2d,
            obj->yan2d,
            (obj->zoom2d > 0 ? obj->zoom2d : 2000) * 100 / head->zoom);
}

void
app_if_head_wants(struct App* app)
{
    assert(app);
    if( app->if_models.head_count == 0 || !app->tree )
        return;
    app_if_forget_closed(app);
    /* An active head re-set from its npc is a new head task: raised here,
     * with the wants, every frame -- the npc changes without the tree. */
    app_if_heads_follow(app);
    for( int i = 0; i < app->if_models.head_count; i++ )
    {
        struct UIIfHead* head = &app->if_models.heads[i];

        if( head->wanted_gen == app->tree->generation )
            continue;
        head->wanted_gen = app->tree->generation;
        if( App_IfModelPending(app, head->com_id) )
            continue;
        /* A head left with no task (a cancel, a logout, a checkpoint), or an
         * npc head whose varps now choose another npc: another head to load. */
        if( !head->landed ||
            (head->kind == UI_IFHEAD_NPC &&
             App_NpctypeResolveMultiId(app, head->npc_id) != head->landed_npc_id) )
            App_IfModelWant(app, head->com_id);
    }
}

/* Bind the heads whose tasks have landed onto their MODEL nodes. Runs each
 * redraw: an entry applies once its task has composited it AND its component
 * is mounted, then only re-applies when the tree generation changes
 * (remount/rebuild) -- mirroring the reference resolving getModel every draw.
 * Nothing here loads or composites anything new: a head is attempted once, by
 * its task (Task_IfModel, app/app_tasks_if_assets.c). A head that needs a
 * want when the tree has moved -- left with no task (a logout cancelled it, a
 * checkpoint restored it), or an npc head whose varps now choose another npc
 * -- has it raised by app_if_head_wants, before the frame's last pump; this
 * bind adds no task. It also releases the customised chatheads nothing
 * draws or names any more. */
/* A customised chathead is in use while a head names its override or a
 * widget still draws it (UITreeSceneBridge_NpcHeadCustomSweep). */
static bool
app_if_custom_head_in_use(
    void* user,
    uint32_t serial,
    int scene_id)
{
    struct App const* app = user;

    for( int i = 0; i < app->if_models.head_count; i++ )
    {
        struct UIIfHead const* head = &app->if_models.heads[i];

        if( head->custom_serial == serial || head->bound_scene_id == scene_id )
            return true;
    }
    return false;
}

void
app_if_head_poll(struct App* app)
{
    if( UITreeSceneBridge_NpcHeadCustomCount(&app->bridge) > 0 )
    {
        app_if_forget_closed(app);
        UITreeSceneBridge_NpcHeadCustomSweep(&app->bridge, app_if_custom_head_in_use, app);
    }
    if( app->if_models.head_count == 0 || !app->tree )
        return;

    app_if_forget_closed(app);
    for( int i = 0; i < app->if_models.head_count; i++ )
    {
        struct UIIfHead* head = &app->if_models.heads[i];
        int scene_id;
        int first_apply;

        if( head->applied_gen == app->tree->generation )
            continue;
        /* UI mutations can advance the tree generation every tick.  The
         * binding still re-applies as required, but its diagnostic should say
         * when this stored request first became visible to the tree, not flood
         * the trace once per unrelated UI mutation. */
        first_apply = head->applied_gen == 0;

        /* A head with no task, or an npc head whose varps now choose
         * another child, was asked for again by app_if_head_wants, before the
         * frame's last pump: this runs after it, and a task added here would
         * be published ready. Keep the stored id as the shell, matching
         * IfType.model1Id, but resolve it every time the binding is made: the
         * selected child owns the actual heads and recolours, and another
         * child is another head, which binds when it lands. */
        if( head->landed && head->kind == UI_IFHEAD_NPC &&
            App_NpctypeResolveMultiId(app, head->npc_id) != head->landed_npc_id )
        {
            head->applied_gen = app->tree->generation;
            continue;
        }

        scene_id = head->landed ? head->scene_id : -1;
        /* IF_SETOBJECT: the obj the widget draws, the count variant on an IF3
         * component; the task composited both. */
        if( scene_id >= 0 && head->kind == UI_IFHEAD_OBJ )
            scene_id = UITreeSceneBridge_EnsureObjModel(&app->bridge, app_if_obj_shown(app, head));
        if( scene_id < 0 )
        {
            /* IF_SETMODEL replaced the widget's model the moment it landed
             * (reference Client.ts IF_SETMODEL writes model1Id), and a model
             * that is not loaded draws nothing (IfType.getModel answers null;
             * the draw skips it). So until this one composites -- forever,
             * for an id the cache does not hold -- the widget shows nothing,
             * not the model it had before. -1 is the tree's "no model". */
            if( head->kind == UI_IFHEAD_MODEL && UITree_ApplyModel(app->tree, head->com_id, -1) )
                head->bound_scene_id = -1;
            /* Nothing to bind until its task lands (which binds it again) or
             * the tree moves. Stamped after the apply, which may move the
             * generation itself. */
            head->applied_gen = app->tree->generation;
            continue;
        }

        if( UITree_ApplyModel(app->tree, head->com_id, scene_id) )
        {
            head->bound_scene_id = scene_id;
            /* Server IF_SETNPCHEAD reaches this async App path rather than the
             * CS2 host opcode path, so mirror the latter's opt-in trace here.
             * It makes a composed-but-currently-tab-hidden portrait observable
             * without changing its render or visibility state. */
            if( first_apply && head->kind == UI_IFHEAD_NPC && getenv("TORIRS_NPC_HEAD_DEBUG") )
                TORIRS_LOG(
                    "npc_head: npc=%d component=0x%08x scene=%d applied=1\n",
                    head->npc_id,
                    (unsigned)head->com_id,
                    scene_id);
            /* IF_SETOBJECT's view: the IF1 or the IF3 branch (above). */
            if( head->kind == UI_IFHEAD_OBJ )
                app_if_obj_pose(app, head);
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
    app_if_forget_closed(app);
    for( int i = 0; i < app->if_models.player_model_count; i++ )
    {
        struct UIIfPlayerModel* model = &app->if_models.player_models[i];
        if( model->com_id < 0 || !model->built || model->applied_gen == app->tree->generation )
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
    int rebuilt = 0;

    if( !app->tree || !app->world )
        return;
    lp = app_local_player(app);
    if( !lp )
        return; /* offline / not spawned yet — the baked default avatar stands */

    changed = UIIfModels_LocalPlayerChanged(
        &app->if_models, lp->appearance.slots, lp->appearance.colors, lp->gender);

    /* Keyed by content, like the body itself. A new appearance whose parts
     * are still loading does not build (the builder is all or nothing), so
     * the figure keeps the last whole body and asks again next tick. */
    if( changed &&
        UITreeSceneBridge_BuildLocalPlayerModel(
            &app->bridge, lp->appearance.slots, lp->appearance.colors, lp->gender) >= 0 )
    {
        UIIfModels_LocalPlayerBuilt(
            &app->if_models, lp->appearance.slots, lp->appearance.colors, lp->gender);
        rebuilt = 1;
    }
    scene_id = app->bridge.local_player_scene_id;
    if( scene_id < 0 )
        return; /* nothing whole composited yet */

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
    yan =
        ((int)(sin((double)FrameClockSerial_LogicCycle(&app->frame_clock.serial) / 40.0) * 256.0)) &
        0x7ff;

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
            RedrawRequest_Raise(&app->redraw_request);
        (void)UITree_SetModelAt(app->tree, i, scene_id);
        (void)UITree_SetModelPoseAt(
            app->tree, i, node->u.rs_model.x_offset, node->u.rs_model.y_offset, 150, yan, 0, 0);
        /* The entity owns this clock; the UI driver must not advance it again. */
        (void)UITree_SetModelAnimationAt(app->tree, i, seq_id, seq_frame, 0, 1);
        bound = 1;
    }

    if( rebuilt && torirs_env_anim_debug() )
        TORIRS_LOG(
            "player_model: rebuilt cycle=%llu scene=%d seq=%d frame=%d bound=%d\n",
            (unsigned long long)FrameClockSerial_LogicCycle(&app->frame_clock.serial),
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
    app_if_forget_closed(app);
    /* The sequence is only drawn: its load starts now, and the widget shows
     * its rest pose until it is here (uitree_anim.h). */
    if( anim_id >= 0 )
        (void)Assets_Want(app->assets, AssetKey_Make(ASSET_KIND_SEQUENCE, anim_id, 0));
    if( UIIfModels_SetHeadAnim(&app->if_models, component_id, anim_id) )
        RedrawRequest_Raise(&app->redraw_request);
    if( UIIfModels_SetPlayerModelAnim(&app->if_models, component_id, anim_id) )
        RedrawRequest_Raise(&app->redraw_request);
    UITree_ApplyModelAnim(app->tree, component_id, anim_id);
}
