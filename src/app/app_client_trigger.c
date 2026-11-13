/*
 * Client triggers: the clientscripts the cache binds to an npc or loc coming
 * into view.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"
#include "mem/torirs_mem.h"
#include "torirs_env.h"

/*
 * ---------------------------------------------------------------------------
 * Client triggers (game/rs_client_trigger.h).
 * ---------------------------------------------------------------------------
 *
 * The scripts the cache expects the CLIENT to find and run: one per npc type
 * (or category) when it walks on screen, one per loc type when the scene
 * builder places it. Nothing calls them; they are addressed by the hash of a
 * group name, and until now this client had no way to reach a single one.
 */

/*
 * The subject, queued.
 *
 * RS_CS2_RunScript does not run a script -- it queues one, because a script
 * may have to be read off disk first. So writing the active-subject register
 * beside the queue call is writing it for whichever npc happens to be last:
 * one region load queues twenty-six copies of the global npc-add script, they
 * all run during the same settle, and every one of them sees npc twenty-six.
 * Measured before this existed -- all twenty-six npcs shared one overlay pair,
 * indices 0 and 1.
 *
 * The fix is to make the write part of the queue rather than of the caller: a
 * one-shot task that carries its own snapshot, queued immediately in front of
 * the script. The queue is a strict serial FIFO, so "immediately in front"
 * survives every IO yield the script itself takes.
 */
struct Task_ClientTriggerSubject
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_CS2Host* host;
    int kind;
    struct RS_ClientOpContext ctx;
};

/* Private to this unit, declared up front so definition order is free. */
static int
Task_ClientTriggerSubject_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io);
static void
Task_ClientTriggerSubject_Free(struct ToriRS_Task* task_base);
static void
app_client_trigger_queue(
    struct App* app,
    struct RS_ClientOpContext const* ctx,
    int script_id);
static int
app_client_trigger_lookup_script(
    void* user,
    int name_hash);
static int
app_client_trigger_script(
    struct App* app,
    int trigger,
    int subject,
    int category);
static void
app_client_trigger_debug(
    char const* what,
    int trigger,
    int subject,
    int category,
    int script_id);
static void
app_client_trigger_debug_coord(
    char const* what,
    int trigger,
    int subject,
    int coord);
static void
app_client_trigger_npc(
    struct App* app,
    struct WorldEntity_NPC* npc,
    int trigger);
static void
app_client_trigger_loc_fire(
    struct App* app,
    struct WorldEntity_Scenery* loc,
    int trigger,
    int category,
    int script_id);

static int
Task_ClientTriggerSubject_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
{
    struct Task_ClientTriggerSubject* task = (struct Task_ClientTriggerSubject*)task_base;

    (void)io;
    PT_BEGIN(&task->pt);
    RS_ClientOpActiveSet(&task->host->clientop, (enum RS_ClientOpKind)task->kind, &task->ctx);
    PT_END(&task->pt);
}

static void
Task_ClientTriggerSubject_Free(struct ToriRS_Task* task_base)
{
    ToriMem_Free(ToriMem_Script(), task_base);
}

static struct ToriRS_TaskVTable Task_ClientTriggerSubject_VTable = {
    .run = Task_ClientTriggerSubject_Run,
    .free = Task_ClientTriggerSubject_Free,
};

