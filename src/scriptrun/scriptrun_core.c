#include "scriptrun_core.h"

#include "game/rs_entity_sync.h"
#include "net/rev/gameproto_parse.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rev/packets/pkt_npc_info.h"
#include "net/rev/packets/pkt_player_appearance.h"
#include "net/rev/packets/pkt_player_info.h"
#include "net/rev/revpacket.h"
#include "torirsserver/torirs_server.h"
#include "world/entity_npc.h"
#include "world/entity_player.h"
#include "world/world.h"
#include "net/rev/rsprot_bridge.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* game/task_exec_entity_info.c's constants, restated: the decoder's array
 * size and the local-player sentinel while no UPDATE_PID has arrived (rev 239
 * never sends one). */
enum
{
    CORE_ENTITY_OPS_MAX = 2048,
    CORE_LOCAL_SENTINEL = 2047,
};

/* net/rev/osrs239/osrs239_entity_info.c: the v5 player table, one per process.
 * Declared where they are used, as net/net.c declares them. */
void
osrs239_playerinfo_set_local(int local_index);
size_t
osrs239_playerinfo_state_size(void);
void
osrs239_playerinfo_state_save(void* out);
void
osrs239_playerinfo_state_load(void const* in);

/* world/world.h's hitmark defaults: the values the client falls back to when
 * the hitsplat config has no record (game/rs_hitsplat.c). */
#ifndef WORLD_HITMARK_DEFAULT_DURATION
#define WORLD_HITMARK_DEFAULT_DURATION 70
#endif

/* ------------------------------------------------------------------ setup */

struct ScriptrunCore*
ScriptrunCore_New(struct GameProtoRevTable const* rev)
{
    struct ScriptrunCore* core;

    assert(rev);
    core = calloc(1, sizeof(*core));
    assert(core);
    core->rev = rev;
    core->world = World_New();
    assert(core->world);
    core->esync = calloc(1, sizeof(*core->esync));
    assert(core->esync);
    RS_EntitySync_Init(core->esync);
    core->npc_ops = calloc(CORE_ENTITY_OPS_MAX, sizeof(struct PktNpcInfoOp));
    assert(core->npc_ops);
    core->player_ops = calloc(CORE_ENTITY_OPS_MAX, sizeof(struct PktPlayerInfoOp));
    assert(core->player_ops);
    for( int i = 0; i < SCRIPTRUN_INVS; i++ )
        core->invs[i].inv_id = -1;
    core->toplevel = -1;
    core->resume_pending = -1;
    core->next_element_id = 1;
    if( rev->player_info_read )
    {
        core->player_decoder_state = calloc(1, osrs239_playerinfo_state_size());
        assert(core->player_decoder_state);
    }
    core->run_energy = 10000;
    return core;
}

void
ScriptrunCore_Free(struct ScriptrunCore* core)
{
    if( !core )
        return;
    ScriptrunCore_FreeCollision(core);
    RS_EntitySync_Free(core->esync);
    free(core->esync);
    World_Free(core->world);
    free(core->npc_ops);
    free(core->player_ops);
    free(core->player_decoder_state);
    free(core);
}

void
ScriptrunCore_SetLocalIndex(
    struct ScriptrunCore* core,
    int index)
{
    assert(core);
    assert(index >= 0);
    core->esync->local_pid = index;
    if( core->player_decoder_state )
    {
        osrs239_playerinfo_set_local(index);
        osrs239_playerinfo_state_save(core->player_decoder_state);
    }
}

void
ScriptrunCore_Event(
    struct ScriptrunCore* core,
    int kind,
    int a,
    int b,
    int c,
    int d)
{
    struct ScriptrunEvent* ev;

    assert(core);
    assert(kind > SCRIPTRUN_EV_NONE && kind < SCRIPTRUN_EV_KIND_COUNT);
    core->event_serial++;
    ev = &core->events[core->event_serial % SCRIPTRUN_EVENTS];
    ev->serial = core->event_serial;
    ev->kind = kind;
    ev->cycle = (int)core->world->cycle;
    ev->a = a;
    ev->b = b;
    ev->c = c;
    ev->d = d;
}

void
ScriptrunCore_BeginTick(
    struct ScriptrunCore* core,
    int server_tick)
{
    int now;
    int kept;

    assert(core);
    core->tick = server_tick;
    core->world->cycle = (uint64_t)server_tick * SCRIPTRUN_CYCLES_PER_TICK;
    now = (int)core->world->cycle;

    /* A projectile the client would no longer draw: past its end cycle. */
    kept = 0;
    for( int i = 0; i < core->projectile_count; i++ )
    {
        struct ScriptrunProjectile const* p = &core->projectiles[i];
        if( p->launch_cycle + p->end_delay >= now )
            core->projectiles[kept++] = *p;
    }
    core->projectile_count = kept;
    /* Map graphics: kept for 40 ticks, the longest the floor graphics this
     * runner is built for last (Verzik's pools, 14 ticks). A spotanim's own
     * sequence length would be exact; the consumers judge by id and tile. */
    kept = 0;
    for( int i = 0; i < core->map_anim_count; i++ )
    {
        struct ScriptrunMapAnim const* a = &core->map_anims[i];
        if( now - a->cycle <= 40 * SCRIPTRUN_CYCLES_PER_TICK )
            core->map_anims[kept++] = *a;
    }
    core->map_anim_count = kept;
}

/* ------------------------------------------------------------------ helpers */

static int
core_base_x(struct ScriptrunCore const* core)
{
    return (core->rebuild_zone_x - 6) * 8;
}

static int
core_base_z(struct ScriptrunCore const* core)
{
    return (core->rebuild_zone_z - 6) * 8;
}

static int
core_local_pid(struct ScriptrunCore const* core)
{
    return core->esync->local_pid >= 0 ? core->esync->local_pid : CORE_LOCAL_SENTINEL;
}

/* game/task_exec_entity_info.c player_local_tile: routeX[0], not the grid. */
static void
core_player_local_tile(
    struct ScriptrunCore* core,
    int* out_x,
    int* out_z,
    int* out_level)
{
    int world_idx;

    *out_x = 52;
    *out_z = 52;
    *out_level = 0;
    if( RS_EntitySync_FindPlayer(core->esync, core_local_pid(core), &world_idx, NULL) )
    {
        struct WorldEntity_Player* player =
            World_EntityPoolGet(&core->world->entities.player, world_idx);
        if( player )
        {
            *out_x = player->pathing.route_x[0];
            *out_z = player->pathing.route_z[0];
            *out_level = player->grid_position.level;
        }
    }
}

