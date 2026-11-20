#include "game/task_entity_assets.h"
#include "mem/torirs_mem.h"

#include "app/app_parallel_task.h"

#include "app.h"
#include "engine/cache_provider.h"
#include "engine/entity_model_build.h"
#include "engine/player_appearance.h"
#include "game/rs_entity_sync.h"
#include "engine/asset_requests.h"
#include "net/rev/packets/pkt_player_appearance.h"
#include "toridraw_animation.h"
#include "toridraw_scene.h"
#include "world/world.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* The five stance sequences an npc drawn as `npctype` plays, in the order
 * NpcMultiLoad fetched them: the SHELL's (ToriRS_NpctypeEntityFacts -- the
 * wire's record supplies the stance set, the rung the body), so the body
 * fetched is the one the spawn, the retype and NpcMultiLoad play (FAULTS E4:
 * this read the drawn type's set). `shell` NULL: no separate shell. */
static void
npc_stance_seqs(
    struct ToriRS_Npctype const* npctype,
    struct ToriRS_Npctype const* shell,
    int out[5])
{
    struct ToriRS_NpcEntityFacts facts;

    assert(npctype);
    assert(out);
    ToriRS_NpctypeEntityFacts(npctype, shell, &facts);
    out[0] = facts.readyanim;
    out[1] = facts.walkanim;
    out[2] = facts.walkanim_b;
    out[3] = facts.walkanim_r;
    out[4] = facts.walkanim_l;
}

/* The shell record behind `base_npc_id`, or NULL (none, or not resident:
 * ToriRS_NpctypeEntityFacts then reads the drawn type alone). */
static struct ToriRS_Npctype const*
npc_shell(
    struct App* app,
    int base_npc_id)
{
    return base_npc_id >= 0 ? CacheProvider_NpctypeGet(app->provider, base_npc_id) : NULL;
}

int
EntityAssets_NpcBodyResident(
    struct App* app,
    int npc_id,
    int base_npc_id)
{
    struct ToriRS_Npctype* npctype;
    int seqs[5];

    assert(app);
    if( npc_id < 0 )
        return 1;
    npctype = CacheProvider_NpctypeGet(app->provider, npc_id);
    if( !npctype )
        return 1;
    for( int i = 0; i < npctype->models_count; i++ )
        if( npctype->models[i] >= 0 && !CacheProvider_ModelHas(app->provider, npctype->models[i]) )
            return 0;
    npc_stance_seqs(npctype, npc_shell(app, base_npc_id), seqs);
    for( int i = 0; i < 5; i++ )
        if( seqs[i] >= 0 && !ToriDraw_SceneAnimationHas(app->scene, seqs[i]) )
            return 0;
    return 1;
}

int
EntityAssets_PlayerBodyResident(
    struct App* app,
    int const slots[12],
    int gender,
    int const* seq_ids,
    int seq_count)
{
    assert(app);
    assert(slots);
    assert(seq_ids || seq_count == 0);
    if( !PlayerModel_AppearanceResident(app->provider, slots, gender) )
        return 0;
    for( int i = 0; i < seq_count; i++ )
        if( seq_ids[i] >= 0 && !ToriDraw_SceneAnimationHas(app->scene, seq_ids[i]) )
            return 0;
    return 1;
}

/*
 * Await every id through the assets table, counted on `pending`. The table
 * holds at most one load per id however many fan-outs (and the per-frame
 * binders) ask for it, so a model or a stance two bodies share is read once;
 * a duplicate in one list is the same entry asked twice.
 */
static void
fanout_models(
    struct App* app,
    int const* ids,
    int count,
    int* pending)
{
    for( int i = 0; i < count; i++ )
        if( ids[i] >= 0 )
            Assets_Await(app->assets, AssetKey_Make(ASSET_KIND_MODEL, ids[i], 0), pending);
}

static void
fanout_seqs(
    struct App* app,
    int const* ids,
    int count,
    int* pending)
{
    for( int i = 0; i < count; i++ )
        if( ids[i] >= 0 )
            Assets_Await(app->assets, AssetKey_Make(ASSET_KIND_SEQUENCE, ids[i], 0), pending);
}

/* The config behind one appearance value, through the assets table
 * (ASSET_KIND_CONFIG): one read however many bodies and held items in flight
 * name it (each fetch queued its own before). Nothing for an empty slot. */
static void
await_slot_config(
    struct App* app,
    int slot,
    int* pending)
{
    enum AppearanceSlotKind const kind = Appearance_SlotKind(slot);

    if( kind == APPEARANCE_SLOT_KIT )
        Assets_Await(
            app->assets,
            AssetKey_Make(ASSET_KIND_CONFIG, Appearance_SlotKit(slot), ASSET_CONFIG_IDK),
            pending);
    else if( kind == APPEARANCE_SLOT_OBJ )
        Assets_Await(
            app->assets,
            AssetKey_Make(ASSET_KIND_CONFIG, Appearance_SlotObj(slot), ASSET_CONFIG_OBJ),
            pending);
}

/* The idk / obj config behind every slot. A value named twice is the same
 * entry asked twice. */
static void
fanout_slot_configs(
    struct App* app,
    int const slots[12],
    int* pending)
{
    for( int i = 0; i < 12; i++ )
        await_slot_config(app, slots[i], pending);
}

/* ------------------------------------------------------------------ npc */

struct Task_NpcBodyLand
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    int base_npc_id;
    int npc_id;
    /* The end has been reported to the table. */
    int ended;
    int pending;
};

