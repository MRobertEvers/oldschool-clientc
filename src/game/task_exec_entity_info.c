#include "task_exec_entity_info.h"

#include "app.h"
#include "game/rs_healthbar.h"
#include "game/rs_hitsplat.h"
#include "engine/entity_model_build.h"
#include "engine/player_appearance.h"
#include "game/rs_chat.h"
#include "game/rs_entity_sync.h"
#include "net/net.h"
#include "net/rev/packets/pkt_npc_info.h"
#include "net/rev/packets/pkt_player_appearance.h"
#include "net/rev/packets/pkt_player_info.h"
#include "net/wordpack.h"
#include "toridraw_animation.h"
#include "toridraw_scene.h"
#include "world/entity_pathing.h"
#include "world/world.h"

#include <assert.h>
#include <rsbuffer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

/*
 * Turn one HEADBAR block into the record the overlay reads.
 *
 * The wire carries fills, a duration and a start delay; how long the bar then
 * lingers is the healthbar TYPE's (opcode 5), which is why this lives here --
 * task_exec is the side that can reach the config table, and World must not
 * grow a dependency on it just to compute an expiry.
 *
 * Reference `class66.method1483`: an update is dropped once
 * `type.persist + startCycle + duration <= loopCycle`.
 */
static struct WorldEntity_Headbar
headbar_from_block(
    struct App* app,
    struct World const* world,
    int type,
    int duration,
    int start_delay,
    int start_fill,
    int end_fill)
{
    struct RS_HealthbarType const* cfg = RS_Healthbars_TypeFor(&app->healthbars, type);
    struct WorldEntity_Headbar bar;

    bar.type = type;
    bar.start_cycle = world->cycle + start_delay;
    bar.duration = duration;
    bar.start_fill = start_fill;
    bar.end_fill = end_fill;
    bar.end_cycle = bar.start_cycle + duration + cfg->persist_cycles;
    return bar;
}

enum
{
    ENTITY_INFO_OPS_MAX = 2048,
    /* Local-player sentinel while UPDATE_PID has not arrived. */
    ENTITY_INFO_LOCAL_SENTINEL = 2047,
    /* Classic human movement set — placeholder until the APPEARANCE mask
     * lands (reference always follows a spawn with an appearance update). */
    PLAYER_DEFAULT_SEQ_READY = 808,
};

static void
entity_debug_log(char const* fmt, int a, int b)
{
    if( getenv("TORIRS_NET_DEBUG") )
        TORIRS_LOG(fmt, a, b);
}

/*
 * TORIRS_NPC_TRACE=<npc_id>[,<npc_id>...]: narrate every NPC_INFO operation that
 * lands on those npc types, with the identifiers that have to agree for it to
 * land on the right one.
 *
 * Built for "the Queen Black Dragon sometimes disappears mid-fight", which is
 * the visible end of a class of failure -- an op applied to the wrong entity,
 * or a spawn that never happened -- where nothing errors and the packet is
 * well-formed. What matters is not the op but the four numbers beside it:
 *
 *   slot     the server's PRIVATE per-observer npc name (ToriRSServerPlayerSlotMap).
 *            The client keys its registry by this. If it changes for the same
 *            creature, the client sees a despawn and a fresh spawn.
 *   list_idx the position in the list both sides rebuild this packet, which is
 *            what extended info is addressed by. See the invariant on
 *            RS_EntitySync::active_npcs.
 *   world    the client's world-pool index; -1 means the slot resolved to no
 *            entity, i.e. every following op on it is silently discarded.
 *   element  the scene element; -1 means nothing is drawn for it.
 *
 * Pair it with TORIRSSERVER_NPC_TRACE=<npc_id> (torirs_server_encode.c) on the same run:
 * the server prints the slot it allocated, and this prints the slot the client
 * resolved. They must agree, every tick, for the whole fight.
 */
static int
npc_trace_wants(int npc_id)
{
    static char const* spec = NULL;
    static int parsed = 0;
    char const* p;

    if( !parsed )
    {
        parsed = 1;
        spec = getenv("TORIRS_NPC_TRACE");
    }
    if( !spec || !*spec || npc_id < 0 )
        return 0;
    for( p = spec; *p; )
    {
        char* end = NULL;
        long const want = strtol(p, &end, 10);
        if( end == p )
            break;
        if( want == npc_id )
            return 1;
        p = (*end == ',') ? end + 1 : end;
        if( !*p )
            break;
    }
    return 0;
}

/* The npc type currently behind `world_idx`, or -1. The type is what the trace
 * filters on, and it is only knowable once the entity exists -- a spawn is
 * traced from its pending type instead. */
static int
npc_trace_type_of(struct App* app, int world_idx)
{
    struct WorldEntity_NPC* npc;

    assert(app);
    if( !app->world || world_idx < 0 )
        return -1;
    npc = World_EntityPoolGet(&app->world->entities.npc, world_idx);
    return npc ? npc->npc_id : -1;
}

static void
npc_trace(
    struct App* app,
    int npc_id,
    int slot,
    int list_idx,
    int world_idx,
    int element_id,
    char const* what,
    int detail)
{
    if( !npc_trace_wants(npc_id) )
        return;
    TORIRS_LOG("npc_trace: npc=%d slot=%d list_idx=%d world=%d element=%d cycle=%d %s=%d\n",
        npc_id, slot, list_idx, world_idx, element_id,
        (app && app->world) ? app->world->cycle : -1, what, detail);
}

/*
 * Op-array scratch.
 *
 * Both decoders below write into an ENTITY_INFO_OPS_MAX array and drop it when
 * the packet finishes. At 2048 entries that is tens of KB calloc'd, zeroed and
 * freed for every PLAYER_INFO and every NPC_INFO -- and the server sends both
 * on every tick. Nothing outlives the task, so keep one array per decoder on
 * App and hand it back instead.
 *
 * App_LogicTick settles each packet task before queueing the next, so a second
 * borrow cannot overlap the first; the busy flag with a plain calloc fallback
 * keeps that a performance assumption rather than a correctness one.
 *
 * Release frees the say strings either way, then zeroes only the ops the decode
 * actually wrote -- the array is handed out zeroed and op_count is assigned in
 * the same protothread segment as the read, with no yield in between, so no
 * entry past op_count can be dirty.
 */
static struct PktNpcInfoOp*
npc_ops_borrow(struct App* app)
{
    if( app->npc_info_ops_scratch_busy )
        return calloc(ENTITY_INFO_OPS_MAX, sizeof(struct PktNpcInfoOp));
    if( !app->npc_info_ops_scratch )
    {
        app->npc_info_ops_scratch =
            calloc(ENTITY_INFO_OPS_MAX, sizeof(struct PktNpcInfoOp));
        if( !app->npc_info_ops_scratch )
            return NULL;
    }
    app->npc_info_ops_scratch_busy = 1;
    return app->npc_info_ops_scratch;
}

static void
npc_ops_release(struct App* app, struct PktNpcInfoOp* ops, int op_count)
{
    if( !ops )
        return;
    if( op_count < 0 )
        op_count = 0;
    if( op_count > ENTITY_INFO_OPS_MAX )
        op_count = ENTITY_INFO_OPS_MAX;
    pkt_npc_info_ops_free(ops, op_count);
    if( ops != app->npc_info_ops_scratch )
    {
        free(ops);
        return;
    }
    if( op_count > 0 )
        memset(ops, 0, (size_t)op_count * sizeof(*ops));
    app->npc_info_ops_scratch_busy = 0;
}

static struct PktPlayerInfoOp*
player_ops_borrow(struct App* app)
{
    if( app->player_info_ops_scratch_busy )
        return calloc(ENTITY_INFO_OPS_MAX, sizeof(struct PktPlayerInfoOp));
    if( !app->player_info_ops_scratch )
    {
        app->player_info_ops_scratch =
            calloc(ENTITY_INFO_OPS_MAX, sizeof(struct PktPlayerInfoOp));
        if( !app->player_info_ops_scratch )
            return NULL;
    }
    app->player_info_ops_scratch_busy = 1;
    return app->player_info_ops_scratch;
}

static void
player_ops_release(struct App* app, struct PktPlayerInfoOp* ops, int op_count)
{
    if( !ops )
        return;
    if( op_count < 0 )
        op_count = 0;
    if( op_count > ENTITY_INFO_OPS_MAX )
        op_count = ENTITY_INFO_OPS_MAX;
    pkt_player_info_ops_free(ops, op_count);
    if( ops != app->player_info_ops_scratch )
    {
        free(ops);
        return;
    }
    if( op_count > 0 )
        memset(ops, 0, (size_t)op_count * sizeof(*ops));
    app->player_info_ops_scratch_busy = 0;
}