/* RS_EntityInfo_NpcOrigin, less the sailing view. */
static void
core_npc_origin(
    struct ScriptrunCore* core,
    int* out_x,
    int* out_z,
    int* out_level)
{
    core_player_local_tile(core, out_x, out_z, out_level);
    if( core->npc_origin_valid )
    {
        *out_x = core->npc_origin_x;
        *out_z = core->npc_origin_z;
    }
}

static struct WorldEntity_Headbar
core_headbar(
    struct ScriptrunCore* core,
    int type,
    int duration,
    int start_delay,
    int start_fill,
    int end_fill)
{
    struct WorldEntity_Headbar bar;

    memset(&bar, 0, sizeof(bar));
    bar.type = type;
    bar.start_cycle = (int)core->world->cycle + start_delay;
    bar.duration = duration;
    bar.start_fill = start_fill;
    bar.end_fill = end_fill;
    /* The type's persist cycles are a render nicety; the fill is what the
     * drive reports. */
    bar.end_cycle = bar.start_cycle + duration;
    return bar;
}

int
ScriptrunCore_LocalTile(
    struct ScriptrunCore const* core,
    int* x,
    int* z,
    int* level)
{
    int sx, sz, slevel;
    int world_idx;

    assert(core);
    assert(x);
    assert(z);
    assert(level);
    if( !core->have_scene ||
        !RS_EntitySync_FindPlayer(core->esync, core_local_pid(core), &world_idx, NULL) )
        return 0;
    core_player_local_tile((struct ScriptrunCore*)core, &sx, &sz, &slevel);
    *x = core_base_x(core) + sx;
    *z = core_base_z(core) + sz;
    *level = slevel;
    return 1;
}

/* ------------------------------------------------------------ player info */

struct PlayerApply
{
    struct ScriptrunCore* core;
    int cur_pid;
    int old_count;
    int need_ensure;
};

static int
player_target(
    struct PlayerApply* self,
    int* out_element_id)
{
    int world_idx = -1;
    int element_id = -1;

    if( self->cur_pid >= 0 )
        RS_EntitySync_FindPlayer(self->core->esync, self->cur_pid, &world_idx, &element_id);
    if( out_element_id )
        *out_element_id = element_id;
    return world_idx;
}

/* game/task_exec_entity_info.c player_target_op. */
static int
player_target_op(
    struct PlayerApply* self,
    struct PktPlayerInfoOp const* op)
{
    struct ScriptrunCore* core = self->core;
    struct RS_EntitySync* esync = core->esync;

    switch( op->kind )
    {
    case PKT_PLAYER_INFO_OP_SET_LOCAL_PLAYER:
        self->cur_pid = core_local_pid(core);
        break;
    case PKT_PLAYER_INFO_OP_ADD_PLAYER_OLD_OPBITS_IDX:
        self->cur_pid =
            (int)op->_bitvalue < self->old_count ? core->player_old_list[op->_bitvalue] : -1;
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
        int pid =
            (int)op->_bitvalue < self->old_count ? core->player_old_list[op->_bitvalue] : -1;
        if( pid >= 0 )
            RS_EntitySync_RemovePlayer(esync, core->world, pid);
        self->cur_pid = -1;
        return 1;
    }
    case PKT_PLAYER_INFO_OP_REMOVE_PLAYER_PID:
        RS_EntitySync_RemovePlayer(esync, core->world, (int)op->_bitvalue);
        self->cur_pid = -1;
        return 1;
    case PKT_PLAYER_INFO_OPBITS_COUNT_RESET:
    {
        int keep = (int)op->_bitvalue;
        for( int i = keep; i < self->old_count; i++ )
            RS_EntitySync_RemovePlayer(esync, core->world, core->player_old_list[i]);
        esync->active_player_count = 0;
        return 1;
    }
    case PKT_PLAYER_INFO_OPBITS_INFO:
        return 1;
    default:
        return 0;
    }
    self->need_ensure = (self->cur_pid >= 0 && player_target(self, NULL) < 0) ? 1 : 0;
    return 1;
}

/* player_ensure_now: a player entity with no body. */
static void
player_ensure_now(struct PlayerApply* self)
{
    struct ScriptrunCore* core = self->core;
    struct WorldEntityFacet_IdleAnimations idle;
    int tile_x, tile_z, level;
    int idx;
    int element_id;

    self->need_ensure = 0;
    core_player_local_tile(core, &tile_x, &tile_z, &level);
    memset(&idle, 0, sizeof(idle));
    element_id = core->next_element_id++;
    idx = World_PlayerSpawn(core->world, element_id, level, tile_x, tile_z, idle);
    if( idx < 0 )
        return;
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(&core->world->entities.player, idx);
        if( player )
        {
            player->server_pid = self->cur_pid;
            RS_EntitySync_RegisterPlayer(core->esync, self->cur_pid, element_id, idx);
        }
    }
}

/* game/task_exec_entity_info.c player_apply_op, applied at once (no awaits). */
static void
player_apply_op(
    struct PlayerApply* self,
    struct PktPlayerInfoOp const* op)
{
    struct ScriptrunCore* core = self->core;
    struct World* world = core->world;
    int idx = player_target(self, NULL);

