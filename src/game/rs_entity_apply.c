#include "game/rs_entity_apply.h"

#include "game/rs_chat.h"
#include "game/rs_entity_sync.h"
#include "log/torirs_log.h"
#include "mem/torirs_mem.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rev/packets/pkt_player_appearance.h"
#include "streams/rsprot_info.h"
#include "torirs_env.h"
#include "world/entity_npc.h"
#include "world/entity_player.h"
#include "world/world.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The local player's pid while UPDATE_PID has not arrived (rev 239 never
 * sends one): the decoders' own sentinel. */
#define RS_ENTITY_LOCAL_SENTINEL 2047

/*
 * The previous packet's tracked list, and the slots this packet removed, for
 * the one call running. The apply is synchronous and nothing it calls applies
 * another packet, so one set serves every caller (each headless bot, the real
 * client, a replay keyframe) in turn; `busy` says so if that ever changes.
 */
static struct
{
    int busy;
    int old_list[RS_ENTITY_SYNC_MAX_NPCS > RS_ENTITY_SYNC_MAX_PLAYERS ? RS_ENTITY_SYNC_MAX_NPCS
                                                                       : RS_ENTITY_SYNC_MAX_PLAYERS];
    int cleared[RS_ENTITY_SYNC_MAX_NPCS];
} g_scratch;

static void
entity_debug_log(char const* fmt, int a, int b)
{
    if( torirs_env_net_debug() )
        TORIRS_LOG(fmt, a, b);
    (void)fmt;
    (void)a;
    (void)b;
}

static void
note(
    struct RS_EntityApplyIn const* in,
    struct RS_EntityNote const* n)
{
    in->host->present(in->host->ctx, n);
}

/* ------------------------------------------------------------------ */
/* Tracing (TORIRS_NPC_TRACE)                                          */
/* ------------------------------------------------------------------ */

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

/* The npc type currently behind `world_idx`, or -1. */
static int
npc_trace_type_of(
    struct World* world,
    int world_idx)
{
    struct WorldEntity_NPC* npc;

    if( world_idx < 0 )
        return -1;
    npc = World_EntityPoolGet(&world->entities.npc, world_idx);
    return npc ? npc->npc_id : -1;
}

static void
npc_trace(
    struct World* world,
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
        npc_id, slot, list_idx, world_idx, element_id, (int)world->cycle, what, detail);
    (void)slot;
    (void)list_idx;
    (void)element_id;
    (void)what;
    (void)detail;
}

/* With TORIRS_NPC_TRACE, the state each traced npc holds as the next NPC_INFO
 * arrives -- what the client did with the last one. */
static void
npc_trace_state(struct World* world)
{
    struct World_EntityPool* pool = &world->entities.npc;

    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC const* npc = World_EntityPoolGet(pool, i);

        if( !npc || !npc_trace_wants(npc->npc_id) )
            continue;
        TORIRS_LOG(
            "npc_state: npc=%d world=%d cycle=%d tile=%d,%d route=%d yaw=%d dst=%d face=%d "
            "primary=%d delay=%d frame=%d secondary=%d\n",
            npc->npc_id, i, (int)world->cycle, npc->grid_position.x, npc->grid_position.z,
            npc->pathing.route_length, npc->orientation.yaw, npc->orientation.dst_yaw,
            npc->facing.entity_id, (int)(int16_t)npc->animation.primary.anim_id,
            npc->animation.primary.delay, npc->animation.primary.frame,
            (int)(int16_t)npc->animation.secondary.anim_id);
    }
}

/* ------------------------------------------------------------------ */
/* Shared pieces                                                       */
/* ------------------------------------------------------------------ */

static struct WorldEntity_Headbar
headbar_from_block(
    struct RS_EntityApplyIn const* in,
    struct RsprotInfoHeadbar const* h)
{
    struct WorldEntity_Headbar bar;

    memset(&bar, 0, sizeof(bar));
    bar.type = h->type;
    bar.start_cycle = (int)in->world->cycle + h->start_delay;
    bar.duration = h->duration;
    bar.start_fill = h->start_fill;
    bar.end_fill = h->end_fill;
    /* Reference `class66.method1483`: an update is dropped once
     * `type.persist + startCycle + duration <= loopCycle`. */
    bar.end_cycle = bar.start_cycle + h->duration + in->host->headbar_persist(in->host->ctx, h->type);
    return bar;
}

struct WorldEntityFacet_IdleAnimations
RS_EntityApply_PlayerSpawnIdle(void)
{
    return (struct WorldEntityFacet_IdleAnimations){
        .readyanim = 808,
        .walkanim = 819,
        .turnanim = 823,
        .turnanim_r = 823,
        .runanim = 824,
        .walkanim_b = 820,
        .walkanim_r = 822,
        .walkanim_l = 821,
        .runanim_b = -1,
        .runanim_r = -1,
        .runanim_l = -1,
        .crawlanim = -1,
        .crawlanim_b = -1,
        .crawlanim_r = -1,
        .crawlanim_l = -1,
    };
}

void
RS_EntityApply_PlayerAppearance(
    struct World* world,
    int idx,
    struct PktPlayerAppearance const* appearance)
{
    struct WorldEntityFacet_IdleAnimations idle = {
        .readyanim = appearance->readyanim,
        .walkanim = appearance->walkanim,
        .turnanim = appearance->turnanim,
        /* rev-239 class60: field1481 = field1480 -- one turn seq both ways. */
        .turnanim_r = appearance->turnanim,
        .runanim = appearance->runanim,
        .walkanim_b = appearance->walkanim_b,
        .walkanim_r = appearance->walkanim_r,
        .walkanim_l = appearance->walkanim_l,
        /* The appearance block states no run or crawl directions. */
        .runanim_b = -1,
        .runanim_r = -1,
        .runanim_l = -1,
        .crawlanim = -1,
        .crawlanim_b = -1,
        .crawlanim_r = -1,
        .crawlanim_l = -1,
    };
    struct WorldEntity_Player* ent;

    assert(world);
    assert(appearance);
    World_PlayerSetAppearance(
        world,
        idx,
        appearance->slots,
        appearance->identkit,
        appearance->colors,
        &idle,
        appearance->name,
        appearance->combat_level,
        appearance->gender);
    ent = World_EntityPoolGet(&world->entities.player, idx);
    if( ent )
    {
        /* Overhead prayer/skull headicon bitmask (reference
         * ClientPlayer.headicons, appearance g1). */
        ent->headicon = appearance->headicon;
        /* The 0xffff first equipment entry's npc: every appearance restates
         * it (LostCity ClientPlayer.ts setAppearance). */
        ent->transmog_npc_id = appearance->npc_id;
    }
}

static void
npc_type_facts(
    struct WorldEntity_NPC* npc,
    struct RS_EntityNpcType const* type)
{
    /* What the server set on the npc stays (WORLD_NPC_OVERRIDE_*). */
    if( !(npc->overrides & WORLD_NPC_OVERRIDE_LEVEL) )
        npc->combat_level = type->combat_level;
    npc->alwaysontop = type->alwaysontop;
    npc->minimap_visible = type->minimap_visible;
    npc->interactable = type->interactable;
    npc->facing.turn_speed = type->turn_speed;
    npc->facing.no_rotation_flag = type->no_rotation_flag;
    if( !(npc->overrides & WORLD_NPC_OVERRIDE_NAME) )
        snprintf(npc->name, sizeof(npc->name), "%s", type->name);
    for( int i = 0; i < 5; i++ )
        snprintf(npc->actions[i].name, sizeof(npc->actions[i].name), "%s", type->actions[i]);
}

static void
npc_type_check(struct RS_EntityNpcType const* type)
{
    assert(type);
    assert(type->name);
    for( int i = 0; i < 5; i++ )
        assert(type->actions[i]);
    (void)type;
}

void
RS_EntityApply_NpcSpawned(
    struct World* world,
    int idx,
    struct RS_EntityNpcType const* type)
{
    struct WorldEntity_NPC* npc;

    assert(world);
    npc_type_check(type);
    npc = World_EntityPoolGet(&world->entities.npc, idx);
    assert(npc);
    npc_type_facts(npc, type);
}

void
RS_EntityApply_NpcRetyped(
    struct World* world,
    int idx,
    int npc_type,
    struct RS_EntityNpcType const* type)
{
    struct WorldEntity_NPC* npc;

    assert(world);
    npc_type_check(type);
    npc = World_EntityPoolGet(&world->entities.npc, idx);
    assert(npc);
    {
        struct WorldEntityFacet_IdleAnimations const idle =
            (npc->overrides & WORLD_NPC_OVERRIDE_STANCES) ? npc->idle_animations : type->idle;
        World_NpcSetType(world, idx, npc_type, type->size, &idle);
    }
    npc = World_EntityPoolGet(&world->entities.npc, idx);
    assert(npc);
    npc_type_facts(npc, type);
}