/*
 * TORIRS_NPCINFO_BREAKDOWN=<ms>: split one NPC_INFO apply into decode / await /
 * spawn / retype / move and print the split when it exceeds <ms>. The exec
 * pipeline runs one packet at a time, so file statics are safe accumulators;
 * they are reset at the top of each run.
 */
uint64_t
PlatformWindow_TicksUs(void);

static int g_npcinfo_bd_ms = -1;
static uint64_t g_bd_decode;
static uint64_t g_bd_await;
static uint64_t g_bd_spawn;
static uint64_t g_bd_retype;
static uint64_t g_bd_apply;
static int g_bd_ops;
static int g_bd_spawns;

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
#define BD_ADD(acc, t0)                                                                            \
    do                                                                                             \
    {                                                                                              \
        if( npcinfo_bd_on() )                                                                      \
            (acc) += PlatformWindow_TicksUs() - (t0);                                                 \
    } while( 0 )

void
Task_EntityInfoScratchFree(struct App* app)
{
    assert(app);
    free(app->npc_info_ops_scratch);
    free(app->player_info_ops_scratch);
    app->npc_info_ops_scratch = NULL;
    app->player_info_ops_scratch = NULL;
    app->npc_info_ops_scratch_busy = 0;
    app->player_info_ops_scratch_busy = 0;
}

/* ------------------------------------------------------------------ */
/* PLAYER_INFO                                                         */
/* ------------------------------------------------------------------ */

enum PlayerOpNeed
{
    PLAYER_NEED_NONE = 0,
    PLAYER_NEED_APPEARANCE,
    PLAYER_NEED_SEQ,
};

struct Task_ExecPlayerInfo
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    uint8_t const* data; /* borrowed from the owning packet task */
    int length;

    struct PktPlayerInfoReader reader;
    struct PktPlayerInfoOp* ops;
    int op_count;
    int op_i;

    int old_list[RS_ENTITY_SYNC_MAX_PLAYERS];
    int old_count;

    int cur_pid; /* server slot the following ops target; -1 = none */
    int need_ensure;
    /* Decided before the need_ensure awaits and read after them, so it cannot
     * be a local: the protothread resumes into a `case __LINE__:` in the loop
     * body and never re-runs the initialiser. */
    int op_consumed;

    struct PktPlayerAppearance app_decoded;
    int cfg_i;
    int model_ids[64];
    int model_count;
    int model_i;
    int seq_i;
    int pending_seq;
    int pending_delay;
    int held_vals[2]; /* replaceheld left/right, as canonical appearance slots */
};

/*
 * Resolve the current target's world-pool index (and optionally its scene
 * element) from `cur_pid`, at the point of use.
 *
 * Deliberately NOT cached on the task. This function body spans
 * PT_TASK_AWAITSELF_IF suspensions, and world-pool indices and scene element
 * ids are recycled across them -- a value resolved before an await can name a
 * different player afterwards while still looking valid. `cur_pid` is the only
 * identity stable across a suspension, so everything derives from it here and
 * nothing derived is allowed to outlive the expression that used it. The scan
 * is linear over the tracked entities, a handful per packet.
 */
static inline int
player_target(struct Task_ExecPlayerInfo* self, int* out_element_id)
{
    int world_idx = -1;
    int element_id = -1;

    if( self->cur_pid >= 0 )
        RS_EntitySync_FindPlayer(&self->app->esync, self->cur_pid, &world_idx, &element_id);
    if( out_element_id )
        *out_element_id = element_id;
    return world_idx;
}

static int
local_player_pid(struct RS_EntitySync const* esync)
{
    return esync->local_pid >= 0 ? esync->local_pid : ENTITY_INFO_LOCAL_SENTINEL;
}

/* Local player's scene tile (fallback: classic scene center) for relative
 * adds. Server-local coords are classic-scene relative ((zone-6)*8), which
 * matches our scene base after REBUILD_NORMAL.
 *
 * The tile is routeX[0]/routeZ[0], NOT grid_position: the reference bases
 * every relative add on `localPlayer.routeX[0] + dx` (Client.ts:8034, 8369),
 * which is the player's *authoritative destination* tile — updated the moment
 * a walk step is pushed. grid_position only catches up when the draw position
 * arrives, so mid-walk it lags by one or more tiles and every NPC/player
 * added or teleported during that window landed on the wrong grid square
 * while the local player looked correct. */
static void
player_local_tile(
    struct Task_ExecPlayerInfo* self,
    int* out_x,
    int* out_z,
    int* out_level)
{
    int world_idx;
    *out_x = 52;
    *out_z = 52;
    *out_level = 0;
    if( self->app->npc_update_origin_valid )
    {
        *out_x = self->app->npc_update_origin_x;
        *out_z = self->app->npc_update_origin_z;
    }
    if( RS_EntitySync_FindPlayer(&self->app->esync, local_player_pid(&self->app->esync), &world_idx, NULL) )
    {
        struct WorldEntity_Player* player =
            World_EntityPoolGet(&self->app->world->entities.player, world_idx);
        if( player )
        {
            if( !self->app->npc_update_origin_valid )
            {
                *out_x = player->pathing.route_x[0];
                *out_z = player->pathing.route_z[0];
            }
            *out_level = player->grid_position.level;
        }
    }
}

/* Resolve the current target after a target-setting op; flags ensure when
 * the entity does not exist yet. Returns 1 when the op was fully handled. */
static int
player_target_op(
    struct Task_ExecPlayerInfo* self,
    struct PktPlayerInfoOp const* op)
{
    struct RS_EntitySync* esync = &self->app->esync;

    switch( op->kind )
    {
    case PKT_PLAYER_INFO_OP_SET_LOCAL_PLAYER:
        self->cur_pid = local_player_pid(esync);
        break;
    /*
     * Positional, exactly as the npc list is -- see the note on
     * PKT_NPC_INFO_OP_ADD_NPC_OLD_OPBITS_IDX in npc_target_op for the full
     * reasoning; this is the same invariant on the other stream.
     *
     * The classic decoder advances `new_idx` for every tracked entry it reads,
     * including the ones whose info bit is clear (pkt_player_info.c), and
     * SET_PLAYER_OPBITS_IDX below resolves an extended-info block as
     * `active_players[new_idx]`. So this append cannot be conditional: dropping
     * an unresolvable entry shortens the list and every later appearance, chat,
     * hit or animation block in the packet lands on the player after its
     * intended target. Record -1 and let the ops treat it as no target.
     *
     * The local player is deliberately NOT in this list -- the decoder gives it
     * the 2047 sentinel rather than a list position, and the executor resolves
     * it through esync.local_pid. The v5 (rev-239) stream never emits this op
     * at all: it addresses players by slot, so only the classic lanes are
     * positional here.
     */
    case PKT_PLAYER_INFO_OP_ADD_PLAYER_OLD_OPBITS_IDX:
        self->cur_pid = (int)op->_bitvalue < self->old_count ? self->old_list[op->_bitvalue] : -1;
        if( esync->active_player_count < RS_ENTITY_SYNC_MAX_PLAYERS )
            esync->active_players[esync->active_player_count++] = self->cur_pid;
        break;
    case PKT_PLAYER_INFO_OP_ADD_PLAYER_NEW_OPBITS_PID:
        self->cur_pid = (int)op->_bitvalue;
        if( esync->active_player_count < RS_ENTITY_SYNC_MAX_PLAYERS )
            esync->active_players[esync->active_player_count++] = self->cur_pid;
        break;
    case PKT_PLAYER_INFO_OP_SET_PLAYER_OPBITS_IDX:
        self->cur_pid = (int)op->_bitvalue < esync->active_player_count
                            ? esync->active_players[op->_bitvalue]
                            : -1;
        break;
    case PKT_PLAYER_INFO_OP_CLEAR_PLAYER_OPBITS_IDX:
    {
        int pid = (int)op->_bitvalue < self->old_count ? self->old_list[op->_bitvalue] : -1;
        if( pid >= 0 )
            RS_EntitySync_RemovePlayer(esync, self->app->world, pid);
        self->cur_pid = -1;
        return 1;
    }
    case PKT_PLAYER_INFO_OP_REMOVE_PLAYER_PID:
        /* The v5 form of the drop: the payload IS the pid, because that stream
         * addresses players by their slot rather than by a position in the
         * previous packet's tracked list. */
        RS_EntitySync_RemovePlayer(esync, self->app->world, (int)op->_bitvalue);
        self->cur_pid = -1;
        return 1;
    case PKT_PLAYER_INFO_OPBITS_COUNT_RESET:
    {
        /* Client-TS getPlayerPosOldVis: when the wire 8-bit count is less than
         * the previous tracked length, indices [count, oldCount) are removed.
         * Plane-change zeros tracked_count and sends count=0 — without this,
         * old-floor players linger in the world pool (drawn on minusedlevel,
         * rejected by pick). */
        int keep = (int)op->_bitvalue;
        for( int i = keep; i < self->old_count; i++ )
            RS_EntitySync_RemovePlayer(esync, self->app->world, self->old_list[i]);
        esync->active_player_count = 0;
        return 1;
    }
    case PKT_PLAYER_INFO_OPBITS_INFO:
        return 1;
    default:
        return 0; /* not a target op */
    }

