/*
 * A seek's world keyframe, applied by the executor (replay_world.h): what
 * a REPLAY_WORLD control frame (net/torirs_control.h) does, as one awaited
 * task on the packet pipeline.
 *
 *   1. The world's dynamic state goes: every player and npc this client
 *      tracks, the world entities, queued zone packets. What the chase left
 *      standing is the world of wherever the seek started.
 *   2. The scene. A different one is rebuilt from the original REBUILD,
 *      verbatim, through Task_GameProtoExec's own REBUILD arm (forced, so the
 *      same-zone early-out cannot skip it; its MAP_BUILD_COMPLETE is what the
 *      transport's map-build barrier waits for). The loaded one has every
 *      zone put back to the map as UPDATE_ZONE_FULL_FOLLOWS puts one back.
 *   3. The zone ops alive at the keyframe, in birth order, through the zone
 *      arms the packets themselves run through.
 *   4. The player decoder's table, loaded AFTER the scene: a login REBUILD's
 *      parse seeds it.
 *   5. The players, then the npcs in list order, each with its sticky ops, as
 *      op lists through the PLAYER_INFO / NPC_INFO tasks' own op arms: a
 *      player is placed as the stream places a player entering view
 *      (target, ABS_XZLEVEL as a jump), an npc as one entering view
 *      (ADD_NEW, NPCTYPE, DELTA_XZ from the origin as a jump).
 *   6. The NPC_HEADICON_SPECIFICs still in force, as the packets themselves.
 */
#include "replay/replay_world.h"

#include "app.h"
#include "game/rs_entity_sync.h"
#include "game/rs_gameproto_exec.h"
#include "game/task_exec_entity_info.h"
#include "game/task_gameproto_exec.h"
#include "mem/torirs_mem.h"
#include "net/net.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rev/revpacket.h"
#include "platform/platform_window.h"
#include "world/wev.h"
#include "world/world.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "log/torirs_log.h"

struct Task_ReplayWorldApply
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    struct ReplayWorldDelivery* d;
    /** The op list handed to an entity task: borrowed by it until it ends. */
    RsprotInfoOp* ops;
    int op_count;
    int rebuilt;
    uint64_t t0;
};

/* --- 1. the world's dynamic state ----------------------------------------- */

void
ReplayWorld_ClearEntities(struct App* app)
{
    struct RS_EntitySync* es = &app->esync;
    int const count = es->registry.count;
    int* ids;

    assert(app);
    /* Every registered player and npc, not the tracked lists: the OSRS player
     * stream lists only the players a packet touched, so a stationary one is
     * in the registry and in no list. Collected first: removing edits the
     * registry. */
    ids = malloc((size_t)(count > 0 ? count : 1) * sizeof(*ids));
    assert(ids);
    for( int i = 0; i < count; i++ )
        ids[i] = es->registry.records[i].entity_id;
    for( int i = 0; i < count; i++ )
    {
        int const kind = WORLD_ENTITY_KIND_OF(ids[i]);
        int const index = WORLD_ENTITY_INDEX_OF(ids[i]);

        if( kind == WORLD_ENTITY_KIND_PLAYER )
            RS_EntitySync_RemovePlayer(es, app->world, index);
        else if( kind == WORLD_ENTITY_KIND_NPC )
            RS_EntitySync_RemoveNpc(es, app->world, index);
    }
    free(ids);
    for( int v = 1; v < WORLDVIEW_MAX; v++ )
        RS_EntitySync_ClearViewNpcs(es, app->world, v);
    es->active_player_count = 0;
    es->active_npc_count = 0;
    /* CLEAR_ENTITIES's world-entity half (rs_gameproto_exec.c). */
    while( Wevs_ViewListCount(&app->wevs, WORLDVIEW_ROOT) > 0 )
        App_WevDespawn(app, Wevs_ViewListAt(&app->wevs, WORLDVIEW_ROOT, 0)->id);
    app->active_world = WORLDVIEW_ROOT;
    app->active_world_level = 0;
    app->pending_zone_count = 0;
}

/* --- 2. the scene ----------------------------------------------------------- */

