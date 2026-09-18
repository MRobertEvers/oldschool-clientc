#include "game/task_entity_assets.h"

#include "app.h"
#include "engine/cache_provider.h"
#include "engine/entity_model_build.h"
#include "engine/player_appearance.h"
#include "game/rs_entity_sync.h"
#include "net/rev/packets/pkt_player_appearance.h"
#include "toridraw_animation.h"
#include "toridraw_scene.h"
#include "world/world.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* The five stance sequences an npc type names, in the order NpcMultiLoad
 * used to fetch them. */
static void
npc_stance_seqs(
    struct ToriRS_Npctype const* npctype,
    int out[5])
{
    assert(npctype);
    assert(out);
    out[0] = npctype->readyanim;
    out[1] = npctype->walkanim;
    out[2] = npctype->walkanim_b;
    out[3] = npctype->walkanim_r;
    out[4] = npctype->walkanim_l;
}

int
EntityAssets_NpcBodyResident(
    struct App* app,
    int npc_id)
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
    npc_stance_seqs(npctype, seqs);
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
 * Queue a loader for every distinct id as a sibling counted on `pending`.
 * Duplicates are skipped because the loaders only decline an id that is
 * already RESIDENT, and two in flight for one id would both decode it.
 */
static void
fanout_models(
    struct App* app,
    int const* ids,
    int count,
    int* pending)
{
    for( int i = 0; i < count; i++ )
    {
        int dup = 0;
        if( ids[i] < 0 )
            continue;
        for( int j = 0; j < i && !dup; j++ )
            dup = ids[j] == ids[i];
        if( dup )
            continue;
        ToriRS_TaskQueue_AddJoined(
            app->runner.queue, CreateTask_ModelLoad(app->provider, ids[i]), pending);
    }
}

static void
fanout_seqs(
    struct App* app,
    int const* ids,
    int count,
    int* pending)
{
    for( int i = 0; i < count; i++ )
    {
        int dup = 0;
        if( ids[i] < 0 )
            continue;
        for( int j = 0; j < i && !dup; j++ )
            dup = ids[j] == ids[i];
        if( dup )
            continue;
        ToriRS_TaskQueue_AddJoined(
            app->runner.queue,
            CreateTask_SequenceLoad(app->provider, app->scene, ids[i]),
            pending);
    }
}

/* The idk / obj config behind every slot, one sibling each (a kit and an
 * obj config never share an id space, so only same-kind duplicates skip). */
static void
fanout_slot_configs(
    struct App* app,
    int const slots[12],
    int* pending)
{
    for( int i = 0; i < 12; i++ )
    {
        int dup = 0;
        struct ToriRS_Task* task;
        for( int j = 0; j < i && !dup; j++ )
            dup = slots[j] == slots[i];
        if( dup )
            continue;
        switch( Appearance_SlotKind(slots[i]) )
        {
        case APPEARANCE_SLOT_KIT:
            task = CreateTask_IdkLoad(app->provider, Appearance_SlotKit(slots[i]));
            break;
        case APPEARANCE_SLOT_OBJ:
            task = CreateTask_ObjLoad(app->provider, Appearance_SlotObj(slots[i]));
            break;
        case APPEARANCE_SLOT_EMPTY:
        default:
            task = NULL;
            break;
        }
        if( task )
            ToriRS_TaskQueue_AddJoined(app->runner.queue, task, pending);
    }
}

/* ------------------------------------------------------------------ npc */

struct Task_NpcBodyLand
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    int base_npc_id;
    int npc_id;
    /* This task holds the in-flight entry for its type (the table was not
     * full when it was made), so it is the one to clear it. */
    int registered;
    int pending;
};

static int
npc_body_land_inflight(
    struct App* app,
    int base_npc_id,
    int npc_id)
{
    for( int i = 0; i < app->npc_body_lands.count; i++ )
        if( app->npc_body_lands.base[i] == base_npc_id && app->npc_body_lands.type[i] == npc_id )
            return 1;
    return 0;
}

static void
npc_body_land_forget(
    struct App* app,
    int base_npc_id,
    int npc_id)
{
    for( int i = 0; i < app->npc_body_lands.count; i++ )
    {
        if( app->npc_body_lands.base[i] != base_npc_id || app->npc_body_lands.type[i] != npc_id )
            continue;
        app->npc_body_lands.count--;
        app->npc_body_lands.base[i] = app->npc_body_lands.base[app->npc_body_lands.count];
        app->npc_body_lands.type[i] = app->npc_body_lands.type[app->npc_body_lands.count];
        return;
    }
}