    /* Target changed: flag a spawn when the pid resolves to nothing. */
    (void)esync;
    self->need_ensure = (self->cur_pid >= 0 && player_target(self, NULL) < 0) ? 1 : 0;
    return 1;
}

/* Spawn the current target with the default composited model; the
 * appearance mask that follows swaps in the real look. Assets are cached
 * (the task awaited them). */
static void
player_ensure_now(struct Task_ExecPlayerInfo* self)
{
    struct App* app = self->app;
    int tile_x, tile_z, level;
    int idx;

    self->need_ensure = 0;
    player_local_tile(self, &tile_x, &tile_z, &level);
    idx = App_WorldSpawnSyncedPlayer(app, tile_x, tile_z, level);
    if( idx < 0 )
        return;
    {
        struct WorldEntity_Player* player =
            World_EntityPoolGet(&app->world->entities.player, idx);
        if( player )
        {
            player->server_pid = self->cur_pid;
            RS_EntitySync_RegisterPlayer(&app->esync, self->cur_pid, player->element_id, idx);
        }
    }
    entity_debug_log("entity_sync: player %d spawned (world idx %d)\n", self->cur_pid, idx);
}

/* Apply one non-target op that needs no cache IO. Returns a PlayerOpNeed for
 * the ops the protothread must await loads for. */
static enum PlayerOpNeed
player_apply_op(
    struct Task_ExecPlayerInfo* self,
    struct PktPlayerInfoOp const* op)
{
    struct App* app = self->app;
    struct World* world = app->world;
    int idx = player_target(self, NULL);

    if( idx < 0 )
        return PLAYER_NEED_NONE;