/* One hit: a soaked hit draws its soak as a second splat on the same delay;
 * a delay-only hit (type -1) draws nothing. Duration and slot-full policy are
 * the hitsplat TYPE's (the host's lookup). */
static void
apply_damage(
    struct RS_EntityApplyIn const* in,
    int idx,
    int is_player,
    struct RsprotInfoDamage const* d)
{
    for( int part = 0; part < (d->soaked ? 2 : 1); part++ )
    {
        int type = part ? d->soak_type : d->damage_type;
        int duration;
        int policy;

        if( type < 0 )
            continue;
        in->host->hitsplat(in->host->ctx, type, &duration, &policy);
        if( is_player )
            World_PlayerAddHitmarkTimed(
                in->world, idx, type, part ? d->soak_damage : d->damage, d->health,
                d->total_health, d->delay, part ? 0 : d->slots, duration, policy);
        else
            World_NpcAddHitmarkTimed(
                in->world, idx, type, part ? d->soak_damage : d->damage, d->health,
                d->total_health, d->delay, part ? 0 : d->slots, duration, policy);
    }
}

/* ------------------------------------------------------------------ */
/* PLAYER_INFO                                                         */
/* ------------------------------------------------------------------ */

struct PlayerApply
{
    struct RS_EntityApplyIn const* in;
    int old_count;
    int cur_pid; /* server slot the following ops target; -1 = none */
    int need_ensure;
};

static int
local_player_pid(struct RS_EntitySync const* esync)
{
    return esync->local_pid >= 0 ? esync->local_pid : RS_ENTITY_LOCAL_SENTINEL;
}

/*
 * Resolve the current target's world-pool index from `cur_pid`, at the point
 * of use. `cur_pid` is the only identity worth trusting: pool indices and
 * scene element ids are recycled.
 */
static int
player_target(struct PlayerApply* self)
{
    int world_idx = -1;

    if( self->cur_pid >= 0 )
        RS_EntitySync_FindPlayer(self->in->esync, self->cur_pid, &world_idx, NULL);
    return world_idx;
}

/* Local player's scene tile (fallback: classic scene center) for relative
 * adds. The tile is routeX[0]/routeZ[0], NOT grid_position: the reference
 * bases every relative add on `localPlayer.routeX[0] + dx` (Client.ts:8034,
 * 8369), the player's authoritative destination tile; grid_position lags it
 * mid-walk by one or more tiles. */
static void
player_local_tile(
    struct RS_EntityApplyIn const* in,
    int* out_x,
    int* out_z,
    int* out_level)
{
    int world_idx;

    *out_x = 52;
    *out_z = 52;
    *out_level = 0;
    if( RS_EntitySync_FindPlayer(in->esync, local_player_pid(in->esync), &world_idx, NULL) )
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(&in->world->entities.player, world_idx);
        if( player )
        {
            *out_x = player->pathing.route_x[0];
            *out_z = player->pathing.route_z[0];
            *out_level = player->grid_position.level;
        }
    }
}

/* Target/list bookkeeping. Returns 1 when the op was fully handled. */
static int
player_target_op(
    struct PlayerApply* self,
    struct RsprotInfoOp const* op)
{
    struct RS_EntitySync* esync = self->in->esync;
    struct World* world = self->in->world;

    switch( op->kind )
    {
    case RSPROT_OP_SET_LOCAL_PLAYER:
        self->cur_pid = local_player_pid(esync);
        break;
    /*
     * Positional, exactly as the npc list is. The classic decoder advances
     * `new_idx` for every tracked entry it reads, including the ones whose
     * info bit is clear, and SET_EXT resolves an extended-info block as
     * `active_players[new_idx]`. So this append cannot be conditional:
     * dropping an unresolvable entry shortens the list and every later block
     * in the packet lands on the player after its intended target. Record -1
     * and let the ops treat it as no target. The local player is NOT in this
     * list (the 2047 sentinel / esync.local_pid). The v5 (rev-239) stream
     * never emits this op: it addresses players by slot.
     */
    case RSPROT_OP_ADD_OLD:
        self->cur_pid = (int)op->value < self->old_count ? g_scratch.old_list[op->value] : -1;
        if( esync->active_player_count < RS_ENTITY_SYNC_MAX_PLAYERS )
            esync->active_players[esync->active_player_count++] = self->cur_pid;
        break;
    case RSPROT_OP_ADD_NEW:
        self->cur_pid = (int)op->value;
        if( esync->active_player_count < RS_ENTITY_SYNC_MAX_PLAYERS )
            esync->active_players[esync->active_player_count++] = self->cur_pid;
        break;
    case RSPROT_OP_SET_EXT:
        self->cur_pid =
            (int)op->value < esync->active_player_count ? esync->active_players[op->value] : -1;
        break;
    case RSPROT_OP_CLEAR_OLD:
    {
        int pid = (int)op->value < self->old_count ? g_scratch.old_list[op->value] : -1;
        if( pid >= 0 )
            RS_EntitySync_RemovePlayer(esync, world, pid);
        self->cur_pid = -1;
        return 1;
    }
    case RSPROT_OP_REMOVE_PID:
        /* The v5 form of the drop: the payload IS the pid. */
        RS_EntitySync_RemovePlayer(esync, world, (int)op->value);
        self->cur_pid = -1;
        return 1;
    case RSPROT_OP_COUNT_RESET:
    {
        /* When the wire 8-bit count is less than the previous tracked length,
         * indices [count, oldCount) are removed. A plane change zeros the
         * tracked count and sends count=0: without this, old-floor players
         * linger in the world pool. */
        int keep = (int)op->value;
        for( int i = keep; i < self->old_count; i++ )
            RS_EntitySync_RemovePlayer(esync, world, g_scratch.old_list[i]);
        esync->active_player_count = 0;
        return 1;
    }
    case RSPROT_OP_INFO:
        return 1;
    default:
        return 0; /* not a target op */
    }

    /* Target changed: flag a spawn when the pid resolves to nothing. */
    self->need_ensure = (self->cur_pid >= 0 && player_target(self) < 0) ? 1 : 0;
    return 1;
}

/* Spawn the current target with no body; the appearance block that follows
 * names the body, which the client mounts once it is resident. */
static void
player_ensure_now(struct PlayerApply* self)
{
    struct RS_EntityApplyIn const* in = self->in;
    int tile_x, tile_z, level;
    int idx;

    self->need_ensure = 0;
    player_local_tile(in, &tile_x, &tile_z, &level);
    idx = in->host->player_spawn(in->host->ctx, tile_x, tile_z, level);
    if( idx < 0 )
        return;
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(&in->world->entities.player, idx);
        if( player )
        {
            player->server_pid = self->cur_pid;
            RS_EntitySync_RegisterPlayer(in->esync, self->cur_pid, player->element_id, idx);
        }
    }
    entity_debug_log("entity_sync: player %d spawned (world idx %d)\n", self->cur_pid, idx);
}

/* The wire's absolute tile for the player at `idx` (entity_facets.h
 * abs_x/abs_z): placed by a coordinate op, moved by a delta or a step. */
static void
player_abs_place(
    struct World* world,
    int idx,
    int abs_x,
    int abs_z)
{
    struct WorldEntity_Player* player = World_EntityPoolGet(&world->entities.player, idx);

    if( !player )
        return;
    player->view_placement.abs_x = abs_x;
    player->view_placement.abs_z = abs_z;
    player->view_placement.has_abs = 1;
}

static void
player_abs_move(
    struct World* world,
    int idx,
    int dx,
    int dz)
{
    struct WorldEntity_Player* player = World_EntityPoolGet(&world->entities.player, idx);

    if( !player || !player->view_placement.has_abs )
        return;
    player->view_placement.abs_x += dx;
    player->view_placement.abs_z += dz;
}

static void
player_abs_step(
    struct World* world,
    int idx,
    int direction)
{
    int dx;
    int dz;

    World_DirectionDelta(direction, &dx, &dz);
    player_abs_move(world, idx, dx, dz);
}

static void
player_note_op(
    struct PlayerApply* self,
    int idx,
    struct RsprotInfoOp const* op,
    int x0,
    int z0,
    int x1,
    int z1)
{
    struct RS_EntityNote n = { .kind = RS_ENTITY_NOTE_PLAYER_OP, .idx = idx, .pid = self->cur_pid,
                               .slot = -1, .op = op, .x0 = x0, .z0 = z0, .x1 = x1, .z1 = z1 };
    note(self->in, &n);
}

static void
player_chat_line(
    struct PlayerApply* self,
    int idx,
    char const* sender,
    char const* sender_key,
    char const* text)
{
    struct RS_EntityNote n = {
        .kind = RS_ENTITY_NOTE_CHAT, .idx = idx, .pid = self->cur_pid, .slot = -1,
        .chat_type = RS_CHAT_TYPE_PUBLIC, .sender = sender, .sender_key = sender_key, .text = text,
    };
    note(self->in, &n);
}

/* Apply one non-target op to the current target. */
static void
player_apply_op(
    struct PlayerApply* self,
    struct RsprotInfoOp const* op)
{
    struct RS_EntityApplyIn const* in = self->in;
    struct World* world = in->world;
    int idx = player_target(self);

