/*
 * Varp-driven npc and loc transforms: the multi-resolve tasks and the refresh
 * on change.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"
#include "mem/torirs_mem.h"
#include "game/task_entity_assets.h"

/* Load and resolve only the config chain. Body and interface-head consumers
 * deliberately share this step, then await their own distinct model sets. */
struct Task_NpcMultiResolve
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    int base_npc_id;
    int* out_npc_id;
    int current_npc_id;
    int depth;
    /* The config read in flight, counted through the assets table
     * (ASSET_KIND_CONFIG, ASSET_CONFIG_NPC): one load per npc type however
     * many resolves, scripts and heads ask for it at once. */
    int join;
};

/* The rare varp retype whose new rung's config is not resident: it waits for
 * the config chain on the packet queue (the type is required: it decides
 * size, ops and stances a later packet reads), and nothing else -- the body
 * is the npc's body task's. */
struct Task_AppNpcTransform
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    int server_slot;
    uint32_t spawn_serial;
    int base_npc_id;
    int resolved_npc_id;
};

/* Private to this unit, declared up front so definition order is free. */
static int
Task_NpcMultiResolve_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io);
static void
Task_NpcMultiResolve_Free(struct ToriRS_Task* base);
static int
Task_NpcMultiLoad_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io);
static void
Task_NpcMultiLoad_Free(struct ToriRS_Task* base);
static int
app_npc_transform_depends_on_varp(
    struct App* app,
    int base_npc_id,
    int varp_id);
static void
app_varp_refresh_npc_transforms(
    struct App* app,
    int varp_id);
static int
app_loc_transform_depends_on_varp(
    struct App* app,
    struct ToriRS_Location const* loc,
    int varp_id);
static void
app_varp_refresh_loc_transforms(
    struct App* app,
    int varp_id);

static int
Task_NpcMultiResolve_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_NpcMultiResolve* self = (struct Task_NpcMultiResolve*)base;
    struct App* app = self->app;

    (void)io;
    PT_BEGIN(&self->pt);

    self->current_npc_id = self->base_npc_id;
    *self->out_npc_id = self->base_npc_id;

    for( self->depth = 0; self->depth <= TORIRS_NPC_MULTI_MAX_DEPTH && self->current_npc_id >= 0;
         self->depth++ )
    {
        (void)Assets_Await(
            app->assets,
            AssetKey_Make(ASSET_KIND_CONFIG, self->current_npc_id, ASSET_CONFIG_NPC),
            &self->join);
        PT_TASK_JOIN(join);
        {
            struct ToriRS_Npctype* npctype =
                CacheProvider_NpctypeGet(app->provider, self->current_npc_id);
            int next;

            if( !npctype || npctype->transform_count <= 0 || !npctype->transforms )
                break;
            next = VarPManager_ResolveTransform(
                &app->varps,
                npctype->transforms,
                npctype->transform_count,
                npctype->transform_varbit,
                npctype->transform_varp);
            if( next < 0 )
            {
                self->current_npc_id = -1;
                break;
            }
            if( next == self->current_npc_id || self->depth == TORIRS_NPC_MULTI_MAX_DEPTH )
                break;
            self->current_npc_id = next;
        }
    }

    *self->out_npc_id = self->current_npc_id;
    PT_END(&self->pt);
}

static void
Task_NpcMultiResolve_Free(struct ToriRS_Task* base)
{
    struct Task_NpcMultiResolve* self = (struct Task_NpcMultiResolve*)base;
    /* Freed while it waits (its parent cancelled, a queue torn down): the
     * table must not count on a join that is gone. */
    if( self->join > 0 )
        Assets_Unjoin(self->app->assets, &self->join);
    ToriMem_Free(ToriMem_Process(), base);
}

static struct ToriRS_TaskVTable Task_NpcMultiResolve_VTable = {
    .run = Task_NpcMultiResolve_Run,
    .free = Task_NpcMultiResolve_Free,
};

struct ToriRS_Task*
CreateTask_NpcMultiResolve(
    struct App* app,
    int base_npc_id,
    int* out_npc_id)
{
    struct Task_NpcMultiResolve* task;

    assert(app);
    assert(app->assets);
    assert(out_npc_id);
    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_NpcMultiResolve_VTable;
    strncpy(task->task.name, "NpcMultiResolve", sizeof(task->task.name) - 1);
    task->app = app;
    task->base_npc_id = base_npc_id;
    task->out_npc_id = out_npc_id;
    PT_INIT(&task->pt);
    return &task->task;
}

