#include "task_exec_entity_info.h"
#include "mem/torirs_mem.h"

#include "app.h"
#include "engine/asset_requests.h"
#include "game/entity_info_scratch.h"
#include "game/rs_entity_apply.h"
#include "game/rs_entity_sync.h"
#include "game/rs_healthbar.h"
#include "game/rs_hitsplat.h"
#include "game/task_entity_assets.h"
#include "net/net.h"
#include "net/rev/packets/pkt_player_appearance.h"
#include "streams/rsprot_info.h"
#include "world/world.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

/*
 * PLAYER_INFO and NPC_INFO on the real client: what is the App's.
 *
 * The rules are game/rs_entity_apply.c's, the one copy both clients run. This
 * unit decodes the packet (the revision's reader), awaits the npc configs an
 * NPC_INFO names -- the one thing the rules REQUIRE that may not be resident
 * -- and answers the apply's lookups, makers and presentation notes from the
 * App: scene elements and bodies, the drive ring, the chatbox, asset wants,
 * the cache's npc-add script.
 */

/* Local-player sentinel while UPDATE_PID has not arrived. */
#define ENTITY_INFO_LOCAL_SENTINEL 2047

/*
 * TORIRS_NPCINFO_BREAKDOWN=<ms>: split one NPC_INFO into decode / await /
 * apply and print the split when it exceeds <ms>. The exec pipeline runs one
 * packet at a time, so file statics are safe accumulators.
 */
uint64_t
PlatformWindow_TicksUs(void);

static int g_npcinfo_bd_ms = -1;

static int
npcinfo_bd_on(void)
{
    if( g_npcinfo_bd_ms < 0 )
    {
        char const* v = getenv("TORIRS_NPCINFO_BREAKDOWN");
        g_npcinfo_bd_ms = (v && v[0]) ? atoi(v) : 0;
    }
    return g_npcinfo_bd_ms > 0;
}

#define BD_T0() (npcinfo_bd_on() ? PlatformWindow_TicksUs() : 0)

void
Task_EntityInfoScratchFree(struct App* app)
{
    assert(app);
    EntityInfoScratch_Free(&app->entity_info_scratch);
}

void
RS_EntityInfo_NpcOrigin(
    struct App* app,
    int* out_x,
    int* out_z,
    int* out_level)
{
    int world_idx;
    assert(app);
    assert(out_x);
    assert(out_z);
    assert(out_level);
    struct RS_EntitySync* esync = &app->esync;
    *out_x = 52;
    *out_z = 52;
    *out_level = 0;
    if( RS_EntitySync_FindPlayer(
            esync,
            esync->local_pid >= 0 ? esync->local_pid : ENTITY_INFO_LOCAL_SENTINEL,
            &world_idx,
            NULL) )
    {
        struct WorldEntity_Player* player =
            World_EntityPoolGet(&app->world->entities.player, world_idx);
        if( player )
        {
            /* routeX[0], not grid_position: the reference bases every
             * relative add on the destination tile. */
            *out_x = player->pathing.route_x[0];
            *out_z = player->pathing.route_z[0];
            *out_level = player->grid_position.level;
        }
    }
    /* Revision239 explicitly states the ROOT origin for NPC deltas. The
     * local player's route is deck-local aboard a boat and cannot anchor a
     * shore NPC or a projected deckhand. PLAYER_INFO never reads this state. */
    if( RS_NpcUpdateOrigin_IsSet(&app->npc_update_origin) )
    {
        *out_x = RS_NpcUpdateOrigin_X(&app->npc_update_origin);
        *out_z = RS_NpcUpdateOrigin_Z(&app->npc_update_origin);
    }
    if( Wevs_AboardView(&app->wevs) > 0 && Wevs_IsLive(&app->wevs, Wevs_AboardView(&app->wevs)) )
        *out_level = Wevs_Get(&app->wevs, Wevs_AboardView(&app->wevs))->parent_level;
}

/* ------------------------------------------------------------------ */
/* The App as the shared apply's host                                  */
/* ------------------------------------------------------------------ */