static int
Task_NpcBodyLand_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_NpcBodyLand* self = (struct Task_NpcBodyLand*)base;
    struct App* app = self->app;

    (void)io;
    PT_BEGIN(&self->pt);

    {
        struct ToriRS_Npctype* npctype = CacheProvider_NpctypeGet(app->provider, self->npc_id);
        if( npctype )
        {
            int seqs[5];
            fanout_models(app, npctype->models, npctype->models_count, &self->pending);
            npc_stance_seqs(npctype, npc_shell(app, self->base_npc_id), seqs);
            fanout_seqs(app, seqs, 5, &self->pending);
        }
    }
    PT_TASK_JOIN(pending);

    /* The end wakes every npc's body task (app/app_tasks_entities.c,
     * Task_NpcBody) that awaits this (type, base): each re-resolves its own
     * npc and mounts the body. */
    self->ended = 1;
    Assets_Finished(
        app->assets, AssetKey_Make(ASSET_KIND_NPC_BODY, self->npc_id, self->base_npc_id), true);

    PT_END(&self->pt);
}

static void
Task_NpcBodyLand_Free(struct ToriRS_Task* base)
{
    struct Task_NpcBodyLand* self = (struct Task_NpcBodyLand*)base;
    /* A task freed before it ran to its end (a queue torn down mid-load)
     * must not leave its type loading for the rest of the session. */
    if( !self->ended )
    {
        if( self->pending > 0 )
            Assets_Unjoin(self->app->assets, &self->pending);
        Assets_Abandoned(
            self->app->assets, AssetKey_Make(ASSET_KIND_NPC_BODY, self->npc_id, self->base_npc_id));
    }
    ToriMem_Free(ToriMem_Session(), base);
}

static struct ToriRS_TaskVTable Task_NpcBodyLand_VTable = {
    .run = Task_NpcBodyLand_Run,
    .free = Task_NpcBodyLand_Free,
};

/* NPC_BODY's loader: id = the type drawn, variant = its base. */
static enum AssetStart
npc_body_start(
    void* data,
    struct Assets* assets,
    struct AssetKey key)
{
    struct App* app = data;
    struct Task_NpcBodyLand* task;