    if( idx < 0 )
        return;

    switch( op->kind )
    {
    case RSPROT_OP_WALKDIR:
        World_PlayerPathPushStep(world, idx, WORLD_PATHSTEP_WALK, (int)op->value);
        player_abs_step(world, idx, (int)op->value);
        break;
    case RSPROT_OP_RUNDIR:
        World_PlayerPathPushStep(world, idx, WORLD_PATHSTEP_RUN, (int)op->value);
        player_abs_step(world, idx, (int)op->value);
        break;
    case RSPROT_OP_LOCAL_XZLEVEL:
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(&world->entities.player, idx);
        /* Scene-local. */
        player_abs_place(world, idx, in->scene_base_x + op->xz_level.x, in->scene_base_z + op->xz_level.z);
        World_PlayerPathJump(world, idx, op->xz_level.jump, op->xz_level.x, op->xz_level.z);
        if( player )
            player->grid_position.level = op->xz_level.level;
        break;
    }
    case RSPROT_OP_ABS_XZLEVEL:
    {
        /* The v5 stream's world coordinate, brought into the scene. Unless
         * it is inside a live view's STAGING rectangle -- a rider aboard a
         * hull, whose tiles are deck-instance squares hundreds of zones off
         * this scene. Those rebase into the view's own space (deob: actors
         * carry view-local coordinates), because root-relative they overflow
         * every uint8_t in the route queue. `home_view` is what tells the
         * routing pass the stored coordinates' frame. */
        struct WorldEntity_Player* player = World_EntityPoolGet(&world->entities.player, idx);
        int next_x;
        int next_z;
        int home_view = in->host->home_view(in->host->ctx, op->xz_level.x, op->xz_level.z, &next_x, &next_z);
        int step_type = WORLD_PATHSTEP_WALK;
        struct CollisionMap* collision = NULL;

        player_abs_place(world, idx, op->xz_level.x, op->xz_level.z);
        if( home_view == 0 )
        {
            next_x = op->xz_level.x - in->scene_base_x;
            next_z = op->xz_level.z - in->scene_base_z;
        }
        if( player )
            player->view_placement.home_view = home_view;
        {
            struct RS_EntityNote n = { .kind = RS_ENTITY_NOTE_PLAYER_PLACED, .idx = idx,
                                       .pid = self->cur_pid, .slot = -1, .view = home_view };
            note(in, &n);
        }
        /* Cancelled by an exact move in the same update: the coordinate is
         * the base its tiles are relative to, and nothing more. A player
         * never placed still needs its first snap. */
        if( op->xz_level.route_only && player && !player->pathing.unplaced )
        {
            World_PlayerPathSetHead(world, idx, next_x, next_z);
            player->grid_position.level = op->xz_level.level;
            break;
        }
        /* class174: 2 run, 0 crawl, anything else the walk default. */
        if( op->xz_level.has_move_speed && op->xz_level.move_speed == RSPROT_TRAVERSAL_RUN )
            step_type = WORLD_PATHSTEP_RUN;
        else if( op->xz_level.has_move_speed && op->xz_level.move_speed == RSPROT_TRAVERSAL_CRAWL )
            step_type = WORLD_PATHSTEP_CRAWL;
        /* `level` is a uint8_t, so only the upper bound is a real test. A
         * homed actor's coordinates are view-local -- the root collision map
         * cannot route between them, so the run-route smoothing (cosmetic)
         * sits out and the endpoint is queued bare. */
        if( home_view == 0 && player && player->grid_position.level == op->xz_level.level &&
            op->xz_level.level < COLLISION_LEVELS )
            collision = world->collision_maps[op->xz_level.level];
        World_PlayerPathJumpCollisionAware(
            world, idx, collision, op->xz_level.jump, next_x, next_z, step_type);
        if( player && op->xz_level.has_move_speed )
            entity_debug_log(
                "entity_sync: player traversal=%d run=%d\n", op->xz_level.move_speed,
                player->pathing.route_step[0]);
        if( player )
            player->grid_position.level = op->xz_level.level;
        entity_debug_log("entity_sync: abs move to scene %d,%d\n", next_x, next_z);
        break;
    }
    case RSPROT_OP_DELTA_XZ:
    {
        int lx, lz, llevel;
        player_local_tile(in, &lx, &lz, &llevel);
        World_PlayerPathJump(world, idx, op->delta_xz.jump, lx + op->delta_xz.dx, lz + op->delta_xz.dz);
        player_abs_move(world, idx, op->delta_xz.dx, op->delta_xz.dz);
        break;
    }
    case RSPROT_OP_APPEARANCE:
    {
        /* The block's layout is the revision's: a rev whose appearance shape
         * moved states its own reader (GameProtoRevTable.appearance_decode),
         * NULL the classic block. Either way the canonical appearance. */
        struct PktPlayerAppearance decoded;
        int ok;

        memset(&decoded, 0, sizeof(decoded));
        ok = in->rev->appearance_decode
                 ? in->rev->appearance_decode(op->appearance.appearance, op->appearance.len, &decoded)
                 : PktPlayerAppearance_Decode(&decoded, op->appearance.appearance, op->appearance.len);
        if( ok )
        {
            /* Data only: slots, colours, stances, name. The body is derived
             * from it by the client, once every part is resident. */
            RS_EntityApply_PlayerAppearance(world, idx, &decoded);
            {
                struct RS_EntityNote n = { .kind = RS_ENTITY_NOTE_PLAYER_APPEARANCE, .idx = idx,
                                           .pid = self->cur_pid, .slot = -1, .appearance = &decoded };
                note(in, &n);
            }
        }
        break;
    }
    case RSPROT_OP_SEQUENCE:
        if( torirs_env_net_debug() )
            TORIRS_LOG("player_info: sequence idx=%d id=%d delay=%d\n", idx, op->sequence.sequence_id,
                op->sequence.delay);
        World_PlayerSetPrimaryAnimation(world, idx, op->sequence.sequence_id, op->sequence.delay);
        {
            struct RS_EntityNote n = { .kind = RS_ENTITY_NOTE_PLAYER_SEQ, .idx = idx, .pid = self->cur_pid,
                                       .slot = -1, .seq = op->sequence.sequence_id,
                                       .delay = op->sequence.delay };
            note(in, &n);
        }
        break;
    case RSPROT_OP_FACE_ENTITY:
    {
        int entity_id = op->face_entity.entity_id == 65535 ? -1 : op->face_entity.entity_id;
        player_note_op(self, idx, op, 0, 0, 0, 0);
        if( op->face_entity.modern )
            World_PlayerBeginModernFacing(world, idx, op->face_entity.movement_mode);
        /* A world entity has no registry here: the gamepack's missing-target
         * path turns to the fallback angle. */
        if( op->face_entity.target_type == RSPROT_FACE_TARGET_WORLD_ENTITY && op->face_entity.modern )
        {
            World_PlayerFaceAngle(world, idx, op->face_entity.fallback_angle, op->face_entity.instant);
            break;
        }
        World_PlayerFaceEntityDetailed(
            world, idx, entity_id, op->face_entity.has_fallback_angle ? op->face_entity.fallback_angle : -1,
            op->face_entity.has_fallback_angle && op->face_entity.instant);
        break;
    }
    case RSPROT_OP_FACE_COORD:
        /* Raw wire half-tiles: World_Cycle converts them (the reference keeps
         * faceSquareX/Z absolute and subtracts mapBuildBase at use time, so
         * 0,0 stays the "no target" sentinel). */
        player_note_op(self, idx, op, 0, 0, 0, 0);
        if( op->face_coord.modern )
            World_PlayerBeginModernFacing(world, idx, op->face_coord.movement_mode);
        World_PlayerFaceCoord(world, idx, op->face_coord.x, op->face_coord.z);
        /* `instant` is as sent: it means nothing without a target. */
        if( op->face_coord.instant && (op->face_coord.x != 0 || op->face_coord.z != 0) )
        {
            struct WorldEntity_Player* player = World_EntityPoolGet(&world->entities.player, idx);
            player->facing.instant = true;
        }
        entity_debug_log("entity_sync: player faces coord %d,%d\n", op->face_coord.x, op->face_coord.z);
        break;
    case RSPROT_OP_SAY:
        if( op->say.text )
        {
            struct WorldEntity_Player* player = World_EntityPoolGet(&world->entities.player, idx);
            char const* name = player && player->name[0] ? player->name : NULL;

            player_chat_line(self, idx, name ? name : "Player", name, op->say.text);
            /* Forced chat draws overhead in plain yellow (colour/effect 0). */
            World_PlayerSetChat(world, idx, op->say.text, 0, 0);
        }
        break;
    case RSPROT_OP_CHAT:
        if( op->chat.data && op->chat.length > 0 )
        {
            char* text;
            /* The revision's chat text: OldSchool's Huffman, LostCity's
             * wordpack. */
            assert(in->rev->chat_unpack);
            text = in->rev->chat_unpack(op->chat.data, op->chat.length);
            if( text )
            {
                struct WorldEntity_Player* player = World_EntityPoolGet(&world->entities.player, idx);
                char const* name = player && player->name[0] ? player->name : NULL;
                char crowned[RS_CHAT_SENDER_LEN];

                /* `type` is the speaker's crown: it decorates the printed name
                 * only; the sender friend/ignore keys on stays the bare name. */
                RS_Chat_CrownedName(op->chat.type, name ? name : "Player", crowned, sizeof(crowned));
                player_chat_line(self, idx, crowned, name, text);
                /* colourEffect: high byte = chatColour, low byte = chatEffect
                 * (reference Client.ts:8164). */
                World_PlayerSetChat(
                    world, idx, text, op->chat.colour_effect >> 8, op->chat.colour_effect & 0xff);
                ToriMem_Free(ToriMem_Net(), text); /* chat_unpack's */
            }
        }
        break;
    case RSPROT_OP_DAMAGE:
    case RSPROT_OP_DAMAGE2:
        apply_damage(in, idx, 1, &op->damage);
        break;
    case RSPROT_OP_HEADBAR:
        /* The entity draws one bar: a block's first. */
        if( op->headbar.index != 0 )
            break;
        if( op->headbar.remove )
            World_PlayerClearHealthbar(world, idx);
        else
            World_PlayerSetHealthbar(world, idx, headbar_from_block(in, &op->headbar));
        break;
    case RSPROT_OP_SPOTANIM:
        if( torirs_env_net_debug() )
            TORIRS_LOG("player_info: spotanim idx=%d slot=%d id=%d height=%d delay=%d\n", idx,
                op->spotanim.slot, op->spotanim.spotanim_id, op->spotanim.height_delay >> 16,
                op->spotanim.height_delay & 0xffff);
        World_PlayerSetSpotanim(
            world, idx, op->spotanim.slot, op->spotanim.spotanim_id, op->spotanim.height_delay >> 16,
            op->spotanim.height_delay & 0xffff, op->spotanim.loop);
        if( op->spotanim.spotanim_id >= 0 )
        {
            struct RS_EntityNote n = { .kind = RS_ENTITY_NOTE_SPOTANIM, .idx = idx, .pid = self->cur_pid,
                                       .slot = -1, .seq = op->spotanim.spotanim_id };
            note(in, &n);
        }
        break;
    case RSPROT_OP_EXACT_MOVE:
    {
        int sx = op->exact_move.start_x;
        int sz = op->exact_move.start_z;
        int ex = op->exact_move.end_x;
        int ez = op->exact_move.end_z;

        if( op->exact_move.relative )
        {
            struct WorldEntity_Player* player = World_EntityPoolGet(&world->entities.player, idx);
            sx += player->pathing.route_x[0];
            sz += player->pathing.route_z[0];
            ex += player->pathing.route_x[0];
            ez += player->pathing.route_z[0];
        }
        /* The client half of TORIRSSERVER_EXT_DEBUG's exact-move line. */
        if( torirs_env_net_debug() )
            TORIRS_LOG("exactmove player idx=%d (%d,%d)->(%d,%d) cycles %d..%d facing=%d yaw=%d\n", idx,
                sx, sz, ex, ez, op->exact_move.start_cycle_delta, op->exact_move.end_cycle_delta,
                op->exact_move.facing, (int)op->exact_move.facing_is_yaw);
        player_note_op(self, idx, op, sx, sz, ex, ez);
        World_PlayerSetExactMoveDetailed(
            world, idx, sx, sz, ex, ez, op->exact_move.start_cycle_delta, op->exact_move.end_cycle_delta,
            op->exact_move.facing, op->exact_move.facing_is_yaw);
        break;
    }
    case RSPROT_OP_TINT:
        if( torirs_env_net_debug() )
            TORIRS_LOG("player_info: tint idx=%d cycles +%d..+%d hsl=%d,%d,%d weight=%d\n", idx,
                op->tint.start_cycle_delta, op->tint.end_cycle_delta, (int8_t)op->tint.hue,
                (int8_t)op->tint.saturation, (int8_t)op->tint.lightness, (int8_t)op->tint.weight);
        World_PlayerSetTint(
            world, idx, op->tint.start_cycle_delta, op->tint.end_cycle_delta, (int8_t)op->tint.hue,
            (int8_t)op->tint.saturation, (int8_t)op->tint.lightness, (int8_t)op->tint.weight);
        break;
    case RSPROT_OP_FACE_ANGLE:
        player_note_op(self, idx, op, 0, 0, 0, 0);
        if( op->face_angle.modern )
            World_PlayerBeginModernFacing(world, idx, op->face_angle.movement_mode);
        World_PlayerFaceAngle(world, idx, op->face_angle.angle, op->face_angle.instant);
        break;
    case RSPROT_OP_NAME_EXTRAS:
    {
        /* deob Statics.method3160: before the name, after it, after the
         * combat level -- the minimenu row prints them (Statics ~29047). */
        struct WorldEntity_Player* player = World_EntityPoolGet(&world->entities.player, idx);
        char const* parts[3] = { op->name_extras.before_name, op->name_extras.after_name,
                                 op->name_extras.after_combat_level };

        for( int i = 0; player && i < 3; i++ )
            snprintf(player->name_extras[i], sizeof(player->name_extras[i]), "%s", parts[i] ? parts[i] : "");
        break;
    }
    case RSPROT_OP_RESET:
        /* Applied at the start of its record (player_reset). */
        break;
    case RSPROT_OP_FREEZE:
        /* deob class95 ~650: 65535 is none; the byte != 0 is the reset. */
        World_PlayerFreeze(
            world, idx, op->freeze.delay == 65535 ? -1 : (int)op->freeze.delay,
            op->freeze.duration == 65535 ? -1 : (int)op->freeze.duration, op->freeze.cancel != 0);
        break;
    case RSPROT_OP_TRANSPARENCY:
        player_note_op(self, idx, op, 0, 0, 0, 0);
        World_PlayerSetTransparency(
            world, idx, (int16_t)op->transparency.start_cycle_delta,
            (int16_t)op->transparency.end_cycle_delta, (uint8_t)op->transparency.from,
            (uint8_t)op->transparency.to, op->transparency.use_start);
        break;
    default:
        break;
    }
}