    switch( op->kind )
    {
    case PKT_PLAYER_INFO_OPBITS_WALKDIR:
        World_PlayerPathPushStep(world, idx, WORLD_PATHSTEP_WALK, (int)op->_bitvalue);
        break;
    case PKT_PLAYER_INFO_OPBITS_RUNDIR:
        World_PlayerPathPushStep(world, idx, WORLD_PATHSTEP_RUN, (int)op->_bitvalue);
        break;
    case PKT_PLAYER_INFO_OP_LOCAL_XZLEVEL:
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(&world->entities.player, idx);
        World_PlayerPathJump(
            world,
            idx,
            op->_local_xz_level.jump,
            op->_local_xz_level.x,
            op->_local_xz_level.z);
        if( player )
            player->grid_position.level = op->_local_xz_level.level;
        break;
    }
    case PKT_PLAYER_INFO_OP_ABS_XZLEVEL:
    {
        /* The v5 stream's world coordinate, brought into the scene the same way
         * the projectile and map-flag decoders do: the scene's south-west
         * corner is the rebuild's centre zone less the six zones of margin the
         * client keeps on each side.
         *
         * Unless the coordinate is inside a live view's STAGING rectangle — a
         * rider aboard a hull, whose tiles are deck-instance squares hundreds
         * of zones off this scene. Those rebase into the view's own space
         * (deob: actors carry view-local coordinates), because root-relative
         * they overflow every uint8_t in the route queue. `home_view` is what
         * tells the routing pass the stored coordinates' frame. */
        struct WorldEntity_Player* player = World_EntityPoolGet(&world->entities.player, idx);
        int origin_x = (app->rebuild_zone_x - 6) * 8;
        int origin_z = (app->rebuild_zone_z - 6) * 8;
        int next_x;
        int next_z;
        int home_view = App_WevHomeViewForAbsTile(
            app, op->_local_xz_level.x, op->_local_xz_level.z, &next_x, &next_z);

        if( home_view == 0 )
        {
            next_x = op->_local_xz_level.x - origin_x;
            next_z = op->_local_xz_level.z - origin_z;
        }
        if( player )
            player->view_placement.home_view = home_view;
        int step_type =
            op->_local_xz_level.has_move_speed &&
                    op->_local_xz_level.move_speed == PKT_PLAYER_TRAVERSAL_RUN
                ? WORLD_PATHSTEP_RUN
                : WORLD_PATHSTEP_WALK;
        struct CollisionMap* collision = NULL;

        /* `level` is a uint8_t, so only the upper bound is a real test. A
         * homed actor's coordinates are view-local — the root collision map
         * cannot route between them, so the run-route smoothing (cosmetic)
         * sits out and the endpoint is queued bare. */
        if( home_view == 0 && player &&
            player->grid_position.level == op->_local_xz_level.level &&
            op->_local_xz_level.level < COLLISION_LEVELS )
            collision = world->collision_maps[op->_local_xz_level.level];

        World_PlayerPathJumpCollisionAware(
            world,
            idx,
            collision,
            op->_local_xz_level.jump,
            next_x,
            next_z,
            step_type);
        if( player && op->_local_xz_level.has_move_speed )
        {
            entity_debug_log(
                "entity_sync: player traversal=%d run=%d\n",
                op->_local_xz_level.move_speed,
                player->pathing.route_run[0]);
        }
        if( player )
            player->grid_position.level = op->_local_xz_level.level;
        entity_debug_log(
            "entity_sync: abs move to scene %d,%d\n",
            next_x,
            next_z);
        break;
    }
    case PKT_PLAYER_INFO_OP_DELTA_XZ:
    {
        int lx, lz, llevel;
        player_local_tile(self, &lx, &lz, &llevel);
        World_PlayerPathJump(
            world, idx, op->_delta_xz.jump, lx + op->_delta_xz.dx, lz + op->_delta_xz.dz);
        break;
    }
    case PKT_PLAYER_INFO_OP_APPEARANCE:
    {
        /* The block's layout is the revision's, not this layer's: a rev whose
         * appearance shape moved states its own reader
         * (GameProtoRevTable.appearance_decode) and NULL means the classic
         * block. Either way what lands in `app_decoded` is the same canonical
         * appearance — see pkt_player_appearance.h. */
        struct GameProtoRevTable const* rev = app->net ? app->net->rev : NULL;
        int decoded =
            rev && rev->appearance_decode
                ? rev->appearance_decode(
                      op->_appearance.appearance, op->_appearance.len, &self->app_decoded)
                : PktPlayerAppearance_Decode(
                      &self->app_decoded, op->_appearance.appearance, op->_appearance.len);
        if( decoded )
            return PLAYER_NEED_APPEARANCE;
        break;
    }
    case PKT_PLAYER_INFO_OP_SEQUENCE:
        if( getenv("TORIRS_NET_DEBUG") )
            TORIRS_LOG("player_info: sequence idx=%d id=%d delay=%d\n",
                idx,
                op->_sequence.sequence_id,
                op->_sequence.delay);
        self->pending_seq = op->_sequence.sequence_id;
        self->pending_delay = op->_sequence.delay;
        return PLAYER_NEED_SEQ;
    case PKT_PLAYER_INFO_OP_FACE_ENTITY:
    {
        int entity_id = op->_face_entity.entity_id == 65535 ? -1 : op->_face_entity.entity_id;
        if( op->_face_entity.modern )
            World_PlayerBeginModernFacing(world, idx, op->_face_entity.movement_mode);
        World_PlayerFaceEntityDetailed(
            world,
            idx,
            entity_id,
            op->_face_entity.has_fallback_angle ? op->_face_entity.fallback_angle : -1,
            op->_face_entity.instant);
        break;
    }
    case PKT_PLAYER_INFO_OP_FACE_COORD:
        /* Raw wire half-tiles: World_Cycle converts them (the reference keeps
         * faceSquareX/Z absolute and subtracts mapBuildBase at use time, so
         * 0,0 stays the "no target" sentinel). */
        if( op->_face_coord.modern )
            World_PlayerBeginModernFacing(world, idx, op->_face_coord.movement_mode);
        World_PlayerFaceCoord(world, idx, op->_face_coord.x, op->_face_coord.z);
        if( op->_face_coord.instant )
        {
            struct WorldEntity_Player* player =
                World_EntityPoolGet(&world->entities.player, idx);
            player->facing.instant = true;
        }
        entity_debug_log(
            "entity_sync: player faces coord %d,%d\n", op->_face_coord.x, op->_face_coord.z);
        break;
    case PKT_PLAYER_INFO_OP_SAY:
        if( op->_say.text )
        {
            struct WorldEntity_Player* player = World_EntityPoolGet(&world->entities.player, idx);
            RS_CS2Host_ChatAdd(
                &app->host,
                RS_CHAT_TYPE_PUBLIC,
                player && player->name[0] ? player->name : "Player",
                player && player->name[0] ? player->name : NULL,
                op->_say.text);
            App_NotifyChatMessage(
                app,
                RS_CHAT_TYPE_PUBLIC,
                player && player->name[0] ? player->name : "Player",
                op->_say.text);
            /* Forced chat draws overhead in plain yellow (colour/effect 0). */
            World_PlayerSetChat(world, idx, op->_say.text, 0, 0);
        }
        break;
    case PKT_PLAYER_INFO_OP_CHAT:
        if( op->_chat.data && op->_chat.length > 0 )
        {
            struct RSCache_Buffer chatbuf;
            char* text;
            RSCache_BufferInit(&chatbuf, (uint8_t*)op->_chat.data, op->_chat.length);
            text = wordpack_unpack(&chatbuf, op->_chat.length);
            if( text )
            {
                struct WorldEntity_Player* player =
                    World_EntityPoolGet(&world->entities.player, idx);
                RS_CS2Host_ChatAdd(
                    &app->host,
                    RS_CHAT_TYPE_PUBLIC,
                    player && player->name[0] ? player->name : "Player",
                    player && player->name[0] ? player->name : NULL,
                    text);
                App_NotifyChatMessage(
                    app,
                    RS_CHAT_TYPE_PUBLIC,
                    player && player->name[0] ? player->name : "Player",
                    text);
                /* colourEffect: high byte = chatColour, low byte = chatEffect
                 * (reference Client.ts:8164). */
                World_PlayerSetChat(
                    world, idx, text, op->_chat.colour_effect >> 8,
                    op->_chat.colour_effect & 0xff);
                free(text);
            }
        }
        break;
    case PKT_PLAYER_INFO_OP_DAMAGE:
    case PKT_PLAYER_INFO_OP_DAMAGE2:
        World_PlayerAddHitmarkTimed(
            world,
            idx,
            op->_damage.damage_type,
            op->_damage.damage,
            op->_damage.health,
            op->_damage.total_health,
            op->_damage.delay,
            op->_damage.slots,
            /* Duration and slot-full policy are the hitsplat TYPE's, from the
             * config group (opcodes 9 and 12) — not fixed client constants. The
             * accessors fall back to the reference's own defaults (70, discard)
             * when the cache has no record, which is what was hardcoded before. */
            RS_Hitsplats_DurationFor(&self->app->hitsplats, op->_damage.damage_type),
            RS_Hitsplats_SlotPolicyFor(&self->app->hitsplats, op->_damage.damage_type));
        break;
    case PKT_PLAYER_INFO_OP_HEADBAR:
        if( op->_headbar.remove )
            World_PlayerClearHealthbar(world, idx);
        else
            World_PlayerSetHealthbar(
                world,
                idx,
                headbar_from_block(
                    self->app, world, op->_headbar.type, op->_headbar.duration,
                    op->_headbar.start_delay, op->_headbar.start_fill,
                    op->_headbar.end_fill));
        break;
    case PKT_PLAYER_INFO_OP_SPOTANIM:
        if( getenv("TORIRS_NET_DEBUG") )
            TORIRS_LOG("player_info: spotanim idx=%d id=%d height=%d delay=%d\n",
                idx,
                op->_spotanim.spotanim_id,
                op->_spotanim.height_delay >> 16,
                op->_spotanim.height_delay & 0xffff);
        World_PlayerSetSpotanim(
            world,
            idx,
            op->_spotanim.spotanim_id,
            op->_spotanim.height_delay >> 16,
            op->_spotanim.height_delay & 0xffff);
        break;
    case PKT_PLAYER_INFO_OP_EXACT_MOVE:
    {
        int sx = op->_exactmove.start_x;
        int sz = op->_exactmove.start_z;
        int ex = op->_exactmove.end_x;
        int ez = op->_exactmove.end_z;

        if( op->_exactmove.relative )
        {
            struct WorldEntity_Player* player =
                World_EntityPoolGet(&world->entities.player, idx);
            sx += player->pathing.route_x[0];
            sz += player->pathing.route_z[0];
            ex += player->pathing.route_x[0];
            ez += player->pathing.route_z[0];
        }
        /* The client half of TORIRSSERVER_EXT_DEBUG's exact-move line. Without the
         * pair, "the obstacle did not glide" cannot be split into "the server
         * never set the mask" and "the client dropped the block". */
        if( getenv("TORIRS_NET_DEBUG") )
            TORIRS_LOG("exactmove player idx=%d (%d,%d)->(%d,%d) cycles %d..%d "
                    "facing=%d yaw=%d\n",
                    idx, sx, sz, ex, ez, op->_exactmove.start_cycle_delta,
                    op->_exactmove.end_cycle_delta, op->_exactmove.facing,
                    (int)op->_exactmove.facing_is_yaw);
        World_PlayerSetExactMoveDetailed(
            world,
            idx,
            sx,
            sz,
            ex,
            ez,
            op->_exactmove.start_cycle_delta,
            op->_exactmove.end_cycle_delta,
            op->_exactmove.facing,
            op->_exactmove.facing_is_yaw);
        break;
    }
    case PKT_PLAYER_INFO_OP_FACE_ANGLE:
        if( op->_face_angle.modern )
            World_PlayerBeginModernFacing(world, idx, op->_face_angle.movement_mode);
        World_PlayerFaceAngle(world, idx, op->_face_angle.angle, op->_face_angle.instant);
        break;
    default:
        break;
    }
    return PLAYER_NEED_NONE;
}

static struct ToriRS_Task*
player_slot_cfg_task(struct Task_ExecPlayerInfo* self)
{
    int value = self->app_decoded.slots[self->cfg_i];
    switch( Appearance_SlotKind(value) )
    {
    case APPEARANCE_SLOT_KIT:
        return CreateTask_IdkLoad(self->app->provider, Appearance_SlotKit(value));
    case APPEARANCE_SLOT_OBJ:
        return CreateTask_ObjLoad(self->app->provider, Appearance_SlotObj(value));
    case APPEARANCE_SLOT_EMPTY:
    default:
        return NULL;
    }
}

static struct ToriRS_Task*
player_appearance_seq_task(struct Task_ExecPlayerInfo* self)
{
    int ids[7] = {
        self->app_decoded.readyanim,   self->app_decoded.turnanim,
        self->app_decoded.walkanim,    self->app_decoded.walkanim_b,
        self->app_decoded.walkanim_l,  self->app_decoded.walkanim_r,
        self->app_decoded.runanim,
    };
    int seq_id = ids[self->seq_i];
    if( seq_id < 0 )
        return NULL;
    return CreateTask_SequenceLoad(self->app->provider, self->app->scene, seq_id);
}