static int
Task_NpcBodyLand_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
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
            npc_stance_seqs(npctype, seqs);
            fanout_seqs(app, seqs, 5, &self->pending);
        }
    }
    PT_TASK_JOIN(pending);

    /* Forgotten BEFORE the walk: an npc of this type spawned from here on
     * finds the body resident and queues nothing; one spawned during the
     * load found this entry and queued nothing either, and the walk below
     * is what dresses it. */
    if( self->registered )
        npc_body_land_forget(app, self->base_npc_id, self->npc_id);
    self->registered = 0;

    if( app->world )
    {
        struct World_EntityPool* pool = &app->world->entities.npc;
        for( int idx = 0; idx < pool->count; idx++ )
        {
            struct WorldEntity_NPC* npc;
            if( !World_EntityPoolIsActive(pool, idx) )
                continue;
            npc = World_EntityPoolGet(pool, idx);
            /* Retyped while the body was on the wire: that op queued its
             * own load. */
            if( !npc || npc->base_npc_id != self->base_npc_id || npc->npc_id != self->npc_id )
                continue;
            App_WorldApplyNpcType(app, idx, npc->element_id, self->npc_id, self->base_npc_id);
            app->need_redraw = 1;
        }
    }

    PT_END(&self->pt);
}

static void
Task_NpcBodyLand_Free(struct ToriRS_Task* base)
{
    struct Task_NpcBodyLand* self = (struct Task_NpcBodyLand*)base;
    /* A task freed before it ran to its end (a queue torn down mid-load)
     * must not leave its type marked in flight for the rest of the session. */
    if( self->registered )
        npc_body_land_forget(self->app, self->base_npc_id, self->npc_id);
    free(base);
}

static struct ToriRS_TaskVTable Task_NpcBodyLand_VTable = {
    .run = Task_NpcBodyLand_Run,
    .free = Task_NpcBodyLand_Free,
};

struct ToriRS_Task*
CreateTask_NpcBodyLand(
    struct App* app,
    int base_npc_id,
    int npc_id)
{
    struct Task_NpcBodyLand* task;
    int const capacity =
        (int)(sizeof(app->npc_body_lands.base) / sizeof(app->npc_body_lands.base[0]));

    assert(app);
    assert(npc_id >= 0);
    if( npc_body_land_inflight(app, base_npc_id, npc_id) )
        return NULL;
    /* A full table is a crowd of sixty-four distinct types arriving in one
     * pass; the sixty-fifth simply loads without the dedupe, as every type
     * did before there was one. */
    task = calloc(1, sizeof(*task));
    assert(task);
    if( app->npc_body_lands.count < capacity )
    {
        app->npc_body_lands.base[app->npc_body_lands.count] = base_npc_id;
        app->npc_body_lands.type[app->npc_body_lands.count] = npc_id;
        app->npc_body_lands.count++;
        task->registered = 1;
    }
    task->task.vtable = &Task_NpcBodyLand_VTable;
    strncpy(task->task.name, "NpcBodyLand", sizeof(task->task.name) - 1);
    task->app = app;
    task->base_npc_id = base_npc_id;
    task->npc_id = npc_id;
    PT_INIT(&task->pt);
    return &task->task;
}

/* --------------------------------------------------------------- player */

struct Task_PlayerBodyLand
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    int slots[12];
    int gender;
    int seq_ids[7];
    int seq_count;
    int model_ids[64];
    int model_count;
    int pending;
};

static int
Task_PlayerBodyLand_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_PlayerBodyLand* self = (struct Task_PlayerBodyLand*)base;
    struct App* app = self->app;

    (void)io;
    PT_BEGIN(&self->pt);

    /* The configs name the models, so they land first; the stances need
     * nothing and ride along with them. */
    fanout_slot_configs(app, self->slots, &self->pending);
    fanout_seqs(app, self->seq_ids, self->seq_count, &self->pending);
    PT_TASK_JOIN(pending);

    self->model_count = PlayerModel_CollectAppearanceModelIds(
        app->provider,
        self->slots,
        self->gender,
        self->model_ids,
        (int)(sizeof(self->model_ids) / sizeof(self->model_ids[0])));
    fanout_models(app, self->model_ids, self->model_count, &self->pending);
    PT_TASK_JOIN(pending);

    /* Nothing to apply: every player whose wanted body this completes
     * rebuilds on the next frame's reconcile. */
    app->need_redraw = 1;

    PT_END(&self->pt);
}

static void
Task_PlayerBodyLand_Free(struct ToriRS_Task* base)
{
    free(base);
}

static struct ToriRS_TaskVTable Task_PlayerBodyLand_VTable = {
    .run = Task_PlayerBodyLand_Run,
    .free = Task_PlayerBodyLand_Free,
};