static void
app_client_trigger_queue(
    struct App* app,
    struct RS_ClientOpContext const* ctx,
    int script_id)
{
    struct Task_ClientTriggerSubject* task;

    assert(app);
    assert(ctx);

    task = ToriMem_Calloc(ToriMem_Script(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_ClientTriggerSubject_VTable;
    strcpy(task->task.name, "ClientTriggerSubject");
    task->host = &app->host;
    task->kind = ctx->kind;
    task->ctx = *ctx;
    PT_INIT(&task->pt);
    ToriRS_TaskQueue_Add(app->frame_runner.queue, &task->task);

    RS_CS2_RunScript(&app->host, &app->frame_runner, script_id, NULL, 0, 0, NULL, 0);
}

/* RS_ClientTriggerScriptLookupFn over the cache provider. */
static int
app_client_trigger_lookup_script(
    void* user,
    int name_hash)
{
    return CacheProvider_ClientScriptIdByNameHash((struct CacheProvider*)user, name_hash);
}

/** The clientscript bound to `trigger` for this subject, or -1. Narrowest form
 *  first, exactly as `ClientScript::Get` walks them. */
static int
app_client_trigger_script(
    struct App* app,
    int trigger,
    int subject,
    int category)
{
    assert(app);

    /* "Is there a cache yet" is this caller's question. */
    if( !app->provider )
        return -1;
    return RS_ClientTriggerScriptFor(
        trigger, subject, category, app_client_trigger_lookup_script, app->provider);
}

static void
app_client_trigger_debug(
    char const* what,
    int trigger,
    int subject,
    int category,
    int script_id)
{
    if( !torirs_env_trigger_debug() )
        return;
    TORIRS_LOG(
        "trigger: %s %d (subject=%d category=%d) -> script %d\n",
        what,
        trigger,
        subject,
        category,
        script_id);
}

/** The same line with the subject's coord, for the ops that compare against a
 *  server-published one. Split out because most triggers have no coord to
 *  print and the extra field would be -1 noise on every npc. */
static void
app_client_trigger_debug_coord(
    char const* what,
    int trigger,
    int subject,
    int coord)
{
    if( !torirs_env_trigger_debug() )
        return;
    TORIRS_LOG("trigger: %s %d subject=%d coord=%d\n", what, trigger, subject, coord);
}

/** Fire an npc trigger with the npc as the active subject. */
static void
app_client_trigger_npc(
    struct App* app,
    struct WorldEntity_NPC* npc,
    int trigger)
{
    struct ToriRS_Npctype* type;
    struct RS_ClientOpContext ctx;
    int script_id;

    assert(app);
    assert(npc);

    if( !app->world )
        return;
    type = CacheProvider_NpctypeGet(app->provider, npc->npc_id);
    script_id = app_client_trigger_script(app, trigger, npc->npc_id, type ? type->category : 0);
    app_client_trigger_debug("npc", trigger, npc->npc_id, type ? type->category : 0, script_id);
    if( script_id < 0 )
        return;

    memset(&ctx, 0, sizeof(ctx));
    ctx.kind = RS_CLIENTOP_NPC;
    ctx.layer = -1;
    ctx.uid = npc->server_slot;
    ctx.type = npc->npc_id;
    ctx.coord = RS_CLIENTOP_COORD(
        npc->grid_position.level,
        app->world->_base_tile_x + npc->grid_position.x,
        app->world->_base_tile_z + npc->grid_position.z);
    snprintf(ctx.name, sizeof(ctx.name), "%s", npc->name);
    /*
     * The ACTIVE register, not a client-op dispatch context.
     *
     * A dispatch is gated on the script id that was named for it, which is
     * right for a right-click row and wrong here: a trigger script calls procs
     * and installs hooks that read the subject back later, and the reference
     * models exactly that with a register that simply stands until something
     * else writes it. See rs_clientop.h.
     */
    app_client_trigger_queue(app, &ctx, script_id);
}

/** Fire a loc trigger with the loc as the active subject. */
void
app_client_trigger_loc(
    struct App* app,
    struct WorldEntity_Scenery* loc,
    int trigger)
{
    struct ToriRS_Location* type;

    assert(app);
    assert(loc);

    if( !app->world )
        return;
    type = CacheProvider_LocationGet(app->provider, loc->loc_id);
    app_client_trigger_loc_fire(app, loc, trigger, type ? type->category : 0,
        app_client_trigger_script(app, trigger, loc->loc_id, type ? type->category : 0));
}

/* The trigger for `loc` once its script is known (-1: none). */
static void
app_client_trigger_loc_fire(
    struct App* app,
    struct WorldEntity_Scenery* loc,
    int trigger,
    int category,
    int script_id)
{
    struct RS_ClientOpContext ctx;

    assert(app);
    assert(loc);
    assert(app->world);
    app_client_trigger_debug("loc", trigger, loc->loc_id, category, script_id);
    if( script_id < 0 )
        return;

    memset(&ctx, 0, sizeof(ctx));
    ctx.kind = RS_CLIENTOP_LOC;
    ctx.uid = -1;
    ctx.type = loc->loc_id;
    ctx.coord = RS_CLIENTOP_COORD(
        loc->grid_position.level,
        app->world->_base_tile_x + loc->grid_position.x,
        app->world->_base_tile_z + loc->grid_position.z);
    ctx.layer = World_LocShapeToLayer(loc->shape);
    snprintf(ctx.name, sizeof(ctx.name), "%s", loc->info->name);
    app_client_trigger_debug_coord("loc", trigger, loc->loc_id, ctx.coord);
    app_client_trigger_queue(app, &ctx, script_id);
}

/**
 * The npc's NPC_ADD trigger, from the entity-sync path.
 *
 * Not from the spawn helper, which is where it started: `server_slot` is
 * written by the caller AFTER the helper returns, so the trigger read -1 for
 * every npc, every overlay keyed on the same absent subject, and the per-frame
 * anchor pass reaped them all a frame later. From here the npc is finished.
 */
void
App_ClientTriggerNpcAdd(
    struct App* app,
    int npc_pool_index)
{
    struct WorldEntity_NPC* npc;

    assert(app);

    if( !app->world )
        return;
    npc = World_EntityPoolGet(&app->world->entities.npc, npc_pool_index);
    if( npc )
        app_client_trigger_npc(app, npc, RS_TRIGGER_NPC_ADD);
}

/**
 * Fire LOC_ADD for every loc in the freshly built scene.
 *
 * Once per world build rather than per frame: scenery only changes when the
 * scene is rebuilt or a zone packet mutates one loc, so a per-frame
 * reconciliation would walk tens of thousands of entries to find nothing.
 *
 * The reference fires this from `Client::OnLoadLocation`, one loc at a time as
 * the builder places it. This client's builder does not have a seam there, and
 * the observable difference is only WHEN inside one build the script runs --
 * every loc in the scene still gets exactly one.
 */
void
app_client_triggers_world_loaded(struct App* app)
{
    struct World_EntityPool* pool;

    assert(app);

    if( !app->world || !app->provider )
        return;
    pool = &app->world->entities.scenery;
    {
        /*
         * The script a loc's trigger runs is a function of its type alone, and
         * a region's few thousand locs are a few hundred types: each type is
         * resolved once (three name-hash lookups and a type fetch) and
         * remembered for the pass. Every loc still fires, in pool order.
         */
        enum
        {
            MEMO_CAP = 4096
        };
        struct
        {
            int loc_id_plus1;
            int category;
            int script_id;
        } memo[MEMO_CAP];
        memset(memo, 0, sizeof(memo));
        for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(pool, i) )
        {
            struct WorldEntity_Scenery* loc = World_EntityPoolGet(pool, i);
            unsigned slot;
            int probes = 0;
            if( !loc )
                continue;
            slot = ((unsigned)loc->loc_id * 2654435761u) & (MEMO_CAP - 1);
            while( memo[slot].loc_id_plus1 != 0 && memo[slot].loc_id_plus1 != loc->loc_id + 1 &&
                   probes < MEMO_CAP )
            {
                slot = (slot + 1u) & (MEMO_CAP - 1);
                probes++;
            }
            if( probes >= MEMO_CAP )
            {
                /* Full (no region has this many types): unmemoised. */
                app_client_trigger_loc(app, loc, RS_TRIGGER_LOC_ADD);
                continue;
            }
            if( memo[slot].loc_id_plus1 == 0 )
            {
                struct ToriRS_Location* type = CacheProvider_LocationGet(app->provider, loc->loc_id);
                memo[slot].loc_id_plus1 = loc->loc_id + 1;
                memo[slot].category = type ? type->category : 0;
                memo[slot].script_id = app_client_trigger_script(
                    app, RS_TRIGGER_LOC_ADD, loc->loc_id, memo[slot].category);
            }
            app_client_trigger_loc_fire(
                app, loc, RS_TRIGGER_LOC_ADD, memo[slot].category, memo[slot].script_id);
        }
    }
}

/**
 * Re-fire every ADD trigger for everything already in the world.
 *
 * For a tree rebuild, which is the one event that destroys a scripted overlay
 * without destroying its subject. See App::client_trigger_overlay_com.
 *
 * The overlay records go first: their `component_id`s name nodes that no
 * longer exist, and a GET op answering "you already have one" would stop the
 * script rebuilding it.
 */
void
app_client_triggers_refire(struct App* app)
{
    struct World_EntityPool* pool;

    assert(app);

    if( !app->world || !app->provider )
        return;

    RS_OverlayReset(&app->host.overlay);
    /* The pile subjects survive a UI remount just like NPCs and scenery.
     * Rebuild their native CS2 labels at the ordinary ground-items tick. */
    RS_GroundItemsDirty_MarkAll(&app->ground_items_dirty);
    app_client_triggers_world_loaded(app);

    pool = &app->world->entities.npc;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, i);
        /* An npc with no server slot has no uid for an overlay to key on --
         * offline debug spawns, and npcs mid-sync. The next NPC_INFO gives it
         * one and App_ClientTriggerNpcAdd fires there. */
        if( npc && npc->server_slot >= 0 )
            app_client_trigger_npc(app, npc, RS_TRIGGER_NPC_ADD);
    }
}