    if( idx < 0 )
        return;

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
            world, idx, op->_local_xz_level.jump, op->_local_xz_level.x, op->_local_xz_level.z);
        if( player )
            player->grid_position.level = op->_local_xz_level.level;
        break;
    }
    case PKT_PLAYER_INFO_OP_ABS_XZLEVEL:
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(&world->entities.player, idx);
        int next_x = op->_local_xz_level.x - core_base_x(core);
        int next_z = op->_local_xz_level.z - core_base_z(core);
        int step_type = op->_local_xz_level.has_move_speed &&
                                op->_local_xz_level.move_speed == PKT_PLAYER_TRAVERSAL_RUN
                            ? WORLD_PATHSTEP_RUN
                            : WORLD_PATHSTEP_WALK;

        World_PlayerPathJumpCollisionAware(
            world, idx, NULL, op->_local_xz_level.jump, next_x, next_z, step_type);
        if( player )
            player->grid_position.level = op->_local_xz_level.level;
        break;
    }
    case PKT_PLAYER_INFO_OP_DELTA_XZ:
    {
        int lx, lz, llevel;
        core_player_local_tile(core, &lx, &lz, &llevel);
        World_PlayerPathJump(
            world, idx, op->_delta_xz.jump, lx + op->_delta_xz.dx, lz + op->_delta_xz.dz);
        break;
    }
    case PKT_PLAYER_INFO_OP_APPEARANCE:
    {
        /* The name is what a client draws over a player and what the drive's
         * players() reports; the body is not modelled here. */
        struct PktPlayerAppearance decoded;
        int ok;

        memset(&decoded, 0, sizeof(decoded));
        ok = core->rev->appearance_decode
                 ? core->rev->appearance_decode(
                       op->_appearance.appearance, op->_appearance.len, &decoded)
                 : PktPlayerAppearance_Decode(
                       &decoded, op->_appearance.appearance, op->_appearance.len);
        if( ok )
        {
            struct WorldEntity_Player* player =
                World_EntityPoolGet(&world->entities.player, idx);
            if( player )
            {
                strncpy(player->name, decoded.name, sizeof(player->name) - 1);
                player->name[sizeof(player->name) - 1] = '\0';
            }
        }
        break;
    }
    case PKT_PLAYER_INFO_OP_SEQUENCE:
        World_PlayerSetPrimaryAnimation(
            world, idx, op->_sequence.sequence_id, op->_sequence.delay);
        if( self->cur_pid == core_local_pid(core) && op->_sequence.sequence_id >= 0 )
        {
            int k = core->own_seq_count < 16 ? core->own_seq_count++ : 15;
            if( k == 15 && core->own_seq_count == 16 )
            {
                memmove(core->own_seq, core->own_seq + 1, 15 * sizeof(int));
                memmove(core->own_seq_tick, core->own_seq_tick + 1, 15 * sizeof(int));
                memmove(core->own_seq_n, core->own_seq_n + 1, 15 * sizeof(int));
            }
            core->own_seq_starts++;
            core->own_seq[k] = op->_sequence.sequence_id;
            core->own_seq_tick[k] = core->tick;
            core->own_seq_n[k] = core->own_seq_starts;
        }
        break;
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
        if( op->_face_coord.modern )
            World_PlayerBeginModernFacing(world, idx, op->_face_coord.movement_mode);
        World_PlayerFaceCoord(world, idx, op->_face_coord.x, op->_face_coord.z);
        break;
    case PKT_PLAYER_INFO_OP_SAY:
        if( op->_say.text )
            World_PlayerSetChat(world, idx, op->_say.text, 0, 0);
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
            WORLD_HITMARK_DEFAULT_DURATION,
            WORLD_HITMARK_POLICY_DISCARD);
        break;
    case PKT_PLAYER_INFO_OP_HEADBAR:
        if( op->_headbar.remove )
            World_PlayerClearHealthbar(world, idx);
        else
            World_PlayerSetHealthbar(
                world,
                idx,
                core_headbar(
                    core, op->_headbar.type, op->_headbar.duration, op->_headbar.start_delay,
                    op->_headbar.start_fill, op->_headbar.end_fill));
        break;
    case PKT_PLAYER_INFO_OP_SPOTANIM:
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
}

static void
core_player_info(
    struct ScriptrunCore* core,
    uint8_t const* data,
    int length)
{
    struct PktPlayerInfoOp* ops = core->player_ops;
    struct PlayerApply self;
    int op_count;

    assert(core->rev->player_info_read);
    op_count = core->rev->player_info_read(data, length, ops, CORE_ENTITY_OPS_MAX);

    memset(&self, 0, sizeof(self));
    self.core = core;
    self.old_count = core->esync->active_player_count;
    memcpy(core->player_old_list, core->esync->active_players,
           (size_t)self.old_count * sizeof(core->player_old_list[0]));
    core->esync->active_player_count = 0;
    self.cur_pid = -1;

    for( int i = 0; i < op_count; i++ )
    {
        int consumed = player_target_op(&self, &ops[i]);
        if( self.need_ensure )
            player_ensure_now(&self);
        if( !consumed )
            player_apply_op(&self, &ops[i]);
    }
    pkt_player_info_ops_free(ops, op_count);
}

/* --------------------------------------------------------------- npc info */

struct NpcApply
{
    struct ScriptrunCore* core;
    int cur_slot;
    int old_count;
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
        RS_EntitySync_FindNpc(self->core->esync, self->cur_slot, &world_idx, &element_id);
    if( out_element_id )
        *out_element_id = element_id;
    return world_idx;
}

int
ScriptrunCore_NpcResolve(
    struct ScriptrunCore const* core,
    int npc_id)
{
    assert(core);
    /* App_NpctypeResolveMultiId: VarPManager_ResolveTransform over this
     * client's varps, four rungs (TORIRS_NPC_MULTI_MAX_DEPTH). */
    for( int depth = 0; depth < 4 && npc_id >= 0; depth++ )
    {
        const struct ToriRSServerNpcInfo* row = ToriRSServer_NpcInfoRecord(npc_id);
        int index = -1;
        int resolved;

        if( !row || row->transform_count <= 0 || !row->transforms )
            return npc_id;
        if( row->transform_varbit != -1 )
        {
            if( !ScriptrunCore_Varbit(core, row->transform_varbit, &index) )
                index = 0;
        }
        else if( row->transform_varp >= 0 && row->transform_varp < SCRIPTRUN_VARPS )
            index = core->varps[row->transform_varp];
        if( index >= 0 && index < row->transform_count - 1 )
            resolved = row->transforms[index];
        else
            resolved = row->transforms[row->transform_count - 1];
        if( resolved < 0 )
            return -1;
        if( resolved == npc_id )
            return npc_id;
        npc_id = resolved;
    }
    return npc_id;
}

/* The npc's config as the client's cache carries it: name, size, combat
 * level, off the RUNG this client's varps select, gap-filled from the shell
 * (torirs_types.h ToriRS_NpcEntityFacts: a rung is a delta -- verzik_initial's
 * rung states no size, its shell says 5). Read through the server's table of
 * the pack's CLIENT records -- the same bytes the client codec reads. */
static void
npc_type_config(
    struct ScriptrunCore const* core,
    int wire_id,
    int* out_live_id,
    char* name,
    int name_cap,
    int* size,
    int* combat_level)
{
    int live = ScriptrunCore_NpcResolve(core, wire_id);
    const struct ToriRSServerNpcInfo* rung = ToriRSServer_NpcInfoRecord(live >= 0 ? live : wire_id);
    const struct ToriRSServerNpcInfo* shell = ToriRSServer_NpcInfoRecord(wire_id);

    *out_live_id = live >= 0 ? live : wire_id;
    snprintf(name, (size_t)name_cap, "%s", rung && rung->name ? rung->name : "");
    /* ToriRS_NpctypeEntityFacts' rule, word for word. */
    *size = rung && rung->size > 0 ? rung->size : 1;
    if( shell && shell != rung && *size <= 1 && shell->size > 1 )
        *size = shell->size;
    *combat_level = rung ? rung->combat_level : 0;
}

