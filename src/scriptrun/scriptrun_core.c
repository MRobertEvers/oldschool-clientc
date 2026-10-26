#include "scriptrun_core.h"
#include "mem/torirs_mem.h"

#include "game/rs_chat.h" /* RS_CHAT_TYPE_PUBLIC: the client chatbox's type */
#include "game/rs_entity_sync.h"
#include "net/wordpack.h"
#include "net/rev/gameproto_parse.h"
#include "net/rev/gameproto_revisions.h"
#include "streams/rsprot_info.h"
#include "net/rev/packets/pkt_player_appearance.h"
#include "net/rev/revpacket.h"
#include "torirsserver/torirs_server.h"
#include "world/entity_npc.h"
#include "world/entity_player.h"
#include "world/world.h"
#include "net/rev/rsprot_bridge.h"
#include "net/rev/osrs241/packetin.h" /* the opcode names, for the wirelog's every-packet line */

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
    /* An op list's strings and blobs: the payload and a NUL per op at most. */
    CORE_ENTITY_BYTES_MAX = 65536 + CORE_ENTITY_OPS_MAX,
    CORE_LOCAL_SENTINEL = 2047,
};

static FILE*
core_wirelog_file(void);

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
    core = ToriMem_Calloc(ToriMem_ServerPlayers(), 1, sizeof(*core));
    assert(core);
    core->rev = rev;
    core->skillmulti_script = -1;
    core->menu_script = -1;
    core->world = World_New();
    assert(core->world);
    core->esync = ToriMem_Calloc(ToriMem_ServerPlayers(), 1, sizeof(*core->esync));
    assert(core->esync);
    RS_EntitySync_Init(core->esync);
    core->npc_ops =
        ToriMem_Calloc(ToriMem_ServerPlayers(), CORE_ENTITY_OPS_MAX, sizeof(struct RsprotInfoOp));
    assert(core->npc_ops);
    core->player_ops = ToriMem_Calloc(
        ToriMem_ServerPlayers(), CORE_ENTITY_OPS_MAX, sizeof(struct RsprotInfoOp));
    assert(core->player_ops);
    core->info_bytes = ToriMem_Alloc(ToriMem_ServerPlayers(), CORE_ENTITY_BYTES_MAX);
    assert(core->info_bytes);
    for( int i = 0; i < SCRIPTRUN_INVS; i++ )
        core->invs[i].inv_id = -1;
    core->toplevel = -1;
    core->resume_pending = -1;
    core->next_element_id = 1;
    if( rev->player_info_state_size )
    {
        core->player_decoder_state =
            ToriMem_Calloc(ToriMem_ServerPlayers(), 1, rev->player_info_state_size());
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
    ToriMem_Free(ToriMem_ServerPlayers(), core->locs);
    UIIfEventTable_Free(&core->if_events);
    RS_EntitySync_Free(core->esync);
    ToriMem_Free(ToriMem_ServerPlayers(), core->esync);
    World_Free(core->world);
    ToriMem_Free(ToriMem_ServerPlayers(), core->npc_ops);
    ToriMem_Free(ToriMem_ServerPlayers(), core->player_ops);
    ToriMem_Free(ToriMem_ServerPlayers(), core->info_bytes);
    ToriMem_Free(ToriMem_ServerPlayers(), core->player_decoder_state);
    ToriMem_Free(ToriMem_ServerPlayers(), core);
}