    (void)assets;
    task = ToriMem_Calloc(ToriMem_Session(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_NpcBodyLand_VTable;
    strncpy(task->task.name, "NpcBodyLand", sizeof(task->task.name) - 1);
    task->app = app;
    task->base_npc_id = key.variant;
    task->npc_id = key.id;
    PT_INIT(&task->pt);
    ToriRS_TaskQueue_Add(app->frame_runner.queue, &task->task);
    return ASSET_START_LOADING;
}

static bool
npc_body_resident(
    void* data,
    struct AssetKey key)
{
    return EntityAssets_NpcBodyResident((struct App*)data, key.id, key.variant) != 0;
}

/* --------------------------------------------------------------- player */

/*
 * A player's body fetch: ONE parallel task per distinct appearance in flight,
 * listed in the pending index (ENTITY_ASSETS_KIND_PLAYER_BODY) under a hash
 * of what it fetches, so a second appearance block naming the same body while
 * the first is still loading -- every PLAYER_INFO that re-sends a stranger's
 * appearance, or the design screen's prediction followed by the server's
 * echo -- finds it and adds nothing (each queued its own fetch before).
 *
 * Why the pending index and not an assets-table entry of its own: a table
 * entry is keyed (kind, id, variant) and its loader is handed nothing but that
 * key, and twelve slots, a gender and seven stances do not fit in it. A hash
 * as the id would need a second table beside it to give the slots back to the
 * loader -- the second tracker this replaces -- and two bodies whose hashes
 * collided would share one entry and the second would never load. The parts
 * are already one entry each (CONFIG, MODEL, SEQUENCE) and shared by every
 * fetch; what needs finding is the fetch itself, and a found task carries its
 * own data, so a collision is told apart by comparing it and costs at most a
 * second fetch, never a missing one.
 */
struct Task_PlayerBodyLand
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_ParallelTask par;
    struct App* app;
    int slots[12];
    int gender;
    int seq_ids[ENTITY_ASSETS_PLAYER_SEQS_MAX];
    int seq_count;
    int model_ids[64];
    int model_count;
};

static int
Task_PlayerBodyLand_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_PlayerBodyLand* self = (struct Task_PlayerBodyLand*)base;
    struct App* app = self->app;

    (void)io;
    PT_BEGIN(&self->pt);

    /* The configs name the models, so they land first; the stances need
     * nothing and ride along with them. */
    fanout_slot_configs(app, self->slots, &self->par.join);
    fanout_seqs(app, self->seq_ids, self->seq_count, &self->par.join);
    PT_PARALLEL_AWAIT_ASSETS(app->assets);
    /* A logout cancelled it: nothing is drawn from it any more. */
    if( RS_ParallelTask_Cancelled(&self->par) )
        PT_EXIT(&self->pt);

    self->model_count = PlayerModel_CollectAppearanceModelIds(
        app->provider,
        self->slots,
        self->gender,
        self->model_ids,
        (int)(sizeof(self->model_ids) / sizeof(self->model_ids[0])));
    fanout_models(app, self->model_ids, self->model_count, &self->par.join);
    PT_PARALLEL_AWAIT_ASSETS(app->assets);
    if( RS_ParallelTask_Cancelled(&self->par) )
        PT_EXIT(&self->pt);

    /* Nothing to apply: every player whose wanted body this completes
     * rebuilds on the next frame's reconcile. */
    RedrawRequest_Raise(&app->redraw_request);

    PT_END(&self->pt);
}

static void
Task_PlayerBodyLand_Free(struct ToriRS_Task* base)
{
    struct Task_PlayerBodyLand* self = (struct Task_PlayerBodyLand*)base;
    /* Unlists it, and forgets a join still counted (a queue torn down
     * mid-fetch). */
    App_ParallelTaskRelease(self->app, &self->par);
    ToriMem_Free(ToriMem_Session(), base);
}

static struct ToriRS_TaskVTable Task_PlayerBodyLand_VTable = {
    .run = Task_PlayerBodyLand_Run,
    .free = Task_PlayerBodyLand_Free,
};

/* FNV-1a over what a body fetch reads: the slots, the gender, the stances. */
static uint32_t
player_body_hash(
    int const slots[12],
    int gender,
    int const* seq_ids,
    int seq_count)
{
    uint32_t h = 2166136261u;
    int words[12 + 2 + ENTITY_ASSETS_PLAYER_SEQS_MAX];
    int n = 0;

    for( int i = 0; i < 12; i++ )
        words[n++] = slots[i];
    words[n++] = gender;
    words[n++] = seq_count;
    for( int i = 0; i < seq_count; i++ )
        words[n++] = seq_ids[i];
    for( int i = 0; i < n; i++ )
    {
        uint32_t w = (uint32_t)words[i];
        for( int b = 0; b < 4; b++ )
        {
            h ^= (w >> (8 * b)) & 0xffu;
            h *= 16777619u;
        }
    }
    return h;
}