static int
scene_loaded(
    struct App const* app,
    struct ReplayLedgerScene const* row)
{
    return app->world_active && app->world && app->world->load_complete && !row->instanced &&
           !app->rebuild_instance_active && app->rebuild_zone_x == row->zone_x &&
           app->rebuild_zone_z == row->zone_z;
}

/* The original REBUILD, parsed as net parses it and executed through
 * Task_GameProtoExec's REBUILD arm (NULL, said, when it no longer parses). */
static struct ToriRS_Task*
rebuild_task(
    struct App* app,
    struct ReplayWorldDelivery const* d)
{
    struct RevPacket packet;
    int parsed;

    assert(d->scene_bytes);
    assert(app->net);
    memset(&packet, 0, sizeof(packet));
    parsed = app->net->rev->parse_wire(app->net->rev, d->scene_wire, d->scene_bytes, d->scene_len,
                                       &packet);
    if( parsed <= 0 )
    {
        gameproto_free(&packet);
        TORIRS_ERR("replay: the keyframe's REBUILD (wire %d, %d bytes) does not parse\n",
                   d->scene_wire, d->scene_len);
        return NULL;
    }
    /* Past the same-zone early-out: the scene the keyframe names is the one
     * to stand in, whatever the client was showing. */
    app->net_force_rebuild = 1;
    return CreateTask_GameProtoExec(app, &packet);
}

/* --- 5. the entities --------------------------------------------------------- */

static RsprotInfoOp*
op_push(
    struct Task_ReplayWorldApply* self,
    int cap,
    enum RsprotInfoOpKind kind)
{
    RsprotInfoOp* op;

    assert(self->op_count < cap);
    (void)cap;
    op = &self->ops[self->op_count++];
    memset(op, 0, sizeof(*op));
    op->kind = kind;
    return op;
}

static int
player_ops_build(struct Task_ReplayWorldApply* self)
{
    struct ReplayWorldKeyframe const* kf = &self->d->kf;
    RsprotPlayerInfo const* table = (RsprotPlayerInfo const*)kf->player_table;
    int const cap = kf->player_count * 2 + kf->op_count + 1;

    free(self->ops);
    self->ops = malloc((size_t)cap * sizeof(*self->ops));
    assert(self->ops);
    self->op_count = 0;
    for( int i = 0; i < kf->player_count; i++ )
    {
        struct ReplayWorldPlayer const* p = &kf->players[i];
        RsprotInfoOp* op;

        /* rsprot_info_player_target, then the ABS_XZLEVEL a slot entering
         * high resolution reads as (rsprot_info_player.c, moved()). */
        if( p->pid == kf->local_index )
            op_push(self, cap, RSPROT_OP_SET_LOCAL_PLAYER);
        else
            op_push(self, cap, RSPROT_OP_ADD_NEW)->value = (uint64_t)p->pid;
        op = op_push(self, cap, RSPROT_OP_ABS_XZLEVEL);
        op->xz_level.x = (int16_t)p->x;
        op->xz_level.z = (int16_t)p->z;
        op->xz_level.level = (uint8_t)p->level;
        op->xz_level.jump = true;
        op->xz_level.move = RSPROT_PLAYER_MOVE_ADD;
        op->xz_level.has_move_speed = true;
        op->xz_level.move_speed = table->move_speed[p->pid];
        for( int k = 0; k < p->op_count; k++ )
        {
            assert(self->op_count < cap);
            self->ops[self->op_count++] = kf->ops[p->op_first + k];
        }
    }
    return self->op_count;
}