/* npc_spawn_now: a model-less npc entity registered under its slot. */
static void
npc_spawn_now(struct NpcApply* self)
{
    struct ScriptrunCore* core = self->core;
    struct WorldEntityFacet_IdleAnimations idle;
    char name[64];
    int size, combat_level;
    int tile_x, tile_z, level;
    int element_id;
    int idx;
    int live;

    npc_type_config(core, self->pending_base_type, &live, name, (int)sizeof(name), &size, &combat_level);
    core_npc_origin(core, &tile_x, &tile_z, &level);
    memset(&idle, 0, sizeof(idle));
    element_id = core->next_element_id++;
    /* The client spawns the RUNG (app_world_spawn.c) and keeps the wire's id
     * as base_npc_id. */
    idx = World_NpcSpawn(core->world, element_id, live, level, tile_x, tile_z, size, idle);
    if( idx < 0 )
        return;
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(&core->world->entities.npc, idx);
        if( npc )
        {
            npc->base_npc_id = self->pending_base_type;
            npc->server_slot = self->cur_slot;
            npc->combat_level = combat_level;
            snprintf(npc->name, sizeof(npc->name), "%s", name);
            RS_EntitySync_RegisterNpc(core->esync, self->cur_slot, element_id, idx);
        }
    }
}

/* A server retype (CHANGE_TYPE, and an add over a live slot's new type). */
static void
npc_retype_now(
    struct NpcApply* self,
    int idx)
{
    struct ScriptrunCore* core = self->core;
    char name[64];
    int size, combat_level;
    struct WorldEntity_NPC* npc;

    int live;

    npc_type_config(core, self->pending_base_type, &live, name, (int)sizeof(name), &size, &combat_level);
    World_NpcSetType(core->world, idx, live, size, NULL);
    npc = World_EntityPoolGet(&core->world->entities.npc, idx);
    if( npc )
    {
        npc->base_npc_id = self->pending_base_type;
        npc->combat_level = combat_level;
        snprintf(npc->name, sizeof(npc->name), "%s", name);
    }
}

/* app_varp_transforms.c: a varp change re-resolves every multinpc in view
 * (its rung, name and size), the wire id kept as base. */
static void
core_npcs_reresolve(struct ScriptrunCore* core)
{
    struct World_EntityPool* pool = &core->world->entities.npc;

    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, i);
        const struct ToriRSServerNpcInfo* shell;
        char name[64];
        int size, combat_level, live;

        if( !npc || npc->server_slot < 0 )
            continue;
        shell = ToriRSServer_NpcInfoRecord(npc->base_npc_id);
        if( !shell || shell->transform_count <= 0 )
            continue;
        npc_type_config(core, npc->base_npc_id, &live, name, (int)sizeof(name), &size, &combat_level);
        if( live == npc->npc_id )
            continue;
        World_NpcSetType(core->world, i, live, size, NULL);
        npc->combat_level = combat_level;
        snprintf(npc->name, sizeof(npc->name), "%s", name);
    }
}

/* game/task_exec_entity_info.c npc_target_op. */
static int
npc_target_op(
    struct NpcApply* self,
    struct PktNpcInfoOp const* op)
{
    struct ScriptrunCore* core = self->core;
    struct RS_EntitySync* esync = core->esync;

    switch( op->kind )
    {
    case PKT_NPC_INFO_OP_ADD_NPC_OLD_OPBITS_IDX:
        self->cur_slot =
            (int)op->_bitvalue < self->old_count ? core->npc_old_list[op->_bitvalue] : -1;
        if( esync->active_npc_count < RS_ENTITY_SYNC_MAX_NPCS )
            esync->active_npcs[esync->active_npc_count++] = self->cur_slot;
        break;
    case PKT_NPC_INFO_OP_ADD_NPC_NEW_OPBITS_PID:
    {
        int stale_world_idx = -1;
        int stale_element_id = -1;
        self->cur_slot = (int)op->_bitvalue;
        if( self->cur_slot >= 0 &&
            RS_EntitySync_FindNpc(esync, self->cur_slot, &stale_world_idx, &stale_element_id) )
            RS_EntitySync_RemoveNpc(esync, core->world, self->cur_slot);
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
        int slot = (int)op->_bitvalue < self->old_count ? core->npc_old_list[op->_bitvalue] : -1;
        if( slot >= 0 )
            RS_EntitySync_RemoveNpc(esync, core->world, slot);
        self->cur_slot = -1;
        return 1;
    }
    case PKT_NPC_INFO_OPBITS_COUNT_RESET:
    {
        int keep = (int)op->_bitvalue;
        for( int i = keep; i < self->old_count; i++ )
            RS_EntitySync_RemoveNpc(esync, core->world, core->npc_old_list[i]);
        esync->active_npc_count = 0;
        return 1;
    }
    case PKT_NPC_INFO_OPBITS_INFO:
        return 1;
    default:
        return 0;
    }
    return 1;
}

/* game/task_exec_entity_info.c npc_apply_op, with its awaits resolved at once:
 * a spawn or a retype is applied in place (no multinpc rung is resolved; the
 * wire type is drawn), a SEQUENCE is applied immediately as the client does. */
static void
npc_apply_op(
    struct NpcApply* self,
    struct PktNpcInfoOp const* op)
{
    struct ScriptrunCore* core = self->core;
    struct World* world = core->world;
    int idx = npc_target(self, NULL);