struct RS_PendingKey
EntityAssets_PlayerBodyKey(
    int const slots[12],
    int gender,
    int const* seq_ids,
    int seq_count)
{
    assert(slots);
    assert(seq_ids || seq_count == 0);
    assert(seq_count >= 0);
    assert(seq_count <= ENTITY_ASSETS_PLAYER_SEQS_MAX);
    return RS_PendingKey_Id((int)player_body_hash(slots, gender, seq_ids, seq_count), 0);
}

static int
player_body_same(
    struct Task_PlayerBodyLand const* task,
    int const slots[12],
    int gender,
    int const* seq_ids,
    int seq_count)
{
    return task->gender == gender && task->seq_count == seq_count &&
           memcmp(task->slots, slots, sizeof(task->slots)) == 0 &&
           (seq_count == 0 || memcmp(task->seq_ids, seq_ids, (size_t)seq_count * sizeof(int)) == 0);
}

int
EntityAssets_WantPlayerBody(
    struct App* app,
    int const slots[12],
    int gender,
    int const* seq_ids,
    int seq_count)
{
    struct RS_PendingKey key;
    struct RS_ParallelTask* pending;
    struct Task_PlayerBodyLand* task;

    assert(app);
    assert(app->pending);
    key = EntityAssets_PlayerBodyKey(slots, gender, seq_ids, seq_count);
    if( EntityAssets_PlayerBodyResident(app, slots, gender, seq_ids, seq_count) )
        return 0;
    pending = RS_Pending_Find(app->pending, ENTITY_ASSETS_KIND_PLAYER_BODY, key);
    if( pending &&
        player_body_same(
            (struct Task_PlayerBodyLand const*)((char const*)pending -
                                                offsetof(struct Task_PlayerBodyLand, par)),
            slots,
            gender,
            seq_ids,
            seq_count) )
        return 0;

    task = ToriMem_Calloc(ToriMem_Session(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_PlayerBodyLand_VTable;
    strncpy(task->task.name, "PlayerBodyLand", sizeof(task->task.name) - 1);
    task->app = app;
    memcpy(task->slots, slots, sizeof(task->slots));
    task->gender = gender;
    if( seq_count > 0 )
        memcpy(task->seq_ids, seq_ids, (size_t)seq_count * sizeof(int));
    task->seq_count = seq_count;
    PT_INIT(&task->pt);
    /* The parallel queue, as an ordinary task: the body is only drawn, and
     * the reconcile keeps the last whole one until this is in. */
    App_ParallelTaskAdd(app, &task->task, &task->par, ENTITY_ASSETS_KIND_PLAYER_BODY, key, -1);
    return 1;
}

int
EntityAssets_PlayerBodyPending(struct App const* app)
{
    assert(app);
    return RS_Pending_Count(app->pending, ENTITY_ASSETS_KIND_PLAYER_BODY);
}

/* ------------------------------------------------------- player transmog
 *
 * The transmog and held-item fetches are LOADERS of the assets table
 * (PLAYER_TRANSMOG, PLAYER_HELD): one entry per key, one load in flight, found
 * by Assets_InFlight / Assets_State -- the table is their index. They apply
 * nothing (the per-frame reconcile reads the entity as it is now), so no
 * packet, scope or test has a target to find them by, and they are not
 * listed in the pending index. */

struct Task_PlayerTransmogLand
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    int npc_id;
    int resolved_npc_id;
    int pending;
    int ended;
};

static int
Task_PlayerTransmogLand_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_PlayerTransmogLand* self = (struct Task_PlayerTransmogLand*)base;
    struct App* app = self->app;

    (void)io;
    PT_BEGIN(&self->pt);

    /* The config chain first (the shell and every multinpc rung): the npc
     * the player is drawn as is whatever it selects. */
    PT_TASK_AWAITSELF_IF(CreateTask_NpcMultiResolve(app, self->npc_id, &self->resolved_npc_id));
    if( self->resolved_npc_id < 0 )
    {
        self->ended = 1;
        Assets_Finished(app->assets, AssetKey_Make(ASSET_KIND_PLAYER_TRANSMOG, self->npc_id, 0), true);
        PT_EXIT(&self->pt);
    }

    {
        struct ToriRS_Npctype* npctype = CacheProvider_NpctypeGet(app->provider, self->resolved_npc_id);
        if( npctype )
        {
            int seqs[5];
            fanout_models(app, npctype->models, npctype->models_count, &self->pending);
            /* The transmog's own record is the shell whose stances play. */
            npc_stance_seqs(npctype, npc_shell(app, self->npc_id), seqs);
            fanout_seqs(app, seqs, 5, &self->pending);
        }
    }
    PT_TASK_JOIN(pending);

    /* Nothing to apply: the per-frame body reconcile mounts the npc's model
     * on every player drawn as it (app_world_reconcile_player_transmog). */
    self->ended = 1;
    Assets_Finished(app->assets, AssetKey_Make(ASSET_KIND_PLAYER_TRANSMOG, self->npc_id, 0), true);
    RedrawRequest_Raise(&app->redraw_request);

    PT_END(&self->pt);
}