static int
Task_NpcMultiLoad_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_NpcMultiLoad* self = (struct Task_NpcMultiLoad*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);

    PT_TASK_AWAITSELF_IF(
        CreateTask_NpcMultiResolve(app, self->base_npc_id, &self->resolved_npc_id));

    if( self->resolved_npc_id >= 0 )
    {
        /*
         * The terminal config was loaded by the walk above. Load its complete
         * body and movement set before the caller mounts/replaces the model.
         *
         * The body parts AND the five movement sequences go out TOGETHER.
         * Awaiting them one at a time is one network round trip per part on a
         * cache being streamed, and an npc body is commonly four models and
         * five sequences each several reads deep -- for a roster of a thousand
         * that is the difference between a world that populates and one that
         * trickles. They are independent reads with a common consumer, which
         * is exactly the shape the runner can overlap: awaited through the
         * assets table, each is one load on the parallel queue with its own
         * IO slot -- shared with any body or binder asking for the same part
         * -- and they are all on the wire at once.
         *
         * Then joined, not waited on for residency: a part the cache cannot
         * serve never becomes resident, and the residency wait this replaced
         * spent its whole budget on one -- while a loader that fails still
         * ENDS, which is what the join counts. The npc then spawns missing the
         * part, exactly as the old per-part await did.
         */
        {
            struct ToriRS_Npctype* npctype =
                CacheProvider_NpctypeGet(app->provider, self->resolved_npc_id);
            for( self->model_i = 0; npctype && self->model_i < npctype->models_count;
                 self->model_i++ )
            {
                if( npctype->models[self->model_i] >= 0 )
                    Assets_Await(
                        app->assets,
                        AssetKey_Make(ASSET_KIND_MODEL, npctype->models[self->model_i], 0),
                        &self->pending);
            }
            if( npctype )
            {
                /* The movement set the npc will play is the SHELL's
                 * (ToriRS_NpcEntityFacts): the wire's record, which the walk
                 * above loaded first. */
                struct ToriRS_NpcEntityFacts facts;
                app_npc_entity_facts(app, self->base_npc_id, npctype, &facts);
                int const seqs[5] = {
                    facts.readyanim,  facts.walkanim,   facts.walkanim_b,
                    facts.walkanim_r, facts.walkanim_l,
                };
                for( self->seq_i = 0; self->seq_i < 5; self->seq_i++ )
                {
                    if( seqs[self->seq_i] >= 0 )
                        Assets_Await(
                            app->assets,
                            AssetKey_Make(ASSET_KIND_SEQUENCE, seqs[self->seq_i], 0),
                            &self->pending);
                }
            }
        }
        PT_TASK_JOIN(pending);
    }

    *self->out_npc_id = self->resolved_npc_id;
    PT_END(&self->pt);
}

static void
Task_NpcMultiLoad_Free(struct ToriRS_Task* base)
{
    struct Task_NpcMultiLoad* self = (struct Task_NpcMultiLoad*)base;
    /* Freed while its parts still load: the table must not count them on
     * this task any more. At shutdown the table is closed and ignores it. */
    if( self->pending > 0 )
        Assets_Unjoin(self->app->assets, &self->pending);
    ToriMem_Free(ToriMem_Process(), base);
}

static struct ToriRS_TaskVTable Task_NpcMultiLoad_VTable = {
    .run = Task_NpcMultiLoad_Run,
    .free = Task_NpcMultiLoad_Free,
};

struct ToriRS_Task*
CreateTask_NpcMultiLoad(
    struct App* app,
    int base_npc_id,
    int* out_npc_id)
{
    struct Task_NpcMultiLoad* task;

    assert(app && out_npc_id);
    task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_NpcMultiLoad_VTable;
    strncpy(task->task.name, "NpcMultiLoad", sizeof(task->task.name) - 1);
    task->app = app;
    task->base_npc_id = base_npc_id;
    task->out_npc_id = out_npc_id;
    PT_INIT(&task->pt);
    return &task->task;
}