    switch( op->kind )
    {
    case PKT_NPC_INFO_OPBITS_NPCTYPE:
        self->pending_base_type = (int)op->_bitvalue;
        if( self->cur_slot >= 0 && idx < 0 )
            npc_spawn_now(self);
        break;
    case PKT_NPC_INFO_OPBITS_WALKDIR:
        if( idx >= 0 )
            World_NpcPathPushStep(world, idx, WORLD_PATHSTEP_WALK, (int)op->_bitvalue);
        break;
    case PKT_NPC_INFO_OPBITS_RUNDIR:
        if( idx >= 0 )
            World_NpcPathPushStep(world, idx, WORLD_PATHSTEP_RUN, (int)op->_bitvalue);
        break;
    case PKT_NPC_INFO_OP_DELTA_XZ:
        if( idx >= 0 )
        {
            int lx, lz, llevel;
            core_npc_origin(core, &lx, &lz, &llevel);
            World_NpcPathJump(
                world, idx, op->_delta_xz.jump, lx + op->_delta_xz.dx, lz + op->_delta_xz.dz);
        }
        break;
    case PKT_NPC_INFO_OP_SEQUENCE:
        if( idx >= 0 )
        {
            World_NpcSetPrimaryAnimation(
                world, idx, op->_sequence.sequence_id, op->_sequence.delay);
            /* DRIVE_STAMP npc_seq: a=slot b=seq c=tick, on the op's arrival. */
            ScriptrunCore_Event(core, SCRIPTRUN_EV_NPC_SEQ, self->cur_slot,
                                op->_sequence.sequence_id, core->tick, 0);
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
        }
        break;
    case PKT_NPC_INFO_OP_FACE_COORD:
        if( idx >= 0 )
        {
            struct WorldEntity_NPC* npc;
            if( op->_face_coord.modern )
                World_NpcBeginModernFacing(world, idx, op->_face_coord.movement_mode);
            World_NpcFaceCoord(world, idx, op->_face_coord.x, op->_face_coord.z);
            npc = World_EntityPoolAt(&world->entities.npc, idx);
            if( op->_face_coord.instant )
                npc->facing.instant = true;
            npc->face_sent_x = op->_face_coord.x;
            npc->face_sent_z = op->_face_coord.z;
            npc->face_sent_cycle = (int)world->cycle;
            /* DRIVE_STAMP npc_face: a=slot b=x c=z d=tick (tiles, halved). */
            ScriptrunCore_Event(core, SCRIPTRUN_EV_NPC_FACE, self->cur_slot,
                                op->_face_coord.x >> 1, op->_face_coord.z >> 1, core->tick);
        }
        break;
    case PKT_NPC_INFO_OP_SAY:
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
                WORLD_HITMARK_DEFAULT_DURATION,
                WORLD_HITMARK_POLICY_DISCARD);
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
                    core_headbar(
                        core, op->_headbar.type, op->_headbar.duration, op->_headbar.start_delay,
                        op->_headbar.start_fill, op->_headbar.end_fill));
        }
        break;
    case PKT_NPC_INFO_OP_CHANGE_TYPE:
        self->pending_base_type = op->_change_type.npc_type;
        if( idx >= 0 )
            npc_retype_now(self, idx);
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
            struct WorldEntity_NPC* npc = World_EntityPoolAt(&world->entities.npc, idx);
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
                world, idx, sx, sz, ex, ez, op->_exactmove.start_cycle_delta,
                op->_exactmove.end_cycle_delta, op->_exactmove.facing,
                op->_exactmove.facing_is_yaw);
        }
        break;
    case PKT_NPC_INFO_OP_FACE_ANGLE:
        if( idx >= 0 )
        {
            struct WorldEntity_NPC* npc = World_EntityPoolAt(&world->entities.npc, idx);
            int angle;
            if( op->_face_angle.modern )
                World_NpcBeginModernFacing(world, idx, op->_face_angle.movement_mode);
            angle = op->_face_angle.spawn && npc->facing.turn_speed == 0 ? 0
                                                                         : op->_face_angle.angle;
            World_NpcFaceAngle(world, idx, angle, op->_face_angle.instant);
        }
        break;
    case PKT_NPC_INFO_OP_SPAWN_CYCLE:
        if( idx >= 0 )
            ((struct WorldEntity_NPC*)World_EntityPoolAt(&world->entities.npc, idx))->spawn_cycle =
                (uint32_t)op->_bitvalue;
        break;
    case PKT_NPC_INFO_OP_VISIBLE_OPS:
        if( idx >= 0 )
            ((struct WorldEntity_NPC*)World_EntityPoolAt(&world->entities.npc, idx))->visible_ops =
                (uint8_t)op->_bitvalue;
        break;
    case PKT_NPC_INFO_OP_NAME_CHANGE:
        if( idx >= 0 && op->_name_change.name )
        {
            struct WorldEntity_NPC* npc = World_EntityPoolAt(&world->entities.npc, idx);
            snprintf(npc->name, sizeof(npc->name), "%s", op->_name_change.name);
        }
        break;
    case PKT_NPC_INFO_OP_LEVEL_CHANGE:
        if( idx >= 0 )
            ((struct WorldEntity_NPC*)World_EntityPoolAt(&world->entities.npc, idx))->combat_level =
                (int)(uint32_t)op->_bitvalue;
        break;
    case PKT_NPC_INFO_OP_BAS_CHANGE:
        if( idx >= 0 )
        {
            struct WorldEntity_NPC* npc = World_EntityPoolAt(&world->entities.npc, idx);
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
}

static void
core_npc_info(
    struct ScriptrunCore* core,
    uint8_t const* data,
    int length)
{
    struct PktNpcInfoOp* ops = core->npc_ops;
    struct NpcApply self;
    int op_count;

    assert(core->rev->npc_info_read);
    op_count = core->rev->npc_info_read(data, length, ops, CORE_ENTITY_OPS_MAX);

    memset(&self, 0, sizeof(self));
    self.core = core;
    self.old_count = core->esync->active_npc_count;
    memcpy(core->npc_old_list, core->esync->active_npcs,
           (size_t)self.old_count * sizeof(core->npc_old_list[0]));
    core->esync->active_npc_count = 0;
    self.cur_slot = -1;
    self.pending_base_type = -1;

    for( int i = 0; i < op_count; i++ )
    {
        if( !npc_target_op(&self, &ops[i]) )
            npc_apply_op(&self, &ops[i]);
    }
    pkt_npc_info_ops_free(ops, op_count);
}

/* ------------------------------------------------------------ zone packets */

static void
core_zone_tile(
    struct ScriptrunCore const* core,
    int pos,
    int* abs_x,
    int* abs_z,
    int* level)
{
    *abs_x = core_base_x(core) + core->zone_base_x + ((pos >> 4) & 7);
    *abs_z = core_base_z(core) + core->zone_base_z + (pos & 7);
    *level = core->zone_level;
}

static int
core_zone_header_level(
    struct ScriptrunCore* core,
    int header_level)
{
    int x, z, level;

    if( header_level >= 0 )
        return header_level;
    if( ScriptrunCore_LocalTile(core, &x, &z, &level) )
        return level;
    return 0;
}