/* Does op `kind` end the current player record (the ops after it address
 * another player, or none)? */
static int
player_op_is_target(int kind)
{
    switch( kind )
    {
    case RSPROT_OP_SET_LOCAL_PLAYER:
    case RSPROT_OP_ADD_OLD:
    case RSPROT_OP_ADD_NEW:
    case RSPROT_OP_SET_EXT:
    case RSPROT_OP_CLEAR_OLD:
    case RSPROT_OP_REMOVE_PID:
    case RSPROT_OP_COUNT_RESET:
    case RSPROT_OP_INFO:
        return 1;
    default:
        return 0;
    }
}

/* Does the record starting at `from` carry a RESET block? */
static int
player_record_resets(
    struct RsprotInfoOp const* ops,
    int from,
    int count)
{
    for( int i = from; i < count && !player_op_is_target(ops[i].kind); i++ )
        if( ops[i].kind == RSPROT_OP_RESET )
            return 1;
    return 0;
}

/*
 * The RESET block (PLAYER_INFO 0x2 at rev 239, `player_reset`): the actor
 * reset, deob class95.method3110, which the reference runs BEFORE any other
 * block of the record (class109.method3804 tests 0x2 first and reads the
 * block's byte later, in wire order). The value itself is read and dropped.
 * What it clears, and where this client holds it:
 *
 *   field1353 = null, instance field1467 = null   transparency: `transparency`
 *                                                 unset (opaque)
 *   field1351 / 1352 = -1, field1340.method5133,  tint: `tint`, cycles -1 and
 *   Statics.method3526(instance, -1, -1, ..)      weight 0 (never active)
 *   field1349.method873, instance method3538      the facing block's state
 *                                                 (class44): `facing` back to
 *                                                 no target, mode 0
 *   field1344 = walk, instance field1530[0]       the persistent traversal
 *                                                 (the decoder's,
 *                                                 rsprot_info_reset) and the
 *                                                 newest route step's
 *                                                 (`route_step[0]`)
 *   Arrays.fill(field1355, ""), field553          the name decorations
 *   Statics.method7179(instance, -1, 0)           the sequence
 *   field1345 = null                              the stored appearance BLOB
 *                                                 the reference keeps to
 *                                                 build a later instance;
 *                                                 this client keeps only the
 *                                                 decoded appearance on the
 *                                                 entity, which the reference
 *                                                 does not clear either: no
 *                                                 such state.
 */