static void
Task_PlayerTransmogLand_Free(struct ToriRS_Task* base)
{
    struct Task_PlayerTransmogLand* self = (struct Task_PlayerTransmogLand*)base;
    if( !self->ended )
    {
        if( self->pending > 0 )
            Assets_Unjoin(self->app->assets, &self->pending);
        Assets_Abandoned(
            self->app->assets, AssetKey_Make(ASSET_KIND_PLAYER_TRANSMOG, self->npc_id, 0));
    }
    if( self->pt.user )
        task_free((struct ToriRS_Task*)self->pt.user);
    ToriMem_Free(ToriMem_Session(), base);
}

static struct ToriRS_TaskVTable Task_PlayerTransmogLand_VTable = {
    .run = Task_PlayerTransmogLand_Run,
    .free = Task_PlayerTransmogLand_Free,
};

/* PLAYER_TRANSMOG's loader. */
static enum AssetStart
player_transmog_start(
    void* data,
    struct Assets* assets,
    struct AssetKey key)
{
    struct App* app = data;
    struct Task_PlayerTransmogLand* task;

    (void)assets;
    task = ToriMem_Calloc(ToriMem_Session(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_PlayerTransmogLand_VTable;
    strncpy(task->task.name, "PlayerTransmogLand", sizeof(task->task.name) - 1);
    task->app = app;
    task->npc_id = key.id;
    task->resolved_npc_id = -1;
    PT_INIT(&task->pt);
    ToriRS_TaskQueue_Add(app->frame_runner.queue, &task->task);
    return ASSET_START_LOADING;
}

/* Is the npc a transmogged player is drawn as here: the shell's config and
 * every rung its multinpc chain takes under the current varps, then that
 * type's body and stances? A config that is not resident is "not here" (the
 * resolve would read it as "no transform"). */
static bool
player_transmog_resident(
    void* data,
    struct AssetKey key)
{
    struct App* app = data;
    int npc_id = key.id;

    for( int guard = 0; guard < TORIRS_NPC_MULTI_MAX_DEPTH; guard++ )
    {
        struct ToriRS_Npctype* npctype = CacheProvider_NpctypeGet(app->provider, npc_id);
        int resolved;

        if( !npctype )
            return false;
        if( npctype->transform_count <= 0 )
            break;
        resolved = VarPManager_ResolveTransform(
            &app->varps,
            npctype->transforms,
            npctype->transform_count,
            npctype->transform_varbit,
            npctype->transform_varp);
        if( resolved < 0 )
            return true; /* drawn as nothing: nothing to load */
        if( resolved == npc_id )
            break;
        npc_id = resolved;
    }
    return EntityAssets_NpcBodyResident(app, npc_id, key.id) != 0;
}

void
EntityAssets_WantPlayerTransmog(
    struct App* app,
    int npc_id)
{
    assert(app);
    assert(npc_id >= 0);
    (void)Assets_Want(app->assets, AssetKey_Make(ASSET_KIND_PLAYER_TRANSMOG, npc_id, 0), NULL);
}

/* ----------------------------------------------------------- held items */

struct Task_PlayerHeldLand
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    int seq_id;
    int gender;
    int held_vals[2]; /* replaceheld left/right, as canonical appearance slots */
    int model_ids[64];
    int model_count;
    int pending;
    int ended;
};

/* What a sequence puts in a player's hands, as appearance slots (3 = right,
 * 5 = left), or 0 when it holds no obj. The sequence must be resident. */
static int
held_slots_of(
    struct App* app,
    int seq_id,
    int held_vals[2],
    int held_slots[12])
{
    struct ToriDraw_Animation* prim = ToriDraw_SceneAnimationGet(app->scene, seq_id);

    held_vals[0] = Appearance_FromCacheValue(prim ? prim->replaceheldleft : -1);
    held_vals[1] = Appearance_FromCacheValue(prim ? prim->replaceheldright : -1);
    for( int k = 0; k < 12; k++ )
        held_slots[k] = -1;
    held_slots[3] = Appearance_SlotKind(held_vals[1]) == APPEARANCE_SLOT_OBJ ? held_vals[1] : -1;
    held_slots[5] = Appearance_SlotKind(held_vals[0]) == APPEARANCE_SLOT_OBJ ? held_vals[0] : -1;
    return held_slots[3] >= 0 || held_slots[5] >= 0;
}

static void
held_land_end(struct Task_PlayerHeldLand* self)
{
    self->ended = 1;
    Assets_Finished(
        self->app->assets,
        AssetKey_Make(ASSET_KIND_PLAYER_HELD, self->seq_id, self->gender),
        true);
}

static int
Task_PlayerHeldLand_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_PlayerHeldLand* self = (struct Task_PlayerHeldLand*)base;
    struct App* app = self->app;