static int
app_npc_transform_depends_on_varp(
    struct App* app,
    int base_npc_id,
    int varp_id)
{
    int npc_id = base_npc_id;

    for( int depth = 0; depth <= TORIRS_NPC_MULTI_MAX_DEPTH && npc_id >= 0; depth++ )
    {
        struct ToriRS_Npctype* npc = CacheProvider_NpctypeGet(app->provider, npc_id);
        int next;

        if( !npc || npc->transform_count <= 0 || !npc->transforms )
            return 0;
        /* A negative varp is the wildcard this walk alone accepts: "anything
         * with a transform table at all". Nothing passes it today -- the
         * change callback always carries a real varp -- but it is what makes
         * this a walk over rungs rather than a test of one. */
        if( varp_id < 0 )
            return 1;
        if( VarPManager_TransformDependsOnVarp(
                &app->varps,
                npc->transforms,
                npc->transform_count,
                npc->transform_varbit,
                npc->transform_varp,
                varp_id) )
            return 1;
        next = VarPManager_ResolveTransform(
            &app->varps,
            npc->transforms,
            npc->transform_count,
            npc->transform_varbit,
            npc->transform_varp);
        if( next < 0 || next == npc_id )
            return 0;
        npc_id = next;
    }
    return 0;
}

/* The rung `base_npc_id` resolves to now (-1: none) when every config along
 * the chain is resident: 1. 0 when a link is not resident (the walk
 * Task_NpcMultiResolve makes, synchronously). */
static int
app_npc_chain_resolve_resident(
    struct App* app,
    int base_npc_id,
    int* out_npc_id)
{
    int current = base_npc_id;

    assert(out_npc_id);
    for( int depth = 0; depth <= TORIRS_NPC_MULTI_MAX_DEPTH && current >= 0; depth++ )
    {
        struct ToriRS_Npctype* npctype = CacheProvider_NpctypeGet(app->provider, current);
        int next;

        if( !npctype )
            return 0;
        if( npctype->transform_count <= 0 || !npctype->transforms )
            break;
        next = VarPManager_ResolveTransform(
            &app->varps,
            npctype->transforms,
            npctype->transform_count,
            npctype->transform_varbit,
            npctype->transform_varp);
        if( next < 0 )
        {
            current = -1;
            break;
        }
        if( next == current || depth == TORIRS_NPC_MULTI_MAX_DEPTH )
            break;
        current = next;
    }
    *out_npc_id = current;
    return 1;
}

/*
 * Draw the npc at `world_idx` as the rung `resolved` (-1: its multinpc selects
 * nothing) under its shell. The type is applied now; the overrides the server
 * set on the npc (name, level, stances) are kept (WORLD_NPC_OVERRIDE_*), and
 * the body, when not resident, is the npc's body task's (App_WorldApplyNpcType
 * wants it).
 */
static void
app_npc_varp_retype_apply(
    struct App* app,
    int world_idx,
    int resolved)
{
    struct WorldEntity_NPC* npc = World_EntityPoolGet(&app->world->entities.npc, world_idx);
    int const hidden = resolved < 0;
    int effective;

    assert(npc);
    effective = hidden ? npc->base_npc_id : resolved;
    if( npc->npc_id != effective )
        App_WorldApplyNpcType(app, world_idx, npc->element_id, effective, npc->base_npc_id);
    npc = World_EntityPoolGet(&app->world->entities.npc, world_idx);
    assert(npc);
    npc->multinpc_hidden = hidden != 0;
}

static int
Task_AppNpcTransform_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_AppNpcTransform* self = (struct Task_AppNpcTransform*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);
    PT_TASK_AWAITSELF_IF(
        CreateTask_NpcMultiResolve(app, self->base_npc_id, &self->resolved_npc_id));
    {
        int world_idx = -1;
        struct WorldEntity_NPC* npc = NULL;

        if( RS_EntitySync_FindNpc(&app->esync, self->server_slot, &world_idx, NULL) && world_idx >= 0 )
            npc = World_EntityPoolGet(&app->world->entities.npc, world_idx);
        /* The configs can take a read: still the same npc under the same
         * shell (a despawn, a slot reuse or a server CHANGE_TYPE since)? */
        if( npc && npc->spawn_serial == self->spawn_serial && npc->base_npc_id == self->base_npc_id )
            app_npc_varp_retype_apply(app, world_idx, self->resolved_npc_id);
    }
    PT_END(&self->pt);
}

static void
Task_AppNpcTransform_Free(struct ToriRS_Task* base)
{
    ToriMem_Free(ToriMem_Process(), base);
}

static struct ToriRS_TaskVTable Task_AppNpcTransform_VTable = {
    .run = Task_AppNpcTransform_Run,
    .free = Task_AppNpcTransform_Free,
};