/* What one packet's host calls read: the App, and the npc types this packet
 * named with the rung each resolved to when its configs were awaited. */
struct EntityInfoHost
{
    struct App* app;
    int const* types;
    int const* resolved;
    int type_count;
};

/* Every type an op list names was awaited before the apply
 * (RS_EntityApply_NpcTypes): the answer is the awaited one. */
static int
host_npc_resolve(
    void* ctx,
    int base_type)
{
    struct EntityInfoHost const* h = ctx;

    for( int i = 0; i < h->type_count; i++ )
        if( h->types[i] == base_type )
            return h->resolved[i];
    assert(0 && "an npc type the packet task did not await");
    return base_type;
}

static int
host_npc_type(
    void* ctx,
    int npc_type,
    int base_type,
    struct RS_EntityNpcType* out)
{
    struct EntityInfoHost const* h = ctx;

    return App_WorldNpcTypeRecord(h->app, npc_type, base_type, out);
}

/*
 * The hitsplat type whose duration and slot-full policy a splat of wire type
 * `type` takes: the type its selector (opcodes 17/18) picks now, as the sprite
 * drawn is (FAULTS E8; deob class105: `method268` then `method9599` when the
 * record is a selector). A selector that picks nothing keeps the wire type's.
 * The accessors fall back to the reference's own defaults (70, discard) when
 * the cache has no record.
 */
static void
host_hitsplat(
    void* ctx,
    int type,
    int* out_duration,
    int* out_policy)
{
    struct EntityInfoHost const* h = ctx;
    int resolved = RS_Hitsplats_ResolveType(&h->app->hitsplats, &h->app->varps, type);
    int policy_type = resolved >= 0 ? resolved : type;

    *out_duration = RS_Hitsplats_DurationFor(&h->app->hitsplats, policy_type);
    *out_policy = RS_Hitsplats_SlotPolicyFor(&h->app->hitsplats, policy_type);
}

/* How long a bar lingers is the healthbar TYPE's (opcode 5). */
static int
host_headbar_persist(
    void* ctx,
    int type)
{
    struct EntityInfoHost const* h = ctx;

    return RS_Healthbars_TypeFor(&h->app->healthbars, type)->persist_cycles;
}

static int
host_home_view(
    void* ctx,
    int abs_x,
    int abs_z,
    int* out_x,
    int* out_z)
{
    struct EntityInfoHost const* h = ctx;

    return App_WevHomeViewForAbsTile(h->app, abs_x, abs_z, out_x, out_z);
}

/* The root's origin is RS_EntityInfo_NpcOrigin's. A deck's is
 * SET_NPC_UPDATE_ORIGIN as sent -- deck-local tiles, the deck's own build
 * area -- on the deck's own plane. */
static void
host_npc_origin(
    void* ctx,
    int view,
    int* out_x,
    int* out_z,
    int* out_level)
{
    struct EntityInfoHost const* h = ctx;

    if( view == WORLDVIEW_ROOT )
    {
        RS_EntityInfo_NpcOrigin(h->app, out_x, out_z, out_level);
        return;
    }
    *out_x = RS_NpcUpdateOrigin_X(&h->app->npc_update_origin);
    *out_z = RS_NpcUpdateOrigin_Z(&h->app->npc_update_origin);
    *out_level = App_WevDeckLevel(h->app, view);
}

static int
host_player_spawn(
    void* ctx,
    int tile_x,
    int tile_z,
    int level)
{
    struct EntityInfoHost const* h = ctx;

    return App_WorldSpawnSyncedPlayer(h->app, tile_x, tile_z, level);
}

static int
host_npc_spawn(
    void* ctx,
    int npc_type,
    int base_type,
    int tile_x,
    int tile_z,
    int level)
{
    struct EntityInfoHost const* h = ctx;

    return App_WorldSpawnSyncedNpc(h->app, npc_type, base_type, tile_x, tile_z, level);
}

/* Its body, when not resident, is wanted by the retype itself. */
static void
host_npc_retype(
    void* ctx,
    int idx,
    int element_id,
    int npc_type,
    int base_type)
{
    struct EntityInfoHost const* h = ctx;

    App_WorldApplyNpcType(h->app, idx, element_id, npc_type, base_type);
}