static int
Task_ExecPlayerInfo_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_ExecPlayerInfo* self = (struct Task_ExecPlayerInfo*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);

    self->ops = player_ops_borrow(app);
    assert(self->ops);
    /* A revision whose stream is a different CODEC states its own reader
     * (GameProtoRevTable.player_info_read); NULL means the classic bitstream.
     * Both produce the same op array, so nothing below this line branches. */
    {
        struct GameProtoRevTable const* rev = app->net ? app->net->rev : NULL;

        self->op_count =
            rev && rev->player_info_read
                ? rev->player_info_read(
                      self->data, self->length, self->ops, ENTITY_INFO_OPS_MAX)
                : pkt_player_info_reader_read(
                      &self->reader, self->data, self->length, self->ops, ENTITY_INFO_OPS_MAX);
    }

    /* Snapshot the tracked list: ADD_OLD indexes the PREVIOUS packet's
     * order while the walk rebuilds the live list in place. */
    self->old_count = app->esync.active_player_count;
    memcpy(
        self->old_list,
        app->esync.active_players,
        (size_t)self->old_count * sizeof(self->old_list[0]));
    app->esync.active_player_count = 0;
    self->cur_pid = -1;

    for( self->op_i = 0; self->op_i < self->op_count; self->op_i++ )
    {
        self->op_consumed = player_target_op(self, &self->ops[self->op_i]);

        if( self->need_ensure )
        {
            PT_TASK_AWAITSELF_IF(CreateTask_PlayerAppearanceLoad(app->provider));
            PT_TASK_AWAITSELF_IF(
                CreateTask_SequenceLoad(app->provider, app->scene, PLAYER_DEFAULT_SEQ_READY));
            player_ensure_now(self);
        }

        if( !self->op_consumed )
        {
            int need = player_apply_op(self, &self->ops[self->op_i]);
            if( need == PLAYER_NEED_APPEARANCE )
            {
                for( self->cfg_i = 0; self->cfg_i < 12; self->cfg_i++ )
                {
                    PT_TASK_AWAITSELF_IF(player_slot_cfg_task(self));
                }
                self->model_count = PlayerModel_CollectAppearanceModelIds(
                    app->provider,
                    self->app_decoded.slots,
                    self->app_decoded.gender,
                    self->model_ids,
                    (int)(sizeof(self->model_ids) / sizeof(self->model_ids[0])));
                for( self->model_i = 0; self->model_i < self->model_count; self->model_i++ )
                {
                    PT_TASK_AWAITSELF_IF(
                        CreateTask_ModelLoad(app->provider, self->model_ids[self->model_i]));
                }
                for( self->seq_i = 0; self->seq_i < 7; self->seq_i++ )
                {
                    PT_TASK_AWAITSELF_IF(player_appearance_seq_task(self));
                }
                /* Re-resolve after the yields, for the same reason the npc
                 * path does: world pool indices and scene element ids are
                 * recycled, and the awaits above let the world move underneath
                 * the pair cached before them. `cur_pid` is the stable
                 * identity. Applying a stale pair here dresses some other
                 * player in this one's appearance. */
                /* Resolved AFTER the awaits above, never before them. */
                {
                    int element_id;
                    int const world_idx = player_target(self, &element_id);
                    if( world_idx >= 0 )
                        App_WorldApplyPlayerAppearance(
                            app, world_idx, element_id, &self->app_decoded);
                }
            }
            else if( need == PLAYER_NEED_SEQ )
            {
                PT_TASK_AWAITSELF_IF(
                    self->pending_seq >= 0
                        ? CreateTask_SequenceLoad(app->provider, app->scene, self->pending_seq)
                        : NULL);

                /* Held-item replacement (reference ClientPlayer.getTempModel2 via
                 * SeqType.replaceheldleft/right, opcodes 6/7): a woodcutting/
                 * mining-style seq swaps a worn item for an obj that is NOT part
                 * of the player's appearance, so its config + wear models were
                 * never fetched by the APPEARANCE path. Ensure them now — the
                 * per-frame swap (app_world_apply_player_held_items) builds the
                 * player model synchronously and silently drops any wear model
                 * that is not resident, so the swapped item would otherwise never
                 * appear. The reference defers the model build until
                 * ObjType.checkWearModel loads; we pre-load instead. Loading is
                 * idempotent, so re-issuing on every SEQ is cheap. */
                {
                    struct ToriDraw_Animation* prim =
                        self->pending_seq >= 0
                            ? ToriDraw_SceneAnimationGet(app->scene, self->pending_seq)
                            : NULL;
                    self->held_vals[0] =
                        Appearance_FromCacheValue(prim ? prim->replaceheldleft : -1);
                    self->held_vals[1] =
                        Appearance_FromCacheValue(prim ? prim->replaceheldright : -1);
                }
                /* Obj configs first — the seq's replaceheld values are appearance
                 * slots, so only an obj-range one names an obj; anything lower
                 * hides the item and needs no model. */
                for( self->cfg_i = 0; self->cfg_i < 2; self->cfg_i++ )
                    PT_TASK_AWAITSELF_IF(
                        Appearance_SlotKind(self->held_vals[self->cfg_i]) ==
                                APPEARANCE_SLOT_OBJ
                            ? CreateTask_ObjLoad(
                                  app->provider,
                                  Appearance_SlotObj(self->held_vals[self->cfg_i]))
                            : NULL);
                /* Then their gendered wear models (slot 3 = right hand, slot 5 =
                 * left hand — same appearance encoding the swap feeds the build). */
                {
                    struct WorldEntity_Player* held_player = World_EntityPoolGet(
                        &app->world->entities.player, player_target(self, NULL));
                    int held_slots[12];
                    for( int k = 0; k < 12; k++ )
                        held_slots[k] = -1;
                    held_slots[3] =
                        Appearance_SlotKind(self->held_vals[1]) == APPEARANCE_SLOT_OBJ
                            ? self->held_vals[1]
                            : -1;
                    held_slots[5] =
                        Appearance_SlotKind(self->held_vals[0]) == APPEARANCE_SLOT_OBJ
                            ? self->held_vals[0]
                            : -1;
                    self->model_count = PlayerModel_CollectAppearanceModelIds(
                        app->provider,
                        held_slots,
                        held_player ? held_player->gender : 0,
                        self->model_ids,
                        (int)(sizeof(self->model_ids) / sizeof(self->model_ids[0])));
                }
                for( self->model_i = 0; self->model_i < self->model_count; self->model_i++ )
                    PT_TASK_AWAITSELF_IF(
                        CreateTask_ModelLoad(app->provider, self->model_ids[self->model_i]));

                {
                    int const world_idx = player_target(self, NULL);
                    if( world_idx >= 0 )
                        World_PlayerSetPrimaryAnimation(
                            app->world, world_idx, self->pending_seq, self->pending_delay);
                }
            }
        }
    }

    app->need_redraw = 1;

    PT_END(&self->pt);
}

static void
Task_ExecPlayerInfo_Free(struct ToriRS_Task* base)
{
    struct Task_ExecPlayerInfo* self = (struct Task_ExecPlayerInfo*)base;
    player_ops_release(self->app, self->ops, self->op_count);
    free(self);
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
    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_ExecPlayerInfo_VTable;
    strncpy(task->task.name, "ExecPlayerInfo", sizeof(task->task.name) - 1);
    task->app = app;
    task->data = data;
    task->length = length;
    PT_INIT(&task->pt);
    return &task->task;
}

/* ------------------------------------------------------------------ */
/* NPC_INFO                                                            */
/* ------------------------------------------------------------------ */

enum NpcOpNeed
{
    NPC_NEED_NONE = 0,
    NPC_NEED_SPAWN,
    NPC_NEED_SEQ,
    NPC_NEED_CHANGE_TYPE,
};

struct Task_ExecNpcInfo
{
    struct ToriRS_Task task;
    struct pt pt;
    struct App* app;
    uint8_t const* data;
    int length;

    struct PktNpcInfoReader reader;
    struct PktNpcInfoOp* ops;
    int op_count;
    int op_i;

    int old_list[RS_ENTITY_SYNC_MAX_NPCS];
    int old_count;

    int cur_slot;

    /* The wire type remains the per-player multiNpc wrapper; pending_npc_type
     * is the child selected from this App's local varps after async loading. */
    int pending_npc_base_type;
    int pending_npc_type;
    int pending_npc_hidden;
    int pending_seq;
    int pending_delay;

    uint64_t bd_start; /* TORIRS_NPCINFO_BREAKDOWN only */
};

static void
npc_local_tile(
    struct Task_ExecNpcInfo* self,
    int* out_x,
    int* out_z,
    int* out_level)
{
    int world_idx;
    struct RS_EntitySync* esync = &self->app->esync;
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
            World_EntityPoolGet(&self->app->world->entities.player, world_idx);
        if( player )
        {
            /* routeX[0], not grid_position — see player_local_tile. */
            *out_x = player->pathing.route_x[0];
            *out_z = player->pathing.route_z[0];
            *out_level = player->grid_position.level;
        }
    }
}