/*
 * A varp changed: every npc whose multinpc chain depends on it is marked, and
 * App_EntityNpcRetypeFlush resolves and applies it -- before the next packet
 * that is not a var write, or the frame -- so several varps in a row that one
 * npc depends on make ONE retype, to the rung they select together, not one
 * packet-queue task per varp that each waited out the body (FAULTS E6).
 *
 * Every npc there is lives in the root World's pool -- a deck's are homed to
 * their view (view_placement.home_view) -- so this one walk covers every live
 * view. A transmogged player's new rung is wanted here too: the per-frame
 * reconcile draws it once it is in (it was never fetched).
 *
 * The mark (WorldEntity_NPC.retype_wanted, World.npc_retype_wanted) is not a
 * task in the pending index: it is a coalescing of var writes applied on the
 * MAIN queue at a fixed point (App_EntityNpcRetypeBeforePacket, the frame),
 * because the type is required -- size, ops and stances a later packet reads
 * -- so it may not wait on the parallel queue. It lives on the npc and goes
 * with it, and the configs it may need are read through the assets table
 * (Task_NpcMultiResolve); the body is the npc's body task's.
 */
static void
app_varp_refresh_npc_transforms(
    struct App* app,
    int varp_id)
{
    struct World* world;
    struct World_EntityPool* pool;

    assert(app);
    world = app->world;
    /* Before the first world, or with no cache: nothing is drawn yet. */
    if( !world || !app->provider )
        return;
    pool = &world->entities.npc;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, i);

        assert(npc);
        if( npc->retype_wanted || !app_npc_transform_depends_on_varp(app, npc->base_npc_id, varp_id) )
            continue;
        npc->retype_wanted = true;
        world->npc_retype_wanted++;
    }
    pool = &world->entities.player;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(pool, i);

        assert(player);
        if( player->transmog_npc_id >= 0 &&
            app_npc_transform_depends_on_varp(app, player->transmog_npc_id, varp_id) )
            EntityAssets_WantPlayerTransmog(app, player->transmog_npc_id);
    }
}

void
App_EntityNpcRetypeFlush(struct App* app)
{
    struct World* world;
    struct World_EntityPool* pool;

    assert(app);
    world = app->world;
    if( !world || world->npc_retype_wanted == 0 )
        return;
    /* Mid-rebuild: kept until the scene is built (it was skipped outright,
     * and the npc kept the old rung until the next varp). */
    if( !world->load_complete )
        return;
    pool = &world->entities.npc;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL && world->npc_retype_wanted > 0;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, i);
        int resolved;

        assert(npc);
        if( !npc->retype_wanted )
            continue;
        npc->retype_wanted = false;
        world->npc_retype_wanted--;
        if( app_npc_chain_resolve_resident(app, npc->base_npc_id, &resolved) )
        {
            app_npc_varp_retype_apply(app, i, resolved);
            continue;
        }
        /* A rung never seen: its config is required, so the packet queue
         * waits for it (configs are local after the boot fill). */
        {
            struct Task_AppNpcTransform* task = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*task));
            assert(task);
            task->task.vtable = &Task_AppNpcTransform_VTable;
            strncpy(task->task.name, "NpcTransform", sizeof(task->task.name) - 1);
            task->app = app;
            task->server_slot = npc->server_slot;
            task->spawn_serial = npc->spawn_serial;
            task->base_npc_id = npc->base_npc_id;
            task->resolved_npc_id = npc->npc_id;
            PT_INIT(&task->pt);
            TaskRunner_AddRenderBlockingSerialTask(&app->packet_runner, &task->task);
        }
    }
    /* Whatever the walk did not reach was released with its npc. */
    world->npc_retype_wanted = 0;
}

void
App_EntityNpcRetypeBeforePacket(
    struct App* app,
    int packet_type)
{
    assert(app);
    /* A run of var writes is one change as far as a multinpc is concerned. */
    if( packet_type == PKT_NAME_VARP_SMALL || packet_type == PKT_NAME_VARP_LARGE ||
        packet_type == PKT_NAME_VARP_LONG || packet_type == PKT_NAME_VARP_SYNC ||
        packet_type == PKT_NAME_VARP_RESET )
        return;
    App_EntityNpcRetypeFlush(app);
}

/*
 * Live multiloc remorph (Java ClientLocAnim / OpenRS2 Loc.getMultiLoc): when a
 * varp that drives a LocType transform table changes, re-apply each matching
 * scenery instance so the model/name/ops track the new child without a zone
 * LOC packet or a full chunk rebuild. Queues App_WorldLocChange (async model
 * wait) with the same BASE loc_id the map placed.
 */
static int
app_loc_transform_depends_on_varp(
    struct App* app,
    struct ToriRS_Location const* loc,
    int varp_id)
{
    assert(app);
    assert(loc);
    return VarPManager_TransformDependsOnVarp(
               &app->varps,
               loc->transforms,
               loc->transform_count,
               loc->transform_varbit,
               loc->transform_varp,
               varp_id)
               ? 1
               : 0;
}