void
ScriptrunCore_SetLocalIndex(
    struct ScriptrunCore* core,
    int index)
{
    assert(core);
    assert(index >= 0);
    core->esync->local_pid = index;
    /* The live client mirrors this every frame (app_world_frame); the world's
     * local-player rules (the scene-edge teleport, the off-band reset) read it. */
    core->world->local_pid = index;
    if( core->player_decoder_state )
    {
        core->rev->player_info_set_local(index);
        core->rev->player_info_state_save(core->player_decoder_state);
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
    kept = 0;
    for( int i = 0; i < core->loc_anim_count; i++ )
    {
        struct ScriptrunLocAnim const* a = &core->loc_anims[i];
        if( core->tick - a->tick <= SCRIPTRUN_LOC_ANIM_TICKS )
            core->loc_anims[kept++] = *a;
    }
    core->loc_anim_count = kept;
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

/* WorldviewRegistry_HomeViewForAbsTile: the live view whose staging
 * rectangle holds an absolute tile (a rider on a deck), or 0. A view not yet
 * rebuilt has base 0,0 and claims nothing. */
static int
core_home_view(
    struct ScriptrunCore const* core,
    int abs_x,
    int abs_z)
{
    for( int id = 1; id < SCRIPTRUN_VIEWS; id++ )
    {
        struct ScriptrunView const* v = &core->views[id];

        if( !v->live || (v->base_x == 0 && v->base_z == 0) )
            continue;
        if( abs_x < v->base_x || abs_x >= v->base_x + v->size_x )
            continue;
        if( abs_z < v->base_z || abs_z >= v->base_z + v->size_z )
            continue;
        return id;
    }
    return 0;
}

/* The origin the SET_ACTIVE_WORLD cursor's zone headers are relative to: the
 * root scene's south-west corner, or the addressed deck's staging base
 * (torirs_server_encode.c ToriRSServer_SendZoneHeaderAt). */
static int
core_cursor_base_x(struct ScriptrunCore const* core)
{
    if( core->active_world > 0 )
        return core->views[core->active_world].base_x;
    return core_base_x(core);
}

static int
core_cursor_base_z(struct ScriptrunCore const* core)
{
    if( core->active_world > 0 )
        return core->views[core->active_world].base_z;
    return core_base_z(core);
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
ScriptrunCore_InstanceZone(
    struct ScriptrunCore const* core,
    int x,
    int z,
    int level,
    int* template_x,
    int* template_z,
    int* template_level,
    int* rotation)
{
    int dx;
    int dz;
    int32_t packed;

    assert(core);
    assert(template_x);
    assert(template_z);
    assert(template_level);
    assert(rotation);
    if( !core->have_scene || !core->instance_active || level < 0 || level > 3 )
        return 0;
    /* The grid is 13 zones a side with the rebuild's zone at index 6 (the
     * client's own indexing, app_plugin_instance_zone). */
    dx = (x >> 3) - (core->rebuild_zone_x - 6);
    dz = (z >> 3) - (core->rebuild_zone_z - 6);
    if( dx < 0 || dx >= 13 || dz < 0 || dz >= 13 )
        return 0;
    packed = core->instance_zones[level * 169 + dx * 13 + dz];
    if( packed == 0 )
        return 0;
    *rotation = (packed >> 1) & 3;
    *template_z = ((packed >> 3) & 0x7ff) * 8;
    *template_x = ((packed >> 14) & 0x3ff) * 8;
    *template_level = (packed >> 24) & 3;
    return 1;
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
    /* Aboard, the route is deck-local (player_apply_op's ABS_XZLEVEL): the
     * absolute tile is the deck's staging tile, as the server keeps it. */
    if( core->local_view > 0 && core->views[core->local_view].live )
    {
        *x = core->views[core->local_view].base_x + sx;
        *z = core->views[core->local_view].base_z + sz;
    }
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
    struct RsprotInfoOp const* op)
{
    struct ScriptrunCore* core = self->core;
    struct RS_EntitySync* esync = core->esync;

    switch( op->kind )
    {
    case RSPROT_OP_SET_LOCAL_PLAYER:
        self->cur_pid = core_local_pid(core);
        break;
    case RSPROT_OP_ADD_OLD:
        self->cur_pid =
            (int)op->value < self->old_count ? core->player_old_list[op->value] : -1;
        if( esync->active_player_count < RS_ENTITY_SYNC_MAX_PLAYERS )
            esync->active_players[esync->active_player_count++] = self->cur_pid;
        break;
    case RSPROT_OP_ADD_NEW:
        self->cur_pid = (int)op->value;
        if( esync->active_player_count < RS_ENTITY_SYNC_MAX_PLAYERS )
            esync->active_players[esync->active_player_count++] = self->cur_pid;
        break;
    case RSPROT_OP_SET_EXT:
        self->cur_pid = (int)op->value < esync->active_player_count
                            ? esync->active_players[op->value]
                            : -1;
        break;
    case RSPROT_OP_CLEAR_OLD:
    {
        int pid =
            (int)op->value < self->old_count ? core->player_old_list[op->value] : -1;
        if( pid >= 0 )
            RS_EntitySync_RemovePlayer(esync, core->world, pid);
        self->cur_pid = -1;
        return 1;
    }
    case RSPROT_OP_REMOVE_PID:
        RS_EntitySync_RemovePlayer(esync, core->world, (int)op->value);
        self->cur_pid = -1;
        return 1;
    case RSPROT_OP_COUNT_RESET:
    {
        int keep = (int)op->value;
        for( int i = keep; i < self->old_count; i++ )
            RS_EntitySync_RemovePlayer(esync, core->world, core->player_old_list[i]);
        esync->active_player_count = 0;
        return 1;
    }
    case RSPROT_OP_INFO:
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

/* A group's instance was replaced or closed: its IF_SETHIDE flags go with it
 * (the client reloads the cache's components on every open). */
static void
core_forget_hides(
    struct ScriptrunCore* core,
    int group)
{
    int kept = 0;
    for( int i = 0; i < core->sethide_count; i++ )
        if( ((core->sethides[i].component >> 16) & 0xffff) != group )
            core->sethides[kept++] = core->sethides[i];
    core->sethide_count = kept;
}

static void
core_message(
    struct ScriptrunCore* core,
    int type,
    char const* name,
    char const* text);

/* game/task_exec_entity_info.c player_apply_op, applied at once (no awaits). */
static void
player_apply_op(
    struct PlayerApply* self,
    struct RsprotInfoOp const* op)
{
    struct ScriptrunCore* core = self->core;
    struct World* world = core->world;
    int idx = player_target(self, NULL);

    if( idx < 0 )
        return;

    switch( op->kind )
    {
    case RSPROT_OP_WALKDIR:
        World_PlayerPathPushStep(world, idx, WORLD_PATHSTEP_WALK, (int)op->value);
        break;
    case RSPROT_OP_RUNDIR:
        World_PlayerPathPushStep(world, idx, WORLD_PATHSTEP_RUN, (int)op->value);
        break;
    case RSPROT_OP_LOCAL_XZLEVEL:
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(&world->entities.player, idx);
        World_PlayerPathJump(
            world, idx, op->xz_level.jump, op->xz_level.x, op->xz_level.z);
        if( player )
            player->grid_position.level = op->xz_level.level;
        break;
    }
    case RSPROT_OP_ABS_XZLEVEL:
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(&world->entities.player, idx);
        int next_x = op->xz_level.x - core_base_x(core);
        int next_z = op->xz_level.z - core_base_z(core);
        int step_type = WORLD_PATHSTEP_WALK;
        /* task_exec_entity_info.c: a coordinate inside a live view's staging
         * rectangle is a rider on a deck, rebased into the view's own space
         * (root-relative it overflows the uint8_t route queue). */
        int home_view =
            core_home_view(core, op->xz_level.x, op->xz_level.z);

        if( home_view > 0 )
        {
            next_x = op->xz_level.x - core->views[home_view].base_x;
            next_z = op->xz_level.z - core->views[home_view].base_z;
        }
        if( player )
            player->view_placement.home_view = home_view;
        if( self->cur_pid == core_local_pid(core) )
            core->local_view = home_view;

        /* class174: 2 run, 0 crawl, anything else the walk default. */
        if( op->xz_level.has_move_speed &&
            op->xz_level.move_speed == RSPROT_TRAVERSAL_RUN )
            step_type = WORLD_PATHSTEP_RUN;
        else if( op->xz_level.has_move_speed &&
                 op->xz_level.move_speed == RSPROT_TRAVERSAL_CRAWL )
            step_type = WORLD_PATHSTEP_CRAWL;
        World_PlayerPathJumpCollisionAware(
            world, idx, NULL, op->xz_level.jump, next_x, next_z, step_type);
        if( player )
            player->grid_position.level = op->xz_level.level;
        break;
    }
    case RSPROT_OP_DELTA_XZ:
    {
        int lx, lz, llevel;
        core_player_local_tile(core, &lx, &lz, &llevel);
        World_PlayerPathJump(
            world, idx, op->delta_xz.jump, lx + op->delta_xz.dx, lz + op->delta_xz.dz);
        break;
    }
    case RSPROT_OP_APPEARANCE:
    {
        /* The name is what a client draws over a player and what the drive's
         * players() reports; the body is not modelled here. */
        struct PktPlayerAppearance decoded;
        int ok;

        memset(&decoded, 0, sizeof(decoded));
        ok = core->rev->appearance_decode
                 ? core->rev->appearance_decode(
                       op->appearance.appearance, op->appearance.len, &decoded)
                 : PktPlayerAppearance_Decode(
                       &decoded, op->appearance.appearance, op->appearance.len);
        if( ok )
        {
            struct WorldEntity_Player* player =
                World_EntityPoolGet(&world->entities.player, idx);
            if( player )
            {
                strncpy(player->name, decoded.name, sizeof(player->name) - 1);
                player->name[sizeof(player->name) - 1] = '\0';
                /* The walk set the server chose (a held crate's carry, a
                 * weapon's stance): what the drive's players() reports. */
                player->idle_animations.readyanim = decoded.readyanim;
                player->idle_animations.walkanim = decoded.walkanim;
                player->idle_animations.runanim = decoded.runanim;
            }
        }
        break;
    }
    case RSPROT_OP_SEQUENCE:
        World_PlayerSetPrimaryAnimation(
            world, idx, op->sequence.sequence_id, op->sequence.delay);
        if( self->cur_pid == core_local_pid(core) && op->sequence.sequence_id >= 0 )
        {
            int k = core->own_seq_count < 16 ? core->own_seq_count++ : 15;
            if( k == 15 && core->own_seq_count == 16 )
            {
                memmove(core->own_seq, core->own_seq + 1, 15 * sizeof(int));
                memmove(core->own_seq_tick, core->own_seq_tick + 1, 15 * sizeof(int));
                memmove(core->own_seq_n, core->own_seq_n + 1, 15 * sizeof(int));
            }
            core->own_seq_starts++;
            core->own_seq[k] = op->sequence.sequence_id;
            core->own_seq_tick[k] = core->tick;
            core->own_seq_n[k] = core->own_seq_starts;
        }
        break;
    case RSPROT_OP_FACE_ENTITY:
    {
        int entity_id = op->face_entity.entity_id == 65535 ? -1 : op->face_entity.entity_id;
        if( self->cur_pid == core_local_pid(core) && core_wirelog_file() )
            fprintf(core_wirelog_file(), "%d\tface_entity\t%d\n", core->tick, entity_id);
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
            world,
            idx,
            entity_id,
            op->face_entity.has_fallback_angle ? op->face_entity.fallback_angle : -1,
            op->face_entity.has_fallback_angle && op->face_entity.instant);
        break;
    }
    case RSPROT_OP_FACE_COORD:
        if( self->cur_pid == core_local_pid(core) && core_wirelog_file() )
            fprintf(core_wirelog_file(), "%d\tface_coord\t%d\t%d\n", core->tick, op->face_coord.x >> 1,
                    op->face_coord.z >> 1);
        if( op->face_coord.modern )
            World_PlayerBeginModernFacing(world, idx, op->face_coord.movement_mode);
        World_PlayerFaceCoord(world, idx, op->face_coord.x, op->face_coord.z);
        break;
    case RSPROT_OP_SAY:
        if( op->say.text )
        {
            /* the client's arm: a chatbox row (public, the speaker's name)
             * and the overhead line */
            struct WorldEntity_Player* player = World_EntityPoolGet(&world->entities.player, idx);
            core_message(core, RS_CHAT_TYPE_PUBLIC, player && player->name[0] ? player->name : "Player",
                         op->say.text);
            ScriptrunCore_Event(core, SCRIPTRUN_EV_CHAT_MESSAGE, RS_CHAT_TYPE_PUBLIC, core->message_serial, 0, 0);
            World_PlayerSetChat(world, idx, op->say.text, 0, 0);
        }
        break;
    case RSPROT_OP_CHAT:
        /* Public chat (the player-info chat block, the sender's own included:
         * the server echoes a line only this way). The client's arm in
         * game/task_exec_entity_info.c, minus the UI: unpack, a chatbox row of
         * type RS_CHAT_TYPE_PUBLIC under the speaker's name, the overhead. */
        if( op->chat.data && op->chat.length > 0 )
        {
            struct RSCache_Buffer chatbuf;
            char* text;
            RSCache_BufferInit(&chatbuf, (uint8_t*)op->chat.data, op->chat.length);
            text = wordpack_unpack(&chatbuf, op->chat.length);
            if( text )
            {
                struct WorldEntity_Player* player = World_EntityPoolGet(&world->entities.player, idx);
                core_message(core, RS_CHAT_TYPE_PUBLIC, player && player->name[0] ? player->name : "Player", text);
                ScriptrunCore_Event(core, SCRIPTRUN_EV_CHAT_MESSAGE, RS_CHAT_TYPE_PUBLIC, core->message_serial, 0,
                                    0);
                World_PlayerSetChat(world, idx, text, op->chat.colour_effect >> 8, op->chat.colour_effect & 0xff);
                ToriMem_Free(ToriMem_Net(), text); /* wordpack_unpack's */
            }
        }
        break;
    case RSPROT_OP_DAMAGE:
    case RSPROT_OP_DAMAGE2:
        /* A soaked hit draws its soak as a second splat on the same delay; a
         * delay-only hit (type -1) draws nothing. */
        for( int part = 0; part < (op->damage.soaked ? 2 : 1); part++ )
        {
            int type = part ? op->damage.soak_type : op->damage.damage_type;
            if( type < 0 )
                continue;
            World_PlayerAddHitmarkTimed(
                world,
                idx,
                type,
                part ? op->damage.soak_damage : op->damage.damage,
                op->damage.health,
                op->damage.total_health,
                op->damage.delay,
                part ? 0 : op->damage.slots,
                WORLD_HITMARK_DEFAULT_DURATION,
                WORLD_HITMARK_POLICY_DISCARD);
        }
        break;
    case RSPROT_OP_HEADBAR:
        /* The entity draws one bar: a block's first. */
        if( op->headbar.index != 0 )
            break;
        if( op->headbar.remove )
            World_PlayerClearHealthbar(world, idx);
        else
            World_PlayerSetHealthbar(
                world,
                idx,
                core_headbar(
                    core, op->headbar.type, op->headbar.duration, op->headbar.start_delay,
                    op->headbar.start_fill, op->headbar.end_fill));
        break;
    case RSPROT_OP_SPOTANIM:
        World_PlayerSetSpotanim(
            world,
            idx,
            op->spotanim.slot,
            op->spotanim.spotanim_id,
            op->spotanim.height_delay >> 16,
            op->spotanim.height_delay & 0xffff,
            op->spotanim.loop);
        break;
    case RSPROT_OP_EXACT_MOVE:
    {
        int sx = op->exact_move.start_x;
        int sz = op->exact_move.start_z;
        int ex = op->exact_move.end_x;
        int ez = op->exact_move.end_z;

        if( op->exact_move.relative )
        {
            struct WorldEntity_Player* player =
                World_EntityPoolGet(&world->entities.player, idx);
            sx += player->pathing.route_x[0];
            sz += player->pathing.route_z[0];
            ex += player->pathing.route_x[0];
            ez += player->pathing.route_z[0];
        }
        /* Absolute tiles, and the delays in wire order: the 241 decoder files
         * the wire's delay1 as end_cycle_delta (osrs241_entity_info.c). */
        if( self->cur_pid == core_local_pid(core) && core_wirelog_file() )
            fprintf(core_wirelog_file(), "%d\texactmove\t%d,%d\t%d,%d\t%d\t%d\t%d\n", core->tick,
                    sx + world->_base_tile_x, sz + world->_base_tile_z, ex + world->_base_tile_x,
                    ez + world->_base_tile_z, op->exact_move.end_cycle_delta, op->exact_move.start_cycle_delta,
                    op->exact_move.facing);
        World_PlayerSetExactMoveDetailed(
            world,
            idx,
            sx,
            sz,
            ex,
            ez,
            op->exact_move.start_cycle_delta,
            op->exact_move.end_cycle_delta,
            op->exact_move.facing,
            op->exact_move.facing_is_yaw);
        break;
    }
    case RSPROT_OP_FACE_ANGLE:
        if( self->cur_pid == core_local_pid(core) && core_wirelog_file() )
            fprintf(core_wirelog_file(), "%d\tface_angle\t%d\t%d\n", core->tick, op->face_angle.angle,
                    op->face_angle.instant ? 1 : 0);
        if( op->face_angle.modern )
            World_PlayerBeginModernFacing(world, idx, op->face_angle.movement_mode);
        World_PlayerFaceAngle(world, idx, op->face_angle.angle, op->face_angle.instant);
        break;
    case RSPROT_OP_TRANSPARENCY:
        if( self->cur_pid == core_local_pid(core) && core_wirelog_file() )
            fprintf(core_wirelog_file(), "%d\ttransparency\t%d\t%d\t%d\t%d\t%d\n", core->tick,
                    (int16_t)op->transparency.start_cycle_delta, (int16_t)op->transparency.end_cycle_delta,
                    (uint8_t)op->transparency.from, (uint8_t)op->transparency.to, op->transparency.use_start ? 1 : 0);
        World_PlayerSetTransparency(
            world,
            idx,
            (int16_t)op->transparency.start_cycle_delta,
            (int16_t)op->transparency.end_cycle_delta,
            (uint8_t)op->transparency.from,
            (uint8_t)op->transparency.to,
            op->transparency.use_start);
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
    struct RsprotInfoOp* ops = core->player_ops;
    struct PlayerApply self;
    RsprotInfoList list;
    int op_count;

    assert(core->rev->player_info_read);
    rsprot_info_list_init(&list, ops, CORE_ENTITY_OPS_MAX, core->info_bytes, CORE_ENTITY_BYTES_MAX);
    op_count = core->rev->player_info_read(data, length, &list);

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
}

/* --------------------------------------------------------------- npc info */

struct NpcApply
{
    struct ScriptrunCore* core;
    int cur_slot;
    int old_count;
    int pending_base_type;
    /* The world this NPC_INFO describes (the SET_ACTIVE_WORLD cursor), and
     * that world's positional npc list -- the client's per-World lists. */
    int view;
    int* list;
    int* count;
    int cap;
};

/* The origin of this NPC_INFO's deltas: the root's (core_npc_origin), or for a
 * deck's own stream SET_NPC_UPDATE_ORIGIN as sent -- deck-local tiles -- on the
 * deck's plane (the rider's own when they ride it; decks are authored at 1). */
static void
npc_apply_origin(
    struct NpcApply* self,
    int* out_x,
    int* out_z,
    int* out_level)
{
    struct ScriptrunCore* core = self->core;
    int px, pz, plevel;

    if( self->view == 0 )
    {
        core_npc_origin(core, out_x, out_z, out_level);
        return;
    }
    core_player_local_tile(core, &px, &pz, &plevel);
    *out_x = core->npc_origin_x;
    *out_z = core->npc_origin_z;
    *out_level = core->local_view == self->view ? plevel : 1;
}

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

/* The npc's config as the client's cache carries it: name and combat level
 * off the RUNG this client's varps select, size off the shell the wire named
 * (torirs_types.h ToriRS_NpcEntityFacts: verzik_initial's rung states no
 * size, its shell says 5). Read through the server's table of the pack's
 * CLIENT records -- the same bytes the client codec reads. */
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
    {
        const struct ToriRSServerNpcInfo* src = shell ? shell : rung;
        *size = src && src->size > 0 ? src->size : 1;
    }
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
    npc_apply_origin(self, &tile_x, &tile_z, &level);
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
            npc->view_placement.home_view = self->view;
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
    struct RsprotInfoOp const* op)
{
    struct ScriptrunCore* core = self->core;
    struct RS_EntitySync* esync = core->esync;

    switch( op->kind )
    {
    case RSPROT_OP_ADD_OLD:
        self->cur_slot =
            (int)op->value < self->old_count ? core->npc_old_list[op->value] : -1;
        if( *self->count < self->cap )
            self->list[(*self->count)++] = self->cur_slot;
        break;
    case RSPROT_OP_ADD_NEW:
    {
        int stale_world_idx = -1;
        int stale_element_id = -1;
        self->cur_slot = (int)op->value;
        if( self->cur_slot >= 0 &&
            RS_EntitySync_FindNpc(esync, self->cur_slot, &stale_world_idx, &stale_element_id) )
            RS_EntitySync_RemoveNpc(esync, core->world, self->cur_slot);
        if( *self->count < self->cap )
            self->list[(*self->count)++] = self->cur_slot;
        break;
    }
    case RSPROT_OP_SET_EXT:
        self->cur_slot = (int)op->value < *self->count
                             ? self->list[op->value]
                             : -1;
        break;
    case RSPROT_OP_CLEAR_OLD:
    {
        int slot = (int)op->value < self->old_count ? core->npc_old_list[op->value] : -1;
        if( slot >= 0 )
            RS_EntitySync_RemoveNpc(esync, core->world, slot);
        self->cur_slot = -1;
        return 1;
    }
    case RSPROT_OP_COUNT_RESET:
    {
        int keep = (int)op->value;
        for( int i = keep; i < self->old_count; i++ )
            RS_EntitySync_RemoveNpc(esync, core->world, core->npc_old_list[i]);
        *self->count = 0;
        return 1;
    }
    case RSPROT_OP_INFO:
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
    struct RsprotInfoOp const* op)
{
    struct ScriptrunCore* core = self->core;
    struct World* world = core->world;
    int idx = npc_target(self, NULL);

    switch( op->kind )
    {
    case RSPROT_OP_NPCTYPE:
        self->pending_base_type = (int)op->value;
        if( self->cur_slot >= 0 && idx < 0 )
            npc_spawn_now(self);
        break;
    case RSPROT_OP_WALKDIR:
        if( idx >= 0 )
            World_NpcPathPushStep(world, idx, WORLD_PATHSTEP_WALK, (int)op->value);
        break;
    case RSPROT_OP_RUNDIR:
        if( idx >= 0 )
            World_NpcPathPushStep(world, idx, WORLD_PATHSTEP_RUN, (int)op->value);
        break;
    case RSPROT_OP_CRAWLDIR:
        if( idx >= 0 )
            World_NpcPathPushStep(world, idx, WORLD_PATHSTEP_CRAWL, (int)op->value);
        break;
    case RSPROT_OP_DELTA_XZ:
        if( idx >= 0 )
        {
            int lx, lz, llevel;
            npc_apply_origin(self, &lx, &lz, &llevel);
            /* TORIRS_SCRIPTRUN_NPC_MOVE_TRACE=1: every add record's placement
             * as the bot read it, with the jump bit (`[teleport]` is 0,
             * `[telejump]` and a fresh add 1). */
            if( getenv("TORIRS_SCRIPTRUN_NPC_MOVE_TRACE") )
                fprintf(stderr, "scriptrun_npc_move: slot=%d tile=%d,%d jump=%d\n", self->cur_slot,
                        lx + op->delta_xz.dx, lz + op->delta_xz.dz, op->delta_xz.jump ? 1 : 0);
            World_NpcPathJump(
                world, idx, op->delta_xz.jump, lx + op->delta_xz.dx, lz + op->delta_xz.dz);
        }
        break;
    case RSPROT_OP_SEQUENCE:
        if( idx >= 0 )
        {
            World_NpcSetPrimaryAnimation(
                world, idx, op->sequence.sequence_id, op->sequence.delay);
            /* DRIVE_STAMP npc_seq: a=slot b=seq c=tick, on the op's arrival. */
            ScriptrunCore_Event(core, SCRIPTRUN_EV_NPC_SEQ, self->cur_slot,
                                op->sequence.sequence_id, core->tick, 0);
        }
        break;
    case RSPROT_OP_FACE_ENTITY:
        if( idx >= 0 )
        {
            int entity_id = op->face_entity.entity_id == 65535 ? -1 : op->face_entity.entity_id;
            if( op->face_entity.modern )
                World_NpcBeginModernFacing(world, idx, op->face_entity.movement_mode);
            /* A world entity has no registry here: the gamepack's
             * missing-target path turns to the fallback angle. */
            if( op->face_entity.target_type == RSPROT_FACE_TARGET_WORLD_ENTITY &&
                op->face_entity.modern )
            {
                World_NpcFaceAngle(world, idx, op->face_entity.fallback_angle, op->face_entity.instant);
                break;
            }
            World_NpcFaceEntityDetailed(
                world,
                idx,
                entity_id,
                op->face_entity.has_fallback_angle ? op->face_entity.fallback_angle : -1,
                op->face_entity.has_fallback_angle && op->face_entity.instant);
        }
        break;
    case RSPROT_OP_FACE_COORD:
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
            npc->face_sent_x = op->face_coord.x;
            npc->face_sent_z = op->face_coord.z;
            npc->face_sent_cycle = (int)world->cycle;
            /* DRIVE_STAMP npc_face: a=slot b=x c=z d=tick (tiles, halved). */
            ScriptrunCore_Event(core, SCRIPTRUN_EV_NPC_FACE, self->cur_slot,
                                op->face_coord.x >> 1, op->face_coord.z >> 1, core->tick);
        }
        break;
    case RSPROT_OP_SAY:
        if( idx >= 0 && op->say.text )
            World_NpcSetChat(world, idx, op->say.text, 0, 0);
        break;
    case RSPROT_OP_DAMAGE:
        /* As the player's: a soak is a second splat, a type of -1 none. */
        for( int part = 0; idx >= 0 && part < (op->damage.soaked ? 2 : 1); part++ )
        {
            int type = part ? op->damage.soak_type : op->damage.damage_type;
            if( type < 0 )
                continue;
            World_NpcAddHitmarkTimed(
                world,
                idx,
                type,
                part ? op->damage.soak_damage : op->damage.damage,
                op->damage.health,
                op->damage.total_health,
                op->damage.delay,
                part ? 0 : op->damage.slots,
                WORLD_HITMARK_DEFAULT_DURATION,
                WORLD_HITMARK_POLICY_DISCARD);
        }
        break;
    case RSPROT_OP_HEADBAR:
        /* The npc draws one bar: a block's first. */
        if( idx >= 0 && op->headbar.index == 0 )
        {
            if( op->headbar.remove )
                World_NpcClearHealthbar(world, idx);
            else
                World_NpcSetHealthbar(
                    world,
                    idx,
                    core_headbar(
                        core, op->headbar.type, op->headbar.duration, op->headbar.start_delay,
                        op->headbar.start_fill, op->headbar.end_fill));
        }
        break;
    case RSPROT_OP_CHANGE_TYPE:
        self->pending_base_type = op->change_type.npc_type;
        if( idx >= 0 )
            npc_retype_now(self, idx);
        break;
    case RSPROT_OP_SPOTANIM:
        if( idx >= 0 )
            World_NpcSetSpotanim(
                world,
                idx,
                op->spotanim.slot,
                op->spotanim.spotanim_id,
                op->spotanim.height_delay >> 16,
                op->spotanim.height_delay & 0xffff,
                op->spotanim.loop);
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
                op->exact_move.end_cycle_delta, op->exact_move.facing,
                op->exact_move.facing_is_yaw);
        }
        break;
    case RSPROT_OP_FACE_ANGLE:
        if( idx >= 0 )
        {
            struct WorldEntity_NPC* npc = World_EntityPoolAt(&world->entities.npc, idx);
            int angle;
            if( op->face_angle.modern )
                World_NpcBeginModernFacing(world, idx, op->face_angle.movement_mode);
            angle = op->face_angle.spawn && npc->facing.turn_speed == 0 ? 0
                                                                         : op->face_angle.angle;
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
        }
        break;
    case RSPROT_OP_LEVEL_CHANGE:
        if( idx >= 0 )
            ((struct WorldEntity_NPC*)World_EntityPoolAt(&world->entities.npc, idx))->combat_level =
                (int)(uint32_t)op->value;
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
    case RSPROT_OP_BODY_CUSTOM:
        if( idx >= 0 )
            World_NpcSetBodyModels(
                world, idx, op->body.models, op->body.reset ? 0 : op->body.model_count);
        break;
    case RSPROT_OP_BAS_CHANGE:
        if( idx >= 0 )
        {
            struct WorldEntity_NPC* npc = World_EntityPoolAt(&world->entities.npc, idx);
            uint32_t mask = op->bas_change.mask;
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

static void
core_npc_info(
    struct ScriptrunCore* core,
    uint8_t const* data,
    int length)
{
    struct RsprotInfoOp* ops = core->npc_ops;
    struct NpcApply self;
    RsprotInfoList list;
    int op_count;

    assert(core->rev->npc_info_read);
    rsprot_info_list_init(&list, ops, CORE_ENTITY_OPS_MAX, core->info_bytes, CORE_ENTITY_BYTES_MAX);
    op_count = core->rev->npc_info_read(data, length, &list);

    memset(&self, 0, sizeof(self));
    self.core = core;
    self.view = core->active_world;
    if( self.view > 0 && !core->views[self.view].live )
        return;
    RS_EntitySync_NpcList(core->esync, self.view, &self.list, &self.count, &self.cap);
    self.old_count = *self.count;
    memcpy(core->npc_old_list, self.list, (size_t)self.old_count * sizeof(core->npc_old_list[0]));
    *self.count = 0;
    self.cur_slot = -1;
    self.pending_base_type = -1;

    for( int i = 0; i < op_count; i++ )
    {
        if( !npc_target_op(&self, &ops[i]) )
            npc_apply_op(&self, &ops[i]);
    }
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
    *abs_x = core_cursor_base_x(core) + core->zone_base_x + ((pos >> 4) & 7);
    *abs_z = core_cursor_base_z(core) + core->zone_base_z + (pos & 7);
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
        ScriptrunCore_LocChange(core, x, z, level, pkt->info >> 2, pkt->info & 3, pkt->loc_id, pkt);
        break;
    }
    case PKT_NAME_LOC_DEL:
    {
        struct PktLocDel const* pkt = payload;
        core_zone_tile(core, pkt->pos, &x, &z, &level);
        ScriptrunCore_LocChange(core, x, z, level, pkt->info >> 2, pkt->info & 3, -1, NULL);
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
    case PKT_NAME_LOC_ANIM:
    {
        struct PktLocAnim const* pkt = payload;
        struct ScriptrunLocAnim* a;
        core_zone_tile(core, pkt->pos, &x, &z, &level);
        /* the live ring asserts the same capacity (APP_DRIVE_LOC_ANIM_CAP) */
        assert(core->loc_anim_count < SCRIPTRUN_LOC_ANIMS);
        a = &core->loc_anims[core->loc_anim_count++];
        a->x = x;
        a->z = z;
        a->level = level;
        a->shape = pkt->info >> 2;
        a->seq = pkt->seq_id;
        a->tick = core->tick;
        break;
    }
    case PKT_NAME_SOUND_AREA:
    {
        /* Logged only: a scriptrun plays no sound, but the wire audit needs every
         * area sound the zone sent, out of range or not (RSProx hears sparring
         * gladiators 20 tiles off). */
        struct PktSoundArea const* pkt = payload;
        core_zone_tile(core, pkt->pos, &x, &z, &level);
        if( core_wirelog_file() )
            fprintf(core_wirelog_file(), "%d\tsound_area\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", core->tick, pkt->id,
                    pkt->loops, pkt->delay, pkt->radius, pkt->inner, x, z);
        break;
    }
    case PKT_NAME_MAP_ANIM:
    {
        struct PktMapAnim const* pkt = payload;
        if( pkt->id == 65535 )
            break;
        core_zone_tile(core, pkt->pos, &x, &z, &level);
        assert(core->map_anim_count < SCRIPTRUN_MAP_ANIMS);
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
        assert(core->projectile_count < SCRIPTRUN_PROJECTILES);
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
        if( core_wirelog_file() )
            fprintf(core_wirelog_file(), "%d\tprojectile\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
                    core->tick, pkt->spotanim, x, z, p->dst_x, p->dst_z, pkt->target, pkt->src_height,
                    pkt->dst_height, pkt->start_delay, pkt->end_delay, pkt->peak, pkt->arc);
        break;
    }
    case PKT_NAME_SCRIPTEDPROJ_ADD:
    {
        struct PktScriptedProjAdd const* pkt = payload;
        struct ScriptrunProjectile* p;
        assert(core->projectile_count < SCRIPTRUN_PROJECTILES);
        core_zone_tile(core, pkt->pos, &x, &z, &level);
        p = &core->projectiles[core->projectile_count++];
        p->spotanim = pkt->spotanim;
        p->src_x = x;
        p->src_z = z;
        p->dst_x = pkt->target_x;
        p->dst_z = pkt->target_z;
        p->level = pkt->target_level;
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

/* ------------------------------------------------------- world entities */

/* SET_ACTIVE_WORLD: wire data, so a dead or out-of-range view is refused
 * (the client logs and keeps its cursor), never asserted. */
static void
core_set_active_world(
    struct ScriptrunCore* core,
    struct PktSetActiveWorld const* pkt)
{
    if( pkt->world_id < 0 || pkt->world_id >= SCRIPTRUN_VIEWS ||
        (pkt->world_id > 0 && !core->views[pkt->world_id].live) )
    {
        core->wev_refused++;
        return;
    }
    core->active_world = pkt->world_id;
    core->active_world_level = pkt->level < 0 ? 0 : (pkt->level > 3 ? 3 : pkt->level);
    if( pkt->world_id > 0 )
        core->views[pkt->world_id].level = core->active_world_level;
}

static void
core_view_despawn(
    struct ScriptrunCore* core,
    int id)
{
    int kept = 0;

    assert(id > 0 && id < SCRIPTRUN_VIEWS);
    ScriptrunCore_DropViewLocs(core, id);
    RS_EntitySync_ClearViewNpcs(core->esync, core->world, id);
    memset(&core->views[id], 0, sizeof(core->views[id]));
    for( int i = 0; i < core->view_list_count; i++ )
        if( core->view_list[i] != id )
            core->view_list[kept++] = core->view_list[i];
    core->view_list_count = kept;
    if( core->local_view == id )
        core->local_view = 0;
}

/* WORLDENTITY_INFO, as exec_worldentity_info applies it: records address the
 * view list positionally, a short count despawns the tail, despawns are
 * collected before they are swept, then the spawn trailer. The transform kept
 * is the newest target (a move is a delta off the last one). */
static void
core_worldentity_info(
    struct ScriptrunCore* core,
    struct PktWorldEntityInfo const* pkt)
{
    int despawn[SCRIPTRUN_VIEWS];
    int despawn_count = 0;

    /* Only the root carries an entity list in this engine (no boat inside a
     * boat); one aimed at a deck, or longer than the list, is the server and
     * this client disagreeing -- refused like the client's count check. */
    if( core->active_world != 0 || pkt->count > core->view_list_count )
    {
        core->wev_refused++;
        return;
    }
    for( int i = pkt->count; i < core->view_list_count; i++ )
        despawn[despawn_count++] = core->view_list[i];
    for( int i = 0; i < pkt->count; i++ )
    {
        struct PktWevUpdate const* up = &pkt->updates[i];
        struct ScriptrunView* v = &core->views[core->view_list[i]];

        if( up->op == PKT_WEV_OP_DESPAWN )
        {
            despawn[despawn_count++] = core->view_list[i];
            continue;
        }
        if( up->op == PKT_WEV_OP_ENQUEUE || up->op == PKT_WEV_OP_SNAP )
        {
            v->fine_x += up->dx;
            v->fine_z += up->dz;
            v->angle = (v->angle + up->dangle) & 0x7ff;
        }
        if( up->has_op_mask )
            v->op_mask = up->op_mask;
    }
    for( int i = 0; i < despawn_count; i++ )
        core_view_despawn(core, despawn[i]);
    for( int i = 0; i < pkt->spawn_count; i++ )
    {
        struct PktWevSpawn const* sp = &pkt->spawns[i];
        struct ScriptrunView* v;

        if( sp->id <= 0 || sp->id >= SCRIPTRUN_VIEWS || sp->size_x_tiles <= 0 ||
            sp->size_z_tiles <= 0 || core->view_list_count >= SCRIPTRUN_VIEWS - 1 )
        {
            core->wev_refused++;
            continue;
        }
        if( core->views[sp->id].live )
            core_view_despawn(core, sp->id);
        v = &core->views[sp->id];
        v->live = 1;
        v->config_id = sp->config_id;
        v->size_x = sp->size_x_tiles;
        v->size_z = sp->size_z_tiles;
        v->priority_group = sp->priority_group;
        v->fine_x = sp->x;
        v->fine_z = sp->z;
        v->angle = sp->angle & 0x7ff;
        v->op_mask = sp->has_op_mask ? sp->op_mask : PKT_WEV_OP_MASK_ALL;
        core->view_list[core->view_list_count++] = sp->id;
    }
}

/* REBUILD_WORLDENTITY: the cursor's deck map. Its base is the staging square
 * every deck tile, rider and deck zone header is relative to; its grid's
 * extent is the view's spawn-time size (PktRebuildWev_DecodeZones). */
static void
core_rebuild_worldentity(
    struct ScriptrunCore* core,
    struct PktRebuildWev const* pkt)
{
    int32_t* zones;
    struct ScriptrunView* v;
    int id = core->active_world;

    if( id <= 0 || !core->views[id].live )
    {
        core->wev_refused++;
        return;
    }
    v = &core->views[id];
    zones = ToriMem_Calloc(ToriMem_ServerPlayers(), PKT_MAP_REBUILD_ZONES, sizeof(*zones));
    assert(zones);
    if( !PktRebuildWev_DecodeZones(pkt, v->size_x / 8, v->size_z / 8, zones) )
    {
        core->wev_refused++;
        ToriMem_Free(ToriMem_ServerPlayers(), zones);
        return;
    }
    ScriptrunCore_DropViewLocs(core, id);
    v->base_x = pkt->base_x;
    v->base_z = pkt->base_z;
    ScriptrunCore_LoadViewLocs(core, id, zones, v->size_x / 8, v->size_z / 8);
    ToriMem_Free(ToriMem_ServerPlayers(), zones);
}

static void
core_rebuild(
    struct ScriptrunCore* core,
    int zone_x,
    int zone_z)
{
    if( core->have_scene && (zone_x != core->rebuild_zone_x || zone_z != core->rebuild_zone_z) )
    {
        /* App_WorldRebuildShift: entities keep their world tile. The delta is
         * the NEW base minus the old, as task_gameproto_exec.c passes it;
         * World_ShiftEntities subtracts it. The reverse sign moved every kept
         * entity twice the rebuild's distance the wrong way: the Maiden, kept
         * across the walkway entry's 6424 -> 6384 re-centre, landed 40 tiles
         * west of the scene and her room's bots never saw her again. */
        int dx = (zone_x - core->rebuild_zone_x) * 8;
        int dz = (zone_z - core->rebuild_zone_z) * 8;
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

/* TORIRS_SCRIPTRUN_WIRELOG=<file>: one line per decoded packet of the kinds a
 * content audit diffs against an RSProx transcript and the tick log does not
 * carry (interfaces, clientscripts, messages, vars, camera, minimap, synths,
 * music), as `<tick>\t<kind>\t<fields>`. Component ids print as group:child,
 * vars by number: tools/rsprox/wirelog_tells.py names them. */
static FILE*
core_wirelog_file(void)
{
    static int opened = 0;
    static FILE* f = NULL;

    if( !opened )
    {
        char const* path = getenv("TORIRS_SCRIPTRUN_WIRELOG");
        opened = 1;
        if( path && *path )
        {
            f = fopen(path, "w");
            assert(f);
        }
    }
    return f;
}

/* RSProt's name for a decoded packet kind, for the wirelog. */
static char const*
core_pkt_name(int pkt_name)
{
    for( size_t i = 0; i < sizeof(g_packet_in_definitions_osrs241) / sizeof(g_packet_in_definitions_osrs241[0]);
         i++ )
        if( g_packet_in_definitions_osrs241[i].name == pkt_name )
            return g_packet_in_definitions_osrs241[i].prot;
    return "?";
}

/* Every packet the bot receives, named, whether or not it is decoded or acted
 * on: a wire audit that compares only the kinds someone thought to log cannot
 * see a kind one side sends and the other never does. */
static void
core_wirelog_every(
    struct ScriptrunCore* core,
    int pkt_name,
    int len)
{
    FILE* f = core_wirelog_file();

    if( f )
        fprintf(f, "%d\tpkt\t%s\t%d\n", core->tick, core_pkt_name(pkt_name), len);
}

static void
core_wirelog(
    struct ScriptrunCore* core,
    int pkt_name,
    struct RevPacket const* p)
{
    FILE* f = core_wirelog_file();
    int t = core->tick;

    if( !f )
        return;
#define COMP(uid) ((unsigned)(uid) >> 16), ((unsigned)(uid)&0xffffu)
    switch( pkt_name )
    {
    case PKT_NAME_IF_OPENSUB:
        fprintf(f, "%d\tif_opensub\t%u:%u\t%d\t%d\n", t, COMP(p->_if_opensub.target_uid),
                p->_if_opensub.interface_id, p->_if_opensub.type);
        break;
    case PKT_NAME_IF_CLOSESUB:
        fprintf(f, "%d\tif_closesub\t%u:%u\n", t, COMP(p->_if_closesub.target_uid));
        break;
    case PKT_NAME_IF_SETTEXT:
        fprintf(f, "%d\tif_settext\t%u:%u\t%s\n", t, COMP(p->_if_settext.component_id),
                p->_if_settext.text ? p->_if_settext.text : "");
        break;
    case PKT_NAME_IF_SETEVENTS:
        fprintf(f, "%d\tif_setevents\t%u:%u\t%d\t%d\t%d\n", t, COMP(p->_if_setevents.component_id),
                p->_if_setevents.from, p->_if_setevents.to, p->_if_setevents.events);
        break;
    case PKT_NAME_IF_SETHIDE:
        fprintf(f, "%d\tif_sethide\t%u:%u\t%d\n", t, COMP(p->_if_sethide.component_id), p->_if_sethide.hide);
        break;
    case PKT_NAME_IF_SETCOLOUR:
        fprintf(f, "%d\tif_setcolour\t%u:%u\t%d\n", t, COMP(p->_if_setcolour.component_id),
                p->_if_setcolour.colour);
        break;
    case PKT_NAME_IF_SETOBJECT:
        fprintf(f, "%d\tif_setobject\t%u:%u\t%d\t%d\n", t, COMP(p->_if_setobject.component_id),
                p->_if_setobject.obj_id, p->_if_setobject.zoom);
        break;
    case PKT_NAME_IF_SETMODEL:
        fprintf(f, "%d\tif_setmodel\t%u:%u\t%d\n", t, COMP(p->_if_setmodel.component_id),
                p->_if_setmodel.model_id);
        break;
    case PKT_NAME_IF_SETPOSITION:
        fprintf(f, "%d\tif_setposition\t%u:%u\t%d\t%d\n", t, COMP(p->_if_setposition.component_id),
                p->_if_setposition.x, p->_if_setposition.z);
        break;
    case PKT_NAME_UPDATE_STAT:
        fprintf(f, "%d\tupdate_stat\t%d\t%d\t%d\n", t, p->_update_stat.stat, p->_update_stat.level,
                p->_update_stat.xp);
        break;
    case PKT_NAME_HINT_ARROW:
        fprintf(f, "%d\thint_arrow\t%d\t%d\t%d\t%d\n", t, p->_hint_arrow.type, p->_hint_arrow.id,
                p->_hint_arrow.z, p->_hint_arrow.height);
        break;
    case PKT_NAME_MIDI_JINGLE:
        fprintf(f, "%d\tmidi_jingle\t%d\t%d\n", t, p->_midi_jingle.id, p->_midi_jingle.delay);
        break;
    case PKT_NAME_UPDATE_INV_FULL:
        fprintf(f, "%d\tupdate_inv_full\t%u:%u\t%d\t%d", t, COMP(p->_update_inv_full.component_id),
                p->_update_inv_full.inv_id, p->_update_inv_full.size);
        for( int i = 0; i < p->_update_inv_full.size && p->_update_inv_full.obj_ids; i++ )
            if( p->_update_inv_full.obj_ids[i] > 0 )
                fprintf(f, "\t%d:%dx%d", i, p->_update_inv_full.obj_ids[i] - 1,
                        p->_update_inv_full.obj_counts ? p->_update_inv_full.obj_counts[i] : 0);
        fputc('\n', f);
        break;
    case PKT_NAME_UPDATE_INV_PARTIAL:
        fprintf(f, "%d\tupdate_inv_partial\t%u:%u\t%d", t, COMP(p->_update_inv_partial.component_id),
                p->_update_inv_partial.inv_id);
        for( int i = 0; i < p->_update_inv_partial.count && p->_update_inv_partial.entries; i++ )
            fprintf(f, "\t%d:%dx%d", p->_update_inv_partial.entries[i].slot,
                    p->_update_inv_partial.entries[i].obj_id - 1, p->_update_inv_partial.entries[i].count);
        fputc('\n', f);
        break;
    case PKT_NAME_IF_SETANIM:
        fprintf(f, "%d\tif_setanim\t%u:%u\t%d\n", t, COMP(p->_if_setanim.component_id), p->_if_setanim.anim_id);
        break;
    case PKT_NAME_IF_SETNPCHEAD:
        fprintf(f, "%d\tif_setnpchead\t%u:%u\t%d\n", t, COMP(p->_if_setnpchead.component_id),
                p->_if_setnpchead.npc_id);
        break;
    case PKT_NAME_IF_SETPLAYERHEAD:
        fprintf(f, "%d\tif_setplayerhead\t%u:%u\n", t, COMP(p->_if_setplayerhead.component_id));
        break;
    case PKT_NAME_RUNCLIENTSCRIPT:
        if( p->_runclientscript )
        {
            struct PktRunClientScript const* rcs = p->_runclientscript;
            fprintf(f, "%d\trunclientscript\t%d\t", t, rcs->script_id);
            for( int a = 0; a < rcs->argc && a < PKT_RUNCLIENTSCRIPT_ARG_MAX; a++ )
            {
                if( (rcs->str_mask >> a) & 1u )
                    fprintf(f, "%s\"%s\"", a ? "," : "", pkt_runclientscript_str(rcs, a));
                else
                    fprintf(f, "%s%d", a ? "," : "", rcs->intv[a]);
            }
            fputc('\n', f);
        }
        break;
    case PKT_NAME_MESSAGE_GAME:
        fprintf(f, "%d\tmessage_game\t%d\t%s\n", t, p->_message_game.type,
                p->_message_game.text ? p->_message_game.text : "");
        break;
    case PKT_NAME_VARP_SMALL:
        fprintf(f, "%d\tvarp\t%d\t%d\n", t, p->_varp_small.variable, p->_varp_small.value);
        break;
    case PKT_NAME_VARP_LARGE:
        fprintf(f, "%d\tvarp\t%d\t%d\n", t, p->_varp_large.variable, p->_varp_large.value);
        break;
    case PKT_NAME_VARP_LONG:
        fprintf(f, "%d\tvarp_long\t%d\t%lld\n", t, p->_varp_long.variable,
                (long long)p->_varp_long.value);
        break;
    case PKT_NAME_MINIMAP_TOGGLE:
        fprintf(f, "%d\tminimap_toggle\t%d\n", t, p->_minimap_toggle.state);
        break;
    case PKT_NAME_CAM_UNLOCK:
        fprintf(f, "%d\tcam_unlock\t%d\n", t, p->_cam_unlock.unlock);
        break;
    case PKT_NAME_HIDENPCOPS:
    case PKT_NAME_HIDELOCOPS:
    case PKT_NAME_HIDEOBJOPS:
        fprintf(f, "%d\t%s\t%d\n", t,
                pkt_name == PKT_NAME_HIDENPCOPS   ? "hidenpcops"
                : pkt_name == PKT_NAME_HIDELOCOPS ? "hidelocops"
                                                  : "hideobjops",
                p->_hide_ops.hidden);
        break;
    case PKT_NAME_SYNTH_SOUND:
        fprintf(f, "%d\tsynth_sound\t%d\t%d\t%d\n", t, p->_synth_sound.id, p->_synth_sound.loops,
                p->_synth_sound.delay);
        break;
    case PKT_NAME_MIDI_SONG:
        fprintf(f, "%d\tmidi_song\t%d\n", t, p->_midi_song.id);
        break;
    case PKT_NAME_MIDI_SONG_WITHSECONDARY:
        fprintf(f, "%d\tmidi_song_withsecondary\t%d\t%d\n", t, p->_midi_song_with_secondary.primary_id,
                p->_midi_song_with_secondary.secondary_id);
        break;
    case PKT_NAME_CAM_RESET:
        fprintf(f, "%d\tcam_reset\n", t);
        break;
    case PKT_NAME_CAM_SHAKE:
        fprintf(f, "%d\tcam_shake\t%d\t%d\t%d\t%d\n", t, p->_cam_shake.axis, p->_cam_shake.amplitude,
                p->_cam_shake.frequency, p->_cam_shake.speed);
        break;
    case PKT_NAME_CAM_MOVETO:
        fprintf(f, "%d\tcam_moveto\t%d\t%d\t%d\t%d\t%d\t%d\n", t, p->_cam_moveto.local_x, p->_cam_moveto.local_z,
                p->_cam_moveto.height, p->_cam_moveto.rate, p->_cam_moveto.rate2, p->_cam_moveto.absolute);
        break;
    case PKT_NAME_CAM_LOOKAT:
        fprintf(f, "%d\tcam_lookat\t%d\t%d\t%d\t%d\t%d\t%d\n", t, p->_cam_lookat.local_x, p->_cam_lookat.local_z,
                p->_cam_lookat.height, p->_cam_lookat.rate, p->_cam_lookat.rate2, p->_cam_lookat.absolute);
        break;
    case PKT_NAME_CAM_MOVETO_CYCLES:
        fprintf(f, "%d\tcam_moveto_cycles\t%d\t%d\t%d\t%d\t%d\t%d\n", t, p->_cam_moveto_cycles.x,
                p->_cam_moveto_cycles.z, p->_cam_moveto_cycles.height, p->_cam_moveto_cycles.cycles,
                p->_cam_moveto_cycles.ignore_terrain, p->_cam_moveto_cycles.easing);
        break;
    case PKT_NAME_CAM_ROTATETO_COORDINATE:
        fprintf(f, "%d\tcam_rotateto_coordinate\t%d\t%d\t%d\t%d\t%d\n", t, p->_cam_rotateto_coordinate.x,
                p->_cam_rotateto_coordinate.z, p->_cam_rotateto_coordinate.height,
                p->_cam_rotateto_coordinate.cycles, p->_cam_rotateto_coordinate.easing);
        break;
    default:
        break;
    }
#undef COMP
    fflush(f);
}

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
        core->rev->player_info_state_load(core->player_decoder_state);
    core_packet(core, pkt_name, payload, len);
    if( core->player_decoder_state )
        core->rev->player_info_state_save(core->player_decoder_state);
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

    core_wirelog_every(core, pkt_name, len);
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
    data = ToriMem_Alloc(ToriMem_ServerPlayers(), (size_t)(len > 0 ? len : 1));
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
        ToriMem_Free(ToriMem_ServerPlayers(), data);
        return;
    }
    core_wirelog(core, pkt_name, &packet);

    switch( pkt_name )
    {
    case PKT_NAME_REBUILD_NORMAL:
        core_rebuild(core, packet._map_rebuild.zonex, packet._map_rebuild.zonez);
        core->instance_active = 0;
        ScriptrunCore_BuildCollision(core);
        break;
    case PKT_NAME_REBUILD_REGION:
        /* An instance: the scene is copied zone by zone from template zones
         * (WorldBuilder_RebuildInstance), collision with it. */
        core_rebuild(core, packet._map_rebuild.zonex, packet._map_rebuild.zonez);
        assert(packet._map_rebuild.zones);
        memcpy(core->instance_zones, packet._map_rebuild.zones, sizeof(core->instance_zones));
        core->instance_active = 1;
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
        x0 = core_cursor_base_x(core) + core->zone_base_x;
        z0 = core_cursor_base_z(core) + core->zone_base_z;
        level = core->zone_level;
        /* ...and resets its locs to the map's, as the client's
         * zone_full_reset_locs does: the state behind this header names only
         * what still differs from the map, so a change the server has since
         * reverted would otherwise stand here and not on the client. */
        ScriptrunCore_ZoneResetLocs(core, x0, z0, level);
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
    case PKT_NAME_MAP_ANIM_SPECIFIC:
    {
        /* MAP_ANIM for this player alone: a build-area tile, no zone header,
         * on the player's own plane -- recorded as the map anim it plays as. */
        struct PktMapAnimSpecific const* pkt = &packet._map_anim_specific;
        int px, pz, plevel;

        if( pkt->id == 65535 )
            break;
        if( !ScriptrunCore_LocalTile(core, &px, &pz, &plevel) )
            plevel = 0;
        assert(core->map_anim_count < SCRIPTRUN_MAP_ANIMS);
        {
            struct ScriptrunMapAnim* a = &core->map_anims[core->map_anim_count++];
            a->spotanim = pkt->id;
            a->x = core_cursor_base_x(core) + pkt->x;
            a->z = core_cursor_base_z(core) + pkt->z;
            a->level = plevel;
            a->height = pkt->height;
            a->delay = pkt->delay;
            a->cycle = (int)core->world->cycle;
        }
        break;
    }
    case PKT_NAME_LOC_ANIM:
        core_zone_sub(core, pkt_name, &packet._loc_anim);
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
    case PKT_NAME_SET_ACTIVE_WORLD:
        core_set_active_world(core, &packet._set_active_world);
        break;
    case PKT_NAME_WORLDENTITY_INFO:
        core_worldentity_info(core, &packet._worldentity_info);
        break;
    case PKT_NAME_REBUILD_WORLDENTITY:
        core_rebuild_worldentity(core, &packet._rebuild_wev);
        break;
    case PKT_NAME_SERVER_TICK_END:
        /* SET_ACTIVE_WORLD lasts until the fence (rs_gameproto_exec.c). */
        core->active_world = 0;
        core->active_world_level = 0;
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
        /* a = type, b = serial: the client's DRIVE_STAMP (rs_chat.c
         * RS_Chat_AddMessage), which QD.player._line_by_serial reads. */
        ScriptrunCore_Event(core, SCRIPTRUN_EV_CHAT_MESSAGE, packet._message_game.type,
                            core->message_serial, 0, 0);
        break;
    case PKT_NAME_IF_OPENTOP:
        core->resume_pending = -1;
        core->toplevel = packet._if_opentop.interface_id;
        core->sethide_count = 0;
        core->mount_count = 0;
        UIIfEventTable_Clear(&core->if_events);
        break;
    case PKT_NAME_IF_OPENSUB:
    {
        core->resume_pending = -1;
        int target = packet._if_opensub.target_uid;
        int i;
        for( i = 0; i < core->mount_count; i++ )
            if( core->mounts[i].target_uid == target )
                break;
        /* The group leaving the slot loses its armed events, even when the
         * same group goes straight back in (UIIfEventTable_DropGroup). */
        if( i < core->mount_count )
            UIIfEventTable_DropGroup(&core->if_events, core->mounts[i].interface_id);
        if( i == core->mount_count && core->mount_count < SCRIPTRUN_MOUNTS )
            core->mount_count++;
        core_forget_hides(core, packet._if_opensub.interface_id);
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
                core_forget_hides(core, core->mounts[i].interface_id);
                UIIfEventTable_DropGroup(&core->if_events, core->mounts[i].interface_id);
                core->mounts[i] = core->mounts[--core->mount_count];
                break;
            }
        }
        break;
    case PKT_NAME_IF_SETEVENTS:
        UIIfEventTable_Set(&core->if_events, packet._if_setevents.component_id,
                           packet._if_setevents.from, packet._if_setevents.to,
                           packet._if_setevents.events);
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
    case PKT_NAME_IF_SETHIDE:
    {
        int comp = packet._if_sethide.component_id;
        int i;
        for( i = 0; i < core->sethide_count; i++ )
            if( core->sethides[i].component == comp )
                break;
        if( i == core->sethide_count )
        {
            assert(core->sethide_count < SCRIPTRUN_SETHIDES);
            core->sethide_count++;
        }
        core->sethides[i].component = comp;
        core->sethides[i].hidden = packet._if_sethide.hide ? 1 : 0;
        break;
    }
    case PKT_NAME_RUNCLIENTSCRIPT:
        if( packet._runclientscript )
        {
            struct PktRunClientScript const* rcs = packet._runclientscript;
            int strs = 0;
            if( core->menu_script < 0 || rcs->script_id == core->menu_script )
            {
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
                                     pkt_runclientscript_str(rcs, a));
                    }
                }
                core->last_script.serial = ++core->script_serial;
            }
            if( rcs->script_id == core->skillmulti_script && rcs->argc > 20 )
            {
                core->skillmulti_mode = rcs->intv[0];
                core->skillmulti_max = rcs->intv[1];
                core->skillmulti_default = rcs->intv[20];
                core->skillmulti_valid = 1;
            }
        }
        break;
    case PKT_NAME_LOGOUT:
        core->logged_out = 1;
        break;
    default:
        break;
    }
    gameproto_free(&packet);
    ToriMem_Free(ToriMem_ServerPlayers(), data);
}

/* ------------------------------------------------------------- readers */

int
ScriptrunCore_IfEvents(
    struct ScriptrunCore const* core,
    int com_id,
    int sub_id,
    unsigned* out_events)
{
    assert(core);
    assert(out_events);
    return UIIfEventTable_Lookup(&core->if_events, com_id, sub_id, out_events);
}

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

int
ScriptrunCore_SetHide(
    struct ScriptrunCore const* core,
    int component)
{
    assert(core);
    for( int i = 0; i < core->sethide_count; i++ )
        if( core->sethides[i].component == component )
            return core->sethides[i].hidden;
    return -1;
}