/* A new player's appearance, past the World write: the local name to the
 * chatbox, and the body's assets. The body is derived from the entity each
 * frame (app_world_reconcile_player_body) and rebuilt only once every part is
 * resident; PlayerBodyLand is only the fetch, on the parallel queue, so the
 * packet pipeline never parks on it. See game/task_entity_assets.h. */
static void
host_player_appearance(
    struct App* app,
    int idx,
    struct PktPlayerAppearance const* appearance)
{
    int const seqs[7] = {
        appearance->readyanim,  appearance->turnanim,   appearance->walkanim,
        appearance->walkanim_b, appearance->walkanim_l, appearance->walkanim_r,
        appearance->runanim,
    };

    App_WorldApplyPlayerAppearance(app, idx, appearance);
    /* Transmogged (the 0xffff entry): the body is the npc's, wanted through
     * the assets table. The fetch below still runs for the stances. */
    if( appearance->npc_id >= 0 )
        EntityAssets_WantPlayerTransmog(app, appearance->npc_id);
    if( !EntityAssets_PlayerBodyResident(app, appearance->slots, appearance->gender, seqs, 7) )
        ToriRS_TaskQueue_Add(
            app->frame_runner.queue,
            CreateTask_PlayerBodyLand(app, appearance->slots, appearance->gender, seqs, 7));
}

static void
host_present(
    void* ctx,
    struct RS_EntityNote const* n)
{
    struct EntityInfoHost const* h = ctx;
    struct App* app = h->app;

    switch( n->kind )
    {
    case RS_ENTITY_NOTE_NPC_GONE:
        App_EntityNpcGone(app, n->slot);
        break;
    case RS_ENTITY_NOTE_NPC_SPAWNED:
        /* Its body: none yet unless it is resident; a parallel task mounts it
         * when it is. */
        App_EntityNpcBodyWant(app, n->idx);
        break;
    case RS_ENTITY_NOTE_NPC_ADDED:
        /* Tell the drive ring and the plugins (FAULTS E2), and run the
         * cache's NPC_ADD script (FAULTS E1). See game/rs_client_trigger.h. */
        App_WorldNpcSpawnAnnounce(app, n->idx);
        App_ClientTriggerNpcAdd(app, n->idx);
        break;
    case RS_ENTITY_NOTE_NPC_SEQ:
        /* DRIVE_STAMP: npc_seq -- a=server slot b=seq c=tick. On the op's
         * ARRIVAL, not on the track changing: a seq re-sent while it is
         * already playing does not restart (world_apply_primary_animation),
         * so an npc repeating one attack seq would otherwise raise one edge
         * for the whole fight (WorldEntity_NPC.seq_sent_id). */
        if( n->slot >= 0 )
            App_DriveEvent(
                app, RS_EVENT_NPC_SEQ, n->slot, n->seq,
                (int32_t)(app->world->cycle / APP_SERVER_TICK_LOGIC_CYCLES), 0);
        /* Wanted, not loaded here: one load however many ask. */
        if( n->seq >= 0 )
            (void)Assets_Want(app->assets, AssetKey_Make(ASSET_KIND_SEQUENCE, n->seq, 0));
        break;
    case RS_ENTITY_NOTE_NPC_FACE:
        /* DRIVE_STAMP: npc_face -- a=server slot b=tile x c=tile z d=tick.
         * On the op's ARRIVAL, like npc_seq. Tiles are absolute, the wire's
         * half-tiles halved (the centre tile of a sized square). */
        if( n->slot >= 0 )
            App_DriveEvent(
                app, RS_EVENT_NPC_FACE, n->slot, n->x >> 1, n->z >> 1,
                (int32_t)(app->world->cycle / APP_SERVER_TICK_LOGIC_CYCLES));
        break;
    case RS_ENTITY_NOTE_PLAYER_APPEARANCE:
        host_player_appearance(app, n->idx, n->appearance);
        break;
    case RS_ENTITY_NOTE_PLAYER_SEQ:
        /* PlayerHeldLand fetches whatever the seq swaps into the player's
         * hands, gendered as the player is now: one load per (seq, gender). */
        if( n->seq >= 0 )
        {
            struct WorldEntity_Player const* player =
                World_EntityPoolGet(&app->world->entities.player, n->idx);
            EntityAssets_WantPlayerHeld(app, n->seq, player ? player->gender : 0);
        }
        break;
    case RS_ENTITY_NOTE_SPOTANIM:
        App_EntitySpotanimPrefetch(app, n->seq);
        break;
    case RS_ENTITY_NOTE_CHAT:
        RS_CS2Host_ChatAdd(&app->host, n->chat_type, n->sender, n->sender_key, n->text);
        App_NotifyChatMessage(app, n->chat_type, n->sender_key ? n->sender_key : "Player", n->text);
        break;
    case RS_ENTITY_NOTE_PLAYER_PLACED:
    case RS_ENTITY_NOTE_PLAYER_OP:
        break;
    }
}