static int
npc_ops_build(struct Task_ReplayWorldApply* self)
{
    struct App* app = self->app;
    struct ReplayWorldKeyframe const* kf = &self->d->kf;
    int const cap = kf->npc_count * 3 + kf->op_count + 1;
    int origin_x;
    int origin_z;
    int origin_level;
    int base_x;
    int base_z;

    /* Where NPC_INFO measures a delta from, now that the players stand where
     * the keyframe put them and the origin is the keyframe's. */
    RS_EntityInfo_NpcOrigin(app, &origin_x, &origin_z, &origin_level);
    base_x = app->world->_base_tile_x;
    base_z = app->world->_base_tile_z;

    free(self->ops);
    self->ops = malloc((size_t)cap * sizeof(*self->ops));
    assert(self->ops);
    self->op_count = 0;
    for( int i = 0; i < kf->npc_count; i++ )
    {
        struct ReplayWorldNpc const* n = &kf->npcs[i];
        RsprotInfoOp* op;

        if( n->slot < 0 )
        {
            /* A position the list holds with nothing behind it: an ADD_OLD
             * past the (empty) old list appends -1, as the stream's own
             * does (npc_target_op). */
            op_push(self, cap, RSPROT_OP_ADD_OLD)->value = (uint64_t)RS_ENTITY_SYNC_MAX_NPCS;
            continue;
        }
        op_push(self, cap, RSPROT_OP_ADD_NEW)->value = (uint64_t)n->slot;
        if( n->type == REPLAY_NPC_TYPE_NONE )
            continue;
        op_push(self, cap, RSPROT_OP_NPCTYPE)->value = (uint64_t)(uint32_t)n->type;
        op = op_push(self, cap, RSPROT_OP_DELTA_XZ);
        op->delta_xz.dx = (int16_t)(n->x - base_x - origin_x);
        op->delta_xz.dz = (int16_t)(n->z - base_z - origin_z);
        op->delta_xz.jump = true;
        for( int k = 0; k < n->op_count; k++ )
        {
            assert(self->op_count < cap);
            self->ops[self->op_count++] = kf->ops[n->op_first + k];
        }
    }
    return self->op_count;
}

/* An npc stands on the plane the origin had when it spawned (the stream
 * carries none); the keyframe says which that was. */
static void
npc_levels(struct Task_ReplayWorldApply* self)
{
    struct App* app = self->app;
    struct ReplayWorldKeyframe const* kf = &self->d->kf;

    for( int i = 0; i < kf->npc_count; i++ )
    {
        int world_idx;
        struct WorldEntity_NPC* npc;

        if( kf->npcs[i].slot < 0 ||
            !RS_EntitySync_FindNpc(&app->esync, kf->npcs[i].slot, &world_idx, NULL) )
            continue;
        npc = World_EntityPoolGet(&app->world->entities.npc, world_idx);
        if( npc )
            npc->grid_position.level = kf->npcs[i].level;
    }
}

/* The NPC_HEADICON_SPECIFICs still in force, through the packet's own arm
 * (exec_npc_headicon_specific: the npc found by slot, the type's icons copied
 * in on the first write), once the npcs stand. */
_Static_assert(REPLAY_NPC_HEADICON_SLOTS == WORLD_NPC_HEADICON_SLOTS,
               "the ledger keeps a sticky slot per head-icon slot");

static void
apply_npc_headicons(struct Task_ReplayWorldApply* self)
{
    struct App* app = self->app;
    struct ReplayWorldKeyframe const* kf = &self->d->kf;
    struct RS_GameProtoCtx ctx = {
        .tree = app->tree,
        .invs = &app->invs,
        .varps = &app->varps,
        .stats = &app->stats,
        .chat = &app->chat,
        .app = app,
    };

    for( int i = 0; i < kf->npc_headicon_count; i++ )
    {
        struct ReplayWorldNpcHeadicon const* h = &kf->npc_headicons[i];
        struct RevPacket packet;

        memset(&packet, 0, sizeof(packet));
        packet.packet_type = PKT_NAME_NPC_HEADICON_SPECIFIC;
        packet._npc_headicon_specific.npc_index = h->npc_slot;
        packet._npc_headicon_specific.slot = h->icon_slot;
        packet._npc_headicon_specific.group = h->group;
        packet._npc_headicon_specific.index = h->index;
        RS_GameProto_Exec(&ctx, &packet);
    }
}

/* --- the task --------------------------------------------------------------- */

static void
apply_zones_and_table(struct Task_ReplayWorldApply* self)
{
    struct App* app = self->app;
    struct ReplayWorldKeyframe const* kf = &self->d->kf;
    struct GameProtoRevTable const* rev = app->net->rev;
    struct RS_GameProtoCtx ctx = {
        .tree = app->tree,
        .invs = &app->invs,
        .varps = &app->varps,
        .stats = &app->stats,
        .chat = &app->chat,
        .app = app,
    };

    for( int i = 0; i < kf->zone_op_count; i++ )
    {
        struct ReplayWorldZoneOp const* z = &kf->zone_ops[i];

        RS_GameProto_ReplayZoneOp(&ctx, &z->pkt, z->base_x, z->base_z, z->level);
    }
    assert(rev->player_info_state_size);
    assert(rev->player_info_state_load);
    assert(rev->player_info_state_size() == kf->player_table_size);
    rev->player_info_state_load(kf->player_table);
    app->esync.local_pid = kf->local_index;
    app->npc_update_origin_valid = kf->npc_origin_valid;
    if( kf->npc_origin_valid )
    {
        app->npc_update_origin_x = kf->npc_origin_x;
        app->npc_update_origin_z = kf->npc_origin_z;
    }
}