static void
npc_spawn_now(struct Task_ExecNpcInfo* self)
{
    struct App* app = self->app;
    int tile_x, tile_z, level;
    int idx;

    npc_local_tile(self, &tile_x, &tile_z, &level);
    idx = App_WorldSpawnSyncedNpc(
        app, self->pending_npc_type, self->pending_npc_base_type, tile_x, tile_z, level);
    if( idx < 0 )
    {
        /* The one failure with no retry: npc_add fires once per spawn, so an
         * npc that fails here is absent for the rest of the session while its
         * slot stays in the tracked list and every later op on it is dropped. */
        npc_trace(
            app, self->pending_npc_type, self->cur_slot, -1, -1, -1, "SPAWN_FAILED", 1);
        return;
    }
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(&app->world->entities.npc, idx);
        int element_id = -1;
        if( npc )
        {
            npc->base_npc_id = self->pending_npc_base_type;
            npc->multinpc_hidden = self->pending_npc_hidden != 0;
            npc->server_slot = self->cur_slot;
            element_id = npc->element_id;
            RS_EntitySync_RegisterNpc(&app->esync, self->cur_slot, element_id, idx);
            /* The cache's own "an npc appeared" script, now that the npc
             * carries the uid every op keys on. See game/rs_client_trigger.h. */
            App_ClientTriggerNpcAdd(app, idx);
        }
        npc_trace(
            app, self->pending_npc_type, self->cur_slot, -1, idx, element_id, "SPAWNED", 1);
    }
    entity_debug_log("entity_sync: npc %d spawned (world idx %d)\n", self->cur_slot, idx);
}

/*
 * Resolve the current target's world-pool index (and optionally its scene
 * element) from `cur_slot`, at the point of use. See player_target above for
 * why this is never cached across a PT_TASK_AWAITSELF_IF suspension.
 */
static inline int
npc_target(struct Task_ExecNpcInfo* self, int* out_element_id)
{
    int world_idx = -1;
    int element_id = -1;

    if( self->cur_slot >= 0 )
        RS_EntitySync_FindNpc(&self->app->esync, self->cur_slot, &world_idx, &element_id);
    if( out_element_id )
        *out_element_id = element_id;
    return world_idx;
}

/* Target/list bookkeeping. Returns 1 when the op was fully handled. */
static int
npc_target_op(
    struct Task_ExecNpcInfo* self,
    struct PktNpcInfoOp const* op)
{
    struct RS_EntitySync* esync = &self->app->esync;

    switch( op->kind )
    {
    /*
     * The rebuilt list is POSITIONAL, and the decoder is what defines the
     * positions.
     *
     * Extended info (SEQUENCE, TRANSFORMATION, SPOTANIM, HITMARKS, ...) is not
     * addressed by server slot -- it is addressed by the npc's index in the
     * list the two sides rebuild in lockstep this tick, and
     * SET_NPC_OPBITS_IDX below resolves it as `active_npcs[list_idx]`. The
     * decoder advances that index for EVERY npc it keeps or adds
     * (osrs239_entity_info.c, `list_idx++`), whether or not this side can
     * resolve the slot behind it.
     *
     * So the append here must be unconditional too. Skipping it -- which the
     * old code did whenever `old_list` was shorter than the wire's count and
     * the lookup yielded -1 -- leaves this list one entry short of the
     * decoder's, and from that point every remaining mask in the packet lands
     * on the npc AFTER the one it was written for. That is silent: the ops all
     * apply, to the wrong entity. An unresolvable position is recorded as -1,
     * which the ops below already treat as "no target", so the failure mode
     * becomes one dropped update instead of a whole packet of misdirected ones.
     */
    case PKT_NPC_INFO_OP_ADD_NPC_OLD_OPBITS_IDX:
        self->cur_slot = (int)op->_bitvalue < self->old_count ? self->old_list[op->_bitvalue] : -1;
        if( esync->active_npc_count < RS_ENTITY_SYNC_MAX_NPCS )
            esync->active_npcs[esync->active_npc_count++] = self->cur_slot;
        break;
    /*
     * ENTERING VIEW: the server is telling us this slot is a NEW npc.
     *
     * The slot is the server's private per-observer name and it is REUSED --
     * `ToriRSServer_SlotMapRelease` frees a name the moment an npc leaves view or
     * teleports, and `ToriRSServer_SlotMapAcquire` hands it straight back out
     * round-robin. This side only drops a name when it sees an explicit
     * CLEAR keyed by the npc's position in the PREVIOUS packet's list, so any
     * release the client never saw a matching CLEAR for leaves a stale
     * registration behind.
     *
     * Adopting the new npc without clearing that first is what made calling a
     * familiar move the Queen Black Dragon: the familiar teleports, the server
     * frees and re-mints its name, the name collides with the one still
     * registered to her, and the DELTA_XZ that follows this op resolves
     * `cur_slot` to HER world index and jumps her to `local_player_tile + dx`
     * -- one pop, right next to the player. Every other field in the record
     * (type, facing, spawn cycle) lands on her too.
     *
     * "Entering view" is unambiguous about intent, so the disagreement is
     * resolved in the server's favour: whatever this side still has under that
     * name is stale by definition and is despawned before the new npc takes
     * it. Reported, because a collision means a release went unmatched
     * upstream and that is still worth finding.
     */
    case PKT_NPC_INFO_OP_ADD_NPC_NEW_OPBITS_PID:
    {
        int stale_world_idx = -1;
        int stale_element_id = -1;

        self->cur_slot = (int)op->_bitvalue;
        if( self->cur_slot >= 0 &&
            RS_EntitySync_FindNpc(esync, self->cur_slot, &stale_world_idx, &stale_element_id) )
        {
            TORIRS_LOG("entity_sync: npc slot %d entered view but is still registered "
                "(world %d, element %d) - despawning the stale entity first\n",
                self->cur_slot, stale_world_idx, stale_element_id);
            npc_trace(
                self->app, npc_trace_type_of(self->app, stale_world_idx), self->cur_slot, -1,
                stale_world_idx, stale_element_id, "STALE_SLOT_REUSED", 1);
            RS_EntitySync_RemoveNpc(esync, self->app->world, self->cur_slot);
        }
        if( esync->active_npc_count < RS_ENTITY_SYNC_MAX_NPCS )
            esync->active_npcs[esync->active_npc_count++] = self->cur_slot;
        break;
    }
    case PKT_NPC_INFO_OP_SET_NPC_OPBITS_IDX:
        self->cur_slot = (int)op->_bitvalue < esync->active_npc_count
                             ? esync->active_npcs[op->_bitvalue]
                             : -1;
        break;
    case PKT_NPC_INFO_OP_CLEAR_NPC_OPBITS_IDX:
    {
        int slot = (int)op->_bitvalue < self->old_count ? self->old_list[op->_bitvalue] : -1;
        if( slot >= 0 )
        {
            int w = -1, e = -1;
            RS_EntitySync_FindNpc(esync, slot, &w, &e);
            npc_trace(
                self->app, npc_trace_type_of(self->app, w), slot, (int)op->_bitvalue, w, e,
                "REMOVE", 1);
            RS_EntitySync_RemoveNpc(esync, self->app->world, slot);
        }
        self->cur_slot = -1;
        return 1;
    }
    case PKT_NPC_INFO_OPBITS_COUNT_RESET:
    {
        /* Client-TS getNpcPosOldVis: when the wire 8-bit count is less than
         * the previous tracked length, indices [count, oldCount) are removed.
         * Plane-change zeros tracked_count and sends count=0 — without this,
         * old-floor NPCs linger in the world pool (drawn on minusedlevel,
         * rejected by pick → visible but Walk-here only). */
        int keep = (int)op->_bitvalue;
        for( int i = keep; i < self->old_count; i++ )
        {
            int w = -1, e = -1;
            RS_EntitySync_FindNpc(esync, self->old_list[i], &w, &e);
            npc_trace(
                self->app, npc_trace_type_of(self->app, w), self->old_list[i], i, w, e,
                "REMOVE_COUNT_SHRINK", keep);
            RS_EntitySync_RemoveNpc(esync, self->app->world, self->old_list[i]);
        }
        esync->active_npc_count = 0;
        return 1;
    }
    case PKT_NPC_INFO_OPBITS_INFO:
        return 1;
    default:
        return 0;
    }

    (void)esync;
    {
        int element_id;
        int const world_idx = npc_target(self, &element_id);
        npc_trace(
            self->app, npc_trace_type_of(self->app, world_idx), self->cur_slot,
            (int)op->_bitvalue, world_idx, element_id, "target_op", (int)op->kind);
    }
    return 1;
}

static enum NpcOpNeed
npc_apply_op(
    struct Task_ExecNpcInfo* self,
    struct PktNpcInfoOp const* op)
{
    struct App* app = self->app;
    struct World* world = app->world;
    int idx = npc_target(self, NULL);