static struct RS_EntityApplyHost const k_app_host = {
    .ctx = NULL,
    .npc_resolve = host_npc_resolve,
    .npc_type = host_npc_type,
    .hitsplat = host_hitsplat,
    .headbar_persist = host_headbar_persist,
    .home_view = host_home_view,
    .npc_origin = host_npc_origin,
    .player_spawn = host_player_spawn,
    .npc_spawn = host_npc_spawn,
    .npc_retype = host_npc_retype,
    .present = host_present,
};

static void
entity_apply_in(
    struct App* app,
    struct EntityInfoHost* h,
    struct RS_EntityApplyHost* host,
    struct RS_EntityApplyIn* in)
{
    assert(app->net);
    *host = k_app_host;
    host->ctx = h;
    h->app = app;
    in->world = app->world;
    in->esync = &app->esync;
    in->rev = app->net->rev;
    in->host = host;
    in->scene_base_x = RS_RebuildRegion_SceneBaseX(&app->rebuild_region);
    in->scene_base_z = RS_RebuildRegion_SceneBaseZ(&app->rebuild_region);
}

/* ------------------------------------------------------------------ */
/* PLAYER_INFO                                                         */
/* ------------------------------------------------------------------ */

struct Task_ExecPlayerInfo
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    uint8_t const* data; /* borrowed from the owning packet task */
    int length;

    RsprotInfoList info;
    struct RsprotInfoOp* ops; /* info.ops, or a replay keyframe's */
    int op_count;
};

static int
Task_ExecPlayerInfo_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_ExecPlayerInfo* self = (struct Task_ExecPlayerInfo*)base;
    struct App* app = self->app;

    /* Nothing in a PLAYER_INFO is awaited (a new player's body is visual);
     * the task keeps its shape for the replay's decoded lists. */
    (void)io;
    PT_BEGIN(&self->pt);

    /* A replay keyframe hands its op list over already decoded
     * (CreateTask_ExecPlayerInfoOps); a packet is read here. */
    if( !self->ops )
    {
        EntityInfoScratch_Borrow(&app->entity_info_scratch.player, &self->info);
        self->ops = self->info.ops;
        /* The revision's reader (GameProtoRevTable.player_info_read). Every
         * reader writes the same op list. */
        assert(app->net);
        assert(app->net->rev->player_info_read);
        self->op_count = app->net->rev->player_info_read(self->data, self->length, &self->info);
    }
    {
        struct EntityInfoHost h = { 0 };
        struct RS_EntityApplyHost host;
        struct RS_EntityApplyIn in;

        entity_apply_in(app, &h, &host, &in);
        RS_EntityApply_PlayerInfo(&in, self->ops, self->op_count);
    }
    RedrawRequest_Raise(&app->redraw_request);

    PT_END(&self->pt);
}

static void
Task_ExecPlayerInfo_Free(struct ToriRS_Task* base)
{
    struct Task_ExecPlayerInfo* self = (struct Task_ExecPlayerInfo*)base;
    EntityInfoScratch_Release(&self->app->entity_info_scratch.player, &self->info);
    ToriMem_Free(ToriMem_Session(), self);
}