    (void)io;
    PT_BEGIN(&self->pt);

    /* The sequence through the table: the track binder wants the same one
     * every frame, and the two share its one load. */
    Assets_Await(app->assets, AssetKey_Make(ASSET_KIND_SEQUENCE, self->seq_id, 0), &self->pending);
    PT_TASK_JOIN(pending);

    /*
     * Held-item replacement (reference ClientPlayer.getTempModel2 via
     * SeqType.replaceheldleft/right, opcodes 6/7): a woodcutting or mining
     * style seq swaps a worn item for an obj that is NOT part of the
     * player's appearance, so its config and wear models were never fetched
     * by the APPEARANCE path. The per-frame body reconcile
     * (app_world_reconcile_player_body) keeps the last whole body until the
     * held obj is resident, so it is fetched here.
     */
    {
        int held_slots[12];
        if( !held_slots_of(app, self->seq_id, self->held_vals, held_slots) )
        {
            held_land_end(self);
            PT_EXIT(&self->pt);
        }
    }

    /* Obj configs first: only an obj-range value names an obj. Through the
     * table, sharing a read with any body fetch that names the same obj. */
    for( int k = 0; k < 2; k++ )
        if( Appearance_SlotKind(self->held_vals[k]) == APPEARANCE_SLOT_OBJ )
            await_slot_config(app, self->held_vals[k], &self->pending);
    PT_TASK_JOIN(pending);

    /* Then their wear models, gendered as the entry's key says (slot 3 =
     * right hand, slot 5 = left hand -- the appearance encoding the swap
     * feeds the build). */
    {
        int held_slots[12];
        (void)held_slots_of(app, self->seq_id, self->held_vals, held_slots);
        self->model_count = PlayerModel_CollectAppearanceModelIds(
            app->provider,
            held_slots,
            self->gender,
            self->model_ids,
            (int)(sizeof(self->model_ids) / sizeof(self->model_ids[0])));
    }
    fanout_models(app, self->model_ids, self->model_count, &self->pending);
    PT_TASK_JOIN(pending);

    /* The per-frame body reconcile puts them in the player's hands. */
    held_land_end(self);
    RedrawRequest_Raise(&app->redraw_request);