static int
Task_ReplayWorldApply_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IOBatch* io)
{
    struct Task_ReplayWorldApply* self = (struct Task_ReplayWorldApply*)base;
    struct App* app = self->app;

    (void)io;
    PT_BEGIN(&self->pt);

    self->t0 = PlatformWindow_Ticks64();
    assert(app->world);
    assert(app->net);
    /* Done already when the chase began (exec_control_replay_coord); a seek
     * whose keyframe follows no chase tick of its own comes here with the
     * world it started from. */
    ReplayWorld_ClearEntities(app);
    /* The chase's stated tile gives way to the world it stood in for. */
    app->replay_chase_coord_valid = 0;

    if( self->d->kf.scene >= 0 &&
        (self->d->rebuild || !scene_loaded(app, &self->d->kf.scene_row)) )
    {
        if( !self->d->rebuild )
            TORIRS_REPORT("replay: the keyframe's scene is not the loaded one; rebuilding it "
                          "(the transport did not hold the clock for it)\n");
        /* A rebuild keeps what stands in the scene: it re-places the loc
         * changes and carries the ground items over (app_world_load.c), which
         * is right for a server's REBUILD and wrong here, where they belong
         * to the tick the seek left. A seek back from tick 3000 to 2999 on
         * RSProx 96 kept five ground items the keyframe did not name. */
        if( app->world_active && app->world->load_complete )
            RS_GameProto_ReplayClearScene(app);
        self->rebuilt = 1;
        PT_TASK_AWAITSELF_IF(rebuild_task(app, self->d));
    }
    else
    {
        app->net_force_rebuild = 0;
        RS_GameProto_ReplayClearScene(app);
    }

    apply_zones_and_table(self);

    if( player_ops_build(self) > 0 )
        PT_TASK_AWAITSELF_IF(CreateTask_ExecPlayerInfoOps(app, self->ops, self->op_count));
    if( npc_ops_build(self) > 0 )
        PT_TASK_AWAITSELF_IF(CreateTask_ExecNpcInfoOps(app, self->ops, self->op_count));
    npc_levels(self);
    apply_npc_headicons(self);
    app->need_redraw = 1;

    TORIRS_REPORT("replay: world keyframe at tick %d: scene %s, %d zone ops, %d players, %d npcs "
                  "(%.1f ms)\n",
                  self->d->tick, self->rebuilt ? "rebuilt" : "kept, its zones reset",
                  self->d->kf.zone_op_count, self->d->kf.player_count, self->d->kf.npc_count,
                  (double)(PlatformWindow_Ticks64() - self->t0));

    PT_END(&self->pt);
}

static void
Task_ReplayWorldApply_Free(struct ToriRS_Task* base)
{
    struct Task_ReplayWorldApply* self = (struct Task_ReplayWorldApply*)base;

    ReplayWorldDelivery_Free(self->d);
    free(self->ops);
    ToriMem_Free(ToriMem_Session(), self);
}

static struct ToriRS_TaskVTable Task_ReplayWorldApply_VTable = {
    .run = Task_ReplayWorldApply_Run,
    .free = Task_ReplayWorldApply_Free,
};

struct ToriRS_Task*
CreateTask_ReplayWorldApply(
    struct App* app,
    struct ReplayWorldDelivery* d)
{
    struct Task_ReplayWorldApply* task;

    assert(app);
    assert(d);
    task = ToriMem_Calloc(ToriMem_Session(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_ReplayWorldApply_VTable;
    strncpy(task->task.name, "ReplayWorldApply", sizeof(task->task.name) - 1);
    task->app = app;
    task->d = d;
    PT_INIT(&task->pt);
    return &task->task;
}