    switch( op->kind )
    {
    case PKT_NPC_INFO_OPBITS_NPCTYPE:
        /* Preserve the wire wrapper. It is resolved only after its config is
         * loaded, and retained on the entity so later local varp changes can
         * select a different child for this player without server mutation. */
        self->pending_npc_base_type = (int)op->_bitvalue;
        if( self->cur_slot >= 0 && idx < 0 )
            return NPC_NEED_SPAWN;
        break;
    case PKT_NPC_INFO_OPBITS_WALKDIR:
        if( idx >= 0 )
        {
            npc_trace(
                self->app, npc_trace_type_of(self->app, idx), self->cur_slot, -1, idx, -1,
                "MOVE_WALK", (int)op->_bitvalue);
            World_NpcPathPushStep(world, idx, WORLD_PATHSTEP_WALK, (int)op->_bitvalue);
        }
        break;
    case PKT_NPC_INFO_OPBITS_RUNDIR:
        if( idx >= 0 )
        {
            npc_trace(
                self->app, npc_trace_type_of(self->app, idx), self->cur_slot, -1, idx, -1,
                "MOVE_RUN", (int)op->_bitvalue);
            World_NpcPathPushStep(world, idx, WORLD_PATHSTEP_RUN, (int)op->_bitvalue);
        }
        break;
    case PKT_NPC_INFO_OP_DELTA_XZ:
        if( idx >= 0 )
        {
            int lx, lz, llevel;
            npc_local_tile(self, &lx, &lz, &llevel);
            /*
             * The op that teleports an npc, and the one that moves the Queen
             * when a familiar is called: `idx` is whatever `cur_slot` resolved
             * to, and the destination is the LOCAL PLAYER's tile plus a small
             * delta -- so a misdirected one lands the wrong creature next to
             * the player. Traced with both the slot and the destination so the
             * log says which npc was targeted and where it was sent.
             */
            npc_trace(
                self->app, npc_trace_type_of(self->app, idx), self->cur_slot, -1, idx,
                (lx + op->_delta_xz.dx) * 1000 + (lz + op->_delta_xz.dz), "MOVE_JUMP_TO_TILE",
                op->_delta_xz.jump);
            World_NpcPathJump(
                world, idx, op->_delta_xz.jump, lx + op->_delta_xz.dx, lz + op->_delta_xz.dz);
        }
        break;
    case PKT_NPC_INFO_OP_SEQUENCE:
        if( getenv("TORIRS_ANIM_DEBUG") )
            TORIRS_LOG("anim: npc SEQUENCE op seq=%d delay=%d slot=%d world_idx=%d%s\n",
                    op->_sequence.sequence_id, op->_sequence.delay, self->cur_slot, idx,
                    idx >= 0 ? "" : "  <-- NO TARGET, dropped");
        if( idx >= 0 )
        {
            self->pending_seq = op->_sequence.sequence_id;
            self->pending_delay = op->_sequence.delay;
            return NPC_NEED_SEQ;
        }
        break;
    case PKT_NPC_INFO_OP_FACE_ENTITY:
        if( idx >= 0 )
        {
            int entity_id = op->_face_entity.entity_id == 65535 ? -1 : op->_face_entity.entity_id;
            if( op->_face_entity.modern )
                World_NpcBeginModernFacing(world, idx, op->_face_entity.movement_mode);
            World_NpcFaceEntityDetailed(
                world,
                idx,
                entity_id,
                op->_face_entity.has_fallback_angle ? op->_face_entity.fallback_angle : -1,
                op->_face_entity.instant);
            entity_debug_log("entity_sync: npc %d faces entity %d\n", self->cur_slot, entity_id);
        }
        break;
    case PKT_NPC_INFO_OP_FACE_COORD:
        /* Raw wire half-tiles — see the player branch. */
        if( idx >= 0 )
        {
            if( op->_face_coord.modern )
                World_NpcBeginModernFacing(world, idx, op->_face_coord.movement_mode);
            World_NpcFaceCoord(world, idx, op->_face_coord.x, op->_face_coord.z);
            if( op->_face_coord.instant )
            {
                struct WorldEntity_NPC* npc =
                    World_EntityPoolAt(&world->entities.npc, idx);
                npc->facing.instant = true;
            }
            entity_debug_log(
                "entity_sync: npc faces coord %d,%d\n", op->_face_coord.x, op->_face_coord.z);
        }
        break;
    case PKT_NPC_INFO_OP_SAY:
        /* NPC forced chat: plain yellow overhead (colour/effect 0). */
        if( idx >= 0 && op->_say.text )
            World_NpcSetChat(world, idx, op->_say.text, 0, 0);
        break;
    case PKT_NPC_INFO_OP_DAMAGE:
        if( idx >= 0 )
            World_NpcAddHitmarkTimed(
                world,
                idx,
                op->_damage.damage_type,
                op->_damage.damage,
                op->_damage.health,
                op->_damage.total_health,
                op->_damage.delay,
                op->_damage.slots,
                RS_Hitsplats_DurationFor(&self->app->hitsplats, op->_damage.damage_type),
                RS_Hitsplats_SlotPolicyFor(&self->app->hitsplats, op->_damage.damage_type));
        break;
    case PKT_NPC_INFO_OP_HEADBAR:
        if( idx >= 0 )
        {
            if( op->_headbar.remove )
                World_NpcClearHealthbar(world, idx);
            else
                World_NpcSetHealthbar(
                    world,
                    idx,
                    headbar_from_block(
                        self->app, world, op->_headbar.type, op->_headbar.duration,
                        op->_headbar.start_delay, op->_headbar.start_fill,
                        op->_headbar.end_fill));
        }
        break;
    case PKT_NPC_INFO_OP_CHANGE_TYPE:
        /* A server transformation replaces the wrapper itself. The selected
         * child remains client-local and may differ between observers. */
        self->pending_npc_base_type = op->_change_type.npc_type;
        if( idx >= 0 )
            return NPC_NEED_CHANGE_TYPE;
        break;
    case PKT_NPC_INFO_OP_SPOTANIM:
        if( idx >= 0 )
            World_NpcSetSpotanim(
                world,
                idx,
                op->_spotanim.spotanim_id,
                op->_spotanim.height_delay >> 16,
                op->_spotanim.height_delay & 0xffff);
        break;
    case PKT_NPC_INFO_OP_EXACT_MOVE:
        if( idx >= 0 )
        {
            struct WorldEntity_NPC* npc =
                World_EntityPoolAt(&world->entities.npc, idx);
            int sx = op->_exactmove.start_x;
            int sz = op->_exactmove.start_z;
            int ex = op->_exactmove.end_x;
            int ez = op->_exactmove.end_z;

            if( op->_exactmove.relative )
            {
                sx += npc->pathing.route_x[0];
                sz += npc->pathing.route_z[0];
                ex += npc->pathing.route_x[0];
                ez += npc->pathing.route_z[0];
            }
            World_NpcSetExactMoveDetailed(
                world,
                idx,
                sx,
                sz,
                ex,
                ez,
                op->_exactmove.start_cycle_delta,
                op->_exactmove.end_cycle_delta,
                op->_exactmove.facing,
                op->_exactmove.facing_is_yaw);
        }
        break;
    case PKT_NPC_INFO_OP_FACE_ANGLE:
        if( idx >= 0 )
        {
            struct WorldEntity_NPC* npc =
                World_EntityPoolAt(&world->entities.npc, idx);
            if( op->_face_angle.modern )
                World_NpcBeginModernFacing(world, idx, op->_face_angle.movement_mode);
            int angle = op->_face_angle.spawn && npc->facing.turn_speed == 0
                            ? 0
                            : op->_face_angle.angle;
            World_NpcFaceAngle(world, idx, angle, op->_face_angle.instant);
        }
        break;
    case PKT_NPC_INFO_OP_SPAWN_CYCLE:
        if( idx >= 0 )
        {
            struct WorldEntity_NPC* npc =
                World_EntityPoolAt(&world->entities.npc, idx);
            npc->spawn_cycle = (uint32_t)op->_bitvalue;
        }
        break;
    case PKT_NPC_INFO_OP_VISIBLE_OPS:
        if( idx >= 0 )
        {
            struct WorldEntity_NPC* npc =
                World_EntityPoolAt(&world->entities.npc, idx);
            npc->visible_ops = (uint8_t)op->_bitvalue;
        }
        break;
    case PKT_NPC_INFO_OP_NAME_CHANGE:
        if( idx >= 0 && op->_name_change.name )
        {
            struct WorldEntity_NPC* npc =
                World_EntityPoolAt(&world->entities.npc, idx);
            strncpy(npc->name, op->_name_change.name, sizeof(npc->name) - 1);
            npc->name[sizeof(npc->name) - 1] = '\0';
        }
        break;
    case PKT_NPC_INFO_OP_LEVEL_CHANGE:
        if( idx >= 0 )
        {
            struct WorldEntity_NPC* npc =
                World_EntityPoolAt(&world->entities.npc, idx);
            npc->combat_level = (int)(uint32_t)op->_bitvalue;
        }
        break;
    case PKT_NPC_INFO_OP_BAS_CHANGE:
        if( idx >= 0 )
        {
            struct WorldEntity_NPC* npc =
                World_EntityPoolAt(&world->entities.npc, idx);
            uint32_t mask = op->_bas_change.mask;

            if( mask & (1u << 0) ) npc->idle_animations.turnanim = op->_bas_change.turnanim;
            if( mask & (1u << 2) ) npc->idle_animations.walkanim = op->_bas_change.walkanim;
            if( mask & (1u << 3) ) npc->idle_animations.walkanim_b = op->_bas_change.walkanim_b;
            if( mask & (1u << 4) ) npc->idle_animations.walkanim_l = op->_bas_change.walkanim_l;
            if( mask & (1u << 5) ) npc->idle_animations.walkanim_r = op->_bas_change.walkanim_r;
            if( mask & (1u << 6) ) npc->idle_animations.runanim = op->_bas_change.runanim;
            if( mask & (1u << 14) ) npc->idle_animations.readyanim = op->_bas_change.readyanim;
        }
        break;
    default:
        break;
    }
    return NPC_NEED_NONE;
}