/*
 * Whether `varp_id` drives any link of `loc`'s transform chain, and -- through
 * `out_hidden` -- whether that chain resolves to -1 right now. A link whose
 * config is not resident yet counts as shown: the loc-change task that follows
 * awaits it and resolves again.
 */
static int
app_loc_chain_depends_on_varp(
    struct App* app,
    struct ToriRS_Location* loc,
    int varp_id,
    int* out_hidden)
{
    int depends = 0;

    assert(app);
    assert(out_hidden);
    *out_hidden = 0;
    for( int depth = 0; loc && depth < 16; ++depth )
    {
        if( !depends && app_loc_transform_depends_on_varp(app, loc, varp_id) )
            depends = 1;
        if( loc->transform_count <= 0 || !loc->transforms )
            break;
        int next = VarPManager_ResolveTransform(
            &app->varps,
            loc->transforms,
            loc->transform_count,
            loc->transform_varbit,
            loc->transform_varp);
        if( next < 0 )
        {
            *out_hidden = 1;
            break;
        }
        if( next == loc->id )
            break;
        loc = CacheProvider_LocationGet(app->provider, next);
    }
    return depends;
}

/*
 * Which varps a view's placed and multiloc-hidden locs can depend on right
 * now, so a change to any OTHER varp does not walk the scenery pool.
 *
 * The walk below asks every placed loc "does this varp drive a link of your
 * transform chain?". The root scene holds about 23,000 scenery entities and a
 * session sends a varp every tick or so, almost none of which drive a loc: on
 * RSProx 96 the walk was 10% of a replay's whole run time (2026-11-12), each
 * one a CacheProvider lookup per entity to answer "no".
 *
 * The set is exact while nothing that decides a chain has changed:
 *   - which locs are placed: the pool's epoch, bumped on every activate,
 *     release, reset and re-register (World_SceneryRegister);
 *   - which are hidden: the builder's hidden count;
 *   - how each chain resolves: only a varp IN the set can change that, and
 *     every write of a varp value reaches app_varp_change (ApplySmall/Large,
 *     ApplySync, ResetAll, the optimistic setters, SetPreview), so a walk on
 *     a hit drops the set and the next change rebuilds it.
 * A chain with a link that is not resident cannot be listed, and a config
 * arriving later moves no epoch, so that is its own state: "cannot say",
 * which walks every time as before.
 *
 * TORIRS_LOC_VARP_INDEX_CHECK=1 walks even where the set says there is
 * nothing to find, and aborts if the walk finds something.
 */
enum
{
    APP_LOC_VARP_BITS = 1 << 15
};

struct AppLocVarpIndex
{
    struct World const* world;
    unsigned int epoch;
    int hidden_count;
    /** 0: not built. 1: complete. -1: cannot say. */
    int state;
    uint32_t bits[APP_LOC_VARP_BITS / 32];
};

static struct AppLocVarpIndex s_loc_varp_index[WORLDVIEW_MAX];

static void
app_loc_varp_index_note(
    struct App* app,
    struct AppLocVarpIndex* index,
    struct ToriRS_Location const* loc)
{
    /* The two keys VarPManager_TransformDependsOnVarp tests. */
    int keys[2];

    if( loc->transform_count <= 0 || !loc->transforms )
        return;
    keys[0] = loc->transform_varp;
    keys[1] = VarPManager_VarbitBaseVar(&app->varps, loc->transform_varbit);
    for( int i = 0; i < 2; ++i )
    {
        if( keys[i] < 0 )
            continue;
        if( keys[i] >= APP_LOC_VARP_BITS )
        {
            index->state = -1;
            continue;
        }
        index->bits[keys[i] >> 5] |= 1u << (keys[i] & 31);
    }
}

/* Every link the walks below can reach from `loc`, as they reach it. */
static void
app_loc_varp_index_note_chain(
    struct App* app,
    struct AppLocVarpIndex* index,
    struct ToriRS_Location const* loc)
{
    for( int depth = 0; depth < 16; ++depth )
    {
        if( !loc )
        {
            index->state = -1;
            return;
        }
        app_loc_varp_index_note(app, index, loc);
        if( loc->transform_count <= 0 || !loc->transforms )
            return;
        int next = VarPManager_ResolveTransform(
            &app->varps,
            loc->transforms,
            loc->transform_count,
            loc->transform_varbit,
            loc->transform_varp);
        if( next < 0 || next == loc->id )
            return;
        loc = CacheProvider_LocationGet(app->provider, next);
    }
}