static void
core_zone_sub(
    struct ScriptrunCore* core,
    int name,
    void const* payload)
{
    int x, z, level;

    switch( name )
    {
    case PKT_NAME_LOC_ADD_CHANGE:
    {
        struct PktLocAddChange const* pkt = payload;
        core_zone_tile(core, pkt->pos, &x, &z, &level);
        ScriptrunCore_LocChange(core, x, z, level, pkt->info >> 2, pkt->info & 3, pkt->loc_id);
        break;
    }
    case PKT_NAME_LOC_DEL:
    {
        struct PktLocDel const* pkt = payload;
        core_zone_tile(core, pkt->pos, &x, &z, &level);
        ScriptrunCore_LocChange(core, x, z, level, pkt->info >> 2, pkt->info & 3, -1);
        break;
    }
    case PKT_NAME_OBJ_ADD:
    {
        struct PktObjAdd const* pkt = payload;
        core_zone_tile(core, pkt->pos, &x, &z, &level);
        if( core->obj_count < SCRIPTRUN_OBJS )
        {
            struct ScriptrunObj* o = &core->objs[core->obj_count++];
            o->x = x;
            o->z = z;
            o->level = level;
            o->obj_id = pkt->obj_id;
            o->count = pkt->count;
        }
        break;
    }
    case PKT_NAME_OBJ_DEL:
    {
        struct PktObjDel const* pkt = payload;
        core_zone_tile(core, pkt->pos, &x, &z, &level);
        for( int i = 0; i < core->obj_count; i++ )
        {
            struct ScriptrunObj* o = &core->objs[i];
            if( o->x == x && o->z == z && o->level == level && o->obj_id == pkt->obj_id )
            {
                core->objs[i] = core->objs[--core->obj_count];
                break;
            }
        }
        break;
    }
    case PKT_NAME_MAP_ANIM:
    {
        struct PktMapAnim const* pkt = payload;
        if( pkt->id == 65535 )
            break;
        core_zone_tile(core, pkt->pos, &x, &z, &level);
        if( core->map_anim_count < SCRIPTRUN_MAP_ANIMS )
        {
            struct ScriptrunMapAnim* a = &core->map_anims[core->map_anim_count++];
            a->spotanim = pkt->id;
            a->x = x;
            a->z = z;
            a->level = level;
            a->height = pkt->height;
            a->delay = pkt->delay;
            a->cycle = (int)core->world->cycle;
        }
        break;
    }
    case PKT_NAME_MAP_PROJANIM:
    {
        struct PktMapProjAnim const* pkt = payload;
        struct ScriptrunProjectile* p;
        if( core->projectile_count >= SCRIPTRUN_PROJECTILES )
            break;
        core_zone_tile(core, pkt->pos, &x, &z, &level);
        p = &core->projectiles[core->projectile_count++];
        p->spotanim = pkt->spotanim;
        p->src_x = x;
        p->src_z = z;
        if( pkt->dst_abs )
        {
            p->dst_x = pkt->dst_abs_x;
            p->dst_z = pkt->dst_abs_z;
            level = pkt->dst_abs_level;
        }
        else
        {
            p->dst_x = x + pkt->dx_offset;
            p->dst_z = z + pkt->dz_offset;
        }
        p->level = level;
        p->target = pkt->target;
        p->launch_cycle = (int)core->world->cycle;
        p->start_delay = pkt->start_delay;
        p->end_delay = pkt->end_delay;
        break;
    }
    default:
        break;
    }
}

/* -------------------------------------------------------------- the rest */

static struct ScriptrunInv*
core_inv(
    struct ScriptrunCore* core,
    int inv_id)
{
    struct ScriptrunInv* free_slot = NULL;

    for( int i = 0; i < SCRIPTRUN_INVS; i++ )
    {
        if( core->invs[i].inv_id == inv_id )
            return &core->invs[i];
        if( core->invs[i].inv_id < 0 && !free_slot )
            free_slot = &core->invs[i];
    }
    assert(free_slot);
    free_slot->inv_id = inv_id;
    free_slot->size = 0;
    for( int s = 0; s < SCRIPTRUN_INV_SLOTS; s++ )
    {
        free_slot->obj[s] = -1;
        free_slot->count[s] = 0;
    }
    return free_slot;
}

static void
core_message(
    struct ScriptrunCore* core,
    int type,
    char const* name,
    char const* text)
{
    struct ScriptrunMessage* m;

    core->message_serial++;
    m = &core->messages[core->message_serial % SCRIPTRUN_MESSAGES];
    m->serial = core->message_serial;
    m->type = type;
    snprintf(m->name, sizeof(m->name), "%s", name ? name : "");
    snprintf(m->text, sizeof(m->text), "%s", text ? text : "");
}

static void
core_rebuild(
    struct ScriptrunCore* core,
    int zone_x,
    int zone_z)
{
    if( core->have_scene && (zone_x != core->rebuild_zone_x || zone_z != core->rebuild_zone_z) )
    {
        /* App_WorldRebuildShift: entities keep their world tile. */
        int dx = (core->rebuild_zone_x - zone_x) * 8;
        int dz = (core->rebuild_zone_z - zone_z) * 8;
        World_ShiftEntities(core->world, dx, dz);
    }
    core->rebuild_zone_x = zone_x;
    core->rebuild_zone_z = zone_z;
    core->have_scene = 1;
    core->world->_base_tile_x = core_base_x(core);
    core->world->_base_tile_z = core_base_z(core);
    core->world->load_complete = true;
}

static void
core_packet(
    struct ScriptrunCore* core,
    int pkt_name,
    const uint8_t* payload,
    int len);

void
ScriptrunCore_Packet(
    struct ScriptrunCore* core,
    int pkt_name,
    const uint8_t* payload,
    int len)
{
    assert(core);
    assert(len >= 0);
    core->packets++;
    if( core->player_decoder_state )
        osrs239_playerinfo_state_load(core->player_decoder_state);
    core_packet(core, pkt_name, payload, len);
    if( core->player_decoder_state )
        osrs239_playerinfo_state_save(core->player_decoder_state);
}