static int
Task_ExecNpcInfo_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_ExecNpcInfo* self = (struct Task_ExecNpcInfo*)base;
    struct App* app = self->app;

    PT_BEGIN(&self->pt);

    g_bd_decode = g_bd_await = g_bd_spawn = g_bd_retype = g_bd_apply = 0;
    g_bd_ops = g_bd_spawns = 0;
    self->bd_start = BD_T0();

    self->ops = npc_ops_borrow(app);
    assert(self->ops);
    /* The new-npc record's slot and type widths are revision state, not
     * constants — see GameProtoRevTable.npc_type_bits. Zero widths (no net
     * layer, as in the unit tests) take the classic defaults. */
    pkt_npc_info_reader_init(
        &self->reader,
        app->net && app->net->rev ? app->net->rev->npc_slot_bits : 0,
        app->net && app->net->rev ? app->net->rev->npc_type_bits : 0);
    {
        struct GameProtoRevTable const* rev = app->net ? app->net->rev : NULL;
        uint64_t t0 = BD_T0();

        self->op_count =
            rev && rev->npc_info_read
                ? rev->npc_info_read(self->data, self->length, self->ops, ENTITY_INFO_OPS_MAX)
                : pkt_npc_info_reader_read(
                      &self->reader, self->data, self->length, self->ops, ENTITY_INFO_OPS_MAX);
        BD_ADD(g_bd_decode, t0);
    }

    self->old_count = app->esync.active_npc_count;
    memcpy(
        self->old_list,
        app->esync.active_npcs,
        (size_t)self->old_count * sizeof(self->old_list[0]));
    app->esync.active_npc_count = 0;
    self->cur_slot = -1;

    for( self->op_i = 0; self->op_i < self->op_count; self->op_i++ )
    {
        uint64_t bd_t = BD_T0();
        int targeted = npc_target_op(self, &self->ops[self->op_i]);

        BD_ADD(g_bd_apply, bd_t);
        g_bd_ops++;
        if( !targeted )
        {
            int need;

            bd_t = BD_T0();
            need = npc_apply_op(self, &self->ops[self->op_i]);
            BD_ADD(g_bd_apply, bd_t);
            if( need == NPC_NEED_SPAWN || need == NPC_NEED_CHANGE_TYPE )
            {
                g_bd_spawns++;
                PT_TASK_AWAITSELF_IF(CreateTask_NpcMultiLoad(
                    app, self->pending_npc_base_type, &self->pending_npc_type));
                /* A hidden transform still needs a live entity for later
                 * masks and varp-driven reappearance. Mount the model-less
                 * wrapper as that marker until its selected child changes. */
                self->pending_npc_hidden = self->pending_npc_type < 0;
                if( self->pending_npc_type < 0 )
                    self->pending_npc_type = self->pending_npc_base_type;
                /*
                 * Resolved AFTER the awaits, never before them.
                 *
                 * the target's indices used to be resolved before the
                 * asset awaits, and a world pool index and a scene element id
                 * are both RECYCLED. Across those yields the client keeps
                 * running -- entities despawn, pool slots and element ids are
                 * handed out again -- so by the time the loads land the pair
                 * may name a completely different creature, and
                 * App_WorldApplyNpcType would then retype and re-place THAT
                 * one. A big familiar is the worst case because its models are
                 * the slowest to load and so hold the longest yield: calling a
                 * titan in the QBD arena made the Queen jump to the player,
                 * because she was what those stale indices had come to mean.
                 *
                 * `cur_slot` is the server's npc name and is stable across the
                 * yield, so it is the only thing here worth trusting; resolve
                 * from it again rather than from what was cached before.
                 */
                bd_t = BD_T0();
                {
                    int element_id;
                    int const world_idx = npc_target(self, &element_id);
                    if( world_idx < 0 )
                    {
                        npc_spawn_now(self);
                        BD_ADD(g_bd_spawn, bd_t);
                    }
                    else
                    {
                        struct WorldEntity_NPC* npc =
                            World_EntityPoolGet(&app->world->entities.npc, world_idx);
                        if( npc )
                        {
                            npc->base_npc_id = self->pending_npc_base_type;
                            npc->multinpc_hidden = self->pending_npc_hidden != 0;
                        }
                        App_WorldApplyNpcType(
                            app,
                            world_idx,
                            element_id,
                            self->pending_npc_type,
                            self->pending_npc_base_type);
                        BD_ADD(g_bd_retype, bd_t);
                    }
                }
            }
            else if( need == NPC_NEED_SEQ )
            {
                PT_TASK_AWAITSELF_IF(
                    self->pending_seq >= 0
                        ? CreateTask_SequenceLoad(app->provider, app->scene, self->pending_seq)
                        : NULL);
                {
                    int const world_idx = npc_target(self, NULL);
                    if( world_idx >= 0 )
                        World_NpcSetPrimaryAnimation(
                            app->world, world_idx, self->pending_seq, self->pending_delay);
                    else if( getenv("TORIRS_ANIM_DEBUG") )
                        TORIRS_LOG("anim: npc seq %d DROPPED - slot %d no longer resolves "
                                "(reaped while the sequence load was awaiting)\n",
                                self->pending_seq, self->cur_slot);
                }
            }
        }
    }

    app->need_redraw = 1;

    if( npcinfo_bd_on() )
    {
        uint64_t total = PlatformWindow_TicksUs() - self->bd_start;
        if( total >= (uint64_t)g_npcinfo_bd_ms * 1000u )
            TORIRS_LOG("npcinfo_bd: total %.2f decode %.2f apply %.2f spawn %.2f retype %.2f "
                "rest %.2f | ops %d spawns %d\n",
                total / 1000.0,
                g_bd_decode / 1000.0,
                g_bd_apply / 1000.0,
                g_bd_spawn / 1000.0,
                g_bd_retype / 1000.0,
                (total - g_bd_decode - g_bd_apply - g_bd_spawn - g_bd_retype) / 1000.0,
                g_bd_ops,
                g_bd_spawns);
    }

    PT_END(&self->pt);
}

static void
Task_ExecNpcInfo_Free(struct ToriRS_Task* base)
{
    struct Task_ExecNpcInfo* self = (struct Task_ExecNpcInfo*)base;
    npc_ops_release(self->app, self->ops, self->op_count);
    free(self);
}

static struct ToriRS_TaskVTable Task_ExecNpcInfo_VTable = {
    .run = Task_ExecNpcInfo_Run,
    .free = Task_ExecNpcInfo_Free,
};

struct ToriRS_Task*
CreateTask_ExecNpcInfo(
    struct App* app,
    uint8_t const* data,
    int length)
{
    struct Task_ExecNpcInfo* task;

    assert(app);
    if( length <= 0 )
        return NULL;
    assert(data);
    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_ExecNpcInfo_VTable;
    strncpy(task->task.name, "ExecNpcInfo", sizeof(task->task.name) - 1);
    task->app = app;
    task->data = data;
    task->length = length;
    PT_INIT(&task->pt);
    return &task->task;
}