struct ToriRS_Task*
CreateTask_PlayerBodyLand(
    struct App* app,
    int const slots[12],
    int gender,
    int const* seq_ids,
    int seq_count)
{
    struct Task_PlayerBodyLand* task;

    assert(app);
    assert(slots);
    assert(seq_ids || seq_count == 0);
    assert(seq_count >= 0);
    task = calloc(1, sizeof(*task));
    assert(task);
    assert(seq_count <= (int)(sizeof(task->seq_ids) / sizeof(task->seq_ids[0])));
    task->task.vtable = &Task_PlayerBodyLand_VTable;
    strncpy(task->task.name, "PlayerBodyLand", sizeof(task->task.name) - 1);
    task->app = app;
    memcpy(task->slots, slots, sizeof(task->slots));
    task->gender = gender;
    memcpy(task->seq_ids, seq_ids, (size_t)seq_count * sizeof(int));
    task->seq_count = seq_count;
    PT_INIT(&task->pt);
    return &task->task;
}

/* ----------------------------------------------------------- held items */

struct Task_PlayerHeldLand
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    int server_pid;
    int seq_id;
    int held_vals[2]; /* replaceheld left/right, as canonical appearance slots */
    int cfg_i;
    int model_ids[64];
    int model_count;
    int pending;
};

static int
Task_PlayerHeldLand_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_PlayerHeldLand* self = (struct Task_PlayerHeldLand*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);

    PT_TASK_AWAITSELF_IF(CreateTask_SequenceLoad(app->provider, app->scene, self->seq_id));

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
        struct ToriDraw_Animation* prim = ToriDraw_SceneAnimationGet(app->scene, self->seq_id);
        self->held_vals[0] = Appearance_FromCacheValue(prim ? prim->replaceheldleft : -1);
        self->held_vals[1] = Appearance_FromCacheValue(prim ? prim->replaceheldright : -1);
    }
    if( Appearance_SlotKind(self->held_vals[0]) != APPEARANCE_SLOT_OBJ &&
        Appearance_SlotKind(self->held_vals[1]) != APPEARANCE_SLOT_OBJ )
        PT_EXIT(&self->pt);

    /* Obj configs first: only an obj-range value names an obj. */
    for( self->cfg_i = 0; self->cfg_i < 2; self->cfg_i++ )
        PT_TASK_AWAITSELF_IF(
            Appearance_SlotKind(self->held_vals[self->cfg_i]) == APPEARANCE_SLOT_OBJ
                ? CreateTask_ObjLoad(app->provider, Appearance_SlotObj(self->held_vals[self->cfg_i]))
                : NULL);

    /* Then their gendered wear models (slot 3 = right hand, slot 5 = left
     * hand -- the appearance encoding the swap feeds the build). */
    {
        int world_idx = -1;
        struct WorldEntity_Player* player = NULL;
        int held_slots[12];

        if( RS_EntitySync_FindPlayer(&app->esync, self->server_pid, &world_idx, NULL) &&
            world_idx >= 0 )
            player = World_EntityPoolGet(&app->world->entities.player, world_idx);
        for( int k = 0; k < 12; k++ )
            held_slots[k] = -1;
        held_slots[3] = Appearance_SlotKind(self->held_vals[1]) == APPEARANCE_SLOT_OBJ
                            ? self->held_vals[1]
                            : -1;
        held_slots[5] = Appearance_SlotKind(self->held_vals[0]) == APPEARANCE_SLOT_OBJ
                            ? self->held_vals[0]
                            : -1;
        self->model_count = PlayerModel_CollectAppearanceModelIds(
            app->provider,
            held_slots,
            player ? player->gender : 0,
            self->model_ids,
            (int)(sizeof(self->model_ids) / sizeof(self->model_ids[0])));
    }
    fanout_models(app, self->model_ids, self->model_count, &self->pending);
    PT_TASK_JOIN(pending);

    /* The per-frame body reconcile puts them in the player's hands. */
    app->need_redraw = 1;

    PT_END(&self->pt);
}

static void
Task_PlayerHeldLand_Free(struct ToriRS_Task* base)
{
    free(base);
}

static struct ToriRS_TaskVTable Task_PlayerHeldLand_VTable = {
    .run = Task_PlayerHeldLand_Run,
    .free = Task_PlayerHeldLand_Free,
};

struct ToriRS_Task*
CreateTask_PlayerHeldLand(
    struct App* app,
    int server_pid,
    int seq_id)
{
    struct Task_PlayerHeldLand* task;

    assert(app);
    assert(server_pid >= 0);
    if( seq_id < 0 )
        return NULL;
    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_PlayerHeldLand_VTable;
    strncpy(task->task.name, "PlayerHeldLand", sizeof(task->task.name) - 1);
    task->app = app;
    task->server_pid = server_pid;
    task->seq_id = seq_id;
    PT_INIT(&task->pt);
    return &task->task;
}