static void
core_packet(
    struct ScriptrunCore* core,
    int pkt_name,
    const uint8_t* payload,
    int len)
{
    struct RevPacket packet;
    uint8_t* data;
    int parsed;

    /* PLAYER_INFO / NPC_INFO: the client decodes the raw stream itself. */
    if( pkt_name == PKT_NAME_PLAYER_INFO )
    {
        core_player_info(core, payload, len);
        return;
    }
    if( pkt_name == PKT_NAME_NPC_INFO )
    {
        core_npc_info(core, payload, len);
        return;
    }

    /* gameproto_parse reads from a mutable buffer. */
    data = malloc((size_t)(len > 0 ? len : 1));
    assert(data);
    if( len > 0 )
        memcpy(data, payload, (size_t)len);
    /* net/net.c's three parsers, in its order: the generated rsprot codecs,
     * the revision's own overrides, then the shared parser. */
    memset(&packet, 0, sizeof(packet));
    packet.packet_type = pkt_name;
    {
        int via = 0;
        parsed = rsprot_bridge_parse(core->rev, pkt_name, data, len, &packet);
        if( parsed < 0 && core->rev->parse )
        {
            via = 1;
            parsed = core->rev->parse(core->rev, pkt_name, data, len, &packet);
        }
        if( parsed < 0 )
        {
            via = 2;
            parsed = gameproto_parse(core->rev, (enum GameProtoPktName)pkt_name, data, len, &packet);
        }
        if( getenv("TORIRS_SCRIPTRUN_DEBUG") && pkt_name == PKT_NAME_REBUILD_NORMAL )
            fprintf(stderr, "scriptrun: REBUILD_NORMAL len %d parsed %d via %s\n", len, parsed,
                    via == 0 ? "rsprot" : via == 1 ? "rev" : "gameproto");
    }
    if( parsed <= 0 )
    {
        core->unhandled++;
        gameproto_free(&packet);
        free(data);
        return;
    }

    switch( pkt_name )
    {
    case PKT_NAME_REBUILD_NORMAL:
        core_rebuild(core, packet._map_rebuild.zonex, packet._map_rebuild.zonez);
        ScriptrunCore_BuildCollision(core);
        break;
    case PKT_NAME_REBUILD_REGION:
        /* An instance: the scene is copied zone by zone from template zones
         * (WorldBuilder_RebuildInstance), collision with it. */
        core_rebuild(core, packet._map_rebuild.zonex, packet._map_rebuild.zonez);
        assert(packet._map_rebuild.zones);
        ScriptrunCore_BuildInstanceCollision(core, packet._map_rebuild.zones);
        break;
    case PKT_NAME_SET_NPC_UPDATE_ORIGIN:
        core->npc_origin_x = packet._set_npc_update_origin.x;
        core->npc_origin_z = packet._set_npc_update_origin.z;
        core->npc_origin_valid = 1;
        break;
    case PKT_NAME_UPDATE_PID:
        core->esync->local_pid = packet._update_pid.local_player_index;
        break;
    case PKT_NAME_UPDATE_ZONE_PARTIAL_FOLLOWS:
        core->zone_base_x = packet._update_zone_partial_follows.base_x;
        core->zone_base_z = packet._update_zone_partial_follows.base_z;
        core->zone_level =
            core_zone_header_level(core, packet._update_zone_partial_follows.level);
        break;
    case PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS:
    {
        int x0, z0, level;
        core->zone_base_x = packet._update_zone_full_follows.base_x;
        core->zone_base_z = packet._update_zone_full_follows.base_z;
        core->zone_level = core_zone_header_level(core, packet._update_zone_full_follows.level);
        /* A full zone resend restates every obj in it: forget the old ones. */
        x0 = core_base_x(core) + core->zone_base_x;
        z0 = core_base_z(core) + core->zone_base_z;
        level = core->zone_level;
        for( int i = 0; i < core->obj_count; )
        {
            struct ScriptrunObj* o = &core->objs[i];
            if( o->level == level && o->x >= x0 && o->x < x0 + 8 && o->z >= z0 && o->z < z0 + 8 )
                core->objs[i] = core->objs[--core->obj_count];
            else
                i++;
        }
        break;
    }
    case PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED:
    {
        struct PktUpdateZoneEnclosed const* enc = &packet._update_zone_enclosed;
        core->zone_base_x = enc->base_x;
        core->zone_base_z = enc->base_z;
        core->zone_level = core_zone_header_level(core, enc->level);
        for( int i = 0; i < enc->count; i++ )
            core_zone_sub(core, enc->entries[i].name, &enc->entries[i]._loc_add_change);
        break;
    }
    case PKT_NAME_LOC_ADD_CHANGE:
        core_zone_sub(core, pkt_name, &packet._loc_add_change);
        break;
    case PKT_NAME_LOC_DEL:
        core_zone_sub(core, pkt_name, &packet._loc_del);
        break;
    case PKT_NAME_OBJ_ADD:
        core_zone_sub(core, pkt_name, &packet._obj_add);
        break;
    case PKT_NAME_OBJ_DEL:
        core_zone_sub(core, pkt_name, &packet._obj_del);
        break;
    case PKT_NAME_MAP_ANIM:
        core_zone_sub(core, pkt_name, &packet._map_anim);
        break;
    case PKT_NAME_MAP_PROJANIM:
        core_zone_sub(core, pkt_name, &packet._map_projanim);
        break;
    case PKT_NAME_VARP_SMALL:
        if( packet._varp_small.variable >= 0 && packet._varp_small.variable < SCRIPTRUN_VARPS )
        {
            core->varps[packet._varp_small.variable] = packet._varp_small.value;
            ScriptrunCore_Event(core, SCRIPTRUN_EV_VARP_CHANGED, packet._varp_small.variable,
                                packet._varp_small.value, 0, 0);
            core_npcs_reresolve(core);
        }
        break;
    case PKT_NAME_VARP_LARGE:
        if( packet._varp_large.variable >= 0 && packet._varp_large.variable < SCRIPTRUN_VARPS )
        {
            core->varps[packet._varp_large.variable] = packet._varp_large.value;
            ScriptrunCore_Event(core, SCRIPTRUN_EV_VARP_CHANGED, packet._varp_large.variable,
                                packet._varp_large.value, 0, 0);
            core_npcs_reresolve(core);
        }
        break;
    case PKT_NAME_SERVER_TICK_END:
        /* DRIVE_STAMP server_tick: a=world cycle. Every packet of the tick is
         * applied by now. */
        ScriptrunCore_Event(core, SCRIPTRUN_EV_SERVER_TICK, (int)core->world->cycle, 0, 0, 0);
        break;
    case PKT_NAME_VARP_RESET:
        memset(core->varps, 0, sizeof(core->varps));
        core_npcs_reresolve(core);
        break;
    case PKT_NAME_UPDATE_STAT:
        if( packet._update_stat.stat >= 0 && packet._update_stat.stat < SCRIPTRUN_STATS )
        {
            core->stat_level[packet._update_stat.stat] = packet._update_stat.level;
            core->stat_xp[packet._update_stat.stat] = packet._update_stat.xp;
        }
        break;
    case PKT_NAME_UPDATE_RUNENERGY:
        core->run_energy = packet._update_run_energy.run_energy_raw;
        break;
    case PKT_NAME_UPDATE_INV_FULL:
    {
        struct PktUpdateInvFull const* pkt = &packet._update_inv_full;
        struct ScriptrunInv* inv = core_inv(core, pkt->inv_id);
        for( int s = 0; s < SCRIPTRUN_INV_SLOTS; s++ )
        {
            inv->obj[s] = -1;
            inv->count[s] = 0;
        }
        inv->size = pkt->size < SCRIPTRUN_INV_SLOTS ? pkt->size : SCRIPTRUN_INV_SLOTS;
        for( int s = 0; s < inv->size; s++ )
        {
            /* Parsed ids are 0-based, -1 empty (osrs239_parse: g2 - 1), as
             * the client's InvManager stores them. */
            inv->obj[s] = pkt->obj_ids[s];
            inv->count[s] = inv->obj[s] >= 0 ? pkt->obj_counts[s] : 0;
        }
        ScriptrunCore_Event(core, SCRIPTRUN_EV_INV_CHANGED, pkt->inv_id, 0, 0, 0);
        break;
    }
    case PKT_NAME_UPDATE_INV_PARTIAL:
    {
        struct PktUpdateInvPartial const* pkt = &packet._update_inv_partial;
        struct ScriptrunInv* inv = core_inv(core, pkt->inv_id);
        for( int i = 0; i < pkt->count; i++ )
        {
            int s = pkt->entries[i].slot;
            if( s < 0 || s >= SCRIPTRUN_INV_SLOTS )
                continue;
            inv->obj[s] = pkt->entries[i].obj_id;
            inv->count[s] = inv->obj[s] >= 0 ? pkt->entries[i].count : 0;
            if( s + 1 > inv->size )
                inv->size = s + 1;
        }
        ScriptrunCore_Event(core, SCRIPTRUN_EV_INV_CHANGED, pkt->inv_id, 0, 0, 0);
        break;
    }
    case PKT_NAME_MESSAGE_GAME:
        core_message(core, packet._message_game.type, packet._message_game.name,
                     packet._message_game.text);
        ScriptrunCore_Event(core, SCRIPTRUN_EV_CHAT_MESSAGE, core->message_serial,
                            packet._message_game.type, 0, 0);
        break;
    case PKT_NAME_IF_OPENTOP:
        core->resume_pending = -1;
        core->toplevel = packet._if_opentop.interface_id;
        core->mount_count = 0;
        break;
    case PKT_NAME_IF_OPENSUB:
    {
        core->resume_pending = -1;
        int target = packet._if_opensub.target_uid;
        int i;
        for( i = 0; i < core->mount_count; i++ )
            if( core->mounts[i].target_uid == target )
                break;
        if( i == core->mount_count && core->mount_count < SCRIPTRUN_MOUNTS )
            core->mount_count++;
        if( i < core->mount_count )
        {
            core->mounts[i].target_uid = target;
            core->mounts[i].interface_id = packet._if_opensub.interface_id;
            core->mounts[i].type = packet._if_opensub.type;
        }
        /* The client raises sub_opened on the packet and sub_mounted once the
         * group is loaded; the bot's group is loaded the moment it is named. */
        ScriptrunCore_Event(core, SCRIPTRUN_EV_SUB_OPENED, target, packet._if_opensub.interface_id,
                            packet._if_opensub.type, 0);
        ScriptrunCore_Event(core, SCRIPTRUN_EV_SUB_MOUNTED, target, packet._if_opensub.interface_id,
                            packet._if_opensub.type, 0);
        break;
    }
    case PKT_NAME_IF_CLOSESUB:
        core->resume_pending = -1;
        for( int i = 0; i < core->mount_count; i++ )
        {
            if( core->mounts[i].target_uid == packet._if_closesub.target_uid )
            {
                ScriptrunCore_Event(core, SCRIPTRUN_EV_SUB_CLOSED, packet._if_closesub.target_uid,
                                    core->mounts[i].interface_id, 0, 0);
                core->mounts[i] = core->mounts[--core->mount_count];
                break;
            }
        }
        break;
    case PKT_NAME_IF_SETTEXT:
    {
        int comp = packet._if_settext.component_id;
        int i;
        for( i = 0; i < core->settext_count; i++ )
            if( core->settexts[i].component == comp )
                break;
        if( i == core->settext_count && core->settext_count < SCRIPTRUN_SETTEXTS )
            core->settext_count++;
        if( i < core->settext_count )
        {
            core->settexts[i].component = comp;
            snprintf(core->settexts[i].text, sizeof(core->settexts[i].text), "%s",
                     packet._if_settext.text ? packet._if_settext.text : "");
        }
        break;
    }
    case PKT_NAME_RUNCLIENTSCRIPT:
        if( packet._runclientscript )
        {
            struct PktRunClientScript const* rcs = packet._runclientscript;
            int strs = 0;
            memset(&core->last_script, 0, sizeof(core->last_script));
            core->last_script.script_id = rcs->script_id;
            core->last_script.argc = rcs->argc < SCRIPTRUN_SCRIPT_ARGS ? rcs->argc
                                                                        : SCRIPTRUN_SCRIPT_ARGS;
            for( int a = 0; a < core->last_script.argc; a++ )
            {
                core->last_script.int_args[a] = rcs->intv[a];
                if( (rcs->str_mask >> a) & 1u )
                {
                    if( strs < 4 )
                        snprintf(core->last_script.str_args[strs++], SCRIPTRUN_TEXT, "%s",
                                 rcs->strv[a]);
                }
            }
            core->last_script.serial = ++core->script_serial;
        }
        break;
    case PKT_NAME_LOGOUT:
        core->logged_out = 1;
        break;
    default:
        break;
    }
    gameproto_free(&packet);
    free(data);
}