    PT_END(&self->pt);
}

static void
Task_PlayerHeldLand_Free(struct ToriRS_Task* base)
{
    struct Task_PlayerHeldLand* self = (struct Task_PlayerHeldLand*)base;
    if( !self->ended )
    {
        if( self->pending > 0 )
            Assets_Unjoin(self->app->assets, &self->pending);
        Assets_Abandoned(
            self->app->assets, AssetKey_Make(ASSET_KIND_PLAYER_HELD, self->seq_id, self->gender));
    }
    ToriMem_Free(ToriMem_Session(), base);
}

static struct ToriRS_TaskVTable Task_PlayerHeldLand_VTable = {
    .run = Task_PlayerHeldLand_Run,
    .free = Task_PlayerHeldLand_Free,
};

/* PLAYER_HELD's loader: id = the seq, variant = the gender. */
static enum AssetStart
player_held_start(
    void* data,
    struct Assets* assets,
    struct AssetKey key)
{
    struct App* app = data;
    struct Task_PlayerHeldLand* task;

    (void)assets;
    task = ToriMem_Calloc(ToriMem_Session(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_PlayerHeldLand_VTable;
    strncpy(task->task.name, "PlayerHeldLand", sizeof(task->task.name) - 1);
    task->app = app;
    task->seq_id = key.id;
    task->gender = key.variant;
    PT_INIT(&task->pt);
    ToriRS_TaskQueue_Add(app->frame_runner.queue, &task->task);
    return ASSET_START_LOADING;
}

/* The sequence, and whatever it puts in the hands: their configs and their
 * wear models for the gender. */
static bool
player_held_resident(
    void* data,
    struct AssetKey key)
{
    struct App* app = data;
    int held_vals[2];
    int held_slots[12];
    int model_ids[64];
    int model_count;

    if( !ToriDraw_SceneAnimationHas(app->scene, key.id) )
        return false;
    if( !held_slots_of(app, key.id, held_vals, held_slots) )
        return true;
    for( int i = 0; i < 2; i++ )
        if( Appearance_SlotKind(held_vals[i]) == APPEARANCE_SLOT_OBJ &&
            !CacheProvider_ObjtypeGet(app->provider, Appearance_SlotObj(held_vals[i])) )
            return false;
    model_count = PlayerModel_CollectAppearanceModelIds(
        app->provider, held_slots, key.variant, model_ids, 64);
    for( int i = 0; i < model_count; i++ )
        if( model_ids[i] >= 0 && !CacheProvider_ModelHas(app->provider, model_ids[i]) )
            return false;
    return true;
}

void
EntityAssets_WantPlayerHeld(
    struct App* app,
    int seq_id,
    int gender)
{
    assert(app);
    assert(seq_id >= 0);
    (void)Assets_Want(app->assets, AssetKey_Make(ASSET_KIND_PLAYER_HELD, seq_id, gender), NULL);
}

void
EntityAssets_WantPlayerHeldForPid(
    struct App* app,
    int server_pid,
    int seq_id)
{
    int world_idx = -1;
    struct WorldEntity_Player const* player = NULL;

    assert(app);
    assert(seq_id >= 0);
    if( server_pid >= 0 && RS_EntitySync_FindPlayer(&app->esync, server_pid, &world_idx, NULL) &&
        world_idx >= 0 )
        player = World_EntityPoolGet(&app->world->entities.player, world_idx);
    EntityAssets_WantPlayerHeld(app, seq_id, player ? player->gender : 0);
}

void
EntityAssets_RegisterLoaders(struct App* app)
{
    struct AssetLoader npc_body = { npc_body_start, npc_body_resident, app };
    struct AssetLoader transmog = { player_transmog_start, player_transmog_resident, app };
    struct AssetLoader held = { player_held_start, player_held_resident, app };

    assert(app);
    Assets_SetLoader(app->assets, ASSET_KIND_NPC_BODY, &npc_body);
    Assets_SetLoader(app->assets, ASSET_KIND_PLAYER_TRANSMOG, &transmog);
    Assets_SetLoader(app->assets, ASSET_KIND_PLAYER_HELD, &held);
}