static int
app_loc_varp_index_check(void)
{
    static int cached = -1;

    if( cached < 0 )
    {
        char const* env = getenv("TORIRS_LOC_VARP_INDEX_CHECK");

        cached = env && env[0] == '1';
    }
    return cached;
}

/* One view's half of app_varp_refresh_loc_transforms. Returns how many
 * placements it queued a re-place for. */
static int
app_varp_refresh_view_loc_transforms(
    struct App* app,
    int view,
    int varp_id)
{
    int previous_view;

    assert(app);
    assert(app->provider);
    assert(varp_id >= 0);
    assert(WorldviewRegistry_IsLive(&app->worldviews, view));
    previous_view = WorldviewRegistry_Active(&app->worldviews);
    {
        struct World* world = WorldviewRegistry_Get(&app->worldviews, view)->world;
        if( !world || !world->load_complete )
            return 0;
        enum
        {
            MAX_REFRESH = 256
        };
        struct
        {
            int x, z, level, loc_id, shape, angle, op_flags;
            char ops[5][32];
        } pending[MAX_REFRESH];
        int n = 0;
        struct World_EntityPool* pool = &world->entities.scenery;
        struct AppLocVarpIndex* index = &s_loc_varp_index[view];
        struct WorldBuilder* index_builder =
            WorldviewRegistry_Get(&app->worldviews, view)->builder;
        int const index_hidden = index_builder ? WorldBuilder_HiddenLocCount(index_builder) : 0;
        int const building = index->world != world || index->epoch != pool->epoch ||
                             index->hidden_count != index_hidden || index->state <= 0;
        int const skip = !building && varp_id < APP_LOC_VARP_BITS &&
                         !(index->bits[varp_id >> 5] & (1u << (varp_id & 31)));

        if( skip && !app_loc_varp_index_check() )
            return 0;
        if( building )
        {
            memset(index->bits, 0, sizeof(index->bits));
            index->world = world;
            index->epoch = pool->epoch;
            index->hidden_count = index_hidden;
            index->state = 1;
            for( int i = 0; i < index_hidden; ++i )
                app_loc_varp_index_note_chain(
                    app,
                    index,
                    CacheProvider_LocationGet(
                        app->provider, WorldBuilder_HiddenLocGet(index_builder, i)->loc_id));
        }
        for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(pool, i) )
        {
            struct WorldEntity_Scenery* sc = World_EntityPoolGet(pool, i);
            assert(sc);
            struct ToriRS_Location* loc = CacheProvider_LocationGet(app->provider, sc->loc_id);
            int depends = 0;
            if( building )
                app_loc_varp_index_note_chain(app, index, loc);
            /* A varp may drive a descendant of the placed wrapper. Every
             * config along a previously materialised chain is resident. */
            for( int depth = 0; loc && depth < 16; ++depth )
            {
                if( app_loc_transform_depends_on_varp(app, loc, varp_id) )
                {
                    depends = 1;
                    break;
                }
                if( loc->transform_count <= 0 || !loc->transforms )
                    break;
                int next = VarPManager_ResolveTransform(
                    &app->varps,
                    loc->transforms,
                    loc->transform_count,
                    loc->transform_varbit,
                    loc->transform_varp);
                if( next < 0 || next == loc->id )
                    break;
                loc = CacheProvider_LocationGet(app->provider, next);
            }
            if( !depends )
                continue;
            int duplicate = 0;
            for( int j = 0; j < n; ++j )
                if( pending[j].x == sc->grid_position.x && pending[j].z == sc->grid_position.z &&
                    pending[j].level == sc->grid_position.level && pending[j].shape == sc->shape )
                {
                    duplicate = 1;
                    break;
                }
            if( duplicate )
                continue;
            if( n == MAX_REFRESH )
            {
                /* The entities after this one were not listed. */
                if( building )
                    index->state = -1;
                break;
            }
            pending[n].x = sc->grid_position.x;
            pending[n].z = sc->grid_position.z;
            pending[n].level = sc->grid_position.level;
            pending[n].loc_id = sc->loc_id;
            pending[n].shape = sc->shape;
            pending[n].angle = sc->angle;
            pending[n].op_flags = 0x1f;
            memset(pending[n].ops, 0, sizeof(pending[n].ops));
            for( int op = 0; op < 5; ++op )
                if( sc->placement_op_overrides & (1 << op) )
                {
                    if( !(sc->placement_op_mask & (1 << op)) )
                        pending[n].op_flags &= ~(1 << op);
                    else
                        snprintf(
                            pending[n].ops[op],
                            sizeof(pending[n].ops[op]),
                            "%s",
                            sc->info->actions[op].name);
                }
            ++n;
        }
        /*
         * And the placements that are not in the pool because their multiloc
         * hid them. The reference keeps a hidden multiloc in its scene and
         * re-resolves its model every draw (Deobfuscator/src_osrs239/deob/
         * class123.java method4108/method4109, runelite's DynamicObject), so
         * it reappears on the frame the varbit changes; here the loc-change
         * path re-places it from the base id the map named. Recruitment
         * Drive's Lady Table room hides its answer statue while you memorise
         * and must show it again when rd_room_order returns to 0.
         */
        struct WorldBuilder* builder = WorldviewRegistry_Get(&app->worldviews, view)->builder;
        int hidden_count = builder ? WorldBuilder_HiddenLocCount(builder) : 0;
        for( int i = 0; i < hidden_count && n < MAX_REFRESH; ++i )
        {
            struct WorldBuilderHiddenLoc const* hidden = WorldBuilder_HiddenLocGet(builder, i);
            int const x = hidden->scene_x;
            int const z = hidden->scene_z;
            int const level = hidden->level;
            int const shape = hidden->shape;
            int still_hidden = 0;
            struct ToriRS_Location* loc = CacheProvider_LocationGet(app->provider, hidden->loc_id);
            if( !loc || !app_loc_chain_depends_on_varp(app, loc, varp_id, &still_hidden) )
                continue;
            if( still_hidden )
                continue;
            /* Something else stands in that layer now: re-placing would
             * replace it, which no varbit asked for. */
            if( World_SceneryFindAt(world, x, z, level, shape) >= 0 )
                continue;
            int duplicate = 0;
            for( int j = 0; j < n; ++j )
                if( pending[j].x == x && pending[j].z == z && pending[j].level == level &&
                    pending[j].shape == shape )
                {
                    duplicate = 1;
                    break;
                }
            if( duplicate )
                continue;
            pending[n].x = x;
            pending[n].z = z;
            pending[n].level = level;
            pending[n].loc_id = hidden->loc_id;
            pending[n].shape = shape;
            pending[n].angle = hidden->angle;
            /* The placement's own menu came through the hide with it: a zone
             * LOC_ADD with door-script ops that landed while its multiloc was
             * -1 is re-placed with those ops, not the loctype's. */
            pending[n].op_flags = hidden->op_flags;
            for( int op = 0; op < 5; ++op )
                snprintf(pending[n].ops[op], sizeof(pending[n].ops[op]), "%s", hidden->ops[op]);
            ++n;
        }
        if( skip )
        {
            /* Only under TORIRS_LOC_VARP_INDEX_CHECK: the set said no loc
             * here depends on this varp, and the walk must agree. */
            if( n != 0 )
            {
                fprintf(
                    stderr,
                    "app: the loc varp index said nothing in view %d depends on varp %d, "
                    "and the walk found %d placements that do\n",
                    view,
                    varp_id,
                    n);
                abort();
            }
        }
        else if( !building )
        {
            /* Walked on a hit: this varp drives a chain, which may resolve
             * to other links now. List them again on the next change. */
            index->state = 0;
        }
        WorldviewRegistry_SetActive(&app->worldviews, view);
        /* A change already pending for a tile is newer than the placement
         * this walk found there, and is kept. */
        for( int i = 0; i < n; ++i )
            app_world_loc_refresh_ops(
                app,
                pending[i].x,
                pending[i].z,
                pending[i].level,
                pending[i].loc_id,
                pending[i].shape,
                pending[i].angle,
                pending[i].op_flags,
                pending[i].ops);
        WorldviewRegistry_SetActive(&app->worldviews, previous_view);
        return n;
    }
}