/* ------------------------------------------------------------- readers */

int
ScriptrunCore_Varbit(
    struct ScriptrunCore const* core,
    int varbit_id,
    int* value)
{
    int basevar, startbit, endbit;
    uint32_t mask;
    int width;

    assert(core);
    assert(value);
    if( !ToriRSServer_VarbitRange(varbit_id, &basevar, &startbit, &endbit) )
        return 0;
    if( basevar < 0 || basevar >= SCRIPTRUN_VARPS )
        return 0;
    width = endbit - startbit + 1;
    mask = width >= 32 ? 0xffffffffu : ((1u << width) - 1u);
    *value = (int)(((uint32_t)core->varps[basevar] >> startbit) & mask);
    return 1;
}

struct ScriptrunInv const*
ScriptrunCore_Inv(
    struct ScriptrunCore const* core,
    int inv_id)
{
    assert(core);
    for( int i = 0; i < SCRIPTRUN_INVS; i++ )
        if( core->invs[i].inv_id == inv_id )
            return &core->invs[i];
    return NULL;
}

int
ScriptrunCore_MountedAt(
    struct ScriptrunCore const* core,
    int target_uid)
{
    assert(core);
    for( int i = 0; i < core->mount_count; i++ )
        if( core->mounts[i].target_uid == target_uid )
            return core->mounts[i].interface_id;
    return -1;
}

int
ScriptrunCore_InterfaceOpen(
    struct ScriptrunCore const* core,
    int interface_id)
{
    assert(core);
    if( core->toplevel == interface_id )
        return 1;
    for( int i = 0; i < core->mount_count; i++ )
        if( core->mounts[i].interface_id == interface_id )
            return 1;
    return 0;
}

char const*
ScriptrunCore_Text(
    struct ScriptrunCore const* core,
    int component)
{
    assert(core);
    for( int i = 0; i < core->settext_count; i++ )
        if( core->settexts[i].component == component )
            return core->settexts[i].text;
    return NULL;
}