static void
player_reset(
    struct RS_EntityApplyIn const* in,
    int idx)
{
    struct WorldEntity_Player* player = World_EntityPoolGet(&in->world->entities.player, idx);

    if( !player )
        return;
    for( int i = 0; i < 3; i++ )
        player->name_extras[i][0] = '\0';
    player->transparency = (struct WorldEntityTransparency){ 0 };
    player->tint = (struct WorldEntityTint){ .start_cycle = -1, .end_cycle = -1 };
    World_PlayerBeginModernFacing(in->world, idx, 0);
    player->pathing.route_step[0] = WORLD_PATHSTEP_WALK;
    World_PlayerSetPrimaryAnimation(in->world, idx, -1, 0);
}

static void
apply_in_check(struct RS_EntityApplyIn const* in)
{
    assert(in);
    assert(in->world);
    assert(in->esync);
    assert(in->rev);
    assert(in->host);
    assert(in->host->npc_resolve);
    assert(in->host->npc_type);
    assert(in->host->hitsplat);
    assert(in->host->headbar_persist);
    assert(in->host->home_view);
    assert(in->host->npc_origin);
    assert(in->host->player_spawn);
    assert(in->host->npc_spawn);
    assert(in->host->npc_retype);
    assert(in->host->present);
    (void)in;
}

void
RS_EntityApply_PlayerInfo(
    struct RS_EntityApplyIn const* in,
    struct RsprotInfoOp const* ops,
    int count)
{
    struct PlayerApply self;

    apply_in_check(in);
    if( count == 0 )
        return;
    assert(ops);
    assert(!g_scratch.busy);
    g_scratch.busy = 1;

    memset(&self, 0, sizeof(self));
    self.in = in;
    /* Snapshot the tracked list: ADD_OLD indexes the PREVIOUS packet's order
     * while the walk rebuilds the live list in place. */
    self.old_count = in->esync->active_player_count;
    memcpy(g_scratch.old_list, in->esync->active_players,
        (size_t)self.old_count * sizeof(g_scratch.old_list[0]));
    in->esync->active_player_count = 0;
    self.cur_pid = -1;

    for( int i = 0; i < count; i++ )
    {
        int consumed = player_target_op(&self, &ops[i]);

        /* A new player exists at once, drawing nothing until its
         * appearance's body is resident. */
        if( self.need_ensure )
            player_ensure_now(&self);
        /* A record that resets the actor resets it before its other blocks. */
        if( consumed && self.cur_pid >= 0 && player_op_is_target(ops[i].kind) &&
            player_record_resets(ops, i + 1, count) )
        {
            int idx = player_target(&self);
            if( idx >= 0 )
                player_reset(in, idx);
        }
        if( !consumed )
            player_apply_op(&self, &ops[i]);
    }
    g_scratch.busy = 0;
}

/* ------------------------------------------------------------------ */
/* NPC_INFO                                                            */
/* ------------------------------------------------------------------ */

struct NpcApply
{
    struct RS_EntityApplyIn const* in;
    /* The world this packet describes: SET_ACTIVE_WORLD's view (deob:
     * NPC_INFO applies to `activeWorld`, each World holding its own npcs).
     * 0 is the root. `list` is that world's positional npc list. */
    int view;
    int* list;
    int* count;
    int cap;
    int old_count;

    int cur_slot;
    /* Slots this packet removed, held until its end (g_scratch.cleared): an
     * add of the same slot in the same packet is the npc MOVED, not a new one
     * (the deob keeps the Npc in npcs[] across the removal and its add finds
     * it there: RSProx's `teleport` / `telejump`). */
    int cleared_count;
    /* The current slot was one of them: its add record moves the npc it has
     * (the jump bit decides walk or snap) and its spawn facing is not applied
     * (deob: only `isNew` takes the angle). */
    int cur_revived;
    /* The slot this packet just added an npc in, until its add record ends:
     * announced then, once the record's ops have placed it. -1: none. */
    int add_slot;
    /* The wire type: the per-player multiNpc wrapper. */
    int pending_base_type;
};

static int
npc_target(
    struct NpcApply* self,
    int* out_element_id)
{
    int world_idx = -1;
    int element_id = -1;

    if( self->cur_slot >= 0 )
        RS_EntitySync_FindNpc(self->in->esync, self->cur_slot, &world_idx, &element_id);
    if( out_element_id )
        *out_element_id = element_id;
    return world_idx;
}

static void
npc_origin(
    struct NpcApply* self,
    int* out_x,
    int* out_z,
    int* out_level)
{
    self->in->host->npc_origin(self->in->host->ctx, self->view, out_x, out_z, out_level);
}

/* Remove the npc in `slot`, and whatever the client keeps for it. */
static void
npc_remove(
    struct NpcApply* self,
    int slot)
{
    struct RS_EntityNote n = { .kind = RS_ENTITY_NOTE_NPC_GONE, .idx = -1, .slot = slot, .pid = -1 };

    note(self->in, &n);
    RS_EntitySync_RemoveNpc(self->in->esync, self->in->world, slot);
}

/* The npc this packet added has its whole add record applied: announce it,
 * and run the cache's NPC_ADD script, once the record's ops placed it
 * (FAULTS E1, E2). */
static void
npc_finish_add(struct NpcApply* self)
{
    int world_idx = -1;

    if( self->add_slot < 0 )
        return;
    if( RS_EntitySync_FindNpc(self->in->esync, self->add_slot, &world_idx, NULL) && world_idx >= 0 )
    {
        struct RS_EntityNote n = { .kind = RS_ENTITY_NOTE_NPC_ADDED, .idx = world_idx,
                                   .slot = self->add_slot, .pid = -1 };
        note(self->in, &n);
    }
    self->add_slot = -1;
}

/* Does op `kind` change which npc the following ops address (the end of the
 * current record)? */
static int
npc_op_is_target(int kind)
{
    switch( kind )
    {
    case RSPROT_OP_ADD_OLD:
    case RSPROT_OP_ADD_NEW:
    case RSPROT_OP_SET_EXT:
    case RSPROT_OP_CLEAR_OLD:
    case RSPROT_OP_COUNT_RESET:
    case RSPROT_OP_INFO:
        return 1;
    default:
        return 0;
    }
}

/* Target/list bookkeeping. Returns 1 when the op was fully handled. */
static int
npc_target_op(
    struct NpcApply* self,
    struct RsprotInfoOp const* op)
{
    struct RS_EntitySync* esync = self->in->esync;
    struct World* world = self->in->world;

    switch( op->kind )
    {
    /*
     * The rebuilt list is POSITIONAL, and the decoder is what defines the
     * positions. Extended info is addressed by the npc's index in the list
     * the two sides rebuild in lockstep this tick (SET_EXT resolves
     * `list[list_idx]`), and the decoder advances that index for EVERY npc
     * it keeps or adds, whether or not this side can resolve the slot behind
     * it. So the append is unconditional too: an unresolvable position is
     * recorded as -1, which the ops treat as "no target", so the failure is
     * one dropped update instead of a whole packet of misdirected ones.
     */
    case RSPROT_OP_ADD_OLD:
        self->cur_slot = (int)op->value < self->old_count ? g_scratch.old_list[op->value] : -1;
        self->cur_revived = 0;
        if( *self->count < self->cap )
            self->list[(*self->count)++] = self->cur_slot;
        break;
    /*
     * ENTERING VIEW: the server is telling us this slot is a NEW npc. The
     * slot is the server's private per-observer name and it is REUSED, so a
     * release the client never saw a matching CLEAR for leaves a stale
     * registration behind (calling a familiar once moved the Queen Black
     * Dragon onto the player through one). "Entering view" is unambiguous
     * about intent: whatever this side still has under that name is stale and
     * is despawned before the new npc takes it -- unless this packet removed
     * that slot itself, which makes the add a move (cur_revived).
     */
    case RSPROT_OP_ADD_NEW:
    {
        int stale_world_idx = -1;
        int stale_element_id = -1;

        self->cur_slot = (int)op->value;
        self->cur_revived = 0;
        for( int i = 0; i < self->cleared_count; i++ )
        {
            if( g_scratch.cleared[i] == self->cur_slot )
            {
                g_scratch.cleared[i] = g_scratch.cleared[--self->cleared_count];
                self->cur_revived = 1;
                break;
            }
        }
        if( self->cur_revived )
        {
            npc_trace(world, -1, self->cur_slot, -1, -1, -1, "READDED_SAME_PACKET", 1);
        }
        else if( self->cur_slot >= 0 &&
                 RS_EntitySync_FindNpc(esync, self->cur_slot, &stale_world_idx, &stale_element_id) )
        {
            TORIRS_LOG("entity_sync: npc slot %d entered view but is still registered "
                       "(world %d, element %d) - despawning the stale entity first\n",
                self->cur_slot, stale_world_idx, stale_element_id);
            npc_trace(world, npc_trace_type_of(world, stale_world_idx), self->cur_slot, -1, stale_world_idx,
                stale_element_id, "STALE_SLOT_REUSED", 1);
            npc_remove(self, self->cur_slot);
        }
        if( *self->count < self->cap )
            self->list[(*self->count)++] = self->cur_slot;
        break;
    }
    case RSPROT_OP_SET_EXT:
        self->cur_slot = (int)op->value < *self->count ? self->list[op->value] : -1;
        self->cur_revived = 0;
        break;
    case RSPROT_OP_CLEAR_OLD:
    {
        int slot = (int)op->value < self->old_count ? g_scratch.old_list[op->value] : -1;
        if( slot >= 0 )
        {
            int w = -1, e = -1;
            RS_EntitySync_FindNpc(esync, slot, &w, &e);
            npc_trace(world, npc_trace_type_of(world, w), slot, (int)op->value, w, e, "REMOVE", 1);
            /* Removed at the packet's end unless this packet adds it again. */
            if( self->cleared_count < RS_ENTITY_SYNC_MAX_NPCS )
                g_scratch.cleared[self->cleared_count++] = slot;
            else
                npc_remove(self, slot);
        }
        self->cur_slot = -1;
        return 1;
    }
    case RSPROT_OP_COUNT_RESET:
    {
        /* When the wire 8-bit count is less than the previous tracked length,
         * indices [count, oldCount) are removed. A plane change zeros the
         * tracked count and sends count=0: without this, old-floor npcs linger
         * in the world pool (drawn on minusedlevel, rejected by pick). */
        int keep = (int)op->value;
        for( int i = keep; i < self->old_count; i++ )
        {
            int w = -1, e = -1;
            RS_EntitySync_FindNpc(esync, g_scratch.old_list[i], &w, &e);
            npc_trace(world, npc_trace_type_of(world, w), g_scratch.old_list[i], i, w, e,
                "REMOVE_COUNT_SHRINK", keep);
            npc_remove(self, g_scratch.old_list[i]);
        }
        *self->count = 0;
        return 1;
    }
    case RSPROT_OP_INFO:
        return 1;
    default:
        return 0;
    }