static void
app_varp_refresh_loc_transforms(
    struct App* app,
    int varp_id)
{
    assert(app);
    if( !app->provider || varp_id < 0 )
        return;
    /* Each view has independent scene-local tile keys. In particular, (3,2)
     * on a raft must never retype (3,2) in the root or another boat. */
    for( int view = 0; view < WORLDVIEW_MAX; ++view )
    {
        if( !WorldviewRegistry_IsLive(&app->worldviews, view) )
            continue;
        (void)app_varp_refresh_view_loc_transforms(app, view, varp_id);
    }
}

int
app_varp_refresh_root_loc_transforms(
    struct App* app,
    int varp_id)
{
    assert(app);
    assert(varp_id >= 0);
    if( !app->provider )
        return 0;
    return app_varp_refresh_view_loc_transforms(app, WORLDVIEW_ROOT, varp_id);
}

/*
 * Plain value-change callback: loc transforms + anything else that must react
 * to optimistic CS2/IF1 writes as well as server VARP packets. Must NOT feed
 * the CS2 var-transmit ring (that is app_varp_server_update only).
 */
void
app_varp_change(
    void* userdata,
    int varp_id)
{
    struct App* app = (struct App*)userdata;

    UITree_HostInputsChanged(&app->ui_host, UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_CLIENT_STATE));
    RedrawRequest_Raise(&app->redraw_request);
    app_varp_refresh_loc_transforms(app, varp_id);
    app_varp_refresh_npc_transforms(app, varp_id);
    /* Modern audio slider clicks call GAMEOPTION/DEVICEOPTION directly, while
     * the four mute icons only write their backing varps. Both paths must
     * reach the same host snapshot; this is the reference's client-side varp
     * side effect and deliberately does not feed the var-transmit ring. */
    RS_CS2Host_SyncAudioVarp(&app->host, varp_id);
}