static struct ToriRS_TaskVTable Task_ExecPlayerInfo_VTable = {
    .run = Task_ExecPlayerInfo_Run,
    .free = Task_ExecPlayerInfo_Free,
};

struct ToriRS_Task*
CreateTask_ExecPlayerInfo(
    struct App* app,
    uint8_t const* data,
    int length)
{
    struct Task_ExecPlayerInfo* task;

    assert(app);
    if( length <= 0 )
        return NULL;
    assert(data);
    task = ToriMem_Calloc(ToriMem_Session(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_ExecPlayerInfo_VTable;
    strncpy(task->task.name, "ExecPlayerInfo", sizeof(task->task.name) - 1);
    task->app = app;
    task->data = data;
    task->length = length;
    PT_INIT(&task->pt);
    return &task->task;
}

struct ToriRS_Task*
CreateTask_ExecPlayerInfoOps(
    struct App* app,
    struct RsprotInfoOp const* ops,
    int count)
{
    struct Task_ExecPlayerInfo* task;

    assert(app);
    assert(ops);
    assert(count > 0);
    task = ToriMem_Calloc(ToriMem_Session(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_ExecPlayerInfo_VTable;
    strncpy(task->task.name, "ExecPlayerInfoOps", sizeof(task->task.name) - 1);
    task->app = app;
    /* Borrowed for the task's life; never written. */
    task->ops = (struct RsprotInfoOp*)ops;
    task->op_count = count;
    PT_INIT(&task->pt);
    return &task->task;
}

/* ------------------------------------------------------------------ */
/* NPC_INFO                                                            */
/* ------------------------------------------------------------------ */

struct Task_ExecNpcInfo
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    uint8_t const* data;
    int length;
    /** NPC_INFO_LARGE: rev->npc_info_read_large decodes it. */
    int large;

    RsprotInfoList info;
    struct RsprotInfoOp* ops; /* info.ops, or a replay keyframe's */
    int op_count;

    /* The world this packet describes: SET_ACTIVE_WORLD's view when it was
     * queued (deob: NPC_INFO applies to `activeWorld`). 0 is the root. */
    int view;

    /* The npc wire types the op list names, and the rung each resolves to
     * under this App's varps: awaited before the apply, which reads them. */
    int* types;
    int* resolved;
    int type_count;
    int type_i;

    uint64_t bd_start; /* TORIRS_NPCINFO_BREAKDOWN only */
    uint64_t bd_decode;
    uint64_t bd_await;
};

static int
Task_ExecNpcInfo_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_ExecNpcInfo* self = (struct Task_ExecNpcInfo*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);

    self->bd_start = BD_T0();
    /* A replay keyframe's list arrives decoded (CreateTask_ExecNpcInfoOps). */
    if( !self->ops )
    {
        struct GameProtoRevTable const* rev;

        EntityInfoScratch_Borrow(&app->entity_info_scratch.npc, &self->info);
        self->ops = self->info.ops;
        assert(app->net);
        rev = app->net->rev;
        if( self->large )
        {
            /* Only a table that names NPC_INFO_LARGE routes one here. */
            assert(rev->npc_info_read_large);
            self->op_count = rev->npc_info_read_large(self->data, self->length, &self->info);
        }
        else
        {
            assert(rev->npc_info_read);
            self->op_count = rev->npc_info_read(self->data, self->length, &self->info);
        }
    }
    self->bd_decode = BD_T0();

    /*
     * The CONFIG walk only -- each named wrapper and its transform rungs --
     * which is answered inside the pass once the config group is resident
     * (the boot fills it). The type is REQUIRED: it decides the npc's size,
     * ops and stances, which later packets read. The body is not awaited:
     * this queue is the packet pipeline, and the entity is applied with an
     * empty body when its models are not in; NpcBodyLand on the parallel
     * queue dresses it when they are. See game/task_entity_assets.h.
     */
    if( self->op_count > 0 )
    {
        self->types = ToriMem_Calloc(ToriMem_Session(), (size_t)self->op_count, sizeof(int));
        assert(self->types);
        self->resolved = ToriMem_Calloc(ToriMem_Session(), (size_t)self->op_count, sizeof(int));
        assert(self->resolved);
        self->type_count = RS_EntityApply_NpcTypes(self->ops, self->op_count, self->types, self->op_count);
    }
    for( self->type_i = 0; self->type_i < self->type_count; self->type_i++ )
        PT_TASK_AWAITSELF_IF(CreateTask_NpcMultiResolve(
            app, self->types[self->type_i], &self->resolved[self->type_i]));
    self->bd_await = BD_T0();

    /* A deck view that despawned while this packet waited (a hull freed
     * between SET_ACTIVE_WORLD and here) has no npcs left to describe. */
    if( self->view != WORLDVIEW_ROOT && !WorldviewRegistry_IsLive(&app->worldviews, self->view) )
        PT_EXIT(&self->pt);

    {
        struct EntityInfoHost h = { 0 };
        struct RS_EntityApplyHost host;
        struct RS_EntityApplyIn in;

        entity_apply_in(app, &h, &host, &in);
        h.types = self->types;
        h.resolved = self->resolved;
        h.type_count = self->type_count;
        RS_EntityApply_NpcInfo(&in, self->view, self->ops, self->op_count);
    }

    RedrawRequest_Raise(&app->redraw_request);

    if( npcinfo_bd_on() )
    {
        uint64_t now = PlatformWindow_TicksUs();
        uint64_t total = now - self->bd_start;
        if( total >= (uint64_t)g_npcinfo_bd_ms * 1000u )
            TORIRS_LOG("npcinfo_bd: total %.2f decode %.2f await %.2f apply %.2f | ops %d types %d\n",
                total / 1000.0, (self->bd_decode - self->bd_start) / 1000.0,
                (self->bd_await - self->bd_decode) / 1000.0, (now - self->bd_await) / 1000.0,
                self->op_count, self->type_count);
    }

    PT_END(&self->pt);
}

static void
Task_ExecNpcInfo_Free(struct ToriRS_Task* base)
{
    struct Task_ExecNpcInfo* self = (struct Task_ExecNpcInfo*)base;
    EntityInfoScratch_Release(&self->app->entity_info_scratch.npc, &self->info);
    ToriMem_Free(ToriMem_Session(), self->types);
    ToriMem_Free(ToriMem_Session(), self->resolved);
    ToriMem_Free(ToriMem_Session(), self);
}

static struct ToriRS_TaskVTable Task_ExecNpcInfo_VTable = {
    .run = Task_ExecNpcInfo_Run,
    .free = Task_ExecNpcInfo_Free,
};

struct ToriRS_Task*
CreateTask_ExecNpcInfo(
    struct App* app,
    uint8_t const* data,
    int length,
    int large)
{
    struct Task_ExecNpcInfo* task;

    assert(app);
    if( length <= 0 )
        return NULL;
    assert(data);
    task = ToriMem_Calloc(ToriMem_Session(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_ExecNpcInfo_VTable;
    strncpy(task->task.name, "ExecNpcInfo", sizeof(task->task.name) - 1);
    task->app = app;
    task->data = data;
    task->length = length;
    task->large = large;
    task->view = WorldviewRegistry_Active(&app->worldviews);
    PT_INIT(&task->pt);
    return &task->task;
}

struct ToriRS_Task*
CreateTask_ExecNpcInfoOps(
    struct App* app,
    struct RsprotInfoOp const* ops,
    int count)
{
    struct Task_ExecNpcInfo* task;

    assert(app);
    assert(ops);
    assert(count > 0);
    task = ToriMem_Calloc(ToriMem_Session(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_ExecNpcInfo_VTable;
    strncpy(task->task.name, "ExecNpcInfoOps", sizeof(task->task.name) - 1);
    task->app = app;
    task->ops = (struct RsprotInfoOp*)ops;
    task->op_count = count;
    task->view = WORLDVIEW_ROOT;
    PT_INIT(&task->pt);
    return &task->task;
}