    {
        int element_id;
        int const world_idx = npc_target(self, &element_id);
        npc_trace(world, npc_trace_type_of(world, world_idx), self->cur_slot, (int)op->value, world_idx,
            element_id, "target_op", (int)op->kind);
    }
    return 1;
}

/* A new npc in the current slot, of the pending wire type as this client's
 * varps resolve it. A hidden rung still makes a live entity (later masks and
 * a varp-driven reappearance need it): the model-less wrapper. */
static void
npc_spawn_now(struct NpcApply* self)
{
    struct RS_EntityApplyIn const* in = self->in;
    int live = in->host->npc_resolve(in->host->ctx, self->pending_base_type);
    int hidden = live < 0;
    int tile_x, tile_z, level;
    int idx;

    if( hidden )
        live = self->pending_base_type;
    npc_origin(self, &tile_x, &tile_z, &level);
    idx = in->host->npc_spawn(in->host->ctx, live, self->pending_base_type, tile_x, tile_z, level);
    if( idx < 0 )
    {
        /* The one failure with no retry: the slot stays in the tracked list
         * and every later op on it is dropped. */
        npc_trace(in->world, live, self->cur_slot, -1, -1, -1, "SPAWN_FAILED", 1);
        return;
    }
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(&in->world->entities.npc, idx);
        int element_id = -1;

        if( npc )
        {
            npc->base_npc_id = self->pending_base_type;
            npc->multinpc_hidden = hidden != 0;
            /* A deck stream's npc stands in that view's own tiles, as a
             * rider does (PLAYER_INFO's staging homing). */
            npc->view_placement.home_view = self->view;
            npc->server_slot = self->cur_slot;
            element_id = npc->element_id;
            RS_EntitySync_RegisterNpc(in->esync, self->cur_slot, element_id, idx);
            {
                struct RS_EntityNote n = { .kind = RS_ENTITY_NOTE_NPC_SPAWNED, .idx = idx,
                                           .slot = self->cur_slot, .pid = -1 };
                note(in, &n);
            }
            /* Announced when its add record ends (npc_finish_add). */
            self->add_slot = self->cur_slot;
        }
        npc_trace(in->world, live, self->cur_slot, -1, idx, element_id, "SPAWNED", 1);
    }
    entity_debug_log("entity_sync: npc %d spawned (world idx %d)\n", self->cur_slot, idx);
}

/* The current npc takes the pending wire type: a server type change, or an
 * add over a slot this packet removed whose type differs. */
static void
npc_retype_now(struct NpcApply* self)
{
    struct RS_EntityApplyIn const* in = self->in;
    int live = in->host->npc_resolve(in->host->ctx, self->pending_base_type);
    int hidden = live < 0;
    int element_id;
    int idx = npc_target(self, &element_id);
    struct WorldEntity_NPC* npc;

    assert(idx >= 0);
    if( hidden )
        live = self->pending_base_type;
    npc = World_EntityPoolGet(&in->world->entities.npc, idx);
    if( npc )
    {
        npc->base_npc_id = self->pending_base_type;
        npc->multinpc_hidden = hidden != 0;
        /* A server type change: the type's name, level and stances again. */
        npc->overrides = 0;
    }
    in->host->npc_retype(in->host->ctx, idx, element_id, live, self->pending_base_type);
}

/*
 * BODY_CUSTOM / HEAD_CUSTOM (deob Statics ~47300-47520 and ~47775-47855):
 * 0x1 clears the override (class86.method2907 / Statics.method2951); any
 * other block is a whole new one (a new class399), so what an earlier block
 * set and this one does not send goes back to the type's. Models only with
 * 0x2 -- without it the type's own models carry this block's recolours and
 * retextures. The mirror is the typed forms' byte (== 1) or the counted
 * forms' 0x10 itself; the player composition is the counted forms' 0x20.
 *
 * Every model the block sent, however many (FAULTS E12). A count with no list
 * is the op list's byte arena having overflowed (rsprot_info_ints): the
 * values were never kept, so the npc keeps the override it has, and says so.
 */
static void
npc_apply_custom(
    struct World* world,
    int idx,
    int head,
    struct RsprotInfoBodyCustom const* b)
{
    struct WorldNpcOverrideIn in;
    bool models_sent = (b->flag & 0x2) != 0;

    if( b->reset )
    {
        if( head )
            World_NpcSetHeadCustom(world, idx, NULL);
        else
            World_NpcSetBodyCustom(world, idx, NULL);
        return;
    }
    if( (models_sent && b->model_count > 0 && !b->models) || (b->recolour_count > 0 && !b->recolours) ||
        (b->retexture_count > 0 && !b->retextures) || (b->identkit_count > 0 && !b->identkits) )
    {
        TORIRS_ERR(
            "npc_info: npc %d %s customisation of %d models, %d recolours, %d retextures lost: the op "
            "list overflowed\n",
            idx,
            head ? "chathead" : "body",
            (int)b->model_count,
            (int)b->recolour_count,
            (int)b->retexture_count);
        return;
    }
    in = (struct WorldNpcOverrideIn){
        .models_sent = models_sent,
        .model_count = models_sent ? b->model_count : 0,
        .models = models_sent ? b->models : NULL,
        .recolour_count = b->recolour_count,
        .recolours = b->recolours,
        .retexture_count = b->retexture_count,
        .retextures = b->retextures,
        .mirror = b->mirror >= 0 ? b->mirror == 1 : (b->flag & 0x10) != 0,
        .player = (b->flag & 0x20) != 0 && b->body_type >= 0,
        .body_type = b->body_type,
        .identkit_count = b->identkit_count,
        .identkits = b->identkits,
    };
    if( head )
        World_NpcSetHeadCustom(world, idx, &in);
    else
        World_NpcSetBodyCustom(world, idx, &in);
}

static void
npc_apply_op(
    struct NpcApply* self,
    struct RsprotInfoOp const* op)
{
    struct RS_EntityApplyIn const* in = self->in;
    struct World* world = in->world;
    int idx = npc_target(self, NULL);