/*
 * Server varp update -> CS2 host, so the tick's var-transmit pump re-dispatches
 * the hooks that list this varp as a trigger. Userdata is the app, not the host,
 * because the same callback routes client-code varps (the sound volume setting)
 * to their subsystems.
 *
 * Deliberately NOT wired to the plain value-change callback, and not wired to
 * varcs at all. The reference feeds its changed-varp ring only from the
 * VARP_SMALL / VARP_LARGE / VARP_RESET packet handlers: a script-side write
 * (CS2 SETVARP, IF1 button, varbit set) updates the varp and notifies the
 * server but never enters the ring, and `Varcs` writes touch nothing beyond
 * their own map. Wiring either of those in makes the dispatch self-feeding —
 * a hook whose script writes a var bumps the change serial, which re-triggers
 * that same hook next tick, forever. That is what had rev230's gameframe
 * rebuilding the popout strip, the world-hop list (601 dynamic children) and
 * the 161|36 listener from scratch every ~8 frames, and it is why the hovered
 * component id climbed without end: every rebuild hands the same three popout
 * icons brand-new dynamic uids.
 *
 * Loc remorph runs from ChangeFn (also fired by ApplySmall/Large when the value
 * actually changes), so this path only adds CS2 transmit + clientcode audio.
 */
void
app_varp_server_update(
    void* userdata,
    int varp_id)
{
    struct App* app = (struct App*)userdata;
    assert(app);

    /* varp_changed is stamped by the packet model, right after the write
     * whose server update this is (game/rs_server_state.c). */

    RS_CS2Host_NotifyVarChanged(&app->host, varp_id);

    /* Client-code varps are settings the client acts on rather than displays.
     * Code 4 is the sound-effect volume slider (reference Client.updateVarp:
     * 0..3 pick 128/96/64/32, 4 mutes) — the only one audio cares about, and the
     * only reason the player's volume choice reaches the platform at all.
     *
     * Codes 18 and 22 are the Controls panel's two Attack-options dropdowns.
     * They are read here rather than by the minimenu builder because the
     * reference stores the DERIVED enum, not the varp: the builder must not see
     * the zero a never-transmitted varp holds (that reads as "Depends on combat
     * levels" while the reference is still suppressing every Attack row). */
    switch( VarPManager_GetClientcode(&app->varps, varp_id) )
    {
    case 4:
        RS_Audio_SetVolumeLevel(
            &app->audio, VarPManager_GetVarp(&app->varps, varp_id), &app->audio_out);
        break;
    case RS_ATTACK_OPTION_CLIENTCODE_PLAYER:
        if( app->features.attack_option_model == TORIRS_ATTACK_OPTION_MODEL_SETTINGS )
            RS_PlayerOps_SetPlayerAttackOption(
                &app->player_ops,
                RS_AttackOption_FromVarp(VarPManager_GetVarp(&app->varps, varp_id)));
        break;
    case RS_ATTACK_OPTION_CLIENTCODE_NPC:
        if( app->features.attack_option_model == TORIRS_ATTACK_OPTION_MODEL_SETTINGS )
            RS_PlayerOps_SetNpcAttackOption(
                &app->player_ops,
                RS_AttackOption_FromVarp(VarPManager_GetVarp(&app->varps, varp_id)));
        break;
    default:
        break;
    }
}