    switch( op->kind )
    {
    case RSPROT_OP_NPCTYPE:
        /* The wire wrapper is kept on the entity so a later local varp change
         * can select a different child without another add. */
        self->pending_base_type = (int)op->value;
        if( self->cur_slot >= 0 && idx < 0 )
        {
            npc_spawn_now(self);
            break;
        }
        /* Re-added under another type: the npc it has takes the new one. */
        if( self->cur_revived && idx >= 0 )
        {
            struct WorldEntity_NPC const* npc = World_EntityPoolAt(&world->entities.npc, idx);

            if( npc->base_npc_id != (int)op->value )
                npc_retype_now(self);
        }
        break;
    case RSPROT_OP_WALKDIR:
        if( idx >= 0 )
        {
            npc_trace(world, npc_trace_type_of(world, idx), self->cur_slot, -1, idx, -1, "MOVE_WALK",
                (int)op->value);
            World_NpcPathPushStep(world, idx, WORLD_PATHSTEP_WALK, (int)op->value);
        }
        break;
    case RSPROT_OP_RUNDIR:
        if( idx >= 0 )
        {
            npc_trace(world, npc_trace_type_of(world, idx), self->cur_slot, -1, idx, -1, "MOVE_RUN",
                (int)op->value);
            World_NpcPathPushStep(world, idx, WORLD_PATHSTEP_RUN, (int)op->value);
        }
        break;
    case RSPROT_OP_CRAWLDIR:
        if( idx >= 0 )
        {
            npc_trace(world, npc_trace_type_of(world, idx), self->cur_slot, -1, idx, -1, "MOVE_CRAWL",
                (int)op->value);
            World_NpcPathPushStep(world, idx, WORLD_PATHSTEP_CRAWL, (int)op->value);
        }
        break;
    case RSPROT_OP_DELTA_XZ:
        if( idx >= 0 )
        {
            int lx, lz, llevel;
            npc_origin(self, &lx, &lz, &llevel);
            /* The op that teleports an npc: the destination is the origin
             * plus a small delta, so a misdirected one lands the wrong
             * creature next to the player. Traced with the slot and the
             * destination. */
            npc_trace(world, npc_trace_type_of(world, idx), self->cur_slot, -1, idx,
                (lx + op->delta_xz.dx) * 1000 + (lz + op->delta_xz.dz), "MOVE_JUMP_TO_TILE",
                op->delta_xz.jump);
            /* TORIRS_SCRIPTRUN_NPC_MOVE_TRACE=1: every add record's placement
             * with the jump bit (`[teleport]` is 0, `[telejump]` and a fresh
             * add 1). */
            if( getenv("TORIRS_SCRIPTRUN_NPC_MOVE_TRACE") )
                fprintf(stderr, "scriptrun_npc_move: slot=%d tile=%d,%d jump=%d\n", self->cur_slot,
                    lx + op->delta_xz.dx, lz + op->delta_xz.dz, op->delta_xz.jump ? 1 : 0);
            World_NpcPathJump(world, idx, op->delta_xz.jump, lx + op->delta_xz.dx, lz + op->delta_xz.dz);
        }
        break;
    case RSPROT_OP_SEQUENCE:
        if( getenv("TORIRS_ANIM_DEBUG") )
            TORIRS_LOG("anim: npc SEQUENCE op seq=%d delay=%d slot=%d world_idx=%d%s\n",
                op->sequence.sequence_id, op->sequence.delay, self->cur_slot, idx,
                idx >= 0 ? "" : "  <-- NO TARGET, dropped");
        if( idx >= 0 )
        {
            npc_trace(world, npc_trace_type_of(world, idx), self->cur_slot, op->sequence.delay, idx, -1,
                "SEQUENCE", op->sequence.sequence_id);
            World_NpcSetPrimaryAnimation(world, idx, op->sequence.sequence_id, op->sequence.delay);
            {
                struct WorldEntity_NPC const* npc = World_EntityPoolGet(&world->entities.npc, idx);
                struct RS_EntityNote n = { .kind = RS_ENTITY_NOTE_NPC_SEQ, .idx = idx,
                                           .slot = npc ? npc->server_slot : -1, .pid = -1,
                                           .seq = op->sequence.sequence_id, .delay = op->sequence.delay };
                note(in, &n);
            }
        }
        break;
    case RSPROT_OP_FACE_ENTITY:
        if( idx >= 0 )
        {
            int entity_id = op->face_entity.entity_id == 65535 ? -1 : op->face_entity.entity_id;
            /* list_idx carries the modern movement mode (-1 on a classic
             * block): whether the npc keeps facing the target while it walks. */
            npc_trace(world, npc_trace_type_of(world, idx), self->cur_slot,
                op->face_entity.modern ? (int)op->face_entity.movement_mode : -1, idx, -1, "FACE_ENTITY",
                entity_id);
            if( op->face_entity.modern )
                World_NpcBeginModernFacing(world, idx, op->face_entity.movement_mode);
            /* A world entity has no registry here: the gamepack's
             * missing-target path turns to the fallback angle. */
            if( op->face_entity.target_type == RSPROT_FACE_TARGET_WORLD_ENTITY && op->face_entity.modern )
            {
                World_NpcFaceAngle(world, idx, op->face_entity.fallback_angle, op->face_entity.instant);
                break;
            }
            World_NpcFaceEntityDetailed(
                world, idx, entity_id,
                op->face_entity.has_fallback_angle ? op->face_entity.fallback_angle : -1,
                op->face_entity.has_fallback_angle && op->face_entity.instant);
            entity_debug_log("entity_sync: npc %d faces entity %d\n", self->cur_slot, entity_id);
        }
        break;
    case RSPROT_OP_FACE_COORD:
        /* Raw wire half-tiles -- see the player branch. */
        if( idx >= 0 )
        {
            struct WorldEntity_NPC* npc;

            if( op->face_coord.modern )
                World_NpcBeginModernFacing(world, idx, op->face_coord.movement_mode);
            World_NpcFaceCoord(world, idx, op->face_coord.x, op->face_coord.z);
            npc = World_EntityPoolAt(&world->entities.npc, idx);
            /* `instant` is as sent: it means nothing without a target. */
            if( op->face_coord.instant && (op->face_coord.x != 0 || op->face_coord.z != 0) )
                npc->facing.instant = true;
            /* What the server SENT, kept past the turn that consumes
             * facing.square_x/z (WorldEntity_NPC.face_sent_x). */
            npc->face_sent_x = op->face_coord.x;
            npc->face_sent_z = op->face_coord.z;
            npc->face_sent_cycle = (int)world->cycle;
            {
                struct RS_EntityNote n = { .kind = RS_ENTITY_NOTE_NPC_FACE, .idx = idx,
                                           .slot = npc->server_slot, .pid = -1, .x = op->face_coord.x,
                                           .z = op->face_coord.z };
                note(in, &n);
            }
            entity_debug_log("entity_sync: npc faces coord %d,%d\n", op->face_coord.x, op->face_coord.z);
        }
        break;
    case RSPROT_OP_SAY:
        /* NPC forced chat: plain yellow overhead (colour/effect 0). */
        if( idx >= 0 && op->say.text )
            World_NpcSetChat(world, idx, op->say.text, 0, 0);
        break;
    case RSPROT_OP_DAMAGE:
        if( idx >= 0 )
            apply_damage(in, idx, 0, &op->damage);
        break;
    case RSPROT_OP_HEADBAR:
        /* The npc draws one bar: a block's first. */
        if( idx >= 0 && op->headbar.index == 0 )
        {
            if( op->headbar.remove )
                World_NpcClearHealthbar(world, idx);
            else
                World_NpcSetHealthbar(world, idx, headbar_from_block(in, &op->headbar));
        }
        break;
    case RSPROT_OP_CHANGE_TYPE:
        /* A server transformation replaces the wrapper itself. The selected
         * child remains client-local and may differ between observers. */
        self->pending_base_type = op->change_type.npc_type;
        if( idx >= 0 )
            npc_retype_now(self);
        break;
    case RSPROT_OP_SPOTANIM:
        if( idx >= 0 )
        {
            World_NpcSetSpotanim(
                world, idx, op->spotanim.slot, op->spotanim.spotanim_id, op->spotanim.height_delay >> 16,
                op->spotanim.height_delay & 0xffff, op->spotanim.loop);
            if( op->spotanim.spotanim_id >= 0 )
            {
                struct RS_EntityNote n = { .kind = RS_ENTITY_NOTE_SPOTANIM, .idx = idx,
                                           .slot = self->cur_slot, .pid = -1,
                                           .seq = op->spotanim.spotanim_id };
                note(in, &n);
            }
        }
        break;
    case RSPROT_OP_EXACT_MOVE:
        if( idx >= 0 )
        {
            struct WorldEntity_NPC* npc = World_EntityPoolAt(&world->entities.npc, idx);
            int sx = op->exact_move.start_x;
            int sz = op->exact_move.start_z;
            int ex = op->exact_move.end_x;
            int ez = op->exact_move.end_z;

            if( op->exact_move.relative )
            {
                sx += npc->pathing.route_x[0];
                sz += npc->pathing.route_z[0];
                ex += npc->pathing.route_x[0];
                ez += npc->pathing.route_z[0];
            }
            World_NpcSetExactMoveDetailed(
                world, idx, sx, sz, ex, ez, op->exact_move.start_cycle_delta,
                op->exact_move.end_cycle_delta, op->exact_move.facing, op->exact_move.facing_is_yaw);
        }
        break;
    case RSPROT_OP_FACE_ANGLE:
        /* An add that moved an npc this packet removed takes no spawn facing
         * (deob: only `isNew` does). */
        if( idx >= 0 && !(op->face_angle.spawn && self->cur_revived) )
        {
            struct WorldEntity_NPC* npc = World_EntityPoolAt(&world->entities.npc, idx);
            int angle;

            if( op->face_angle.modern )
                World_NpcBeginModernFacing(world, idx, op->face_angle.movement_mode);
            angle = op->face_angle.spawn && npc->facing.turn_speed == 0 ? 0 : op->face_angle.angle;
            World_NpcFaceAngle(world, idx, angle, op->face_angle.instant);
        }
        break;
    case RSPROT_OP_SPAWN_CYCLE:
        if( idx >= 0 )
            ((struct WorldEntity_NPC*)World_EntityPoolAt(&world->entities.npc, idx))->spawn_cycle =
                (uint32_t)op->value;
        break;
    case RSPROT_OP_VISIBLE_OPS:
        if( idx >= 0 )
            ((struct WorldEntity_NPC*)World_EntityPoolAt(&world->entities.npc, idx))->visible_ops =
                (uint8_t)op->value;
        break;
    case RSPROT_OP_NAME_CHANGE:
        if( idx >= 0 && op->name_change.name )
        {
            struct WorldEntity_NPC* npc = World_EntityPoolAt(&world->entities.npc, idx);
            snprintf(npc->name, sizeof(npc->name), "%s", op->name_change.name);
            npc->overrides |= WORLD_NPC_OVERRIDE_NAME;
        }
        break;
    case RSPROT_OP_LEVEL_CHANGE:
        if( idx >= 0 )
        {
            struct WorldEntity_NPC* npc = World_EntityPoolAt(&world->entities.npc, idx);
            npc->combat_level = (int)(uint32_t)op->value;
            npc->overrides |= WORLD_NPC_OVERRIDE_LEVEL;
        }
        break;
    case RSPROT_OP_HEADICON_CUSTOM:
        if( idx >= 0 )
        {
            int group[WORLD_NPC_HEADICON_SLOTS];
            int index[WORLD_NPC_HEADICON_SLOTS];

            for( int i = 0; i < WORLD_NPC_HEADICON_SLOTS; i++ )
            {
                group[i] = op->headicon.group[i];
                index[i] = op->headicon.index[i];
            }
            World_NpcSetHeadiconCustom(world, idx, op->headicon.mask != 0, group, index);
        }
        break;
    case RSPROT_OP_FREEZE:
        /* deob Statics ~47731: 65535 is none; the byte != 0 is the reset. */
        if( idx >= 0 )
            World_NpcFreeze(
                world, idx, op->freeze.delay == 65535 ? -1 : (int)op->freeze.delay,
                op->freeze.duration == 65535 ? -1 : (int)op->freeze.duration, op->freeze.cancel != 0);
        break;
    case RSPROT_OP_BODY_CUSTOM:
    case RSPROT_OP_HEAD_CUSTOM:
        if( idx >= 0 )
            npc_apply_custom(world, idx, op->kind == RSPROT_OP_HEAD_CUSTOM, &op->body);
        break;
    case RSPROT_OP_TINT:
        if( idx >= 0 )
            World_NpcSetTint(
                world, idx, op->tint.start_cycle_delta, op->tint.end_cycle_delta, (int8_t)op->tint.hue,
                (int8_t)op->tint.saturation, (int8_t)op->tint.lightness, (int8_t)op->tint.weight);
        break;
    case RSPROT_OP_TRANSPARENCY:
        if( idx >= 0 )
            World_NpcSetTransparency(
                world, idx, (int16_t)op->transparency.start_cycle_delta,
                (int16_t)op->transparency.end_cycle_delta, (uint8_t)op->transparency.from,
                (uint8_t)op->transparency.to, op->transparency.use_start);
        break;
    case RSPROT_OP_BAS_CHANGE:
        if( idx >= 0 )
        {
            struct WorldEntity_NPC* npc = World_EntityPoolAt(&world->entities.npc, idx);
            uint32_t mask = op->bas_change.mask;
            struct RS_EntityNpcType type;

            /* The block replaces the whole set: a set bit's id, else the
             * type's own (deob Statics, NPC_INFO flag 0x400000, every field
             * `(mask & bit) == 0 ? npc.type.<anim> : read`). So an empty mask
             * is the reset -- the Archive Guardian's `[bas]` as he lifts. A
             * type whose config is not known keeps what it had. */
            if( in->host->npc_type(in->host->ctx, npc->npc_id, npc->base_npc_id, &type) )
                npc->idle_animations = type.idle;
            /* An empty mask is the reset to the type's own: no override. */
            if( mask )
                npc->overrides |= WORLD_NPC_OVERRIDE_STANCES;
            else
                npc->overrides &= (uint8_t)~WORLD_NPC_OVERRIDE_STANCES;
            if( mask & (1u << 0) ) npc->idle_animations.turnanim = op->bas_change.turnanim;
            if( mask & (1u << 1) ) npc->idle_animations.turnanim_r = op->bas_change.turnanim_r;
            if( mask & (1u << 2) ) npc->idle_animations.walkanim = op->bas_change.walkanim;
            if( mask & (1u << 3) ) npc->idle_animations.walkanim_b = op->bas_change.walkanim_b;
            if( mask & (1u << 4) ) npc->idle_animations.walkanim_l = op->bas_change.walkanim_l;
            if( mask & (1u << 5) ) npc->idle_animations.walkanim_r = op->bas_change.walkanim_r;
            if( mask & (1u << 6) ) npc->idle_animations.runanim = op->bas_change.runanim;
            if( mask & (1u << 7) ) npc->idle_animations.runanim_b = op->bas_change.runanim_b;
            if( mask & (1u << 8) ) npc->idle_animations.runanim_l = op->bas_change.runanim_l;
            if( mask & (1u << 9) ) npc->idle_animations.runanim_r = op->bas_change.runanim_r;
            if( mask & (1u << 10) ) npc->idle_animations.crawlanim = op->bas_change.crawlanim;
            if( mask & (1u << 11) ) npc->idle_animations.crawlanim_b = op->bas_change.crawlanim_b;
            if( mask & (1u << 12) ) npc->idle_animations.crawlanim_l = op->bas_change.crawlanim_l;
            if( mask & (1u << 13) ) npc->idle_animations.crawlanim_r = op->bas_change.crawlanim_r;
            if( mask & (1u << 14) ) npc->idle_animations.readyanim = op->bas_change.readyanim;
        }
        break;
    default:
        break;
    }
}

void
RS_EntityApply_NpcInfo(
    struct RS_EntityApplyIn const* in,
    int view,
    struct RsprotInfoOp const* ops,
    int count)
{
    struct NpcApply self;

    apply_in_check(in);
    assert(view >= 0);
    if( count > 0 )
        assert(ops);
    assert(!g_scratch.busy);
    g_scratch.busy = 1;

    npc_trace_state(in->world);
    memset(&self, 0, sizeof(self));
    self.in = in;
    self.view = view;
    RS_EntitySync_NpcList(in->esync, view, &self.list, &self.count, &self.cap);
    self.old_count = *self.count;
    memcpy(g_scratch.old_list, self.list, (size_t)self.old_count * sizeof(g_scratch.old_list[0]));
    *self.count = 0;
    self.cur_slot = -1;
    self.add_slot = -1;
    self.cleared_count = 0;
    self.cur_revived = 0;
    self.pending_base_type = -1;

    for( int i = 0; i < count; i++ )
    {
        if( npc_op_is_target(ops[i].kind) )
            npc_finish_add(&self);
        if( !npc_target_op(&self, &ops[i]) )
            npc_apply_op(&self, &ops[i]);
    }

    /* The last record's add, if it was one. */
    npc_finish_add(&self);

    /* Removed and not added back: gone. */
    for( int i = 0; i < self.cleared_count; i++ )
        npc_remove(&self, g_scratch.cleared[i]);
    g_scratch.busy = 0;
}

int
RS_EntityApply_NpcOldType(
    struct World* world,
    struct RS_EntitySync* esync,
    int view,
    int old)
{
    int* list;
    int* count;
    int cap;
    int world_idx = -1;
    int element_id = -1;
    struct WorldEntity_NPC* npc;

    assert(world);
    assert(esync);
    assert(view >= 0);
    assert(old >= 0);
    RS_EntitySync_NpcList(esync, view, &list, &count, &cap);
    (void)cap;
    if( old >= *count || list[old] < 0 )
        return -1;
    if( !RS_EntitySync_FindNpc(esync, list[old], &world_idx, &element_id) || world_idx < 0 )
        return -1;
    npc = World_EntityPoolGet(&world->entities.npc, world_idx);
    return npc ? npc->base_npc_id : -1;
}

int
RS_EntityApply_NpcTypes(
    struct RsprotInfoOp const* ops,
    int count,
    int* out,
    int cap)
{
    int n = 0;

    assert(out);
    (void)cap;
    if( count == 0 )
        return 0;
    assert(ops);
    for( int i = 0; i < count; i++ )
    {
        int type;
        int seen = 0;

        if( ops[i].kind == RSPROT_OP_NPCTYPE )
            type = (int)ops[i].value;
        else if( ops[i].kind == RSPROT_OP_CHANGE_TYPE )
            type = ops[i].change_type.npc_type;
        else
            continue;
        for( int j = 0; j < n && !seen; j++ )
            seen = out[j] == type;
        if( seen )
            continue;
        assert(n < cap);
        out[n++] = type;
    }
    return n;
}
